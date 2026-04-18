/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      void_config.h
 * Desc:      Configuration constants for VOID Protocol Satellite.
 * -------------------------------------------------------------------------*/

#ifndef VOID_CONFIG_H
#define VOID_CONFIG_H

// Heltec V3 LoRa Pins (SX1262)
#define RADIO_NSS       8
#define RADIO_RST       12
#define RADIO_BUSY      13
#define RADIO_DIO1      14

// Heltec V3 OLED Pins
#define OLED_SDA        17
#define OLED_SCL        18
#define OLED_RST        21
#define OLED_ADDR       0x3C

// LoRa Config (EU868)
#define LORA_FREQ       868.0
#define LORA_BW         125.0
#define LORA_SF         9
#define LORA_CR         5
#define LORA_SYNC       0x12 // Private Sync Word

// VOID-128: Asset identity + APIDs + duty-cycle gate
// Constants match gateway/test/utils/generate_packets.go deterministic mode
// so flat-sat captures are byte-identical to the checked-in golden vectors.
#define SELLER_SAT_ID   0xCAFEBABEu  // Sat A (seller / invoice issuer)
#define BUYER_SAT_ID    0xCAFEBABEu  // Sat B (buyer / Mule) — alpha demo shares deterministic ID; flight builds use per-board IDs
#define SELLER_APID     100u         // CCSDS APID for Sat A
#define BUYER_APID      101u         // CCSDS APID for Sat B
#define MIN_TX_INTERVAL_MS  30000u   // ≥30 s inter-packet gate (manual duty-cycle, pending VOID-070)

#endif