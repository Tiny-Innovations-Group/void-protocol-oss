/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      void_protocol.h
 * Desc:      Main VOID Protocol class definition and global instance.
 *            This is the central hub for all protocol operations, including
 *            packet handling, display updates, and CRC calculations.
 * -------------------------------------------------------------------------
 * WARNING: Payloads are Little-Endian. Headers are Big-Endian.
 * -------------------------------------------------------------------------*/

#ifndef VOID_PROTOCOL_H
#define VOID_PROTOCOL_H

#include <Arduino.h>
#include <RadioLib.h>
#include <sodium.h>
#include "SSD1306Wire.h"
#include "void_packets.h" // Your Packet Structs

class VoidProtocol {
public:
    SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY);
    SSD1306Wire display = SSD1306Wire(OLED_ADDR, OLED_SDA, OLED_SCL, GEOMETRY_128_64);

    void begin();
    void updateDisplay(String status, String subtext);
    void hexDump(uint8_t* data, size_t len);
    uint32_t calculateCRC(uint8_t* data, size_t len);

    #ifdef DEMO
    void pollDemoTriggers();
    #endif
};

extern VoidProtocol Void; // Global instance

#endif