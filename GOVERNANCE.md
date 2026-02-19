# 🏛️ Open Source Strategy & Commercial Boundaries (v2.1)
> **Authority:** Tiny Innovation Group Ltd
> 
> 
> **Applicability:** All Engineering, Product, and Legal Staff
> 
> **Model:** Open Core (Thick Client / Intelligent Cloud)
> 
> **Status:** **APPROVED**
> 

## 1. The Core Philosophy

We strictly adhere to a **"Thick Client, Thin Server"** model for Open Source.

- **Open Source (The "Device"):** We give away the smartest, fastest, most robust *device-side* code possible. If it runs on the satellite or the laptop, it is OSS. This drives adoption and becomes the standard.
- **Enterprise (The "Network"):** We monetize the *coordination* of those devices. Identity authority, settlement aggregation, and fleet analytics are proprietary.

**Rule of Thumb:** Optimizing **one** node is Open Source. Orchestrating **many** nodes is Enterprise.

---

## 2. The "Hard Line" Definition

| **Feature Domain** | **🌍 Void Protocol OSS (Community)** | **🔒 Void Enterprise (Commercial Product)** |
| --- | --- | --- |
| **Connectivity** | **Full Core.** Packet structures, Handshake logic, ChaCha20/Ed25519 crypto. *Goal: Standard Adoption.* | **Superset.** Inherits Core + Drivers for restricted hardware (e.g., Link-16, rad-hard FPGAs). |
| **Settlement** | **Lite Batching (Limit 10).** Aggregates receipts locally to match the physical downlink queue. Submits 1 batch to L2. *Goal: Usable UX.* | **High-Frequency Batching (Limit ∞).** Aggregates thousands of transactions across *multiple* ground stations. Optimized for MEV protection & gas arbitrage. |
| **Identity (PKI)** | **Smart Client.** Fetches keys via HTTP, caches them locally (NVS/Disk). Handles offline retries. | **Authoritative Cloud.** The API itself. Handles key rotation, revocation lists, & reputation scoring. |
| **Interface** | **CLI Only.** Python scripts for debugging/dev (`ground_station.py`) with local logs. | **Mission Control.** Web/Mobile Dashboards. Fleet maps, analytics, user management. |
| **Database** | **Client Cache.** SQLite/JSON for local state. "My Satellite's History." | **Global Ledger.** Postgres/TimescaleDB. "The Network's History." |
| **Hardware** | **Drivers.** Abstractions for ATECC608/SE050 crypto chips. | **Provisioning.** The factory tool to *inject* keys into those chips securely. |

---

## 3. Repository Governance

### 3.1 Public Repo (`void-protocol-oss`)

- **License:** Apache 2.0.
- **Content:** The "batteries-included" SDK.
- **Contribution:** Accepts community PRs (signed).
- **Security:** **ALL** cryptographic primitives (Handshake, Signatures) and **ALL** client-side validation logic live here.
- **User Experience:** "Clone and Run." Connects to TIG Testnet by default.

### 3.2 Private Repo (`void-protocol-enterprise`)

- **License:** Proprietary / Commercial EULA.
- **Content:** The Server-Side Infrastructure.
- **Secret Sauce:**
    - The "Batcher" (Gas Optimization Engine).
    - The "Registry" (Global Identity API).
    - The "Dashboard" (React/Frontend).
    - The "Factory Provisioner" (Hardware setup tool).

---

## 4. Technical Workflow: The Private Mirror

We use a **"Superset Mirror"** workflow. The Private Repo contains *everything* in the Public Repo plus our proprietary code.

### 4.1 Setup (One-Time)

Run this on your local machine to link the repositories:

Bash

```bash
`# 1. Clone the Public Repo
git clone https://github.com/Tiny-Innovations/void-protocol-oss.git
cd void-protocol-oss

# 2. Add Private Repo as a Remote
git remote add production https://github.com/Tiny-Innovations/void-protocol-enterprise.git

# 3. Verify Remotes
git remote -v
# origin     https://github.com/.../void-protocol-oss.git (fetch/push)
# production https://github.com/.../void-protocol-enterprise.git (fetch/push)`
```

### 4.2 Daily Workflow

**Scenario A: Pushing an Open Source Fix (e.g., Packet Structure)**

- *Action:* Push to `origin`.
- *Then:* Sync to `production` so Enterprise stays up to date.

Bash

```bash
git commit -m "Fix: CRC32 calculation in Packet B"
git push origin main      # Public sees it
git push production main  # Private gets the fix too
```

**Scenario B: Pushing a Proprietary Feature (e.g., Billing Dashboard)**

- *Action:* Push **ONLY** to `production`.

Bash

```bash
git commit -m "Feat: Add Stripe billing integration"
git push production main  # ONLY Private sees it
# DO NOT push to origin!
```

---

## 5. The Decision Matrix (For Developers)

When writing code, ask: **"Does this make the *Network* smarter, or just the *Device*?"**

1. **Is this a feature to cache keys so the device works offline?**
    - ✅ **Push to Public (`origin`).** (Makes the device robust).
2. **Is this a feature to calculate a 'Trust Score' based on payment history?**
    - ❌ **Push to Private (`production`).** (Network Intelligence).
3. **Is this a driver for a secure element (TPM)?**
    - ✅ **Push to Public (`origin`).** (Hardware abstraction).
4. **Is this a script to batch-create 1,000 identities?**
    - ❌ **Push to Private (`production`).** (Fleet Management).

---

**© 2026 Tiny Innovation Group Ltd.**