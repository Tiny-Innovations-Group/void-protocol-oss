# 🛰️ Satellite Firmware (Edge Node)

> **Authority:** Tiny Innovation Group Ltd  
> 
> **License:** Apache 2.0  
> 
> **Hardware Target:** ESP32-S3 (Heltec WiFi LoRa 32 V3)
> 
> **Build System:** PlatformIO

This directory contains the hardware-specific implementations for the Void Protocol orbital assets. It imports the pure protocol logic from `../void-core/` and binds it to the physical LoRa transceivers, OLED displays, and ESP32 hardware random number generators.

## 📡 Satellite Roles
This unified codebase compiles into two distinct binaries depending on the selected build environment:
* **Sat A (Seller):** Generates service invoices and broadcasts them.
* **Sat B (Buyer/Mule):** Captures Packet A, encapsulates it with payment intent, signs it, and facilitates the uplink.

## 🚀 Quick Start
This project requires PlatformIO and is pre-configured for the Heltec WiFi LoRa 32 V3.

```bash
# Build the firmware (Strict compiler flags enforced)
pio run -e heltec_wifi_lora_32_V3

# Upload to the Heltec board
pio run -e heltec_wifi_lora_32_V3 -t upload

---

*© 2026 Tiny Innovation Group Ltd.*