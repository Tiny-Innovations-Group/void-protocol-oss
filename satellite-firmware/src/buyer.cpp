/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      buyer.cpp
 * Desc:      Buyer-side Mule State Machine — VOID-128 PacketB TX pipeline.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/
#include "void_protocol.h"
#include "void_config.h"
#include "void_payment_payload.h"
#include "buyer.h"
#include "security_manager.h"
#include "gps_stub.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// --- Static state (no heap) ---
static bool       invoice_pending = false;
static PacketA_t  pending_invoice;

// --- Duty-cycle observation (VOID-128) ---
// DUTY_CYCLE_TARGET_MS (36 s) comes from void_config.h. At this stage we only
// LOG the observed inter-TX gap; hard enforcement is deferred to VOID-070.
// A gap below target is flagged "UNDER" in the serial log, not dropped.
static unsigned long last_tx_ms = 0;  // 0 sentinel = no TX yet this session

#if VOID_PROTOCOL_TYPE == 2
static constexpr uint32_t SNLP_SYNC_WORD = 0x1D01A5A5u;
#endif

// Extract the 11-bit CCSDS APID from a raw received buffer.
// Caller MUST have already bounded the length to >= SIZE_VOID_HEADER.
static uint16_t extractAPID(const uint8_t* buf) {
#if VOID_PROTOCOL_TYPE == 2
    // SNLP prepends a 4-byte sync word before the CCSDS ID field.
    return static_cast<uint16_t>(((buf[4] & 0x07u) << 8) | buf[5]);
#else
    return static_cast<uint16_t>(((buf[0] & 0x07u) << 8) | buf[1]);
#endif
}

#if VOID_PROTOCOL_TYPE == 2
// First-line filter against noise and foreign LoRa traffic on crowded
// amateur bands (Protocol-spec-SNLP.md §1.1).
static bool validSyncWord(const uint8_t* buf) {
    const uint32_t sync =
          (static_cast<uint32_t>(buf[0]) << 24)
        | (static_cast<uint32_t>(buf[1]) << 16)
        | (static_cast<uint32_t>(buf[2]) <<  8)
        |  static_cast<uint32_t>(buf[3]);
    return sync == SNLP_SYNC_WORD;
}
#endif

// Pack a VOID header (BE) for a non-command telemetry packet into the
// supplied byte buffer. `body_len` is the full frame size minus
// SIZE_VOID_HEADER. The CCSDS packet_len field is encoded as
// (body_len - 1) to match gateway/test/utils/generate_packets.go::
// buildHeader, which is the authority for the golden vector bytes.
static void packVoidHeader(uint8_t* hdr, uint16_t apid, uint16_t body_len) {
#if VOID_PROTOCOL_TYPE == 2
    // Sync word 0x1D01A5A5 (BE32)
    hdr[0] = 0x1Du; hdr[1] = 0x01u; hdr[2] = 0xA5u; hdr[3] = 0xA5u;
    uint8_t* const ccsds = hdr + 4;
#else
    uint8_t* const ccsds = hdr;
#endif

    // CCSDS ID field: version=0 | type=0 (telemetry) | sec=1 | apid(11)
    const uint16_t id = static_cast<uint16_t>(0x0800u | (apid & 0x07FFu));
    ccsds[0] = static_cast<uint8_t>((id >> 8) & 0xFFu);
    ccsds[1] = static_cast<uint8_t>(id & 0xFFu);

    // Sequence: flags=11 (unsegmented continuous), count=0
    ccsds[2] = 0xC0u;
    ccsds[3] = 0x00u;

    // packet_len = body_len - 1 (BE16)
    const uint16_t plen = static_cast<uint16_t>(body_len - 1u);
    ccsds[4] = static_cast<uint8_t>((plen >> 8) & 0xFFu);
    ccsds[5] = static_cast<uint8_t>(plen & 0xFFu);

#if VOID_PROTOCOL_TYPE == 2
    // SNLP 4-byte align_pad (zeroed)
    hdr[10] = 0x00u;
    hdr[11] = 0x00u;
    hdr[12] = 0x00u;
    hdr[13] = 0x00u;
#endif
}

// Little-endian u32 load from a byte buffer. Used for wire-format CRC
// comparisons where the field is LE regardless of host endianness.
static uint32_t loadLE32(const uint8_t* p) {
    return  static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) <<  8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

