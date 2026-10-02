#pragma once
// Position + course over ground. The single source of truth for "where am I"
// and "which way am I heading", shared by the nav / compass / map screens.
//
// Course-over-ground ring: a heading source independent of trail recording,
// sampled every ~1 s from the GPS whenever it has a fix. Heading = bearing
// across the window (oldest→newest) once the cumulative movement clears
// MIN_MOVE_M; gross GPS jumps are rejected on insert.

#include "../GeoUtils.h"

class CourseEngine {
public:
  void begin(SensorManager* sensors) { _sensors = sensors; }

  void loop() {
    if ((int32_t)(millis() - _next_sample_ms) < 0) return;
    _next_sample_ms = millis() + 1000UL;
    int32_t lat, lon;
    if (currentLocation(lat, lon)) pushFix(lat, lon);
  }

  // Current GPS position (1e6-scaled degrees), false when there's no usable fix.
  bool currentLocation(int32_t& lat, int32_t& lon) const {
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    if (loc && loc->isValid()) {
      lat = (int32_t)loc->getLatitude();
      lon = (int32_t)loc->getLongitude();
      return true;
    }
    return false;
  }

  // Current GPS altitude in metres, false without a fix.
  bool currentAltitude(int16_t& m) const {
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    if (!loc || !loc->isValid()) return false;
    long a = loc->getAltitude() / 1000;   // the provider reports millimetres
    m = (int16_t)(a < -32000 ? -32000 : a > 32000 ? 32000 : a);
    return true;
  }

  // Current course over ground in degrees (0..359), or false if not enough
  // recent movement to derive a stable heading. Holds the last good heading
  // while standing still.
  bool currentCourse(int& deg_out) const {
    static const float MIN_MOVE_M = 6.0f;   // window must span ≥ this to be a real heading
    if (_count < 2) {
      if (_deg >= 0) { deg_out = _deg; return true; }  // hold last good
      return false;
    }
    const Fix& oldest = _ring[_head];
    const Fix& newest = _ring[(_head + _count - 1) % RING];
    float span_m = geo::haversineKm(oldest.lat, oldest.lon, newest.lat, newest.lon) * 1000.0f;
    if (span_m < MIN_MOVE_M) {
      if (_deg >= 0) { deg_out = _deg; return true; }  // standing still → hold last
      return false;
    }
    _deg = geo::bearingDeg(oldest.lat, oldest.lon, newest.lat, newest.lon);
    deg_out = _deg;
    return true;
  }

private:
  // Insert a GPS fix into the ring, rejecting gross outliers (a jump implying
  // an impossible speed) so one bad fix can't swing the heading.
  void pushFix(int32_t lat, int32_t lon) {
    static const uint32_t MAX_GAP_MS = 15000;  // GPS gap longer than this → window is stale
    uint32_t now = millis();
    if (_count > 0) {
      const Fix& prev = _ring[(_head + _count - 1) % RING];
      uint32_t dt = now - prev.ms;
      if (dt > MAX_GAP_MS) {
        // GPS was lost for a while: the old fixes are far in the past, so a
        // window spanning them would imply a bogus "teleport" heading. Restart
        // the ring from this fix (the last-good _deg is kept for display).
        _head = 0; _count = 0;
      } else if (dt > 0) {
        float dist_m = geo::haversineKm(prev.lat, prev.lon, lat, lon) * 1000.0f;
        float speed  = dist_m / (dt / 1000.0f);   // m/s
        if (speed > 50.0f) return;                 // > 180 km/h between fixes → reject
      }
    }
    int pos;
    if (_count < RING) { pos = (_head + _count) % RING; _count++; }
    else { pos = _head; _head = (_head + 1) % RING; }
    _ring[pos].lat = lat; _ring[pos].lon = lon; _ring[pos].ms = now;
  }

  static const int RING = 5;
  struct Fix { int32_t lat, lon; uint32_t ms; };

  SensorManager* _sensors = nullptr;
  Fix      _ring[RING];
  uint8_t  _head = 0, _count = 0;
  mutable int _deg = -1;         // last good heading (cached by currentCourse), -1 = none yet
  uint32_t _next_sample_ms = 0;
};
