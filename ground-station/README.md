# Void Ground Station

> **Authority:** Tiny Innovation Group Ltd
> **License:** Apache 2.0
> **Status:** Pre-alpha — Phase A scaffolding. Not flight-ready. Not audited.
> **Role:** Desktop C++14 bridge between the LoRa edge (Heltec) and the Go L2 gateway.

This directory contains the desktop-side C++14 application that sits between the
radio edge (a Heltec board running `void-core/` firmware) and the Go gateway
(`gateway/`). It is one of the four components in the Phase A flat-sat desk
demo: **two Heltecs + this ground-station bouncer + Go gateway + local Anvil
chain**.

---

## 1. What this binary does (current behaviour)

Today, `ground_station` is a thin, single-threaded polling loop plus a
background CLI thread. On start-up it:

1. Opens a USB serial port (POSIX `termios` on macOS/Linux, `CreateFileA` on
   Windows) at 115 200 baud via the in-house `serial_hal` shim.
2. Spawns a CLI thread that accepts three commands over stdin:
   * `h`      → writes a single `H` byte to the serial port (triggers a
     Packet H handshake on the Heltec).
   * `ack`    → writes `ACK_BUY\n` to the serial port (authorises a buy).
   * `tst_ack`→ runs a fully in-memory pipeline test using a mock
     `PacketB_t` (no hardware required).
   * `exit`   → clean shutdown.
3. In the main loop, reads lines from the serial port and dispatches on prefix:
   * `INVOICE:` → logs that an Invoice (Packet A) arrived; waits for operator
     `ack`.
   * `PACKET_B:` → hex-decodes the rest of the line into a 176-byte buffer,
     passes it through the `Bouncer`, and on success pushes the sanitised
     62-byte inner-invoice body to the Go gateway as an HTTP POST to
     `http://127.0.0.1:8080/api/v1/ingest`.

All buffers are stack- or BSS-allocated. There is no `new`, no `std::string`,
no VLA. Per `CLAUDE.md`.

---

## 2. What this binary does **not** do yet

This is scaffolding. The crypto gate — the whole point of the word
"bouncer" — is currently stubbed. Specifically:

| Function                        | File           | Current behaviour                        | Phase A requirement                  |
| ------------------------------- | -------------- | ---------------------------------------- | ------------------------------------ |
| `Bouncer::validate_signature`   | `bouncer.cpp`  | Returns `true` unconditionally.          | Real Ed25519 verify via libsodium.   |
| `Bouncer::decrypt_payload`      | `bouncer.cpp`  | `memcpy` passthrough (no key used).      | *Out of Phase A scope* — plaintext SNLP only. Keep stubbed for now. |
| `receipts.json` persistence     | *(not yet)*    | No persistence anywhere.                 | Append-only, crash-safe, survives restart (VOID-130). |
| `PacketB_t` definition reach    | `bouncer.cpp`  | Includes `void_packets.h` via CMake hop. | Must resolve to the canonical SNLP struct; tier selection logic lives in `void-core/`. |
| Packet A / ACK / C / D handling | *(not yet)*    | Only Packet B is parsed; A is only logged. | Full six-packet loop: A → B → ACK → C → D → Heartbeat. |

**There is also a known build breakage:** `CMakeLists.txt` lists
`src/main.cpp`, but only `src/main_new.cpp` and `src/main_old.cpp` exist on
disk. One of them must be chosen, renamed, and the other archived under
`legacy/` before a clean build is possible. `main_new.cpp` is the Phase A
candidate — it has the gateway bridge and the `tst_ack` harness wired in;
`main_old.cpp` predates the gateway client.

These gaps are what this directory is *for*. They are not bugs — they are the
scope of the next few tickets on the journey-to-HAB path.

---

## 3. Architecture (current layout)

