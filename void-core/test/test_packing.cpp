/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_packing.cpp
 * Desc:      Legacy pragma-pack smoke test (tier-agnostic).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include "void_packets.h"

// Smoke check: the #pragma pack(push, 1) wrapping the packet structs must
// still be in effect. Deeper size/offset assertions now live in
// test_packet_sizes.cpp and test_offsets.cpp (VOID-125).
TEST(PackingTest, StructsArePragmaPackedByteAligned) {
    EXPECT_EQ(alignof(PacketB_t), 1u) << "PacketB_t is not byte-aligned!";
    EXPECT_EQ(alignof(PacketA_t), 1u) << "PacketA_t is not byte-aligned!";
    EXPECT_EQ(alignof(PacketH_t), 1u) << "PacketH_t is not byte-aligned!";
}

TEST(PackingTest, CoreSizesMatchTierConstants) {
    EXPECT_EQ(sizeof(PacketA_t),        static_cast<size_t>(SIZE_PACKET_A));
    EXPECT_EQ(sizeof(PacketB_t),        static_cast<size_t>(SIZE_PACKET_B));
    EXPECT_EQ(sizeof(PacketH_t),        static_cast<size_t>(SIZE_PACKET_H));
    EXPECT_EQ(sizeof(HeartbeatPacket_t), static_cast<size_t>(SIZE_HEARTBEAT_PCK));
}
