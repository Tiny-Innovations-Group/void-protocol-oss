/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      http_poller.h
 * Desc:      VOID-142 — bounded raw-socket HTTP/1.1 GET/POST for the
 *            console's status + chain polls. Mirrors the bouncer's
 *            proven egress_poll_client transport (VOID-138).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef VOID_GROUND_STATION_UI_HTTP_POLLER_H
#define VOID_GROUND_STATION_UI_HTTP_POLLER_H

#include <cstddef>
#include <cstdint>

// One blocking HTTP round trip (fresh TCP, Connection: close, read to
// EOF into caller buffers). Transport failures return false; an HTTP
// round trip that completed returns true with status_code set — same
// contract as egress::EgressPollClient (VOID-138).
//
// Body never silently truncates: oversized responses return false.

struct http_result_t {
    bool   ok;
    int    status_code;
    size_t body_len;
};

http_result_t http_get(const char* host, std::uint16_t port, const char* path,
                       std::uint8_t* body, std::size_t body_cap);

http_result_t http_post_json(const char* host, std::uint16_t port, const char* path,
                             const char* json, std::uint8_t* body, std::size_t body_cap);

#endif // VOID_GROUND_STATION_UI_HTTP_POLLER_H
