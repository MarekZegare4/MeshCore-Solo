#pragma once
// Learning the cell's discharge curve (NodePrefs::batt_curve_mv) from a run
// on battery, so the percentage -- and the time left from it (BattLog.h) --
// follow this cell, not a table.
//
// A run: from unplugging (or a boot on battery) to the low-battery shutdown
// (Settings > Power > Battery shutdown), with no USB in between. Its first
// half hour is skipped (the surface charge going); it counts when it lasted
// at least 6 h and started at 3.9 V or more. At a steady average draw the
// charge used goes with the time, so the curve's k x 10 % point is the
// voltage when k x 10 % of the run's time was still to go:
//   started full (4.1 V or more after the half hour): 100 % at the start;
//   started lower: the start reads its % on the curve in use (4.02 V: 86 %),
//   the points above it stay as they were.
// 0 % is the shutdown voltage. The voltage is the screen-off one when most of
// the run has some (the screen's load pulls it down by tens of mV), and a
// mean of neighbouring samples.
//
// A new run is weighed against the curve learned before (the second 1/2, the
// third 1/3, then 1/4 each; a partial run less), so one odd run can't spoil
// it and an ageing cell still shows. A curve set by hand (the editor, the
// console's batt.curve) isn't touched until Reset.
//
// Only on boards that can tell USB from battery (isExternalPowered()): the
// nRF52 ones and the L2. Elsewhere BATT_LEARN is 0 and nothing is compiled.
//
// Standalone header: no MeshCore or display dependencies but Battery.h.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "Battery.h"

#ifndef BATT_LEARN
  #if defined(NRF52_PLATFORM) || defined(SEEED_WIO_TRACKER_L2) || defined(SIM_PLATFORM) || defined(BATT_LEARN_TEST)
    #define BATT_LEARN 1
  #else
    #define BATT_LEARN 0
  #endif
#endif

namespace battlearn {

// NodePrefs::batt_curve_src
enum Src : uint8_t { BUILTIN, MANUAL, LEARNED };

// Where the curve in use came from: an unset curve is the built-in one, a set
// one not marked learned was set by hand (also every curve set before 2.4).
inline Src source(const uint16_t* curve, uint8_t src) {
  if (!battery::validCurve(curve)) return BUILTIN;
  return src == LEARNED ? LEARNED : MANUAL;
}

// The curve set by hand (an editor, the console), or back to the built-in
// one (Reset): NodePrefs' fields, whichever struct holds them.
template <typename P> inline void markManual(P& p) { p.batt_curve_src = MANUAL; }
template <typename P> inline void markBuiltin(P& p) {
  p.batt_curve_src = BUILTIN; p.batt_learn_runs = 0; p.batt_learn_date = 0;
}

// "Learned (2 runs)", "Set by hand", "Built in".
inline void sourceText(const uint16_t* curve, uint8_t src, uint8_t runs, char* o, int n) {
  switch (source(curve, src)) {
    case LEARNED: snprintf(o, n, runs > 1 ? "Learned (%u runs)" : "Learned (%u run)", runs); break;
    case MANUAL:  snprintf(o, n, "Set by hand"); break;
    default:      snprintf(o, n, "Built in"); break;
  }
}

enum State : uint8_t {
  WAITING,    // on USB, or a run broken: starts when unplugged
  SETTLING,   // the first half hour on battery
  RUNNING,    // a run under way
  TOO_LOW,    // started under MIN_START_MV: charge first
};

class Learner {
public:
  static const int N = 96;                         // slots: 384 bytes
  static const uint32_t FIRST_SLOT_MS = 120000UL;  // 2 min, doubling as they fill (96 x 32 min = 51 h)
  static const uint32_t SETTLE_MS = 30UL * 60000UL;
  static const uint32_t MIN_RUN_MS = 6UL * 3600000UL;
  static const int MIN_SLOTS = 16;
  static const int MIN_START_MV = 3900, FULL_MV = 4100;
  static const int RISE_MV = 150;                  // above the run's lowest: charging after all

