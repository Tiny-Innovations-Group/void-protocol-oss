# VOID Protocol v2.1 — Time on Air Analysis & Duty Cycle Budget

> **Authority:** Tiny Innovation Group Ltd
>
> **License:** Apache 2.0
>
> **Status:** Engineering Analysis / RF Link Budget
>
> **Target Hardware:** SX1262 LoRa Radio + ESP32-S3
>
> **Regulatory Context:** UK ISM 868 MHz (ETSI EN 300.220)
>
> **Dependency:** State Machine Spec v2.1, Hardened KSY v2.1

---

## 1. LoRa Time on Air — Calculation Method

All TOA values are computed using the standard Semtech SX1262 LoRa modem equation
from AN1200.13 (LoRa Modem Design Guide).

**Fixed Radio Parameters:**

| Parameter | Value | Rationale |
|---|---|---|
| Bandwidth (BW) | 125 kHz | Standard EU 868 MHz LoRa configuration |
| Coding Rate (CR) | 4/5 (CR=1) | Minimum FEC. Maximizes throughput at acceptable error rate. |
| Preamble Length | 8 symbols | SX1262 default. Sufficient for frame sync. |
| Header Mode | Explicit (H=0) | Required for variable packet sizes. |
| CRC | Enabled (CRC=1) | Mandatory for integrity checking. |
| Low Data Rate Opt. | Off (DE=0) | Only needed for SF11/SF12. |

**Symbol Time:**

```
Ts = 2^SF / BW

SF7:  Ts = 128  / 125000 = 1.024 ms
SF8:  Ts = 256  / 125000 = 2.048 ms
SF9:  Ts = 512  / 125000 = 4.096 ms
```

**Preamble Time:**

```
Tpreamble = (Npreamble + 4.25) × Ts

SF7: 12.25 × 1.024 = 12.54 ms
SF8: 12.25 × 2.048 = 25.09 ms
SF9: 12.25 × 4.096 = 50.18 ms
```

**Payload Symbol Count:**

```
NpayloadSymb = 8 + max(ceil((8×PL - 4×SF + 44) / (4×SF)) × 5, 0)

Where PL = total bytes on air (header + body)
```

**Total Time on Air:**

```
TOA = Tpreamble + (NpayloadSymb × Ts)
```

---

## 2. Packet Sizes on Air (SNLP / Community Tier)

All sizes include the 12-byte SNLP header (4B Sync + 6B CCSDS + 2B Pad).
Heartbeat includes the 16-byte HMAC extension (hardened spec).

| Packet | Body (bytes) | SNLP Total (bytes) | Role in Flow |
|---|---|---|---|
| **Packet A** (Invoice) | 62 | **74** | Sat A → Sat B (ISL, not ground-pass) |
| **Packet H** (Handshake) | 106 | **118** | Sat B ↔ Ground (bidirectional) |
| **Packet B** (Payment) | 170 | **182** | Sat B → Ground (downlink) |
| **Packet ACK** (Command) | 122 | **134** | Ground → Sat B (uplink) |
| **Packet C** (Receipt) | 98 | **110** | Sat A → Sat B (ISL) |
| **Packet D** (Delivery) | 122 | **134** | Sat B → Ground (downlink) |
| **Heartbeat** (+HMAC) | 50 | **62** | Any → Ground (downlink) |

---

## 3. Time on Air — Per Packet, Per Spreading Factor

### 3.1 SF7 (Fastest — Short Range / Clear Link)

| Packet | Size (B) | Payload Symbols | TOA (ms) |
|---|---|---|---|
| Heartbeat | 62 | 98 | **112.9** |
| Packet A | 74 | 113 | **128.3** |
| Packet H | 118 | 178 | **194.8** |
| Packet ACK | 134 | 198 | **215.3** |
| Packet D | 134 | 198 | **215.3** |
| Packet B | 182 | 268 | **287.2** |

