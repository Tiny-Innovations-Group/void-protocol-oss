# VOID Protocol — KSY, Security & Alignment Audit (VOID-006)

> **Date:** 2026-04-14
> **Scope:** Protocol hardening against adversary specialised in static memory manipulation, RF exploitation, and embedded software attacks. 32/64-bit machine cycle alignment verification.
> **Inputs:** All protocol specs, C++ packed structs (SNLP + CCSDS), live KSY (`void_protocol_final_v2.ksy`), hardened KSY drafts (`docs/todo/*.ksy`), `security_manager.cpp`, existing KSY Hardening Audit.
> **Rule:** No code changes. Findings only. Tickets to follow.

---

## Executive Summary

This audit found **3 Critical security vulnerabilities**, **4 High-severity alignment/scoping issues**, and **6 Medium structural issues** across the protocol stack. The most severe finding is a **ChaCha20 nonce truncation bug** that makes decryption impossible at the ground station, and a **signature scope divergence** between SNLP and CCSDS tiers that leaves `sat_id` and `nonce` unsigned in SNLP mode.

The existing KSY Hardening Audit (`VOP_KSY_Hardening_Audit.md`) identified 6 critical faults. Of those, **none have been applied to the live KSY file** — the hardened modular KSY files exist only as drafts in `docs/todo/`. This audit extends that work with byte-level alignment analysis, cryptographic implementation review, and adversary-focused attack surface mapping.

---

## PART 1: CRITICAL SECURITY VULNERABILITIES

### SEC-01 — ChaCha20 Nonce Truncation (CRITICAL)

**File:** `void-core/src/security_manager.cpp:117-125`

```cpp
uint8_t nonce[12];
randombytes_buf(nonce, 12);       // 12 random bytes generated
uint32_t safe_nonce;
memcpy(&safe_nonce, nonce, 4);    // Only first 4 bytes kept
pkt.nonce = safe_nonce;           // Packet stores 4 bytes (uint32_t)
crypto_stream_chacha20_xor(pkt.enc_payload, payload_in, len, nonce, _session_key);
                                  // Encryption uses all 12 bytes
```

**The Problem:** ChaCha20 requires a 12-byte nonce. The firmware generates a full 12-byte random nonce and uses it for encryption, but only stores 4 bytes (`uint32_t nonce`) in the Packet B wire format. The remaining 8 bytes are lost. The Ground Station receives only 4 bytes and **cannot reconstruct the full nonce**, making decryption mathematically impossible.

**Attack Surface:** This is not exploitable by an adversary — it is a self-inflicted denial of service. No encrypted Packet B can ever be decrypted by the ground station.

**Additionally:** The protocol spec defines `nonce` as a monotonic counter (uint32_t), but the implementation uses `randombytes_buf`. Counter-based nonces are deterministic and reconstructible; random nonces are not. The spec and implementation disagree.

**Fix Required:**
- Option A: Use a counter-based nonce. Construct the 12-byte ChaCha20 nonce deterministically from the 4-byte counter (e.g., zero-pad the remaining 8 bytes, or derive from `sat_id || counter`).
- Option B: Expand the wire-format `nonce` field to 12 bytes. This adds 8 bytes to Packet B (184 → 192 SNLP, 176 → 184 CCSDS). Requires spec and KSY changes.

---

### SEC-02 — Signature Scope Divergence Between Tiers (CRITICAL)

**File:** `void-core/src/security_manager.cpp:134-135`

```cpp
crypto_sign_detached(pkt.signature, &sig_len,
    reinterpret_cast<uint8_t*>(&pkt), 108, _identity_priv);
//                                    ^^^
//  Signs exactly 108 bytes from start of struct, regardless of header size
```

**The Problem:** The signing function always signs 108 bytes from `&pkt`. But `VoidHeader_t` is **6 bytes** (CCSDS) or **14 bytes** (SNLP). This means:

| Tier | Header | Signed Region (108 bytes) | Covers sat_id? | Covers nonce? |
|------|--------|---------------------------|-----------------|---------------|
| CCSDS | 6B | header(6) + epoch_ts(8) + pos_vec(24) + enc_payload(62) + sat_id(4) + nonce(4) | **YES** | **YES** |
| SNLP | 14B | header(14) + epoch_ts(8) + pos_vec(24) + enc_payload(62) | **NO** | **NO** |

