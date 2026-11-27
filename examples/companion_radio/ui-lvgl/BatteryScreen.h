#pragma once
// Settings › Power › Battery curve: the cell voltage at 0, 10, ... 100 %
// (NodePrefs::batt_curve_mv, ui-core/Battery.h) as a chart. A tap selects
// the point nearest it along the % axis: ▲ / ▼ appear by it, each tap 0.01 V
// (held, they repeat), its value beside them. Dragging a point still moves it
// roughly. Points stay between their neighbours; the first change copies the
// built-in curve in, Reset goes back to it. A dashed line marks the cell now,
// so a point can be set against what the battery reads. (The whole curve at
// once: the console's batt.curve.) Included by UITask.cpp.

namespace battview {
static lv_obj_t* s_chart = nullptr;
static lv_obj_t* s_now = nullptr;     // "Now 3912 mV  76 %"
static lv_obj_t* s_reset = nullptr;   // header button's label
static int s_sel = -1;                // the point selected (its arrows shown)
static bool s_drag = false;           // the finger moving it
static int s_arrow = 0;               // +1 / -1: an arrow held
static uint32_t s_next_ms = 0;        // its next repeat
static lv_point_t s_down = { 0, 0 };  // where the finger went down
static const int ARR_W = 36, ARR_H = 30;   // an arrow's touch box
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

// The selected point's arrows: above and below it, or -- with no room there
// (the chart's top or bottom) -- beside it. `right`: where its value goes.
struct Arrows { lv_area_t up, dn; bool right; };
static Arrows arrowsAt(const lv_area_t& a, int i) {
  const int x = ptX(a, i), y = mvY(a, battery::curveMvAt(i));
  Arrows r;
  r.right = x < (a.x1 + a.x2) / 2;   // the value on the roomier side
  int ux = x, uy = y - 6 - ARR_H / 2, dy = y + 6 + ARR_H / 2;
  if (uy - ARR_H / 2 < a.y1 || dy + ARR_H / 2 > a.y2 - PAD_B + 6) {   // beside it, one over the other
    ux = r.right ? x + 10 + ARR_W / 2 : x - 10 - ARR_W / 2;
    uy = y - ARR_H / 2 - 1;
    dy = y + ARR_H / 2 + 1;
    if (uy - ARR_H / 2 < a.y1) { uy = a.y1 + ARR_H / 2; dy = uy + ARR_H + 2; }
    if (dy + ARR_H / 2 > a.y2) { dy = a.y2 - ARR_H / 2; uy = dy - ARR_H - 2; }
  }
  r.up = { ux - ARR_W / 2, uy - ARR_H / 2, ux + ARR_W / 2 - 1, uy + ARR_H / 2 - 1 };
  r.dn = { ux - ARR_W / 2, dy - ARR_H / 2, ux + ARR_W / 2 - 1, dy + ARR_H / 2 - 1 };
  return r;
}
static bool inArea(const lv_area_t& r, const lv_point_t& p) { return p.x >= r.x1 && p.x <= r.x2 && p.y >= r.y1 && p.y <= r.y2; }

// The selected point a step up (+1) or down (-1), 10 mV, between its neighbours.
static bool step(int dir) {
  NodePrefs* np = s_ui->prefsMut();
  if (!np || s_sel < 0) return false;
  uint16_t* c = np->batt_curve_mv;
  if (!battery::validCurve(c))
    for (int k = 0; k < PTS; k++) c[k] = (uint16_t)battery::builtinMvAt(k);
  const int lo = s_sel > 0 ? c[s_sel - 1] + 10 : battery::CURVE_MIN_MV;
  const int hi = s_sel < PTS - 1 ? c[s_sel + 1] - 10 : battery::CURVE_MAX_MV;
  const int mv = constrain((int)c[s_sel] + dir * 10, lo, hi);
  if (mv == c[s_sel]) return false;
  c[s_sel] = (uint16_t)mv;
  battlearn::markManual(*np);   // learning leaves it now
  return true;
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
    r.bg_color = lv_color_hex(i == s_sel ? theme::ACCENT : theme::BG);
    r.border_color = lv_color_hex(theme::ACCENT);
    r.border_width = 2;
    const int d = i == s_sel ? 7 : 4, x = ptX(a, i), y = mvY(a, battery::curveMvAt(i));
    lv_area_t c = { x - d, y - d, x + d, y + d };
    lv_draw_rect(layer, &r, &c);
  }
  if (s_sel >= 0) {   // the selected point: its arrows, and its value on the roomier side
    const Arrows ar = arrowsAt(a, s_sel);
    for (int k = 0; k < 2; k++) {
      const lv_area_t& b = k ? ar.dn : ar.up;
      const bool held = s_arrow == (k ? -1 : 1);
      lv_draw_rect_dsc_t bg;
      lv_draw_rect_dsc_init(&bg);
      bg.radius = theme::RADIUS_SM;
      bg.bg_color = lv_color_hex(held ? theme::ACCENT : theme::SURFACE_2);
      lv_area_t in = { b.x1 + 3, b.y1 + 2, b.x2 - 3, b.y2 - 2 };   // drawn a little inside its touch box
      lv_draw_rect(layer, &bg, &in);
      lv_draw_label_dsc_t d;
      lv_draw_label_dsc_init(&d);
      d.font = THEME_FONT_BODY;
      d.color = lv_color_hex(held ? theme::BG : theme::TEXT);
      d.align = LV_TEXT_ALIGN_CENTER;
      d.text = k ? LV_SYMBOL_DOWN : LV_SYMBOL_UP;
      lv_area_t t2 = { in.x1, (in.y1 + in.y2) / 2 - 8, in.x2, (in.y1 + in.y2) / 2 + 9 };
      lv_draw_label(layer, &d, &t2);
    }
    const int mv = battery::curveMvAt(s_sel);
    snprintf(t, sizeof(t), "%d%%  %d.%02d V", s_sel * 10, mv / 1000, mv % 1000 / 10);
    const int x = ptX(a, s_sel), cy = (ar.up.y2 + ar.dn.y1) / 2, LW = 92;
    const bool beside = ar.up.x1 > x || ar.up.x2 < x;   // the arrows: beside the point, the value past them
    int lx = ar.right ? (beside ? ar.up.x2 + 6 : x + 12) : (beside ? ar.up.x1 - 6 - LW : x - 12 - LW);
    lx = constrain(lx, (int)a.x1 + PAD_L, (int)a.x2 - LW);
    lv_draw_rect_dsc_t bg;   // on a pill, clear of the grid and the "now" line
    lv_draw_rect_dsc_init(&bg);
    bg.radius = theme::RADIUS_SM;
    bg.bg_color = lv_color_hex(theme::SURFACE_2);
    lv_area_t ba = { lx, cy - 9, lx + LW - 1, cy + 9 };
    lv_draw_rect(layer, &bg, &ba);
    text(layer, t, lx, cy - 8, LW, LV_TEXT_ALIGN_CENTER, theme::TEXT);
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
  bool changed = false;
  if (code == LV_EVENT_PRESSED) {
    s_down = p;
    s_drag = false;
    s_arrow = 0;
    if (s_sel >= 0) {   // the selected point's arrows first: they lie over its neighbours
      const Arrows ar = arrowsAt(a, s_sel);
      s_arrow = inArea(ar.up, p) ? 1 : inArea(ar.dn, p) ? -1 : 0;
    }
    if (s_arrow) {
      changed = step(s_arrow);
      s_next_ms = millis() + 400;   // held: repeats after this
    } else {   // the point nearest along the % axis, if the finger is near the curve at all
      int best = 0;
      for (int i = 1; i < PTS; i++) if (abs(ptX(a, i) - p.x) < abs(ptX(a, best) - p.x)) best = i;
      const bool near = abs(ptX(a, best) - p.x) <= 16 && abs(mvY(a, battery::curveMvAt(best)) - p.y) <= 40;
      s_sel = near ? best : -1;
    }
    lv_obj_invalidate(obj);
  }
  if (code == LV_EVENT_PRESSING) {
    if (s_arrow && (int32_t)(millis() - s_next_ms) >= 0) {
      s_next_ms = millis() + 80;
      changed = step(s_arrow);
    } else if (!s_arrow && s_sel >= 0 && (s_drag || abs(p.y - s_down.y) > 8)) {   // a drag: coarse, to the finger
      s_drag = true;
      NodePrefs* np = s_ui->prefsMut();
      if (np) {
        uint16_t* c = np->batt_curve_mv;
        if (!battery::validCurve(c))
          for (int k = 0; k < PTS; k++) c[k] = (uint16_t)battery::builtinMvAt(k);
        const int lo = s_sel > 0 ? c[s_sel - 1] + 10 : battery::CURVE_MIN_MV;
        const int hi = s_sel < PTS - 1 ? c[s_sel + 1] - 10 : battery::CURVE_MAX_MV;
        const int mv = constrain((yMv(a, p.y) + 5) / 10 * 10, lo, hi);
        if (mv != c[s_sel]) { c[s_sel] = (uint16_t)mv; battlearn::markManual(*np); changed = true; }
      }
    }
  }
  if (changed) { lv_obj_invalidate(obj); s_ui->refreshBattCurve(); }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    const bool save = s_arrow || s_drag;
    s_arrow = 0;
    s_drag = false;
    lv_obj_invalidate(obj);
    if (save) prefsSave();
  }
}

