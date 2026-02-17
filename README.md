# Void Protocol v2.0

> 🛰️ VOID PROTOCOL v2.0 | Tiny Innovation Group Ltd
> Authority: Tiny Innovation Group Ltd
> License: Apache 2.0
> Status: Authenticated Clean Room Spec

Void Protocol v2.0 is a high-performance, military-grade settlement layer designed for **Machine-to-Machine (M2M)** transactions between orbital assets. It facilitates trustless commerce (data, fuel, or compute) by utilizing an encrypted store-and-forward architecture optimized for embedded satellite hardware like the **ESP32-S3** (Heltec V3).

---

## 🛠️ System Architecture

The protocol operates across three distinct segments to ensure settlement finality within a **60-second window**:

1. **Sat A (Seller):** Generates service invoices and broadcasts them via LoRa/S-Band.
2. **Sat B (Buyer/Mule):** Acts as a data courier, encapsulating payment intent and signing transactions using **Physical Unclonable Functions (PUF)**.
3. **Ground Station:** The L2 bridge authority that validates signatures, executes smart contracts, and issues the encrypted "Unlock" command.

---

## 🔒 Security & Performance

- **Hybrid Endianness:** CCSDS Headers are processed in **Big-Endian** for ground compatibility, while payloads use **Little-Endian** for native MCU performance.
- **Cryptographic Stack:** Utilizes **ChaCha20** for stream encryption and **SHA-256** for hardware-accelerated integrity checks.
- **Entropy Rule:** Mandatory **86-character passphrases** ensure a full 256-bit key space saturation (~258 bits of entropy).
- **Cycle Optimization:** All packet structures (120B/128B/176B) are aligned to **64-bit machine boundaries** to prevent memory access penalties on ARM and ESP32 architectures.

---

## 📦 Packet Overview

| Packet       | Name           | Size | Function                                            |
| ------------ | -------------- | ---- | --------------------------------------------------- |
| **Packet A** | Invoice        | 68B  | Public service offer from Sat A                     |
| **Packet B** | Payment        | 176B | Encapsulated intent signed by Sat B                 |
| **ACK**      | Acknowledgment | 120B | Ground-to-Sat unlock command and relay instructions |
| **Packet C** | Receipt        | 104B | Proof-of-Execution signed by Sat A                  |
| **Packet D** | Delivery       | 128B | Final delivery of receipt to Ground                 |

---

## 🚀 Development with PlatformIO

This project is structured for **PlatformIO** and optimized for the **Heltec WiFi LoRa 32 V3** (ESP32-S3). To maintain "Clean Room" integrity, ensure your environment adheres to the following specification:

### Hardware & Framework

- **MCU:** ESP32-S3 (via `heltec_wifi_lora_32_V3` board profile)
- **Framework:** Arduino
- **LoRa Region:** EU868
- **Monitor Speed:** 115200 with exception decoding enabled

### Dependencies

- **Sodium:** Used for ChaCha20 and SHA-256 primitives.
- **RadioLib:** Manages LoRa hardware abstractions.
- **SSD1306 Driver:** For local telemetry display on the Heltec OLED.

### Directory Structure

- `/include`: Protocol headers (`Protocol-spec.h`, `Acknowledgment-spec.h`, `Receipt-spec.h`).
- `/src`: Implementation of the `VoidController` and `SecurityManager`.
- `/docs`: Finalized Markdown specifications for the protocol segments.

---

## 🖋️ Contribution Policy

This is an open-source project. However, all contributors must be authenticated with the **Tiny-Innovations-Group** organization to push changes. Ensure your local git identity is configured to match your authorized organization account:

```bash
git config user.name "your-account"
git config user.email "your-email"

```

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.
