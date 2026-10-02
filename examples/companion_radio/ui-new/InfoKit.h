#pragma once
// Building blocks for screens that show data (Status, the tool screens): one
// look for a value row, a section break, a meter, a chip, a big number with
// its unit, a switch and a history line. The 128x64 counterpart of the L2's
// info cards. Sizes follow the font (line height, miniIconScale), never a
// fixed 128x64, so a larger-font e-ink layout gets them scaled for free.

#include <helpers/ui/DisplayDriver.h>
#include <string.h>
#include "icons.h"

namespace info {

// Whether a value fits on its label's line (else rowH() gives it a line below).
// `reserve` keeps the value clear of a scroll column.
inline bool rowFits(DisplayDriver& d, const char* label, const char* value, int reserve = 0, int x0 = 1) {
  if (!value || !*value) return true;
  const int room = d.width() - reserve - 2 - (x0 + d.getTextWidth(label) + d.getCharWidth());
  return (int)d.getTextWidth(value) <= room;
}
inline int rowH(DisplayDriver& d, const char* label, const char* value, int reserve = 0) {
  return rowFits(d, label, value, reserve) ? d.lineStep() : d.lineStep() + d.getLineHeight();
}
// A row of rowH(): the value beside the label, or right-aligned on the line
// under it when it's too long for that (ending in an ellipsis only past the
// full width).
inline void rowWrapped(DisplayDriver& d, int y, const char* label, const char* value, int reserve = 0, int x0 = 1) {
  if (rowFits(d, label, value, reserve, x0)) {
    d.setCursor(x0, y);
    d.print(label);
    if (value && *value) d.drawTextRightAlign(d.width() - reserve - 2, y, value);
    return;
  }
  d.setCursor(x0, y);
  d.print(label);
  const int right = d.width() - reserve - 2, room = right - x0;   // the whole line is its own
  const int vy = y + d.getLineHeight();
  if ((int)d.getTextWidth(value) <= room) d.drawTextRightAlign(right, vy, value);
  else d.drawTextEllipsized(right - room, vy, room, value);
}

// A dotted rule across [x, x + w), the separator between categories.
inline void rule(DisplayDriver& d, int x, int y, int w) {
  const int s = miniIconScale(d);
  for (int i = 0; i < w; i += 2 * s) d.fillRect(x + i, y, s, s);
}
inline void vrule(DisplayDriver& d, int x, int y, int h) {
  const int s = miniIconScale(d);
  for (int i = 0; i < h; i += 2 * s) d.fillRect(x, y + i, s, s);
}

// A section title: the label centred between two dotted rules. One line tall.
inline void section(DisplayDriver& d, int y, const char* label, int reserve = 0) {
  const int right = d.width() - reserve - 1, lw = d.getTextWidth(label);
  const int lx = (right - lw) / 2, ry = y + d.getLineHeight() / 2, gap = 2 * d.getCharWidth() / 3;
  rule(d, 1, ry, lx - gap - 1);
  d.setCursor(lx, y);
  d.print(label);
  rule(d, lx + lw + gap, ry, right - (lx + lw + gap));
}

// A battery glyph the size of a mini icon (7 x 5 at 1x), filled to frac.
inline int batteryW(DisplayDriver& d) { return 7 * miniIconScale(d); }
inline void battery(DisplayDriver& d, int x, int y, float frac) {
  const int s = miniIconScale(d);
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d.drawRect(x, y, 6 * s, 5 * s);
  d.fillRect(x + 6 * s, y + s, s, 3 * s);
  const int fw = (int)(4 * s * frac + 0.5f);
  if (fw > 0) d.fillRect(x + s, y + s, fw, 3 * s);
}

// A soft-cornered meter, filled to frac (0..1).
inline void meter(DisplayDriver& d, int x, int y, int w, int h, float frac) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d.drawSoftRect(x, y, w, h);
  const int in = d.sepH() + 1, fw = (int)((w - 2 * in) * frac + 0.5f);
  if (fw > 0) d.fillRect(x + in, y + in, fw, h - 2 * in);
}

// A chip: a state as a filled pill (FIX, REC), a parameter as an outline
// (SF8). One line plus 2 px tall; returns the x after it, gap included.
inline int chipW(DisplayDriver& d, const char* t) { return d.getTextWidth(t) + 5; }
inline int chip(DisplayDriver& d, int x, int y, const char* t, bool filled = false) {
  const int w = chipW(d, t), h = d.getLineHeight() + 2;
  if (filled) d.fillSoftRect(x, y, w, h);
  else        d.drawSoftRect(x, y, w, h);
  if (filled) d.setColor(DisplayDriver::DARK);
  d.setCursor(x + 3, y + 1);
  d.print(t);
  d.setColor(DisplayDriver::LIGHT);
  return x + w + 3;
}

// A value at twice the text size with its unit beside it at the normal size,
// sharing the baseline. Two lines tall; returns its width.
inline int big(DisplayDriver& d, int x, int y, const char* value, const char* unit) {
  d.setTextSize(2);
  d.setCursor(x, y);
  d.print(value);
  int w = d.getTextWidth(value);
  d.setTextSize(1);
  if (unit && *unit) {
    d.setCursor(x + w + 3, y + d.getLineHeight());
    d.print(unit);
    w += 3 + d.getTextWidth(unit);
  }
  return w;
}
inline int bigH(DisplayDriver& d) { return 2 * d.getLineHeight(); }

