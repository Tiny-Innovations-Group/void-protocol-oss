/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      service_poller.cpp
 * Desc:      VOID-142 — gateway status + Anvil block probes, bounded
 *            JSON scanning. Poll cadence lives in the render loop.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "service_poller.h"
#include "http_poller.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr char kGwHost[]   = "127.0.0.1";
constexpr int  kGwPort     = 8080;
constexpr int  kChainPort  = 8545;

// Bounded response body workspace shared per tick.
std::uint8_t g_body[8192];

// find "\"key\"" within body, returning the index just past the ':'
// or body_len when absent. Values are bounded by the body's own size.
std::size_t find_key(const char* body, const std::size_t body_len,
                     const char* key) {
    char needle[64];
    const int n = std::snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(needle)) {
        return body_len;
    }
    const std::size_t nlen = static_cast<std::size_t>(n);
    if (body_len < nlen) {
        return body_len;
    }
    for (std::size_t i = 0; i + nlen <= body_len; ++i) {
        if (std::memcmp(body + i, needle, nlen) == 0) {
            return i + nlen;
        }
    }
    return body_len;
}

} // namespace

int json_get_long(const char* body, const std::size_t body_len,
                  const char* key, long* out) {
    if (body == nullptr || out == nullptr || key == nullptr) {
        return 0;
    }
    const std::size_t at = find_key(body, body_len, key);
    if (at >= body_len) {
        return 0;
    }
    long value = 0;
    bool any = false;
    bool neg = false;
    std::size_t i = at;
    if (i < body_len && body[i] == '-') {
        neg = true;
        ++i;
    }
    for (; i < body_len && body[i] >= '0' && body[i] <= '9'; ++i) {
        value = value * 10 + (body[i] - '0');
        any = true;
    }
    if (!any) {
        return 0;
    }
    *out = neg ? -value : value;
    return 1;
}

int json_get_bool(const char* body, const std::size_t body_len,
                  const char* key, int* out) {
    if (body == nullptr || out == nullptr || key == nullptr) {
        return 0;
    }
    const std::size_t at = find_key(body, body_len, key);
    if (at >= body_len || at + 5 > body_len) {
        return 0;
    }
    if (std::memcmp(body + at, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (at + 6 <= body_len && std::memcmp(body + at, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

int json_get_str(const char* body, const std::size_t body_len,
                 const char* key, char* out, const std::size_t out_cap) {
    if (body == nullptr || out == nullptr || key == nullptr || out_cap == 0) {
        return 0;
    }
    out[0] = '\0';
    std::size_t at = find_key(body, body_len, key);
    if (at >= body_len || body[at] != '"') {
        return 0;
    }
    ++at;
    std::size_t n = 0;
    while (at + n < body_len && body[at + n] != '"' && n + 1 < out_cap) {
        out[n] = body[at + n];
        ++n;
    }
    if (at + n >= body_len) {
        out[0] = '\0';
        return 0;
    }
    out[n] = '\0';
    return 1;
}

void service_poll_tick(service_view_t* view) {
    if (view == nullptr) {
        return;
    }

    // --- Gateway /api/v1/status ---
    const http_result_t gw = http_get(kGwHost, kGwPort, "/api/v1/status",
                                      g_body, sizeof(g_body));
    view->gw_connected = (gw.ok && gw.status_code == 200) ? 1 : 0;
    if (view->gw_connected != 0 && gw.body_len > 0) {
        const char* body = reinterpret_cast<const char*>(g_body);
        long v = 0;
        if (json_get_long(body, gw.body_len, "uptime_sec", &v) != 0) {
            view->uptime_sec = v;
        }
        if (json_get_long(body, gw.body_len, "packets_in", &v) != 0) {
            view->packets_in = v;
        }
        if (json_get_long(body, gw.body_len, "sig_verify_ok", &v) != 0) {
            view->sig_ok = v;
        }
        if (json_get_long(body, gw.body_len, "sig_verify_fail", &v) != 0) {
            view->sig_fail = v;
        }
        if (json_get_long(body, gw.body_len, "intents_queued", &v) != 0) {
            view->intents_queued = v;
        }
        if (json_get_long(body, gw.body_len, "receipts_pending", &v) != 0) {
            view->receipts_pending = v;
        }
        if (json_get_long(body, gw.body_len, "receipts_dispatched", &v) != 0) {
            view->receipts_dispatched = v;
        }
        int b = 0;
        if (json_get_bool(body, gw.body_len, "chain_enabled", &b) != 0) {
            view->chain_enabled = b;
        }
        json_get_str(body, gw.body_len, "escrow_address",
                     view->escrow, sizeof(view->escrow));
    }

    // --- Anvil eth_blockNumber ---
    static const char kBlockReq[] =
        "{\"jsonrpc\":\"2.0\",\"method\":\"eth_blockNumber\",\"params\":[],\"id\":1}";
    const http_result_t ch = http_post_json(kGwHost, kChainPort, "/",
                                            kBlockReq, g_body, sizeof(g_body));
    view->chain_connected = 0;
    if (ch.ok && ch.status_code == 200 && ch.body_len > 0) {
        const char* body = reinterpret_cast<const char*>(g_body);
        char hex[16] = "";
        if (json_get_str(body, ch.body_len, "result", hex, sizeof(hex)) != 0 &&
            hex[0] == '0' && hex[1] == 'x') {
            // Bounded hex→decimal (block numbers fit in a long on any
            // conceivable flat-sat run).
            unsigned long block = 0;
            bool any = false;
            for (std::size_t i = 2; hex[i] != '\0'; ++i) {
                const char c = hex[i];
                unsigned digit = 0;
                if (c >= '0' && c <= '9')      digit = static_cast<unsigned>(c - '0');
                else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
                else break;
                block = (block << 4) | digit;
                any = true;
            }
            if (any) {
                std::snprintf(view->block_dec, sizeof(view->block_dec),
                              "%lu", block);
                view->chain_connected = 1;
            }
        }
    }
}