```
ground-station/
├── CMakeLists.txt          CMake 3.14+, C++14, -Werror, libsodium via FetchContent
├── include/
│   ├── bouncer.h           Ed25519 + ChaCha20 gate class (stubbed)
│   ├── gateway_client.h    Zero-heap HTTP POST to the Go gateway
│   └── serial_hal.h        Four-function USB serial HAL
└── src/
    ├── bouncer.cpp         Bouncer::process_packet — length-check → cast → verify → decrypt
    ├── gateway_client.cpp  Winsock / BSD sockets, snprintf HTTP, 256-byte static JSON buffer
    ├── serial_hal.cpp      termios (POSIX) + CreateFileA/DCB (Win32)
    ├── main_new.cpp        Main loop + CLI + test harness [Phase A candidate]
    └── main_old.cpp        Pre-gateway prototype [to archive]
```

The `Bouncer` class is the only non-trivial object in the binary. It owns a
32-byte session key (zeroed on construction) and provides three operations:
`validate_signature`, `decrypt_payload`, and the composite `process_packet`
which performs a **length check first, struct cast second** — matching the
VOID-112 invariant in `CLAUDE.md`.

`GatewayClient` holds a 256-byte static JSON buffer and a 512-byte stack HTTP
request buffer. No heap, no string class. Target is hard-coded to
`127.0.0.1:8080` for the flat-sat demo; the host/port are already passed
through the constructor so an environment override is a later, trivial
change.

---

## 4. Dependencies

The build is "clone and run" — nothing global needs to be installed beyond a
toolchain and CMake.

| Dependency       | Version            | Sourced how                                  |
| ---------------- | ------------------ | -------------------------------------------- |
| C++ compiler     | C++14, GCC/Clang/MSVC | System-installed                             |
| CMake            | ≥ 3.14             | System-installed                             |
| libsodium        | 1.0.20-RELEASE     | `FetchContent` via `robinlinden/libsodium-cmake` |
| void-core        | in-repo            | `../void-core/include` + `security_manager.cpp` pulled into the target |

The libsodium dependency is in the CMake graph *now* even though the bouncer
is not yet calling into it. That's intentional: the static-asserts on
`sizeof(PacketB_t)` and the signature-scope constants come in via the
`../void-core/include` path, and libsodium is ready to wire into
`validate_signature` in the next ticket.

---

## 5. Build & run

**Prerequisites:** A C++14 compiler and CMake ≥ 3.14. On macOS: `xcode-select
--install` + `brew install cmake`. On Debian: `apt install build-essential
cmake`. On Windows: Visual Studio 2019+ with MSVC, plus CMake from Kitware.

**Fix the `main.cpp` reference first** (one-time, until the rename lands):

```bash
cd ground-station/src
mv main_new.cpp main.cpp
mkdir -p ../legacy && mv main_old.cpp ../legacy/
```

Then:

```bash
cd ground-station
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

A successful build produces `build/ground_station` and a
`metadata/ground-station-sbom.json` CycloneDX stub (post-build custom
command).

**Run in test mode** (no hardware, no gateway required for the bouncer-only
path):

```bash
./build/ground_station
# then type: tst_ack
```

**Run against hardware + the Go gateway:**

```bash
# terminal 1 — gateway
cd ../gateway && go run ./cmd/gateway

