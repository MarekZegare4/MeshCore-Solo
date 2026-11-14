#pragma once
// Locator: the one "active target" the device tracks (a waypoint, or a person
// by 6-byte pubkey prefix -- persisted in NodePrefs::locator_*), the geofence
// crossing state machine around it and the proximity beeper. The target is
// shared by the Locator geofence, the Nav bearing/ETA view and the map.
//
// Output: UiEventType::LocatorCrossed (text = ready-made alert, flag = arrived)
// on an armed crossing, UiEventType::LocatorBeep for each proximity tick.

#include "../GeoUtils.h"
#include "UiEvents.h"
#include "CourseEngine.h"
#include "LiveShareEngine.h"

class LocatorEngine {
public:
  void begin(NodePrefs* prefs, const CourseEngine* course, const LiveShareEngine* live,
             UiEventQueue* events) {
    _prefs = prefs; _course = course; _live = live; _events = events;
  }

  void loop() {
    // Crossing check -- cheap; a few seconds of latency at the boundary is fine.
    if ((int32_t)(millis() - _next_eval_ms) >= 0) {
      _next_eval_ms = millis() + 3000UL;
      evaluate();
    }
    // Proximity beeper -- its own short cadence (the crossing check is too coarse).
    proximityBeeper();
  }

  // Re-arm the crossing state machine so the next evaluation initialises
  // silently (target/radius changed, or a fresh GPS wake may deliver a
  // still-settling first fix -- neither must read as a crossing).
  void reset() { _known = false; }

  // ── Target ─────────────────────────────────────────────────────────────────
  // setTarget() only *defines* the target (fields + re-arm); the caller decides
  // when to persist. kind 0 = waypoint (key ignored), 1 = person (key required),
  // 2 = channel live share (no key: followed by the sender name in `name`,
  // lat/lon = where they were, used once their share goes stale).
  void setTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
    if (!_prefs) return;
    _prefs->locator_target_kind = kind;
    if (kind == 1 && key) memcpy(_prefs->locator_key, key, NodePrefs::FAVOURITE_PREFIX_LEN);
    _prefs->locator_lat_1e6 = lat;
    _prefs->locator_lon_1e6 = lon;
    snprintf(_prefs->locator_label, sizeof(_prefs->locator_label), "%s", name);
    _prefs->locator_has_target = 1;
    reset();   // re-seed the crossing engine so the change can't fire on a stale state
  }
  void clearTarget() {
    if (!_prefs) return;
    _prefs->locator_has_target = 0;
    reset();
  }
  // If the active target is exactly this waypoint, clear it and persist (called
  // from waypoint deletion so the Locator can't keep pointing at a spot that no
  // longer exists).
  // The target is the waypoint at this spot.
  bool targetsWaypoint(int32_t lat_1e6, int32_t lon_1e6) const {
    return _prefs && _prefs->locator_has_target && _prefs->locator_target_kind == 0 &&
           _prefs->locator_lat_1e6 == lat_1e6 && _prefs->locator_lon_1e6 == lon_1e6;
  }
  void clearTargetIfWaypoint(int32_t lat_1e6, int32_t lon_1e6) {
    if (!_prefs || !_prefs->locator_has_target || _prefs->locator_target_kind != 0) return;
    if (_prefs->locator_lat_1e6 != lat_1e6 || _prefs->locator_lon_1e6 != lon_1e6) return;
    clearTarget();
    the_mesh.savePrefs();
  }
  // Contact removed: drop the target if it was this person. Returns true if
  // prefs changed (caller persists).
  bool onContactRemoved(const uint8_t* pub_key) {
    if (!_prefs || !_prefs->locator_has_target || _prefs->locator_target_kind != 1) return false;
    if (memcmp(_prefs->locator_key, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) != 0) return false;
    clearTarget();
    return true;
  }

  // One precedence for a person's position — an active [LOC] live share wins,
  // else the last-advertised GPS fix. Not everyone keeps live-sharing on, so the
  // fallback lets a rarely-updating but stationary node (a repeater, or someone
  // who shared a fix once) still work as a target. Optional live/ts report
  // freshness for the picker's age tag.
  bool resolvePersonPos(const uint8_t* key, int32_t& lat, int32_t& lon,
                        bool* live = nullptr, uint32_t* ts = nullptr) const {
    if (live) *live = false;
    if (ts)   *ts   = 0;
    if (!key) return false;
    const LiveTrackStore::Entry* e =
        _live->track().activeByKey(key, (uint32_t)rtc_clock.getCurrentTime());
    if (e) {
      lat = e->lat_1e6; lon = e->lon_1e6;
      if (live) *live = true;
      if (ts)   *ts   = e->ts;
      return true;
    }
    ContactInfo* c = the_mesh.lookupContactByPubKey(key, NodePrefs::FAVOURITE_PREFIX_LEN);
    if (c && (c->gps_lat != 0 || c->gps_lon != 0)) {
      lat = c->gps_lat; lon = c->gps_lon;
      if (ts) *ts = c->lastmod;
      return true;
    }
    return false;
  }

  // Resolved position of the active target. Gated only on a target being set,
  // independent of whether the Locator alert is enabled, so a destination you
  // set still shows on the map.
  bool activeTargetPos(int32_t& lat, int32_t& lon) const {
    if (!_prefs || !_prefs->locator_has_target) return false;
    if (_prefs->locator_target_kind == 1)
      return resolvePersonPos(_prefs->locator_key, lat, lon);
    if (_prefs->locator_target_kind == 2) {
      const LiveTrackStore::Entry* e = _live->track().activeByName(
          _prefs->locator_label, sizeof(_prefs->locator_label) - 1, (uint32_t)rtc_clock.getCurrentTime());
      if (e) { lat = e->lat_1e6; lon = e->lon_1e6; return true; }
    }
    lat = _prefs->locator_lat_1e6;
    lon = _prefs->locator_lon_1e6;
    return true;
  }

