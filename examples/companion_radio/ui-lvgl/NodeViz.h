#pragma once
// Node detail's picture card, over its info rows: where the node is, on a
// small compass rose (north up, an arrow towards it), and how a message gets
// there, a chain from you to it with a ring per repeater on the way. Either half shows only when
// there's something to draw (a position of both ends / a contact's path).
// ui-new draws the same on e-ink (NearbyScreen.h).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp before the node detail.

namespace nodeviz {

static const int ROSE_W = 64, ROSE_H = 76;   // the ring, N over it
static lv_obj_t* s_card = nullptr;
static lv_obj_t* s_rose = nullptr;
static lv_obj_t* s_path_col = nullptr;
static lv_obj_t* s_chain = nullptr;
static lv_obj_t* s_path_txt = nullptr;
static int s_bearing = -1;   // degrees, -1: no position
static int s_hops = -1;      // 0..63, 0xFF flood, -1: not a contact

static void onRoseDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const float r = ROSE_W / 2.0f - 8, cx = (a.x1 + a.x2) / 2.0f, cy = a.y2 - r - 3;

  lv_draw_arc_dsc_t ad;   // the ring
  lv_draw_arc_dsc_init(&ad);
  ad.center.x = (int32_t)cx; ad.center.y = (int32_t)cy;
  ad.radius = (uint16_t)r;
  ad.start_angle = 0; ad.end_angle = 360;
  ad.width = 1;
  ad.color = lv_color_hex(theme::TEXT_MUTED);
  lv_draw_arc(layer, &ad);

  lv_draw_line_dsc_t ld;  // a tick every 45 degrees, longer at the cardinals
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(theme::TEXT_MUTED);
  for (int deg = 0; deg < 360; deg += 45) {
    const float ang = deg * (float)M_PI / 180.0f, sx = sinf(ang), sy = -cosf(ang);
    const float len = deg % 90 == 0 ? 6 : 3;
    ld.width = deg % 90 == 0 ? 2 : 1;
    ld.p1.x = cx + sx * r;         ld.p1.y = cy + sy * r;
    ld.p2.x = cx + sx * (r - len); ld.p2.y = cy + sy * (r - len);
    lv_draw_line(layer, &ld);
  }

  lv_draw_label_dsc_t td;  // N above the ring
  lv_draw_label_dsc_init(&td);
  td.align = LV_TEXT_ALIGN_CENTER;
  td.font = THEME_FONT_SMALL;
  td.color = lv_color_hex(theme::FAIL);
  td.text = "N";
  const int32_t h = lv_font_get_line_height(td.font);
  lv_area_t la = { (int32_t)cx - 10, (int32_t)(cy - r) - h - 1, (int32_t)cx + 10, (int32_t)(cy - r) - 1 };
  lv_draw_label(layer, &td, &la);

  if (s_bearing < 0) return;
  const float ang = s_bearing * (float)M_PI / 180.0f, sx = sinf(ang), sy = -cosf(ang);
  const float tip = r - 4, base = tip - 12;
  ld.color = lv_color_hex(theme::ACCENT);   // the arrow: a shaft from the centre, a head at the ring
  ld.width = 3;
  ld.round_start = ld.round_end = 1;
  ld.p1.x = cx - sx * (r * 0.35f); ld.p1.y = cy - sy * (r * 0.35f);
  ld.p2.x = cx + sx * base;        ld.p2.y = cy + sy * base;
  lv_draw_line(layer, &ld);
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = lv_color_hex(theme::ACCENT);
  tri.opa = LV_OPA_COVER;
  tri.p[0].x = cx + sx * tip;               tri.p[0].y = cy + sy * tip;
  tri.p[1].x = cx + sx * base + sy * 7;     tri.p[1].y = cy + sy * base - sx * 7;
  tri.p[2].x = cx + sx * base - sy * 7;     tri.p[2].y = cy + sy * base + sx * 7;
  lv_draw_triangle(layer, &tri);
}

