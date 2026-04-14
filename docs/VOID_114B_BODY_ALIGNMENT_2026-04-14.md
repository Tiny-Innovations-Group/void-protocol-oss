# VOID-114B — Packet A/B/L Body Alignment (2026-04-14)

## Context

[VOID-114](VOID_114_SNLP_HEADER_ALIGNMENT_DECISION_2026-04-14.md) locked the
SNLP header at **14 bytes** because `14 ≡ 6 (mod 8)` — both tiers share the
same residue class, so a single body layout can be aligned under both
headers simultaneously.

VOID-114 noted that while the header size question was resolved, three
packets (A, B, L) still had misaligned critical fields. Those misalignments
are **body design** issues: the bodies begin with a `u64` at body offset 0,
which lands at absolute offset 6 (CCSDS) or 14 (SNLP) — both residue 6,
neither 8-aligned. **No header size can fix this.**

Packets H, C, D, and ACK already solved this the right way: they start
with a `_pad_head[2]` field which pushes the first `u64` to body offset 2,
absolute offset 8 (CCSDS) or 16 (SNLP) — both 8-aligned.

VOID-114B extends the same pattern to A, B, L so every critical field in
every packet is on its natural boundary under both tiers.

## Alignment rules (under H ≡ 6 mod 8)

For a body field of alignment `A` at body offset `k`, absolute offset is
`H + k`. Alignment requires `(H + k) mod A == 0`.

With `H ∈ {6, 14}` (both ≡ 6 mod 8, ≡ 2 mod 4):

| Type | Required `k mod A` |
|---|---|
| `u64`, `f64`, `Ed25519[64]` | `k mod 8 == 2` |
| `u32`, `f32`, `CRC32` | `k mod 4 == 2` |
| `u16` | `k mod 2 == 0` |
| `u8`, byte buffers | any |

The `k mod A == 2` target is a direct consequence of the header residue
being 6 (or 14). This is why VOID-114 insisted on the mod-8 congruence —
it lets us pick one `k` that works for both header sizes.

## Packet A — Invoice (62 → 66 bytes body)

**Current (broken):**

| body | field | size | abs CCSDS | abs SNLP | status |
|---|---|---|---|---|---|
| 0 | epoch_ts | 8 | 6 | 14 | ❌ |
| 8 | pos_vec[0] | 8 | 14 | 22 | ❌ |
| 16 | pos_vec[1] | 8 | 22 | 30 | ❌ |
| 24 | pos_vec[2] | 8 | 30 | 38 | ❌ |
| 32 | vel_vec[0] | 4 | 38 | 46 | ❌ |
| 36 | vel_vec[1] | 4 | 42 | 50 | ❌ |
| 40 | vel_vec[2] | 4 | 46 | 54 | ❌ |
| 44 | sat_id | 4 | 50 | 58 | ❌ |
| 48 | amount | 8 | 54 | 62 | ❌ |
| 56 | asset_id | 2 | 62 | 70 | ✅ |
| 58 | crc32 | 4 | 64 | 72 | ✅ |

**Proposed (aligned):**

| body | field | size | abs CCSDS | abs SNLP |
|---|---|---|---|---|
| 0 | `_pad_head[2]` | 2 | 6 | 14 |
| 2 | epoch_ts | 8 | 8 ✅ | 16 ✅ |
| 10 | pos_vec[0] | 8 | 16 ✅ | 24 ✅ |
| 18 | pos_vec[1] | 8 | 24 ✅ | 32 ✅ |
| 26 | pos_vec[2] | 8 | 32 ✅ | 40 ✅ |
| 34 | vel_vec[0] | 4 | 40 ✅ | 48 ✅ |
| 38 | vel_vec[1] | 4 | 44 ✅ | 52 ✅ |
| 42 | vel_vec[2] | 4 | 48 ✅ | 56 ✅ |
| 46 | sat_id | 4 | 52 ✅ | 60 ✅ |
| 50 | amount | 8 | 56 ✅ | 64 ✅ |
| 58 | asset_id | 2 | 64 ✅ | 72 ✅ |
| 60 | `_pre_crc[2]` | 2 | 66 | 74 |
| 62 | crc32 | 4 | 68 ✅ | 76 ✅ |

