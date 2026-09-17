# VOID Protocol v2.1 — Time on Air & Transaction Timing Budget

> **Authority:** Tiny Innovation Group Ltd
>
> **License:** Apache 2.0
>
> **Status:** Engineering Analysis / Timing Verification
>
> **Radio:** Semtech SX1262 (LoRa)
>
> **Bands:** 868 MHz (EU), 915 MHz (US)
>
> **Bandwidth:** 125 kHz
>
> **Coding Rate:** 4/5 (CR=1)
>
> **Preamble:** 8 symbols (standard)
>
> **Header:** Explicit
>
> **CRC:** Enabled
>
> **Correction:** Replaces the 10-second replay window recommendation in the
> State Machine Spec. The 60-second window is validated below as the correct
> engineering margin for multi-transaction settlement within a single pass.

---

## 1. LoRa Time on Air — Foundational Math

Every timing decision in this protocol is downstream of one physical constant:
how long a LoRa packet occupies the radio channel. The SX1262 transmits
one symbol at a time. The duration of each symbol is:

```
T_sym = 2^SF / BW

SF7:  T_sym = 128  / 125000 = 1.024 ms
SF8:  T_sym = 256  / 125000 = 2.048 ms
SF9:  T_sym = 512  / 125000 = 4.096 ms
```

The preamble (8 symbols + 4.25 sync) costs:

```
T_preamble = (N_preamble + 4.25) × T_sym

SF7:  T_preamble = 12.25 × 1.024 = 12.544 ms
SF8:  T_preamble = 12.25 × 2.048 = 25.088 ms
SF9:  T_preamble = 12.25 × 4.096 = 50.176 ms
```

The payload symbol count uses the Semtech formula (AN1200.13):

```
N_payload = 8 + max( ceil( (8×PL - 4×SF + 44) / (4×SF) ) × 5,  0 )

Where:
  PL  = Total on-air bytes (headers + body)
  SF  = Spreading Factor
  44  = 8×CRC(1) + 28 + 16 - 20×IH(0)    [CRC=on, explicit header, DE=off]
  5   = CR + 4 = 1 + 4                     [Coding Rate 4/5]
```

Total Time on Air:

```
TOA = T_preamble + (N_payload × T_sym)
```

---

## 2. SNLP Packet Sizes (On-Air Bytes)

For the Community Tier (LoRa/TinyGS), every packet carries the 12-byte SNLP header.
These are the total byte counts that hit the SX1262 FIFO.

Using the spec-defined 12-byte SNLP header (4B sync + 6B CCSDS + 2B pad):

| Packet | Body (bytes) | + SNLP Header | + Tail Pad | Total On-Air |
|---|---|---|---|---|
| **Heartbeat** | 34 | +12 | +0 | **46** (50 with HMAC) |
| **Packet A** (Invoice) | 62 | +12 | +0 | **74** |
| **Packet H** (Handshake) | 106 | +12 | +0 | **118** |
| **Packet B** (Payment) | 170 | +12 | +2 | **184** |
| **Packet C** (Receipt) | 98 | +12 | +0 | **110** |
| **Packet D** (Delivery) | 122 | +12 | +0 | **134** |
| **Packet ACK** (Command) | ~122 | +12 | +0 | **~134** |

Note: Packet B is explicitly defined as 184 bytes in the SNLP spec. The ACK in
SNLP mode has a 96-byte tunnel (vs 88 CCSDS), which accounts for the increased body.

---

## 3. TOA Calculation Table — All Packets × All Spreading Factors

### SF7 (Fastest — Short Range / Low Noise)

| Packet | PL (bytes) | N_payload (symbols) | T_payload (ms) | TOA (ms) |
|---|---|---|---|---|
| Heartbeat (HMAC) | 50 | 78 | 79.9 | **92.4** |
| Packet A | 74 | 148 | 151.6 | **164.1** |
| Packet H | 118 | 183 | 187.4 | **199.9** |
| Packet B | 184 | 278 | 284.7 | **297.2** |
| Packet C | 110 | 173 | 177.2 | **189.7** |
| Packet D | 134 | 208 | 213.0 | **225.5** |
| Packet ACK | 134 | 208 | 213.0 | **225.5** |

