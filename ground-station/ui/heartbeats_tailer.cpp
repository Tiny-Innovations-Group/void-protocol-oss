/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      heartbeats_tailer.cpp
 * Desc:      VOID-146 — heartbeats.json incremental tailer: bounded
 *            JSON scans, per-APID live upsert, history ring push.
 *            Mirrors receipts_tailer's path resolution; heartbeats
 *            are append-only evidence, so the tail is offset-based
 *            (no full re-scan, no upsert semantics needed).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "heartbeats_tailer.h"
#include "service_poller.h" // json_get_long
#include "proc_manager.h"   // proc_repo_root

#include <cstdio>
#include <cstdlib> // std::getenv (VOID_HEARTBEATS_PATH)
#include <cstring>
#include <ctime>   // localtime_r (reception-time rendering)

namespace {

// One heartbeats.json line ≈ 400 B (raw_hex 96 chars dominates).
// 4 KiB mirrors the receipts tailer's headroom.
constexpr std::size_t kLineCap = 4096;

void resolve_path(heartbeats_view_t* view) {
    if (view->path_ok) {
        return;
    }
    const char* env = std::getenv("VOID_HEARTBEATS_PATH");
    if (env != nullptr && env[0] != '\0') {
        std::snprintf(view->path, sizeof(view->path), "%s", env);
    } else {
        const char* root = proc_repo_root();
        if (root == nullptr || root[0] == '\0') {
            return; // path_ok stays false — nothing to tail
        }
        std::snprintf(view->path, sizeof(view->path),
                      "%s/gateway/data/heartbeats.json", root);
    }
    view->path_ok = true;
}

// received_at_ms is int64 Unix-ms (~1.75e12) — deliberately NOT routed
// through json_get_long (`long` is 32-bit on LLP64 targets and would
// silently corrupt the freshness clock). Same key-match semantics as
// service_poller's find_key: "\"key\":" then bounded digit scan with
// an overflow guard. Returns 1 on success, 0 on miss.
int hb_json_get_i64(const char* body, const std::size_t body_len,
                    const char* key, long long* out) {
    if (body == nullptr || out == nullptr || key == nullptr) {
        return 0;
    }
    char needle[64];
    const int n = std::snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(needle)) {
        return 0;
    }
    const std::size_t nlen = static_cast<std::size_t>(n);
    std::size_t at = body_len;
    for (std::size_t i = 0; i + nlen <= body_len; ++i) {
        if (std::memcmp(body + i, needle, nlen) == 0) {
            at = i + nlen;
            break;
        }
    }
    if (at >= body_len) {
        return 0;
    }
    long long value = 0;
    bool any = false;
    std::size_t i = at;
    if (i < body_len && body[i] == '-') {
        ++i; // received_at_ms is positive in practice; bound anyway
    }
    for (; i < body_len && body[i] >= '0' && body[i] <= '9'; ++i) {
        if (value > (1000000000000000000LL - (body[i] - '0')) / 10) {
            return 0; // overflow guard — refuse absurd timestamps
        }
        value = value * 10 + (body[i] - '0');
        any = true;
    }
    if (!any) {
        return 0;
    }
    *out = value;
    return 1;
}