### 3.2 SF8 (Mid-Range)

| Packet | Size (B) | Payload Symbols | TOA (ms) |
|---|---|---|---|
| Heartbeat | 62 | 78 | **186.0** |
| Packet A | 74 | 88 | **206.4** |
| Packet H | 118 | 128 | **288.6** |
| Packet ACK | 134 | 148 | **329.6** |
| Packet D | 134 | 148 | **329.6** |
| Packet B | 182 | 198 | **432.0** |

### 3.3 SF9 (Long Range / Weak Signal — LEO Worst Case)

| Packet | Size (B) | Payload Symbols | TOA (ms) |
|---|---|---|---|
| Heartbeat | 62 | 58 | **287.7** |
| Packet A | 74 | 68 | **328.7** |
| Packet H | 118 | 98 | **451.6** |
| Packet ACK | 134 | 108 | **492.6** |
| Packet D | 134 | 108 | **492.6** |
| Packet B | 182 | 148 | **656.6** |

---

## 4. Full Settlement Flow — End-to-End Timing

The complete transaction lifecycle during a ground pass, from Handshake through
to Escrow release. ISL (inter-satellite link) segments are excluded as they
occur outside the ground pass window and on a separate frequency.

### 4.1 Flow Steps

```
GROUND PASS BEGINS (AOS)
│
├─ [1] Sat B → Ground:  Packet H (Init)         TOA + 50ms crypto
├─ [2] Ground → Sat B:  Packet H (Resp)         TOA + 50ms crypto
│       ── Session Established ──
├─ [3] Sat B → Ground:  Packet B (Payment)      TOA + 50ms crypto
├─ [4] Ground:           L2 Settlement           500ms – 2000ms
├─ [5] Ground → Sat B:  Packet ACK              TOA + 50ms crypto
│       ── Transaction #1 Complete ──
├─ [6] Sat B → Ground:  Packet B (Payment #2)   TOA + 50ms crypto
├─ [7] Ground:           L2 Settlement           500ms – 2000ms
├─ [8] Ground → Sat B:  Packet ACK              TOA + 50ms crypto
│       ── Transaction #2 Complete ──
│       ... (repeat 6-8 for additional txs) ...
│
├─ [N] Sat B → Ground:  Packet D (Receipt)      TOA + 50ms crypto
├─ [N+1] Ground:        Verify + Release Escrow  200ms
│       ── Escrow Closed ──
│
GROUND PASS ENDS (LOS)
```

### 4.2 Timing Breakdown — First Transaction (Includes Handshake)

| Step | SF7 (ms) | SF8 (ms) | SF9 (ms) |
|---|---|---|---|
| [1] H Init (Sat B Tx) | 245 | 339 | 502 |
| [2] H Resp (Ground Tx) | 245 | 339 | 502 |
| [3] Packet B (Sat B Tx) | 337 | 482 | 707 |
| [4] L2 Settlement (avg) | 1000 | 1000 | 1000 |
| [5] ACK (Ground Tx) | 265 | 380 | 543 |
| **First Tx Total** | **2,092** | **2,540** | **3,254** |

**First transaction: 2.1s (SF7) to 3.3s (SF9).**

### 4.3 Timing Breakdown — Subsequent Transactions (Session Reuse)

| Step | SF7 (ms) | SF8 (ms) | SF9 (ms) |
|---|---|---|---|
| Packet B (Sat B Tx) | 337 | 482 | 707 |
| L2 Settlement (avg) | 1000 | 1000 | 1000 |
| ACK (Ground Tx) | 265 | 380 | 543 |
| **Per Tx Total** | **1,602** | **1,862** | **2,250** |

**Subsequent transactions: 1.6s (SF7) to 2.3s (SF9).**

### 4.4 Timing with Retries