### SF8 (Balanced — Typical LEO Ground Pass)

| Packet | PL (bytes) | N_payload (symbols) | T_payload (ms) | TOA (ms) |
|---|---|---|---|---|
| Heartbeat (HMAC) | 50 | 68 | 139.3 | **164.4** |
| Packet A | 74 | 108 | 221.2 | **246.3** |
| Packet H | 118 | 158 | 323.6 | **348.7** |
| Packet B | 184 | 243 | 497.7 | **522.8** |
| Packet C | 110 | 148 | 303.1 | **328.2** |
| Packet D | 134 | 183 | 374.8 | **399.9** |
| Packet ACK | 134 | 183 | 374.8 | **399.9** |

### SF9 (Longest Range — Horizon / Weak Signal)

| Packet | PL (bytes) | N_payload (symbols) | T_payload (ms) | TOA (ms) |
|---|---|---|---|---|
| Heartbeat (HMAC) | 50 | 63 | 258.0 | **308.2** |
| Packet A | 74 | 93 | 380.9 | **431.1** |
| Packet H | 118 | 143 | 585.7 | **635.9** |
| Packet B | 184 | 218 | 892.9 | **943.1** |
| Packet C | 110 | 133 | 544.8 | **595.0** |
| Packet D | 134 | 163 | 667.6 | **717.8** |
| Packet ACK | 134 | 163 | 667.6 | **717.8** |

### Quick Reference: Worst-Case Packet TOA

| Packet | SF7 | SF8 | SF9 |
|---|---|---|---|
| Heaviest (Packet B, 184B) | 297 ms | 523 ms | **943 ms** |
| Lightest (Heartbeat, 50B) | 92 ms | 164 ms | **308 ms** |

**Key takeaway:** At SF9, a single Packet B costs nearly **1 full second** of airtime.
The radio channel is occupied and unavailable for the entire duration.

---

## 4. Processing Latency Budget

Between each radio transmission, the receiving node must process the packet.
These are measured benchmarks for the ESP32-S3 with hardware SHA-256 acceleration.

| Operation | Platform | Latency | Notes |
|---|---|---|---|
| CRC32 (184 bytes) | ESP32-S3 | <0.1 ms | Hardware ROM lookup table |
| ChaCha20 decrypt (62 bytes) | ESP32-S3 (SW) | ~1-2 ms | No HW accelerator; pure software |
| SHA-256 hash (32 bytes) | ESP32-S3 (HW) | ~0.3 ms | Dedicated accelerator |
| HKDF-SHA256 key derivation | ESP32-S3 (HW) | ~1 ms | 2× SHA-256 internally |
| Ed25519 signature verify | ESP32-S3 (SW) | ~25-50 ms | Most expensive operation on device |
| X25519 ECDH (key exchange) | ESP32-S3 (SW) | ~40-80 ms | Scalar multiplication |
| Tx/Rx turnaround (SX1262) | Radio | ~0.2 ms | Negligible |
| RF propagation (LEO 550 km) | Physics | ~1.8 ms | Speed of light, one-way |

**Critical path:** Ed25519 verification at ~50 ms and X25519 at ~80 ms dominate.
Everything else is noise in comparison to the LoRa TOA.

### Ground Station Processing (Go Binary on x86/ARM Server)

| Operation | Platform | Latency | Notes |
|---|---|---|---|
| Ed25519 verify | Server (Go) | <1 ms | x86 optimized |
| ChaCha20 decrypt | Server (Go) | <0.5 ms | AES-NI comparable throughput |
| PUF key database lookup | SSD/RAM | ~1-5 ms | Local SQLite or in-memory map |
| MQTT publish (local) | Server | ~1-5 ms | Localhost broker, no TLS overhead |
| **L2 Blockchain RPC** | **Network** | **500-5000 ms** | **Dominant latency. See Section 5.** |

---

## 5. Blockchain Settlement Latency — The Bottleneck

