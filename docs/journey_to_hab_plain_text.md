# Journey to HAB — Plaintext Alpha (LOCKED PATH)

- **Locked:** 2026-04-16
- **Last revised:** 2026-04-16 (v4 — full commerce loop: ACK + Packet C + Packet D on the flat-sat wire, `receipts.json` persistence, TRL 4 evidence target; 3 KSY hygiene flips back on path + 3 new tickets)
- **Goal:** One end-to-end VOID transaction — **Invoice → Payment → ACK → on-chain Settlement → Receipt → Delivery** — in **plaintext SNLP**, executed on a high-altitude balloon within **6 months** (target launch window: **October 2026**). Heartbeat telemetry runs throughout for flight tracking.
- **Flat-sat milestone (dry HAB, desk-top):** End of Phase A (ticket #17, VOID-130). Two Heltec boards + ground bouncer + gateway + local Anvil chain complete a full A→B→ACK→Settle→C→D commerce loop, 10/10 consecutive passes. All settlement events persisted to `receipts.json` and surviving a gateway restart. This is the TRL 4 evidence pack for seed funding.
- **Out of scope for this journey:** ChaCha20 payload encryption, heartbeat crypto auth, nonce/replay tracking, production key management, simulation/stub harnesses, testnet smart-contract deployment, and the KSY hygiene tickets (VOID-115, VOID-121) that only touch packets not flown in alpha. These re-enter scope for the post-HAB beta journey — do **NOT** smuggle them in here.
- **Authority:** This document + the Notion `Journey = journey-to-hab` tag are the single source of truth for the critical path. If a ticket is not listed here, it is **not** on the path.

---

## 🔒 Rules of the Locked Path

1. **No divergence.** If a future chat / PR / idea is not one of the 37 tickets below, it does not belong on this journey. Park it for the post-HAB beta journey.
2. **Order matters.** The `Journey Order` number in Notion is the execution sequence. Do not start ticket `N` until ticket `N-1` is Done, **unless** explicitly flagged "can run in parallel" below.
3. **Mark progress in this file.** When a ticket is Done, wrap the row's `# / Ticket / Title` cells in `~~…~~` strikethrough and update Notion status in the same commit. The markdown is the at-a-glance progress board.
4. **Closing the journey** = ticket #37 (VOID-101 Flight Report) is Done after a successful HAB flight has proven ticket #17 (flat-sat alpha demo) works from the sky. Until then, this journey is open.
5. **Adding a ticket** requires: updating this markdown, setting `Journey` + `Journey Order` on the Notion page, and explicitly stating which existing ticket it sits before/after and why.
6. **Removing a ticket** requires explicit sign-off and a Change Log entry in §Change Log.

---

## 🧭 Journey Map

```
Phase A — Alpha Software Gate (Flat Sat, full loop)  (Weeks 1–7)   Tickets 1–17
Phase B — Ingest & Compliance Runway                 (Weeks 7–12)  Tickets 18–28
Phase C — Environmental & RF Validation              (Weeks 12–16) Tickets 29–31
Phase D — HAB Hardware & Launch Ops                  (Weeks 14–22) Tickets 32–35
Phase E — Flight & Post-Flight                       (Weeks 22–26) Tickets 36–37
```

Overlap between Phase B, C, D is intentional — CAA paperwork and long-lead hardware procurement run in parallel with bench work.

**Flat-sat gate = end of Phase A (ticket #17).** Solo dev realistic estimate: **6–7 weeks** for Phase A (25–31 dev-days). TRL 4 evidence complete at this point: real hardware + real protocol + real settlement + persisted receipts.

---

## 🔁 Flat-Sat Wire (Phase A Closes With This Loop)

```
Seller    ──A (Invoice)──►         Buyer
Buyer     ──B (Payment, signed)──► Ground Bouncer
Ground    ──ACK (B received, sig-verified)──► Buyer
Ground    ──(HTTP)──►              Gateway
Gateway   ──(web3.go)──►           Local Anvil contract  ── settles
Gateway   ── writes ──►            receipts.json (append-only, survives restart)
Gateway   ──C (Receipt, signed)──► Ground Bouncer ──► Seller
Seller    ──D (Delivery)──►        Ground Bouncer ──► Buyer
                  (Heartbeat Packet L emitted by both Heltecs throughout)
```

Six packet types on the alpha wire: **A, B, ACK, C, D, Heartbeat.** All plaintext SNLP (14-byte header).

---

## 📋 Ordered Ticket List (37 tickets)

> Legend: **Status** = current Notion status. **Priority** = Notion priority. "Parallel-ok" = may start before prior ticket lands, without risking rework.

### Phase A — Alpha Software Gate / Flat Sat (Weeks 1–7)

Goal: `scripts/demo_alpha.sh` → two Heltecs + ground bouncer + Go gateway + local Anvil chain execute the full **A→B→ACK→Settle→C→D** loop, **10/10 passes on a desk** plus 1/1 tamper rejection. All settlements persisted to `gateway/data/receipts.json`, re-loaded on gateway restart with no double-settle. **This is the flat-sat dry-HAB + TRL 4 milestone.**

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| ~~1~~ | ~~[VOID-127](https://www.notion.so/344d64b77e4081d99ba3dac7d406d283)~~ | ~~Alpha Plaintext Build Flag (skip ChaCha20)~~ | ~~Done~~ | ~~P0~~ | ~~Foundation. Must land first.~~ |
| ~~2~~ | ~~[VOID-120](https://www.notion.so/342d64b77e40818faf9ff12dc913f07e)~~ | ~~Fix SNLP Header File Offset Comments~~ | ~~Done~~ | ~~P1~~ | ~~**KSY hygiene — included.** Comments in `void_packets_snlp.h` still reference CCSDS (6B) header offsets. Anyone writing signature code next reads these and gets it wrong. Trivial fix, huge safety value for #5/#9/#10.~~ |
| ~~3~~ | ~~[VOID-132](https://www.notion.so/344d64b77e40811b89ccf588d6a27a41)~~ | ~~Synthetic GPS Stub for Alpha Demo~~ | ~~Done~~ | ~~P1~~ | ~~Parallel-ok with #1, #2. Unblocks #5 without waiting on real u-blox (#16).~~ |
| ~~4~~ | ~~[VOID-050](https://www.notion.so/33fd64b77e4081658b86ebbb21ca7294)~~ | ~~Contract Scaffold~~ | ~~Done~~ | ~~P0~~ | ~~Foundry project at `contracts/` (user-locked Foundry over Hardhat). Local Anvil only for flat-sat.~~ |
| ~~5~~ | ~~[VOID-128](https://www.notion.so/344d64b77e408184a9dbd4bba2e192d9)~~ | ~~Buyer Packet B TX Pipeline (firmware, plaintext)~~ | ~~Done~~ | ~~P0~~ | ~~Requires #1, #2, #3. Closes #8 on completion. Biggest single bottleneck in Phase A (~4–5 dev-days).~~ |
| ~~6~~ | ~~[VOID-051](https://www.notion.so/33fd64b77e408136be97fec120fcac01)~~ | ~~Minimal Escrow Function~~ | ~~Done~~ | ~~P0~~ | ~~`contracts/src/Escrow.sol` with `settleBatch` (max 10, USDC-only, duplicate-nonce guard, PENDING status + SettlementCreated event). 6/6 Foundry tests + live Anvil `cast send` verified. Go RPC wiring lands in #14.~~ |
| 7 | [VOID-022](https://www.notion.so/33fd64b77e4081cc883ce96536eb6340) | Heartbeat Generation & Transmission | In progress | P0 | Firmware emits heartbeat every N seconds. Alpha flies with UNAUTHENTICATED heartbeat (VOID-118 deferred) — explicit choice for telemetry-only use. |
| 8 | [VOID-033](https://www.notion.so/33fd64b77e4081e1bb3bd7a014c4cff2) | Packet B Transmission & Capture | In progress | P0 | Verification gate for #5. Captures a real `.bin` and diffs against golden vector. |
| 9 | [VOID-116](https://www.notion.so/342d64b77e4081f1840af2b05c401807) | Align RelayOps u32 Fields in PacketAck | ⏸ Deferred (post-flat-sat) | P2 | **Deferred 2026-04-19 (see Change Log v5).** Cosmetic alignment of u32 fields inside a packed struct — zero runtime cost on ESP32-S3 (compiler emits byte-by-byte access; LoRa 1 kbps masks any cycle difference). Fix would shuffle existing pad bytes; packet size and wire bit-flip surface unchanged. Does **NOT** block #13 (ACK emission). |
| 10 | [VOID-119](https://www.notion.so/342d64b77e4081beb7adc427af016de9) | Align PacketD global_crc | ⏸ Deferred (post-flat-sat) | P2 | **Deferred 2026-04-19 (see Change Log v5).** Same reasoning as #9 — `global_crc` misalignment is handled transparently by packed-struct byte access. Does **NOT** block #16 (D emission). |
| ~~11~~ | ~~[VOID-122](https://www.notion.so/342d64b77e408150bc66c313cdbcc0a5)~~ | ~~CRC Pre-Validation Before D/ACK Dispatch~~ | ~~Done~~ | ~~P0~~ | ~~Gateway `handlePayloadBody` now verifies global CRC32 for PacketD and PacketAck (both tiers) after F-03 magic byte, before trusting any body field. Helper `void_protocol.ValidateFrameCRC32` + constants `Packet{D,Ack}CrcOffsetFromEnd`. 14/14 table-driven tests: valid paths 200, CRC-field / in-scope body flips 400, PacketD tail (out-of-scope) carve-out 200. Specs updated.~~ |
| ~~12~~ | ~~[VOID-129](https://www.notion.so/344d64b77e40814ba8a9eaf9f58daf4f)~~ | ~~Wire Ground Bouncer → Gateway Signature Verify~~ | ~~Done~~ | ~~P0~~ | ~~Requires #5. Full Ed25519 verify over VOID-111 scope — 400 on failure, structured log `event=packetb.sig_fail`. Table-driven tests cover both tiers + sig-scope boundary.~~ |
| ~~13~~ | ~~[VOID-134](https://www.notion.so/344d64b77e4081c399b2ecc9c1c2df62)~~ | ~~Ground Bouncer → Buyer ACK Emission (firmware)~~ | ~~Done~~ | ~~P0~~ | ~~**LANDED (v10).** Pure-function `ack_builder::build(AckInputs, out, 136)` — byte-exact match to `test/vectors/snlp/packet_ack.bin` (5 red-green tests). Wired into `main_new.cpp` PACKET_B handler: on `process_packet` success, extracts `PacketB.sat_id`, builds PacketAck, emits over serial as `PACKET_ACK_TX:<hex>\n` for firmware-side LoRa TX. Fire-and-forget, no retry (alpha). 41/41 ground-station ctest green, production `-Werror` clean.~~ |
| ~~14~~ | ~~[VOID-052](https://www.notion.so/33fd64b77e4081a3a570f7effd075c17)~~ | ~~Gateway → Contract Integration~~ | ~~Done~~ | ~~P0~~ | ~~`gateway/internal/core/chain/` EscrowClient + BufferedSubmitter (10 or 5 s, retry-once). Ingest handler enqueues intent post-sig-verify, wallet looked up from registry. Server wires via env vars (`VOID_ESCROW_ADDRESS` / `VOID_ETH_RPC_URL` / `VOID_GATEWAY_PRIVATE_KEY`). Tests: 3 simulated.Backend integration + 7 submitter/nonce unit + 3 ingest-bridge. VOID-051 amended (Change Log v6) to include `wallet` in SettlementIntent so the on-chain record is self-contained TRL-4 evidence.~~ |
| ~~15~~ | ~~[VOID-135](https://www.notion.so/344d64b77e40817f883ce2f70ed7fe76) + [VOID-138](https://www.notion.so/VOID-138-Bouncer-C-Egress-Poll-Client-347d64b77e40813386e0e5dcfc2bacc5)~~ | ~~Gateway Packet C Receipt + `receipts.json` + bouncer egress poll~~ | ~~Done~~ | ~~P0~~ | ~~Three-PR close (Change Log v7/v8/v9). **#15a:** in-process event watcher + PacketC builder + JSONL store. **#15b-gateway:** HTTP egress endpoints (GET /pending, POST /ack), PENDING/DISPATCHED state machine, crash-safety across restart proven by 27 Go tests. **#15c = VOID-138:** C++ bouncer poll client — GoogleTest bootstrapped in `ground-station/`, hand-rolled bounded JSON scanner, hex decoder, raw-socket HTTP client, template-based static-dispatch orchestrator (no virtuals, no heap), poll thread wired into main_new.cpp. 36 new C++ tests. Produces the TRL 4 artefact (`receipts.json`).~~ |
| ~~16~~ | ~~[VOID-136](https://www.notion.so/344d64b77e40818ba490c2d7cbd637a4)~~ | ~~Seller Packet D Delivery Emission (firmware)~~ | ~~Done~~ | ~~P0~~ | ~~**LANDED (v11).** Pure-function `packet_d_builder::build(DeliveryInputs, out, 136)` in `void-core/` — byte-exact match to `test/vectors/snlp/packet_d.bin` (6 red-green tests). Wired into `satellite-firmware/src/seller.cpp`: on SNLP `PacketC` RX (APID = SELLER_APID, len = SIZE_PACKET_C), CRC-verify over scope `[0..103]`, copy the 98 body bytes into `PacketD.payload`, build + `radio.transmit` the 136 B frame. Legacy TunnelData branch preserved as parallel path (removed in a follow-up). 60/60 void-core ctest green across both SNLP and CCSDS targets.~~ |
| 17 | [VOID-130](https://www.notion.so/344d64b77e4081889dece11360229b97) | Alpha End-to-End Bench Demo — Flat Sat (full A→B→ACK→Settle→C→D loop) | Not started | P0 | **🎯 FLAT-SAT / ALPHA-DONE GATE / TRL 4.** Real Heltec × 2 + ground bouncer + gateway + local Anvil, full commerce loop, 10 consecutive passes + 1 tamper-reject, `receipts.json` shows 10 settled events, gateway restart mid-run produces 0 double-settles. Nothing in Phase B starts until this is green. |

### Phase B — Ingest & Compliance Runway (Weeks 7–12)

Two workstreams run side-by-side: **ingest plumbing + testnet promotion** (18–21) and **RF/regulatory** (22–28). CAA NOTAM (#28) has a 28-day statutory lead — **file on day 1 of Phase B.**

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 18 | [VOID-133](https://www.notion.so/344d64b77e4081b7ad23d84758db6f6f) | HTTP→MQTT Bridge OR Direct TinyGS HTTP Ingest | Not started | P1 | Day-1 spike → pick path → implement. |
| 19 | [VOID-040](https://www.notion.so/33fd64b77e4081468b3ee5d320b2fcf8) | MQTT Client & SNLP Detection | Not started | P0 | Closes once #18 merges (likely absorbed). |
| 20 | [VOID-023](https://www.notion.so/33fd64b77e4081cdabbafbfc07411048) | TinyGS Reception Test | Not started | P0 | Requires #18. Confirms community ground station reports our sync word. |
| 21 | [VOID-053](https://www.notion.so/33fd64b77e408162942ef84d29934153) | End-to-End Settlement Test — TinyGS Path + Testnet Promotion (Capstone) | Not started | P0 | Requires #18–20. Full pipeline: Heltec TX → TinyGS → MQTT → gateway → testnet contract settles → receipt → delivery. **This is where the contract moves from local Anvil to a testnet (Sepolia / Base Sepolia).** |
| 22 | [VOID-011](https://www.notion.so/33fd64b77e4081dcac7bd83e79fb9433) | GPS Module Integration (real u-blox) | Not started | P0 | Parallel-ok with #18–21. Replaces synthetic stub (#3) for flight. |
| 23 | [VOID-034](https://www.notion.so/33fd64b77e4081d592ccc1aec924c44b) | NMEA to ECEF Conversion | Not started | P1 | Blocked by #22. Same PR train. |
| 24 | [VOID-012](https://www.notion.so/342d64b77e40811ba526c97f1469a7b1) | Frequency & Power Compliance Check | Not started | P0 | Paperwork + bench measurement. Start when #17 is green. |
| 25 | [VOID-070](https://www.notion.so/33fd64b77e408102b22ff583a9443fa7) | Airtime Counter | Not started | P0 | Enforces ≤1% duty. Blocks #31 (24 h soak). |
| 26 | [VOID-072](https://www.notion.so/33fd64b77e40811abde6d58ab1b114ff) | Tx Power Compliance | Not started | P1 | Spectrum analyser / calibrated SDR. Feeds #24. |
| 27 | [VOID-131](https://www.notion.so/344d64b77e4081eaa220c2b78d50024c) | HAB Alpha Flight Config & Power Budget | Not started | P1 | Consolidates #24–26 into `flight_alpha` PlatformIO env + doc. |
| 28 | [VOID-090](https://www.notion.so/33fd64b77e4081e1812ae391b7fbbf15) | CAA NOTAM Application | Not started | P0 | **File day 1 of Phase B. Do not delay.** 28-day minimum lead. |

### Phase C — Environmental & RF Validation (Weeks 12–16)

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 29 | [VOID-080](https://www.notion.so/33fd64b77e40819389fdd60e95390819) | Cold Soak Test (–30 °C × 2 h) | Not started | P1 | Feeds #32 enclosure design. |
| 30 | [VOID-081](https://www.notion.so/33fd64b77e4081f797d4fc19766bb1bc) | Range Test (20 km LoS) | Not started | P0 | Budget a second attempt. |
| 31 | [VOID-071](https://www.notion.so/33fd64b77e4081bb927ddde4cdfe8400) | Duty Cycle Soak Test (24 h) | Not started | P0 | Requires #25. |

### Phase D — HAB Hardware & Launch Ops (Weeks 14–22)

Parallel-ok with Phase C. Procure long-lead items (balloon envelope, helium, parachute) week 14.

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 32 | [VOID-091](https://www.notion.so/33fd64b77e4081f59c42c96bafe0c2cf) | Payload Enclosure Design | Not started | P1 | EPS, <500 g. Informed by #29 thermal data. |
| 33 | [VOID-092](https://www.notion.so/33fd64b77e4081ecbf6ffeeae6e74012) | Recovery System | Not started | P1 | Secondary GPS beacon, parachute, cutdown, flight prediction. |
| 34 | [VOID-093](https://www.notion.so/33fd64b77e4081089a6dca41ae9ca768) | Launch Rehearsal (tethered / short free flight) | Not started | P2 | Shakedown. Requires #32, #33. |
| 35 | [VOID-094](https://www.notion.so/33fd64b77e4081eda288e4b0ff62b9f2) | Flight Day Checklist | Not started | P1 | Written deliverable. |

### Phase E — Flight & Post-Flight (Weeks 22–26)

| # | Ticket | Title | Status | Priority | Notes |
|---|---|---|---|---|---|
| 36 | [VOID-082](https://www.notion.so/33fd64b77e4081a18ea5c1868ae38e93) | Full Dress Rehearsal (3 h battery sim) | Not started | P0 | **Final go/no-go gate.** Every prior ticket Done. |
| 37 | [VOID-101](https://www.notion.so/33fd64b77e4081ffa54afa33a6633a56) | Flight Report (populated post-flight) | Not started | P2 | **Closes the journey.** |

---

## 🧱 Hard Dependency Graph

```
Phase A (flat sat, full loop):
  #1 VOID-127 ─┬─► #2 VOID-120
               ├─► #3 VOID-132 ──┐
               └─► #4 VOID-050 ──┼──► #6 VOID-051 (local Anvil) ──┐
                                 │                                │
                                 └──► #5 VOID-128 ──┬──► #7 VOID-022
                                                    │       │
                                                    │       └──► #8 VOID-033
                                                    │                │
                                                    │                ├──► #9 VOID-116 ──┐
                                                    │                ├──► #10 VOID-119 ─┤
                                                    │                └──► #11 VOID-122 ─┤
                                                    │                                   │
                                                    └──► #12 VOID-129 (full verify) ────┤
                                                                                        │
                                    #9 + #11 + #12 ─► #13 VOID-134 (ACK emit) ──────────┤
                                                                                        │
                                    #6 ──► #14 VOID-052 ──► #15 VOID-135 (C + JSON) ────┤
                                                                                        │
                                    #10 + #11 + #15 ─► #16 VOID-136 (D emit) ───────────┤
                                                                                        │
                                                                                        ▼
                                                                                #17 VOID-130
                                                                              🎯 FLAT-SAT / TRL 4 GATE

Phase B launches on #17 green:
  #17 ─┬─► #18 VOID-133 ─► #19 VOID-040 ─► #20 VOID-023 ─► #21 VOID-053 (testnet promotion)
       ├─► #22 VOID-011 ─► #23 VOID-034
       ├─► #24 VOID-012 ─► #26 VOID-072 ─┐
       ├─► #25 VOID-070 ─────────────────┤► #27 VOID-131
       └─► #28 VOID-090  (file day 1)   ─┘

Phase C:
  #25 ─► #31 VOID-071
  #27 ─► #30 VOID-081
  (#29 VOID-080 can start any time after #27)

Phase D:
  #29 ─► #32 VOID-091 ─┐
  Procurement ────────►│
                       ├─► #34 VOID-093 ─► #35 VOID-094
  #32 ─► #33 VOID-092 ─┘

Phase E:
  ALL of Phase A–D ─► #36 VOID-082 ─► (launch) ─► #37 VOID-101 ─► ✅ journey-to-hab complete
```

---

## 🔬 KSY Hygiene Examination (3 of 6 on the path as of v4)

Evaluated for v4 flat-sat scope (A + B + ACK + C + D + Heartbeat on the wire). The question for each: **"Does this ticket touch a packet actually flying in alpha?"**

| Ticket | Affects packets on alpha wire? | Wire-format change? | Decision | Rationale |
|---|---|---|---|---|
| **VOID-115** — Align PacketB/TunnelData signatures to 8-byte | **PacketB: YES.** TunnelData: no | **Yes** — shifts offsets, breaks golden vectors, re-runs VOID-111 scope | **DEFER** | Current layout works correctly on ESP32-S3 (Xtensa LX7 permits unaligned access) and x86-64 gateway. Perf/portability optimisation, not a correctness bug. Retuning the wire now costs more than it saves. Re-evaluate for beta v2.2 wire. |
| **VOID-116** — Align relay_ops u32 in PacketAck | **YES — ACK now flown** | Yes | **INCLUDE as #9** | v4 brings ACK onto the wire (VOID-134). Alignment must be correctness-clean before emission. |
| **VOID-119** — Align PacketD global_crc | **YES — PacketD now flown** | Yes | **INCLUDE as #10** | v4 brings D onto the wire (VOID-136). |
| **VOID-120** — Fix SNLP offset comments | **All SNLP packets** (docs only, not wire) | **No — comments only** | **INCLUDE as #2** | Comment correctness protects every signature-scope ticket in Phase A. |
| **VOID-121** — Segregate demo keys | Build hygiene | No | **DEFER** | Alpha is explicitly a demo build. Mandatory before any non-alpha build. |
| **VOID-122** — CRC pre-validation before D/ACK dispatch | **YES — both D and ACK now flown** | No | **INCLUDE as #11** | Promoted from hygiene → **functional bug risk** in v4. With both ACK and D on the wire, the type-bit dispatch disambiguation must run after a validated CRC or the gateway silently routes the wrong handler. |

**Principle used:** the hygiene ticket earns a seat on the journey if and only if it either (a) changes code a Phase-A engineer will touch, or (b) protects a packet we actually fly in alpha. VOID-116/119/120/122 pass; VOID-115/121 don't.

---

## 🚫 Explicit Non-Goals (do not re-open during this journey)

| Item | Ticket(s) | Why deferred |
|---|---|---|
| ChaCha20 payload encryption | VOID-110 re-enable | Alpha flies plaintext. VOID-127 flag stays on. |
| Heartbeat crypto authentication | VOID-118 | Plaintext, unauthenticated heartbeat is intentional for telemetry — the balloon is ours, nobody else is broadcasting signed heartbeats on our band during the flight window. |
| Nonce / replay rejection | VOID-043 | One-shot 2-hour flight. Replay window is a non-issue. |
| Simulation / stub harnesses | — | Explicit scope call: once software works (Phase A), we move to hardware. No effort spent building virtual devices or software loopbacks to imitate Heltecs. |
| Testnet contract deployment for flat sat | (part of VOID-051/052) | Flat sat uses a local Anvil chain for control + speed. Testnet promotion happens at #21 (VOID-053), paired with TinyGS ingest. |
| KSY alignment hygiene (wire-format churn) | VOID-115 | Affects PacketB sig offset but current layout works. Wire-format cost outweighs the benefit for one flight. |
| Production key segregation | VOID-121 | Demo build permits demo keys. Mandatory before beta. |
| Full SQLite / FastAPI dashboard | — | `receipts.json` (append-only JSONL) is sufficient for TRL 4 evidence. Upgrade deferred to post-HAB. |
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
- Receipt spec: [Receipt-spec.md](Receipt-spec.md)
- Acknowledgment spec: [Acknowledgment-spec.md](Acknowledgment-spec.md)
- Notion database: https://www.notion.so/33fd64b77e4080849c56d65bd7683577

---

## 📝 Change Log

| Date | Change | Reason |
|---|---|---|
| 2026-04-16 | v1 — Journey created, 26 tickets tagged. | Initial lock after project audit. |
| 2026-04-16 | v2 — Added 5 tickets (VOID-050, VOID-051, VOID-052, VOID-053, VOID-120). Renumbered all downstream. | Smart-contract settlement is part of the alpha payment flow. Added VOID-120 after KSY hygiene review. |
| 2026-04-16 | v3 — Flat-sat scope lock. Phase A 12 → 11; Phase B 10 → 11. VOID-053 moved Phase A → Phase B capstone. VOID-051/052 scope-noted as local Anvil only. VOID-129 confirmed as full verify. No simulation stubs. | User scope clarifications: flat sat = desk-top dry HAB on local chain; bouncer verify real, not mocked; no stub harnesses. |
| 2026-04-16 | v4 — **Full commerce loop + TRL 4 target.** Phase A 11 → 17, Phase B 11 → 11 (offset +6). Added 3 new tickets (VOID-134 bouncer ACK emit, VOID-135 gateway Packet C + `receipts.json` persistence, VOID-136 seller Packet D emit). Flipped 3 KSY hygiene tickets back onto the path (VOID-116 ACK align, VOID-119 D align, VOID-122 CRC pre-validation — now a functional bug risk with both D and ACK flying). Alpha-done gate (VOID-130) scope expanded to full A→B→ACK→Settle→C→D loop with `receipts.json` persistence surviving a gateway restart. Renumbered all Phase B/C/D/E tickets +6. | User scope decision: A→B→settle alone is telemetry-with-a-contract, not commerce. Full store-and-forward + receipts + delivery confirmation is what demonstrates a closed-loop M2M transaction and reads as TRL 4 to funders. `receipts.json` is the audit artefact proving it worked. |
| 2026-04-19 | v5 — **VOID-116 (#9) and VOID-119 (#10) deferred to post-flat-sat**. Priority P1 → P2, Status "Not started" → "⏸ Deferred". Dependency references on rows #13 (VOID-134) and #16 (VOID-136) updated: `Requires #9` / `Requires #10` dropped. VOID-122 (#11) stays on the critical path. | Honest cost/benefit review: both deferred tickets are cosmetic u32 alignment inside `#pragma pack(push, 1)` structs. Runtime cost of NOT fixing is zero — the compiler emits byte-by-byte loads, there is no LoadStoreError on ESP32-S3, and our 1 kbps LoRa link masks any CPU cycle difference by orders of magnitude. The proposed fix shuffled pad bytes within the same total frame (120 B / 128 B unchanged), so wire bit-flip surface and airtime were also unchanged. The ACK + D emission tickets (#13, #16) consume the packed structs directly and don't care about the internal offset of a u32 field. Net: P2 hygiene churn on the critical path. Re-evaluate in the post-HAB alignment sweep if a concrete problem surfaces. **VOID-122 kept in scope because CRC pre-validation is a live defensive gate (catches RF bit-flips in any field before dispatch trusts them), not cosmetic alignment.** |
| 2026-04-19 | v6 — **VOID-051 amended: `address wallet` added to `SettlementIntent` + `Settlement` + `SettlementCreated` event.** Landed alongside VOID-052 (#14) in the same PR. Contract bumped from 4-field intent to 5-field; zero-address guard `EmptyWallet()` added; existing tests updated + new `test_rejectsZeroAddressWallet`. Gateway side: `registry.SatRecord.Wallet` (already present as field, was `"test-only"`) now carries a real Anvil address per sat_id, looked up on verified PacketB before submission. | User scope decision 2026-04-19: the flat-sat demo's job is to prove **offline→on-chain transition**. LoRa↔LoRa already proven in prior work. So the on-chain artefact must be **self-contained** — a block-explorer view of the `settleBatch` tx + `SettlementCreated` event must tell the full payment story without consulting the gateway's off-chain JSON log. Recording only `{satId, amount, assetId, txNonce}` on-chain leaves the recipient invisible to an auditor. Adding `wallet` to the intent costs ~30 min of churn on a merged ticket and gives the demo a much stronger "I can prove this payment intent was recorded on-chain to this address" pitch for TRL 4 / seed. Neither option actually moves ERC20 tokens (that's Phase B / VOID-053); the richer on-chain record is the part we can prove today. |
| 2026-04-19 | v7 — **VOID-135 (#15) split into #15a and #15b.** #15a lands the in-process pipeline: `chain.Watcher` polls Escrow.SettlementCreated every 2 s, `receipt.Processor` builds + signs PacketC (byte-exact match to `test/vectors/snlp/packet_c.bin`), `receipt.Store` appends JSONL to `gateway/data/receipts.json` with atomic write + reload dedup set on boot. #15b remains open for LoRa egress to the bouncer + end-to-end kill-gateway-mid-run crash-safety test. Row #15 on the journey stays `🟡` (not struck through) until #15b merges. | Honest complexity read: VOID-135 as originally scoped was ~3-5x the code of VOID-052 because it introduces event watching, persistent state, restart semantics, AND a new LoRa egress architecture we don't have yet. Splitting into two focused PRs keeps each review under ~1000 lines and gives a clean rollback point. #15a is fully testable in-process (covered by `receipt` + `chain.Watcher` tests including `simulated.Backend` integration); #15b needs bouncer coordination and is the obvious seam. |
| 2026-04-19 | v8 — **#15b further split into #15b-gateway (this PR) and #15c = VOID-138 (new ticket).** #15b-gateway lands the full Go side: `receipt.Store` gains DispatchStatus PENDING/DISPATCHED state machine with append-only status updates + reload-takes-latest semantics, legacy #15a lines default to PENDING on upgrade; new `handlers/egress.go` with GET /api/v1/egress/pending + POST /api/v1/egress/ack, page-size cap, 400/404/503 error paths, idempotent ACKs; crash-safety across restart proven end-to-end via simulated close/reopen cycles. 27 new Go tests (7 store, 10 egress handlers, 1 E2E, 3 crash-safety, 6 pre-existing). #15c (VOID-138) carves off the C++ bouncer poll client: it consumes this PR's HTTP contract, hand-rolled bounded JSON scanner, LoRa TX via existing serial_hal, POSTs ACKs. | Deeper-than-expected honest scope read once we were in the code: the bouncer is CERT-compliant C++ with NO existing test infrastructure (no GoogleTest, no ctest, no mock HTTP layer in `ground-station/`). Bundling the bouncer in #15b meant either (a) bootstrapping C++ test infra from scratch in the same PR, or (b) shipping the bouncer without unit tests. Neither was worth the PR-size cost: the gateway-side HTTP contract is fully pinned by the 27 Go tests, so #15c's C++ work is unambiguous. A clean PR seam ships known-correct Go now and lets the C++ PR focus purely on the bouncer implementation + its test infra decision. Row #15 remains 🟡 until VOID-138 merges. |
| 2026-04-20 | v11 — **VOID-136 (#16) landed → seller emits PacketD on verified PacketC.** Phase A commerce-loop emit path is now complete in software: **A → B → ACK → Settle → C → D** all wired end-to-end. New `packet_d_builder` pure-function module in `void-core/{include,src}` (no heap, no virtuals, table-free IEEE-802.3 CRC32). Byte-exact against `test/vectors/snlp/packet_d.bin` under the VOID-123 deterministic input set (`downlink_ts = 1710000100000`, `sat_b_id = 0xCAFEBABE`, `payload[i] = 0xE0 + i`). 6 new `PacketDBuilder` tests (golden byte-equality, size/null guards, F-03 magic at body-offset 0, sat_b_id endianness, tail-pad zero-fill). Hooked into `void-core/CMakeLists.txt` so both SNLP (`void_full_tests`) and CCSDS (`void_full_tests_ccsds`) targets compile it; the SNLP-only tests gate via `#if VOID_PROTOCOL_TYPE == 2`. 60/60 ctest green across both tiers. Wired into `satellite-firmware/src/seller.cpp` as a new SNLP-only PacketC RX handler: matches on `APID == SELLER_APID && len == SIZE_PACKET_C`, verifies CRC32 over the first 104 bytes, copies the 98-byte PacketC body into `PacketD.payload`, calls the builder, `Void.radio.transmit`s the 136 B frame, updates the OLED. Legacy TunnelData / self-generated-PacketC Phase 6 branch preserved as a parallel `else if` — dormant under the post-VOID-135 bouncer-egress architecture but kept for review-scope minimization. | **Design decisions.** (1) **Builder lives in `void-core/`, not `satellite-firmware/`** — mirrors `security_manager` as a shared firmware primitive, plugs into the existing GoogleTest + libsodium suite with zero infra bootstrap, and is callable from any firmware role. Putting it in `satellite-firmware/` would require either a second GoogleTest bootstrap (we did that once for ground-station in VOID-138; once is enough) or shipping the builder untested on the firmware side. (2) **Legacy TunnelData branch kept, not removed** — minimal PR scope per the journey workflow, and the old handler only matches on `apid == 0xA1 && len == 88` which no post-VOID-135 bouncer will emit. It is dead code in practice but the existing firmware demos may still exercise it in isolation; clean removal is a follow-up ticket. (3) **CRC is the "verify" bar on the firmware side, not sig** — full Ed25519 verify on the Heltec requires shipping the gateway's pubkey into firmware, which is a Phase B concern (VOID-118 is deferred per journey non-goals). CRC catches RF bit-flips and is cheap on the ESP32. |
| 2026-04-19 | v10 — **VOID-134 (#13) landed → bouncer emits PacketAck on verified PacketB.** New `ack_builder` pure-function module in `ground-station/` (no heap, no virtuals, table-free IEEE-802.3 CRC32 matching Go's `crc32.ChecksumIEEE`). Byte-exact against `test/vectors/snlp/packet_ack.bin` under the VOID-123 deterministic input set. Wired into `main_new.cpp` PACKET_B handler: on `process_packet` verify-success, extracts `PacketB.sat_id` from SNLP offset 112 (LE u32), builds the 136 B frame, emits as `PACKET_ACK_TX:<hex>\n` via `serial_write_bytes` — mirrors VOID-138's `PACKET_C_TX:` convention. 5 new AckBuilder tests (golden equality, size/null guards, F-03 magic at body-offset 0, target_tx_id endianness). 41/41 ground-station ctest green; production binary compiles clean under full CERT flags. | **Design decision — Option A (ACK on bouncer-verify, independent of gateway delivery).** Two reasons. (1) Literal reading of VOID-134 AC: *"after the ground bouncer successfully verifies... it MUST emit a Packet ACK"*. (2) Decoupling LoRa-downlink semantics from gateway/L2 health means a transient gateway outage doesn't suppress the buyer's receipt confirmation — which is precisely the failure mode the flat-sat is supposed to model out (the crash-safety loop lives in VOID-135/138). Gateway coupling can be tightened in a later ticket if we ever need ACK-on-settled semantics; for the TRL 4 alpha loop, *verify-means-receipted* is the right invariant. Option B (extend `gateway_client::push_to_l2` to surface HTTP status before ACK) was rejected as scope creep: it couples two orthogonal paths and expands this PR's blast radius into `gateway_client.cpp`. |
| 2026-04-19 | v9 — **VOID-138 (#15c) landed → row #15 fully closed.** Bootstrapped GoogleTest via FetchContent in `ground-station/CMakeLists.txt` alongside the existing libsodium dep — matches the `void-core/` pattern, gives the ground-station tree real unit-test coverage for the first time. Built four new components red-green: `egress_json` (bounded hand-rolled JSON scanner, 11 tests), `egress_hex` (224-char → 112-byte decoder, 8 tests), `egress_poll_client` (raw-socket HTTP GET+POST against a canned in-process test server, 8 tests), `egress_orchestrator` (template-based static-dispatch wire-up: poll → parse → hex-decode → LoRa TX → ACK, 8 tests — **NO virtual functions**, tests inject a duck-typed MockHttpClient). Integrated into `main_new.cpp` as a background `egress_poll_listener` thread, polling every 1 s (env-tunable via `VOID_EGRESS_POLL_MS`). `ground_station` production binary now compiles clean under full CERT flags (-Werror -Wshadow -Wvla -Wconversion -Wsign-conversion -Wcast-align -Wold-style-cast) with `VOID_PROTOCOL_TYPE=2` explicitly set. 36/36 ctest green. | User pushback during implementation: "why are we using virtual? we are trying to keep as close as we can to static memory usage and trying to avoid the heap at all cost." Original orchestrator design used an `IEgressHttpClient` abstract base for test-mocking. Refactored to template-on-client-type (`EgressOrchestrator<Client>`) so production plugs in `EgressPollClient` and tests pass a `MockHttpClient` struct — no inheritance, no vtable, full compile-time dispatch. Saved to user memory so future C++ in this tree defaults to static dispatch. |
