#pragma once
// Home > GPS (also Settings > Connections > GPS): what the receiver sees, on
// three tabs in the header, each a screenful. Sky: a plot of the satellites
// in view (centre = overhead, rings at 30 and 60 degrees elevation, filled =
// used in the fix, colour = signal) beside the fix state. Signal: a bar per
// satellite with its C/N0 (dB-Hz) -- the number that tells a weak antenna or
// a noisy board from a sky that just isn't visible -- and the constellations.
// Details: whatever else the receiver reports, a row each once it does.
// Refreshes every second.
//
// The data is helpers/sensors/GpsSky.h (-D GPS_SKYVIEW), fed by the board's
// NMEA stream; the sim feeds it made-up NMEA so the parser runs there too.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after CompassScreen.h.

#include <helpers/sensors/GpsSky.h>

namespace gpsview {

enum : uint8_t { TAB_SKY, TAB_SIGNAL, TAB_DETAILS, TAB_COUNT };
static uint8_t s_tab = TAB_SKY;   // kept across visits

static lv_obj_t* s_sky = nullptr;      // sky plot (drawn)
static lv_obj_t* s_bars = nullptr;     // signal bars (drawn, the box's width or as wide as the satellites need)
static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_used = nullptr;
static lv_obj_t* s_on_btn = nullptr;
// Sky, beside the plot: in view, search time / time to first fix, UTC.
static lv_obj_t *s_inview = nullptr, *s_search = nullptr, *s_ttff = nullptr, *s_utc = nullptr;
// Signal: one pill per constellation.
static lv_obj_t* s_sys_pill[GpsSky::SYS_COUNT];
// Details.
static lv_obj_t *s_quality = nullptr, *s_pos = nullptr, *s_grid = nullptr, *s_alt = nullptr,
                *s_speed = nullptr, *s_course = nullptr, *s_hdop = nullptr, *s_vdop = nullptr,
                *s_pdop = nullptr, *s_cn0 = nullptr, *s_best = nullptr, *s_nmea = nullptr, *s_rx = nullptr;
static lv_obj_t *s_d_fix = nullptr, *s_d_ttff = nullptr, *s_d_search = nullptr, *s_d_utc = nullptr;
static const int SKY = 168;
static const int BAR_PITCH = 26, BAR_W = 16, BARS_H = 120, AXIS_W = 22;

// Snapshot the screen draws from, sorted: used first, then by signal.
static GpsSky::Sat s_sats[GpsSky::MAX_SATS];
static bool s_used_flag[GpsSky::MAX_SATS];
static int s_n = 0;

// Fixed signal colours (not the accent: the user may pick green or red).
static uint32_t snrColor(int snr) {
  if (snr < 0)  return theme::TEXT_MUTED;
  if (snr < 20) return 0xE5534B;
  if (snr < 30) return 0xE5C04B;
  return 0x6FCF6F;
}

#if defined(SIM_PLATFORM) && defined(GPS_SKYVIEW)
  #include <helpers/sensors/GpsSkySim.h>
#endif

// The receiver's data, or nullptr on a board without the parser.
static GpsSky* sky() {
#if defined(SEEED_WIO_TRACKER_L2) && defined(GPS_SKYVIEW)
  return &gps.sky();
#elif defined(SIM_PLATFORM) && defined(GPS_SKYVIEW)
  return gpsSkySim();
#else
  return nullptr;
#endif
}

static void snapshot(GpsSky& g) {
  g.expire();
  s_n = g.count;
  int idx[GpsSky::MAX_SATS];
  for (int i = 0; i < s_n; i++) idx[i] = i;
  // Insertion sort (<= 64): used first, then strongest; untracked last.
  for (int i = 1; i < s_n; i++) {
    int v = idx[i], j = i - 1;
    auto key = [&](int k) { return (g.used(g.sats[k]) ? 1000 : 0) + g.sats[k].snr; };
    while (j >= 0 && key(idx[j]) < key(v)) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = v;
  }
  for (int i = 0; i < s_n; i++) { s_sats[i] = g.sats[idx[i]]; s_used_flag[i] = g.used(g.sats[idx[i]]); }
}

static void dot(lv_layer_t* layer, int32_t x, int32_t y, int32_t r, uint32_t color, bool filled) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.radius = LV_RADIUS_CIRCLE;
  d.bg_color = lv_color_hex(color);
  d.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
  d.border_color = lv_color_hex(color);
  d.border_width = 2;
  d.border_opa = LV_OPA_COVER;
  lv_area_t a = { x - r, y - r, x + r, y + r };
  lv_draw_rect(layer, &d, &a);
}

}  // namespace gpsview

