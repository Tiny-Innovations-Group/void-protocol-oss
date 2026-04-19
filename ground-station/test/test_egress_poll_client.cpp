/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      test_egress_poll_client.cpp
 * Desc:      VOID-138 red-green for the HTTP poll client. Uses a
 *            tiny single-threaded test HTTP server that responds with
 *            caller-supplied canned bytes — real sockets, no mocks,
 *            same wire path the bouncer uses in production.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include <gtest/gtest.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include "egress_poll_client.h"

// ---------------------------------------------------------------------------
// Tiny single-request HTTP test server.
// ---------------------------------------------------------------------------
//
// The bouncer makes one HTTP round-trip per API call (connect → send
// request → read response → close). Mirror that shape here: each test
// spins a server thread that accepts ONE connection, reads the
// request (into a capped buffer so we can assert on it), emits a
// canned response, and exits. The fixture owns the lifecycle.
//
// std::string is used ONLY in test scaffolding — production code
// remains no-heap / bounded-buffer. Tests are allowed the ergonomic
// shortcut because CLAUDE.md's CERT rules target production firmware,
// not harness code.
class OneShotHttpServer {
public:
    OneShotHttpServer() = default;
    ~OneShotHttpServer() { stop(); }

    // Bind to loopback on a kernel-assigned port; returns the port.
    // Must be called before start().
    uint16_t bind_loopback() {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        int reuse = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_port        = 0; // kernel picks
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 1);
        socklen_t alen = sizeof(addr);
        ::getsockname(listen_fd_,
                      reinterpret_cast<sockaddr*>(&addr), &alen);
        port_ = ntohs(addr.sin_port);
        return port_;
    }

    // Start the accept thread. `response` is sent verbatim on the first
    // incoming connection. The request bytes are captured in
    // `captured_request_` (up to 4 KiB) so tests can assert on them.
    void start(std::string response) {
        response_ = std::move(response);
        worker_ = std::thread([this]() { serve_once(); });
    }

    // Wait for the worker to finish serving its one request.
    void join() { if (worker_.joinable()) worker_.join(); }

    void stop() {
        if (listen_fd_ >= 0) {
#ifdef _WIN32
            closesocket(listen_fd_);
#else
            ::close(listen_fd_);
#endif
            listen_fd_ = -1;
        }
        if (worker_.joinable()) worker_.join();
    }

    const std::string& captured_request() const { return captured_request_; }
    uint16_t           port()              const { return port_; }

private:
    void serve_once() {
        sockaddr_in client{};
        socklen_t alen = sizeof(client);
        int cfd = ::accept(listen_fd_,
                           reinterpret_cast<sockaddr*>(&client), &alen);
        if (cfd < 0) return;
        // Read request into a capped buffer so we can grep it.
        char buf[4096];
        ssize_t n = ::recv(cfd, buf, sizeof(buf) - 1, 0);
        if (n > 0) {
            buf[n] = '\0';
            captured_request_.assign(buf, static_cast<size_t>(n));
        }
        ::send(cfd, response_.data(), response_.size(), 0);
#ifdef _WIN32
        ::closesocket(cfd);
#else
        ::close(cfd);
#endif
    }

    int         listen_fd_ = -1;
    uint16_t    port_      = 0;
    std::thread worker_;
    std::string response_;
    std::string captured_request_;
};

// ---- small HTTP builders (for test fixtures) ------------------------------

static std::string http_response(const char* status, const std::string& body) {
    char head[128];
    std::snprintf(head, sizeof(head),
        "HTTP/1.1 %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n\r\n",
        status, body.size());
    return std::string(head) + body;
}

// ---------------------------------------------------------------------------
// fetch_pending tests
// ---------------------------------------------------------------------------

TEST(EgressPollClient, FetchPending200EmptyArray) {
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.start(http_response("200 OK", "[]"));

    egress::EgressPollClient client("127.0.0.1", port);
    uint8_t body[4096] = {0};
    size_t  body_len   = 0;
    int     status     = 0;
    ASSERT_TRUE(client.fetch_pending(body, sizeof(body), body_len, status));
    EXPECT_EQ(status, 200);
    ASSERT_EQ(body_len, 2u);
    EXPECT_EQ(std::memcmp(body, "[]", 2), 0);
    srv.join();
}

