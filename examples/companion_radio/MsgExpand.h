#pragma once
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cstdint>

// Expands {loc} and {time} placeholders in tmpl into out (out_len bytes).
//   lat/lon   — GPS coordinates; gps_valid must be true for {loc} to show coords
//   utc_ts    — unix timestamp from RTC (0 or <1e9 = no valid time)
//   tz_hours  — local timezone offset from UTC (-12..+14)
inline void expandMsg(const char* tmpl, char* out, int out_len,
                      double lat, double lon, bool gps_valid,
                      uint32_t utc_ts, int8_t tz_hours) {
  int oi = 0;
  const char* p = tmpl;
  while (*p && oi < out_len - 1) {
    if (strncmp(p, "{loc}", 5) == 0) {
      char lb[32];
      if (gps_valid)
        snprintf(lb, sizeof(lb), "%.5f,%.5f", lat, lon);
      else
        strcpy(lb, "no GPS");
      int ll = strlen(lb);
      if (oi + ll < out_len - 1) { memcpy(out + oi, lb, ll); oi += ll; }
      p += 5;
    } else if (strncmp(p, "{time}", 6) == 0) {
      if (utc_ts > 1000000000UL) {
        uint32_t local_ts = utc_ts + (int32_t)tz_hours * 3600;
        time_t t = (time_t)local_ts;
        struct tm* ti = gmtime(&t);
        char tb[8];
        snprintf(tb, sizeof(tb), "%02d:%02d", ti->tm_hour, ti->tm_min);
        int tl = strlen(tb);
        if (oi + tl < out_len - 1) { memcpy(out + oi, tb, tl); oi += tl; }
      }
      p += 6;
    } else {
      out[oi++] = *p++;
    }
  }
  out[oi] = '\0';
}
