/*-------------------------------------------------------------------------
 * 🛰️ VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      heartbeat_tx.cpp
 * Desc:      VOID-022 shared 30 s heartbeat telemetry service (seller+buyer).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#include "heartbeat_tx.h"

#include <Arduino.h>
#include <cstdint>

#include "void_protocol.h"

#if VOID_PROTOCOL_TYPE == 2 && defined(VOID_GPS_STUB)

#include "gps_stub.h"
#include "heartbeat_builder.h"

namespace heartbeat_tx {
namespace {

constexpr unsigned long kIntervalMs    = 30000;  // VOID-022 AC cadence
constexpr unsigned long kBusyBackoffMs = 5000;   // CAD-busy retry spacing
// constexpr unsigned long kBusyBackoffMs = 1000;   // CAD-busy retry spacing

// Heltec V3 battery sense: ADC_CTRL (GPIO37) gates a 390k/100k divider
// feeding VBAT_Read (GPIO1); battery mV ≈ adc × 4.9. The enable
// polarity differs across V3 board revisions (V3.0/3.1 active-LOW,
// V3.2 active-HIGH), so probe both and keep whichever reads plausibly.
//
// CRITICAL: the control pin is released to INPUT (high-Z) after every
// probe — bench finding 2026-06-12: leaving it driven holds the divider
// engaged on the battery rail, which lights the charge LED solid orange
// and slow-bleeds an attached battery. The board's own pull resistor
// restores the idle state; we must never park the pin driven.
constexpr uint8_t  kAdcCtrlPin        = 37u;
constexpr uint8_t  kVbattAdcPin       = 1u;
constexpr uint16_t kFallbackVbattMv   = 4100u;  // bench is often USB-only
constexpr int16_t  kFallbackTempCenti = 2300;   // 23.00 °C

bool plausibleVbattMv(uint32_t mv) {
    // 1S LiPo window with margin. USB-only benches (no battery) read
    // ~0 mV; anything absurd means the divider was not engaged at this
    // polarity or there is a wiring fault.
    return mv >= 2500u && mv <= 5500u;
}

uint32_t probeVbattMv(uint8_t level) {
    pinMode(kAdcCtrlPin, OUTPUT);
    digitalWrite(kAdcCtrlPin, level);
    delayMicroseconds(500);
    const uint32_t adc_mv = analogReadMilliVolts(kVbattAdcPin);
    pinMode(kAdcCtrlPin, INPUT);  // release — never hold the rail
    return (adc_mv * 49u) / 10u;
}

uint16_t readVbattMv() {
    const uint32_t low_mv = probeVbattMv(LOW);   // V3.0 / V3.1 enable
    if (plausibleVbattMv(low_mv)) return static_cast<uint16_t>(low_mv);
    const uint32_t high_mv = probeVbattMv(HIGH); // V3.2 enable
    if (plausibleVbattMv(high_mv)) return static_cast<uint16_t>(high_mv);
    return kFallbackVbattMv;  // no battery attached / USB-only bench
}

int16_t readTempCenti() {
    const float t = temperatureRead();  // ESP32-S3 internal sensor, °C
    if (!(t > -40.0f && t < 125.0f)) return kFallbackTempCenti;
    return static_cast<int16_t>(t * 100.0f);
}

}  // namespace

bool service(uint16_t apid, uint8_t sys_state, volatile bool* rx_pending) {
    static unsigned long last_tx         = 0;
    static unsigned long next_attempt_ms = 0;

    // First heartbeat fires one full interval after boot — lets the
    // radio and GPS stub settle before the first frame.
    if (millis() - last_tx < kIntervalMs) return false;

    // Busy-backoff window: after a busy CAD, do NOT retry every loop
    // iteration. Each scanChannel pulls the radio out of RX mode, so a
    // tight retry loop deafens this board exactly while the peer is
    // transmitting (observed on bench: buyer missed every re-advertised
    // invoice once its due heartbeat started CAD-scanning against the
    // seller's traffic). Heartbeat is droppable — losing a second of
    // cadence costs nothing.
    if (millis() < next_attempt_ms) return false;

    // Gather inputs BEFORE the CAD scan so the clear-channel verdict is
    // as fresh as possible when we commit to transmitting.
    GpsStub.update();

    heartbeat_builder::HeartbeatInputs in = {};
    in.apid          = apid;
    in.epoch_ts      = GpsStub.getEpochMs();
    in.pressure_pa   = GpsStub.getPressurePa();
    in.lat_fixed     = GpsStub.getLatFixed1e7();
    in.lon_fixed     = GpsStub.getLonFixed1e7();
    in.vbatt_mv      = readVbattMv();
    in.temp_c        = readTempCenti();
    in.gps_speed_cms = GpsStub.getSpeedCms();
    in.sys_state     = sys_state;
    in.sat_lock      = GpsStub.getSatLock();

    static uint8_t frame[heartbeat_builder::kHeartbeatSize];
    if (!heartbeat_builder::build(in, frame, sizeof(frame))) {
        Serial.println("ERR: heartbeat_builder::build failed.");
        last_tx = millis();  // don't spin on a persistent build failure
        return false;
    }

    // Droppable telemetry: a single CAD scan, then a 1 s backoff while
    // busy, never forced. scanChannel leaves the radio in standby —
    // re-arm RX before returning.
    if (!Void.channelClear()) {
        Void.radio.startReceive();
        next_attempt_ms = millis() + kBusyBackoffMs;
        Serial.println("HB: channel busy - deferred");
        return false;
    }

    // A frame may have landed during the CAD scan (the ISR sets the
    // caller's flag). TX shares the SX126x FIFO base with RX, so
    // transmitting now would clobber the received bytes — yield and
    // let the main loop drain RX first.
    if (rx_pending != nullptr && *rx_pending && Void.isRealReception()) {
        Void.radio.startReceive();
        next_attempt_ms = millis() + kBusyBackoffMs;
        return false;
    }

    Void.radio.transmit(frame, sizeof(frame));
    Void.radio.startReceive();

    // Evidence line: on the buyer board the bouncer forwards this frame
    // to the gateway (heartbeats.json); on the seller it is console
    // evidence captured by pio monitor.
    Serial.print("HEARTBEAT_TX:");
    Void.hexDump(frame, sizeof(frame));

    last_tx = millis();
    return true;
}

}  // namespace heartbeat_tx

#else  // CCSDS tier or no GPS stub: heartbeat disabled, keep linkage.

namespace heartbeat_tx {
bool service(uint16_t /*apid*/, uint8_t /*sys_state*/,
             volatile bool* /*rx_pending*/) { return false; }
}  // namespace heartbeat_tx

#endif  // VOID_PROTOCOL_TYPE == 2 && defined(VOID_GPS_STUB)
