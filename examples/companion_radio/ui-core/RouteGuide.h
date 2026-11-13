#pragma once
// Following a planned route (../PlannedRoute.h), in either direction. Runs in
// the background, whatever screen is up: the frontends show its target as a
// navigate view and the route on their maps.
//
// Each second, with a fix: where on the route you are, as the nearest point of
// the segments around the last place (a few behind, WIN_AHEAD ahead, so a route
// that crosses itself or comes back the same way doesn't throw you to the
// other leg). Nothing near enough there: the whole route is searched, and a
// place further on, within a little of the nearest, wins (a shortcut taken).
// On the first fix, or when the nearest is a jump ahead, the earliest place
// within SNAP_SLACK_M of the nearest: a loop's end lies beside its start, and
// GPS noise mustn't put you at the end.
// Then the target: the first point at least LOOKAHEAD_M further along, so the
// arrow leads round the bends; off the route, the nearest point on it.
//
// Progress is saved every SAVE_M along (NodePrefs::route_seg): after a restart
// the first fix is looked for around there before the whole route.
//
// Off route past OFF_ENTER_M, back on under OFF_LEAVE_M (no flicker at the
// edge); the end within ARRIVE_M: finished. Each pushes a RouteAlert event.

#include "../PlannedRoute.h"
#include "../GeoUtils.h"
#include "CourseEngine.h"
#include "UiEvents.h"

class RouteGuide {
public:
  static const int OFF_ENTER_M = 50;
  static const int OFF_LEAVE_M = 30;
  static const int ARRIVE_M    = 25;
  static const int LOOKAHEAD_M = 25;
  static const int WIN_BACK    = 2;
  static const int WIN_AHEAD   = 40;
  static const int SHORTCUT_SLACK_M = 20;
  static const int SNAP_SLACK_M = 30;
  static const int JUMP_M = 100;
  static const int SAVE_M = 250;
  static const uint32_t POLL_MS = 1000;
  static const uint32_t FULL_SEARCH_MS = 10000;   // off route: the whole route at most this often

  enum State : uint8_t { OFF, ON_ROUTE, OFF_ROUTE, FINISHED };

  // Picks up the route followed before a restart (NodePrefs::route_follow).
  void begin(NodePrefs* prefs, const CourseEngine* course, UiEventQueue* events) {
    _prefs = prefs; _course = course; _events = events;
    if (_prefs && _prefs->route_follow
        && !start((_prefs->route_follow & 0x7F) - 1, _prefs->route_follow & 0x80, _prefs->route_seg))
      remember(-1, false);   // gone since
  }

  // `resume`: the segment it was at before a restart (NodePrefs::route_seg).
  bool start(int slot, bool reversed, int resume = 0) {
    RouteInfo ri;
    if (!PlannedRoutes::info(slot, ri)) return false;
    if (resume < 0 || resume >= ri.count - 1) resume = 0;
    remember(slot, reversed, resume);
    _info = ri;
    _slot = slot;
    buildOverview();
    _rev = reversed;
    _gen = PlannedRoutes::generation();
    _seg = 0;
    _along = 0;
    _off_m = 0;
    _snap = true;
    _have_target = false;
    _joined = false;
    _resume = resume > 0 ? resume : -1;
    _saved_along = 0;
    _next_full = 0;
    _next_poll = 0;
    _state = ON_ROUTE;
    return true;
  }
  void stop() { _state = OFF; _have_target = false; remember(-1, false); }

