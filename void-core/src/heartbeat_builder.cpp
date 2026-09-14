/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      heartbeat_builder.cpp
 * Desc:      VOID-022 pure-function Heartbeat (Packet L) builder, SNLP tier.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "heartbeat_builder.h"

#include <cstring>

namespace heartbeat_builder {
namespace {

// Wire constants locked by the SNLP spec + VOID-123 determinism.
// Matches gateway/test/utils/generate_packets.go::buildHeader when
// (isSnlp=true, isCmd=false).
constexpr uint32_t kSnlpSyncWord = 0x1D01A5A5u;  // VOID-113
constexpr uint16_t kSeqFlags     = 0xC000u;      // hardcoded per generator

// Bit-packing of the CCSDS-style identification field embedded in the
// SNLP header: version(3) | pkt_type(1) | sec_flag(1) | apid(11).
// pkt_type = 0 (telemetry, not command); sec_flag = 1.
constexpr uint16_t IdField(uint16_t apid) {
    return static_cast<uint16_t>((1u << 11) | (apid & 0x7FFu));
}

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
// packet_d_builder.cpp / ack_builder.cpp; kept module-local so
// void-core's runtime library stays standalone (no cross-module link).
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

bool build(const HeartbeatInputs& in, uint8_t* out, size_t out_cap) {
    if (out == nullptr || out_cap < kHeartbeatSize) return false;

    std::memset(out, 0, kHeartbeatSize);
    size_t off = 0;

    // --- 14-byte SNLP header (Big-Endian) ---
    WriteU32BE(out, off, kSnlpSyncWord);                          // 00-03
    WriteU16BE(out, off, IdField(in.apid));                       // 04-05
    WriteU16BE(out, off, kSeqFlags);                              // 06-07
    WriteU16BE(out, off, static_cast<uint16_t>(kBodyLen - 1u));   // 08-09
    WriteU32BE(out, off, 0u);                                     // 10-13 align_pad

    // --- 34-byte body (Little-Endian, VOID-114B layout) ---
    WriteU16LE(out, off, 0u);                                     // 14-15 _pad_head
    WriteU64LE(out, off, in.epoch_ts);                            // 16-23
    WriteU32LE(out, off, in.pressure_pa);                         // 24-27
    WriteU32LE(out, off, static_cast<uint32_t>(in.lat_fixed));    // 28-31
    WriteU32LE(out, off, static_cast<uint32_t>(in.lon_fixed));    // 32-35
    WriteU16LE(out, off, in.vbatt_mv);                            // 36-37
    WriteU16LE(out, off, static_cast<uint16_t>(in.temp_c));       // 38-39
    WriteU16LE(out, off, in.gps_speed_cms);                       // 40-41
    out[off++] = in.sys_state;                                    // 42
    out[off++] = in.sat_lock;                                     // 43

    // --- CRC32 over everything written so far (header + body-ex-CRC) ---
    const uint32_t crc = Crc32Ieee(out, off);
    WriteU32LE(out, off, crc);                                    // 44-47

    return off == kHeartbeatSize;
}

}  // namespace heartbeat_builder
