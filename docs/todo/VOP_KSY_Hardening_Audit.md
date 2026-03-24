# 🛡️ VOP KSY Hardening Audit — `void_protocol_final_v2.ksy`

> **Auditor:** Senior Critical Embedded Systems Engineer
>
> **Date:** 2026-03-22
>
> **Target:** `void_protocol_final_v2.ksy` (VOID Protocol v2.1)
>
> **Verdict:** **6 Critical Faults, 4 Structural Warnings, 2 Spec Divergences**
>
> **Status:** FAIL — Requires refactor before TRL-7 promotion.

---

## 1. CCSDS 133.0-B-2 Compliance Audit

### 1.1 Primary Header Bit Extraction — CRITICAL FAULT

The `header_ccsds` type packs the first 16 bits into `version_type_sec_apid` (u2, Big-Endian) and extracts fields via bitmasking. The CCSDS 133.0-B-2 Section 4.1.3 mandates:

```
Bits [0-2]   Version Number    3 bits
Bit  [3]     Packet Type       1 bit
Bit  [4]     Sec Header Flag   1 bit
Bits [5-15]  APID              11 bits
Bits [16-17] Sequence Flags    2 bits
Bits [18-31] Sequence Count    14 bits
Bits [32-47] Packet Data Len   16 bits
```

**FAULT C-01 (CRITICAL): `seq_count` mask truncates 4 bits.**

```yaml
# CURRENT (BROKEN):
seq_count:
  value: seq_flags_count & 0x3FF    # 10-bit mask → range 0-1023
```

The CCSDS Sequence Count is a **14-bit** field (range 0–16383). The mask `0x3FF` is 10 bits. This silently discards the upper 4 bits of every sequence counter.

**Impact:** The rolling counter wraps at 1024 instead of 16384. On a satellite transmitting one packet per second, the counter collides every ~17 minutes instead of ~4.5 hours. This destroys deduplication logic in the Ground Station FIFO and opens a **replay attack window** where legitimate packets are misidentified as duplicates.

```yaml
# FIX:
seq_count:
  value: seq_flags_count & 0x3FFF   # 14-bit mask → range 0-16383
```

**FAULT C-02 (WARNING): Version field extraction is correct but unvalidated.**

The KSY extracts `version` but never asserts it equals `0b000`. A non-zero version field indicates a future CCSDS revision the parser doesn't understand. In bare-metal, parsing an unknown version without rejection is undefined behavior.

```yaml
# RECOMMENDED: Add doc-level assertion or post-parse check
version:
  value: (version_type_sec_apid >> 13) & 0b111
  valid:
    eq: 0
  doc: "MUST be 0 (CCSDS Version 1). Non-zero = reject."
```

**FAULT C-03 (WARNING): `packet_length` field computed but never used for dispatch.**

The `actual_length` instance (`packet_length + 7`) is defined but the body dispatcher uses `_io.size` instead. Per CCSDS 133.0-B-2 Section 4.1.3.5.2, the Packet Data Length field is the authoritative size indicator. Ignoring it in favor of stream metadata violates the standard's self-delimiting packet contract.

---

## 2. Byte-Level Fragility Analysis

### 2.1 SNLP Header Size: Spec vs. Implementation Divergence

**FAULT F-01 (CRITICAL): SNLP Header is 14 bytes in KSY, 12 bytes in spec.**

| Source | Sync | CCSDS | Pad | Total |
|---|---|---|---|---|
| `Protocol-spec-SNLP.md` (Section 2) | 4B | 6B | **2B** (`0x0000`) | **12B** |
| `KSY_README.md` (Section 1) | 4B | 6B | **4B** | **14B** |
| `void_protocol_final_v2.ksy` | 4B | 6B | **`size: 4`** | **14B** |

The SNLP specification document explicitly defines `align_pad` at offsets 10–11 as 16 bits (2 bytes). The KSY inflates this to 4 bytes.

**Why this matters:** The KSY uses the inflated header to normalize payload lengths across SNLP and CCSDS tiers. For Packet B: `184 - 14 = 170` (matches `packet_b_body`). But this means the KSY is absorbing the SNLP `tail_pad` into the header padding — a sleight-of-hand that breaks if any future packet type has a different tail structure.

**The real problem:** The SNLP spec says Packet B's `tail_pad` is at offsets 182–183. The KSY's `packet_b_body` has no `tail_pad` field. Those 2 bytes are silently consumed by the inflated header pad. This is **not documented anywhere** and creates a silent contract between header sizing and body sizing that will break on the first spec revision.

