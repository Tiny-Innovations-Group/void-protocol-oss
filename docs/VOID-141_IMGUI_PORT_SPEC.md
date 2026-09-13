# VOID-141 — ImGui One-Shot Port Spec (Handover Document)

> **Purpose:** hand this file to an implementer (human or model) to produce the
> Dear ImGui port of the ground-station UI **in one shot**, without re-litigating design.
> The design is **complete and frozen** (decision graph nodes 231–275, 2026-09-12).
>
> **Authoritative visual artifact:** `ground-station/ui/mockup.html` (self-contained;
> open in a browser — the C++ port must be a 1:1 translation of it).

---

## 1. What you are building

- Target: `ground_station_ui` executable in `ground-station/ui/main.cpp` (file exists — stage-1 shell; rewrite it per this spec).
- Backend: GLFW + Dear ImGui + OpenGL3 (FetchContent, same pattern as `void-core/CMakeLists.txt`).
- CMake: optional target guarded by `-DVOID_BUILD_UI=ON`. Existing `ground_station` binary + 41-test ctest suite must remain untouched and green.
- C++14. **CERT/NSA rules apply to all glue code you write**: no `new`/`delete`/`malloc`, no `std::string`/`String`, no VLAs, no C-style casts, `static` for persistent buffers, `snprintf` into bounded `char` buffers. Dear ImGui itself is vendored third-party and exempt, but our code must not adopt its allocation patterns. Build the UI target with the full `-Werror` CERT flag stack.
- Keep the existing mandatory file header block at the top of `main.cpp` (update `Desc`).

## 2. Window & layout geometry

Single OS window, **not resizable**, no `imgui.ini` (`io.IniFilename = nullptr`).

| Region | Rect (px) | Notes |
|---|---|---|
| Console grid (4 panels) | x: 0..1280, y: 0..720 | fixed 2×2 grid |
| Error strip | x: 0..1280, y: 728..~826 | small log box below grid, 8px gap |
| Receipts drawer | x: 1280..1760, y: 0..720 | right of console, visible only when toggled |
| Help modal | centered over everything | overlay + backdrop |

**Total window: 1760 × ~830.** macOS: GL3.2 core profile, GLSL `#version 150`; else GL3.0 / `#version 130`. Vsync on.

Grid math: margin 12, spacing 12 → each panel **622 × 342** at (12,12), (646,12), (12,366), (646,366).

**Font:** render body text at **14 px monospace** (mockup body is 14px). Either `io.Fonts->AddFontFromFileTTF(..., 14.0f)` or accept ImGui default if no TTF ships. Do not hard-depend on a font file being present.

## 3. Theme palette (exact, from mockup CSS)

| Token | Hex | ImVec4 (RGB) |
|---|---|---|
| console bg (window/panel bg) | `#101010` | 0.063, 0.063, 0.063 |
| panel border / separator | `#3f3f48` | 0.247, 0.247, 0.282 |
| text | `#ffffff` | 1.0, 1.0, 1.0 |
| text dim | `#808080` | 0.502 × 3 |
| button | `#24456e` | 0.141, 0.271, 0.431 |
| button hover | `#4297fa` | 0.259, 0.592, 0.980 |
| button active | `#0f87fa` | 0.059, 0.529, 0.980 |
| button danger | `#8c1f1f` | 0.549, 0.122, 0.122 |
| danger hover / active | `#bf3333` / `#e64040` | 0.749,0.200,0.200 / 0.902,0.251,0.251 |
| input bg / input focus | `#1d2f4a` / `#3b5587` | 0.114,0.184,0.431 / 0.231,0.333,0.529 |
| log box bg | `#0a0a0a` | 0.039 × 3 |
| log box border | `#2a2a30` | 0.165, 0.165, 0.188 |
| state OK / swatch | `#43c666` | 0.263, 0.776, 0.400 |
| state/dot idle gray | `#5a5a64` | 0.353 × 3 |
| backdrop | — | rgba(0,0,0,0.65) |

Square markers: panel title markers and any swatches are **10×10 squares, no rounding** (border-radius 0). Buttons are square (radius 0), height 40.

## 4. Global layout container

One borderless full-window ImGui window (`NoTitleBar|NoResize|NoMove|NoCollapse|NoScrollbar`) as the stage for absolutely-placed children (existing stage-1 code already does this via `SetCursorPos`).

---

## 5. Panels (all four are `BeginChild` regions, bordered, bg `#101010`)

### 5.1 Panel 1 — "1. RF Comms (Ground ↔ Sat)"

- Title row: **10×10 gray square marker** + text `1. RF Comms (Ground ↔ Sat)`.
- Separator.
- 7 legs, each a line: name in a 19ch column + colored state word `[STATE]`:

