/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      buyer.cpp
 * Desc:      Buyer-side Mule State Machine — VOID-128 PacketB TX pipeline.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/
#include "void_protocol.h"
#include "void_config.h"
#include "void_payment_payload.h"
#include "buyer.h"
#include "security_manager.h"
#include "gps_stub.h"
#include "packet_d_builder.h"     // VOID-140: kPacketDMagic / kPacketDSize wire layout
#include "heartbeat_tx.h"         // VOID-022: 30 s heartbeat telemetry

#include <cstddef>
#include <cstdint>
#include <cstring>

// --- Static state (no heap) ---
static bool       invoice_pending = false;
static PacketA_t  pending_invoice;

// --- RX ISR flag (DIO1 packet-received interrupt) ---
// Replaces the earlier readData(NULL, 0) poll, which crashed the SX126x
// SPI path with a StoreProhibited when a real packet arrived. Canonical
// RadioLib pattern: ISR sets the flag, loop drains it with a sized read.
static volatile bool rx_flag = false;
static void IRAM_ATTR onRxDone() { rx_flag = true; }

// --- Duty-cycle observation (VOID-128) ---
// DUTY_CYCLE_TARGET_MS (36 s) comes from void_config.h. At this stage we only
// LOG the observed inter-TX gap; hard enforcement is deferred to VOID-070.
// A gap below target is flagged "UNDER" in the serial log, not dropped.
static unsigned long last_tx_ms = 0;  // 0 sentinel = no TX yet this session

// VOID-139 LBT: max CAD attempts before a mandatory frame is sent
// best-effort. ~6 attempts × (8..48 ms backoff) ≈ <200 ms worst case.
static constexpr uint8_t kLbtMaxAttempts = 6;

// Extract the 11-bit CCSDS APID from a raw received buffer.
// Caller MUST have already bounded the length to >= SIZE_VOID_HEADER.
static uint16_t extractAPID(const uint8_t* buf) {
#if VOID_PROTOCOL_TYPE == 2
    // SNLP prepends a 4-byte sync word before the CCSDS ID field.
    return static_cast<uint16_t>(((buf[4] & 0x07u) << 8) | buf[5]);
#else
    return static_cast<uint16_t>(((buf[0] & 0x07u) << 8) | buf[1]);
#endif
}

// Pack a VOID header (BE) for a non-command telemetry packet into the
// supplied byte buffer. `body_len` is the full frame size minus
// SIZE_VOID_HEADER. The CCSDS packet_len field is encoded as
// (body_len - 1) to match gateway/test/utils/generate_packets.go::
// buildHeader, which is the authority for the golden vector bytes.
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