In SNLP mode, the 14-byte header consumes 8 additional bytes of the signed region, pushing `sat_id` and `nonce` outside the signature boundary.

**Attack:** An adversary capturing an SNLP Packet B over RF can:
1. Replace `sat_id` with another satellite's ID (identity spoofing)
2. Replace `nonce` with a previously-used value (replay attack bypass)
3. The Ed25519 signature remains valid because it does not cover those fields

**Additionally:** The CCSDS spec says the signature covers "offsets 00-107" (header-inclusive). The SNLP spec says "offsets 12-113" (header-exclusive, body-only). These define different scoping philosophies but the code implements neither correctly for SNLP.

**Fix Required:** The signed byte count must be header-size-aware:
```
signed_length = sizeof(VoidHeader_t) + 102
// CCSDS: 6 + 102 = 108 (covers through nonce) ✅
// SNLP: 14 + 102 = 116 (covers through nonce) ✅
```
Or: sign body-only (from `epoch_ts` to `nonce`, always 102 bytes) and document that the header is never signed. This is the cleaner approach and matches the SNLP spec's "header is discarded before validation" philosophy. The gateway verifier must match.

---

### SEC-03 — No Buffer Length Validation Before Struct Cast (CRITICAL)

**Files:** `satellite-firmware/src/buyer.cpp`, `satellite-firmware/src/seller.cpp`

The firmware receives radio packets via RadioLib and overlays them onto packed structs using `reinterpret_cast` or `memcpy`. There is no validation that the received byte count matches `sizeof(PacketX_t)` before the cast.

**Attack (Static Memory Manipulation):** An adversary transmitting a deliberately truncated packet (e.g., 80 bytes when 184 are expected) causes the firmware to read beyond the received data into whatever is already in the radio buffer. On the ESP32-S3, the SX1262 FIFO is shared memory. Residual data from previous receptions (which may include fragments of previous signatures, nonces, or key exchanges) is interpreted as fields of the current packet.

**Specific Scenarios:**
- Truncated Packet B: `signature[64]` field reads from stale buffer memory, potentially leaking signature fragments from a previous valid packet.
- Truncated Packet H: `eph_pub_key[32]` reads stale key material, potentially causing ECDH with a known/weak public key.

**Fix Required:** Before any struct cast, assert `received_length == sizeof(PacketX_t)`. Reject packets that are short OR long.

---

## PART 2: HIGH-SEVERITY ALIGNMENT & SCOPING ISSUES

### ALN-01 — SNLP Header Size: Spec vs Code vs Audit Recommendation (Unresolved)

Three conflicting sources define three different SNLP header sizes:

| Source | Sync | CCSDS | Pad | Total | 64-bit Aligned? |
|--------|------|-------|-----|-------|-----------------|
| `Protocol-spec-SNLP.md` (Section 2) | 4B | 6B | **2B** | **12B** | No (12 % 8 = 4) |
| `void_packets_snlp.h` + `void_protocol_final_v2.ksy` | 4B | 6B | **4B** | **14B** | No (14 % 8 = 6) |
| KSY Hardening Audit (Section 7, Recommendation) | 4B | 6B | **6B** | **16B** | **Yes** (16 % 8 = 0) |

**Impact:** The 14-byte header (current code) means the first payload field (`epoch_ts`, a uint64_t) starts at offset 14, which is NOT 8-byte aligned. On ARM Cortex-M or RISC-V without unaligned access support, this causes a **hardware fault**. On ESP32-S3 (Xtensa), it works but requires two 32-bit loads instead of one 64-bit load.

**Decision Required:** Choose ONE header size and align all three sources. Options:

| Option | Header | First u64 at | Pros | Cons |
|--------|--------|-------------|------|------|
| A (Spec) | 12B | Offset 12 (12%8=4) ❌ | Spec-compliant | Still misaligned; body sizes diverge per-tier |
| B (Current) | 14B | Offset 14 (14%8=6) ❌ | Unified body sizes | Neither 32 nor 64-bit aligned |
| C (Audit) | 16B | Offset 16 (16%8=0) ✅ | Perfect 64-bit alignment | +2 bytes per SNLP packet; all sizes change |

