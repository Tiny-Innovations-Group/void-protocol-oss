# Void Protocol v2.1

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
> 
> Authority: Tiny Innovation Group Ltd
> 
> License: Apache 2.0
> 
> Status: Authenticated Clean Room Spec

Void Protocol v2.1 is a **Defense-Grade** settlement layer designed for **Machine-to-Machine (M2M)** transactions between orbital assets. It facilitates trustless commerce (data, fuel, or compute) using an encrypted store-and-forward architecture with **Perfect Forward Secrecy (PFS)**, optimized for embedded satellite hardware like the **ESP32-S3** (Heltec V3).

---

## 🛠️ System Architecture

The protocol operates across four distinct phases to ensure settlement finality within a **60-second window**:

1. **Handshake (AOS):** Upon Acquisition of Signal, Sat B and Ground perform an ephemeral **ECDH Key Exchange** to establish a forward-secure session.
2. **Sat A (Seller):** Generates service invoices and broadcasts them via LoRa/S-Band.
3. **Sat B (Buyer/Mule):** Acts as a secure courier, encapsulating payment intent and signing transactions using **Physical Unclonable Functions (PUF)**.
4. **Ground Station:** The L2 bridge authority that validates signatures, executes smart contracts, and issues the encrypted "Unlock" command.

---

## 🔒 Security & Performance

* **Defense-Grade Stack:** Utilizes **X25519** for ephemeral key exchange, **Ed25519** for identity signatures, and **ChaCha20** for stream encryption.
* **Forward Secrecy:** Session keys are destroyed after every pass, ensuring that physical capture of a device cannot compromise historical transaction logs.
* **Hybrid Endianness:** CCSDS Headers are **Big-Endian** for ground network compatibility, while payloads are **Little-Endian** for native MCU performance.
* **Cycle Optimization:** All packet structures are aligned to **64-bit machine boundaries** (Rule of 8) to prevent memory access penalties on ARM and ESP32 architectures.

---

## 📦 Packet Overview

| Packet | Name | Size | Function |
| --- | --- | --- | --- |
| **Packet H** | **Handshake** | **112B** | **Ephemeral Key Exchange (ECDH)** |
| **Packet A** | Invoice | 68B | Public service offer from Sat A |
| **Packet B** | Payment | 176B | Encapsulated intent signed by Sat B |
| **ACK** | Acknowledgment | 120B | Ground-to-Sat unlock command and relay instructions |
| **Packet C** | Receipt | 104B | Proof-of-Execution signed by Sat A |
| **Packet D** | Delivery | 128B | Final delivery of receipt to Ground |

---

## 🚀 Development with PlatformIO

This project is structured for **PlatformIO** and optimized for the **Heltec WiFi LoRa 32 V3** (ESP32-S3). To maintain "Clean Room" integrity, ensure your environment adheres to the following specification:

### Hardware & Framework

* **MCU:** ESP32-S3 (via `heltec_wifi_lora_32_V3` board profile)
* **Framework:** Arduino
* **LoRa Region:** EU868
* **Monitor Speed:** 115200 with exception decoding enabled

### Dependencies

* **Sodium:** Used for ChaCha20, SHA-256, Ed25519, and X25519 primitives.
* **RadioLib:** Manages LoRa hardware abstractions.
* **SSD1306 Driver:** For local telemetry display on the Heltec OLED.

### Directory Structure

* `/include`: Protocol headers (`Protocol-spec.h`, `Handshake-spec.h`, `Acknowledgment-spec.h`, `Receipt-spec.h`).
* `/src`: Implementation of the `VoidController` and `SecurityManager`.
* `/docs`: Finalized Markdown specifications for the protocol segments.

---

## 🖋️ Contribution Policy

This is an open-source project. However, all contributors must be authenticated with the **Tiny-Innovations-Group** organization to push changes. Ensure your local git identity is configured to match your authorized organization account:

```bash
git config user.name "director-TIG"
git config user.email "director@tiny-innovations.group"

```

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.