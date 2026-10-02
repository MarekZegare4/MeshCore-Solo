#pragma once
// Battery level shared by the frontends' status bars (and ui-new's dashboard
// Batt% field), so both report the same number for the same voltage, the
// user's battery curve, and the Settings > Battery display choice
// (NodePrefs::batt_display_mode).

#include <stdint.h>

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3200
#endif

namespace battery {

enum Mode : uint8_t { ICON, PERCENT, VOLTAGE, MODE_COUNT };   // NodePrefs::batt_display_mode

// The cell voltage at 0, 10, ... 100 % can be set by the user (Settings >
// Battery curve, NodePrefs::batt_curve_mv); all zeros means the built-in
// LiPo curve below. Each frontend points useCurve() at the prefs array once.
static const int CURVE_PTS = 11;
static const int CURVE_MIN_MV = 2500, CURVE_MAX_MV = 4500;

// The user's curve, or nullptr. A function-local static in an inline
// function, so every translation unit shares the one pointer.
inline const uint16_t*& curvePtr() { static const uint16_t* p = nullptr; return p; }
inline void useCurve(const uint16_t* mv) { curvePtr() = mv; }

// Set (not all zeros), in range and rising point to point.
inline bool validCurve(const uint16_t* mv) {
  if (!mv || mv[0] == 0) return false;
  for (int i = 0; i < CURVE_PTS; i++) {
    if (mv[i] < CURVE_MIN_MV || mv[i] > CURVE_MAX_MV) return false;
    if (i > 0 && mv[i] <= mv[i - 1]) return false;
  }
  return true;
}

// Built-in LiPo discharge curve: voltage (mV) -> raw capacity (%).
struct CurvePt { uint16_t mv; uint8_t pct; };
static const CurvePt BUILTIN[] = {
  {3200,  0}, {3300,  3}, {3400,  8}, {3500, 15},
  {3600, 25}, {3650, 33}, {3700, 45}, {3750, 58},
  {3800, 68}, {3900, 77}, {4000, 86}, {4100, 93}, {4170, 100}
};
static const int BUILTIN_LEN = sizeof(BUILTIN) / sizeof(BUILTIN[0]);

// The built-in curve's voltage at point i (i * 10 %): where the editor starts.
inline int builtinMvAt(int i) {
  const int pct = i * 10;
  if (pct <= BUILTIN[0].pct) return BUILTIN[0].mv;
  for (int k = 1; k < BUILTIN_LEN; k++)
    if (pct <= BUILTIN[k].pct)
      return BUILTIN[k-1].mv + (pct - BUILTIN[k-1].pct) * (BUILTIN[k].mv - BUILTIN[k-1].mv) /
                               (BUILTIN[k].pct - BUILTIN[k-1].pct);
  return BUILTIN[BUILTIN_LEN-1].mv;
}

// Voltage at point i of the curve in use.
inline int curveMvAt(int i) {
  const uint16_t* c = curvePtr();
  return validCurve(c) ? c[i] : builtinMvAt(i);
}

// Voltage (mV) -> raw capacity (%) on the curve in use.
inline int rawPercent(int v) {
  const uint16_t* c = curvePtr();
  if (validCurve(c)) {
    if (v <= c[0]) return 0;
    if (v >= c[CURVE_PTS-1]) return 100;
    for (int i = 1; i < CURVE_PTS; i++)
      if (v <= c[i]) return (i - 1) * 10 + (v - c[i-1]) * 10 / (c[i] - c[i-1]);
    return 100;
  }
  if (v <= (int)BUILTIN[0].mv) return BUILTIN[0].pct;
  if (v >= (int)BUILTIN[BUILTIN_LEN-1].mv) return BUILTIN[BUILTIN_LEN-1].pct;
  for (int i = 1; i < BUILTIN_LEN; i++) {
    if (v <= (int)BUILTIN[i].mv) {
      int span_mv  = BUILTIN[i].mv  - BUILTIN[i-1].mv;
      int span_pct = BUILTIN[i].pct - BUILTIN[i-1].pct;
      return BUILTIN[i-1].pct + (v - (int)BUILTIN[i-1].mv) * span_pct / span_mv;
    }
  }
  return 100;
}

// The percentage shown: low_mv (typically NodePrefs.low_batt_mv, the
// user-configurable auto-shutdown threshold in Settings) is rescaled to 0%
// so the bar empties at the cutoff the user actually cares about.
inline int percent(int mv, int low_mv) {
  if (low_mv <= 0) low_mv = BATT_MIN_MILLIVOLTS;
  int raw_pct = rawPercent(mv);
  int low_pct = rawPercent(low_mv);
  int pct = (low_pct >= 100) ? 0 : (raw_pct - low_pct) * 100 / (100 - low_pct);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

inline Mode mode(uint8_t pref) { return pref < MODE_COUNT ? (Mode)pref : ICON; }

}  // namespace battery
