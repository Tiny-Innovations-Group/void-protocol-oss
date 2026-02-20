/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      buyer.cpp
 * Desc:      Buyer-side Mule State Machine.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/
#include "void_protocol.h"
#include "buyer.h"
#include "security_manager.h"

// Static State Tracking (No Heap)
static bool invoice_pending = false;
static PacketA_t pending_invoice;

// Helper to extract APID safely
static uint16_t getAPID(const uint8_t* buf) {
    return ((buf[0] & 0x07) << 8) | buf[1];
}

void runBuyerLoop() {
    // 1. LISTEN TO LORA (SPACE LINK)
    static uint8_t rx_buffer[VOID_MAX_PACKET_SIZE];  
    // int state = Void.radio.readData((uint8_t*)NULL, 0);
    int state = Void.radio.readData(reinterpret_cast<uint8_t*>(NULL), 0);

    if (state == RADIOLIB_ERR_NONE) {
        size_t len = Void.radio.getPacketLength();

        if (len <= VOID_MAX_PACKET_SIZE && len >= SIZE_VOID_HEADER) {
            Void.radio.readData(rx_buffer, len);
            uint16_t apid = getAPID(rx_buffer);

            // Phase 3: Receive Invoice
            if (apid == 0xA1 && len == SIZE_PACKET_A) {
                Void.updateDisplay("BUYER", "RX Invoice! Notifying Ground...");
                
                // Save to static state
                memcpy(&pending_invoice, rx_buffer, SIZE_PACKET_A);
                invoice_pending = true;
                
                // Notify Ground
                Serial.print("INVOICE:");
                Void.hexDump(rx_buffer, len);
            }
            // Phase 7: Receive Receipt (Packet C)
            else if (apid == 0xA1 && len == SIZE_PACKET_C) {
                Void.updateDisplay("BUYER", "RX Receipt! Wrapping Packet D...");
                
                // Wrap and Downlink (Mocking Packet D encapsulation for demo)
                Serial.print("PACKET_D:");
                Void.hexDump(rx_buffer, len); // In prod, wrap in 128-byte PacketD_t
            }
        }
    }

    // 2. LISTEN TO SERIAL (GROUND LINK)
    if (Serial.available() > 0) {
        static char serial_buf[256];
        size_t bytesRead = Serial.readBytesUntil('\n', serial_buf, sizeof(serial_buf) - 1);
        serial_buf[bytesRead] = '\0'; // Null terminate

        // Ground Authorized the Buy
        if (strncmp(serial_buf, "ACK_BUY", 7) == 0 && invoice_pending) {
            Void.updateDisplay("BUYER", "Building Packet B...");
            
            static PacketB_t packet_b;
            // Mock encrypting Packet A into Packet B
            // Security.encryptPacketB(packet_b, (const uint8_t*)&pending_invoice, SIZE_PACKET_A);
            Security.encryptPacketB(packet_b, reinterpret_cast<const uint8_t*>(&pending_invoice), SIZE_PACKET_A);
            Serial.print("PACKET_B:");
            Void.hexDump((const uint8_t*)&packet_b, SIZE_PACKET_B);
            invoice_pending = false; // Clear state
        }
        // Ground Sends L2 ACK/Tunnel Data
        else if (strncmp(serial_buf, "ACK_DOWNLINK:", 13) == 0) {
            Void.updateDisplay("BUYER", "Relaying Tunnel Data...");
            
            // Extract the Tunnel Data (88 Bytes)
            static uint8_t tunnel_data[SIZE_TUNNEL_DATA];
            // For the demo, we just fill it with dummy data as if we parsed the hex
            memset(tunnel_data, 0xAA, SIZE_TUNNEL_DATA); 
            
            // Broadcast to Seller via LoRa
            Void.radio.transmit(tunnel_data, SIZE_TUNNEL_DATA);
            Void.radio.startReceive(); // Go back to listening
        }
        // Ground Replies with its Ephemeral Key
        else if (strncmp(serial_buf, "HANDSHAKE_ACK:", 14) == 0) {
            Void.updateDisplay("AUTH", "Deriving Session Key...");
            
            // Convert Hex back to bytes (Simplified for demo)
            PacketH_t mock_resp;
            for(int i = 0; i < 32; i++) {
                char byteStr[3] = {serial_buf[14 + (i*2)], serial_buf[15 + (i*2)], '\0'};
                mock_resp.eph_pub_key[i] = (uint8_t)strtol(byteStr, NULL, 16);
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