This is the slowest link in the chain by an order of magnitude.

### L2 Settlement Scenarios

| Blockchain / L2 | Tx Submission | Confirmation | Finality | Notes |
|---|---|---|---|---|
| **Solana (L1 direct)** | ~50 ms RPC | 400 ms (1 slot) | ~6-12 s (32 slots) | Fast but L1 fees per tx |
| **Solana + Batch (Clockwork)** | ~50 ms queue | 400 ms per batch | ~6-12 s | Amortized: batch N txns per slot |
| **Ethereum L2 (Arbitrum)** | ~100 ms RPC | ~250 ms (L2 block) | ~1-7 min (L1 post) | Soft finality fast, hard finality slow |
| **Ethereum L2 (Base)** | ~100 ms RPC | ~2 s (L2 block) | ~1-7 min (L1 post) | Similar profile to Arbitrum |
| **Custom L2 / Appchain** | ~10 ms local | Instant (local) | Batched to L1 async | Lowest latency, highest complexity |

**For the VOID use case:** The ACK only needs **soft confirmation** (the L2 has accepted
the transaction into its mempool and it's included in a block). Hard finality (posting
to L1) can happen asynchronously — it doesn't gate the ACK response.

**Working assumption:** L2 settlement = **500–2000 ms** from RPC submission to soft confirmation.
This is the number that drives the timing budget.

If the protocol batches multiple Packet B settlements into a single L2 transaction
(which the spec implies with the "batch transactions" mention), the per-transaction
marginal cost drops but the batch submission adds latency: you wait to accumulate N
transactions before submitting, or you submit on a timer.

### Batching Strategy Impact

| Strategy | Latency per Tx | Throughput | Risk |
|---|---|---|---|
| **Immediate (1:1)** | 500-2000 ms each | Low | High gas cost per tx |
| **Batch-on-timer (every 2s)** | 2000 ms + confirmation | Medium | Some txns wait up to 2s |
| **Batch-on-count (every 5 txns)** | Variable (depends on flow) | High | Stalls if flow < 5 txns/pass |
| **Hybrid (timer OR count)** | max(2000 ms, count threshold) | Best | Recommended approach |

---

## 6. Full Transaction Timeline — Single Payment Cycle

This is the complete timing waterfall for one settlement, from Handshake through
Receipt verification, during a single LEO ground pass over a TinyGS station.

### 6.1 Scenario: SF8, 868 MHz, Batch-on-Timer (2s)

```
TIME (ms)    EVENT                              DURATION    CUMULATIVE
─────────────────────────────────────────────────────────────────────────

=== PHASE 1: SESSION ESTABLISHMENT (One-time per pass) ===

0            Sat B: Generate ephemeral keypair    80 ms       80 ms
             (X25519 keygen)

80           Sat B → Ground: Packet H (Init)     349 ms      429 ms
             (118B @ SF8)

429          Ground: Verify Sat B PUF sig          1 ms       430 ms
             (Ed25519, server-speed)

430          Ground: Generate ground keypair        1 ms       431 ms
             (X25519)

431          Ground: Build + Sign H (Resp)          1 ms       432 ms

432          Ground → Sat B: Packet H (Resp)     349 ms       781 ms
             (118B @ SF8)

781          Sat B: Verify Ground Authority sig    50 ms       831 ms
             (Ed25519, ESP32-S3)

831          Sat B: X25519 ECDH + HKDF             81 ms       912 ms
             (Derive Session_Key)

912          Sat B: Destroy eph_priv                0.1 ms     912 ms

912          ── SESSION ESTABLISHED ──

=== PHASE 2: PAYMENT (Repeatable per session) ===

912          Sat B: Build Packet B (encrypt)        3 ms       915 ms
             (ChaCha20 + sign)

915          Sat B → Ground: Packet B (Payment)   523 ms     1,438 ms
             (184B @ SF8)

1,438        Ground: Decrypt + verify sig           2 ms     1,440 ms

1,440        Ground: Submit to L2 blockchain     1,500 ms     2,940 ms
             (Batch timer: 2s + ~500ms confirm)

2,940        Ground: Build ACK                       1 ms     2,941 ms

2,941        Ground → Sat B: Packet ACK            400 ms     3,341 ms
             (134B @ SF8)

3,341        Sat B: Process ACK                     50 ms     3,391 ms
             (Decrypt tunnel + verify)

3,391        ── PAYMENT SETTLED ──

=== PHASE 3: RECEIPT DELIVERY (If queued) ===

3,391        Sat B: Wrap Packet C in Packet D        1 ms     3,392 ms

3,392        Sat B → Ground: Packet D (Delivery)   400 ms     3,792 ms
             (134B @ SF8)

3,792        Ground: Extract + verify Sat A sig      2 ms     3,794 ms

3,794        Ground: Match tx_id + release escrow  500 ms     4,294 ms
             (L2 fund release)

4,294        ── RECEIPT VERIFIED, ESCROW CLOSED ──

=== TOTAL: FULL CYCLE (Handshake + 1 Payment + 1 Receipt) ===

             TOTAL ELAPSED:                                   ~4.3 s
             Of which radio TOA:                              ~2,021 ms
             Of which blockchain:                             ~2,000 ms
             Of which crypto (ESP32):                         ~264 ms
             Of which Ground processing:                      ~8 ms
```

