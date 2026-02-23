/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      main.cpp
 * Desc:      Multithreaded CLI & Serial Bridge connecting Edge to Web3 Gateway.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <thread>
#include <atomic>
#include <chrono>

#include "serial_hal.h"
#include "bouncer.h"
#include "gateway_client.h"

// --- Global State ---
// Stack-allocated modules (No Heap) as per .cursorrules
std::atomic<bool> is_running{true};
Bouncer edge_firewall;
GatewayClient go_gateway("127.0.0.1", 8080);

// --- Helper: Hex to Binary (No Heap) ---
void hex_to_bin(const char* hex, uint8_t* bin_out, size_t max_len) {
    size_t len = std::strlen(hex);
    for (size_t i = 0; i < len && (i / 2) < max_len; i += 2) {
        char byte_str[3] = {hex[i], hex[i+1], '\0'};
        bin_out[i/2] = static_cast<uint8_t>(std::strtol(byte_str, nullptr, 16));
    }
}

// --- Test Command: Simulate Radio Packet ---
static void test_ack() {
    std::puts("\n[INFO] Simulating incoming PacketB_t from LoRa Radio...");

    PacketB_t mock_radio_rx = {};
    mock_radio_rx.header.apid_lo = 0x08; // Dummy Header
    
    // Inject mock data into the encrypted payload space (62 bytes)
    uint64_t ts = 1708722000;
    uint32_t sat = 999;
    uint64_t amt = 50000;
    uint16_t ast = 1;
    
    std::memcpy(mock_radio_rx.enc_payload + 0, &ts, 8);
    std::memcpy(mock_radio_rx.enc_payload + 44, &sat, 4);
    std::memcpy(mock_radio_rx.enc_payload + 48, &amt, 8);
    std::memcpy(mock_radio_rx.enc_payload + 56, &ast, 2);

    uint8_t sanitized_out[62] = {0};

    // 1. Pass it through the firewall
    if (edge_firewall.process_packet(reinterpret_cast<const uint8_t*>(&mock_radio_rx), sizeof(PacketB_t), sanitized_out, sizeof(sanitized_out))) {
        std::puts("[BOUNCER] ✅ Firewall passed. Bridging to Web3 Gateway...");
        
        // 2. Send via TCP to Go Server
        if (go_gateway.push_to_l2(sanitized_out, sizeof(sanitized_out))) {
            std::puts("[GATEWAY] ✅ Payload delivered to Enterprise Gateway.");
        } else {
            std::puts("[GATEWAY] ❌ Failed to reach Go Gateway on port 8080.");
        }
    } else {
        std::puts("[BOUNCER] ❌ Packet dropped by firewall.");
    }
}

// --- Background CLI Thread ---
void cli_listener() {
    char input[32] = {0};
    std::puts("\n💻 CLI Ready. Commands: 'h', 'ack', 'tst_ack' (Test Pipeline), 'exit'");
    
    while (is_running) {
        if (std::fgets(input, sizeof(input), stdin)) {
            size_t len = std::strlen(input);
            if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0'; // Strip newline

            if (std::strcmp(input, "h") == 0) {
                std::puts("[CLI] Triggering Handshake via USB...");
                const char* cmd = "H";
                serial_write_bytes(reinterpret_cast<const uint8_t*>(cmd), 1);
            } 
            else if (std::strcmp(input, "ack") == 0) {
                std::puts("[CLI] Authorizing Buy...");
                const char* cmd = "ACK_BUY\n";
                serial_write_bytes(reinterpret_cast<const uint8_t*>(cmd), std::strlen(cmd));
            } 
            else if (std::strcmp(input, "tst_ack") == 0) {
                test_ack(); // Run our zero-heap pipeline test
            }
            else if (std::strcmp(input, "exit") == 0) {
                std::puts("[CLI] Shutting down...");
                is_running = false;
            }
        }
    }
}

// --- Main execution ---
int main(int argc, char* argv[]) {
    // We allow running without a COM port strictly for testing the 'tst_ack' CLI command
    bool hardware_connected = false;
    
    if (argc >= 2) {
        if (!serial_open(argv[1], 115200)) {
            std::printf("[ERROR] Failed to connect to %s\n", argv[1]);
        } else {
            std::printf("[SYSTEM] Connected to hardware on %s\n", argv[1]);
            hardware_connected = true;
        }
    } else {
        std::puts("[SYSTEM] Starting in TEST MODE (No COM port provided). Use 'tst_ack'.");
    }

    // Spawn the CLI in the background
    std::thread cli_thread(cli_listener);

    // Buffers for reading USB Serial streams
    uint8_t rx_buf[256];
    char line_buf[512] = {0};
    size_t line_idx = 0;

    // --- The Main Hardware Polling Loop ---
    while (is_running) {
        if (hardware_connected) {
            int bytes = serial_read_bytes(rx_buf, sizeof(rx_buf));
            
            for (int i = 0; i < bytes; i++) {
                char c = static_cast<char>(rx_buf[i]);
                if (c == '\n' || c == '\r') {
                    if (line_idx > 0) {
                        line_buf[line_idx] = '\0'; 
                        
                        if (std::strncmp(line_buf, "PACKET_B:", 9) == 0) {
                            std::puts("\n[HARDWARE] 📦 Received PACKET B from Sat B. Routing to Bouncer...");
                            
                            uint8_t packet_bin[176]; 
                            uint8_t cleartext_out[256];
                            
                            hex_to_bin(&line_buf[9], packet_bin, sizeof(packet_bin));
                            
                            // Let the Bouncer validate the crypto & structural limits
                            if (edge_firewall.process_packet(packet_bin, sizeof(packet_bin), cleartext_out, sizeof(cleartext_out))) {
                                std::puts("[BOUNCER] ✅ Signature Valid. Decryption Success.");
                                
                                // Push the LIVE hardware packet to the Go Gateway
                                if (go_gateway.push_to_l2(cleartext_out, 62)) {
                                    std::puts("[GATEWAY] ✅ Live hardware payload delivered to Enterprise Gateway.");
                                } else {
                                    std::puts("[GATEWAY] ❌ Failed to reach Go Gateway.");
                                }
                            } else {
                                std::puts("[BOUNCER] ❌ Threat Detected. Packet Dropped.");
                            }
                        }


                        // --- RESTORED CHUNK ---
                        else if (std::strncmp(line_buf, "INVOICE:", 8) == 0) {
                            std::puts("\n[HARDWARE] 📄 Received Packet A (Invoice). Awaiting 'ack' command.");
                        }
                        // ----------------------
                        line_idx = 0; // Reset buffer
                    }
                } else if (line_idx < sizeof(line_buf) - 1) {
                    line_buf[line_idx++] = c;
                }
            }
        }
        
        // Sleep for 10ms to prevent CPU pegging (100% usage)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    cli_thread.detach(); 
    if (hardware_connected) serial_close();
    std::puts("[SYSTEM] Ground Station shut down securely.");
    return 0;
}