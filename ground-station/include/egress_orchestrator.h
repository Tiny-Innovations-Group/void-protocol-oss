/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_orchestrator.h
 * Desc:      VOID-138 orchestrator — ties the HTTP poll client, JSON
 *            scanner, hex decoder, and bouncer's LoRa-TX path into one
 *            "drain the gateway's pending queue" operation.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------
 * Design: template-based static dispatch. The orchestrator takes the
 *         HTTP client type as a template parameter so tests can inject
 *         a duck-typed mock without an abstract base class. No virtual
 *         functions, no vtables, no heap — all dispatch resolves at
 *         compile time and inlines.
 * -------------------------------------------------------------------------*/

#ifndef EGRESS_ORCHESTRATOR_H
#define EGRESS_ORCHESTRATOR_H

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "egress_hex.h"
#include "egress_json.h"

namespace egress {

// Function pointer for the LoRa TX callback. No std::function
// (avoids heap captures). `user` is an opaque context the caller
// provides at construction time — typically a SerialHAL pointer or
// equivalent. Returns true on successful TX so the orchestrator
// only ACKs successfully-transmitted records.
using LoraTxFn = bool (*)(const uint8_t* data, size_t len, void* user);

// Per-tick soft cap. Matches the gateway's GET /pending cap of 10,
// so one tick drains at most 10 receipts. Keeping the cap local
// (not shared with gateway/internal/api/handlers) because the
// bouncer must be self-consistent even if the gateway's cap
// changes — sanity backstop, not a coupling.
constexpr size_t EgressMaxPerTick = 10;

// SNLP PacketC frame size — sourced from void_packets_snlp.h
// (14 B header + 98 B body block = 112 B). Duplicated here as a
// compile-time constant so the orchestrator can own its decode
// buffer without pulling the full C++ packet headers in.
constexpr size_t EgressPacketCSize = 112;

// Template parameter `Client` must expose two methods with the
// following signatures (duck-typed — no inheritance required):
//
//   bool fetch_pending(uint8_t* body, size_t cap, size_t& len,
//                      int& status_code);
//   bool ack_dispatched(const char* payment_id,
//                       const char* settlement_tx_hash,
//                       int& status_code);
//
// Production plugs in `EgressPollClient`. Tests plug in a
// MockHttpClient struct with matching methods.
template <typename Client>
class EgressOrchestrator {
public:
    EgressOrchestrator(Client&  client,
                       LoraTxFn tx_fn,
                       void*    tx_user)
        : client_(client), tx_fn_(tx_fn), tx_user_(tx_user) {}

    // Run ONE poll round:
    //   1. fetch_pending
    //   2. parse JSON array
    //   3. for each record: hex-decode PacketC → LoRa TX → ACK
    // Returns:
    //   >= 0  — records dispatched AND acked this tick (may be less
    //           than the number parsed if TX/ACK failed for some)
    //   < 0   — parse error / transport error on the GET (no records
    //           were touched)
    int tick() {
        uint8_t body[RESPONSE_BUF_SIZE];
        size_t  body_len    = 0;
        int     status_code = 0;
        if (!client_.fetch_pending(body, sizeof(body), body_len, status_code)) {
            // Transport failure — caller's poll loop retries next tick.
            return -1;
        }
        if (status_code != 200) {
            // Non-OK status (503 if gateway not configured, etc.) — no
            // records to dispatch, but also NOT a hard error that
            // should propagate up.
            return 0;
        }

        Record recs[EgressMaxPerTick];
        const int parsed = parse_pending_response(
            reinterpret_cast<const char*>(body),
            body_len,
            recs,
            EgressMaxPerTick);
        if (parsed < 0) {
            return -1; // malformed JSON; bail so a transient
                       // corruption doesn't stick
        }

        int dispatched_and_acked = 0;
        for (int i = 0; i < parsed; ++i) {
            const Record& r = recs[i];

            uint8_t frame[EgressPacketCSize];
            const char* hex    = r.packet_c_hex;
            size_t      hexlen = 0;
            while (hex[hexlen] != '\0' && hexlen < EgressPacketCHexMaxLen) {
                ++hexlen;
            }
            if (hexlen != EgressPacketCSize * 2u) {
                std::printf("[EGRESS] skip %s: hex len %zu != %zu\n",
                            r.payment_id, hexlen, EgressPacketCSize * 2u);
                continue;
            }
            if (!hex_decode(hex, hexlen, frame, sizeof(frame))) {
                std::printf("[EGRESS] skip %s: hex decode failed\n",
                            r.payment_id);
                continue;
            }

            if (tx_fn_ == nullptr ||
                !tx_fn_(frame, sizeof(frame), tx_user_)) {
                std::printf("[EGRESS] TX failed for %s (tx=%s); no ACK\n",
                            r.payment_id, r.settlement_tx_hash);
                continue;
            }

            int ack_code = 0;
            const bool ack_ok = client_.ack_dispatched(
                r.payment_id, r.settlement_tx_hash, ack_code);
            if (!ack_ok) {
                std::printf("[EGRESS] ACK transport failed for %s — retry next tick\n",
                            r.payment_id);
                continue;
            }
            if (ack_code != 200) {
                std::printf("[EGRESS] ACK %d for %s; dropping locally\n",
                            ack_code, r.payment_id);
                ++dispatched_and_acked; // gateway is authoritative; we're done
                continue;
            }
            ++dispatched_and_acked;
        }
        return dispatched_and_acked;
    }

private:
    // Response buffer size — 10 records × ~500 B each + headroom.
    static constexpr size_t RESPONSE_BUF_SIZE = 8192;

    Client&  client_;
    LoraTxFn tx_fn_;
    void*    tx_user_;
};

} // namespace egress

#endif // EGRESS_ORCHESTRATOR_H
