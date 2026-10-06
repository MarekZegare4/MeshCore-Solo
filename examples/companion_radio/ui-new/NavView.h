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

  const int step = d.lineStep();
  int y = d.listStart();
  // A screen 40 characters wide (the 4.2"): the lines down the left, and a
  // compass rose (north up) at the right with an arrow to the target and a
  // mark on the rim where you're heading.
  const bool wide = d.width() >= 40 * d.getCharWidth();

  // Distance — emphasised at size 2.
  d.setTextSize(2);
  if (wide) { d.setCursor(2, y); d.print(dist); }
  else      d.drawTextCentered(cx, y, dist);
  y += d.getLineHeight() + 3;
  d.setTextSize(1);
  if (wide) {
    const int lh = d.getLineHeight(), top = d.listStart();
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
  snprintf(line, sizeof(line), "To:  %d %s", to_deg, geo::bearingCardinal(to_deg));
  d.drawTextLeftAlign(2, y, line); y += step;

  if (cog_valid) snprintf(line, sizeof(line), "Hdg: %d %s", cog_deg, geo::bearingCardinal(cog_deg));
  else           snprintf(line, sizeof(line), "Hdg: --");
  d.drawTextLeftAlign(2, y, line); y += step;

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
