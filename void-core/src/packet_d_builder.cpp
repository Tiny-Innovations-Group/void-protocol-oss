/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      packet_d_builder.cpp
 * Desc:      VOID-136 pure-function PacketD (Delivery) builder, SNLP tier.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "packet_d_builder.h"

#include <cstring>

namespace packet_d_builder {
namespace {

// Wire constants locked by the SNLP spec + VOID-123 determinism.
// Matches gateway/test/utils/generate_packets.go::buildHeader when
// (isSnlp=true, isCmd=false) with apid = apidSatB (101).
//
// Note: PacketD is addressed TO Sat B (the buyer) so APID=101, and
// it is a telemetry/data packet (not a command) so pkt_type=0. This
// is the **only** difference vs the PacketAck header — ACK uses
// pkt_type=1 because the bouncer sends a command-class downlink.
constexpr uint32_t kSnlpSyncWord = 0x1D01A5A5u; // VOID-113
constexpr uint16_t kApidSatB     = 101u;
constexpr uint16_t kSeqFlags     = 0xC000u;     // hardcoded per generator
constexpr uint16_t kSnlpDBodyLen = 122u;        // 136 total - 14 header

// Bit-packing of the CCSDS-style identification field embedded in the
// SNLP header: version(3) | pkt_type(1) | sec_flag(1) | apid(11).
// pkt_type = 0 (data, not command); sec_flag = 1.
constexpr uint16_t IdField(uint16_t apid) {
    return static_cast<uint16_t>((1u << 11) | (apid & 0x7FFu));
}

void WriteU32LE(uint8_t* dst, size_t& off, uint32_t v) {
    dst[off++] = static_cast<uint8_t>(v & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}
void WriteU64LE(uint8_t* dst, size_t& off, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        dst[off++] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
    }
}
void WriteU16BE(uint8_t* dst, size_t& off, uint16_t v) {
    dst[off++] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[off++] = static_cast<uint8_t>(v & 0xFFu);
}
void WriteU32BE(uint8_t* dst, size_t& off, uint32_t v) {
    dst[off++] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[off++] = static_cast<uint8_t>(v & 0xFFu);
}

// Table-free IEEE-802.3 CRC-32 — byte-identical to Go's
// hash/crc32.ChecksumIEEE. Same helper is embedded in
// ground-station/src/ack_builder.cpp and in void-core/test/
// test_sign_verify.cpp; we keep a third copy here so void-core's
// runtime library stays standalone (no cross-module link).
uint32_t Crc32Ieee(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = static_cast<uint32_t>(
                -static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

}  // namespace

bool build(const DeliveryInputs& in, uint8_t* out, size_t out_cap) {
    if (out == nullptr || out_cap < kPacketDSize) return false;

    std::memset(out, 0, kPacketDSize);
    size_t off = 0;

    // --- 14-byte SNLP header (Big-Endian) ---
    WriteU32BE(out, off, kSnlpSyncWord);                            // 00-03
    WriteU16BE(out, off, IdField(kApidSatB));                       // 04-05
    WriteU16BE(out, off, kSeqFlags);                                // 06-07
    WriteU16BE(out, off, static_cast<uint16_t>(kSnlpDBodyLen - 1u));// 08-09
    WriteU32BE(out, off, 0u);                                       // 10-13 align_pad

    // --- 122-byte body (Little-Endian) ---
    out[off++] = kPacketDMagic;                                     // 14
    out[off++] = 0u;                                                // 15 _pad_head
    WriteU64LE(out, off, in.downlink_ts);                           // 16-23
    WriteU32LE(out, off, in.sat_b_id);                              // 24-27
    std::memcpy(&out[off], in.payload, kPayloadSize);               // 28-125
    off += kPayloadSize;

    // --- CRC32 over everything written so far (header + body-ex-CRC-ex-tail) ---
    const uint32_t crc = Crc32Ieee(out, off);
    WriteU32LE(out, off, crc);                                      // 126-129

    // --- 6-byte tail pad (already zeroed by the memset, but advance the
    //     cursor so the final assertion is honest) ---
    off += 6;                                                       // 130-135

    return off == kPacketDSize;
}

}  // namespace packet_d_builder