private:
  // Distance (m) from the current GPS fix to the target, plus the configured
  // radius (m). False when no target is set or there's no fix.
  bool distance(float& dist_m, float& radius_m) const {
    int32_t tlat, tlon;
    if (!activeTargetPos(tlat, tlon)) return false;
    int32_t lat, lon;
    if (!_course->currentLocation(lat, lon)) return false;
    dist_m   = geo::haversineKm(lat, lon, tlat, tlon) * 1000.0f;
    radius_m = (float)NodePrefs::locatorRadiusMeters(_prefs->locator_radius_idx);
    return true;
  }

  // Crossing the radius fires according to the configured mode; a hysteresis
  // band on the "leave" edge stops it chattering at the boundary, and the first
  // reading after arming only seeds the inside/outside state.
  void evaluate() {
    if (!_prefs || !_prefs->locator_enabled || !_prefs->locator_has_target) {
      _known = false;
      return;
    }
    float dist, r;
    if (!distance(dist, r)) return;   // armed but no fix yet — keep state
    bool inside;
    if (!_known)       inside = dist <= r;            // seed state
    else if (_inside)  inside = dist <= r * 1.25f;    // leave past band
    else               inside = dist <= r;            // arrive at edge

    if (_known && inside != _inside) {
      uint8_t mode = _prefs->locator_mode;  // 0=arrive,1=leave,2=both
      bool fire = inside ? (mode == 0 || mode == 2) : (mode == 1 || mode == 2);
      if (fire) fireCrossing(inside);
    }
    _inside = inside;
    _known  = true;
  }

  void fireCrossing(bool arrived) {
    const char* lbl = _prefs->locator_label[0] ? _prefs->locator_label : "target";
    bool person = _prefs->locator_target_kind != 0;
    char msg[sizeof(UiEvent::text)];
    // "Near/Away" reads naturally for a moving person; "Arrived/Left" for a place.
    snprintf(msg, sizeof(msg),
             arrived ? (person ? "Near: %s"  : "Arrived: %s")
                     : (person ? "Away: %s"  : "Left: %s"), lbl);
    _events->push(UiEventType::LocatorCrossed, msg, arrived);
  }

  // Ticks while inside the radius, faster the nearer the target.
  void proximityBeeper() {
    static const uint32_t BEEP_MIN_MS = 150;    // fastest cadence (at the target)
    static const uint32_t BEEP_MAX_MS = 2000;   // slowest cadence (at the edge)
    if (!_prefs || !_prefs->locator_enabled || !_prefs->locator_beeper
        || !_prefs->locator_has_target || _prefs->locator_mode == 1) {  // leave-only mode: no homing
      return;
    }
    if ((int32_t)(millis() - _beep_check_ms) < 0) return;
    _beep_check_ms = millis() + 250UL;

    float dist, r;
    if (!distance(dist, r)) return;
    if (dist > r) {                       // outside the zone: stay quiet, beep on re-entry
      _beep_next_ms = millis();
      return;
    }
    if ((int32_t)(millis() - _beep_next_ms) < 0) return;
    float frac = (r > 0) ? dist / r : 0;  // 0 at centre, 1 at edge
    if (frac < 0) frac = 0; else if (frac > 1) frac = 1;
    uint32_t interval = BEEP_MIN_MS + (uint32_t)(frac * (BEEP_MAX_MS - BEEP_MIN_MS));
    _events->push(UiEventType::LocatorBeep);
    _beep_next_ms = millis() + interval;
  }

  NodePrefs*             _prefs  = nullptr;
  const CourseEngine*    _course = nullptr;
  const LiveShareEngine* _live   = nullptr;
  UiEventQueue*          _events = nullptr;

  // _known guards the first evaluation after arming (initialise inside/outside
  // silently, fire only on later crossings).
  uint32_t _next_eval_ms = 0;
  bool     _inside = false;
  bool     _known = false;
  // Beeper: _beep_check_ms throttles the distance poll; _beep_next_ms is when
  // the next tick is due.
  uint32_t _beep_check_ms = 0;
  uint32_t _beep_next_ms = 0;
};
