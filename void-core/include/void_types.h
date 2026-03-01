/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      void_types.h
 * Desc:      Protocol constants and primitive definitions.
 * -------------------------------------------------------------------------*/

#ifndef VOID_TYPES_H
#define VOID_TYPES_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------
// BUILD CONFIGURATION
// ---------------------------------------------------------
// Uncomment the target network for this specific build:
// #define VOID_NETWORK_CCSDS  1   // For S-Band
#define VOID_NETWORK_SNLP 1        // For LoRa

/* --- PROTOCOL CONSTANTS --- */
#define VOID_PROTOCOL_VERSION   0x01
#define VOID_MAX_PACKET_SIZE    184     // Max MTU (Packet B)
#define VOID_SESSION_TTL_DEF    600     // Default session window (10 mins)

/* --- CRYPTO SIZES --- */
#define CRYPTO_KEY_SIZE         32      // ChaCha20/X25519 Key Bytes
#define CRYPTO_SIG_SIZE         64      // Ed25519/PUF Signature Bytes
#define CRYPTO_NONCE_SIZE       12      // ChaCha20 Nonce (Constructed)
#define CRYPTO_HASH_SIZE        32      // SHA-256 Digest

/* --- CCSDS APID MASKS --- */
#define CCSDS_VER_MASK          0xE0    // Version 1 (bits 0-2)
#define CCSDS_TYPE_MASK         0x10    // Type (bit 3)
#define CCSDS_SEC_MASK          0x08    // Secondary Header (bit 4)
#define CCSDS_APID_MASK         0x07FF  // APID (bits 5-15)

#endif // VOID_TYPES_H