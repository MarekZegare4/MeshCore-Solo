#pragma once
// A chart drawn by its object (LV_EVENT_DRAW_MAIN_END): round values up the
// left with a grid line each, labels along the bottom, the samples as a line
// over a fill or as bars. A finger on it shows the sample under it -- a line
// down the plot, the value and where / when on a pill. Diagnostics > History
// and the trail's height profile (ProfileView.h) draw with it; the GPS bars
// take the axis alone.
//
// No transparency: the fill is the accent mixed into the card's colour up
// front, so the panel's renderer only ever copies solid colour.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp before ProfileView.h.

namespace chart {

static const int MAX = 128;

struct Chart {
  int16_t v[MAX];
  uint16_t x[MAX];      // with `spaced`: each sample's place, 0..10000 of the width
  int n = 0, cap = MAX; // without: newest at the right, `cap` samples wide
  bool spaced = false;
  bool bars = false;
  bool from_zero = false;   // the scale starts at 0 (counts)
  int min_span = 1;         // the scale spans at least this (a steady reading stays flat)
  int unit = 1;             // the scale's steps are 1, 2, 5 x 10^k of this many units
  int top = 0;              // room over the plot (a caption)
  uint32_t bg = theme::SURFACE;   // what it's drawn on
  void (*axis)(int v, int step, char* out, size_t n) = nullptr;   // a value up the left
  void (*value)(const Chart& c, int i, char* out, size_t n) = nullptr;   // the pill: the sample...
  void (*where)(const Chart& c, int i, char* out, size_t n) = nullptr;   // ...and where / when
  const char* under[5] = {};   // along the bottom, first at the left, last at the right
  int16_t mark = -1;           // bars: one drawn in the accent, the rest in `bar`
  uint32_t bar = theme::ACCENT;
  int16_t touch = -1;          // the sample under the finger
  mutable int16_t axis_w = 0;  // the plot's left edge from the object's, as last drawn
};

// The step, 1 / 2 / 5 x 10^k units, that cuts `span` into at most `ticks` parts.
static int niceStep(int span, int unit, int ticks) {
  for (int s = unit; ; s *= 10)
    for (int m : { 1, 2, 5 }) if (s * m * ticks >= span) return s * m;
}

struct Scale { int lo, hi, step; };

// lo..hi widened to whole steps.
static Scale scaleFor(int lo, int hi, int min_span, int unit, bool from_zero, int ticks) {
  if (from_zero && lo > 0) lo = 0;
  if (hi - lo < min_span) {
    if (from_zero) hi = lo + min_span;
    else { const int mid = (hi + lo) / 2; lo = mid - min_span / 2; hi = lo + min_span; }
  }
  Scale s;
  s.step = niceStep(hi - lo, unit, ticks);
  s.lo = (lo >= 0 ? lo / s.step : -((-lo + s.step - 1) / s.step)) * s.step;
  s.hi = (hi >= 0 ? (hi + s.step - 1) / s.step : -(-hi / s.step)) * s.step;
  if (s.hi == s.lo) s.hi = s.lo + s.step;
  return s;
}

static void text(lv_layer_t* layer, const char* t, int32_t x1, int32_t y1, int32_t x2, lv_text_align_t al, uint32_t col) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = THEME_FONT_SMALL;
  d.color = lv_color_hex(col);
  d.align = al;
  d.text = t;
  d.text_local = 1;
  lv_area_t r = { x1, y1, x2, y1 + lv_font_get_line_height(d.font) };
  lv_draw_label(layer, &d, &r);
}

static void hline(lv_layer_t* layer, int32_t x1, int32_t x2, int32_t y, uint32_t col, int dash) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(col);
  d.width = 1;
  d.dash_width = d.dash_gap = dash;
  d.p1.x = x1; d.p2.x = x2; d.p1.y = d.p2.y = y;
  lv_draw_line(layer, &d);
}

