/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      seller.cpp
 * Desc:      Seller-side packet generation and unlock execution.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/
#include "void_protocol.h"
#include "seller.h"
#include "void_config.h"          // VOID-128: SELLER_APID / BUYER_APID / BUYER_SAT_ID
#include "packet_d_builder.h"     // VOID-136: pure PacketD emit
#include "heartbeat_tx.h"         // VOID-022: 30 s heartbeat telemetry

#include <cstddef>
#include <cstdint>
#include <cstring>

static PacketA_t invoice;

// --- RX ISR flag (DIO1 packet-received interrupt) ---
// Replaces the earlier readData(NULL, 0) poll that crashed the SX126x
// SPI path with a NULL-pointer StoreProhibited. Canonical RadioLib
// pattern: ISR sets the flag, loop drains it with a sized read.
static volatile bool rx_flag = false;
static void IRAM_ATTR onRxDone() { rx_flag = true; }

// Helper to extract the 11-bit CCSDS APID safely.
// VOID-139: must be tier-aware. Under SNLP (VOID_PROTOCOL_TYPE == 2) the
// 14-byte header prepends a 4-byte sync word, so the CCSDS ID field — and
// thus the APID — lives at buf[4]/buf[5], NOT buf[0]/buf[1]. This mirrors
// buyer.cpp::extractAPID. Reading the CCSDS offsets on an SNLP frame yielded
// ((0x1D & 0x07) << 8) | 0x01 = 1281, so an inbound PacketC never matched
// SELLER_APID (100) and the seller never emitted PacketD — the long-missing
// D-leg. (Protocol-spec-SNLP.md §1.1 sync word + §2 header layout.)
static uint16_t getAPID(const uint8_t* buf) {
#if VOID_PROTOCOL_TYPE == 2
    return static_cast<uint16_t>(((buf[4] & 0x07u) << 8) | buf[5]);
#else
    return static_cast<uint16_t>(((buf[0] & 0x07u) << 8) | buf[1]);
#endif
}

// Pack a VOID header (BE) for a non-command telemetry packet into the
// supplied byte buffer. `body_len` is the full frame size minus
// SIZE_VOID_HEADER. The CCSDS packet_len field is encoded as
// (body_len - 1) to match gateway/test/utils/generate_packets.go::
// buildHeader — the authority for the golden vector bytes.
// Mirrors buyer.cpp so seller TX stays byte-compatible with the buyer
// parser and the checked-in golden vectors.
static void packVoidHeader(uint8_t* hdr, uint16_t apid, uint16_t body_len) {
#if VOID_PROTOCOL_TYPE == 2
    // Sync word 0x1D01A5A5 (BE32)
    hdr[0] = 0x1Du; hdr[1] = 0x01u; hdr[2] = 0xA5u; hdr[3] = 0xA5u;
    uint8_t* const ccsds = hdr + 4;
#else
    uint8_t* const ccsds = hdr;
#endif

    // CCSDS ID field: version=0 | type=0 (telemetry) | sec=1 | apid(11)
    const uint16_t id = static_cast<uint16_t>(0x0800u | (apid & 0x07FFu));
    ccsds[0] = static_cast<uint8_t>((id >> 8) & 0xFFu);
    ccsds[1] = static_cast<uint8_t>(id & 0xFFu);

    // Sequence: flags=11 (unsegmented continuous), count=0
    ccsds[2] = 0xC0u;
    ccsds[3] = 0x00u;

    // packet_len = body_len - 1 (BE16)
    const uint16_t plen = static_cast<uint16_t>(body_len - 1u);
    ccsds[4] = static_cast<uint8_t>((plen >> 8) & 0xFFu);
    ccsds[5] = static_cast<uint8_t>(plen & 0xFFu);

#if VOID_PROTOCOL_TYPE == 2
    // SNLP 4-byte align_pad (zeroed)
    hdr[10] = 0x00u;
    hdr[11] = 0x00u;
    hdr[12] = 0x00u;
    hdr[13] = 0x00u;
#endif
}

