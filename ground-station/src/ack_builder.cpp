/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      ack_builder.cpp
 * Desc:      VOID-134 pure-function PacketAck builder (SNLP tier, alpha).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "ack_builder.h"

#include <cstring>

namespace ack_builder {
namespace {

// Wire constants locked by the SNLP spec + VOID-123 determinism.
// Matches gateway/test/utils/generate_packets.go::buildHeader when
// (isSnlp=true, isCmd=true) and apid=apidSatB (101).
constexpr uint32_t kSnlpSyncWord   = 0x1D01A5A5u; // VOID-113
constexpr uint16_t kApidSatB       = 101u;         // ACK goes downlink to Sat B
constexpr uint16_t kSeqFlags       = 0xC000u;      // Hardcoded per generator
constexpr uint16_t kSnlpAckBodyLen = 122u;         // 136 total - 14 header

// Bit-packing of the CCSDS-style identification field embedded in the
// SNLP header: version(3) | pkt_type(1) | sec_flag(1) | apid(11).
// Mirrors buildHeader(isCmd=true, secFlag=1).
constexpr uint16_t IdField(uint16_t apid) {
    // version=0, pkt_type=1 (cmd), sec_flag=1 → top bits 0x1800.
    return static_cast<uint16_t>(
        (1u << 12) | (1u << 11) | (apid & 0x7FFu));
}

// Little-endian store helpers — advances a cursor. Matches the pattern
// used in void-core/test/test_sign_verify.cpp.
void WriteU16LE(uint8_t* dst, size_t& off, uint16_t v) {
    dst[off++] = static_cast<uint8_t>(v & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}
void WriteU32LE(uint8_t* dst, size_t& off, uint32_t v) {
    dst[off++] = static_cast<uint8_t>(v & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[off++] = static_cast<uint8_t>((v >> 24) & 0xFFu);
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

// Table-free IEEE-802.3 CRC-32, byte-identical to Go's
// hash/crc32.ChecksumIEEE. Kept inline here so the bouncer doesn't
// pull void-core/src into its production link set for a 14-line
// helper.
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

bool build(const AckInputs& in, uint8_t* out, size_t out_cap) {
    if (out == nullptr || out_cap < kPacketAckSize) return false;

    std::memset(out, 0, kPacketAckSize);
    size_t off = 0;

    // --- 14-byte SNLP header (Big-Endian) ---
    WriteU32BE(out, off, kSnlpSyncWord);                          // 00-03
    WriteU16BE(out, off, IdField(kApidSatB));                     // 04-05
    WriteU16BE(out, off, kSeqFlags);                              // 06-07
    WriteU16BE(out, off, static_cast<uint16_t>(kSnlpAckBodyLen - 1u)); // 08-09
    WriteU32BE(out, off, 0u);                                     // 10-13 align_pad

    // --- 122-byte body (Little-Endian) ---
    out[off++] = kPacketAckMagic;                                 // 14 magic
    out[off++] = 0u;                                              // 15 _pad_a
    WriteU32LE(out, off, in.target_tx_id);                        // 16-19
    out[off++] = in.status;                                       // 20
    out[off++] = 0u;                                              // 21 _pad_b
    WriteU16LE(out, off, in.azimuth);                             // 22-23
    WriteU16LE(out, off, in.elevation);                           // 24-25
    WriteU32LE(out, off, in.frequency_hz);                        // 26-29
    WriteU32LE(out, off, in.duration_ms);                         // 30-33
    std::memcpy(&out[off], in.enc_tunnel, kEncTunnelSize);        // 34-129
    off += kEncTunnelSize;
    out[off++] = 0u;                                              // 130 _pad_c lo
    out[off++] = 0u;                                              // 131 _pad_c hi

    // --- CRC32 over everything written so far (header + body-ex-CRC) ---
    const uint32_t crc = Crc32Ieee(out, off);
    WriteU32LE(out, off, crc);                                    // 132-135

    return off == kPacketAckSize;
}

}  // namespace ack_builder
