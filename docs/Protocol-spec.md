# Void Protocol Specification (v2.0)

> 🛰️ VOID PROTOCOL v2.0 | Tiny Innovation Group Ltd
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
> **Metric:** 176-Byte Footprint (Optimized)
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

## 2. Packet A: "The Invoice" (68 Bytes)

_Broadcast by Sat A. Unsigned public offer of service._

| Offset    | Field       | Type     | Size | Description                                                   |
| --------- | ----------- | -------- | ---- | ------------------------------------------------------------- |
| **00-05** | `ccsds_pri` | `u8[6]`  | 6B   | **Big-Endian** Primary Header (APID = Sat A)                  |
| **06-13** | `epoch_ts`  | `u64`    | 8B   | **Little-Endian** Unix Timestamp / Replay Protection          |
| **14-37** | `pos_vec`   | `f64[3]` | 24B  | **Little-Endian** GPS Vector (X, Y, Z in IEEE 754 Double)     |
| **38-49** | `vel_vec`   | `f32[3]` | 12B  | **Little-Endian** Velocity Vector (dX, dY, dZ in IEEE 754)    |
| **50-53** | `sat_id`    | `u32`    | 4B   | **Little-Endian** Unique Seller Identifier                    |
| **54-61** | `amount`    | `u64`    | 8B   | **Little-Endian** Cost in lowest denomination (e.g., Satoshi) |
| **62-63** | `asset_id`  | `u16`    | 2B   | **Little-Endian** Currency Type ID (1=USDC)                   |
| **64-67** | `crc32`     | `u32`    | 4B   | I**Little-Endian** Integrity Checksum                         |

---

## 3. Packet B: "The Encrypted Payment" (176 Bytes)

_Encapsulated payment intent sent by Sat B to the Ground Station._

| Offset      | Field             | Type     | Size | Description                                       |
| ----------- | ----------------- | -------- | ---- | ------------------------------------------------- |
| **00-05**   | `ccsds_pri`       | `u8[6]`  | 6B   | **Big-Endian** Header (APID = Sat B). Cleartext   |
| **06-13**   | `epoch_ts`        | `u64`    | 8B   | **Little-Endian** Sat B Timestamp. Used for Nonce |
| **14-37**   | `pos_vec`         | `f64[3]` | 24B  | **Little-Endian** Sat B Position. Cleartext       |
| **38-99**   | **`enc_payload`** | `u8[62]` | 62B  | **ChaCha20 Encrypted Inner Invoice**              |
| **100-103** | `sat_id`          | `u32`    | 4B   | **Little-Endian** Sat B ID. Cleartext             |
| **104-107** | `nonce`           | `u32`    | 4B   | **Little-Endian**Counter for Encryption Nonce     |
| **108-171** | `signature`       | `u8[64]` | 64B  | **PUF Signature** (Signs offsets 00-107)          |
| **172-175** | `global_crc`      | `u32`    | 4B   | **Little-Endian** Global Packet Integrity Check   |

### 3.1. Inner Invoice Payload (62 Bytes)

_The Plaintext data recovered from `enc_payload` after decryption._

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
| **32 - 47** | Length           | 16 bits | **Total Bytes - 6 - 1** (169 for Packet B) |

---

## 5. Security & Logic Controls

### 5.1 Idempotency & Replay Protection

The SDK implements strict idempotency to prevent double-spending and replay attacks.

- **Nonce Tracking:** Client nodes maintain a `last_nonce` register in volatile memory.
- **Duplicate Rejection:** Any packet with a `nonce` or `epoch_ts` older than 60 seconds or already processed is dropped immediately.

### 5.2 Store-and-Forward Queue (Sat B)

Sat B serves as a data courier (mule). To prevent buffer overflows, it uses a deterministic FIFO policy:

- **Capacity:** 10 Receipts (~1.3 KB RAM).
- **Eviction:** "Drop Oldest" ensures only the most recent network states are prioritized.
- **Deduplication:** Incoming receipts are checked against the queue's `target_tx_id` to prevent redundant storage.

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.