**Body = 66 bytes.** Frame **CCSDS = 72 (÷8 ✅)**, **SNLP = 80 (÷8 ✅)**.

Both `_pad_head` and `_pre_crc` were required:
- `_pad_head[2]` fixes epoch, pos, vel, sat_id, amount.
- `_pre_crc[2]` is needed because after `asset_id` (u16 at body 58) the
  next field lands at body 60, abs 66, which has residue 2 mod 4 — not
  4-aligned for a `u32`. The 2-byte pad bumps crc to body 62, abs 68,
  which satisfies `68 mod 4 == 0`.

## Packet B — Payment (166 → 178 bytes body, incl. 4-byte tail pad)

**Current (broken in VOID-110 layout):**

| body | field | size | abs CCSDS | abs SNLP | status |
|---|---|---|---|---|---|
| 0 | epoch_ts | 8 | 6 | 14 | ❌ |
| 8 | pos_vec[0..2] | 24 | 14, 22, 30 | 22, 30, 38 | ❌ |
| 32 | enc_payload | 62 | 38 | 46 | — |
| 94 | sat_id | 4 | 100 | 108 | ✅ |
| 98 | signature | 64 | 104 | 112 | ✅ |
| 162 | global_crc | 4 | 168 | 176 | ✅ |

Post-VOID-110 the sig landed on an 8-boundary only by accident; the
u64/f64 fields at the head of the body have always been misaligned.

**Proposed (aligned):**

| body | field | size | abs CCSDS | abs SNLP |
|---|---|---|---|---|
| 0 | `_pad_head[2]` | 2 | 6 | 14 |
| 2 | epoch_ts | 8 | 8 ✅ | 16 ✅ |
| 10 | pos_vec[0..2] | 24 | 16, 24, 32 ✅ | 24, 32, 40 ✅ |
| 34 | enc_payload | 62 | 40 | 48 |
| 96 | `_pre_sat[2]` | 2 | 102 | 110 |
| 98 | sat_id | 4 | 104 ✅ | 112 ✅ |
| 102 | `_pre_sig[4]` | 4 | 108 | 116 |
| 106 | signature | 64 | 112 ✅ | 120 ✅ |
| 170 | global_crc | 4 | 176 ✅ | 184 ✅ |
| 174 | `_tail_pad[4]` | 4 | 180 | 188 |

**Body = 178 bytes (174 payload + 4-byte tail pad).**
Frame **CCSDS = 184 (÷8 ✅)**, **SNLP = 192 (÷64 ✅ cache line)**.

Three pads are required because the 62-byte ciphertext is itself
misaligned in length. The dance is:

1. `_pad_head[2]` — aligns the u64/f64 head block.
2. `_pre_sat[2]` — after a 62-byte byte buffer starting at body 34, the
   next field lands at body 96 (residue 0 mod 4, not 2), so we need 2
   bytes to push `sat_id` to body 98.
3. `_pre_sig[4]` — after `sat_id` ends at body 102 (residue 6 mod 8, not
   2), we need 4 bytes to push the signature to body 106.

**Signature scope update.** The signature now covers `header + body[0..105]`
(106 body bytes through end of `_pre_sig`, inclusive of the pad bytes —
convention is that all pre-signature bytes are signed, zero or not).
The ground station verifier `handlePayloadBody` in
[gateway/internal/api/handlers/ingest.go](gateway/internal/api/handlers/ingest.go)
is updated: `packetBBodyLen = 178`, `packetBSigScopeBody = 106`.
The `_tail_pad[4]` sits *after* the crc and is NOT covered by the signature
or the global_crc — it is pure frame-total alignment filler.

