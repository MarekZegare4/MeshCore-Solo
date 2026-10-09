#pragma once
// Clock tools — Alarm, Countdown timer (minutnik), Stopwatch (stoper) and
// Calendar. Entered with Enter on the home CLOCK page. A small top menu picks
// one of the tools; Cancel backs out a level (tool → menu → home).
//
// This screen is pure UI. The time-critical machinery lives in the UI Core's
// ClockEngine (ui-core/ClockEngine.h), which UITask drives every loop
// regardless of the current screen:
//   • Alarm   — the engine schedules an absolute fire instant from NodePrefs'
//     alarm_hour/min (robust to RTC re-syncs) and rings. Here we just edit the
//     persisted fields and call onAlarmChanged() to re-schedule.
//   • Timer   — the engine owns the running countdown (startTimer / stopTimer /
//     isTimerRunning / timerRemainingMs). It rings even when off-screen.
//   • Stopwatch — purely a display utility with no background action, so its
//     millis() state lives here; it keeps counting while you're elsewhere.
//
// Alarm hour/minute and the Timer's HH:MM:SS are both edited with the same
// hand-rolled digit cursor (LEFT/RIGHT moves between digits, UP/DOWN steps
// the digit under it, wrapping within its place and keeping the whole field
// a valid time) rather than the shared DigitEditor framework Settings/
// Repeater use for Freq — DigitEditor edits one flat decimal value, and a
// combined HH:MM/HH:MM:SS field needs each place capped independently (hour
// tens <= 2, minute/second tens <= 5), which a single flat step size can't
// express without landing on invalid times at the boundaries.
//
// E-INK: the running timer/stopwatch readouts would thrash a slow e-paper panel
// if redrawn every second, so on e-ink the live readout refreshes only coarsely
// (and immediately on any key); the millis() timing underneath is unaffected.
// Included by UITask.cpp after the other screen fragments.

#include "InfoKit.h"
#include "../NodePrefs.h"
#include "icons.h"        // drawList / drawRowSelection

class ClockToolsScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  enum View : uint8_t { V_MENU, V_ALARM, V_TIMER, V_STOPWATCH, V_CALENDAR };
  static const int MENU_N = 4;
  uint8_t _view = V_MENU;
  int     _sel = 0, _scroll = 0;       // shared list cursor
  bool    _alarm_dirty = false;        // unsaved edits to alarm_* (persist on exit)
  int     _cal_month = 0;              // Calendar: months from the current one

  // Countdown config (the duration to start; the running countdown lives in UITask).
  uint8_t _timer_h = 0, _timer_m = 5, _timer_s = 0;
  uint8_t _timer_cur = 0;     // digit cursor over HH:MM:SS, 0..5 (skips the colons)

  // Stopwatch (millis — display-only, no background action).
  bool     _sw_running = false;
  uint32_t _sw_start_ms = 0;
  uint32_t _sw_accum_ms = 0;

  // Alarm time's own digit cursor (same shape as the timer's, just HH:MM —
  // no seconds field).
  bool    _alarm_editing = false;
  uint8_t _alarm_cur = 0;     // digit cursor over HH:MM, 0..3 (skips the colon)

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

  // Step the digit under the alarm-time cursor by dir (+1/-1) — same shape as
  // stepTimerDigit, just two fields (hour/minute) instead of three.
  void stepAlarmDigit(int dir) {
    uint8_t* f; int tens_max; bool is_hours;
    if (_alarm_cur < 2) { f = &_prefs->alarm_hour; tens_max = 2; is_hours = true; }
    else                { f = &_prefs->alarm_min;  tens_max = 5; is_hours = false; }
    int t = *f / 10, u = *f % 10;
    if (_alarm_cur % 2 == 0) {                       // tens place
      t = (t + dir + (tens_max + 1)) % (tens_max + 1);
      if (is_hours && t == 2 && u > 3) u = 3;        // 2X hours can't exceed 23
    } else {                                         // units place
      int umax = (is_hours && t == 2) ? 3 : 9;
      u = (u + dir + (umax + 1)) % (umax + 1);
    }
    *f = (uint8_t)(t * 10 + u);
    _alarm_dirty = true;
    _task->onAlarmChanged();
  }

  // Draw the alarm's "HH:MM" value: while this row is in its digit-cursor
  // sub-mode, the digit under the cursor is shown inverted (same convention as
  // DigitEditor::render — assumes the caller is already inside a selected row,
  // ink DARK on a LIGHT selection bar); otherwise just the plain text.
  void drawAlarmTime(DisplayDriver& d, int y, int valx, bool editing) {
    char buf[6];
    fmtClock(buf, sizeof(buf), _prefs->alarm_hour, _prefs->alarm_min);
    if (!editing) { d.setCursor(valx, y); d.print(buf); return; }
    const int cw = d.getCharWidth();
    const int char_idx = _alarm_cur + (_alarm_cur / 2);   // skip the colon
    for (int k = 0; buf[k]; k++) {
      int cx = valx + k * cw;
      if (k == char_idx) {
        d.setColor(DisplayDriver::DARK);
        d.fillSoftRect(cx, y - 1, cw, d.getLineHeight() + 1);
        d.setColor(DisplayDriver::LIGHT);
      } else {
        d.setColor(DisplayDriver::DARK);
      }
      char one[2] = { buf[k], '\0' };
      d.setCursor(cx, y);
      d.print(one);
    }
    d.setColor(DisplayDriver::LIGHT);
  }

  // ── Sub-renders ───────────────────────────────────────────────────────────
  int renderMenu(DisplayDriver& d) {
    drawScreenHeader(d, "Clock tools");
    static const char* items[MENU_N] = { "Alarm", "Timer", "Stopwatch", "Calendar" };
    if (_sel > MENU_N - 1) _sel = MENU_N - 1;
    drawList(d, MENU_N, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(d, y, sel, reserve);
      d.setCursor(4, y);
      d.print(items[i]);
      if (i == 0 && _prefs && _prefs->alarm_on) {   // show the armed alarm time
        char b[8]; fmtClock(b, sizeof(b), _prefs->alarm_hour, _prefs->alarm_min);
        int vx = d.width() - d.getTextWidth(b) - reserve - 2;
        d.setCursor(vx, y); d.print(b);
      }
    });
    return 60000;
  }

  int renderAlarm(DisplayDriver& d) {
    drawScreenHeader(d, "Alarm");
    if (!_prefs) return 60000;
    const char* rows[3] = { "Time", "Repeat", "Armed" };
    if (_sel > 2) _sel = 2;
    int mq_delay = 0;
    drawList(d, 3, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(d, y, sel, reserve);
      if (i == 0) {   // the time, flush right like every value (digits edited in place)
        d.setCursor(2, y); d.print(rows[i]);
        drawAlarmTime(d, y, d.width() - reserve - 2 - d.getTextWidth("00:00"), sel && _alarm_editing);
      } else if (i == 1) {
        int mqr = info::valueRow(d, y, rows[i],
                    NodePrefs::alarmRepeatLabel(NodePrefs::alarmRepeatIdxForMask(_prefs->alarm_repeat_mask)), sel, reserve);
        if (mqr > 0) mq_delay = mqr;
      } else {
        info::switchRow(d, y, rows[i], _prefs->alarm_on, sel, reserve);
      }
    });
    if (_alarm_editing) return 50;
    return (mq_delay > 0 && mq_delay < 60000) ? mq_delay : 60000;
  }

  // On a screen 40 characters wide (the 4.2"): `t` in the big clock's
  // lettering, as large as fits between the header and the hint line, with
  // character `cur` underlined (-1: none). False elsewhere: draw it in text.
  bool bigDigits(DisplayDriver& d, const char* t, int cur) {
    if (d.width() < 40 * d.getCharWidth()) return false;
    const int lh = d.getLineHeight(), top = d.listStart(), bottom = d.height() - 2 * lh;
    int sc = 6;   // the Home clock's size at most
    while (sc > 1 && (info::letteringW(t, sc, sc + 1) > d.width() - 2 * lh
                      || lettering::LOGO_H * sc > bottom - top - lh)) sc--;
    const int gap = sc + 1, h = lettering::LOGO_H * sc;
    const int x0 = (d.width() - info::letteringW(t, sc, gap)) / 2, y = top + (bottom - top - h) / 2;
    info::lettering(d, x0, y, t, sc, gap);
    if (cur >= 0) {
      int x = x0;
      for (int i = 0; i < cur && t[i]; i++) x += lettering::charW(t[i]) * sc + gap;
      d.fillRect(x, y + h + sc, lettering::charW(t[cur]) * sc, sc);
    }
    return true;
  }

  int renderTimer(DisplayDriver& d) {
    drawScreenHeader(d, "Timer");
    const int cx = d.width() / 2;
    if (_task->isTimerRunning()) {
      char buf[16];
      fmtHMS(buf, sizeof(buf), _task->timerRemainingMs());
      if (!bigDigits(d, buf, -1)) {
        d.setTextSize(2);
        d.drawTextCentered(cx, d.listStart() + 4, buf);
        d.setTextSize(1);
      }
      d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1, "Ent=stop");
      return liveTickMs(d, 1000);
    }
    // Setting mode: big HH:MM:SS with the digit under the cursor underlined.
    // LEFT/RIGHT move the cursor one digit, UP/DOWN change it, Enter starts.
    char buf[12];
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u", _timer_h, _timer_m, _timer_s);
    if (bigDigits(d, buf, _timer_cur + (_timer_cur / 2))) {
      d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1, "Up/Dn=set Ent=start");
      return 60000;
    }
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
    drawScreenHeader(d, "Stopwatch");
    const int cx = d.width() / 2;
    char buf[16];
    bool tenths = !d.isEink() && _sw_running;   // tenths only on a fast panel
    fmtStopwatch(buf, sizeof(buf), stopwatchMs(), tenths);
    if (!bigDigits(d, buf, -1)) {
      d.setTextSize(2);
      d.drawTextCentered(cx, d.listStart() + 4, buf);
      d.setTextSize(1);
    }
    const char* hint = _sw_running ? "Ent=stop" : "Ent=start Dn=reset";
    d.drawTextCentered(cx, d.height() - d.getLineHeight() - 1, hint);
    return _sw_running ? liveTickMs(d, 100) : 60000;
  }

  // A month `_cal_month` months from now: the alarm's days and today only on
  // the current one. Where the grid and a title line don't fit (a 64-pixel
  // screen), the grid goes tight and the month and year stand beside it.
  int renderCalendar(DisplayDriver& d) {
    struct tm t;
    if (!localTm(rtc_clock.getCurrentTime(), _prefs ? _prefs->tz_offset_hours : 0, t) || t.tm_year < 120) {
      drawScreenHeader(d, "Calendar");
      d.drawTextCentered(d.width() / 2, d.height() / 2 - d.getLineHeight() / 2, "Clock not set");
      return 60000;
    }
    const bool now = _cal_month == 0;
    uint32_t marked = now && _prefs ? calmath::alarmDays(t, _prefs->alarm_on, _prefs->alarm_repeat_mask,
                                                         _prefs->alarm_hour, _prefs->alarm_min) : 0;
    // The 1st of the month shown, its weekday carried over from today's.
    int wday1 = ((t.tm_wday - (t.tm_mday - 1)) % 7 + 7) % 7;
    for (int k = 0; k < _cal_month; k++) {
      wday1 = (wday1 + calmath::daysIn(t.tm_year, t.tm_mon)) % 7;
      if (++t.tm_mon == 12) { t.tm_mon = 0; t.tm_year++; }
    }
    for (int k = 0; k > _cal_month; k--) {
      if (--t.tm_mon < 0) { t.tm_mon = 11; t.tm_year--; }
      wday1 = ((wday1 - calmath::daysIn(t.tm_year, t.tm_mon)) % 7 + 7) % 7;
    }
    if (!now) { t.tm_mday = 1; t.tm_wday = wday1; }
    static const char* const MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    char title[16];
    snprintf(title, sizeof(title), "%s %d", MON[t.tm_mon], t.tm_year + 1900);
    const int W = d.width(), lh = d.getLineHeight(), cw = d.getCharWidth();
    const int top = d.listStart();
    if (calendar::height(d, t) <= d.height() - top) {
      drawScreenHeader(d, title);
      const int gw = W < 28 * cw ? W : 28 * cw;   // as on the Clock page
      calendar::draw(d, (W - gw) / 2, top, gw, t, marked, false, now);
    } else {
      const int side = 4 * cw + 2;   // "2026"
      calendar::draw(d, 0, 0, W - side, t, marked, true, now);
      char yr[6];
      snprintf(yr, sizeof(yr), "%d", t.tm_year + 1900);
      d.drawTextCentered(W - side / 2, d.height() / 2 - lh, MON[t.tm_mon]);
      d.drawTextCentered(W - side / 2, d.height() / 2, yr);
    }
    return 60000;
  }

  // ── Input per view ────────────────────────────────────────────────────────
  bool inputMenu(char c) {
    if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : MENU_N - 1; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < MENU_N - 1) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      static const View V[MENU_N] = { V_ALARM, V_TIMER, V_STOPWATCH, V_CALENDAR };
      _view = V[_sel];
      _sel = 0; _scroll = 0; _cal_month = 0;
      return true;
    }
    return true;
  }

  bool inputAlarm(char c) {
    if (!_prefs) { if (c == KEY_CANCEL) { _view = V_MENU; _sel = 0; } return true; }

    // Time row's digit-cursor sub-mode (same shape as the Timer's HH:MM:SS
    // cursor): LEFT/RIGHT moves across HH:MM's 4 digits, UP/DOWN steps the
    // digit under the cursor, Enter/Cancel commits and leaves (there's no
    // rollback — every step already writes straight to alarm_hour/min and
    // reschedules, same as the old per-field editor did).
    if (_alarm_editing) {
      if (keyIsPrev(c)) { _alarm_cur = (_alarm_cur + 3) % 4; return true; }
      if (keyIsNext(c)) { _alarm_cur = (_alarm_cur + 1) % 4; return true; }
      if (c == KEY_UP)   { stepAlarmDigit(+1); return true; }
      if (c == KEY_DOWN) { stepAlarmDigit(-1); return true; }
      if (c == KEY_ENTER || c == KEY_CANCEL) { _alarm_editing = false; return true; }
      return true;
    }

    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_alarm_dirty);  // persist alarm fields only if edited
      _task->onAlarmChanged();                 // re-schedule from the new time
      _view = V_MENU; _sel = 0; _scroll = 0;
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 2; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < 2) ? _sel + 1 : 0; return true; }
    // Repeat: LEFT/RIGHT steps Off -> Daily -> Weekdays -> Weekends (wrapping),
    // matching the rest of the UI's convention for multi-value fields (e.g.
    // Settings' Keyboard Alphabet / Battery display); Enter still advances one
    // step too, for parity with those same fields.
    if (_sel == 1 && (keyIsPrev(c) || keyIsNext(c) || c == KEY_ENTER)) {
      int idx = NodePrefs::alarmRepeatIdxForMask(_prefs->alarm_repeat_mask);
      idx = keyIsPrev(c) ? (idx + NodePrefs::ALARM_REPEAT_COUNT - 1) % NodePrefs::ALARM_REPEAT_COUNT
                         : (idx + 1) % NodePrefs::ALARM_REPEAT_COUNT;
      _prefs->alarm_repeat_mask = NodePrefs::alarmRepeatMaskForIdx(idx);
      _alarm_dirty = true; _task->onAlarmChanged();
      return true;
    }
    // Armed: a plain on/off toggle, but still answers LEFT/RIGHT as well as
    // Enter (same as every boolean field in Settings — e.g. Auto-lock, Units,
    // Power save — flips either way rather than reserving direction for
    // "increase/decrease").
    if (_sel == 2 && (keyIsPrev(c) || keyIsNext(c) || c == KEY_ENTER)) {
      _prefs->alarm_on ^= 1; _alarm_dirty = true; _task->onAlarmChanged();
      return true;
    }
    if (c == KEY_ENTER && _sel == 0) { _alarm_editing = true; _alarm_cur = 0; return true; }
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

  // Calendar: Left / Right (Up / Down too) a month, Enter back to this one.
  bool inputCalendar(char c) {
    if (c == KEY_CANCEL) { _view = V_MENU; _sel = 3; return true; }
    if (keyIsPrev(c) || c == KEY_UP)   { if (_cal_month > -120) _cal_month--; return true; }
    if (keyIsNext(c) || c == KEY_DOWN) { if (_cal_month < 120) _cal_month++; return true; }
    if (c == KEY_ENTER) { _cal_month = 0; return true; }
    return true;
  }

public:
  ClockToolsScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override {
    _view = V_MENU; _sel = 0; _scroll = 0;
    _timer_cur = 0;
    _alarm_editing = false;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    switch (_view) {
      case V_ALARM:     return renderAlarm(display);
      case V_TIMER:     return renderTimer(display);
      case V_STOPWATCH: return renderStopwatch(display);
      case V_CALENDAR:  return renderCalendar(display);
      default:          return renderMenu(display);
    }
  }

  bool handleInput(char c) override {
    switch (_view) {
      case V_ALARM:     return inputAlarm(c);
      case V_TIMER:     return inputTimer(c);
      case V_STOPWATCH: return inputStopwatch(c);
      case V_CALENDAR:  return inputCalendar(c);
      default:          return inputMenu(c);
    }
  }
};
