# 🛰️ Void Protocol (v2.1)

> **Authority:** Tiny Innovation Group Ltd  
> 
> **License:** Apache 2.0  
> 
> **Status:** Authenticated Clean Room Spec  
> 
> **Compliance:** NSA/CISA Memory Safety Guidelines & SEI CERT C++  

Void Protocol is a **Defense-Grade, Layer 2 Machine-to-Machine (M2M) settlement protocol** designed for orbital, remote, and air-gapped assets. It facilitates trustless commerce (data, fuel, compute) using an asynchronous "Store-and-Forward" architecture with **Perfect Forward Secrecy (PFS)**. 

This repository contains the **Open Source Thick Client** (Device-Side Core), optimized for embedded hardware like the ESP32-S3 (Heltec V3).

---

## 🛡️ Engineering Standard: "Clean C++"

To meet the rigorous demands of aerospace and defense environments, this codebase strictly adheres to **NSA/CISA Memory Safety Guidelines** and **SEI CERT C++** standards. 

* **Zero-Heap Allocation:** `malloc`, `new`, and `std::string` are strictly banned. All packet buffers and state managers use deterministic, static allocation to mathematically eliminate heap fragmentation.
* **Zero-Copy Deserialization:** Incoming radio payloads are never copied. They are parsed directly from static buffers using strict APID header peeking and exact-length validation before pointer casting.
* **Strict Type & Endian Safety:** Over-The-Air (OTA) structures are forced to byte-alignment (`__attribute__((packed))`). CCSDS Headers are Big-Endian; Payloads are Little-Endian.
* **Compiler Enforced:** The `platformio.ini` environment enforces `-Werror`, `-Wvla`, and static analysis (Clang-Tidy) to reject non-compliant code at build time.

---

## 🛠️ System Architecture

The protocol operates asynchronously to ensure settlement finality across disrupted networks:

1. **Handshake (AOS):** Upon Acquisition of Signal, satellites perform an ephemeral **X25519 ECDH Key Exchange** to establish a forward-secure session.
2. **Sat A (Seller):** Generates service invoices and broadcasts them via LoRa/S-Band.
3. **Sat B (Buyer/Mule):** Acts as a secure courier, encapsulating payment intent and signing transactions using hardware Identity Keys (Ed25519 / PUF).
4. **Ground Station:** The L2 authority that validates signatures, batches settlements, and issues the encrypted "Unlock" command via ChaCha20.

*For full flow details, see [ARCHITECTURE.md](./ARCHITECTURE.md).*

---

## 📦 Packet Overview

| Packet | Name | Size | Function |
| :--- | :--- | :--- | :--- |
| **Packet H** | **Handshake** | 112B | Ephemeral Key Exchange (ECDH) & TTL |
| **Packet A** | Invoice | 68B | Public service offer from Sat A |
| **Packet B** | Payment | 176B | Encapsulated intent signed by Sat B |
| **ACK** | Acknowledgment | 120B | Ground-to-Sat encrypted unlock command |
| **Packet C** | Receipt | 104B | Proof-of-Execution signed by Sat A |
| **Packet D** | Delivery | 128B | Final delivery of receipt to Ground Ledger |

---

## 🚀 Quick Start (PlatformIO)

This project requires **PlatformIO** and is pre-configured for the **Heltec WiFi LoRa 32 V3**.

```bash
# 1. Clone the repository
git clone [https://github.com/Tiny-Innovations-Group/void-protocol-oss.git](https://github.com/Tiny-Innovations-Group/void-protocol-oss.git)
cd void-protocol-oss

# 2. Build the firmware (Strict compiler flags enabled)
pio run -e heltec_wifi_lora_32_V3

# 3. Upload to the Heltec board
pio run -e heltec_wifi_lora_32_V3 -t upload

# 4. Monitor serial output (115200 baud)
pio device monitor -b 115200

```

---

## 📂 Repository Structure

* `include/` - NSA-compliant packed structs (`void_packets.h`), constants (`void_types.h`), and headers.
* `src/` - Core logic (`void_protocol.cpp`, `security_manager.cpp`, `buyer.cpp`, `seller.cpp`).
* `platformio.ini` - Build environment and strict GCC/Clang rules.
* `.cursorrules` - AI assistant guardrails enforcing C++ memory safety.

---

## 📚 Documentation & Governance

Please review the following documents before interacting with or contributing to this repository:

* 🏛️ **[Commercial Boundaries (GOVERNANCE.md)](./GOVERNANCE.md)** - Details our Open Core model and Enterprise feature delineations.
* 🔒 **[Security Policy (SECURITY.md)](./SECURITY.md)** - Threat model and vulnerability disclosure (PGP).
* 📝 **[Contribution Guidelines (CONTRIBUTING.md)](./CONTRIBUTING.md)** - Mandatory C++ coding standards.
* 📜 **[License (LICENSE)](./LICENSE)** - Apache 2.0.

---

*© 2026 Tiny Innovation Group Ltd.*