# Void Protocol Specification  (v2.1) - CCSDS Edition

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0
>
> Status: Authenticated Clean Room Spec
>
> **Endianness**: CCSDS Header (Big-Endian) | Payload (Little-Endian)
>
> **Optimization:** 64-bit machine cycle alignment
>
> **Metric:** 184-Byte Packet B Footprint (VOID-114B: body aligned + _tail_pad[4], was 172)
>
> **Target:** 32/64-bit Hardware
>
> **Security:** ChaCha20 + SHA-256 (Hardware Accelerated)
>
> **Architecture:** Encrypted Store-and-Forward

## 1. Architectural Integrity

The Void Protocol facilitates an encrypted settlement layer for trustless Machine-to-Machine (M2M) transactions between orbital assets.

1. **Sat A (Seller):** Broadcasts an "Invoice" (Packet A).
2. **Sat B (Buyer/Mule):** Captures Packet A, encapsulates it with payment intent, signs it (Packet B), and facilitates the uplink.
3. **Ground Station:** Settlement Authority. Validates signatures, executes L2 Smart Contracts, and issues the "Unlock" command.

### 1.1 Cryptographic Primitives & Hardware Rationale

- **SHA-256:** Utilized for all hashing and key derivation. This primitive was selected due to the dedicated hardware acceleration engines present in the **ESP32-S3** and **ARM Cortex-M** architectures, ensuring high-speed processing with minimal power consumption during brief orbital windows.
- **ChaCha20:** A high-performance stream cipher used for payload encryption. It offers military-grade security without the computational overhead of block ciphers, perfectly suited for resource-constrained embedded systems.
- **The 86-Character Key Rule:** Users must provide an 86-character passphrase. This specific length ensures approximately 258 bits of entropy (), which perfectly saturates the 256-bit key space of the ChaCha20 cipher.

---

## 2. Packet A: "The Invoice" (72 Bytes)

_Broadcast by Sat A. Unsigned public offer of service._

**VOID-114B CHANGE:** The body has grown from 62 to 66 bytes with the addition of `_pad_head[2]` and `_pre_crc[2]` alignment slots. Every critical field (`epoch_ts`, `pos_vec`, `vel_vec`, `sat_id`, `amount`, `crc32`) now lands on its natural alignment boundary. See [`VOID_114B_BODY_ALIGNMENT_2026-04-14.md`](VOID_114B_BODY_ALIGNMENT_2026-04-14.md).

| Offset    | Field       | Type     | Size | Description                                                   |
| --------- | ----------- | -------- | ---- | ------------------------------------------------------------- |
| **00-05** | `ccsds_pri` | `u8[6]`  | 6B   | **Big-Endian** Primary Header (APID = Sat A)                  |
| **06-07** | `_pad_head` | `u8[2]`  | 2B   | VOID-114B alignment pad (keeps u64 head 8-aligned)            |
| **08-15** | `epoch_ts`  | `u64`    | 8B   | **Little-Endian** Unix Timestamp / Replay Protection ✅        |
| **16-39** | `pos_vec`   | `f64[3]` | 24B  | **Little-Endian** GPS Vector (X, Y, Z in IEEE 754 Double) ✅   |
| **40-51** | `vel_vec`   | `f32[3]` | 12B  | **Little-Endian** Velocity Vector (dX, dY, dZ in IEEE 754) ✅  |
| **52-55** | `sat_id`    | `u32`    | 4B   | **Little-Endian** Unique Seller Identifier ✅                  |
| **56-63** | `amount`    | `u64`    | 8B   | **Little-Endian** Cost in lowest denomination (e.g., Satoshi) ✅ |
| **64-65** | `asset_id`  | `u16`    | 2B   | **Little-Endian** Currency Type ID (1=USDC)                   |
| **66-67** | `_pre_crc`  | `u8[2]`  | 2B   | VOID-114B alignment pad (keeps crc32 4-aligned)               |
| **68-71** | `crc32`     | `u32`    | 4B   | **Little-Endian** Integrity Checksum ✅                        |

---