**Recommendation:** Option C (16B header). The 2 extra bytes per packet are negligible vs the guaranteed 64-bit alignment of every subsequent field. All SNLP packet sizes increase by 2B (e.g., Packet B: 184 → 186B). 186 % 8 = 2, so add 6 more bytes of tail padding to reach 192B (192 % 8 = 0). This stays within LoRa SF7-SF9 payload limits (max 222B at SF9).

---

### ALN-02 — Packet A and Packet B: epoch_ts (u64) Misaligned in Both Tiers

**Files:** `void_packets_snlp.h:70-79`, `void_packets_ccsds.h:71-80`

Packets A and B place `epoch_ts` (uint64_t) immediately after the header with no alignment padding. Unlike Packets H, C, and D (which all have `_pad_head` or `session_ttl` to push the first u64 to an 8-byte boundary), Packets A and B skip this step.

**Byte-level proof (CCSDS Packet B):**
```
Offset 0-5:   VoidHeader_t     (6B)
Offset 6-13:  epoch_ts (u64)   → 6 % 8 = 6  ❌ NOT 8-byte aligned
Offset 14-21: pos_vec[0] (f64) → 14 % 8 = 6  ❌
Offset 22-29: pos_vec[1] (f64) → 22 % 8 = 6  ❌
Offset 30-37: pos_vec[2] (f64) → 30 % 8 = 6  ❌
```

**Byte-level proof (SNLP Packet B):**
```
Offset 0-13:  VoidHeader_t     (14B)
Offset 14-21: epoch_ts (u64)   → 14 % 8 = 6  ❌ NOT 8-byte aligned
Offset 22-29: pos_vec[0] (f64) → 22 % 8 = 6  ❌
```

**Impact:** Every `epoch_ts` and `pos_vec` access in Packets A and B requires the CPU to perform two 32-bit loads and a manual combine on architectures that enforce alignment. On ESP32-S3 (Xtensa), this is a ~2x penalty per field access.

**Contrast with well-aligned packets:**
- PacketH_t: `session_ttl` (2B) after 6B header → `timestamp` (u64) at offset 8 ✅
- PacketC_t: `_pad_head` (2B) after 6B header → `exec_time` (u64) at offset 8 ✅

**Fix Required:** Add 2-byte `_pad_head` to Packets A and B, matching the pattern already used in Packets C and D. This increases body sizes by 2B. Update KSY dispatch table, spec docs, and size constants.

---

### ALN-03 — Signature (64B) Misaligned in Packet B and TunnelData

Ed25519 signatures are 64 bytes. While they are byte arrays and don't require hardware alignment for correctness, crypto libraries (including libsodium) can optimise verification when the signature buffer starts on an 8-byte boundary. More critically, the signed message region must align with what both signer and verifier expect.

**Packet B signature offset:**
```
CCSDS: signature at offset 108 → 108 % 8 = 4  ❌
SNLP:  signature at offset 116 → 116 % 8 = 4  ❌
```

**TunnelData ground_sig offset:**
```
CCSDS: ground_sig at offset 20 → 20 % 8 = 4  ❌
SNLP:  ground_sig at offset 28 → 28 % 8 = 4  ❌
```

**Contrast:** PacketH signature is at offset 48 (48 % 8 = 0 ✅) and PacketC signature is at offset 32 (32 % 8 = 0 ✅). These packets demonstrate the correct pattern.

**Fix Required:** Add 4 bytes of padding before the signature field in Packet B and TunnelData to push them to 8-byte boundaries. This requires increasing packet sizes. Alternatively, if the header is expanded to 16B (ALN-01 Option C), recalculate all offsets — the cascade may naturally align signatures.

---

### ALN-04 — RelayOps uint32_t Fields Misaligned in PacketAck

**File:** `void_packets_ccsds.h:145-156`

