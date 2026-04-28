# VOID Protocol — Project Tickets (Pre-Alpha → HAB Launch)

> **Authority:** Tiny Innovation Group Ltd
>
> **Timeline:** 3 months to pre-alpha, 6 months to HAB launch
>
> **Board Structure:** Backlog → In Progress → Review → Done
>
> **Labels:** `firmware`, `ground`, `contract`, `infra`, `docs`, `test`, `regulatory`, `hardware`

---

## Milestones

| ID | Milestone | Target | Gate Criteria |
|---|---|---|---|
| **M0** | Project Setup | Week 1 | Repos created, CI running, board populated |
| **M1** | Radio Blinky | Week 2 | Heltec Tx/Rx confirmed, GPS lock verified |
| **M2** | First Packet on Air | Week 4 | TinyGS station receives valid SNLP heartbeat |
| **M3** | Crypto-Signed Packet B | Week 5 | Packet B with Ed25519 sig verified by Python parser |
| **M4** | Ground Ingest | Week 7 | Go Gateway receives Packet B via MQTT, verifies sig |
| **M5** | Testnet Settlement | Week 9 | End-to-end: radio → MQTT → verify → on-chain escrow |
| **M6** | Test Vector Suite | Week 10 | All packet types parsed identically in Python, Go, C++ |
| **M7** | Duty Cycle Certified | Week 11 | 24hr soak test proves <36s Tx per rolling hour |
| **M8** | Pre-Alpha Gate | Week 12 | Full dress rehearsal: battery, cold, range, settlement |
| **M9** | CAA NOTAM Filed | Week 14 | Application submitted to UK CAA |
| **M10** | Flight Ready | Week 22 | Payload integrated, recovery tested, launch site confirmed |
| **M11** | HAB Launch | Week 24 | Flight executed, telemetry captured, settlement proven |

---

## Epic 0: Project Infrastructure

### VOID-001 — Repository Setup
- **Label:** `infra`
- **Milestone:** M0
- **Priority:** P0
- **Description:** Create monorepo or multi-repo structure.
- **Acceptance Criteria:**
  - `void-firmware/` — PlatformIO project (ESP32-S3 / Heltec V3)
  - `void-gateway/` — Go module
  - `void-contracts/` — Smart contract project (Anchor/Hardhat/Foundry)
  - `void-ksy/` — Hardened KSY modules + test vectors
  - `void-scripts/` — Python tooling (`gen_packet.py`, Kaitai parsers)
  - `.gitignore` covers PlatformIO build artifacts, Go binaries, node_modules
  - `LICENSE` file (Apache 2.0) in root

### VOID-002 — PlatformIO Project Scaffold
- **Label:** `firmware`, `infra`
- **Milestone:** M0
- **Priority:** P0
- **Description:** Initialize PlatformIO project targeting Heltec V3 (ESP32-S3).
- **Acceptance Criteria:**
  - `platformio.ini` configured for `heltec_wifi_lora_32_V3`
  - RadioLib added as dependency
  - libsodium (Arduino-compatible wrapper or ESP-IDF component) added
  - Compiles cleanly with `-Wall -Werror`
  - `sodium_init()` called in `setup()`, returns 0
  - Empty `loop()` runs without crash for 60 seconds

### VOID-003 — Go Gateway Module Init
- **Label:** `ground`, `infra`
- **Milestone:** M0
- **Priority:** P1
- **Description:** Initialize Go module for the ground station gateway.
- **Acceptance Criteria:**
  - `go.mod` initialized
  - MQTT client library added (Eclipse Paho or `emitter`)
  - libsodium Go binding added (`go-sodium` or `crypto/ed25519` stdlib)
  - Compiles cleanly
  - Can connect to a public MQTT broker and subscribe to a test topic

### VOID-004 — CI Pipeline
- **Label:** `infra`
- **Milestone:** M0
- **Priority:** P1
- **Description:** Basic CI that builds firmware and Go gateway on every push.
- **Acceptance Criteria:**
  - GitHub Actions (or equivalent) workflow
  - Firmware: `pio run` succeeds
  - Gateway: `go build ./...` and `go test ./...` succeed
  - KSY: `kaitai-struct-compiler` generates Python and Go parsers without error
  - Runs on push to `main` and on PRs

