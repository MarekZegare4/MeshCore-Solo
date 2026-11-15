#pragma once
// Position + course over ground. The single source of truth for "where am I"
// and "which way am I heading", shared by the nav / compass / map screens.
//
// Course-over-ground ring: a heading source independent of trail recording,
// sampled every ~1 s from the GPS whenever it has a fix. Heading = bearing to
// the newest fix from the latest one at least MIN_MOVE_M behind it, within the
// last RING seconds: a bike's is a couple of seconds old, a slow walk's (3 km/h,
// under a metre a second) fifteen. Gross GPS jumps are rejected on insert.

#include "../GeoUtils.h"

class CourseEngine {
public:
  void begin(SensorManager* sensors) { _sensors = sensors; }

  void loop() {
    if ((int32_t)(millis() - _next_sample_ms) < 0) return;
    _next_sample_ms = millis() + 1000UL;
    int32_t lat, lon;
    if (currentLocation(lat, lon)) {
      _fix_lat = lat; _fix_lon = lon; _fix_ms = millis(); _had_fix = true;
      pushFix(lat, lon);
    }
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

  // The last fix since this start, and how old it is. Only RAM: a position
  // saved on flash has no age, so it can't be told from one a thousand km away.
  bool lastFix(int32_t& lat, int32_t& lon, uint32_t& age_secs) const {
    if (!_had_fix) return false;
    lat = _fix_lat; lon = _fix_lon;
    age_secs = (uint32_t)(millis() - _fix_ms) / 1000UL;
    return true;
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
    // GPS noise is a few metres: over 12 m it turns the heading by ~15° at most.
    static const float MIN_MOVE_M = 12.0f;
    if (_count >= 2) {
      const Fix& newest = _ring[(_head + _count - 1) % RING];
      for (int i = _count - 2; i >= 0; i--) {   // newest first: the shortest stretch that's enough
        const Fix& f = _ring[(_head + i) % RING];
        if (geo::haversineKm(f.lat, f.lon, newest.lat, newest.lon) * 1000.0f < MIN_MOVE_M) continue;
        if (onTheWay(f, _ring[(_head + (i + _count - 1) / 2) % RING], newest))
          _deg = geo::bearingDeg(f.lat, f.lon, newest.lat, newest.lon);
        break;
      }
    }
    if (_deg < 0) return false;
    deg_out = _deg;   // not far enough lately (standing still): the last good one held
    return true;
  }

private:
  struct Fix { int32_t lat, lon; uint32_t ms; };

  // The fix halfway between a and b in time lies along the way from one to the
  // other (each half at least a quarter of it): walking, it does; GPS noise
  // wandering round a standing receiver seldom lines up like that.
  static bool onTheWay(const Fix& a, const Fix& m, const Fix& b) {
    const float kx = cosf(a.lat * 1e-6f * (float)M_PI / 180.0f);
    const float dx = (b.lon - a.lon) * kx, dy = (float)(b.lat - a.lat);
    const float mx = (m.lon - a.lon) * kx, my = (float)(m.lat - a.lat);
    const float t = (mx * dx + my * dy) / (dx * dx + dy * dy);   // m's place from a (0) to b (1)
    return t >= 0.25f && t <= 0.75f;
  }

  int32_t  _fix_lat = 0, _fix_lon = 0;
  uint32_t _fix_ms = 0;
  bool     _had_fix = false;
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

  static const int RING = 30;   // seconds of fixes

  SensorManager* _sensors = nullptr;
  Fix      _ring[RING];
  uint8_t  _head = 0, _count = 0;
  mutable int _deg = -1;         // last good heading (cached by currentCourse), -1 = none yet
  uint32_t _next_sample_ms = 0;
};