#if VOID_PROTOCOL_TYPE == 2
// VOID-139 LBT: max CAD attempts before a mandatory frame (PacketD) is
// sent best-effort.
static constexpr uint8_t kLbtMaxAttempts = 6;

// VOID-136: handle an incoming PacketC (receipt) from the ground
// bouncer egress path (VOID-138). On CRC-valid receipt, emit PacketD
// (delivery) back toward Sat B.
//
// Scope guard: SNLP only. The journey-to-HAB plaintext alpha runs SNLP
// exclusively; a CCSDS variant would need its own packet_d_builder.
//
// CRC scope matches the Go generator (generate_packets.go::genPacketC):
// 14-byte SNLP header + 90-byte body-up-to-CRC. CRC field at [104..107],
// tail pad at [108..111].
// Returns true iff a PacketD (delivery) was actually transmitted — i.e. the
// transaction completed. The caller uses this to re-open advertising.
static bool handlePacketCReceipt(const uint8_t* buf, size_t len) {
    if (len != SIZE_PACKET_C) return false;

    // Read the advertised CRC (little-endian) and recompute over the
    // first 104 bytes. Using Void.calculateCRC to stay consistent with
    // the rest of the firmware's CRC pipeline (IEEE-802.3 CRC32).
    const uint32_t advertised =
          static_cast<uint32_t>(buf[104])
        | (static_cast<uint32_t>(buf[105]) <<  8)
        | (static_cast<uint32_t>(buf[106]) << 16)
        | (static_cast<uint32_t>(buf[107]) << 24);
    const uint32_t calculated = Void.calculateCRC(buf, 104);
    if (advertised != calculated) {
        Serial.println("WARN: PacketC CRC mismatch — receipt dropped.");
        return false;
    }

    // VOID-140: the verified signed receipt IS the unlock authorization —
    // there is no separate unlock packet in the A→B→ACK→Settle→C→D loop
    // (decision node 185). Surface the dispense moment on serial here;
    // the OLED shows it alongside the PacketD delivery TX below. Real
    // service dispensation stays an explicit alpha non-goal.
    Serial.println("SELLER: Receipt verified - UNLOCKED (dispensing)");

    // Build PacketD. Payload convention (void_packets_snlp.h): the 98
    // body bytes of PacketC are copied verbatim into PacketD.payload so
    // the buyer can cross-reference the delivery against the receipt
    // the gateway signed.
    packet_d_builder::DeliveryInputs d_in = {};
    d_in.downlink_ts = static_cast<uint64_t>(millis());
    d_in.sat_b_id    = BUYER_SAT_ID;
    std::memcpy(d_in.payload, buf + SIZE_VOID_HEADER,
                packet_d_builder::kPayloadSize);

    static uint8_t d_frame[packet_d_builder::kPacketDSize];
    if (!packet_d_builder::build(d_in, d_frame, sizeof(d_frame))) {
        Serial.println("ERR: packet_d_builder::build failed.");
        return false;
    }

    // VOID-139 LBT: PacketD (delivery) is mandatory — CAD-gate, then send
    // best-effort after the attempt cap.
    const bool d_clear =
        Void.transmitWhenClear(d_frame, sizeof(d_frame), kLbtMaxAttempts);
    Void.updateDisplay("SELLER", "UNLOCKED - Delivery TX");
    Serial.println(d_clear
        ? "SELLER: PacketC received, PacketD TX'd (clear)"
        : "SELLER: PacketC received, PacketD TX'd (forced)");
    return true;
}
#endif  // VOID_PROTOCOL_TYPE == 2

// VOID-139: seller operational state. A real satellite advertises (beacons)
// when idle, but must NOT keep firing fresh invoices while it is servicing a
// transaction. On a half-duplex single radio a new PacketA beacon collides
// with the in-flight ACK/PacketC/PacketD downlink legs and corrupts the
// buyer's invoice RX (observed as "PacketA CRC fail"). We beacon only while
// ADVERTISING; an emitted invoice moves us to AWAITING_RECEIPT and we stay
// quiet until the transaction completes (PacketD emitted) or a timeout
// fallback re-opens advertising.
enum class SellerState : uint8_t { ADVERTISING, AWAITING_RECEIPT };