## 3. Packet B: "The Encrypted Payment" (184 Bytes)

_Encapsulated payment intent sent by Sat B to the Ground Station._

**VOID-110 CHANGE:** The 4-byte wire `nonce` field has been **removed**. The ChaCha20 nonce is now a deterministic function of fields already present in the packet. See §3.2 below for the full construction.

**VOID-114B CHANGE:** The body has grown from 166 to 178 bytes (174 payload + 4-byte `_tail_pad`). The `_pad_head[2]`, `_pre_sat[2]`, and `_pre_sig[4]` slots place every critical field (`epoch_ts`, `pos_vec`, `sat_id`, `signature`, `global_crc`) on its natural alignment boundary. The `_tail_pad[4]` brings the frame total to **184 bytes (÷8 ✅)** so the SPI DMA burst to the SX1262 lands on a clean Xtensa LX7 word boundary. See [`VOID_114B_BODY_ALIGNMENT_2026-04-14.md`](VOID_114B_BODY_ALIGNMENT_2026-04-14.md).

| Offset      | Field             | Type     | Size | Description                                                         |
| ----------- | ----------------- | -------- | ---- | ------------------------------------------------------------------- |
| **00-05**   | `ccsds_pri`       | `u8[6]`  | 6B   | **Big-Endian** Header (APID = Sat B). Cleartext                     |
| **06-07**   | `_pad_head`       | `u8[2]`  | 2B   | VOID-114B alignment pad                                             |
| **08-15**   | `epoch_ts`        | `u64`    | 8B   | **Little-Endian** millisecond Unix timestamp. Strictly monotonic. ✅ |
| **16-39**   | `pos_vec`         | `f64[3]` | 24B  | **Little-Endian** Sat B Position. Cleartext ✅                       |
| **40-101**  | **`enc_payload`** | `u8[62]` | 62B  | **ChaCha20 Ciphertext** (see §3.2 for nonce construction)           |
| **102-103** | `_pre_sat`        | `u8[2]`  | 2B   | VOID-114B alignment pad                                             |
| **104-107** | `sat_id`          | `u32`    | 4B   | **Little-Endian** Sat B ID. Cleartext. Low 4 bytes of the nonce. ✅  |
| **108-111** | `_pre_sig`        | `u8[4]`  | 4B   | VOID-114B alignment pad                                             |
| **112-175** | `signature`       | `u8[64]` | 64B  | **Ed25519 Signature** over offsets 00–111 (header + body pre-sig) ✅ |
| **176-179** | `global_crc`      | `u32`    | 4B   | **Little-Endian** Global Packet Integrity Check ✅                   |
| **180-183** | `_tail_pad`       | `u8[4]`  | 4B   | VOID-114B frame-total alignment pad (184 ÷8 ✅)                      |

### 3.2 Nonce Derivation (VOID-110)

The ChaCha20-IETF construction requires a 96-bit (12-byte) nonce that is **unique** under a given key. VOID derives this nonce deterministically from fields already present in the packet, so no nonce material is transmitted on the wire:

```
nonce[12] = sat_id[4] || epoch_ts[8]       // both little-endian
```

Both sender and receiver can reconstruct the identical nonce from the packet content alone. No state synchronization is required between Sat B and the Ground Station.

**Uniqueness proof:**

1. `sat_id` is a globally unique 32-bit identity. No two assets share an ID.
2. `epoch_ts` is a 64-bit millisecond Unix timestamp that MUST be strictly monotonic per asset. The firmware enforces this via an NVS-persisted `last_tx_epoch_ms` checkpoint.
3. The LoRa PHY layer (any spreading factor SF7–SF12 at BW125) cannot emit two Packet Bs from the same asset within 1 ms — airtime for a 184-byte frame is ~216 ms minimum.
4. Therefore `(sat_id, epoch_ts)` is unique across the entire operational lifetime of the fleet, and so is the derived nonce.