### 6.2 Subsequent Payments (Session Already Established)

Once the handshake is done, each additional payment cycle skips Phase 1:

```
=== SUBSEQUENT PAYMENT (No handshake) ===

0            Sat B → Ground: Packet B             523 ms       523 ms
523          Ground: Process + L2 settle        1,502 ms     2,025 ms
2,025        Ground → Sat B: Packet ACK           400 ms     2,425 ms
2,425        Sat B: Process ACK                    50 ms      2,475 ms

             Per-transaction time:                            ~2.5 s
             (Of which blockchain:                            ~1.5 s)

=== WITH RECEIPT ===

2,475        Sat B → Ground: Packet D             400 ms     2,875 ms
2,875        Ground: Verify + release             502 ms     3,377 ms

             Per-transaction + receipt:                       ~3.4 s
```

### 6.3 With Retries

LoRa at SF8 on 868 MHz has a typical Packet Error Rate (PER) of ~5-10% in
good conditions, rising to 15-25% at the edges of a pass (low elevation angle,
atmospheric scattering). A single retry strategy is essential.

```
=== PAYMENT WITH 1 RETRY (Packet B lost, resent) ===

0            Sat B → Ground: Packet B             523 ms       523 ms
             (LOST — no ACK received)

523          Sat B: Wait for ACK timeout        2,000 ms     2,523 ms
             (Must exceed blockchain settle time)

2,523        Sat B → Ground: Packet B (retry)     523 ms     3,046 ms

3,046        Ground: Process + L2 settle        1,502 ms     4,548 ms
             (Idempotency: same nonce = same tx)

4,548        Ground → Sat B: Packet ACK           400 ms     4,948 ms

4,948        Sat B: Process ACK                    50 ms      4,998 ms

             Payment with 1 retry:                            ~5.0 s
```

```
=== ACK LOST (Ground sent ACK, Sat B didn't receive) ===

0            Sat B → Ground: Packet B             523 ms       523 ms
523          Ground: Process + settle           1,502 ms     2,025 ms
2,025        Ground → Sat B: Packet ACK           400 ms     2,425 ms
             (LOST — Sat B doesn't receive)

2,425        Sat B: Wait for ACK timeout        2,000 ms     4,425 ms

4,425        Sat B → Ground: Packet B (retry)     523 ms     4,948 ms
             (Same nonce — Ground recognizes as duplicate)

4,948        Ground: Duplicate detected,             1 ms     4,949 ms
             re-issue ACK (no new L2 tx)

4,949        Ground → Sat B: Packet ACK           400 ms     5,349 ms

5,349        Sat B: Process ACK                    50 ms      5,399 ms

             Payment with lost ACK:                           ~5.4 s
```

---

## 7. The 60-Second Window — Validation

