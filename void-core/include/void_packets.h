/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      void_packets.h
 * Desc:      Packed structures for OTA serialization.
 * -------------------------------------------------------------------------
 * WARNING: Payloads are Little-Endian. Headers are Big-Endian.
 * -------------------------------------------------------------------------*/

#ifndef VOID_PACKETS_H
#define VOID_PACKETS_H

#include "void_types.h"

// Enforce 1-byte alignment for wire-format structures
#pragma pack(push, 1)

/* --------------------------------------------------------------------------
 * COMMON HEADER
 * -------------------------------------------------------------------------- */

/**
 * @brief Standard CCSDS Primary Header (6 Bytes)
 * @note  NETWORK BYTE ORDER (Big-Endian)
 */
typedef struct __attribute__((packed)) {
    uint8_t  ver_type_sec;      // Version(3) | Type(1) | SecHead(1) | APID_Hi(3)
    uint8_t  apid_lo;           // APID_Lo(8)
    uint8_t  seq_flags;         // Flags(2) | Count_Hi(6)
    uint8_t  seq_count_lo;      // Count_Lo(8)
    uint16_t packet_len;        // Total Length - 1
} VoidHeader_t;

static_assert(sizeof(VoidHeader_t) == SIZE_VOID_HEADER, "VoidHeader_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 1: HANDSHAKE (Packet H)
 * -------------------------------------------------------------------------- */


 
/**
 * @brief Packet H: Ephemeral Key Exchange
 * @size  112 Bytes
 * @cite  Handshake-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint16_t     session_ttl;   // 06-07: Little-Endian (Alignment + Logic)
    uint64_t     timestamp;     // 08-15: Little-Endian (Unix Epoch)
    uint8_t      eph_pub_key[32]; // 16-47: X25519 Public Key
    uint8_t      signature[64];   // 48-111: Ed25519 Identity Sig
} PacketH_t;

static_assert(sizeof(PacketH_t) == SIZE_PACKET_H, "PacketH_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 2: INVOICE (Packet A)
 * -------------------------------------------------------------------------- */

/**
 * @brief Packet A: The Invoice
 * @size  68 Bytes
 * @cite  Protocol-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint64_t     epoch_ts;      // 06-13: Little-Endian
    double       pos_vec[3];    // 14-37: Little-Endian (IEEE 754 f64)
    float        vel_vec[3];    // 38-49: Little-Endian (IEEE 754 f32)
    uint32_t     sat_id;        // 50-53: Little-Endian
    uint64_t     amount;        // 54-61: Little-Endian
    uint16_t     asset_id;      // 62-63: Little-Endian
    uint32_t     crc32;         // 64-67: Little-Endian
} PacketA_t;

static_assert(sizeof(PacketA_t) == SIZE_PACKET_A, "PacketA_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 3: PAYMENT (Packet B)
 * -------------------------------------------------------------------------- */

/**
 * @brief Packet B: Encrypted Payment
 * @size  176 Bytes
 * @cite  Protocol-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint64_t     epoch_ts;      // 06-13: Little-Endian
    double       pos_vec[3];    // 14-37: Little-Endian
    uint8_t      enc_payload[62]; // 38-99: ChaCha20 Encrypted
    uint32_t     sat_id;        // 100-103: Little-Endian (Cleartext ID)
    uint32_t     nonce;         // 104-107: Little-Endian
    uint8_t      signature[64];   // 108-171: PUF Signature
    uint32_t     global_crc;    // 172-175: Little-Endian
} PacketB_t;

static_assert(sizeof(PacketB_t) == SIZE_PACKET_B, "PacketB_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 4: ACKNOWLEDGEMENT (Downlink)
 * -------------------------------------------------------------------------- */

/**
 * @brief Relay Instructions (Sub-struct)
 * @size  12 Bytes
 * @cite  Acknowledgement-spec.md
 */
typedef struct __attribute__((packed)) {
    uint16_t azimuth;
    uint16_t elevation;
    uint32_t frequency;
    uint32_t duration_ms;
} RelayOps_t;

/**
 * @brief Tunnel Data (The Encrypted Payload)
 * @size  88 Bytes
 * @note  This is the struct recovered after decrypting PacketAck_t::enc_tunnel
 * @cite  Acknowledgment-spec.md [Section C]
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian (APID=Sat A)
    uint8_t      _pad_a[2];     // 06-07: 64-bit Alignment Pad
    uint64_t     block_nonce;   // 08-15: Little-Endian (L2 Block Height)
    uint16_t     cmd_code;      // 16-17: Little-Endian (0x0001 = UNLOCK)
    uint16_t     ttl;           // 18-19: Little-Endian
    uint8_t      ground_sig[64]; // 20-83: Auth Signature
    uint32_t     crc32;         // 84-87: Little-Endian (Inner Checksum)
} TunnelData_t;

static_assert(sizeof(TunnelData_t) == SIZE_TUNNEL_DATA, "TunnelData_t size mismatch");

/**
 * @brief ACK Packet: Ground -> Sat B
 * @size  120 Bytes
 * @cite  Acknowledgement-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint16_t     _pad_a;        // 06-07: Alignment
    uint32_t     target_tx_id;  // 08-11: Little-Endian (Nonce Match)
    uint8_t      status;        // 12: 0x01=Settled
    uint8_t      _pad_b;        // 13: Data boundary
    RelayOps_t   relay_ops;     // 14-25: Relay Instructions
    uint8_t      enc_tunnel[88]; // 26-113: Encrypted Tunnel Data
    uint16_t     _pad_c;        // 114-115: Alignment
    uint32_t     crc32;         // 116-119: Outer Checksum
} PacketAck_t;

static_assert(sizeof(PacketAck_t) == SIZE_PACKET_ACK, "PacketAck_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 5: RECEIPT & DELIVERY (Packet C & D)
 * -------------------------------------------------------------------------- */

/**
 * @brief Packet C: The Receipt
 * @size  104 Bytes
 * @cite  Receipt-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint16_t     _pad_head;     // 06-07: Alignment
    uint64_t     exec_time;     // 08-15: Little-Endian
    uint64_t     enc_tx_id;     // 16-23: Encrypted
    uint8_t      enc_status;    // 24: Encrypted
    uint8_t      _pad_sig[7];   // 25-31: Alignment for Sig
    uint8_t      signature[64];   // 32-95: Sat A PUF Signature
    uint32_t     crc32;         // 96-99: Little-Endian
    uint8_t      _tail_pad[4];  // 100-103: Final Alignment
} PacketC_t;

static_assert(sizeof(PacketC_t) == SIZE_PACKET_C, "PacketC_t size mismatch");

/**
 * @brief Packet D: Delivery
 * @size  128 Bytes
 * @cite  Receipt-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint16_t     _pad_head;     // 06-07: Alignment
    uint64_t     downlink_ts;   // 08-15: Little-Endian
    uint32_t     sat_b_id;      // 16-19: Little-Endian
    uint8_t      payload[98];   // 20-117: Stripped Packet C
    uint32_t     global_crc;    // 118-121: Little-Endian
    uint8_t      _tail[6];      // 122-127: Final Alignment
} PacketD_t;

static_assert(sizeof(PacketD_t) == SIZE_PACKET_D, "PacketD_t size mismatch");

// Restore default alignment
#pragma pack(pop)

#endif // VOID_PACKETS_H