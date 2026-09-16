# Ticket: VOID-143 — Fix Flat-Sat Bench LoRa Packet Drops, CAD Starvation, and Silent RX Failures

**Target Component:** `satellite-firmware` (`seller.cpp`, `buyer.cpp`, `void_protocol.cpp`)
**Severity:** P0 (Blocks TRL 4 Bench Demo)
**Agent Instructions:** Implement the following targeted refactors exactly as specified. Maintain all existing SEI CERT C++ compliance. Do not introduce any dynamic memory allocation.

## 1. Fix Priority Inversion & CAD Starvation (`seller.cpp`)
**Context:** `seller.cpp` checks for TX beaconing before draining the RX FIFO. If CAD is busy and returns early, incoming packets are permanently ignored.
**Action:** 
1. Move the entire "2. LISTEN FOR DOWNLINK" block to the top of `runSellerLoop()`, immediately after the one-shot ISR arming logic.
2. Replace the `SellerState::ADVERTISING` channel clear check with a static retry counter.

* **Locate:** `if (txState == SellerState::ADVERTISING && millis() - lastTx > kBeaconIntervalMs)`
* **Replace the `!Void.channelClear()` check inside it with:**
```cpp
        static uint8_t beacon_cad_retries = 0;
        if (!Void.channelClear()) {
            beacon_cad_retries++;
            Serial.print("WARN: Seller beacon CAD busy (attempt ");
            Serial.print(beacon_cad_retries);
            Serial.println("/5)");
            Void.radio.startReceive();
            
            if (beacon_cad_retries < 5) {
                return; // Safe to return now because RX is handled at the top of the loop
            }
            Serial.println("WARN: Seller beacon CAD cap reached — forcing broadcast");
        }
        beacon_cad_retries = 0;

```

## 2. Surface Suppressed `readData()` Errors & Add Sync Validation (`buyer.cpp` & `seller.cpp`)

**Context:** RadioLib hardware errors are swallowed. `seller.cpp` also skips the SNLP sync-word validation that `buyer.cpp` uses, risking false CRC checks on noise.
**Action:** Add explicit `else` error logging, and unify sync-word checks.

* **File: `buyer.cpp**`
* **Locate:** `const int state = Void.radio.readData(rx_buffer, len);`
* **Implementation:** Append an `else` branch directly after the success block:

```cpp
        } else {
            Serial.print("ERR: RadioLib readData failure code: ");
            Serial.println(state);
        }

```

* **File: `seller.cpp**`
* **Implementation:** Apply the exact same `else` branch as above. Additionally, inside the `RADIOLIB_ERR_NONE` block, add the SNLP sync-word check before `getAPID`:

```cpp
#if VOID_PROTOCOL_TYPE == 2
            const bool sync_ok = validSyncWord(rx_buffer);
            if (!sync_ok) {
                Serial.println("WARN: SELLER dropped frame (Sync Word Fail)");
                Void.radio.startReceive();
                return;
            }
#endif
            uint16_t apid = getAPID(rx_buffer);

```

## 3. Reduce Seller Transaction Timeout (`seller.cpp`)

* **Locate:** `static constexpr unsigned long kEngagedTimeoutMs = 30000;`
* **Implementation:** Change the value to `8000`.

## 4. Fix Blocking UART & Silent Discards (`buyer.cpp` & `void_protocol.cpp`)

**Context:** `Serial.readBytesUntil()` blocks for 1000 ms by default, blinding the LoRa radio. Dropped `ACK_BUY` commands fail silently.
**Action:**

* **File: `void_protocol.cpp**`
* **Locate:** `Serial.begin(115200);` in `VoidProtocol::begin()`
* **Implementation:** Add `Serial.setTimeout(20);` immediately below it.
* **File: `buyer.cpp**`
* **Locate:** `else if (strncmp(serial_buf, "ACK_BUY", 7) == 0 && invoice_pending)`
* **Implementation:** Add a fallback `else if` immediately after this block:

```cpp
        else if (strncmp(serial_buf, "ACK_BUY", 7) == 0) {
            Serial.println("WARN: ACK_BUY received but no invoice is pending.");
        }

```

## 5. Increase RF Link Margin (`void_protocol.cpp`)

* **Locate:** `radio.setOutputPower(5);` inside `VoidProtocol::begin()`.
* **Implementation:** Change the integer argument to `12`.



# Ticket: VOID-144 — Fix Handshake Crypto Signature Mismatch on SNLP Builds (The Spin-Off VOID-144 Ticket)

**Target Component:** `security_manager.cpp`
**Severity:** P1 (Breaks Ground Auth Verification)
**Agent Instructions:** Fix hardcoded CCSDS offset assumptions in the Ed25519 signature payload.

## 1. Fix Hardcoded Pre-Signature Offsets (`security_manager.cpp`)
**Context:** `SecurityManager::prepareHandshake()` assumes `PacketH_t` has a 48-byte pre-signature payload. Under Community Tier (`VOID_PROTOCOL_TYPE == 2`), the SNLP header is 14 bytes, pushing the pre-signature offset to 56 bytes. The current logic signs partial data and leaves the SNLP sync word uninitialized.
**Action:** Replace the hardcoded `48` with the dynamic `offsetof()` macro, and populate the SNLP sync word.

* **File:** `security_manager.cpp`
* **Locate:** `SecurityManager::prepareHandshake()`
* **Implementation:** 
1. Remove the manual `pkt.header.ver_type_sec = 0x18;` header assignments.
2. Replace them with the tier-aware header packer (if available) or explicitly zero the SNLP sync word block if `VOID_PROTOCOL_TYPE == 2`.
3. Update the `crypto_sign_detached` call to use `offsetof`:

```cpp
    // SIGNATURE (Identity binds the Ephemeral Key)
    unsigned long long sig_len;
    const size_t sign_len = offsetof(PacketH_t, signature);
    
    crypto_sign_detached(pkt.signature, &sig_len, reinterpret_cast<uint8_t*>(&pkt), sign_len, _identity_priv);

```
