# Security Policy: Void Protocol

> 🛰️ **Authority:** Tiny Innovation Group Ltd
>
> 🔒 **Status:** Authenticated Clean Room Spec
>
> 🛡️ **Standard:** NSA/CISA "Clean C++" & SEI CERT C++

Tiny Innovation Group Ltd takes the security of the Void Protocol seriously. Because this software is designed for **Defense-Grade M2M Settlement** and orbital hardware, we operate under a strict vulnerability disclosure and engineering policy.

---

## 1. Supported Versions

We currently provide security updates for the following protocol versions:

| Version | Supported | Description |
| :--- | :--- | :--- |
| **v2.1 (Current)** | ✅ Yes | MVP / Open Source Core (Heltec V3 Target) |
| **v1.x** | ❌ No | Deprecated proof-of-concept |

---

## 2. Reporting a Vulnerability

**DO NOT OPEN PUBLIC GITHUB ISSUES FOR SECURITY VULNERABILITIES.**

If you discover a vulnerability in the Void Protocol (e.g., cryptographic bypass, memory corruption, replay attack), you must report it privately. 

1. **Email:** `security@tiny-innovations.group`
2. **Encryption:** All reports MUST be encrypted using the **Director's PGP Public Key** (Available at `https://tiny-innovations.group/security/pgp.txt`).
3. **Format:** Include a Proof of Concept (PoC) script or exact steps to reproduce the flaw.

### 2.1 Response Time & Bounty
* **Triage:** We will acknowledge receipt of your report within **48 hours**.
* **Bounty:** TIG offers a discretionary bug bounty for critical, reproducible flaws found in `Handshake-spec.md`, `Protocol-spec.md`, or the `SecurityManager` implementation.

---

## 3. Threat Model & Scope

When auditing the Void Protocol, please adhere to our defined Threat Model. 

### 🎯 In-Scope (We want to know about these)
* **Cryptographic Flaws:** Weaknesses in our implementation of Ed25519 (Identity), X25519 (Ephemeral Exchange), or ChaCha20 (Payload).
* **Protocol Logic Bypass:** Ability to forge a `PacketAck`, bypass the Session TTL, or execute a Replay Attack.
* **Memory Corruption:** Buffer overflows, stack smashing, or type-confusion vulnerabilities caused by malicious Over-The-Air (OTA) packet injection.

### 🚫 Out-of-Scope (Known hardware limitations of the MVP)
* **Physical Key Extraction:** The current v2.1 open-source release runs on commodity ESP32 hardware (Heltec V3) without a Secure Element (e.g., ATECC608). Physical side-channel attacks or flash dumping to extract the `_identity_priv` key are known risks and are mitigated in the Enterprise hardware tier.
* **Local Serial MITM:** Attacks requiring physical access to the Ground Station USB cable.
* **LoRa Jamming:** Physical RF denial-of-service is handled at the physical layer, not the protocol layer.

---

## 4. Architectural Security Principles

For researchers auditing the code, be aware that this repository enforces the **NSA/CISA Memory Safety Guidelines** and **SEI CERT C++** standards.

1. **No-Heap Policy:** Dynamic memory allocation (`malloc`, `new`, `std::string`) is strictly banned. All packet buffers and state managers are statically allocated to prevent heap fragmentation.
2. **Zero-Copy Parsing:** We do not copy payloads into secondary buffers. Data is verified via strict header APID peeking and length-checks before casting a bounded view over the static receive buffer.
3. **Forward Secrecy:** Ephemeral X25519 private keys are wiped from memory (`sodium_memzero`) the exact millisecond the session key is derived.
4. **Strict Typing:** All OTA packet structures use explicit byte-alignment (`#pragma pack(push, 1)` and `__attribute__((packed))`). 

*By participating in our security program, you agree to coordinate disclosure and allow TIG up to 90 days to patch critical protocol vulnerabilities before public release.*