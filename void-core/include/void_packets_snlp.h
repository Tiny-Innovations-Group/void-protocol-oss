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

#ifndef VOID_SNLP_PACKETS_H
#define VOID_SNLP_PACKETS_H

#include "void_types.h"
#include "void_packets.h" // For size constants

// Enforce 1-byte alignment for wire-format structures
#pragma pack(push, 1)

/* --------------------------------------------------------------------------
 * LORA HEADER
 * -------------------------------------------------------------------------- */

/**
 * @brief Standard SNLP sync word + alignment tail + CCSDS Primary Header (12 Bytes)
 * @note  NETWORK BYTE ORDER (Big-Endian)
 */
typedef struct __attribute__((packed)) {
    uint32_t sync_word;         // 00-03: 0x1D01A5A5
    uint8_t  ver_type_sec;      // Version(3) | Type(1) | SecHead(1) | APID_Hi(3)
    uint8_t  apid_lo;           // APID_Lo(8)
    uint8_t  seq_flags;         // Flags(2) | Count_Hi(6)
    uint8_t  seq_count_lo;      // Count_Lo(8)
    uint16_t packet_len;        // Total Length - 1
    uint32_t align_pad;        // Alignment Padding so all packetes are still optimised for 32/64-bit access
} VoidHeader_t;

static_assert(sizeof(VoidHeader_t) == SIZE_SNLP_HEADER, "VoidHeader_t SNLP size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 1: HANDSHAKE (Packet H)
 * -------------------------------------------------------------------------- */
 
/**
 * @brief Packet H: Ephemeral Key Exchange
 * @size  120 Bytes
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
 * @size  80 Bytes (VOID-114B: body 62→66 with _pad_head + _pre_crc)
 * @cite  Protocol-spec-SNLP.md / VOID_114B_BODY_ALIGNMENT_2026-04-14.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-13: SNLP Header (14B)
    uint16_t     _pad_head;     // 14-15: Alignment (VOID-114B)
    uint64_t     epoch_ts;      // 16-23: Little-Endian (8-aligned ✅)
    double       pos_vec[3];    // 24-47: Little-Endian (IEEE 754 f64, 8-aligned ✅)
    float        vel_vec[3];    // 48-59: Little-Endian (IEEE 754 f32, 4-aligned ✅)
    uint32_t     sat_id;        // 60-63: Little-Endian (4-aligned ✅)
    uint64_t     amount;        // 64-71: Little-Endian (8-aligned ✅)
    uint16_t     asset_id;      // 72-73: Little-Endian
    uint16_t     _pre_crc;      // 74-75: Alignment (VOID-114B)
    uint32_t     crc32;         // 76-79: Little-Endian (4-aligned ✅)
} PacketA_t;

static_assert(sizeof(PacketA_t) == SIZE_PACKET_A, "PacketA_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 3: PAYMENT (Packet B)
 * -------------------------------------------------------------------------- */

/**
 * @brief Packet B: Encrypted Payment
 * @size  192 Bytes (VOID-114B: body 178 = 174 + _tail_pad[4], frame ÷64 ✅)
 * @cite  Protocol-spec-SNLP.md §3 / VOID_114B_BODY_ALIGNMENT_2026-04-14.md
 *
 * VOID-110: nonce is NOT transmitted. SNLP runs in plaintext mode by
 * default. When VOID_ENCRYPT_SNLP is defined the ChaCha20 nonce is
 * derived identically to CCSDS: nonce[12] = sat_id[4] || epoch_ts[8].
 *
 * VOID-114B: body reshaped so every critical field is naturally aligned
 * under the 14-byte SNLP header (6 ≡ 14 mod 8).
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-13: SNLP Header
    uint16_t     _pad_head;     // 14-15: Alignment (VOID-114B)
    uint64_t     epoch_ts;      // 16-23: Little-Endian (8-aligned ✅)
    double       pos_vec[3];    // 24-47: Little-Endian (8-aligned ✅)
    uint8_t      enc_payload[62]; // 48-109: ChaCha20 Ciphertext / plaintext
    uint16_t     _pre_sat;      // 110-111: Alignment (VOID-114B)
    uint32_t     sat_id;        // 112-115: Little-Endian (4-aligned ✅)
    uint32_t     _pre_sig;      // 116-119: Alignment (VOID-114B)
    uint8_t      signature[64];   // 120-183: Ed25519 Signature (8-aligned ✅)
    uint32_t     global_crc;    // 184-187: Little-Endian (4-aligned ✅)
    uint8_t      _tail_pad[4]; // 188-191: Tail pad — frame total 192 (÷64 ✅ cache line)
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
 * @size  96 Bytes
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
 * @size  128 Bytes
 * @cite  Acknowledgement-spec.md
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // 00-05: Big-Endian
    uint16_t     _pad_a;        // 06-07: Alignment
    uint32_t     target_tx_id;  // 08-11: Little-Endian (Nonce Match)
    uint8_t      status;        // 12: 0x01=Settled
    uint8_t      _pad_b;        // 13: Data boundary
    RelayOps_t   relay_ops;     // 14-25: Relay Instructions
    uint8_t      enc_tunnel[96]; // 26-121: Encrypted Tunnel Data
    uint16_t     _pad_c;        // 114-115: Alignment
    uint32_t     crc32;         // 116-119: Outer Checksum
} PacketAck_t;

static_assert(sizeof(PacketAck_t) == SIZE_PACKET_ACK, "PacketAck_t size mismatch");

/* --------------------------------------------------------------------------
 * PHASE 5: RECEIPT & DELIVERY (Packet C & D)
 * -------------------------------------------------------------------------- */

/**
 * @brief Packet C: The Receipt
 * @size  112 Bytes
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
 * @size  136 Bytes
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


/*
 * @brief Packet L: Life/Heartbeat
 * @size  40 Bytes (CCSDS) / 48 Bytes (SNLP)
 * @cite  VOID_114B_BODY_ALIGNMENT_2026-04-14.md
 *
 * VOID-114B: Body reordered so all critical fields land on natural
 * alignment. The legacy reserved[2] field is dropped (unused) and
 * _pad_head takes its place as the forward-compat slot. Total frame
 * size is unchanged.
 */
typedef struct __attribute__((packed)) {
    VoidHeader_t header;        // Polymorphic (6B or 14B)

    uint16_t     _pad_head;     // 00-01: Alignment (VOID-114B)
    uint64_t     epoch_ts;      // 02-09: Unix ts (8-aligned ✅)
    uint32_t     pressure_pa;   // 10-13: Pressure Pa (4-aligned ✅)
    int32_t      lat_fixed;     // 14-17: Lat * 10^7 (4-aligned ✅)
    int32_t      lon_fixed;     // 18-21: Lon * 10^7 (4-aligned ✅)
    uint16_t     vbatt_mv;      // 22-23: Battery mV (2-aligned ✅)
    int16_t      temp_c;        // 24-25: Temp centidegrees (2-aligned ✅)
    uint16_t     gps_speed_cms; // 26-27: Speed cm/s (2-aligned ✅)
    uint8_t      sys_state;     // 28:    State ID
    uint8_t      sat_lock;      // 29:    GPS Lock Count
    uint32_t     crc32;         // 30-33: Checksum (4-aligned ✅)
} HeartbeatPacket_t;

static_assert(sizeof(HeartbeatPacket_t) == SIZE_HEARTBEAT_PCK, "HeartbeatPacket_t size mismatch");

// Restore default alignment
#pragma pack(pop)

#endif // VOID_SNLP_PACKETS_H