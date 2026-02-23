/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      gateway_client.cpp
 * Desc:      Cross-platform zero-heap HTTP/TCP client.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "../include/gateway_client.h"
#include <cstdio>
#include <cstring>

// Cross-Platform Socket Headers
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib") // Tell MSVC to link Winsock
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

GatewayClient::GatewayClient(const char* host, int port) : _host(host), _port(port) {
    std::memset(_json_buffer, 0, sizeof(_json_buffer));
}

bool GatewayClient::format_json(const uint8_t* sanitized_payload, size_t len) {
    if (len < 62) return false; // Must be exactly the InnerInvoice size

    // Safely extract fields using memcpy to prevent misaligned read faults (SEI CERT)
    uint64_t epoch_ts; 
    uint32_t sat_id;   
    uint64_t amount;   
    uint16_t asset_id; 

    std::memcpy(&epoch_ts, sanitized_payload + 0, 8);
    std::memcpy(&sat_id, sanitized_payload + 44, 4);
    std::memcpy(&amount, sanitized_payload + 48, 8);
    std::memcpy(&asset_id, sanitized_payload + 56, 2);

    // Format safely into the static buffer using snprintf as per .cursorrules
    int written = std::snprintf(_json_buffer, sizeof(_json_buffer),
           "{\"epoch_ts\":%llu,\"sat_id\":%u,\"amount\":%llu,\"asset_id\":%u}",
           static_cast<unsigned long long>(epoch_ts),
           static_cast<unsigned int>(sat_id),
           static_cast<unsigned long long>(amount),
           static_cast<unsigned int>(asset_id));

    return (written > 0 && static_cast<size_t>(written) < sizeof(_json_buffer));
}

bool GatewayClient::push_to_l2(const uint8_t* sanitized_payload, size_t len) {
    if (!format_json(sanitized_payload, len)) {
        std::puts("[ERROR] Failed to format JSON payload.");
        return false;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;
#endif

    // 1. Create Socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    // 2. Setup Address
    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(static_cast<uint16_t>(_port));
    inet_pton(AF_INET, _host, &server_addr.sin_addr);

    // 3. Connect
    if (connect(sock, reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
        std::puts("[ERROR] Could not connect to Go Gateway.");
#ifdef _WIN32
        closesocket(sock); WSACleanup();
#else
        close(sock);
#endif
        return false;
    }

    // 4. Construct HTTP POST natively (No Heap)
    char http_request[512] = {0};
    std::snprintf(http_request, sizeof(http_request),
        "POST /api/v1/ingest HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n"
        "%s", _host, _port, std::strlen(_json_buffer), _json_buffer);

    // 5. Send Data
    send(sock, http_request, std::strlen(http_request), 0);

    // 6. Cleanup
#ifdef _WIN32
    closesocket(sock); WSACleanup();
#else
    close(sock);
#endif

    return true;
}