#pragma once
// Shared "navigate to a point" view. The target is just a (lat, lon, label)
// triple, so the same screen serves Waypoints, trail Backtrack and Nearby
// node navigation. No magnetometer: we show two *absolute* bearings — the
// target's bearing and the user's course over ground — and let the user
// compare them ("target 145°, I'm heading 090° → bear right").
//
// Pure render helper; the caller supplies its own GPS fix + COG (from UITask).

#include <helpers/ui/DisplayDriver.h>
#include "../GeoUtils.h"
#include "../ui-core/EtaTracker.h"   // navview::EtaTracker (shared with ui-lvgl)
#include "InfoKit.h"                 // info::rose()

namespace navview {

static const int AHEAD_DEG = 12;   // within this of straight on: "ahead"

// own_valid : is there a usable GPS fix for the device right now
// cog_valid : is there a stable course-over-ground (UITask::currentCourse)
// eta       : optional closing-speed/ETA tracker; when supplied a third line is
//             shown with the approach speed and estimated time of arrival
inline void draw(DisplayDriver& d,
                 bool own_valid, int32_t own_lat, int32_t own_lon,
                 int32_t tgt_lat, int32_t tgt_lon,
                 const char* label,
                 bool cog_valid, int cog_deg,
                 bool imperial,
                 EtaTracker* eta = nullptr) {
  const int cx  = d.width() / 2;
  const int hdr = d.headerH();

  // Title bar: label, inverted (matches the Nearby/discover detail style).
  d.drawInvertedHeader((label && label[0]) ? label : "Navigate");

  if (!own_valid) {
    d.drawTextCentered(cx, hdr + (d.height() - hdr) / 2 - d.getLineHeight(), "No GPS fix");
    return;
  }

  int   to_deg = geo::bearingDeg(own_lat, own_lon, tgt_lat, tgt_lon);
  float dist_km = geo::haversineKm(own_lat, own_lon, tgt_lat, tgt_lon);
  char dist[12];
  geo::fmtDist(dist, sizeof(dist), dist_km, imperial);

  const int lh = d.getLineHeight(), cw = d.getCharWidth();
  int step = d.lineStep();
  int y = d.listStart();
  // A screen 40 characters wide (the 4.2"): the lines down the left, and a
  // compass rose (north up) at the right with an arrow to the target and a
  // mark on the rim where you're heading.
  const bool wide = d.width() >= 40 * cw;

  // The distance at size 2, then `lines` under it (turn / To or To / Hdg, and
  // the ETA). On a short screen (the 64 px OLED) the last would run off the
  // bottom: the gap under the distance goes first, then the pitch of the lines.
  const int lines = eta ? 3 : 2;
  d.setTextSize(2);
  const int dist_h = d.getLineHeight(), dw = d.getTextWidth(dist);
  d.setTextSize(1);
  int dist_gap = 3;
  int over = y + dist_h + dist_gap + (lines - 1) * step + lh - d.height();
  if (over > 0) { const int g = over < dist_gap ? over : dist_gap; dist_gap -= g; over -= g; }
  if (over > 0) { step -= (over + lines - 2) / (lines - 1); if (step < lh) step = lh; }

  // A narrow screen: a dial, the lines down the left. With a course it's a
  // turn dial (the top is where you're heading, turn until the arrow points
  // up); without, the compass rose (north up). Beside the lines, or under them
  // where the screen is tall (the portrait e-ink) and that makes it larger.
  // Too small for one (a very short screen): the text alone, as before.
  int dial_r = 0, dial_x = 0, dial_y = 0;
  if (!wide) {
    const int top = d.listStart();
    const int tw = (dw > 12 * cw ? dw : 12 * cw) + 4;   // the widest of the lines at the left
    int r_side = (d.height() - top) / 2 - lh / 2 - 1;   // room over the ring for the rose's N
    if (r_side > (d.width() - tw) / 2 - 1) r_side = (d.width() - tw) / 2 - 1;
    const int text_bottom = y + dist_h + dist_gap + lines * step;
    int r_below = (d.height() - text_bottom) / 2 - lh / 2 - 2;
    if (r_below > d.width() / 2 - 3) r_below = d.width() / 2 - 3;
    if (r_below > 50) r_below = 50;
    if (r_below >= 14 && r_below > r_side) {
      dial_r = r_below;
      dial_x = d.width() / 2;
      dial_y = text_bottom + (d.height() - text_bottom) / 2 + lh / 4;
    } else if (r_side >= 14) {
      dial_r = r_side;
      dial_x = d.width() - r_side - 2;
      dial_y = top + (d.height() - top) / 2 + lh / 4;
    }
  }
  int turn = 0;   // -180..179 degrees to turn, right positive; with a course
  if (cog_valid) turn = ((to_deg - cog_deg + 540) % 360) - 180;
  const bool ahead = cog_valid && turn <= AHEAD_DEG && turn >= -AHEAD_DEG;

  // Distance — emphasised at size 2.
  d.setTextSize(2);
  if (wide || dial_r) { d.setCursor(2, y); d.print(dist); }
  else                d.drawTextCentered(cx, y, dist);
  y += dist_h + dist_gap;
  d.setTextSize(1);
  if (wide) {
    const int top = d.listStart();
    const int r = (d.height() - top) / 2 - lh;
    const int rx = d.width() - r - lh, ry = top + (d.height() - top) / 2 + lh / 4;
    info::rose(d, rx, ry, r, to_deg);
    if (cog_valid) {   // a small filled triangle outside the rim, pointing in
      const float a = cog_deg * (float)M_PI / 180.0f, sa = sinf(a), ca = cosf(a);
      const int s = miniIconScale(d) * 2;
      for (int k = 0; k <= 3 * s; k++) {
        const float rr = r + 2 + k, half = k / 2.0f;
        const int px = rx + (int)lroundf(rr * sa), py = ry - (int)lroundf(rr * ca);
        info::line(d, px - (int)lroundf(half * ca), py - (int)lroundf(half * sa),
                      px + (int)lroundf(half * ca), py + (int)lroundf(half * sa));
      }
    }
  }

  char line[20];
  if (dial_r) {
    if (cog_valid) info::turnDial(d, dial_x, dial_y, dial_r, turn, ahead);
    else           info::rose(d, dial_x, dial_y, dial_r, to_deg);
  }
  if (dial_r && cog_valid) {   // how far to turn says what "To" and "Hdg" leave to be subtracted
    if (ahead) snprintf(line, sizeof(line), "Straight on");
    else       snprintf(line, sizeof(line), "Turn %c %d", turn > 0 ? 'R' : 'L', turn > 0 ? turn : -turn);
    d.drawTextLeftAlign(2, y, line); y += step;
  }
  snprintf(line, sizeof(line), "To:  %d %s", to_deg, geo::bearingCardinal(to_deg));
  d.drawTextLeftAlign(2, y, line); y += step;

  if (!(dial_r && cog_valid)) {
    if (cog_valid) snprintf(line, sizeof(line), "Hdg: %d %s", cog_deg, geo::bearingCardinal(cog_deg));
    else           snprintf(line, sizeof(line), "Hdg: --");
    d.drawTextLeftAlign(2, y, line); y += step;
  }

  // Optional ETA / closing-speed line. Approaching only — a receding or
  // stationary target shows "ETA: --" rather than a misleading time.
  if (eta) {
    eta->update(dist_km, millis());
    char eta_s[12];
    if (eta->eta(dist_km, eta_s, sizeof(eta_s))) {
      float spd = imperial ? eta->closing_kmh * 0.621371f : eta->closing_kmh;
      snprintf(line, sizeof(line), "ETA: %s @%d%s", eta_s, (int)(spd + 0.5f), imperial ? "mph" : "kmh");
    } else {
      snprintf(line, sizeof(line), "ETA: --");
    }
    d.drawTextLeftAlign(2, y, line); y += step;
  }
}

}  // namespace navview
