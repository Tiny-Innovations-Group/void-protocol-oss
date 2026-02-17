# Acknowledgement Protocol Specification (v2.1)

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0
>
> Status: Authenticated Clean Room Spec
>
> **Core Metric:** 120-Byte Footprint
>
> **Optimization:** 64-bit machine cycle alignment

## A. The Acknowledgement Packet (Downlink)

**Route:** Ground Station → Sat B (Relay)

| Offset      | Field          | Type     | Size | Description                      |
| :---------- | :------------- | :------- | :--- | :------------------------------- |
| **00-05**   | `CCSDS_PRI`    | `u8[6]`  | 6B   | Header: Type=1 (Cmd), APID=Sat B |
| **06-07**   | `_PAD_A`       | `u16`    | 2B   | 32/64-bit alignment pad          |
| **08-11**   | `TARGET_TX_ID` | `u32`    | 4B   | Cleartext transaction nonce      |
| **12**      | `STATUS`       | `u8`     | 1B   | `0x01`=Settled, `0xFF`=Rejected  |
| **13**      | `_PAD_B`       | `u8`     | 1B   | Data boundary pad                |
| **14-25**   | `RELAY_OPS`    | `Struct` | 12B  | Sat B Relay Instructions         |
| **26-113**  | `ENC_TUNNEL`   | `u8[88]` | 88B  | ChaCha20 Encrypted Payload       |
| **114-115** | `_PAD_C`       | `u16`    | 2B   | Alignment to 116                 |
| **116-119** | `CRC32`        | `u32`    | 4B   | Outer Checksum                   |

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

**Total Size:** 88 Bytes (Resides within `ENC_TUNNEL`)
**Constraint:** `BLOCK_NONCE` must sit on 8-byte boundary (Offset 08).

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
