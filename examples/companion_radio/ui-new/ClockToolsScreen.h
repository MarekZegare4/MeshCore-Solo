#pragma once
// Clock tools — Alarm, Countdown timer (minutnik), Stopwatch (stoper) and
// Pomodoro. Entered with Enter on the home CLOCK page. A small top menu picks
// one of the four tools; Cancel backs out a level (tool → menu → home).
//
// This screen is pure UI. The time-critical machinery lives in UITask, which
// drives it every loop regardless of the current screen:
//   • Alarm   — UITask schedules an absolute fire instant from NodePrefs'
//     alarm_hour/min (robust to RTC re-syncs) and rings. Here we just edit the
//     persisted fields and call onAlarmChanged() to re-schedule.
//   • Timer   — UITask owns the running countdown (startTimer / stopTimer /
//     isTimerRunning / timerRemainingMs). It rings even when off-screen.
//   • Stopwatch — purely a display utility with no background action, so its
//     millis() state lives here; it keeps counting while you're elsewhere.
//   • Pomodoro — UITask owns the running Work/Short-Break/Long-Break cycle
//     (startPomodoro / stopPomodoro / pausePomodoro / resumePomodoro /
//     isPomodoroRunning / isPomodoroPaused / pomodoroPhase / pomodoroCycle /
//     pomodoroRemainingMs) and auto-advances phases with a short tone.
//     Pausing just freezes the deadline check; it doesn't touch phase/cycle.
//     Here we edit the persisted durations (NodePrefs pomodoro_*)
//     when idle, or just show the running phase + countdown.
//
// Numeric fields (alarm hour/minute, timer h/m/s) are edited with the shared
// framework DigitEditor (digit-by-digit: LEFT/RIGHT cursor, UP/DOWN +/- the
// place, min/max enforced) — the same widget Settings/Repeater use for Freq,
// rather than a hand-rolled wrap.
//
// E-INK: the running timer/stopwatch readouts would thrash a slow e-paper panel
// if redrawn every second, so on e-ink the live readout refreshes only coarsely
// (and immediately on any key); the millis() timing underneath is unaffected.
// Included by UITask.cpp after the other screen fragments.

#include "../NodePrefs.h"
#include "icons.h"        // drawList / drawRowSelection
#include "DigitEditor.h"  // shared digit-by-digit numeric editor

class ClockToolsScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  enum View : uint8_t { V_MENU, V_ALARM, V_TIMER, V_STOPWATCH, V_POMODORO };
  uint8_t _view = V_MENU;
  int     _sel = 0, _scroll = 0;       // shared list cursor
  bool    _alarm_dirty = false;        // unsaved edits to alarm_* (persist on exit)
  bool    _pomo_dirty = false;         // unsaved edits to pomodoro_* (persist on exit)

  // Countdown config (the duration to start; the running countdown lives in UITask).
  uint8_t _timer_h = 0, _timer_m = 5, _timer_s = 0;
  uint8_t _timer_cur = 0;     // digit cursor over HH:MM:SS, 0..5 (skips the colons)

  // Stopwatch (millis — display-only, no background action).
  bool     _sw_running = false;
  uint32_t _sw_start_ms = 0;
  uint32_t _sw_accum_ms = 0;

  // Shared numeric editor (alarm hour/minute) + which field it targets. The
  // timer uses its own continuous digit cursor (per-place caps a single
  // DigitEditor can't express), so it isn't listed here.
  enum EditKind : uint8_t { EK_NONE, EK_A_HOUR, EK_A_MIN,
                             EK_P_WORK, EK_P_SHORT, EK_P_LONG, EK_P_CYCLES };
  DigitEditor _editor;
  EditKind    _edit_kind = EK_NONE;

  // E-ink: coarse refresh for the live readouts (millis underneath is exact).
  static int liveTickMs(DisplayDriver& d, int oled_ms) { return d.isEink() ? 30000 : oled_ms; }

  static void fmtClock(char* b, int n, int hh, int mm) { snprintf(b, n, "%02d:%02d", hh, mm); }

  // millis → "H:MM:SS" (the countdown always shows hours).
  static void fmtHMS(char* b, int n, uint32_t ms) {
    uint32_t s = ms / 1000, h = s / 3600; s %= 3600;
    uint32_t m = s / 60; s %= 60;
    snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
  }

  // Stopwatch readout: "M:SS.t" while under an hour (tenths only on a fast
  // panel), "H:MM:SS" once it rolls past an hour.
  static void fmtStopwatch(char* b, int n, uint32_t ms, bool tenths) {
    if (ms >= 3600000UL) { fmtHMS(b, n, ms); return; }
    uint32_t total_s = ms / 1000, m = total_s / 60, s = total_s % 60;
    if (tenths) snprintf(b, n, "%lu:%02lu.%lu", (unsigned long)m, (unsigned long)s,
                         (unsigned long)((ms % 1000) / 100));
    else        snprintf(b, n, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
  }

  uint32_t stopwatchMs() const {
    return _sw_accum_ms + (_sw_running ? (millis() - _sw_start_ms) : 0);
  }

  // Step the digit under the timer cursor by dir (+1/−1), wrapping within that
  // place and keeping each field valid: minute/second tens 0-5, hours 0-23.
  void stepTimerDigit(int dir) {
    uint8_t* f; int tens_max; bool is_hours = false;
    if (_timer_cur < 2)      { f = &_timer_h; tens_max = 2; is_hours = true; }
    else if (_timer_cur < 4) { f = &_timer_m; tens_max = 5; }
    else                     { f = &_timer_s; tens_max = 5; }
    int t = *f / 10, u = *f % 10;
    if (_timer_cur % 2 == 0) {                       // tens place
      t = (t + dir + (tens_max + 1)) % (tens_max + 1);
      if (is_hours && t == 2 && u > 3) u = 3;        // 2X hours can't exceed 23
    } else {                                         // units place
      int umax = (is_hours && t == 2) ? 3 : 9;
      u = (u + dir + (umax + 1)) % (umax + 1);
    }
    *f = (uint8_t)(t * 10 + u);
  }

  // Open the DigitEditor on a numeric field (2 integer digits, no decimals).
  // vmin defaults to 0 (Alarm hour/minute); Pomodoro durations/cycles pass 1 —
  // a phase edited down to 0 would re-fire every tick of tickClockTools().
  void editField(EditKind kind, uint8_t value, uint8_t vmax, uint8_t vmin = 0) {
    _edit_kind = kind;
    _editor.begin((float)value, (float)vmin, (float)vmax, 2, 0);
  }

  // Feed a key to the open editor, write the (live) value back to its field, and
  // close the editor when DigitEditor reports DONE/CANCELLED.
  bool feedEditor(char c) {
    DigitEditor::Result r = _editor.handleInput(c);
    uint8_t v = (uint8_t)(_editor.value + 0.5f);
    switch (_edit_kind) {
      case EK_A_HOUR:   _prefs->alarm_hour = v; _alarm_dirty = true; _task->onAlarmChanged(); break;
      case EK_A_MIN:    _prefs->alarm_min  = v; _alarm_dirty = true; _task->onAlarmChanged(); break;
      case EK_P_WORK:   _prefs->pomodoro_work_min        = v; _pomo_dirty = true; break;
      case EK_P_SHORT:  _prefs->pomodoro_short_break_min = v; _pomo_dirty = true; break;
      case EK_P_LONG:   _prefs->pomodoro_long_break_min  = v; _pomo_dirty = true; break;
      case EK_P_CYCLES: _prefs->pomodoro_cycles           = v; _pomo_dirty = true; break;
      default: break;
    }
    if (r != DigitEditor::NONE) _edit_kind = EK_NONE;   // editor closed
    return true;
  }

  // Draw a row's value column: the live editor when this row is the one being
  // edited, otherwise the static text.
  void drawValue(DisplayDriver& d, int y, int valx, int reserve, bool sel,
                 EditKind mykind, const char* text) {
    if (sel && _editor.active && _edit_kind == mykind) _editor.render(d, valx, y);
    else d.drawTextEllipsized(valx, y, d.width() - valx - reserve, text);
  }

  // ── Sub-renders ───────────────────────────────────────────────────────────
  int renderMenu(DisplayDriver& d) {
    d.drawCenteredHeader("CLOCK TOOLS");
    static const char* items[4] = { "Alarm", "Timer", "Stopwatch", "Pomodoro" };
    if (_sel > 3) _sel = 3;
    drawList(d, 4, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(d, y, sel, reserve);
      d.setCursor(4, y);
      d.print(items[i]);
      if (i == 0 && _prefs && _prefs->alarm_on) {   // show the armed alarm time
        char b[8]; fmtClock(b, sizeof(b), _prefs->alarm_hour, _prefs->alarm_min);
        int vx = d.width() - d.getTextWidth(b) - reserve - 2;
        d.setCursor(vx, y); d.print(b);
      } else if (i == 3 && _task->isPomodoroRunning()) {   // show the active phase
        static const char* PHASE_TAG[3] = { "WORK", "BRK", "LONG" };
        uint8_t phase = _task->pomodoroPhase();
        const char* tag = PHASE_TAG[phase < 3 ? phase : 0];
        int vx = d.width() - d.getTextWidth(tag) - reserve - 2;
        d.setCursor(vx, y); d.print(tag);
      }
    });
    return 60000;
  }

  int renderAlarm(DisplayDriver& d) {
    d.drawCenteredHeader("ALARM");
    if (!_prefs) return 60000;
    const char* rows[3] = { "Hour", "Minute", "Armed" };
    if (_sel > 2) _sel = 2;
    const int valx = d.width() / 2 + 6;
    drawList(d, 3, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(d, y, sel, reserve);
      d.setCursor(4, y); d.print(rows[i]);
      char b[6];
      if (i == 0)      snprintf(b, sizeof(b), "%02u", _prefs->alarm_hour);
      else if (i == 1) snprintf(b, sizeof(b), "%02u", _prefs->alarm_min);
      else             snprintf(b, sizeof(b), "%s", _prefs->alarm_on ? "ON" : "OFF");
      EditKind k = (i == 0) ? EK_A_HOUR : (i == 1) ? EK_A_MIN : EK_NONE;
      drawValue(d, y, valx, reserve, sel, k, b);
    });
    return _editor.active ? 50 : 60000;
  }

  int renderTimer(DisplayDriver& d) {
    d.drawCenteredHeader("TIMER");
    const int cx = d.width() / 2;
    if (_task->isTimerRunning()) {
      char buf[16];
      fmtHMS(buf, sizeof(buf), _task->timerRemainingMs());
      d.setTextSize(2);
      d.drawTextCentered(cx, d.listStart() + 4, buf);
      d.setTextSize(1);
      d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1, "Ent=stop");
      return liveTickMs(d, 1000);
    }
    // Setting mode: big HH:MM:SS with the digit under the cursor underlined.
    // LEFT/RIGHT move the cursor one digit, UP/DOWN change it, Enter starts.
    char buf[12];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", _timer_h, _timer_m, _timer_s);
    d.setTextSize(2);
    const int ty = d.listStart() + 2;
    d.drawTextCentered(cx, ty, buf);
    const int cw  = d.getCharWidth();      // size-2 cell width
    const int lh2 = d.getLineHeight();
    const int sx  = cx - d.getTextWidth(buf) / 2;
    const int char_idx = _timer_cur + (_timer_cur / 2);   // skip the two colons
    d.fillRect(sx + char_idx * cw, ty + lh2, cw - 1, 2);
    d.setTextSize(1);
    d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1, "Up/Dn=set Ent=start");
    return 60000;
  }

  int renderStopwatch(DisplayDriver& d) {
    d.drawCenteredHeader("STOPWATCH");
    const int cx = d.width() / 2;
    char buf[16];
    bool tenths = !d.isEink() && _sw_running;   // tenths only on a fast panel
    fmtStopwatch(buf, sizeof(buf), stopwatchMs(), tenths);
    d.setTextSize(2);
    d.drawTextCentered(cx, d.listStart() + 4, buf);
    d.setTextSize(1);
    const char* hint = _sw_running ? "Ent=stop" : "Ent=start Dn=reset";
    d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1, hint);
    return _sw_running ? liveTickMs(d, 100) : 60000;
  }

  int renderPomodoro(DisplayDriver& d) {
    d.drawCenteredHeader("POMODORO");
    const int cx = d.width() / 2;
    if (_task->isPomodoroRunning()) {
      static const char* PHASE_LABEL[3] = { "WORK", "SHORT BREAK", "LONG BREAK" };
      bool paused = _task->isPomodoroPaused();
      uint8_t phase = _task->pomodoroPhase();
      const int ly = d.listStart();
      d.drawTextCentered(cx, ly, PHASE_LABEL[phase < 3 ? phase : 0]);
      char buf[16];
      fmtHMS(buf, sizeof(buf), _task->pomodoroRemainingMs());
      d.setTextSize(2);
      d.drawTextCentered(cx, ly + d.getLineHeight() + 2, buf);
      d.setTextSize(1);
      // Status line (cycle count + PAUSED, when paused) sits just above the
      // key-hint line, which itself flips between Pause/Resume.
      char status[16];
      if (_prefs) snprintf(status, sizeof(status), paused ? "%u/%u PAUSED" : "%u/%u",
                            _task->pomodoroCycle(), _prefs->pomodoro_cycles);
      else        snprintf(status, sizeof(status), "%s", paused ? "PAUSED" : "");
      if (status[0]) d.drawTextCentered(cx, d.height() - 2 * d.getLineHeight() - 3, status);
      d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1,
                          paused ? "Ent=Resume Dn=Stop" : "Ent=Pause Dn=Stop");
      return liveTickMs(d, paused ? 60000 : 1000);
    }
    // Idle: durations list (Work/Short Break/Long Break/Cycles) + Start.
    if (!_prefs) return 60000;
    const char* rows[5] = { "Start", "Work", "Short Break", "Long Break", "Cycles" };
    if (_sel > 4) _sel = 4;
    const int valx = d.width() / 2 + 6;
    drawList(d, 5, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(d, y, sel, reserve);
      d.setCursor(4, y); d.print(rows[i]);
      if (i == 0) return;   // Start has no value column
      char b[8];
      EditKind k;
      switch (i) {
        case 1: snprintf(b, sizeof(b), "%um", _prefs->pomodoro_work_min);        k = EK_P_WORK;   break;
        case 2: snprintf(b, sizeof(b), "%um", _prefs->pomodoro_short_break_min); k = EK_P_SHORT;  break;
        case 3: snprintf(b, sizeof(b), "%um", _prefs->pomodoro_long_break_min);  k = EK_P_LONG;   break;
        default: snprintf(b, sizeof(b), "%u", _prefs->pomodoro_cycles);          k = EK_P_CYCLES; break;
      }
      drawValue(d, y, valx, reserve, sel, k, b);
    });
    return _editor.active ? 50 : 60000;
  }

  // ── Input per view ────────────────────────────────────────────────────────
  bool inputMenu(char c) {
    if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 3; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < 3) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      _view = (_sel == 0) ? V_ALARM : (_sel == 1) ? V_TIMER : (_sel == 2) ? V_STOPWATCH : V_POMODORO;
      _sel = 0; _scroll = 0;
      return true;
    }
    return true;
  }

  bool inputAlarm(char c) {
    if (!_prefs) { if (c == KEY_CANCEL) { _view = V_MENU; _sel = 0; } return true; }
    if (_editor.active) return feedEditor(c);
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_alarm_dirty);  // persist alarm fields only if edited
      _task->onAlarmChanged();                 // re-schedule from the new time
      _view = V_MENU; _sel = 0; _scroll = 0;
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 2; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < 2) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      if (_sel == 0)      editField(EK_A_HOUR, _prefs->alarm_hour, 23);
      else if (_sel == 1) editField(EK_A_MIN,  _prefs->alarm_min,  59);
      else { _prefs->alarm_on ^= 1; _alarm_dirty = true; _task->onAlarmChanged(); }
      return true;
    }
    return true;
  }

  bool inputTimer(char c) {
    if (_task->isTimerRunning()) {
      if (c == KEY_ENTER)  { _task->stopTimer(); return true; }       // stop/cancel countdown
      if (c == KEY_CANCEL) { _view = V_MENU; _sel = 0; return true; } // keeps counting in background
      return true;   // any other key just forces a refresh (e-ink)
    }
    if (c == KEY_CANCEL) { _view = V_MENU; _sel = 0; return true; }
    if (keyIsPrev(c)) { _timer_cur = (_timer_cur + 5) % 6; return true; }  // cursor ←
    if (keyIsNext(c)) { _timer_cur = (_timer_cur + 1) % 6; return true; }  // cursor →
    if (c == KEY_UP)   { stepTimerDigit(+1); return true; }
    if (c == KEY_DOWN) { stepTimerDigit(-1); return true; }
    if (c == KEY_ENTER) {
      uint32_t dur = (((uint32_t)_timer_h * 60 + _timer_m) * 60 + _timer_s) * 1000UL;
      if (dur == 0) { _task->showAlert("Set a duration", 1000); return true; }
      _task->startTimer(dur);
    }
    return true;
  }

  bool inputStopwatch(char c) {
    if (c == KEY_CANCEL) { _view = V_MENU; _sel = 0; return true; }  // keeps running in background
    if (c == KEY_ENTER) {
      if (_sw_running) { _sw_accum_ms += millis() - _sw_start_ms; _sw_running = false; }
      else             { _sw_start_ms = millis(); _sw_running = true; }
      return true;
    }
    if ((c == KEY_UP || c == KEY_DOWN) && !_sw_running) { _sw_accum_ms = 0; return true; }
    return true;   // other keys just refresh the readout
  }

  bool inputPomodoro(char c) {
    if (_task->isPomodoroRunning()) {
      if (c == KEY_ENTER) {   // toggle pause/resume; phase/cycle/deadline untouched
        if (_task->isPomodoroPaused()) _task->resumePomodoro();
        else                           _task->pausePomodoro();
        return true;
      }
      if (c == KEY_DOWN)    { _task->stopPomodoro(); return true; }     // cancel the whole cycle
      if (c == KEY_CANCEL)  { _view = V_MENU; _sel = 0; return true; }  // keeps running/paused in background
      return true;   // any other key just forces a refresh (e-ink)
    }
    if (!_prefs) { if (c == KEY_CANCEL) { _view = V_MENU; _sel = 0; } return true; }
    if (_editor.active) return feedEditor(c);
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_pomo_dirty);
      _view = V_MENU; _sel = 0; _scroll = 0;
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 4; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < 4) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      switch (_sel) {
        case 0: _task->startPomodoro(); break;
        case 1: editField(EK_P_WORK,   _prefs->pomodoro_work_min,        99, 1); break;
        case 2: editField(EK_P_SHORT,  _prefs->pomodoro_short_break_min, 99, 1); break;
        case 3: editField(EK_P_LONG,   _prefs->pomodoro_long_break_min,  99, 1); break;
        case 4: editField(EK_P_CYCLES, _prefs->pomodoro_cycles,           9, 1); break;
      }
      return true;
    }
    return true;
  }

public:
  ClockToolsScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override {
    _view = V_MENU; _sel = 0; _scroll = 0;
    _timer_cur = 0;
    _editor.active = false; _edit_kind = EK_NONE;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    switch (_view) {
      case V_ALARM:     return renderAlarm(display);
      case V_TIMER:     return renderTimer(display);
      case V_STOPWATCH: return renderStopwatch(display);
      case V_POMODORO:  return renderPomodoro(display);
      default:          return renderMenu(display);
    }
  }

  bool handleInput(char c) override {
    switch (_view) {
      case V_ALARM:     return inputAlarm(c);
      case V_TIMER:     return inputTimer(c);
      case V_STOPWATCH: return inputStopwatch(c);
      case V_POMODORO:  return inputPomodoro(c);
      default:          return inputMenu(c);
    }
  }
};
