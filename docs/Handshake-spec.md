# Handshake Protocol Specification (v2.1)

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0
>
> Status: Authenticated Clean Room Spec
>
> **Endianness:** CCSDS Header (Big-Endian) | Payload (Little-Endian)
>
> **Optimization:** 32-bit Packet Alignment / 64-bit Field Alignment
>
> **Metric:** 112-Byte Footprint (Optimized)

## 1. Packet H: "The Handshake" (112 Bytes)

*Bidirectional packet for Ephemeral Key Exchange. Replaces padding with functional TTL.*

| Offset | Field | Type | Size | Endian | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **00-05** | `ccsds_pri` | `u8[6]` | 6B | **Big** | Header (APID = Sender) |
| **06-07** | `session_ttl` | `u16` | **2B** | **Little** | **Session Duration (s).** Aligns Timestamp. |
| **08-15** | `timestamp` | `u64` | 8B | **Little** | **Start Time** (Unix Epoch). |
| **16-47** | `eph_pub_key` | `u8[32]` | 32B | **Little** | **X25519 Ephemeral Public Key** |
| **48-111** | `signature` | `u8[64]` | 64B | - | **Ed25519 Identity Sig** (Signs 00-47) |

**Total Size:** **112 Bytes**. (Power of 8 aligned).

---

## 2. The Session Flow (AOS to LOS)

This sequence MUST complete before any **Packet B (Payment)** or **ACK (Unlock)** is transmitted.

### Phase 1: The Challenge (Sat B → Ground)

1.  **Generate:** Sat B generates a random ephemeral keypair (`eph_priv`, `eph_pub`).
2.  **Clock:** Sat B sets `timestamp` to `NOW()` and `session_ttl` to desired window (e.g., 600s).
3.  **Sign:** Sat B signs `timestamp + session_ttl + eph_pub` using its permanent **PUF Identity Key** (Ed25519).
4.  **Transmit:** Sat B broadcasts **Packet H (Init)**.

### Phase 2: The Verification & Response (Ground → Sat B)

1.  **Verify:** Ground receives Packet H.
    * **Liveness:** Checks `timestamp` is within 60s of Ground Time.
    * **Auth:** Verifies `signature` using Sat B's stored Public Identity Key.
2.  **Set Deadline:** Ground calculates `Session_End = timestamp + session_ttl`. All subsequent packets (B, C, D) are valid only until this time.
3.  **Generate:** Ground generates its own ephemeral keypair (`ground_priv`, `ground_pub`).
4.  **Derive:** Ground computes the **Shared Secret** using (`ground_priv` + `eph_pub`).
5.  **Session Key:** Ground hashes the Secret to create the `Session_Key`.
6.  **Transmit:** Ground sends **Packet H (Resp)** containing its `ground_pub`, signed by the Ground Station Authority Key.

### Phase 3: The Lock (Sat B)

1.  **Verify:** Sat B receives Packet H (Resp).
    * Verifies Ground Signature.
2.  **Derive:** Sat B computes the **Shared Secret** using (`eph_priv` + `ground_pub`).
3.  **Session Key:** Sat B hashes the Secret to create the `Session_Key`.
4.  **Destruction:** Sat B **securely wipes** `eph_priv` from RAM.
5.  **Transition:** State Machine moves to `CONNECTED`. All subsequent traffic (Packet B/C/D) is now encrypted with `Session_Key`.

---

## 3. Security Properties

* **Forward Secrecy:** Since `eph_priv` is deleted immediately after Phase 3, a physical capture of the device (Satellite/Drone) after the session cannot recover the `Session_Key` used to decrypt the logs.
* **Mutual Authentication:** Both parties verify each other's long-term Identity Keys (PUF vs Authority) before deriving the session key.
* **Replay Protection:** The `timestamp` field in Packet H prevents an attacker from re-broadcasting an old handshake to force a key reuse.
* **Flexibility:** The `session_ttl` field allows dynamic session windows for different assets (e.g., 10 mins for LEO Sats, 2 hours for Drones/Rovers).

---

[END OF SPECIFICATION]

Verified for 32/64-bit cycle optimization.

© 2026 Tiny Innovation Group Ltd.

```