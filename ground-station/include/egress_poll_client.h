/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_poll_client.h
 * Desc:      VOID-138 HTTP poll client. Raw-socket GET/POST against the
 *            gateway's /api/v1/egress/pending + /api/v1/egress/ack
 *            endpoints (VOID-135b contract, pinned by the 27 Go tests
 *            in PR #17).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef EGRESS_POLL_CLIENT_H
#define EGRESS_POLL_CLIENT_H

#include <cstddef>
#include <cstdint>

namespace egress {

// EgressPollClient is the bouncer's half of the VOID-135 egress
// channel. One instance per gateway target; stateless, safe to
// re-use across many polls.
//
// Each call opens a fresh TCP connection, sends an HTTP/1.1 request
// with Connection: close, reads the response into a caller-provided
// buffer, and closes. No keep-alive state; matches the existing
// GatewayClient pattern in gateway_client.cpp so the two clients can
// share the same networking model.
//
// Transport failures (connect refused, send error, timeout) return
// false and leave the out-params untouched. HTTP-level failures
// (4xx, 5xx) return true with the status code set — the caller
// decides whether to retry (e.g. the poll loop treats 503 as "try
// again next tick", 404 ack as "drop the record locally").
// No abstract-interface parent: the orchestrator is templated on the
// client type so unit tests pass a mock struct with matching method
// signatures. Static dispatch only — no vtable, no heap.
class EgressPollClient {
public:
    EgressPollClient(const char* host, uint16_t port);

    // GET /api/v1/egress/pending. Writes the raw response body into
    // `body` (no NUL termination; caller reads `body_len` bytes). If
    // the advertised Content-Length exceeds `body_cap`, returns false
    // WITHOUT writing to `body` — the bouncer must not silently
    // truncate the dispatch queue.
    //
    // Returns:
    //   true  — HTTP round-trip completed; `status_code` set to the
    //           numeric status (200 on success, 503 if gateway store
    //           isn't wired, etc.)
    //   false — transport failure (connect refused, read error,
    //           body larger than buffer)
    bool fetch_pending(uint8_t* body,
                       size_t   body_cap,
                       size_t&  body_len,
                       int&     status_code);

    // POST /api/v1/egress/ack with body
    //   {"payment_id":"<payment_id>","settlement_tx_hash":"<tx>"}
    //
    // The payment_id and settlement_tx_hash strings must be trusted
    // (they came from the gateway's own /pending response), so this
    // method does not escape them — a raw '\0' or '"' in either would
    // produce malformed JSON. The gateway's test suite pins the output
    // format (decimal digits + "0x"-prefixed hex), so this is safe.
    //
    // Returns:
    //   true  — HTTP round-trip completed; `status_code` set (200 for
    //           success/idempotent ACK, 404 unknown, 400 bad JSON,
    //           503 store unavailable)
    //   false — transport failure
    bool ack_dispatched(const char* payment_id,
                        const char* settlement_tx_hash,
                        int&        status_code);

private:
    const char* host_;
    uint16_t    port_;
};

} // namespace egress

#endif // EGRESS_POLL_CLIENT_H