```
PacketAck_t (CCSDS):
Offset 14-25: relay_ops (12B)
  Offset 14-15: azimuth (u16)      → 14 % 2 = 0  ✅
  Offset 16-17: elevation (u16)    → 16 % 2 = 0  ✅
  Offset 18-21: frequency (u32)    → 18 % 4 = 2  ❌ NOT 4-byte aligned
  Offset 22-25: duration_ms (u32)  → 22 % 4 = 2  ❌ NOT 4-byte aligned
```

**Cause:** After `status` (1B) + `_pad_b` (1B) = 2B at offset 12-13, the 12-byte RelayOps sub-struct starts at offset 14. The two u16 fields consume 4B, putting the first u32 (`frequency`) at offset 18 (not 4-byte aligned).

**Fix:** Add 2 bytes of padding before `relay_ops` (expanding `_pad_b` from 1B to 3B, or adding a separate `_pad_relay` of 2B) to push `relay_ops` to offset 16. Then `frequency` lands at offset 20 (20 % 4 = 0 ✅) and `duration_ms` at offset 24 (24 % 4 = 0 ✅).

---

## PART 3: MEDIUM-SEVERITY ISSUES

### MED-01 — Live KSY Still Uses `_io.size` for Dispatch (Audit F-02, Unfixed)

**File:** `void_protocol_final_v2.ksy:80-81`

```yaml
payload_len:
  value: >-
    is_snlp ? (_io.size - 14) : (_io.size - 6)
```

The live KSY depends on `_io.size` (stream length) rather than the CCSDS `packet_data_length` field. This breaks on:
- MQTT byte streams (multiple concatenated packets)
- File-based test vectors (concatenated `.bin` files)
- Any non-discrete-buffer transport

The hardened KSY draft (`docs/todo/void_protocol (1).ksy`) fixes this by using `ccsds.data_field_length`. But the fix has not been applied to the live file.

---

### MED-02 — Live KSY seq_count Mask Still 10-bit (Audit C-01, Unfixed)

**File:** `void_protocol_final_v2.ksy:131`

```yaml
seq_count:
  value: seq_flags_count & 0x3FF    # 10-bit mask — SHOULD BE 0x3FFF (14-bit)
```

The CCSDS Sequence Count is a 14-bit field (0-16383). The 10-bit mask truncates it to 0-1023. The counter wraps every ~17 minutes instead of ~4.5 hours, creating a replay deduplication collision window.

The hardened KSY draft (`docs/todo/ccsds_primary_header.ksy:62`) uses the correct `0x3FFF` mask. Not applied to the live file.

---

### MED-03 — Heartbeat Packets Have No Cryptographic Authentication

**Files:** `void_packets_snlp.h:197-221`, `void_packets_ccsds.h:199-223`

Heartbeat packets carry GPS coordinates, battery status, temperature, and system state — all protected only by CRC32 (4 bytes). CRC32 is an error-detection code, NOT a cryptographic primitive. It provides zero protection against intentional forgery.

**Attack (RF Injection):** An adversary with knowledge of the protocol can forge heartbeat packets with false telemetry:
- Spoof GPS coordinates to mislead tracking
- Report false battery levels to trigger ground-side emergency procedures
- Set `sys_state` to error codes to cause unnecessary operational responses

**Fix Required:** Add an HMAC-SHA256 truncated tag (16B) or an Ed25519 signature (64B) to heartbeat packets. The existing `VOP_KSY_Hardening_Audit.md` defers this to v2.2. Given the adversary model (RF-capable attacker), this should be prioritised.

---

### MED-04 — PacketD global_crc Misaligned Due to 98-Byte Payload

**File:** `void_packets_ccsds.h:187-197`

```
Offset 20-117: payload (98B)
Offset 118-121: global_crc (u32)  → 118 % 4 = 2  ❌ NOT 4-byte aligned
```

The 98-byte payload creates a 2-byte misalignment for the subsequent `global_crc` (uint32_t). This forces the CPU to perform unaligned 32-bit access for CRC verification.

**Fix:** The stripped Packet C inside the payload is 98 bytes because the 6B CCSDS header is removed from the 104B Packet C. If 2 bytes of padding are added after the payload (making it a 100-byte field, or adding an explicit `_pad_crc` of 2B), `global_crc` would land at offset 120 (120 % 4 = 0 ✅).

---