void runSellerLoop() {
    static SellerState   txState   = SellerState::ADVERTISING;
    static unsigned long lastTx    = 0;
    static unsigned long engagedAt = 0;
    static uint8_t       rx_buffer[VOID_MAX_PACKET_SIZE];

    // Alpha-bench tunables. Beacon cadence when idle; engaged-timeout is a
    // safety net so a dropped or no-show transaction re-advertises rather
    // than wedging the seller silent forever.
    static constexpr unsigned long kBeaconIntervalMs = 8000;
    static constexpr unsigned long kEngagedTimeoutMs = 8000;  // VOID-143: fast demo retry

    // One-shot ISR arm: register DIO1 packet-received callback and put
    // the radio into continuous RX on first entry.
    static bool radio_armed = false;
    if (!radio_armed) {
        Void.radio.setDio1Action(onRxDone);
        Void.radio.startReceive();
        radio_armed = true;
    }

    // ---------------------------------------------------------
    // 1. LISTEN FOR DOWNLINK (Phase 6) — ISR-gated, sized read.
    //    VOID-143: moved to the TOP of the loop (was the last section)
    //    so a pending frame is drained BEFORE any TX — beacon, PacketD
    //    or heartbeat — can clobber the shared SX126x FIFO base, and a
    //    CAD-busy beacon defer can never starve RX. Guarded block rather
    //    than an early-return gate so the TX work below still runs on
    //    idle iterations (the seller must keep beaconing).
    // ---------------------------------------------------------
    if (rx_flag) {
        rx_flag = false;

        // VOID-139: reject the spurious DIO1 interrupt raised on TxDone (every
        // beacon / PacketD) and CadDone (LBT scanChannel). Only a real RxDone
        // should drive the receive path; otherwise we parse stale FIFO bytes.
        if (!Void.isRealReception()) {
            Void.radio.startReceive();
            return;
        }

        const size_t len = Void.radio.getPacketLength();

        // Safety bounds check
        if (len <= VOID_MAX_PACKET_SIZE && len >= SIZE_VOID_HEADER) {
            const int state = Void.radio.readData(rx_buffer, len);

            if (state == RADIOLIB_ERR_NONE) {
#if VOID_PROTOCOL_TYPE == 2
                // VOID-143: SNLP sync-word filter (Protocol-spec-SNLP.md
                // §1.1) — unified with the buyer's first-line check via
                // VoidProtocol::validSyncWord. Previously the seller parsed
                // inbound frames with no sync validation, risking false
                // CRC passes on noise / foreign LoRa traffic.
                const bool sync_ok = Void.validSyncWord(rx_buffer);
                if (!sync_ok) {
                    Serial.println("WARN: SELLER dropped frame (Sync Word Fail)");
                    Void.radio.startReceive();
                    return;
                }
#endif
                // STRICT HEADER PEEKING
                uint16_t apid = getAPID(rx_buffer);

                // Diagnostic: log every received frame so we can tell the
                // difference between "no LoRa downlink reaching the seller"
                // (silence) and "frame arriving but not matching any handler"
                // (mismatch printed). Cheap, non-conditional, alpha-bench only.
                Serial.print("SELLER: RX frame apid=");
                Serial.print(apid);
                Serial.print(" len=");
                Serial.println(static_cast<unsigned>(len));

                // VOID-136: PacketC (Receipt) from bouncer → emit PacketD (Delivery).
                // Post-VOID-135 the gateway is the authoritative receipt signer
                // and the bouncer egresses PacketC over LoRa. On verified (CRC-OK)
                // RX, Sat A confirms delivery via PacketD back to Sat B.
#if VOID_PROTOCOL_TYPE == 2
                if (apid == SELLER_APID && len == SIZE_PACKET_C) {
                    // Delivery sent → transaction complete. Re-open advertising
                    // with a guard window (lastTx reset) before the next invoice.
                    if (handlePacketCReceipt(rx_buffer, len)) {
                        txState = SellerState::ADVERTISING;
                        lastTx  = millis();
                    }
                } else
#endif
                // Legacy Phase 6 TunnelData branch — preserved for backwards-compat
                // with older mule firmware that still sends a direct UNLOCK command.
                // Expected to go dormant in the post-flat-sat flow (the bouncer
                // egress path replaces it) and will be removed in a follow-up ticket.
                if (apid == 0xA1 && len == SIZE_TUNNEL_DATA) {

                    // Parse the Tunnel Data
                    TunnelData_t* tunnel = reinterpret_cast<TunnelData_t*>(rx_buffer);

                    // Verify the Command Code (0x0001 = UNLOCK)
                    if (tunnel->cmd_code == 0x0001) {
                        Void.updateDisplay("SELLER", "UNLOCKING. Generating Receipt...");

                        // ---------------------------------------------------------
                        // Legacy Phase 7: GENERATE RECEIPT (pre-VOID-135 flow)
                        // ---------------------------------------------------------
                        static PacketC_t receipt;
                        memset(&receipt, 0, sizeof(PacketC_t)); // Wipe to prevent leaks

                        receipt.header.ver_type_sec = 0x18;
                        receipt.header.apid_lo = 0xA1;

                        uint16_t rec_len = SIZE_PACKET_C - 1;


                        receipt.header.packet_len = static_cast<uint16_t>((rec_len >> 8) | (rec_len << 8));

                        // Fill Mock Payload Data
                        receipt.exec_time = millis();
                        receipt.enc_status = 0x01; // Success
                        receipt.enc_tx_id = 0x12345678;
                        receipt.crc32 = Void.calculateCRC(reinterpret_cast<const uint8_t*>(&receipt), SIZE_PACKET_C - 4);

                        // Broadcast Receipt back to Mule
                        Void.radio.transmit(reinterpret_cast<uint8_t*>(&receipt), SIZE_PACKET_C);
                        Void.radio.startReceive();
                    } else {
                        Serial.println("WARN: Tunnel command not recognized.");
                    }
                }
                // VOID-022: peer heartbeat (Packet L, len-unique on the alpha
                // wire). CRC-verify, then surface as a HEARTBEAT_RX evidence
                // line for the operator console capture. CRC-fail heartbeats
                // drop silently — telemetry only.
                else if (len == SIZE_HEARTBEAT_PCK) {
                    const size_t hb_crc_off = SIZE_HEARTBEAT_PCK - 4u;
                    const uint32_t hb_wire =
                          static_cast<uint32_t>(rx_buffer[hb_crc_off])
                        | (static_cast<uint32_t>(rx_buffer[hb_crc_off + 1u]) << 8)
                        | (static_cast<uint32_t>(rx_buffer[hb_crc_off + 2u]) << 16)
                        | (static_cast<uint32_t>(rx_buffer[hb_crc_off + 3u]) << 24);
                    if (hb_wire == Void.calculateCRC(rx_buffer, hb_crc_off)) {
                        Serial.print("HEARTBEAT_RX:");
                        Void.hexDump(rx_buffer, len);
                    }
                }
            } else {
                // VOID-143: surface RadioLib hardware errors (CRC mismatch,
                // SPI faults) instead of swallowing them silently.
                Serial.print("ERR: RadioLib readData failure code: ");
                Serial.println(state);
            }
        }
        Void.radio.startReceive();  // re-arm RX for next packet
    }

    // ---------------------------------------------------------
    // 2. BROADCAST ADVERTISING (Phase 3) — only while idle
    // ---------------------------------------------------------
    if (txState == SellerState::ADVERTISING &&
        millis() - lastTx > kBeaconIntervalMs) {
        // VOID-143: CAD-busy defers now retry with a cap — a permanently
        // busy channel can no longer gag the beacon indefinitely, because
        // after 5 attempts the broadcast is forced. Returning mid-block
        // is safe because RX is drained at the TOP of the loop, before
        // this TX work, so no pending frame is lost. lastTx is NOT
        // advanced on a defer, so the beacon retries promptly.
        static uint8_t beacon_cad_retries = 0;
        if (!Void.channelClear()) {
            beacon_cad_retries++;
            Serial.print("WARN: Seller beacon CAD busy (attempt ");
            Serial.print(beacon_cad_retries);
            Serial.println("/5)");
            Void.radio.startReceive();  // scanChannel left the radio in standby

            if (beacon_cad_retries < 5) {
                return;  // defer this beacon — retry next loop
            }
            Serial.println("WARN: Seller beacon CAD cap reached — forcing broadcast");
        }
        beacon_cad_retries = 0;
        // Clear memory to prevent leaking RAM garbage
        memset(&invoice, 0, sizeof(PacketA_t));

        // 1. Wire header (BE) — byte-packed via canonical helper so the
        //    seller's PacketA matches the buyer's parser and the Go
        //    golden vector byte-for-byte (sync word, APID, packet_len).
        uint8_t hdr_bytes[SIZE_VOID_HEADER];
        packVoidHeader(
            hdr_bytes,
            SELLER_APID,
            static_cast<uint16_t>(SIZE_PACKET_A - SIZE_VOID_HEADER));
        memcpy(&invoice.header, hdr_bytes, SIZE_VOID_HEADER);

        // 2. Payload (Little Endian)
        invoice.epoch_ts = millis();
        invoice.sat_id   = SELLER_SAT_ID;   // 0xCAFEBABE (canonical alpha ID)
        invoice.amount   = 500;
        invoice.asset_id = 1;

        // 3. CRC32 covers [0 .. crc32 offset)
        invoice.crc32 = Void.calculateCRC(
            reinterpret_cast<const uint8_t*>(&invoice),
            offsetof(PacketA_t, crc32));

        // 4. Transmit
        Void.radio.transmit(reinterpret_cast<uint8_t*>(&invoice), SIZE_PACKET_A);
        Serial.println("SELLER: Broadcasted Invoice (PacketA)");
        Serial.print("INVOICE_TX:");
        Void.hexDump(reinterpret_cast<const uint8_t*>(&invoice), SIZE_PACKET_A);
        Void.updateDisplay("SELLER", "Broadcasting Invoice...");
        
        lastTx    = millis();
        engagedAt = millis();
        txState   = SellerState::AWAITING_RECEIPT;  // servicing — stop beaconing
        Void.radio.startReceive();
    }

    // ---------------------------------------------------------
    // 2b. Engaged-timeout safety net — re-open advertising if the
    //     transaction never completed (no buyer / dropped leg). lastTx
    //     is left as-is so the next beacon fires promptly.
    // ---------------------------------------------------------
    if (txState == SellerState::AWAITING_RECEIPT &&
        millis() - engagedAt > kEngagedTimeoutMs) {
        Serial.println("SELLER: receipt timeout — re-advertising");
        txState = SellerState::ADVERTISING;
    }

    // ---------------------------------------------------------
    // 2c. VOID-022 hardening: lost-wakeup recovery. If RxDone is
    //     latched in the radio IRQ register but the DIO1 edge was
    //     missed, rx_flag never fires and the frame rots in the FIFO.
    //     Synthesise the flag; polled at most every 250 ms to keep
    //     SPI pressure negligible.
    // ---------------------------------------------------------
    {
        static unsigned long last_irq_poll = 0;
        if (!rx_flag && millis() - last_irq_poll >= 250) {
            last_irq_poll = millis();
            if (Void.isRealReception()) rx_flag = true;
        }
    }

    // ---------------------------------------------------------
    // 2d. VOID-022: 30 s heartbeat telemetry (droppable, CAD-gated,
    //     1 s backoff while busy). Only when no RX is pending — TX
    //     shares the SX126x FIFO base with RX, so transmitting before
    //     draining a received frame would clobber it.
    // ---------------------------------------------------------
    if (!rx_flag) {
        heartbeat_tx::service(
            SELLER_APID,
            (txState == SellerState::ADVERTISING)
                ? heartbeat_tx::kSysStateRxActive
                : heartbeat_tx::kSysStateConnected,
            &rx_flag);
    }
}