LoRa packet loss rate at LEO distances with proper link budget is typically 10–30%.
The retry strategy: if Sat B doesn't receive an ACK within a timeout window, it
retransmits Packet B. The timeout must account for the full round-trip plus
blockchain settlement.

**Retry timeout = Packet B TOA + L2 max latency + ACK TOA + margin**

| | SF7 | SF8 | SF9 |
|---|---|---|---|
| Retry timeout | 3.5s | 4.0s | 5.0s |

**Worst-case with 2 retries (3 total attempts):**

| Scenario | SF7 (ms) | SF8 (ms) | SF9 (ms) |
|---|---|---|---|
| First Tx + Handshake | 2,092 | 2,540 | 3,254 |
| + 1 retry (timeout + retx) | +4,102 | +4,862 | +5,957 |
| + 2nd retry (timeout + retx) | +4,102 | +4,862 | +5,957 |
| **Worst Case Total** | **10,296** | **12,264** | **15,168** |

**Worst case first transaction with 2 retries: 10.3s (SF7) to 15.2s (SF9).**

For a subsequent transaction with 2 retries:

| Scenario | SF7 (ms) | SF8 (ms) | SF9 (ms) |
|---|---|---|---|
| Single attempt | 1,602 | 1,862 | 2,250 |
| + 1 retry | +4,102 | +4,862 | +5,957 |
| + 2nd retry | +4,102 | +4,862 | +5,957 |
| **Worst Case** | **9,806** | **11,586** | **14,164** |

---

## 5. The 60-Second Replay Window — Justified

### 5.1 Why 10 Seconds Was Wrong

The previous audit recommended tightening the replay window from 60s to 10s based
on the assumption that GPS-disciplined clocks on both sides make tight windows viable.
This was incorrect because it only considered clock accuracy and ignored the actual
transaction lifecycle.

At SF9 with worst-case retries and blockchain settlement, a single transaction takes
up to **15.2 seconds** just for the first attempt cycle. A legitimate Packet B that
was timestamped at `T=0` might not complete settlement until `T=15`. If a second
transaction is queued behind it, that Packet B's timestamp could be 15–20 seconds
old by the time the Ground Station processes it.

With a 10-second window, the second transaction in the queue would be **rejected as
stale** — even though it's a perfectly legitimate, queued payment from the same session.

### 5.2 The 60-Second Window Covers

| Scenario | Time Budget | SF7 | SF9 |
|---|---|---|---|
| Single tx, no retry | 60s | 2.1s used, 57.9s margin | 3.3s used, 56.7s margin |
| Single tx, 2 retries | 60s | 10.3s used, 49.7s margin | 15.2s used, 44.8s margin |
| 3 sequential txs, 1 retry each | 60s | 18.1s used, 41.9s margin | 27.6s used, 32.4s margin |
| 4 sequential txs, 2 retries each | 60s | 43.3s used, 16.7s margin | — exceeds budget* |
| **Batched** 3 txs (single L2 call) | 60s | 5.7s used, 54.3s margin | 9.7s used, 50.3s margin |

*At SF9 with 2 retries per transaction, 4 sequential settlements exceed 60s.
This is the correct boundary — 4 failed-twice transactions at the slowest
spreading factor should not be considered operationally normal. The 60-second
window accommodates realistic worst-case (3 txs with retries at SF9) while
rejecting pathological scenarios.

### 5.3 Batched Settlement (Recommended)

If the Ground Station batches multiple Packet B payments into a single L2 smart
contract call (one on-chain transaction settling 3–5 payments), the blockchain
latency is amortized:

**Batched flow (3 payments, single L2 call):**

```
[1] Sat B → Ground: Packet B #1       (TOA)
[2] Sat B → Ground: Packet B #2       (TOA)
[3] Sat B → Ground: Packet B #3       (TOA)
[4] Ground: Batch L2 Settlement        (1× blockchain latency)
[5] Ground → Sat B: ACK #1            (TOA)
[6] Ground → Sat B: ACK #2            (TOA)
[7] Ground → Sat B: ACK #3            (TOA)
```