### 2.2 Dispatch via `_io.size` — Stream-Hostile

**FAULT F-02 (CRITICAL): Payload dispatch depends on `_io.size`.**

```yaml
payload_len:
  value: >-
    is_snlp ? (_io.size - 14) : (_io.size - 6)
```

`_io.size` returns the total size of the input stream. This **only works** when each packet is provided as an isolated byte buffer. In real-world scenarios:

- **LoRa Radio FIFO:** The SX1262 delivers packets as discrete buffers. `_io.size` works here.
- **MQTT Byte Stream:** If multiple packets are concatenated in a TCP stream (TinyGS MQTT ingest), `_io.size` returns the stream size, not the packet size. Dispatch breaks catastrophically.
- **File-based Testing:** If `.bin` test vectors are concatenated, the parser fails.

**The correct approach:** Use the CCSDS `packet_length` field (already parsed) as the authoritative body length. Fall back to `_io.size` only as a sanity check.

```yaml
# HARDENED:
payload_len:
  value: >-
    is_snlp
    ? (routing_header.as<header_snlp>.ccsds.packet_length + 1)
    : (routing_header.as<header_ccsds>.packet_length + 1)
  doc: |
    CCSDS packet_length = (num_octets_in_packet_data_field) - 1.
    So actual payload = packet_length + 1.
```

### 2.3 The `dispatch_122` Collision — Fragile Branching

**FAULT F-03 (WARNING): Collision resolution has no fallback.**

When `payload_len == 122`, the parser uses `global_packet_type` (0=Telemetry→Packet D, 1=Command→Packet ACK) to disambiguate. If the CCSDS Type bit is corrupted by a single bit-flip on the RF link:

- A Packet D (Delivery) is parsed as an ACK, causing the Ground Station to interpret a receipt as an unlock command.
- A Packet ACK is parsed as a Delivery, causing the gateway to attempt receipt verification on command data.

There is no CRC pre-check before dispatch. The `global_crc` / `crc32` fields are parsed *inside* the body types, meaning integrity verification happens **after** the type decision, not before.

### 2.4 `enc_tunnel` Dynamic Sizing — Variable-Width in Fixed Dispatch

**FAULT F-04 (WARNING): Ternary size breaks formal verification.**

```yaml
enc_tunnel:
  size: "_root.is_snlp ? 96 : 88"
```

The `packet_ack_body` type is not truly fixed-size. Its memory footprint changes based on the transport layer. In a zero-heap C environment, the struct would need to be defined with the **maximum** size (96 bytes) and a length field to indicate actual occupancy. The KSY's ternary is elegant but non-deterministic from a static analysis perspective.

---

## 3. Memory Constraint Audit (128KB Ceiling)

### 3.1 Static Footprint — PASS

All types are bounded. Maximum packet size is 184 bytes (SNLP Packet B). The parser requires no heap allocation, no `repeat` loops, and no unbounded reads. Total worst-case buffer: **184 bytes + parser state ≈ 256 bytes**. Well within 128KB.

### 3.2 Implicit Heap Risk — PASS (with caveat)

No `repeat`, `repeat-expr`, or `repeat-until` constructs detected. All `size` fields are either constants or bounded ternaries. However, the generated code from `kaitai-struct-compiler` may allocate heap memory for string representations of `doc` fields and internal bookkeeping depending on the target language. **Recommendation:** Audit the generated Go/C++ output for hidden `new`/`malloc` calls.

### 3.3 Parse Time Determinism — CONDITIONAL PASS

The `switch-on` dispatchers introduce exactly **one branch** per packet. The `magic_peek` instance adds a second branch for header selection. Total worst-case branches per parse: **3** (header select + body dispatch + collision resolve). This is constant-time and deterministic. No conditional loops exist.

**Caveat:** The `_io.size` dependency adds an implicit syscall or buffer-length query. In bare-metal, this should be replaced with a passed-in length parameter.

---

## 4. Kaitai Best Practices Audit

### 4.1 Monolithic File — STRUCTURAL VIOLATION

The entire protocol (7 packet types, 2 header formats, 3 custom types, 1 dispatcher) lives in a single 280-line file. Kaitai supports `imports` (formerly `includes`) for modular decomposition. The current monolith means:

- A change to `vector_3d` requires re-validating every packet type that uses it.
- The CCSDS header (which should be a reusable, standards-compliant module) is locked inside a protocol-specific file.
- Testing individual packet types requires parsing the entire dispatch tree.

### 4.2 Missing `valid` Constraints

Kaitai supports `valid` assertions on fields. None are used. Key fields that should be constrained:

