# VOID Protocol v2.1 - Binary Schema Specification

> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0
>
> Status: Authenticated Clean Room Spec
> 
> **File:** `void_protocol.ksy`
> 
> **Role:** Authoritative Source of Truth for Binary Parsing
> 
> **Engine:** Kaitai Struct (v0.10+)

## 1. The "Dual-Header" Strategy
This schema implements a **Dynamic Dispatch** system to handle the protocol's two physical transport layers without requiring separate parsers.

| Layer | Header Size | Endianness | Trigger |
| :--- | :--- | :--- | :--- |
| **Community (SNLP)** | **14 Bytes** | Big-Endian | Magic Word `0x1D01A5A5` at Offset 0 |
| **Enterprise (CCSDS)** | **6 Bytes** | Big-Endian | Default (No Magic Word) |

### 1.1 The "Magic Peek" Logic
The parser inspects the first **4 bytes** of the stream (`magic_peek`).
* **IF** `0x1D01A5A5`: The parser engages **SNLP Mode**. It consumes the 14-byte header (Sync + CCSDS + Pad) and sets the `is_snlp` flag to `true`.
* **ELSE**: The parser engages **CCSDS Mode**. It rewinds and consumes a standard 6-byte CCSDS header. `is_snlp` is `false`.

---

## 2. Dynamic Payload Sizing
Payloads are parsed as **Little-Endian**. The parser determines the message type (Packet A, B, etc.) based on the **Remaining Body Length**:

`Body_Len = Total_Packet_Size - Header_Size`

| Payload ID | Body Size | Description |
| :--- | :--- | :--- |
| **Packet A** | 62 Bytes | **Invoice** (Public Offer) |
| **Packet B** | 170 Bytes | **Payment** (Encrypted/Public) |
| **Packet H** | 106 Bytes | **Handshake** (Key Exchange) |
| **Packet C** | 98 Bytes | **Receipt** (Proof of Exec) |
| **Packet D** | 122 Bytes | **Delivery** (Wrapped Receipt) |
| **Packet ACK** | 114 / 122* | **Command** (Unlock Instruction) |

*> Note: Packet D and ACK can collide at 122 bytes. The parser resolves this using the CCSDS `Packet Type` bit (0=Telemetry, 1=Command).*

---

## 3. Special Handling: Tunnel Data
Inside **Packet ACK**, the encrypted `enc_tunnel` field changes size based on the transport layer to maintain alignment. The KSY file handles this via a ternary operator:

* **SNLP Mode:** Tunnel is **96 Bytes** (Includes 8B padding overhead).
* **CCSDS Mode:** Tunnel is **88 Bytes** (Standard compact size).

## 4. Usage
**To Generate a Parser:**
```bash
# Generate Go Code
kaitai-struct-compiler -t go --outdir ./gateway void_protocol.ksy

# Generate Python Code
kaitai-struct-compiler -t python --outdir ./scripts void_protocol.ksy
```

## 5. Testing
In side of the the scripts folder you can find the **gen_packet.py** this can be used to generate `.bin` files that can we used to check the hex values.