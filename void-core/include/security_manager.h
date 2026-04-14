/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      security_manager.h
 * Desc:      Security manager for VOID Protocol Satellite.
 *            Handles key management, session state, and encryption/decryption.
 * -------------------------------------------------------------------------*/

#ifndef SECURITY_MANAGER_H
#define SECURITY_MANAGER_H

#include "void_packets.h"
#include <sodium.h>


// NSA Guideline: Explicit State Management
enum SessionState {
    SESSION_IDLE = 0,
    SESSION_HANDSHAKE_INIT,
    SESSION_HANDSHAKE_WAIT,
    SESSION_ACTIVE,
    SESSION_LOCKED
};

class SecurityManager {
private:
    // --- IDENTITY (PERSISTENT) ---
    // In production, these come from PUF/NVS. For Demo, we hardcode or generate.
    uint8_t _identity_pub[32];
    uint8_t _identity_priv[64];

    // --- SESSION (EPHEMERAL) ---
    uint8_t _eph_pub[32];
    uint8_t _eph_priv[32];
    uint8_t _session_key[32];

    SessionState _state;
    uint64_t     _session_start_ts;
    uint32_t     _session_ttl;

    // --- MONOTONIC EPOCH GUARDRAIL (VOID-110) ---
    // Tracks the highest epoch_ms ever used to derive a ChaCha20 nonce.
    // Persisted to NVS on every N transmits and reloaded on boot.
    // Without this, a clock rollback would cause nonce reuse with the
    // same session key and fully break ChaCha20.
    uint64_t _last_tx_epoch_ms;
    bool     _gps_time_valid;

public:
    SecurityManager();

    // 1. Initialization
    bool begin(); // Loads Identity Keys + reloads _last_tx_epoch_ms from NVS

    // 2. Handshake Logic (Packet H)
    // Generates the "Hello" packet to start a session
    void prepareHandshake(PacketH_t& pkt_out, uint16_t ttl_seconds, uint64_t current_time_ms);

    // Processes the response from Ground and derives the Session Key
    bool processHandshakeResponse(const PacketH_t& pkt_in);

    // 3. Encryption / Decryption (VOID-110 — deterministic nonce)
    // Encrypts the Payment Payload (Packet B).
    // Caller MUST populate pkt.header, pkt.epoch_ts (ms), pkt.pos_vec, pkt.sat_id
    // BEFORE calling this. The nonce is derived internally from sat_id||epoch_ts.
    // Returns false if:
    //   - session is not ACTIVE
    //   - _gps_time_valid is false (GPS gate not lifted)
    //   - pkt.epoch_ts <= _last_tx_epoch_ms (monotonic guardrail tripped)
    //   - len > sizeof(pkt.enc_payload)
    bool encryptPacketB(PacketB_t& pkt, const uint8_t* payload_in, size_t len);

    // Decrypts the Tunnel Data (Packet Ack)
    // Returns true if signature and decryption are valid
    bool decryptTunnel(const uint8_t* cipher_in, size_t len, uint8_t* plain_out);

    // 4. Monotonic epoch guardrail API (VOID-110)
    // Called by main loop once GPS time is fixed and monotonic-vs-NVS check passes.
    void setGpsTimeValid(bool valid) { _gps_time_valid = valid; }
    bool isGpsTimeValid() const { return _gps_time_valid; }
    uint64_t lastTxEpochMs() const { return _last_tx_epoch_ms; }

    // 5. Utility
    bool isSessionActive(uint64_t current_time_ms);
    void wipeSession(); // Secure zeroing of keys
};

// Singleton Instance
extern SecurityManager Security;

#endif