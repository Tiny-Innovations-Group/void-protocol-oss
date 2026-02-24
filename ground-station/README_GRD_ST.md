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

## 🚀 Quick Start (Build & Run)
This desktop application is built using **CMake** and rigorously enforces the same NSA/CISA and SEI CERT C++ memory safety standards as the orbital hardware. 

We utilize a **"Clone and Run"** architecture. You do *not* need to install cryptography libraries (like `libsodium`) on your global system. CMake will securely fetch, compile, and statically link the mathematically verified cryptography binaries entirely within your local build folder.

**Prerequisites:** You only need a C++ compiler (GCC/Clang) and `cmake` installed.

```bash
# 1. Create a dedicated build directory (ignored by git)
mkdir build
cd build

# 2. Fetch dependencies and generate the strict C++ Makefiles
cmake ..

# 3. Compile the executable
make

# 4. Run the Ground Station CLI
./ground_station

```

## 📂 Legacy Code

Note: Early prototyping scripts (`ground_station.py`, `main.py`) have been archived in the `legacy-python/` directory for reference.

---

*© 2026 Tiny Innovation Group Ltd.*