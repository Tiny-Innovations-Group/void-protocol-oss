# Ticket: VOID-143 — Fix Flat-Sat Bench LoRa Packet Drops, CAD Starvation, and Silent RX Failures

**Target Component:** `satellite-firmware` (`seller.cpp`, `buyer.cpp`, `void_protocol.cpp`)[cite: 8, 9, 12]  
**Severity:** P0 (Blocks TRL 4 Bench Demo / VOID-130)[cite: 7]  
**Environment:** Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262), 868 MHz ISM, desk separation (~1 m), near-field high-RF ambient environment[cite: 1, 4]

---

## 1. Context & Root Cause Summary

Intermittent packet delivery failures during the flat-sat transaction loop (A ➔ B ➔ ACK ➔ Settle ➔ C ➔ D) stem from tight CAD spin-locks, silent error suppression on hardware RX errors, asymmetric diagnostic logging, and conservative RF link margins under elevated local RF noise floors[cite: 1, 7, 8, 9, 12]:

1. **CAD Lockout in Seller Beaconing:** `seller.cpp` defers transmission when CAD detects channel activity but never increments an attempt counter or forces a transmission, locking the seller into a continuous deferral loop when ambient RF is elevated[cite: 8].
2. **Silent Packet Drops on `readData()`:** When RadioLib encounters an on-air symbol or CRC error, it returns an error code (e.g., `RADIOLIB_ERR_CRC_MISMATCH = -705`). Neither `buyer.cpp` nor `seller.cpp` has an `else` branch to log these failures[cite: 8, 9].
3. **Overly Long Seller Fallback Window:** `kEngagedTimeoutMs` is set to 30 seconds[cite: 8]. A single dropped packet causes a 30-second silent freeze before advertising re-opens[cite: 8].
4. **Asymmetric Inbound Diagnostics:** `seller.cpp` logs all incoming frames (`apid` and `len`)[cite: 8], whereas `buyer.cpp` silently returns on sync word, length, or APID mismatches[cite: 9].
5. **Low RF TX Power in High-Noise Environments:** `void_protocol.cpp` sets output power to `5 dBm`[cite: 12], which provides inadequate link margin if a nearby cell or radio mast induces receiver LNA desensitization.

---

## 2. Defects & Implementation Details

### Defect 1: Seller CAD Beacon Starvation (`seller.cpp`)
* **Location:** `seller.cpp:159-167`[cite: 8]
* **Problem:** If `Void.channelClear()` returns `false`, `lastTx` is not updated, causing the condition `millis() - lastTx > kBeaconIntervalMs` to remain true on subsequent iterations[cite: 8]. The node enters a high-frequency polling loop without logging or attempting recovery[cite: 8].
* **Action:**
  * Add a CAD retry limit (`kBeaconMaxCadRetries = 5`) and a diagnostic log on deferral[cite: 8].
  * If the retry limit is exceeded, force transmission or back off `lastTx` by 1000 ms to yield the CPU and airtime.

```cpp
// seller.cpp: replace lines 159-167
static uint8_t beacon_cad_retries = 0;
if (txState == SellerState::ADVERTISING &&
    millis() - lastTx > kBeaconIntervalMs) {

    if (!Void.channelClear()) {
        beacon_cad_retries++;
        Serial.print("WARN: Seller beacon CAD busy (attempt ");
        Serial.print(beacon_cad_retries);
        Serial.println("/5)");
        Void.radio.startReceive();

        if (beacon_cad_retries < 5) {
            return; // Retry on subsequent loop
        }
        Serial.println("WARN: Seller beacon CAD cap reached — forcing broadcast");
    }

    beacon_cad_retries = 0;
    // ... proceed with Packet A construction and transmission ...
}

```

---

### Defect 2: Suppressed `readData()` Return Errors (`buyer.cpp` & `seller.cpp`)

* **Location:** `buyer.cpp:166`, `seller.cpp:232`


* **Problem:** Both modules check `if (state == RADIOLIB_ERR_NONE)` but completely omit `else` handlers. Hardware-level CRC failures (`-705`), channel timeouts, and SPI bus errors are discarded silently.


* **Action:** Add `else` handling to report non-zero return codes to `Serial` in both files.



```cpp
// buyer.cpp:166 and seller.cpp:232
const int state = Void.radio.readData(rx_buffer, len);
if (state == RADIOLIB_ERR_NONE) {
    // Existing processing logic
} else {
    Serial.print("ERR: RadioLib readData failure code: ");
    Serial.println(state);
}

```

---

### Defect 3: Reduce Seller Transaction Timeout (`seller.cpp`)

* **Location:** `seller.cpp:148`

* **Problem:** `kEngagedTimeoutMs = 30000` causes an unacceptably long freeze during bench demos if any intermediate leg drops.


* **Action:** Reduce `kEngagedTimeoutMs` to `8000` ms for responsive bench recovery.



```cpp
// seller.cpp:148
static constexpr unsigned long kEngagedTimeoutMs = 8000; // Reduced from 30000

```

---

### Defect 4: Missing Diagnostic RX Logging in Buyer (`buyer.cpp`)

* **Location:** `buyer.cpp:167-175`

* **Problem:** Unlike `seller.cpp` (lines 239–244), `buyer.cpp` provides no trace of dropped packets failing sync-word or APID validation.


* **Action:** Log raw frame arrival, length, sync-word pass/fail, and extracted APID immediately after successful `readData()`.



```cpp
// buyer.cpp: insert after readData() success
#if VOID_PROTOCOL_TYPE == 2
const bool sync_ok = validSyncWord(rx_buffer);
const uint16_t apid = extractAPID(rx_buffer);
Serial.print("BUYER: RX frame len=");
Serial.print(static_cast<unsigned>(len));
Serial.print(" sync=");
Serial.print(sync_ok ? "OK" : "FAIL");
Serial.print(" apid=");
Serial.println(apid);

if (!sync_ok) {
    Void.radio.startReceive();
    return;
}
#endif

```

---

### Defect 5: Increase Output Power Margin (`void_protocol.cpp`)

* **Location:** `void_protocol.cpp:78`

* **Problem:** `radio.setOutputPower(5)` provides inadequate carrier-to-noise ratio in the presence of strong out-of-band RF signals (such as cellular base stations ~400 m away).


* **Action:** Increase power to `12 dBm` (comfortably below the 14 dBm UK legal limit for 868 MHz).



```cpp
// void_protocol.cpp:78
radio.setOutputPower(12); // Increased from 5 dBm for local RF noise rejection

```

---

## 3. Acceptance Criteria

* [ ] `seller.cpp` never deadlocks or stops beaconing for more than 8 seconds when the channel is noisy.


* [ ] All RadioLib `readData` failures output descriptive error codes over `Serial`.


* [ ] `buyer.cpp` logs all detected RF bursts with length, sync status, and APID.


* [ ] Missed transaction loops recover to advertising within 8 seconds.


* [ ] Bench loop runs reliably at 12 dBm without receiver saturation or desense drops.
