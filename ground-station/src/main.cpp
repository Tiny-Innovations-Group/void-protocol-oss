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
#include "egress_poll_client.h"
#include "egress_orchestrator.h"
#include "ack_builder.h"

// --- Global State ---
// Stack-allocated modules (No Heap) as per .cursorrules
std::atomic<bool> is_running{true};
Bouncer edge_firewall;
GatewayClient go_gateway("127.0.0.1", 8080);

// VOID-138: egress poll client pointed at the same gateway as
// `go_gateway`. Constructed with the same host/port so a local-Anvil
// flat-sat invocation "just works" without env-var fiddling. Tuneable
// via VOID_GATEWAY_HOST / VOID_GATEWAY_PORT at runtime below.
egress::EgressPollClient egress_client("127.0.0.1", 8080);

// VOID-138: LoRa TX callback. For flat-sat, the bouncer hands the
// 112-byte PacketC frame to the satellite firmware over USB-serial
// using a PACKET_C_TX:<hex>\n command line — the firmware's LoRa
// radio driver does the actual RF TX. Returns true iff the serial
// write succeeded (which is the bouncer's completion signal; the
// firmware-side LoRa TX may still fail independently, and that's a
// future ticket's concern).
static bool lora_tx_via_serial(const uint8_t* data, size_t len, void* /*user*/) {
    // Re-encode the decoded PacketC back into ASCII hex so the firmware
    // receives the same line format it already handles for other packet
    // types (INVOICE:, PACKET_B:, etc.).
    // The orchestrator decoded to bytes so the production path stays
    // agnostic of transport; re-encoding here owns the serial-line
    // convention used by the existing firmware.
    static constexpr size_t kPrefixLen = 13; // "PACKET_C_TX:"
    static constexpr size_t kMaxHex    = egress::EgressPacketCSize * 2;
    static constexpr size_t kLineCap   = kPrefixLen + kMaxHex + 2; // +\n +\0
    char line[kLineCap];
    std::snprintf(line, sizeof(line), "PACKET_C_TX:");
    static const char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < len && i < egress::EgressPacketCSize; ++i) {
        line[kPrefixLen + i * 2]     = kDigits[(data[i] >> 4) & 0x0Fu];
        line[kPrefixLen + i * 2 + 1] = kDigits[data[i] & 0x0Fu];
    }
    const size_t line_len = kPrefixLen + len * 2u;
    line[line_len]     = '\n';
    line[line_len + 1] = '\0';

    const int n = serial_write_bytes(reinterpret_cast<const uint8_t*>(line),
                                     line_len + 1);
    return n >= 0;
}

// VOID-134: emit a 136-byte SNLP PacketAck frame as "PACKET_ACK_TX:<hex>\n"
// so the firmware-side Heltec LoRa-transmits it back down to Sat B.
// Mirrors the VOID-138 PACKET_C_TX: serial-line convention. Returns
// true iff the serial write succeeded; radio-side TX failure is a
// separate concern (no retry in alpha per the spec).
static bool lora_tx_ack_via_serial(const uint8_t* data, size_t len) {
    static constexpr char   kPrefix[]  = "PACKET_ACK_TX:";
    static constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    static constexpr size_t kMaxHex    = ack_builder::kPacketAckSize * 2;
    static constexpr size_t kLineCap   = kPrefixLen + kMaxHex + 2; // +\n +\0

    if (len != ack_builder::kPacketAckSize) return false;

    char line[kLineCap];
    std::memcpy(line, kPrefix, kPrefixLen);
    static const char kDigits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        line[kPrefixLen + i * 2]     = kDigits[(data[i] >> 4) & 0x0Fu];
        line[kPrefixLen + i * 2 + 1] = kDigits[data[i] & 0x0Fu];
    }
    const size_t line_len = kPrefixLen + len * 2u;
    line[line_len]     = '\n';
    line[line_len + 1] = '\0';

    const int n = serial_write_bytes(reinterpret_cast<const uint8_t*>(line),
                                     line_len + 1);
    return n >= 0;
}

// VOID-134: PacketB.sat_id lives at SNLP offset 112 as a little-endian
// uint32 (see void_packets_snlp.h::PacketB_t). Pulling it by explicit
// byte-shift avoids any misaligned-pointer cast and is endian-safe.
static uint32_t extract_packet_b_sat_id_snlp(const uint8_t* pkt) {
    return  static_cast<uint32_t>(pkt[112])
         | (static_cast<uint32_t>(pkt[113]) <<  8)
         | (static_cast<uint32_t>(pkt[114]) << 16)
         | (static_cast<uint32_t>(pkt[115]) << 24);
}

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

