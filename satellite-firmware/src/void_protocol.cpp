/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      void_protocol.cpp
 * Desc:      Main VOID Protocol Satellite Firmware Implementation.
 * Compliant: No-Heap Strings, Static Buffers.
 * -------------------------------------------------------------------------*/
#include "void_protocol.h"
#include "void_config.h"
#include "security_manager.h"

VoidProtocol Void;

#if VOID_PROTOCOL_TYPE == 2
// VOID-143: SNLP sync word (BE32, Protocol-spec-SNLP.md §1.1) — the first
// four bytes of every SNLP frame. Consumed by validSyncWord().
static constexpr uint32_t SNLP_SYNC_WORD = 0x1D01A5A5u;
#endif

void VoidProtocol::begin()
{
    // 1. Init Serial
    Serial.begin(115200);
    // VOID-143: cap the Stream timeout at 20 ms. The Arduino default of
    // 1000 ms made Serial.readBytesUntil() block and blind the LoRa radio
    // whenever a partial line sat in the UART buffer.
    Serial.setTimeout(20);
    while (!Serial);

    // 2. Init Display
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
    pinMode(VEXT_CTRL, OUTPUT);                                                     
    digitalWrite(VEXT_CTRL, LOW);   
    delay(20);
    digitalWrite(OLED_RST, HIGH);
    display.init();
    display.flipScreenVertically();
    display.setFont(ArialMT_Plain_10);
    updateDisplay("BOOT", "Initializing...");

    // 3. Init Crypto (Sodium)
    if (sodium_init() < 0)
    {
        updateDisplay("ERROR", "Sodium Init Fail");
        while (1)
            ;
    }

    // 4. Init LoRa (SX1262)
    // int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_SYNC, 10);

    // // direct from RadioLib examples, but with explicit casts to satisfy -Wconversion warnings under C++14 strict typing rules
    // int state = radio.begin(
    //     static_cast<float>(LORA_FREQ), 
    //     static_cast<float>(LORA_BW), 
    //     static_cast<uint8_t>(LORA_SF), 
    //     static_cast<uint8_t>(LORA_CR), 
    //     static_cast<uint8_t>(LORA_SYNC), 
    //     static_cast<int8_t>(10)
    // );

    // 4. Init LoRa (SX1262) - Explicitly passing ALL parameters to avoid RadioLib's double-precision defaults
    int state = radio.begin(
        static_cast<float>(LORA_FREQ), 
        static_cast<float>(LORA_BW), 
        static_cast<uint8_t>(LORA_SF), 
        static_cast<uint8_t>(LORA_CR), 
        static_cast<uint8_t>(LORA_SYNC), 
        static_cast<int8_t>(10),       // Power
        static_cast<uint16_t>(8),      // Preamble Length (RadioLib Default)
        1.6f,                          // TCXO Voltage (Explicit float, fixes the 1.6 double error)
        false                          // Use Regulator LDO (RadioLib Default)
    );
    // int state = radio.begin(static_cast<float>(LORA_FREQ), static_cast<float>(LORA_BW), LORA_SF, LORA_CR, LORA_SYNC, 10);
    if (state != RADIOLIB_ERR_NONE)
    {

        char errBuf[32];
        snprintf(errBuf, sizeof(errBuf), "LoRa Fail: %d", state);
        updateDisplay("ERROR", errBuf);

        while (1)
            ;
    }

    // VOID-143: 12 dBm for flat-sat link margin (was 5). SX1262 hardware
    // ceiling is +22 dBm; UK legal limit is 14 dBm ERP.
    radio.setOutputPower(12);

    updateDisplay("READY", "Void v2.1");
}

void VoidProtocol::updateDisplay(const char *status, const char *subtext)
{
    // TODO: optimise font i.e. header, desc, footer and wire(8000) for writing faster
    char line1[32];
    char line2[64];
    display.clear();
    snprintf(line1, sizeof(line1), "Status: %s", status);
    // line2 is just subtext, but ensure no overflow
    strncpy(line2, subtext, sizeof(line2) - 1);
    line2[sizeof(line2) - 1] = '\0';
    display.drawString(0, 0, "VOID PROTOCOL v2.1");
    display.drawString(0, 16, line1);
    display.drawString(0, 32, line2);
    display.display();
}

void VoidProtocol::hexDump(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        if (data[i] < 0x10)
            Serial.print("0");
        Serial.print(data[i], HEX);
    }
    Serial.println();
}

