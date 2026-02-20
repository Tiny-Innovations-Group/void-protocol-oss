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

void VoidProtocol::begin()
{
    // 1. Init Serial
    Serial.begin(115200);
    while (!Serial)
        ;

    // 2. Init Display
    pinMode(OLED_RST, OUTPUT);
    digitalWrite(OLED_RST, LOW);
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
    int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR, LORA_SYNC, 10);
    if (state != RADIOLIB_ERR_NONE)
    {

        char errBuf[32];
        snprintf(errBuf, sizeof(errBuf), "LoRa Fail: %d", state);
        updateDisplay("ERROR", errBuf);

        while (1)
            ;
    }

    // Set Output Power to +22 dBm (Heltec V3 limit)
    radio.setOutputPower(22);

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

// Simple CRC32 wrapper (or use Sodium's logic)
// CRC32 Stub - In production, replace with hardware CRC or optimized table
uint32_t VoidProtocol::calculateCRC(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    // Simple Loop (Not real CRC32, but consistent for demo)
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
    }
    return crc;
}

#ifdef DEMO
void VoidProtocol::pollDemoTriggers() {
    // SIMULATE USB GROUND CONNECTION
    // In production, this is replaced by a formal Ground Station command parser or hardware interrupt.
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        if (cmd == 'H' || cmd == 'h') {
            updateDisplay("AUTH", "Generating Keys...");
            
            static PacketH_t handshake_pkt;
            Security.prepareHandshake(handshake_pkt, VOID_SESSION_TTL_DEF);
            
            Serial.print("HANDSHAKE_TX:");
            // hexDump((uint8_t*)&handshake_pkt, SIZE_PACKET_H);
            hexDump(reinterpret_cast<uint8_t*>(&handshake_pkt), SIZE_PACKET_H);
            updateDisplay("AUTH", "Handshake Sent");
        }
    }
}
#endif