static void onOpenGps(lv_event_t* e) { (void)e; s_ui->showGps(false); }
static void onOpenGpsFromSettings(lv_event_t* e) { (void)e; s_ui->showGps(true); }
static void onGpsTurnOn(lv_event_t* e) { (void)e; s_ui->setGps(true); s_ui->refreshGps(); }

// Sky plot: horizon ring, 30 / 60 degree rings, N-S / E-W lines, one dot per
// satellite at its azimuth (N up) and elevation (overhead in the centre).
static void onGpsSkyDraw(lv_event_t* e) {
  using namespace gpsview;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
  int32_t r = SKY / 2 - 10;

  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = cx; arc.center.y = cy;
  arc.start_angle = 0; arc.end_angle = 360;
  arc.color = lv_color_hex(theme::SURFACE_2);
  for (int el = 0; el < 90; el += 30) {
    arc.radius = (uint16_t)(r * (90 - el) / 90);
    arc.width = el == 0 ? 2 : 1;
    lv_draw_arc(layer, &arc);
  }
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = lv_color_hex(theme::SURFACE_2);
  ld.width = 1;
  ld.p1.x = cx; ld.p1.y = cy - r; ld.p2.x = cx; ld.p2.y = cy + r;
  lv_draw_line(layer, &ld);
  ld.p1.x = cx - r; ld.p1.y = cy; ld.p2.x = cx + r; ld.p2.y = cy;
  lv_draw_line(layer, &ld);

  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.font = THEME_FONT_SMALL;
  td.align = LV_TEXT_ALIGN_CENTER;
  static const char* CARD[4] = { "N", "E", "S", "W" };
  int32_t lh = lv_font_get_line_height(td.font);
  for (int i = 0; i < 4; i++) {
    float ang = i * (float)M_PI / 2;
    int32_t px = cx + (int32_t)(sinf(ang) * (r + 1)), py = cy - (int32_t)(cosf(ang) * (r + 1));
    td.text = CARD[i];
    td.color = lv_color_hex(i == 0 ? theme::ACCENT : theme::TEXT_MUTED);
    lv_area_t la = { px - 8, py - lh / 2, px + 8, py + lh / 2 };
    lv_draw_rect_dsc_t bg;   // a patch of background so the ring doesn't cross the letter
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_hex(theme::BG);
    bg.radius = 4;
    lv_area_t ba = { px - 6, py - lh / 2 + 1, px + 6, py + lh / 2 - 1 };
    lv_draw_rect(layer, &bg, &ba);
    lv_draw_label(layer, &td, &la);
  }

  // Unused first, so the used ones sit on top where they overlap.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < s_n; i++) {
      const GpsSky::Sat& s = s_sats[i];
      if (s.elev < 0 || s.azim < 0 || s_used_flag[i] != (pass == 1)) continue;
      float rr = r * (90 - (s.elev > 90 ? 90 : s.elev)) / 90.0f;
      float ang = s.azim * (float)M_PI / 180.0f;
      dot(layer, cx + (int32_t)(sinf(ang) * rr), cy - (int32_t)(cosf(ang) * rr), 5, snrColor(s.snr), s_used_flag[i]);
    }
  }
}