# terminal 2 — ground station
./build/ground_station /dev/tty.usbmodem14101       # macOS
./build/ground_station /dev/cu.usbserial-0001       # macOS
./build/ground_station /dev/ttyACM0                  # Linux
./build/ground_station COM3                          # Windows
```

---

## 6. Compiler posture

The ground-station is the desktop mirror of the ESP32 firmware's compiler
rules. `CMakeLists.txt` enables:

```
-Wall -Wextra -Werror -Wshadow -Wvla -Wconversion
-Wsign-conversion -Wcast-align -Wold-style-cast
-Wformat-security -O2 -D_FORTIFY_SOURCE=2
```

This is not cosmetic. The bouncer runs on a general-purpose Linux/macOS/Windows
machine at the edge of the system — the one place where a lazy C-style cast
or a sign-conversion bug would be most tempting and most damaging. The desktop
build fails the compile for any of these, so the firmware-side memory-safety
posture does not degrade at the gateway boundary.

---

## 7. Roadmap (tied to the Journey-to-HAB flat-sat)

Work items live in Notion under `Journey = journey-to-hab` and are tracked in
[`../docs/journey_to_hab_plain_text.md`](../docs/journey_to_hab_plain_text.md).
The ground-station-specific tickets on the critical path are:

1. **Pick a main.** Rename `src/main_new.cpp` → `src/main.cpp`, archive
   `main_old.cpp`, unblock the clean build.
2. **Real Ed25519 verify.** Replace the `return true;` in
   `Bouncer::validate_signature` with a libsodium `crypto_sign_verify_detached`
   call, keyed off the bouncer's static signer-pubkey table.
   Signature scope is **exactly** `header + body[0..105]` per VOID-111
   (`packetBSigScopeBody = 106`) — this must be byte-identical to the Go side.
2. **receipts.json append-only persistence (VOID-130).** On every successful
   Packet C (Receipt) forwarded to the gateway, append a one-line JSON record
   to `receipts.json` via `fprintf + fflush + fsync`. Must survive a
   gateway-side crash and replay cleanly on restart.
3. **Full six-packet loop.** Add dispatch for `PACKET_A:`, `ACK:`, `PACKET_C:`,
   `PACKET_D:`, `HEARTBEAT:` alongside the existing `PACKET_B:` handler.
4. **Operator CLI expansion.** Add `inv` (send Invoice), `recv` (dump receipts),
   `status` (report last-seen per packet type) to the existing CLI thread.
5. **Flight-log hook.** Structured stderr logs (one JSON object per line)
   suitable for `journalctl`/`eventlog` capture during the TRL-4 demo run.

Phase A **explicit non-goals** (do not re-introduce these without a change-log
entry in the journey doc): ChaCha20 payload encryption in the bouncer,
heartbeat auth, replay-nonce tracking, simulation harnesses beyond the
existing `tst_ack`, SQLite/FastAPI dashboard, testnet contract deploy. See
`../CLAUDE.md` for the full non-goals list.

---

## 8. Related components

| Directory                               | Role                                                          |
| --------------------------------------- | ------------------------------------------------------------- |
| [`../void-core/`](../void-core/)        | ESP32 firmware — Sat A (seller) and Sat B (buyer/mule).       |
| [`../gateway/`](../gateway/)            | Go L2 gateway — receives bouncer HTTP POSTs, signs to Anvil.  |
| [`../satellite-firmware/`](../satellite-firmware/) | Satellite-specific firmware entrypoints.           |
| [`../docs/`](../docs/)                  | Protocol specs (CCSDS, SNLP, Handshake, ACK, Receipt).        |

For the protocol wire format that the bouncer is parsing, see
[`../docs/Protocol-spec-SNLP.md`](../docs/Protocol-spec-SNLP.md) (community
tier, 14-byte header) and
[`../docs/Protocol-spec-CCSDS.md`](../docs/Protocol-spec-CCSDS.md) (enterprise
tier, 6-byte header). Phase A flat-sat is SNLP-only.

---

## 9. Contributing

Same rules as the rest of the repo — see
[`../CONTRIBUTING.md`](../CONTRIBUTING.md) if present, otherwise
[`../CLAUDE.md`](../CLAUDE.md). Summary:

* No heap. No `std::string`. No VLAs. No C-style casts. No blind casts.
* Every `.cpp`/`.h` file you add gets the mandatory VOID file-header block.
* Every over-the-air struct gets `static_assert(sizeof(X) % 8 == 0)` and
  `static_assert(sizeof(X) <= 255)` adjacent to its definition.
* Stage files explicitly by name. No `git add -A`, no `git commit -am`.

---

*© 2026 Tiny Innovation Group Ltd. Released under the Apache 2.0 licence.*
