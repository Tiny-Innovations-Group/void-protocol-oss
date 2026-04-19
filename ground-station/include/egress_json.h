/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_json.h
 * Desc:      VOID-138 bounded JSON scanner for the bouncer's GET
 *            /api/v1/egress/pending response path.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef EGRESS_JSON_H
#define EGRESS_JSON_H

#include <cstddef>

namespace egress {

// Per-field buffer sizes. The gateway's Record produces:
//   payment_id           — Go big.Int.String() for a uint256: up to 78
//                          decimal digits; pad to 96 for safety.
//   settlement_tx_hash   — "0x" + 64 hex = 66 chars; pad to 80.
//   packet_c_hex         — 112 bytes × 2 = 224 hex chars; pad to 240.
// Each bucket is +1 for the trailing NUL the scanner always writes.
// `static constexpr` chosen over `inline constexpr` because the latter
// is a C++17 feature and this tree builds strict C++14 (ESP32 parity).
static constexpr size_t EgressPaymentIdMaxLen  = 96;
static constexpr size_t EgressTxHashMaxLen     = 80;
static constexpr size_t EgressPacketCHexMaxLen = 240;

// The subset of the gateway's Record the bouncer actually needs to act
// on. Other JSON fields (sat_id, amount, asset_id, wallet,
// block_number, ts_ms, dispatch_status) are parsed over and discarded.
//
// All string fields are NUL-terminated C strings with bounded capacity.
// The scanner MUST write the NUL on every successful parse and MUST
// refuse any input where a value would overflow its bucket.
struct Record {
    char payment_id[EgressPaymentIdMaxLen];
    char settlement_tx_hash[EgressTxHashMaxLen];
    char packet_c_hex[EgressPacketCHexMaxLen];
};

// Parses a bare JSON array of records from `body` (length `body_len`).
// On success, writes up to `max_records` entries into `out` (oldest
// first, matching array order) and returns the number written.
//
// Return semantics:
//   >= 0  success; that many records filled
//   < 0   parse error — `out` contents are unspecified and MUST NOT
//         be read by the caller
//
// Reject conditions (all negative):
//   - body / out NULL, body_len == 0, max_records == 0
//   - missing array brackets
//   - any required field (payment_id, settlement_tx_hash,
//     packet_c_hex) absent from a record
//   - any string value exceeds its bucket length
//   - unterminated string literal
//
// The scanner is LINEAR in body_len and writes NO heap allocations.
int parse_pending_response(const char* body,
                           size_t      body_len,
                           Record*     out,
                           size_t      max_records);

} // namespace egress

#endif // EGRESS_JSON_H