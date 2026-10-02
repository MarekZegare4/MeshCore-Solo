#pragma once
// What the GPS receiver sees (-D GPS_SKYVIEW), drawn by Status › Sky in two
// views swapped with Enter:
//   Sky    -- a polar plot (the rim is the horizon, the centre straight up,
//             north at the top): a filled dot is a satellite the fix uses, a
//             hollow one is tracked but unused, a single pixel is in view with
//             no signal. Beside it: fix, used / in view, HDOP, PDOP and the
//             time to first fix (or how long it's been looking).
//   Signal -- a bar per satellite, its height the signal (C/N0, 50 dB-Hz =
//             full), filled when used; a dotted line marks 30 dB-Hz (good).
//             The top line counts used / in view per constellation.
//
// The data is helpers/sensors/GpsSky.h, fed the board's NMEA by
// MicroNMEALocationProvider (a made-up sky in the simulator: GpsSkySim.h).
// The GPS is kept awake while the Sky tab is open (UITask's keep-awake list).

#include <math.h>
#include <helpers/sensors/GpsSky.h>
#ifdef SIM_PLATFORM
  #include <helpers/sensors/GpsSkySim.h>
#else
  #include <helpers/sensors/MicroNMEALocationProvider.h>
#endif
#include "icons.h"   // miniIconScale()
#include "InfoKit.h" // info::circle()

namespace skyview {

  // The board's location provider is a MicroNMEALocationProvider wherever
  // GPS_SKYVIEW is set (the flag's contract, as on the L2).
  inline GpsSky* sky() {
#ifdef SIM_PLATFORM
    return gpsSkySim();
#else
    LocationProvider* p = sensors.getLocationProvider();
    return p ? &((MicroNMEALocationProvider*)p)->sky() : nullptr;
#endif
  }

  inline void dot(DisplayDriver& d, int x, int y) { d.fillRect(x, y, 1, 1); }

  inline int usedCount(const GpsSky& g) {
    int n = 0;
    for (int i = 0; i < g.count; i++) if (g.used(g.sats[i])) n++;
    return n;
  }

  inline void renderSky(DisplayDriver& d, GpsSky& g, int top) {
    const int lh = d.getLineHeight();
    const int cw = d.getCharWidth();
    const int s = miniIconScale(d);
    // A wide screen sets the numbers beside the plot, a tall one (portrait
    // e-ink) under it, the plot then as wide as the screen.
    const bool stacked = d.height() - top > d.width();
    const int r = stacked ? (d.width() - 1) / 2 - 1 : (d.height() - 1 - top) / 2 - 1;
    const int cx = stacked ? d.width() / 2 : r + 1;
    const int cy = top + (stacked ? lh / 2 : 0) + r + 1;

    info::circle(d, cx, cy, r);         // the horizon
    info::circle(d, cx, cy, r / 2, 2);  // 45 degrees up, dotted
    dot(d, cx, cy);                  // straight up
    d.setColor(DisplayDriver::DARK);   // north, on a gap in the rim
    d.fillRect(cx - cw / 2 - 1, cy - r - lh / 2, cw + 2, lh);
    d.setColor(DisplayDriver::LIGHT);
    d.setCursor(cx - cw / 2, cy - r - lh / 2);
    d.print("N");

    for (int pass = 0; pass < 2; pass++) {   // the used ones last, on top
      for (int i = 0; i < g.count; i++) {
        const GpsSky::Sat& sat = g.sats[i];
        bool used = g.used(sat);
        if (sat.elev < 0 || sat.azim < 0 || used != (pass == 1)) continue;
        float rr = r * (90 - sat.elev) / 90.0f;
        float a = sat.azim * (float)M_PI / 180.0f;
        int x = cx + (int)lroundf(rr * sinf(a)), y = cy - (int)lroundf(rr * cosf(a));
        if (used) {
          d.fillRect(x - s, y - s, 2 * s + 1, 2 * s + 1);
        } else if (sat.snr >= 0) {
          d.setColor(DisplayDriver::DARK);   // clear inside, over a ring
          d.fillRect(x - s, y - s, 2 * s + 1, 2 * s + 1);
          d.setColor(DisplayDriver::LIGHT);
          d.drawRect(x - s, y - s, 2 * s + 1, 2 * s + 1);
        } else {
          dot(d, x, y);
        }
      }
    }

    // The numbers, right of the plot (or under it).
    const int x0 = stacked ? 1 : 2 * r + 6, step = lh + 1;
    int y = stacked ? cy + r + lh / 2 + 2 : top;
    char buf[24];
    d.setCursor(x0, y);
    d.print(g.fix_mode == 3 ? "3D fix" : g.hasFix() ? "2D fix" : "No fix");
    y += step;
    snprintf(buf, sizeof(buf), "Sats %d/%d", usedCount(g), g.count);
    d.setCursor(x0, y); d.print(buf);
    y += step;
    if (g.hdop > 0 && g.hdop < 99) snprintf(buf, sizeof(buf), "HDOP %.1f", g.hdop);
    else snprintf(buf, sizeof(buf), "HDOP -");
    d.setCursor(x0, y); d.print(buf);
    y += step;
    if (g.pdop > 0 && g.pdop < 99) snprintf(buf, sizeof(buf), "PDOP %.1f", g.pdop);
    else snprintf(buf, sizeof(buf), "PDOP -");
    d.setCursor(x0, y); d.print(buf);
    y += step;
    if (g.ttff_ms) snprintf(buf, sizeof(buf), "TTFF %lus", (unsigned long)(g.ttff_ms / 1000));
    else if (g.start_ms) snprintf(buf, sizeof(buf), "Wait %lus", (unsigned long)((millis() - g.start_ms) / 1000));
    else buf[0] = 0;
    d.setCursor(x0, y); d.print(buf);
  }

