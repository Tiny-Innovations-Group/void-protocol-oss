/*-------------------------------------------------------------------------
 * VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      gps_stub.h
 * Desc:      Synthetic GPS source for the alpha balloon demo (VOID-132).
 *            Compiled when VOID_GPS_STUB is defined. Provides ECEF position
 *            and epoch timestamps without a real u-blox module.
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifndef GPS_STUB_H
#define GPS_STUB_H

#include <cstdint>

// WGS84 geodetic waypoint (lat/lon in degrees, altitude in metres ASL).
struct GeoWaypoint {
    uint32_t t_sec;     // seconds since launch
    double   lat_deg;
    double   lon_deg;
    double   alt_m;
};

class GpsStubClass {
public:
    // Initialise the stub. Call once from setup().
    void begin();

    // Advance internal clock and interpolate position.  Call every loop().
    void update();

    // Current ECEF position written into out[3] (X, Y, Z in metres).
    // Layout matches PacketA_t / PacketB_t / HeartbeatPacket_t pos_vec.
    void getPositionVec(double out[3]) const;

    // Current synthetic Unix-epoch timestamp in milliseconds.
    // Monotonically increasing; suitable for epoch_ts fields.
    uint64_t getEpochMs() const;

    // Elapsed mission time in seconds since begin().
    uint32_t missionElapsedSec() const;

    // Always returns true (stub always has a "fix").
    bool hasFix() const { return true; }

private:
    uint32_t _boot_millis;
    double   _pos_ecef[3];
    uint64_t _epoch_ms;
};

extern GpsStubClass GpsStub;

#endif // GPS_STUB_H
