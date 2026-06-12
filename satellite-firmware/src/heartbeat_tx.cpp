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

constexpr unsigned long kIntervalMs = 30000;  // VOID-022 AC cadence

// Heltec V3 battery sense: ADC_CTRL (GPIO37, active LOW) gates a
// 390k/100k divider feeding VBAT_Read (GPIO1); battery mV ≈ adc × 4.9.
constexpr uint8_t  kAdcCtrlPin        = 37u;
constexpr uint8_t  kVbattAdcPin       = 1u;
constexpr uint16_t kFallbackVbattMv   = 4100u;  // bench is often USB-only
constexpr int16_t  kFallbackTempCenti = 2300;   // 23.00 °C

uint16_t readVbattMv() {
    pinMode(kAdcCtrlPin, OUTPUT);
    digitalWrite(kAdcCtrlPin, LOW);   // enable the divider
    delayMicroseconds(500);
    const uint32_t adc_mv = analogReadMilliVolts(kVbattAdcPin);
    digitalWrite(kAdcCtrlPin, HIGH);  // disable to save power
    const uint32_t batt_mv = (adc_mv * 49u) / 10u;
    // USB-only benches read ~0 mV; absurd values mean a wiring or
    // calibration fault. Fall back rather than report garbage telemetry.
    if (batt_mv < 2500u || batt_mv > 5500u) return kFallbackVbattMv;
    return static_cast<uint16_t>(batt_mv);
}

int16_t readTempCenti() {
    const float t = temperatureRead();  // ESP32-S3 internal sensor, °C
    if (!(t > -40.0f && t < 125.0f)) return kFallbackTempCenti;
    return static_cast<int16_t>(t * 100.0f);
}

}  // namespace

bool service(uint16_t apid, uint8_t sys_state) {
    static unsigned long last_tx = 0;

    // First heartbeat fires one full interval after boot — lets the
    // radio and GPS stub settle before the first frame.
    if (millis() - last_tx < kIntervalMs) return false;

    // Droppable telemetry: a single CAD scan, defer while busy (timer
    // not advanced, so it retries next loop), never forced. scanChannel
    // leaves the radio in standby — re-arm RX before returning.
    if (!Void.channelClear()) {
        Void.radio.startReceive();
        return false;
    }

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
bool service(uint16_t /*apid*/, uint8_t /*sys_state*/) { return false; }
}  // namespace heartbeat_tx

#endif  // VOID_PROTOCOL_TYPE == 2 && defined(VOID_GPS_STUB)