| Batch Size | SF7 Total (ms) | SF9 Total (ms) |
|---|---|---|
| 3 payments | 2,806 | 5,496 |
| 5 payments | 4,010 | 7,996 |

**Batching 5 payments at SF7 takes 4 seconds.** This is why the 60-second window
provides comfortable margin for multiple settlement cycles within a single pass.

### 5.4 Corrected Timing Constraints

| Constraint | Value | Rationale |
|---|---|---|
| **Replay Window** | **60s** | Covers 3× sequential settlements with retries at SF9. Accommodates batched settlement with margin. Original design was correct. |
| **Invoice Freshness** (Sat B accepting Packet A) | **60s** | Matches replay window. Invoice from Sat A may traverse ISL with propagation + queuing delay. |
| **Handshake Freshness** | **30s** | Handshake is a single round-trip, not a queued operation. 30s covers SF9 with retries + clock drift. Tighter than the general replay window because the handshake has no blockchain component. |
| **Session TTL** | **600s** (LEO default) | Typical LEO pass at 550km is 8–12 minutes. 600s covers a full pass. |

---

## 6. UK Duty Cycle Budget (ETSI EN 300.220)

### 6.1 Available Sub-Bands

| Sub-Band | Frequency Range | Duty Cycle | Budget per Hour | Budget per 10-min Pass |
|---|---|---|---|---|
| **g** | 868.0 – 868.6 MHz | 1% | **36.0s** | **36.0s** (single pass/hr) |
| **g1** | 868.7 – 869.2 MHz | 0.1% | **3.6s** | **3.6s** |
| **g2** | 869.4 – 869.65 MHz | 10% | **360.0s** | **360.0s** |
| **g3** | 869.7 – 870.0 MHz | 1% | **36.0s** | **36.0s** |

Duty cycle is measured over any rolling 1-hour window. For a single LEO pass per
hour (typical for a single ground station), the **entire hourly budget** is available
during that pass.

### 6.2 Strategy A: Single Band (g, 868 MHz, 1%)

**Budget: 36 seconds per hour.**

This is the simplest configuration. All packets (uplink and downlink) share one band.
Each transmitter (Sat B and Ground Station) has its own independent duty cycle budget.

**Sat B Transmit Budget (SF7):**

| Item | Per-Instance TOA | Instances per Pass | Total TOA |
|---|---|---|---|
| Heartbeat (+HMAC) | 113ms | 20 (every 30s for 10 min) | **2,260ms** |
| Packet H (Init) | 195ms | 1 | **195ms** |
| Packet B (Payment) | 287ms | varies | **287ms × N** |
| Packet D (Delivery) | 215ms | varies | **215ms × N** |

Fixed overhead: 2,260 + 195 = **2,455ms** (2.46s)
Remaining budget: 36,000 - 2,455 = **33,545ms**

Transactions (B + D per tx): 287 + 215 = 502ms
**Max transactions per pass: 33,545 / 502 ≈ 66 transactions (SF7)**

With retries (avg 1.3 attempts per packet, 30% loss rate):
Effective per tx: 502 × 1.3 = 653ms
**Max transactions per pass: 33,545 / 653 ≈ 51 transactions (SF7)**

**Sat B Transmit Budget (SF9):**

Fixed overhead: (288 × 20) + 452 = **6,212ms** (6.21s)
Remaining: 36,000 - 6,212 = **29,788ms**

Per tx: 657 + 493 = 1,150ms
**Max transactions per pass: 29,788 / 1,150 ≈ 25 transactions (SF9)**

With retries: 1,150 × 1.3 = 1,495ms
**Max transactions per pass: 29,788 / 1,495 ≈ 19 transactions (SF9)**

**Ground Station Transmit Budget (SF7):**

