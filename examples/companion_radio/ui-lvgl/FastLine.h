#pragma once
// Thick anti-aliased lines and discs written straight into the layer being
// drawn, for the map's packet flows. LVGL's software renderer works a slanted
// line through four masks over its whole bounding box (lv_draw_sw_line.c), so
// a long diagonal costs its width times its height, again for every band of
// the screen it crosses; here a line costs its length times its width. With a
// few dozen flows on the map that was the difference between the loop
// drawing nothing else and keeping up.
//
// begin() first has LVGL finish what's queued for the layer (the tiles under
// the flows), as lv_refr.c does before a flush; what's drawn after (names,
// dots) goes over. RGB565 and its byte-swapped form only; else begin() says
// no and the caller draws with LVGL.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp before MapScreen.h.

#include <src/draw/lv_draw_private.h>

namespace fastline {

struct Target {
  uint8_t* data = nullptr;
  uint32_t stride = 0;
  lv_area_t buf;    // the draw buffer's area, screen px
  lv_area_t clip;   // where drawing may go, screen px
  bool swapped = false;
};

static bool begin(lv_layer_t* layer, Target& t) {
  lv_draw_buf_t* db = layer ? layer->draw_buf : nullptr;
  if (!db || !db->data) return false;
  const lv_color_format_t cf = (lv_color_format_t)db->header.cf;
  if (cf != LV_COLOR_FORMAT_RGB565 && cf != LV_COLOR_FORMAT_RGB565_SWAPPED) return false;
  while (layer->draw_task_head) {   // the tiles and all queued before: drawn now
    lv_draw_dispatch_wait_for_request();
    lv_draw_dispatch();
  }
  t.data = db->data;
  t.stride = db->header.stride;
  t.buf = layer->buf_area;
  if (!lv_area_intersect(&t.clip, &layer->_clip_area, &layer->buf_area)) return false;
  t.swapped = cf == LV_COLOR_FORMAT_RGB565_SWAPPED;
  return true;
}

static inline uint16_t rgb565(uint32_t hex) {
  return (uint16_t)(((hex >> 8) & 0xF800) | ((hex >> 5) & 0x07E0) | ((hex >> 3) & 0x001F));
}

// `col` over the pixel at `p`, `a` 0..255.
__attribute__((always_inline)) static inline void blendAt(uint16_t* p, bool swapped, uint16_t col, uint32_t a) {
  if (a < 4) return;
  if (a >= 252) { *p = swapped ? __builtin_bswap16(col) : col; return; }
  uint32_t d = swapped ? __builtin_bswap16(*p) : *p;
  uint32_t fg = (col | ((uint32_t)col << 16)) & 0x07E0F81F;
  uint32_t bg = (d | (d << 16)) & 0x07E0F81F;
  bg = ((((fg - bg) * (a >> 3)) >> 5) + bg) & 0x07E0F81F;
  const uint16_t r = (uint16_t)(bg | (bg >> 16));
  *p = swapped ? __builtin_bswap16(r) : r;
}
static inline void blend(const Target& t, int32_t x, int32_t y, uint16_t col, uint32_t a) {
  blendAt((uint16_t*)(t.data + (y - t.buf.y1) * t.stride) + (x - t.buf.x1), t.swapped, col, a);
}

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
// floor / ceil / round without the library calls (an inner loop's rows).
static inline int32_t ifloor(float v) { int32_t i = (int32_t)v; return i - (v < (float)i); }
static inline int32_t iceil(float v) { int32_t i = (int32_t)v; return i + (v > (float)i); }
static inline int32_t iround(float v) { return (int32_t)(v + (v >= 0 ? 0.5f : -0.5f)); }

// A segment for segDist2(): its start, its direction, 1 / its length squared
// (this FPU divides in software, so once a segment, not once a pixel).
struct Seg {
  float ax, ay, dx, dy, inv;
  void set(float x0, float y0, float x1, float y1) {
    ax = x0; ay = y0; dx = x1 - x0; dy = y1 - y0;
    const float l2 = dx * dx + dy * dy;
    inv = l2 > 0 ? 1.0f / l2 : 0;
  }
};
// Squared distance from (px, py) to the segment (its round-ended "capsule").
// Squared: there's no square root in this FPU either.
__attribute__((always_inline)) static inline float segDist2(float px, float py, const Seg& g) {
  const float u = clamp01(((px - g.ax) * g.dx + (py - g.ay) * g.dy) * g.inv);
  const float ex = px - (g.ax + u * g.dx), ey = py - (g.ay + u * g.dy);
  return ex * ex + ey * ey;
}

// A polyline `w` px wide with round ends and joints. Each pixel is put down
// once, by the segment nearest to it -- a see-through line (a casing, a
// fading trail) shows no darker blots where its segments meet.
__attribute__((optimize("O2"))) static void polyline(const Target& t, const int32_t* xs, const int32_t* ys, int n, float w, uint16_t col, uint8_t opa) {
  const float hw = w * 0.5f;
  Seg sg[16];   // segment k-1 -> k in sg[k] (MAX_PTS is 10)
  if (n > 16) n = 16;
  for (int k = 1; k < n; k++) sg[k].set((float)xs[k - 1], (float)ys[k - 1], (float)xs[k], (float)ys[k]);
  for (int k = 1; k < n; k++) {
    const float x0 = xs[k - 1], y0 = ys[k - 1], x1 = xs[k], y1 = ys[k];
    const float dx = x1 - x0, dy = y1 - y0;
    const bool steep = fabsf(dy) >= fabsf(dx);
    // Walk the major axis; at each step only the few pixels across the line
    // (wide enough for the round ends, which reach past the line's centre).
    const float m0 = steep ? fminf(y0, y1) : fminf(x0, x1), m1 = steep ? fmaxf(y0, y1) : fmaxf(x0, x1);
    int32_t a = ifloor(m0 - hw - 1), b = iceil(m1 + hw + 1);
    const int32_t lo = steep ? t.clip.y1 : t.clip.x1, hi = steep ? t.clip.y2 : t.clip.x2;
    if (a < lo) a = lo;
    if (b > hi) b = hi;
    const float major = steep ? dy : dx;
    const float slope = major != 0 ? (steep ? dx : dy) / major : 0;   // minor per major, |slope| <= 1
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) continue;
    const float ux = dx / len, uy = dy / len;
    const float cosv = fabsf(major) / len;   // >= 0.707: the major axis is the longer one
    const float body = hw / cosv + 1;        // half the span across the line's body
    const float s0 = steep ? y0 : x0, n0 = steep ? x0 : y0;
    const int32_t clo = steep ? t.clip.x1 : t.clip.y1, chi = steep ? t.clip.x2 : t.clip.y2;
    // Along the minor axis the signed distance to the line changes by a fixed
    // step: in the body it's kept in 8.8 fixed point, no floats per pixel.
    const int32_t hw_fx = (int32_t)((hw + 0.5f) * 256);
    const int32_t step_fx = iround((steep ? uy : -ux) * 256);   // d per +1 on the minor axis
    for (int32_t m = a; m <= b; m++) {
      // Near an end the round end / the joint: the full distance, and the
      // neighbours' claims. Elsewhere the distance to the line is linear.
      const bool end = m < m0 + hw + 1 || m > m1 - hw - 1;
      const float mm = m < m0 ? m0 : m > m1 ? m1 : (float)m;   // the centre, held at the ends
      const float c = n0 + (mm - s0) * slope;
      const float across = end ? body + hw : body;
      int32_t n1 = ifloor(c - across), n2 = iceil(c + across);
      if (n1 < clo) n1 = clo;
      if (n2 > chi) n2 = chi;
      if (n1 > n2) continue;
      if (!end) {
        const float fx = steep ? (float)n1 : (float)m, fy = steep ? (float)m : (float)n1;
        int32_t d = iround(((fx - x0) * uy - (fy - y0) * ux) * 256);
        uint16_t* p = steep ? (uint16_t*)(t.data + (m - t.buf.y1) * t.stride) + (n1 - t.buf.x1)
                            : (uint16_t*)(t.data + (n1 - t.buf.y1) * t.stride) + (m - t.buf.x1);
        const int32_t pstep = steep ? 1 : (int32_t)(t.stride / 2);
        for (int32_t q = n1; q <= n2; q++, d += step_fx, p += pstep) {
          const int32_t cov = hw_fx - (d < 0 ? -d : d);   // 8.8: 256 = fully inside
          if (cov <= 0) continue;
          blendAt(p, t.swapped, col, cov >= 256 ? opa : (uint32_t)(cov * opa) >> 8);
        }
        continue;
      }
      const float out2 = (hw + 0.5f) * (hw + 0.5f), in2 = hw > 0.5f ? (hw - 0.5f) * (hw - 0.5f) : 0;
      const bool near0 = k > 1 && fabsf((steep ? y0 : x0) - m) <= hw + 1;       // the joint with the one before
      const bool near1 = k + 1 < n && fabsf((steep ? y1 : x1) - m) <= hw + 1;   // ... with the one after
      for (int32_t q = n1; q <= n2; q++) {
        const float px = steep ? (float)q : (float)m, py = steep ? (float)m : (float)q;
        const float d2 = segDist2(px, py, sg[k]);
        if (d2 >= out2) continue;
        // The neighbours own what's nearer to them (the one before, ties too).
        if (near0 && segDist2(px, py, sg[k - 1]) <= d2) continue;
        if (near1 && segDist2(px, py, sg[k + 1]) < d2) continue;
        const float cov = d2 <= in2 ? 1.0f : hw + 0.5f - sqrtf(d2);   // the root only on the edge
        blend(t, steep ? q : m, steep ? m : q, col, cov >= 1 ? opa : (uint32_t)(cov * opa));
      }
    }
  }
}

}  // namespace fastline
