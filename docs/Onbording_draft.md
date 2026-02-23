# 🚀 Void Protocol Onboarding Guide
> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0

This document outlines the **"Flash-Once, Configure-Anywhere"** workflow designed to maximize protocol uptake. By utilizing a "Thick Client" model, we provide the most robust device-side code for satellite operators while keeping network orchestration in the enterprise layer.

## 🛠️ The Philosophy: "Flash Once"

To ensure mission stability and avoid mission-fatal heap fragmentation, we follow a strict deployment pattern:

1. **Immutable Core:** The SDK is flashed to the device (e.g., ESP32-S3) once.
2. **Flexible Provisioning:** All operational parameters are configured post-flash via a lightweight CLI (UART) or remotely via encrypted Radio Frequency (RF).

---

## 🔌 Phase 1: The UART Ignition Flow

The fastest way to initialize a node is via the physical serial interface, mirroring the experience of `npx init`.

### 1. Initialization

Flash the SDK using PlatformIO. On first boot, the device enters a **Provisioning State** and awaits a serial connection.

### 2. The `void-cli` Experience

Connect your device and run the following command:

```bash
void-cli init

```

This boots a lightweight CLI directly from the firmware to configure:

* 
**Node Identity:** Generate an 86-character passphrase to saturate the 256-bit key space of the ChaCha20 cipher.


* 
**Role Selection:** Define the node as a **Sat A (Seller)**, **Sat B (Mule/Buyer)**, or **Ground Station**.


* 
**Commercial Parameters:** * **Amount:** Set the cost in the lowest denomination (e.g., Satoshi).


* 
**Asset ID:** **STABLECOINS ONLY.** To mitigate Solidity-related smart contract risks and volatility, Void Protocol restricts transactions to verified stablecoins (e.g., Asset ID `1` for USDC).




* **Operational Metadata:** Optional description and setting the state to `LISTENING`.

---

## 📡 Phase 2: The RF "Over-The-Air" Flow

For assets already deployed, the protocol supports full **CRUD (Create, Read, Update, Delete)** operations via the encrypted RF tunnel.

### Remote Configuration & CRUD

Using a provisioned Ground Station, you can send encrypted control packets to modify a remote node's state:

| Action | Protocol Mechanism | Description |
| --- | --- | --- |
| **Create** | `Packet H (Init)` | Establish a new forward-secure session for configuration.

 |
| **Read** | `ENC_TUNNEL` | Request a telemetry dump of current node status.

 |
| **Update** | `RELAY_OPS` | Remotely adjust `AZIMUTH`, `ELEVATION`, or `FREQUENCY`.

 |
| **Delete** | `CMD_CODE: 0x00FF` | Securely wipe ephemeral session keys from RAM (Atomic Destruction).

 |

---

## 🏗️ Operational Readiness Checklist

1. 
**Identity Verification:** The node verifies its PUF-based Identity Key against the Ground Authority.


2. 
**Handshake:** An ephemeral **X25519** key exchange is performed to create a `Session_Key`.


3. 
**L2 Settlement:** The node processes **Packet B (Payments)** and **Packet C (Receipts)** in its deterministic 10-receipt FIFO queue.


4. 
**Finality:** Ground station connection is confirmed when the first **ACK (Acknowledgment)** packet is settled.



---

**Would you like me to:**

1. **Refine the `void_types.h` header** to strictly define the supported Stablecoin Asset IDs?
2. **Generate the C++ logic** for the `void-cli` state machine to ensure it rejects non-stablecoin configurations?
3. **Draft the specific "Risk Mitigation" section** for the grant application regarding the Solidity/Stablecoin decision?