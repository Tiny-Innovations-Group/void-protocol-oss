/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_golden_vectors.cpp
 * Desc:      Reads every checked-in golden .bin under test/vectors/<tier>/
 *            and asserts that the C++ parser agrees with the Go generator
 *            byte-for-byte AND field-for-field. Failure here means CCSDS
 *            and SNLP wire formats have diverged.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "void_packets.h"

#ifndef VOID_TEST_VECTORS_DIR
#error "VOID_TEST_VECTORS_DIR must be defined by CMake (absolute path to test/vectors)."
#endif
#ifndef VOID_TEST_VECTORS_TIER
#error "VOID_TEST_VECTORS_TIER must be defined by CMake (\"ccsds\" or \"snlp\")."
#endif

namespace {

// Deterministic inputs — must match gateway/test/utils/generate_packets.go.
constexpr uint64_t kEpochTsMs = 1710000100000ULL;
constexpr uint32_t kSatId     = 0xCAFEBABEu;
constexpr uint64_t kAmount    = 420000000ULL;
constexpr uint16_t kAssetId   = 1;
constexpr double   kPosVec0   = 7010.0;
constexpr double   kPosVec1   = -11990.0;
constexpr double   kPosVec2   = 560.0;
constexpr float    kVelVec0   = 7.5f;
constexpr float    kVelVec1   = -0.2f;
constexpr float    kVelVec2   = 0.01f;

// Read the entire file at <vectors>/<tier>/<name> into buf. Returns number
// of bytes read, or 0 on failure. Static buffer sized to the largest LoRa
// frame so we never touch the heap.
size_t ReadVector(const char* name, uint8_t* buf, size_t buf_cap) {
    std::string path = VOID_TEST_VECTORS_DIR "/" VOID_TEST_VECTORS_TIER "/";
    path += name;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        ADD_FAILURE() << "fopen failed for golden vector: " << path;
        return 0;
    }
    const size_t n = std::fread(buf, 1, buf_cap, f);
    std::fclose(f);
    return n;
}

template <typename T>
bool LoadPacket(const char* name, T* out) {
    static uint8_t buf[256];
    const size_t n = ReadVector(name, buf, sizeof(buf));
    if (n != sizeof(T)) {
        ADD_FAILURE() << name << ": read " << n
                      << " bytes, expected " << sizeof(T);
        return false;
    }
    std::memcpy(out, buf, sizeof(T));
    return true;
}

}  // namespace

TEST(GoldenVectorsTest, PacketAFields) {
    PacketA_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_a.bin", &pkt));
    EXPECT_EQ(pkt.epoch_ts, kEpochTsMs);
    EXPECT_DOUBLE_EQ(pkt.pos_vec[0], kPosVec0);
    EXPECT_DOUBLE_EQ(pkt.pos_vec[1], kPosVec1);
    EXPECT_DOUBLE_EQ(pkt.pos_vec[2], kPosVec2);
    EXPECT_FLOAT_EQ(pkt.vel_vec[0], kVelVec0);
    EXPECT_FLOAT_EQ(pkt.vel_vec[1], kVelVec1);
    EXPECT_FLOAT_EQ(pkt.vel_vec[2], kVelVec2);
    EXPECT_EQ(pkt.sat_id,   kSatId);
    EXPECT_EQ(pkt.amount,   kAmount);
    EXPECT_EQ(pkt.asset_id, kAssetId);
    EXPECT_NE(pkt.crc32, 0u);
}

TEST(GoldenVectorsTest, PacketBFields) {
    PacketB_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_b.bin", &pkt));
    EXPECT_EQ(pkt.epoch_ts, kEpochTsMs);
    EXPECT_DOUBLE_EQ(pkt.pos_vec[0], kPosVec0);
    EXPECT_DOUBLE_EQ(pkt.pos_vec[1], kPosVec1);
    EXPECT_DOUBLE_EQ(pkt.pos_vec[2], kPosVec2);
    EXPECT_EQ(pkt.sat_id, kSatId);
    // First 8 bytes of enc_payload hold the deterministic InvoiceTs.
    uint64_t invoice_ts = 0;
    std::memcpy(&invoice_ts, pkt.enc_payload, sizeof(invoice_ts));
    EXPECT_EQ(invoice_ts, kEpochTsMs);
    // "PAYMENT_INTENT" marker embedded in enc_payload starting at byte 14.
    EXPECT_EQ(std::memcmp(pkt.enc_payload + 14, "PAYMENT_INTENT", 14), 0);
    // _tail_pad (VOID-114B) must be zero on the wire.
    for (size_t i = 0; i < sizeof(pkt._tail_pad); ++i) {
        EXPECT_EQ(pkt._tail_pad[i], 0u) << "tail pad byte " << i << " nonzero";
    }
    EXPECT_NE(pkt.global_crc, 0u);
}

