/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      egress_poll_client.cpp
 * Desc:      Raw-socket HTTP/1.1 client for the VOID-138 bouncer
 *            egress path. Talks to the gateway's VOID-135b endpoints.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "egress_poll_client.h"

#include <cstdio>
#include <cstring>

// Cross-platform socket headers — same split the existing
// gateway_client.cpp uses.
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

namespace egress {
namespace {

// Request header buffer. 512 B is ample for our two fixed request
// shapes: a GET with no body (~180 B) and a POST with a small JSON
// body (~320 B).
constexpr size_t REQ_BUF_SIZE  = 512;

// Temp buffer for a single recv() call on the response stream.
// 2 KiB covers headers + a modest body slice per read; we loop until
// the socket closes or the body_cap is reached.
constexpr size_t RECV_CHUNK    = 2048;

// HTTP response size guard — if the total bytes received from a
// single response exceed this, we bail to avoid a pathological
// gateway feeding the bouncer unbounded data. 64 KiB is 10x the
// largest legitimate /pending response (10 records × ~500 B each).
constexpr size_t RESP_CAP      = 64 * 1024;

// Connect a blocking TCP socket to host:port. Returns the fd or -1.
// Caller closes.
int connect_to(const char* host, uint16_t port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return -1;
#endif
    int fd = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));
    if (fd < 0) return -1;

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        return -1;
    }

    if (::connect(fd,
                  reinterpret_cast<sockaddr*>(&addr),
                  sizeof(addr)) < 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        return -1;
    }
    return fd;
}

void close_fd(int fd) {
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

// Bounded strtoi for Content-Length / status-code lines. Reads decimal
// digits up to `max_chars`, stops at first non-digit, stores into
// `out`. Returns bytes consumed; -1 if no digits read.
int parse_decimal(const char* s, size_t max_chars, size_t& out) {
    size_t v = 0;
    size_t i = 0;
    while (i < max_chars && s[i] >= '0' && s[i] <= '9') {
        v = v * 10u + static_cast<size_t>(s[i] - '0');
        ++i;
    }
    if (i == 0) return -1;
    out = v;
    return static_cast<int>(i);
}

// Parse "HTTP/1.1 <code> <reason>\r\n" — returns the numeric code.
// Returns -1 on malformed input.
int parse_status_line(const char* buf, size_t len) {
    // Accept HTTP/1.0 and HTTP/1.1.
    const char* prefix10 = "HTTP/1.0 ";
    const char* prefix11 = "HTTP/1.1 ";
    if (len < 12) return -1;
    if (std::memcmp(buf, prefix10, 9) != 0 &&
        std::memcmp(buf, prefix11, 9) != 0) {
        return -1;
    }
    size_t code = 0;
    int consumed = parse_decimal(&buf[9], len - 9, code);
    if (consumed <= 0) return -1;
    return static_cast<int>(code);
}

// Find the end of the HTTP header block (CRLFCRLF). Returns offset
// one past the terminator, or 0 if not found.
size_t find_header_end(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i + 3 < len; ++i) {
        if (buf[i]   == '\r' && buf[i+1] == '\n' &&
            buf[i+2] == '\r' && buf[i+3] == '\n') {
            return i + 4;
        }
    }
    return 0;
}

// Scan the headers for "Content-Length: <n>". Returns true + sets
// `len` on match; false if the header isn't present (we'll read
// until EOF in that case).
bool parse_content_length(const uint8_t* hdr, size_t hdr_len, size_t& len) {
    const char* needle = "Content-Length:";
    const size_t nlen  = std::strlen(needle);
    for (size_t i = 0; i + nlen < hdr_len; ++i) {
        bool match = true;
        for (size_t j = 0; j < nlen; ++j) {
            const char c = static_cast<char>(hdr[i + j]);
            const char n = needle[j];
            // Case-insensitive compare for the ASCII letters; header
            // names are case-insensitive per RFC 7230.
            char cl = c, nl = n;
            if (cl >= 'A' && cl <= 'Z') cl = static_cast<char>(cl - 'A' + 'a');
            if (nl >= 'A' && nl <= 'Z') nl = static_cast<char>(nl - 'A' + 'a');
            if (cl != nl) { match = false; break; }
        }
        if (!match) continue;
        size_t p = i + nlen;
        while (p < hdr_len && (hdr[p] == ' ' || hdr[p] == '\t')) ++p;
        size_t n = 0;
        int c = parse_decimal(reinterpret_cast<const char*>(&hdr[p]),
                              hdr_len - p, n);
        if (c <= 0) return false;
        len = n;
        return true;
    }
    return false;
}

