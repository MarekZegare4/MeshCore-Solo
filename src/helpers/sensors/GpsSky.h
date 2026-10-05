#pragma once
// What the GPS receiver sees, for a status screen: every satellite in view
// (constellation, elevation, azimuth, signal) and which ones the fix uses,
// the dilutions of precision, fix type, speed and course. Fed the same NMEA
// characters MicroNMEA gets (which only keeps the fix itself), parsing
// GSV / GSA / GGA / RMC from any talker (GP, GL, GA, GB/BD, GQ, GN).
//
// Opt-in per board (-D GPS_SKYVIEW, used by MicroNMEALocationProvider), so
// boards without a screen for it don't carry the ~1.5 KB table.

#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

class GpsSky {
public:
  enum : uint8_t { SYS_GPS, SYS_GLONASS, SYS_GALILEO, SYS_BEIDOU, SYS_QZSS, SYS_SBAS, SYS_OTHER, SYS_COUNT };
  struct Sat {
    uint16_t prn;
    uint8_t  sys;
    int8_t   elev;       // degrees, -1 = not reported
    int16_t  azim;       // degrees, -1 = not reported
    int8_t   snr;        // C/N0 in dB-Hz, -1 = not tracked
    uint32_t seen_ms;    // last GSV mentioning it
    uint32_t used_ms;    // last GSA listing it in the fix (0 = never)
  };
  static const int MAX_SATS = 64;
  static const uint32_t SAT_TIMEOUT_MS = 10000;   // gone from GSV this long: out of view
  static const uint32_t USED_TIMEOUT_MS = 4000;

  Sat      sats[MAX_SATS];
  int      count = 0;
  uint8_t  fix_quality = 0;   // GGA: 0 none, 1 GPS, 2 DGPS, 4/5 RTK, 6 dead reckoning
  uint8_t  fix_mode = 0;      // GSA: 0 unknown, 1 no fix, 2 2D, 3 3D
  uint8_t  sats_used = 0;     // GGA
  float    pdop = 0, hdop = 0, vdop = 0;   // 0 = unknown
  float    alt_m = 0;
  bool     alt_valid = false;
  float    speed_kmh = 0, course_deg = 0;
  bool     rmc_valid = false;   // RMC status A
  uint8_t  utc_h = 0, utc_m = 0, utc_s = 0;
  bool     utc_valid = false;
  uint32_t sentences = 0, bad_sentences = 0;
  uint32_t last_ms = 0;       // last good sentence
  uint32_t start_ms = 0;      // first sentence since reset(): time to first fix counts from here
  uint32_t ttff_ms = 0;       // 0 = no fix yet

  // The receiver was (re)started: forget everything, restart the TTFF clock.
  void reset() {
    count = 0;
    fix_quality = fix_mode = sats_used = 0;
    pdop = hdop = vdop = 0;
    alt_valid = rmc_valid = utc_valid = false;
    start_ms = ttff_ms = 0;
    _len = 0;
  }

  // The receiver was switched off: a search that never ended stops counting
  // (a fix already found keeps its time-to-first-fix to look at).
  void halt() { if (!ttff_ms) start_ms = 0; }

  void feed(char c) {
    if (c == '$') { _len = 0; _buf[_len++] = c; return; }
    if (_len == 0) return;
    if (c == '\r' || c == '\n') { _buf[_len] = '\0'; line(); _len = 0; return; }
    if (_len < sizeof(_buf) - 1) _buf[_len++] = c;
    else _len = 0;   // overlong: drop it
  }

  bool used(const Sat& s) const { return s.used_ms && millis() - s.used_ms < USED_TIMEOUT_MS; }
  bool hasFix() const { return fix_quality > 0 || fix_mode >= 2; }

  // Drop satellites no longer reported (call before reading the table).
  void expire() {
    uint32_t now = millis();
    int k = 0;
    for (int i = 0; i < count; i++) if (now - sats[i].seen_ms < SAT_TIMEOUT_MS) sats[k++] = sats[i];
    count = k;
  }

  static char sysLetter(uint8_t sys) {
    static const char L[SYS_COUNT] = { 'G', 'R', 'E', 'C', 'J', 'S', '?' };
    return sys < SYS_COUNT ? L[sys] : '?';
  }
  static const char* sysName(uint8_t sys) {
    static const char* const N[SYS_COUNT] = { "GPS", "GLONASS", "Galileo", "BeiDou", "QZSS", "SBAS", "Other" };
    return sys < SYS_COUNT ? N[sys] : "Other";
  }

private:
  char   _buf[100];
  size_t _len = 0;
  uint32_t _gsa_ms = 0;

