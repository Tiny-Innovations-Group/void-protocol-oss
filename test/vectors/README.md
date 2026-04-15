# VOID Protocol — Golden Wire-Format Vectors

This directory contains the **single source of truth** for the VOID protocol
binary wire format. Every `.bin` file below is a deterministic, byte-reproducible
snapshot of one packet type, emitted by the Go generator, and consumed
byte-for-byte by both the Go regression suite (VOID-124) and the C++ regression
suite (VOID-125).

> **Do not edit these files by hand.** They exist to catch the kind of bug
> where someone silently changes the wire format or a packet struct on one
> side (Go or C++) and not the other. The cross-implementation guarantee is
> "both sides produce bytes identical to the committed `.bin`". If you edit a
> vector, you have broken that guarantee.

## Directory layout

```
test/vectors/
├── ccsds/               # Enterprise tier — 6-byte CCSDS primary header
│   ├── packet_a.bin     # 72  bytes — Invoice
│   ├── packet_b.bin     # 184 bytes — Payment
│   ├── packet_c.bin     # 104 bytes — Receipt
│   ├── packet_d.bin     # 128 bytes — Delivery
│   ├── packet_h.bin     # 112 bytes — Handshake
│   ├── packet_ack.bin   # 120 bytes — Acknowledgement
│   └── packet_l.bin     # 40  bytes — Heartbeat / LoRa beacon
└── snlp/                # Community tier — 14-byte SNLP header
    ├── packet_a.bin     # 80  bytes
    ├── packet_b.bin     # 192 bytes
    ├── packet_c.bin     # 112 bytes
    ├── packet_d.bin     # 136 bytes
    ├── packet_h.bin     # 120 bytes
    ├── packet_ack.bin   # 136 bytes
    └── packet_l.bin     # 48  bytes
```

Every file satisfies three hard invariants:

1. `sizeof % 4 == 0` — 32-bit machine-word aligned.
2. `sizeof % 8 == 0` — 64-bit machine-word aligned.
3. `sizeof <= 255`  — SX1262 LoRa PHY payload ceiling.

The generator refuses to write any vector that fails any of these checks.

## Deterministic inputs

The generator hard-codes the following constants. Every packet that contains
the corresponding field embeds the exact value below. Changing any of these
invalidates every committed vector and cascades into both regression suites.

| Field        | Type      | Value                                                                |
|--------------|-----------|----------------------------------------------------------------------|
| `epoch_ts`   | `uint64`  | `1710000100000` (ms since unix epoch)                                |
| `sat_id`     | `uint32`  | `0xCAFEBABE`                                                         |
| `amount`     | `uint64`  | `420000000`                                                          |
| `asset_id`   | `uint16`  | `1`                                                                  |
| `pos_vec`    | `f64[3]`  | `{7010.0, -11990.0, 560.0}`                                          |
| `vel_vec`    | `f32[3]`  | `{7.5, -0.2, 0.01}`                                                  |

### Ed25519 test keypair

All signatures in the golden vectors are produced with a single Ed25519
keypair, seeded from the following **32-byte hex string** (little-endian
byte order, matching `ed25519.NewKeyFromSeed` semantics):

```
bc1df4fa6e3d7048992f14e655060cbb2190bded9002524c06e7cbb163df15fb
```

This seed is a test-only value. It is NOT a flight key and must never be
used for production signing. Both the Go generator and the C++
`test_sign_verify.cpp` (VOID-125) derive their signing key from this exact
seed; the resulting signature bytes on `packet_b.bin` must match byte-for-byte.

### Zero RNG

The generator makes **zero** calls to `time.Now()`, `crypto/rand`, or `math/rand`.
Every byte in every vector is a deterministic function of the constants above.
This is why running the generator twice produces byte-identical output.

## Regeneration

From the repo root:

```bash
go run gateway/test/utils/generate_packets.go --deterministic --out test/vectors
```

The `--deterministic` flag is **required**. Without it the generator exits
with a non-zero status and a pointer to VOID-123.

### When to regenerate

Only when one of the following is true:

1. A packet struct field layout changes (and the change has landed in both
   `void-core/include/void_packets_*.h` and the Go generator).
2. A wire-format constant in this file changes (epoch, sat_id, seed, etc.).
3. A new packet type is added to the protocol.

In every other case — including "the CI diff job is red" — **do not
regenerate**. The correct action is to investigate why a downstream
consumer has drifted from the committed vectors.

### PR rule

Any commit that modifies files in this directory must:

1. Include a justification in the PR description (which ticket, which
   wire-format change).
2. Update the corresponding fields in this README if the deterministic inputs
   have changed.
3. Be reviewed by at least one person who has verified the C++ and Go
   regression suites still pass locally.

## Tickets

- **VOID-123** — this directory (golden vectors).
- **VOID-124** — Go regression suite (`gateway/internal/void_protocol/`,
  `gateway/internal/api/handlers/`) that loads these vectors.
- **VOID-125** — C++ regression suite (`void-core/test/`) that loads these
  vectors via `-DVOID_TEST_VECTORS_DIR`.
- **VOID-126** — CI workflow that runs both suites on every PR and
  diff-checks regenerated vectors against this directory.
