#pragma once
// Small 1-px drawing primitives the DisplayDriver abstraction doesn't provide
// (no line / circle in the GFX wrapper used here). Shared by the map and the
// compass so the Bresenham / midpoint routines aren't copy-pasted per screen.
// Everything is plotted as 1x1 fillRect to stay within DisplayDriver's API.

#include <helpers/ui/DisplayDriver.h>
#include <stdlib.h>   // abs
#include <math.h>     // lroundf
#include "../Trail.h"

namespace gfx {

// A dash pattern: bit i of `bits` says whether step i of every `len` is
// drawn (steps of a line's width, so a doubled panel doubles the dashes).
// len 0: solid.
struct Dash { uint16_t bits; uint8_t len; };
static const Dash DASH_SOLID = { 0, 0 };

// Bresenham line between two points.
static inline void drawLine(DisplayDriver& d, int x0, int y0, int x1, int y1, Dash dash = DASH_SOLID) {
  int dx =  abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  const int t = d.sepH();   // 2px on a doubled panel
  for (int i = 0; ; i++) {
    if (!dash.len || (dash.bits >> ((i / t) % dash.len) & 1)) d.fillRect(x0 - t / 2, y0 - t / 2, t, t);
    if (x0 == x1 && y0 == y1) break;
    int e2 = err * 2;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// The part of a line inside a box (x, y, w, h), Liang-Barsky; nothing when
// it misses the box. For lines whose ends may lie far off the screen: the
// ends are floats, so a point projected thousands of screens away (a map
// zoomed in) neither overflows an int nor turns the line around.
static inline void drawLineClipped(DisplayDriver& d, float x0, float y0, float x1, float y1, int bx, int by, int bw, int bh,
                                   Dash dash = DASH_SOLID) {
  float t0 = 0, t1 = 1;
  const float dx = x1 - x0, dy = y1 - y0;
  const float p[4] = { -dx, dx, -dy, dy };
  const float q[4] = { x0 - bx, bx + bw - 1 - x0, y0 - by, by + bh - 1 - y0 };
  for (int i = 0; i < 4; i++) {
    if (p[i] == 0) { if (q[i] < 0) return; continue; }
    float r = q[i] / p[i];
    if (p[i] < 0) { if (r > t1) return; if (r > t0) t0 = r; }
    else          { if (r < t0) return; if (r < t1) t1 = r; }
  }
  drawLine(d, (int)lroundf(x0 + t0 * dx), (int)lroundf(y0 + t0 * dy), (int)lroundf(x0 + t1 * dx), (int)lroundf(y0 + t1 * dy), dash);
}

// Midpoint-circle outline.
static inline void drawCircle(DisplayDriver& d, int cx, int cy, int r) {
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    d.fillRect(cx + x, cy + y, 1, 1); d.fillRect(cx + y, cy + x, 1, 1);
    d.fillRect(cx - y, cy + x, 1, 1); d.fillRect(cx - x, cy + y, 1, 1);
    d.fillRect(cx - x, cy - y, 1, 1); d.fillRect(cx - y, cy - x, 1, 1);
    d.fillRect(cx + y, cy - x, 1, 1); d.fillRect(cx + x, cy - y, 1, 1);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

// The same outline, only the part inside a box (x, y, w, h), as thick as a
// line (sepH: 2px on a doubled panel).
static inline void drawCircleClipped(DisplayDriver& d, int cx, int cy, int r, int bx, int by, int bw, int bh) {
  const int t = d.sepH();
  auto dot = [&](int x, int y) {
    if (x - t / 2 >= bx && y - t / 2 >= by && x - t / 2 + t <= bx + bw && y - t / 2 + t <= by + bh)
      d.fillRect(x - t / 2, y - t / 2, t, t);
  };
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    dot(cx + x, cy + y); dot(cx + y, cy + x); dot(cx - y, cy + x); dot(cx - x, cy + y);
    dot(cx - x, cy - y); dot(cx - y, cy - x); dot(cx + y, cy - x); dot(cx + x, cy - y);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

// Walks a TrailStore and renders it as a connected polyline via drawLine(),
// breaking the line at each TRAIL_FLAG_SEG_START point (trail paused/
// restarted) instead of bridging the dead-time gap. Shared by the full Trail
// map and the Home screen's mini-map preview, which only differ in how they
// project lat/lon to screen coords and what (if anything) they draw at a
// break. `project(lat, lon, x, y)` fills the screen coords for one point;
// `on_break(x_prev_end, y_prev_end, x_new_start, y_new_start)` runs at each
// break instead of drawLine() — pass a no-op lambda for a silent gap.
template <typename Project, typename OnBreak>
static void drawTrail(DisplayDriver& d, TrailStore& tr, Project project, OnBreak on_break) {
  if (tr.count() == 0) return;
  int x0, y0; project(tr.at(0).lat_1e6, tr.at(0).lon_1e6, x0, y0);
  for (int i = 1; i < tr.count(); i++) {
    const TrailPoint& pt = tr.at(i);
    int x1, y1; project(pt.lat_1e6, pt.lon_1e6, x1, y1);
    if (pt.flags & TRAIL_FLAG_SEG_START) on_break(x0, y0, x1, y1);
    else                                  drawLine(d, x0, y0, x1, y1);
    x0 = x1; y0 = y1;
  }
  // The simplifier's pending point: the straight stretch walked since the
  // last vertex, so the line reaches where you are instead of lagging behind.
  TrailPoint pend;
  if (tr.pendingPoint(pend)) {
    int x1, y1; project(pend.lat_1e6, pend.lon_1e6, x1, y1);
    drawLine(d, x0, y0, x1, y1);
  }
}

// The route followed, dashed, from the RouteGuide's overview in RAM (a whole
// route read off the flash each frame made the map lag). Shared by the Trail
// map and Home's mini-map; `ex, ey` get its end in the direction followed, for
// the caller's marker. False with nothing to draw.
template <typename Guide, typename Project>
static bool drawRoute(DisplayDriver& d, const Guide& g, Project project, int& ex, int& ey) {
  const int n = g.overviewCount();
  if (n <= 0) return false;
  const Dash dash = { 0x3, 4 };
  int px = 0, py = 0;
  for (int i = 0; i < n; i++) {
    int32_t la, lo;
    g.overviewPoint(i, la, lo);
    int x, y;
    project(la, lo, x, y);
    if (i > 0) drawLine(d, px, py, x, y, dash);
    px = x; py = y;
  }
  int32_t la, lo;
  g.overviewPoint(g.reversed() ? 0 : n - 1, la, lo);
  project(la, lo, ex, ey);
  return true;
}

}  // namespace gfx
