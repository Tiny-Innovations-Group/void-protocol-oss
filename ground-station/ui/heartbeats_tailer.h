/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      heartbeats_tailer.h
 * Desc:      VOID-146 — incremental tailer for gateway/data/
 *            heartbeats.json (VOID-022 evidence log): per-APID live
 *            health upsert + bounded 1000-entry history ring. Feeds
 *            Panel 2's SAT HEALTH box and the side-panel HEARTBEATS
 *            tab. Zero gateway/protocol changes — read-only file tap.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_HEARTBEATS_TAILER_H
#define VOID_GROUND_STATION_UI_HEARTBEATS_TAILER_H

#include <cstddef>

// SAT HEALTH nodes (Panel 2). Bounded — a flat-sat bench flies 2; 6
// covers a wider bring-up without overflowing the frozen box height.
constexpr std::size_t kHbNodesMax = 6;

// History ring ceiling (task spec: last 1,000 entries). Static
// storage: 1000 × 36 B ≈ 36 KiB — bounded, zero heap.
constexpr std::size_t kHbHistMax = 1000;

// One live constellation node, keyed by APID (100 = Sat A seller,
// 101 = Sat B buyer — gateway telemetry/store.go). One row per APID;
// fields hold the latest gateway-validated frame's values.
struct hb_node_t {
    bool       used;
    int        apid;
    unsigned   vbatt_mv;    // wire vbatt_mv (millivolts)
    int        temp_c;      // wire temp_c — CENTIDEGREES (store.go)
    unsigned   sat_lock;    // GPS lock count
    unsigned   pressure_pa; // atmospheric pressure, Pa
    int        lat_fixed;   // degrees × 1e7
    int        lon_fixed;   // degrees × 1e7
    long long  last_rx_ms;  // gateway received_at_ms (freshness clock)
};

// One history row (side-panel HEARTBEATS tab). Reception time is
// pre-rendered once at ingest ("HH:MM:SS" local) — the tab never
// re-derives it per frame.
struct hb_rec_t {
    char     t[12];
    int      apid;
    unsigned vbatt_mv;
    int      temp_c;       // centidegrees
    unsigned sat_lock;
    unsigned pressure_pa;
    int      lat_fixed;
    int      lon_fixed;
};

struct heartbeats_view_t {
    char        path[300]; // resolved once (env or repo root)
    bool        path_ok;
    long        tail_off;  // bytes already consumed (incremental tail)
    hb_node_t   nodes[kHbNodesMax];
    std::size_t node_count;
    hb_rec_t    hist[kHbHistMax];
    std::size_t hist_count;
    std::size_t hist_next;  // circular write index
};

// Flat-sat roles (gateway telemetry/store.go: "100 seller / 101
// buyer"). Unknown APIDs still get a live row — labelled so a
// misconfigured board is visible, not hidden.
inline const char* hb_role_tag(const int apid) {
    return (apid == 100) ? "Sat A" : (apid == 101) ? "Sat B" : "Sat ?";
}

// 2 Hz poll: resolve the path (VOID_HEARTBEATS_PATH wins, else
// <repo root>/gateway/data/heartbeats.json via proc_manager's
// anchor), then consume only the bytes appended since the last
// poll. New complete lines upsert the live node table (keyed by
// APID — never duplicated) and push onto the history ring.
// Truncation (file replaced) resets the offset; a trailing partial
// line (gateway mid-append) is left for the next poll.
void heartbeats_tail_tick(heartbeats_view_t* view);

// Operator CLEAR (view-only): flush the history ring. The live node
// table and heartbeats.json itself are untouched — new heartbeats
// keep arriving into both.
void heartbeats_clear_view(heartbeats_view_t* view);

#endif // VOID_GROUND_STATION_UI_HEARTBEATS_TAILER_H
