#pragma once
// The battery since boot, shared by both UIs: a sample every interval into
// N slots; when they're full, neighbours are merged in pairs and the
// interval doubles, so the log always covers the whole time since boot in
// N/2..N samples (a minute apart at first, an hour apart after ~2 days at
// N = 64). Each sample also notes whether external power was in (USB):
// the estimate only looks at the time on battery since it was last
// unplugged.
//
// estimate(): the percentage (the user's curve, battery::percent) fitted
// against time over the last few hours on battery -- a least-squares line,
// so the ADC's jitter averages out -- and what's left at that rate.
//
// Standalone header (no MeshCore / display dependencies but Battery.h).

#include <stdint.h>
#include <stdio.h>
#include "StatusHistory.h"
#include "Battery.h"

namespace uicore {

template <int N, unsigned long FIRST_MS = 60000UL>
struct BattLog {
  static_assert(N >= 8 && N % 2 == 0, "N: even, at least 8");
  History<N> mv;          // millivolts, oldest first (the chart's input)
  bool ext[N] = {};       // the same slots: on external power then
  unsigned long interval_ms = FIRST_MS;
  uint32_t version = 0;   // counts every change, for a view to see one

  void tick(unsigned long now, int batt_mv, bool external) {
    if (batt_mv <= 0) return;
    if (_next && (long)(now - _next) < 0) return;
    if (mv.n == N) compact();
    ext[mv.n] = external;   // History pushes at n while not full
    mv.push((int16_t)batt_mv);
    _next = now + interval_ms;
    version++;
  }

  // How far back the log goes, in minutes.
  uint32_t spanMinutes() const { return mv.n > 1 ? (uint32_t)(mv.n - 1) * (interval_ms / 60000UL) : 0; }
  // Sample i's age, in minutes (0 = the newest).
  uint32_t agoMinutes(int i) const { return (uint32_t)(mv.n - 1 - i) * (interval_ms / 60000UL); }

  struct Estimate {
    bool ok;            // enough time on battery to say
    bool charging;      // on external power now: nothing to estimate
    int pct_per_h_x10;  // the drain, % an hour x 10
    uint32_t minutes_left;
  };
  // At least MIN_MIN minutes on battery, fitted over the last WINDOW_MIN.
  Estimate estimate(int low_mv, uint32_t min_min = 30, uint32_t window_min = 360) const {
    Estimate e = { false, false, 0, 0 };
    if (!mv.n) return e;
    if (ext[mv.n - 1]) { e.charging = true; return e; }
    const uint32_t step = interval_ms / 60000UL ? interval_ms / 60000UL : 1;
    int first = mv.n - 1;   // back to the last unplug, at most the window
    while (first > 0 && !ext[first - 1] && (uint32_t)(mv.n - first) * step < window_min) first--;
    const int k = mv.n - first;
    if (k < 4 || (uint32_t)(k - 1) * step < min_min) return e;
    // Least squares of % on minutes (t = 0 the oldest in the window).
    double st = 0, sp = 0, stt = 0, stp = 0;
    for (int i = 0; i < k; i++) {
      const double t = (double)i * step, p = battery::percent(mv.at(first + i), low_mv);
      st += t; sp += p; stt += t * t; stp += t * p;
    }
    const double den = k * stt - st * st;
    if (den <= 0) return e;
    const double slope = (k * stp - st * sp) / den;   // % a minute; negative while draining
    e.ok = true;
    e.pct_per_h_x10 = (int)(-slope * 600 + 0.5);
    const int pct_now = battery::percent(mv.at(mv.n - 1), low_mv);
    if (slope < -0.0005) {   // under 0.03 %/h is no drain worth a figure
      const double m = pct_now / -slope;
      e.minutes_left = m > 99 * 60 ? 99 * 60 : (uint32_t)m;
    }
    return e;
  }

private:
  unsigned long _next = 0;

  // Neighbours merged in pairs (a pair on power if either was), the
  // interval doubled: N samples become N/2, spanning the same time.
  void compact() {
    int16_t v[N / 2];
    bool x[N / 2];
    for (int i = 0; i < N / 2; i++) {
      v[i] = (int16_t)((mv.at(2 * i) + mv.at(2 * i + 1)) / 2);
      x[i] = ext[2 * i] || ext[2 * i + 1];
    }
    mv.n = 0; mv.head = 0;
    for (int i = 0; i < N / 2; i++) { ext[i] = x[i]; mv.push(v[i]); }
    interval_ms *= 2;
  }
};

// "13 h", "2 d 4 h", "45 min": a span or a time left, as short as it reads.
inline void spanText(uint32_t mins, char* out, int n) {
  if (mins < 60) snprintf(out, n, "%lu min", (unsigned long)mins);
  else if (mins < 48 * 60) snprintf(out, n, "%lu h", (unsigned long)((mins + 30) / 60));
  else snprintf(out, n, "%lu d %lu h", (unsigned long)(mins / 1440), (unsigned long)(mins % 1440 / 60));
}

}  // namespace uicore