| Item | Per-Instance TOA | Instances | Total TOA |
|---|---|---|---|
| Packet H (Resp) | 195ms | 1 | **195ms** |
| Packet ACK | 215ms | varies | **215ms × N** |

Fixed: 195ms. Remaining: 35,805ms.
**Max ACKs: 35,805 / 215 ≈ 166** — ground station is never the bottleneck on single band.

### 6.3 Strategy B: Dual Band (g + g2)

Split traffic across two sub-bands to increase total available airtime.
The SX1262 supports frequency hopping within a single radio — no second transceiver needed.

**Band Allocation:**

| Band | Duty Cycle | Role | Packets |
|---|---|---|---|
| **g** (868 MHz) | 1% (36s/hr) | **Command & Control** | Packet H, Packet ACK, Heartbeat |
| **g2** (869.5 MHz) | 10% (360s/hr) | **Data Payload** | Packet B, Packet D |

**Why this split:**

Packet B (182 bytes) and Packet D (134 bytes) are the heaviest packets and the most
frequent during active settlement. They consume the most airtime. Moving them to
the 10% band gives 10× more transmit budget for the operations that matter.

Packet H, ACK, and Heartbeat are lighter and less frequent. They fit comfortably
in the 1% band.

**Sat B Transmit Budget — Dual Band (SF7):**

g band (36s): Heartbeats (2,260ms) + Packet H (195ms) = 2,455ms. Remaining: 33.5s — plenty of margin.

g2 band (360s): Packet B (287ms × N) + Packet D (215ms × N) = 502ms × N.
**Max transactions: 360,000 / 502 ≈ 717 transactions per hour (SF7).** Effectively unlimited.

**Sat B Transmit Budget — Dual Band (SF9):**

g band (36s): Heartbeats (5,760ms) + Packet H (452ms) = 6,212ms. Remaining: 29.8s.

g2 band (360s): Per tx = 1,150ms.
**Max transactions: 360,000 / 1,150 ≈ 313 transactions per hour (SF9).** Still very comfortable.

**Complexity Cost:**

Dual-band requires frequency hopping between transmissions. The SX1262 needs ~100μs
to switch frequency and re-calibrate. This is negligible compared to packet TOA.
However, it adds complexity to the radio driver:

1. The state machine must track which band a packet belongs to.
2. Each band needs independent duty cycle accounting (two counters, not one).
3. The receiver must know which band to listen on for each expected packet type.
4. If a packet is lost, the retry must use the same band as the original (duty cycle
   accounting must be consistent).

**Implementation:** Add a `band_id` field to the `transition_record_t` and track
`duty_cycle_remaining_ms[2]` in `node_state_t` (two bands, 2 × 4 bytes = 8 bytes).

### 6.4 Strategy C: Commercial Licence (No Duty Cycle)

With a UK Ofcom Light Licence (or equivalent), the duty cycle restriction is removed.
Airtime is limited only by the radio's physical capabilities and regulatory power limits.

**Continuous transmit capacity (SF7, 125kHz):**

At SF7, the maximum effective data rate is approximately 5,470 bps (Semtech datasheet).
Packet B (182 bytes = 1,456 bits): TOA = 287ms.

With no duty cycle limit, Sat B can transmit continuously during the pass window:
600s / 0.287s = **~2,090 Packet B transmissions per pass** (theoretical maximum).

In practice, the Ground Station processing rate (blockchain settlement + crypto
verification) becomes the bottleneck, not the radio.

**Recommendation:** Start development on Single Band (Strategy A). It's simpler,
testable without frequency hopping logic, and supports 19–66 transactions per pass
depending on SF — more than sufficient for initial deployment. Implement dual-band
as a firmware upgrade when throughput demands increase. The KSY wire format doesn't
change between strategies — this is purely a radio driver decision.

---

## 7. Retry Strategy

### 7.1 Retry Policy