  bool  active() const    { return _state == ON_ROUTE || _state == OFF_ROUTE; }
  State state() const     { return _state; }
  bool  offRoute() const  { return _state == OFF_ROUTE; }
  int   slot() const      { return _state == OFF ? -1 : _slot; }
  bool  reversed() const  { return _rev; }
  // Track back: the trail's copy, followed backwards (UiCore::startTrackBack).
  bool  trackBack() const { return active() && _slot == PlannedRoutes::TRAIL_SLOT; }
  const char* name() const { return _info.name; }
  const RouteInfo& info() const { return _info; }
  uint32_t totalM() const     { return _info.length_m; }
  uint32_t remainingM() const { return _along >= _info.length_m ? 0 : (uint32_t)(_info.length_m - _along); }
  uint32_t offM() const       { return (uint32_t)_off_m; }
  // A place on the route found yet (the first fix after start()).
  bool placed() const { return !_snap; }
  bool target(int32_t& lat, int32_t& lon) const {
    if (!active() || !_have_target) return false;
    lat = _t_lat; lon = _t_lon;
    return true;
  }
  // Where you are along the route, as a point on it (for the map).
  bool onRoutePos(int32_t& lat, int32_t& lon) const {
    if (!active() || _snap) return false;
    lat = _p_lat; lon = _p_lon;
    return true;
  }

  // The route for a map, from RAM: at most OVERVIEW points spread evenly along
  // it (the ends always), so drawing it never reads the flash. A map shows the
  // whole route, so that's as fine as its pixels; in file order.
  int overviewCount() const { return _state == OFF ? 0 : _ov_n; }
  void overviewPoint(int i, int32_t& lat, int32_t& lon) const {
    lat = _info.min_lat + (int32_t)((int64_t)_ov[i].la * ((int64_t)_info.max_lat - _info.min_lat) / 65535);
    lon = _info.min_lon + (int32_t)((int64_t)_ov[i].lo * ((int64_t)_info.max_lon - _info.min_lon) / 65535);
  }

  // "4.2 km left", "Off route 80 m", "Finished": a header for the navigate view.
  void label(char* out, int n, bool imperial) const {
    char d[12];
    if (_state == FINISHED) { snprintf(out, n, "%s", finishedText()); return; }
    if (_snap) { snprintf(out, n, "%s: no fix", _slot == PlannedRoutes::TRAIL_SLOT ? "Track back" : "Route"); return; }
    if (_state == OFF_ROUTE) {
      geo::fmtDist(d, sizeof(d), _off_m / 1000.0f, imperial);
      snprintf(out, n, "Off route %s", d);
      return;
    }
    geo::fmtDist(d, sizeof(d), remainingM() / 1000.0f, imperial);
    snprintf(out, n, _slot == PlannedRoutes::TRAIL_SLOT ? "%s to start" : "%s left", d);
  }

