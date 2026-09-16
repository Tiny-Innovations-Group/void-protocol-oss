/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      service_poller.h
 * Desc:      VOID-142 — live service view model: gateway /api/v1/status
 *            + Anvil eth_blockNumber, bounded hand-rolled JSON scans.
 *            Polled from the render loop (single-threaded, no workers).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_SERVICE_POLLER_H
#define VOID_GROUND_STATION_UI_SERVICE_POLLER_H

#include <cstddef>

// Snapshot the panels render from. All-bounded; no strings owned
// elsewhere. Values hold only what Panel 2/3 display.
struct service_view_t {
    int  gw_connected;       // last GET /status returned HTTP 200
    long uptime_sec;
    long packets_in;
    long sig_ok;
    long sig_fail;
    long intents_queued;
    long receipts_pending;
    long receipts_dispatched;
    int  chain_enabled;      // gateway reports on-chain pipeline
    char escrow[48];         // gateway-reported contract address

    int  chain_connected;    // eth_blockNumber produced a parseable block
    char block_dec[24];      // decimal block number ("--" never written)
};

// One poll cycle. Both targets are probed; failures flip the respective
// *_connected flag without touching the last good numbers (the frozen
// readout keeps showing stale-but-labelled data, the Status field is
// the health signal).
void service_poll_tick(service_view_t* view);

// Bounded JSON scans over an in-memory body (no std::string, no heap).
// Extract "key":number / "key":true|false / "key":"string" — enough
// for the status and eth_blockNumber payloads.
int  json_get_long(const char* body, std::size_t body_len, const char* key, long* out);
int  json_get_bool(const char* body, std::size_t body_len, const char* key, int* out);
int  json_get_str(const char* body, std::size_t body_len, const char* key,
                  char* out, std::size_t out_cap);

#endif // VOID_GROUND_STATION_UI_SERVICE_POLLER_H
