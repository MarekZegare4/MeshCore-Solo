#pragma once

#include <Arduino.h>
#include <math.h>
#include <stdint.h>
#include <time.h>
#include "Persist.h"
#include "GeoUtils.h"

// RAM-only GPS trail ring buffer.
// Storage cost: CAPACITY(512) × sizeof(TrailPoint)(16 B, padded) = 8 KB,
// always resident in .bss (UITask::_trail, and ui_task is a global object —
// see main.cpp). Because the nRF52 heap region starts just above .bss, every
// byte added here is a byte taken from the heap: at 1024 points (16 KB) free
// heap fell to ~4 KB and the input/menu path started failing its allocations
// while the periodic redraw kept running. Keep this conservative. The trail
// survives auto-off (only
// the display blanks) but is lost on reboot — user explicitly snapshots to a
// LittleFS slot before powering down to keep it.
//
// Samples are simplified in-stream as they arrive (see addPoint()): a straight
// stretch is stored as just its two endpoints, no matter how long, while a
// turn still gets a vertex roughly every min-delta of deviation — so CAPACITY
// covers a much longer route than CAPACITY × min-delta would suggest.

// Boards with PSRAM raise it (the L2: -D TRAIL_CAPACITY=32768, 512 KB, since
// UiCore is heap-allocated there, in PSRAM); the default stays for the nRF52.
#ifndef TRAIL_CAPACITY
#define TRAIL_CAPACITY 512
#endif
// Straight-run simplification (addPoint()): on by default, for the nRF52's
// 512 points. A board with room for every sample turns it off
// (-D TRAIL_SIMPLIFY=0): each sample past the min-delta gate is a vertex.
#ifndef TRAIL_SIMPLIFY
#define TRAIL_SIMPLIFY 1
#endif

struct TrailPoint {
  int32_t  lat_1e6;
  int32_t  lon_1e6;
  uint32_t ts;            // epoch seconds (RTC)
  uint8_t  flags;         // bit 0 = SEG_START (don't draw a line from the previous point)
};

static const uint8_t TRAIL_FLAG_SEG_START = 0x01;

class TrailStore {
public:
  static const int CAPACITY = TRAIL_CAPACITY;
  // _count is serialised as uint16_t in the save header — fail the build loudly
  // if CAPACITY is ever grown past what that can hold, rather than truncating.
  static_assert(CAPACITY <= 0xFFFF, "TrailStore::CAPACITY must fit in the uint16_t save-header count");

  // Fixed sampling cadence — matches the sensor manager's default GPS update
  // rate (1 s). Density is controlled by the min-delta gate (settings) rather
  // than by throttling the GPS poll. The NodePrefs::trail_interval_idx field
  // is retained as reserved for backwards compatibility but no longer used.
  static const uint16_t SAMPLING_SECS = 1;

  // Min-delta (metres) gates samples too close to the previous one.
  // Default 5 m: keeps walking jitter out, dense enough for a visible trail.
  // The index is a unit-agnostic "level" (0=finest … 3=coarsest); the actual
  // gate distance and its label follow the global metric/imperial preference.
  static const uint8_t MIN_DELTA_COUNT = 4;
  static uint16_t minDeltaMeters(uint8_t idx, bool imperial) {
    static const uint16_t MET[MIN_DELTA_COUNT] = { 5, 10, 25, 100 };
    static const uint16_t IMP[MIN_DELTA_COUNT] = { 5, 9, 23, 91 };  // ≈ 15/30/75/300 ft
    if (idx >= MIN_DELTA_COUNT) idx = 0;
    return imperial ? IMP[idx] : MET[idx];
  }
  static const char* minDeltaLabel(uint8_t idx, bool imperial) {
    static const char* MET[MIN_DELTA_COUNT] = { "5 m", "10 m", "25 m", "100 m" };
    static const char* IMP[MIN_DELTA_COUNT] = { "15 ft", "30 ft", "75 ft", "300 ft" };
    if (idx >= MIN_DELTA_COUNT) idx = 0;
    return imperial ? IMP[idx] : MET[idx];
  }

