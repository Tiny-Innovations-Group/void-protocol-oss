# Acknowledgement Protocol Specification (v2.1)

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0
>
> Status: Authenticated Clean Room Spec
>
> **Core Metric:** 120-Byte Footprint (CCSDS) / 136-Byte Footprint (SNLP)
>
> **Optimization:** 64-bit machine cycle alignment
>
> **Tier Split (VOID-006, F-04 Option B, 2026-04-15):** The ACK body is
> defined as two fixed-size sibling types (`packet_ack_body_ccsds` and
> `packet_ack_body_snlp`) in the KSY schema. Both variants share an
> identical prefix; only `ENC_TUNNEL` differs in length (88B vs 96B).
> Every field has a compile-time offset in both tiers — no runtime
> size selection.
>
> **Magic Byte (VOID-006, F-03, 2026-04-15):** Body offset 0 carries the
> fixed discriminant `MAGIC = 0xAC`. This closes the `dispatch_122`
> single-bit-flip collision with Packet D (`MAGIC = 0xD0`) — Hamming
> distance 4 guarantees no single RF bit-flip can transmute one type
> into the other. The byte is absorbed from the legacy 2-byte `_PAD_A`;
> every downstream field offset and the total frame size are unchanged.
> Kaitai `dispatch_122` routes on this byte; the gateway performs an
> application-layer `raw[headerLen] == 0xAC` check before trusting the
> parser. Reject with HTTP 400 on any mismatch.

## A. The Acknowledgement Packet — CCSDS Tier (Downlink)

**Route:** Ground Station → Sat B (Relay)
**Wire Frame:** 120 bytes (6B CCSDS header + 114B body)
**KSY type:** `packet_ack_body_ccsds`

| Offset      | Field          | Type     | Size | Description                                  |
| :---------- | :------------- | :------- | :--- | :------------------------------------------- |
| **00-05**   | `CCSDS_PRI`    | `u8[6]`  | 6B   | Header: Type=1 (Cmd), APID=Sat B             |
| **06**      | `MAGIC`        | `u8`     | 1B   | **F-03 discriminant: `0xAC` (body offset 0)** |
| **07**      | `_PAD_A`       | `u8`     | 1B   | 32/64-bit alignment pad                      |
| **08-11**   | `TARGET_TX_ID` | `u32`    | 4B   | Cleartext transaction nonce                  |
| **12**      | `STATUS`       | `u8`     | 1B   | `0x01`=Settled, `0xFF`=Rejected              |
| **13**      | `_PAD_B`       | `u8`     | 1B   | Data boundary pad                            |
| **14-25**   | `RELAY_OPS`    | `Struct` | 12B  | Sat B Relay Instructions                     |
| **26-113**  | `ENC_TUNNEL`   | `u8[88]` | 88B  | ChaCha20 Encrypted Payload                   |
| **114-115** | `_PAD_C`       | `u16`    | 2B   | Alignment to 116                             |
| **116-119** | `CRC32`        | `u32`    | 4B   | Outer Checksum                               |

### A.1 The Acknowledgement Packet — SNLP Tier (Downlink)

**Route:** Ground Station → Sat B (Relay)
**Wire Frame:** 136 bytes (14B SNLP header + 122B body)
**KSY type:** `packet_ack_body_snlp`

Identical field layout to the CCSDS variant through offset 25. The only
divergence is `ENC_TUNNEL` at 96 bytes (vs 88B), which pushes the tail
fields forward by 8 bytes. The extra 8 bytes accommodate the embedded
SNLP header carried inside the encrypted tunnel payload.

