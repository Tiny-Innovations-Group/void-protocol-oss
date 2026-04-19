/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_hex.h
 * Desc:      VOID-138 bounded ASCII-hex → bytes decoder. Used to turn
 *            the gateway's packet_c_hex string into the 112-byte PacketC
 *            the bouncer will LoRa-TX.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef EGRESS_HEX_H
#define EGRESS_HEX_H

#include <cstddef>
#include <cstdint>

namespace egress {

// Decode `hex_len` ASCII hex chars from `hex` into `out`. Writes
// `hex_len / 2` bytes on success. Accepts lower- and upper-case
// digits. Returns:
//   true  — exactly hex_len/2 bytes written to out
//   false — odd length / non-hex char / insufficient out_cap / NULL input
bool hex_decode(const char*  hex,
                size_t       hex_len,
                uint8_t*     out,
                size_t       out_cap);

} // namespace egress

#endif // EGRESS_HEX_H
