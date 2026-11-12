#pragma once
// A GPX file into a planned route (../PlannedRoute.h), on the device: the L2
// reads them off its SD card. The same result as the site's Route card and
// tools/route.py: every track segment joined in order (else the route's
// points), repeats dropped, then simplified to PlannedRoutes::MAX_POINTS with
// Douglas-Peucker by rank -- the bends stay, the straight stretches go.
//
// The file is read a block at a time and only the tags that matter are looked
// at (<trkpt>, <rtept>, their <ele>, the first <name> of a <trk> / <rte>), so a
// file of any size passes; the points it holds are kept in PSRAM, up to
// MAX_IN. Needs stdio (FILE*): the L2's card, the sim's disk.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "../PlannedRoute.h"

namespace gpximport {

static const int MAX_IN = 200000;   // points read from one file; past that, the rest is left

enum Result : uint8_t { OK, NO_FILE, NO_POINTS, NO_MEMORY, NO_SLOT, WRITE_FAILED };

inline const char* message(Result r) {
  switch (r) {
    case OK:           return "Route imported";
    case NO_FILE:      return "Can't read the file";
    case NO_POINTS:    return "No track or route in it";
    case NO_MEMORY:    return "Too big to import";
    case NO_SLOT:      return "Route slots full: delete one";
    default:           return "Can't write the route";
  }
}

template <class T> static T* bigAlloc(size_t n) {
#if defined(ESP32)
  if (void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return (T*)p;
#endif
  return (T*)malloc(n * sizeof(T));
}

struct Pt { int32_t lat, lon; int16_t ele; uint8_t rte; };

// The value of attribute `name` in a tag's text (`tag`, up to `end`), as a
// number; false when it isn't there.
static bool attr(const char* tag, const char* end, const char* name, double& out) {
  const size_t nl = strlen(name);
  for (const char* p = tag; p + nl + 2 < end; p++) {
    if (memcmp(p, name, nl) != 0 || (p > tag && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '\n' && p[-1] != '\r')) continue;
    const char* q = p + nl;
    while (q < end && (*q == ' ' || *q == '=')) q++;
    if (q >= end || (*q != '"' && *q != '\'')) continue;
    out = strtod(q + 1, nullptr);
    return true;
  }
  return false;
}

// Reads the GPX into a PSRAM array; *n points, *name the track's name (if any).
static Result readGpx(FILE* f, Pt*& pts, int& n, char* name, size_t name_n) {
  n = 0;
  name[0] = '\0';
  pts = bigAlloc<Pt>(MAX_IN);
  if (!pts) return NO_MEMORY;
  static const size_t BUF = 4096;
  char* buf = (char*)malloc(BUF + 1);
  if (!buf) { free(pts); pts = nullptr; return NO_MEMORY; }
  size_t have = 0;
  bool in_pt = false, in_ele = false, in_trk = false, want_name = false;
  int ntrk = 0, nrte = 0;
  for (;;) {
    size_t got = fread(buf + have, 1, BUF - have, f);
    have += got;
    buf[have] = '\0';
    size_t i = 0;
    while (i < have) {
      char* lt = (char*)memchr(buf + i, '<', have - i);
      if (!lt) { if (!in_ele && !want_name) i = have; break; }   // a value running past the block: kept
      // Text before a tag: an <ele> or <name> value being read.
      if (in_ele && n > 0) {
        char* e = buf + i;
        if (lt > e) { double v = strtod(e, nullptr); pts[n - 1].ele = (int16_t)(v > 32767 ? 32767 : v < -32767 ? -32767 : lround(v)); }
        in_ele = false;
      }
      if (want_name) {
        size_t l = lt - (buf + i);
        while (l && (buf[i] == ' ' || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t')) { i++; l--; }
        while (l && (buf[i + l - 1] == ' ' || buf[i + l - 1] == '\n' || buf[i + l - 1] == '\r')) l--;
        if (l >= name_n) l = name_n - 1;
        memcpy(name, buf + i, l);
        name[l] = '\0';
        want_name = false;
      }
      char* gt = (char*)memchr(lt, '>', buf + have - lt);
      if (!gt) { i = lt - buf; break; }   // the tag runs past the block: next read
      // Tag name, past an optional namespace prefix ("gpx:trkpt").
      const char* t = lt + 1;
      const bool closing = *t == '/';
      if (closing) t++;
      const char* tn = t;
      while (tn < gt && *tn != ' ' && *tn != '/' && *tn != '>' && *tn != ':') tn++;
      if (*tn == ':') t = tn + 1;
      auto is = [&](const char* s) { size_t l = strlen(s); return strncmp(t, s, l) == 0 && (t[l] == ' ' || t[l] == '>' || t[l] == '/' || t[l] == '\t' || t[l] == '\n' || t[l] == '\r'); };
      if (!closing && (is("trkpt") || is("rtept"))) {
        double la, lo;
        const bool rte = t[0] == 'r';
        if (attr(t, gt, "lat", la) && attr(t, gt, "lon", lo) && n < MAX_IN && fabs(la) <= 90 && fabs(lo) <= 180) {
          Pt& p = pts[n++];
          p.lat = (int32_t)lround(la * 1e6);
          p.lon = (int32_t)lround(lo * 1e6);
          p.ele = ROUTE_ELE_NONE;
          p.rte = rte;
          if (rte) nrte++; else ntrk++;
          in_pt = gt[-1] != '/';
        }
      } else if (closing && (is("trkpt") || is("rtept"))) {
        in_pt = false;
      } else if (!closing && is("ele") && in_pt) {
        in_ele = true;
      } else if (!closing && (is("trk") || is("rte"))) {
        in_trk = true;
      } else if (!closing && is("name") && in_trk && !name[0] && n == 0) {
        want_name = true;
      } else if (!closing && (is("trkseg"))) {
        in_trk = false;   // a track's name comes before its segments
      }
      i = gt + 1 - buf;
    }
    if (i < have) memmove(buf, buf + i, have - i);
    have -= i;
    if (have >= BUF) have = 0;   // one tag longer than the buffer: dropped
    if (got == 0) break;
  }
  free(buf);
  // Tracks win; the route's points only when there is no track.
  int k = 0;
  const bool want_rte = ntrk == 0;
  for (int j = 0; j < n; j++) {
    if ((pts[j].rte != 0) != want_rte) continue;
    if (k > 0 && pts[j].lat == pts[k - 1].lat && pts[j].lon == pts[k - 1].lon) continue;   // repeats
    pts[k++] = pts[j];
  }
  n = k;
  (void)nrte;
  if (n < 2) { free(pts); pts = nullptr; return NO_POINTS; }
  return OK;
}

// Douglas-Peucker ranks (metres, the ends +inf), into `rank`; as the site's.
static bool rankPoints(const Pt* pts, int n, float* rank) {
  struct Span { int a, b; float cap; };
  Span* stack = bigAlloc<Span>(n + 2);
  if (!stack) return false;
  const float kx = cosf(pts[0].lat * 1e-6f * (float)M_PI / 180.0f) * 0.11132f, ky = 0.110574f;
  for (int i = 0; i < n; i++) rank[i] = 0;
  rank[0] = rank[n - 1] = INFINITY;
  int sp = 0;
  stack[sp++] = { 0, n - 1, INFINITY };
  while (sp) {
    Span s = stack[--sp];
    if (s.b - s.a < 2) continue;
    const float ax = 0, ay = 0;
    const float bx = (pts[s.b].lon - pts[s.a].lon) * kx, by = (pts[s.b].lat - pts[s.a].lat) * ky;
    const float l2 = bx * bx + by * by;
    float best = -1;
    int bi = s.a + 1;
    for (int i = s.a + 1; i < s.b; i++) {
      const float px = (pts[i].lon - pts[s.a].lon) * kx, py = (pts[i].lat - pts[s.a].lat) * ky;
      float t = l2 > 0 ? (px * bx + py * by) / l2 : 0;
      if (t < 0) t = 0; else if (t > 1) t = 1;
      const float ex = ax + t * bx - px, ey = ay + t * by - py;
      const float d = ex * ex + ey * ey;
      if (d > best) { best = d; bi = i; }
    }
    float d = sqrtf(best);
    if (d > s.cap) d = s.cap;   // never above its parent: kept in order
    rank[bi] = d;
    stack[sp++] = { s.a, bi, d };   // at most one more span than taken: n + 2 holds them
    stack[sp++] = { bi, s.b, d };
  }
  free(stack);
  return true;
}

// The whole import: `path` into the first free slot. `max_points` 0: the
// device's limit. `slot_out` the slot written, `kept` the points kept,
// `dev_m` how far the simplified line strays from the file's at most.
static Result importFile(const char* path, const char* fallback_name, int max_points,
                         int& slot_out, int& kept, float& dev_m) {
  slot_out = -1; kept = 0; dev_m = 0;
  const int slot = PlannedRoutes::freeSlot();
  if (slot < 0) return NO_SLOT;
  FILE* f = fopen(path, "rb");
  if (!f) return NO_FILE;
  Pt* pts = nullptr;
  int n = 0;
  char name[ROUTE_NAME_LEN];
  Result r = readGpx(f, pts, n, name, sizeof(name));
  fclose(f);
  if (r != OK) return r;
  if (!name[0] && fallback_name) {
    snprintf(name, sizeof(name), "%s", fallback_name);
    char* dot = strrchr(name, '.');
    if (dot) *dot = '\0';
  }
  int want = max_points > 0 && max_points < PlannedRoutes::MAX_POINTS ? max_points : PlannedRoutes::MAX_POINTS;
  if (want < 2) want = 2;
  // Which points stay: all, or the `want` best ranked.
  uint8_t* keep = bigAlloc<uint8_t>(n);
  if (!keep) { free(pts); return NO_MEMORY; }
  if (n <= want) {
    memset(keep, 1, n);
  } else {
    float* rank = bigAlloc<float>(n);
    int* order = bigAlloc<int>(n);
    if (!rank || !order || !rankPoints(pts, n, rank)) { free(rank); free(order); free(keep); free(pts); return NO_MEMORY; }
    for (int i = 0; i < n; i++) order[i] = i;
    std::sort(order, order + n, [&](int a, int b) { return rank[a] > rank[b]; });
    memset(keep, 0, n);
    for (int i = 0; i < want; i++) keep[order[i]] = 1;
    dev_m = rank[order[want]];
    free(rank); free(order);
  }
  int count = 0;
  bool ele = false;
  for (int i = 0; i < n; i++) if (keep[i]) { count++; if (pts[i].ele != ROUTE_ELE_NONE) ele = true; }
  PlannedRoutes w;
  bool ok = w.begin(slot, (uint16_t)count, ele ? PlannedRoutes::FLAG_ELE : 0, name[0] ? name : "Route");
  for (int i = 0; ok && i < n; i++) if (keep[i]) ok = w.add(pts[i].lat, pts[i].lon, pts[i].ele);
  ok = ok && w.finish();
  free(keep);
  free(pts);
  if (!ok) { w.abort(); return WRITE_FAILED; }
  slot_out = slot;
  kept = count;
  return OK;
}

}  // namespace gpximport
