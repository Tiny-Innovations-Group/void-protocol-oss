/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_egress_orchestrator.cpp
 * Desc:      VOID-138 red-green for the full drain-pending orchestration.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "egress_orchestrator.h"

// ---------------------------------------------------------------------------
// Mock HTTP client — canned responses + ACK recorder.
//
// No inheritance / no vtable. The orchestrator is templated on the
// client type so any struct with matching method signatures plugs in
// (static dispatch, no heap / no virtual).
// ---------------------------------------------------------------------------
struct MockHttpClient {
    // Canned /pending response body + status.
    std::string pending_body;
    int         pending_status = 200;
    bool        pending_returns = true; // false simulates transport failure

    // Canned /ack response status.
    int         ack_status = 200;
    bool        ack_returns = true;

    struct AckCall {
        std::string payment_id;
        std::string tx_hash;
    };
    std::vector<AckCall> acks_received;

    bool fetch_pending(uint8_t* body, size_t cap, size_t& len,
                       int& status_code) {
        if (!pending_returns) return false;
        status_code = pending_status;
        if (pending_body.size() > cap) return false;
        std::memcpy(body, pending_body.data(), pending_body.size());
        len = pending_body.size();
        return true;
    }

    bool ack_dispatched(const char* pid, const char* tx,
                        int& status_code) {
        acks_received.push_back({pid, tx});
        if (!ack_returns) return false;
        status_code = ack_status;
        return true;
    }
};

// ---------------------------------------------------------------------------
// LoRa TX mock — records the 112-byte frames the orchestrator hands out.
// ---------------------------------------------------------------------------
struct TxCapture {
    std::vector<std::vector<uint8_t>> frames;
    bool next_returns = true; // flip to false to simulate TX failure
};

static bool capture_tx(const uint8_t* data, size_t len, void* user) {
    auto* cap = static_cast<TxCapture*>(user);
    cap->frames.emplace_back(data, data + len);
    return cap->next_returns;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(EgressOrchestrator, EmptyQueueIsZeroDispatched) {
    MockHttpClient client;
    client.pending_body = "[]";
    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);

    EXPECT_EQ(orch.tick(), 0);
    EXPECT_EQ(cap.frames.size(), 0u);
    EXPECT_EQ(client.acks_received.size(), 0u);
}

TEST(EgressOrchestrator, SingleRecordDispatchesAndAcks) {
    MockHttpClient client;
    // Build 112-byte PacketC = 224 hex chars. Use a recognisable pattern
    // so we can assert the decoded bytes match.
    std::string hex;
    hex.reserve(224);
    for (size_t i = 0; i < 112; ++i) {
        static const char kDigits[] = "0123456789abcdef";
        uint8_t b = static_cast<uint8_t>(i & 0xFF);
        hex += kDigits[(b >> 4) & 0xF];
        hex += kDigits[b & 0xF];
    }
    client.pending_body =
        "[{\"payment_id\":\"42\",\"settlement_tx_hash\":\"0xbeef\","
        "\"packet_c_hex\":\"" + hex + "\"}]";

    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);
    ASSERT_EQ(orch.tick(), 1);

    ASSERT_EQ(cap.frames.size(), 1u);
    ASSERT_EQ(cap.frames[0].size(), 112u);
    for (size_t i = 0; i < 112; ++i) {
        EXPECT_EQ(cap.frames[0][i], static_cast<uint8_t>(i & 0xFF));
    }

    ASSERT_EQ(client.acks_received.size(), 1u);
    EXPECT_EQ(client.acks_received[0].payment_id, "42");
    EXPECT_EQ(client.acks_received[0].tx_hash,    "0xbeef");
}

