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

static PacketA_t invoice;

// Helper to extract APID safely
static uint16_t getAPID(const uint8_t* buf) {
    return ((buf[0] & 0x07) << 8) | buf[1];
}

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
        invoice.header.packet_len = (raw_len >> 8) | (raw_len << 8);

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

            // Phase 6 Check: Is it addressed to Sat A (0xA1) and exactly 88 bytes?
            if (apid == 0xA1 && len == SIZE_TUNNEL_DATA) {
                
                // Parse the Tunnel Data
                TunnelData_t* tunnel = reinterpret_cast<TunnelData_t*>(rx_buffer);
                
                // Verify the Command Code (0x0001 = UNLOCK)
                if (tunnel->cmd_code == 0x0001) {
                    Void.updateDisplay("SELLER", "UNLOCKING. Generating Receipt...");
                    
                    // ---------------------------------------------------------
                    // 3. GENERATE RECEIPT (Phase 7)
                    // ---------------------------------------------------------
                    static PacketC_t receipt;
                    memset(&receipt, 0, sizeof(PacketC_t)); // Wipe to prevent leaks
                    
                    receipt.header.ver_type_sec = 0x18;
                    receipt.header.apid_lo = 0xA1;
                    
                    uint16_t rec_len = SIZE_PACKET_C - 1;
                    receipt.header.packet_len = (rec_len >> 8) | (rec_len << 8);
                    
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