  inline void renderSignal(DisplayDriver& d, GpsSky& g, int top) {
    const int lh = d.getLineHeight();
    const int W = d.width();

    // Used / in view per constellation, the ones with any in view.
    char line[40];
    int o = 0;
    for (uint8_t sys = 0; sys < GpsSky::SYS_COUNT && o < (int)sizeof(line) - 8; sys++) {
      int seen = 0, used = 0;
      for (int i = 0; i < g.count; i++) {
        if (g.sats[i].sys != sys) continue;
        seen++;
        if (g.used(g.sats[i])) used++;
      }
      if (seen) o += snprintf(line + o, sizeof(line) - o, "%s%c%d/%d", o ? " " : "", GpsSky::sysLetter(sys), used, seen);
    }
    d.drawTextEllipsized(0, top, W, o ? line : "None in view");

    // Used first, then the strongest.
    int idx[GpsSky::MAX_SATS];
    int n = g.count;
    for (int i = 0; i < n; i++) idx[i] = i;
    auto key = [&](int i) { return (g.used(g.sats[i]) ? 100 : 0) + g.sats[i].snr; };
    for (int i = 1; i < n; i++) {   // insertion sort, <= 64
      int v = idx[i], j = i - 1;
      while (j >= 0 && key(idx[j]) < key(v)) { idx[j + 1] = idx[j]; j--; }
      idx[j + 1] = v;
    }
    int fit = (W + 1) / 3;   // bars at least 2 px wide, 1 px apart
    if (n > fit) n = fit;
    if (!n) return;
    int bw = (W + 1) / n - 1;
    if (bw > 8) bw = 8;

    const int y0 = top + lh + 3, y1 = d.height() - 1, H = y1 - y0;
    for (int x = 0; x < W; x += 3) dot(d, x, y1 - H * 30 / 50);   // 30 dB-Hz
    for (int k = 0; k < n; k++) {
      const GpsSky::Sat& sat = g.sats[idx[k]];
      int x = k * (bw + 1);
      int snr = sat.snr > 50 ? 50 : sat.snr;
      int h = snr < 0 ? 1 : (H * snr / 50 > 2 ? H * snr / 50 : 2);
      if (g.used(sat) || h <= 2) d.fillRect(x, y1 - h + 1, bw, h);
      else {
        d.setColor(DisplayDriver::DARK);   // over the dotted line
        d.fillRect(x, y1 - h + 1, bw, h);
        d.setColor(DisplayDriver::LIGHT);
        d.drawRect(x, y1 - h + 1, bw, h);
      }
    }
  }

  // The sky (or, with `signal`, the bars) under `top`, or why there's none.
  // Returns the ms until the next redraw.
  inline int render(DisplayDriver& d, int top, bool signal, bool gps_on) {
    const int cx = d.width() / 2, lh = d.getLineHeight();
    GpsSky* g = sky();
    if (!g) {
      d.drawTextCentered(cx, top + lh, "No GPS data");
      return 1000;
    }
    g->expire();
    if (!g->sentences || millis() - g->last_ms > 5000) {
      d.drawTextCentered(cx, top + lh, gps_on ? "Waiting for GPS" : "GPS is off");
      if (!gps_on) { d.drawTextCentered(cx, top + 2 * lh + 2, "Enter on GPS tab"); return 1000; }
      return drawLoadingDots(d, cx, top + 3 * lh + 4);
    }
    if (signal) renderSignal(d, *g, top);
    else renderSky(d, *g, top);
    return 1000;
  }

}  // namespace skyview
