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

/* --- PROTOCOL CONSTANTS --- */
#define VOID_PROTOCOL_VERSION   0x01
#define VOID_MAX_PACKET_SIZE    176     // Max MTU (Packet B)
#define VOID_SESSION_TTL_DEF    600     // Default session window (10 mins)

/* --- CRYPTO SIZES --- */
#define CRYPTO_KEY_SIZE         32      // ChaCha20/X25519 Key Bytes
#define CRYPTO_SIG_SIZE         64      // Ed25519/PUF Signature Bytes
#define CRYPTO_NONCE_SIZE       12      // ChaCha20 Nonce (Constructed)
#define CRYPTO_HASH_SIZE        32      // SHA-256 Digest

/* --- PACKET SIZES (STRICT) --- */
#define SIZE_PACKET_A           68      // Invoice
#define SIZE_PACKET_B           176     // Payment
#define SIZE_PACKET_H           112     // Handshake
#define SIZE_PACKET_C           104     // Receipt
#define SIZE_PACKET_D           128     // Delivery
#define SIZE_PACKET_ACK         120     // Acknowledgement
#define SIZE_TUNNEL_DATA        88      // Tunnel Data (from PacketAck_t::enc_tunnel)
#define SIZE_VOID_HEADER         6       // CCSDS Primary Header

/* --- CCSDS APID MASKS --- */
#define CCSDS_VER_MASK          0xE0    // Version 1 (bits 0-2)
#define CCSDS_TYPE_MASK         0x10    // Type (bit 3)
#define CCSDS_SEC_MASK          0x08    // Secondary Header (bit 4)
#define CCSDS_APID_MASK         0x07FF  // APID (bits 5-15)

#endif // VOID_TYPES_H