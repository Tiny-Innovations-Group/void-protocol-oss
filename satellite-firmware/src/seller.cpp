/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      seller.cpp
 * Desc:      Seller-side packet generation and transmission loop.
 * Compliant: NSA Clean C++ (Static Alloc, Endian Safe).
 * -------------------------------------------------------------------------*/
#include "void_protocol.h"
#include "seller.h"



// Static storage for the outbound packet (No Heap)
static PacketA_t invoice;

void runSellerLoop() {
    static unsigned long lastTx = 0;
    
    // Broadcast every 5 seconds (Advertising Phase)
    if (millis() - lastTx > 5000) {

        // 1. HEADER GENERATION (Big-Endian Compliance)
        // Ver(3)|Type(1)|Sec(1)|APID_Hi(3) = 000 1 1 000 = 0x18
        invoice.header.ver_type_sec = 0x18; 
        invoice.header.apid_lo = 0xA1;      // APID 0xA1 (ID: Sat A)
        invoice.header.seq_flags = 0xC0;    // Unsegmented (11)
        invoice.header.seq_count_lo = 0x00; // Counter (Mock)

        // Packet Length: 16-bit Big Endian (Total Length - 1)
        uint16_t raw_len = SIZE_PACKET_A - 1;
        invoice.header.packet_len = (raw_len >> 8) | (raw_len << 8);

        // 2. PAYLOAD GENERATION (Little-Endian / Native ESP32)
        invoice.sat_id = 0xAAAAAAAA;        // "Sat A"
        invoice.amount = 500;               // 5.00 USDC
        invoice.asset_id = 1;               // USDC
        invoice.epoch_ts = millis();        // Timestamp

        // Calculate CRC (Placeholder) - In production, this would be a proper CRC32 over the header + payload -> does func need to use const
        invoice.crc32 = Void.calculateCRC((uint8_t*)&invoice, SIZE_PACKET_A - 4);


        // 3. TRANSMIT (Zero Copy)
        // We cast the struct directly to uint8_t* for the radio driver
        int state = Void.radio.transmit((uint8_t*)&invoice, SIZE_PACKET_A);
        
        if (state == RADIOLIB_ERR_NONE) {
            Void.updateDisplay("SELLER", "Broadcasting Invoice...");
            Serial.print("[TX] Sent Packet A. Len: ");
            Serial.println(SIZE_PACKET_A);
        } else {
            Void.updateDisplay("ERROR", "Tx Failed");
        }
        
        lastTx = millis();
        
        // 3. Listen Window (60s would go here)
        Void.radio.startReceive();
    }
}