  // Speed / pace display units. UNITS_KMH / UNITS_MPH show speed; UNITS_PACE_KM
  // / UNITS_PACE_MI show time per distance ("pace"). Index 0 = km/h default.
  enum Units : uint8_t {
    UNITS_KMH     = 0,
    UNITS_MPH     = 1,
    UNITS_PACE_KM = 2,
    UNITS_PACE_MI = 3,
  };
  static const uint8_t UNITS_COUNT = 4;
  static const char* unitLabel(uint8_t idx) {
    static const char* L[UNITS_COUNT] = { "km/h", "mph", "min/km", "min/mi" };
    return L[idx < UNITS_COUNT ? idx : 0];
  }
  static bool unitIsPace(uint8_t idx) { return idx == UNITS_PACE_KM || idx == UNITS_PACE_MI; }

  bool isActive() const { return _active; }
  void setActive(bool a) {
    if (a && !_active) {
      // off → on: start a new session timer.
      _session_start_ms = millis();
      _paused = false;
    } else if (_active && !a) {
      // on → off: bank the elapsed of this session and arm a segment break
      // so the renderer doesn't draw a straight line through the dead time.
      if (_session_start_ms != 0) {
        _accumulated_ms   += millis() - _session_start_ms;
        _session_start_ms  = 0;
      }
      flushPending();
      _pending_seg_break = true;
      _paused = false;
    }
    _active = a;
  }

  // Auto-pause: freeze the active trail without ending the session. Banks the
  // running session time (so elapsedSeconds() stops advancing) and arms a
  // segment break so the map doesn't draw a line across the idle gap; resuming
  // restarts the session timer. Distinct from setActive() — the trail stays
  // "on" (UI shows "paused", home-screen blink keeps going). No-op if inactive.
  bool isPaused() const { return _paused; }
  void setPaused(bool p) {
    if (!_active || p == _paused) return;
    if (p) {
      if (_session_start_ms != 0) {
        _accumulated_ms  += millis() - _session_start_ms;
        _session_start_ms = 0;
      }
      flushPending();
      _pending_seg_break = true;
    } else {
      _session_start_ms = millis();  // resume timing from now
    }
    _paused = p;
  }

  int  count() const { return _count; }
  bool empty() const { return _count == 0; }

  // i = 0 → oldest entry, i = count()-1 → newest.
  const TrailPoint& at(int i) const { return _buf[(_head + i) % CAPACITY]; }
  const TrailPoint& first() const   { return at(0); }
  const TrailPoint& last()  const   { return at(_count - 1); }

  void clear() {
    _head = 0; _count = 0;
    _pending_seg_break = false;
    _accumulated_ms    = 0;
    _session_start_ms  = 0;
    _paused            = false;
    _has_pending       = false;
    _dist_m            = 0;
    _gen++;
  }

  // For whoever mirrors the trail (the map's line, the L2's card copy): gen()
  // changes when the points are replaced (clear, load), seq() counts every
  // point ever added -- the newest seq() - old_seq points are new, unless the
  // ring has also dropped its oldest (count() == CAPACITY).
  uint32_t gen() const { return _gen; }
  uint32_t seq() const { return _seq; }
  // The simplifier's uncommitted candidate: where the trail really ends now.
  bool pendingPoint(TrailPoint& out) const { if (_has_pending) out = _pending; return _has_pending; }

  // Returns true if the sample was accepted (passed the min-delta gate) —
  // whether that landed it as a new committed vertex right away, or just
  // extended the pending candidate (see below). First point of the ring and
  // the first point after a stop/start cycle get flagged TRAIL_FLAG_SEG_START
  // so the map renderer breaks the line.
  //
  // Straight-run simplification (a fixed-corridor / Reumann–Witkam pass): a
  // sample is only ever a *candidate* vertex (_pending) until a later one
  // proves the run has bent. The corridor is the straight line anchored at the
  // last committed vertex and aimed at the first sample of this run (_dir);
  // each new sample is tested against *that fixed line*. While it stays within
  // CORRIDOR_FACTOR x min_delta_m of the corridor the run is still "straight
  // enough", so drop the intermediate and extend; once a sample leaves the
  // corridor the previous in-corridor sample (_pending) was the last good
  // vertex, so commit it and open a fresh corridor from there.
  //
  // The corridor is deliberately tighter than the min-delta gate itself
  // (CORRIDOR_FACTOR < 1): the gate is the noise floor for *rejecting* samples
  // outright, while the corridor decides when a run has bent enough to need a
  // new vertex. Field data showed plenty of headroom (4 km at a 10 m gate cost
  // just 38 of the 512-point capacity), so a tighter corridor trades some of
  // that headroom for a polyline that hugs the real track more closely.
  //
  // The fixed direction is the whole point: testing the newest sample against
  // a line that re-aims at the newest sample (or testing the previous
  // candidate, which always sits right beside that moving endpoint where the
  // cross-track is near zero) lets a long gentle curve slip through one
  // sub-min_delta step at a time and collapse to a single chord that deviates
  // arbitrarily far. Anchoring the direction bounds the stored polyline to
  // ~CORRIDOR_FACTOR x min_delta_m of the real track, while a straight road
  // still costs just its two endpoints no matter how long it is.
  // How much tighter the simplification corridor is than the min-delta gate
  // (see addPoint() above). 1.0 would track the gate exactly; lowering this
  // commits more vertices per route (more fidelity, less reduction).
  static constexpr float CORRIDOR_FACTOR = 0.5f;

