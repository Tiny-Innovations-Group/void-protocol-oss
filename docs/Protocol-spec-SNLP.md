# 🛰️ Void Protocol Specification (v2.1) - SNLP Edition

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> **Authority:** Tiny Innovation Group Ltd
>
> **Standard:** Space Network Layer Protocol (SNLP)
>
> **License:** Apache 2.0
>
> **Status:** Production-Ready / TRL 6
>
> **Endianness:** SNLP Header (Big-Endian) | Payload (Little-Endian)
>
> **Optimization:** 64-bit machine cycle alignment
>
> **Metric:** 192-byte Packet B Footprint (VOID-114B: body 178 + `_tail_pad[4]`, 3×64 cache-line clean)
>
> **Target:** 32/64-bit Hardware (Heltec LoRa V3 / ESP32-S3)
>
> **Security:** Ed25519 Signing + SHA-256 (hardware-accelerated) — `enc_payload` is **plaintext** in the default SNLP build
>
> **Architecture:** Plaintext Store-and-Forward
>
> **Primary Use Case:** LoRa LEO Satellites, High-Altitude Balloons

**NOTE:** Data sent and received via TinyGS must be public and not encrypted; it may carry identifiable Ed25519 signatures. This is a legal constraint of amateur-band operation (FCC Part 97 / ITU §25) and is deliberately different from the CCSDS tier, which runs `enc_payload` under ChaCha20.

## 1. Overview & Rationale

The Space Network Layer Protocol (SNLP) is a lightweight, high-reliability framing standard designed for **asynchronous, low-bandwidth radio links**. While the CCSDS header is the gold standard for licensed commercial bands (S-Band/X-Band), SNLP is optimized for the **ISM bands (433MHz / 868MHz / 915MHz)** where LoRa operates.

### 1.1 Key Advantages

* **32/64-bit Cycle Optimization:** The SNLP header is **14 bytes** (`sync_word[4] + ccsds[6] + align_pad[4]`). This is the only header size that maintains mod-8 congruence with the 6-byte CCSDS header, which is the necessary condition for both tiers to share a single body struct layout. See [`VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md`](VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md) for the full alignment proof across all packet types.
* **Zero-Overhead Parsing:** The SNLP header prepends a 4-byte software sync word to a standard 6-byte CCSDS header. This allows the C++ codebase to parse both Community and Enterprise traffic utilizing the exact same memory-mapping structs.
* **Extreme Reliability Sync:** Uses a 32-bit Sync Word (`0x1D01A5A5`) to allow the Ground Station Bouncer to instantly filter out noise and other satellite telemetry on crowded amateur LoRa networks (e.g., Norby, Polytech).

### 1.2 Architectural Integrity

The Void Protocol facilitates a settlement layer for trustless Machine-to-Machine (M2M) transactions between orbital assets. Under SNLP the payload runs in plaintext to comply with amateur-band encryption bans; authenticity is preserved by Ed25519 signatures over every payment frame.

1. **Sat A (Seller):** Broadcasts an "Invoice" (Packet A).
2. **Sat B (Buyer/Mule):** Captures Packet A, encapsulates it with payment intent, signs it (Packet B), and facilitates the uplink via a community ground station.
3. **Ground Station (TinyGS / SatNOGS / Bouncer):** Settlement Authority. Validates Ed25519 signatures, executes L2 Smart Contracts, and issues the "Unlock" command.

### 1.3 Cryptographic Primitives & Hardware Rationale

- **SHA-256:** Utilised for all hashing and key derivation. Selected for the dedicated hardware acceleration engines present in the **ESP32-S3** and **ARM Cortex-M** architectures, ensuring high-speed processing with minimal power consumption during brief orbital windows.
- **Ed25519:** The sole authenticity primitive under default SNLP. Signatures are 64 bytes and verify in ~0.2 ms on ESP32-S3. Covers `header + body[0..pre_sig]` for Packet B — see §4 for the exact signed region.
- **ChaCha20 (optional, non-default):** Available only when compiled with `VOID_ENCRYPT_SNLP` for licensed-band SNLP deployments. NOT engaged on amateur-band TinyGS / HAB flights. See §4.2 for the nonce derivation when this path is enabled.
- **The 86-character key rule:** If ChaCha20 is engaged, users must provide an 86-character passphrase. This length ensures approximately 258 bits of entropy, which saturates the 256-bit key space of the ChaCha20 cipher.

---

## 2. The SNLP Primary Header (14 Bytes)

