#pragma once
// Building blocks for screens that show data (Status, the tool screens): one
// look for a value row, a section break, a meter, a chip, a big number with
// its unit, a switch and a history line. The 128x64 counterpart of the L2's
// info cards. Sizes follow the font (line height, miniIconScale), never a
// fixed 128x64, so a larger-font e-ink layout gets them scaled for free.

#include <helpers/ui/DisplayDriver.h>
#include <string.h>
#include <math.h>
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

// A battery glyph the size of a mini icon (7 x 5 at 1x, 9 x 7 in the large
// set), its corners softly rounded, filled to frac.
static constexpr int BATT_BODY_W = MINI_ICONS_LARGE ? 8 : 6, BATT_BODY_H = MINI_ICONS_LARGE ? 7 : 5;
inline int batteryW(DisplayDriver& d) { return (BATT_BODY_W + 1) * miniIconScale(d); }
inline int batteryH(DisplayDriver& d) { return BATT_BODY_H * miniIconScale(d); }
inline void battery(DisplayDriver& d, int x, int y, float frac) {
  const int s = miniIconScale(d), w = BATT_BODY_W * s, h = BATT_BODY_H * s;
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d.fillRect(x + 1, y, w - 2, 1);           // the outline without its corner pixels
  d.fillRect(x + 1, y + h - 1, w - 2, 1);
  d.fillRect(x, y + 1, 1, h - 2);
  d.fillRect(x + w - 1, y + 1, 1, h - 2);
  d.fillRect(x + w, y + s, s, h - 2 * s);
  const int in = MINI_ICONS_LARGE ? 2 * s : s;   // the large one keeps a gap inside its outline
  const int fw = (int)((w - 2 * in) * frac + 0.5f);
  if (fw > 0) d.fillRect(x + in, y + in, fw, h - 2 * in);
}

// A soft-cornered meter, filled to frac (0..1).
inline void meter(DisplayDriver& d, int x, int y, int w, int h, float frac) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  d.drawSoftRect(x, y, w, h);
  const int in = d.sepH() + 1, fw = (int)((w - 2 * in) * frac + 0.5f);
  if (fw > 0) d.fillRect(x + in, y + in, fw, h - 2 * in);
}

