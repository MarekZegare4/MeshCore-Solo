#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <Arduino.h>

// Settings > Clock: a date and time, "2026-11-14 12:30", edited a field at a
// time. LEFT/RIGHT moves between year, month, day, hour and minute; UP/DOWN
// steps the one under the cursor, wrapping within it (the day kept within its
// month); Enter takes it, Cancel leaves it. With one button (EDIT_DIGITS) a
// click is UP, a double DOWN, a hold the next field, past the minute done --
// as DigitEditor's digits.
struct DateTimeEditor {
  int  year = 2026, month = 1, day = 1, hour = 0, minute = 0;
  int8_t cursor = 0;      // 0 year .. 4 minute
  bool active = false;

  enum Result { NONE, DONE, CANCELLED };
  static const int FIELDS = 5;

  void begin(int y, int mo, int d, int h, int mi) {
    year = y < 2024 ? 2024 : (y > 2099 ? 2099 : y);
    month = mo; day = d; hour = h; minute = mi;
    cursor = 0; active = true;
  }

  static int daysIn(int y, int m) {
    static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 29 : D[(m - 1) % 12];
  }
  static int wrap(int v, int lo, int hi) { return v < lo ? hi : (v > hi ? lo : v); }

  void step(int dir) {
    switch (cursor) {
      case 0: year = wrap(year + dir, 2024, 2099); break;
      case 1: month = wrap(month + dir, 1, 12); break;
      case 2: day = wrap(day + dir, 1, daysIn(year, month)); break;
      case 3: hour = wrap(hour + dir, 0, 23); break;
      default: minute = wrap(minute + dir, 0, 59); break;
    }
    if (day > daysIn(year, month)) day = daysIn(year, month);
  }

  Result handleInput(char c) {
    if (!active) return CANCELLED;
    if (c == KEY_NEXT) {
      if (cursor < FIELDS - 1) { cursor++; return NONE; }
      active = false;
      return DONE;
    }
    if (keyIsPrev(c)) { if (cursor > 0) cursor--; return NONE; }
    if (keyIsNext(c)) { if (cursor < FIELDS - 1) cursor++; return NONE; }
    if (c == KEY_UP)   { step(1);  return NONE; }
    if (c == KEY_DOWN) { step(-1); return NONE; }
    if (c == KEY_ENTER)  { active = false; return DONE; }
    if (c == KEY_CANCEL) { active = false; return CANCELLED; }
    return NONE;
  }

  void format(char* b, int n) const {
    snprintf(b, n, "%04d-%02d-%02d %02d:%02d", year, month, day, hour, minute);
  }

  // At (x, y) with the field under the cursor inverted, as DigitEditor::render
  // (inside a selected row: ink DARK on a LIGHT bar; LIGHT restored after).
  void render(DisplayDriver& d, int x, int y) const {
    static const uint8_t START[FIELDS] = { 0, 5, 8, 11, 14 }, LEN[FIELDS] = { 4, 2, 2, 2, 2 };
    char buf[20];
    format(buf, sizeof(buf));
    const int cw = d.getCharWidth();
    for (int k = 0; buf[k]; k++) {
      int cx = x + k * cw;
      bool on = k >= START[cursor] && k < START[cursor] + LEN[cursor];
      d.setColor(UIColor::window_bkg);
      if (on) {
        d.fillSoftRect(cx, y - 1, cw, d.getLineHeight() + 1);
        d.setColor(UIColor::primary_txt);
      }
      char one[2] = { buf[k], '\0' };
      d.setCursor(cx, y);
      d.print(one);
    }
    d.setColor(UIColor::primary_txt);
  }
};