// An on/off switch, right edge at x_right, centred on a text line at y: a
// filled pill with the knob right when on, an outline with the knob left off.
// `inv` draws it in a selected row's colours (dark on the light bar) and
// leaves the ink dark, as the row found it.
inline int switchW(DisplayDriver& d) { return 13 * miniIconScale(d); }
inline void toggle(DisplayDriver& d, int x_right, int y, bool on, bool inv = false) {
  const DisplayDriver::Color ink = inv ? DisplayDriver::DARK : DisplayDriver::LIGHT;
  const DisplayDriver::Color paper = inv ? DisplayDriver::LIGHT : DisplayDriver::DARK;
  const int s = miniIconScale(d), w = switchW(d), h = 7 * s;
  const int x = x_right - w, ty = y + (d.getLineHeight() - h) / 2;
  d.setColor(ink);
  if (on) {
    d.fillSoftRect(x, ty, w, h);
    d.setColor(paper);
    d.fillRect(x + w - 5 * s, ty + s, 3 * s, h - 2 * s);
  } else {
    d.drawSoftRect(x, ty, w, h);
    d.fillRect(x + 2 * s, ty + 2 * s, 3 * s, h - 4 * s);
  }
  d.setColor(ink);
}

// ── List rows (call after drawRowSelection(); text is already in the row's ink)
// A setting or field: label at x0, value flush right with its unit after a
// space ("50 m"). A value too long for the room beside the label is cut, and
// scrolls while the row is selected (sel).
// Returns that marquee's next-step delay (0: nothing moving).
inline int valueRow(DisplayDriver& d, int y, const char* label, const char* value, bool sel,
                    int reserve, int x0 = 2) {
  d.setCursor(x0, y);
  d.print(label);
  if (!value || !*value) return 0;
  const int right = d.width() - reserve - 2;
  const int lx = x0 + (*label ? d.getTextWidth(label) + d.getCharWidth() : 0);
  if (lx + (int)d.getTextWidth(value) <= right) { d.drawTextRightAlign(right, y, value); return 0; }
  const int r = d.drawTextEllipsized(lx, y, right - lx, value, sel);
  return sel && r > 0 ? r : 0;
}
// A row from a screen's value formatter: exactly "ON" / "OFF" is a yes/no
// setting and gets the switch; anything else (an option that can be "Off"
// among numbers included) is a value. Returns valueRow()'s marquee delay.
inline void switchRow(DisplayDriver& d, int y, const char* label, bool on, bool sel, int reserve, int x0 = 2);
inline int listRow(DisplayDriver& d, int y, const char* label, const char* value, bool sel,
                   int reserve, int x0 = 2) {
  if (value && (!strcmp(value, "ON") || !strcmp(value, "OFF"))) {
    switchRow(d, y, label, value[1] == 'N', sel, reserve, x0);
    return 0;
  }
  return valueRow(d, y, label, value, sel, reserve, x0);
}
// An on/off row: the label, then a switch at the right edge.
inline void switchRow(DisplayDriver& d, int y, const char* label, bool on, bool sel, int reserve, int x0) {
  d.setCursor(x0, y);
  d.print(label);
  toggle(d, d.width() - reserve - 2, y, on, sel);
}

// The last N samples of one reading, oldest first, for a history line.
template <int N>
struct History {
  int16_t v[N];
  uint8_t n = 0, head = 0;
  void push(int16_t x) { v[head] = x; head = (head + 1) % N; if (n < N) n++; }
  int16_t at(int i) const { return v[(head + N - n + i) % N]; }   // 0 = oldest
};

// A history line in a w x h box: a dotted baseline, the samples scaled
// between their own min and max (at least `min_span` apart, so a flat
// reading stays flat), newest at the right edge.
template <int N>
inline void spark(DisplayDriver& d, int x, int y, int w, int h, const History<N>& hs, int min_span = 4) {
  const int s = miniIconScale(d);
  for (int i = 0; i < w; i += 3 * s) d.fillRect(x + i, y + h - s, s, s);
  if (hs.n < 2) return;
  int lo = hs.at(0), hi = lo;
  for (int i = 1; i < hs.n; i++) { int v = hs.at(i); if (v < lo) lo = v; if (v > hi) hi = v; }
  if (hi - lo < min_span) { int mid = (hi + lo) / 2; lo = mid - min_span / 2; hi = lo + min_span; }
  int px = -1, py = 0;
  for (int i = 0; i < hs.n; i++) {
    const int cx = x + w - 1 - (hs.n - 1 - i) * (w - 1) / (N - 1);
    const int cy = y + h - 1 - (hs.at(i) - lo) * (h - 1) / (hi - lo);
    if (px >= 0) {   // a step: across, then up or down
      d.fillRect(px, py, cx - px + 1, s);
      d.fillRect(cx, py < cy ? py : cy, s, (py < cy ? cy - py : py - cy) + s);
    }
    px = cx; py = cy;
  }
}

// Blocks stacked down from `top`, with the first `skip` left out: the way a
// tab scrolls by whole blocks. Call place(h) before drawing each block; it
// says whether the block is on screen and where (at). The totals then give
// the scroll indicator and whether DOWN has anything left to show.
// A measuring Flow (measure()) only adds the heights up and draws nothing.
struct Flow {
  int y, bottom, skip;
  int idx = 0, at = 0;
  long total_px = 0, skipped_px = 0;
  bool more = false, measuring = false;
  Flow(int top, int bottom_, int skip_) : y(top), bottom(bottom_), skip(skip_) {}
  static Flow measure() { Flow f(0, 0, 0); f.measuring = true; return f; }
  bool place(int h) {
    total_px += h;
    if (measuring) return false;
    if (idx++ < skip) { skipped_px += h; return false; }
    if (more || y + h > bottom) { more = true; return false; }
    at = y; y += h;
    return true;
  }
};

}  // namespace info