*This 14-byte structure acts as a "Universal Adapter." Once the 4-byte Sync Word is stripped, the next 6 bytes are fully compliant with CCSDS 133.0-B-2. The final 4 bytes serve as an alignment buffer. The 14-byte size is chosen because `14 ≡ 6 (mod 8)`, which matches the CCSDS header congruence — this is the only way for both tiers to share a single body layout while preserving 64-bit alignment of body fields. See VOID_114 decision doc.*

| Offset | Bit Offset | Field Name | Size | Value / Description |
| --- | --- | --- | --- | --- |
| **00-03** | `0 - 31` | **`sync_word`** | 32 bits | `0x1D01A5A5` (Identifies the VOID packet in noisy ISM bands) |
| **04** | `32 - 34` | `version` | 3 bits | `000` (Matches CCSDS Version 1) |
|  | `35` | `type` | 1 bit | `0` = Telemetry, `1` = Command |
|  | `36` | `sec_hdr_flag` | 1 bit | `1` = Present (Timestamp included) |
| **04-05** | `37 - 47` | `network_id` | 11 bits | Replaces CCSDS APID. (e.g., `0x01` for tinyGS, `0x02` for SatNOGS) |
| **06-07** | `48 - 49` | `seq_flags` | 2 bits | `11` = Unsegmented (Future proofs for fragmentation) |
|  | `50 - 63` | `seq_count` | 14 bits | Rolling counter (0-16383) |
| **08-09** | `64 - 79` | `length` | 16 bits | Length of the payload (excluding this 14-byte header) |
| **10-13** | `80 - 111` | `align_pad` | 32 bits | `0x00000000` (alignment buffer — preserves body-layout parity with CCSDS mod-8 congruence) |

### 2.1 network_id Naming Domain & Global Identity Extension

The SNLP header repurposes the 11 bits formerly used for CCSDS APID as `network_id` (e.g., `0x01` for TinyGS, `0x02` for SatNOGS). This field carries **ground-station routing hints** for the community relay network — not orbital asset identity.

Global Web3 Identity is carried by the `sat_id` (32-bit) field in every packet body, matching the CCSDS tier's extension convention (see [Protocol-spec-CCSDS.md §4.1](Protocol-spec-CCSDS.md)). This allows over 4.2 billion sovereign orbital identities while preserving compliance with **CCSDS 133.0-B-2 Section 2.1.1**, which permits extending the naming domain via mission-specific payload fields when the 11-bit APID space is exhausted.

---

## 3. Packet A: "The Community Invoice" (80 Bytes — VOID-114B)

*Broadcast by Sat A. Unsigned public offer of service. Offsets below use the **14-byte** SNLP header confirmed by VOID-114.*

**VOID-114B CHANGE:** Same 66-byte body as the CCSDS tier (`_pad_head[2]` + fields + `_pre_crc[2]` + `crc32`). Every critical field (`epoch_ts`, `pos_vec`, `vel_vec`, `sat_id`, `amount`, `crc32`) lands on its natural alignment boundary under the 14-byte SNLP header (6 ≡ 14 mod 8). See [`VOID_114B_BODY_ALIGNMENT_2026-04-14.md`](VOID_114B_BODY_ALIGNMENT_2026-04-14.md).

| Offset    | Field           | Type     | Size | Description                                                                   |
| --------- | --------------- | -------- | ---- | ----------------------------------------------------------------------------- |
| **00-13** | **SNLP Header** | —        | 14B  | **Big-Endian** Sync Word + CCSDS fields + align_pad (network_id = Sat A)      |
| **14-15** | `_pad_head`     | `u8[2]`  | 2B   | VOID-114B alignment pad (keeps `u64` head 8-aligned)                          |
| **16-23** | `epoch_ts`      | `u64`    | 8B   | **Little-Endian** Unix Timestamp / Replay Protection ✅                        |
| **24-47** | `pos_vec`       | `f64[3]` | 24B  | **Little-Endian** GPS Vector (X, Y, Z in IEEE 754 Double) ✅                   |
| **48-59** | `vel_vec`       | `f32[3]` | 12B  | **Little-Endian** Velocity Vector (dX, dY, dZ in IEEE 754) ✅                  |
| **60-63** | `sat_id`        | `u32`    | 4B   | **Little-Endian** Unique Seller Identifier ✅                                  |
| **64-71** | `amount`        | `u64`    | 8B   | **Little-Endian** Cost in lowest denomination (e.g., Satoshi) ✅               |
| **72-73** | `asset_id`      | `u16`    | 2B   | **Little-Endian** Currency Type ID (1=USDC)                                   |
| **74-75** | `_pre_crc`      | `u8[2]`  | 2B   | VOID-114B alignment pad (keeps `crc32` 4-aligned)                             |
| **76-79** | `crc32`         | `u32`    | 4B   | **Little-Endian** Integrity Checksum ✅                                        |

