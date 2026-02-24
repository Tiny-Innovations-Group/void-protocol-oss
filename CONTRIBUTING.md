# Contributing to Void Protocol

> 🛰️ **Authority:** Tiny Innovation Group Ltd
>
> 🔒 **Status:** Restricted Access / Authenticated Contributors Only
>
> 🛡️ **Standard:** NSA/CISA "Clean C++" & SEI CERT C++

Thank you for your interest in contributing to the Void Protocol. Because this software is designed for **Defense-Grade M2M Settlement** and orbital hardware, we adhere to strict standards regarding identity, code provenance, and engineering rigor.

All contributors must follow these guidelines without exception.

---

## 1. Identity & Authentication (Chain of Custody)

This repository operates under a **Zero-Trust Contribution Model**. Anonymous or unverified commits will be rejected.

### 1.1 Organization Membership
* You must be a verified member of the **[Tiny-Innovations-Group](https://github.com/Tiny-Innovations-Group)** organization.
* External contributors must request an invite via the `director-TIG` channel before submitting code.

### 1.2 Git Identity
Your local Git configuration **MUST** match your verified organization identity.
```bash
git config user.name "Your Name (TIG)"
git config user.email "your.handle@tiny-innovations.group"
git config commit.gpgsign true  # GPG Signing is mandatory for Core Logic

```

---

## 2. "Clean Room" Engineering Policy

To maintain our **Apache 2.0** licensing and freedom from IP contamination:

1. **Original Work Only:** Do not copy-paste code from StackOverflow, GPL repositories, or proprietary sources.
2. **No AI-Generated Boilerplate:** While AI can be used for reference, the final committed code must be hand-audited.
3. **Library Restrictions:**
* **Allowed:** `libsodium`, `RadioLib` (MIT/Apache compatible).
* **Banned:** Any GPLv3 libraries that would force copy-left licensing on the commercial stack.



---

## 3. NSA "Clean C++" Standards (Mandatory)

We strictly enforce the **NSA/CISA Memory Safety Guidelines** for C++. Code failing these checks will be rejected during Code Review.

### 3.1 Memory Safety (The "No-Heap" Rule)

* ❌ **BANNED:** `new`, `delete`, `malloc`, `free`. Dynamic memory allocation causes heap fragmentation and timing jitter, which is unacceptable in orbit.
* ✅ **REQUIRED:** Use **Static Allocation** (global/class members) or **Stack Allocation** (local variables).
* ✅ **REQUIRED:** Use **RAII (Resource Acquisition Is Initialization)**. Hardware resources (Radio, OLED) must be initialized in the class constructor and released (if applicable) in the destructor.

### 3.2 Pointer Discipline

* ❌ **BANNED:** Raw pointers (`uint8_t* ptr`) for ownership.
* ❌ **BANNED:** `void*` arithmetic.
* ✅ **REQUIRED:** Use **References (`&`)** for passing objects (e.g., `void transmit(PacketA_t& pkt)`).
* ✅ **REQUIRED:** Use `const` everywhere. If a variable doesn't change, it must be `const`.

### 3.3 Class Design (Encapsulation)

* **Classes vs. Namespaces:** We use `class` to enforce state isolation.
* **Private by Default:** All member variables must be `private` or `protected`.
* **Singletons:** Hardware managers (Radio, Crypto) must be Singletons to prevent race conditions.


* **Rule of Zero:** Classes should not define custom destructors unless managing a specific non-memory resource (like a file handle or radio lock).

### 3.4 Data Integrity

* **Structure Packing:** All OTA structures must be wrapped in `#pragma pack(push, 1)`.
* **Static Assertions:** usage of `static_assert(sizeof(T) == X)` is mandatory to prevent compiler padding drift.
* **Endianness:**
* **Headers:** Big-Endian (Network Order).
* **Payloads:** Little-Endian (native ESP32).
* *Explicitly document endianness in every struct comment.*



---


## 4. C++ Ground Station CLI (`/ground_station`)

We have deprecated the legacy Python prototypes. The Ground Station is now a high-performance, native C++ application.

* **Build System:** **CMake** is mandatory. All cryptographic dependencies must be statically linked.
* **Memory Safety:** The desktop `Bouncer` enforces the exact same "Zero-Heap" and SEI CERT C++ rules as the orbital hardware. No `std::string` or `malloc` is permitted in the packet parsing path.
* **Networking:** The `GatewayClient` must utilize cross-platform, zero-heap socket wrappers (native POSIX / Winsock2) to bridge telemetry to the Go Gateway.
* **Threading:** The CLI uses a dual-thread architecture. Hardware polling and user input loops must be strictly segregated to prevent blocking IO during telemetry ingestion.
* 
---

## 5. Development Environment

To ensure reproducible builds, you must use the specified toolchain:

* **IDE:** VS Code + PlatformIO.
* **Hardware:** Heltec WiFi LoRa 32 V3 (EU868 Region).
* **Config:** Do not modify `platformio.ini` versions without approval.

```ini
[env:heltec_wifi_lora_32_V3]
platform = espressif32
framework = arduino
monitor_speed = 115200

```

---

## 6. Security & Vulnerability Reporting

**DO NOT** open public GitHub Issues for security vulnerabilities (e.g., key leakage, replay attacks).

* **Process:** Encrypt your report using the **Director's Public Key**.
* **Email:** `security@tiny-innovations.group`
* **Bounty:** TIG offers a bounty for valid critical protocol flaws found in `Handshake-spec.md` or `Protocol-spec.md`.

---

## 7. Pull Request Checklist

Before submitting a PR, verify:

1. [ ] **Hardware Test:** Code successfully flashes to Heltec V3 and boots.
2. [ ] **Static Analysis:** Code passes `cppcheck` with no warnings.
3. [ ] **Cycle Check:** No 64-bit misalignment in packet structures.
4. [ ] **Clean Room:** No copy-pasted GPL code.
5. [ ] **Signed:** Commit is GPG signed.

*By submitting a Pull Request, you certify that you have the right to submit your contribution and that you agree to the Apache 2.0 License terms.*