// Little-endian u32 load from a byte buffer. Used for wire-format CRC
// comparisons where the field is LE regardless of host endianness.
static uint32_t loadLE32(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) <<  8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// ---------------------------------------------------------------------------
// runBuyerLoop — RX PacketA, on "ACK_BUY" from ground build & TX PacketB.
// ---------------------------------------------------------------------------
void runBuyerLoop() {
    // One-shot ISR arm: register DIO1 packet-received callback and put
    // the radio into continuous RX on first entry.
    static bool radio_armed = false;
    if (!radio_armed) {
        Void.radio.setDio1Action(onRxDone);
        Void.radio.startReceive();
        radio_armed = true;
    }

    // =====================================================================
    // 1. LoRa space-link receive (ISR-gated, sized read)
    // =====================================================================
    if (rx_flag) {
        rx_flag = false;

        // VOID-139: DIO1 also fires on TxDone/CadDone, so this flag is set
        // after every transmit() and scanChannel(). Reject the phantom
        // before reading the radio — otherwise getPacketLength() returns the
        // STALE last-RX length and readData() hands back the just-transmitted
        // FIFO bytes (TX/RX share base 0x00), which then fail the PacketA CRC
        // check. This — not RF collision — is the "PacketA CRC fail" drop.
        if (!Void.isRealReception()) {
            Void.radio.startReceive();
            return;
        }

        static uint8_t rx_buffer[VOID_MAX_PACKET_SIZE];
        const size_t len = Void.radio.getPacketLength();

        if (len <= VOID_MAX_PACKET_SIZE && len >= SIZE_VOID_HEADER) {
            const int state = Void.radio.readData(rx_buffer, len);
            if (state == RADIOLIB_ERR_NONE) {
#if VOID_PROTOCOL_TYPE == 2
                // VOID-143: unified SNLP sync-word filter (moved from a
                // file-static here into VoidProtocol so the seller shares it).
                if (!Void.validSyncWord(rx_buffer)) {
                    Void.radio.startReceive();
                    return;
                }
#endif
                const uint16_t apid = extractAPID(rx_buffer);

                // --- RX PacketA: Invoice from Sat A (SELLER_APID) ---
                if (apid == SELLER_APID && len == SIZE_PACKET_A) {
                    // VOID-128: validate the invoice CRC32 BEFORE struct cast.
                    // Protects downstream state from a flipped-bit header that
                    // happened to pass the sync-word + length + APID checks.
                    const size_t   crc_end  = offsetof(PacketA_t, crc32);
                    const uint32_t calc_crc = Void.calculateCRC(rx_buffer, crc_end);
                    const uint32_t wire_crc = loadLE32(rx_buffer + crc_end);
                    if (calc_crc != wire_crc) {
                        Void.updateDisplay("BUYER", "PacketA CRC fail — drop");
                        Serial.println("WARN:PacketA CRC mismatch, dropped");
                        Void.radio.startReceive();
                        return;
                    }

                    Void.updateDisplay("BUYER", "RX Invoice! Notifying Ground...");
                    memcpy(&pending_invoice, rx_buffer, SIZE_PACKET_A);
                    invoice_pending = true;

                    Serial.print("INVOICE:");
                    Void.hexDump(rx_buffer, len);
                }
                // --- RX PacketD: Delivery confirmation from Sat A (VOID-140) ---
                // Final leg of the A→B→ACK→Settle→C→D loop: the seller
                // accepted the signed receipt (its UNLOCK moment) and
                // dispatched delivery. PacketAck shares APID 101 and the
                // 136 B frame size — pkt_type differs but the F-03 magic at
                // body offset 0 is the canonical resolver (0xD0 = PacketD,
                // 0xAC = PacketAck). Display-only per decision 192→197:
                // no gateway forwarding in alpha.
                //
                // (Replaces the stale pre-VOID-135 passthrough that matched
                // PacketC [apid 100 / len 112] and mislabelled it PACKET_D:
                // on serial — the real PacketD was silently dropped.)
#if VOID_PROTOCOL_TYPE == 2
                else if (apid == BUYER_APID && len == SIZE_PACKET_D &&
                         rx_buffer[SIZE_VOID_HEADER] ==
                             packet_d_builder::kPacketDMagic) {
                    // CRC32 (IEEE-802.3, LE on wire) covers header + body up
                    // to but excluding the CRC field; the 6-byte tail pad is
                    // out of scope (packet_d_builder.cpp: CRC at [126..130)).
                    constexpr size_t kDCrcOffset =
                        packet_d_builder::kPacketDSize - 10u;  // 4 CRC + 6 tail
                    const uint32_t calc_crc =
                        Void.calculateCRC(rx_buffer, kDCrcOffset);
                    const uint32_t wire_crc = loadLE32(rx_buffer + kDCrcOffset);
                    if (calc_crc != wire_crc) {
                        Serial.println("WARN:PacketD CRC mismatch, dropped");
                    } else {
                        Void.updateDisplay("BUYER", "DELIVERY RECEIVED");
                        Serial.print("PACKET_D_RX:");
                        Void.hexDump(rx_buffer, len);
                        Serial.println(
                            "BUYER: PacketD verified - delivery confirmed, loop closed");
                    }
                }
#endif
                // VOID-022: peer heartbeat (Packet L, len-unique on the
                // alpha wire). CRC-verify, then emit HEARTBEAT_RX — the
                // bouncer forwards this line's frame to the gateway for
                // heartbeats.json evidence. CRC-fail drops silently.
                else if (len == SIZE_HEARTBEAT_PCK) {
                    const size_t hb_crc_off = SIZE_HEARTBEAT_PCK - 4u;
                    const uint32_t hb_wire = loadLE32(rx_buffer + hb_crc_off);
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

    // =====================================================================
    // 1b. VOID-022 hardening: lost-wakeup recovery — if RxDone is latched
    //     in the radio IRQ register but the DIO1 edge was missed, rx_flag
    //     never fires and the frame rots in the FIFO. Synthesise the flag;
    //     polled at most every 250 ms.
    // =====================================================================
    {
        static unsigned long last_irq_poll = 0;
        if (!rx_flag && millis() - last_irq_poll >= 250) {
            last_irq_poll = millis();
            if (Void.isRealReception()) rx_flag = true;
        }
    }

    // =====================================================================
    // 1c. VOID-022: 30 s heartbeat telemetry (droppable, CAD-gated, 1 s
    //     backoff while busy). Skipped while an RX is pending (shared
    //     SX126x FIFO base).
    // =====================================================================
    if (!rx_flag) {
        heartbeat_tx::service(
            BUYER_APID,
            invoice_pending ? heartbeat_tx::kSysStateConnected
                            : heartbeat_tx::kSysStateRxActive,
            &rx_flag);
    }

    // =====================================================================
    // 2. Serial ground-link commands
    // =====================================================================
    if (Serial.available() > 0) {
        // 512 holds the longest line the bouncer ever sends:
        // "PACKET_ACK_TX:" (14) + 136-byte SNLP frame as hex (272) + \n + \0 = 288.
        // 512 gives headroom for any future frame growth.
        static char serial_buf[512];
        const size_t bytesRead =
            Serial.readBytesUntil('\n', serial_buf, sizeof(serial_buf) - 1);
        serial_buf[bytesRead] = '\0';

        // TODO: look into using (strncmp(serial_buf, "ACK_BUY", 7) == 0) for all command parsing to avoid the single-char command edge case. 
        // This would also allow us to add more commands without worrying about the "H" vs "HANDSHAKE_ACK" overlap.
        // -----------------------------------------------------------------
        // Ground triggered a handshake (single-char 'H'/'h' command).
        // -----------------------------------------------------------------
        if (bytesRead == 1 && (serial_buf[0] == 'H' || serial_buf[0] == 'h')) {
            Void.updateDisplay("AUTH", "Generating Keys...");

            static PacketH_t handshake_pkt;
            Security.prepareHandshake(
                handshake_pkt, VOID_SESSION_TTL_DEF, millis());

            Serial.print("HANDSHAKE_TX:");
            Void.hexDump(
                reinterpret_cast<uint8_t*>(&handshake_pkt), SIZE_PACKET_H);
            Void.updateDisplay("AUTH", "Handshake Sent");
        }
        // -----------------------------------------------------------------
        // Ground authorised the buy -> build & TX PacketB
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "ACK_BUY", 7) == 0 && invoice_pending) {
            // --- Duty-cycle observation (non-blocking) ---
            if (last_tx_ms != 0) {
                const unsigned long now = millis();
                const unsigned long gap = now - last_tx_ms;
                char gap_line[80];
                snprintf(gap_line, sizeof(gap_line),
                         "DUTY_GAP_MS:%lu target=%u%s",
                         gap,
                         static_cast<unsigned>(DUTY_CYCLE_TARGET_MS),
                         (gap < DUTY_CYCLE_TARGET_MS) ? " UNDER" : " OK");
                Serial.println(gap_line);
            }

            Void.updateDisplay("BUYER", "Building Packet B...");

            // --- Build PacketB_t in a static buffer (no heap) ---
            static PacketB_t packet_b;
            memset(&packet_b, 0, sizeof(PacketB_t));

            // 2a. Wire header (BE) — byte-packed to match Go golden vectors.
            uint8_t hdr_bytes[SIZE_VOID_HEADER];
            packVoidHeader(
                hdr_bytes,
                BUYER_APID,
                static_cast<uint16_t>(SIZE_PACKET_B - SIZE_VOID_HEADER));
            memcpy(&packet_b.header, hdr_bytes, SIZE_VOID_HEADER);

            // 2b. Outer mule fields (LE).
            GpsStub.update();
            packet_b.epoch_ts = GpsStub.getEpochMs();
            double pos_tmp[3];
            GpsStub.getPositionVec(pos_tmp);
            memcpy(packet_b.pos_vec, pos_tmp, sizeof(pos_tmp));

            // 2c. Inner Invoice Payload — verbatim echo of the received
            // PacketA body fields (Protocol-spec-SNLP.md §4.3).
            InvoicePayload_t inner;
            memset(&inner, 0, sizeof(InvoicePayload_t));
            inner.epoch_ts = pending_invoice.epoch_ts;
            memcpy(inner.pos_vec, pending_invoice.pos_vec, sizeof(inner.pos_vec));
            memcpy(inner.vel_vec, pending_invoice.vel_vec, sizeof(inner.vel_vec));
            inner.sat_id   = pending_invoice.sat_id;
            inner.amount   = pending_invoice.amount;
            inner.asset_id = pending_invoice.asset_id;
            inner.crc32    = pending_invoice.crc32;

            // 2d. Sat B identity — low 4 bytes of the derived ChaCha20
            // nonce under the optional encrypted SNLP build (VOID-110).
            packet_b.sat_id = BUYER_SAT_ID;

            // 2e. Copy inner payload into enc_payload AND Ed25519-sign.
            // Under VOID_ALPHA_PLAINTEXT this stores cleartext and skips
            // session / GPS / monotonic-epoch guards (VOID-127).
            if (!Security.encryptPacketB(
                    packet_b,
                    reinterpret_cast<const uint8_t*>(&inner),
                    sizeof(InvoicePayload_t))) {
                Void.updateDisplay("ERROR", "encryptPacketB failed");
                Serial.println("ERROR:encryptPacketB returned false");
                invoice_pending = false;
                return;
            }

            // 2f. global_crc — covers header + body up to (but excluding)
            // the global_crc field itself. _tail_pad is ignored.
            const size_t crc_end = offsetof(PacketB_t, global_crc);
            packet_b.global_crc  = Void.calculateCRC(
                reinterpret_cast<const uint8_t*>(&packet_b), crc_end);

            // 2g. Transmit (LBT-gated) and return to receive. PacketB is a
            // mandatory frame — transmitWhenClear waits for a clear channel,
            // then sends best-effort after the attempt cap.
            if (!Void.transmitWhenClear(
                    reinterpret_cast<uint8_t*>(&packet_b), SIZE_PACKET_B,
                    kLbtMaxAttempts)) {
                Serial.println("WARN: PacketB TX forced (channel never cleared)");
            }

            Serial.print("PACKET_B:");
            Void.hexDump(reinterpret_cast<const uint8_t*>(&packet_b), SIZE_PACKET_B);

            last_tx_ms      = millis();
            invoice_pending = false;
        }
        // -----------------------------------------------------------------
        // VOID-143: orphan ACK_BUY — ground authorised a buy but no
        // invoice is pending (invoice dropped / already consumed). Log it
        // instead of discarding silently so the operator sees the lost leg.
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "ACK_BUY", 7) == 0) {
            Serial.println("WARN: ACK_BUY received but no invoice is pending.");
        }
        // -----------------------------------------------------------------
        // Ground relays an L2 ACK / tunnel payload — broadcast to Sat A
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "ACK_DOWNLINK:", 13) == 0) {
            Void.updateDisplay("BUYER", "Relaying Tunnel Data...");

            static uint8_t tunnel_data[SIZE_TUNNEL_DATA];
            memset(tunnel_data, 0xAA, SIZE_TUNNEL_DATA);

            Void.radio.transmit(tunnel_data, SIZE_TUNNEL_DATA);
            Void.radio.startReceive();
        }
        // -----------------------------------------------------------------
        // VOID-134: bouncer userland built a PacketAck and asked us to
        // relay it back over LoRa to Sat B (the buyer satellite — i.e.,
        // ourselves in the flat-sat topology, but the buyer's RX path
        // will hear it as a peer broadcast). Line format:
        //   PACKET_ACK_TX:<272-hex-chars>\n   (14-byte prefix + 136 SNLP body)
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "PACKET_ACK_TX:", 14) == 0) {
            constexpr size_t kPrefix = 14;
            if (bytesRead < kPrefix + 2 * SIZE_PACKET_ACK) {
                Serial.println("WARN: PACKET_ACK_TX line truncated; dropped.");
            } else {
                static uint8_t ack_frame[SIZE_PACKET_ACK];
                for (size_t i = 0; i < SIZE_PACKET_ACK; ++i) {
                    const char byteStr[3] = {
                        serial_buf[kPrefix + (i * 2)],
                        serial_buf[kPrefix + (i * 2) + 1],
                        '\0'
                    };
                    ack_frame[i] = static_cast<uint8_t>(
                        strtol(byteStr, NULL, 16));
                }
                const bool ack_clear = Void.transmitWhenClear(
                    ack_frame, SIZE_PACKET_ACK, kLbtMaxAttempts);
                Serial.println(ack_clear
                    ? "BUYER: Relayed PacketAck over LoRa (clear)"
                    : "BUYER: Relayed PacketAck over LoRa (forced)");
            }
        }
        // -----------------------------------------------------------------
        // VOID-135b/138: bouncer egress poll handed us a PacketC for
        // downlink to Sat A (the seller). Line format:
        //   PACKET_C_TX:<224-hex-chars>\n    (12-byte prefix + 112 SNLP body)
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "PACKET_C_TX:", 12) == 0) {
            constexpr size_t kPrefix = 12;
            if (bytesRead < kPrefix + 2 * SIZE_PACKET_C) {
                Serial.println("WARN: PACKET_C_TX line truncated; dropped.");
            } else {
                static uint8_t c_frame[SIZE_PACKET_C];
                for (size_t i = 0; i < SIZE_PACKET_C; ++i) {
                    const char byteStr[3] = {
                        serial_buf[kPrefix + (i * 2)],
                        serial_buf[kPrefix + (i * 2) + 1],
                        '\0'
                    };
                    c_frame[i] = static_cast<uint8_t>(
                        strtol(byteStr, NULL, 16));
                }
                const bool c_clear = Void.transmitWhenClear(
                    c_frame, SIZE_PACKET_C, kLbtMaxAttempts);
                Serial.println(c_clear
                    ? "BUYER: Relayed PacketC over LoRa (clear)"
                    : "BUYER: Relayed PacketC over LoRa (forced)");
            }
        }
        // -----------------------------------------------------------------
        // Ground returns its ephemeral key — derive session key
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "HANDSHAKE_ACK:", 14) == 0) {
            Void.updateDisplay("AUTH", "Deriving Session Key...");

            PacketH_t mock_resp;
            memset(&mock_resp, 0, sizeof(PacketH_t));
            for (int i = 0; i < 32; ++i) {
                const char byteStr[3] = {
                    serial_buf[14 + (i * 2)],
                    serial_buf[15 + (i * 2)],
                    '\0'
                };
                mock_resp.eph_pub_key[i] =
                    static_cast<uint8_t>(strtol(byteStr, NULL, 16));
            }

            if (Security.processHandshakeResponse(mock_resp)) {
                Void.updateDisplay("AUTH", "Session ACTIVE");
                Serial.println("SAT_READY: Session Key Derived locally.");
            } else {
                Void.updateDisplay("ERROR", "ECDH Math Failed");
            }
        }
    }
}
