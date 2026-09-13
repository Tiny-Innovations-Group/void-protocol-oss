/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      log_store.h
 * Desc:      VOID-142 — bounded ring of fixed-width log lines. Render
 *            loop is single-threaded, so no locking: push from the
 *            tick drain, render via a concatenated snapshot buffer.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_LOG_STORE_H
#define VOID_GROUND_STATION_UI_LOG_STORE_H

#include <cstddef>
#include <cstdio>
#include <cstring>

// One line of log output (child stdout, tailer output, poll echoes).
constexpr std::size_t kLogLineCap = 128;
// Rings hold the newest 64 lines — panels show a handful, scroll back
// is bounded so a long demo cannot grow memory.
constexpr std::size_t kLogLinesMax = 64;

struct log_ring_t {
    char        lines[kLogLinesMax][kLogLineCap];
    std::size_t count; // lines currently stored (≤ kLogLinesMax)
    std::size_t next;  // circular write index
};

inline void log_ring_reset(log_ring_t* r) {
    if (r == nullptr) return;
    r->count = 0;
    r->next  = 0;
    r->lines[0][0] = '\0';
}

// Copy `line` into the ring (NUL-terminated, truncated at kLogLineCap-1).
inline void log_ring_push(log_ring_t* r, const char* line) {
    if (r == nullptr || line == nullptr) return;
    std::snprintf(r->lines[r->next], kLogLineCap, "%s", line);
    r->next = (r->next + 1) % kLogLinesMax;
    if (r->count < kLogLinesMax) {
        r->count++;
    }
}

// Concatenate oldest→newest into `out` (≤cap, always NUL-terminated).
// Matches the frozen mockup's "newest at bottom" semantics.
inline void log_ring_render(const log_ring_t* r, char* out, std::size_t cap) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (r == nullptr || r->count == 0) return;
    std::size_t used = 0;
    // Oldest is (next - count) mod kLogLinesMax.
    std::size_t idx = (r->next + kLogLinesMax - r->count) % kLogLinesMax;
    for (std::size_t i = 0; i < r->count; ++i) {
        const char* line = r->lines[idx];
        const std::size_t len = std::strlen(line);
        if (used + len + 2 > cap) {
            break; // leave room for '\n' + NUL
        }
        std::memcpy(out + used, line, len);
        used += len;
        out[used++] = '\n';
        idx = (idx + 1) % kLogLinesMax;
    }
    out[used] = '\0';
}

#endif // VOID_GROUND_STATION_UI_LOG_STORE_H
