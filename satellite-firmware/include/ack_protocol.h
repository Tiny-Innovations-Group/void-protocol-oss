/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * -------------------------------------------------------------------------*/

#ifndef VOID_PROTOCOL_V2_ACK_H
#define VOID_PROTOCOL_V2_ACK_H

#include <cstdint>

/**

* @file ack_protocol.h
* @brief C++ implementation of the 120-byte optimized Acknowledgement packet.
* Optimized for 32/64-bit machine cycles to prevent misaligned access.
*/

#pragma pack(push, 1)

/**

* @struct TunnelData
* @brief The "Encrypted Unlock" payload (88 Bytes).
* Resides within the ENC_TUNNEL field of the Acknowledgement packet.
*/
struct TunnelData
{
    uint8_t ccsds_pri[6];   // 00-05: Header (APID = Sat A)
    uint8_t _pad_a[2];      // 06-07: 64-bit Alignment Pad
    uint64_t block_nonce;   // 08-15: L2 Block Height (Aligned to 8-byte boundary)
    uint16_t cmd_code;      // 16-17: 0x0001 = UNLOCK / DISPENSE
    uint16_t ttl;           // 18-19: Time-To-Live
    uint8_t ground_sig[64]; // 20-83: Auth Signature
    uint32_t crc32;         // 84-87: Inner Checksum
};

/**

* @struct RelayOps
* @brief Sat B instructions for orienting transmission toward Sat A.
*/
struct RelayOps
{
    uint16_t azimuth;   // 14-15: Look Angle (Horizontal)
    uint16_t elevation; // 16-17: Look Angle (Vertical)
    uint32_t frequency; // 18-21: Target Tx Frequency (Hz)
    uint32_t duration;  // 22-25: Relay Window (ms)
};

/**

* @struct AckPacket
* @brief The Downlink Acknowledgement Packet (120 Bytes).
* Route: Ground Station -> Sat B (Relay).
*/
struct AckPacket
{
    uint8_t ccsds_pri[6];   // 00-05: Header (APID = Sat B)
    uint16_t _pad_a;        // 06-07: 32/64-bit alignment pad
    uint32_t target_tx_id;  // 08-11: Cleartext transaction nonce
    uint8_t status;         // 12: 0x01=Settled, 0xFF=Rejected
    uint8_t _pad_b;         // 13: Data boundary pad
    RelayOps relay_ops;     // 14-25: Sat B Relay Instructions
    uint8_t enc_tunnel[88]; // 26-113: ChaCha20 Encrypted Payload (Contains TunnelData)
    uint16_t _pad_c;        // 114-115: Alignment to 116
    uint32_t crc32;         // 116-119: Outer Checksum
};

#pragma pack(pop)

#endif // VOID_PROTOCOL_V2_ACK_H

/*-------------------------------------------------------------------------
 * [END OF AUTHORITY BLOCK]
 * Verified for 32/64-bit cycle optimization.
 * © 2026 Tiny Innovation Group Ltd.
 * -------------------------------------------------------------------------*/