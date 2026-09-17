# VOID Protocol — Project Audit

> **Date:** 2026-04-14
> **Author:** Claude (automated audit)
> **Purpose:** Reconcile Notion ticket statuses against actual repository state
> **Repository:** void-protocol (branch: main, commit bfa7950)

---

## Executive Summary

**The Notion board is significantly out of date.** Only 5 tickets are marked Done and 1 In Progress, but the codebase contains completed work spanning Milestones M0 through M6. At least **15 tickets should be moved to Done** and **3-4 more should be marked In Progress or partial**. One ticket (VOID-012) appears to be **missing from the Notion board entirely**.

The project is realistically at **~M4/M5 boundary** — core firmware, packet definitions, crypto, and gateway ingestion are functional. The critical remaining gaps before Pre-Alpha are: GPS integration, MQTT, L2 smart contract, duty cycle enforcement, and environmental testing.

---

## Notion Board Status (As-Is)

| Status | Count | Tickets |
|--------|-------|---------|
| **Done** | 5 | VOID-001, 002, 003, 004, 005 |
| **In Progress** | 1 | VOID-006 |
| **Not Started** | 37 | Everything else |
| **Missing** | 1 | VOID-012 (not found in Notion DB) |

---

## Ticket-by-Ticket Audit

### Legend

- **SYNC** = Notion status matches repo state
- **MISMATCH** = Code is further ahead than Notion reflects
- **MISSING** = Ticket not found in Notion board
- **PARTIAL** = Some acceptance criteria met, not all

---

### M0 — Project Setup (Week 1)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-001 — Repository Setup | Done | **Done** | SYNC | Repo exists: `satellite-firmware/`, `gateway/`, `ground-station/`, `void-core/`, `scripts/`, `docs/`. Note: directory names evolved from spec (`void-firmware/` → `satellite-firmware/`, etc.) but all components present. LICENSE (Apache 2.0) in root. |
| VOID-002 — PlatformIO Scaffold | Done | **Done** | SYNC | `platformio.ini` configured for `heltec_wifi_lora_32_V3`. RadioLib 7.5.0 + libsodium as deps. Compiles with `-Wall -Werror -Wvla -Wconversion`. `sodium_init()` called in `Security.begin()`. |
| VOID-003 — Go Gateway Module Init | Done | **Done** | SYNC | `go.mod` initialized. Uses `crypto/ed25519` (stdlib). Gin HTTP framework. Compiles cleanly. |
| VOID-004 — CI Pipeline | Done | **Partial** | **NEEDS VERIFICATION** | No `.github/workflows/` directory found in repo. If CI exists elsewhere (e.g., external service), it's not in the repo. Acceptance criteria requires GitHub Actions or equivalent. |
| VOID-005 — Project Board Setup | Done | **Done** | SYNC | Notion board exists with tickets imported. |

**M0 Summary:** 4/5 confirmed Done. VOID-004 needs verification — CI workflow files not found in repo.

---

### M1 — Radio Blinky (Week 2)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-010 — SX1262 Basic Tx/Rx | Not started | **Done** | **MISMATCH** | RadioLib `SX1262` initialized in `void_config.h` (pins: NSS=8, RST=12, BUSY=13, DIO1=14). LoRa at 868.0 MHz, BW 125kHz, SF9, CR 4/5. Both `buyer.cpp` and `seller.cpp` transmit and receive packets via RadioLib. Seller broadcasts every 8s, buyer listens and responds. |
| VOID-011 — GPS Module Integration | Not started | **Not Done** | SYNC | No GPS library (TinyGPS++ or equivalent) integrated. No NMEA parsing code. Hardware pins not configured for GPS UART. Position vectors use hardcoded dummy values. |
| VOID-012 — Frequency & Power Check | **MISSING FROM NOTION** | **Partial** | **MISSING** | LoRa configured at 868.0 MHz (correct band). Power/spurious emission testing not documented. Ticket needs to be created in Notion. |

**M1 Summary:** VOID-010 is fully done but marked Not started. VOID-012 is missing from Notion entirely. GPS (VOID-011) genuinely not started.

---

