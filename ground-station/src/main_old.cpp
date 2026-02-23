/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      main.cpp
 * Desc:      CLI & Serial Hardware Bridge for Void Ground Station.
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

// --- Global State ---
std::atomic<bool> is_running{true};
Bouncer edge_firewall;

// --- Helper: Hex to Binary (No Heap) ---
void hex_to_bin(const char* hex, uint8_t* bin_out, size_t max_len) {
    size_t len = std::strlen(hex);
    for (size_t i = 0; i < len && (i / 2) < max_len; i += 2) {
        char byte_str[3] = {hex[i], hex[i+1], '\0'};
        bin_out[i/2] = static_cast<uint8_t>(std::strtol(byte_str, nullptr, 16));
    }
}

// --- Background CLI Thread ---
void cli_listener() {
    char input[32] = {0};
    std::puts("\n💻 CLI Ready. Commands: 'h' (Handshake), 'ack' (Approve Buy), 'exit' (Quit)");
    
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
            else if (std::strcmp(input, "exit") == 0) {
                std::puts("[CLI] Shutting down...");
                is_running = false;
            }
        }
    }
}

// --- Main execution ---
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::puts("[ERROR] Missing COM Port.\nUsage: ./ground_station <PORT> (e.g. /dev/tty.usbmodem14101 or COM3)");
        return 1;
    }

    if (!serial_open(argv[1], 115200)) {
        std::printf("[ERROR] Failed to connect to %s\n", argv[1]);
        return 1;
    }
    std::printf("[SYSTEM] Connected to hardware on %s\n", argv[1]);

    // Spawn the CLI in the background
    std::thread cli_thread(cli_listener);

    // Buffers for reading USB Serial streams
    uint8_t rx_buf[256];
    char line_buf[512] = {0};
    size_t line_idx = 0;

    // --- The Main Hardware Polling Loop ---
    while (is_running) {
        int bytes = serial_read_bytes(rx_buf, sizeof(rx_buf));
        
        for (int i = 0; i < bytes; i++) {
            char c = static_cast<char>(rx_buf[i]);
            if (c == '\n' || c == '\r') {
                if (line_idx > 0) {
                    line_buf[line_idx] = '\0'; // Null terminate
                    
                    // 1. Detect Packet B (Encrypted Payment)
                    if (std::strncmp(line_buf, "PACKET_B:", 9) == 0) {
                        std::puts("\n[HARDWARE] 📦 Received PACKET B from Sat B. Routing to Bouncer...");
                        
                        uint8_t packet_bin[176]; // SIZE_PACKET_B
                        uint8_t cleartext_out[256];
                        
                        hex_to_bin(&line_buf[9], packet_bin, sizeof(packet_bin));
                        
                        // Let the Bouncer validate the crypto & structural limits
                        if (edge_firewall.process_packet(packet_bin, sizeof(packet_bin), cleartext_out, sizeof(cleartext_out))) {
                            std::puts("[BOUNCER] ✅ Signature Valid. Decryption Success. Ready for L2 Settlement.");
                        } else {
                            std::puts("[BOUNCER] ❌ Threat Detected. Packet Dropped.");
                        }
                    }
                    // 2. Detect Invoice
                    else if (std::strncmp(line_buf, "INVOICE:", 8) == 0) {
                        std::puts("\n[HARDWARE] 📄 Received Packat A (Invoice). Awaiting 'ack' command.");
                    }
                    
                    line_idx = 0; // Reset buffer
                }
            } else if (line_idx < sizeof(line_buf) - 1) {
                line_buf[line_idx++] = c;
            }
        }
        
        // Sleep for 10ms to prevent CPU pegging (100% usage)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    cli_thread.detach(); 
    serial_close();
    std::puts("[SYSTEM] Hardware safely disconnected.");
    return 0;
}