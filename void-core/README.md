# 📦 Void Core (Pure Protocol Layer)

> **Authority:** Tiny Innovation Group Ltd  
> 
> **License:** Apache 2.0  
> 
> **Status:** Authenticated Clean Room Spec
> 
> **Compliance:** NSA/CISA Memory Safety & SEI CERT C++

This directory acts as the **Single Source of Truth** for the Void Protocol. It contains the pure mathematical, cryptographic, and structural definitions required to execute trustless M2M transactions. 

To guarantee absolute portability between embedded orbital hardware and desktop ground stations, **this directory contains zero hardware dependencies.** There are no references to `Arduino.h`, radio drivers, or hardware serial interfaces.

## 🛡️ Core Guarantees
* **Zero-Heap Allocation:** Dynamic memory allocation (`malloc`, `new`, and `std::string`) is strictly banned to mathematically eliminate heap fragmentation.
* **Zero-Copy Deserialization:** Incoming payloads are parsed directly from static buffers using exact-length validation prior to pointer casting.
* **Strict Type Alignment:** OTA structures are forced to byte-alignment using `__attribute__((packed))`.

## 🔑 Cryptographic Primitives
* **ChaCha20:** Utilized for high-speed, hardware-friendly payload encryption.
* **SHA-256:** Utilized for all hashing and key derivation.
* **Ed25519 & X25519:** Utilized for hardware identity signatures and Ephemeral ECDH Key Exchanges.
---

*© 2026 Tiny Innovation Group Ltd.*