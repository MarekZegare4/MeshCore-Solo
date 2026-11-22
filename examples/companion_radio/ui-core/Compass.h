#pragma once
// The compass, on a board with a magnetometer and an accelerometer
// (HAS_COMPASS): which way the device faces, tilt compensated, from the
// magnetometer corrected by its calibration (MagCal.h). CourseEngine prefers
// it to the GPS course, so the compass, the nav arrow and the maps follow
// where the device points, standing still too.
//
// The board gives its readings with board_compass_read(), already in the
// device's frame: x towards the top of the screen, y to its right, z into the
// screen (down when it lies face up). The accelerometer reads up at rest.
//
// Calibration: start() collects the readings (4.8 KB, only meanwhile) while
// the device is turned every way; finish() fits them and, when the fit is good,
// keeps it in the prefs (NodePrefs::compass_cal). The declination
// (compass_decl_x10) turns magnetic north into true, as the maps are.

#include <math.h>
#include <stdint.h>
#include <string.h>
#include "MagCal.h"

#ifndef HAS_COMPASS
  #define HAS_COMPASS 0
#endif

#if HAS_COMPASS
// The board's readings, in the device's frame (see above). False: none now.
bool board_compass_read(magcal::Vec& acc, magcal::Vec& mag);
#endif

class Compass {
public:
  static const uint32_t PERIOD_MS = 100;
  static const uint32_t CAL_PERIOD_MS = 40;   // while calibrating: the screen's ball follows the hand
  static constexpr float MAX_RMS = 0.08f;   // a fit straying more from the sphere isn't kept

  template <typename P> void begin(P* prefs) {
    _cal_f = prefs->compass_cal;
    _decl_x10 = &prefs->compass_decl_x10;
    load();
  }

  // Every pass of the loop; reads the board every PERIOD_MS.
  void loop(uint32_t now) {
#if HAS_COMPASS
    if (now - _last_ms < (_col ? CAL_PERIOD_MS : PERIOD_MS)) return;
    _last_ms = now;
    magcal::Vec a, m;
    if (!board_compass_read(a, m)) return;
    _present = true;
    _acc = a;
    _raw = m;
    _read_ms = now;
    if (_col && now - _add_ms >= PERIOD_MS) {
      _add_ms = now;
      if (_col->add(m)) {
        const int k = _col->coverage();
        if (k > _cells) { _cells = k; _cell_ms = now; }
      }
      if (_auto && autoDone(now)) {
        float rms;
        _auto_result = finish(rms);
        _auto_rms = rms;
        if (_auto_result == SAVED && _saved) _saved();
        if (_col) cancel();   // a poor fit isn't left collecting
      }
    }
    if (!calibrated()) return;
    _mag = _cal.apply(m);
    const float h = magcal::heading(_mag, a, { 1, 0, 0 });
    if (h < 0) return;
    // A circular mean, so 359 and 1 average to 0, not 180; quick enough to
    // follow a turn, slow enough to steady the last degree or two.
    const float r = h * 0.0174533f;
    if (now - _head_ms > 1000) { _sx = cosf(r); _sy = sinf(r); }
    else { _sx += 0.3f * (cosf(r) - _sx); _sy += 0.3f * (sinf(r) - _sy); }
    _head_ms = now;
#else
    (void)now;
#endif
  }

  // A board with one that has answered.
  bool present() const { return _present; }
  bool calibrated() const { return _cal.field > 0; }

  // Which way the device faces, degrees from true north; false when there's
  // no heading (not calibrated, no reading lately).
  bool heading(int& deg) const {
    if (!calibrated() || !_head_ms || millis() - _head_ms > 1000) return false;
    float h = atan2f(_sy, _sx) * 57.2957795f + (_decl_x10 ? *_decl_x10 / 10.0f : 0);
    while (h < 0) h += 360;
    while (h >= 360) h -= 360;
    deg = (int)(h + 0.5f) % 360;
    return true;
  }

  // The latest readings: raw field, corrected field (uT), acceleration.
  magcal::Vec raw() const { return _raw; }
  magcal::Vec field() const { return _mag; }
  magcal::Vec accel() const { return _acc; }
  const magcal::Cal& cal() const { return _cal; }

  // ── Calibration ─────────────────────────────────────────────────────────
  bool calibrating() const { return _col != nullptr; }
  const magcal::Collector* collector() const { return _col; }
  // `automatic`: it ends by itself (fit, and kept when good) once the sphere
  // is covered, or the coverage has stopped growing for a while past the
  // minimum -- for a device turned off its USB cable, or a screen that just
  // shows how far it's got. `saved` (optional) then stores the prefs.
  bool start(bool automatic = false, void (*saved)() = nullptr) {
    if (!_col) _col = new magcal::Collector();
    if (!_col) return false;
    _col->reset();
    _cells = 0;
    _cell_ms = millis();
    _auto = automatic;
    _saved = saved;
    _auto_result = NONE;
    return true;
  }
  void cancel() { delete _col; _col = nullptr; _auto = false; }
  // The fit of what was collected; kept (and the collecting ended) when it's
  // good. `rms`: how far it strays from the sphere (0.02: 2 %), -1 when no fit.
  enum Result { NONE, NOT_ENOUGH, NO_FIT, POOR, SAVED };
  // How an automatic one ended (NONE: still going, or none run), and its fit.
  Result autoResult() const { return _auto_result; }
  float autoRms() const { return _auto_rms; }
  Result finish(float& rms) {
    rms = -1;
    if (!_col) return NOT_ENOUGH;
    if (!_col->ready()) return NOT_ENOUGH;
    magcal::Cal c;
    if (!_col->fit(c, &rms)) return NO_FIT;
    if (rms > MAX_RMS) return POOR;
    _cal = c;
    save();
    _head_ms = 0;
    cancel();
    return SAVED;
  }
  void forget() { _cal = magcal::Cal(); _cal.field = 0; save(); _head_ms = 0; }

private:
  float* _cal_f = nullptr;
  int16_t* _decl_x10 = nullptr;
  magcal::Cal _cal;
  magcal::Collector* _col = nullptr;
  magcal::Vec _raw = { 0, 0, 0 }, _mag = { 0, 0, 0 }, _acc = { 0, 0, 0 };
  uint32_t _last_ms = 0, _read_ms = 0, _head_ms = 0, _add_ms = 0;
  float _sx = 1, _sy = 0;
  bool _present = false;
  bool _auto = false;
  void (*_saved)() = nullptr;
  int _cells = 0;
  uint32_t _cell_ms = 0;
  Result _auto_result = NONE;
  float _auto_rms = -1;

  static const uint32_t STALL_MS = 15000;   // no new cell this long past the minimum: done
  bool autoDone(uint32_t now) const {
    if (!_col->ready()) return false;
    return _cells >= magcal::Collector::CELLS - 2 || _col->count() >= magcal::Collector::N ||
           now - _cell_ms >= STALL_MS;
  }

  void load() {
    if (!_cal_f) return;
    memcpy(&_cal.A[0][0], _cal_f, 9 * sizeof(float));
    memcpy(_cal.b, _cal_f + 9, 3 * sizeof(float));
    _cal.field = _cal_f[12];
    if (!(_cal.field > 0 && _cal.field < 1000)) _cal = magcal::Cal(), _cal.field = 0;   // unset or not a number
  }
  void save() {
    if (!_cal_f) return;
    memcpy(_cal_f, &_cal.A[0][0], 9 * sizeof(float));
    memcpy(_cal_f + 9, _cal.b, 3 * sizeof(float));
    _cal_f[12] = _cal.field;
  }
};
