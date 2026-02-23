/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * File:      test_packing.cpp
 * Desc:      Verification of struct packing and memory alignment.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include "void_packets.h"

TEST(PackingTest, VerifyOTASizes) {
    // Verified against README.md specifications
    EXPECT_EQ(sizeof(VoidHeader_t), 6)   << "VoidHeader_t must be exactly 6 bytes.";
    EXPECT_EQ(sizeof(PacketH_t), 112)    << "PacketH_t (Handshake) must be 112 bytes.";
    EXPECT_EQ(sizeof(PacketA_t), 68)     << "PacketA_t (Invoice) must be 68 bytes.";
    EXPECT_EQ(sizeof(PacketB_t), 176)    << "PacketB_t (Payment) must be 176 bytes.";
}

TEST(PackingTest, VerifyAlignment) {
    // Verify that the structs don't have hidden padding bytes
    // (This ensures our #pragma pack(push, 1) is working)
    EXPECT_TRUE(alignof(PacketB_t) == 1) << "PacketB_t is not byte-aligned!";
}