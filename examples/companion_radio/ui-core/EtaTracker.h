#pragma once
// Closing-speed / ETA estimator for the "navigate to a point" views. The caller
// keeps one instance per navigation session and feeds it the live target
// distance, as often as it draws. Speed is the rate the gap closes, so it works
// for both a fixed waypoint and a moving live contact; ETA = remaining distance
// / closing speed, meaningful only while actually approaching.
//
// The rate is taken across the last N distances, one kept every STEP_MS (~16 s):
// from one second to the next GPS noise of a few metres is ±10 km/h, and a
// redraw a moment after a fix (a tap) made it a hundred.

#include <stdint.h>
#include <stdio.h>

namespace navview {

struct EtaTracker {
  static const uint32_t STEP_MS = 2000, MIN_SPAN_MS = 6000;
  static const int N = 8;   // so the window is up to N * STEP_MS (16 s)
  float    closing_kmh = 0.0f;   // > 0 = gap shrinking (approaching)
  bool     have        = false;
  void reset() { _n = 0; have = false; closing_kmh = 0.0f; }
  void update(float dist_km, uint32_t now_ms) {
    if (_n == 0 || now_ms - _ms[(_head + _n - 1) % N] >= STEP_MS) {
      int pos;
      if (_n < N) pos = (_head + _n++) % N;
      else { pos = _head; _head = (_head + 1) % N; }
      _km[pos] = dist_km; _ms[pos] = now_ms;
    }
    const uint32_t span = now_ms - _ms[_head];
    if (span < MIN_SPAN_MS) return;   // too short to tell (have stays as it was)
    closing_kmh = (_km[_head] - dist_km) / (span / 3600000.0f);   // +approaching, -receding
    have = true;
  }
  // "12m" / "1h05m" into out, or false while not approaching.
  bool eta(float dist_km, char* out, size_t n) const {
    if (!have || closing_kmh <= 0.3f) return false;
    int secs = (int)(dist_km / closing_kmh * 3600.0f);
    if (secs < 3600) snprintf(out, n, "%dm", (secs + 59) / 60);
    else             snprintf(out, n, "%dh%02dm", secs / 3600, (secs % 3600) / 60);
    return true;
  }

private:
  float    _km[N];
  uint32_t _ms[N];
  uint8_t  _head = 0, _n = 0;
};

}  // namespace navview