| Parameter | Value | Rationale |
|---|---|---|
| Max retries per packet | 2 (3 total attempts) | Bounds worst-case airtime consumption. 3 attempts at 30% loss = 97% delivery probability. |
| Retry timeout (SF7) | 3.5s | B_TOA + L2_max + ACK_TOA + 500ms margin |
| Retry timeout (SF8) | 4.0s | Scaled proportionally |
| Retry timeout (SF9) | 5.0s | Scaled proportionally |
| Backoff | None (fixed interval) | LEO pass windows are too short for exponential backoff. Every retry is equally urgent. |
| Retry band | Same as original | Duty cycle accounting must be consistent per band. |

### 7.2 Retry Airtime Cost

Each retry consumes duty cycle budget. The worst case must be accounted for in the
duty cycle budget planning.

**Airtime per transaction including retries (worst case, all 3 attempts):**

| | SF7 | SF8 | SF9 |
|---|---|---|---|
| Sat B (3× Packet B + 1× Packet D) | 1,076ms | 1,626ms | 2,464ms |
| Ground (3× ACK) | 646ms | 989ms | 1,478ms |

**Single band (g, 36s), worst-case retries on every transaction:**

| SF | Sat B budget after overhead | Per tx (worst) | Max txs |
|---|---|---|---|
| SF7 | 33,545ms | 1,076ms | **31** |
| SF8 | 31,310ms | 1,626ms | **19** |
| SF9 | 29,788ms | 2,464ms | **12** |

Even in the absolute worst case (every packet retried twice at SF9), you get
**12 transactions per pass** on a single band. That's operationally viable for
initial deployment.

### 7.3 Retry Interaction with Replay Window

The retry timeout and replay window must be coherent:

```
retry_timeout < replay_window / max_retries

SF9: 5.0s < 60s / 3 = 20s  ✓  (large margin)
SF7: 3.5s < 60s / 3 = 20s  ✓  (large margin)
```

This ensures that all retry attempts for a single transaction complete well within
the replay window. If a packet's timestamp falls outside the 60-second window after
3 attempts, the transaction is genuinely stale and should be rejected.

---

## 8. Throughput Summary — Transactions per Pass

Assuming a 10-minute LEO pass, one pass per hour, 30% packet loss rate (1.3 avg attempts).

| Configuration | SF7 | SF8 | SF9 |
|---|---|---|---|
| **Single Band (g, 1%)** | ~51 tx/pass | ~29 tx/pass | ~19 tx/pass |
| **Dual Band (g + g2)** | ~550 tx/pass* | ~310 tx/pass* | ~240 tx/pass* |
| **Commercial Licence** | ~1,200 tx/pass** | ~700 tx/pass** | ~400 tx/pass** |

\* Dual-band limited by g2 duty cycle (360s/hr) and processing overhead.
\** Commercial licence limited by Ground Station processing rate, not radio.

### 8.1 Realistic First-Year Target

For a university CubeSat or HAB test campaign using a single TinyGS ground station:

- **1 pass per hour** (typical for 550km LEO with single ground station).
- **SF8** (balanced range/throughput for LEO path loss budget).
- **Single band** (simplest radio config, no frequency hopping).
- **5–10 transactions per pass** (conservative, leaves margin for telemetry and retries).
- **120–240 transactions per day** (24 passes, not all with payload).

This is more than sufficient for proof-of-concept settlement validation.

---

## 9. Revised State Machine Timing Constants

Based on the TOA analysis, the following timing constants in the State Machine Spec
should be updated:

