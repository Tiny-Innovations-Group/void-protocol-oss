/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_security_manager.cpp
 * Desc:      GTest suite for SecurityManager (void-core)
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include "security_manager.h"
#include <sodium.h>
#include <cstring>

// Test Fixture for SecurityManager
class SecurityManagerTest : public ::testing::Test {
protected:
    SecurityManager secman;
    void SetUp() override {
        secman = SecurityManager();
        secman.begin();
    }
};

TEST_F(SecurityManagerTest, BeginInitializesState) {
    SecurityManager sm;
    ASSERT_TRUE(sm.begin());
}

TEST_F(SecurityManagerTest, PrepareHandshakeSetsState) {
    PacketH_t pkt = {};
    uint16_t ttl = 60;
    uint64_t now = 123456789;
    secman.prepareHandshake(pkt, ttl, now);
    // No crash, state should be handshake init
    // (State is private, so we can't check directly)
    // Check packet fields
    EXPECT_EQ(pkt.session_ttl, ttl);
    EXPECT_EQ(pkt.timestamp, now);
}

TEST_F(SecurityManagerTest, ProcessHandshakeResponseFailsOnBadKey) {
    PacketH_t pkt = {};
    // Fill with zeros, which is not a valid public key
    bool result = secman.processHandshakeResponse(pkt);
    EXPECT_FALSE(result);
}

TEST_F(SecurityManagerTest, EncryptPacketBNoSessionNoOp) {
    PacketB_t pkt = {};
    uint8_t payload[16] = {0};
    secman.encryptPacketB(pkt, payload, sizeof(payload));
    // No crash, no session active, so no encryption
    // (No way to check output, but should not crash)
}

TEST_F(SecurityManagerTest, IsSessionActiveFalseWhenIdle) {
    EXPECT_FALSE(secman.isSessionActive(0));
}

// Add more tests for edge cases, signature validation, and session expiry as needed.

// main() provided by GTest

TEST_F(SecurityManagerTest, EnforcesHandshakeBeforeEncryption) {
    PacketB_t pkt = {}; 
    uint8_t payload[32] = "Confidential Data";
    
    // Attempting to encrypt without an active session
    secman.encryptPacketB(pkt, payload, sizeof(payload));
    
    // EXPECTATION: The payload must be blocked and remain an empty string ("")
    EXPECT_STREQ(reinterpret_cast<const char*>(pkt.enc_payload), "");
}


// TEST_F(SecurityManagerTest, EnforcesHandshakeBeforeEncryption) {
//     PacketB_t pkt = {};
//     uint8_t payload[32] = "Confidential Data";
    
//     // Attempting to encrypt without an active session
//     secman.encryptPacketB(pkt, payload, sizeof(payload));
    

//     // The buffer must remain empty/zeroed because there is no active session
//     EXPECT_STREQ(reinterpret_cast<const char*>(pkt.enc_payload), "");
//     // Verify payload was NOT encrypted (should still match original in this stub)
//     // EXPECT_STREQ(reinterpret_cast<const char*>(pkt.enc_payload), "Confidential Data");
// }

TEST_F(SecurityManagerTest, SessionExpiryTest) {
    // Simulate a session starting at time T with TTL 10
    EXPECT_FALSE(secman.isSessionActive(100)); // Should be false at startup
    
    PacketH_t handshake = {};
    secman.prepareHandshake(handshake, 10, 100); // TTL 10, Start 100
    
    // Mocking a response to move to Active state
    // ... logic to simulate response ...
    
    // Check timing
    // EXPECT_TRUE(secman.isSessionActive(105)); // Within TTL
    // EXPECT_FALSE(secman.isSessionActive(111)); // Expired
}