// Ingest one parsed record: live-table upsert (keyed by APID —
// latest frame wins, one row per satellite, never duplicated) plus
// one history-ring push. Ring order == file order == arrival order.
void ingest(heartbeats_view_t* view, const long long rx_ms, const long apid,
            const long vbatt_mv, const long temp_c, const long sat_lock,
            const long pressure_pa, const long lat_fixed,
            const long lon_fixed) {
    // Live table. A full table still records history below — the
    // ring is the evidence, the box is the summary.
    hb_node_t* slot = nullptr;
    for (std::size_t i = 0; i < view->node_count; ++i) {
        if (view->nodes[i].used && view->nodes[i].apid == apid) {
            slot = &view->nodes[i];
            break;
        }
    }
    if (slot == nullptr && view->node_count < kHbNodesMax) {
        slot = &view->nodes[view->node_count++];
        slot->used = true;
        slot->apid = static_cast<int>(apid);
    }
    if (slot != nullptr) {
        slot->vbatt_mv    = static_cast<unsigned>(vbatt_mv);
        slot->temp_c      = static_cast<int>(temp_c);
        slot->sat_lock    = static_cast<unsigned>(sat_lock);
        slot->pressure_pa = static_cast<unsigned>(pressure_pa);
        slot->lat_fixed   = static_cast<int>(lat_fixed);
        slot->lon_fixed   = static_cast<int>(lon_fixed);
        slot->last_rx_ms  = rx_ms;
    }

    // History ring.
    hb_rec_t& rec = view->hist[view->hist_next];
    rec.apid       = static_cast<int>(apid);
    rec.vbatt_mv   = static_cast<unsigned>(vbatt_mv);
    rec.temp_c     = static_cast<int>(temp_c);
    rec.sat_lock   = static_cast<unsigned>(sat_lock);
    rec.pressure_pa = static_cast<unsigned>(pressure_pa);
    rec.lat_fixed   = static_cast<int>(lat_fixed);
    rec.lon_fixed   = static_cast<int>(lon_fixed);
    // Reception time rendered ONCE at ingest from the gateway's
    // received_at_ms — the tab renders the stored string, never
    // re-derives it per frame (and a UI restart still shows the
    // true ground-reception time, not the re-read time).
    const std::time_t secs = static_cast<std::time_t>(rx_ms / 1000);
    struct tm tm_buf;
    if (localtime_r(&secs, &tm_buf) != nullptr) {
        std::snprintf(rec.t, sizeof(rec.t), "%02d:%02d:%02d",
                      tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    } else {
        rec.t[0] = '\0';
    }
    view->hist_next = (view->hist_next + 1) % kHbHistMax;
    if (view->hist_count < kHbHistMax) {
        view->hist_count++;
    }
}

} // namespace

void heartbeats_tail_tick(heartbeats_view_t* view) {
    if (view == nullptr) {
        return;
    }
    resolve_path(view);
    if (!view->path_ok) {
        return;
    }
    FILE* f = std::fopen(view->path, "r");
    if (f == nullptr) {
        return; // no gateway / no heartbeats yet — keep prior state
    }
    // Truncation (file replaced by a fresh session) → restart the
    // tail; live rows re-populate from the new file.
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return;
    }
    const long size = std::ftell(f);
    if (size < 0) {
        std::fclose(f);
        return;
    }
    if (size < view->tail_off) {
        view->tail_off = 0;
    }
    if (std::fseek(f, view->tail_off, SEEK_SET) != 0) {
        std::fclose(f);
        return;
    }

    // Consume only complete lines. A trailing partial line (gateway
    // mid-append) stays unconsumed so the next poll re-reads it from
    // its start once the newline lands.
    long consumed = view->tail_off;
    char line[kLineCap];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        const std::size_t len = std::strlen(line);
        if (len == 0) {
            break;
        }
        if (line[len - 1] != '\n') {
            if (len + 1 >= sizeof(line)) {
                // Over-long line: skip past its newline and move on —
                // a wedged offset would silently blind the tailer.
                int c = std::fgetc(f);
                while (c != EOF && c != '\n') {
                    ++consumed;
                    c = std::fgetc(f);
                }
                if (c == '\n') {
                    ++consumed;
                }
                continue;
            }
            break; // partial tail — leave for the next poll
        }
        consumed += static_cast<long>(len);
        if (len < 12) {
            continue; // garbage / blank
        }
        long apid = 0;
        if (json_get_long(line, len, "apid", &apid) == 0) {
            continue; // apid is the one mandatory field
        }
        long long rx_ms = 0;
        (void)hb_json_get_i64(line, len, "received_at_ms", &rx_ms);
        long vbatt_mv = 0;
        long temp_c = 0;
        long sat_lock = 0;
        long pressure_pa = 0;
        long lat_fixed = 0;
        long lon_fixed = 0;
        (void)json_get_long(line, len, "vbatt_mv", &vbatt_mv);
        (void)json_get_long(line, len, "temp_c", &temp_c);
        (void)json_get_long(line, len, "sat_lock", &sat_lock);
        (void)json_get_long(line, len, "pressure_pa", &pressure_pa);
        (void)json_get_long(line, len, "lat_fixed", &lat_fixed);
        (void)json_get_long(line, len, "lon_fixed", &lon_fixed);
        ingest(view, rx_ms, apid, vbatt_mv, temp_c, sat_lock,
               pressure_pa, lat_fixed, lon_fixed);
    }
    view->tail_off = consumed;
    std::fclose(f);
}

void heartbeats_clear_view(heartbeats_view_t* view) {
    if (view == nullptr) {
        return;
    }
    view->hist_count = 0;
    view->hist_next  = 0;
    view->hist[0].t[0] = '\0';
}
