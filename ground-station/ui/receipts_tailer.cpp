/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      receipts_tailer.cpp
 * Desc:      VOID-142 — receipts.json upsert scanner. Bounded line
 *            assembly; json_get_* helpers come from service_poller.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "receipts_tailer.h"
#include "service_poller.h" // json_get_str
#include "proc_manager.h"   // proc_repo_root

#include <cstdio>
#include <cstring>
#include <cstdlib> // std::getenv (VOID_RECEIPTS_PATH)

namespace {

// One receipts.json line ~600 B (packet_c_hex dominates). 4 KiB gives
// generous headroom without dragging memory around.
constexpr std::size_t kLineCap = 4096;

void resolve_path(receipts_view_t* view) {
    if (view->path_ok) {
        return;
    }
    const char* env = std::getenv("VOID_RECEIPTS_PATH");
    if (env != nullptr && env[0] != '\0') {
        std::snprintf(view->path, sizeof(view->path), "%s", env);
    } else {
        const char* root = proc_repo_root();
        if (root == nullptr || root[0] == '\0') {
            return; // path_ok stays false — nothing to tail
        }
        std::snprintf(view->path, sizeof(view->path),
                      "%s/gateway/data/receipts.json", root);
    }
    view->path_ok = true;
}

// Upsert one parsed line into the bounded table. Latest line with a
// given key wins — gateway semantics (VOID-135b state machine).
void upsert(receipts_view_t* view, const char* pid, const char* hash,
            const char* status) {
    receipt_key_t* slot = nullptr;
    for (std::size_t i = 0; i < view->key_count; ++i) {
        receipt_key_t& k = view->keys[i];
        if (k.used && std::strcmp(k.payment_id, pid) == 0 &&
            std::strcmp(k.tx_hash, hash) == 0) {
            slot = &k;
            break;
        }
    }
    if (slot == nullptr) {
        if (view->key_count >= sizeof(view->keys) / sizeof(view->keys[0])) {
            return; // table full — drop rather than overflow (CERT)
        }
        slot = &view->keys[view->key_count++];
        slot->used = true;
    }
    std::snprintf(slot->payment_id, sizeof(slot->payment_id), "%s", pid);
    std::snprintf(slot->tx_hash, sizeof(slot->tx_hash), "%s", hash);
    std::snprintf(slot->status, sizeof(slot->status), "%s",
                  (status != nullptr && status[0] != '\0') ? status : "PENDING");
}

} // namespace

void receipts_tail_tick(receipts_view_t* view) {
    if (view == nullptr) {
        return;
    }
    resolve_path(view);
    if (!view->path_ok) {
        view->key_count = 0;
        view->settlements = 0;
        return;
    }
    FILE* f = std::fopen(view->path, "r");
    if (f == nullptr) {
        view->key_count = 0;
        view->settlements = 0;
        return;
    }
    // Re-scan from scratch each poll — drop stale keys first so a
    // rewritten file (fresh anvil session) doesn't duplicate history.
    for (std::size_t i = 0; i < sizeof(view->keys) / sizeof(view->keys[0]); ++i) {
        view->keys[i].used = false;
    }
    view->key_count = 0;

    char line[kLineCap];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
        const std::size_t len = std::strlen(line);
        if (len < 12) {
            continue; // garbage / blank
        }
        char pid[96] = "";
        char hash[80] = "";
        char status[16] = "";
        if (json_get_str(line, len, "payment_id", pid, sizeof(pid)) == 0) {
            continue;
        }
        if (json_get_str(line, len, "settlement_tx_hash", hash, sizeof(hash)) == 0) {
            continue;
        }
        json_get_str(line, len, "dispatch_status", status, sizeof(status));
        upsert(view, pid, hash, status);
    }
    std::fclose(f);
    view->settlements = view->key_count;
}
