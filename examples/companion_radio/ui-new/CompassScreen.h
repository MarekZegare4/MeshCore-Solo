#pragma once
// Tools › Compass. A standalone heading view: shows the device's course over
// ground (derived from GPS movement — there is no magnetometer) as a horizontal
// scrolling tape (N..E..S..W) under a fixed travel-direction marker, plus a
// large numeric degrees + cardinal readout. When standing still the heading is
// undefined, so it shows a hint to move.
//
// A linear tape (no trig) reads well in the short vertical space of the OLED,
// where a circular dial leaves only a few-pixel needle.
//
// Reuses the UI Core's COG ring (currentCourse) — works whether or not a trail is
// being recorded.

#include "../GeoUtils.h"
#include "icons.h"   // miniIconScale() — keeps the tape ticks/pointer legible on e-ink

class CompassScreen : public UIScreen {
  UITask* _task;

  bool gpsValid() const { int32_t lat, lon; return _task->currentLocation(lat, lon); }

  static const char* cardinalLetter(int deg) {
    switch (deg) { case 0: return "N"; case 90: return "E";
                   case 180: return "S"; case 270: return "W"; }
    return "";
  }

public:
  CompassScreen(UITask* task) : _task(task) {}
  void onShow() override {}

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Compass");

    const int cx  = display.width() / 2;
    const int ch  = display.getLineHeight();
    const int cw  = display.getCharWidth();
    // Reserve a size-2 row at the bottom for the numeric readout.
    display.setTextSize(2);
    const int bigH = display.getLineHeight();
    display.setTextSize(1);
    const int top      = display.listStart();
    const int readout_y = display.height() - bigH - 1;   // size-2 readout baseline
    const int mid      = (top + readout_y) / 2;

    if (!gpsValid()) {
      display.drawTextCentered(cx, mid - ch / 2, "No GPS fix");
      return 1000;
    }

    int cog;
    bool have = _task->currentCourse(cog);

    if (!have) {
      display.drawTextCentered(cx, mid - ch, "move to");
      display.drawTextCentered(cx, mid,      "set heading");
      return 1000;
    }

    // ── heading tape ─────────────────────────────────────────────────────────
    // Centre of the tape = current course; bearings spread left/right at a fixed
    // pixels-per-degree. HALF_SPAN degrees reach each edge.
    const int area_x = 2;
    const int area_w = display.width() - 4;
    const float HALF_SPAN = 80.0f;                 // degrees visible to each side
    const float ppd = (area_w / 2.0f) / HALF_SPAN;

    // Tick/pointer sizes are authored at 1x and scaled by the font (same factor
    // the mini-icon framework uses), so they stay legible next to the 2x
    // cardinal labels and numeric readout on landscape e-ink.
    const int s  = miniIconScale(display);
    const int sh = display.sepH();        // baseline thickness (2px landscape e-ink, else 1)
    const int pointerH_units = 6;
    const int pointerH  = pointerH_units * s;
    const int tickMajor = 6 * s;
    const int tickMinor = 3 * s;
    const int blockH    = pointerH + sh + tickMajor + 2 + ch;
    const int block_top = top + ((readout_y - top) - blockH) / 2;
    const int line_y    = block_top + pointerH;     // scale baseline
    const int label_y   = line_y + sh + tickMajor + 2;
    const int x_lo = area_x, x_hi = area_x + area_w;

    // Scale baseline.
    display.fillRect(area_x, line_y, area_w, sh);

    // Ticks + cardinal labels every 30°; majors (N/E/S/W) are taller + labelled.
    for (int deg = 0; deg < 360; deg += 30) {
      int delta = deg - cog;
      while (delta >  180) delta -= 360;
      while (delta < -180) delta += 360;
      int x = cx + (int)(delta * ppd);
      if (x < x_lo || x > x_hi) continue;
      bool major = (deg % 90 == 0);
      display.fillRect(x - s / 2, line_y + sh, s, major ? tickMajor : tickMinor);
      if (major) {
        const char* letter = cardinalLetter(deg);
        display.setCursor(x - cw / 2, label_y);
        display.print(letter);
      }
    }

    // Fixed travel-direction pointer: a downward triangle whose tip touches the
    // baseline at screen centre (your current course always sits under it).
    // Each of the pointerH_units logical rows is drawn as an s-pixel-tall strip
    // — the same block-scaling miniIconDraw uses for bitmap icons.
    for (int i = 0; i < pointerH_units; i++) {
      int half = (pointerH_units - 1 - i) * s;
      display.fillRect(cx - half, block_top + i * s, 2 * half + 1, s);
    }

    // ── numeric readout ──────────────────────────────────────────────────────
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %s", cog, geo::bearingCardinal(cog));
    display.setTextSize(2);
    display.drawTextCentered(cx, readout_y, buf);
    display.setTextSize(1);
    return 1000;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) { _task->leaveTool(); return true; }
    return true;
  }
};
