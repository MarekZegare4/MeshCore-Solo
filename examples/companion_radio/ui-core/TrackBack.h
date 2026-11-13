#pragma once
// Track back: retrace the recorded trail in reverse. Snaps onto the route at
// the recorded point nearest to you, then targets each earlier breadcrumb in
// turn -- advancing when you come within ARRIVE_M of it, or of one of the
// SKIP_AHEAD after it (a corner cut) -- until the trail's start is reached.
// Where there are planned routes (FEAT_ROUTES) the trail is followed as a
// route instead (UiCore::startTrackBack); this is for the boards without. The frontend shows the current breadcrumb as a navigate
// target (ui-new: NavView; ui-lvgl: the Navigation map).

#include "../Trail.h"
#include "../GeoUtils.h"

class TrackBack {
public:
  static const int ARRIVE_M = 20;
  static const int SKIP_AHEAD = 8;
  enum Step : uint8_t { NONE, ADVANCED, ARRIVED };

  // False when there's no trail to walk back (fewer than 2 points).
  bool start(const TrailStore& s, bool have_fix, int32_t lat, int32_t lon) {
    if (s.count() < 2) return false;
    int idx = s.count() - 1;   // default: the newest end of the trail
    if (have_fix) {
      float best = 1e30f;
      for (int i = 0; i < s.count(); i++) {
        float d = geo::haversineKm(lat, lon, s.at(i).lat_1e6, s.at(i).lon_1e6);
        if (d < best) { best = d; idx = i; }
      }
    }
    _idx = idx;
    _active = true;
    return true;
  }
  void stop() { _active = false; }
  bool active() const { return _active; }
  int  index() const { return _idx; }   // current breadcrumb; 0 = the trail start

  // Feed the current fix; advances past a reached breadcrumb, ends at the start.
  Step poll(const TrailStore& s, bool have_fix, int32_t lat, int32_t lon) {
    if (!_active) return NONE;
    if (_idx < 0 || _idx >= s.count()) { _active = false; return NONE; }   // trail reset under us
    if (!have_fix) return NONE;
    int reached = -1;   // the furthest along the way back within reach
    for (int i = _idx; i >= 0 && i >= _idx - SKIP_AHEAD; i--)
      if (geo::haversineKm(lat, lon, s.at(i).lat_1e6, s.at(i).lon_1e6) * 1000.0f <= (float)ARRIVE_M) reached = i;
    if (reached < 0) return NONE;
    _idx = reached;
    if (_idx > 0) { _idx--; return ADVANCED; }
    _active = false;
    return ARRIVED;
  }

  // Header text for the current leg: "Trail start" on the last one, else points to go.
  void label(char* out, int n) const {
    if (_idx == 0) snprintf(out, n, "Trail start");
    else           snprintf(out, n, "Back: %d pt", _idx);
  }

private:
  int  _idx = 0;
  bool _active = false;
};
