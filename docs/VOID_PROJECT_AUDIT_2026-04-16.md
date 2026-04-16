# VOID Protocol — Project Audit & Alpha-Balloon Path Plan

- **Date:** 2026-04-16
- **Author:** Claude (automated audit on branch `claude/gifted-bose`)
- **Scope:** Reconcile Notion ticket board against current `main` codebase; chart the critical path to a **high-altitude balloon (HAB) alpha test within 6 months** doing **one plaintext end-to-end transaction over SNLP** (no ChaCha20 in alpha).
- **Authority for canonical wire format:** [docs/kaitai_struct/](kaitai_struct/) (hardened KSY, source of truth).

---

## 1. Executive Summary

The codebase is in strong structural shape. All **packet structs, KSY schemas, Ed25519 signing scaffolding, parser, and regression suites** are present and pass on both C++ and Go sides. The **cross-implementation gate (VOID-126) is green** and golden vectors (VOID-123) are checked in and deterministic.

The **remaining work to reach an alpha balloon flight** is almost entirely **integration glue** — not protocol work. The critical path is:

1. Bypass ChaCha20 via a build flag (plaintext-only alpha).
2. Complete the buyer-side Packet B TX pipeline in firmware.
3. Wire the ground bouncer's signature check through to the gateway (currently stubbed).
4. Produce a bench-top end-to-end demo, then layer on HAB-specific config, GPS stub, MQTT ingest, and flight ops.

At 61 total tickets (27 Done, 4 In progress, 30 Not started as of audit start), we were **carrying 3 stale "In progress" tickets** that are already delivered by later consolidation tickets. Those have been moved to Done. Seven new tickets (**VOID-127 through VOID-133**) have been created to capture the alpha path explicitly.

---

## 2. Ticket Reconciliation

### 2.1 Stale tickets moved to Done

| Ticket | Previous status | Evidence it is done |
|---|---|---|
| **VOID-061 — Kaitai Parser Validation (Go)** | In progress | [gateway/internal/void_protocol/void_protocol.go](../gateway/internal/void_protocol/void_protocol.go) is the generated parser; [parser_test.go](../gateway/internal/void_protocol/parser_test.go), [roundtrip_test.go](../gateway/internal/void_protocol/roundtrip_test.go), [header_size_test.go](../gateway/internal/void_protocol/header_size_test.go) pass. Consolidated under VOID-124 (Done). |
| **VOID-063 — Firmware Struct Validation** | In progress | [void-core/test/test_golden_vectors.cpp](../void-core/test/test_golden_vectors.cpp), [test_packet_sizes.cpp](../void-core/test/test_packet_sizes.cpp), [test_offsets.cpp](../void-core/test/test_offsets.cpp), [test_packing.cpp](../void-core/test/test_packing.cpp) cover it. Consolidated under VOID-125 (Done). |
| **VOID-117 — Deploy Hardened Modular KSY Files** | Not started | All four hardened modules live at [docs/kaitai_struct/](kaitai_struct/) — `void_protocol.ksy`, `ccsds_primary_header.ksy`, `snlp_header.ksy`, `tig_common_types.ksy`. [CLAUDE.md](../CLAUDE.md) declares this directory the source of truth. Delivered by PR #6 (commit `873e90d`). |

No other ticket was judged safely stale. **VOID-022 (Heartbeat Generation & TX)** and **VOID-033 (Packet B TX & Capture)** remain genuinely in progress — struct and framing exist but transmission loops are incomplete.

### 2.2 New tickets created for the alpha-balloon path

All created in Notion under the Tickets database:

| ID | Title | Priority | Role |
|---|---|---|---|
| **VOID-127** | Alpha Plaintext Build Flag (skip ChaCha20) | P0 | Foundation. `VOID_ALPHA_PLAINTEXT` compile flag bypasses ChaCha20 in firmware & gateway; signing stays on. |
| **VOID-128** | Buyer Packet B TX Pipeline (firmware, plaintext) | P0 | Complete the buyer satellite loop: RX Packet A → build B → Ed25519 sign → LoRa TX. Closes VOID-033. |
| **VOID-129** | Wire Ground Bouncer → Gateway Signature Verify | P0 | Replace `Bouncer::validate_signature()` stub ([ground-station/src/bouncer.cpp](../ground-station/src/bouncer.cpp)) by forwarding raw frames to gateway's existing Ed25519 verifier (VOID-042, Done). |
| **VOID-130** | Alpha End-to-End Bench Demo (plaintext SNLP) | P0 | 2× Heltec V3 + ground station + gateway on bench. 10 consecutive successful transactions + 1 deliberate tamper-rejection test = "alpha done" gate. |
| **VOID-131** | HAB Alpha Flight Config & Power Budget | P1 | `flight_alpha` PlatformIO env, duty-cycle math, battery budget, thermal profile, flight-mode logging. |
| **VOID-132** | Synthetic GPS Stub for Alpha Demo | P1 | Replay a pre-recorded HAB trajectory via `VOID_GPS_STUB` flag so VOID-011 (real u-blox) doesn't block flight. |
| **VOID-133** | HTTP→MQTT Bridge OR Direct TinyGS HTTP Ingest | P1 | Closes the TinyGS gap. Pick: small Go adapter service OR add MQTT listener inside gateway. Closes VOID-040, unblocks VOID-023. |

