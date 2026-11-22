#pragma once
// Tools > Compass > Calibrate (a board with a magnetometer: HAS_COMPASS).
//
// The readings collected (ui-core/Compass.h, MagCal.h) as dots on a sphere
// that turns with the device: where the Earth's field points, seen from the
// device, is the pen -- always the middle of the ball, facing you -- and
// turning the device rolls the ball under it, so new dots are painted where
// it hasn't been yet. Dots on the near side solid, the far side single pixels;
// three great circles of the device's own axes give the ball its turn (far
// halves dotted). Beside it how far it has got, and the result: kept when the
// fit is good, Enter to try again otherwise.
//
// The view of the ball: towards you the field's direction, to the right east,
// up what's left -- all from the latest accelerometer and magnetometer
// readings, the magnetometer taken from the middle of what's been collected
// (its hard iron, roughly), as nothing is calibrated yet.

#include <math.h>
#include "GfxUtils.h"
#include "../ui-core/Compass.h"

class CompassCalView {
public:
  // Starts collecting; ends by itself (Compass::start(true)).
  void begin(Compass& c, void (*saved)()) {
    _c = &c;
    _c->start(true, saved);
  }
  void end() { if (_c && _c->calibrating()) _c->cancel(); }
  bool done() const { return _c && !_c->calibrating(); }
  void restart(void (*saved)()) { if (_c) _c->start(true, saved); }

  // Returns the next refresh in ms.
  int render(DisplayDriver& d) {
    using magcal::Vec;
    const int top = d.listStart();
    const int h = d.height() - top;
    const int lh = d.getLineHeight();
    const int s = d.sepH();
    const int R = (h - 8 * s) / 2;
    const int cx = R + 6 * s, cy = top + h / 2;

    // The view: the field towards you (w), east to the right (u), v up.
    const magcal::Collector* col = _c->collector();
    const Vec c0 = col && col->count() ? col->roughCentre() : Vec{ 0, 0, 0 };
    const Vec m = sub(_c->raw(), c0), a = _c->accel();
    Vec w = norm(m), dn = norm({ -a.x, -a.y, -a.z });
    Vec u = norm(cross(dn, m));                    // east
    if (len(u) < 0.5f) u = { 0, 1, 0 };
    const Vec v = cross(w, u);

    auto project = [&](Vec p, int& x, int& y) {    // a unit vector in the device's frame
      x = cx + (int)lroundf(R * dot(p, u));
      y = cy - (int)lroundf(R * dot(p, v));
      return dot(p, w);                             // > 0: the near side
    };

    gfx::drawCircle(d, cx, cy, R);
    // The device's own axes as three great circles.
    static const gfx::Dash DOTS = { 0x1, 3 };
    for (int plane = 0; plane < 3; plane++) {
      int px = 0, py = 0;
      float pz = 0;
      for (int k = 0; k <= 48; k++) {
        const float t = k * (6.2831853f / 48);
        const float c = cosf(t), sn = sinf(t);
        const Vec p = plane == 0 ? Vec{ c, sn, 0 } : plane == 1 ? Vec{ c, 0, sn } : Vec{ 0, c, sn };
        int x, y;
        const float z = project(p, x, y);
        if (k) gfx::drawLine(d, px, py, x, y, (z + pz) > 0 ? gfx::DASH_SOLID : DOTS);
        px = x; py = y; pz = z;
      }
    }
    // The device's forward axis, a short tick where it comes out of the ball.
    {
      int x, y;
      if (project({ 1, 0, 0 }, x, y) > 0) d.fillRect(x - 2 * s, y - 2 * s, 4 * s, 4 * s);
    }

    // The readings.
    int n = 0, cells = 0;
    if (col) {
      n = col->count();
      cells = col->coverage();
      for (int i = 0; i < n; i++) {
        int x, y;
        const float z = project(norm(sub(col->point(i), c0)), x, y);
        if (z > 0) d.fillRect(x - s, y - s, 3 * s, 3 * s);
        else d.fillRect(x, y, s, s);
      }
    }
    // The pen: where the next reading lands.
    d.drawRect(cx - 4 * s, cy - 4 * s, 9 * s, 9 * s);

    // Beside it.
    const int tx = cx + R + 10 * s, tw = d.width() - tx - 4 * s;
    int y = top + 4 * s;
    char t[40];
    const Compass::Result res = _c->autoResult();
    if (!done()) {
      d.setCursor(tx, y); d.print("Turn it every way:"); y += lh;
      d.setCursor(tx, y); d.print("over, round, on its"); y += lh;
      d.setCursor(tx, y); d.print("edges, slowly."); y += lh * 3 / 2;
      d.setCursor(tx, y); d.print("Away from metal,"); y += lh;
      d.setCursor(tx, y); d.print("phones, speakers."); y += lh * 2;
      // Coverage: the share of the sphere's 54 cells with a reading.
      const int pct = cells * 100 / magcal::Collector::CELLS;
      snprintf(t, sizeof(t), "Covered %d %%", pct);
      d.setCursor(tx, y); d.print(t); y += lh + 2 * s;
      d.drawRect(tx, y, tw, 6 * s);
      d.fillRect(tx, y, tw * cells / magcal::Collector::CELLS, 6 * s);
      y += 6 * s + lh / 2;
      snprintf(t, sizeof(t), "%d readings", n);
      d.setCursor(tx, y); d.print(t);
      return 40;   // the ball follows the hand; only its rows go to the panel
    }
    if (res == Compass::SAVED) {
      d.setCursor(tx, y); d.print("Calibrated."); y += lh * 3 / 2;
      snprintf(t, sizeof(t), "%.1f %% off the sphere", _c->autoRms() * 100);
      d.setCursor(tx, y); d.print(t); y += lh;
      snprintf(t, sizeof(t), "Field %.0f uT", _c->cal().field);
      d.setCursor(tx, y); d.print(t); y += lh * 2;
      d.setCursor(tx, y); d.print("Enter: again"); y += lh;
      d.setCursor(tx, y); d.print("Back: done");
    } else {
      d.setCursor(tx, y); d.print(res == Compass::POOR ? "A poor fit" : "No fit"); y += lh;
      if (_c->autoRms() >= 0) {
        snprintf(t, sizeof(t), "(%.1f %% off the sphere).", _c->autoRms() * 100);
        d.setCursor(tx, y); d.print(t); y += lh;
      }
      y += lh / 2;
      d.setCursor(tx, y); d.print("Something moved near"); y += lh;
      d.setCursor(tx, y); d.print("it, or metal is close."); y += lh * 2;
      d.setCursor(tx, y); d.print("Enter: again"); y += lh;
      d.setCursor(tx, y); d.print("Back: leave as it was");
    }
    return 1000;
  }

private:
  Compass* _c = nullptr;

  using V = magcal::Vec;
  static V sub(V a, V b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
  static float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  static V cross(V a, V b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
  static float len(V a) { return sqrtf(dot(a, a)); }
  static V norm(V a) { const float l = len(a); return l > 1e-6f ? V{ a.x / l, a.y / l, a.z / l } : V{ 1, 0, 0 }; }
};
