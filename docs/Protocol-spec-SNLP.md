# 🛰️ Void Protocol Specification (v2.1) - SNLP Edition

> **Authority:** Tiny Innovation Group Ltd
> 
> **Standard:** Space Network Layer Protocol (SNLP)
> 
> **License:** Apache 2.0
> 
> **Status:** Production-Ready / TRL 6
> 
> **Primary Use Case:** LoRa LEO Satellites, High-Altitude Balloons

**NOTE: Data sent and received via TinyGS has to be public and not encrtyped, it can contain identifiable signtures**

## 1. Overview & Rationale

The Space Network Layer Protocol (SNLP) is a lightweight, high-reliability framing standard designed for **asynchronous, low-bandwidth radio links**. While the CCSDS header is the gold standard for licensed commercial bands (S-Band/X-Band), SNLP is optimized for the **ISM bands (433MHz / 868MHz / 915MHz)** where LoRa operates.

### 1.1 Key Advantages

* **32/64-bit Cycle Optimization:** The SNLP header is **14 bytes** (`sync_word[4] + ccsds[6] + align_pad[4]`). This is the only header size that maintains mod-8 congruence with the 6-byte CCSDS header, which is the necessary condition for both tiers to share a single body struct layout. See [`VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md`](VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md) for the full alignment proof across all packet types.
* **Zero-Overhead Parsing:** The SNLP header prepends a 4-byte software sync word to a standard 6-byte CCSDS header. This allows the C++ codebase to parse both Community and Enterprise traffic utilizing the exact same memory-mapping structs.
* **Extreme Reliability Sync:** Uses a 32-bit Sync Word (`0x1D01A5A5`) to allow the Ground Station Bouncer to instantly filter out noise and other satellite telemetry on crowded amateur LoRa networks (e.g., Norby, Polytech).

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

---

## 3. Packet B: "The Community Payment" (192 Bytes — VOID-114B)

*Encapsulated payment intent sent by Sat B to the tinyGS Network. Offsets below use the **14-byte** SNLP header confirmed by VOID-114.*

**VOID-110 CHANGE:** The 4-byte wire `nonce` field has been **removed**. The ChaCha20 nonce is now derived deterministically from fields already present in the packet. See §3.2 below.

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

### 3.2 Nonce Derivation (VOID-110)

SNLP transmits `enc_payload` in **plaintext** by default to comply with amateur-band encryption bans (FCC Part 97, ITU §25). The ChaCha20 path is therefore not engaged in the default SNLP build, and the nonce derivation below applies only when the firmware is explicitly compiled with `VOID_ENCRYPT_SNLP` (non-default, typically reserved for licensed-band SNLP deployments).

When encryption IS engaged, the 12-byte ChaCha20 nonce is derived identically to the CCSDS tier:

```
nonce[12] = sat_id[4] || epoch_ts[8]       // both little-endian
```

See [Protocol-spec-CCSDS.md §3.2](Protocol-spec-CCSDS.md) for the full uniqueness proof and the monotonic-epoch guardrail requirements. SNLP-tier satellites that engage the encrypted path MUST implement the same NVS persistence and GPS-gate as CCSDS satellites.

---

## 4. Implementation Logic (The Bouncer)

The Ground Station `Bouncer` acts as a dual-mode firewall, minimizing heap allocations and branching logic.

### 4.1 Multiplexer Logic

1. **Read First 4 Bytes (32-bit Int):**
* If `0x1D01A5A5`: The packet is **SNLP**. The pointer skips the 4-byte Sync Word, maps the 6-byte CCSDS fields, and ignores the 4-byte alignment buffer.
* If the first 3 bits are `000`: The packet is **Enterprise CCSDS**. The buffer is cast directly to the standard CCSDS struct without advancing the pointer.


2. **Discard Header:** The routing header is evaluated and then discarded before signature validation.
3. **Validate Inner Payload:** The 178-byte payload (offsets 14–191 for Packet B, inclusive of the 4-byte `_tail_pad`) is extracted, the signature is mathematically verified against the header + first 106 body bytes (through `_pre_sig`), and the payload is routed to the Go Gateway. The `_tail_pad` is ignored — it is frame-total alignment filler, not part of the signed or CRC-covered region.

---

## 5. Use Case Scenarios

### 5.1 The "Balloon Armada" (Hobbyist Testing)

In a High-Altitude Balloon (HAB) launch, users utilize a Heltec V3 board configured for SNLP.

* **Modulation:** LoRa 868MHz/915MHz.
* **Relay:** Any tinyGS ground station within 300km captures the SNLP frame.
* **Settlement:** The tinyGS MQTT server forwards the frame to the VOID Go Gateway for settlement.

### 5.2 University CubeSats (Norby / Polytech)

Small university satellites often use LoRa for telemetry due to low power requirements.

* **Role:** These satellites act as "Mules" (Sat B).
* **Benefit:** By using SNLP, students can use community ground stations for their missions, significantly reducing the cost of mission operations while earning settlement rewards for data delivery.

### 5.3 Hardware Requirements

SNLP is optimized for the **SX1262 LoRa Radio** and **ESP32-S3** platforms (Heltec LoRa V3 reference hardware). Post-VOID-114B, the Packet B MTU is **192 bytes** (3×64, cache-line clean), still fitting within a single LoRa packet at standard Spreading Factors (SF7 – SF9) without triggering radio hardware fragmentation limits. The +12 byte cost (+8 inner alignment pads + 4-byte tail pad) delivers zero-trap Xtensa LX7 access for every critical field AND a DMA-coalesced SPI burst to the SX1262 that never crosses a word boundary.

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.