// ---------------------------------------------------------------------------
// runBuyerLoop — RX PacketA, on "ACK_BUY" from ground build & TX PacketB.
// ---------------------------------------------------------------------------
void runBuyerLoop() {
    // =====================================================================
    // 1. LoRa space-link receive
    // =====================================================================
    static uint8_t rx_buffer[VOID_MAX_PACKET_SIZE];
    const int state = Void.radio.readData(reinterpret_cast<uint8_t*>(NULL), 0);

    if (state == RADIOLIB_ERR_NONE) {
        const size_t len = Void.radio.getPacketLength();

        if (len <= VOID_MAX_PACKET_SIZE && len >= SIZE_VOID_HEADER) {
            Void.radio.readData(rx_buffer, len);

#if VOID_PROTOCOL_TYPE == 2
            if (!validSyncWord(rx_buffer)) {
                Void.radio.startReceive();
                return;
            }
#endif
            const uint16_t apid = extractAPID(rx_buffer);

            // --- RX PacketA: Invoice from Sat A (SELLER_APID) ---
            if (apid == SELLER_APID && len == SIZE_PACKET_A) {
                // VOID-128: validate the invoice CRC32 BEFORE struct cast.
                // Protects downstream state from a flipped-bit header that
                // happened to pass the sync-word + length + APID checks.
                const size_t   crc_end  = offsetof(PacketA_t, crc32);
                const uint32_t calc_crc = Void.calculateCRC(rx_buffer, crc_end);
                const uint32_t wire_crc = loadLE32(rx_buffer + crc_end);
                if (calc_crc != wire_crc) {
                    Void.updateDisplay("BUYER", "PacketA CRC fail — drop");
                    Serial.println("WARN:PacketA CRC mismatch, dropped");
                    Void.radio.startReceive();
                    return;
                }

                Void.updateDisplay("BUYER", "RX Invoice! Notifying Ground...");
                memcpy(&pending_invoice, rx_buffer, SIZE_PACKET_A);
                invoice_pending = true;

                Serial.print("INVOICE:");
                Void.hexDump(rx_buffer, len);
            }
            // --- RX PacketC: Receipt from Sat A (downlink passthrough) ---
            else if (apid == SELLER_APID && len == SIZE_PACKET_C) {
                Void.updateDisplay("BUYER", "RX Receipt! Wrapping Packet D...");
                Serial.print("PACKET_D:");
                Void.hexDump(rx_buffer, len);
            }
        }
    }

    // =====================================================================
    // 2. Serial ground-link commands
    // =====================================================================
    if (Serial.available() > 0) {
        static char serial_buf[256];
        const size_t bytesRead =
            Serial.readBytesUntil('\n', serial_buf, sizeof(serial_buf) - 1);
        serial_buf[bytesRead] = '\0';

        // -----------------------------------------------------------------
        // Ground authorised the buy -> build & TX PacketB
        // -----------------------------------------------------------------
        if (strncmp(serial_buf, "ACK_BUY", 7) == 0 && invoice_pending) {
            // --- Duty-cycle observation (non-blocking) ---
            if (last_tx_ms != 0) {
                const unsigned long now = millis();
                const unsigned long gap = now - last_tx_ms;
                char gap_line[80];
                snprintf(gap_line, sizeof(gap_line),
                         "DUTY_GAP_MS:%lu target=%u%s",
                         gap,
                         static_cast<unsigned>(DUTY_CYCLE_TARGET_MS),
                         (gap < DUTY_CYCLE_TARGET_MS) ? " UNDER" : " OK");
                Serial.println(gap_line);
            }

            Void.updateDisplay("BUYER", "Building Packet B...");

            // --- Build PacketB_t in a static buffer (no heap) ---
            static PacketB_t packet_b;
            memset(&packet_b, 0, sizeof(PacketB_t));

            // 2a. Wire header (BE) — byte-packed to match Go golden vectors.
            uint8_t hdr_bytes[SIZE_VOID_HEADER];
            packVoidHeader(
                hdr_bytes,
                BUYER_APID,
                static_cast<uint16_t>(SIZE_PACKET_B - SIZE_VOID_HEADER));
            memcpy(&packet_b.header, hdr_bytes, SIZE_VOID_HEADER);

            // 2b. Outer mule fields (LE).
            GpsStub.update();
            packet_b.epoch_ts = GpsStub.getEpochMs();
            double pos_tmp[3];
            GpsStub.getPositionVec(pos_tmp);
            memcpy(packet_b.pos_vec, pos_tmp, sizeof(pos_tmp));

            // 2c. Inner Invoice Payload — verbatim echo of the received
            // PacketA body fields (Protocol-spec-SNLP.md §4.3).
            InvoicePayload_t inner;
            memset(&inner, 0, sizeof(InvoicePayload_t));
            inner.epoch_ts = pending_invoice.epoch_ts;
            memcpy(inner.pos_vec, pending_invoice.pos_vec, sizeof(inner.pos_vec));
            memcpy(inner.vel_vec, pending_invoice.vel_vec, sizeof(inner.vel_vec));
            inner.sat_id   = pending_invoice.sat_id;
            inner.amount   = pending_invoice.amount;
            inner.asset_id = pending_invoice.asset_id;
            inner.crc32    = pending_invoice.crc32;

            // 2d. Sat B identity — low 4 bytes of the derived ChaCha20
            // nonce under the optional encrypted SNLP build (VOID-110).
            packet_b.sat_id = BUYER_SAT_ID;

            // 2e. Copy inner payload into enc_payload AND Ed25519-sign.
            // Under VOID_ALPHA_PLAINTEXT this stores cleartext and skips
            // session / GPS / monotonic-epoch guards (VOID-127).
            if (!Security.encryptPacketB(
                    packet_b,
                    reinterpret_cast<const uint8_t*>(&inner),
                    sizeof(InvoicePayload_t))) {
                Void.updateDisplay("ERROR", "encryptPacketB failed");
                Serial.println("ERROR:encryptPacketB returned false");
                invoice_pending = false;
                return;
            }

            // 2f. global_crc — covers header + body up to (but excluding)
            // the global_crc field itself. _tail_pad is ignored.
            const size_t crc_end = offsetof(PacketB_t, global_crc);
            packet_b.global_crc  = Void.calculateCRC(
                reinterpret_cast<const uint8_t*>(&packet_b), crc_end);

            // 2g. Transmit and return to receive.
            Void.radio.transmit(
                reinterpret_cast<uint8_t*>(&packet_b), SIZE_PACKET_B);
            Void.radio.startReceive();

            Serial.print("PACKET_B:");
            Void.hexDump(reinterpret_cast<const uint8_t*>(&packet_b), SIZE_PACKET_B);

            last_tx_ms      = millis();
            invoice_pending = false;
        }
        // -----------------------------------------------------------------
        // Ground relays an L2 ACK / tunnel payload — broadcast to Sat A
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "ACK_DOWNLINK:", 13) == 0) {
            Void.updateDisplay("BUYER", "Relaying Tunnel Data...");

            static uint8_t tunnel_data[SIZE_TUNNEL_DATA];
            memset(tunnel_data, 0xAA, SIZE_TUNNEL_DATA);

            Void.radio.transmit(tunnel_data, SIZE_TUNNEL_DATA);
            Void.radio.startReceive();
        }
        // -----------------------------------------------------------------
        // Ground returns its ephemeral key — derive session key
        // -----------------------------------------------------------------
        else if (strncmp(serial_buf, "HANDSHAKE_ACK:", 14) == 0) {
            Void.updateDisplay("AUTH", "Deriving Session Key...");

            PacketH_t mock_resp;
            memset(&mock_resp, 0, sizeof(PacketH_t));
            for (int i = 0; i < 32; ++i) {
                const char byteStr[3] = {
                    serial_buf[14 + (i * 2)],
                    serial_buf[15 + (i * 2)],
                    '\0'
                };
                mock_resp.eph_pub_key[i] =
                    static_cast<uint8_t>(strtol(byteStr, NULL, 16));
            }

            if (Security.processHandshakeResponse(mock_resp)) {
                Void.updateDisplay("AUTH", "Session ACTIVE");
                Serial.println("SAT_READY: Session Key Derived locally.");
            } else {
                Void.updateDisplay("ERROR", "ECDH Math Failed");
            }
        }
    }
}
