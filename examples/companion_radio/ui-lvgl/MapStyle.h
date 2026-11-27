#pragma once

// How lines and markers are drawn over a map: the main map and Home's mini
// map alike. Over any base layer (Topo's orange roads and blue water, the
// satellite, the dark and light canvases) and in either theme, so nothing
// here takes the theme's BG:
//  - your own navigation (the trail, the route, the line to the target) is a
//    saturated colour over a white casing;
//  - radio traffic (packet flows, a message's route) a bright colour over a
//    dark casing -- the two never read as one;
//  - a marker dot is its colour inside a white ring with a thin dark edge.
namespace mapstyle {

static const uint32_t TRAIL = 0x1E88FF;   // walked: a deeper blue than Topo's water
static const uint32_t ROUTE = 0xE0218A;   // planned: magenta, as a navigator's course line
static const uint32_t LIGHT_CASE = 0xFFFFFF;
static const uint32_t DARK_CASE = 0x111116;
static const lv_opa_t LIGHT_CASE_OPA = LV_OPA_90;
static const lv_opa_t DARK_CASE_OPA = LV_OPA_70;
static const int TRAIL_W = 3, ROUTE_W = 4;
static const int CASE_W = 3;              // a casing is this much wider than its line
static const int DASH = 10, GAP = 7;      // dashed lines (the software renderer dashes only level ones)
static const uint32_t RING = 0xFFFFFF;    // round a marker dot
static const uint32_t EDGE = 0x000000;    // round the ring
static const lv_opa_t EDGE_OPA = LV_OPA_50;

static void lineDsc(lv_draw_line_dsc_t& d, uint32_t col, int w, lv_opa_t opa) {
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(col);
  d.width = w;
  d.opa = opa;
  d.round_start = d.round_end = 1;
}

static lv_opa_t caseOpa(uint32_t casing, lv_opa_t opa) {
  return (lv_opa_t)(opa * (casing == LIGHT_CASE ? LIGHT_CASE_OPA : DARK_CASE_OPA) / 255);
}

// A polyline (LV_DRAW_LINE_POINT_NONE breaks it) over its casing: all of the
// casing first, then the line, so the joints stay clean.
static void polyline(lv_layer_t* layer, const lv_point_precise_t* p, int n, uint32_t col, int w,
                     uint32_t casing, lv_opa_t opa = LV_OPA_COVER) {
  if (n < 2) return;
  lv_draw_line_dsc_t d;
  lineDsc(d, casing, w + CASE_W, caseOpa(casing, opa));
  d.points = const_cast<lv_point_precise_t*>(p);   // only read
  d.point_cnt = n;
  lv_draw_line(layer, &d);
  d.color = lv_color_hex(col);
  d.width = w;
  d.opa = opa;
  lv_draw_line(layer, &d);
}

// The dashes of a polyline, the pattern carried on across its bends; only
// those in the layer's clip.
static void dashes(lv_layer_t* layer, const lv_point_precise_t* p, int n, const lv_draw_line_dsc_t& proto) {
  lv_draw_line_dsc_t d = proto;
  d.points = nullptr;
  d.point_cnt = 0;
  const lv_area_t& clip = layer->_clip_area;
  const float pad = d.width;
  float phase = 0;   // along the pattern: 0..DASH a dash, DASH..DASH+GAP a gap
  for (int i = 1; i < n; i++) {
    const lv_point_precise_t a = p[i - 1], b = p[i];
    if (a.x == LV_DRAW_LINE_POINT_NONE || b.x == LV_DRAW_LINE_POINT_NONE) { phase = 0; continue; }
    const float dx = (float)(b.x - a.x), dy = (float)(b.y - a.y);
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) continue;
    const bool off = fminf(a.x, b.x) - pad > clip.x2 || fmaxf(a.x, b.x) + pad < clip.x1 ||
                     fminf(a.y, b.y) - pad > clip.y2 || fmaxf(a.y, b.y) + pad < clip.y1;
    float s = 0;
    while (s < len) {
      const float left = phase < DASH ? DASH - phase : DASH + GAP - phase;
      const float e = fminf(len, s + left);
      if (phase < DASH && !off) {
        d.p1.x = (lv_value_precise_t)lroundf(a.x + dx * s / len);
        d.p1.y = (lv_value_precise_t)lroundf(a.y + dy * s / len);
        d.p2.x = (lv_value_precise_t)lroundf(a.x + dx * e / len);
        d.p2.y = (lv_value_precise_t)lroundf(a.y + dy * e / len);
        lv_draw_line(layer, &d);
      }
      phase += e - s;
      if (phase >= DASH + GAP) phase = 0;
      s = e;
    }
  }
}

// A dashed polyline, over dashes of its casing (`casing` 0: none).
static void dashedPolyline(lv_layer_t* layer, const lv_point_precise_t* p, int n, uint32_t col, int w,
                           uint32_t casing, lv_opa_t opa = LV_OPA_COVER) {
  if (n < 2) return;
  lv_draw_line_dsc_t d;
  if (casing) {
    lineDsc(d, casing, w + CASE_W, caseOpa(casing, opa));
    dashes(layer, p, n, d);
  }
  lineDsc(d, col, w, opa);
  dashes(layer, p, n, d);
}

// A marker dot's look: the fill (bg_color) is the caller's.
// Points laid out for obj at screen origin (x0, y0), moved with it to where
// it is at its draw (a scroll, a slid page) -- the gaps (POINT_NONE) kept.
static void followObj(lv_obj_t* obj, lv_point_precise_t* p, int n, int32_t& x0, int32_t& y0) {
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  if (a.x1 == x0 && a.y1 == y0) return;
  for (int i = 0; i < n; i++) {
    if (p[i].x == LV_DRAW_LINE_POINT_NONE) continue;
    p[i].x += a.x1 - x0;
    p[i].y += a.y1 - y0;
  }
  x0 = a.x1; y0 = a.y1;
}

static void dotDsc(lv_draw_rect_dsc_t& d, int ring = 2, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_opa = opa;
  d.border_color = lv_color_hex(RING);
  d.border_width = ring;
  d.border_opa = opa;
  d.outline_color = lv_color_hex(EDGE);
  d.outline_width = 1;
  d.outline_pad = 0;
  d.outline_opa = (lv_opa_t)(opa * EDGE_OPA / 255);
}

// An object as a marker dot: `fill` inside a white ring, a thin dark edge.
static void styleDot(lv_obj_t* o, int d, uint32_t fill, int ring) {
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(RING), 0);
  lv_obj_set_style_border_width(o, ring, 0);
  lv_obj_set_style_outline_color(o, lv_color_hex(EDGE), 0);
  lv_obj_set_style_outline_width(o, 1, 0);
  lv_obj_set_style_outline_pad(o, 0, 0);
  lv_obj_set_style_outline_opa(o, EDGE_OPA, 0);
}

// An object as a target ring: `col`, white round it.
static void styleRing(lv_obj_t* o, int d, uint32_t col, int w) {
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_border_width(o, w, 0);
  lv_obj_set_style_outline_color(o, lv_color_hex(RING), 0);
  lv_obj_set_style_outline_width(o, 2, 0);
  lv_obj_set_style_outline_pad(o, 0, 0);
  lv_obj_set_style_outline_opa(o, LV_OPA_90, 0);
}

}  // namespace mapstyle
