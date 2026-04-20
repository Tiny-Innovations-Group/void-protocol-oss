/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      ack_builder.h
 * Desc:      VOID-134 pure-function PacketAck builder (SNLP tier, alpha).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_ACK_BUILDER_H
#define VOID_ACK_BUILDER_H

#include <cstddef>
#include <cstdint>

// The journey-to-HAB plaintext alpha runs the SNLP tier exclusively.
// CCSDS-tier ACK framing is intentionally deferred — not in-scope for
// TRL 4 (see docs/journey_to_hab_plain_text.md non-goals).
namespace ack_builder {

static constexpr size_t  kPacketAckSize      = 136;  // SNLP PacketAck_t
static constexpr size_t  kEncTunnelSize      = 96;   // SNLP TunnelData_t
static constexpr uint8_t kPacketAckMagic     = 0xAC; // F-03 body-offset-0 magic
static constexpr uint8_t kAckStatusVerified  = 0x01; // RECEIVED_VERIFIED

// Inputs needed to fully determine a PacketAck frame. Header-level
// values (APID, sync word, sec flag, seq_count) are held constant
// inside build() to match the VOID-123 golden-vector generator — see
// gateway/test/utils/generate_packets.go::genPacketAck.
struct AckInputs {
    uint32_t target_tx_id;                    // Links ACK ↔ PacketB (convention: PacketB.sat_id)
    uint8_t  status;                           // kAckStatusVerified = 0x01
    uint16_t azimuth;                          // degrees
    uint16_t elevation;                        // degrees
    uint32_t frequency_hz;
    uint32_t duration_ms;
    uint8_t  enc_tunnel[kEncTunnelSize];       // opaque tunnel blob (plaintext pass-through in alpha)
};

// Writes exactly kPacketAckSize bytes to `out` in SNLP wire format.
// Returns true iff `out_cap >= kPacketAckSize`. Byte-identical to the
// Go generator's genPacketAck(true) output when given the VOID-123
// deterministic input set.
bool build(const AckInputs& in, uint8_t* out, size_t out_cap);

}  // namespace ack_builder

#endif  // VOID_ACK_BUILDER_H
