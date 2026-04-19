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

// VOID-128: Asset identity + APIDs + duty-cycle observation target
// Constants match gateway/test/utils/generate_packets.go deterministic mode
// so flat-sat captures are byte-identical to the checked-in golden vectors.
#define SELLER_SAT_ID   0xCAFEBABEu  // Sat A (seller / invoice issuer)
#define BUYER_SAT_ID    0xCAFEBABEu  // Sat B (buyer / Mule) — alpha demo shares deterministic ID; flight builds use per-board IDs
#define SELLER_APID     100u         // CCSDS APID for Sat A
#define BUYER_APID      101u         // CCSDS APID for Sat B

// Duty-cycle TARGET for EU868 / amateur-band unlicensed LoRa.
// 36 s is the legal inter-packet minimum under a 1 % duty cycle at the
// frame sizes / spreading factors used by the flat-sat build. At this
// stage the buyer only LOGS the observed gap — hard enforcement is
// deferred to VOID-070 once we have real flight-duration data. Until
// then, treat dips below this as a flag, not a fault.
#define DUTY_CYCLE_TARGET_MS  36000u

#endif