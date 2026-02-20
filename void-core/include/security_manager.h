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

public:
    SecurityManager();

    // 1. Initialization
    bool begin(); // Loads Identity Keys
    
    // 2. Handshake Logic (Packet H)
    // Generates the "Hello" packet to start a session
    void prepareHandshake(PacketH_t& pkt_out, uint16_t ttl_seconds, uint64_t current_time_ms);
    
    // Processes the response from Ground and derives the Session Key
    bool processHandshakeResponse(const PacketH_t& pkt_in);

    // 3. Encryption / Decryption
    // Encrypts the Payment Payload (Packet B)
    void encryptPacketB(PacketB_t& pkt, const uint8_t* payload_in, size_t len);
    
    // Decrypts the Tunnel Data (Packet Ack)
    // Returns true if signature and decryption are valid
    bool decryptTunnel(const uint8_t* cipher_in, size_t len, uint8_t* plain_out);

    // 4. Utility
    bool isSessionActive(uint64_t current_time_ms);
    void wipeSession(); // Secure zeroing of keys
};

// Singleton Instance
extern SecurityManager Security;

#endif