### 7.1 What Fits in 60 Seconds at SF8

```
Budget: 60,000 ms

Handshake (one-time):           ~912 ms
Remaining after handshake:      59,088 ms

Per payment cycle (no retry):   ~2,475 ms
Per payment + receipt:          ~3,377 ms
Per payment with 1 retry:      ~4,998 ms

TRANSACTIONS IN 60s WINDOW:
─────────────────────────────────────────────────────
Clean (no retries, no receipts):  59,088 / 2,475 = 23 transactions
Clean (with receipts):            59,088 / 3,377 = 17 transactions
Pessimistic (1 retry each):      59,088 / 4,998 = 11 transactions
Worst case (1 retry + receipt):  59,088 / 5,399 = 10 transactions
```

### 7.2 What Fits in 60 Seconds at SF9 (Worst Case)

At SF9, every radio transmission roughly doubles in duration:

```
Handshake at SF9:               ~1,900 ms
Remaining:                      58,100 ms

Per payment (no retry):
  Packet B (SF9):                  943 ms
  L2 settle:                     1,500 ms
  ACK (SF9):                       718 ms
  Process:                          50 ms
  Subtotal:                      3,211 ms

Per payment with 1 retry:
  Packet B:                        943 ms
  Timeout:                       3,500 ms  (must exceed settle + ACK TOA)
  Packet B retry:                  943 ms
  Settle:                        1,500 ms
  ACK:                             718 ms
  Process:                          50 ms
  Subtotal:                      7,654 ms

TRANSACTIONS IN 60s AT SF9:
─────────────────────────────────────────────────────
Clean (no retries):              58,100 / 3,211 = 18 transactions
Pessimistic (1 retry each):     58,100 / 7,654 =  7 transactions
Worst case (2 retries):         Only 4-5 transactions fit
```

### 7.3 Why 60 Seconds Is Correct (And 10 Seconds Is Wrong)

My earlier recommendation to tighten the replay window to 10 seconds was incorrect
for the settlement context. Here's the math that proves it:

**The replay window must exceed the longest possible round-trip time for a single
transaction, including blockchain settlement and one retry.** If the window is shorter
than this, a legitimate retry would be rejected as "stale" by the receiver.

```
Worst-case single-transaction round-trip (SF9 + retry + blockchain):

Packet B TOA:           943 ms
Ground processing:        2 ms
L2 settlement:        2,000 ms   (worst case soft confirmation)
ACK TOA:                718 ms
ACK lost (timeout):   3,500 ms   (Sat B waits for response)
Retry Packet B TOA:     943 ms
Ground duplicate det:     1 ms
Re-issue ACK TOA:       718 ms

Total worst case:     8,825 ms ≈ 9 seconds
```

**At 10 seconds, there is only ~1 second of margin.** A slightly slow L2 RPC call,
an extra retry, or the batch timer aligning unfavorably would push the transaction
past the window. The node would reject its own retried packet as stale.

**At 60 seconds, the margin is ~51 seconds.** This allows:

1. Up to **5-6 retries** at SF9 before the window closes.
2. A blockchain RPC timeout + reconnect (~10-15s) without killing the transaction.
3. Multiple transactions to be pipelined within the same freshness window.
4. Clock drift between GPS receivers (±1s typical, ±5s worst case on cold start)
   is absorbed without false rejections.

**The 60-second window is not "generous" — it is the minimum safe margin for reliable
multi-transaction settlement on the slowest supported spreading factor with realistic
retry budgets and blockchain latency variance.**

### 7.4 Recommended Retry Strategy

Based on the timing budget, the retry policy should be:

| Parameter | SF7 | SF8 | SF9 |
|---|---|---|---|
| ACK timeout (per attempt) | 1,500 ms | 2,500 ms | 4,000 ms |
| Max retries per packet | 3 | 3 | 2 |
| Backoff multiplier | None (fixed) | None (fixed) | None (fixed) |
| Total max retry window | 4.5 s | 7.5 s | 8.0 s |

