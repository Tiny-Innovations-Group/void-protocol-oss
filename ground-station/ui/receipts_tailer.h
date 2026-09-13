/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      receipts_tailer.h
 * Desc:      VOID-142 — read-only tailer for gateway/data/receipts.json.
 *            Append-only file re-read per poll; latest line per
 *            payment_id|settlement_tx_hash key wins (PENDING →
 *            DISPATCHED flips are second lines, VOID-135b). Operator
 *            CLEAR hides the current keys (view-only) — the file
 *            itself is never modified (TRL 4 restart evidence).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_RECEIPTS_TAILER_H
#define VOID_GROUND_STATION_UI_RECEIPTS_TAILER_H

#include <cstddef>

// The drawer shows current per-key state, newest at bottom (mockup's
// "newest last" semantics). Bounded table; re-resolve+re-read each
// poll so a gateway restart rewriting the file self-heals.
struct receipt_key_t {
    char payment_id[96];
    char tx_hash[80];
    char status[16];
    bool used;
};

struct receipts_view_t {
    char           path[300];       // resolved once (env or repo root)
    bool           path_ok;         // false → drawer shows "no file"
    receipt_key_t  keys[32];
    std::size_t    key_count;
    std::size_t    settlements;     // == key_count (unique keys)
    receipt_key_t  hidden[32];      // CLEAR snapshot — drawer skips these
    std::size_t    hidden_count;
};

// Resolve the path (VOID_RECEIPTS_PATH wins, else <repo root>/
// gateway/data/receipts.json via proc_manager's anchor) and scan the
// whole file into the upsert table. Cheap: a flat-sat file is KiBs.
void receipts_tail_tick(receipts_view_t* view);

// Operator CLEAR: snapshot every currently-known key into `hidden`.
// The drawer skips hidden (payment_id, tx_hash) pairs across the 2 Hz
// re-reads while NEW receipts still appear; receipts.json stays
// untouched. Bounded: at most sizeof(hidden)/sizeof(hidden[0]) keys.
void receipts_clear_view(receipts_view_t* view);

// 1 when (pid, hash) matches a hidden snapshot pair.
bool receipt_is_hidden(const receipts_view_t* view, const char* pid,
                       const char* hash);

#endif // VOID_GROUND_STATION_UI_RECEIPTS_TAILER_H