### M2 — First Packet on Air (Week 4)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-020 — Static Type Definitions | Not started | **Done** | **MISMATCH** | `void_types.h`: protocol constants, crypto sizes. `void_packets_snlp.h` (226 lines): all structs with `__attribute__((packed))` and `static_assert` for every size. `void_packets_ccsds.h` (228 lines): same for enterprise tier. Header sizes: 12B (SNLP), 6B (CCSDS). All packet types defined. |
| VOID-021 — SNLP Frame Builder | Not started | **Done** | **MISMATCH** | SNLP header writes sync word `0x1D01A5A5` at offset 0. CCSDS header with correct `packet_data_length`. 2-byte alignment pad. Frame building integrated into packet structs. Both tiers selectable via `VOID_PROTOCOL_TYPE` compile flag. |
| VOID-022 — Heartbeat Gen & Tx | Not started | **Partial** | **MISMATCH** | `HeartbeatPacket_t` defined (48B struct) with fields for battery, temp, pressure, GPS, state. CRC32 computation implemented. BUT: no live sensor population — GPS data missing (VOID-011 blocker), no ADC battery reading, no internal temp sensor read. Struct is complete, data population is not. |
| VOID-023 — TinyGS Reception Test | Not started | **Not Done** | SYNC | No evidence of TinyGS reception verification. This is a test/validation ticket requiring physical hardware setup. |

**M2 Summary:** VOID-020 and VOID-021 are done. VOID-022 is partially done (struct complete, sensor integration blocked on GPS). VOID-023 is a hardware test, genuinely not started.

---

### M3 — Crypto-Signed Packet B (Week 5)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-030 — Pre-Shared Key Provisioning | Not started | **Done** | **MISMATCH** | `security_manager.cpp`: Ed25519 seed derived from 86-char passphrase via SHA-256. `begin()` generates keypair. Role-based seeds (Seller/Buyer). `crypto_sign_ed25519_sk_to_pk()` recovery confirmed in implementation. |
| VOID-031 — Static Invoice Definition | Not started | **Done** | **MISMATCH** | `seller.cpp`: hardcoded invoice with `sat_id=0xAAAAAAAA`, `amount=500`, `asset_id=1`. CRC32 computed. Invoice struct matches `PacketA_t` definition. |
| VOID-032 — Packet B Construction | Not started | **Done** | **MISMATCH** | `buyer.cpp`: builds complete Packet B — encrypts invoice payload with ChaCha20 (`Security.encryptPacketB()`), generates 12-byte nonce, signs with Ed25519, computes global CRC. Total 184B (SNLP) / 176B (CCSDS). |
| VOID-033 — Packet B Tx & Capture | Not started | **Partial** | **MISMATCH** | Packet B transmits via RadioLib (`buyer.cpp`). However, no documented TinyGS capture verification or Python Kaitai parser validation against captured packets. Tx works, verification gap. |
| VOID-034 — NMEA to ECEF Conversion | Not started | **Not Done** | SYNC | No NMEA parser. No ECEF conversion function. Blocked on VOID-011 (GPS integration). Position vectors remain hardcoded. |

**M3 Summary:** VOID-030, 031, 032 are done. VOID-033 is partially done (Tx works, capture test not done). VOID-034 blocked on GPS.

---

### M4 — Ground Ingest (Week 7)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-040 — MQTT Client & SNLP Detection | Not started | **Not Done** | SYNC | Gateway uses HTTP POST (`/api/v1/ingest`), not MQTT. No MQTT client library in `go.mod`. No MQTT connection code. This is a genuine gap — the gateway currently requires direct HTTP ingestion rather than MQTT from TinyGS. |
| VOID-041 — Packet B Parser (Go) | Not started | **Done** | **MISMATCH** | `void_protocol.go`: Kaitai auto-generated parser handles all packet types including Packet B. `ingest.go`: dispatches on packet type, extracts all fields. Kaitai runtime in `go.mod`. |
| VOID-042 — Signature Verification (Go) | Not started | **Done** | **MISMATCH** | `verifier.go` (46 lines): loads public key from registry, decodes hex, validates `ed25519.PublicKeySize`, calls `ed25519.Verify()`. `ingest.go`: calls verification on Packet B and C. Returns 401 on crypto failure. |
| VOID-043 — Nonce Tracking & Replay | Not started | **Not Done** | SYNC | No nonce tracking map. No replay rejection logic. TODO in gateway codebase. |
| VOID-044 — Heartbeat Parser (Go) | Not started | **Done** | **MISMATCH** | `ingest.go`: type-switch dispatcher handles Heartbeat (L) type, logs temp, battery, GPS fields as formatted JSON. |