static int textW(const char* t) {
  lv_point_t sz;
  lv_text_get_size(&sz, t, THEME_FONT_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}

// The values up the left of y_top..y_bot and a grid line across to x2 at
// each; returns where the plot starts. `fmt` writes a value.
static int32_t axis(lv_layer_t* layer, int32_t x1, int32_t x2, int32_t y_top, int32_t y_bot, const Scale& s,
                    void (*fmt)(int v, int step, char* out, size_t n), uint32_t grid) {
  char t[16];
  int w = 0;
  for (int v = s.lo; v <= s.hi; v += s.step) { fmt(v, s.step, t, sizeof(t)); const int tw = textW(t); if (tw > w) w = tw; }
  const int32_t px = x1 + w + 6;
  const int lh = lv_font_get_line_height(THEME_FONT_SMALL);
  for (int v = s.lo; v <= s.hi; v += s.step) {
    const int32_t y = y_bot - (int32_t)(v - s.lo) * (y_bot - y_top) / (s.hi - s.lo);
    hline(layer, px, x2, y, grid, 0);
    fmt(v, s.step, t, sizeof(t));
    text(layer, t, x1, y - lh / 2, px - 6, LV_TEXT_ALIGN_RIGHT, theme::TEXT_MUTED);
  }
  return px;
}

// Where the plot is inside the object's area.
struct Plot { int32_t x1, x2, y1, y2; };

static Plot plotOf(const Chart& c, const lv_area_t& a, int32_t px) {
  const int lh = lv_font_get_line_height(THEME_FONT_SMALL);
  return { px, a.x2 - 2, a.y1 + c.top + lh / 2, a.y2 - lh - 2 };
}

static int32_t xOf(const Chart& c, const Plot& p, int i) {
  const int32_t w = p.x2 - p.x1;
  if (c.spaced) return p.x1 + (int32_t)c.x[i] * w / 10000;
  return p.x2 - (int32_t)(c.n - 1 - i) * w / (c.cap > 1 ? c.cap - 1 : 1);
}

static void onDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  const Chart& c = *(const Chart*)lv_event_get_user_data(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const int lh = lv_font_get_line_height(THEME_FONT_SMALL);
  const uint32_t grid = theme::mix(theme::TEXT_MUTED, c.bg, 22);

  int lo = c.n ? c.v[0] : 0, hi = lo;
  for (int i = 0; i < c.n; i++) { if (c.v[i] < lo) lo = c.v[i]; if (c.v[i] > hi) hi = c.v[i]; }
  const Scale s = scaleFor(lo, hi, c.min_span, c.unit, c.from_zero || c.bars, 3);
  const int32_t y_top = a.y1 + c.top + lh / 2, y_bot = a.y2 - lh - 2;
  const int32_t px = c.n ? axis(layer, a.x1, a.x2 - 2, y_top, y_bot, s, c.axis, grid) : a.x1 + 2;
  const Plot p = plotOf(c, a, px);
  c.axis_w = (int16_t)(px - a.x1);

  // Along the bottom, spread evenly.
  int nu = 0;
  while (nu < 5 && c.under[nu]) nu++;
  for (int k = 0; k < nu; k++) {
    const int32_t x = nu > 1 ? p.x1 + (p.x2 - p.x1) * k / (nu - 1) : p.x1;
    const lv_text_align_t al = k == 0 ? LV_TEXT_ALIGN_LEFT : k == nu - 1 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER;
    const int32_t x1 = k == 0 ? x : k == nu - 1 ? x - 60 : x - 30;
    text(layer, c.under[k], x1, p.y2 + 2, x1 + 59, al, theme::TEXT_MUTED);
  }

  if (c.n < (c.bars ? 1 : 2)) {   // nothing yet: a dotted baseline
    hline(layer, p.x1, p.x2, p.y2, theme::TEXT_MUTED, 2);
    return;
  }
  auto Y = [&](int v) { return (int32_t)(p.y2 - (int32_t)(v - s.lo) * (p.y2 - p.y1) / (s.hi - s.lo)); };

  if (c.bars) {   // a bar a sample, a pixel apart
    const int pitch = ((p.x2 - p.x1) + c.cap / 2) / c.cap, bw = pitch > 1 ? pitch - 1 : 1;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    for (int i = 0; i < c.n; i++) {
      const int32_t y = Y(c.v[i]);
      if (y >= p.y2) continue;   // nothing (none heard, or the bottom of the scale)
      rd.bg_color = lv_color_hex(i == c.touch ? theme::TEXT : i == c.mark ? theme::ACCENT : c.bar);
      const int32_t x = xOf(c, p, i);
      lv_area_t r = { x - bw + 1, y, x, p.y2 };
      lv_draw_rect(layer, &rd, &r);
    }
  } else {
    lv_draw_line_dsc_t fill;   // a column at a time under the line (triangles would leave seams)
    lv_draw_line_dsc_init(&fill);
    fill.color = lv_color_hex(theme::mix(theme::ACCENT, c.bg, 14));
    fill.width = 1;
    for (int i = 1; i < c.n; i++) {
      const int32_t xa = xOf(c, p, i - 1), xb = xOf(c, p, i), ya = Y(c.v[i - 1]), yb = Y(c.v[i]);
      for (int32_t x = (i == 1 ? xa : xa + 1); x <= xb; x++) {
        fill.p1.x = fill.p2.x = x;
        fill.p1.y = xb > xa ? ya + (yb - ya) * (x - xa) / (xb - xa) : yb;
        fill.p2.y = p.y2;
        if (fill.p1.y < fill.p2.y) lv_draw_line(layer, &fill);
      }
    }
    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(theme::ACCENT);
    ld.width = 2;
    ld.round_start = ld.round_end = 1;
    for (int i = 1; i < c.n; i++) {
      ld.p1.x = xOf(c, p, i - 1); ld.p1.y = Y(c.v[i - 1]);
      ld.p2.x = xOf(c, p, i);     ld.p2.y = Y(c.v[i]);
      lv_draw_line(layer, &ld);
    }
  }

  if (c.touch < 0 || c.touch >= c.n) return;
  // The sample under the finger: a line down, a dot on the line, a pill over the plot.
  const int i = c.touch;
  const int32_t x = xOf(c, p, i), y = Y(c.v[i]);
  lv_draw_line_dsc_t vl;
  lv_draw_line_dsc_init(&vl);
  vl.color = lv_color_hex(theme::TEXT_MUTED);
  vl.width = 1;
  vl.p1.x = vl.p2.x = x; vl.p1.y = a.y1 + c.top; vl.p2.y = p.y2;
  lv_draw_line(layer, &vl);
  if (!c.bars) {
    lv_draw_rect_dsc_t dd;
    lv_draw_rect_dsc_init(&dd);
    dd.radius = LV_RADIUS_CIRCLE;
    dd.bg_color = lv_color_hex(theme::TEXT);
    dd.border_color = lv_color_hex(c.bg);
    dd.border_width = 2;
    lv_area_t da = { x - 5, y - 5, x + 5, y + 5 };
    lv_draw_rect(layer, &dd, &da);
  }
  char val[24] = "", at[24] = "", t[52];
  if (c.value) c.value(c, i, val, sizeof(val));
  if (c.where) c.where(c, i, at, sizeof(at));
  snprintf(t, sizeof(t), at[0] ? "%s  %s" : "%s", val, at);
  const int32_t w = textW(t) + 12;
  int32_t bx = x - w / 2;
  if (bx < a.x1) bx = a.x1;
  if (bx + w > a.x2) bx = a.x2 - w;
  lv_draw_rect_dsc_t bg;
  lv_draw_rect_dsc_init(&bg);
  bg.radius = theme::RADIUS_SM;
  bg.bg_color = lv_color_hex(theme::SURFACE_2);
  lv_area_t ba = { bx, a.y1 + c.top, bx + w - 1, a.y1 + c.top + lh + 1 };
  lv_draw_rect(layer, &bg, &ba);
  text(layer, t, bx, a.y1 + c.top + 1, bx + w - 1, LV_TEXT_ALIGN_CENTER, theme::TEXT);
}

// The finger: the nearest sample along the width, while it's down.
static void onTouch(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  Chart& c = *(Chart*)lv_event_get_user_data(e);
  int16_t t = c.touch;
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    lv_indev_t* in = lv_indev_active();
    if (!in || !c.n) return;
    lv_point_t pt;
    lv_indev_get_point(in, &pt);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const Plot p = plotOf(c, a, a.x1 + c.axis_w);
    int best = -1;
    int32_t bd = 0;
    for (int i = 0; i < c.n; i++) {
      const int32_t d = LV_ABS(xOf(c, p, i) - pt.x);
      if (best < 0 || d < bd) { best = i; bd = d; }
    }
    t = (int16_t)best;
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    t = -1;
  } else {
    return;
  }
  if (t != c.touch) { c.touch = t; lv_obj_invalidate(o); }
}

// The chart's object, `h` tall, drawing `c`.
static lv_obj_t* create(lv_obj_t* parent, Chart& c, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(o, LV_PCT(100));
  lv_obj_set_height(o, h);
  c.touch = -1;
  lv_obj_add_event_cb(o, onDraw, LV_EVENT_DRAW_MAIN_END, &c);
  lv_obj_add_event_cb(o, onTouch, LV_EVENT_ALL, &c);
  return o;
}

// "35 min ago", "2 h 10 min ago", "now".
static void ago(uint32_t mins, char* out, size_t n) {
  if (!mins) snprintf(out, n, "now");
  else if (mins < 60) snprintf(out, n, "%lu min ago", (unsigned long)mins);
  else if (mins % 60) snprintf(out, n, "%lu h %lu min ago", (unsigned long)(mins / 60), (unsigned long)(mins % 60));
  else snprintf(out, n, "%lu h ago", (unsigned long)(mins / 60));
}

}  // namespace chart
