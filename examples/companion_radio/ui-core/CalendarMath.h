#pragma once
// A month's calendar, shared by both UIs: its length, the column (0 = Monday)
// its 1st falls in, how many weeks it spans, and the days left in it the
// alarm goes off on. ui-new draws it on e-ink (CalendarView.h), ui-lvgl in
// Clock > Calendar.

#include <stdint.h>
#include <time.h>

namespace calmath {

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

// The days left this month the alarm goes off on, bit (day - 1) each: the
// repeat days (`mask`: bit per tm_wday), or a one-shot's next (today or
// tomorrow). `t` is the local time now.
inline uint32_t alarmDays(const struct tm& t, bool on, uint8_t mask, int hour, int min) {
  if (!on) return 0;
  const int n = daysIn(t.tm_year, t.tm_mon);
  const bool passed = t.tm_hour * 60 + t.tm_min >= hour * 60 + min;
  uint32_t days = 0;
  for (int day = t.tm_mday; day <= n; day++) {
    if (day == t.tm_mday && passed) continue;
    const int wday = (t.tm_wday + day - t.tm_mday) % 7;
    if (mask ? (mask & (1 << wday)) : true) {
      days |= 1UL << (day - 1);
      if (!mask) break;   // one-shot: only the next
    }
  }
  return days;
}

}  // namespace calmath
