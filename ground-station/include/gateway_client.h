/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * File:      gateway_client.h
 * Desc:      Zero-heap HTTP/TCP client to bridge C++ edge to Go Enterprise.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef GATEWAY_CLIENT_H
#define GATEWAY_CLIENT_H

#include <cstdint>
#include <cstddef>
#include "../../void-core/include/void_packets.h"

class GatewayClient {
private:
    const char* _host;
    int _port;
    char _json_buffer[256]; // Static buffer for JSON payload

    // Formats the raw 62-byte InnerInvoice into a JSON string safely
    bool format_json(const uint8_t* sanitized_payload, size_t len);

public:
    GatewayClient(const char* host, int port);

    // Sends the sanitized payload to the Go Gin server
    bool push_to_l2(const uint8_t* sanitized_payload, size_t len);
};

#endif // GATEWAY_CLIENT_H