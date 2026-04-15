/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_offsets.cpp
 * Desc:      Pins every critical packet field offset via offsetof(). Any
 *            accidental struct reshape fails this gate before it can reach
 *            the wire. Source of truth: docs/Protocol-spec-CCSDS.md and
 *            docs/Protocol-spec-SNLP.md.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <cstddef>
#include "void_packets.h"

// ---------------------------------------------------------------------------
// Tier-conditional offset tables. The CCSDS header is 6 bytes, the SNLP
// header is 14 bytes, so every post-header field is shifted by 8 between
// tiers. These constants are the spec — they are what Go, the KSY file,
// and the docs all agree on.
// ---------------------------------------------------------------------------
#if VOID_PROTOCOL_TYPE == 1
    // ---- CCSDS (6-byte header) ----
    constexpr size_t kHdrLen       = 6;
    // Packet A
    constexpr size_t kA_EpochTs    = 8;
    constexpr size_t kA_PosVec     = 16;
    constexpr size_t kA_VelVec     = 40;
    constexpr size_t kA_SatId      = 52;
    constexpr size_t kA_Amount     = 56;
    constexpr size_t kA_Crc32      = 68;
    // Packet B
    constexpr size_t kB_EpochTs    = 8;
    constexpr size_t kB_PosVec     = 16;
    constexpr size_t kB_EncPayload = 40;
    constexpr size_t kB_SatId      = 104;
    constexpr size_t kB_Signature  = 112;
    constexpr size_t kB_GlobalCrc  = 176;
    constexpr size_t kB_TailPad    = 180;
    // Heartbeat
    constexpr size_t kL_EpochTs    = 8;
    constexpr size_t kL_PressurePa = 16;
    constexpr size_t kL_LatFixed   = 20;
    constexpr size_t kL_LonFixed   = 24;
    constexpr size_t kL_Crc32      = 36;
#elif VOID_PROTOCOL_TYPE == 2
    // ---- SNLP (14-byte header) ----
    constexpr size_t kHdrLen       = 14;
    constexpr size_t kA_EpochTs    = 16;
    constexpr size_t kA_PosVec     = 24;
    constexpr size_t kA_VelVec     = 48;
    constexpr size_t kA_SatId      = 60;
    constexpr size_t kA_Amount     = 64;
    constexpr size_t kA_Crc32      = 76;
    constexpr size_t kB_EpochTs    = 16;
    constexpr size_t kB_PosVec     = 24;
    constexpr size_t kB_EncPayload = 48;
    constexpr size_t kB_SatId      = 112;
    constexpr size_t kB_Signature  = 120;
    constexpr size_t kB_GlobalCrc  = 184;
    constexpr size_t kB_TailPad    = 188;
    constexpr size_t kL_EpochTs    = 16;
    constexpr size_t kL_PressurePa = 24;
    constexpr size_t kL_LatFixed   = 28;
    constexpr size_t kL_LonFixed   = 32;
    constexpr size_t kL_Crc32      = 44;
#else
    #error "test_offsets.cpp: unknown VOID_PROTOCOL_TYPE"
#endif

TEST(OffsetsTest, HeaderLength) {
    EXPECT_EQ(sizeof(VoidHeader_t), kHdrLen);
    EXPECT_EQ(offsetof(PacketA_t, header), 0u);
    EXPECT_EQ(offsetof(PacketB_t, header), 0u);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, header), 0u);
}

TEST(OffsetsTest, PacketAFieldOffsets) {
    EXPECT_EQ(offsetof(PacketA_t, epoch_ts), kA_EpochTs);
    EXPECT_EQ(offsetof(PacketA_t, pos_vec),  kA_PosVec);
    EXPECT_EQ(offsetof(PacketA_t, vel_vec),  kA_VelVec);
    EXPECT_EQ(offsetof(PacketA_t, sat_id),   kA_SatId);
    EXPECT_EQ(offsetof(PacketA_t, amount),   kA_Amount);
    EXPECT_EQ(offsetof(PacketA_t, crc32),    kA_Crc32);
    // Natural-alignment sanity (VOID-114B).
    EXPECT_EQ(offsetof(PacketA_t, epoch_ts) % 8, 0u);
    EXPECT_EQ(offsetof(PacketA_t, pos_vec)  % 8, 0u);
    EXPECT_EQ(offsetof(PacketA_t, sat_id)   % 4, 0u);
    EXPECT_EQ(offsetof(PacketA_t, amount)   % 8, 0u);
}

TEST(OffsetsTest, PacketBFieldOffsets) {
    EXPECT_EQ(offsetof(PacketB_t, epoch_ts),    kB_EpochTs);
    EXPECT_EQ(offsetof(PacketB_t, pos_vec),     kB_PosVec);
    EXPECT_EQ(offsetof(PacketB_t, enc_payload), kB_EncPayload);
    EXPECT_EQ(offsetof(PacketB_t, sat_id),      kB_SatId);
    EXPECT_EQ(offsetof(PacketB_t, signature),   kB_Signature);
    EXPECT_EQ(offsetof(PacketB_t, global_crc),  kB_GlobalCrc);
    EXPECT_EQ(offsetof(PacketB_t, _tail_pad),   kB_TailPad);
    // Natural-alignment sanity.
    EXPECT_EQ(offsetof(PacketB_t, epoch_ts)   % 8, 0u);
    EXPECT_EQ(offsetof(PacketB_t, pos_vec)    % 8, 0u);
    EXPECT_EQ(offsetof(PacketB_t, sat_id)     % 4, 0u);
    EXPECT_EQ(offsetof(PacketB_t, signature)  % 8, 0u);
    EXPECT_EQ(offsetof(PacketB_t, global_crc) % 4, 0u);
}

TEST(OffsetsTest, PacketBSignatureScopeIsOffsetof) {
    // VOID-111: signature scope equals offsetof(PacketB_t, signature).
    // CCSDS: 112 bytes. SNLP: 120 bytes. This is the invariant that the
    // Go generator, the C++ signer, and the audit doc all agree on.
    EXPECT_EQ(offsetof(PacketB_t, signature), kB_Signature);
    constexpr size_t body_sig_scope = kB_Signature - kHdrLen;
    EXPECT_EQ(body_sig_scope, 106u)
        << "VOID-111: pre-sig body bytes must equal 106 (see VOID-123 generator).";
}

TEST(OffsetsTest, HeartbeatFieldOffsets) {
    EXPECT_EQ(offsetof(HeartbeatPacket_t, epoch_ts),    kL_EpochTs);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, pressure_pa), kL_PressurePa);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, lat_fixed),   kL_LatFixed);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, lon_fixed),   kL_LonFixed);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, crc32),       kL_Crc32);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, epoch_ts)    % 8, 0u);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, pressure_pa) % 4, 0u);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, lat_fixed)   % 4, 0u);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, lon_fixed)   % 4, 0u);
    EXPECT_EQ(offsetof(HeartbeatPacket_t, crc32)       % 4, 0u);
}
