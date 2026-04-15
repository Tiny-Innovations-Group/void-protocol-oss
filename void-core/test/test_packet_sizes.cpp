/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_packet_sizes.cpp
 * Desc:      Runtime sizeof() gate — mirrors the compile-time static_asserts
 *            in void_packets_{ccsds,snlp}.h. Belt-and-braces against a
 *            future #pragma change that silently disables them.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include "void_packets.h"

namespace {

constexpr size_t kLoRaMaxPayload = 255;

template <typename T>
void ExpectWirePacket(const char* name, size_t expected_size) {
    EXPECT_EQ(sizeof(T), expected_size)
        << name << ": sizeof mismatch vs SIZE_PACKET constant.";
    EXPECT_EQ(sizeof(T) % 4, 0u)
        << name << ": not 32-bit aligned (%4 != 0).";
    EXPECT_EQ(sizeof(T) % 8, 0u)
        << name << ": not 64-bit aligned (%8 != 0).";
    EXPECT_LE(sizeof(T), kLoRaMaxPayload)
        << name << ": exceeds 255-byte LoRa PHY ceiling.";
}

}  // namespace

TEST(PacketSizesTest, AllPacketsMatchTierConstants) {
    ExpectWirePacket<PacketA_t>("PacketA_t",         SIZE_PACKET_A);
    ExpectWirePacket<PacketB_t>("PacketB_t",         SIZE_PACKET_B);
    ExpectWirePacket<PacketC_t>("PacketC_t",         SIZE_PACKET_C);
    ExpectWirePacket<PacketD_t>("PacketD_t",         SIZE_PACKET_D);
    ExpectWirePacket<PacketH_t>("PacketH_t",         SIZE_PACKET_H);
    ExpectWirePacket<PacketAck_t>("PacketAck_t",     SIZE_PACKET_ACK);
    ExpectWirePacket<TunnelData_t>("TunnelData_t",   SIZE_TUNNEL_DATA);
    ExpectWirePacket<HeartbeatPacket_t>("HeartbeatPacket_t", SIZE_HEARTBEAT_PCK);
}

#if VOID_PROTOCOL_TYPE == 1
TEST(PacketSizesTest, CcsdsHeaderIsSixBytes) {
    EXPECT_EQ(sizeof(VoidHeader_t), 6u);
    EXPECT_EQ(sizeof(VoidHeader_t), static_cast<size_t>(SIZE_CCSDS_HEADER));
}
#elif VOID_PROTOCOL_TYPE == 2
TEST(PacketSizesTest, SnlpHeaderIsFourteenBytes) {
    EXPECT_EQ(sizeof(VoidHeader_t), 14u);
    EXPECT_EQ(sizeof(VoidHeader_t), static_cast<size_t>(SIZE_SNLP_HEADER));
}
#endif
