/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      http_poller.cpp
 * Desc:      VOID-142 — bounded raw-socket HTTP/1.1 round trips. Socket
 *            discipline mirrors ground-station/src/egress_poll_client.cpp
 *            (VOID-138): connect timeout, SO_RCVTIMEO, read-to-EOF cap.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "http_poller.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
// The console workflow is POSIX-first for flat-sat (VOID-142 stage 1).
// Win32 support lands if the demo bench ever moves off mac/Linux.
http_result_t http_get(const char*, std::uint16_t, const char*,
                       std::uint8_t*, std::size_t) {
    return {false, 0, 0};
}
http_result_t http_post_json(const char*, std::uint16_t, const char*,
                             const char*, std::uint8_t*, std::size_t) {
    return {false, 0, 0};
}
#else

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace {

// Response wire cap: the status JSON is <1 KiB; receipts/a chain reply
// similar. 64 KiB mirrors the bouncer's RESP_CAP discipline.
constexpr std::size_t RESP_CAP  = 65536;
constexpr std::size_t REQ_CAP   = 1024;
constexpr std::size_t RECV_GRAIN = 4096;
constexpr int         IO_TIMEOUT_SEC = 2;

void close_fd(int fd) { if (fd >= 0) ::close(fd); }

int connect_to(const char* host, std::uint16_t port) {
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u",
                  static_cast<unsigned>(port));
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* list = nullptr;
    if (getaddrinfo(host, port_str, &hints, &list) != 0) {
        return -1;
    }
    int fd = -1;
    for (struct addrinfo* rp = list; rp != nullptr; rp = rp->ai_next) {
        fd = static_cast<int>(socket(rp->ai_family, rp->ai_socktype,
                                     rp->ai_protocol));
        if (fd < 0) continue;
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close_fd(fd);
        fd = -1;
    }
    freeaddrinfo(list);
    if (fd < 0) return -1;
    // Send/recv timeouts keep a hung peer from stalling the UI frame.
    struct timeval tv;
    tv.tv_sec  = IO_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

bool send_all(int fd, const char* buf, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
        const ssize_t w = ::send(fd, buf + off, len - off, 0);
        if (w <= 0) return false;
        off += static_cast<std::size_t>(w);
    }
    return true;
}

// -1 on error/overflow, else bytes read.
size_t read_full(int fd, std::uint8_t* resp, std::size_t cap) {
    std::size_t total = 0;
    for (;;) {
        std::uint8_t chunk[RECV_GRAIN];
        const ssize_t r = ::recv(fd, reinterpret_cast<char*>(chunk),
                                 sizeof(chunk), 0);
        if (r < 0) return static_cast<std::size_t>(-1);
        if (r == 0) break;
        if (total + static_cast<std::size_t>(r) > cap) {
            return static_cast<std::size_t>(-1);
        }
        std::memcpy(resp + total, chunk, static_cast<std::size_t>(r));
        total += static_cast<std::size_t>(r);
    }
    return total;
}

int parse_status(const std::uint8_t* resp, std::size_t n) {
    // "HTTP/1.1 NNN ..."
    if (n < 16) return -1;
    int code = 0;
    if (std::sscanf(reinterpret_cast<const char*>(resp),
                    "HTTP/%*3s %d", &code) != 1) {
        return -1;
    }
    return code;
}

// Header/body separator offset, 0 if absent.
std::size_t header_end(const std::uint8_t* resp, std::size_t n) {
    if (n < 4) return 0;
    for (std::size_t i = 0; i + 4 <= n; ++i) {
        if (resp[i] == '\r' && resp[i + 1] == '\n' &&
            resp[i + 2] == '\r' && resp[i + 3] == '\n') {
            return i + 4;
        }
    }
    return 0;
}

http_result_t round_trip(const char* host, std::uint16_t port,
                         const char* req, std::size_t req_len,
                         std::uint8_t* body, std::size_t body_cap) {
    http_result_t out = {false, 0, 0};
    const int fd = connect_to(host, port);
    if (fd < 0) return out;
    if (!send_all(fd, req, req_len)) {
        close_fd(fd);
        return out;
    }
    std::uint8_t resp[RESP_CAP];
    const std::size_t n = read_full(fd, resp, sizeof(resp));
    close_fd(fd);
    if (n == static_cast<std::size_t>(-1) || n < 12) return out;

    const int code = parse_status(resp, n);
    if (code < 0) return out;
    const std::size_t body_off = header_end(resp, n);
    if (body_off == 0) return out;
    const std::size_t body_bytes = n - body_off;
    if (body_bytes > body_cap) return out; // no silent truncation
    if (body != nullptr && body_bytes > 0) {
        std::memcpy(body, resp + body_off, body_bytes);
    }
    out.ok = true;
    out.status_code = code;
    out.body_len = body_bytes;
    return out;
}

} // namespace

http_result_t http_get(const char* host, const std::uint16_t port,
                       const char* path, std::uint8_t* body,
                       const std::size_t body_cap) {
    char req[REQ_CAP];
    const int n = std::snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n\r\n",
        path, host, static_cast<unsigned>(port));
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(req)) {
        return {false, 0, 0};
    }
    return round_trip(host, port, req, static_cast<std::size_t>(n),
                      body, body_cap);
}

http_result_t http_post_json(const char* host, const std::uint16_t port,
                             const char* path, const char* json,
                             std::uint8_t* body, const std::size_t body_cap) {
    const std::size_t json_len = std::strlen(json);
    char req[REQ_CAP];
    const int n = std::snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n\r\n",
        path, host, static_cast<unsigned>(port),
        static_cast<unsigned>(json_len));
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(req) ||
        static_cast<std::size_t>(n) + json_len >= sizeof(req)) {
        return {false, 0, 0};
    }
    // Append the JSON body inside the same bounded request buffer.
    std::memcpy(req + static_cast<std::size_t>(n), json, json_len);
    return round_trip(host, port, req, static_cast<std::size_t>(n) + json_len,
                      body, body_cap);
}

#endif // _WIN32