| Leg name (exactly) | Initial state |
|---|---|
| `A    — Invoice` | IDLE |
| `B    — Payment` | IDLE |
| `ACK  — Sig Verified` | IDLE |
| `SETTLE — On-Chain` | IDLE |
| `C    — Receipt` | IDLE |
| `D    — Delivery` | IDLE |
| `HB   — Heartbeat` | IDLE |

- State colors: IDLE = dim `#808080`, ACTIVE = `#4297fa`, OK = `#43c666`, FAIL = `#e64040`.
- Separator, then dim label `LOG (sample)`, then a **log window** (spec §6) filling leftover height.
- Bottom line (3px top padding): `Passes 0/10 · Tamper rejects 0 · Restart-safe --` with bound counters (`pass-count`, `tamper-count`, `restart-safe`).
- **Semantics:** this panel is the air-side view — ALL comms between the ground-station radio and satellite Heltecs (not a commerce dashboard). Settle/A→B→ACK/C/D/HB labels are event categories for filtering; SETTLE is on-chain-tagged but still appears here as an event.

### 5.2 Panel 2 — "2. Go Server (Gateway)"

- Square marker + title, separator.
- Readout (fixed 18ch label column):
```
Status:           UNCONNECTED
Packets in:       --
Sig verify ok:    --
Sig verify fail:  --
Intents queued:   --
Receipts PENDING: --
Receipts SENT:    --
```
- Dim label `HTTP LOG (sample)` + log window.

### 5.3 Panel 3 — "3. L2 Blockchain (Anvil)"

- Square marker + title, separator.
- Readout (13ch column):
```
Status:      UNCONNECTED
Block:       --
Contract:    --
Settlements: --
```
- Dim label `BLOCKCHAIN LOG (sample — newest at bottom)` + log window. **No receipt box** (receipts live in the drawer).

### 5.4 Panel 4 — "4. Operator Controls"

- Title row: 10×10 marker + `4. Operator Controls` + **right-aligned `RUN 00:00:00`** (ticking session clock, starts when the app opens; `margin-left:auto` equivalent).
- Separator.
- Field row: label `CONTRACT VALUE:` + input (width 280, height 24, bg `#1d2f4a`, focus bg `#3b5587`). **Input model: static `char[32]`, digits only, max 16 chars, default text `500`** (mirror: bounded InputText, digits filtered in the input callback).
- Dim hint line: `amount used by LOAD NEXT CONTRACT`.
- Buttons (full width = panel content width, height 40, square):
  1. `LOAD NEXT CONTRACT`
  2. row pair: `START` | `STOP` (half width each)
  3. trio row: `SEND ACK` | `VIEW RECEIPTS` | `HELP` (thirds)
  4. `EXIT` — danger palette, full width
- Separator, then `Last action: --` (echo line).

## 6. Shared components

### 6.1 Log window (used in Panels 1/2/3 + error strip + drawer)

- Inset box: bg `#0a0a0a`, border `#2a2a30`, small padding, **scrolls both axes**, `white-space: pre` (ImGui: `BeginChild` + monospace text lines, keep `ImGuiSelectableFlags`/plain `Text`).
- Newest entries at the bottom.
- Sample content (port exactly; label each as sample):
  - Panel 1 `LOG (sample)`:
```
10:14:02  A     Invoice TX'd                 apid=100 len=80
10:14:05  B     Payment RX                   sig OK
10:14:06  ACK   Buyer ack TX'd               apid=101 len=136
10:14:12  SETTLE settleBatch block=2          tx=0xb21f...
10:14:14  C     Receipt built                status=PENDING
10:14:15  C     Receipt dispatched           apid=100 len=112
10:14:18  D     Delivery RX                  CRC OK
10:15:23  HB    heartbeat                    VBAT=3980mV T=38C
```
  - Panel 2 `HTTP LOG (sample)`:
```
10:14:05  200  POST  /api/v1/ingest    packetb.enqueued
10:14:12  200  GET   /pending          receipts=true
10:14:14  200  POST  /ack              status=dispatched
10:14:15  200  GET   /pending          receipts=false
10:15:02  200  GET   /api/v1/status
10:15:47  400  POST  /api/v1/ingest    sig fail
```
  - Panel 3 `BLOCKCHAIN LOG (sample — newest at bottom)`:
```
10:14:12  block 2 mined
10:14:12  tx 0xb21f  settleBatch()            escrow
10:14:12  event SettlementCreated             block=2
```
  - Error strip `ERRORS — all panels (sample)`:
```
10:15:47  gw      packetb.sig_fail — 400 (bad Ed25519 sig)
10:15:12  chain   escrow submit retry #1 (RPC timeout)
10:14:59  rf      PacketA CRC fail — frame dropped
```

### 6.2 Receipts drawer (toggled side panel)

