#pragma once
// Home > Compass -- ui-new's Tools > Compass. The course over ground from GPS
// movement (no magnetometer; the Core's CourseEngine, trail or not) on a dial
// that turns so the heading is up under a fixed pointer, with the degrees and
// cardinal beside it. Standing still, the heading is undefined: the dial greys
// out and a hint says to move.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after DiagScreen.h.

namespace compassview {

static lv_obj_t* s_dial = nullptr;
static lv_obj_t* s_deg = nullptr;
static lv_obj_t* s_degsign = nullptr;
static lv_obj_t* s_card = nullptr;
static lv_obj_t* s_hint = nullptr;
static int s_cog = 0;
static bool s_have = false;
static const int DIAL = 176;

}  // namespace compassview

static void onOpenCompass(lv_event_t* e) { (void)e; s_ui->showCompass(); }

// The dial, drawn over its round background: ticks every 10 degrees (longer
// every 30, longest at the cardinals), N / E / S / W and the 30-degree
// numbers, all turned by the heading; the fixed pointer at the top.
static void onCompassDraw(lv_event_t* e) {
  using namespace compassview;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  float cx = (a.x1 + a.x2) / 2.0f, cy = (a.y1 + a.y2) / 2.0f;
  float r = DIAL / 2.0f - 14;   // the ring; the pointer sits outside it
  uint32_t ink = s_have ? theme::TEXT : theme::TEXT_MUTED;
  static const char* LBL[12] = { "N", "30", "60", "E", "120", "150", "S", "210", "240", "W", "300", "330" };

  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.align = LV_TEXT_ALIGN_CENTER;
  for (int deg = 0; deg < 360; deg += 10) {
    float ang = (deg - s_cog) * (float)M_PI / 180.0f;
    float sx = sinf(ang), sy = -cosf(ang);
    bool card = deg % 90 == 0, major = deg % 30 == 0;
    float len = card ? 12 : major ? 9 : 5;
    ld.width = card ? 3 : major ? 2 : 1;
    ld.color = lv_color_hex(ink);
    ld.p1.x = cx + sx * r;         ld.p1.y = cy + sy * r;
    ld.p2.x = cx + sx * (r - len); ld.p2.y = cy + sy * (r - len);
    lv_draw_line(layer, &ld);
    if (!major) continue;
    td.text = LBL[deg / 30];
    td.font = card ? THEME_FONT_LARGE : THEME_FONT_SMALL;
    td.color = lv_color_hex(deg == 0 && s_have ? theme::FAIL : card ? ink : theme::TEXT_MUTED);
    float lr = r - (card ? 26 : 22);
    int32_t h = lv_font_get_line_height(td.font);
    int32_t px = (int32_t)(cx + sx * lr), py = (int32_t)(cy + sy * lr);
    lv_area_t la = { px - 20, py - h / 2, px + 20, py + h / 2 };
    lv_draw_label(layer, &td, &la);
  }

  // Fixed pointer: points down at the ring, the heading always under it.
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = lv_color_hex(theme::ACCENT);
  tri.opa = LV_OPA_COVER;
  tri.p[0].x = cx - 8; tri.p[0].y = cy - r - 12;
  tri.p[1].x = cx + 8; tri.p[1].y = cy - r - 12;
  tri.p[2].x = cx;     tri.p[2].y = cy - r + 2;
  lv_draw_triangle(layer, &tri);
}

void UITask::showCompass() {
  _screen = SCR_COMPASS;
  buildCompass();
}

void UITask::buildCompass() {
  using namespace compassview;
  lv_obj_t* body = newScreen("Compass", true);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

  s_dial = lv_obj_create(body);
  styleSurface(s_dial, theme::BG);
  lv_obj_remove_flag(s_dial, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_dial, DIAL, DIAL);
  lv_obj_set_style_radius(s_dial, LV_RADIUS_CIRCLE, 0);
  lv_obj_add_event_cb(s_dial, onCompassDraw, LV_EVENT_DRAW_MAIN_END, NULL);

  lv_obj_t* col = lv_obj_create(body);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, 110, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(col, 2, 0);
  lv_obj_t* num = lv_obj_create(col);   // "044" + a raised degree sign (ui_font_40 is digits only)
  lv_obj_remove_style_all(num);
  lv_obj_set_size(num, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(num, LV_FLEX_FLOW_ROW);
  s_deg = label(num, "", THEME_FONT_CLOCK, theme::TEXT);
  s_degsign = label(num, "", THEME_FONT_LARGE, theme::TEXT);
  s_card = label(col, "", THEME_FONT_LARGE, theme::ACCENT);
  s_hint = noteLabel(col, "");
  lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
  refreshCompass();
}

// From loop(), once a second.
void UITask::refreshCompass() {
  using namespace compassview;
  if (_screen != SCR_COMPASS || !s_dial) return;
  int32_t lat, lon;
  int cog = 0;
  const char* hint = nullptr;
  if (!_core->course.currentLocation(lat, lon))
    hint = _core->gpsAvailable() && !_core->gpsEnabled() ? "GPS is off - turn it on in Settings" : "Waiting for a GPS fix";
  else if (!_core->course.currentCourse(cog)) hint = "Move to set the heading";
  bool have = hint == nullptr;
  if (have != s_have || cog != s_cog) {
    s_have = have;
    if (have) s_cog = cog;
    lv_obj_invalidate(s_dial);
  }
  if (have) {
    setTextFmt(s_deg, "%03d", cog);
    setText(s_degsign, "\xC2\xB0");
    setText(s_card, geo::bearingCardinal(cog));
    setText(s_hint, "Course over ground");
  } else {
    setText(s_deg, "");
    setText(s_degsign, "");
    setText(s_card, "--");
    setText(s_hint, hint);
  }
}
