/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_sign_verify.cpp
 * Desc:      Constructs Packet B in C++ with the VOID-123 deterministic
 *            inputs, signs with libsodium Ed25519 using offsetof(PacketB_t,
 *            signature) as scope (VOID-111), and compares byte-for-byte
 *            to the checked-in golden vector. Strongest possible cross-
 *            implementation guarantee: Go generator == C++ signer ==
 *            committed .bin.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <sodium.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "void_packets.h"

#ifndef VOID_TEST_VECTORS_DIR
#error "VOID_TEST_VECTORS_DIR must be defined by CMake."
#endif
#ifndef VOID_TEST_VECTORS_TIER
#error "VOID_TEST_VECTORS_TIER must be defined by CMake."
#endif

namespace {

// ---- VOID-123 deterministic inputs (mirror generate_packets.go) ----
constexpr uint64_t kEpochTsMs  = 1710000100000ULL;
constexpr uint32_t kSatId      = 0xCAFEBABEu;
constexpr uint64_t kAmount     = 420000000ULL;
constexpr uint16_t kAssetId    = 1;
constexpr double   kPosVec[3]  = {7010.0, -11990.0, 560.0};
constexpr float    kVelVec[3]  = {7.5f, -0.2f, 0.01f};
constexpr uint32_t kSyncWord   = 0x1D01A5A5u;
constexpr uint16_t kApidSatB   = 101;

// Ed25519 seed = sha256 of the deterministic test secret. Must match
// detSeedHex in gateway/test/utils/generate_packets.go.
constexpr uint8_t kDetSeed[32] = {
    0xbc, 0x1d, 0xf4, 0xfa, 0x6e, 0x3d, 0x70, 0x48,
    0x99, 0x2f, 0x14, 0xe6, 0x55, 0x06, 0x0c, 0xbb,
    0x21, 0x90, 0xbd, 0xed, 0x90, 0x02, 0x52, 0x4c,
    0x06, 0xe7, 0xcb, 0xb1, 0x63, 0xdf, 0x15, 0xfb,
};

// Classic CRC32 (IEEE 802.3 / Go hash/crc32.ChecksumIEEE) — table-free,
// bit-serial. Only used in tests, so footprint matters more than speed.
uint32_t Crc32Ieee(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

// Store a 16/32/64-bit value little-endian into dst, advancing the cursor.
void WriteU16LE(uint8_t* dst, size_t* off, uint16_t v) {
    dst[(*off)++] = static_cast<uint8_t>(v & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void WriteU32LE(uint8_t* dst, size_t* off, uint32_t v) {
    dst[(*off)++] = static_cast<uint8_t>(v & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>((v >> 8) & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>((v >> 24) & 0xFF);
}
void WriteU64LE(uint8_t* dst, size_t* off, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        dst[(*off)++] = static_cast<uint8_t>((v >> (8 * i)) & 0xFF);
    }
}
void WriteU16BE(uint8_t* dst, size_t* off, uint16_t v) {
    dst[(*off)++] = static_cast<uint8_t>((v >> 8) & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>(v & 0xFF);
}
void WriteU32BE(uint8_t* dst, size_t* off, uint32_t v) {
    dst[(*off)++] = static_cast<uint8_t>((v >> 24) & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>((v >> 8) & 0xFF);
    dst[(*off)++] = static_cast<uint8_t>(v & 0xFF);
}

// Build the tier header (6 bytes CCSDS or 14 bytes SNLP) for a Packet B
// with a 178-byte body. Matches generate_packets.go::buildHeader exactly.
size_t BuildHeader(uint8_t* dst, bool is_snlp, uint16_t body_len, uint16_t apid) {
    size_t off = 0;
    if (is_snlp) {
        WriteU32BE(dst, &off, kSyncWord);
    }
    const uint16_t id_field =
        static_cast<uint16_t>((1u << 11) | (apid & 0x7FFu));  // sec flag set
    WriteU16BE(dst, &off, id_field);
    WriteU16BE(dst, &off, 0xC000u);                      // seq flags
    WriteU16BE(dst, &off, static_cast<uint16_t>(body_len - 1));
    if (is_snlp) {
        WriteU32BE(dst, &off, 0u);                       // 4B align pad
    }
    return off;
}

size_t ReadVector(const char* name, uint8_t* buf, size_t buf_cap) {
    std::string path = VOID_TEST_VECTORS_DIR "/" VOID_TEST_VECTORS_TIER "/";
    path += name;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return 0;
    const size_t n = std::fread(buf, 1, buf_cap, f);
    std::fclose(f);
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construct Packet B from scratch in C++ using the deterministic VOID-123
// inputs, sign it with libsodium ed25519, CRC32-IEEE it, and assert it is
// byte-identical to the golden vector. Any drift — header layout, field
// order, signature scope, CRC scope, tail pad, seed handling — fails here.
// ---------------------------------------------------------------------------
TEST(SignVerifyTest, PacketBMatchesGoldenByteForByte) {
    ASSERT_GE(sodium_init(), 0);

#if VOID_PROTOCOL_TYPE == 1
    constexpr bool kIsSnlp = false;
    constexpr size_t kHdrLen = 6;
#elif VOID_PROTOCOL_TYPE == 2
    constexpr bool kIsSnlp = true;
    constexpr size_t kHdrLen = 14;
#else
#error "test_sign_verify.cpp: unknown VOID_PROTOCOL_TYPE"
#endif

    constexpr size_t kBodyLen   = 178;
    constexpr size_t kFrameLen  = kHdrLen + kBodyLen;
    static_assert(kFrameLen == SIZE_PACKET_B,
                  "Packet B frame length drifted from tier constant.");

    uint8_t frame[256] = {0};

    // 1) Header.
    const size_t hdr_written = BuildHeader(frame, kIsSnlp, kBodyLen, kApidSatB);
    ASSERT_EQ(hdr_written, kHdrLen);

    // 2) Body before signature (106 bytes): _pad_head(2) + epoch_ts(8) +
    //    pos_vec(24) + enc_payload(62) + _pre_sat(2) + sat_id(4) + _pre_sig(4).
    size_t off = kHdrLen;
    const size_t body_start = off;

    off += 2;                                             // _pad_head
    WriteU64LE(frame, &off, kEpochTsMs);                  // epoch_ts
    for (int i = 0; i < 3; ++i) {
        uint64_t bits = 0;
        std::memcpy(&bits, &kPosVec[i], sizeof(double));
        WriteU64LE(frame, &off, bits);
    }
    // enc_payload: Inner Invoice Payload (62B) — verbatim echo of the paired
    // PacketA body per Protocol-spec-SNLP.md §4.3 / CCSDS §3.3. The trailing
    // crc32 is the ORIGINAL invoice CRC, which we pull directly from the
    // paired golden PacketA so any divergence surfaces here too.
    const size_t enc_start = off;
    WriteU64LE(frame, &off, kEpochTsMs);                          // epoch_ts (8B)
    for (int i = 0; i < 3; ++i) {
        uint64_t bits = 0;
        std::memcpy(&bits, &kPosVec[i], sizeof(double));
        WriteU64LE(frame, &off, bits);                            // pos_vec f64[3]
    }
    for (int i = 0; i < 3; ++i) {
        uint32_t bits = 0;
        std::memcpy(&bits, &kVelVec[i], sizeof(float));
        WriteU32LE(frame, &off, bits);                            // vel_vec f32[3]
    }
    WriteU32LE(frame, &off, kSatId);                              // sat_id   (4B)
    WriteU64LE(frame, &off, kAmount);                             // amount   (8B)
    WriteU16LE(frame, &off, kAssetId);                            // asset_id (2B)
    {
        uint8_t golden_a[256] = {0};
        const size_t golden_a_len =
            ReadVector("packet_a.bin", golden_a, sizeof(golden_a));
        ASSERT_EQ(golden_a_len, SIZE_PACKET_A)
            << "golden packet_a.bin missing / wrong size — "
               "Inner Invoice Payload needs its CRC";
        uint32_t inv_crc = 0;
        std::memcpy(&inv_crc, golden_a + SIZE_PACKET_A - 4, 4);
        WriteU32LE(frame, &off, inv_crc);                         // crc32    (4B)
    }
    ASSERT_EQ(off - enc_start, 62u)
        << "Inner Invoice Payload drifted from 62 B (SNLP §4.3)";

    off += 2;                                             // _pre_sat
    WriteU32LE(frame, &off, kSatId);                      // sat_id
    off += 4;                                             // _pre_sig

    ASSERT_EQ(off - body_start, 106u)
        << "VOID-111: pre-sig body must be exactly 106 bytes.";

    // 3) Signature: covers header + body[0..105]. This is the
    //    offsetof(PacketB_t, signature) contract from VOID-111.
    const size_t sig_scope = offsetof(PacketB_t, signature);
    ASSERT_EQ(sig_scope, kHdrLen + 106u);

    uint8_t ed_pub[crypto_sign_PUBLICKEYBYTES];
    uint8_t ed_priv[crypto_sign_SECRETKEYBYTES];
    ASSERT_EQ(crypto_sign_seed_keypair(ed_pub, ed_priv, kDetSeed), 0);

    unsigned long long sig_len = 0;
    ASSERT_EQ(crypto_sign_detached(frame + off, &sig_len,
                                   frame, sig_scope, ed_priv),
              0);
    ASSERT_EQ(sig_len, 64u);
    off += 64;

    // 4) CRC32 covers header + body so far (170 body bytes, no crc/tail_pad).
    const size_t crc_scope = off;
    const uint32_t crc = Crc32Ieee(frame, crc_scope);
    WriteU32LE(frame, &off, crc);

    // 5) _tail_pad[4] — already zeroed.
    off += 4;

    ASSERT_EQ(off, kFrameLen);

    // 6) Load the golden vector and compare byte-for-byte.
    uint8_t golden[256] = {0};
    const size_t golden_len = ReadVector("packet_b.bin", golden, sizeof(golden));
    ASSERT_EQ(golden_len, kFrameLen)
        << "golden packet_b.bin size drift";

    for (size_t i = 0; i < kFrameLen; ++i) {
        ASSERT_EQ(frame[i], golden[i])
            << "byte " << i << " mismatch between C++ signer and Go golden vector";
    }

    // 7) And verify the signature round-trips through libsodium.
    EXPECT_EQ(crypto_sign_verify_detached(frame + sig_scope,
                                          frame, sig_scope, ed_pub),
              0);
}