// IEEE 802.3 CRC32 (reflected, polynomial 0xEDB88320, init/final 0xFFFFFFFF).
// Byte-identical to Go's hash/crc32.ChecksumIEEE — required so firmware
// PacketB.global_crc matches the gateway-side parser and the checked-in
// golden vectors under test/vectors/.
uint32_t VoidProtocol::calculateCRC(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = (crc & 1u) ? 0xEDB88320u : 0u;
            crc = (crc >> 1) ^ mask;
        }
    }
    return ~crc;
}

#if VOID_PROTOCOL_TYPE == 2
// VOID-143: SNLP sync-word filter — see void_protocol.h. SNLP-only: the
// CCSDS tier has no sync word (its 6-byte primary header starts with the
// version/ID field).
bool VoidProtocol::validSyncWord(const uint8_t* buf) {
    const uint32_t sync =
          (static_cast<uint32_t>(buf[0]) << 24)
        | (static_cast<uint32_t>(buf[1]) << 16)
        | (static_cast<uint32_t>(buf[2]) <<  8)
        |  static_cast<uint32_t>(buf[3]);
    return sync == SNLP_SYNC_WORD;
}
#endif

// VOID-139: one hardware CAD pass. RadioLib's scanChannel() drives the
// SX1262's built-in Channel Activity Detection and returns
// RADIOLIB_LORA_DETECTED when a LoRa preamble is on the air. Any other
// result (RADIOLIB_CHANNEL_FREE or a negative error code) is treated as
// "clear" — failing open so a CAD fault can never permanently gag a
// mandatory transmit. Leaves the radio in standby (callers re-arm RX).
// If detection proves unreliable, tune RadioLib's CAD params via
// radio.setCad(...) — defaults are adequate for the flat-sat bench.
bool VoidProtocol::channelClear()
{
    const int16_t state = radio.scanChannel();
    return state != RADIOLIB_LORA_DETECTED;
}

// VOID-139: distinguish a genuine packet reception from the spurious DIO1
// interrupt that the SX1262 raises on TxDone / CadDone. getIrqFlags() reads
// the hardware IRQ status register (non-destructive — readData() clears it
// later). RxDone and TxDone are distinct bits, so testing the RxDone bit is
// robust even if a stale TxDone bit is still set: only a real reception sets
// RxDone. Without this gate, every transmit() leaves rx_flag=true and the
// next loop parses stale FIFO bytes as a bogus PacketA → "PacketA CRC fail".
bool VoidProtocol::isRealReception()
{
    const uint32_t irq = radio.getIrqFlags();
    return (irq & static_cast<uint32_t>(RADIOLIB_SX126X_IRQ_RX_DONE)) != 0u;
}

// VOID-139: CAD-gated mandatory transmit. Polls the channel up to
// maxAttempts times with a short escalating backoff and transmits the
// instant the air is clear. If it never clears, transmits anyway
// (best-effort) — a relayed PacketC/PacketAck/PacketD MUST go out or the
// commerce loop stalls; politeness yields to delivery. Returns true if
// the frame went out on a verified-clear channel, false on the forced
// fallback. Re-arms RX after the send.
bool VoidProtocol::transmitWhenClear(uint8_t* data, size_t len,
                                     uint8_t maxAttempts)
{
    for (uint8_t i = 0; i < maxAttempts; ++i) {
        if (channelClear()) {
            radio.transmit(data, len);
            radio.startReceive();
            return true;
        }
        // Escalating backoff staggers two stations so they don't lock-step
        // retry into each other: 8, 16, 24 ... ms.
        delay(static_cast<uint32_t>(8u + i * 8u));
    }
    radio.transmit(data, len);   // forced best-effort send
    radio.startReceive();
    return false;
}

#ifdef DEMO
void VoidProtocol::pollDemoTriggers() {
    // SIMULATE USB GROUND CONNECTION
    // In production, this is replaced by a formal Ground Station command parser or hardware interrupt.
    if (Serial.available() > 0) {
        char cmd = static_cast<char>(Serial.read());
        if (cmd == 'H' || cmd == 'h') {
            updateDisplay("AUTH", "Generating Keys...");
            
            static PacketH_t handshake_pkt;
            Security.prepareHandshake(handshake_pkt, VOID_SESSION_TTL_DEF, millis());
            
            Serial.print("HANDSHAKE_TX:");
            // hexDump((uint8_t*)&handshake_pkt, SIZE_PACKET_H);
            hexDump(reinterpret_cast<uint8_t*>(&handshake_pkt), SIZE_PACKET_H);
            updateDisplay("AUTH", "Handshake Sent");
        }
    }
}
#endif