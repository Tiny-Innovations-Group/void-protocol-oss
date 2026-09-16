# Ticket: VOID-143 — Fix Flat-Sat Bench LoRa Packet Drops, CAD Starvation, and Silent RX Failures

**Target Component:** `satellite-firmware` (`seller.cpp`, `buyer.cpp`, `void_protocol.cpp`)

**Severity:** P0 (Blocks TRL 4 Bench Demo)

**Agent Instructions:** Implement the following five targeted refactors exactly as specified. Maintain all existing SEI CERT C++ compliance. Do not introduce any dynamic memory allocation (no heap usage). All injected variables must use `static` duration where state retention is required.

## 1. Fix Seller CAD Beacon Starvation (`seller.cpp`)

**Context:** The seller currently deadlocks in a high-frequency CAD polling loop if the channel is busy because `lastTx` is not advanced on deferral.
**Action:** Inject a static retry counter to force a best-effort transmission if the channel remains saturated.

* **File:** `seller.cpp`
* **Locate:** The `SellerState::ADVERTISING` block where `!Void.channelClear()` is evaluated.
* **Replace with:**

```cpp
    static uint8_t beacon_cad_retries = 0;
    
    // ---------------------------------------------------------
    // 1. BROADCAST ADVERTISING (Phase 3) — only while idle
    // ---------------------------------------------------------
    if (txState == SellerState::ADVERTISING &&
        millis() - lastTx > kBeaconIntervalMs) {
        
        if (!Void.channelClear()) {
            beacon_cad_retries++;
            Serial.print("WARN: Seller beacon CAD busy (attempt ");
            Serial.print(beacon_cad_retries);
            Serial.println("/5)");
            Void.radio.startReceive();
            
            if (beacon_cad_retries < 5) {
                return; // Defer and retry promptly on next loop
            }
            Serial.println("WARN: Seller beacon CAD cap reached — forcing broadcast");
        }
        
        // Air is clear OR retry cap reached; proceed with transmission
        beacon_cad_retries = 0;
        
        // Clear memory to prevent leaking RAM garbage
        memset(&invoice, 0, sizeof(PacketA_t));

```

## 2. Surface Suppressed `readData()` Hardware Errors (`buyer.cpp` & `seller.cpp`)

**Context:** RadioLib errors (e.g., CRC mismatch `-705`) are currently swallowed because the RX loops only handle `RADIOLIB_ERR_NONE`.
**Action:** Add explicit `else` blocks to log all non-zero return codes.

* **File:** `seller.cpp`
* **Locate:** `const int state = Void.radio.readData(rx_buffer, len);`
* **Implementation:** Append an `else` branch directly after the `if (state == RADIOLIB_ERR_NONE) { ... }` block to output:

```cpp
        } else {
            Serial.print("ERR: RadioLib readData failure code: ");
            Serial.println(state);
        }

```

* **File:** `buyer.cpp`
* **Locate:** `const int state = Void.radio.readData(rx_buffer, len);`
* **Implementation:** Append the identical `else` branch logic as above.

## 3. Reduce Seller Transaction Timeout (`seller.cpp`)

**Context:** The 30-second fallback window is too long for responsive bench testing if an intermediate leg drops.
**Action:** Reduce the timeout variable.

* **File:** `seller.cpp`
* **Locate:** `static constexpr unsigned long kEngagedTimeoutMs = 30000;`
* **Implementation:** Change the value to `8000`.

```cpp
    static constexpr unsigned long kEngagedTimeoutMs = 8000;

```

## 4. Add Diagnostic RX Logging to Buyer (`buyer.cpp`)

**Context:** `buyer.cpp` silently drops incoming LoRa bursts that fail sync-word or APID validation, hindering physical-layer debugging.
**Action:** Inject frame diagnostic logging immediately after a successful `readData()` call.

* **File:** `buyer.cpp`
* **Locate:** The success branch `if (state == RADIOLIB_ERR_NONE)` inside `runBuyerLoop()`.
* **Implementation:** Insert the diagnostic trace immediately before the sync-word filter:

```cpp
            if (state == RADIOLIB_ERR_NONE) {
#if VOID_PROTOCOL_TYPE == 2
                const bool sync_ok = validSyncWord(rx_buffer);
                const uint16_t parsed_apid = extractAPID(rx_buffer);
                
                Serial.print("BUYER: RX frame len=");
                Serial.print(static_cast<unsigned>(len));
                Serial.print(" sync=");
                Serial.print(sync_ok ? "OK" : "FAIL");
                Serial.print(" apid=");
                Serial.println(parsed_apid);

                if (!sync_ok) {
                    Void.radio.startReceive();
                    return;
                }
#endif
                const uint16_t apid = extractAPID(rx_buffer);

```

## 5. Increase RF Link Margin for Bench Resilience (`void_protocol.cpp`)

**Context:** The current TX power is insufficient to overcome local RF noise desensitization.
**Action:** Elevate output power within UK ISM legal limits.

* **File:** `void_protocol.cpp`
* **Locate:** `radio.setOutputPower(5);` inside `VoidProtocol::begin()`.
* **Implementation:** Change the integer argument to `12`.

```cpp
    radio.setOutputPower(12);

```