// One bar per satellite: C/N0 on top, the bar (solid = used in the fix), the
// constellation letter and number under it. 50 dB-Hz fills the height.
static void onGpsBarsDraw(lv_event_t* e) {
  using namespace gpsview;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.font = THEME_FONT_SMALL;
  td.align = LV_TEXT_ALIGN_CENTER;
  int32_t lh = lv_font_get_line_height(td.font);
  int32_t base = a.y2 - lh - 2, top = a.y1 + lh + 2, full = base - top;

  lv_draw_line_dsc_t ld;   // a line every 10 dB-Hz, 30 dashed: above it, a good signal
  lv_draw_line_dsc_init(&ld);
  ld.width = 1;
  ld.p1.x = a.x1; ld.p2.x = a.x2;
  for (int v = 10; v <= 50; v += 10) {
    ld.color = lv_color_hex(v == 30 ? theme::TEXT_MUTED : theme::mix(theme::TEXT_MUTED, theme::BG, 22));
    ld.dash_width = ld.dash_gap = v == 30 ? 3 : 0;
    ld.p1.y = ld.p2.y = base - full * v / 50;
    lv_draw_line(layer, &ld);
  }

  for (int i = 0; i < s_n; i++) {
    const GpsSky::Sat& s = s_sats[i];
    int32_t x = a.x1 + i * BAR_PITCH + (BAR_PITCH - BAR_W) / 2;
    int snr = s.snr < 0 ? 0 : (s.snr > 50 ? 50 : s.snr);
    int32_t h = full * snr / 50;
    if (h < 2) h = 2;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.radius = 3;
    const uint32_t col = snrColor(s.snr);   // unused: the colour faded into the page, solid
    d.bg_color = lv_color_hex(s_used_flag[i] ? col : theme::mix(col, theme::BG, 30));
    d.border_color = lv_color_hex(col);
    d.border_width = s_used_flag[i] ? 0 : 1;
    lv_area_t ba = { x, base - h, x + BAR_W - 1, base };
    lv_draw_rect(layer, &d, &ba);

    char t[8];
    if (s.snr >= 0) {
      snprintf(t, sizeof(t), "%d", s.snr);
      td.text = t;
      td.color = lv_color_hex(theme::TEXT);
      lv_area_t va = { x - 6, base - h - lh - 1, x + BAR_W + 5, base - h - 1 };
      lv_draw_label(layer, &td, &va);
    }
    char p[8];
    snprintf(p, sizeof(p), "%c%d", GpsSky::sysLetter(s.sys), s.prn);
    td.text = p;
    td.color = lv_color_hex(s_used_flag[i] ? theme::TEXT : theme::TEXT_MUTED);
    lv_area_t pa = { x - (BAR_PITCH - BAR_W) / 2, base + 2, x + BAR_W + (BAR_PITCH - BAR_W) / 2 - 1, base + 2 + lh };
    lv_draw_label(layer, &td, &pa);
  }
}

// The bars' scale, left of them (they scroll, it stays): 0 to 50 dB-Hz.
static void onGpsAxisDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const int32_t lh = lv_font_get_line_height(THEME_FONT_SMALL);
  const int32_t base = a.y2 - lh - 2, full = base - (a.y1 + lh + 2);
  char t[4];
  for (int v = 0; v <= 50; v += 10) {
    snprintf(t, sizeof(t), "%d", v);
    const int32_t y = base - full * v / 50;
    chart::text(layer, t, a.x1, y - lh / 2, a.x2 - 4, LV_TEXT_ALIGN_RIGHT, theme::TEXT_MUTED);
  }
}

static void onGpsTab(lv_event_t* e) {
  gpsview::s_tab = (uint8_t)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e));
  s_ui->buildGps();
}

void UITask::showGps(bool from_settings) {
  _gps_from_settings = from_settings;
  _screen = SCR_GPS;
  buildGps();
}

