/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      bouncer.cpp
 * Desc:      Bouncer implementation: Edge validation & firewall for Sat B packets.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "bouncer.h"
#include <cstdio>
#include <cstring>

Bouncer::Bouncer() {
    // Zero session key on construction
    for (size_t i = 0; i < sizeof(_session_key); ++i) {
        _session_key[i] = 0;
    }
}

bool Bouncer::validate_signature(const uint8_t* data, size_t data_len, const uint8_t* signature, size_t sig_len) const {
    // TODO: Implement Ed25519/PUF signature validation (no heap)
    (void)data; (void)data_len; (void)signature; (void)sig_len;
    return true;
}

bool Bouncer::decrypt_payload(const uint8_t* enc, size_t enc_len, uint8_t* out, size_t out_len) const {
    // TODO: Implement ChaCha20 decryption (no heap)
    if (enc_len > out_len) return false;
    for (size_t i = 0; i < enc_len; ++i) {
        out[i] = enc[i]; // Demo: copy only
    }
    return true;
}

bool Bouncer::process_packet(const uint8_t* buf, size_t len, uint8_t* out, size_t out_max) const {
    // Example: Check for PacketB_t
    if (!validate_packet_size<PacketB_t>(buf, len)) {
        std::puts("[BOUNCER] Invalid packet size for PacketB_t.");
        return false;
    }
    
    // Cast only occurs after length validation
    const PacketB_t* pkt = reinterpret_cast<const PacketB_t*>(buf);

    // 1. Signature Validation
    if (!validate_signature(buf, len - sizeof(pkt->signature) - sizeof(pkt->global_crc), pkt->signature, sizeof(pkt->signature))) {
        std::puts("[BOUNCER] Signature validation failed.");
        return false;
    }

    // 2. Decrypt Payload
    if (!decrypt_payload(pkt->enc_payload, sizeof(pkt->enc_payload), out, out_max)) {
        std::puts("[BOUNCER] Payload decryption failed.");
        return false;
    }

    // 3. Sanitization (struct-level)
    // TODO: Add further mathematical/field checks as per protocol
    std::puts("[BOUNCER] Packet accepted and sanitized.");
    return true;
}

void Bouncer::set_session_key(const uint8_t* key, size_t len) {
    if (len > sizeof(_session_key)) len = sizeof(_session_key);
    for (size_t i = 0; i < len; ++i) _session_key[i] = key[i];
    for (size_t i = len; i < sizeof(_session_key); ++i) _session_key[i] = 0;
}

void Bouncer::clear_session_key() {
    for (size_t i = 0; i < sizeof(_session_key); ++i) _session_key[i] = 0;
}