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
* **Go:** `v1.21` or higher
* **Kaitai Runtime:** `go get github.com/kaitai-io/kaitai_struct_go_runtime/kaitai`

### Building the Parser
The core logic is generated from the KSY file. If you modify the protocol definition, regenerate the Go library:

```bash
# Regenerate the Go Parser from KSY source
kaitai-struct-compiler -t go --outdir ./gateway void_protocol.ksy

```

### Running the Gateway

```bash
# Start the ingestion engine
go run main.go

```

## 🏛️ Open Source Boundaries

This Gateway represents the "Client Cache" and "Single-Session" reference implementation. It is designed for standard adoption and easy deployment. Multi-session fleet multiplexing and global identity registries are reserved for the Enterprise edition.

---

© 2026 Tiny Innovation Group Ltd.