| Field | Constraint | Reason |
|---|---|---|
| `header_ccsds.version` | `eq: 0` | CCSDS v1 only |
| `header_snlp.sync_word` | Already `contents` (good) | — |
| `packet_a_body.asset_id` | `any-of: [1]` | Stablecoin-only mandate |
| `packet_ack_body.status` | `any-of: [0x01, 0xFF]` | Settled or Rejected only |
| `heartbeat_body.sys_state` | `max: 5` (or known enum) | Bounded state machine |

### 4.3 No Enums

The KSY uses raw integer values for semantically meaningful fields (`status`, `sys_state`, `cmd_code`, `asset_id`). Kaitai's `enums` feature would make the schema self-documenting and enable generated code to use type-safe constants.

### 4.4 `doc-ref` is a Scalar, Not a List

```yaml
doc-ref:
  https://github.com/...
  https://github.com/...
```

Kaitai's `doc-ref` field expects a single string or a YAML list. The current multi-line format may be parsed as a single concatenated string by some KSY versions. Should be:

```yaml
doc-ref:
  - https://github.com/.../Acknowledgment-spec.md
  - https://github.com/.../Protocol-spec-CCSDS.md
```

---

## 5. Decoupling Strategy — Proposed Modular Architecture

### 5.1 File Decomposition

```
void-protocol-ksy/
├── ccsds_primary_header.ksy    # Standards-compliant, reusable
├── snlp_header.ksy             # Imports ccsds_primary_header
├── tig_common_types.ksy        # Vectors, signatures, enums
├── vop_invoice.ksy             # Packet A body
├── vop_payment.ksy             # Packet B body
├── vop_handshake.ksy           # Packet H body
├── vop_receipt.ksy             # Packet C body
├── vop_delivery.ksy            # Packet D body
├── vop_ack.ksy                 # Packet ACK body + relay_ops_t
├── vop_heartbeat.ksy           # Heartbeat body
└── void_protocol.ksy           # Root dispatcher (imports all above)
```

### 5.2 Module Responsibilities

**`ccsds_primary_header.ksy`** — The "Gold Standard" Module

This file owns the 6-byte CCSDS header and nothing else. It is reusable across any CCSDS-compliant project.

```yaml
meta:
  id: ccsds_primary_header
  title: "CCSDS 133.0-B-2 Space Packet Primary Header"
  endian: be
  license: Apache-2.0

doc: |
  Strict implementation of CCSDS 133.0-B-2 Section 4.1.3.
  6-octet Primary Header. Big-Endian. Self-delimiting.

seq:
  - id: version_type_flag_apid
    type: u2
  - id: seq_flags_count
    type: u2
  - id: packet_data_length
    type: u2

instances:
  version:
    value: (version_type_flag_apid >> 13) & 0b111
    valid:
      eq: 0
    doc: "3-bit Version Number. MUST be 0 (CCSDS Version 1)."

  packet_type:
    value: (version_type_flag_apid >> 12) & 0b1
    doc: "0=Telemetry (TM), 1=Telecommand (TC)"

  sec_header_flag:
    value: (version_type_flag_apid >> 11) & 0b1
    doc: "1=Secondary Header present."

  apid:
    value: version_type_flag_apid & 0x7FF
    doc: "11-bit Application Process Identifier."

  seq_flags:
    value: (seq_flags_count >> 14) & 0b11
    doc: "00=Continuation, 01=First, 10=Last, 11=Unsegmented."

  seq_count:
    value: seq_flags_count & 0x3FFF
    doc: "14-bit Packet Sequence Count (0-16383). FIXED: was 0x3FF."

  data_field_length:
    value: packet_data_length + 1
    doc: "Actual octets in packet data field = packet_data_length + 1."

  total_packet_length:
    value: packet_data_length + 7
    doc: "Total packet size = header(6) + data_field_length."
```

**`snlp_header.ksy`** — Community Transport Wrapper

```yaml
meta:
  id: snlp_header
  title: "SNLP Frame Header (Community Tier)"
  endian: be
  license: Apache-2.0
  imports:
    - ccsds_primary_header

doc: |
  12-byte SNLP header: 4B Sync + 6B CCSDS + 2B Alignment Pad.
  CORRECTED: Pad is 2 bytes per spec, not 4.

seq:
  - id: sync_word
    contents: [0x1D, 0x01, 0xA5, 0xA5]
    doc: "32-bit Sync Word for ISM band frame detection."

  - id: ccsds
    type: ccsds_primary_header
    doc: "Standard 6-byte CCSDS Primary Header."

  - id: align_pad
    type: u2
    valid:
      eq: 0
    doc: "2-byte alignment buffer. MUST be 0x0000. Reconciled with SNLP spec."
```