---

## 4. Packet B: "The Community Payment" (192 Bytes — VOID-114B)

*Encapsulated payment intent sent by Sat B to the tinyGS Network. Offsets below use the **14-byte** SNLP header confirmed by VOID-114.*

**VOID-110 CHANGE:** The 4-byte wire `nonce` field has been **removed**. The ChaCha20 nonce is now derived deterministically from fields already present in the packet. See §4.2 below.

**VOID-114B CHANGE:** The body has grown from 166 to 178 bytes (174 payload + 4-byte `_tail_pad`). The `_pad_head[2]`, `_pre_sat[2]`, and `_pre_sig[4]` slots place every critical field on its natural alignment boundary under the 14-byte SNLP header (6 ≡ 14 mod 8). The `_tail_pad[4]` brings the frame total to **192 bytes (÷64 ✅ cache line)** — 192 = 3×64 is a clean multiple of the Xtensa LX7 cache line so the SPI DMA burst to the SX1262 never crosses a word boundary. See [`VOID_114B_BODY_ALIGNMENT_2026-04-14.md`](VOID_114B_BODY_ALIGNMENT_2026-04-14.md).

| Offset | Field | Size | Description |
| --- | --- | --- | --- |
| **00-13** | **SNLP Header** | 14B | **32-bit Sync Word** + **6-byte CCSDS Structure** + **4-byte Align Pad** |
| **14-15** | `_pad_head` | 2B | VOID-114B alignment pad |
| **16-23** | `epoch_ts` | 8B | Little-Endian millisecond Unix timestamp. Strictly monotonic. ✅ |
| **24-47** | `pos_vec` | 24B | Sat B Position (GPS Double Vector). ✅ |
| **48-109** | **`enc_payload`** | 62B | **Inner Invoice** — plaintext under SNLP for amateur-band compliance. |
| **110-111** | `_pre_sat` | 2B | VOID-114B alignment pad |
| **112-115** | `sat_id` | 4B | Sat B ID (Mule ID). Low 4 bytes of the derived nonce. ✅ |
| **116-119** | `_pre_sig` | 4B | VOID-114B alignment pad |
| **120-183** | `signature` | 64B | **Ed25519 Signature** — covers offsets 00–119. 8-byte aligned ✅ |
| **184-187** | `global_crc` | 4B | Global Packet Integrity Check. ✅ |
| **188-191** | `_tail_pad` | 4B | VOID-114B frame-total alignment pad (192 ÷64 ✅ cache line) |

### 4.2 Nonce Derivation (VOID-110)

SNLP transmits `enc_payload` in **plaintext** by default to comply with amateur-band encryption bans (FCC Part 97, ITU §25). The ChaCha20 path is therefore not engaged in the default SNLP build, and the nonce derivation below applies only when the firmware is explicitly compiled with `VOID_ENCRYPT_SNLP` (non-default, typically reserved for licensed-band SNLP deployments).

When encryption IS engaged, the 12-byte ChaCha20 nonce is derived identically to the CCSDS tier:

```
nonce[12] = sat_id[4] || epoch_ts[8]       // both little-endian
```

See [Protocol-spec-CCSDS.md §3.2](Protocol-spec-CCSDS.md) for the full uniqueness proof and the monotonic-epoch guardrail requirements. SNLP-tier satellites that engage the encrypted path MUST implement the same NVS persistence and GPS-gate as CCSDS satellites.

### 4.3 Inner Invoice Payload (62 Bytes)

_Plaintext on the wire under SNLP — the gateway reads this directly from `enc_payload` (no decryption required in default SNLP mode). Byte-identical to the CCSDS tier — see [Protocol-spec-CCSDS.md §3.3](Protocol-spec-CCSDS.md) — so a single buyer implementation works across tiers._

| Offset    | Field      | Type     | Size | Description                                  |
| --------- | ---------- | -------- | ---- | -------------------------------------------- |
| **00-07** | `epoch_ts` | `u64`    | 8B   | **Little-Endian** Original Invoice Timestamp |
| **08-31** | `pos_vec`  | `f64[3]` | 24B  | **Little-Endian** Sat A Position Vector      |
| **32-43** | `vel_vec`  | `f32[3]` | 12B  | **Little-Endian** Sat A Velocity Vector      |
| **44-47** | `sat_id`   | `u32`    | 4B   | **Little-Endian** Seller Identifier          |
| **48-55** | `amount`   | `u64`    | 8B   | **Little-Endian** Transaction Value          |
| **56-57** | `asset_id` | `u16`    | 2B   | **Little-Endian** Currency ID                |
| **58-61** | `crc32`    | `u32`    | 4B   | **Little-Endian** Internal Invoice Checksum  |

