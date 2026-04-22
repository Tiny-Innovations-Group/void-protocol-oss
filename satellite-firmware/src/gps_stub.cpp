/*-------------------------------------------------------------------------
 * VOID PROTOCOL v2.1 | Tiny Innovation Group Ltd
 * -------------------------------------------------------------------------
 * Authority: Tiny Innovation Group Ltd
 * License:   Apache 2.0
 * Status:    Authenticated Clean Room Spec
 * File:      gps_stub.cpp
 * Desc:      Synthetic GPS source for the alpha balloon demo (VOID-132).
 *            Replays a pre-recorded ~160 min HAB trajectory at wall-clock
 *            speed, falling back to fixed coordinates once the trajectory
 *            completes (or if no trajectory is compiled in).
 * Compliant: NSA Clean C++ / SEI CERT
 * -------------------------------------------------------------------------*/

#ifdef VOID_GPS_STUB

#include "gps_stub.h"
#include <Arduino.h>
#include <cmath>
#include <cstring>

// ── Synthetic epoch base ────────────────────────────────────────────
// 2026-10-01T00:00:00Z — close to the target HAB launch window.
// Added to millis() to produce a plausible Unix-epoch timestamp.
static const uint64_t kEpochBaseMs = 1790985600000ULL;

// Epoch base must be post-2026 and pre-2030 to stay plausible.
static_assert(kEpochBaseMs > 1767225600000ULL,  // 2026-01-01
              "Epoch base too far in the past");
static_assert(kEpochBaseMs < 1893456000000ULL,  // 2030-01-01
              "Epoch base too far in the future");

// ── WGS84 constants ────────────────────────────────────────────────
static const double kWgs84A  = 6378137.0;           // semi-major axis (m)
static const double kWgs84E2 = 6.69437999014e-3;    // first eccentricity squared

// ── Geodetic → ECEF conversion (WGS84) ─────────────────────────────
// Standard closed-form transform. No heap, no dynamic allocation.
static void geodeticToEcef(double lat_deg, double lon_deg, double alt_m,
                           double out[3]) {
    const double deg2rad = 3.14159265358979323846 / 180.0;
    const double lat = lat_deg * deg2rad;
    const double lon = lon_deg * deg2rad;
    const double slat = sin(lat);
    const double clat = cos(lat);
    const double slon = sin(lon);
    const double clon = cos(lon);

    const double N = kWgs84A / sqrt(1.0 - kWgs84E2 * slat * slat);

    out[0] = (N + alt_m) * clat * clon;           // X
    out[1] = (N + alt_m) * clat * slon;           // Y
    out[2] = (N * (1.0 - kWgs84E2) + alt_m) * slat; // Z
}

// ── Pre-recorded HAB trajectory ─────────────────────────────────────
// Simulated launch from Elsworth, Cambridgeshire, UK (52.24 N, -0.02 E).
// Ascent ~5 m/s to 30 km, burst, descent ~5 m/s.  Total ~160 min.
// Wind drift: gentle NE push.  11 waypoints, linearly interpolated.
static constexpr GeoWaypoint kTrajectory[] = {
    //  t(s)   lat       lon       alt(m)
    {     0, 52.2400, -0.0200,     50.0 },  // Launch (ground)
    {   600, 52.2420, -0.0100,   3050.0 },  // 10 min — climbing
    {  1200, 52.2500,  0.0100,   6050.0 },  // 20 min
    {  2400, 52.2700,  0.0500,  12050.0 },  // 40 min
    {  3600, 52.3000,  0.1000,  18050.0 },  // 60 min
    {  4800, 52.3300,  0.1600,  24050.0 },  // 80 min
    {  6000, 52.3600,  0.2200,  30050.0 },  // 100 min — burst altitude
    {  6600, 52.3800,  0.2600,  24050.0 },  // 110 min — descending
    {  7200, 52.3900,  0.2800,  18050.0 },  // 120 min
    {  8400, 52.4000,  0.3000,   6050.0 },  // 140 min
    {  9600, 52.4100,  0.3200,     50.0 },  // 160 min — landing
};
static const size_t kTrajectoryLen = sizeof(kTrajectory) / sizeof(kTrajectory[0]);