**M4 Summary:** VOID-041, 042, 044 are done. VOID-040 (MQTT) and VOID-043 (nonce tracking) are genuinely not started. The gateway works but via HTTP, not MQTT — this is a significant architectural deviation from the ticket spec.

---

### M5 — Testnet Settlement (Week 9)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-050 — Contract Scaffold | Not started | **Not Done** | SYNC | No smart contract directory (void-contracts/, contracts/, etc.) in repo. No Anchor/Hardhat/Foundry config. |
| VOID-051 — Minimal Escrow Function | Not started | **Not Done** | SYNC | Blocked on VOID-050. |
| VOID-052 — Gateway → Contract Integration | Not started | **Not Done** | SYNC | `ingest.go` line ~156 has a TODO comment for L2 settlement. No RPC client code. |
| VOID-053 — End-to-End Settlement Test | Not started | **Not Done** | SYNC | Blocked on VOID-050, 051, 052. |

**M5 Summary:** All 4 tickets genuinely not started. Smart contract work is the next major gap.

---

### M6 — Test Vector Suite (Week 10)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-060 — Binary Test Vector Generator | Not started | **Done** | **MISMATCH** | `gateway/test/utils/generate_packets.go` exists. CHANGELOG v2.2.0 confirms: "Replaced `gen_packet.py` with Go native generator using `crypto/ed25519` and `hash/crc32`." Generates `.bin` files with correct signatures and CRCs. |
| VOID-061 — Kaitai Parser Validation | Not started | **Partial** | **MISMATCH** | Ticket specifies Python parser validation. The Go parser is generated and working (`void_protocol.go`). Python validation not confirmed. The Notion ticket title says "(GO)" but the spec document says "(Python)" — potential ticket title mismatch. |
| VOID-062 — Go Parser Validation | Not started | **Done** | **MISMATCH** | `void_protocol.go` auto-generated from Kaitai. Used in production by `ingest.go` to parse all packet types. Kaitai runtime in `go.mod`. |
| VOID-063 — Firmware Struct Validation | Not started | **Partial** | **MISMATCH** | `void-core/test/test_packing.cpp` validates struct sizes via `static_assert`. However, full test vector `.bin` loading and field comparison against `.json` expected values not confirmed. |

**M6 Summary:** VOID-060 and 062 are done. VOID-061 and 063 are partially done.

---

### M7 — Duty Cycle Certified (Week 11)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-070 — Airtime Counter | Not started | **Not Done** | SYNC | No `duty_cycle_t` struct. No TOA tracking after transmissions. No `TX_INHIBIT` flag. |
| VOID-071 — Duty Cycle Soak Test | Not started | **Not Done** | SYNC | Blocked on VOID-070. |
| VOID-072 — Tx Power Compliance | Not started | **Not Done** | SYNC | No RF power measurement documentation. |

**M7 Summary:** All genuinely not started. This is regulatory-critical work.

---

### M8 — Pre-Alpha Gate (Week 12)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-080 — Cold Soak Test | Not started | **Not Done** | SYNC | Hardware test. |
| VOID-081 — Range Test | Not started | **Not Done** | SYNC | Hardware test. |
| VOID-082 — Full Dress Rehearsal | Not started | **Not Done** | SYNC | Depends on all M7. |

**M8 Summary:** All genuinely not started. These are physical hardware tests.

---

### M9 — CAA NOTAM (Week 14)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-090 — CAA NOTAM Application | Not started | **Not Done** | SYNC | Regulatory. |
| VOID-091 — Payload Enclosure Design | Not started | **Not Done** | SYNC | Hardware. |

**M9 Summary:** All genuinely not started.

---

### M10 — Flight Ready (Week 22)

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-092 — Recovery System | Not started | **Not Done** | SYNC | Hardware. |
| VOID-093 — Launch Rehearsal | Not started | **Not Done** | SYNC | Hardware test. |
| VOID-094 — Flight Day Checklist | Not started | **Not Done** | SYNC | Docs. |

---

