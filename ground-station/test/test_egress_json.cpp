/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_egress_json.cpp
 * Desc:      VOID-138 red-green for the bounded JSON scanner the bouncer
 *            uses to parse GET /api/v1/egress/pending responses.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "egress_json.h"

// The bouncer MUST parse a bare JSON array of records the gateway emits
// at GET /api/v1/egress/pending. Each record has 10+ fields but only 3
// matter for dispatch:
//
//   payment_id           — opaque decimal string, used as the dedup key
//   settlement_tx_hash   — "0x"-prefixed hex, used as the dedup key
//   packet_c_hex         — 224 ASCII chars, decodes to the 112 B PacketC
//                          frame the bouncer LoRa-TXes
//
// Everything else (sat_id, amount, asset_id, wallet, block_number,
// ts_ms, dispatch_status) must be parsed over and ignored without
// reading off the buffer. Malformed input MUST NOT write to `out`
// and MUST return a negative error code.
//
// Bounded buffers (EgressPaymentIdMaxLen / EgressTxHashMaxLen /
// EgressPacketCHexMaxLen) are enforced — any value longer than its
// bucket rejects the WHOLE response (fail-closed).

namespace {

// Byte-length-excluding-null helper so tests don't keep subtracting 1.
template <size_t N>
constexpr size_t lit_len(const char (&)[N]) { return N - 1; }

} // namespace

TEST(EgressJSON, EmptyArrayReturnsZero) {
    const char body[] = "[]";
    egress::Record recs[4];
    EXPECT_EQ(egress::parse_pending_response(body, lit_len(body), recs, 4), 0);
}

TEST(EgressJSON, EmptyArrayWithWhitespaceReturnsZero) {
    const char body[] = "  [   ]  ";
    egress::Record recs[4];
    EXPECT_EQ(egress::parse_pending_response(body, lit_len(body), recs, 4), 0);
}

TEST(EgressJSON, SingleRecordExtractsThreeFields) {
    const char body[] =
        "[{\"payment_id\":\"123\",\"settlement_tx_hash\":\"0xabc\","
        "\"packet_c_hex\":\"dead\"}]";
    egress::Record recs[4];
    ASSERT_EQ(egress::parse_pending_response(body, lit_len(body), recs, 4), 1);
    EXPECT_STREQ(recs[0].payment_id,          "123");
    EXPECT_STREQ(recs[0].settlement_tx_hash,  "0xabc");
    EXPECT_STREQ(recs[0].packet_c_hex,        "dead");
}

TEST(EgressJSON, RealGatewayResponseShapeExtracts) {
    // Mirror the actual gateway payload: 10 fields per record, JSON encoder
    // order is stable-enough (but not guaranteed) — scanner must find
    // keys by name, not position.
    const char body[] =
        "[{"
          "\"payment_id\":\"62823921007141009161605796512\","
          "\"settlement_tx_hash\":\"0xbeef\","
          "\"sat_id\":3405691582,"
          "\"amount\":\"420000000\","
          "\"asset_id\":1,"
          "\"wallet\":\"0x70997970C51812dc3A010C7d01b50e0d17dc79C8\","
          "\"packet_c_hex\":\"cafebabe\","
          "\"block_number\":42,"
          "\"ts_ms\":1710000100000,"
          "\"dispatch_status\":\"PENDING\""
        "}]";
    egress::Record recs[4];
    ASSERT_EQ(egress::parse_pending_response(body, lit_len(body), recs, 4), 1);
    EXPECT_STREQ(recs[0].payment_id,
                 "62823921007141009161605796512");
    EXPECT_STREQ(recs[0].settlement_tx_hash, "0xbeef");
    EXPECT_STREQ(recs[0].packet_c_hex,       "cafebabe");
}