// You and the node filled in the accent, the repeaters between as rings,
// dotted links; as many rings as fit, the count says the rest.
static void onChainDraw(lv_event_t* e) {
  if (s_hops < 0 || s_hops == 0xFF) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const int R = 6, PITCH = 26, w = a.x2 - a.x1 + 1, cy = (a.y1 + a.y2) / 2;
  const int cap = (w - 2 * R) / PITCH - 1;
  const int shown = s_hops > cap ? (cap < 1 ? 1 : cap) : s_hops, n = shown + 2;
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(theme::TEXT_MUTED);
  ld.width = 2;
  ld.dash_width = 3; ld.dash_gap = 3;
  ld.p1.x = a.x1 + R; ld.p1.y = cy;
  ld.p2.x = a.x1 + R + (n - 1) * PITCH; ld.p2.y = cy;
  lv_draw_line(layer, &ld);
  for (int k = 0; k < n; k++) {
    const bool end = k == 0 || k == n - 1;
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    rd.radius = LV_RADIUS_CIRCLE;
    rd.bg_color = lv_color_hex(end ? theme::ACCENT : theme::SURFACE);
    rd.border_color = lv_color_hex(end ? theme::ACCENT : theme::TEXT_MUTED);
    rd.border_width = 2;
    const int x = a.x1 + R + k * PITCH;
    lv_area_t ra = { x - R, cy - R, x + R, cy + R };
    lv_draw_rect(layer, &rd, &ra);
  }
}

}  // namespace nodeviz

// The card, over the node's info rows. Filled by nodeVizSet().
static void nodeVizBuild(lv_obj_t* parent) {
  using namespace nodeviz;
  s_card = lv_obj_create(parent);
  styleSurface(s_card, theme::SURFACE);
  lv_obj_remove_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_card, LV_OBJ_FLAG_CLICKABLE);
  fillWidth(s_card);
  lv_obj_set_height(s_card, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(s_card, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(s_card, 6, 0);
  lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(s_card, 12, 0);

  s_rose = lv_obj_create(s_card);
  lv_obj_remove_style_all(s_rose);
  lv_obj_remove_flag(s_rose, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_rose, ROSE_W, ROSE_H);
  lv_obj_add_event_cb(s_rose, onRoseDraw, LV_EVENT_DRAW_MAIN_END, NULL);

  s_path_col = lv_obj_create(s_card);
  lv_obj_remove_style_all(s_path_col);
  lv_obj_remove_flag(s_path_col, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_width(s_path_col, 1);
  lv_obj_set_flex_grow(s_path_col, 1);
  lv_obj_set_height(s_path_col, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(s_path_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(s_path_col, 4, 0);
  label(s_path_col, "Path", THEME_FONT_SMALL, theme::TEXT_MUTED);
  s_chain = lv_obj_create(s_path_col);
  lv_obj_remove_style_all(s_chain);
  lv_obj_remove_flag(s_chain, LV_OBJ_FLAG_CLICKABLE);
  fillWidth(s_chain);
  lv_obj_set_height(s_chain, 16);
  lv_obj_add_event_cb(s_chain, onChainDraw, LV_EVENT_DRAW_MAIN_END, NULL);
  s_path_txt = label(s_path_col, "", THEME_FONT_BODY, theme::TEXT);
}

// `bearing` in degrees or -1; `hops` 0..63, 0xFF (flood) or -1 (no contact).
static void nodeVizSet(int bearing, int hops) {
  using namespace nodeviz;
  if (!s_card) return;
  if (bearing != s_bearing) { s_bearing = bearing; lv_obj_invalidate(s_rose); }
  if (hops != s_hops) {
    s_hops = hops;
    lv_obj_invalidate(s_chain);
    char t[24];
    if (hops == 0xFF) snprintf(t, sizeof(t), "Flood, no route yet");
    else if (hops == 0) snprintf(t, sizeof(t), "Direct");
    else snprintf(t, sizeof(t), "%d hop%s", hops, hops == 1 ? "" : "s");
    setText(s_path_txt, t);
    if (hops == 0xFF) lv_obj_add_flag(s_chain, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s_chain, LV_OBJ_FLAG_HIDDEN);
  }
  if (bearing < 0) lv_obj_add_flag(s_rose, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s_rose, LV_OBJ_FLAG_HIDDEN);
  if (hops < 0) lv_obj_add_flag(s_path_col, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s_path_col, LV_OBJ_FLAG_HIDDEN);
  if (bearing < 0 && hops < 0) lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(s_card, LV_OBJ_FLAG_HIDDEN);
}
