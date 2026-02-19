/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      main.cpp
 * Desc:      Main entry point for VOID Protocol satellite firmware.
 * Compliant: NSA Clean C++ (Strict Typing)
 * -------------------------------------------------------------------------*/
#include <Arduino.h>
#include "void_protocol.h"
#include "security_manager.h"
#include "seller.h"
#include "buyer.h"

// --- CONFIGURATION ---
// #define ROLE_SELLER
#define ROLE_BUYER
#define DEMO 1 // <--- Uncomment to enable Serial 'H' Handshake trigger
// ---------------------

void setup()
{
    Void.begin();

    // Initialize the Security Engine (Sodium & Keys)
    if (!Security.begin())
    {
        Void.updateDisplay("ERROR", "Sec Manager Fail");
        while (1)
            ;
    }

#ifdef ROLE_SELLER
    Void.updateDisplay("ROLE", "SELLER (Sat A)");
#elif defined(ROLE_BUYER)
    Void.updateDisplay("ROLE", "BUYER (Sat B)");
    Void.radio.startReceive(); // Buyer starts in RX mode

#ifdef DEMO
    Serial.println("DEMO MODE: Send 'H' via Serial to initiate Handshake.");
#endif
#else
#error "Please define a ROLE in main.cpp"
#endif
}

void loop()
{
#ifdef ROLE_SELLER
    runSellerLoop();
#elif defined(ROLE_BUYER)
    runBuyerLoop();
#ifdef DEMO
    Void.pollDemoTriggers();
#endif

#endif
}