**Why not random?** A random nonce would require transmitting 12 additional bytes on every packet. Deterministic derivation saves 4 bytes net on the wire (after removing the old broken 4-byte field), removes an RNG call from the hot path, and — critically — **makes nonce reuse physically impossible** rather than "statistically unlikely."

**Required guardrails.** This construction is only secure if the monotonic-epoch invariant holds across reboots. The firmware MUST:

- Persist `last_tx_epoch_ms` to NVS every N transmits.
- On boot, refuse all TX until GPS time is fixed **and** exceeds `last_tx_epoch_ms` by a safety margin.
- If the clock regresses mid-session, immediately call `wipeSession()` and halt TX.
- The Ground Station maintains a replay window keyed on `(sat_id, epoch_ts)` and rejects any duplicate tuple.

Without these guardrails a clock rollback (brownout, flash failure, or deliberate tamper) would cause nonce reuse and full keystream recovery. These guardrails are mandatory; they ship with the fix, not as a follow-up.

### 3.3 Inner Invoice Payload (62 Bytes)

_The plaintext data recovered from `enc_payload` after decryption. Byte-identical to the SNLP tier — see [Protocol-spec-SNLP.md §4.3](Protocol-spec-SNLP.md)._

| Offset    | Field      | Type     | Size | Description                                  |
| --------- | ---------- | -------- | ---- | -------------------------------------------- |
| **00-07** | `epoch_ts` | `u64`    | 8B   | **Little-Endian** Original Invoice Timestamp |
| **08-31** | `pos_vec`  | `f64[3]` | 24B  | **Little-Endian** Sat A Position Vector      |
| **32-43** | `vel_vec`  | `f32[3]` | 12B  | **Little-Endian** Sat A Velocity Vector      |
| **44-47** | `sat_id`   | `u32`    | 4B   | **Little-Endian** Seller Identifier          |
| **48-55** | `amount`   | `u64`    | 8B   | **Little-Endian** Transaction Value          |
| **56-57** | `asset_id` | `u16`    | 2B   | **Little-Endian** Currency ID                |
| **58-61** | `crc32`    | `u32`    | 4B   | **Little-Endian** Internal Invoice Checksum  |

---

## 4. CCSDS Header Bit-Level Layout (6 Bytes) **Big-Endian**

_Ensures compatibility with commercial ground stations (AWS/KSAT)._

| Bit Offset  | Field Name       | Size    | Value / Note                               |
| ----------- | ---------------- | ------- | ------------------------------------------ |
| **0 - 2**   | Version          | 3 bits  | `000` (CCSDS Version 1)                    |
| **3**       | Type             | 1 bit   | `0` = Telemetry (TM)                       |
| **4**       | Sec. Header Flag | 1 bit   | `1` = Present (Timestamp included)         |
| **5 - 15**  | **APID**         | 11 bits | **Application Process ID** (Sat ID)        |
| **16 - 17** | Seq. Flags       | 2 bits  | `11` = Unsegmented                         |
| **18 - 31** | Seq. Count       | 14 bits | Rolling counter (0-16383)                  |
| **32 - 47** | Length           | 16 bits | **Total Bytes - 6 - 1** (177 for Packet B, VOID-114B) |

### 4.1 APID Naming Domain & Global Identity Extension
The standard CCSDS APID is restricted to an 11-bit field, yielding a maximum of 2,048 unique identifiers per naming domain. To support a globally scaled Decentralized Physical Infrastructure Network (DePIN) across multiple independent fleets, the VOID Protocol strictly adheres to the CCSDS extension guidelines.

As per **CCSDS 133.0-B-2 (Space Packet Protocol, Section 2.1.1)** [^1]:
> *"If missions wish to use the APID naming domain to service... a deployment of multiple spacecraft, those missions must either manage and suballocate assignments in the single APID naming domain within the enterprise or define a way to extend it using mission-specific fields in the packet secondary header."* 

**Implementation:** The VOID Protocol utilizes the 11-bit APID strictly for local RF hardware routing. Global Web3 Identity is extended into the payload via the `sat_id` (32-bit) field, allowing for over 4.2 billion sovereign orbital identities while maintaining strict compliance with the CCSDS 133.0-B-2 Recommended Standard. 

