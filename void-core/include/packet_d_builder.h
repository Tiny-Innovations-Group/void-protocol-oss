/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      packet_d_builder.h
 * Desc:      VOID-136 pure-function PacketD (Delivery) builder, SNLP tier.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_PACKET_D_BUILDER_H
#define VOID_PACKET_D_BUILDER_H

#include <cstddef>
#include <cstdint>

// SNLP-only. The journey-to-HAB plaintext alpha runs SNLP exclusively;
// a CCSDS variant would live in a parallel builder (not on the locked
// path — see docs/journey_to_hab_plain_text.md non-goals).
namespace packet_d_builder {

static constexpr size_t  kPacketDSize   = 136;  // SNLP PacketD_t total frame
static constexpr size_t  kPayloadSize   = 98;   // stripped PacketC body
static constexpr uint8_t kPacketDMagic  = 0xD0; // F-03 body-offset-0 magic

// Inputs needed to fully determine a PacketD frame. Header-level
// values (APID, sync word, seq_count, pkt_type) are held constant in
// build() to match the VOID-123 golden-vector generator — see
// gateway/test/utils/generate_packets.go::genPacketD.
struct DeliveryInputs {
    uint64_t downlink_ts;              // seller's ms-since-epoch at TX time
    uint32_t sat_b_id;                  // target buyer sat_id (copied from verified PacketC)
    uint8_t  payload[kPayloadSize];     // convention: stripped PacketC body (opaque to this builder)
};

// Writes exactly kPacketDSize bytes to `out` in SNLP wire format.
// Returns true iff `out_cap >= kPacketDSize`. Byte-identical to the
// Go generator's genPacketD(isSnlp=true) output when given the
// VOID-123 deterministic input set.
bool build(const DeliveryInputs& in, uint8_t* out, size_t out_cap);

}  // namespace packet_d_builder

#endif  // VOID_PACKET_D_BUILDER_H