TEST(GoldenVectorsTest, PacketHFields) {
    PacketH_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_h.bin", &pkt));
    EXPECT_EQ(pkt.session_ttl, 900u);
    EXPECT_EQ(pkt.timestamp,   kEpochTsMs);
    for (size_t i = 0; i < sizeof(pkt.eph_pub_key); ++i) {
        EXPECT_EQ(pkt.eph_pub_key[i],
                  static_cast<uint8_t>(0xA0 + i))
            << "eph_pub_key[" << i << "] drift";
    }
}

TEST(GoldenVectorsTest, PacketCFields) {
    PacketC_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_c.bin", &pkt));
    EXPECT_EQ(pkt.exec_time,  kEpochTsMs);
    EXPECT_EQ(pkt.enc_tx_id,  0xDEADBEEFCAFEBABEULL);
    EXPECT_EQ(pkt.enc_status, 1u);
    for (size_t i = 0; i < sizeof(pkt._tail_pad); ++i) {
        EXPECT_EQ(pkt._tail_pad[i], 0u);
    }
}

TEST(GoldenVectorsTest, PacketDFields) {
    PacketD_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_d.bin", &pkt));
    EXPECT_EQ(pkt.downlink_ts, kEpochTsMs);
    EXPECT_EQ(pkt.sat_b_id,    kSatId);
    for (size_t i = 0; i < sizeof(pkt.payload); ++i) {
        EXPECT_EQ(pkt.payload[i], static_cast<uint8_t>(0xE0 + i))
            << "payload[" << i << "] drift";
    }
}

TEST(GoldenVectorsTest, PacketAckFields) {
    PacketAck_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_ack.bin", &pkt));
    EXPECT_EQ(pkt.target_tx_id,          0xCAFEBABEu);
    EXPECT_EQ(pkt.status,                1u);
    EXPECT_EQ(pkt.relay_ops.azimuth,     180u);
    EXPECT_EQ(pkt.relay_ops.elevation,   45u);
    EXPECT_EQ(pkt.relay_ops.frequency,   437200000u);
    EXPECT_EQ(pkt.relay_ops.duration_ms, 5000u);
}

TEST(GoldenVectorsTest, HeartbeatFields) {
    HeartbeatPacket_t pkt{};
    ASSERT_TRUE(LoadPacket("packet_l.bin", &pkt));
    EXPECT_EQ(pkt.epoch_ts,      kEpochTsMs);
    EXPECT_EQ(pkt.pressure_pa,   100800u);
    EXPECT_EQ(pkt.lat_fixed,     515000000);
    EXPECT_EQ(pkt.lon_fixed,     -128000);
    EXPECT_EQ(pkt.vbatt_mv,      4100u);
    EXPECT_EQ(pkt.temp_c,        2300);
    EXPECT_EQ(pkt.gps_speed_cms, 600u);
    EXPECT_EQ(pkt.sys_state,     3u);
    EXPECT_EQ(pkt.sat_lock,      8u);
}

TEST(GoldenVectorsTest, AllVectorsMatchSizeof) {
    struct { const char* name; size_t want; } cases[] = {
        {"packet_a.bin",   sizeof(PacketA_t)},
        {"packet_b.bin",   sizeof(PacketB_t)},
        {"packet_c.bin",   sizeof(PacketC_t)},
        {"packet_d.bin",   sizeof(PacketD_t)},
        {"packet_h.bin",   sizeof(PacketH_t)},
        {"packet_ack.bin", sizeof(PacketAck_t)},
        {"packet_l.bin",   sizeof(HeartbeatPacket_t)},
    };
    static uint8_t buf[256];
    for (const auto& c : cases) {
        const size_t n = ReadVector(c.name, buf, sizeof(buf));
        EXPECT_EQ(n, c.want) << c.name << " size drift";
    }
}
