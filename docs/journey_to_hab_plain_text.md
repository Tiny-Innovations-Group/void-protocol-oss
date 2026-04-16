# Journey to HAB — Plaintext Alpha (LOCKED PATH)

- **Locked:** 2026-04-16
- **Last revised:** 2026-04-16 (v3 — flat-sat scope: VOID-051/052 on local chain, VOID-053 rescoped + moved to Phase B, VOID-129 stays full-verify, Phase A collapses 12 → 11)
- **Goal:** One end-to-end VOID transaction — **LoRa → Ground → Gateway → on-chain settlement** — in **plaintext SNLP**, executed on a high-altitude balloon within **6 months** (target launch window: **October 2026**). Heartbeat telemetry runs throughout for flight tracking.
- **Flat-sat milestone (dry HAB, desk-top):** End of Phase A (ticket #11, VOID-130). Two Heltec boards + ground bouncer + gateway + local Anvil chain complete a full plaintext payment + heartbeat loop, 10/10 consecutive passes. No RF range, no balloon, no testnet — desk-controlled.
- **Out of scope for this journey:** ChaCha20 payload encryption, heartbeat crypto auth, nonce/replay tracking, production key management, simulation/stub harnesses, testnet smart-contract deployment, and the KSY hygiene tickets that only touch packets not flown in alpha. These re-enter scope for the post-HAB beta journey — do **NOT** smuggle them in here.
- **Authority:** This document + the Notion `Journey = journey-to-hab` tag are the single source of truth for the critical path. If a ticket is not listed here, it is **not** on the path.

---

## 🔒 Rules of the Locked Path

1. **No divergence.** If a future chat / PR / idea is not one of the 31 tickets below, it does not belong on this journey. Park it for the post-HAB beta journey.
2. **Order matters.** The `Journey Order` number in Notion is the execution sequence. Do not start ticket `N` until ticket `N-1` is Done, **unless** explicitly flagged "can run in parallel" below.
3. **Closing the journey** = ticket #31 (VOID-101 Flight Report) is Done after a successful HAB flight has proven ticket #11 (flat-sat alpha demo) works from the sky. Until then, this journey is open.
4. **Adding a ticket** requires: updating this markdown, setting `Journey` + `Journey Order` on the Notion page, and explicitly stating which existing ticket it sits before/after and why.
5. **Removing a ticket** requires explicit sign-off and a Change Log entry in §7.

---

## 🧭 Journey Map

```
Phase A — Alpha Software Gate (Flat Sat)   (Weeks 1–5)   Tickets 1–11
Phase B — Ingest & Compliance Runway       (Weeks 5–10)  Tickets 12–22
Phase C — Environmental & RF Validation    (Weeks 10–14) Tickets 23–25
Phase D — HAB Hardware & Launch Ops        (Weeks 12–20) Tickets 26–29
Phase E — Flight & Post-Flight             (Weeks 20–24) Tickets 30–31
```

Overlap between Phase B, C, D is intentional — CAA paperwork and long-lead hardware procurement run in parallel with bench work.

**Flat-sat gate = end of Phase A (ticket #11).** Solo dev realistic estimate: **4–5 weeks** for Phase A (18.5–22.5 dev-days), assuming no unknown unknowns in Solidity or RadioLib.

---

## 📋 Ordered Ticket List (31 tickets)

> Legend: **Status** = current Notion status. **Priority** = Notion priority. "Parallel-ok" = may start before prior ticket lands, without risking rework.

### Phase A — Alpha Software Gate / Flat Sat (Weeks 1–5)

Goal: `scripts/demo_alpha.sh` → two Heltec boards + ground bouncer + Go gateway + local Anvil chain complete a full A→B→Heartbeat→Gateway→Contract settlement loop, **10/10 passes on a desk** plus 1/1 tamper rejection. **This is the flat-sat dry-HAB milestone.** No RF range, no TinyGS, no testnet.

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| ~~1~~ | ~~[VOID-127](https://www.notion.so/344d64b77e4081d99ba3dac7d406d283)~~ | ~~Alpha Plaintext Build Flag (skip ChaCha20)~~ | ~~Done~~ | ~~P0~~ | ~~Foundation. Must land first.~~ |
| ~~2~~ | ~~[VOID-120](https://www.notion.so/342d64b77e40818faf9ff12dc913f07e)~~ | ~~Fix SNLP Header File Offset Comments~~ | ~~Done~~ | ~~P1~~ | ~~**KSY hygiene — included.** Comments in `void_packets_snlp.h` still reference CCSDS (6B) header offsets. Anyone writing signature code next reads these and gets it wrong. Trivial fix, huge safety value for #5/#9/#10.~~ |
| ~~3~~ | ~~[VOID-132](https://www.notion.so/344d64b77e40811b89ccf588d6a27a41)~~ | ~~Synthetic GPS Stub for Alpha Demo~~ | ~~Done~~ | ~~P1~~ | ~~Parallel-ok with #1, #2. Unblocks #5 without waiting on real u-blox (#16).~~ |
| 4 | [VOID-050](https://www.notion.so/33fd64b77e4081658b86ebbb21ca7294) | Contract Scaffold | Not started | P0 | Parallel-ok with all firmware work. Local Hardhat/Foundry project. No blockchain dependency. |
| 5 | [VOID-128](https://www.notion.so/344d64b77e408184a9dbd4bba2e192d9) | Buyer Packet B TX Pipeline (firmware, plaintext) | Not started | P0 | Requires #1, #2, #3. Closes #8 on completion. Biggest single bottleneck in Phase A (~4–5 dev-days). |
| 6 | [VOID-051](https://www.notion.so/33fd64b77e408136be97fec120fcac01) | Minimal Escrow Function | Not started | P0 | Requires #4. **Flat-sat scope: single-transaction `settle_batch` on a local chain (Anvil/Hardhat) only.** No testnet deployment for alpha — that moves to Phase B. Unit-tested with Foundry/Hardhat locally. |
| 7 | [VOID-022](https://www.notion.so/33fd64b77e4081cc883ce96536eb6340) | Heartbeat Generation & Transmission | In progress | P0 | Firmware emits heartbeat every N seconds. **Alpha flies with UNAUTHENTICATED heartbeat** (VOID-118 deferred) — explicit, documented choice for telemetry-only use. |
| 8 | [VOID-033](https://www.notion.so/33fd64b77e4081e1bb3bd7a014c4cff2) | Packet B Transmission & Capture | In progress | P0 | Verification gate for #5. Captures a real `.bin` and diffs against golden vector. |
| 9 | [VOID-052](https://www.notion.so/33fd64b77e4081a3a570f7effd075c17) | Gateway → Contract Integration | Not started | P0 | Requires #4, #6. Go gateway (`web3.go` / `go-ethereum`) submits verified PacketB to the **local Anvil chain** for flat sat. Testnet (Sepolia/Base) deploy deferred to Phase B alongside #15. |
| 10 | [VOID-129](https://www.notion.so/344d64b77e40814ba8a9eaf9f58daf4f) | Wire Ground Bouncer → Gateway Signature Verify | Not started | P0 | Requires #5. **Full verification — not stubbed, not bypassed.** The bouncer MUST perform real Ed25519 verification over the VOID-111 signature scope (`header + body[0..105]`) before forwarding to the gateway. A failed verify = a dropped packet, logged. |
| 11 | [VOID-130](https://www.notion.so/344d64b77e4081889dece11360229b97) | Alpha End-to-End Bench Demo — Flat Sat (plaintext SNLP) | Not started | P0 | **🎯 FLAT-SAT / ALPHA-DONE GATE.** Real Heltec × 2 + ground bouncer + gateway + local Anvil chain, 10 consecutive passes + tamper-reject. This is the desk-top dry HAB. Nothing in Phase B starts until this is green. |

### Phase B — Ingest & Compliance Runway (Weeks 5–10)

Two workstreams run side-by-side: **ingest plumbing + testnet promotion** (12–15) and **RF/regulatory** (16–22). CAA NOTAM (#22) has a 28-day statutory lead — **file on day 1 of Phase B.**

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 12 | [VOID-133](https://www.notion.so/344d64b77e4081b7ad23d84758db6f6f) | HTTP→MQTT Bridge OR Direct TinyGS HTTP Ingest | Not started | P1 | Day-1 spike → pick path → implement. |
| 13 | [VOID-040](https://www.notion.so/33fd64b77e4081468b3ee5d320b2fcf8) | MQTT Client & SNLP Detection | Not started | P0 | Closes once #12 merges (likely absorbed). |
| 14 | [VOID-023](https://www.notion.so/33fd64b77e4081cdabbafbfc07411048) | TinyGS Reception Test | Not started | P0 | Requires #12. Confirms community ground station reports our sync word. |
| 15 | [VOID-053](https://www.notion.so/33fd64b77e408162942ef84d29934153) | End-to-End Settlement Test — TinyGS Path (Capstone) | Not started | P0 | Requires #12–14. **Rescoped from "software loopback" → real full-pipeline test:** Heltec TX → TinyGS ground → MQTT → gateway → contract settles → receipt. First point on the journey where the contract runs on a **testnet** (Sepolia / Base Sepolia), promoted from the local Anvil used at #11. No simulation/stub harnesses — flat sat (#11) already proved the software loop; this ticket proves the real radio + community ingest + live chain chain. |
| 16 | [VOID-011](https://www.notion.so/33fd64b77e4081dcac7bd83e79fb9433) | GPS Module Integration (real u-blox) | Not started | P0 | Parallel-ok with #12–15. Replaces synthetic stub (#3) for flight. |
| 17 | [VOID-034](https://www.notion.so/33fd64b77e4081d592ccc1aec924c44b) | NMEA to ECEF Conversion | Not started | P1 | Blocked by #16. Same PR train. |
| 18 | [VOID-012](https://www.notion.so/342d64b77e40811ba526c97f1469a7b1) | Frequency & Power Compliance Check | Not started | P0 | Paperwork + bench measurement. Start when #11 is green. |
| 19 | [VOID-070](https://www.notion.so/33fd64b77e408102b22ff583a9443fa7) | Airtime Counter | Not started | P0 | Enforces ≤1% duty. Blocks #25 (24 h soak). |
| 20 | [VOID-072](https://www.notion.so/33fd64b77e40811abde6d58ab1b114ff) | Tx Power Compliance | Not started | P1 | Spectrum analyser / calibrated SDR. Feeds #18. |
| 21 | [VOID-131](https://www.notion.so/344d64b77e4081eaa220c2b78d50024c) | HAB Alpha Flight Config & Power Budget | Not started | P1 | Consolidates #18–20 into `flight_alpha` PlatformIO env + doc. |
| 22 | [VOID-090](https://www.notion.so/33fd64b77e4081e1812ae391b7fbbf15) | CAA NOTAM Application | Not started | P0 | **File day 1 of Phase B. Do not delay.** 28-day minimum lead. |

### Phase C — Environmental & RF Validation (Weeks 10–14)

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 23 | [VOID-080](https://www.notion.so/33fd64b77e408193 89fdd60e95390819) | Cold Soak Test (–30 °C × 2 h) | Not started | P1 | Feeds #26 enclosure design. |
| 24 | [VOID-081](https://www.notion.so/33fd64b77e4081f797d4fc19766bb1bc) | Range Test (20 km LoS) | Not started | P0 | Budget a second attempt. |
| 25 | [VOID-071](https://www.notion.so/33fd64b77e4081bb927ddde4cdfe8400) | Duty Cycle Soak Test (24 h) | Not started | P0 | Requires #19. |

### Phase D — HAB Hardware & Launch Ops (Weeks 12–20)

Parallel-ok with Phase C. Procure long-lead items (balloon envelope, helium, parachute) week 12.

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 26 | [VOID-091](https://www.notion.so/33fd64b77e4081f59c42c96bafe0c2cf) | Payload Enclosure Design | Not started | P1 | EPS, <500 g. Informed by #23 thermal data. |
| 27 | [VOID-092](https://www.notion.so/33fd64b77e4081ecbf6ffeeae6e74012) | Recovery System | Not started | P1 | Secondary GPS beacon, parachute, cutdown, flight prediction. |
| 28 | [VOID-093](https://www.notion.so/33fd64b77e4081089a6dca41ae9ca768) | Launch Rehearsal (tethered / short free flight) | Not started | P2 | Shakedown. Requires #26, #27. |
| 29 | [VOID-094](https://www.notion.so/33fd64b77e4081eda288e4b0ff62b9f2) | Flight Day Checklist | Not started | P1 | Written deliverable. |

### Phase E — Flight & Post-Flight (Weeks 20–24)

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 30 | [VOID-082](https://www.notion.so/33fd64b77e4081a18ea5c1868ae38e93) | Full Dress Rehearsal (3 h battery sim) | Not started | P0 | **Final go/no-go gate.** Every prior ticket Done. |
| 31 | [VOID-101](https://www.notion.so/33fd64b77e4081ffa54afa33a6633a56) | Flight Report (populated post-flight) | Not started | P2 | **Closes the journey.** |

---

## 🧱 Hard Dependency Graph

```
Phase A (flat sat):
  #1 VOID-127 ─┬─► #2 VOID-120
               ├─► #3 VOID-132 ──┐
               └─► #4 VOID-050 ──┼──► #6 VOID-051 (local Anvil) ──┐
                                 │                                │
                                 └──► #5 VOID-128 ──┬──► #7 VOID-022
                                                    │       │
                                                    │       └──► #8 VOID-033
                                                    │                │
                                                    └────────────────┴──► #10 VOID-129 (full verify)
                                                                                │
                                      #6 ──► #9 VOID-052 (local chain) ─────────┤
                                                                                │
                                                                                └──► #11 VOID-130
                                                                                     🎯 FLAT-SAT GATE

Phase B launches on #11 green:
  #11 ─┬─► #12 VOID-133 ─► #13 VOID-040 ─► #14 VOID-023 ─► #15 VOID-053 (testnet capstone)
       ├─► #16 VOID-011 ─► #17 VOID-034
       ├─► #18 VOID-012 ─► #20 VOID-072 ─┐
       ├─► #19 VOID-070 ────────────────┬├─► #21 VOID-131
       └─► #22 VOID-090  (file day 1)   ─┘

Phase C:
  #19 ─► #25 VOID-071
  #21 ─► #24 VOID-081
  (#23 VOID-080 can start any time after #21)

Phase D:
  #23 ─► #26 VOID-091 ─┐
  Procurement ────────►│
                       ├─► #28 VOID-093 ─► #29 VOID-094
  #26 ─► #27 VOID-092 ─┘

Phase E:
  ALL of Phase A–D ─► #30 VOID-082 ─► (launch) ─► #31 VOID-101 ─► ✅ journey-to-hab complete
```

---

## 🔬 KSY Hygiene Examination (why 1 of 6 made the cut)

Evaluated on 2026-04-16. The question for each: **"Does this ticket touch a packet actually flying in alpha — or does it only clean up something on the wire we won't transmit?"**

| Ticket | Affects packets on alpha wire? | Wire-format change? | Decision | Rationale |
|---|---|---|---|---|
| **VOID-115** — Align PacketB/TunnelData signatures to 8-byte | **PacketB: YES.** TunnelData: no (not on alpha wire) | **Yes** — shifts offsets, breaks golden vectors, re-runs VOID-111 scope | **DEFER** | Current layout works correctly on ESP32-S3 (Xtensa LX7 permits unaligned access) and x86-64 gateway. This is a perf/portability optimisation, not a correctness bug. Retuning the wire now costs more than it saves. Re-evaluate for beta with v2.2 wire format. |
| **VOID-116** — Align relay_ops u32 in PacketAck | **No** — PacketAck not on alpha wire | Yes | **DEFER** | Alpha flow = A → B → Heartbeat → Gateway → Contract. No in-band ACK on the RF link. |
| **VOID-119** — Align PacketD global_crc | **No** — PacketD (Delivery) not on alpha wire | Yes | **DEFER** | Delivery packet unused in alpha transaction. |
| **VOID-120** — Fix SNLP offset comments | **All SNLP packets** (docs only, not wire) | **No — comments only** | **INCLUDE as #2** | Comments in `void_packets_snlp.h` still say `00-05` (CCSDS 6-byte header) but SNLP header is 14 bytes. Anyone writing signature code for VOID-128/129/#10 reads these and derives wrong offsets. Trivial edit, high protection against a catastrophic signature-scope bug during the rest of Phase A. |
| **VOID-121** — Segregate demo keys | Build hygiene | No | **DEFER** | Alpha is explicitly a demo build — keys belong in the source tree for now. Mandatory before any non-alpha build. |
| **VOID-122** — CRC pre-validation before D/ACK dispatch | **No** — both D and ACK absent from alpha wire | No | **DEFER** | PacketB already CRC-gated via VOID-112 (Done). The D-vs-ACK disambiguation bug only fires when both are transmitted. |

**Principle used:** the hygiene ticket earns a seat on the journey if and only if it either (a) changes code a Phase-A engineer will touch, or (b) protects a packet we actually fly in alpha. VOID-120 passes both tests; the others are beta-journey material.

---

## 🚫 Explicit Non-Goals (do not re-open during this journey)

| Item | Ticket(s) | Why deferred |
|---|---|---|
| ChaCha20 payload encryption | VOID-110 re-enable | Alpha flies plaintext. VOID-127 flag stays on. |
| Heartbeat crypto authentication | VOID-118 | Plaintext, unauthenticated heartbeat is intentional for telemetry — the balloon is ours, nobody else is broadcasting signed heartbeats on our band during the flight window. |
| Nonce / replay rejection | VOID-043 | One-shot 2-hour flight. Replay window is a non-issue. |
| Simulation / stub harnesses | — | Explicit scope call: once software works (Phase A), we move to hardware. No effort spent building virtual devices or software loopbacks to imitate Heltecs. |
| Testnet contract deployment for flat sat | (part of VOID-051/052) | Flat sat uses a local Anvil chain for control + speed. Testnet promotion happens at #15 (VOID-053), paired with TinyGS ingest — no value in promoting the contract before the community ingest path exists. |
| KSY alignment hygiene (wire-format churn) | VOID-115, VOID-116, VOID-119, VOID-122 | See §KSY Hygiene — none touch packets on the alpha wire, or the wire-format cost outweighs the benefit for one flight. |
| Production key segregation | VOID-121 | Demo build permits demo keys. Mandatory before beta. |
| Pre-Alpha README | VOID-100 (already Done) | No further docs work required to launch. |

---

## 🗃️ Notion View — "Journey to HAB"

Recommended saved Board view in the Tickets database:

- **Filter:** `Journey` is `journey-to-hab`
- **Sort:** `Journey Order` ascending
- **Group by:** `Status`

This view **is** the sprint board for the next 6 months. Everything else is noise.

---

## 📎 Source Documents

- Full project audit: [VOID_PROJECT_AUDIT_2026-04-16.md](VOID_PROJECT_AUDIT_2026-04-16.md)
- Root project instructions: [../CLAUDE.md](../CLAUDE.md)
- Canonical wire schema: [kaitai_struct/](kaitai_struct/)
- KSY security/alignment audit (source of VOID-115–VOID-122): [VOID_KSY_SECURITY_ALIGNMENT_AUDIT_2026-04-14.md](VOID_KSY_SECURITY_ALIGNMENT_AUDIT_2026-04-14.md)
- Notion database: https://www.notion.so/33fd64b77e4080849c56d65bd7683577

---

## 📝 Change Log

| Date | Change | Reason |
|---|---|---|
| 2026-04-16 | v1 — Journey created, 26 tickets tagged. | Initial lock after project audit. |
| 2026-04-16 | v2 — Added 5 tickets (VOID-050, VOID-051, VOID-052, VOID-053, VOID-120). Renumbered all downstream. | Smart-contract settlement is part of the alpha payment flow — a balloon demo that shows radio-only without money moving would not sell. Added VOID-120 (SNLP offset comments) after KSY hygiene review: it's the only hygiene ticket that protects code Phase A will actively touch. |
| 2026-04-16 | v3 — **Flat-sat scope lock.** Phase A shrinks 12 → 11; Phase B grows 10 → 11. VOID-053 moved Phase A #11 → Phase B #15 and rescoped from "software loopback" → full TinyGS end-to-end settlement capstone (this ticket was mis-scoped in v2 — a real-RF test belongs after MQTT ingest exists, not before). VOID-051/052 scope-noted as **local Anvil chain only** for flat sat; testnet promotion happens at #15. VOID-129 explicitly confirmed as **full Ed25519 verify, not bypassed** — the bouncer is part of the real security story, not a pass-through. No simulation/stub harnesses on the path — software-working → hardware-testing, no virtual-device layer in between. Phase A renumbered: VOID-130 ALPHA-DONE GATE moves from #12 → #11. | Three scope clarifications from the user: (1) flat sat ≈ desk-top dry HAB using a local chain for determinism and speed; (2) bouncer verification is real, not a mock; (3) no stubs — hardware is the test harness. VOID-053's original description in Notion ("Full pipeline test: Heltec Tx → TinyGS → MQTT → gateway → contract settles") confirms it always belonged in Phase B; v2 placed it incorrectly. |