TEST(EgressOrchestrator, BatchOfThreeDispatchesInOrder) {
    MockHttpClient client;
    std::string hex(224, '0'); // 112 zero bytes — shape matters, not content
    client.pending_body =
        "["
          "{\"payment_id\":\"1\",\"settlement_tx_hash\":\"0xA\","
           "\"packet_c_hex\":\"" + hex + "\"},"
          "{\"payment_id\":\"2\",\"settlement_tx_hash\":\"0xB\","
           "\"packet_c_hex\":\"" + hex + "\"},"
          "{\"payment_id\":\"3\",\"settlement_tx_hash\":\"0xC\","
           "\"packet_c_hex\":\"" + hex + "\"}"
        "]";
    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);
    ASSERT_EQ(orch.tick(), 3);

    ASSERT_EQ(cap.frames.size(), 3u);
    ASSERT_EQ(client.acks_received.size(), 3u);
    EXPECT_EQ(client.acks_received[0].payment_id, "1");
    EXPECT_EQ(client.acks_received[1].payment_id, "2");
    EXPECT_EQ(client.acks_received[2].payment_id, "3");
}

TEST(EgressOrchestrator, TransportFailureOnFetchReturnsNegative) {
    MockHttpClient client;
    client.pending_returns = false; // simulate connect-refused
    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);

    EXPECT_LT(orch.tick(), 0);
    EXPECT_EQ(cap.frames.size(), 0u);
    EXPECT_EQ(client.acks_received.size(), 0u);
}

TEST(EgressOrchestrator, NonOkStatusOnFetchReturnsZeroNoAcks) {
    MockHttpClient client;
    client.pending_body   = "{\"error\":\"…\"}";
    client.pending_status = 503;
    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);

    // Transport succeeded but the gateway isn't wired — nothing to
    // dispatch. Orchestrator returns 0, does NOT treat it as a hard
    // error (poll loop will retry next tick).
    EXPECT_EQ(orch.tick(), 0);
    EXPECT_EQ(cap.frames.size(), 0u);
    EXPECT_EQ(client.acks_received.size(), 0u);
}

TEST(EgressOrchestrator, MalformedJsonReturnsNegative) {
    MockHttpClient client;
    client.pending_body = "{not json";
    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);

    EXPECT_LT(orch.tick(), 0);
    EXPECT_EQ(cap.frames.size(), 0u);
    EXPECT_EQ(client.acks_received.size(), 0u);
}

TEST(EgressOrchestrator, BadPacketCHexSkipsRecord) {
    MockHttpClient client;
    // First record has a well-formed 224-char hex; second has odd length.
    std::string hex(224, 'a');
    client.pending_body =
        "["
          "{\"payment_id\":\"ok\",\"settlement_tx_hash\":\"0x1\","
           "\"packet_c_hex\":\"" + hex + "\"},"
          "{\"payment_id\":\"bad\",\"settlement_tx_hash\":\"0x2\","
           "\"packet_c_hex\":\"abc\"}" // odd length → decode fails
        "]";
    TxCapture cap;
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);

    // 1 record dispatched (the good one); bad record skipped.
    EXPECT_EQ(orch.tick(), 1);
    ASSERT_EQ(cap.frames.size(), 1u);
    ASSERT_EQ(client.acks_received.size(), 1u);
    EXPECT_EQ(client.acks_received[0].payment_id, "ok");
}

TEST(EgressOrchestrator, TxFailureMeansNoAck) {
    MockHttpClient client;
    std::string hex(224, '0');
    client.pending_body =
        "[{\"payment_id\":\"x\",\"settlement_tx_hash\":\"0xX\","
        "\"packet_c_hex\":\"" + hex + "\"}]";
    TxCapture cap;
    cap.next_returns = false; // LoRa TX fails
    egress::EgressOrchestrator<MockHttpClient> orch(client, capture_tx, &cap);

    // TX attempted but failed → no ACK sent → 0 dispatched-and-acked.
    EXPECT_EQ(orch.tick(), 0);
    EXPECT_EQ(cap.frames.size(), 1u);
    EXPECT_EQ(client.acks_received.size(), 0u);
}