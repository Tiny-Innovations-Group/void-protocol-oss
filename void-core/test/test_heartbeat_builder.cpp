/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_heartbeat_builder.cpp
 * Desc:      VOID-022 red-green for Heartbeat builder vs SNLP golden vector.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

// The builder is SNLP-only for the journey-to-HAB plaintext alpha.
// void-core/CMakeLists.txt compiles this translation unit for BOTH
// tier targets; under the CCSDS target the tests below do nothing —
// gating avoids golden-vector mismatches against the parallel CCSDS
// heartbeat frame (40 B, different bytes).
#include <gtest/gtest.h>

#if VOID_PROTOCOL_TYPE == 2

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "heartbeat_builder.h"

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
// generator's genPacketL(isSnlp=true): apid=apidSatB(101),
// epoch=detEpochTsMs, pressure=100800, lat=515000000, lon=-128000,
// vbatt=4100, temp=2300, speed=600, sys_state=3, sat_lock=8.
heartbeat_builder::HeartbeatInputs DeterministicInputs() {
    heartbeat_builder::HeartbeatInputs in = {};
    in.apid          = 101u;
    in.epoch_ts      = 1710000100000ull;
    in.pressure_pa   = 100800u;
    in.lat_fixed     = 515000000;
    in.lon_fixed     = -128000;
    in.vbatt_mv      = 4100u;
    in.temp_c        = 2300;
    in.gps_speed_cms = 600u;
    in.sys_state     = 3u;   // sys_state::rx_active (tig_common_types.ksy)
    in.sat_lock      = 8u;
    return in;
}

}  // namespace

TEST(HeartbeatBuilder, WritesExactlyGoldenVectorBytes) {
    uint8_t golden[256] = {0};
    const size_t golden_len =
        ReadGoldenSnlp("packet_l.bin", golden, sizeof(golden));
    ASSERT_EQ(golden_len, heartbeat_builder::kHeartbeatSize);

    uint8_t built[heartbeat_builder::kHeartbeatSize] = {0};
    ASSERT_TRUE(heartbeat_builder::build(DeterministicInputs(),
                                         built, sizeof(built)));

    for (size_t i = 0; i < heartbeat_builder::kHeartbeatSize; ++i) {
        EXPECT_EQ(built[i], golden[i])
            << "byte mismatch at offset " << i;
    }
}

TEST(HeartbeatBuilder, RejectsUndersizedBuffer) {
    uint8_t small[heartbeat_builder::kHeartbeatSize - 1] = {0};
    EXPECT_FALSE(heartbeat_builder::build(DeterministicInputs(),
                                          small, sizeof(small)));
}

TEST(HeartbeatBuilder, RejectsNullBuffer) {
    EXPECT_FALSE(heartbeat_builder::build(DeterministicInputs(),
                                          nullptr, 256));
}

TEST(HeartbeatBuilder, ApidIsEncodedInHeaderIdField) {
    // Both boards emit heartbeats; identity rides solely on the APID.
    // id = version(0) | pkt_type(0) | sec_flag(1) | apid → 0x0800 | apid.
    uint8_t built[heartbeat_builder::kHeartbeatSize] = {0};
    heartbeat_builder::HeartbeatInputs in = DeterministicInputs();
    in.apid = 100u;  // SELLER_APID
    ASSERT_TRUE(heartbeat_builder::build(in, built, sizeof(built)));
    EXPECT_EQ(built[4], 0x08);
    EXPECT_EQ(built[5], 0x64);
}

TEST(HeartbeatBuilder, LonFixedIsSignedLittleEndian) {
    // lon = -128000 = 0xFFFE0C00 two's complement → LE bytes 00 0C FE FF
    // at frame offset 32 (header 14 + pad 2 + ts 8 + pressure 4 + lat 4).
    uint8_t built[heartbeat_builder::kHeartbeatSize] = {0};
    ASSERT_TRUE(heartbeat_builder::build(DeterministicInputs(),
                                         built, sizeof(built)));
    EXPECT_EQ(built[32], 0x00);
    EXPECT_EQ(built[33], 0x0C);
    EXPECT_EQ(built[34], 0xFE);
    EXPECT_EQ(built[35], 0xFF);
}

TEST(HeartbeatBuilder, SysStateAndSatLockAtBodyTail) {
    uint8_t built[heartbeat_builder::kHeartbeatSize] = {0};
    heartbeat_builder::HeartbeatInputs in = DeterministicInputs();
    in.sys_state = 4u;  // sys_state::connected (transaction in flight)
    in.sat_lock  = 11u;
    ASSERT_TRUE(heartbeat_builder::build(in, built, sizeof(built)));
    EXPECT_EQ(built[42], 4u);
    EXPECT_EQ(built[43], 11u);
}

TEST(HeartbeatBuilder, CrcChangesWhenBodyChanges) {
    // The trailing CRC32 covers header + body[0..44): flipping any input
    // must move at least one of the four CRC bytes at [44..48).
    uint8_t a[heartbeat_builder::kHeartbeatSize] = {0};
    uint8_t b[heartbeat_builder::kHeartbeatSize] = {0};
    heartbeat_builder::HeartbeatInputs in = DeterministicInputs();
    ASSERT_TRUE(heartbeat_builder::build(in, a, sizeof(a)));
    in.vbatt_mv = 3700u;
    ASSERT_TRUE(heartbeat_builder::build(in, b, sizeof(b)));
    EXPECT_NE(0, std::memcmp(&a[44], &b[44], 4));
}

#endif  // VOID_PROTOCOL_TYPE == 2
