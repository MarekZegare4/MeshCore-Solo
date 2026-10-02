#pragma once
// Settings › Power › Battery curve: the cell voltage at 0, 10, ... 100 %
// (NodePrefs::batt_curve_mv, ui-core/Battery.h) as a chart. A finger takes
// the point nearest it along the % axis and drags it up or down in 10 mV
// steps, kept between its neighbours; the first drag copies the built-in
// curve in, Reset goes back to it. A dashed line marks the cell now, so a
// point can be set against what the battery reads. Included by UITask.cpp.

namespace battview {
static lv_obj_t* s_chart = nullptr;
static lv_obj_t* s_now = nullptr;     // "Now 3912 mV  76 %"
static lv_obj_t* s_reset = nullptr;   // header button's label
static int s_drag = -1;               // point under the finger
static const int PTS = battery::CURVE_PTS;
static const int MV_LO = 3000, MV_HI = 4400;   // the chart's voltage range
static const int PAD_L = 44, PAD_R = 12, PAD_T = 10, PAD_B = 22;

static int ptX(const lv_area_t& a, int i) { return a.x1 + PAD_L + i * (lv_area_get_width(&a) - PAD_L - PAD_R) / (PTS - 1); }
static int mvY(const lv_area_t& a, int mv) {
  const int h = lv_area_get_height(&a) - PAD_T - PAD_B;
  return a.y2 - PAD_B - (constrain(mv, MV_LO, MV_HI) - MV_LO) * h / (MV_HI - MV_LO);
}
static int yMv(const lv_area_t& a, int y) {
  const int h = lv_area_get_height(&a) - PAD_T - PAD_B;
  return MV_LO + (a.y2 - PAD_B - y) * (MV_HI - MV_LO) / h;
}

static void text(lv_layer_t* layer, const char* t, int x, int y, int w, lv_text_align_t align, uint32_t col) {
  lv_draw_label_dsc_t d;
  lv_draw_label_dsc_init(&d);
  d.font = THEME_FONT_SMALL;
  d.color = lv_color_hex(col);
  d.align = align;
  d.text = t;
  d.text_local = 1;
  lv_area_t r = { x, y, x + w - 1, y + 16 };
  lv_draw_label(layer, &d, &r);
}
static void line(lv_layer_t* layer, int x1, int y1, int x2, int y2, uint32_t col, int w, int dash = 0) {
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(col);
  d.width = w;
  d.dash_width = dash;
  d.dash_gap = dash;
  d.p1.x = x1; d.p1.y = y1; d.p2.x = x2; d.p2.y = y2;
  lv_draw_line(layer, &d);
}

static void onDraw(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_current_target_obj(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  char t[16];
  for (int mv = 3200; mv <= MV_HI; mv += 400) {   // voltage grid
    const int y = mvY(a, mv);
    line(layer, a.x1 + PAD_L, y, a.x2 - PAD_R, y, theme::SURFACE_2, 1);
    snprintf(t, sizeof(t), "%d.%d V", mv / 1000, mv % 1000 / 100);
    text(layer, t, a.x1, y - 8, PAD_L - 6, LV_TEXT_ALIGN_RIGHT, theme::TEXT_MUTED);
  }
  for (int i = 0; i < PTS; i += 5) {   // 0, 50, 100 %
    snprintf(t, sizeof(t), "%d%%", i * 10);
    const int x = i == PTS - 1 ? a.x2 - 40 : ptX(a, i) - 20;   // the last one ends at the edge
    text(layer, t, x, a.y2 - PAD_B + 4, 40, i == PTS - 1 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, theme::TEXT_MUTED);
  }
  const int now = s_ui ? s_ui->battMv() : 0;
  if (now > 0) line(layer, a.x1 + PAD_L, mvY(a, now), a.x2 - PAD_R, mvY(a, now), theme::OK, 1, 3);
  for (int i = 1; i < PTS; i++)
    line(layer, ptX(a, i - 1), mvY(a, battery::curveMvAt(i - 1)), ptX(a, i), mvY(a, battery::curveMvAt(i)), theme::ACCENT, 2);
  for (int i = 0; i < PTS; i++) {
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = LV_RADIUS_CIRCLE;
    r.bg_color = lv_color_hex(i == s_drag ? theme::ACCENT : theme::BG);
    r.border_color = lv_color_hex(theme::ACCENT);
    r.border_width = 2;
    const int d = i == s_drag ? 7 : 4, x = ptX(a, i), y = mvY(a, battery::curveMvAt(i));
    lv_area_t c = { x - d, y - d, x + d, y + d };
    lv_draw_rect(layer, &r, &c);
  }
  if (s_drag >= 0) {   // the point being moved, above it
    const int x = ptX(a, s_drag), y = mvY(a, battery::curveMvAt(s_drag));
    snprintf(t, sizeof(t), "%d%%  %d mV", s_drag * 10, battery::curveMvAt(s_drag));
    const int lx = constrain(x - 50, (int)a.x1, (int)a.x2 - 100);
    lv_draw_rect_dsc_t bg;   // on a pill, clear of the grid and the "now" line
    lv_draw_rect_dsc_init(&bg);
    bg.radius = theme::RADIUS_SM;
    bg.bg_color = lv_color_hex(theme::SURFACE_2);
    lv_area_t ba = { lx, y - 30, lx + 99, y - 12 };
    lv_draw_rect(layer, &bg, &ba);
    text(layer, t, lx, y - 29, 100, LV_TEXT_ALIGN_CENTER, theme::TEXT);
  }
}

static void onTouch(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* obj = lv_event_get_current_target_obj(e);
  lv_indev_t* in = lv_indev_active();
  lv_point_t p = { 0, 0 };
  if (in) lv_indev_get_point(in, &p);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  if (code == LV_EVENT_PRESSED) {   // the point nearest along the % axis
    int best = 0;
    for (int i = 1; i < PTS; i++) if (abs(ptX(a, i) - p.x) < abs(ptX(a, best) - p.x)) best = i;
    s_drag = best;
  }
  NodePrefs* np = s_ui->prefsMut();
  if ((code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) && s_drag >= 0 && np) {
    uint16_t* c = np->batt_curve_mv;
    if (!battery::validCurve(c))
      for (int k = 0; k < PTS; k++) c[k] = (uint16_t)battery::builtinMvAt(k);
    const int lo = s_drag > 0 ? c[s_drag - 1] + 10 : battery::CURVE_MIN_MV;
    const int hi = s_drag < PTS - 1 ? c[s_drag + 1] - 10 : battery::CURVE_MAX_MV;
    const int mv = constrain((yMv(a, p.y) + 5) / 10 * 10, lo, hi);
    if (mv != c[s_drag]) { c[s_drag] = (uint16_t)mv; lv_obj_invalidate(obj); s_ui->refreshBattCurve(); }
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    s_drag = -1;
    lv_obj_invalidate(obj);
    prefsSave();
  }
}

static void onReset(lv_event_t* e) {
  (void)e;
  NodePrefs* np = s_ui->prefsMut();
  if (!np || !battery::validCurve(np->batt_curve_mv)) return;
  memset(np->batt_curve_mv, 0, sizeof(np->batt_curve_mv));
  prefsSave();
  if (s_chart) lv_obj_invalidate(s_chart);
  s_ui->refreshBattCurve();
}
}  // namespace battview

void UITask::showBattCurve() {
  using namespace battview;
  _screen = SCR_BATT;
  lv_obj_t* body = newScreen("Battery curve", true);
  headerButton(_header, LV_SYMBOL_REFRESH " Reset", onReset, 8, &s_reset);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  s_chart = lv_obj_create(body);
  styleSurface(s_chart, theme::BG);
  lv_obj_remove_flag(s_chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(s_chart, LV_PCT(100));
  lv_obj_set_flex_grow(s_chart, 1);
  lv_obj_add_flag(s_chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_chart, LV_OBJ_FLAG_PRESS_LOCK);   // still its drag when the finger strays off it
  lv_obj_add_event_cb(s_chart, onDraw, LV_EVENT_DRAW_MAIN_END, NULL);
  lv_obj_add_event_cb(s_chart, onTouch, LV_EVENT_ALL, NULL);
  s_now = noteLabel(body, "");
  s_drag = -1;
  refreshBattCurve();
}

// Once a second from loop(), and after a change: the cell now, the reset's state.
void UITask::refreshBattCurve() {
  using namespace battview;
  if (_screen != SCR_BATT || !s_now) return;
  const int mv = battMv();
  const bool custom = _prefs && battery::validCurve(_prefs->batt_curve_mv);
  char t[64];
  if (mv > 0) snprintf(t, sizeof(t), "Now %d mV  -  %d %%  -  %s curve", mv, battery::rawPercent(mv), custom ? "your" : "LiPo");
  else snprintf(t, sizeof(t), "%s curve  -  drag a point up or down", custom ? "Your" : "LiPo");
  lv_label_set_text(s_now, t);
  if (s_reset) lv_obj_set_style_text_opa(s_reset, custom ? LV_OPA_COVER : LV_OPA_40, 0);
  if (s_chart && s_drag < 0) lv_obj_invalidate(s_chart);   // the "now" line moves with the cell
}