---

## 3. Codebase Reality Check

### 3.1 What works today

- **Packet structs (A, B, C, D, H, ACK, Heartbeat)** in both SNLP and CCSDS tiers — sizes locked, `static_assert` both `% 8 == 0` and `≤ 255` bytes. See [void-core/include/void_packets.h](../void-core/include/void_packets.h), [void_packets_snlp.h](../void-core/include/void_packets_snlp.h), [void_packets_ccsds.h](../void-core/include/void_packets_ccsds.h).
- **Kaitai parsers**: canonical KSY at [docs/kaitai_struct/](kaitai_struct/); Go generated parser in gateway; C++ structs byte-identical (VOID-125).
- **Ed25519 signing** on firmware via libsodium: [void-core/src/security_manager.cpp](../void-core/src/security_manager.cpp).
- **Go gateway**: `POST /api/v1/ingest`, parser, Ed25519 verifier ([gateway/internal/core/security/verifier.go](../gateway/internal/core/security/verifier.go)), in-memory sat registry.
- **Seller firmware loop**: Packet A broadcast every 8 s (`satellite-firmware/src/seller.cpp`).
- **LoRa PHY**: SX1262 via RadioLib, EU868, configured in `satellite-firmware/include/void_config.h`.
- **Build & CI**: `gateway` builds clean (`go build ./...` succeeds); C++ `void_full_tests` target builds two tier variants. GitHub Actions cross-impl gate green (VOID-126).
- **Golden vectors**: 14 deterministic `.bin` files under [test/vectors/](../test/vectors/).

### 3.2 What is NOT done (ranked by blast radius for alpha)

| Gap | Where | Blast radius |
|---|---|---|
| **Buyer Packet B TX pipeline** stubbed | [satellite-firmware/src/buyer.cpp](../satellite-firmware/src/buyer.cpp) | ★★★ Blocks any end-to-end transaction. Owned by VOID-128. |
| **Ground bouncer crypto** stubbed (`validate_signature`, `decrypt_payload` both return unconditionally) | [ground-station/src/bouncer.cpp:23-38](../ground-station/src/bouncer.cpp) | ★★★ Owned by VOID-129. For alpha plaintext, decrypt stub is acceptable; verify stub must be wired. |
| **No MQTT transport** into gateway | `gateway/internal/transport/` absent | ★★ Blocks TinyGS. Owned by VOID-133. |
| **ChaCha20 encrypt path** unimplemented (stub in security_manager) | Firmware + gateway | ★★ **Deferred for alpha** per VOID-127. Becomes star-3 for beta/production. |
| **GPS real u-blox integration** | VOID-011 open | ★★ Mitigated by VOID-132 synthetic stub for alpha. |
| **Duty-cycle enforcement** | VOID-070 open | ★★ Manual scheduling acceptable for one alpha flight. |
| **Nonce/replay tracking** (gateway) | VOID-043 open | ★ Irrelevant for one-shot alpha demo; critical for beta. |
| **NVS persistence of monotonic epoch** | [security_manager.cpp](../void-core/src/security_manager.cpp) marked `TODO(VOID-110)` | ★ Reboot scenario uncommon on one flight. |
| **Smart contract settlement** | VOID-050/051/052/053 all open | ★ **Not required for alpha balloon test.** This is the "money moves" phase and should be explicitly out of scope until post-flight. |

### 3.3 Medium-priority hygiene tickets that can wait

VOID-115 (PacketB/TunnelData signature alignment), VOID-116 (relay_ops u32 alignment), VOID-118 (heartbeat auth), VOID-119 (PacketD global_crc alignment), VOID-120 (SNLP offset comments), VOID-121 (demo key segregation), VOID-122 (CRC pre-validation before dispatch).

**Recommendation:** keep as Not started. Revisit after VOID-130 passes. None block an alpha flight; all are prudent for beta.

---