  void loop() {
    if (!active() || !_course) return;
    if ((int32_t)(millis() - _next_poll) < 0) return;
    _next_poll = millis() + POLL_MS;

    if (_gen != PlannedRoutes::generation()) {   // replaced or deleted under us
      RouteInfo ri;
      if (!PlannedRoutes::info(_slot, ri) || ri.count != _info.count || ri.length_m != _info.length_m) {
        stop();
        if (_events) _events->push(UiEventType::RouteAlert, "Route removed", false);
        return;
      }
      _gen = PlannedRoutes::generation();
    }

    int32_t lat, lon;
    if (!_course->currentLocation(lat, lon)) return;
    _lat0 = lat; _lon0 = lon;
    _kx = cosf(lat * 1e-6f * (float)M_PI / 180.0f) * 0.11132f;   // metres per 1e-6 degree of longitude here
    _ky = 0.11132f;

    // Far from the whole route (started away from it, or wandered off): the
    // overview is near enough, and the flash isn't searched end to end each time.
    if ((_snap || _state == OFF_ROUTE) && farFromRoute()) return;

    char path[8];
    PlannedRoutes::slotPath(_slot, path, sizeof(path));
    File f = PlannedRoutes::openRead(path);
    if (!f) return;

    Cand best, ahead;
    const int nseg = _info.count - 1;
    if (_snap && _resume >= 0) {   // after a restart: where it was, if you're still there
      const int s0 = _resume - WIN_BACK < 0 ? 0 : _resume - WIN_BACK;
      const int s1 = _resume + WIN_AHEAD > nseg - 1 ? nseg - 1 : _resume + WIN_AHEAD;
      Cand near, unused;
      scan(f, s0, s1, near, unused);
      _resume = -1;
      if (near.seg >= 0 && near.d <= OFF_ENTER_M) {
        earliestNear(f, s0, s1, near);
        _snap = false;
        _joined = true;
        _off_m = near.d;
        _seg = near.seg; _along = _saved_along = near.along;
        _p_lat = near.lat; _p_lon = near.lon;
        findTarget(f);
        f.close();
        return;
      }   // else found afresh, as at a start
    }
    if (!_snap) {
      int s0 = _seg - WIN_BACK, s1 = _seg + WIN_AHEAD;
      if (s0 < 0) s0 = 0;
      if (s1 > nseg - 1) s1 = nseg - 1;
      scan(f, s0, s1, best, ahead);
      if (best.seg >= 0 && best.along - _along > JUMP_M) earliestNear(f, s0, s1, best);
    }
    if (_snap || best.d > OFF_ENTER_M) {
      if (_snap || (int32_t)(millis() - _next_full) >= 0) {
        _next_full = millis() + FULL_SEARCH_MS;
        Cand all, all_ahead;
        scan(f, 0, nseg - 1, all, all_ahead);
        // The nearest (the earliest of equals: a loop's start, not its end), unless
        // a place further on is nearly as near -- a shortcut, but never past half
        // the route, and not on the first fix: where you start is the start.
        const bool shortcut = !_snap && all_ahead.seg >= 0 && all_ahead.d <= all.d + SHORTCUT_SLACK_M
                              && all_ahead.along - _along < _info.length_m / 2.0f;
        Cand pick = shortcut ? all_ahead : all;
        if (_snap && all.seg >= 0) { earliestNear(f, 0, nseg - 1, all); pick = all; }
        if (pick.seg >= 0 && (best.seg < 0 || pick.d < best.d)) best = pick;
      }
    }
    if (best.seg < 0) { f.close(); return; }

    _off_m = best.d;
    const bool was_snap = _snap;
    _snap = false;
    if (_state == ON_ROUTE && best.d > OFF_ENTER_M && !was_snap) {
      _state = OFF_ROUTE;
      if (_events) _events->push(UiEventType::RouteAlert, "Off route", false);
    } else if (_state == OFF_ROUTE && best.d < OFF_LEAVE_M) {
      _state = ON_ROUTE;
      if (_events) _events->push(UiEventType::RouteAlert, _joined ? "Back on route" : "On route", true);
    } else if (was_snap && best.d > OFF_ENTER_M) {
      _state = OFF_ROUTE;   // started away from it: no alarm, the view says so
    }
    if (_state == ON_ROUTE) _joined = true;

    // Progress moves only while on the route: off it, the place it left stays.
    if (_state == ON_ROUTE) {
      _seg = best.seg;
      _along = best.along;
      _p_lat = best.lat; _p_lon = best.lon;
      if (fabsf(_along - _saved_along) >= SAVE_M) { _saved_along = _along; saveSeg(_seg); }
    }

    if (_state == OFF_ROUTE) {
      _t_lat = best.lat; _t_lon = best.lon;   // back to the nearest point of it
      _have_target = true;
    } else {
      findTarget(f);
    }
    f.close();

    if (_state == ON_ROUTE && remainingM() <= (uint32_t)ARRIVE_M) {
      _state = FINISHED;
      _have_target = false;
      remember(-1, false);   // done: a restart doesn't start it over
      if (_events) _events->push(UiEventType::RouteAlert, finishedText(), true);
    }
  }

private:
  const char* finishedText() const { return _slot == PlannedRoutes::TRAIL_SLOT ? "Back at the start" : "Route finished"; }

