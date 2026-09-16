/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      log_store.h
 * Desc:      VOID-142 — bounded ring of fixed-width log lines with a
 *            source tag ("gw" / "anvil" / "ui"). Render filter selects
 *            one source or all. Single-threaded: push on tick, render
 *            via snapshot buffer.
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
    char        tags[kLogLinesMax][8];
    char        lines[kLogLinesMax][kLogLineCap];
    std::size_t count; // lines currently stored (≤ kLogLinesMax)
    std::size_t next;  // circular write index
};

inline void log_ring_reset(log_ring_t* r) {
    if (r == nullptr) return;
    r->count = 0;
    r->next  = 0;
    r->lines[0][0] = '\0';
    r->tags[0][0]  = '\0';
}

// Copy (tag, line) into the ring. Truncation is bounded per field.
inline void log_ring_push(log_ring_t* r, const char* tag, const char* line) {
    if (r == nullptr || line == nullptr) return;
    std::snprintf(r->tags[r->next], 8, "%s", (tag != nullptr) ? tag : "");
    std::snprintf(r->lines[r->next], kLogLineCap, "%s", line);
    r->next = (r->next + 1) % kLogLinesMax;
    if (r->count < kLogLinesMax) {
        r->count++;
    }
}

// Concatenate oldest→newest into `out`, filtered by tag when `filter`
// is non-empty ("newest at bottom" semantics preserved).
inline void log_ring_render(const log_ring_t* r, char* out, std::size_t cap,
                            const char* filter) {
    if (out == nullptr || cap == 0) return;
    out[0] = '\0';
    if (r == nullptr || r->count == 0) return;
    const bool filter_on = (filter != nullptr && filter[0] != '\0');
    std::size_t used = 0;
    // Oldest is (next - count) mod kLogLinesMax.
    std::size_t idx = (r->next + kLogLinesMax - r->count) % kLogLinesMax;
    for (std::size_t i = 0; i < r->count; ++i) {
        if (filter_on && std::strcmp(r->tags[idx], filter) != 0) {
            idx = (idx + 1) % kLogLinesMax;
            continue;
        }
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

// VOID-145: raw serial debug ring. Same bounded-ring pattern as
// log_ring_t, but lines up to 512 chars — raw wire lines carry full
// hex payloads (PACKET_B: + 384 hex ≈ 393 chars) that the 128-char
// kLogLineCap would truncate to uselessness. Mirrors the bouncer's
// own line_buf[512], so the tab shows everything the bouncer can
// parse. 64 × 512 = 32 KiB static storage; zero heap.
constexpr std::size_t kDebugLineCap = 512;

struct debug_ring_t {
    char        lines[kLogLinesMax][kDebugLineCap];
    std::size_t count; // lines currently stored (≤ kLogLinesMax)
    std::size_t next;  // circular write index
};

inline void debug_ring_reset(debug_ring_t* r) {
    if (r == nullptr) return;
    r->count = 0;
    r->next  = 0;
    r->lines[0][0] = '\0';
}

// Copy line into the ring. Truncation is bounded per field.
inline void debug_ring_push(debug_ring_t* r, const char* line) {
    if (r == nullptr || line == nullptr) return;
    std::snprintf(r->lines[r->next], kDebugLineCap, "%s", line);
    r->next = (r->next + 1) % kLogLinesMax;
    if (r->count < kLogLinesMax) {
        r->count++;
    }
}

// Concatenate oldest→newest into `out` ("newest at bottom" semantics
// preserved; no filter — the ring has a single source by construction).
inline void debug_ring_render(const debug_ring_t* r, char* out,
                              std::size_t cap) {
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