  bool addPoint(int32_t lat_1e6, int32_t lon_1e6, uint32_t ts, uint16_t min_delta_m) {
    const TrailPoint* ref = _has_pending ? &_pending : (_count > 0 ? &last() : nullptr);
    if (ref && !_pending_seg_break) {
      float d = haversineMeters(ref->lat_1e6, ref->lon_1e6, lat_1e6, lon_1e6);
      if (d < (float)min_delta_m) return false;
    }

    if (_count == 0 || _pending_seg_break) {
      flushPending();   // shouldn't normally have one here, but never lose real distance
      commitPoint(lat_1e6, lon_1e6, ts, TRAIL_FLAG_SEG_START);
      _pending_seg_break = false;
      return true;
    }

#if !TRAIL_SIMPLIFY
    commitPoint(lat_1e6, lon_1e6, ts, 0);
    return true;
#endif
    TrailPoint sample{ lat_1e6, lon_1e6, ts, 0 };
    if (!_has_pending) {
      _pending     = sample;   // last in-corridor sample (the commit candidate)
      _dir         = sample;   // fixes the corridor direction: last() → _dir
      _has_pending = true;
      return true;
    }

    if (crossTrackMeters(last(), _dir, sample) <= (float)min_delta_m * CORRIDOR_FACTOR) {
      _pending = sample;                                            // within corridor — extend
    } else {
      commitPoint(_pending.lat_1e6, _pending.lon_1e6, _pending.ts, 0);  // left corridor — keep last good
      _pending = sample;   // the exiting sample opens the next run...
      _dir     = sample;   // ...and fixes its corridor direction from the just-committed vertex
    }
    return true;
  }

  // Sum of pairwise Haversine deltas across the whole ring, skipping segment
  // boundaries (a SEG_START point isn't reached from its predecessor). The
  // uncommitted candidate (_pending) is counted too, so the live total doesn't
  // stall over a long straight run — where simplification holds the whole
  // stretch as one pending point until a bend forces a commit (see addPoint()).
  // Kept as a running total (commitPoint()), not summed on every call: the
  // L2's trail runs to tens of thousands of points.
  uint32_t totalDistanceMeters() const {
    float d = _dist_m;
    if (_has_pending && _count > 0)
      d += haversineMeters(last().lat_1e6, last().lon_1e6,
                            _pending.lat_1e6, _pending.lon_1e6);
    return (uint32_t)d;
  }

  // Cumulative active tracking time across all start→stop sessions, in
  // seconds. Counts ticks while the trail is on, freezes while it's off.
  // millis()-based so it doesn't depend on RTC sync.
  uint32_t elapsedSeconds() const {
    uint32_t ms = _accumulated_ms;
    if (_active && _session_start_ms != 0) ms += millis() - _session_start_ms;
    return ms / 1000;
  }

  // Average speed in km/h = total distance / cumulative active time.
  uint16_t avgSpeedKmh() const {
    uint32_t es = elapsedSeconds();
    if (es == 0) return 0;
    return (uint16_t)((float)totalDistanceMeters() / (float)es * 3.6f);
  }

  // Compute bounding box across all points. Returns false if empty.
  bool boundingBox(int32_t& min_lat, int32_t& min_lon,
                   int32_t& max_lat, int32_t& max_lon) const {
    if (_count == 0) return false;
    min_lat = max_lat = first().lat_1e6;
    min_lon = max_lon = first().lon_1e6;
    for (int i = 1; i < _count; i++) {
      const auto& p = at(i);
      if (p.lat_1e6 < min_lat) min_lat = p.lat_1e6;
      if (p.lat_1e6 > max_lat) max_lat = p.lat_1e6;
      if (p.lon_1e6 < min_lon) min_lon = p.lon_1e6;
      if (p.lon_1e6 > max_lon) max_lon = p.lon_1e6;
    }
    return true;
  }

