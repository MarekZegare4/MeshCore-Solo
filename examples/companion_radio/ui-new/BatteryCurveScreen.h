#pragma once
#include "InfoKit.h"
#include "../ui-core/Battery.h"
// Settings › System › Battery curve: the cell voltage at 0, 10, ... 100 %
// (NodePrefs::batt_curve_mv, ui-core/Battery.h). Left/Right move the selected
// point 10 mV, kept between its neighbours; the first change copies the
// built-in curve in, Reset goes back to it. "Now" shows the cell as read, to
// set a point against. Saved on the way out.
// Included by UITask.cpp after AutoAdvertScreen.h.

class BatteryCurveScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  int  _sel = 1, _scroll = 0;
  bool _dirty = false;

  static const int PTS = battery::CURVE_PTS;
  static const int ROWS = 1 + PTS + 1;   // Now, the points, Reset
  static const int STEP_MV = 10;

  void nudge(int i, int d) {
    uint16_t* c = _prefs->batt_curve_mv;
    if (!battery::validCurve(c))
      for (int k = 0; k < PTS; k++) c[k] = (uint16_t)battery::builtinMvAt(k);
    const int lo = i > 0       ? c[i - 1] + STEP_MV : battery::CURVE_MIN_MV;
    const int hi = i < PTS - 1 ? c[i + 1] - STEP_MV : battery::CURVE_MAX_MV;
    c[i] = (uint16_t)constrain((int)c[i] + d * STEP_MV, lo, hi);
    _dirty = true;
  }

public:
  BatteryCurveScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _sel = 1; _scroll = 0; _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Battery curve");
    const int mv = _task->getBattMilliVolts();
    const bool custom = _prefs && battery::validCurve(_prefs->batt_curve_mv);
    drawList(display, ROWS, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      char l[12], v[20];
      if (i == 0) {
        strcpy(l, "Now");
        snprintf(v, sizeof(v), "%d mV %d%%", mv, battery::rawPercent(mv));
      } else if (i <= PTS) {
        snprintf(l, sizeof(l), "%d%%", (i - 1) * 10);
        snprintf(v, sizeof(v), "%d mV", battery::curveMvAt(i - 1));
      } else {
        strcpy(l, "Reset");
        strcpy(v, custom ? "" : "LiPo");   // the built-in curve is the one in use
      }
      info::valueRow(display, y, l, v, sel, reserve);
    });
    return 1000;   // "Now" follows the cell
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoSettingsScreen();
      return true;
    }
    if (c == KEY_UP)   { if (_sel > 0) _sel--; return true; }
    if (c == KEY_DOWN) { if (_sel < ROWS - 1) _sel++; return true; }
    if (!_prefs) return false;
    if (_sel >= 1 && _sel <= PTS && (keyIsNext(c) || keyIsPrev(c))) {
      nudge(_sel - 1, keyIsNext(c) ? 1 : -1);
      return true;
    }
    if (_sel == ROWS - 1 && c == KEY_ENTER && battery::validCurve(_prefs->batt_curve_mv)) {
      memset(_prefs->batt_curve_mv, 0, sizeof(_prefs->batt_curve_mv));
      _dirty = true;
      return true;
    }
    return false;
  }
};