  void remember(int slot, bool reversed, int seg = 0) {
    if (!_prefs) return;
    const uint8_t v = slot < 0 ? 0 : (uint8_t)((slot + 1) | (reversed ? 0x80 : 0));
    if (_prefs->route_follow == v && _prefs->route_seg == (uint16_t)seg) return;
    _prefs->route_follow = v;
    _prefs->route_seg = (uint16_t)seg;
    the_mesh.savePrefs();
  }
  void saveSeg(int seg) {
    if (!_prefs || _prefs->route_seg == (uint16_t)seg) return;
    _prefs->route_seg = (uint16_t)seg;
    the_mesh.savePrefs();
  }

  NodePrefs* _prefs = nullptr;
  static const int OVERVIEW = 256;
  struct OvPoint { uint16_t la, lo; };   // 0..65535 across the route's bounding box

  static const int FAR_M = 1000;

  // The nearest overview point; past FAR_M away, that's the target and the
  // state is off route (an alarm only if it was on it).
  bool farFromRoute() {
    float best = 1e30f;
    int32_t blat = 0, blon = 0;
    for (int i = 0; i < _ov_n; i++) {
      int32_t la, lo;
      overviewPoint(i, la, lo);
      const float x = (float)(lo - _lon0) * _kx, y = (float)(la - _lat0) * _ky;
      const float d = x * x + y * y;
      if (d < best) { best = d; blat = la; blon = lo; }
    }
    if (_ov_n == 0 || best <= (float)FAR_M * FAR_M) return false;
    _off_m = sqrtf(best);
    const bool was_snap = _snap;
    _snap = false;
    if (_state == ON_ROUTE) {
      _state = OFF_ROUTE;
      if (_events && !was_snap) _events->push(UiEventType::RouteAlert, "Off route", false);   // not for a start away from it
    }
    _t_lat = blat; _t_lon = blon;
    _have_target = true;
    return true;
  }

  void buildOverview() {
    _ov_n = 0;
    const int64_t sla = (int64_t)_info.max_lat - _info.min_lat, slo = (int64_t)_info.max_lon - _info.min_lon;
    const float step = _info.count > OVERVIEW ? _info.length_m / (float)(OVERVIEW - 1) : 0;
    float next = 0;
    const int last = _info.count - 1;
    PlannedRoutes::forEach(_slot, [&](const RoutePoint& p, int i) {
      if (i != last && (p.cum_m < next || _ov_n >= OVERVIEW - 1)) return;
      next = p.cum_m + step;
      OvPoint& o = _ov[_ov_n++];
      o.la = sla > 0 ? (uint16_t)(((int64_t)p.lat_1e6 - _info.min_lat) * 65535 / sla) : 0;
      o.lo = slo > 0 ? (uint16_t)(((int64_t)p.lon_1e6 - _info.min_lon) * 65535 / slo) : 0;
    });
  }

  OvPoint _ov[OVERVIEW];
  int     _ov_n = 0;

  struct Cand {
    int     seg = -1;
    float   d = 1e30f;
    float   along = 0;
    int32_t lat = 0, lon = 0;
  };

  // Logical points (in the direction followed) [k, k + n) into _buf.
  int load(File& f, int k, int n) {
    const int count = _info.count;
    if (k < 0) k = 0;
    if (k + n > count) n = count - k;
    if (n <= 0) return 0;
    const int phys = _rev ? count - k - n : k;
    int got = PlannedRoutes::readPoints(f, phys, n, _buf);
    if (got != n) return 0;
    if (_rev) {
      for (int a = 0, b = n - 1; a < b; a++, b--) { RoutePoint t = _buf[a]; _buf[a] = _buf[b]; _buf[b] = t; }
      for (int j = 0; j < n; j++) _buf[j].cum_m = _info.length_m - _buf[j].cum_m;
    }
    _buf_k = k;
    _buf_n = n;
    return n;
  }

  void toXY(const RoutePoint& p, float& x, float& y) const {
    x = (float)(p.lon_1e6 - _lon0) * _kx;
    y = (float)(p.lat_1e6 - _lat0) * _ky;
  }