TEST(EgressPollClient, FetchPendingCapturesRealRecord) {
    const std::string canned =
        "[{\"payment_id\":\"123\",\"settlement_tx_hash\":\"0xabc\","
        "\"packet_c_hex\":\"cafe\"}]";
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.start(http_response("200 OK", canned));

    egress::EgressPollClient client("127.0.0.1", port);
    uint8_t body[4096] = {0};
    size_t  body_len   = 0;
    int     status     = 0;
    ASSERT_TRUE(client.fetch_pending(body, sizeof(body), body_len, status));
    EXPECT_EQ(status, 200);
    ASSERT_EQ(body_len, canned.size());
    EXPECT_EQ(std::memcmp(body, canned.data(), canned.size()), 0);

    // Assert the request was a GET on /api/v1/egress/pending (bouncer
    // must NOT send any weird path or method).
    EXPECT_NE(srv.captured_request().find("GET /api/v1/egress/pending"),
              std::string::npos);
    srv.join();
}

TEST(EgressPollClient, FetchPending503ReturnsStatusFalseOK) {
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.start(http_response("503 Service Unavailable",
                            "{\"error\":\"egress pipeline not configured\"}"));

    egress::EgressPollClient client("127.0.0.1", port);
    uint8_t body[4096] = {0};
    size_t  body_len   = 0;
    int     status     = 0;
    // Non-2xx is NOT a transport failure — client returns true with
    // the status so the caller can decide whether to retry.
    ASSERT_TRUE(client.fetch_pending(body, sizeof(body), body_len, status));
    EXPECT_EQ(status, 503);
    srv.join();
}

TEST(EgressPollClient, FetchPendingConnectRefusedFails) {
    // Bind a socket, then close it immediately so the port is vacant.
    // The client MUST report transport failure (false), not hang.
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.stop(); // close listen socket — nothing will accept on `port`

    egress::EgressPollClient client("127.0.0.1", port);
    uint8_t body[4096] = {0};
    size_t  body_len   = 0;
    int     status     = 0;
    EXPECT_FALSE(client.fetch_pending(body, sizeof(body), body_len, status));
}

TEST(EgressPollClient, FetchPendingBodyTooLargeRejected) {
    // Server promises a 2000-byte body; client only has 256 bytes of
    // buffer. Client MUST refuse — do NOT overflow, do NOT truncate
    // silently (either would corrupt the dispatch pipeline).
    std::string big(2000, 'x');
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.start(http_response("200 OK", big));

    egress::EgressPollClient client("127.0.0.1", port);
    uint8_t body[256] = {0};
    size_t  body_len  = 0;
    int     status    = 0;
    EXPECT_FALSE(client.fetch_pending(body, sizeof(body), body_len, status));
    srv.join();
}

// ---------------------------------------------------------------------------
// ack_dispatched tests
// ---------------------------------------------------------------------------

TEST(EgressPollClient, AckDispatched200SendsExpectedBody) {
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.start(http_response("200 OK", "{\"status\":\"dispatched\"}"));

    egress::EgressPollClient client("127.0.0.1", port);
    int status = 0;
    ASSERT_TRUE(client.ack_dispatched("42", "0xbeef", status));
    EXPECT_EQ(status, 200);

    const std::string& req = srv.captured_request();
    EXPECT_NE(req.find("POST /api/v1/egress/ack"), std::string::npos);
    EXPECT_NE(req.find("\"payment_id\":\"42\""), std::string::npos);
    EXPECT_NE(req.find("\"settlement_tx_hash\":\"0xbeef\""), std::string::npos);
    srv.join();
}

TEST(EgressPollClient, AckDispatched404PropagatesStatus) {
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.start(http_response("404 Not Found",
                            "{\"error\":\"unknown payment_id\"}"));

    egress::EgressPollClient client("127.0.0.1", port);
    int status = 0;
    ASSERT_TRUE(client.ack_dispatched("bogus", "0xzero", status));
    EXPECT_EQ(status, 404);
    srv.join();
}

TEST(EgressPollClient, AckDispatchedConnectRefusedFails) {
    OneShotHttpServer srv;
    uint16_t port = srv.bind_loopback();
    srv.stop();

    egress::EgressPollClient client("127.0.0.1", port);
    int status = 0;
    EXPECT_FALSE(client.ack_dispatched("1", "0xa", status));
}
