#pragma once
// A trail's height profile (ui-core/TrailProfile.h) as a chart (Chart.h):
// the height over the distance, the climb and the highest and lowest points
// over it. In the live trail's Map tools section and
// a saved trail's popup. ui-new draws the same on e-ink and the OLED
// (TrailScreen.h).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp before NavMap.h.

namespace profileview {

static trailprofile::Sampler s_prof;   // what the chart on screen shows
static chart::Chart s_chart;
static char s_under[3][12];

static void distText(float m, char* out, size_t n) {
  if (m < 1000) snprintf(out, n, "%d m", (int)lroundf(m));
  else snprintf(out, n, "%.1f km", m / 1000.0f);
}

}  // namespace profileview

// The chart, `h` tall, into `parent`, for what s_prof holds; nothing when
// the trail has no heights. Returns the chart, or null.
static lv_obj_t* profileChart(lv_obj_t* parent, int h) {
  using namespace profileview;
  if (!s_prof.st.any || s_prof.n < 2) return nullptr;
  chart::Chart& c = s_chart;
  c = chart::Chart();
  const float total = s_prof.total > 0 ? s_prof.total : 1.0f;
  c.n = s_prof.n;
  c.spaced = true;
  for (int i = 0; i < c.n; i++) {
    c.v[i] = s_prof.alt[i];
    c.x[i] = (uint16_t)lroundf(s_prof.dist[i] / total * 10000);
  }
  c.min_span = 20;
  c.axis = [](int v, int, char* o, size_t n) { snprintf(o, n, "%d m", v); };
  c.value = [](const chart::Chart& c, int i, char* o, size_t n) { snprintf(o, n, "%d m", c.v[i]); };
  c.where = [](const chart::Chart& c, int i, char* o, size_t n) { (void)c; distText(s_prof.dist[i], o, n); };
  distText(0, s_under[0], sizeof(s_under[0]));
  distText(total / 2, s_under[1], sizeof(s_under[1]));
  distText(total, s_under[2], sizeof(s_under[2]));
  for (int j = 0; j < 3; j++) c.under[j] = s_under[j];
  const int lh = lv_font_get_line_height(THEME_FONT_SMALL);
  c.top = lh + 2;   // the caption
  lv_obj_t* box = lv_obj_create(parent);
  styleSurface(box, theme::SURFACE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  fillWidth(box);
  lv_obj_set_height(box, h);
  lv_obj_set_style_radius(box, theme::RADIUS_SM, 0);
  lv_obj_set_style_pad_all(box, 6, 0);
  lv_obj_t* o = chart::create(box, c, h - 12);
  char t[64];
  snprintf(t, sizeof(t), "Climb %d m  -  highest %d m  -  lowest %d m", s_prof.st.gain, s_prof.st.hi, s_prof.st.lo);
  lv_obj_align(label(o, t, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_TOP_LEFT, 0, 0);
  return box;
}
