#pragma once
// Shared geographic helpers used by the navigation features (Nearby Nodes,
// Waypoints, trail course-over-ground). Coordinates are fixed-point degrees
// scaled by 1e6 (the same representation used by the GPS provider and the
// contact/trail records). All functions are pure and header-inline.

#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef M_PI
  #define M_PI 3.14159265358979323846
#endif

namespace geo {

// Great-circle distance between two 1e6-scaled lat/lon points, in kilometres.
static inline float haversineKm(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  static const float DEG2RAD = (float)M_PI / 180.0f;
  float la1 = lat1 * (1e-6f * DEG2RAD);
  float la2 = lat2 * (1e-6f * DEG2RAD);
  float dla = (lat2 - lat1) * (1e-6f * DEG2RAD);
  float dlo = (lon2 - lon1) * (1e-6f * DEG2RAD);
  float a   = sinf(dla/2)*sinf(dla/2) + cosf(la1)*cosf(la2)*sinf(dlo/2)*sinf(dlo/2);
  return 6371.0f * 2.0f * asinf(sqrtf(a));
}

// Initial bearing from point 1 to point 2, degrees 0..359 (0 = north).
static inline int bearingDeg(int32_t lat1, int32_t lon1, int32_t lat2, int32_t lon2) {
  static const float DEG2RAD = (float)M_PI / 180.0f;
  float la1 = lat1 * (1e-6f * DEG2RAD);
  float la2 = lat2 * (1e-6f * DEG2RAD);
  float dlo = (lon2 - lon1) * (1e-6f * DEG2RAD);
  float b   = atan2f(sinf(dlo)*cosf(la2),
                     cosf(la1)*sinf(la2) - sinf(la1)*cosf(la2)*cosf(dlo)) * (180.0f / (float)M_PI);
  if (b < 0.0f) b += 360.0f;
  return (int)(b + 0.5f) % 360;
}

// 8-point cardinal label for a bearing in degrees.
static inline const char* bearingCardinal(int deg) {
  static const char* dirs[] = { "N","NE","E","SE","S","SW","W","NW" };
  return dirs[((deg + 22) % 360) / 45];
}

// Human distance string. Metric: "850m" / "2.3km" / "140km". Imperial:
// "850ft" / "2.3mi" / "140mi" (feet below ~1000 ft, then miles). `spaced`
// puts a space before the unit ("850 m"), for value rows with room for it.
static inline void fmtDist(char* buf, int n, float km, bool imperial, bool spaced = false) {
  const char* sp = spaced ? " " : "";
  if (imperial) {
    float ft = km * 3280.84f;
    if (ft < 1000.0f) { snprintf(buf, n, "%d%sft", (int)(ft + 0.5f), sp); return; }
    float mi = km * 0.621371f;
    if (mi < 100.0f)  snprintf(buf, n, "%.1f%smi", mi, sp);
    else              snprintf(buf, n, "%d%smi",  (int)(mi + 0.5f), sp);
  } else {
    if      (km < 1.0f)   snprintf(buf, n, "%d%sm",   (int)(km * 1000 + 0.5f), sp);
    else if (km < 100.0f) snprintf(buf, n, "%.1f%skm", km, sp);
    else                  snprintf(buf, n, "%d%skm",  (int)(km + 0.5f), sp);
  }
}

// Compact age tag for a timestamp, e.g. "12s" / "5m" / "3h" / "2d" — sized to
// sit inline after a name (unlike a full "X ago" sentence). Empty string for
// a genuinely unknown (0) timestamp (callers like NearbyScreen::fmtAge() rely
// on this to show "unknown" for a contact never actually heard from, which a
// clamped "0s ago" would misreport as just-seen). A timestamp slightly ahead
// of `now` (sender/receiver clock skew -- e.g. an incoming message whose
// sender's clock runs a little fast, or ours hasn't synced yet) clamps to
// "0s" instead of going blank, since it did just arrive. Takes `now` rather
// than reading the RTC itself, so this stays a pure function like the rest
// of this file.
static inline void fmtAgeShort(char* buf, int n, uint32_t now, uint32_t lastmod) {
  if (lastmod == 0) { buf[0] = '\0'; return; }
  uint32_t age = (now > lastmod) ? (now - lastmod) : 0;
  if      (age < 60)    snprintf(buf, n, "%us", (unsigned)age);
  else if (age < 3600)  snprintf(buf, n, "%um", (unsigned)(age / 60));
  else if (age < 86400) snprintf(buf, n, "%uh", (unsigned)(age / 3600));
  else                  snprintf(buf, n, "%ud", (unsigned)(age / 86400));
}

// Tag marking a shared waypoint inside a message: "[WAY]<lat>,<lon> <label>".
// A plain {loc} expansion ("<lat>,<lon>") parses too — the tag just adds intent
// and a label, and keeps the text readable on apps/firmware that don't know it.
#define WAYPOINT_MSG_TAG "[WAY]"

// Scan a message for an embedded "lat,lon" location (decimal degrees, the same
// text {loc} emits). If a WAYPOINT_MSG_TAG precedes it, the trailing text is
// taken as the label. Returns true and fills lat/lon (1e6-scaled) on a valid,
// in-range coordinate; label (optional) gets the trimmed tag suffix or "".
static inline bool parseLatLon(const char* text, int32_t& lat_1e6, int32_t& lon_1e6,
                               char* label = nullptr, int label_n = 0) {
  if (label && label_n > 0) label[0] = '\0';
  if (!text) return false;

  const char* tag  = strstr(text, WAYPOINT_MSG_TAG);
  const char* scan = tag ? tag + strlen(WAYPOINT_MSG_TAG) : text;

  for (const char* p = scan; *p; p++) {
    if (*p != '-' && *p != '+' && *p != '.' && !isdigit((unsigned char)*p)) continue;
    char* end = nullptr;
    double la = strtod(p, &end);
    if (end == p) continue;
    if (!memchr(p, '.', end - p)) continue;           // require a decimal point (skip "5,6")
    const char* q = end;
    while (*q == ' ') q++;
    if (*q != ',') { p = end - 1; continue; }
    q++;
    while (*q == ' ') q++;
    char* end2 = nullptr;
    double lo = strtod(q, &end2);
    if (end2 == q) continue;
    if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0) continue;

    lat_1e6 = (int32_t)(la * 1e6 + (la < 0 ? -0.5 : 0.5));   // round in double — float loses ~1m at 1e8
    lon_1e6 = (int32_t)(lo * 1e6 + (lo < 0 ? -0.5 : 0.5));
    if (label && label_n > 0 && tag) {                // label only from a tagged share
      const char* s = end2;
      while (*s == ' ') s++;
      int n = 0;
      while (s[n] && n < label_n - 1) { label[n] = s[n]; n++; }
      while (n > 0 && label[n - 1] == ' ') n--;        // trim trailing spaces
      label[n] = '\0';
    }
    return true;
  }
  return false;
}

// Tag marking a live position share inside a message: "[LOC]<lat>,<lon>".
// Distinct from WAYPOINT_MSG_TAG: a waypoint is a static point-of-interest to
// save, whereas this announces the *sender's own* current position so the
// receiver can update its bearing/track on that node. Both reuse parseLatLon
// for the coordinate, and both stay readable on firmware/apps that don't know
// the tag (it just looks like a coordinate).
#define LOCATION_MSG_TAG "[LOC]"

// True if `text` carries a LOCATION_MSG_TAG share; fills lat/lon (1e6-scaled)
// from the coordinate that follows it. Returns false for plain text, for a
// bare coordinate, or for a [WAY] share — only an explicit [LOC] tag counts,
// so ordinary messages that happen to contain digits don't move anyone's pin.
static inline bool parseLocShare(const char* text, int32_t& lat_1e6, int32_t& lon_1e6) {
  if (!text) return false;
  const char* tag = strstr(text, LOCATION_MSG_TAG);
  if (!tag) return false;
  return parseLatLon(tag + strlen(LOCATION_MSG_TAG), lat_1e6, lon_1e6);
}

}  // namespace geo