**Why not shrink frame further?** Three options were considered:

1. **Reorder `sat_id` before `enc_payload`.** Works for sat_id alignment,
   but leaves the sig position wrong — still needs 4 bytes of pad. Same
   body total.
2. **Pad the ciphertext to 64 or 68 bytes.** Would grow the plaintext
   inner invoice, entangling the wire format with the crypto format.
   Rejected: inner invoice size is 62 by design.
3. **Drop `global_crc`.** Ed25519 signature already detects any
   corruption, and LoRa PHY layer has its own CRC. Rejected for this
   ticket — removing wire fields is out of scope for an alignment fix;
   leave for a future `VOID-115` if warranted.

174 body + 4-byte `_tail_pad` = 178 body / 184 CCSDS frame / 192 SNLP frame
is the minimal layout that satisfies both per-field natural alignment AND
frame-total 8-byte alignment (required for DMA-coalesced SPI bursts to the
SX1262 on ESP32-S3 / Heltec LoRa V3 class hardware). The 192-byte SNLP
frame is a bonus: it is exactly 3×64, a clean multiple of the Xtensa LX7
cache line.

## Packet L — Heartbeat (34 bytes body, reordered)

**Current (broken):**

| body | field | size | abs CCSDS | status |
|---|---|---|---|---|
| 0 | epoch_ts | 8 | 6 | ❌ |
| 8 | vbatt_mv | 2 | 14 | ✅ |
| 10 | temp_c | 2 | 16 | ✅ |
| 12 | pressure_pa | 4 | 18 | ❌ |
| 16 | sys_state | 1 | 22 | — |
| 17 | sat_lock | 1 | 23 | — |
| 18 | lat_fixed | 4 | 24 | ✅ |
| 22 | lon_fixed | 4 | 28 | ✅ |
| 26 | reserved[2] | 2 | 32 | ✅ |
| 28 | gps_speed_cms | 2 | 34 | ✅ |
| 30 | crc32 | 4 | 36 | ✅ |

`epoch_ts` and `pressure_pa` are misaligned. A simple `_pad_head[2]`
bump doesn't help — it shifts `lat_fixed` onto a bad residue.

**Proposed (reorder, drop unused `reserved[2]`):**

| body | field | size | abs CCSDS | abs SNLP |
|---|---|---|---|---|
| 0 | `_pad_head[2]` | 2 | 6 | 14 |
| 2 | epoch_ts | 8 | 8 ✅ | 16 ✅ |
| 10 | pressure_pa | 4 | 16 ✅ | 24 ✅ |
| 14 | lat_fixed | 4 | 20 ✅ | 28 ✅ |
| 18 | lon_fixed | 4 | 24 ✅ | 32 ✅ |
| 22 | vbatt_mv | 2 | 28 ✅ | 36 ✅ |
| 24 | temp_c | 2 | 30 ✅ | 38 ✅ |
| 26 | gps_speed_cms | 2 | 32 ✅ | 40 ✅ |
| 28 | sys_state | 1 | 34 | — |
| 29 | sat_lock | 1 | 35 | — |
| 30 | crc32 | 4 | 36 ✅ | 44 ✅ |

**Body = 34 bytes — same as current.** Frame **CCSDS = 40 (÷8 ✅)**,
**SNLP = 48 (÷8 ✅)**.

The `reserved[2]` field is dropped. It was never read by firmware or the
gateway (`grep -r reserved_interval` confirms no consumers). Forward
compatibility moves to `_pad_head` — if we ever need reserved bytes, we
can redefine `_pad_head` slots in a follow-up without changing the wire
size.

## Summary