- 480 × 720, docked **flush to the right edge of the console** (not overlaying panels), visible only when open; closed by default.
- Hidden/shown by the `VIEW RECEIPTS` button, which **flips its own label** to `HIDE RECEIPTS` (and back). "Last action" echo shows `SHOW RECEIPTS` / `HIDE RECEIPTS` accordingly.
- Header (dim): `receipts.json — read-only tail (sample)`, then a log window with 8 sample receipt lines, newest last, e.g.
`{"payment_id":"a1b2c3","tx_hash":"0xb21f","status":"DISPATCHED"}` (plus `d78a91/0x2b3c DISPATCHED`, `c3d4e5/0x9f8e DISPATCHED`, `f0a1b2/0x5d6c PENDING`, `708a9b/0x1a2b DISPATCHED`, `4d5e6f/0x77cc DISPATCHED`, `9c0d1e/0xa1e2 PENDING`, `e5f6a7/0xc3a9 DISPATCHED`).

### 6.3 Help modal (HELP button)

- Opens a full-console overlay: backdrop rgba(0,0,0,0.65) (z above drawer), centered modal 620px wide, max-height 660, scrollable, sections:
  1. `What you need to do` — runbook (copy verbatim from `mockup.html` §help-modal):
     set value → LOAD NEXT CONTRACT (deploys fresh Escrow + re-points gateway) → START (brings up Anvil → gateway → bouncer in order) → watch panels → SEND ACK (operator authorises buyer payment) → VIEW RECEIPTS → STOP / EXIT.
  2. `Panels` — meaning of RF Comms (all radio comms ground ↔ sats: A/B/ACK/C/D/HB), Go Server, L2.
  3. `Leg states` — IDLE/ACTIVE/OK/FAIL legend (FAIL resets pass counter).
  4. `Gate` — one clean pass = A → B → ACK → SETTLE → C → D; 10 consecutive passes = flat-sat alpha done.
- Dismiss: `CLOSE HELP` button (200px wide) or click on the backdrop.

### 6.4 Interactive behaviors to port (all inert-data, real-echo)

- `setLegState(name, state)` helper: updates one leg's text + color (keep as a C++ function; test friendly).
- LOAD NEXT CONTRACT press → echo `LOAD NEXT CONTRACT (value=<input or 0>)`.
- START/STOP/SEND ACK/EXIT presses → echo their label.
- HELP press → open modal (echo not required).
- Run clock: `RUN hh:mm:ss`, increments once per second from app open.
- `CONTRACT VALUE` input: digits filtered, ≤16 chars, empty reads as `0` on LOAD.

---

## 7. Known accepted-scoped items (do NOT add, do NOT "fix")

- No live data: UNCONNECTED statuses, sample log lines, zero pass counters are **intended**.
- No confirmation prompt on EXIT (deferred).
- Panel markers stay gray `#5a5a64` (wiring stage activates green/red).
- No per-leg click/hex expansion (deferred, spec doc §Window Specs).
- Panel titles render at 14 px body size (mockup h2 is 13 px) — single font atlas, accepted 2026-09-13.

## 8. Wiring notes (future tickets, out of port scope — do not implement now)

- Reuse bouncer stdin CLI: `ack` (operator-buy-authorisation, NOT VOID-134's auto-ACK), `h` (help), `tst_ack`, `exit`. UI→bouncer via spawned child with piped stdin.
- Gotchas carried over: ignore SIGPIPE in the UI; `setvbuf(stdout, NULL, _IOLBF, 0)` in bouncer so logs flush through the pipe; STOP = SIGTERM children + wait; EXIT = `exit\n` to bouncer + waitpid, then close UI.
- CONTRACT VALUE needs a pass-through path to the seller's invoice amount (env var or gateway) — does not exist in the bouncer CLI today.
- Live data sources when wiring: gateway `GET /api/v1/status` (to be added, read-only), receipts.json tailer (read-only, drawer), heartbeats.json, bouncer stdout → Panel 1 LOG.

## 9. Acceptance for the port PR

1. `cmake -S . -B build -DVOID_BUILD_UI=ON` builds `ground_station_ui` clean under the CERT `-Werror` stack; `main.cpp` contains no heap use, no `std::string`, no VLAs.
2. Visual parity with `mockup.html` at 1:1: same titles, same readout strings, same button order/labels, same colors per §3, same sample content per §6.1.
3. All behaviors from §6.4 work (clock ticks, value binding in LOAD echo, drawer toggle + label flip, modal open/close).
4. Existing suites untouched and green: ground-station ctest 41/41, gateway `go test ./...`.
5. Runtime smoke without any gateway/bouncer running (everything shows UNCONNECTED/IDLE/--).

*Drafted 2026-09-12 · decision graph 231–275 · design frozen. Port landed and verified 2026-09-13 (graph 278+): clean -Werror build, 41/41 ctest, runtime smoke, glyph parity.*