  // Persistent snapshot — single slot at the given filesystem path.
  // Layout: 4-byte magic "TRAL", uint8 version, uint8 reserved, uint16 count,
  // uint32 accumulated_ms, then `count` raw TrailPoint records. count is
  // clamped to CAPACITY on load.
  static const uint32_t SAVE_MAGIC = 0x4C415254;  // "TRAL"
  static const uint8_t  SAVE_VERSION = 1;

  // Caller supplies an opened, writable File (the FS-open call is
  // platform-specific). Returns true if the header and every point wrote
  // cleanly. The file is left open for the caller to close.
  template <typename F>
  bool writeTo(F& file) {
    flushPending();   // the snapshot should include the latest position, not lag behind it
    if (!persist::writeHeader(file, SAVE_MAGIC, SAVE_VERSION, (uint16_t)_count)) return false;
    uint32_t accum = currentAccumulatedMs();
    if (file.write((uint8_t*)&accum, sizeof(accum)) != sizeof(accum)) return false;
    for (int i = 0; i < _count; i++) {
      if (file.write((uint8_t*)&at(i), sizeof(TrailPoint)) != sizeof(TrailPoint)) return false;
    }
    return true;
  }

  template <typename F>
  bool readFrom(F& file) {
    uint16_t cnt = 0;
    uint32_t accum = 0;
    if (!persist::readHeader(file, SAVE_MAGIC, SAVE_VERSION, cnt)) return false;
    if (file.read((uint8_t*)&accum, sizeof(accum)) != (int)sizeof(accum)) return false;
    if (cnt > CAPACITY) return false;
    if (_active) {
      _active = false;
      _session_start_ms = 0;
    }
    _paused = false;
    _has_pending = false;   // any candidate belonged to the session being replaced
    _head = 0;
    _count = 0;
    for (int i = 0; i < cnt; i++) {
      TrailPoint p;
      int n = file.read((uint8_t*)&p, sizeof(TrailPoint));
      if (n != (int)sizeof(TrailPoint)) break;
      _buf[_count++] = p;
    }
    _accumulated_ms = accum;
    _pending_seg_break = true;
    _dist_m = 0;
    for (int i = 1; i < _count; i++)
      if (!(_buf[i].flags & TRAIL_FLAG_SEG_START))
        _dist_m += haversineMeters(_buf[i - 1].lat_1e6, _buf[i - 1].lon_1e6, _buf[i].lat_1e6, _buf[i].lon_1e6);
    _gen++;
    return true;
  }

  // GPX writers — shared helpers so we can dump from RAM and from flash
  // through the same formatting code.