**`tig_common_types.ksy`** — Shared Primitives

```yaml
meta:
  id: tig_common_types
  title: "TIG Common Binary Types"
  endian: le
  license: Apache-2.0

doc: "Reusable field types for all VOP packet bodies."

enums:
  asset_id:
    1: usdc
    # Future stablecoins registered here

  settlement_status:
    0x01: settled
    0xFF: rejected

  sys_state:
    0: boot
    1: idle
    2: tx
    3: rx
    4: connected
    5: error

  cmd_code:
    0x0001: unlock_dispense
    0x00FF: atomic_destroy

types:
  vector_3d:
    doc: "IEEE 754 Double-Precision 3D Vector (24 bytes)"
    seq:
      - id: x
        type: f8
        doc: "ECEF X (meters)"
      - id: y
        type: f8
        doc: "ECEF Y (meters)"
      - id: z
        type: f8
        doc: "ECEF Z (meters)"

  vector_3f:
    doc: "IEEE 754 Single-Precision 3D Vector (12 bytes)"
    seq:
      - id: x
        type: f4
      - id: y
        type: f4
      - id: z
        type: f4

  ed25519_signature:
    doc: "64-byte Ed25519 / PUF Identity Signature"
    seq:
      - id: raw
        size: 64

  relay_ops:
    doc: "12-byte Relay Operations Sub-structure"
    seq:
      - id: azimuth
        type: u2
        doc: "Horizontal Look Angle (u16)"
      - id: elevation
        type: u2
        doc: "Vertical Look Angle (u16)"
      - id: frequency
        type: u4
        doc: "Target Tx Frequency in Hz"
      - id: duration_ms
        type: u4
        doc: "Relay Window in milliseconds"
```

### 5.3 Hardened Root Dispatcher — `void_protocol.ksy`

```yaml
meta:
  id: void_protocol
  title: "VOID Protocol v2.1 — Root Dispatcher"
  endian: le
  license: Apache-2.0
  imports:
    - ccsds_primary_header
    - snlp_header
    - tig_common_types
    # Body imports omitted for brevity — one per packet type

seq:
  - id: routing_header
    type:
      switch-on: _root.is_snlp
      cases:
        true: snlp_header
        false: ccsds_primary_header

  - id: body
    size: _root.body_length
    type:
      switch-on: _root.body_length
      cases:
        62:  packet_a_body
        170: packet_b_body
        106: packet_h_body
        98:  packet_c_body
        122: dispatch_122
        114: packet_ack_body
        34:  heartbeat_body

instances:
  magic_peek:
    pos: 0
    type: u4be

  is_snlp:
    value: magic_peek == 0x1D01A5A5

  # HARDENED: Use CCSDS packet_data_length, not _io.size
  body_length:
    value: >-
      is_snlp
      ? (routing_header.as<snlp_header>.ccsds.data_field_length)
      : (routing_header.as<ccsds_primary_header>.data_field_length)
    doc: |
      Derived from CCSDS packet_data_length field.
      Replaces the fragile _io.size dependency.

  global_packet_type:
    value: >-
      is_snlp
      ? (routing_header.as<snlp_header>.ccsds.packet_type)
      : (routing_header.as<ccsds_primary_header>.packet_type)
```

Key change: `body` now uses `size: _root.body_length` to explicitly bound the sub-stream. This makes each body type parse within a delimited window, preventing overruns if the binary data is corrupt or concatenated.

---

## 6. Consolidated Fault Table

