/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_ack_builder.cpp
 * Desc:      VOID-134 red-green for PacketAck builder vs golden vector.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "ack_builder.h"

// VOID_TEST_VECTORS_DIR is defined by CMake on the test target and
// points at `<repo>/test/vectors`.
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
// generator's genPacketAck(isSnlp=true).
ack_builder::AckInputs DeterministicInputs() {
    ack_builder::AckInputs in = {};
    in.target_tx_id = 0xCAFEBABEu;
    in.status       = ack_builder::kAckStatusVerified;
    in.azimuth      = 180;
    in.elevation    = 45;
    in.frequency_hz = 437200000u;
    in.duration_ms  = 5000u;
    for (size_t i = 0; i < ack_builder::kEncTunnelSize; ++i) {
        in.enc_tunnel[i] = static_cast<uint8_t>(0xAAu + i);
    }
    return in;
}

}  // namespace

TEST(AckBuilder, WritesExactlyGoldenVectorBytes) {
    uint8_t golden[256] = {0};
    const size_t golden_len =
        ReadGoldenSnlp("packet_ack.bin", golden, sizeof(golden));
    ASSERT_EQ(golden_len, ack_builder::kPacketAckSize);

    uint8_t built[ack_builder::kPacketAckSize] = {0};
    ASSERT_TRUE(ack_builder::build(DeterministicInputs(),
                                   built, sizeof(built)));

    for (size_t i = 0; i < ack_builder::kPacketAckSize; ++i) {
        EXPECT_EQ(built[i], golden[i])
            << "byte mismatch at offset " << i;
    }
}

TEST(AckBuilder, RejectsUndersizedBuffer) {
    uint8_t small[ack_builder::kPacketAckSize - 1] = {0};
    EXPECT_FALSE(ack_builder::build(DeterministicInputs(),
                                    small, sizeof(small)));
}

TEST(AckBuilder, RejectsNullBuffer) {
    EXPECT_FALSE(ack_builder::build(DeterministicInputs(),
                                    nullptr, 256));
}

TEST(AckBuilder, MagicByteAtBodyOffsetZero) {
    uint8_t built[ack_builder::kPacketAckSize] = {0};
    ASSERT_TRUE(ack_builder::build(DeterministicInputs(),
                                   built, sizeof(built)));
    // Body starts at offset 14 (SNLP header is 14 B, F-03 magic sits
    // at body-offset 0).
    EXPECT_EQ(built[14], ack_builder::kPacketAckMagic);
}

TEST(AckBuilder, TargetTxIdIsLittleEndianAtBodyOffsetTwo) {
    uint8_t built[ack_builder::kPacketAckSize] = {0};
    ack_builder::AckInputs in = DeterministicInputs();
    in.target_tx_id = 0x11223344u;
    ASSERT_TRUE(ack_builder::build(in, built, sizeof(built)));
    EXPECT_EQ(built[14 + 2 + 0], 0x44);
    EXPECT_EQ(built[14 + 2 + 1], 0x33);
    EXPECT_EQ(built[14 + 2 + 2], 0x22);
    EXPECT_EQ(built[14 + 2 + 3], 0x11);
}