The buyer constructs this by echoing the corresponding fields of the received Packet A verbatim. No reformatting, no magic markers. The trailing `crc32` is the original invoice CRC — the gateway verifies it against the cached Packet A to reject substitution attacks at settlement time.

---

## 5. Security & Logic Controls

### 5.1 Idempotency & Replay Protection

The SDK implements strict idempotency to prevent double-spending and replay attacks.

- **Monotonic Epoch Tracking:** The Ground Station maintains a per-asset `last_epoch_ms` register and rejects any packet whose `epoch_ts` is less-than-or-equal-to the last accepted value for that `sat_id`.
- **Freshness Window:** Any packet with `|now − epoch_ts| > 60s` is dropped (session TTL horizon).
- **Replay Cache:** A sliding `(sat_id, epoch_ts)` set keyed with a 24h TTL backstops the monotonic check against out-of-order arrivals.
- **Sender-Side Guardrail:** Sat B's firmware persists `last_tx_epoch_ms` to NVS and refuses TX until GPS time strictly exceeds the stored value. Under the optional `VOID_ENCRYPT_SNLP` build this guardrail is mandatory to protect the deterministic ChaCha20 nonce against clock rollback (see §4.2). Under default plaintext SNLP it is retained to prevent trivial replay.

### 5.2 Store-and-Forward Queue (Sat B)

Sat B serves as a data courier (mule). To prevent buffer overflows, it uses a deterministic FIFO policy:

- **Capacity:** 10 Receipts (~1.3 KB RAM).
- **Eviction:** "Drop Oldest" ensures only the most recent network states are prioritized.
- **Deduplication:** Incoming receipts are checked against the queue's `target_tx_id` to prevent redundant storage.

---

## 6. Implementation Logic (The Bouncer)

The Ground Station `Bouncer` acts as a dual-mode firewall, minimizing heap allocations and branching logic.

### 6.1 Multiplexer Logic

1. **Read First 4 Bytes (32-bit Int):**
* If `0x1D01A5A5`: The packet is **SNLP**. The pointer skips the 4-byte Sync Word, maps the 6-byte CCSDS fields, and ignores the 4-byte alignment buffer.
* If the first 3 bits are `000`: The packet is **Enterprise CCSDS**. The buffer is cast directly to the standard CCSDS struct without advancing the pointer.


2. **Discard Header:** The routing header is evaluated and then discarded before signature validation.
3. **Validate Inner Payload:** The 178-byte payload (offsets 14–191 for Packet B, inclusive of the 4-byte `_tail_pad`) is extracted, the signature is mathematically verified against the header + first 106 body bytes (through `_pre_sig`), and the payload is routed to the Go Gateway. The `_tail_pad` is ignored — it is frame-total alignment filler, not part of the signed or CRC-covered region.

---

## 7. Use Case Scenarios

### 7.1 The "Balloon Armada" (Hobbyist Testing)

In a High-Altitude Balloon (HAB) launch, users utilize a Heltec V3 board configured for SNLP.

* **Modulation:** LoRa 868MHz/915MHz.
* **Relay:** Any tinyGS ground station within 300km captures the SNLP frame.
* **Settlement:** The tinyGS MQTT server forwards the frame to the VOID Go Gateway for settlement.

### 7.2 University CubeSats (Norby / Polytech)

Small university satellites often use LoRa for telemetry due to low power requirements.

* **Role:** These satellites act as "Mules" (Sat B).
* **Benefit:** By using SNLP, students can use community ground stations for their missions, significantly reducing the cost of mission operations while earning settlement rewards for data delivery.

### 7.3 Hardware Requirements

SNLP is optimized for the **SX1262 LoRa Radio** and **ESP32-S3** platforms (Heltec LoRa V3 reference hardware). Post-VOID-114B, the Packet B MTU is **192 bytes** (3×64, cache-line clean), still fitting within a single LoRa packet at standard Spreading Factors (SF7 – SF9) without triggering radio hardware fragmentation limits. The +12 byte cost (+8 inner alignment pads + 4-byte tail pad) delivers zero-trap Xtensa LX7 access for every critical field AND a DMA-coalesced SPI burst to the SX1262 that never crosses a word boundary.

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.