static void onReset(lv_event_t* e) {
  (void)e;
  NodePrefs* np = s_ui->prefsMut();
  if (!np || !battery::validCurve(np->batt_curve_mv)) return;
  memset(np->batt_curve_mv, 0, sizeof(np->batt_curve_mv));
  battlearn::markBuiltin(*np);
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
  lv_obj_set_scrollable(body, false);
  s_chart = lv_obj_create(body);
  styleSurface(s_chart, theme::BG);
  lv_obj_set_scrollable(s_chart, false);
  lv_obj_set_width(s_chart, LV_PCT(100));
  lv_obj_set_flex_grow(s_chart, 1);
  lv_obj_set_clickable(s_chart, true);
  lv_obj_set_press_lock(s_chart, true);   // still its drag when the finger strays off it
  lv_obj_add_event_cb(s_chart, onDraw, LV_EVENT_DRAW_MAIN_END, NULL);
  lv_obj_add_event_cb(s_chart, onTouch, LV_EVENT_ALL, NULL);
  s_now = noteLabel(body, "");
  s_sel = -1;
  s_drag = false;
  s_arrow = 0;
  refreshBattCurve();
}

// Once a second from loop(), and after a change: the cell now, the reset's state.
void UITask::refreshBattCurve() {
  using namespace battview;
  if (_screen != SCR_BATT || !s_now) return;
  const int mv = battMv();
  const bool custom = _prefs && battery::validCurve(_prefs->batt_curve_mv);
  char src[24], learn[64] = "", t[128];
  battlearn::sourceText(_prefs ? _prefs->batt_curve_mv : nullptr, _prefs ? _prefs->batt_curve_src : 0,
                        _prefs ? _prefs->batt_learn_runs : 0, src, sizeof(src));
#if BATT_LEARN
  if (_prefs && _prefs->batt_learn) {
    if (_core->battLearning()) _core->batt_learn.longText(millis(), learn, sizeof(learn));
    else snprintf(learn, sizeof(learn), "Not learning: set by hand (Reset to learn)");
  }
#endif
  if (mv > 0) snprintf(t, sizeof(t), "Now %d mV  -  %d %%  -  %s", mv, battery::rawPercent(mv), src);
  else snprintf(t, sizeof(t), "%s  -  tap a point, then its arrows", src);
  if (learn[0]) { strncat(t, "\n", sizeof(t) - strlen(t) - 1); strncat(t, learn, sizeof(t) - strlen(t) - 1); }
  setText(s_now, t);
  const lv_opa_t ro = custom ? LV_OPA_COVER : LV_OPA_40;
  if (s_reset && lv_obj_get_style_text_opa(s_reset, LV_PART_MAIN) != ro) lv_obj_set_style_text_opa(s_reset, ro, 0);
  // The "now" line moves with the cell: redrawn when the reading changes (the
  // smoothed one, every few seconds), not each second.
  static int s_drawn_mv = -1;
  if (s_chart && !s_drag && !s_arrow && mv != s_drawn_mv) { s_drawn_mv = mv; lv_obj_invalidate(s_chart); }
}
