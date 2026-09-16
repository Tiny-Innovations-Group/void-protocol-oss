# VOID-141 — ImGui Interactive Ground Station UI (4-Window Demo Console)

> ⚠️ **SUPERSEDED 2026-09-12** — This is the original pre-freeze ticket doc, retained for history. The frozen design lives in [VOID-141_IMGUI_PORT_SPEC.md](VOID-141_IMGUI_PORT_SPEC.md) + [ground-station/ui/mockup.html](../ground-station/ui/mockup.html). Where this doc conflicts (4 dockable windows, live-data scope, Shut Off confirmation prompt, receipts tail in the L2 window), the PORT_SPEC governs: one 1760×826 window with 4 fixed panels, inert port first, live-data wiring deferred to future tickets (spec §8).

> **Notion fields:** Status: `Not started` · Priority: `P0` · Journey: `journey-to-hab` · Journey Order: `6.5` *(top of the open list — user decision 2026-09-11, journey Change Log v16)*

---

## 🎯 Goal

Build a native desktop operator console for the ground station using **Dear ImGui** (C++). One application, **four windows**, giving a live, interactable view of the flat-sat commerce loop plus operator control of the demo stack:

| # | Window | Shows |
|---|--------|-------|
| 1 | **Pipeline** | The interactive A → B → ACK → Settle → C → D commerce loop (+ Heartbeat), per-leg live state |
| 2 | **Go Server** | Gateway process health, ingest/verify/settlement/egress counters, recent events |
| 3 | **L2 Blockchain** | Local Anvil chain: block height, `settleBatch` txs, `SettlementCreated` events, receipts |
| 4 | **Controls** | Buttons: **Load Next Contract**, **Start**, **Stop**, **Shut Off** |

> 📝 *Speech-to-text interpretation (correct if wrong): "hour go server" → **our Go server** (the gateway); "L2 clock chain" → **L2 blockchain** (local Anvil); "button us off" → **Shut Off** button.*

## 🧾 Why (Context)

- Phase A closes at #17 (VOID-130) with a desk demo currently observable only via serial logs, gateway stdout, and `receipts.json`/`heartbeats.json` on disk.
- A single live console turns that into a **presentable TRL 4 evidence artefact** for seed-funding demos: the whole Invoice → Payment → ACK → Settlement → Receipt → Delivery loop visible in one place, with the on-chain proof beside it.
- **Primary driver (user, 2026-09-11):** the console is the observation surface for the flat-sat demo **screen recording** — once this work begins, the demo can be recorded and presented through it.
- The journey explicitly deferred the *SQLite/FastAPI web dashboard* — this is **not that**. It is a small native host-side tool reusing existing data sources; no database, no web server.

## 🪟 Window Specifications

### 1. Pipeline (interactive loop view)
- Visual leg diagram of the alpha wire: `A → B → ACK → Settle → C → D`, with Heartbeat shown as a continuous background lane.
- Each leg shows live state: `IDLE / ACTIVE / OK / FAIL`, last-activity timestamp, and packet counters.
- Clicking a leg expands its most recent frame as annotated hex (sync word, APID, F-03 magic, CRC result).
- Pass counter toward the #17 gate (e.g. `7 / 10 consecutive passes`), reset on any FAIL.

