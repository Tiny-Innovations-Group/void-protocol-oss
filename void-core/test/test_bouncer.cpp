/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * File:      test_bouncer.cpp
 * Desc:      Edge firewall validation for Ground Station.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include "bouncer.h" // Testing the class logic directly

class BouncerTest : public ::testing::Test {
protected:
    Bouncer edge_gate;
    uint8_t out_buf[512];
};

TEST_F(BouncerTest, RejectsInvalidSize) {
    uint8_t garbage[10] = {0};
    // Packet B expects 176 bytes; 10 bytes should be rejected immediately
    bool result = edge_gate.process_packet(garbage, sizeof(garbage), out_buf, sizeof(out_buf));
    EXPECT_FALSE(result) << "Bouncer failed to reject an undersized packet.";
}

TEST_F(BouncerTest, ProtectsOutputBuffer) {
    uint8_t valid_pkt_b[176] = {0};
    uint8_t tiny_out[10]; // Too small for Packet B payload
    
    // Bouncer should detect the output buffer is too small for decryption
    bool result = edge_gate.process_packet(valid_pkt_b, 176, tiny_out, sizeof(tiny_out));
    EXPECT_FALSE(result) << "Bouncer allowed a potential stack overflow into out_buf.";
}