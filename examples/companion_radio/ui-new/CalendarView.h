#pragma once
// The current month as a grid of days, weeks from Monday, today in a filled
// soft box, marked days (the alarm's) underlined. For the room a tall e-ink
// screen has under its clock (the Clock page, the lock screen).

#include <time.h>
#include <helpers/ui/DisplayDriver.h>
#include "InfoKit.h"

namespace calendar {

inline int daysIn(int year, int mon) {   // mon 0..11, year as in tm (since 1900)
  static const uint8_t D[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  const int y = year + 1900;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return D[mon] + (mon == 1 && leap ? 1 : 0);
}
// The column (0 = Monday) the 1st falls in.
inline int firstCol(const struct tm& t) {
  const int wday1 = ((t.tm_wday - (t.tm_mday - 1)) % 7 + 7) % 7;   // 0 = Sunday
  return (wday1 + 6) % 7;
}
inline int weeks(const struct tm& t) { return (firstCol(t) + daysIn(t.tm_year, t.tm_mon) + 6) / 7; }

// Its height: the day names, a rule, then a row a week.
inline int height(DisplayDriver& d, const struct tm& t) {
  return (1 + weeks(t)) * (d.getLineHeight() + 2) + 3;
}

// `marked`: bit (day - 1) set for each day to underline.
inline void draw(DisplayDriver& d, int x, int y, int w, const struct tm& t, uint32_t marked = 0) {
  static const char* const NAMES[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
  const int lh = d.getLineHeight(), step = lh + 2, cw = w / 7;
  const int x0 = x + (w - 7 * cw) / 2;
  d.setColor(DisplayDriver::LIGHT);
  for (int c = 0; c < 7; c++) d.drawTextCentered(x0 + c * cw + cw / 2, y, NAMES[c]);
  info::rule(d, x0, y + lh + 1, 7 * cw);
  const int top = y + step + 3, col0 = firstCol(t), n = daysIn(t.tm_year, t.tm_mon);
  char buf[3];
  for (int day = 1; day <= n; day++) {
    const int k = col0 + day - 1, cx = x0 + (k % 7) * cw, cy = top + (k / 7) * step;
    snprintf(buf, sizeof(buf), "%d", day);
    if (day == t.tm_mday) {
      d.fillSoftRect(cx + 1, cy - 1, cw - 2, lh + 2);
      d.setColor(DisplayDriver::DARK);
      d.drawTextCentered(cx + cw / 2, cy, buf);
      d.setColor(DisplayDriver::LIGHT);
    } else {
      d.drawTextCentered(cx + cw / 2, cy, buf);
    }
    if (marked & (1UL << (day - 1))) {   // under the number, in the gap below it
      const int uw = d.getTextWidth(buf);
      d.setColor(day == t.tm_mday ? DisplayDriver::DARK : DisplayDriver::LIGHT);
      d.fillRect(cx + (cw - uw) / 2, cy + lh, uw, 1);
      d.setColor(DisplayDriver::LIGHT);
    }
  }
}

}  // namespace calendar
