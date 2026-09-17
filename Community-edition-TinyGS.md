# 📡 VOID Protocol: Community Edition (TinyGS)


> 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
>
> Authority: Tiny Innovation Group Ltd
>
> License: Apache 2.0
>
> Status: Authenticated Clean Room Spec
> 
> **Integration Layer:** VOID Gateway <-> TinyGS MQTT
>
> **License Strategy:** Network Air Gap (Apache 2.0 Client / GPL Server)
>
> **Status:** Proposal / Experimental

This document outlines the architecture for the **Community Edition** of the VOID Protocol, which utilizes the **TinyGS** network as its physical transport layer for Low-Earth Orbit (LEO) settlement.

---

## 1. The "Air Gap" Licensing Model

The VOID Gateway is open-sourced under **Apache 2.0**. The TinyGS firmware and server stack are **GPL/Copyleft**. To ensure strict legal compliance and prevent license conflicts, we utilize a **Network Air Gap Architecture**.

### How it Works

* **No Linked Binaries:** The VOID Gateway **never** links against TinyGS libraries or includes their code directly.
* **Standard Protocols:** All interaction occurs over public, standard network interfaces (MQTT and HTTP JSON APIs).
* **Legal Standing:** In open-source licensing, a client program communicating with a server over a network is considered a **"Separate Work."**
* **Result:** The VOID Gateway remains Apache 2.0 (permissive), while respecting the TinyGS GPL (viral) protections on their own infrastructure. We do not "infect" their stack, and they do not restrict ours.

---

## 2. Why TinyGS?

We chose TinyGS as our primary community transport layer for three specific engineering reasons:

1. **LoRa/ESP32 Native:** The **SNLP (Community Tier)** of the VOID Protocol is designed for high-latency, low-bandwidth links. TinyGS's massive network of cheap, accessible LoRa hardware is the perfect physical match.
2. **Real-Time Settlement (MQTT):** Unlike archive-based networks (e.g., SatNOGS) which require post-pass uploading, TinyGS uses **MQTT** to push packets instantly. This allows our L2 Settlement Engine to finalize payments in milliseconds.
3. **Density:** The sheer number of TinyGS ground stations provides the redundancy required for a global financial network without requiring expensive proprietary hardware.

---

## 3. Security & Integration Logic

To comply with the **TinyGS Manifesto**, our integration is strictly bifurcated into "Public Read" and "Consensual Write."

### 👂 The Global Ear (Public Read)

* **Mechanism:** The Gateway subscribes to the global `tinygs/packets` MQTT topic.
* **Open Access:** We listen to **every** station. If a random community member's station in Tokyo picks up a `Packet B` (Payment), our Gateway ingests it, verifies the signature, and executes the settlement.
* **Benefit:** Every TinyGS station acts as a passive "Oracle" for the blockchain, verifying satellite location and data integrity.

### 🗣️ The Trusted Voice (Consensual Write)

* **Rule:** We **never** force a station to transmit without explicit permission.
* **Mechanism:** The Gateway uses a local `trusted_stations.json` configuration file.
* **Logic:**
1. Gateway calculates a command is needed (e.g., `Packet ACK` to unlock a satellite).
2. Gateway checks: *"Is the station currently in view a Trusted Peer?"*
3. **IF YES:** Gateway uses the stored credentials to publish to `tinygs/station/{id}/tx`.
4. **IF NO:** Gateway holds the command in a queue until a Trusted Peer comes into view.



---

## 4. Infrastructure Setup (Recommended)

For the Community Edition, we recommend a lightweight, containerized deployment to minimize costs while maintaining "Space Grade" uptime.

### Stack Suggestion

* **Host:** **Railway** or **DigitalOcean** (Docker-native hosting).
* **Gateway:** Go Binary (handling MQTT ingest and Blockchain RPC).
* **Sidecar:** C++ Docker Container (handling Flight Code verification).
* **Config:** `trusted_stations.json` (No database required for static trust).

### Deployment Flow

1. **Deploy Sidecar:** The C++ validator starts and exposes port `8080`.
2. **Deploy Gateway:** The Go process starts, connects to TinyGS MQTT, and links to the Sidecar.
3. **Onboard Peers:** Add "Ally" station credentials to the JSON config and restart the Gateway (Zero-downtime restarts supported).

---

## 5. The "Ecosystem Grant" (Giving Back)

We believe that L2 protocols should support the L1 infrastructure they rely on. We are not just users of TinyGS; we are financial contributors.

### The Smart Contract Tithe

* **Mechanism:** The **VOID Settlement Contract** includes a hardcoded fee split.
* **Logic:** For every transaction routed through the TinyGS network, a **% Protocol Fee** is automatically diverted to a wallet address controlled by the **TinyGS Admins**.
* **Transparency:** This happens on-chain. It is trustless, automatic, and ensures that as the VOID network grows, the TinyGS project funding grows with it.

---

## 6. How We "Hear" the Queue

We utilize **Opportunistic Uplink Routing** to manage the command queue without spamming the network.

1. **Ingest:** We receive a packet from `Station_Alpha` via the global feed.
2. **Identify:** The JSON metadata identifies `Station_Alpha` as the source.
3. **Lookup:** We check `Station_Alpha` against our `trusted_stations.json`.
4. **Decision:**
* *Match:* We immediately flush the pending command queue for that satellite to `Station_Alpha`.
* *No Match:* We log the pass for coverage analysis but do not attempt to transmit.



*© 2026 Tiny Innovation Group Ltd.*