## 4. Critical Path to Alpha Balloon Flight (≤ 6 months)

### Phase A — Alpha Software Gate (Weeks 1–3)
**Goal:** 10-run bench demo with plaintext transaction.

1. **VOID-127** — plaintext build flag (firmware + gateway). ~1–2 days.
2. **VOID-132** — synthetic GPS stub. ~2–3 days (can run in parallel).
3. **VOID-128** — buyer Packet B TX pipeline. ~3–5 days.
4. **VOID-129** — bouncer → gateway signature forwarding. ~1–2 days.
5. **VOID-130** — bench demo (alpha done). ~2–3 days including tamper tests.

Exit criterion: `scripts/demo_alpha.sh` prints "PASS" for 10 consecutive runs and "REJECT" for a tampered frame.

### Phase B — Ingest & Compliance Runway (Weeks 4–8)
6. **VOID-133** — TinyGS MQTT ingest.
7. **VOID-012** — frequency & ETSI power compliance paperwork.
8. **VOID-070 + VOID-072** — airtime counter + conducted-power test on spectrum analyser.
9. **VOID-131** — flight config + power budget doc.

### Phase C — Environmental & RF Validation (Weeks 8–14)
10. **VOID-080** — cold-soak –30 °C × 2 h.
11. **VOID-081** — 20 km line-of-sight range test (ideally from a high-site hill).
12. **VOID-071** — 24 h duty-cycle soak.

### Phase D — HAB Hardware & Launch Ops (Weeks 12–20)
13. **VOID-091** — payload enclosure (EPS, < 500 g).
14. **VOID-092** — recovery (secondary GPS beacon, parachute, cutdown, flight prediction).
15. **VOID-090** — CAA NOTAM filing (typically 28-day minimum lead time; file early).
16. **VOID-093** — tethered/short free-flight rehearsal.
17. **VOID-094** — flight-day checklist.

### Phase E — Flight & Post-flight (Weeks 20–24)
18. **VOID-082** — full dress rehearsal.
19. **Launch window** — primary balloon launch.
20. **VOID-101** — flight report.

### Explicit Non-Goals for Alpha Flight
- ChaCha20 encryption (VOID-127 deliberately defers; enc path is for beta).
- Smart contract on-chain settlement (VOID-050/051/052/053 — post-alpha).
- Nonce/replay protection (VOID-043 — post-alpha).
- Heartbeat cryptographic auth (VOID-118 — post-alpha).
- Production key segregation (VOID-121 — before any non-alpha build).

---

## 5. Risk Register (alpha horizon)

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| CAA NOTAM filing slips | Medium | Flight delayed 4+ weeks | Start VOID-090 in Phase B (week 4) not Phase D. |
| Range test fails below 20 km | Medium | Flight ops uncertainty | Budget a second attempt; consider antenna tuning + higher-gain yagi on ground. |
| Heltec V3 cold-fails at –30 °C | Medium | Payload death at altitude | VOID-080 early; insulation + self-heating resistor if required. |
| GPS stub masks real integration bugs | High | Post-flight debugging pain | VOID-011 real u-blox work should begin in parallel with Phase B even if flight uses stub. |
| Bouncer stub removal regressions | Low | Demo fails under tamper | VOID-130 mandates explicit tamper case in acceptance. |
| ChaCha20 technical debt accrues | High | Harder to retrofit post-alpha | Keep VOID-127 flag narrowly scoped to encrypt/decrypt — do NOT also disable nonce derivation or signature scope, so the re-enable path stays a one-line change. |

---

## 6. Concrete Next Actions for This Week

1. **Create branch `alpha/void-127-plaintext-flag`**, implement the flag in firmware + gateway, land PR. [VOID-127]
2. **Start buyer pipeline PR** targeting `satellite-firmware/src/buyer.cpp`. [VOID-128]
3. **Spike VOID-133** — decide MQTT bridge vs native listener by end of week; write decision doc.
4. **Draft CAA NOTAM application** now (do NOT wait for Phase D). [VOID-090]
5. **Order HAB hardware long-lead items** (balloon envelopes, He, parachutes) based on predicted 6-month timeline.

---

## 7. Closing Note

The protocol is sound. The schemas are hardened. The tests pass. What's left is the messy but tractable work of turning a spec-compliant packet library into a balloon that flies. **Alpha is roughly 3 weeks of engineering away** (VOID-127 → VOID-130) plus **~4 months of RF validation and flight-ops** that cannot be compressed.

Ship VOID-127 through VOID-130 first. Then we have a demo to stand behind while the long-lead paperwork and hardware proceed in parallel.