// VOID-138: egress poll thread. Drains pending receipts from the Go
// gateway every `interval_ms` and dispatches each PacketC via LoRa
// (through the serial HAL). Exits cleanly on `is_running = false`.
//
// Env tuning:
//   VOID_EGRESS_POLL_MS   — poll interval (default 1000 ms)
//   VOID_EGRESS_DISABLED  — set to "1" to skip the thread entirely
//
// Non-fatal errors are logged and the loop continues — the gateway's
// PENDING state is authoritative, so a transient failure here just
// means the record stays PENDING and gets re-offered next tick.
void egress_poll_listener() {
    const char* disabled = std::getenv("VOID_EGRESS_DISABLED");
    if (disabled != nullptr && std::strcmp(disabled, "1") == 0) {
        std::puts("[EGRESS] VOID_EGRESS_DISABLED=1 — poll thread not started.");
        return;
    }

    // Interval: parse env var if set, else default 1000 ms.
    unsigned interval_ms = 1000;
    if (const char* iv = std::getenv("VOID_EGRESS_POLL_MS")) {
        char* endp = nullptr;
        const long v = std::strtol(iv, &endp, 10);
        if (endp != nullptr && *endp == '\0' && v > 0 && v <= 60000) {
            interval_ms = static_cast<unsigned>(v);
        }
    }

    egress::EgressOrchestrator<egress::EgressPollClient> orch(
        egress_client, lora_tx_via_serial, nullptr);
    std::printf("[EGRESS] 🔁 Polling gateway for pending receipts every %u ms.\n",
                interval_ms);

    while (is_running) {
        const int dispatched = orch.tick();
        if (dispatched > 0) {
            std::printf("[EGRESS] ✅ Dispatched %d receipt(s) this tick.\n",
                        dispatched);
        } else if (dispatched < 0) {
            // Transport or parse error — wait out the tick and retry.
            // Common during startup before the gateway has its HTTP
            // listener up.
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
    std::puts("[EGRESS] Shutting down poll thread.");
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
                const char* cmd = "H\n";
                serial_write_bytes(reinterpret_cast<const uint8_t*>(cmd), std::strlen(cmd));
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

    // VOID-138: spawn the egress poll thread. Joined below on shutdown.
    std::thread egress_thread(egress_poll_listener);

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
                            
                            uint8_t packet_bin[SIZE_PACKET_B]; 
                            uint8_t cleartext_out[256];
                            
                            hex_to_bin(&line_buf[9], packet_bin, sizeof(packet_bin));
                            
                            // Let the Bouncer validate the crypto & structural limits
                            if (edge_firewall.process_packet(packet_bin, sizeof(packet_bin), cleartext_out, sizeof(cleartext_out))) {
                                std::puts("[BOUNCER] ✅ Signature Valid. Decryption Success.");

                                // VOID-134: emit PacketAck on the downlink independently
                                // of gateway delivery. Per Acknowledgement-spec, the ACK
                                // confirms reception — gateway/L2 settlement is a later
                                // phase and failing to reach L2 must not suppress it.
                                // Fire-and-forget, no retry (alpha).
                                ack_builder::AckInputs ack_in = {};
                                ack_in.target_tx_id = extract_packet_b_sat_id_snlp(packet_bin);
                                ack_in.status       = ack_builder::kAckStatusVerified;
                                ack_in.azimuth      = 180;        // flat-sat fixed pointing
                                ack_in.elevation    = 45;
                                ack_in.frequency_hz = 437200000u; // 437.2 MHz ISM
                                ack_in.duration_ms  = 5000u;
                                // enc_tunnel left zero-filled: plaintext alpha has no
                                // on-chain UNLOCK sig to carry (VOID-134 non-goal).

                                uint8_t ack_frame[ack_builder::kPacketAckSize];
                                if (ack_builder::build(ack_in, ack_frame, sizeof(ack_frame)) &&
                                    lora_tx_ack_via_serial(ack_frame, sizeof(ack_frame))) {
                                    std::puts("[ACK] ✅ PacketAck emitted over LoRa downlink.");
                                } else {
                                    std::puts("[ACK] ⚠️  PacketAck emit failed (non-fatal).");
                                }

                                // Push the LIVE hardware packet to the Go Gateway
                                if (go_gateway.push_to_l2(cleartext_out, 62)) {
                                    std::puts("[GATEWAY] ✅ Live hardware payload delivered to Gateway.");
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
    // VOID-138: egress poll thread watches `is_running` and exits
    // promptly on shutdown. Join rather than detach so its last log
    // line lands before main returns.
    if (egress_thread.joinable()) egress_thread.join();
    if (hardware_connected) serial_close();
    std::puts("[SYSTEM] Ground Station shut down securely.");
    return 0;
}