TEST(EgressJSON, BatchOfThreeParsesInOrder) {
    const char body[] =
        "["
          "{\"payment_id\":\"1\",\"settlement_tx_hash\":\"0xa\","
            "\"packet_c_hex\":\"aa\"},"
          "{\"payment_id\":\"2\",\"settlement_tx_hash\":\"0xb\","
            "\"packet_c_hex\":\"bb\"},"
          "{\"payment_id\":\"3\",\"settlement_tx_hash\":\"0xc\","
            "\"packet_c_hex\":\"cc\"}"
        "]";
    egress::Record recs[4];
    ASSERT_EQ(egress::parse_pending_response(body, lit_len(body), recs, 4), 3);
    EXPECT_STREQ(recs[0].payment_id, "1");
    EXPECT_STREQ(recs[1].payment_id, "2");
    EXPECT_STREQ(recs[2].payment_id, "3");
    EXPECT_STREQ(recs[0].settlement_tx_hash, "0xa");
    EXPECT_STREQ(recs[1].settlement_tx_hash, "0xb");
    EXPECT_STREQ(recs[2].settlement_tx_hash, "0xc");
    EXPECT_STREQ(recs[0].packet_c_hex, "aa");
    EXPECT_STREQ(recs[1].packet_c_hex, "bb");
    EXPECT_STREQ(recs[2].packet_c_hex, "cc");
}

TEST(EgressJSON, RespectsMaxRecordsCap) {
    // 5 records in the array, caller asks for only 3 — scanner stops
    // after the 3rd (must not write past `out`).
    const char body[] =
        "["
          "{\"payment_id\":\"1\",\"settlement_tx_hash\":\"0xa\","
            "\"packet_c_hex\":\"11\"},"
          "{\"payment_id\":\"2\",\"settlement_tx_hash\":\"0xb\","
            "\"packet_c_hex\":\"22\"},"
          "{\"payment_id\":\"3\",\"settlement_tx_hash\":\"0xc\","
            "\"packet_c_hex\":\"33\"},"
          "{\"payment_id\":\"4\",\"settlement_tx_hash\":\"0xd\","
            "\"packet_c_hex\":\"44\"},"
          "{\"payment_id\":\"5\",\"settlement_tx_hash\":\"0xe\","
            "\"packet_c_hex\":\"55\"}"
        "]";
    egress::Record recs[3];
    EXPECT_EQ(egress::parse_pending_response(body, lit_len(body), recs, 3), 3);
    EXPECT_STREQ(recs[0].payment_id, "1");
    EXPECT_STREQ(recs[2].payment_id, "3");
}

TEST(EgressJSON, MalformedJSONRejectedWithNegative) {
    const char body[] = "{not valid json";
    egress::Record recs[4];
    EXPECT_LT(egress::parse_pending_response(body, lit_len(body), recs, 4), 0);
}

TEST(EgressJSON, UnterminatedStringRejected) {
    const char body[] =
        "[{\"payment_id\":\"never ends"; // closing quote + bracket missing
    egress::Record recs[4];
    EXPECT_LT(egress::parse_pending_response(body, lit_len(body), recs, 4), 0);
}

TEST(EgressJSON, MissingRequiredFieldRejected) {
    // Record has settlement_tx_hash + packet_c_hex but no payment_id.
    const char body[] =
        "[{\"settlement_tx_hash\":\"0xa\",\"packet_c_hex\":\"aa\"}]";
    egress::Record recs[4];
    EXPECT_LT(egress::parse_pending_response(body, lit_len(body), recs, 4), 0);
}

TEST(EgressJSON, OverlongPaymentIdRejected) {
    // Build a payment_id string of EgressPaymentIdMaxLen + 5 chars.
    char huge[egress::EgressPaymentIdMaxLen + 10];
    std::memset(huge, '9', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';

    char body[512];
    int n = std::snprintf(body, sizeof(body),
        "[{\"payment_id\":\"%s\",\"settlement_tx_hash\":\"0xa\","
        "\"packet_c_hex\":\"aa\"}]",
        huge);
    ASSERT_GT(n, 0);
    ASSERT_LT(static_cast<size_t>(n), sizeof(body));

    egress::Record recs[4];
    EXPECT_LT(egress::parse_pending_response(body,
                                              static_cast<size_t>(n),
                                              recs, 4),
              0);
}

TEST(EgressJSON, NullOrZeroInputsRejected) {
    egress::Record recs[4];
    EXPECT_LT(egress::parse_pending_response(nullptr, 10, recs, 4), 0);
    const char body[] = "[]";
    EXPECT_LT(egress::parse_pending_response(body, lit_len(body), nullptr, 4), 0);
    EXPECT_LT(egress::parse_pending_response(body, lit_len(body), recs, 0), 0);
}