### M11 — HAB Launch (Week 24)

No tickets in this milestone (it's the launch event itself).

---

### Epic 10 — Documentation

| Ticket | Notion Status | Actual Status | Verdict | Evidence |
|--------|--------------|---------------|---------|----------|
| VOID-100 — Pre-Alpha README | Not started | **Done** | **MISMATCH** | Comprehensive `README.md` in root. Also: `ARCHITECTURE.md`, `SECURITY.md`, `CONTRIBUTING.md`, `GOVERNANCE.md`, `COMPLIANCE.md`, `CHANGELOG.md`, `CODE_OF_CONDUCT.md`, plus 8+ spec docs in `docs/`. Far exceeds acceptance criteria. |
| VOID-101 — Flight Report Template | Not started | **Not Done** | SYNC | No template. M10 target. |

---

## Summary: What Notion Should Show

### Tickets That Should Be Marked DONE (15)

| Ticket | Milestone | Why |
|--------|-----------|-----|
| VOID-010 — SX1262 Basic Tx/Rx | M1 | RadioLib fully configured, both roles transmit/receive |
| VOID-020 — Static Type Definitions | M2 | All packet structs defined, packed, static_assert validated |
| VOID-021 — SNLP Frame Builder | M2 | Sync word, CCSDS header, alignment pad all implemented |
| VOID-030 — Pre-Shared Key Provisioning | M3 | Ed25519 keypair generation from passphrase, role-based seeds |
| VOID-031 — Static Invoice Definition | M3 | Hardcoded invoice in seller.cpp with correct fields |
| VOID-032 — Packet B Construction | M3 | Full Packet B: ChaCha20 encryption, Ed25519 signature, CRC |
| VOID-041 — Packet B Parser (Go) | M4 | Kaitai auto-generated, used in production gateway |
| VOID-042 — Signature Verification (Go) | M4 | Ed25519 verify via Go stdlib, registry lookup |
| VOID-044 — Heartbeat Parser (Go) | M4 | Type-switch dispatcher in ingest.go |
| VOID-060 — Binary Test Vector Generator | M6 | Go-native replacement for gen_packet.py (per CHANGELOG v2.2.0) |
| VOID-062 — Go Parser Validation | M6 | Kaitai Go parser generated and integrated |
| VOID-100 — Pre-Alpha README | Docs | Comprehensive README + 8 spec docs + CHANGELOG |

### Tickets That Should Be Marked IN PROGRESS (4)

| Ticket | Milestone | What's Done | What's Left |
|--------|-----------|-------------|-------------|
| VOID-022 — Heartbeat Gen & Tx | M2 | HeartbeatPacket_t struct defined (48B), CRC32 implemented | Live sensor population (GPS, ADC, temp) — blocked on VOID-011 |
| VOID-033 — Packet B Tx & Capture | M3 | Packet B transmits via LoRa | TinyGS capture verification, Python Kaitai parse validation |
| VOID-061 — Kaitai Parser Validation | M6 | Go parser working. Ticket title in Notion says "(GO)" but spec says "(Python)" | Need Python parser validation or clarify scope |
| VOID-063 — Firmware Struct Validation | M6 | test_packing.cpp validates struct sizes | Full .bin loading and field comparison against .json vectors |

### Tickets Correctly Marked NOT STARTED (22)

VOID-011, VOID-023, VOID-034, VOID-040, VOID-043, VOID-050, VOID-051, VOID-052, VOID-053, VOID-070, VOID-071, VOID-072, VOID-080, VOID-081, VOID-082, VOID-090, VOID-091, VOID-092, VOID-093, VOID-094, VOID-101

### Ticket MISSING From Notion (1)

| Ticket | Milestone | Description |
|--------|-----------|-------------|
| VOID-012 — Frequency & Power Compliance Check | M1 | Verify 868 MHz band, +14 dBm ERP. Not found in Notion database. |

---

## Milestone Gate Assessment

| Milestone | Gate Status | Blockers |
|-----------|-----------|----------|
| **M0 — Project Setup** | **PASSED** | Verify CI pipeline (VOID-004) actually exists |
| **M1 — Radio Blinky** | **PARTIALLY PASSED** | Radio: done. GPS: not done (VOID-011). Can't confirm GPS lock. |
| **M2 — First Packet on Air** | **PARTIALLY PASSED** | Structs and frames: done. Heartbeat with live data: blocked on GPS. TinyGS reception: not tested. |
| **M3 — Crypto-Signed Packet B** | **MOSTLY PASSED** | Packet B construction and crypto: done. TinyGS capture verification: not done. NMEA/ECEF: blocked on GPS. |
| **M4 — Ground Ingest** | **PARTIALLY PASSED** | Parsing + sig verification: done. BUT via HTTP not MQTT. Nonce tracking: not done. |
| **M5 — Testnet Settlement** | **NOT PASSED** | No smart contract. No L2 integration. |
| **M6 — Test Vector Suite** | **PARTIALLY PASSED** | Go vectors and parser: done. Python + firmware validation: incomplete. |
| **M7 — Duty Cycle** | **NOT PASSED** | No airtime counter. No compliance testing. |
| **M8 — Pre-Alpha Gate** | **NOT PASSED** | Requires M7. Physical tests needed. |
| **M9-M11** | **NOT PASSED** | Regulatory + hardware. |

---

## Critical Path: What to Work on Next

Based on this audit, the highest-impact work to unlock forward progress:

### Priority 1: Unblock M1/M2 (GPS is the biggest single blocker)

1. **VOID-011 — GPS Module Integration** — Unblocks VOID-022 (heartbeat with live data), VOID-034 (ECEF conversion), and all position-dependent functionality.

### Priority 2: Complete M4 (Gateway needs MQTT)

2. **VOID-040 — MQTT Client & SNLP Detection** — The gateway currently uses HTTP POST, but the architecture requires MQTT from TinyGS. This is the bridge between satellite and ground.
3. **VOID-043 — Nonce Tracking & Replay Rejection** — Security-critical, straightforward to implement (in-memory map).

### Priority 3: Start M5 (Smart Contract)

4. **VOID-050 — Contract Scaffold** — Unblocks the entire L2 settlement chain (VOID-051, 052, 053).

### Priority 4: Regulatory (M7, time-sensitive)

5. **VOID-070 — Airtime Counter** — Duty cycle enforcement is regulatory-critical and must be in place before any outdoor/flight testing.

---

## Architectural Deviations from Original Spec

These are not bugs — they're evolution decisions that should be documented:

| Original Spec | Current Reality | Impact |
|---------------|----------------|--------|
| Directory: `void-firmware/` | `satellite-firmware/` | Naming only. No functional impact. |
| Directory: `void-gateway/` | `gateway/` | Naming only. |
| Directory: `void-contracts/` | Does not exist | Smart contract work not started. |
| Directory: `void-ksy/` | KSY integrated into `void-core/` and generated in `gateway/` | Distributed but functional. |
| Directory: `void-scripts/` | `scripts/` | Naming only. |
| Gateway ingests via MQTT | Gateway ingests via HTTP POST | Significant deviation. HTTP works for direct testing but production flow requires MQTT from TinyGS. |
| `gen_packet.py` (Python) | `generate_packets.go` (Go) | Intentional migration (CHANGELOG v2.2.0). Eliminates Python dependency. |
| `ground_keys.json` for key exchange | Hardcoded registry in `registry.go` | Functional but less flexible. |
| Added: `void-core/` | Not in original spec | New shared library layer for packet structs and crypto. Good architectural addition. |
| Added: `ground-station/` (C++) | Not in original spec | C++ ground station with bouncer/gateway client. Partially implemented. |

---

## Recommended Notion Board Actions

1. **Move 15 tickets to Done** (see list above)
2. **Move 4 tickets to In Progress** (see list above)
3. **Create VOID-012** (Frequency & Power Compliance Check) — it's in the spec doc but missing from the board
4. **Update VOID-006 status** — KSY audit may be further along than "In Progress" given Kaitai parsers are generated and integrated
5. **Add notes to VOID-004** — Verify CI pipeline exists; if not, it should be moved back to In Progress
6. **Clarify VOID-061 scope** — Notion title says "(GO)" but spec says "(Python)". If Go validation is the actual scope, it's done.

---

*This audit was generated by comparing the repository state at commit bfa7950 against the Notion "Tickets" database in the Void Protocol workspace. No code changes were made.*
