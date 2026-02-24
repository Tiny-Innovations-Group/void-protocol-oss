# 🛰️ Void Protocol Specification (v2.1) - SNLP Edition

> **Authority:** Tiny Innovation Group Ltd
> 
> **Standard:** Space Network Layer Protocol (SNLP)
> 
> **License:** Apache 2.0
> 
> **Status:** Production-Ready / TRL 6
> 
> **Primary Use Case:** LoRa LEO Satellites, High-Altitude Balloons, tinyGS Integration

## 1. Overview & Rationale

The Space Network Layer Protocol (SNLP) is a lightweight, high-reliability framing standard designed for **asynchronous, low-bandwidth radio links**. While the CCSDS header is the gold standard for licensed commercial bands (S-Band/X-Band), SNLP is optimized for the **ISM bands (433MHz / 868MHz / 915MHz)** where LoRa operates.

### 1.1 Key Advantages

* **Zero-Overhead Parsing:** The SNLP header is architected as a 4-byte software sync word prepended to a standard 6-byte CCSDS header. This allows the C++ codebase to parse both Community and Enterprise traffic utilizing the exact same memory-mapping structs.
* **Extreme Reliability Sync:** Uses a 32-bit Sync Word (`0x1D01A5A5`) to allow the Ground Station Bouncer to instantly filter out noise and other satellite telemetry on crowded amateur LoRa networks (e.g., Norby, Polytech).
* **tinyGS Native:** Designed to be encapsulated within a standard LoRa frame, allowing the 1,000+ community ground stations to act as a decentralized relay for VOID transactions.

---

## 2. The SNLP Primary Header (10 Bytes)

*This 10-byte structure acts as a "Universal Adapter." Once the 4-byte Sync Word is stripped, the remaining 6 bytes are fully compliant with CCSDS 133.0-B-2 (Space Packet Protocol).*

| Offset | Bit Offset | Field Name | Size | Value / Description |
| :--- | :--- | :--- | :--- | :--- |
| **00-03** | `0 - 31` | **`sync_word`** | 32 bits | `0x1D01A5A5` (Identifies the VOID packet in noisy ISM bands) |
| **04** | `32 - 34` | `version` | 3 bits | `000` (Matches CCSDS Version 1) |
| | `35` | `type` | 1 bit | `0` = Telemetry, `1` = Command |
| | `36` | `sec_hdr_flag`| 1 bit | `1` = Present (Timestamp included) |
| **04-05**| `37 - 47` | `network_id` | 11 bits | Replaces CCSDS APID. (e.g., `0x01` for tinyGS, `0x02` for SatNOGS) |
| **06-07** | `48 - 49` | `seq_flags` | 2 bits | `11` = Unsegmented (Future proofs for fragmentation) |
| | `50 - 63` | `seq_count` | 14 bits | Rolling counter (0-16383) |
| **08-09** | `64 - 79` | `length` | 16 bits | Length of the payload (excluding this 10-byte header) |

---

## 3. Packet B: "The Community Payment" (180 Bytes)

*Encapsulated payment intent sent by Sat B to the tinyGS Network.*

| Offset | Field | Size | Description |
| :--- | :--- | :--- | :--- |
| **00-09** | **SNLP Header** | 10B | **32-bit Sync Word** + **6-byte CCSDS Structure** |
| **10-17** | `epoch_ts` | 8B | Little-Endian Sat B Timestamp. |
| **18-41** | `pos_vec` | 24B | Sat B Position (GPS Double Vector). |
| **42-103** | **`enc_payload`**| 62B | **ChaCha20 Encrypted Inner Invoice** |
| **104-107**| `sat_id` | 4B | Sat B ID (Mule ID). Extracted here instead of the header. |
| **108-111**| `nonce` | 4B | Encryption Nonce Counter. |
| **112-175**| `signature` | 64B | **PUF Signature** (Signs offsets 10-111). |
| **176-179**| `global_crc` | 4B | Global Packet Integrity Check. |

---

## 4. Implementation Logic (The Bouncer)

The Ground Station `Bouncer` acts as a dual-mode firewall, minimizing heap allocations and branching logic. 

### 4.1 Multiplexer Logic

1. **Read First 4 Bytes (32-bit Int):**
   * If `0x1D01A5A5`: The packet is **SNLP**. The pointer is advanced by 4 bytes (stripping the Sync Word). The remaining buffer is then cast directly to the standard CCSDS struct.
   * If the first 3 bits are `000`: The packet is **Enterprise CCSDS**. The buffer is cast directly to the standard CCSDS struct without advancing the pointer.
2. **Discard Header:** The routing header is evaluated and then discarded before signature validation.
3. **Validate Inner Payload:** The 170-byte core payload (offsets 10-179) is extracted, verified, and sent to the Go Gateway. 

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
SNLP is optimized for the **SX1262 LoRa Radio** and **ESP32-S3** platforms. It requires exactly **180 bytes** of MTU (Maximum Transmission Unit), fitting beautifully within a single LoRa packet at standard Spreading Factors (SF7 - SF9) without triggering radio hardware fragmentation limits.

---

*© 2026 Tiny Innovation Group Ltd.*