---

## 5. Security & Logic Controls

### 5.1 Idempotency & Replay Protection

The SDK implements strict idempotency to prevent double-spending and replay attacks.

- **Monotonic Epoch Tracking:** The Ground Station maintains a per-asset `last_epoch_ms` register and rejects any packet whose `epoch_ts` is less-than-or-equal-to the last accepted value for that `sat_id`.
- **Freshness Window:** Any packet with `|now − epoch_ts| > 60s` is dropped (session TTL horizon).
- **Replay Cache:** A sliding `(sat_id, epoch_ts)` set keyed with a 24h TTL backstops the monotonic check against out-of-order arrivals.
- **Sender-Side Guardrail:** Sat B's firmware persists `last_tx_epoch_ms` to NVS and refuses TX until GPS time strictly exceeds the stored value. This protects the deterministic ChaCha20 nonce against clock rollback (see §3.2).

### 5.2 Store-and-Forward Queue (Sat B)

Sat B serves as a data courier (mule). To prevent buffer overflows, it uses a deterministic FIFO policy:

- **Capacity:** 10 Receipts (~1.3 KB RAM).
- **Eviction:** "Drop Oldest" ensures only the most recent network states are prioritized.
- **Deduplication:** Incoming receipts are checked against the queue's `target_tx_id` to prevent redundant storage.

---

## 6. Implementation Logic (The Bouncer)

The Ground Station `Bouncer` acts as a dual-mode firewall that accepts traffic from both the CCSDS Enterprise tier and the SNLP Community tier, minimising heap allocations and branching logic.

### 6.1 Multiplexer Logic

1. **Read First 4 Bytes (32-bit Int):**
   * If `0x1D01A5A5`: The packet is **SNLP**. The pointer skips the 4-byte Sync Word, maps the 6-byte CCSDS fields, and ignores the 4-byte alignment buffer.
   * If the first 3 bits are `000`: The packet is **Enterprise CCSDS**. The buffer is cast directly to the standard CCSDS struct without advancing the pointer.
2. **Discard Header:** The routing header is evaluated and then discarded before signature validation.
3. **Validate Inner Payload:** The body is extracted, the Ed25519 signature is mathematically verified against `header + body[0..pre_sig]`, and the payload is routed to the Go Gateway. The `_tail_pad` is ignored — it is frame-total alignment filler, not part of the signed or CRC-covered region.

---

## 7. Use Case Scenarios

### 7.1 Enterprise LEO / GEO Settlement

Commercial satellites on licensed S-Band or X-Band uplink directly to operator-owned ground stations (AWS Ground Station, KSAT, Viasat). The CCSDS primary header guarantees compatibility with existing downlink infrastructure; the VOID payload travels inside the CCSDS data field as opaque telemetry as far as the operator is concerned, and the Go Gateway is co-located behind the station firewall.

* **Modulation:** Licensed-band (S / X / Ka).
* **Relay:** Operator-owned station, no community hand-off.
* **Settlement:** Private L2 contract, enterprise key custody.

### 7.2 Inter-Satellite Relay (Mesh Mule)

Sat A broadcasts an invoice that is captured by a Sat B mule in the same orbital plane, which then downlinks Packet B during its own ground-station pass. The CCSDS tier is preferred here because the encrypted `enc_payload` prevents intermediate mules from observing transaction contents.

### 7.3 Hardware Requirements

CCSDS tier targets **licensed-band transceivers** (S-Band + above) on mission-grade buses. Reference hardware is anything with a CCSDS-compliant framer — the VOID spec is radio-agnostic above the framer. ESP32-S3 and ARM Cortex-M are the reference compute targets for hardware-accelerated SHA-256 and ChaCha20 (see §1.1).

---


[^1]: *Consultative Committee for Space Data Systems (CCSDS). "Space Packet Protocol." Recommended Standard, CCSDS 133.0-B-2, Blue Book, June 2020.* [Link](https://ccsds.org/Pubs/133x0b2e2.pdf)

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.
