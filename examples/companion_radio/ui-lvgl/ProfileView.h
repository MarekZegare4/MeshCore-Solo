#pragma once
// A trail's height profile (ui-core/TrailProfile.h) as a chart: the height
// over the distance, a line in the accent over a faint fill, the climb and
// the highest and lowest points over it, top left. In the live trail's Map tools section and
// a saved trail's popup. ui-new draws the same on e-ink and the OLED
// (TrailScreen.h).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp before NavMap.h.

namespace profileview {

static trailprofile::Sampler s_prof;   // what the chart on screen shows

static void onDraw(lv_event_t* e) {
  const trailprofile::Sampler& p = s_prof;
  if (p.n < 2) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int lo = p.st.lo, hi = p.st.hi;
  if (hi - lo < 20) { const int mid = (hi + lo) / 2; lo = mid - 10; hi = mid + 10; }
  const float total = p.total > 0 ? p.total : 1.0f;
  const int x0 = a.x1 + 2, w = a.x2 - a.x1 - 4, y0 = a.y1 + 20, h = a.y2 - y0 - 2, base = a.y2 - 1;   // under the caption
  auto X = [&](int i) { return (int32_t)(x0 + p.dist[i] / total * w); };
  auto Y = [&](int i) { return (int32_t)(y0 + h - (p.alt[i] - lo) * h / (hi - lo)); };

  lv_draw_line_dsc_t fill;   // under the line, a column at a time (triangles would leave seams)
  lv_draw_line_dsc_init(&fill);
  fill.color = lv_color_hex(theme::ACCENT);
  fill.opa = LV_OPA_20;
  fill.width = 1;
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(theme::ACCENT);
  ld.width = 2;
  ld.round_start = ld.round_end = 1;
  for (int i = 1; i < p.n; i++) {
    const int32_t xa = X(i - 1), xb = X(i), ya = Y(i - 1), yb = Y(i);
    for (int32_t x = (i == 1 ? xa : xa + 1); x <= xb; x++) {
      fill.p1.x = fill.p2.x = x;
      fill.p1.y = xb > xa ? ya + (yb - ya) * (x - xa) / (xb - xa) : yb;
      fill.p2.y = base;
      lv_draw_line(layer, &fill);
    }
    ld.p1.x = xa; ld.p1.y = ya; ld.p2.x = xb; ld.p2.y = yb;
    lv_draw_line(layer, &ld);
  }
}

}  // namespace profileview

// The chart, `h` tall, into `parent`, for what s_prof holds; nothing when
// the trail has no heights. Returns the chart, or null.
static lv_obj_t* profileChart(lv_obj_t* parent, int h) {
  using namespace profileview;
  if (!s_prof.st.any || s_prof.n < 2) return nullptr;
  lv_obj_t* c = lv_obj_create(parent);
  styleSurface(c, theme::SURFACE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  fillWidth(c);
  lv_obj_set_height(c, h);
  lv_obj_set_style_radius(c, theme::RADIUS_SM, 0);
  lv_obj_add_event_cb(c, onDraw, LV_EVENT_DRAW_MAIN_END, NULL);
  char t[64];
  snprintf(t, sizeof(t), "Climb %d m  -  highest %d m  -  lowest %d m", s_prof.st.gain, s_prof.st.hi, s_prof.st.lo);
  lv_obj_align(label(c, t, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_TOP_LEFT, 6, 2);
  return c;
}
