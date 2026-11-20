#pragma once
#include "InfoKit.h"
#include "../ui-core/Battery.h"
#include "../ui-core/BattLearn.h"
// Settings › System › Battery curve: the cell voltage at 0, 10, ... 100 %
// (NodePrefs::batt_curve_mv, ui-core/Battery.h). Left/Right move the selected
// point 10 mV, kept between its neighbours; the first change copies the
// built-in curve in (and makes it one set by hand: learning leaves it), Reset
// goes back to it. "Now" shows the cell as read, to set a point against;
// "Curve" where the one in use came from; "Learn" how a run to learn it from
// is going (ui-core/BattLearn.h; Enter switches it). Saved on the way out.
// Included by UITask.cpp after AutoAdvertScreen.h.

class BatteryCurveScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  int  _sel = 1, _scroll = 0;
  bool _dirty = false;

  static const int PTS = battery::CURVE_PTS;
  static const int TOP = 1 + 2 * BATT_LEARN;   // Now (Curve, Learn): the rows above the points
  static const int ROWS = TOP + PTS + 1;       // ..., the points, Reset
  static const int STEP_MV = 10;

  void nudge(int i, int d) {
    uint16_t* c = _prefs->batt_curve_mv;
    if (!battery::validCurve(c))
      for (int k = 0; k < PTS; k++) c[k] = (uint16_t)battery::builtinMvAt(k);
    const int lo = i > 0       ? c[i - 1] + STEP_MV : battery::CURVE_MIN_MV;
    const int hi = i < PTS - 1 ? c[i + 1] - STEP_MV : battery::CURVE_MAX_MV;
    c[i] = (uint16_t)constrain((int)c[i] + d * STEP_MV, lo, hi);
    battlearn::markManual(*_prefs);
    _dirty = true;
  }

public:
  BatteryCurveScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _sel = TOP; _scroll = 0; _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    drawScreenHeader(display, "Battery curve");
    const int mv = _task->getBattMilliVolts();
    const bool custom = _prefs && battery::validCurve(_prefs->batt_curve_mv);
    drawList(display, ROWS, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      char l[12], v[20];
      if (i == 0) {
        strcpy(l, "Now");
        snprintf(v, sizeof(v), "%d mV %d%%", mv, battery::rawPercent(mv));
#if BATT_LEARN
      } else if (i == 1) {
        static const char* SRC[] = { "Built in", "Manual", "Learned" };
        strcpy(l, "Curve");
        const auto src = _prefs ? battlearn::source(_prefs->batt_curve_mv, _prefs->batt_curve_src) : battlearn::BUILTIN;
        if (src == battlearn::LEARNED) snprintf(v, sizeof(v), "Learned %ux", _prefs->batt_learn_runs);
        else strcpy(v, SRC[src]);
      } else if (i == 2) {
        strcpy(l, "Learn");
        if (!_prefs || !_prefs->batt_learn) strcpy(v, "Off");
        else if (!_task->core().battLearning()) strcpy(v, "Manual");   // a curve set by hand: Reset first
        else _task->core().batt_learn.shortText(millis(), v, sizeof(v));
#endif
      } else if (i < TOP + PTS) {
        snprintf(l, sizeof(l), "%d%%", (i - TOP) * 10);
        snprintf(v, sizeof(v), "%d mV", battery::curveMvAt(i - TOP));
      } else {
        strcpy(l, "Reset");
        strcpy(v, custom ? "" : "LiPo");   // the built-in curve is the one in use
      }
      info::valueRow(display, y, l, v, sel, reserve);
    });
    return 1000;   // "Now" follows the cell
  }

  // One button: a curve point is a number to edit.
  bool valueRow() override { return _prefs && _sel >= TOP && _sel < TOP + PTS; }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoSettingsScreen();
      return true;
    }
    if (c == KEY_UP)   { if (_sel > 0) _sel--; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < ROWS - 1) ? _sel + 1 : 0; return true; }   // wraps, as the lists do
    if (!_prefs) return false;
    if (_sel >= TOP && _sel < TOP + PTS && (keyIsNext(c) || keyIsPrev(c))) {
      nudge(_sel - TOP, keyIsNext(c) ? 1 : -1);
      return true;
    }
#if BATT_LEARN
    if (_sel == 2 && (c == KEY_ENTER || keyIsNext(c) || keyIsPrev(c))) {
      _prefs->batt_learn = !_prefs->batt_learn;
      _dirty = true;
      return true;
    }
#endif
    if (_sel == ROWS - 1 && c == KEY_ENTER && battery::validCurve(_prefs->batt_curve_mv)) {
      memset(_prefs->batt_curve_mv, 0, sizeof(_prefs->batt_curve_mv));
      battlearn::markBuiltin(*_prefs);
      _dirty = true;
      return true;
    }
    return false;
  }
};