// Midpoint circle; `step` > 1 dots it (every step-th point of each octant).
inline void circle(DisplayDriver& d, int cx, int cy, int r, int step = 1) {
  int x = r, y = 0, err = 1 - r, i = 0;
  while (x >= y) {
    if (i++ % step == 0) {
      d.fillRect(cx + x, cy + y, 1, 1); d.fillRect(cx - x, cy + y, 1, 1);
      d.fillRect(cx + x, cy - y, 1, 1); d.fillRect(cx - x, cy - y, 1, 1);
      d.fillRect(cx + y, cy + x, 1, 1); d.fillRect(cx - y, cy + x, 1, 1);
      d.fillRect(cx + y, cy - x, 1, 1); d.fillRect(cx - y, cy - x, 1, 1);
    }
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

// A 1 px line (Bresenham).
inline void line(DisplayDriver& d, int x0, int y0, int x1, int y1) {
  const int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    d.fillRect(x0, y0, 1, 1);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// An arrow of length 2r centred on (cx, cy), pointing to `deg` (0 = up /
// north, clockwise): a shaft and a two-stroke head at the tip.
inline void arrow(DisplayDriver& d, int cx, int cy, int r, int deg) {
  const float a = deg * (float)M_PI / 180.0f, sx = sinf(a), sy = -cosf(a);
  const int tx = cx + (int)lroundf(r * sx), ty = cy + (int)lroundf(r * sy);
  line(d, cx - (int)lroundf(r * sx), cy - (int)lroundf(r * sy), tx, ty);
  const float h = r * 0.6f;
  for (int k = -1; k <= 1; k += 2) {   // the head's strokes, 30 degrees off the shaft
    const float b = a + (float)M_PI + k * 0.52f;
    line(d, tx, ty, tx + (int)lroundf(h * sinf(b)), ty - (int)lroundf(h * cosf(b)));
  }
}

// A compass rose of radius r round (cx, cy), north up: the ring, a tick in at
// each cardinal point, N in a gap at the top, and -- for deg >= 0 -- a needle
// from the centre to the ring, towards deg.
inline void rose(DisplayDriver& d, int cx, int cy, int r, int deg) {
  const int s = miniIconScale(d), t = 3 * s, lh = d.getLineHeight(), cw = d.getCharWidth();
  circle(d, cx, cy, r);
  circle(d, cx, cy, r / 2, 3);
  d.fillRect(cx - s / 2, cy + r - t, s, t);   // S
  d.fillRect(cx + r - t, cy - s / 2, t, s);   // E
  d.fillRect(cx - r, cy - s / 2, t, s);       // W
  d.setColor(DisplayDriver::DARK);            // N, on a gap in the ring
  d.fillRect(cx - cw / 2 - 1, cy - r - lh / 2, cw + 2, lh);
  d.setColor(DisplayDriver::LIGHT);
  d.setCursor(cx - cw / 2, cy - r - lh / 2);
  d.print("N");
  if (deg >= 0) {
    const float a = deg * (float)M_PI / 180.0f;
    const int tx = cx + (int)lroundf((r - t - s) * sinf(a)), ty = cy - (int)lroundf((r - t - s) * cosf(a));
    for (int o = -(s / 2); o <= s / 2; o++) line(d, cx + o, cy, tx + o, ty);
    const float h = r * 0.3f;
    for (int k = -1; k <= 1; k += 2) {
      const float b = a + (float)M_PI + k * 0.45f;
      line(d, tx, ty, tx + (int)lroundf(h * sinf(b)), ty - (int)lroundf(h * cosf(b)));
    }
  }
  d.fillRect(cx - s, cy - s, 2 * s + 1, 2 * s + 1);   // the centre: you
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

// Ordered (Bayer 4x4) dithering: whether pixel (x, y) is ink at a density of
// level/16. For graded fills on large elements only -- it breaks up small
// ones.
inline bool bayer(int x, int y, int level) {
  static const uint8_t M[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
  return M[y & 3][x & 3] < level;
}
// A column from y0 down to (not including) y1, dithered from dense at the top
// to sparse at the bottom: the glow under a chart's line.
inline void fadeColumn(DisplayDriver& d, int x, int y0, int y1) {
  const int h = y1 - y0;
  for (int y = y0; y < y1; y++) {
    const int level = 9 - 8 * (y - y0) / (h > 1 ? h : 1);   // 9/16 under the line -> 1/16 at the floor
    if (bayer(x, y, level)) d.fillRect(x, y, 1, 1);
  }
}

// A history as a chart, for a screen with the room (e-ink): a soft frame with
// a dotted middle line, the samples as a step line over a dithered glow --
// or, with `bars`, one bar each from the bottom, for counts -- the newest at
// the right edge, then a caption line under the frame: `left` at its left
// (how far back it goes), `right` at its right. chartH() is the whole
// block's height.
inline int chartH(DisplayDriver& d, int frame_h) { return frame_h + d.getLineHeight() + 3; }
template <int N>
inline void chart(DisplayDriver& d, int x, int y, int w, int h, const History<N>& hs, int min_span,
                  bool bars, const char* left, const char* right) {
  d.drawSoftRect(x, y, w, h);
  const int s = miniIconScale(d);
  for (int i = x + 3; i < x + w - 3; i += 3 * s) d.fillRect(i, y + h / 2, s, s);
  const int ix = x + 2, iy = y + 2, iw = w - 4, ih = h - 4;
  if (hs.n < (bars ? 1 : 2)) {   // nothing to draw yet: say so over the line
    const int tw = d.getTextWidth("no data"), tx = x + (w - tw) / 2, ty = y + (h - d.getLineHeight()) / 2;
    d.setColor(DisplayDriver::DARK);
    d.fillRect(tx - 2, ty, tw + 4, d.getLineHeight());
    d.setColor(DisplayDriver::LIGHT);
    d.setCursor(tx, ty);
    d.print("no data");
  } else {
    int lo = bars ? 0 : hs.at(0), hi = lo;
    for (int i = 0; i < hs.n; i++) { int v = hs.at(i); if (v < lo) lo = v; if (v > hi) hi = v; }
    if (hi - lo < min_span) { if (bars) hi = lo + min_span; else { int mid = (hi + lo) / 2; lo = mid - min_span / 2; hi = lo + min_span; } }
    const int bw = iw / N > 1 ? iw / N : 1;
    int px = -1, py = 0;
    for (int i = 0; i < hs.n; i++) {
      const int cx = ix + iw - 1 - (hs.n - 1 - i) * (iw - 1) / (N - 1);
      const int cy = iy + ih - 1 - (hs.at(i) - lo) * (ih - 1) / (hi - lo);
      if (bars) {
        if (hs.at(i) > lo) d.fillRect(cx - bw + 1, cy, bw, iy + ih - cy);
      } else {
        if (px >= 0) {
          d.fillRect(px, py, cx - px + 1, 1);
          d.fillRect(cx, py < cy ? py : cy, 1, (py < cy ? cy - py : py - cy) + 1);
          for (int c = px + 1; c <= cx; c++) fadeColumn(d, c, (c < cx ? py : cy) + 2, iy + ih);
        }
        px = cx; py = cy;
      }
    }
  }
  const int ty = y + h + 2;
  if (left && *left)   { d.setCursor(x, ty); d.print(left); }
  if (right && *right) d.drawTextRightAlign(x + w, ty, right);
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