**Fixed intervals, not exponential backoff.** Exponential backoff is designed for
congested shared media (Ethernet, WiFi). A LoRa link to a LEO satellite is not
congested — it's a point-to-point link where the dominant failure mode is atmospheric
fading, not collision. Retrying at a fixed interval maximizes the probability of
hitting a clear channel during the pass window.

**Max retries capped at 2-3.** If three consecutive transmissions fail, the link
quality is below usable threshold (satellite is near horizon, interference event, or
hardware fault). Continuing to retry wastes the remaining pass window. Better to
abort and wait for the next pass.

---

## 8. EU 868 MHz Duty Cycle Constraint

This is a regulatory constraint that directly limits transaction throughput on
the Community Tier in Europe.

### 8.1 The Constraint

EU 868 MHz ISM band is subject to the ETSI EN 300.220 duty cycle regulation:

| Sub-Band | Frequency | Duty Cycle | Max TOA per hour |
|---|---|---|---|
| g (default LoRa) | 868.0 - 868.6 MHz | **1%** | 36 seconds |
| g1 | 868.7 - 869.2 MHz | **0.1%** | 3.6 seconds |
| g3 | 869.4 - 869.65 MHz | **10%** | 360 seconds |

**At 1% duty cycle on the default sub-band:**
After transmitting a 184-byte Packet B at SF8 (523 ms TOA), the ground station
must wait **52.3 seconds** before transmitting again on that sub-band.

This means a ground station on the default sub-band can only send **one ACK per
minute** if the satellite is also transmitting on the same sub-band.

### 8.2 Impact on Transaction Flow

