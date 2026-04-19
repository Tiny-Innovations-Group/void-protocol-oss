/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_egress_hex.cpp
 * Desc:      VOID-138 red-green for the bounded hex decoder.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "egress_hex.h"

// Decoder semantics the tests below pin:
//   • Decodes hex_len ASCII chars into hex_len/2 bytes.
//   • Mixed case ok ("AbCd" == "abcd").
//   • Odd length rejects (can't produce a full byte).
//   • Any non-hex char rejects (no silent skip).
//   • Insufficient output capacity rejects.
//   • NULL inputs reject.
//   • The 224-char PacketC hex happy path works (the production case).

TEST(EgressHex, DecodesSimpleSequence) {
    const char hex[] = "cafebabe";
    uint8_t out[4] = {0};
    ASSERT_TRUE(egress::hex_decode(hex, 8, out, sizeof(out)));
    EXPECT_EQ(out[0], 0xCA);
    EXPECT_EQ(out[1], 0xFE);
    EXPECT_EQ(out[2], 0xBA);
    EXPECT_EQ(out[3], 0xBE);
}

TEST(EgressHex, DecodesMixedCase) {
    const char hex[] = "AbCdEf01";
    uint8_t out[4] = {0};
    ASSERT_TRUE(egress::hex_decode(hex, 8, out, sizeof(out)));
    EXPECT_EQ(out[0], 0xAB);
    EXPECT_EQ(out[1], 0xCD);
    EXPECT_EQ(out[2], 0xEF);
    EXPECT_EQ(out[3], 0x01);
}

TEST(EgressHex, EmptyInputIsValidAndWritesNothing) {
    uint8_t out[4] = {0xAA, 0xAA, 0xAA, 0xAA};
    ASSERT_TRUE(egress::hex_decode("", 0, out, sizeof(out)));
    // Output buffer must NOT have been touched.
    EXPECT_EQ(out[0], 0xAA);
    EXPECT_EQ(out[1], 0xAA);
    EXPECT_EQ(out[2], 0xAA);
    EXPECT_EQ(out[3], 0xAA);
}

TEST(EgressHex, OddLengthRejected) {
    uint8_t out[4] = {0};
    EXPECT_FALSE(egress::hex_decode("abc", 3, out, sizeof(out)));
}

TEST(EgressHex, NonHexCharRejected) {
    // The 'z' is illegal; the decoder MUST reject the whole string,
    // not silently skip or substitute.
    uint8_t out[4] = {0};
    EXPECT_FALSE(egress::hex_decode("zzzz", 4, out, sizeof(out)));
}

TEST(EgressHex, InsufficientOutputCapacityRejected) {
    const char hex[] = "cafebabe";
    uint8_t out[3] = {0}; // need 4 bytes, we have 3
    EXPECT_FALSE(egress::hex_decode(hex, 8, out, sizeof(out)));
}

TEST(EgressHex, NullInputsRejected) {
    uint8_t out[4] = {0};
    EXPECT_FALSE(egress::hex_decode(nullptr, 4, out, sizeof(out)));
    EXPECT_FALSE(egress::hex_decode("cafebabe", 8, nullptr, sizeof(out)));
}

TEST(EgressHex, FullPacketCRoundTrip) {
    // The production case: 224 hex chars in, 112 bytes out.
    // Use a sliding pattern so the bit placement is checkable.
    char hex[224];
    for (size_t i = 0; i < 112; ++i) {
        const uint8_t b = static_cast<uint8_t>(i & 0xFFu);
        static const char kHexDigits[] = "0123456789abcdef";
        hex[i * 2]     = kHexDigits[(b >> 4) & 0xF];
        hex[i * 2 + 1] = kHexDigits[b & 0xF];
    }
    uint8_t out[112] = {0};
    ASSERT_TRUE(egress::hex_decode(hex, sizeof(hex), out, sizeof(out)));
    for (size_t i = 0; i < sizeof(out); ++i) {
        EXPECT_EQ(out[i], static_cast<uint8_t>(i & 0xFFu)) << "byte " << i;
    }
}