# 💻 Ground Station (C++ CLI & Bouncer)

> **Authority:** Tiny Innovation Group Ltd  
> 
> **License:** Apache 2.0  
> 
> **Interface:** CLI Only
> 
> **Role:** Edge Validation & Hardware Bridge

The Ground Station acts as the physical bridge between the RF network (Sat B) and the Web3 infrastructure (Gateway). This directory contains the C++ application responsible for interfacing with the local receiver hardware via Serial/USB.

## 🛑 The "Bouncer" Layer
Before any data is passed to the Go Gateway or the blockchain, this client acts as a strict firewall:
* **Signature Validation:** It verifies the Sat B PUF signature to ensure authenticity.
* **Decryption:** It decrypts the payload using the established SESSION_KEY.
* **Sanitization:** It mathematically verifies the packet structures against the `../void-core/` definitions.

## 📂 Legacy Code
Note: Early prototyping scripts (`ground_station.py`, `main.py`) have been archived in the `legacy-python/` directory for reference.

---

*© 2026 Tiny Innovation Group Ltd.*