### VOID-005 — Project Board Setup
- **Label:** `infra`
- **Milestone:** M0
- **Priority:** P0
- **Description:** Create Trello/Notion board with columns and import tickets.
- **Acceptance Criteria:**
  - Columns: Backlog, In Progress, Review, Done, Blocked
  - Labels created for all categories
  - Milestones created with target dates
  - All tickets from this document imported
  - Each ticket has an owner (even if it's the same person)

---

## Epic 1: Radio & GPS Foundation

### VOID-010 — SX1262 Basic Tx/Rx
- **Label:** `firmware`
- **Milestone:** M1
- **Priority:** P0
- **Depends on:** VOID-002
- **Description:** Confirm RadioLib drives the SX1262 on Heltec V3. Transmit a raw byte payload and receive it on a second Heltec or SDR.
- **Acceptance Criteria:**
  - RadioLib `SX1262` object initialized with correct pin mapping for Heltec V3
  - LoRa configured: 868.0 MHz, BW 125kHz, SF8, CR 4/5
  - Transmit 32-byte test payload every 5 seconds
  - Second Heltec (or SDR) receives and prints payload
  - RSSI and SNR logged on receiver side

### VOID-011 — GPS Module Integration
- **Label:** `firmware`, `hardware`
- **Milestone:** M1
- **Priority:** P0
- **Depends on:** VOID-002
- **Description:** Wire u-blox GPS module to Heltec V3 via UART. Parse NMEA sentences for position and time.
- **Acceptance Criteria:**
  - GPS module wired: Tx→RX2, Rx→TX2, VCC→3.3V, GND→GND
  - TinyGPS++ or equivalent library parsing `$GPRMC` and `$GPGGA`
  - Serial output shows: lat, lon, altitude, UTC time, satellite count
  - Cold start to first fix < 60 seconds (outdoor, clear sky)
  - PPS pin connected to ESP32 GPIO for future time-sync (interrupt configured but handler can be stub)

### VOID-012 — Frequency & Power Compliance Check
- **Label:** `firmware`, `regulatory`
- **Milestone:** M1
- **Priority:** P0
- **Depends on:** VOID-010
- **Description:** Verify the Heltec V3 transmits within UK ISM 868 MHz band limits.
- **Acceptance Criteria:**
  - Tx frequency confirmed at 868.0–868.6 MHz (sub-band g) via SDR waterfall
  - Tx power set to +14 dBm maximum (ETSI limit for ERP in sub-band g)
  - No spurious emissions visible on adjacent bands
  - Document the exact RadioLib configuration values used

---

## Epic 2: SNLP Framing & Heartbeat

### VOID-020 — Static Type Definitions (`void_types.h`)
- **Label:** `firmware`
- **Milestone:** M2
- **Priority:** P0
- **Depends on:** VOID-002
- **Description:** Implement all packet structs as packed C structs matching the hardened KSY.
- **Acceptance Criteria:**
  - `node_state_t` struct defined, statically allocated, < 1.5KB
  - `snlp_header_t` — 12 bytes, sync word + CCSDS + 2-byte pad
  - `packet_b_body_t` — 170 bytes, all fields at correct offsets
  - `heartbeat_body_t` — 34 bytes (50 with HMAC extension, but HMAC deferred for pre-alpha)
  - `ccsds_header_t` — 6 bytes, bitfield extraction functions
  - `static_assert(sizeof(snlp_header_t) == 12)` for every struct
  - All structs `__attribute__((packed))` with explicit padding fields

### VOID-021 — SNLP Frame Builder
- **Label:** `firmware`
- **Milestone:** M2
- **Priority:** P0
- **Depends on:** VOID-020
- **Description:** Function that takes a body buffer + length and wraps it in a complete SNLP frame.
- **Acceptance Criteria:**
  - `uint16_t snlp_frame_build(uint8_t *out, const uint8_t *body, uint16_t body_len, uint16_t apid, uint8_t type, uint16_t seq_count)`
  - Writes sync word `0x1D01A5A5` at offset 0
  - Writes CCSDS header with correct `packet_data_length` (body_len + pad - 1)
  - Writes 2-byte alignment pad
  - Copies body after header
  - Returns total frame size
  - Unit test: output matches hand-crafted hex vector

### VOID-022 — Heartbeat Generation & Transmission
- **Label:** `firmware`
- **Milestone:** M2
- **Priority:** P0
- **Depends on:** VOID-011, VOID-021
- **Description:** Generate heartbeat packets from live sensor data and transmit via SNLP.
- **Acceptance Criteria:**
  - Heartbeat populated from: GPS (lat, lon, speed, sat count), ADC (battery voltage), internal temp sensor
  - `sys_state` set to current state machine state ID
  - CRC32 computed over heartbeat body (excluding CRC field itself)
  - Wrapped in SNLP frame and transmitted via RadioLib every 30 seconds
  - Verified on receiver: hex dump matches KSY heartbeat_body layout

### VOID-023 — TinyGS Reception Test
- **Label:** `firmware`, `test`
- **Milestone:** M2
- **Priority:** P0
- **Depends on:** VOID-022
- **Description:** Confirm a TinyGS ground station (own or community) receives the SNLP heartbeat and forwards it via MQTT.
- **Acceptance Criteria:**
  - Own TinyGS station configured to listen on 868.0 MHz, SF8, BW 125kHz
  - Station receives heartbeat frame
  - Frame appears on TinyGS MQTT feed (or local MQTT broker if self-hosted)
  - Raw hex payload extractable from MQTT message
  - Parse raw hex with Python Kaitai parser — all fields decode correctly

---

## Epic 3: Packet B & Cryptography

### VOID-030 — Pre-Shared Key Provisioning
- **Label:** `firmware`
- **Milestone:** M3
- **Priority:** P0
- **Depends on:** VOID-002
- **Description:** Generate and burn a pre-shared Ed25519 keypair and ChaCha20 symmetric key into firmware for pre-alpha. Replaces the full Handshake flow.
- **Acceptance Criteria:**
  - `config.h` contains: `STATIC_ED25519_SECRET_KEY[64]`, `STATIC_ED25519_PUBLIC_KEY[32]`, `STATIC_SESSION_KEY[32]`
  - Keys generated by a Python script using libsodium and printed as C arrays
  - Same public key and session key stored in a `ground_keys.json` for the Go Gateway
  - `sodium_init()` succeeds and `crypto_sign_ed25519_sk_to_pk()` recovers the correct public key from the secret key on-device

### VOID-031 — Static Invoice Definition
- **Label:** `firmware`
- **Milestone:** M3
- **Priority:** P0
- **Description:** Hardcode a test invoice (Packet A body) in flash. This simulates Sat A broadcasting an offer.
- **Acceptance Criteria:**
  - `static_invoice` contains: valid `epoch_ts` (set at boot from GPS), dummy `pos_vec`, `vel_vec`, `sat_id=0x00000001`, `amount=1000` (0.001 USDC equivalent), `asset_id=1` (USDC), valid CRC32
  - Invoice is 62 bytes, matches `packet_a_body` in KSY

### VOID-032 — Packet B Construction
- **Label:** `firmware`
- **Milestone:** M3
- **Priority:** P0
- **Depends on:** VOID-020, VOID-030, VOID-031
- **Description:** Build a complete Packet B from the static invoice + live Sat B telemetry.
- **Acceptance Criteria:**
  - `epoch_ts` from GPS time
  - `pos_vec` from GPS position (converted to ECEF doubles)
  - `enc_payload`: static invoice encrypted with `crypto_aead_chacha20poly1305_ietf_encrypt` using pre-shared session key and nonce derived from `nonce` counter
  - `sat_id` set to Sat B's provisioned ID
  - `nonce` incremented per packet (stored in `node_state_t`)
  - `signature`: `crypto_sign_ed25519_detached` over the correct byte range
  - `global_crc`: CRC32 over entire body
  - Total body: 170 bytes. Wrapped in SNLP frame: 182 bytes.

### VOID-033 — Packet B Transmission & Capture
- **Label:** `firmware`, `test`
- **Milestone:** M3
- **Priority:** P0
- **Depends on:** VOID-032, VOID-023
- **Description:** Transmit Packet B and verify the complete binary is correct.
- **Acceptance Criteria:**
  - Packet B transmitted via RadioLib at configured interval
  - Captured on TinyGS station or SDR
  - Raw hex parsed by Python Kaitai parser: all fields decode correctly
  - `enc_payload` decrypted by Python script using the pre-shared key: inner invoice fields match
  - Ed25519 signature verified by Python script using the public key

### VOID-034 — NMEA to ECEF Conversion
- **Label:** `firmware`
- **Milestone:** M3
- **Priority:** P1
- **Depends on:** VOID-011
- **Description:** Convert GPS lat/lon/alt (WGS84) to ECEF X/Y/Z doubles for `pos_vec`.
- **Acceptance Criteria:**
  - Function: `void gps_to_ecef(double lat_deg, double lon_deg, double alt_m, double *x, double *y, double *z)`
  - Uses WGS84 ellipsoid constants (a = 6378137.0, f = 1/298.257223563)
  - Unit test: known lat/lon/alt produces correct ECEF within 1m accuracy
  - No floating-point library dependencies beyond what ESP-IDF provides

---

## Epic 4: Ground Station Gateway

### VOID-040 — MQTT Client & SNLP Detection
- **Label:** `ground`
- **Milestone:** M4
- **Priority:** P0
- **Depends on:** VOID-003
- **Description:** Go Gateway connects to TinyGS MQTT and identifies SNLP packets.
- **Acceptance Criteria:**
  - Connects to TinyGS MQTT broker (or local broker for testing)
  - Subscribes to `tinygs/packets` (or equivalent topic)
  - On message: checks first 4 bytes for sync word `0x1D01A5A5`
  - If SNLP: extracts CCSDS header, logs APID, packet type, sequence count, payload length
  - If not SNLP: logs "unknown frame" and skips
  - Graceful reconnect on MQTT disconnect

### VOID-041 — Packet B Parser (Go)
- **Label:** `ground`
- **Milestone:** M4
- **Priority:** P0
- **Depends on:** VOID-040
- **Description:** Parse the Packet B body from the SNLP frame in Go.
- **Acceptance Criteria:**
  - Strip 12-byte SNLP header
  - Parse all Packet B fields into a Go struct with correct byte offsets
  - Extract `enc_payload` (62 bytes)
  - Extract `signature` (64 bytes)
  - Extract `global_crc` and validate against computed CRC32
  - Log all parsed fields to stdout

### VOID-042 — Signature Verification (Go)
- **Label:** `ground`
- **Milestone:** M4
- **Priority:** P0
- **Depends on:** VOID-041, VOID-030
- **Description:** Verify the Ed25519 signature on Packet B using the pre-shared public key.
- **Acceptance Criteria:**
  - Load public key from `ground_keys.json`
  - Extract signed byte range from Packet B
  - Verify using `crypto/ed25519.Verify()` (Go stdlib)
  - Log "SIGNATURE VALID" or "SIGNATURE INVALID" with sat_id and nonce
  - On valid: decrypt `enc_payload` using session key and nonce
  - Log decrypted inner invoice fields

### VOID-043 — Nonce Tracking & Replay Rejection
- **Label:** `ground`
- **Milestone:** M4
- **Priority:** P1
- **Depends on:** VOID-042
- **Description:** Track `last_nonce` per `sat_id` and reject replays.
- **Acceptance Criteria:**
  - In-memory map: `sat_id → last_nonce`
  - On Packet B: check `nonce > last_nonce` for this `sat_id`
  - If replay: log "REPLAY REJECTED" with details, discard
  - If valid: update `last_nonce`, proceed to settlement
  - Check `epoch_ts` within 60 seconds of ground station clock

### VOID-044 — Heartbeat Parser (Go)
- **Label:** `ground`
- **Milestone:** M4
- **Priority:** P2
- **Depends on:** VOID-040
- **Description:** Parse heartbeat packets for telemetry monitoring.
- **Acceptance Criteria:**
  - Detect heartbeat by body length (34 bytes) and telemetry type bit
  - Parse all fields: timestamp, battery, temp, pressure, state, GPS
  - Convert fixed-point lat/lon to decimal degrees
  - Log to structured format (JSON) for dashboard consumption

---

## Epic 5: L2 Smart Contract

### VOID-050 — Contract Scaffold
- **Label:** `contract`
- **Milestone:** M5
- **Priority:** P0
- **Description:** Initialize smart contract project for target chain (Solana/EVM).
- **Acceptance Criteria:**
  - Project created with framework (Anchor for Solana, Foundry for EVM)
  - Compiles cleanly
  - Deploys to local testnet (solana-test-validator or Anvil)
  - Basic "hello world" function callable

### VOID-051 — Minimal Escrow Function
- **Label:** `contract`
- **Milestone:** M5
- **Priority:** P0
- **Depends on:** VOID-050
- **Description:** Implement a `settle_batch` function that records settlement intents.
- **Acceptance Criteria:**
  - Function accepts: array of `{sat_id, amount, asset_id, tx_nonce}` (max 10)
  - Validates `asset_id == 1` (USDC only)
  - Stores each entry in contract state with status `PENDING`
  - Emits event per entry: `SettlementCreated(sat_id, amount, tx_nonce)`
  - Deployed on testnet
  - Callable from Go Gateway via RPC

### VOID-052 — Gateway → Contract Integration
- **Label:** `ground`, `contract`
- **Milestone:** M5
- **Priority:** P0
- **Depends on:** VOID-042, VOID-051
- **Description:** Wire the Go Gateway to submit verified Packet B data to the testnet contract.
- **Acceptance Criteria:**
  - On valid Packet B: extract `{sat_id, amount, asset_id, nonce}`
  - Buffer up to 10 settlements (or flush on 5-second timer, whichever comes first)
  - Submit batch to `settle_batch` via RPC
  - Log on-chain transaction hash
  - Handle RPC errors gracefully (retry once, then log failure)

### VOID-053 — End-to-End Settlement Test
- **Label:** `test`
- **Milestone:** M5
- **Priority:** P0
- **Depends on:** VOID-052
- **Description:** Full pipeline test: Heltec transmits Packet B → TinyGS receives → MQTT → Gateway → contract settles.
- **Acceptance Criteria:**
  - Heltec transmits Packet B with known parameters
  - Gateway receives, verifies signature, decrypts payload
  - Gateway submits to testnet contract
  - Contract emits `SettlementCreated` event
  - Event visible on testnet block explorer
  - Total latency from Tx to on-chain event: < 30 seconds

---

## Epic 6: Test Vectors & Validation

### VOID-060 — `gen_packet.py` — Binary Test Vector Generator
- **Label:** `test`, `docs`
- **Milestone:** M6
- **Priority:** P0
- **Description:** Python script that generates `.bin` files for every packet type on both SNLP and CCSDS tiers.
- **Acceptance Criteria:**
  - Generates: Packet A, B, C, D, H, ACK, Heartbeat × 2 tiers = 14 files
  - Each file has a companion `.json` with expected field values
  - Uses libsodium Python bindings for real Ed25519 signatures and ChaCha20 encryption
  - All CRC32 values computed correctly
  - SNLP frames include sync word and correct CCSDS `packet_data_length`

### VOID-061 — Kaitai Parser Validation (Python)
- **Label:** `test`
- **Milestone:** M6
- **Priority:** P0
- **Depends on:** VOID-060
- **Description:** Generate Python parser from hardened KSY and validate against all test vectors.
- **Acceptance Criteria:**
  - `kaitai-struct-compiler -t python` runs without error on all KSY modules
  - Python test script parses every `.bin` file
  - Every parsed field matches the companion `.json` expected values
  - `seq_count` correctly parses 14-bit values > 1023 (regression for C-01 fix)

### VOID-062 — Go Parser Validation
- **Label:** `test`, `ground`
- **Milestone:** M6
- **Priority:** P1
- **Depends on:** VOID-060
- **Description:** Generate Go parser from KSY and validate against test vectors.
- **Acceptance Criteria:**
  - `kaitai-struct-compiler -t go` runs without error
  - Go test parses every `.bin` file
  - Field values match Python parser output byte-for-byte

### VOID-063 — Firmware Struct Validation
- **Label:** `firmware`, `test`
- **Milestone:** M6
- **Priority:** P1
- **Depends on:** VOID-060
- **Description:** Verify C++ packed structs parse test vectors identically.
- **Acceptance Criteria:**
  - Load each `.bin` into a `uint8_t` buffer on ESP32
  - `memcpy` into the corresponding packed struct
  - Print all fields via serial
  - Compare against `.json` expected values
  - Confirm no alignment faults (monitor ESP32 exception handler)

---

## Epic 7: Duty Cycle & Compliance

### VOID-070 — Airtime Counter
- **Label:** `firmware`
- **Milestone:** M7
- **Priority:** P0
- **Depends on:** VOID-010
- **Description:** Implement a rolling 1-hour duty cycle counter that tracks total Tx airtime.
- **Acceptance Criteria:**
  - `duty_cycle_t` struct: `uint32_t tx_ms_this_hour`, `uint32_t window_start_ms`
  - Incremented after every `RadioLib.transmit()` call by the measured TOA
  - Rolling window: resets `tx_ms_this_hour` when `millis() - window_start_ms > 3600000`
  - Hard limit: if `tx_ms_this_hour >= 36000` (36s), set `TX_INHIBIT` flag
  - `TX_INHIBIT` prevents all transmissions until the window rolls over
  - Unit test: simulate 100 transmissions, verify counter tracks correctly and inhibits at limit

### VOID-071 — Duty Cycle Soak Test
- **Label:** `firmware`, `test`
- **Milestone:** M7
- **Priority:** P0
- **Depends on:** VOID-070, VOID-022, VOID-033
- **Description:** Run the Heltec for 24 hours with heartbeat + Packet B on a loop. Verify duty cycle never exceeds 1%.
- **Acceptance Criteria:**
  - 24-hour continuous operation
  - Serial log captures every Tx event with: timestamp, packet type, TOA_ms, cumulative_ms, remaining_ms
  - Post-analysis script confirms: no 1-hour rolling window exceeds 36,000ms
  - `TX_INHIBIT` fires correctly if the test deliberately accelerates the Tx rate
  - No watchdog resets, no heap exhaustion, no radio hangs during the 24 hours

### VOID-072 — Tx Power Compliance
- **Label:** `firmware`, `regulatory`
- **Milestone:** M7
- **Priority:** P1
- **Depends on:** VOID-012
- **Description:** Verify conducted power output with an RF power meter or calibrated SDR.
- **Acceptance Criteria:**
  - Measured Tx power ≤ +14 dBm ERP (ETSI sub-band g limit)
  - Document antenna gain assumption (0 dBi for stock Heltec whip)
  - If using external antenna: compute ERP = conducted power + antenna gain and confirm ≤ +14 dBm

---

## Epic 8: Environmental & Range Testing

### VOID-080 — Cold Soak Test
- **Label:** `firmware`, `hardware`, `test`
- **Milestone:** M8
- **Priority:** P1
- **Description:** Operate the Heltec + GPS payload in a freezer at -30°C for 2 hours.
- **Acceptance Criteria:**
  - GPS maintains lock (or reacquires within 60s after cold start)
  - Battery voltage remains above ESP32 brownout threshold (3.0V)
  - Radio transmissions continue with no frequency drift > ±5 kHz (verify via SDR)
  - No unexpected reboots or watchdog resets
  - Log: temperature, battery voltage, GPS lock status every 30 seconds

### VOID-081 — Range Test
- **Label:** `firmware`, `test`
- **Milestone:** M8
- **Priority:** P0
- **Description:** Verify reception at realistic distance using a portable ground station.
- **Acceptance Criteria:**
  - Transmitter: Heltec V3 at +14 dBm, stock antenna, stationary
  - Receiver: second Heltec or TinyGS station, 20km away (line of sight preferred)
  - Test at SF7, SF8, SF9
  - Record: RSSI, SNR, packet loss rate (transmit 100 packets per SF)
  - SF8 packet loss < 30% at 20km = pass (this validates the link budget for HAB at altitude)

### VOID-082 — Full Dress Rehearsal
- **Label:** `firmware`, `ground`, `test`
- **Milestone:** M8
- **Priority:** P0
- **Depends on:** All M7 tickets
- **Description:** Complete end-to-end test on battery power simulating a HAB flight.
- **Acceptance Criteria:**
  - Heltec running on battery (no USB power)
  - GPS locked, heartbeats transmitting, Packet B transmitting
  - Ground station receiving via TinyGS MQTT
  - Gateway processing: sig verification, decryption, nonce check
  - Contract settling on testnet
  - Run for 3 hours continuous (simulated flight duration)
  - Duty cycle compliance maintained throughout
  - All telemetry logged and reviewable

---

## Epic 9: HAB Flight Preparation

### VOID-090 — CAA NOTAM Application
- **Label:** `regulatory`
- **Milestone:** M9
- **Priority:** P0
- **Description:** File Notice to Airmen with UK CAA for the balloon launch.
- **Acceptance Criteria:**
  - NOTAM application submitted via CAA portal
  - Launch site identified (>5 miles from any airfield)
  - Launch window proposed (4-week window for weather flexibility)
  - Payload weight declared (must be < 2kg for exemption from full CAA permission)
  - Confirmation received from CAA

### VOID-091 — Payload Enclosure Design
- **Label:** `hardware`
- **Milestone:** M10
- **Priority:** P1
- **Description:** Design and build the HAB payload box.
- **Acceptance Criteria:**
  - Expanded polystyrene (EPS) enclosure, minimum 30mm wall thickness
  - Heltec V3 + GPS + battery mounted securely (hot glue or foam cradle)
  - Antenna routed externally through enclosure wall
  - Battery: LiPo with low-temp rating, capacity for 4 hours at expected current draw
  - Total payload weight < 500g (well under 2kg CAA exemption)
  - Clearly labelled with contact details and "HARMLESS SCIENTIFIC EXPERIMENT" per CAA guidance

### VOID-092 — Recovery System
- **Label:** `hardware`
- **Milestone:** M10
- **Priority:** P1
- **Description:** Implement payload recovery tracking.
- **Acceptance Criteria:**
  - Secondary GPS tracker (independent of Heltec) for recovery — e.g., cheap GSM/GPS module or LoRaWAN tracker
  - Parachute sized for < 5 m/s descent rate at payload weight
  - Cutdown mechanism (nichrome wire on timer or altitude trigger) if required by launch site
  - Flight prediction run using CUSF predictor (predict.habhub.org) for launch day

### VOID-093 — Launch Rehearsal
- **Label:** `hardware`, `test`
- **Milestone:** M10
- **Priority:** P2
- **Description:** Tethered or short free-flight test before the main launch.
- **Acceptance Criteria:**
  - Payload powered on, GPS locked, transmitting
  - Ground station receiving telemetry
  - If tethered: raise to 30m on string, confirm GPS, radio, battery under outdoor conditions
  - If free flight: low-altitude launch (small balloon, expected burst at < 5km), test recovery system
  - Post-flight: review all telemetry logs, confirm no anomalies

### VOID-094 — Flight Day Checklist
- **Label:** `docs`
- **Milestone:** M10
- **Priority:** P1
- **Description:** Written checklist for launch day operations.
- **Acceptance Criteria:**
  - Pre-launch: battery charge check, GPS lock confirm, ground station online, MQTT connected, gateway running, contract funded on testnet
  - Launch: inflation procedure, payload attach, release procedure
  - Flight: monitor dashboard, log any anomalies, note burst time and altitude
  - Recovery: track secondary GPS, retrieve payload, download serial logs
  - Post-flight: compare serial logs with ground station logs, verify all settlements on-chain, write flight report

---

## Epic 10: Documentation

### VOID-100 — Pre-Alpha README
- **Label:** `docs`
- **Milestone:** M5
- **Priority:** P1
- **Description:** Repository README covering: what VOID Protocol is, how to build firmware, how to run gateway, how to deploy contract.
- **Acceptance Criteria:**
  - Architecture diagram (text or Mermaid)
  - Build instructions for each component
  - "Quick Start" section: flash Heltec, run gateway, see settlement
  - Links to spec documents (KSY, State Machine, TOA Analysis)

### VOID-101 — Flight Report Template
- **Label:** `docs`
- **Milestone:** M10
- **Priority:** P2
- **Description:** Template for the post-HAB-flight report.
- **Acceptance Criteria:**
  - Sections: flight summary, telemetry analysis, settlement results, lessons learned
  - Placeholder for: altitude profile, ground track map, packet loss statistics, on-chain transaction links
  - This becomes the foundation for grant applications and conference presentations

---

## Ticket Summary

| Epic | Count | Critical Path |
|---|---|---|
| 0: Infrastructure | 5 | Yes (week 1) |
| 1: Radio & GPS | 3 | Yes (week 2) |
| 2: SNLP & Heartbeat | 4 | Yes (weeks 3–4) |
| 3: Packet B & Crypto | 5 | Yes (weeks 4–5) |
| 4: Gateway | 5 | Yes (weeks 5–7) |
| 5: Smart Contract | 4 | Yes (weeks 7–9) |
| 6: Test Vectors | 4 | Yes (week 10) |
| 7: Duty Cycle | 3 | Yes (week 11) |
| 8: Env & Range Test | 3 | Yes (week 12) |
| 9: HAB Flight Prep | 5 | Yes (weeks 13–24) |
| 10: Documentation | 2 | No (parallel) |
| **Total** | **43** | |

---

*© 2026 Tiny Innovation Group Ltd.*