// Send a whole buffer, looping past short writes.
bool send_all(int fd, const char* buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t w = ::send(fd, buf + off, len - off, 0);
        if (w <= 0) return false;
        off += static_cast<size_t>(w);
    }
    return true;
}

// Read the whole response into `resp` (capped at RESP_CAP). Returns
// the number of bytes read, or SIZE_MAX on error / overflow.
size_t read_full_response(int fd, uint8_t* resp, size_t resp_cap) {
    size_t total = 0;
    while (total < resp_cap) {
        uint8_t chunk[RECV_CHUNK];
        ssize_t r = ::recv(fd, reinterpret_cast<char*>(chunk),
                           sizeof(chunk), 0);
        if (r < 0) return static_cast<size_t>(-1); // transport error
        if (r == 0) break;                         // peer closed
        if (total + static_cast<size_t>(r) > resp_cap) {
            return static_cast<size_t>(-1);        // too big
        }
        std::memcpy(&resp[total], chunk, static_cast<size_t>(r));
        total += static_cast<size_t>(r);
    }
    return total;
}

// Shared HTTP round-trip engine. Sends `req` on a fresh socket, reads
// the response, parses status + body. Writes the body into `body` (up
// to `body_cap`) on success.
//
// If body size exceeds body_cap, returns false (no silent truncation).
bool do_round_trip(const char* host,
                   uint16_t    port,
                   const char* req,
                   size_t      req_len,
                   uint8_t*    body,
                   size_t      body_cap,
                   size_t&     body_len,
                   int&        status_code) {
    int fd = connect_to(host, port);
    if (fd < 0) return false;

    if (!send_all(fd, req, req_len)) {
        close_fd(fd);
        return false;
    }

    uint8_t resp[RESP_CAP];
    const size_t n = read_full_response(fd, resp, sizeof(resp));
    close_fd(fd);
    if (n == static_cast<size_t>(-1)) return false;
    if (n < 12) return false; // too short for "HTTP/1.1 xxx"

    // Parse status.
    status_code = parse_status_line(
        reinterpret_cast<const char*>(resp), n);
    if (status_code < 0) return false;

    // Find header terminator.
    const size_t body_off = find_header_end(resp, n);
    if (body_off == 0) return false;
    const size_t body_bytes = n - body_off;

    // Check Content-Length if present — if it disagrees with the
    // bytes actually received, bail.
    size_t advertised = 0;
    if (parse_content_length(resp, body_off, advertised)) {
        if (advertised != body_bytes) return false;
    }

    if (body_bytes > body_cap) return false; // no silent truncation
    if (body != nullptr && body_bytes > 0) {
        std::memcpy(body, &resp[body_off], body_bytes);
    }
    body_len = body_bytes;
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------

EgressPollClient::EgressPollClient(const char* host, uint16_t port)
    : host_(host), port_(port) {}

bool EgressPollClient::fetch_pending(uint8_t* body,
                                     size_t   body_cap,
                                     size_t&  body_len,
                                     int&     status_code) {
    if (body == nullptr || body_cap == 0) return false;

    char req[REQ_BUF_SIZE];
    const int n = std::snprintf(req, sizeof(req),
        "GET /api/v1/egress/pending HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n\r\n",
        host_, static_cast<unsigned>(port_));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(req)) return false;

    return do_round_trip(host_, port_,
                         req, static_cast<size_t>(n),
                         body, body_cap, body_len, status_code);
}

bool EgressPollClient::ack_dispatched(const char* payment_id,
                                      const char* settlement_tx_hash,
                                      int&        status_code) {
    if (payment_id == nullptr || settlement_tx_hash == nullptr) return false;

    // Format the JSON body into a bounded buffer.
    char json[256];
    const int jn = std::snprintf(json, sizeof(json),
        "{\"payment_id\":\"%s\",\"settlement_tx_hash\":\"%s\"}",
        payment_id, settlement_tx_hash);
    if (jn < 0 || static_cast<size_t>(jn) >= sizeof(json)) return false;

    char req[REQ_BUF_SIZE];
    const int n = std::snprintf(req, sizeof(req),
        "POST /api/v1/egress/ack HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        host_, static_cast<unsigned>(port_), jn, json);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(req)) return false;

    uint8_t sink[1024];
    size_t  sink_len = 0;
    return do_round_trip(host_, port_,
                         req, static_cast<size_t>(n),
                         sink, sizeof(sink),
                         sink_len, status_code);
}

} // namespace egress