### 2. Go Server (gateway)
- Process alive/last-seen, listen port, uptime.
- Counters: packets ingested, Ed25519 verify pass/fail, settlement intents enqueued, `settleBatch` ok/fail, receipts PENDING/DISPATCHED.
- Scrolling recent-event pane (last N gateway events).
- Data source: new lightweight read-only **`GET /api/v1/status`** endpoint on the gateway (part of this ticket's scope) + existing egress endpoints.

### 3. L2 Blockchain (Anvil)
- Chain head: block number, latest block timestamp, gas used.
- Escrow contract address currently in use; per-run settlement history (from `SettlementCreated` logs via `eth_getLogs`).
- Live tail of `gateway/data/receipts.json` (read-only; append-only file tailed by the UI — no writes).

### 4. Controls (interactable)
| Button | Behaviour |
|---|---|
| **Load Next Contract** | Deploy a fresh `Escrow.sol` to the running Anvil (`forge create` / `cast`), capture the new address, re-point the gateway (`VOID_ESCROW_ADDRESS`), confirm with one probe call. Prepares a clean slate for the next demo run. |
| **Start** | Bring up the demo stack in dependency order: Anvil → gateway → bouncer. Buttons disabled/greyed while a transition is in flight. |
| **Stop** | Graceful halt of the demo loop (SIGTERM children, wait for exit, preserve `receipts.json`). |
| **Shut Off** | Full graceful shutdown of the entire stack **and** the UI itself. Requires confirmation. |

## 🏗️ Architecture & Constraints

- **Host-side operator tool, not flight firmware.** Lives in `ground-station/ui/` (or a new `tools/` dir — owner decision). ESP32 firmware is untouched; zero wire-format impact.
- **CERT compliance still applies to our glue code**: static/stack buffers only, `snprintf` into bounded `char` buffers, no `new`/`delete`/`std::string`, no VLAs, `static_cast` only. Dear ImGui is vendored third-party code and is exempt from our internal rules (same as libsodium), but our code must not adopt its allocation patterns.
- **Backend:** GLFW + OpenGL3 (canonical ImGui desktop backend, works on macOS dev machines).
- **Build:** CMake, added as an optional target (`-DVOID_BUILD_UI=ON`) so the existing CERT `-Werror` production binary and 41-test ctest suite are unaffected.
- **All UI→system interaction is read-mostly.** The only writes are the four control actions, each going through an existing mechanism (process spawn, HTTP POST, forge/cast). The UI never mutates `receipts.json`, never touches the LoRa serial port directly, and never crafts packets.

## ✅ Acceptance Criteria

1. Single binary launches four dockable ImGui windows matching the table above.
2. Pipeline window reflects a live bench run: with the flat-sat stack running, each leg transitions IDLE→ACTIVE→OK within 2 s of the corresponding event, and FAIL shows red on CRC/sig rejection.
3. Go Server window shows live counters that increment during a run and a last-event feed; gateway kill → window shows `DOWN` within 2 s.
4. L2 window block height advances with Anvil and each settlement appears with its tx hash; entries match `receipts.json` 1:1.
5. **Load Next Contract** deploys a fresh Escrow, the new address is displayed, and the gateway settles subsequent intents against it (verified by a `SettlementCreated` log at the new address).
6. **Start/Stop** bring the stack up/down without orphan processes; **Shut Off** closes everything including the UI, with a confirmation prompt.
7. `receipts.json` is byte-identical before/after any UI session except for legitimate new settlements (UI never truncates/rewrites it).
8. Existing suites still green: `go test ./...` (gateway, incl. new status-endpoint tests) and ground-station `ctest` unchanged and passing.

## 🚫 Non-Goals

- Not a web dashboard; no SQLite/FastAPI (remains deferred per the journey non-goals).
- No new packet types, no wire-format changes, no firmware changes.
- No TinyGS/testnet interaction — local Anvil only (matches Phase A scope).
- No authentication/multi-user — single-operator local console.

## 🔗 Dependencies

- Consumes interfaces built in **VOID-135 / VOID-138** (egress endpoints, `receipts.json`), **VOID-051/052** (Escrow + gateway chain client), and **VOID-022** (`heartbeats.json` evidence log for the Heartbeat lane).
- Adds one small read-only gateway status endpoint (Go side included in this ticket).

## 📍 Journey Placement (DECIDED 2026-09-11)

**Inserted at #6.5 — top of the open list** (journey Change Log v16). Sits after #6 (last Done ticket), before #7 (in progress) and #17 (gate). Marked **Parallel-ok**: host-side desktop tooling, zero firmware/wire-format impact, so it does not disturb #7 bench validation or #17 gate scope. Driver: it is the observation surface for the flat-sat demo screen recording.

---
*Drafted 2026-09-11 · logged in decision graph as goal node 216.*