  static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  }

  static uint8_t talkerSys(const char* t) {
    if (t[0] == 'G' && t[1] == 'P') return SYS_GPS;
    if (t[0] == 'G' && t[1] == 'L') return SYS_GLONASS;
    if (t[0] == 'G' && t[1] == 'A') return SYS_GALILEO;
    if ((t[0] == 'G' && t[1] == 'B') || (t[0] == 'B' && t[1] == 'D')) return SYS_BEIDOU;
    if ((t[0] == 'G' && t[1] == 'Q') || (t[0] == 'Q' && t[1] == 'Z')) return SYS_QZSS;
    return SYS_OTHER;   // GN: mixed, decided per PRN
  }
  // NMEA 4.1 system ID (GSA field 18).
  static uint8_t idSys(int id) {
    switch (id) {
      case 1: return SYS_GPS; case 2: return SYS_GLONASS; case 3: return SYS_GALILEO;
      case 4: return SYS_BEIDOU; case 5: return SYS_QZSS; default: return SYS_OTHER;
    }
  }
  // The classic NMEA numbering, when the talker doesn't say.
  static uint8_t prnSys(int prn) {
    if (prn >= 1 && prn <= 32) return SYS_GPS;
    if (prn >= 33 && prn <= 64) return SYS_SBAS;
    if (prn >= 65 && prn <= 96) return SYS_GLONASS;
    if (prn >= 193 && prn <= 200) return SYS_QZSS;
    if (prn >= 201 && prn <= 263) return SYS_BEIDOU;
    if (prn >= 301 && prn <= 336) return SYS_GALILEO;
    return SYS_OTHER;
  }

  Sat* find(uint8_t sys, int prn) {
    for (int i = 0; i < count; i++) if (sats[i].prn == prn && sats[i].sys == sys) return &sats[i];
    return nullptr;
  }

  void line() {
    // "$TTSSS,...*CS": checksum over everything between '$' and '*'.
    char* star = strrchr(_buf, '*');
    if (!star || star - _buf < 7) { bad_sentences++; return; }
    uint8_t cs = 0;
    for (char* p = _buf + 1; p < star; p++) cs ^= (uint8_t)*p;
    int h = hexVal(star[1]), l = hexVal(star[2]);
    if (h < 0 || l < 0 || cs != (uint8_t)(h << 4 | l)) { bad_sentences++; return; }
    *star = '\0';

    char* f[24];
    int nf = 0;
    for (char* p = _buf + 1; nf < 24;) {
      f[nf++] = p;
      char* comma = strchr(p, ',');
      if (!comma) break;
      *comma = '\0';
      p = comma + 1;
    }
    if (strlen(f[0]) != 5) { bad_sentences++; return; }
    sentences++;
    uint32_t now = millis();
    last_ms = now;
    if (!start_ms) start_ms = now ? now : 1;
    const char* type = f[0] + 2;
    uint8_t tsys = talkerSys(f[0]);

    if (!strcmp(type, "GSV")) {
      // total, index, in view, then (prn, elevation, azimuth, snr) x up to 4 [, signal id]
      for (int g = 4; g + 3 < nf; g += 4) {   // whole groups only: a lone trailing field is the signal id
        if (!f[g][0]) continue;
        int prn = atoi(f[g]);
        if (prn <= 0) continue;
        uint8_t sys = tsys == SYS_OTHER ? prnSys(prn) : (tsys == SYS_GPS && prn >= 33 && prn <= 64 ? SYS_SBAS : tsys);
        Sat* s = find(sys, prn);
        if (!s) {
          if (count >= MAX_SATS) { expire(); if (count >= MAX_SATS) continue; }
          s = &sats[count++];
          memset(s, 0, sizeof(*s));
          s->prn = (uint16_t)prn;
          s->sys = sys;
        }
        s->elev = f[g + 1][0] ? (int8_t)atoi(f[g + 1]) : -1;
        s->azim = f[g + 2][0] ? (int16_t)atoi(f[g + 2]) : -1;
        s->snr  = f[g + 3][0] ? (int8_t)atoi(f[g + 3]) : -1;
        s->seen_ms = now;
      }
    } else if (!strcmp(type, "GSA")) {
      // mode, fix (1/2/3), 12 PRNs, PDOP, HDOP, VDOP [, system id]
      _gsa_ms = now;
      if (nf > 2) fix_mode = (uint8_t)atoi(f[2]);
      if (nf > 15 && f[15][0]) pdop = atof(f[15]);
      if (nf > 16 && f[16][0]) hdop = atof(f[16]);
      if (nf > 17 && f[17][0]) vdop = atof(f[17]);
      uint8_t sys = nf > 18 && f[18][0] ? idSys(atoi(f[18])) : tsys;
      for (int i = 3; i <= 14 && i < nf; i++) {
        if (!f[i][0]) continue;
        int prn = atoi(f[i]);
        Sat* s = find(sys == SYS_OTHER ? prnSys(prn) : sys, prn);
        if (!s && sys == SYS_GPS) s = find(SYS_SBAS, prn);
        if (s) s->used_ms = now;
      }
    } else if (!strcmp(type, "GGA")) {
      // time, lat, N, lon, E, quality, used, HDOP, altitude, M, ...
      if (nf > 6) fix_quality = (uint8_t)atoi(f[6]);
      if (nf > 7) sats_used = (uint8_t)atoi(f[7]);
      if (nf > 8 && f[8][0] && now - _gsa_ms > 3000) hdop = atof(f[8]);   // GSA's is finer
      alt_valid = nf > 9 && f[9][0] && fix_quality > 0;
      if (alt_valid) alt_m = atof(f[9]);
      if (fix_quality > 0 && !ttff_ms) ttff_ms = now - start_ms + 1;
    } else if (!strcmp(type, "RMC")) {
      // time, status, lat, N, lon, E, speed (knots), course, date, ...
      if (nf > 1 && strlen(f[1]) >= 6) {
        utc_h = (uint8_t)((f[1][0] - '0') * 10 + f[1][1] - '0');
        utc_m = (uint8_t)((f[1][2] - '0') * 10 + f[1][3] - '0');
        utc_s = (uint8_t)((f[1][4] - '0') * 10 + f[1][5] - '0');
        utc_valid = true;
      }
      rmc_valid = nf > 2 && f[2][0] == 'A';
      speed_kmh = nf > 7 && f[7][0] ? atof(f[7]) * 1.852f : 0;
      course_deg = nf > 8 && f[8][0] ? atof(f[8]) : 0;
    }
  }
};
