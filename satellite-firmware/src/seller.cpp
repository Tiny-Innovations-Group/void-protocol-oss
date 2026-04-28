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

// Helper to extract APID safely
static uint16_t getAPID(const uint8_t* buf) {
    return static_cast<uint16_t>(((buf[0] & 0x07) << 8) | buf[1]);
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
static void handlePacketCReceipt(const uint8_t* buf, size_t len) {
    if (len != SIZE_PACKET_C) return;

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
        return;
    }

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
        return;
    }

    Void.radio.transmit(d_frame, sizeof(d_frame));
    Void.updateDisplay("SELLER", "Delivery TX (PacketD)");
    Void.radio.startReceive();
}
#endif  // VOID_PROTOCOL_TYPE == 2

void runSellerLoop() {
    static unsigned long lastTx = 0;
    static uint8_t rx_buffer[VOID_MAX_PACKET_SIZE];

    // One-shot ISR arm: register DIO1 packet-received callback and put
    // the radio into continuous RX on first entry.
    static bool radio_armed = false;
    if (!radio_armed) {
        Void.radio.setDio1Action(onRxDone);
        Void.radio.startReceive();
        radio_armed = true;
    }

    // ---------------------------------------------------------
    // 1. BROADCAST ADVERTISING (Phase 3)
    // ---------------------------------------------------------
    if (millis() - lastTx > 8000) {
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
        
        lastTx = millis();
        Void.radio.startReceive();
    }

    // ---------------------------------------------------------
    // 2. LISTEN FOR TUNNEL RELAY (Phase 6) — ISR-gated, sized read
    // ---------------------------------------------------------
    if (!rx_flag) return;
    rx_flag = false;
    const size_t len = Void.radio.getPacketLength();

    // Safety bounds check
    if (len <= VOID_MAX_PACKET_SIZE && len >= SIZE_VOID_HEADER) {
        const int state = Void.radio.readData(rx_buffer, len);

        if (state == RADIOLIB_ERR_NONE) {
            // STRICT HEADER PEEKING
            uint16_t apid = getAPID(rx_buffer);

            // VOID-136: PacketC (Receipt) from bouncer → emit PacketD (Delivery).
            // Post-VOID-135 the gateway is the authoritative receipt signer
            // and the bouncer egresses PacketC over LoRa. On verified (CRC-OK)
            // RX, Sat A confirms delivery via PacketD back to Sat B.
#if VOID_PROTOCOL_TYPE == 2
            if (apid == SELLER_APID && len == SIZE_PACKET_C) {
                handlePacketCReceipt(rx_buffer, len);
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
        }
    }
    Void.radio.startReceive();  // re-arm RX for next packet
}