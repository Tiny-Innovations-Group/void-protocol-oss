/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      void_payment_payload.h
 * Desc:      62 B Inner Invoice Payload carried inside PacketB.enc_payload.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------
 * Canonical layout per Protocol-spec-SNLP.md §4.3 (byte-identical to
 * Protocol-spec-CCSDS.md §3.3, so a single buyer implementation works
 * across both tiers). The buyer builds this by echoing the corresponding
 * fields of the received PacketA verbatim — no reformatting, no magic
 * markers. The trailing `crc32` is the ORIGINAL invoice checksum; the
 * gateway matches it against its cached PacketA to reject substitution
 * attacks at settlement time.
 *
 * Plaintext on the wire under the default SNLP build (amateur-band
 * compliant, FCC Part 97 / ITU §25). Under the optional CCSDS encrypted
 * build the same 62 bytes become ChaCha20 ciphertext; the struct layout
 * is unchanged.
 * -------------------------------------------------------------------------
 * WARNING: Fields are Little-Endian on the wire.
 * -------------------------------------------------------------------------*/

#ifndef VOID_PAYMENT_PAYLOAD_H
#define VOID_PAYMENT_PAYLOAD_H

#include <cstdint>
#include <cstddef>

// Size constant — mirrors the PacketB_t::enc_payload[62] slot across both
// wire tiers. Defined alongside the struct for use in firmware, tests, and
// golden-vector generators without having to pull in void_packets_*.h.
#define SIZE_INVOICE_PAYLOAD 62u

#pragma pack(push, 1)

/**
 * @brief Inner Invoice Payload — verbatim echo of PacketA body fields.
 * @size  62 Bytes
 * @cite  Protocol-spec-SNLP.md §4.3 / Protocol-spec-CCSDS.md §3.3
 */
typedef struct __attribute__((packed)) {
    uint64_t epoch_ts;      // 00-07: Little-Endian original invoice timestamp
    double   pos_vec[3];    // 08-31: Little-Endian Sat A position (ECEF f64)
    float    vel_vec[3];    // 32-43: Little-Endian Sat A velocity (f32)
    uint32_t sat_id;        // 44-47: Little-Endian seller identifier
    uint64_t amount;        // 48-55: Little-Endian transaction value
    uint16_t asset_id;      // 56-57: Little-Endian currency id
    uint32_t crc32;         // 58-61: Little-Endian original PacketA CRC
} InvoicePayload_t;

#pragma pack(pop)

static_assert(sizeof(InvoicePayload_t) == SIZE_INVOICE_PAYLOAD,
              "InvoicePayload_t must be 62 B (Protocol-spec-SNLP.md §4.3)");
static_assert(offsetof(InvoicePayload_t, epoch_ts) == 0,
              "InvoicePayload_t::epoch_ts drifted from offset 0");
static_assert(offsetof(InvoicePayload_t, pos_vec)  == 8,
              "InvoicePayload_t::pos_vec drifted from offset 8");
static_assert(offsetof(InvoicePayload_t, vel_vec)  == 32,
              "InvoicePayload_t::vel_vec drifted from offset 32");
static_assert(offsetof(InvoicePayload_t, sat_id)   == 44,
              "InvoicePayload_t::sat_id drifted from offset 44");
static_assert(offsetof(InvoicePayload_t, amount)   == 48,
              "InvoicePayload_t::amount drifted from offset 48");
static_assert(offsetof(InvoicePayload_t, asset_id) == 56,
              "InvoicePayload_t::asset_id drifted from offset 56");
static_assert(offsetof(InvoicePayload_t, crc32)    == 58,
              "InvoicePayload_t::crc32 drifted from offset 58");

#endif // VOID_PAYMENT_PAYLOAD_H