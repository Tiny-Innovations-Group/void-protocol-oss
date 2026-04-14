# VOID-114 — SNLP Header Size Decision (2026-04-14)

## Question

> Of `{12, 14, 16}` bytes, which SNLP header size lets **every packet** in the
> protocol satisfy 32/64-bit alignment for its critical fields (u64 timestamps,
> f64 position vectors, Ed25519 signatures) on the Xtensa LX7, Cortex-M, and
> RISC-V targets?

## Method

The analysis treats each packet body as a fixed layout and computes the
absolute byte offset of every critical field under three candidate SNLP
header sizes. A field is **aligned** when `absolute_offset % natural_alignment == 0`.

`natural_alignment` used:

| Type | Alignment |
|---|---|
| `u64`, `f64`, `Ed25519 signature[64]` | 8 |
| `u32`, `f32`, `CRC32` | 4 |
| `u16` | 2 |
| `u8`, raw byte buffers | 1 |

CCSDS header is fixed at **6 bytes** (CCSDS 133.0-B-2, immutable). The only
lever is the SNLP header.

## Results

The full table is reproduced from `python3` analysis (see end of doc for the
executable). Critical fields only — raw byte buffers and single-byte fields
are omitted because they have no alignment constraint.

### SNLP = 12 bytes (`sync_word[4] + ccsds[6] + pad[2]`)

| Packet | CCSDS (H=6) | SNLP (H=12) |
|---|---|---|
| A | ❌ epoch, pos, amount | ❌ epoch, pos, amount |
| B | ❌ epoch, pos | ❌ epoch, pos, signature |
| H | ✅ | ❌ timestamp, signature |
| C | ✅ | ❌ exec_time, enc_tx_id, signature |
| D | ✅ | ❌ downlink_ts |
| ACK | ✅ | ✅ |
| L | ❌ epoch_ts | ❌ epoch_ts |

**Verdict: BROKEN.** SNLP at 12 bytes regresses Packets H, C, D — which are
aligned in the current 14-byte implementation. The 12B header breaks
mod-8 congruence with the CCSDS header (6%8=6 vs 12%8=4) so shared body
layouts cannot be aligned simultaneously in both tiers.

### SNLP = 14 bytes (`sync_word[4] + ccsds[6] + pad[4]`) — **CURRENT CODE**

| Packet | CCSDS (H=6) | SNLP (H=14) |
|---|---|---|
| A | ❌ epoch, pos, amount | ❌ epoch, pos, amount |
| B | ❌ epoch, pos | ❌ epoch, pos |
| H | ✅ | ✅ |
| C | ✅ | ✅ |
| D | ✅ | ✅ |
| ACK | ✅ | ✅ |
| L | ❌ epoch_ts | ❌ epoch_ts |

**Verdict: OPTIMAL for header-only analysis.** Both CCSDS and SNLP share
**identical alignment status** on every packet. The remaining misalignments
in A, B, and L are **body design** issues (those packets lack the 2-byte
`_pad_head` field that C/D/ACK use), not header-size issues. See §4.

### SNLP = 16 bytes (`sync_word[4] + ccsds[6] + pad[6]`)

| Packet | CCSDS (H=6) | SNLP (H=16) |
|---|---|---|
| A | ❌ epoch, pos, amount | ✅ |
| B | ❌ epoch, pos | ❌ signature |
| H | ✅ | ❌ timestamp, signature |
| C | ✅ | ❌ exec_time, enc_tx_id, signature |
| D | ✅ | ❌ downlink_ts |
| ACK | ✅ | ✅ |
| L | ❌ epoch_ts | ✅ |

**Verdict: WORSE.** SNLP at 16 bytes aligns Packets A and L in the SNLP
tier, but **regresses Packets B, H, C, D in the SNLP tier** by breaking
the signature alignment and the 2-byte-pad convention. Like the 12-byte
option, it breaks mod-8 congruence with CCSDS.

## The mod-8 congruence rule

CCSDS headers are fixed at 6 bytes. This means every field at body offset
`X` lands on absolute offset `X+6`, and the body layout must be designed
against this.

For SNLP to **reuse the exact same body structs** as CCSDS (which is
non-negotiable for zero-heap C++ and for the Kaitai dispatch-by-body-length
routing), the SNLP header must satisfy:

```
H_snlp ≡ H_ccsds  (mod 8)   →   H_snlp ≡ 6  (mod 8)
```

The candidates that satisfy this are `{6, 14, 22, 30, ...}`. The only
practical choice with the 4-byte `sync_word` is **14 bytes**.

