# Changelog
All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [2.1.3] - 2026-02-24
### Added
- Formalized `Protocol-spec-SNLP.md` specification for amateur ISM bands (LoRa/tinyGS/HABs).
- Explicitly documented CCSDS APID extension compliance (CCSDS 133.0-B-2 Section 2.1.1) utilizing the 32-bit `sat_id` to support 4.2 billion global DePIN identities.

### Changed
- Redesigned the SNLP Community Payment (Packet B) to a strictly optimized **184-byte** footprint, guaranteeing perfect zero-copy memory alignment for both 32-bit and 64-bit hardware architectures.
- Standardized the SNLP Header into a 12-byte "Universal Adapter" comprising a 32-bit Sync Word (`0x1D01A5A5`), a standard 6-byte CCSDS core, and a 2-byte hardware alignment buffer.

## [2.1.2] - 2026-02-23
### Added
- Native C++ Ground Station implementation (`main.cpp`) to replace legacy Python prototypes.
- Multi-threaded CLI architecture for simultaneous hardware polling and user command input.
- Cross-platform `GatewayClient` in C++ for zero-heap TCP/HTTP communication with the Enterprise Gateway.
- Go-based Enterprise Gateway for high-concurrency L2 Web3 routing and settlement.
- Support for "Sync Word Multiplexer" to dynamically handle both CCSDS (Enterprise) and SNLP (tinyGS) headers.

### Changed
- Migrated primary Ground Station logic from Python to C++ to enforce strict orbital memory safety standards.
- Archived early prototyping scripts (`ground_station.py`, `main.py`) to the `legacy-python/` directory.
- Updated `Bouncer` packet processing to support flexible network headers without compromising cryptographic verification.

### Security
- Standardized `reinterpret_cast` usage to occur only after strict length and header ID verification.
- Enforced static memory allocation for all network buffers to prevent heap fragmentation.
- Implemented `snprintf`-based JSON serialization to prevent buffer overflows during data bridging.

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