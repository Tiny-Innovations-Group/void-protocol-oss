/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      pass_store.h
 * Desc:      VOID-141 console polish — bounded ring of per-pass records
 *            for the Panel-1 PASS HISTORY column. Each record is either
 *            a completed pass (streak-relative #, wall-clock time,
 *            A→D duration) or a tamper-reject RESET marker. Session
 *            lifetime only; receipts.json remains the persisted
 *            evidence (TRL 4). Single-threaded: push on tick, render
 *            via snapshot.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_PASS_STORE_H
#define VOID_GROUND_STATION_UI_PASS_STORE_H

#include <cstddef>
#include <cstring>

// One PASS HISTORY row. `t` is "HH:MM:SS" wall clock (bounded copy in
// main.cpp via localtime_r + snprintf). `n` is the streak-relative
// pass number (1-based, mirrors g_passes at close); reset rows carry
// n == 0 and no duration.
struct pass_rec_t {
    bool  used;      // slot holds a valid record
    bool  is_reset;  // true → tamper-reject RESET marker row
    int   n;         // streak-relative pass # (0 on reset rows)
    char  t[12];     // "14:32:05"
    float dur_s;     // A→D seconds (pass rows only)
};

// Newest 16 rows kept — the 10/10 gate plus reset markers with margin.
// A long demo cannot grow memory: the ring is a fixed static array.
constexpr std::size_t kPassRecsMax = 16;

struct pass_ring_t {
    pass_rec_t recs[kPassRecsMax];
    std::size_t count; // records currently stored (≤ kPassRecsMax)
    std::size_t next;  // circular write index
    bool        fresh; // a row landed since last render (autoscroll)
};

inline void pass_ring_reset(pass_ring_t* r) {
    if (r == nullptr) return;
    for (std::size_t i = 0; i < kPassRecsMax; ++i) {
        r->recs[i].used = false;
    }
    r->count = 0;
    r->next  = 0;
    r->fresh = false;
}

// Copy one record into the ring (bounded per field). `t` and `dur_s`
// are pre-formatted by the caller; only fixed-size copies happen here.
inline void pass_ring_push(pass_ring_t* r, const bool is_reset, const int n,
                           const char* t, const float dur_s) {
    if (r == nullptr) return;
    pass_rec_t& slot = r->recs[r->next];
    slot.used     = true;
    slot.is_reset = is_reset;
    slot.n        = n;
    slot.dur_s    = dur_s;
    if (t != nullptr) {
        std::memcpy(slot.t, t, sizeof(slot.t) - 1);
        slot.t[sizeof(slot.t) - 1] = '\0';
    } else {
        slot.t[0] = '\0';
    }
    r->next = (r->next + 1) % kPassRecsMax;
    if (r->count < kPassRecsMax) {
        ++r->count;
    }
    r->fresh = true; // renderer autoscrolls to the new row
}

// Fetch the record `back` entries from the newest (0 = newest).
// Returns nullptr when out of range — callers render what exists.
inline const pass_rec_t* pass_ring_peek(const pass_ring_t* r,
                                        const std::size_t back) {
    if (r == nullptr || back >= r->count) return nullptr;
    const std::size_t idx =
        (r->next + kPassRecsMax - 1 - back) % kPassRecsMax;
    return &r->recs[idx];
}

#endif // VOID_GROUND_STATION_UI_PASS_STORE_H