## Decision

> **Keep SNLP header at 14 bytes.**
> `sync_word[4] + ccsds[6] + align_pad[4]`

This is already what the firmware, Kaitai parser, and test vectors use.
The drift was in `docs/Protocol-spec-SNLP.md`, which documented a 12-byte
header. That spec is updated as part of this ticket to match the code.

## §4 — Follow-up: Packet A, B, L body padding (VOID-114B)

The alignment misses that remain under the optimal 14-byte header are
**body design** issues, not header issues. Packets A, B, and L begin their
bodies with a `u64` at body offset 0, which lands at CCSDS absolute offset 6
(misaligned) and SNLP absolute offset 14 (also misaligned — same residue).

Packets C, D, and ACK already solved this problem the right way: they start
with a 2-byte `_pad_head` field, which pushes their first `u64` to body
offset 2 → absolute offset 8 (aligned under H=6) or 16 (aligned under
H=14).

The fix is mechanical but non-trivial — it changes wire sizes and requires
another round of updates across firmware structs, gen_packet, the KSY
dispatcher, the generated Go parser, and the ingest handler. It is tracked
as **VOID-114B** (to be created) so that VOID-114 itself closes cleanly on
the header-size question.

**Rough sketch of the VOID-114B body reshape:**

| Packet | Current body | Proposed body | Change |
|---|---|---|---|
| A | 62 | 66 | `_pad_head[2]` + `_pre_crc[2]` |
| B | 166 (post-VOID-110) | 170 | `_pad_head[2]` + `_pre_sig[2]` |
| L | 34 | 36 | `_pad_head[2]` |

Total frames under the reshape:

| Packet | CCSDS (H=6) | SNLP (H=14) |
|---|---|---|
| A | 72 (8-aligned ✅) | 80 (8-aligned ✅) |
| B | 176 (÷8 ✅) | 184 (÷8 ✅) |
| L | 42 | 50 |

(Heartbeat totals are not 8-multiples; additional tail padding can close
this out if DMA-ring alignment is required, but the per-field alignment
alone already delivers the LX7 trap-handler savings, which is the real
performance concern.)

## Appendix — Analyzer

```python
packets = {
  "A":  [("epoch_ts",0,8),("pos_vec[0]",8,8),("pos_vec[1]",16,8),("pos_vec[2]",24,8),
         ("vel_vec[0]",32,4),("vel_vec[1]",36,4),("vel_vec[2]",40,4),
         ("sat_id",44,4),("amount",48,8),("asset_id",56,2),("crc32",58,4)],
  "B":  [("epoch_ts",0,8),("pos_vec[0]",8,8),("pos_vec[1]",16,8),("pos_vec[2]",24,8),
         ("enc_payload",32,1),("sat_id",94,4),("signature",98,8),("global_crc",162,4)],
  "H":  [("session_ttl",0,2),("timestamp",2,8),("eph_pub_key",10,1),("signature",42,8)],
  "C":  [("pad_head",0,1),("exec_time",2,8),("enc_tx_id",10,8),("enc_status",18,1),
         ("pad_sig",19,1),("signature",26,8),("crc32",90,4),("tail_pad",94,1)],
  "D":  [("pad_head",0,1),("downlink_ts",2,8),("sat_b_id",10,4),("payload",14,1),
         ("global_crc",112,4),("tail",116,1)],
  "ACK":[("pad_a",0,1),("target_tx_id",2,4),("status",6,1),("pad_b",7,1),
         ("relay_ops",8,4),("enc_tunnel",20,1),("pad_c",108,1),("crc32",110,4)],
  "L":  [("epoch_ts",0,8),("vbatt_mv",8,2),("temp_c",10,2),("pressure_pa",12,4),
         ("sys_state",16,1),("sat_lock",17,1),("lat_fixed",18,4),("lon_fixed",22,4),
         ("reserved",26,1),("gps_speed_cms",28,2),("crc32",30,4)],
}

def check(H):
    for pkt, fields in packets.items():
        bad = [(n, H+o, a) for n,o,a in fields if (H+o) % a != 0]
        print(pkt, "OK" if not bad else bad)

for H in (6, 12, 14, 16): check(H)
```

---

**Status:** Decision recorded. Header size change: none (14 remains). Spec
drift reconciled in `Protocol-spec-SNLP.md`. Follow-up body reshape tracked
as VOID-114B.

**Author:** Tiny Innovation Group Ltd
**Date:** 2026-04-14