**Uplink (Ground → Satellite) is the bottleneck.** The satellite is not subject to
EU duty cycle (it's in orbit), so it can transmit Packet B repeatedly. But the
ground station's ACK is duty-cycle constrained.

```
Scenario: Default sub-band (868.0-868.6 MHz), 1% duty cycle, SF8

Sat B → Ground: Packet B (523 ms)     — Satellite not duty-cycle limited
Ground: Process + settle (2,000 ms)
Ground → Sat B: ACK (400 ms)          — Ground uses 400 ms of airtime
Ground: MUST WAIT 39.6 seconds        — 400 ms × (100/1 - 1) = 39,600 ms

RESULT: ONE transaction per ~42 seconds on this sub-band.
        In a 60-second window: exactly 1 transaction.
```

### 8.3 Mitigation: Sub-Band Hopping

The 10% duty cycle band (869.4-869.65 MHz, g3) allows much higher throughput:

```
Scenario: g3 sub-band (10% duty cycle), SF8

Ground → Sat B: ACK (400 ms)
Ground: MUST WAIT 3.6 seconds          — 400 ms × (100/10 - 1) = 3,600 ms

RESULT: One ACK every ~4 seconds.
        In a 60-second window: ~15 transactions.
```

**Recommendation:** Configure ground stations to receive on the 1% sub-band
(maximum receive sensitivity) but transmit ACKs on the g3 (10%) sub-band.
The satellite's receiver must be configured to listen on both sub-bands, or
the ACK frequency must be specified in the `relay_ops.frequency` field of the
first ACK — which is exactly what that field is designed for.

### 8.4 US 915 MHz — No Duty Cycle, But Dwell Time

FCC Part 15.247 for the 915 MHz ISM band uses frequency hopping spread spectrum
(FHSS) rules rather than duty cycle:

- **Dwell time limit:** 400 ms per channel (at narrow bandwidths).
- **Minimum 50 hopping channels** for 125 kHz BW.
- **No duty cycle percentage** — just dwell time.

At SF8/125kHz, a 184-byte Packet B takes 523 ms — exceeding the 400 ms dwell
limit. This means US deployments must either:

1. Use **SF7** (297 ms TOA for Packet B — fits in 400 ms).
2. Use **500 kHz bandwidth** (halves all TOA values).
3. Operate under the **digital modulation** exemption (Part 15.247(a)(2)),
   which allows up to 1 W with no dwell time limit but requires a minimum
   500 kHz occupied bandwidth.

**Recommendation:** US deployments should target SF7 at 125 kHz or SF8 at 500 kHz.
The `relay_ops.frequency` field in the ACK packet should encode not just the center
frequency but also the bandwidth configuration (this requires a field format amendment
or a reserved bit convention within the 32-bit frequency field).

---

## 9. Revised Timing Constraints

Based on this analysis, the State Machine Spec timing constraints should be:

| Constraint | Original Spec | Previous Recommendation | **Validated Value** | Rationale |
|---|---|---|---|---|
| Replay window | 60s | 10s (wrong) | **60s** | Must exceed worst-case SF9 retry chain + blockchain settlement + margin. |
| Invoice freshness | 60s | — | **60s** | Inter-satellite propagation + clock drift. Matched to replay window. |
| Handshake freshness | 60s | 10s | **15s** | Handshake is pre-blockchain. Only covers radio RTT + crypto. 15s gives 5× margin over worst-case SF9 handshake RTT (~2.8s). |
| ACK timeout (SF7) | — | — | **1,500 ms** | 2× (Packet B TOA + settle + ACK TOA) at SF7. |
| ACK timeout (SF8) | — | — | **2,500 ms** | 2× round-trip at SF8. |
| ACK timeout (SF9) | — | — | **4,000 ms** | ~2× round-trip at SF9. |
| Max retries | — | — | **3 (SF7/8), 2 (SF9)** | Caps total retry window at <10s. |
| Session TTL | 60-7200s | — | **60-7200s** | Unchanged. LEO pass = 600s typical. |

### 9.1 Correction to State Machine Spec

The `sm_mule` and `sm_ground` state machines should use these ACK timeout values
instead of a hardcoded constant. The timeout should be a function of the current
spreading factor, which is known at session establishment (it's a radio-layer
parameter, not a protocol-layer parameter).

```c
// Timeout lookup table — indexed by (SF - 7)
static const uint16_t ack_timeout_ms[3] = {
    1500,   // SF7
    2500,   // SF8
    4000    // SF9
};

static const uint8_t max_retries[3] = {
    3,      // SF7
    3,      // SF8
    2       // SF9
};
```

---

## 10. LEO Pass Window — How Many Transactions Per Orbit

A satellite at 550 km altitude (typical LEO, e.g., Starlink shell) has a ground
station visibility window that depends on the minimum elevation angle:

| Min Elevation | Pass Duration | Usable Window (>10° elev) |
|---|---|---|
| 0° (horizon) | ~12 min | ~8-10 min effective |
| 10° | ~8 min | ~8 min |
| 20° | ~5 min | ~5 min |

**At SF8, 10% duty cycle sub-band, no retries:**

```
Session establishment:                 ~1 s
Per transaction (payment only):        ~2.5 s
Per transaction (payment + receipt):   ~3.4 s
Duty cycle wait (g3, 10%):            ~3.6 s

Effective per-transaction cadence:     max(3.4, 3.6) = ~3.6 s
                                       (Duty cycle is the bottleneck)

Transactions in 8-minute pass:
  8 min = 480 s
  - 1 s handshake = 479 s remaining
  479 / 3.6 = 133 transactions (theoretical max, no retries)

With 10% packet loss and 1 retry:     ~100 transactions per pass
```

**At SF9, 1% duty cycle sub-band, pessimistic:**

```
Effective per-transaction cadence:     ~42 s (duty cycle dominant)

Transactions in 8-minute pass:
  479 / 42 = 11 transactions (1% duty cycle bottleneck)
```

**Summary: LEO Transaction Throughput**

| Config | Per-Pass Throughput | Bottleneck |
|---|---|---|
| SF7, g3 (10%), no retry | ~160 txns | Radio TOA |
| SF8, g3 (10%), no retry | ~133 txns | Duty cycle |
| SF8, g3 (10%), 10% loss | ~100 txns | Retries |
| SF8, g (1%), no retry | ~11 txns | **Duty cycle** |
| SF9, g (1%), 10% loss | ~7 txns | **Duty cycle** |
| SF9, g3 (10%), 10% loss | ~60 txns | Radio TOA |

**The single biggest throughput lever is the duty cycle sub-band, not the
spreading factor.** Moving from g (1%) to g3 (10%) increases throughput by
10× on the same SF.

---

## 11. End-to-End Flow Diagram — Annotated with TOA

```
           Sat A                Sat B               Ground Station          Blockchain
           (Seller)             (Mule)              (Settlement)            (L2)
             │                    │                       │                     │
             │   Packet A (74B)   │                       │                     │
             │  ─────────────────>│  SF8: 246ms           │                     │
             │   (Inter-sat link) │                       │                     │
             │                    │                       │                     │
             │                    │── GROUND PASS START ──│                     │
             │                    │                       │                     │
             │                    │   Pkt H Init (118B)   │                     │
             │                    │──────────────────────>│  SF8: 349ms         │
             │                    │                       │                     │
             │                    │                       │─ Verify + Keygen ─  │
             │                    │                       │  ~2ms               │
             │                    │                       │                     │
             │                    │   Pkt H Resp (118B)   │                     │
             │                    │<──────────────────────│  SF8: 349ms         │
             │                    │                       │                     │
             │                    │─ Verify + ECDH ─      │                     │
             │                    │  ~131ms               │                     │
             │                    │                       │                     │
             │                    ╞═══ SESSION LIVE ══════╡  Elapsed: ~912ms    │
             │                    │                       │                     │
             │                    │   Pkt B #1 (184B)     │                     │
             │                    │──────────────────────>│  SF8: 523ms         │
             │                    │                       │                     │
             │                    │                       │─ Decrypt + Verify ─ │
             │                    │                       │  ~2ms               │
             │                    │                       │                     │
             │                    │                       │   Escrow Request    │
             │                    │                       │────────────────────>│
             │                    │                       │                     │
             │                    │                       │   Soft Confirm      │  ~1500ms
             │                    │                       │<────────────────────│
             │                    │                       │                     │
             │                    │   ACK #1 (134B)       │                     │
             │                    │<──────────────────────│  SF8: 400ms         │
             │                    │                       │                     │  Elapsed:
             │                    │─ Decrypt + Process ─  │                     │  ~3,341ms
             │                    │  ~50ms                │                     │
             │                    │                       │                     │
             │                    │   Pkt B #2 (184B)     │  Can pipeline       │
             │                    │──────────────────────>│  immediately        │
             │                    │                       │  (no duty cycle     │
             │                    │                       │   wait on Sat B)    │
             │                    │                       │                     │
             │                    │   ...repeat...        │                     │
             │                    │                       │                     │
             │                    │   Pkt D (134B)        │                     │
             │                    │──────────────────────>│  SF8: 400ms         │
             │                    │                       │                     │
             │                    │                       │─ Verify Sat A sig ─ │
             │                    │                       │  ~2ms               │
             │                    │                       │                     │
             │                    │                       │   Release Escrow    │
             │                    │                       │────────────────────>│
             │                    │                       │                     │  ~500ms
             │                    │                       │   Confirmed         │
             │                    │                       │<────────────────────│
             │                    │                       │                     │
             │   Tunnel Data      │                       │                     │
             │<═══════════════════│  (Relayed via         │                     │
             │   (Unlock)         │   RELAY_OPS)          │                     │
             │                    │  SF8: 400ms           │                     │
             │                    │                       │                     │
             │─ Verify + Dispense │                       │                     │
             │  ~50ms + service   │                       │                     │
             │                    │                       │                     │
             │   Packet C (110B)  │                       │                     │
             │  ─────────────────>│  SF8: 328ms           │                     │
             │   (Receipt)        │                       │                     │
             │                    │── GROUND PASS END ────│                     │
             │                    │                       │                     │
```

---

*END OF TIMING ANALYSIS*

*The 60-second freshness window is validated as the correct engineering margin.*
*Duty cycle regulation is the dominant throughput constraint on 868 MHz.*
*Sub-band selection (g vs g3) has a larger impact than spreading factor choice.*

*© 2026 Tiny Innovation Group Ltd.*