  // Every few seconds: the smoothed voltage, on USB, the screen lit.
  void tick(uint32_t now, int mv, bool external, bool screen_on) {
    if (mv <= 0) return;
    if (external) { reset(); _armed = true; return; }
    if (_state == WAITING) {
      if (!_armed) return;
      _armed = false;
      _state = SETTLING;
      _t0 = now;
      return;
    }
    if (_state == TOO_LOW) return;
    if (_state == SETTLING) {
      if (now - _t0 < SETTLE_MS) return;
      _state = RUNNING;
      _ts = _slot_t = now;
      _slot_ms = FIRST_SLOT_MS;
      _n = 0;
      clearAcc();
    }
    while (now - _slot_t >= _slot_ms) {   // the slot done: into the log (a stall: the last one again)
      const uint32_t ms = _slot_ms;
      if (!closeSlot(true)) return;
      _slot_t += ms;
    }
    _sum_all += mv; _n_all++;
    if (!screen_on) { _sum_off += mv; _n_off++; }
  }

  State state() const { return _state; }
  // Time into the run (since the half hour), ms.
  uint32_t runMs(uint32_t now) const { return _state == RUNNING ? now - _ts : 0; }
  // The run's start voltage, once its first slot is in (0 before).
  int startMv() const { return _n ? _all[0] : 0; }

  // At the low-battery shutdown: the curve from this run, merged into `old`
  // (the curve in use: 11 points, 0..100 %) with weight for `runs` learned
  // before (0: replaces it). `low_mv`: the shutdown voltage. False when the
  // run doesn't count; `out` is then untouched.
  bool finish(uint32_t now, int low_mv, const uint16_t* old, uint8_t runs, uint16_t* out) {
    if (_state != RUNNING || low_mv <= 0) return false;
    if (_n_all) {   // the slot under way, with its own length
      _part_ms = now - _slot_t;
      if (!closeSlot(false)) return false;
    } else _part_ms = _slot_ms;
    const uint32_t len = now - _ts;
    if (len < MIN_RUN_MS || _n < MIN_SLOTS) return false;

    float v[N];
    series(v);
    const float t0 = slotMid(0), t_end = (float)len;
    if (t_end - t0 <= 0) return false;
    const int vs = (int)(v[0] + 0.5f);
    if (vs < MIN_START_MV) return false;
    int ps = vs >= FULL_MV ? 100 : battery::rawPercent(vs);
    if (ps > 100) ps = 100;
    if (ps < 20) return false;

    const int P = battery::CURVE_PTS;
    uint16_t c[P];
    c[0] = (uint16_t)low_mv;
    for (int k = 1; k < P; k++) {
      if (k * 10 > ps) { c[k] = old[k]; continue; }
      const float f = (float)(k * 10) / ps;   // the share of the run still to go
      c[k] = (uint16_t)(at(v, t_end - f * (t_end - t0)) + 0.5f);
    }
    float w = 1.0f;
    if (runs > 0) {
      w = 1.0f / (runs + 1);
      if (w < 0.25f) w = 0.25f;
      if (ps < 100) w *= ps / 100.0f;
    }
    uint16_t m[P];
    for (int k = 0; k < P; k++) m[k] = (uint16_t)(old[k] + w * ((int)c[k] - (int)old[k]) + 0.5f);
    m[0] = (uint16_t)low_mv;
    for (int k = 1; k < P; k++) if (m[k] <= m[k - 1]) m[k] = m[k - 1] + 5;   // strictly rising
    if (!battery::validCurve(m)) return false;
    memcpy(out, m, sizeof(m));
    _full = ps == 100;
    reset();
    return true;
  }
  bool lastFull() const { return _full; }

