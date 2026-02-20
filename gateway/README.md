# 🌐 Gateway (Go Web3 Router)

> **Authority:** Tiny Innovation Group Ltd  
> 
> **License:** Apache 2.0  
> 
> **Language:** Go
> 
> **Role:** L2 Settlement & Ledger Authority

The Gateway is the off-hardware, high-concurrency routing engine for the Void Protocol. It receives clean, cryptographically verified packet data from the local C++ Ground Station and orchestrates the final movement of capital on the blockchain.

## ⛓️ Settlement Architecture
* **L2 Execution:** The Gateway interacts directly with the L2 Smart Contract to execute the settlement.
* **Batching Limits:** Per the open-source governance model, this community edition supports "Lite Batching" with a limit of 10 aggregated receipts.
* **Escrow Resolution:** Once identity is verified, it triggers the final release of funds from the Escrow Smart Contract to the Seller's wallet.

## 🏛️ Open Source Boundaries
This Gateway represents the "Client Cache" and "Single-Session" reference implementation. It is designed for standard adoption and easy deployment. Multi-session fleet multiplexing and global identity registries are reserved for the Enterprise edition.

---

*© 2026 Tiny Innovation Group Ltd.*