| Offset      | Field          | Type     | Size | Description                                  |
| :---------- | :------------- | :------- | :--- | :------------------------------------------- |
| **00-13**   | `SNLP_HDR`     | `u8[14]` | 14B  | Sync + CCSDS + align_pad                     |
| **14**      | `MAGIC`        | `u8`     | 1B   | **F-03 discriminant: `0xAC` (body offset 0)** |
| **15**      | `_PAD_A`       | `u8`     | 1B   | 32/64-bit alignment pad                      |
| **16-19**   | `TARGET_TX_ID` | `u32`    | 4B   | Cleartext transaction nonce                  |
| **20**      | `STATUS`       | `u8`     | 1B   | `0x01`=Settled, `0xFF`=Rejected              |
| **21**      | `_PAD_B`       | `u8`     | 1B   | Data boundary pad                            |
| **22-33**   | `RELAY_OPS`    | `Struct` | 12B  | Sat B Relay Instructions                     |
| **34-129**  | `ENC_TUNNEL`   | `u8[96]` | 96B  | ChaCha20 Encrypted Payload                   |
| **130-131** | `_PAD_C`       | `u16`    | 2B   | Alignment to 132                             |
| **132-135** | `CRC32`        | `u32`    | 4B   | Outer Checksum                               |

---

### A.2 CRC Pre-Validation Gate (VOID-122)

Packet ACK carries **no Ed25519 signature** — the trailing `CRC32` is the only integrity gate on its in-body fields (`target_tx_id`, `status`, `relay_ops`, `enc_tunnel`). The ingest handler MUST verify the CRC before trusting any body field beyond the F-03 magic byte. Failing CRC rejects with HTTP 400 + structured log `level=warn event=packetack.crc_fail`.

- **CRC field position:** last 4 bytes of the frame (`frame_size - 4`).
- **CRC coverage:** `frame[0:frame_size - 4]` — header + every body byte that precedes the CRC.
- **Hash:** IEEE 802.3 CRC32 (polynomial `0xEDB88320`, reflected), byte-identical to Go's `hash/crc32.ChecksumIEEE` and the C++ firmware's `VoidProtocol::calculateCRC`.

This gate complements the F-03 magic byte (body offset 0 = `0xAC`) on the `dispatch_122` collision zone: F-03 catches a single RF bit-flip on the CCSDS type bit (Hamming distance 4 between `0xAC` and `0xD0`), and CRC catches every other in-scope byte flip.

---

## B. Relay Ops 12 bytes (Sub-structure)

Used by Sat B to orient transmission toward Sat A.

| Offset    | Field       | Type  | Size | Description              |
| :-------- | :---------- | :---- | :--- | :----------------------- |
| **14-15** | `AZIMUTH`   | `u16` | 2B   | Look Angle (Horizontal)  |
| **16-17** | `ELEVATION` | `u16` | 2B   | Look Angle (Vertical)    |
| **18-21** | `FREQUENCY` | `u32` | 4B   | Target Tx Frequency (Hz) |
| **22-25** | `DURATION`  | `u32` | 4B   | Relay Window (ms)        |

---

## C. Tunnel Data (The "Encrypted Unlock")

**Total Size:** 88 Bytes (CCSDS `ENC_TUNNEL`) / 96 Bytes (SNLP `ENC_TUNNEL`)
**Constraint:** `BLOCK_NONCE` must sit on 8-byte boundary (Offset 08).

The decrypted tunnel payload layout below describes the 88-byte CCSDS
form. The SNLP 96-byte form carries the same fields preceded by an 8-byte
SNLP sub-header (see [Protocol-spec-SNLP.md](Protocol-spec-SNLP.md)).

| Offset    | Field         | Type     | Size | Description                          |
| :-------- | :------------ | :------- | :--- | :----------------------------------- |
| **00-05** | `CCSDS_PRI`   | `u8[6]`  | 6B   | Header: APID=Sat A                   |
| **06-07** | `_PAD_A`      | `u8[2]`  | 2B   | **64-bit Alignment Pad**             |
| **08-15** | `BLOCK_NONCE` | `u64`    | 8B   | L2 Block Height / Replay Protection  |
| **16-17** | `CMD_CODE`    | `u16`    | 2B   | `0x0001` = UNLOCK / DISPENSE         |
| **18-19** | `TTL`         | `u16`    | 2B   | Time-To-Live                         |
| **20-83** | `GROUND_SIG`  | `u8[64]` | 64B  | Auth Signature (Signs offsets 00-19) |
| **84-87** | `CRC32`       | `u32`    | 4B   | Inner Checksum                       |

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.
