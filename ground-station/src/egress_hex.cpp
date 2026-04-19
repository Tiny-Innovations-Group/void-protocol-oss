/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_hex.cpp
 * Desc:      VOID-138 bounded ASCII-hex decoder.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "egress_hex.h"

namespace egress {
namespace {

// Table-free nibble decode. Returns a negative value on a non-hex byte
// so the caller can fail-closed without branching on ranges.
int nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

} // anonymous namespace

bool hex_decode(const char*  hex,
                size_t       hex_len,
                uint8_t*     out,
                size_t       out_cap) {
    if (hex == nullptr || out == nullptr) return false;
    if ((hex_len & 1u) != 0u) return false;                 // odd length
    const size_t need = hex_len / 2u;
    if (need > out_cap) return false;                       // overflow
    for (size_t i = 0; i < need; ++i) {
        const int hi = nibble(hex[i * 2u]);
        const int lo = nibble(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) return false;                 // non-hex
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

} // namespace egress
