# Changelog
All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [2.1.1] - 2026-02-20
### Added
- Python L2 Ground Station CLI (`main.py`, `ground_station.py`) with strict `ctypes` struct mapping.
- Full End-to-End state machine logic for Sat A (Seller) and Sat B (Buyer).
- Conditional build environments (`buyer_demo`, `seller_prod`) mapped via `platformio.ini`.

### Security
- Enforced strict `reinterpret_cast` compliance for all hardware zero-copy deserialization.
- Segregated hardware PUF identity seeds to prevent key cloning across satellite roles.


## [2.1.0] - 2026-02-19
### Added
- Initial Clean Room implementation of Void Protocol for ESP32.
- NSA/SEI CERT compliant static memory allocation buffers.
- Zero-copy OTA packet inspection and strictly typed casting.
- Ed25519 Identity and X25519 Ephemeral key exchange (Security Manager).