| ID | Severity | Category | Description | Fix |
|---|---|---|---|---|
| **C-01** | **CRITICAL** | CCSDS Compliance | `seq_count` mask is `0x3FF` (10-bit), should be `0x3FFF` (14-bit). Counter wraps at 1024 instead of 16384. | Change mask to `0x3FFF`. |
| **C-02** | WARNING | CCSDS Compliance | `version` field parsed but never validated. Non-zero version = unknown protocol revision. | Add `valid: { eq: 0 }`. |
| **C-03** | WARNING | CCSDS Compliance | `packet_length` / `actual_length` computed but unused for dispatch. Violates self-delimiting packet contract. | Use `data_field_length` for body dispatch. |
| **F-01** | **CRITICAL** | Spec Divergence | SNLP header is 14B in KSY but 12B in `Protocol-spec-SNLP.md`. The extra 2B absorb `tail_pad` silently. | Reconcile spec: either update the SNLP spec to 14B or fix KSY to 12B and add `tail_pad` to relevant bodies. |
| **F-02** | **CRITICAL** | Stream Safety | `payload_len` uses `_io.size`. Breaks on concatenated streams, MQTT ingest, and multi-packet files. | Replace with CCSDS `packet_data_length + 1`. |
| **F-03** | WARNING | Fault Tolerance | `dispatch_122` resolves Packet D vs ACK via Type bit with no CRC pre-check. Single bit-flip = wrong parse path. | Pre-validate outer CRC before dispatch, or add a packet-type magic byte at offset 0 of the body. |
| **F-04** | WARNING | Formal Verification | `enc_tunnel` size is a runtime ternary (`96 : 88`). Not statically analyzable. | Define two separate ACK body types (`packet_ack_body_snlp`, `packet_ack_body_ccsds`) with fixed sizes. |
| **K-01** | INFO | Kaitai Practice | Monolithic single-file schema. No `imports`. | Decompose per Section 5. |
| **K-02** | INFO | Kaitai Practice | No `enums` for semantic fields (`status`, `asset_id`, `sys_state`). | Add enums to `tig_common_types.ksy`. |
| **K-03** | INFO | Kaitai Practice | No `valid` constraints on any parsed fields. | Add constraints per Section 4.2 table. |
| **K-04** | INFO | Kaitai Practice | `doc-ref` is multi-line scalar, not YAML list. | Convert to YAML list syntax. |

---

## 7. SNLP Tail-Pad Reconciliation: The Two Options

The 14B-vs-12B SNLP header divergence (F-01) requires an architectural decision. Both options are valid; the choice depends on whether you prioritize spec fidelity or parser simplicity.

### Option A: Fix KSY to Match Spec (12B Header)

- Set `align_pad` in `header_snlp` to `size: 2` (matching the SNLP spec's 16-bit pad).
- `payload_len` for SNLP becomes `_io.size - 12` (or `ccsds.data_field_length`).
- Packet B body grows from 170 → 172 bytes. Add `tail_pad: size: 2` at the end of `packet_b_body`.
- Every SNLP-specific packet body that has trailing padding must include it explicitly.
- The body dispatch `switch-on` must handle **two different payload lengths** for the same logical packet (e.g., 170 for CCSDS Packet B, 172 for SNLP Packet B).

**Tradeoff:** Spec-compliant but breaks the "unified body" design. Dispatch tree doubles in complexity.

### Option B: Update Spec to Match KSY (14B Header)

- Amend `Protocol-spec-SNLP.md` Section 2 to define `align_pad` as 32 bits (4 bytes) at offsets 10–13.
- The SNLP header becomes 14 bytes officially.
- Body types remain unified across both tiers.
- Document the design rationale: "The SNLP header absorbs trailing alignment padding to normalize payload sizes across transport tiers."

**Tradeoff:** Clean parser, but the SNLP header no longer ends on a natural 64-bit boundary (14 % 8 = 6). The 12-byte version (12 % 4 = 0) had cleaner 32-bit alignment.

**Recommendation:** Option B, with one modification — expand the pad to **6 bytes** (offsets 10–15), making the SNLP header **16 bytes** (16 % 8 = 0, perfect 64-bit alignment). This adds 2 bytes to every SNLP packet but guarantees the payload always starts on a 64-bit boundary, which is the stated design goal. Update the SNLP spec, the KSY, and the KSY_README simultaneously.

---

## 8. Packet B Signature Scope Discrepancy

A secondary audit finding across documents:

| Source | Signature Covers |
|---|---|
| `Protocol-spec-CCSDS.md` (Section 3) | Offsets **00–107** (includes CCSDS header) |
| `Protocol-spec-SNLP.md` (Section 3) | Offsets **12–113** (excludes SNLP header) |

This is **intentional** per the Bouncer logic (SNLP spec Section 4.1): "The routing header is evaluated and then discarded before signature validation." However, the CCSDS spec signs offsets 00–107, which **includes** the CCSDS header itself. If the CCSDS header is also "discarded" before validation, the signature scope should start at offset 06 (post-header), not 00.

**Resolution needed:** Either the CCSDS signature truly covers the header (binding routing to payment — more secure), or both tiers should sign payload-only (simpler verification). Document the decision explicitly.

---

*END OF AUDIT*

*Verified against CCSDS 133.0-B-2 Section 4.1.3 and Kaitai Struct User Guide v0.10.*

*© 2026 Audit Report — Prepared for Tiny Innovation Group Ltd.*