  template <typename S>
  static size_t gpxHeader(S& out) {
    size_t n = 0;
    n += out.print(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    n += out.print(F("<gpx version=\"1.1\" creator=\"MeshCore\" "
                      "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"));
    return n;
  }

  template <typename S>
  static size_t gpxTrackOpen(S& out, const char* name) {
    size_t n = 0;
    n += out.print(F("<trk><name>"));
    n += out.print(name);
    n += out.print(F("</name>\n"));
    return n;
  }

  // Emit saved waypoints as <wpt> elements. In GPX 1.1 these must precede the
  // <trk>. Duck-typed over any store exposing count()/at(i) whose entries have
  // lat_1e6 / lon_1e6 / ts / label, so Trail.h stays decoupled from Waypoint.h.
  template <typename S, typename WP>
  static size_t gpxWaypoints(S& out, WP& store) {
    size_t n = 0;
    for (int i = 0; i < store.count(); i++) {
      const auto& w = store.at(i);
      // XML-escape the user label (&, <, > only).
      char esc[64]; int e = 0;
      for (const char* p = w.label; *p && e < (int)sizeof(esc) - 6; p++) {
        if      (*p == '&') { memcpy(esc + e, "&amp;", 5); e += 5; }
        else if (*p == '<') { memcpy(esc + e, "&lt;",  4); e += 4; }
        else if (*p == '>') { memcpy(esc + e, "&gt;",  4); e += 4; }
        else                  esc[e++] = *p;
      }
      esc[e] = '\0';
      char buf[160];
      int len = snprintf(buf, sizeof(buf),
        "<wpt lat=\"%.6f\" lon=\"%.6f\"><name>%s</name>",
        w.lat_1e6 / 1.0e6, w.lon_1e6 / 1.0e6, esc);
      if (len > 0) {
        if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;  // truncated: emit chars only, not the NUL
        n += out.write((const uint8_t*)buf, (size_t)len);
      }
      if (w.ts > 1000000000UL) {                 // append <time> when the RTC was set
        time_t t = (time_t)w.ts;
        struct tm* gt = ::gmtime(&t);
        if (gt) {
          len = snprintf(buf, sizeof(buf),
            "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>",
            gt->tm_year + 1900, gt->tm_mon + 1, gt->tm_mday,
            gt->tm_hour, gt->tm_min, gt->tm_sec);
          if (len > 0) { if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
                         n += out.write((const uint8_t*)buf, (size_t)len); }
        }
      }
      n += out.print(F("</wpt>\n"));
    }
    return n;
  }

  template <typename S>
  static size_t gpxFooter(S& out, bool in_segment) {
    size_t n = 0;
    if (in_segment) n += out.print(F("</trkseg>\n"));
    n += out.print(F("</trk></gpx>\n"));
    return n;
  }

  // Emit a single <trkpt>; opens a <trkseg> on a segment boundary. Updates
  // `in_segment` to track open/close pairing.
  template <typename S>
  static size_t gpxPoint(S& out, const TrailPoint& p, bool first, bool& in_segment) {
    size_t n = 0;
    bool seg_start = first || (p.flags & TRAIL_FLAG_SEG_START);
    if (seg_start) {
      if (in_segment) n += out.print(F("</trkseg>\n"));
      n += out.print(F("<trkseg>\n"));
      in_segment = true;
    }
    char buf[120];
    time_t t = (time_t)p.ts;
    struct tm* gt = ::gmtime(&t);
    if (!gt) return n;  // defensive: skip malformed timestamps
    int len = snprintf(buf, sizeof(buf),
      "<trkpt lat=\"%.6f\" lon=\"%.6f\"><time>%04d-%02d-%02dT%02d:%02d:%02dZ</time></trkpt>\n",
      p.lat_1e6 / 1.0e6, p.lon_1e6 / 1.0e6,
      gt->tm_year + 1900, gt->tm_mon + 1, gt->tm_mday,
      gt->tm_hour, gt->tm_min, gt->tm_sec);
    if (len < 0) return n;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;  // truncated: emit chars only, not the NUL
    n += out.write((const uint8_t*)buf, (size_t)len);
    return n;
  }

  // Dump the live RAM ring as GPX (with saved waypoints). Returns bytes written.
  template <typename S, typename WP>
  size_t exportGpx(S& out, WP& wpts, const char* trk_name = "MeshCore Trail") {
    size_t total = gpxHeader(out);
    total += gpxWaypoints(out, wpts);
    total += gpxTrackOpen(out, trk_name);
    bool in_segment = false;
    for (int i = 0; i < _count; i++) {
      total += gpxPoint(out, at(i), i == 0, in_segment);
    }
    total += gpxFooter(out, in_segment);
    return total;
  }

  // Stream a saved trail straight from the open file as GPX without
  // touching the live RAM ring. Returns 0 on format mismatch.
  template <typename F, typename S, typename WP>
  static size_t exportGpxFromFile(F& file, S& out, WP& wpts, const char* trk_name = "MeshCore Trail") {
    uint16_t cnt = 0;
    uint32_t accum = 0;
    if (!persist::readHeader(file, SAVE_MAGIC, SAVE_VERSION, cnt)) return 0;
    if (file.read((uint8_t*)&accum, sizeof(accum)) != (int)sizeof(accum)) return 0;
    if (cnt > CAPACITY) return 0;

    size_t total = gpxHeader(out);
    total += gpxWaypoints(out, wpts);
    total += gpxTrackOpen(out, trk_name);
    bool in_segment = false;
    for (uint16_t i = 0; i < cnt; i++) {
      TrailPoint p;
      int n = file.read((uint8_t*)&p, sizeof(TrailPoint));
      if (n != (int)sizeof(TrailPoint)) break;
      total += gpxPoint(out, p, i == 0, in_segment);
    }
    total += gpxFooter(out, in_segment);
    return total;
  }

  uint32_t currentAccumulatedMs() const {
    uint32_t ms = _accumulated_ms;
    if (_active && _session_start_ms != 0) ms += millis() - _session_start_ms;
    return ms;
  }

  // Approximate great-circle distance in metres. Delegates to the shared
  // geo:: Haversine (km) so there is a single implementation.
  static float haversineMeters(int32_t la1, int32_t lo1, int32_t la2, int32_t lo2) {
    return geo::haversineKm(la1, lo1, la2, lo2) * 1000.0f;
  }

private:
  TrailPoint _buf[CAPACITY];
  int      _head             = 0;
  int      _count            = 0;
  bool     _active           = false;
  bool     _paused           = false;  // auto-paused (active but timer/sampling frozen)
  bool     _pending_seg_break = false;  // next addPoint flags itself SEG_START
  uint32_t _accumulated_ms   = 0;       // banked active time across previous sessions
  uint32_t _session_start_ms = 0;       // millis() of the current active session, 0 if none
  float    _dist_m           = 0;       // totalDistanceMeters() without the pending candidate
  uint32_t _gen              = 0;       // see gen() / seq()
  uint32_t _seq              = 0;

  // Streaming simplification: a sample that passed the min-delta gate but
  // hasn't been committed as a real vertex yet — see addPoint(). _dir is the
  // first sample of the current run; last()→_dir fixes the corridor direction
  // every later sample of the run is tested against.
  bool       _has_pending = false;
  TrailPoint _pending;
  TrailPoint _dir;

  void commitPoint(int32_t lat_1e6, int32_t lon_1e6, uint32_t ts, uint8_t flags) {
    if (_count > 0 && !(flags & TRAIL_FLAG_SEG_START))
      _dist_m += haversineMeters(last().lat_1e6, last().lon_1e6, lat_1e6, lon_1e6);
    int pos;
    if (_count < CAPACITY) {
      pos = (_head + _count) % CAPACITY;
      _count++;
    } else {   // full: the oldest point goes, and the stretch from it to the next
      if (!(at(1).flags & TRAIL_FLAG_SEG_START))
        _dist_m -= haversineMeters(at(0).lat_1e6, at(0).lon_1e6, at(1).lat_1e6, at(1).lon_1e6);
      if (_dist_m < 0) _dist_m = 0;
      pos = _head;
      _head = (_head + 1) % CAPACITY;
    }
    _seq++;
    _buf[pos].lat_1e6 = lat_1e6;
    _buf[pos].lon_1e6 = lon_1e6;
    _buf[pos].ts      = ts;
    _buf[pos].flags   = flags;
  }

  // Commit the pending candidate (if any) as a real vertex. Called before a
  // segment break (stop/pause/save) so the last stretch of a straight run is
  // never silently dropped just because no bend came along to force a commit.
  void flushPending() {
    if (!_has_pending) return;
    commitPoint(_pending.lat_1e6, _pending.lon_1e6, _pending.ts, 0);
    _has_pending = false;
  }

  // Perpendicular ("cross-track") distance from q to the line through a→b, in
  // metres. Planar approximation (equirectangular, centred at a) — accurate
  // enough at trail scale, where consecutive points are metres to a few km
  // apart. Falls back to a straight distance to a if a and b coincide.
  static float crossTrackMeters(const TrailPoint& a, const TrailPoint& b, const TrailPoint& q) {
    static const float M_PER_DEG = 111320.0f;   // metres per degree of latitude
    float lat_rad   = (a.lat_1e6 * 1e-6f) * ((float)M_PI / 180.0f);
    float lon_scale = cosf(lat_rad);
    float bx = (b.lon_1e6 - a.lon_1e6) * 1e-6f * M_PER_DEG * lon_scale;
    float by = (b.lat_1e6 - a.lat_1e6) * 1e-6f * M_PER_DEG;
    float ab_len = sqrtf(bx * bx + by * by);
    if (ab_len < 0.01f) return haversineMeters(a.lat_1e6, a.lon_1e6, q.lat_1e6, q.lon_1e6);
    float qx = (q.lon_1e6 - a.lon_1e6) * 1e-6f * M_PER_DEG * lon_scale;
    float qy = (q.lat_1e6 - a.lat_1e6) * 1e-6f * M_PER_DEG;
    float cross = bx * qy - by * qx;
    return fabsf(cross) / ab_len;
  }
};