  // `best` the nearest of s0..s1: the nearest of the first stretch of them
  // within SNAP_SLACK_M of it instead.
  void earliestNear(File& f, int s0, int s1, Cand& best) {
    Cand first, unused1, unused2;
    scan(f, s0, s1, unused1, unused2, best.d + SNAP_SLACK_M, &first);
    if (first.seg >= 0) best = first;
  }

  // Segments s0..s1 (segment s joins points s and s+1): the nearest of all, and
  // the nearest of those ending past where you were (a step ahead of _along);
  // with `first`, the nearest of the first run of segments passing within first_max.
  void scan(File& f, int s0, int s1, Cand& best, Cand& ahead, float first_max = 0, Cand* first = nullptr) {
    bool run_over = false;
    for (int s = s0; s <= s1; ) {
      int n = load(f, s, BUF);
      if (n < 2) break;
      for (int j = 0; j + 1 < n && s + j <= s1; j++) {
        const RoutePoint& a = _buf[j];
        const RoutePoint& b = _buf[j + 1];
        float ax, ay, bx, by;
        toXY(a, ax, ay); toXY(b, bx, by);
        const float dx = bx - ax, dy = by - ay;
        const float l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? -(ax * dx + ay * dy) / l2 : 0;   // the point is the origin
        if (t < 0) t = 0; else if (t > 1) t = 1;
        const float px = ax + t * dx, py = ay + t * dy;
        const float d = sqrtf(px * px + py * py);
        Cand c;
        c.seg = s + j;
        c.d = d;
        c.along = a.cum_m + t * ((float)b.cum_m - (float)a.cum_m);
        c.lat = a.lat_1e6 + (int32_t)lroundf(t * (float)(b.lat_1e6 - a.lat_1e6));
        c.lon = a.lon_1e6 + (int32_t)lroundf(t * (float)(b.lon_1e6 - a.lon_1e6));
        if (d < best.d) best = c;
        if (first && !run_over) {
          if (d <= first_max) { if (d < first->d) *first = c; }
          else if (first->seg >= 0) run_over = true;
        }
        if (c.along > _along + LOOKAHEAD_M && d < ahead.d) ahead = c;
      }
      s += n - 1;   // the last point read starts the next block's first segment
    }
  }

  // The first point LOOKAHEAD_M or more past _along; else the route's end.
  void findTarget(File& f) {
    for (int k = _seg + 1; k < _info.count; ) {
      int n = load(f, k, BUF);
      if (n <= 0) break;
      for (int j = 0; j < n; j++) {
        if ((float)_buf[j].cum_m - _along >= LOOKAHEAD_M || k + j == _info.count - 1) {
          _t_lat = _buf[j].lat_1e6; _t_lon = _buf[j].lon_1e6;
          _have_target = true;
          return;
        }
      }
      k += n;
    }
  }

  static const int BUF = 32;
  RoutePoint _buf[BUF];
  int _buf_k = 0, _buf_n = 0;

  const CourseEngine* _course = nullptr;
  UiEventQueue* _events = nullptr;
  RouteInfo _info = {};
  int      _slot = -1;
  bool     _rev = false;
  uint16_t _gen = 0;
  State    _state = OFF;
  bool     _snap = true;
  int      _seg = 0;
  float    _along = 0;
  float    _off_m = 0;
  bool     _have_target = false;
  bool     _joined = false;    // been on the route since start(): "Back on route", not "On route"
  int      _resume = -1;       // the segment saved before a restart, for the first fix
  float    _saved_along = 0;   // _along when route_seg was last saved (every SAVE_M)
  int32_t  _t_lat = 0, _t_lon = 0;
  int32_t  _p_lat = 0, _p_lon = 0;
  int32_t  _lat0 = 0, _lon0 = 0;
  float    _kx = 0.11132f, _ky = 0.11132f;
  uint32_t _next_poll = 0, _next_full = 0;
};
