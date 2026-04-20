/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_packet_d_builder.cpp
 * Desc:      VOID-136 red-green for PacketD builder vs SNLP golden vector.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

// The builder is SNLP-only for the journey-to-HAB plaintext alpha.
// void-core/CMakeLists.txt compiles this translation unit for BOTH
// tier targets (void_full_tests = SNLP, void_full_tests_ccsds = CCSDS)
// so the sources list stays symmetric. Under the CCSDS target the
// tests below do nothing — gating avoids golden-vector mismatches
// against the parallel CCSDS PacketD frame (128 B, different bytes).
#include <gtest/gtest.h>

#if VOID_PROTOCOL_TYPE == 2

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "packet_d_builder.h"

#ifndef VOID_TEST_VECTORS_DIR
#error "VOID_TEST_VECTORS_DIR must be set by the build system."
#endif

namespace {

// Read a golden wire-format .bin from test/vectors/snlp/. Returns the
// number of bytes read; 0 on failure.
size_t ReadGoldenSnlp(const char* name, uint8_t* buf, size_t buf_cap) {
    char path[512];
    std::snprintf(path, sizeof(path),
                  "%s/snlp/%s", VOID_TEST_VECTORS_DIR, name);
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return 0;
    const size_t n = std::fread(buf, 1, buf_cap, f);
    std::fclose(f);
    return n;
}

// VOID-123 deterministic inputs — byte-for-byte parity with the Go
// generator's genPacketD(isSnlp=true).
//   downlink_ts = detEpochTsMs = 1710000100000
//   sat_b_id    = detSatId     = 0xCAFEBABE
//   payload[i]  = byte(0xE0 + i)
packet_d_builder::DeliveryInputs DeterministicInputs() {
    packet_d_builder::DeliveryInputs in = {};
    in.downlink_ts = 1710000100000ull;
    in.sat_b_id    = 0xCAFEBABEu;
    for (size_t i = 0; i < packet_d_builder::kPayloadSize; ++i) {
        in.payload[i] = static_cast<uint8_t>(0xE0u + i);
    }
    return in;
}

}  // namespace

TEST(PacketDBuilder, WritesExactlyGoldenVectorBytes) {
    uint8_t golden[256] = {0};
    const size_t golden_len =
        ReadGoldenSnlp("packet_d.bin", golden, sizeof(golden));
    ASSERT_EQ(golden_len, packet_d_builder::kPacketDSize);

    uint8_t built[packet_d_builder::kPacketDSize] = {0};
    ASSERT_TRUE(packet_d_builder::build(DeterministicInputs(),
                                        built, sizeof(built)));

    for (size_t i = 0; i < packet_d_builder::kPacketDSize; ++i) {
        EXPECT_EQ(built[i], golden[i])
            << "byte mismatch at offset " << i;
    }
}

TEST(PacketDBuilder, RejectsUndersizedBuffer) {
    uint8_t small[packet_d_builder::kPacketDSize - 1] = {0};
    EXPECT_FALSE(packet_d_builder::build(DeterministicInputs(),
                                         small, sizeof(small)));
}

TEST(PacketDBuilder, RejectsNullBuffer) {
    EXPECT_FALSE(packet_d_builder::build(DeterministicInputs(),
                                         nullptr, 256));
}

TEST(PacketDBuilder, MagicByteAtBodyOffsetZero) {
    uint8_t built[packet_d_builder::kPacketDSize] = {0};
    ASSERT_TRUE(packet_d_builder::build(DeterministicInputs(),
                                        built, sizeof(built)));
    EXPECT_EQ(built[14], packet_d_builder::kPacketDMagic);
}

TEST(PacketDBuilder, SatBIdIsLittleEndianAtBodyOffsetTen) {
    uint8_t built[packet_d_builder::kPacketDSize] = {0};
    packet_d_builder::DeliveryInputs in = DeterministicInputs();
    in.sat_b_id = 0x11223344u;
    ASSERT_TRUE(packet_d_builder::build(in, built, sizeof(built)));
    // Body offset 10 (= header 14 + magic 1 + pad 1 + ts 8) holds sat_b_id.
    EXPECT_EQ(built[14 + 10 + 0], 0x44);
    EXPECT_EQ(built[14 + 10 + 1], 0x33);
    EXPECT_EQ(built[14 + 10 + 2], 0x22);
    EXPECT_EQ(built[14 + 10 + 3], 0x11);
}

TEST(PacketDBuilder, TailSixBytesAreZero) {
    uint8_t built[packet_d_builder::kPacketDSize] = {0};
    // Seed with a non-zero payload pattern to ensure the trailing pad
    // isn't accidentally inheriting payload bytes.
    packet_d_builder::DeliveryInputs in = DeterministicInputs();
    for (size_t i = 0; i < packet_d_builder::kPayloadSize; ++i) {
        in.payload[i] = 0xFFu;
    }
    ASSERT_TRUE(packet_d_builder::build(in, built, sizeof(built)));
    for (size_t i = 130; i < 136; ++i) {
        EXPECT_EQ(built[i], 0x00) << "non-zero tail pad at offset " << i;
    }
}

#endif  // VOID_PROTOCOL_TYPE == 2
