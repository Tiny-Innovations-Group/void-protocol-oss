/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      bouncer.h
 * Desc:      Bouncer definition: Edge validation & firewall for Sat B packets.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef BOUNCER_H
#define BOUNCER_H

#include <cstdint>
#include <cstddef>
// CMake target_include_directories handles the path resolution
#include "void_packets.h" 

class Bouncer {
private:
    uint8_t _session_key[32];

public:
    Bouncer();

    // --- Signature Validation ---
    bool validate_signature(const uint8_t* data, size_t data_len, const uint8_t* signature, size_t sig_len) const;
    
    // --- Decrypt Payload ---
    bool decrypt_payload(const uint8_t* enc, size_t enc_len, uint8_t* out, size_t out_len) const;

    // --- Packet Structure Validation ---
    // Template methods must be implemented in the header
    template<typename T>
    bool validate_packet_size(const uint8_t* buf, size_t len) const {
        (void)buf;
        return len == sizeof(T);
    }

    // --- Bouncer Main Entry ---
    bool process_packet(const uint8_t* buf, size_t len, uint8_t* out, size_t out_max) const;

    // --- Session Key Management ---
    void set_session_key(const uint8_t* key, size_t len);
    void clear_session_key();
};

#endif // BOUNCER_H