### MED-05 — Comment Offsets in SNLP Header File Reference CCSDS Offsets

**File:** `void_packets_snlp.h` (all packet structs)

Every struct in the SNLP file has offset comments copied from the CCSDS version:

```cpp
// SNLP PacketB_t (void_packets_snlp.h)
VoidHeader_t header;        // 00-05: Big-Endian     ← WRONG: SNLP header is 14 bytes (00-13)
uint64_t     epoch_ts;      // 06-13: Little-Endian  ← WRONG: actually offset 14-21
double       pos_vec[3];    // 14-37: Little-Endian  ← WRONG: actually offset 22-45
uint8_t      signature[64]; // 108-171: PUF Signature ← WRONG: actually offset 116-179
```

**Impact:** A developer or security auditor using these comments as a reference will compute incorrect signature scope boundaries, incorrect CRC ranges, and incorrect gateway verification offsets. This is a documentation-level bug but has direct security implications.

**Fix:** Recalculate and correct all offset comments in `void_packets_snlp.h` to reflect the actual 14-byte SNLP header.

---

### MED-06 — Hardcoded Demo Keys in Production Source

**File:** `void-core/src/security_manager.cpp:27-31`

```cpp
#ifdef ROLE_SELLER
    env_key = "Q9p4M&JjEUrmf3fW$i5AfWkWNREpEN8OYHXUg6F0^R9O2nm5EoQor%mtlcVCyLlC";
#else
    env_key = "!1V2#E!UDzTeUjEvqYLbHt8BFnsnlWsIQPrl44zmPZZ8bSkqwng&JPQJQ2QHZVIx";
#endif
```

These are clearly marked as demo keys, but they reside in the main source tree. An embedded software attacker performing firmware extraction (JTAG, UART, or flash dump) recovers these strings immediately. If the demo firmware is ever deployed without changing keys, all identity signatures are compromised.

**Fix:** Move demo keys to a separate `demo_keys.h` that is `.gitignore`'d or clearly segregated. Add a compile-time `#error` if `PRODUCTION` flag is set and demo keys are still present.

---

## PART 4: KSY DEPLOYMENT STATUS

### Status of Existing Hardening Audit Findings

| Audit ID | Severity | Description | Fix Status |
|----------|----------|-------------|------------|
| **C-01** | CRITICAL | `seq_count` mask 0x3FF (10-bit) | **Draft fixed** in `docs/todo/ccsds_primary_header.ksy:62`. NOT in live KSY. |
| **C-02** | WARNING | CCSDS `version` field unvalidated | **Draft fixed** in `docs/todo/ccsds_primary_header.ksy:42`. NOT in live KSY. |
| **C-03** | WARNING | `packet_length` computed but unused for dispatch | **Draft fixed** in `docs/todo/void_protocol (1).ksy:101-108`. NOT in live KSY. |
| **F-01** | CRITICAL | SNLP header 14B in KSY, 12B in spec | **OPEN.** Hardened drafts use 12B (Option A). Code still uses 14B. Decision needed. |
| **F-02** | CRITICAL | `payload_len` uses `_io.size` | **Draft fixed** in `docs/todo/void_protocol (1).ksy:101-108`. NOT in live KSY. |
| **F-03** | WARNING | `dispatch_122` no CRC pre-check | **OPEN.** Cannot fix in KSY alone; requires application-layer hook. |
| **F-04** | WARNING | `enc_tunnel` runtime ternary size | **OPEN.** Hardened draft retains ternary with audit note. |
| **K-01** | INFO | Monolithic single-file KSY | **Draft fixed** — modular decomposition in `docs/todo/`. NOT deployed. |
| **K-02** | INFO | No enums for semantic fields | **Draft fixed** in `docs/todo/tig_common_types.ksy`. NOT deployed. |
| **K-03** | INFO | No `valid` constraints | **Draft fixed** across all hardened KSY files. NOT deployed. |
| **K-04** | INFO | `doc-ref` multi-line scalar | **Draft fixed** in `docs/todo/void_protocol (1).ksy:35-38`. NOT deployed. |

**Bottom line:** The hardened KSY files exist as drafts but zero fixes have been applied to the live `void_protocol_final_v2.ksy`. The gateway's auto-generated parser (`void_protocol.go`) was built from the **unhardened** KSY.

