# 🌐 Gateway (Go Web3 Router)

> **Authority:** Tiny Innovation Group Ltd  
> 
> **License:** Apache 2.0  
> 
> **Language:** Go (v1.21+)  
> 
> **Schema:** `void_protocol.ksy` (Kaitai Struct)
> 
> **Role:** L2 Settlement & Ledger Authority

The Gateway is the off-hardware, high-concurrency routing engine for the Void Protocol. It ingests raw binary frames from the Ground Station (Bouncer) or TinyGS Network (MQTT), parses them using the **Universal Binary Schema**, and orchestrates the final movement of capital on the blockchain.

## 🛠️ Technical Architecture

### 1. Ingestion Pipeline (Kaitai Struct)
Unlike legacy systems that rely on manual byte parsing, this Gateway uses the auto-generated **`void_protocol`** Go package.
* **Source of Truth:** All parsing logic is derived strictly from `void_protocol.ksy`.
* **Dual-Stack Routing:** The Gateway automatically detects the frame type:
    * **SNLP (Community):** Enforces **Plaintext Visibility** (TinyGS Mandate).
    * **CCSDS (Enterprise):** Handles **ChaCha20 Decryption** for private payloads.

### 2. Settlement Logic
* **L2 Execution:** Interactions are triggered directly against the Settlement Smart Contract.
* **Batching:** Per the open-source governance model, this community edition supports "Lite Batching" (limit: 10 aggregated receipts).
* **Escrow Resolution:** Once a **Packet C (Receipt)** is cryptographically verified, the Gateway triggers the release of funds from Escrow to the Seller.

## 🚀 Setup & Usage

### Prerequisites
* **Go:** `v1.21` or higher (`go mod download` resolves all deps including the kaitai Go runtime).
* **Foundry:** `anvil`, `forge`, `cast` — install from <https://getfoundry.sh>. Verified with `anvil 1.5.1`.
* **kaitai-struct-compiler:** `0.11+`. Only required if you regenerate the parser from KSY (`brew install kaitai-struct-compiler`).

### Regenerating the Parser (only if KSY changed)
The Go parser at `gateway/internal/void_protocol/protocol/` is auto-generated from the modular KSY tree under `docs/kaitai_struct/`. The hand-written `constants.go` + `crc.go` next to it hold wire-format invariants (`PacketBBodyLen`, `PacketBSigScopeBody`, etc.) and are not generated.

```bash
kaitai-struct-compiler -t go \
  --go-package protocol \
  --outdir gateway/internal/void_protocol/protocol \
  --import-path docs/kaitai_struct \
  docs/kaitai_struct/void_protocol.ksy
```

After regenerating, `cd gateway && go build ./... && go test ./...` to confirm no consumer drifted.

### Local Flat-Sat Bring-Up

The gateway's on-chain pipeline (settlement submitter + receipt watcher) is gated on `VOID_ESCROW_ADDRESS`. Without it the gateway boots in **parse + verify only** mode — useful for parser regression runs but no settlement happens.

For the full A→B→ACK→Settle→C→D loop you need three terminals:

#### 1. Local chain (leave running)
```bash
anvil
```
Anvil binds `127.0.0.1:8545` (chain ID `31337`) by default — that matches `defaultEthRPCURL` in the gateway.

#### 2. Deploy the Escrow contract
Deploy once per fresh anvil session. Anvil account #0's first deploy is always at the same deterministic address:

```bash
cd contracts
forge create src/Escrow.sol:Escrow \
  --rpc-url http://127.0.0.1:8545 \
  --private-key 0xac0974bec39a17e36ba4a6b4d238ff944bacb478cbed5efcae784d7bf4f2ff80 \
  --broadcast
# → Deployed to: 0x5FbDB2315678afecb367f032d93F642f64180aa3
```

The private key above is anvil's default account #0 — flat-sat demo only, never a production key.

#### 3. Run the gateway with the on-chain pipeline live
```bash
cd gateway
VOID_ALPHA_PLAINTEXT=1 \
VOID_ESCROW_ADDRESS=0x5FbDB2315678afecb367f032d93F642f64180aa3 \
go run ./cmd/server
```

A clean startup banner looks like:
```
🗄️  Registry Initialized with Hardcoded Satellites
🔑 Flat-sat demo Ed25519 pubkey injected for sat 0xCAFEBABE
🔗 On-chain submitter wired | rpc=http://127.0.0.1:8545 escrow=0x5FbDB2... chain_id=31337
📒 Receipt watcher wired   | receipts=gateway/data/receipts.json interval=2s
🚀 VOID Enterprise Gateway listening on :8080
```

If you only want parse+verify (for example, when chasing a kaitai/sig-verify regression without a chain), omit `VOID_ESCROW_ADDRESS` — the gateway will log `⚠️ VOID_ESCROW_ADDRESS not set — on-chain pipeline disabled`.

### Environment variables

| Var | Default | Purpose |
|---|---|---|
| `VOID_ALPHA_PLAINTEXT` | unset | Set to `1` for alpha plaintext mode (no ChaCha20 decrypt). Required to match the firmware build. |
| `VOID_ESCROW_ADDRESS` | unset (disables on-chain) | Deployed Escrow contract address. Enables submitter + receipt watcher. |
| `VOID_ETH_RPC_URL` | `http://127.0.0.1:8545` | Override to point at a non-local chain. |
| `VOID_GATEWAY_PRIVATE_KEY` | anvil acct #0 (demo) | Hex secp256k1 key the gateway uses to sign `settleBatch` calls. |
| `VOID_RECEIPTS_PATH` | `gateway/data/receipts.json` | Append-only JSONL store for settled-receipt artefacts. |
| `VOID_RECEIPTS_SELLER_SEED` | demo `bc1df4fa…` | Hex Ed25519 seed for PacketC signing. Same seed the firmware uses under `VOID_ALPHA_PLAINTEXT`. |
| `VOID_RECEIPTS_SELLER_APID` | `100` | APID stamped on emitted PacketCs. |

## 🏛️ Open Source Boundaries

This Gateway represents the "Client Cache" and "Single-Session" reference implementation. It is designed for standard adoption and easy deployment. Multi-session fleet multiplexing and global identity registries are reserved for the Enterprise edition.

---

© 2026 Tiny Innovation Group Ltd.