void UITask::buildGps() {
  using namespace gpsview;
  lv_obj_t* body = newScreen("GPS", true);
  s_sky = s_bars = s_status = s_used = s_on_btn = nullptr;
  s_inview = s_search = s_ttff = s_utc = nullptr;
  s_quality = s_pos = s_grid = s_alt = s_speed = s_course = s_hdop = s_vdop = s_pdop = nullptr;
  s_cn0 = s_best = s_nmea = s_rx = s_d_fix = s_d_ttff = s_d_search = s_d_utc = nullptr;
  for (lv_obj_t*& p : s_sys_pill) p = nullptr;
  lv_obj_set_style_pad_row(body, 6, 0);
  if (_header) {
    static const char* TABS[] = { "Sky", "Signal", "Details", "" };
    lv_obj_t* tabs = segmented(_header, TABS, s_tab, 186, 28, theme::SURFACE);
    lv_obj_align(tabs, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(tabs, onGpsTab, LV_EVENT_VALUE_CHANGED, NULL);
  }

  if (s_tab == TAB_SKY) {   // the plot left, the fix right
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* top = lv_obj_create(body);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(top, theme::GAP, 0);
    s_sky = lv_obj_create(top);
    styleSurface(s_sky, theme::BG);
    lv_obj_remove_flag(s_sky, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_sky, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_sky, SKY, SKY);
    lv_obj_add_event_cb(s_sky, onGpsSkyDraw, LV_EVENT_DRAW_MAIN_END, NULL);

    lv_obj_t* col = lv_obj_create(top);
    lv_obj_remove_style_all(col);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_set_style_pad_top(col, 4, 0);
    s_status = label(col, "", THEME_FONT_LARGE, theme::TEXT);
    s_used = noteLabel(col, "", THEME_FONT_BODY, theme::TEXT);
    lv_obj_set_style_pad_bottom(s_used, 4, 0);
    lv_obj_t* facts = infoCard(col);   // no card of its own: rows on the page
    lv_obj_set_style_bg_opa(facts, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_hor(facts, 0, 0);
    s_inview = infoRow(facts, "In view", "", theme::TEXT, THEME_FONT_SMALL);
    s_search = infoRow(facts, "Searching", "", theme::TEXT, THEME_FONT_SMALL);
    s_ttff = infoRow(facts, "First fix", "", theme::TEXT, THEME_FONT_SMALL);
    s_utc = infoRow(facts, "UTC", "", theme::TEXT, THEME_FONT_SMALL);
    s_on_btn = lv_button_create(col);
    lv_obj_set_height(s_on_btn, 34);
    lv_obj_set_style_radius(s_on_btn, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(s_on_btn, 0, 0);
    lv_obj_center(label(s_on_btn, LV_SYMBOL_GPS "  Turn on", THEME_FONT_BODY, theme::TEXT));
    stylePrimary(s_on_btn);
    lv_obj_add_event_cb(s_on_btn, onGpsTurnOn, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_on_btn, LV_OBJ_FLAG_HIDDEN);
  } else if (s_tab == TAB_SIGNAL) {
    // The scale, then the bars, which scroll sideways when the satellites don't fit.
    lv_obj_t* row = lv_obj_create(body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), BARS_H + 8);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_t* ax = lv_obj_create(row);
    lv_obj_remove_style_all(ax);
    lv_obj_remove_flag(ax, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ax, AXIS_W, BARS_H);
    lv_obj_add_event_cb(ax, onGpsAxisDraw, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_t* sc = lv_obj_create(row);
    styleSurface(sc, theme::BG);
    lv_obj_set_height(sc, BARS_H + 8);
    lv_obj_set_flex_grow(sc, 1);
    lv_obj_set_scroll_dir(sc, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(sc, LV_SCROLLBAR_MODE_AUTO);
    s_bars = lv_obj_create(sc);
    lv_obj_remove_style_all(s_bars);
    lv_obj_remove_flag(s_bars, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bars, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(s_bars, BAR_PITCH, BARS_H);
    lv_obj_set_style_min_width(s_bars, LV_PCT(100), 0);   // the scale's lines span the box from the start
    lv_obj_add_event_cb(s_bars, onGpsBarsDraw, LV_EVENT_DRAW_MAIN_END, NULL);

    // Constellations: used / in view, a pill each (a pill never breaks in two).
    lv_obj_t* pills = lv_obj_create(body);
    lv_obj_remove_style_all(pills);
    lv_obj_set_size(pills, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(pills, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(pills, 6, 0);
    lv_obj_set_style_pad_row(pills, 6, 0);
    for (uint8_t k = 0; k < GpsSky::SYS_COUNT; k++) {
      lv_obj_t* p = label(pills, "", THEME_FONT_SMALL, theme::TEXT);
      lv_obj_set_style_bg_color(p, lv_color_hex(theme::SURFACE), 0);
      lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_pad_hor(p, 10, 0);
      lv_obj_set_style_pad_ver(p, 3, 0);
      lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
      s_sys_pill[k] = p;
    }
    noteLabel(body, "Solid = used in the fix. 30+ is strong; below 25, a fix is unlikely.");
  } else {   // Details: a row once the receiver reports it
    lv_obj_t* g = infoCard(body);
    s_d_fix = infoRow(g, "Fix", "");
    s_quality = infoRow(g, "Correction", "");
    s_d_ttff = infoRow(g, "First fix", "");
    s_d_search = infoRow(g, "Searching", "");
    s_d_utc = infoRow(g, "UTC", "");
    sectionTitle(body, "POSITION");
    g = infoCard(body);
    s_pos = infoRow(g, "Coordinates", "");
    s_grid = infoRow(g, "Locator", "");
    s_alt = infoRow(g, "Altitude", "");
    s_speed = infoRow(g, "Speed", "");
    s_course = infoRow(g, "Course", "");
    sectionTitle(body, "PRECISION");
    g = infoCard(body);
    s_hdop = infoRow(g, "Horizontal (HDOP)", "");
    s_vdop = infoRow(g, "Vertical (VDOP)", "");
    s_pdop = infoRow(g, "Overall (PDOP)", "");
    sectionTitle(body, "RECEIVER");
    g = infoCard(body);
    s_cn0 = infoRow(g, "Average signal, used", "");
    s_best = infoRow(g, "Strongest", "");
    s_nmea = infoRow(g, "NMEA sentences", "");
    s_rx = infoRow(g, "Data received", "");
  }
  refreshGps();
}

namespace gpsview {

// A dilution of precision and what it means (the usual bands).
static void dopText(float d, char* out, size_t n) {
  if (d <= 0) { out[0] = 0; return; }
  const char* w = d < 1 ? "ideal" : d < 2 ? "excellent" : d < 5 ? "good" : d < 10 ? "moderate" : d < 20 ? "fair" : "poor";
  snprintf(out, n, "%.1f  %s", d, w);
}

// The Maidenhead locator, 6 characters ("JO90ab").
static void locator(double lat, double lon, char* out, size_t n) {
  if (n < 7) { out[0] = 0; return; }
  double x = lon + 180, y = lat + 90;
  out[0] = 'A' + (int)(x / 20); out[1] = 'A' + (int)(y / 10);
  x = fmod(x, 20); y = fmod(y, 10);
  out[2] = '0' + (int)(x / 2); out[3] = '0' + (int)y;
  x = fmod(x, 2); y = fmod(y, 1);
  out[4] = 'a' + (int)(x * 12); out[5] = 'a' + (int)(y * 24);
  out[6] = 0;
}

// A Details card with no row to show goes, with its title over it.
static void hideEmpty(lv_obj_t* card) {
  bool any = false;
  for (uint32_t i = 0; i < lv_obj_get_child_count(card); i++)
    if (!lv_obj_has_flag(lv_obj_get_child(card, i), LV_OBJ_FLAG_HIDDEN)) { any = true; break; }
  const int32_t at = lv_obj_get_index(card);
  lv_obj_t* title = at > 0 ? lv_obj_get_child(lv_obj_get_parent(card), at - 1) : nullptr;
  for (lv_obj_t* o : { card, title }) {
    if (!o) continue;
    if (any) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

static const char* qualityName(uint8_t q) {
  switch (q) {
    case 1: return "None (GPS)";
    case 2: return "DGPS / SBAS";
    case 4: return "RTK fixed";
    case 5: return "RTK float";
    case 6: return "Dead reckoning";
    default: return "";
  }
}

}  // namespace gpsview

// From loop(), once a second. Each tab's objects are null on the others.
void UITask::refreshGps() {
  using namespace gpsview;
  if (_screen != SCR_GPS) return;
  GpsSky* g = sky();
  bool off = _core->gpsAvailable() && !_core->gpsEnabled();
  bool data = g && g->last_ms && millis() - g->last_ms < 3000;
  if (off || !data) s_n = 0;
  else snapshot(*g);
  if (s_on_btn) {
    if (off) lv_obj_remove_flag(s_on_btn, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_on_btn, LV_OBJ_FLAG_HIDDEN);
  }

  const char* status = "Not available";
  uint32_t status_col = theme::TEXT_MUTED;
  char used[48] = "", inview[16] = "", search[16] = "", ttff[16] = "", utc[16] = "";
  char pos[32] = "", grid[8] = "", alt[16] = "", speed[16] = "", course[16] = "";
  char hdop[24] = "", vdop[24] = "", pdop[24] = "", cn0[16] = "", best[24] = "", nmea[32] = "", rx[24] = "";
  bool fix = false;
  if (g && off) status = "GPS is off";
  else if (g && !data) {
    status = "No data";
    status_col = theme::FAIL;
#if defined(SEEED_WIO_TRACKER_L2)
    snprintf(used, sizeof(used), "Nothing from the receiver (%lu B)", (unsigned long)gps.rxChars());
#else
    snprintf(used, sizeof(used), "Nothing from the receiver");
#endif
  } else if (g) {
    int tracked = 0, used_n = 0, cn_sum = 0, best_i = -1;
    for (int i = 0; i < s_n; i++) {
      if (s_sats[i].snr >= 0) { tracked++; if (best_i < 0 || s_sats[i].snr > s_sats[best_i].snr) best_i = i; }
      if (s_used_flag[i]) { used_n++; if (s_sats[i].snr > 0) cn_sum += s_sats[i].snr; }
    }
    fix = g->hasFix();
    status = fix ? (g->fix_mode == 2 ? "2D fix" : "3D fix") : "Searching";
    status_col = fix ? theme::OK : theme::ACCENT;
    snprintf(used, sizeof(used), "%d of %d used", fix ? (used_n ? used_n : g->sats_used) : 0, s_n);
    snprintf(inview, sizeof(inview), "%d heard", tracked);
    uint32_t since = g->start_ms ? (millis() - g->start_ms) / 1000 : 0;
    if (g->ttff_ms) snprintf(ttff, sizeof(ttff), "%lu s", (unsigned long)(g->ttff_ms / 1000));
    else snprintf(search, sizeof(search), "%lu:%02lu", (unsigned long)(since / 60), (unsigned long)(since % 60));
    if (g->utc_valid) snprintf(utc, sizeof(utc), "%02u:%02u:%02u", g->utc_h, g->utc_m, g->utc_s);
    int32_t lat, lon;
    if (fix && _core->course.currentLocation(lat, lon)) {
      snprintf(pos, sizeof(pos), "%.5f, %.5f", lat / 1e6, lon / 1e6);
      locator(lat / 1e6, lon / 1e6, grid, sizeof(grid));
    }
    if (fix && g->alt_valid) snprintf(alt, sizeof(alt), "%.0f m", g->alt_m);
    if (fix && g->rmc_valid) {
      snprintf(speed, sizeof(speed), "%.1f km/h", g->speed_kmh);
      if (g->speed_kmh >= 2) {   // standing still, the course is noise
        static const char* const C8[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
        snprintf(course, sizeof(course), "%.0f\xC2\xB0 %s", g->course_deg, C8[((int)lroundf(g->course_deg / 45)) & 7]);
      }
    }
    if (fix) { dopText(g->hdop, hdop, sizeof(hdop)); dopText(g->vdop, vdop, sizeof(vdop)); dopText(g->pdop, pdop, sizeof(pdop)); }
    if (used_n && cn_sum) snprintf(cn0, sizeof(cn0), "%d dB-Hz", cn_sum / used_n);
    if (best_i >= 0) snprintf(best, sizeof(best), "%c%d  %d dB-Hz", GpsSky::sysLetter(s_sats[best_i].sys), s_sats[best_i].prn, s_sats[best_i].snr);
    if (g->sentences) snprintf(nmea, sizeof(nmea), g->bad_sentences ? "%lu, %lu bad" : "%lu",
                               (unsigned long)g->sentences, (unsigned long)g->bad_sentences);
#if defined(SEEED_WIO_TRACKER_L2)
    const unsigned long b = gps.rxChars();
    if (b >= 10240) snprintf(rx, sizeof(rx), "%lu KB", b / 1024); else snprintf(rx, sizeof(rx), "%lu B", b);
#endif
  }

  if (s_status) {   // Sky
    setText(s_status, status);
    setTextColor(s_status, status_col);
    setText(s_used, used);
    infoSet(s_inview, inview);
    infoSet(s_search, search);
    infoSet(s_ttff, ttff);
    infoSet(s_utc, utc);
    lv_obj_invalidate(s_sky);
  }
  if (s_bars) {   // Signal
    for (uint8_t k = 0; k < GpsSky::SYS_COUNT; k++) {
      int n = 0, u = 0;
      for (int i = 0; i < s_n; i++) if (s_sats[i].sys == k) { n++; if (s_used_flag[i]) u++; }
      if (!n) { lv_obj_add_flag(s_sys_pill[k], LV_OBJ_FLAG_HIDDEN); continue; }
      setTextFmt(s_sys_pill[k], "%c  %s  %d/%d", GpsSky::sysLetter(k), GpsSky::sysName(k), u, n);
      lv_obj_remove_flag(s_sys_pill[k], LV_OBJ_FLAG_HIDDEN);
    }
    int32_t w = s_n * BAR_PITCH;
    lv_obj_set_width(s_bars, w < BAR_PITCH ? BAR_PITCH : w);
    lv_obj_invalidate(s_bars);
  }
  if (s_d_fix) {   // Details
    char f[64];
    snprintf(f, sizeof(f), used[0] ? "%s, %s" : "%s", status, used);
    infoSet(s_d_fix, f);
    infoSet(s_quality, fix && g ? qualityName(g->fix_quality) : "");
    infoSet(s_d_ttff, ttff);
    infoSet(s_d_search, search);
    infoSet(s_d_utc, utc);
    infoSet(s_pos, pos);
    infoSet(s_grid, grid);
    infoSet(s_alt, alt);
    infoSet(s_speed, speed);
    infoSet(s_course, course);
    infoSet(s_hdop, hdop);
    infoSet(s_vdop, vdop);
    infoSet(s_pdop, pdop);
    infoSet(s_cn0, cn0);
    infoSet(s_best, best);
    infoSet(s_nmea, nmea);
    infoSet(s_rx, rx);
    for (lv_obj_t* v : { s_pos, s_hdop, s_cn0 }) hideEmpty(lv_obj_get_parent(lv_obj_get_parent(v)));
  }
}
