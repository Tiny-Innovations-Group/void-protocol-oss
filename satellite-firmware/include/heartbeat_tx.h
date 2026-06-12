/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      heartbeat_tx.h
 * Desc:      VOID-022 shared 30 s heartbeat telemetry service (seller+buyer).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_HEARTBEAT_TX_H
#define VOID_HEARTBEAT_TX_H

#include <cstdint>

// VOID-022: 30-second heartbeat telemetry service shared by the seller
// and buyer roles. SNLP-only (alpha); compiles to a no-op without the
// GPS stub or under the CCSDS tier.
namespace heartbeat_tx {

// sys_state IDs from docs/kaitai_struct/tig_common_types.ksy.
static constexpr uint8_t kSysStateRxActive  = 3u;  // listening / idle
static constexpr uint8_t kSysStateConnected = 4u;  // transaction in flight

// Call once per loop iteration while no RX is pending (TX shares the
// SX126x FIFO base with RX — transmitting before draining a received
// frame would clobber it). Emits when the 30 s timer is due AND a
// single CAD scan reports the channel clear (defers while busy —
// droppable telemetry, never forced). Returns true iff a frame was
// transmitted on this call.
bool service(uint16_t apid, uint8_t sys_state);

}  // namespace heartbeat_tx

#endif  // VOID_HEARTBEAT_TX_H
