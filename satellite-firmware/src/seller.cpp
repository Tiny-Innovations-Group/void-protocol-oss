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

// Helper to extract APID safely
static uint16_t getAPID(const uint8_t* buf) {
    return static_cast<uint16_t>(((buf[0] & 0x07) << 8) | buf[1]);
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
    
    // ---------------------------------------------------------
    // 1. BROADCAST ADVERTISING (Phase 3)
    // ---------------------------------------------------------
    if (millis() - lastTx > 8000) {
        // Clear memory to prevent leaking RAM garbage
        memset(&invoice, 0, sizeof(PacketA_t));

        // Header (Big Endian)
        invoice.header.ver_type_sec = 0x18; 
        invoice.header.apid_lo = 0xA1;
        invoice.header.seq_flags = 0xC0;
        invoice.header.seq_count_lo = 0x00;
        
        uint16_t raw_len = SIZE_PACKET_A - 1;
        // Wrap the math in static_cast
        invoice.header.packet_len = static_cast<uint16_t>((raw_len >> 8) | (raw_len << 8));

        // Payload (Little Endian)
        invoice.sat_id = 0xAAAAAAAA;
        invoice.amount = 500;
        invoice.asset_id = 1;
        invoice.epoch_ts = millis();
        invoice.crc32 = Void.calculateCRC(reinterpret_cast<const uint8_t*>(&invoice), SIZE_PACKET_A - 4);

        // Transmit using strictly typed reinterpret_cast
        Void.radio.transmit(reinterpret_cast<uint8_t*>(&invoice), SIZE_PACKET_A);
        Void.updateDisplay("SELLER", "Broadcasting Invoice...");
        
        lastTx = millis();
        Void.radio.startReceive();
    }

    // ---------------------------------------------------------
    // 2. LISTEN FOR TUNNEL RELAY (Phase 6)
    // ---------------------------------------------------------
    static uint8_t rx_buffer[VOID_MAX_PACKET_SIZE];
    int state = Void.radio.readData(reinterpret_cast<uint8_t*>(NULL), 0);

    if (state == RADIOLIB_ERR_NONE) {
        size_t len = Void.radio.getPacketLength();

        // Safety bounds check
        if (len <= VOID_MAX_PACKET_SIZE && len >= SIZE_VOID_HEADER) {
            Void.radio.readData(rx_buffer, len);
            
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
}