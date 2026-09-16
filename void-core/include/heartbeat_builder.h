/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      heartbeat_builder.h
 * Desc:      VOID-022 pure-function Heartbeat (Packet L) builder, SNLP tier.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_HEARTBEAT_BUILDER_H
#define VOID_HEARTBEAT_BUILDER_H

#include <cstddef>
#include <cstdint>

// SNLP-only. The journey-to-HAB plaintext alpha runs SNLP exclusively;
// a CCSDS variant would live in a parallel builder (not on the locked
// path — see docs/journey_to_hab_plain_text.md non-goals).
namespace heartbeat_builder {

static constexpr size_t   kHeartbeatSize = 48;  // SNLP HeartbeatPacket_t frame
static constexpr uint16_t kBodyLen       = 34;  // VOID-114B body bytes

// Inputs needed to fully determine a Heartbeat frame. Unlike PacketD the
// APID is an input: BOTH boards emit heartbeats and the APID is the only
// identity field on the frame (seller = 100, buyer = 101). Header
// seq_count / pkt_type stay constant to match the VOID-123 golden-vector
// generator — see gateway/test/utils/generate_packets.go::genPacketL.
struct HeartbeatInputs {
    uint16_t apid;           // emitter identity (SELLER_APID / BUYER_APID)
    uint64_t epoch_ts;       // ms since Unix epoch
    uint32_t pressure_pa;    // barometric pressure, Pa
    int32_t  lat_fixed;      // latitude  * 1e7 (signed)
    int32_t  lon_fixed;      // longitude * 1e7 (signed)
    uint16_t vbatt_mv;       // battery voltage, mV
    int16_t  temp_c;         // temperature, centidegrees C (signed)
    uint16_t gps_speed_cms;  // ground speed, cm/s
    uint8_t  sys_state;      // tig_common_types.ksy sys_state enum ID
    uint8_t  sat_lock;       // GPS satellites locked
};

// Writes exactly kHeartbeatSize bytes to `out` in SNLP wire format.
// Returns true iff `out_cap >= kHeartbeatSize`. Byte-identical to the
// Go generator's genPacketL(isSnlp=true) output when given the
// VOID-123 deterministic input set.
bool build(const HeartbeatInputs& in, uint8_t* out, size_t out_cap);

}  // namespace heartbeat_builder

#endif  // VOID_HEARTBEAT_BUILDER_H
