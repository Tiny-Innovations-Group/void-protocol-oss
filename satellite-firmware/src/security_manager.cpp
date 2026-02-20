/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      security_manager.cpp
 * Desc:      Security Manager for VOID Protocol Satellite Firmware (Ed25519/X25519/ChaCha20).  
 * Compliant: NSA Clean C++ (RAII, No-Heap, Forward Secrecy).  
 * -------------------------------------------------------------------------*/

#include "security_manager.h"

SecurityManager Security;

SecurityManager::SecurityManager() : _state(SESSION_IDLE) {}


bool SecurityManager::begin() {
    if (sodium_init() < 0) return false;

    // Simulate Unique Factory Injected PUFs based on Role
    const char* env_key;
    #ifdef ROLE_SELLER
        // Unique silicon simulation for Sat A !!FAKE KEY FOR DEMO ONLY!!
        env_key = "Q9p4M&JjEUrmf3fW$i5AfWkWNREpEN8OYHXUg6F0^R9O2nm5EoQor%mtlcVCyLlC"; 
    #else
        // Unique silicon simulation for Sat B !!FAKE KEY FOR DEMO ONLY!!
        env_key = "!1V2#E!UDzTeUjEvqYLbHt8BFnsnlWsIQPrl44zmPZZ8bSkqwng&JPQJQ2QHZVIx";
    #endif
    
    uint8_t seed[crypto_sign_SEEDBYTES];
    crypto_hash_sha256(seed, reinterpret_cast<const uint8_t*>(env_key), strlen(env_key));

    crypto_sign_seed_keypair(_identity_pub, _identity_priv, seed);
    sodium_memzero(seed, sizeof(seed));

    return true;
}



// --- PHASE 2: HANDSHAKE (Sat B -> Ground) ---
void SecurityManager::prepareHandshake(PacketH_t& pkt, uint16_t ttl_seconds) {
    // 1. Generate Ephemeral X25519 Keypair (The "Throwaway" Keys)
    crypto_box_keypair(_eph_pub, _eph_priv);

    // 2. Set State
    _state = SESSION_HANDSHAKE_INIT;
    _session_ttl = ttl_seconds;
    _session_start_ts = millis();

    // 3. Populate Packet Header
    pkt.header.ver_type_sec = 0x18; // Version 0, Type 1, Sec 1, APID (Upper)
    pkt.header.apid_lo = 0xB2;      // APID: Sat B
    pkt.header.seq_flags = 0xC0;    // Unsegmented (11)
    pkt.header.seq_count_lo = 0x00; // Counter (Mock)

    // Packet Length: Swap to Big-Endian
    uint16_t raw_len = SIZE_PACKET_H - 1;
    pkt.header.packet_len = (raw_len >> 8) | (raw_len << 8);

    // 4. Fill Data
    pkt.session_ttl = ttl_seconds;
    pkt.timestamp = millis(); // Demo: Use millis. Prod: Use GPS Epoch.
    memcpy(pkt.eph_pub_key, _eph_pub, 32);

    // 5. SIGNATURE (Identity binds the Ephemeral Key)
    // Sign bytes 0 to 47 (Header + TTL + TS + PubKey)
    unsigned long long sig_len;
    // crypto_sign_detached(pkt.signature, &sig_len, (uint8_t*)&pkt, 48, _identity_priv);
    crypto_sign_detached(pkt.signature, &sig_len, reinterpret_cast<uint8_t*>(&pkt), 48, _identity_priv);
    // Defensive: Ensure signature length is as expected (Ed25519 = 64 bytes)
    if (sig_len != 64) {
        // Handle error: signature length mismatch (could log, assert, or set error state)
        // For embedded, a simple infinite loop is a safe fail-stop
        while (1) {}
    }
}

// --- PHASE 2: RESPONSE (Ground -> Sat B) ---
bool SecurityManager::processHandshakeResponse(const PacketH_t& pkt_in) {
    // 1. Verify Ground Signature (Validation)
    // For demo, we assume Ground is trusted if sig validates.  
    // For demo, we skip checking the Ground's signature against a root CA.
    // In Prod, we check pkt_in.signature against a stored Ground Public Key.
    
    // 2. DERIVE SESSION KEY (ECDH)
    // Sat B Private + Ground Public = Shared Secret
    if (crypto_scalarmult(_session_key, _eph_priv, pkt_in.eph_pub_key) != 0) {
        return false; // Math failed (e.g. weak point)
    }

    // 3. Hash the raw ECDH output to get a clean ChaCha20 Key
    // Use Generic Hash (BLAKE2b)
    crypto_generichash(_session_key, 32, _session_key, 32, NULL, 0);

    // 4. CLEANUP (Forward Secrecy)
    // Destroy the private ephemeral key immediately!
    sodium_memzero(_eph_priv, sizeof(_eph_priv));
    
    _state = SESSION_ACTIVE;
    return true;
}

// --- PHASE 3: ENCRYPT PACKET B (Payment) ---
void SecurityManager::encryptPacketB(PacketB_t& pkt, const uint8_t* payload_in, size_t len) {
    if (_state != SESSION_ACTIVE) return;

    // 1. Generate Nonce (Random 12 bytes) or Counter-based
    uint8_t nonce[12];
    randombytes_buf(nonce, 12);
    
    // Copy Nonce to packet (4 bytes used in struct, usually extended in prod)
    // For MVP struct `nonce` is u32, so we use first 4 bytes
    memcpy(&pkt.nonce, nonce, 4); 

    // 2. Encrypt (ChaCha20-Poly1305 or XChaCha20)
    // We use ChaCha20 stream (no auth tag here, as Packet B has Outer Sig)
    crypto_stream_chacha20_xor(pkt.enc_payload, payload_in, len, nonce, _session_key);

    // 3. SIGN THE OUTER PACKET (PUF)
    // Sign everything from Header to Nonce (Offset 0 to 107)
    unsigned long long sig_len;
    // crypto_sign_detached(pkt.signature, &sig_len, (uint8_t*)&pkt, 108, _identity_priv);
    crypto_sign_detached(pkt.signature, &sig_len, reinterpret_cast<uint8_t*>(&pkt), 108, _identity_priv);
    // Defensive: Ensure signature length is as expected (Ed25519 = 64 bytes)
    if (sig_len != 64) {
        while (1) {}
    }
}

// --- UTILITIES ---
bool SecurityManager::isSessionActive() {
    if (_state != SESSION_ACTIVE) return false;
    // Check TTL
    if ((millis() - _session_start_ts) > (_session_ttl * 1000)) {
        wipeSession(); // Time's up!
        return false;
    }
    return true;
}

void SecurityManager::wipeSession() {
    sodium_memzero(_session_key, 32);
    sodium_memzero(_eph_priv, 32); // Just in case
    _state = SESSION_IDLE;
}