  // "14 h", "Waiting" ... for a value column (8 characters).
  void shortText(uint32_t now, char* o, int n) const {
    switch (_state) {
      case WAITING:  snprintf(o, n, "Waiting"); break;
      case SETTLING: snprintf(o, n, "Starting"); break;
      case TOO_LOW:  snprintf(o, n, "Charge"); break;
      default:       snprintf(o, n, "%lu h", (unsigned long)(runMs(now) / 3600000UL)); break;
    }
  }
  // A line saying what to do.
  void longText(uint32_t now, char* o, int n) const {
    switch (_state) {
      case WAITING:  snprintf(o, n, "Learning starts when unplugged"); break;
      case SETTLING: snprintf(o, n, "Learning: starting, keep it on battery"); break;
      case TOO_LOW:  snprintf(o, n, "To learn: charge it to 3.9 V or more, unplug"); break;
      default:
        snprintf(o, n, "Learning: %lu h, keep it on battery until it turns off",
                 (unsigned long)(runMs(now) / 3600000UL));
        break;
    }
  }

private:
  int16_t _all[N], _off[N];   // per slot: the mean voltage; screen off (0: none)
  int _n = 0;
  State _state = WAITING;
  bool _armed = true;         // a boot on battery starts a run too
  bool _full = false;
  uint32_t _t0 = 0, _ts = 0, _slot_t = 0, _slot_ms = FIRST_SLOT_MS, _part_ms = FIRST_SLOT_MS;
  int32_t _sum_all = 0, _sum_off = 0;
  uint32_t _n_all = 0, _n_off = 0;
  int _min_mv = 0;

  void clearAcc() { _sum_all = _sum_off = 0; _n_all = _n_off = 0; }
  void reset() { _state = WAITING; _n = 0; _min_mv = 0; clearAcc(); }

  // The slot under way into the log; full, pairs merged and the slot length
  // doubled (`compact`). A slot with no samples (a stall) repeats the last.
  // False: the run ended here (too low at the start, or the voltage rose:
  // charging).
  bool closeSlot(bool compact) {
    if (!_n_all && !_n) return true;
    const int a = _n_all ? (int)(_sum_all / _n_all) : _all[_n - 1];
    const int f = _n_all ? (_n_off ? (int)(_sum_off / _n_off) : 0) : _off[_n - 1];
    clearAcc();
    if (_n == 0 && a < MIN_START_MV) { _state = TOO_LOW; return false; }
    if (_min_mv && a > _min_mv + RISE_MV) { reset(); return false; }
    if (!_min_mv || a < _min_mv) _min_mv = a;
    _all[_n] = (int16_t)a;
    _off[_n] = (int16_t)f;
    _n++;
    if (compact && _n == N) {
      for (int i = 0; i < N / 2; i++) {
        _all[i] = (int16_t)((_all[2 * i] + _all[2 * i + 1]) / 2);
        const int x = _off[2 * i], y = _off[2 * i + 1];
        _off[i] = (int16_t)(x && y ? (x + y) / 2 : x ? x : y);
      }
      _n = N / 2;
      _slot_ms *= 2;
    }
    return true;
  }

  // Slot i's middle, ms from the run's start (the last one may be short).
  float slotMid(int i) const {
    if (i == _n - 1) return (float)i * _slot_ms + _part_ms / 2.0f;
    return (i + 0.5f) * _slot_ms;
  }

  // The voltages used: screen-off ones when at least half the slots have
  // them (the rest: their all-samples mean plus the run's average offset),
  // else all samples; then each the mean of it and its neighbours.
  void series(float* v) const {
    int both = 0;
    long d = 0;
    for (int i = 0; i < _n; i++) if (_off[i]) { both++; d += _off[i] - _all[i]; }
    const bool off = both * 2 >= _n;
    const float dm = both ? (float)d / both : 0;
    float r[N];
    for (int i = 0; i < _n; i++) r[i] = off ? (_off[i] ? _off[i] : _all[i] + dm) : _all[i];
    for (int i = 0; i < _n; i++) {
      float s = 0;
      int k = 0;
      for (int j = i - 1; j <= i + 1; j++) if (j >= 0 && j < _n) { s += r[j]; k++; }
      v[i] = s / k;
    }
  }

  // The series at time t (ms from the run's start), linear between slots.
  float at(const float* v, float t) const {
    if (t <= slotMid(0)) return v[0];
    for (int i = 1; i < _n; i++) {
      const float a = slotMid(i - 1), b = slotMid(i);
      if (t <= b) return v[i - 1] + (v[i] - v[i - 1]) * (t - a) / (b - a);
    }
    return v[_n - 1];
  }
};

}  // namespace battlearn
