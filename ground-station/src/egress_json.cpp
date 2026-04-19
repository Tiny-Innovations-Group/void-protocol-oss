/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_json.cpp
 * Desc:      Bounded hand-rolled JSON scanner for VOID-138 egress path.
 *            Parses the gateway's GET /api/v1/egress/pending response
 *            (bare JSON array of records) and extracts the three fields
 *            the bouncer needs: payment_id, settlement_tx_hash,
 *            packet_c_hex. Other fields are parsed over and discarded.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------
 * Design notes:
 *  - Linear walk, no heap allocations, no std::string.
 *  - Bounded copy into caller-provided Record buckets; any string
 *    exceeding its bucket length rejects the WHOLE response (fail-closed).
 *  - Escape handling is minimal (pass-through after \\). The gateway
 *    fields we care about are decimal digits / hex / "0x…" — Go's
 *    encoding/json never escapes them. A future change that introduces
 *    quoted chars in these fields needs a parser rev.
 *  - Nested object detection uses a brace-depth counter so a
 *    gracefully-formed but verbose record (e.g. with a nested sub-
 *    object in a future schema) still terminates on the correct }.
 * -------------------------------------------------------------------------*/

#include "egress_json.h"

#include <cstring>

namespace egress {
namespace {

// Skip JSON whitespace (spaces, tabs, newlines, CR). Returns new pos.
size_t skip_ws(const char* body, size_t pos, size_t end) {
    while (pos < end) {
        const char c = body[pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        ++pos;
    }
    return pos;
}

// Extract a JSON string value. `pos` MUST point at the opening `"`.
// Copies the body into `out` (bounded by out_max-1 + a trailing NUL).
// Advances `pos` past the closing quote on success.
// Returns true on success, false on overflow / unterminated literal.
bool extract_string(const char* body, size_t& pos, size_t end,
                    char* out, size_t out_max) {
    if (out_max == 0) return false;
    if (pos >= end || body[pos] != '"') return false;
    ++pos; // skip opening quote
    size_t written = 0;
    while (pos < end) {
        char c = body[pos];
        if (c == '"') {
            // Closing quote — terminate output.
            if (written >= out_max) return false;
            out[written] = '\0';
            ++pos;
            return true;
        }
        // Pass-through after backslash (handles \\" in arbitrary JSON,
        // though the gateway's fields never emit escapes).
        if (c == '\\' && pos + 1 < end) {
            ++pos;
            c = body[pos];
        }
        if (written + 1 >= out_max) return false; // overflow (leave room for NUL)
        out[written] = c;
        ++written;
        ++pos;
    }
    return false; // unterminated string
}

// Locate `"<key>"` inside [start, end), step past the colon, and extract
// the JSON string value into `out`. Scans linearly; O(end-start) per key.
bool find_and_extract_key(const char* body, size_t start, size_t end,
                          const char* key_literal,
                          char* out, size_t out_max) {
    const size_t key_len = std::strlen(key_literal);
    size_t pos = start;
    while (pos < end) {
        // Advance to the next `"` — might be a key quote or a value quote.
        while (pos < end && body[pos] != '"') ++pos;
        if (pos >= end) return false;

        // Check that a full "<key>" fits in the remaining window.
        if (pos + 1 + key_len + 1 > end) return false;

        if (body[pos + 1 + key_len] == '"' &&
            std::memcmp(&body[pos + 1], key_literal, key_len) == 0) {
            // Matched "<key>". Step past the closing quote of the key,
            // skip ws, expect ':', skip ws, then extract the value.
            pos += 1 + key_len + 1;
            pos = skip_ws(body, pos, end);
            if (pos >= end || body[pos] != ':') return false;
            ++pos;
            pos = skip_ws(body, pos, end);
            return extract_string(body, pos, end, out, out_max);
        }

        // False positive (different key or value string) — step past
        // the current quote and keep scanning. Because the quote we
        // matched might be the OPEN quote of a value (which could
        // itself contain a `:` that would confuse naive parsers),
        // advance to the matching closing quote first.
        ++pos;
        while (pos < end) {
            const char c = body[pos];
            if (c == '\\' && pos + 1 < end) { pos += 2; continue; }
            if (c == '"') { ++pos; break; }
            ++pos;
        }
    }
    return false;
}

// Find the index one past the matching `}` for an object opening at
// `pos` (which MUST be `{`). Handles quoted strings and escapes so a
// `}` inside a string doesn't fool the brace counter. Returns
// body_len on unmatched / unterminated input.
size_t find_object_end(const char* body, size_t pos, size_t end) {
    if (pos >= end || body[pos] != '{') return end;
    int depth = 0;
    bool in_string = false;
    while (pos < end) {
        const char c = body[pos];
        if (in_string) {
            if (c == '\\' && pos + 1 < end) { pos += 2; continue; }
            if (c == '"') in_string = false;
            ++pos;
            continue;
        }
        if (c == '"')       { in_string = true; ++pos; continue; }
        else if (c == '{')  { ++depth; }
        else if (c == '}')  { --depth; if (depth == 0) { return pos + 1; } }
        ++pos;
    }
    return end; // unmatched
}

} // anonymous namespace

int parse_pending_response(const char* body,
                           size_t      body_len,
                           Record*     out,
                           size_t      max_records) {
    if (!body || body_len == 0 || !out || max_records == 0) return -1;

    size_t pos = skip_ws(body, 0, body_len);
    if (pos >= body_len || body[pos] != '[') return -1;
    ++pos;
    pos = skip_ws(body, pos, body_len);

    // Empty array — 0 records, valid response.
    if (pos < body_len && body[pos] == ']') return 0;

    size_t count = 0;
    while (pos < body_len) {
        pos = skip_ws(body, pos, body_len);
        if (pos >= body_len || body[pos] != '{') return -1;

        const size_t obj_start = pos;
        const size_t obj_end   = find_object_end(body, pos, body_len);
        if (obj_end >= body_len && (obj_end == body_len && body[body_len - 1] != '}')) {
            return -1;
        }
        if (obj_end == body_len) return -1;

        Record& r = out[count];
        // Zero each bucket's first byte so partial-extract still leaves
        // a valid C string.
        r.payment_id[0]         = '\0';
        r.settlement_tx_hash[0] = '\0';
        r.packet_c_hex[0]       = '\0';

        if (!find_and_extract_key(body, obj_start, obj_end, "payment_id",
                                  r.payment_id, EgressPaymentIdMaxLen))
            return -1;
        if (!find_and_extract_key(body, obj_start, obj_end, "settlement_tx_hash",
                                  r.settlement_tx_hash, EgressTxHashMaxLen))
            return -1;
        if (!find_and_extract_key(body, obj_start, obj_end, "packet_c_hex",
                                  r.packet_c_hex, EgressPacketCHexMaxLen))
            return -1;

        ++count;
        pos = obj_end;

        // Cap reached — stop even if more records remain.
        if (count >= max_records) {
            return static_cast<int>(count);
        }

        pos = skip_ws(body, pos, body_len);
        if (pos >= body_len) return -1;
        if (body[pos] == ',') {
            ++pos;
            continue;
        }
        if (body[pos] == ']') {
            return static_cast<int>(count);
        }
        return -1;
    }
    return -1;
}

} // namespace egress
