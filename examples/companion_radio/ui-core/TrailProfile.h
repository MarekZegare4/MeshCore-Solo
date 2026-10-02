#pragma once
// A trail's height profile, shared by both UIs: the lowest and highest
// altitude, the climb (rises past 3 m of GPS jitter), and a walk over the
// points that carry an altitude with the distance covered to each. Duck-typed
// over any store with count() / at(i) whose points have lat_1e6, lon_1e6,
// flags and alt_m (TrailStore, a saved trail).

#include "../Trail.h"

namespace trailprofile {

struct Stats {
  bool any = false;   // some point has an altitude
  int lo = 0, hi = 0; // metres
  int gain = 0;       // the climb, metres
  // One altitude more, in order.
  void add(int a) {
    if (!any) { lo = hi = a; any = true; }
    if (a < lo) lo = a;
    if (a > hi) hi = a;
    if (_last == TRAIL_ALT_NONE || a < _last) _last = a;
    else if (a - _last >= 3) { gain += a - _last; _last = a; }
  }
private:
  int _last = TRAIL_ALT_NONE;
};

template <class Store>
Stats stats(const Store& s) {
  Stats st;
  for (int i = 0; i < s.count(); i++)
    if (s.at(i).alt_m != TRAIL_ALT_NONE) st.add(s.at(i).alt_m);
  return st;
}

// fn(metres from the start, altitude) for each point with an altitude, in
// order. A segment start adds no distance (a gap in the recording).
template <class Store, class Fn>
void walk(const Store& s, Fn fn) {
  float cum = 0;
  for (int i = 0; i < s.count(); i++) {
    const auto& p = s.at(i);
    if (i > 0 && !(p.flags & TRAIL_FLAG_SEG_START))
      cum += TrailStore::haversineMeters(s.at(i - 1).lat_1e6, s.at(i - 1).lon_1e6, p.lat_1e6, p.lon_1e6);
    if (p.alt_m != TRAIL_ALT_NONE) fn(cum, (int)p.alt_m);
  }
}

// The profile of a trail too long to keep (a file read point by point): at
// most K samples, the stats of all of them. Fed point by point; when full,
// every other sample goes and only every second one after is kept, so the
// samples stay spread over the whole trail.
struct Sampler {
  static const int K = 128;
  float dist[K];      // metres from the start
  int16_t alt[K];
  int n = 0;
  float total = 0;    // the trail's length, metres
  Stats st;

  void feed(const TrailPoint& p) {
    if (_have && !(p.flags & TRAIL_FLAG_SEG_START))
      total += TrailStore::haversineMeters(_prev_lat, _prev_lon, p.lat_1e6, p.lon_1e6);
    _have = true; _prev_lat = p.lat_1e6; _prev_lon = p.lon_1e6;
    if (p.alt_m == TRAIL_ALT_NONE) return;
    st.add(p.alt_m);
    if (_skip++ % _stride) return;
    if (n == K) {   // full: keep every other, take every second from now on
      for (int i = 0; i < K / 2; i++) { dist[i] = dist[2 * i]; alt[i] = alt[2 * i]; }
      n = K / 2;
      _stride *= 2;
      _skip = 1;
    }
    dist[n] = total; alt[n] = p.alt_m; n++;
  }
  template <class Store>
  void feedAll(const Store& s) { for (int i = 0; i < s.count(); i++) feed(s.at(i)); }

private:
  bool _have = false;
  int32_t _prev_lat = 0, _prev_lon = 0;
  int _stride = 1, _skip = 0;
};

}  // namespace trailprofile