---

## PART 5: FULL ALIGNMENT MAP (All Packets, Both Tiers)

### Legend
- ✅ = Field starts on its natural alignment boundary
- ❌ = Field is misaligned (requires multi-cycle access on strict-alignment architectures)

### CCSDS Tier (6-Byte Header)

| Packet | Field | Type | Offset | Natural Align | Aligned? |
|--------|-------|------|--------|---------------|----------|
| **A** | epoch_ts | u64 | 6 | 8 | ❌ (6%8=6) |
| **A** | pos_vec[0] | f64 | 14 | 8 | ❌ (14%8=6) |
| **A** | sat_id | u32 | 50 | 4 | ❌ (50%4=2) |
| **A** | amount | u64 | 54 | 8 | ❌ (54%8=6) |
| **B** | epoch_ts | u64 | 6 | 8 | ❌ (6%8=6) |
| **B** | pos_vec[0] | f64 | 14 | 8 | ❌ (14%8=6) |
| **B** | signature | 64B | 108 | 8 | ❌ (108%8=4) |
| **H** | timestamp | u64 | 8 | 8 | ✅ |
| **H** | eph_pub_key | 32B | 16 | 8 | ✅ |
| **H** | signature | 64B | 48 | 8 | ✅ |
| **C** | exec_time | u64 | 8 | 8 | ✅ |
| **C** | signature | 64B | 32 | 8 | ✅ |
| **D** | downlink_ts | u64 | 8 | 8 | ✅ |
| **D** | global_crc | u32 | 118 | 4 | ❌ (118%4=2) |
| **ACK** | frequency | u32 | 18 | 4 | ❌ (18%4=2) |
| **ACK** | duration_ms | u32 | 22 | 4 | ❌ (22%4=2) |
| **Tunnel** | block_nonce | u64 | 8 | 8 | ✅ |
| **Tunnel** | ground_sig | 64B | 20 | 8 | ❌ (20%8=4) |
| **Heartbeat** | epoch_ts | u64 | 6 | 8 | ❌ (6%8=6) |
| **Heartbeat** | pressure_pa | u32 | 18 | 4 | ❌ (18%4=2) |

### SNLP Tier (14-Byte Header)

| Packet | Field | Type | Offset | Natural Align | Aligned? |
|--------|-------|------|--------|---------------|----------|
| **A** | epoch_ts | u64 | 14 | 8 | ❌ (14%8=6) |
| **A** | sat_id | u32 | 58 | 4 | ❌ (58%4=2) |
| **A** | amount | u64 | 62 | 8 | ❌ (62%8=6) |
| **B** | epoch_ts | u64 | 14 | 8 | ❌ (14%8=6) |
| **B** | signature | 64B | 116 | 8 | ❌ (116%8=4) |
| **H** | timestamp | u64 | 16 | 8 | ✅ |
| **H** | signature | 64B | 56 | 8 | ✅ |
| **C** | exec_time | u64 | 16 | 8 | ✅ |
| **C** | signature | 64B | 40 | 8 | ✅ |
| **D** | downlink_ts | u64 | 16 | 8 | ✅ |
| **D** | global_crc | u32 | 126 | 4 | ❌ (126%4=2) |
| **ACK** | frequency | u32 | 26 | 4 | ❌ (26%4=2) |
| **Heartbeat** | epoch_ts | u64 | 14 | 8 | ❌ (14%8=6) |
| **Heartbeat** | pressure_pa | u32 | 26 | 4 | ❌ (26%4=2) |

### Alignment Summary

| Packet | 64-bit fields aligned? | 32-bit fields aligned? | Signature 8-byte aligned? | Total size % 8 = 0? |
|--------|----------------------|----------------------|--------------------------|---------------------|
| **H** | ✅ | ✅ | ✅ | ✅ (112/120) |
| **C** | ✅ | ✅ | ✅ | ✅ (104/112) |
| **D** | ✅ | ❌ (global_crc) | N/A | ✅ (128/136) |
| **A** | ❌ | ❌ (sat_id) | N/A | ❌/✅ (68/76) |
| **B** | ❌ | ✅ | ❌ | ✅ (176/184) |
| **ACK** | ✅ | ❌ (relay_ops) | N/A | ✅ (120/136) |
| **Tunnel** | ✅ | ✅ | ❌ | ✅ (88/96) |
| **Heartbeat** | ❌ | ❌ (pressure_pa) | N/A | ✅ (40/48) |

