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
#include "void_config.h" // Your Packet Structs

class VoidProtocol {
public:
    SX1262 radio = new Module(RADIO_NSS, RADIO_DIO1, RADIO_RST, RADIO_BUSY);
    SSD1306Wire display = SSD1306Wire(OLED_ADDR, OLED_SDA, OLED_SCL, GEOMETRY_128_64);

    void begin();
    void updateDisplay(const char* status, const char* subtext);
    void hexDump(const uint8_t* data, size_t len);
    uint32_t calculateCRC(const uint8_t* data, size_t len);

    // VOID-139: Listen-before-talk (LBT) on the shared half-duplex LoRa
    // channel. channelClear() runs one hardware CAD scan (SX1262 Channel
    // Activity Detection) and returns true when no LoRa preamble is on the
    // air. transmitWhenClear() CAD-gates a MANDATORY transmit: it waits for
    // a clear channel up to maxAttempts (short escalating backoff between
    // tries), then transmits best-effort regardless — dropping a relayed
    // receipt/unlock would stall the commerce loop. Returns true iff the
    // frame went out on a verified-clear channel. Both leave the radio in
    // RX afterwards.
    bool channelClear();
    bool transmitWhenClear(uint8_t* data, size_t len, uint8_t maxAttempts);

    // VOID-139: true only when the radio's IRQ flags show a completed packet
    // RECEPTION (RxDone). Our single DIO1 action (onRxDone) is also asserted
    // by the SX1262 on TxDone and CadDone, so a blocking transmit() or a
    // scanChannel() sets the rx_flag spuriously. Callers MUST gate on this
    // before trusting getPacketLength()/readData() — otherwise they parse
    // stale FIFO bytes (the just-transmitted frame; TX and RX share FIFO
    // base 0x00) and mis-report an app-layer CRC failure.
    bool isRealReception();

#if VOID_PROTOCOL_TYPE == 2
    // VOID-143: SNLP sync-word validation (Protocol-spec-SNLP.md §1.1).
    // First-line filter against noise and foreign LoRa traffic on crowded
    // amateur bands — every SNLP frame opens with sync word 0x1D01A5A5
    // (BE32); frames that fail it are dropped before any header parsing.
    // Previously file-static in buyer.cpp only — the seller parsed inbound
    // frames with no sync check, risking false CRC passes on noise. Unifies
    // both roles on one implementation.
    bool validSyncWord(const uint8_t* buf);
#endif

    #ifdef DEMO
    void pollDemoTriggers();
    #endif
};

extern VoidProtocol Void; // Global instance

#endif