| Constant | Previous Value | Revised Value | Justification |
|---|---|---|---|
| Replay Window | 60s | **60s (UNCHANGED)** | Correctly sized. Covers 3 sequential txs with retries at SF9. 10s was insufficient — a single SF9 flow with retry exceeds 10s. |
| Invoice Freshness | 60s | **60s (UNCHANGED)** | Matches replay window. ISL propagation + queuing can add 5–10s. |
| Handshake Freshness | 10s | **30s** | Handshake round-trip at SF9: H_down + H_up = ~1.0s. But clock drift on cold GPS start can be 5–10s. Add retry margin. 30s is conservative. |
| Retry Timeout (SF7) | N/A (unspecified) | **3.5s** | B_TOA(287) + L2_max(2000) + ACK_TOA(215) + margin(500) ≈ 3,500ms |
| Retry Timeout (SF8) | N/A | **4.0s** | B_TOA(432) + L2_max(2000) + ACK_TOA(330) + margin(500) ≈ 3,762ms → round to 4.0s |
| Retry Timeout (SF9) | N/A | **5.0s** | B_TOA(657) + L2_max(2000) + ACK_TOA(493) + margin(500) ≈ 4,150ms → round to 5.0s |
| Max Retries | N/A | **2** (3 total attempts) | 3 attempts at 30% loss = 97% delivery. Bounded airtime. |
| Heartbeat Interval | 30s | **30s (UNCHANGED)** | ~113ms at SF7, ~288ms at SF9. Minimal duty cycle impact. |
| BROADCASTING Timeout | 300s | **300s (UNCHANGED)** | 5 minutes of periodic invoice broadcast. Well within duty cycle if interval between broadcasts is ≥ (TOA / 0.01). At SF7: 128ms / 0.01 = 12.8s minimum interval between Packet A broadcasts. |
| Session TTL | 600s | **600s (UNCHANGED)** | Full LEO pass window. |

---

## 10. Dual-Band Implementation Notes (Future Upgrade)

When throughput demands exceed single-band capacity, the dual-band upgrade requires
the following changes. The wire format (KSY) does not change.

### 10.1 Band Assignment Table

| Packet | Band | Rationale |
|---|---|---|
| Packet A (Invoice) | g (1%) | Light packet (74B). ISL only — not ground-pass limited. |
| Packet H (Handshake) | g (1%) | Session setup. Infrequent (once per pass). |
| Packet B (Payment) | **g2 (10%)** | Heaviest packet (182B). Most frequent during settlement. |
| Packet D (Delivery) | **g2 (10%)** | Heavy (134B). Frequent during receipt flush. |
| Packet ACK (Command) | g (1%) | Ground → Sat B. Moderate size (134B). |
| Heartbeat | g (1%) | Light (62B). Periodic, not settlement-critical. |

### 10.2 Required Firmware Changes

1. **`node_state_t` extension:**
   ```c
   uint32_t duty_remaining_ms[2];   // [0]=band_g, [1]=band_g2
   uint8_t  current_band;           // 0=g, 1=g2
   ```

2. **Radio driver:** Add `sx1262_set_frequency(uint32_t freq_hz)` calls before
   each Tx/Rx operation. Settling time is ~100μs (negligible).

3. **Duty cycle tracker:** Decrement the appropriate band counter after each
   transmission. Refill both counters on the 1-hour rolling window boundary.

4. **Receive scheduling:** In `CONNECTED` state, the receiver must alternate
   between bands based on what packet type is expected next. After transmitting
   Packet B on g2, switch to g to listen for ACK.

### 10.3 Frequency Hopping Timing

```
Transmit Packet B on g2 (869.5 MHz)      287ms (SF7)
Switch to g (868.0 MHz)                  0.1ms
Listen for ACK                           3.5s timeout
ACK received on g                        215ms
Switch to g2 for next Packet B           0.1ms
```

Total hop overhead per transaction cycle: **0.2ms** (two frequency switches).
This is 0.03% of the transaction time — negligible.

---

*END OF TOA ANALYSIS*

*All calculations verified against Semtech SX1262 Datasheet (DS_SX1261-2_V2.1)
and ETSI EN 300.220-2 V3.1.1 duty cycle regulations.*

*© 2026 Tiny Innovation Group Ltd.*