**Result:** Only Packets H and C achieve full alignment across all field types. Packets A, B, and Heartbeat have the worst alignment due to the missing `_pad_head` after the header.

---

## PART 6: CONSOLIDATED FINDINGS TABLE

| ID | Severity | Category | Summary | Effort |
|----|----------|----------|---------|--------|
| **SEC-01** | CRITICAL | Crypto | ChaCha20 nonce truncation — 12B nonce used, 4B stored. Decryption impossible. | Medium |
| **SEC-02** | CRITICAL | Crypto | Signature scope divergence — `sat_id` and `nonce` unsigned in SNLP mode. | Medium |
| **SEC-03** | CRITICAL | Memory Safety | No buffer length check before struct cast. Stale memory interpreted as fields. | Small |
| **ALN-01** | HIGH | Architecture | SNLP header size: spec (12B) vs code (14B) vs recommended (16B). Unresolved. | Large (cascading) |
| **ALN-02** | HIGH | Alignment | Packets A, B, Heartbeat: epoch_ts (u64) not 8-byte aligned. Missing `_pad_head`. | Medium |
| **ALN-03** | HIGH | Alignment | PacketB and TunnelData signatures not 8-byte aligned. | Medium |
| **ALN-04** | HIGH | Alignment | PacketAck RelayOps u32 fields not 4-byte aligned. | Small |
| **MED-01** | MEDIUM | KSY | Live KSY uses `_io.size` for dispatch (breaks on streams). Hardened draft exists. | Small |
| **MED-02** | MEDIUM | KSY | Live KSY `seq_count` mask still 10-bit. Hardened draft exists. | Small |
| **MED-03** | MEDIUM | Security | Heartbeat packets unauthenticated (CRC32 only, no HMAC/signature). | Medium |
| **MED-04** | MEDIUM | Alignment | PacketD `global_crc` misaligned due to 98B payload. | Small |
| **MED-05** | MEDIUM | Documentation | SNLP header file offset comments reference CCSDS offsets. | Small |
| **MED-06** | MEDIUM | Security | Demo keys hardcoded in production source tree. | Small |

---

## PART 7: RECOMMENDED TICKET SEQUENCE

The following order respects dependency chains and prioritises security:

### Phase 1: Fix Security Showstoppers (Before any further integration testing)
1. **SEC-01** — Fix ChaCha20 nonce construction (spec + firmware + gateway)
2. **SEC-02** — Fix signature scope to be header-size-aware (firmware + gateway + spec docs)
3. **SEC-03** — Add buffer length validation before all struct casts (firmware)

### Phase 2: Resolve Architecture Decision (Drives all subsequent alignment work)
4. **ALN-01** — Decide SNLP header size (12B / 14B / 16B). Update spec, code, KSY, and all size constants. This is the "big bang" change — everything downstream depends on it.

### Phase 3: Apply Alignment Fixes (Requires ALN-01 decision)
5. **ALN-02** — Add `_pad_head` to Packets A, B, Heartbeat
6. **ALN-03** — Align signatures in Packet B and TunnelData
7. **ALN-04** — Align RelayOps fields in PacketAck
8. **MED-04** — Align PacketD `global_crc`

### Phase 4: Deploy Hardened KSY (Parallel with Phase 3)
9. **MED-01 + MED-02 + K-01 through K-04** — Replace live monolithic KSY with hardened modular KSY files from `docs/todo/`. Regenerate all parsers (Go, Python, C++).

### Phase 5: Hardening
10. **MED-03** — Add HMAC or signature to heartbeat packets
11. **MED-05** — Fix SNLP offset comments
12. **MED-06** — Segregate demo keys from production source

---

*This audit was performed against commit bfa7950 on 2026-04-14. No code was modified.*

*Prepared for Tiny Innovation Group Ltd.*