// ── Trajectory compile-time guards ──────────────────────────────────
// These prevent silent corruption of the trajectory table.
static_assert(sizeof(kTrajectory) / sizeof(kTrajectory[0]) == 11,
              "Trajectory must have exactly 11 waypoints — do not add/remove without updating this assert");
// First waypoint must be t=0 (launch origin).
static_assert(kTrajectory[0].t_sec == 0,
              "Trajectory must start at t=0");
// Last waypoint must be the expected flight duration (9600s = 160 min).
static_assert(kTrajectory[10].t_sec == 9600,
              "Trajectory must end at t=9600s (160 min)");
// Burst altitude sanity: waypoint 6 (100 min) should be peak (~30 km).
static_assert(kTrajectory[6].alt_m > 29000.0 && kTrajectory[6].alt_m < 35000.0,
              "Burst waypoint altitude must be 29-35 km");
// Landing altitude sanity: last waypoint should be near ground.
static_assert(kTrajectory[10].alt_m < 200.0,
              "Landing waypoint must be below 200m ASL");

// ── Linear interpolation between two waypoints ─────────────────────
static double linearInterp(double a, double b, double t) {
    return a + (b - a) * t;
}

static void interpolateWaypoint(const GeoWaypoint& a, const GeoWaypoint& b,
                                uint32_t t_sec, double out[3]) {
    const double span = static_cast<double>(b.t_sec - a.t_sec);
    const double frac = (span > 0.0)
                        ? static_cast<double>(t_sec - a.t_sec) / span
                        : 0.0;

    const double lat = linearInterp(a.lat_deg, b.lat_deg, frac);
    const double lon = linearInterp(a.lon_deg, b.lon_deg, frac);
    const double alt = linearInterp(a.alt_m,   b.alt_m,   frac);

    geodeticToEcef(lat, lon, alt, out);
}

// ── GpsStubClass implementation ─────────────────────────────────────

GpsStubClass GpsStub;

void GpsStubClass::begin() {
    _boot_millis = millis();
    _epoch_ms    = kEpochBaseMs;

    // Initialise position to launch site.
    geodeticToEcef(kTrajectory[0].lat_deg,
                   kTrajectory[0].lon_deg,
                   kTrajectory[0].alt_m,
                   _pos_ecef);
}

void GpsStubClass::update() {
    const uint32_t elapsed_ms = millis() - _boot_millis;
    const uint32_t elapsed_sec = elapsed_ms / 1000U;

    // Epoch timestamp: synthetic base + wall-clock offset.
    _epoch_ms = kEpochBaseMs + static_cast<uint64_t>(elapsed_ms);

    // Walk the trajectory table to find the bracketing segment.
    const uint32_t last_t = kTrajectory[kTrajectoryLen - 1].t_sec;

    if (elapsed_sec >= last_t) {
        // Past end of trajectory — hold at landing position.
        geodeticToEcef(kTrajectory[kTrajectoryLen - 1].lat_deg,
                       kTrajectory[kTrajectoryLen - 1].lon_deg,
                       kTrajectory[kTrajectoryLen - 1].alt_m,
                       _pos_ecef);
        return;
    }

    for (size_t i = 0; i + 1 < kTrajectoryLen; ++i) {
        if (elapsed_sec >= kTrajectory[i].t_sec &&
            elapsed_sec <  kTrajectory[i + 1].t_sec) {
            interpolateWaypoint(kTrajectory[i], kTrajectory[i + 1],
                                elapsed_sec, _pos_ecef);
            return;
        }
    }

    // Should not reach here, but fallback to launch site.
    geodeticToEcef(kTrajectory[0].lat_deg,
                   kTrajectory[0].lon_deg,
                   kTrajectory[0].alt_m,
                   _pos_ecef);
}

void GpsStubClass::getPositionVec(double out[3]) const {
    memcpy(out, _pos_ecef, sizeof(_pos_ecef));
}

uint64_t GpsStubClass::getEpochMs() const {
    return _epoch_ms;
}

uint32_t GpsStubClass::missionElapsedSec() const {
    return (millis() - _boot_millis) / 1000U;
}

#endif // VOID_GPS_STUB