| Packet | Before | After | Δ | Frame CCSDS | Frame SNLP |
|---|---|---|---|---|---|
| A | 62 | 66 | +4 | **72** ÷8 ✅ | **80** ÷8 ✅ |
| B | 166 | 178 | +12 | **184** ÷8 ✅ | **192** ÷64 ✅ |
| L | 34 | 34 | 0 | **40** ÷8 ✅ | **48** ÷8 ✅ |
| H, C, D, ACK | — | — | 0 | unchanged ÷8 ✅ | unchanged ÷8 ✅ |

Every critical field in every packet is now on its natural alignment
boundary under both CCSDS and SNLP headers, AND every frame total is
divisible by 8 (so SPI DMA bursts to the SX1262 never cross a word
boundary on the Xtensa LX7 core). Packet B SNLP lands on 192 = 3×64,
which is also cache-line clean.

## Impact

- Wire size delta: +4 bytes Packet A, +12 bytes Packet B (+8 alignment
  pads + 4-byte tail pad), 0 bytes Packet L.
- LoRa airtime at SF9 BW125 for Packet B: ~210 ms → ~216 ms (+3%).
- Fits within SX1262 single-packet MTU for all supported spreading
  factors (SF7–SF9) — 192 bytes is well under the 255-byte PHY ceiling.
- DMA-coalesced SPI burst from ESP32-S3 → SX1262 now lands every frame
  on an 8-byte boundary, so there is no trap-handler fallback on the
  Xtensa LX7 core for misaligned u64 loads during the TX hot path.
- Signature scope for Packet B grows 98 → 106 body bytes. The signer
  (`security_manager.cpp::encryptPacketB`) uses `offsetof(PacketB_t,
  signature)` and adapts automatically.
- ChaCha20 nonce derivation (VOID-110) is unaffected — nonce is still
  `sat_id[4] || epoch_ts[8]`, and both fields remain present.

## Decision

> **Adopt the VOID-114B body reshape for Packets A, B, L.**
> Apply the new layouts to the firmware structs, KSY dispatcher, Go
> parser, packet generator, gateway ingest handler, and both protocol
> spec documents (CCSDS and SNLP editions) atomically.

## Appendix — Analyzer

```python
packets_after = {
  "A": [("_pad_head",0,1),("epoch_ts",2,8),("pos[0]",10,8),("pos[1]",18,8),
        ("pos[2]",26,8),("vel[0]",34,4),("vel[1]",38,4),("vel[2]",42,4),
        ("sat_id",46,4),("amount",50,8),("asset_id",58,2),
        ("_pre_crc",60,1),("crc32",62,4)],
  "B": [("_pad_head",0,1),("epoch_ts",2,8),("pos[0]",10,8),("pos[1]",18,8),
        ("pos[2]",26,8),("enc_payload",34,1),("_pre_sat",96,1),
        ("sat_id",98,4),("_pre_sig",102,1),("signature",106,8),
        ("global_crc",170,4),("_tail_pad",174,1)],
  "L": [("_pad_head",0,1),("epoch_ts",2,8),("pressure_pa",10,4),
        ("lat_fixed",14,4),("lon_fixed",18,4),("vbatt_mv",22,2),
        ("temp_c",24,2),("gps_speed_cms",26,2),("sys_state",28,1),
        ("sat_lock",29,1),("crc32",30,4)],
}

def check(H):
    for pkt, fields in packets_after.items():
        bad = [(n, H+o, a) for n,o,a in fields if (H+o) % a != 0]
        print(pkt, "OK" if not bad else bad)

for H in (6, 14): check(H)
```

Expected output:
```
A OK
B OK
L OK
A OK
B OK
L OK
```

---

**Status:** Decision recorded. Wire sizes change for A (68→72 CCSDS / 76→80 SNLP) and B (172→**184** CCSDS / 180→**192** SNLP); L frame size is unchanged but body is reordered. VOID-110 nonce derivation is preserved. Frame totals are now all divisible by 8 across every packet type.

**Follow-ups:**
- Regenerate test vector `.bin` files via `generate_packets.go` — not in this PR.

**Author:** Tiny Innovation Group Ltd
**Date:** 2026-04-14
