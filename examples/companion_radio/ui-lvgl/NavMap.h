#pragma once
// Navigation map (Home > Map): one of the map screen's two modes. It shows only
// what you navigate by: saved waypoints, people sharing their position live,
// the recorded trail, and the Locator's active target with a line to it and a
// bar with distance, bearing, course and ETA. The Nodes map (Nearby > map
// button) is the one with every contact.
//
// Everything shown lives in the UI Core (waypoints, locator target, live
// shares, trail), so ui-new's Trail / Waypoints / Locator screens and this map
// work on the same data. Here: the pin button opens the waypoint list (add at
// the GPS position or by coordinates; tap one for its menu: navigate, rename,
// share, delete), a long-press on the map offers that spot as a waypoint or a
// target, the bar opens the target list.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after MapScreen.h.

namespace navmap {

enum : uint8_t { WP_NAV, WP_RENAME, WP_SHARE, WP_DELETE };

// Trail: normalised Web-Mercator coords (0..1) cached as points are added, so
// a redraw only scales and offsets them. Drawn by one object as a single
// polyline with breaks (LV_DRAW_LINE_POINT_NONE) where a pause starts a new
// segment and where a stretch lies outside the view: an lv_line per stretch
// would make LVGL queue one draw task per point, the queue walked to its end
// for each -- tens of thousands of points made every frame crawl.
static const int TRAIL_SEGS = 256;   // pauses / saves split the trail; beyond this, segments join
// Stored as float offsets from the first point (s_ox/s_oy): a float holds a
// small offset exactly enough, but not a whole 0..1 coordinate at street zoom
// (2^18 * 256 px across -- a float is off by pixels there).
static float* s_nx = nullptr;   // TrailStore::CAPACITY each, in PSRAM (first trail drawn)
static float* s_ny = nullptr;
static double s_ox = 0, s_oy = 0;
// Bounding box of each CHUNK points (and the next chunk's first, so the line
// joining them is inside too): a chunk off the view is skipped whole.
static const int CHUNK = 64;
static const int CHUNKS = (TrailStore::CAPACITY + CHUNK - 1) / CHUNK;
static float* s_cb = nullptr;   // min x, min y, max x, max y per chunk
// Screen points of the visible stretches, with breaks.
static const int PTS_MAX = TrailStore::CAPACITY + 2 * CHUNKS + TRAIL_SEGS + 4;
static lv_point_precise_t* s_pts = nullptr;
static int s_pts_n = 0;
static int32_t s_pts_x0 = 0, s_pts_y0 = 0;   // the trail object's screen origin s_pts was made for
static int s_seg_first[TRAIL_SEGS], s_segs = 0;
// What navTrailSync() has converted so far (s_sync_gen reset: all of it again).
static uint32_t s_sync_gen = 0xFFFFFFFF, s_sync_seq = 0, s_sync_full_at = 0;
static int s_sync_n = 0;
static lv_obj_t* s_trail = nullptr;
static lv_obj_t* s_target_line = nullptr;
static lv_point_precise_t s_target_pts[2];
static lv_obj_t* s_target_ring = nullptr;
static lv_obj_t* s_target_dot = nullptr;
static const int RING_D = 30;   // clear of a 12 px marker dot inside it
static navview::EtaTracker s_eta;
static GpsAverager s_avg;   // "mark here" with Settings > Map > Waypoint averaging
static TrackBack   s_tb;    // walking the trail back: its breadcrumb is the target
static const uint32_t TRAIL_COLOR = 0x4FA3FF;

static void fmtDuration(char* b, int n, uint32_t secs) {
  if (secs >= 3600) snprintf(b, n, "%luh%02lu", (unsigned long)(secs / 3600), (unsigned long)(secs % 3600 / 60));
  else snprintf(b, n, "%lum", (unsigned long)((secs + 59) / 60));
}

// Web-Mercator 0..1. double: at z18 one pixel is 1/67M of the world.
static double normX(int32_t lon_e6) { return (lon_e6 / 1e6 + 180.0) / 360.0; }
static double normY(int32_t lat_e6) {
  double r = lat_e6 / 1e6 * M_PI / 180.0;
  return (1.0 - asinh(tan(r)) / M_PI) / 2.0;
}

// The trail object's draw: s_pts in one draw task (the software renderer
// walks it a segment at a time, clipped to the band being drawn).
static void onTrailDraw(lv_event_t* e) {
  if (s_pts_n < 2) return;
  lv_obj_t* obj = lv_event_get_current_target_obj(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  if (a.x1 != s_pts_x0 || a.y1 != s_pts_y0) {   // moved since the points were made
    for (int i = 0; i < s_pts_n; i++) {
      if (s_pts[i].x == LV_DRAW_LINE_POINT_NONE) continue;
      s_pts[i].x += a.x1 - s_pts_x0;
      s_pts[i].y += a.y1 - s_pts_y0;
    }
    s_pts_x0 = a.x1; s_pts_y0 = a.y1;
  }
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  lv_obj_init_draw_line_dsc(obj, LV_PART_MAIN, &d);
  d.base.layer = lv_event_get_layer(e);
  d.points = s_pts;
  d.point_cnt = s_pts_n;
  lv_draw_line(d.base.layer, &d);
}

// Waypoint labels are 11 bytes of UTF-8 (Waypoint.h): reject input past that,
// so a two-byte letter is never cut in half.
static void onLabelInsert(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* ins = (const char*)lv_event_get_param(e);
  if (ins && strlen(lv_textarea_get_text(ta)) + strlen(ins) > WAYPOINT_LABEL_LEN - 1)
    lv_textarea_set_insert_replace(ta, "");
}

}  // namespace navmap

static void onNavList(lv_event_t* e)    { (void)e; s_ui->navTargetsPopup(); }
static void onNavMark(lv_event_t* e)    { (void)e; s_ui->navWaypointsPopup(); }
static void onNavAvgCancel(lv_event_t* e) { (void)e; s_ui->navAveragingCancel(); }
static void onNavClear(lv_event_t* e)   { (void)e; s_ui->navPick(navmap::code(navmap::T_CLEAR, 0)); }
static void onNavPick(lv_event_t* e)    { s_ui->navPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavWpMenu(lv_event_t* e)  { s_ui->navWaypointMenu((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavWpAction(lv_event_t* e){ s_ui->navWaypointAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavPopupClose(lv_event_t* e) { (void)e; s_ui->navClosePopup(); }
static void onNavRenameKb(lv_event_t* e) { s_ui->navRenameDone(lv_event_get_code(e) == LV_EVENT_READY); }

// ── Layers and controls (built by buildMap in nav mode) ───────────────────────

void UITask::buildNavLayers() {
  lv_obj_t* tr = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(tr);
  lv_obj_set_size(tr, LV_PCT(100), LV_PCT(100));   // clip to the view, whatever the points
  lv_obj_set_style_line_width(tr, 3, 0);
  lv_obj_set_style_line_color(tr, lv_color_hex(navmap::TRAIL_COLOR), 0);
  lv_obj_set_style_line_rounded(tr, true, 0);
  lv_obj_remove_flag(tr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(tr, navmap::onTrailDraw, LV_EVENT_DRAW_MAIN, nullptr);
  navmap::s_trail = tr;
  navmap::s_pts_n = 0;
  navmap::s_target_line = lv_line_create(_map_marks);
  lv_obj_set_size(navmap::s_target_line, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_line_width(navmap::s_target_line, 2, 0);
  lv_obj_set_style_line_color(navmap::s_target_line, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_line_dash_width(navmap::s_target_line, 8, 0);
  lv_obj_set_style_line_dash_gap(navmap::s_target_line, 6, 0);
  lv_obj_remove_flag(navmap::s_target_line, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);

  navmap::s_target_ring = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(navmap::s_target_ring);
  lv_obj_set_size(navmap::s_target_ring, navmap::RING_D, navmap::RING_D);
  lv_obj_set_style_radius(navmap::s_target_ring, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_color(navmap::s_target_ring, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(navmap::s_target_ring, 3, 0);
  lv_obj_remove_flag(navmap::s_target_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(navmap::s_target_ring, LV_OBJ_FLAG_HIDDEN);
  // Centre dot, for a target no marker stands on (a map point, a message
  // position, a person whose live share went stale: last known place).
  navmap::s_target_dot = lv_obj_create(navmap::s_target_ring);
  lv_obj_remove_style_all(navmap::s_target_dot);
  lv_obj_set_size(navmap::s_target_dot, 8, 8);
  lv_obj_set_style_radius(navmap::s_target_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(navmap::s_target_dot, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_bg_opa(navmap::s_target_dot, LV_OPA_COVER, 0);
  lv_obj_remove_flag(navmap::s_target_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(navmap::s_target_dot);
  navmap::s_segs = 0;
  navmap::s_sync_gen = 0xFFFFFFFF;   // a new map: the whole trail converted again
}

void UITask::buildNavControls(lv_obj_t* body) {
  // The target list opens from the bar; the left column: back, mark the spot, tools.
  lv_obj_align(mapButton(body, UI_SYMBOL_PIN, onNavMark), LV_ALIGN_TOP_LEFT, 6, 52);

  _nav_bar = lv_button_create(body);   // tap: the target list
  lv_obj_set_size(_nav_bar, LV_PCT(100), navmap::BAR_H);
  lv_obj_align(_nav_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(_nav_bar, 0, 0);
  lv_obj_set_style_shadow_width(_nav_bar, 0, 0);
  lv_obj_set_style_bg_color(_nav_bar, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(_nav_bar, LV_OPA_COVER, 0);   // opaque, as the map buttons
  lv_obj_set_style_bg_color(_nav_bar, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_border_side(_nav_bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(_nav_bar, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_width(_nav_bar, 1, 0);
  lv_obj_set_style_pad_hor(_nav_bar, theme::PAD, 0);
  lv_obj_set_style_pad_ver(_nav_bar, 3, 0);
  lv_obj_add_event_cb(_nav_bar, onNavList, LV_EVENT_CLICKED, NULL);
  _nav_title = label(_nav_bar, "", THEME_FONT_BODY, theme::ACCENT);
  lv_label_set_long_mode(_nav_title, LV_LABEL_LONG_DOT);
  lv_obj_set_size(_nav_title, 260, 19);   // fixed height: LONG_DOT cuts instead of wrapping
  lv_obj_align(_nav_title, LV_ALIGN_TOP_LEFT, 0, 0);
  _nav_info = label(_nav_bar, "", THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(_nav_info, LV_LABEL_LONG_DOT);
  lv_obj_set_size(_nav_info, 270, 16);
  lv_obj_align(_nav_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  _nav_clear = lv_button_create(_nav_bar);
  lv_obj_set_size(_nav_clear, 34, 34);
  lv_obj_align(_nav_clear, LV_ALIGN_RIGHT_MID, 4, 0);
  lv_obj_set_style_shadow_width(_nav_clear, 0, 0);
  lv_obj_set_style_radius(_nav_clear, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(_nav_clear, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(_nav_clear, onNavClear, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(_nav_clear, LV_SYMBOL_CLOSE, THEME_FONT_BODY, theme::TEXT));
  _nav_avg_pill = mapPill(body, "");   // tap: cancel averaging
  lv_obj_set_style_text_color(_nav_avg_pill, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_pad_all(_nav_avg_pill, 8, 0);
  lv_obj_align(_nav_avg_pill, LV_ALIGN_CENTER, 0, -30);
  lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(_nav_avg_pill, onNavAvgCancel, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
  _nav_rec = mapPill(mapview::s_going, "");   // trail recording / live share running, over the download
  lv_label_set_recolor(_nav_rec, true);
  lv_obj_move_to_index(_nav_rec, 0);
  lv_obj_add_flag(_nav_rec, LV_OBJ_FLAG_HIDDEN);
  _next_nav_bar_ms = 0;
  navmap::s_eta.reset();
  refreshNavBar();
}

// ── Markers, trail, target (rebuilt every few seconds, laid out on every pan) ─

void UITask::rebuildNavMarkers() {
  const WaypointModel& wp = _core->waypoints;
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    char t[WAYPOINT_LABEL_LEN + 8];
    snprintf(t, sizeof(t), UI_SYMBOL_FLAG " %s", w.label[0] ? w.label : "?");
    addMapMark(mapview::MK_WAYPOINT, i, w.lat_1e6, w.lon_1e6, theme::ACCENT, t);
  }
  const LiveTrackStore& lt = _core->live_share.track();
  uint32_t now = rtc_clock.getCurrentTime();
  for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
    if (!lt.isActive(i, now)) continue;
    const LiveTrackStore::Entry& e = lt.slotAt(i);
    char age[8], t[LiveTrackStore::NAME_LEN + 12];
    geo::fmtAgeShort(age, sizeof(age), now, e.ts);
    snprintf(t, sizeof(t), "%s%s%s", e.name, age[0] ? "  " : "", age);
    addMapMark(mapview::MK_LIVE, i, e.lat_1e6, e.lon_1e6, theme::OK, t);
  }

  navTrailSync();
}

// Trail -> normalised points in segments, for layoutNav(). Only the points
// added since the last call are converted (the trail runs to tens of
// thousands of points); all of them when it was replaced (reset, load) or
// its ring, full, dropped its oldest -- at most every 10 s then.
bool UITask::navTrailSync() {
  using namespace navmap;
  TrailStore& ts = _core->trail.store();
  if (!s_pts) {
    s_nx = psramBuf<float>(TrailStore::CAPACITY);
    s_ny = psramBuf<float>(TrailStore::CAPACITY);
    s_cb = psramBuf<float>(4 * CHUNKS);
    s_pts = psramBuf<lv_point_precise_t>(PTS_MAX);
  }
  if (!s_nx || !s_ny || !s_cb || !s_pts) { s_segs = 0; s_sync_n = 0; return false; }
  int n = ts.count();
  if (ts.gen() == s_sync_gen && ts.seq() == s_sync_seq && n == s_sync_n) return false;
  bool all = ts.gen() != s_sync_gen || n < s_sync_n || n - s_sync_n != (int)(ts.seq() - s_sync_seq);
  if (all && ts.gen() == s_sync_gen && n == TrailStore::CAPACITY && millis() - s_sync_full_at < 10000) return false;
  int from = all ? 0 : s_sync_n;
  if (all) { s_segs = 0; s_sync_full_at = millis(); }
  for (int i = from; i < n; i++) {
    const TrailPoint& p = ts.at(i);
    if (i == 0) { s_ox = normX(p.lon_1e6); s_oy = normY(p.lat_1e6); }
    float x = s_nx[i] = (float)(normX(p.lon_1e6) - s_ox);
    float y = s_ny[i] = (float)(normY(p.lat_1e6) - s_oy);
    float* b = &s_cb[4 * (i / CHUNK)];
    if (i % CHUNK == 0) { b[0] = b[2] = x; b[1] = b[3] = y; }
    for (int k = i % CHUNK == 0 && i > 0 ? 2 : 1; k > 0; k--, b -= 4) {   // this chunk; a first point also closes the last
      if (x < b[0]) b[0] = x;
      if (y < b[1]) b[1] = y;
      if (x > b[2]) b[2] = x;
      if (y > b[3]) b[3] = y;
    }
    if (i == 0 || ((p.flags & TRAIL_FLAG_SEG_START) && s_segs < TRAIL_SEGS)) s_seg_first[s_segs++] = i;
  }
  s_sync_gen = ts.gen();
  s_sync_seq = ts.seq();
  s_sync_n = n;
  return true;
}

// The trail's screen points for layoutNav(): only the chunks whose box meets
// the view, and of those only points 2 px or more from the last kept one
// (zoomed out, thousands of points shrink to the few hundred that show). A
// point dropped that way still ends its stretch, so the line reaches it.
static void layoutNavTrail(double scale) {
  using namespace navmap;
  s_pts_n = 0;
  if (!s_trail) return;
  {   // redrawn when the view or the trail changed, not on every map refresh
    static double v_left = NAN, v_top, v_scale;
    static int v_n;
    static uint32_t v_gen, v_seq;
    if (v_left != mapview::s_left || v_top != mapview::s_top || v_scale != scale || v_n != s_sync_n || v_gen != s_sync_gen || v_seq != s_sync_seq) {
      v_left = mapview::s_left; v_top = mapview::s_top; v_scale = scale;
      v_n = s_sync_n; v_gen = s_sync_gen; v_seq = s_sync_seq;
      lv_obj_invalidate(s_trail);
    }
  }
  int n = s_sync_n;
  if (!s_pts || n < 2) return;
  lv_area_t a;
  lv_obj_get_coords(s_trail, &a);
  s_pts_x0 = a.x1; s_pts_y0 = a.y1;
  const double M = 8;   // past the edge: the line's width, rounded caps
  float fs = (float)scale;
  float ox = (float)(s_ox * scale - mapview::s_left) + a.x1;
  float oy = (float)(s_oy * scale - mapview::s_top) + a.y1;
  float vx0 = (float)((mapview::s_left - M) / scale - s_ox), vx1 = (float)((mapview::s_left + lv_area_get_width(&a) + M) / scale - s_ox);
  float vy0 = (float)((mapview::s_top - M) / scale - s_oy),  vy1 = (float)((mapview::s_top + lv_area_get_height(&a) + M) / scale - s_oy);

  bool open = false, held = false;   // a stretch is being drawn; a thinned-out point is owed at its end
  lv_point_precise_t hold = {0, 0}, last = {0, 0};
  auto push = [&](lv_value_precise_t x, lv_value_precise_t y) { s_pts[s_pts_n].x = x; s_pts[s_pts_n].y = y; s_pts_n++; };
  auto endRun = [&]() {
    if (held) push(hold.x, hold.y);
    if (open) push(LV_DRAW_LINE_POINT_NONE, LV_DRAW_LINE_POINT_NONE);
    open = held = false;
  };
  auto emit = [&](int i) {
    lv_value_precise_t x = (lv_value_precise_t)lroundf(s_nx[i] * fs + ox);
    lv_value_precise_t y = (lv_value_precise_t)lroundf(s_ny[i] * fs + oy);
    if (open && fabsf((float)(x - last.x)) < 2 && fabsf((float)(y - last.y)) < 2) { hold.x = x; hold.y = y; held = true; return; }
    push(x, y);
    last.x = x; last.y = y;
    open = true; held = false;
  };
  int seg = 1;   // s_seg_first[0] is the first point
  for (int i0 = 0; i0 < n; i0 += CHUNK) {
    int i1 = i0 + CHUNK < n ? i0 + CHUNK : n;
    const float* b = &s_cb[4 * (i0 / CHUNK)];
    bool starts = seg < s_segs && s_seg_first[seg] == i0;
    if (b[2] < vx0 || b[0] > vx1 || b[3] < vy0 || b[1] > vy1) {   // off the view
      if (open && !starts) emit(i0);   // the last chunk's line to this one's first point
      endRun();
      while (seg < s_segs && s_seg_first[seg] < i1) seg++;
      continue;
    }
    for (int i = i0; i < i1; i++) {
      if (seg < s_segs && s_seg_first[seg] == i) { endRun(); seg++; }
      emit(i);
    }
  }
  endRun();
}

void UITask::layoutNav() {
  if (!navmap::s_target_line) return;
  double scale = (double)(1 << _map_z) * mapview::TILE_PX;
  layoutNavTrail(scale);

  int32_t tlat, tlon, mlat, mlon;
  char name[8];
  bool person, set;
  bool target = navCurrentTarget(tlat, tlon, name, sizeof(name), person, set);
  if (target) {
    int tx = (int)lround(navmap::normX(tlon) * scale - mapview::s_left);
    int ty = (int)lround(navmap::normY(tlat) * scale - mapview::s_top);
    lv_obj_set_pos(navmap::s_target_ring, tx - navmap::RING_D / 2, ty - navmap::RING_D / 2);
    lv_obj_remove_flag(navmap::s_target_ring, LV_OBJ_FLAG_HIDDEN);
    bool marked = false;   // a waypoint / live marker already sits on the target
    const int half = mapview::MARK_D / 2;
    for (int k = 0; k < mapview::s_mark_count && !marked; k++) {
      lv_obj_t* m = mapview::s_marks[k].obj;
      marked = abs(lv_obj_get_x(m) + half - tx) <= 3 && abs(lv_obj_get_y(m) + lv_obj_get_y(mapview::s_marks[k].dot) + half - ty) <= 3;
    }
    if (marked) lv_obj_add_flag(navmap::s_target_dot, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(navmap::s_target_dot, LV_OBJ_FLAG_HIDDEN);
    if (_core->course.currentLocation(mlat, mlon)) {
      navmap::s_target_pts[0].x = (lv_value_precise_t)lround(navmap::normX(mlon) * scale - mapview::s_left);
      navmap::s_target_pts[0].y = (lv_value_precise_t)lround(navmap::normY(mlat) * scale - mapview::s_top);
      navmap::s_target_pts[1].x = tx;
      navmap::s_target_pts[1].y = ty;
      lv_line_set_points(navmap::s_target_line, navmap::s_target_pts, 2);
      lv_obj_remove_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_obj_add_flag(navmap::s_target_ring, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(navmap::s_target_line, LV_OBJ_FLAG_HIDDEN);
  }
}

// Bottom bar: the target, then distance / bearing / own course / ETA.
void UITask::refreshNavBar() {
  if (!_nav_bar) return;
  refreshNavTools();
  {
    // Plain text, the marks coloured (recolor): red for recording, accent
    // for paused and live -- all-red text was hard to read over the map.
    char t[96] = "";
    int o = 0;
    TrailStore& ts = _core->trail.store();
    if (ts.isActive()) {
      char d[12];
      geo::fmtDist(d, sizeof(d), ts.totalDistanceMeters() / 1000.0f, _prefs && _prefs->units_imperial);
      o += snprintf(t + o, sizeof(t) - o, ts.isPaused() ? "#%06lX " LV_SYMBOL_PAUSE "# PAUSED %s" : "#%06lX " LV_SYMBOL_STOP "# REC %s",
                    (unsigned long)(ts.isPaused() ? theme::ACCENT : theme::FAIL), d);
    }
    if (_prefs && _prefs->loc_share_enabled) {
      char left[12];
      navmap::fmtDuration(left, sizeof(left), _core->live_share.remainingSecs());
      o += snprintf(t + o, sizeof(t) - o, "%s#%06lX " LV_SYMBOL_GPS "# LIVE %s", o ? "   " : "", (unsigned long)theme::ACCENT, left);
    }
    setText(_nav_rec, t);
    if (o) lv_obj_remove_flag(_nav_rec, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_nav_rec, LV_OBJ_FLAG_HIDDEN);
  }
  int32_t tlat, tlon;
  char name[40];
  bool person = false, set = false;
  bool pos = navCurrentTarget(tlat, tlon, name, sizeof(name), person, set);
  if (!set) {
    setText(_nav_title, "No target");
    setTextColor(_nav_title, theme::TEXT_MUTED);
    setText(_nav_info, "Tap here to pick a target, or hold the map on a spot");
    lv_obj_add_flag(_nav_clear, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  setTextColor(_nav_title, theme::ACCENT);
  lv_obj_remove_flag(_nav_clear, LV_OBJ_FLAG_HIDDEN);
  setTextFmt(_nav_title, "%s %s", navmap::s_tb.active() ? LV_SYMBOL_LOOP : person ? UI_SYMBOL_USERS : UI_SYMBOL_FLAG,
                        name);
  if (!pos) {
    setText(_nav_info, "Position unknown (not shared recently)");
    return;
  }
  int32_t lat, lon;
  if (!_core->course.currentLocation(lat, lon)) {
    setText(_nav_info, _core->gpsEnabled() || !_core->gpsAvailable()
                                 ? "Waiting for a GPS fix..." : "GPS is off  -  tap the crosshair to turn it on");
    return;
  }
  bool imperial = _prefs->units_imperial;
  float km = geo::haversineKm(lat, lon, tlat, tlon);
  navmap::s_eta.update(km, millis());
  char dist[12], eta[12], hdg[16] = "";
  geo::fmtDist(dist, sizeof(dist), km, imperial);
  int to = geo::bearingDeg(lat, lon, tlat, tlon);
  int cog;
  if (_core->course.currentCourse(cog)) snprintf(hdg, sizeof(hdg), "  -  you %d\xC2\xB0", cog);
  if (navmap::s_eta.eta(km, eta, sizeof(eta)))
    setTextFmt(_nav_info, "%s  %d\xC2\xB0 %s%s  -  ETA %s", dist, to, geo::bearingCardinal(to), hdg, eta);
  else
    setTextFmt(_nav_info, "%s  %d\xC2\xB0 %s%s", dist, to, geo::bearingCardinal(to), hdg);
}

// ── Target selection ──────────────────────────────────────────────────────────

// What the map navigates to: the track-back breadcrumb while walking the trail
// back, else the Locator target. `set`: there is a target at all; returns
// whether its position is known.
bool UITask::navCurrentTarget(int32_t& lat, int32_t& lon, char* name, int n, bool& person, bool& set) {
  person = false;
  TrailStore& ts = _core->trail.store();
  if (navmap::s_tb.active() && navmap::s_tb.index() < ts.count()) {
    set = true;
    navmap::s_tb.label(name, n);
    lat = ts.at(navmap::s_tb.index()).lat_1e6;
    lon = ts.at(navmap::s_tb.index()).lon_1e6;
    return true;
  }
  set = _prefs && _prefs->locator_has_target;
  if (!set) return false;
  person = _prefs->locator_target_kind != 0;
  snprintf(name, n, "%s", _prefs->locator_label[0] ? _prefs->locator_label : "Target");
  return _core->locator.activeTargetPos(lat, lon);
}

// From loop(), once a second on any screen: advance along the trail.
void UITask::navPollTrackBack() {
  if (!navmap::s_tb.active()) return;
  int32_t lat = 0, lon = 0;
  bool fix = _core->course.currentLocation(lat, lon);
  TrackBack::Step st = navmap::s_tb.poll(_core->trail.store(), fix, lat, lon);
  if (st == TrackBack::ADVANCED) navmap::s_eta.reset();
  else if (st == TrackBack::ARRIVED) showToast("Back at the trail start", 4000);
  if (st != TrackBack::NONE && _screen == SCR_MAP && _map_nav) { layoutMap(); refreshNavBar(); }
}

// Frame own position and the target together (or just centre the target).
void UITask::navFrameTarget() {
  int32_t tlat, tlon, lat, lon;
  char name[8];
  bool person, set;
  if (!navCurrentTarget(tlat, tlon, name, sizeof(name), person, set)) return;
  _map_follow = false;
  int w = _map_area ? lv_obj_get_width(_map_area) : 320;
  int h = (_map_area ? lv_obj_get_height(_map_area) : 218) - navmap::BAR_H;
  double tx = navmap::normX(tlon), ty = navmap::normY(tlat);
  double cx = tx, cy = ty;
  int z = _map_z < 14 ? 14 : _map_z;
  if (_core->course.currentLocation(lat, lon)) {
    double mx = navmap::normX(lon), my = navmap::normY(lat);
    cx = (tx + mx) / 2; cy = (ty + my) / 2;
    double dx = fabs(tx - mx) * mapview::TILE_PX, dy = fabs(ty - my) * mapview::TILE_PX;
    z = mapview::MAX_Z - 1;
    while (z > mapview::MIN_Z && (dx * (1 << z) > w - 150 || dy * (1 << z) > h - 110)) z--;   // clear of the buttons
  }
  _map_z = z;
  double n = (double)(1 << z);
  _map_cx = cx * n;
  _map_cy = cy * n + navmap::BAR_H / 2.0 / mapview::TILE_PX;   // keep it clear of the bar
  layoutMap();
}

// Frame the whole trail (after loading one).
void UITask::navFrameTrail() {
  const TrailStore& ts = _core->trail.store();
  if (ts.empty()) return;
  double x0 = 1, x1 = 0, y0 = 1, y1 = 0;
  for (int i = 0; i < ts.count(); i++) {
    double x = navmap::normX(ts.at(i).lon_1e6), y = navmap::normY(ts.at(i).lat_1e6);
    if (x < x0) x0 = x;
    if (x > x1) x1 = x;
    if (y < y0) y0 = y;
    if (y > y1) y1 = y;
  }
  _map_follow = false;
  int w = _map_area ? lv_obj_get_width(_map_area) : 320;
  int h = (_map_area ? lv_obj_get_height(_map_area) : 218) - navmap::BAR_H;
  double dx = (x1 - x0) * mapview::TILE_PX, dy = (y1 - y0) * mapview::TILE_PX;
  int z = mapview::MAX_Z - 1;
  while (z > mapview::MIN_Z && (dx * (1 << z) > w - 110 || dy * (1 << z) > h - 60)) z--;   // clear of the buttons
  _map_z = z;
  double n = (double)(1 << z);
  _map_cx = (x0 + x1) / 2 * n;
  _map_cy = (y0 + y1) / 2 * n + navmap::BAR_H / 2.0 / mapview::TILE_PX;
  layoutMap();
}

void UITask::navSetTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  navmap::s_tb.stop();   // a chosen target replaces walking the trail back
  _core->locator.setTarget(kind, key, lat, lon, name);
  prefsSave();
  navmap::s_eta.reset();
  char t[40];
  snprintf(t, sizeof(t), "Navigating to %s", name);
  showToast(t);
}

void UITask::navPick(int code) {
  uint8_t type = (uint8_t)(code >> 8);
  int idx = code & 0xFF;
  switch (type) {
    case navmap::T_CLEAR:
      if (navmap::s_tb.active()) { navmap::s_tb.stop(); showToast("Track back stopped"); break; }
      _core->locator.clearTarget();
      prefsSave();
      break;
    case navmap::T_WAYPOINT: {
      if (idx >= _core->waypoints.count()) return;
      const Waypoint& w = _core->waypoints.at(idx);
      navSetTarget(0, nullptr, w.lat_1e6, w.lon_1e6, w.label[0] ? w.label : "Waypoint");
      break;
    }
    case navmap::T_TRAILSTART: {
      TrailStore& ts = _core->trail.store();
      if (ts.empty()) return;
      navSetTarget(0, nullptr, ts.first().lat_1e6, ts.first().lon_1e6, "Trail start");
      break;
    }
    case navmap::T_LIVE: {
      const LiveTrackStore& lt = _core->live_share.track();
      if (idx >= LiveTrackStore::CAPACITY || !lt.isActive(idx, rtc_clock.getCurrentTime())) return;
      const LiveTrackStore::Entry& e = lt.slotAt(idx);
      // Followed as the person moves: a verified (DM) share by key, a channel
      // share by sender name.
      navSetTarget(e.verified ? 1 : 2, e.verified ? e.key : nullptr, e.lat_1e6, e.lon_1e6, e.name);
      break;
    }
  }
  navClosePopup();
  if (_screen != SCR_MAP || !_map_nav) { openMap(true); }
  navFrameTarget();
  refreshNavBar();
}

// Node detail > Navigate: a contact as the target (followed by key).
void UITask::navToNode(const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  navSetTarget(1, key, lat, lon, name);
  openMap(true);
  navFrameTarget();
  refreshNavBar();
}

// ── Popups: target list, waypoint menu, rename ────────────────────────────────

lv_obj_t* UITask::navPopupPanel(const char* title, bool full, bool bottom) {
  navClosePopup();
  lv_obj_t* panel = popupOpen(screen(), bottom ? POP_BOTTOM : full ? POP_FULL : POP_FIT, _nav_overlay);
  lv_obj_set_style_pad_row(panel, 6, 0);

  lv_obj_t* hdr = lv_obj_create(panel);
  styleSurface(hdr, theme::BG);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 28);
  lv_obj_t* t = label(hdr, title, THEME_FONT_TITLE, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 230);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(hdr, LV_SYMBOL_CLOSE, onNavPopupClose, 0, NULL);
  return panel;
}

void UITask::navClosePopup() {
  areaPreviewEnd();   // a map area's popup: the map goes back where it was
  if (_nav_overlay) lv_obj_delete_async(_nav_overlay);   // may be closing from its own button
  _nav_overlay = _nav_ta = _nav_kb = _nav_del_lbl = nullptr;
  _nav_trail_lbl = _nav_trail_btn = _nav_reset_lbl = _nav_share_lbl = _nav_share_btn = _nav_tb_btn = nullptr;
}

static void navRowRight(lv_obj_t* row, const char* text, uint32_t col, int right) {
  lv_obj_align(label(row, text, THEME_FONT_SMALL, col), LV_ALIGN_RIGHT_MID, -right, 0);
}

void UITask::navTargetsPopup() {
  lv_obj_t* panel = navPopupPanel("Navigate to", true);
  lv_obj_t* list = scrollList(panel);

  int32_t lat = 0, lon = 0;
  bool gps = _core->course.currentLocation(lat, lon);
  bool imperial = _prefs && _prefs->units_imperial;
  auto dist = [&](int32_t la, int32_t lo, char* out, size_t n) {
    if (gps) geo::fmtDist(out, n, geo::haversineKm(lat, lon, la, lo), imperial);
    else out[0] = '\0';
  };

  if (_prefs && _prefs->locator_has_target) {
    char sub[40];
    snprintf(sub, sizeof(sub), "Now: %s", _prefs->locator_label[0] ? _prefs->locator_label : "target");
    listRow(list, LV_SYMBOL_CLOSE "  Clear target", sub, onNavPick, (void*)(uintptr_t)navmap::code(navmap::T_CLEAR, 0));
  }

  const WaypointModel& wp = _core->waypoints;
  char sec[32];
  snprintf(sec, sizeof(sec), "WAYPOINTS  %d/%d", wp.count(), WaypointStore::CAPACITY);
  sectionTitle(list, sec);
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    char title[WAYPOINT_LABEL_LEN + 8], sub[40], d[12];
    snprintf(title, sizeof(title), UI_SYMBOL_FLAG "  %s", w.label[0] ? w.label : "(unnamed)");
    snprintf(sub, sizeof(sub), "%.5f, %.5f", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);
    lv_obj_t* row = listRow(list, title, sub, onNavPick, (void*)(uintptr_t)navmap::code(navmap::T_WAYPOINT, i));
    lv_obj_t* more = lv_button_create(row);   // the waypoint menu
    lv_obj_set_size(more, 40, 34);
    lv_obj_align(more, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_shadow_width(more, 0, 0);
    lv_obj_set_style_radius(more, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(more, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_add_event_cb(more, onNavWpMenu, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_center(label(more, LV_SYMBOL_EDIT, THEME_FONT_BODY, theme::TEXT));
    dist(w.lat_1e6, w.lon_1e6, d, sizeof(d));
    if (d[0]) navRowRight(row, d, theme::TEXT_MUTED, 52);
  }
  if (wp.count() == 0) {
    noteLabel(list, "None yet. Hold the map to drop one, or tap the pin to mark where you are.");
  }
  TrailStore& ts = _core->trail.store();
  if (!ts.empty()) {
    char d[12], sub[24];
    dist(ts.first().lat_1e6, ts.first().lon_1e6, d, sizeof(d));
    snprintf(sub, sizeof(sub), "%d trail points", ts.count());
    lv_obj_t* row = listRow(list, LV_SYMBOL_HOME "  Trail start", sub, onNavPick,
                            (void*)(uintptr_t)navmap::code(navmap::T_TRAILSTART, 0));
    if (d[0]) navRowRight(row, d, theme::TEXT_MUTED, theme::PAD);
  }

  const LiveTrackStore& lt = _core->live_share.track();
  uint32_t now = rtc_clock.getCurrentTime();
  sectionTitle(list, "SHARING LIVE");
  int live = 0;
  for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
    if (!lt.isActive(i, now)) continue;
    const LiveTrackStore::Entry& e = lt.slotAt(i);
    char age[8], sub[40], d[12];
    geo::fmtAgeShort(age, sizeof(age), now, e.ts);
    snprintf(sub, sizeof(sub), "%s  -  %s ago", e.verified ? "direct" : "channel", age[0] ? age : "0s");
    lv_obj_t* row = listRow(list, e.name, sub, onNavPick, (void*)(uintptr_t)navmap::code(navmap::T_LIVE, i));
    dist(e.lat_1e6, e.lon_1e6, d, sizeof(d));
    if (d[0]) navRowRight(row, d, theme::OK, theme::PAD);
    live++;
  }
  if (live == 0) label(list, "Nobody is sharing their position.", THEME_FONT_SMALL, theme::TEXT_MUTED);
}

// A place's coordinates and, with a fix, how far and which way.
void UITask::placeCard(lv_obj_t* parent, int32_t lat, int32_t lon) {
  lv_obj_t* card = infoCard(parent);
  char t[40];
  snprintf(t, sizeof(t), "%.5f, %.5f", lat / 1e6, lon / 1e6);
  infoRow(card, "Coordinates", t);
  int32_t mlat, mlon;
  if (!_core->course.currentLocation(mlat, mlon)) return;
  char d[12];
  geo::fmtDist(d, sizeof(d), geo::haversineKm(mlat, mlon, lat, lon), _prefs && _prefs->units_imperial);
  int az = geo::bearingDeg(mlat, mlon, lat, lon);
  snprintf(t, sizeof(t), "%s  %d\xC2\xB0 %s", d, az, geo::bearingCardinal(az));
  infoRow(card, "Distance", t);
}

void UITask::navWaypointMenu(int idx) {
  if (idx < 0 || idx >= _core->waypoints.count()) return;
  _nav_wp = idx;
  const Waypoint& w = _core->waypoints.at(idx);
  lv_obj_t* panel = navPopupPanel(w.label[0] ? w.label : "(unnamed)", false);
  placeCard(panel, w.lat_1e6, w.lon_1e6);

  lv_obj_t* acts = buttonBar(panel);
  struct { const char* text; uint8_t act; bool accent; } btns[] = {
    { UI_SYMBOL_COMPASS " Go", navmap::WP_NAV, true },
    { LV_SYMBOL_EDIT " Name", navmap::WP_RENAME, false },
    { LV_SYMBOL_UPLOAD " Share", navmap::WP_SHARE, false },
    { LV_SYMBOL_TRASH, navmap::WP_DELETE, false },
  };
  for (auto& b : btns) {
    lv_obj_t* bt = barButton(acts, b.text, onNavWpAction, b.act, b.accent);
    if (b.act == navmap::WP_DELETE) _nav_del_lbl = lv_obj_get_child(bt, 0);
  }
}

void UITask::navWaypointAction(uint8_t act) {
  int i = _nav_wp;
  if (i < 0 || i >= _core->waypoints.count()) { navClosePopup(); return; }
  switch (act) {
    case navmap::WP_NAV:
      navPick(navmap::code(navmap::T_WAYPOINT, i));
      break;
    case navmap::WP_RENAME:
      navRenamePopup(i);
      break;
    case navmap::WP_SHARE: {
      char text[80];
      _core->waypoints.shareText(i, text, sizeof(text));
      navClosePopup();
      shareToMessage(text);
      break;
    }
    case navmap::WP_DELETE:
      if (!tapConfirmed(_nav_del_lbl, "Delete?")) break;
      _core->waypoints.remove(i);
      navClosePopup();
      rebuildMapMarkers();
      layoutMap();
      refreshNavBar();
      break;
  }
}

void UITask::navRenamePopup(int idx) {
  _nav_wp = idx;
  lv_obj_t* panel = navPopupPanel("Waypoint name", false);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // above the keyboard
  _nav_ta = textField(panel);
  lv_textarea_set_text(_nav_ta, _core->waypoints.at(idx).label);
  lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);   // draws the cursor
  lv_obj_add_event_cb(_nav_ta, navmap::onLabelInsert, LV_EVENT_INSERT, NULL);

  _nav_kb = kb::create(_nav_overlay, _prefs);
  lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
  lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(_nav_kb, _nav_ta);
  lv_obj_add_event_cb(_nav_kb, onNavRenameKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_nav_kb, onNavRenameKb, LV_EVENT_CANCEL, NULL);
}

void UITask::navRenameDone(bool ok) {
  if (ok && _nav_ta && _nav_wp >= 0 && _nav_wp < _core->waypoints.count()) {
    const Waypoint& w = _core->waypoints.at(_nav_wp);
    bool was_target = _prefs && _prefs->locator_has_target && _prefs->locator_target_kind == 0 &&
                      _prefs->locator_lat_1e6 == w.lat_1e6 && _prefs->locator_lon_1e6 == w.lon_1e6;
    _core->waypoints.rename(_nav_wp, lv_textarea_get_text(_nav_ta));
    if (was_target) {   // the bar shows the target's saved label
      snprintf(_prefs->locator_label, sizeof(_prefs->locator_label), "%s", _core->waypoints.at(_nav_wp).label);
      prefsSave();
    }
    rebuildMapMarkers();
    layoutMap();
    refreshNavBar();
  }
  navClosePopup();
}

// ── Adding waypoints ──────────────────────────────────────────────────────────

void UITask::navAddWaypoint(int32_t lat, int32_t lon) {
  if (_core->waypoints.full()) { showToast(waypointsFull()); return; }
  if (!_core->waypoints.add(lat, lon, rtc_clock.getCurrentTime(), "")) return;
  int i = _core->waypoints.count() - 1;
  char t[48];
  snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(i).label);
  showToast(t);
  rebuildMapMarkers();
  layoutMap();
}

void UITask::navMarkHere() {
  int32_t lat, lon;
  if (!ensureGps() || !_core->course.currentLocation(lat, lon)) return;
  uint16_t secs = NodePrefs::gpsAvgSecs(_prefs ? _prefs->gps_avg_idx : 0);
  if (secs == 0) { navAddWaypoint(lat, lon); return; }
  if (_core->waypoints.full()) { showToast(waypointsFull()); return; }
  navmap::s_avg.start(secs, lat, lon);
  navPollAveraging();
}

void UITask::navAveragingCancel() {
  navmap::s_avg.cancel();
  if (_nav_avg_pill) lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
  showToast("Mark cancelled");
}

// From mapLoop(): sample, update the pill, mark the mean when the window closes.
void UITask::navPollAveraging() {
  if (!navmap::s_avg.active()) return;
  int32_t lat = 0, lon = 0, mlat, mlon;
  bool fix = _core->course.currentLocation(lat, lon);
  GpsAverager::Step st = navmap::s_avg.poll(fix, lat, lon, mlat, mlon);
  if (_nav_avg_pill) {
    if (st == GpsAverager::RUNNING) {
      lv_label_set_text_fmt(_nav_avg_pill, UI_SYMBOL_PIN "  Averaging GPS...  %d s  (%lu fixes)  " LV_SYMBOL_CLOSE,
                            navmap::s_avg.remainingSecs(), (unsigned long)navmap::s_avg.samples());
      lv_obj_remove_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_nav_avg_pill, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (st == GpsAverager::DONE) navAddWaypoint(mlat, mlon);
  else if (st == GpsAverager::NO_FIX) showToast("No GPS fix");
}

// Long-press on the map: what to do with that spot (nothing is added until asked).
void UITask::navDropAt(int x, int y) {
  int32_t lat, lon;
  mapPointToLatLon(x, y, lat, lon);
  navSpotPopup(lat, lon);
}

// ── Share into a conversation ─────────────────────────────────────────────────

// Pick a conversation; the text is waiting in its compose field.
void UITask::shareToMessage(const char* text) {
  snprintf(_share_text, sizeof(_share_text), "%s", text);
  showChats();
  showToast("Pick a conversation to share it in");
}

// ── Tools panel: trail recording, live share, arrival alert, each with its
// options (a section of the schema's PG_NAV page); offline maps; layers --
// all of it lives with the map.

namespace navmap {
enum : uint8_t { TL_TRAIL_TOGGLE, TL_TRAIL_SAVE, TL_TRAIL_LOAD, TL_TRAIL_RESET, TL_TRAIL_GPX, TL_TRACKBACK,
                 TL_SHARE_TOGGLE, TL_SHARE_ONCE, TL_DOWNLOAD, TL_AREAS, TL_REGIONS,
                 TL_WP_HERE, TL_WP_COORDS, TL_SPOT_ADD, TL_SPOT_GO,
                 TL_ST_LOAD, TL_ST_GPX, TL_ST_DELETE,
                 TL_OPT_TRAIL, TL_OPT_SHARE, TL_OPT_ALERT };

// Live-share targets offered in the dropdown: channels, then favourite contacts.
static const int SHARE_TARGETS = MAX_GROUP_CHANNELS + 16;
static uint8_t s_share_kind[SHARE_TARGETS];               // 0 = channel, 1 = contact
static uint8_t s_share_ch[SHARE_TARGETS];
static uint8_t s_share_key[SHARE_TARGETS][NodePrefs::FAVOURITE_PREFIX_LEN];
static int     s_share_n = 0;

// Print sink over a FILE* for TrailStore's GPX writer (same duck type as
// ui-new's BoundedSerialPrint).
struct FilePrint {
  FILE* f;
  bool  ok;
  explicit FilePrint(FILE* file) : f(file), ok(true) {}
  size_t write(const uint8_t* b, size_t n) {
    if (!ok) return 0;
    size_t w = fwrite(b, 1, n, f);
    if (w != n) ok = false;
    return w;
  }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const __FlashStringHelper* s) { return print(reinterpret_cast<const char*>(s)); }
};

// FILE* in the shape TrailStore's writeTo / readFrom / exportGpxFromFile use.
struct FileRW {
  FILE* f;
  size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, f); }
  int    read(uint8_t* b, size_t n)        { return (int)fread(b, 1, n, f); }
};

// Saved trails on the card (L2): /sdcard/trails/trail-YYYYMMDD-HHMM.trl, the
// same format as the one internal slot (TrailEngine's /trail), which the
// list also offers -- the low-battery auto-save still goes there.
static const char* const TRAILS_DIR = "/sdcard/trails";
static const int ST_MAX = 40;
static char s_st_names[ST_MAX][28];   // newest first
static int  s_st_n = 0;
static int  s_st_sel = -2;            // the one the popup is about; -1 = the internal slot
static lv_obj_t* s_st_del_lbl = nullptr;

// "trail-20260926-1405.trl" -> "26 Sep 2026  14:05" (else the name).
static void trailTitle(const char* name, char* out, size_t n) {
  int y, mo, d, h, mi;
  if (sscanf(name, "trail-%4d%2d%2d-%2d%2d", &y, &mo, &d, &h, &mi) == 5 && mo >= 1 && mo <= 12)
    snprintf(out, n, "%d %s %d  %02d:%02d", d, MONTHS[mo - 1], y, h, mi);
  else snprintf(out, n, "%s", name);
}

// Points, distance and recorded time of a saved trail, from its file.
template <typename F>
static bool trailSummary(F& io, int& points, float& meters, uint32_t& secs) {
  uint16_t cnt = 0;
  uint32_t accum = 0;
  if (!persist::readHeader(io, TrailStore::SAVE_MAGIC, TrailStore::SAVE_VERSION, cnt)) return false;
  if (io.read((uint8_t*)&accum, sizeof(accum)) != (int)sizeof(accum)) return false;
  points = 0; meters = 0; secs = accum / 1000;
  TrailPoint prev, p;
  for (uint16_t i = 0; i < cnt; i++) {
    if (io.read((uint8_t*)&p, sizeof(p)) != (int)sizeof(p)) break;
    if (points > 0 && !(p.flags & TRAIL_FLAG_SEG_START))
      meters += TrailStore::haversineMeters(prev.lat_1e6, prev.lon_1e6, p.lat_1e6, p.lon_1e6);
    prev = p;
    points++;
  }
  return true;
}

static void scanTrails() {
  s_st_n = 0;
  DIR* d = opendir(TRAILS_DIR);
  if (!d) return;
  while (struct dirent* de = readdir(d)) {
    size_t l = strlen(de->d_name);
    if (l < 5 || l >= sizeof(s_st_names[0]) || strcmp(de->d_name + l - 4, ".trl") != 0) continue;
    if (de->d_name[0] == '.') continue;   // the live trail's own copy (.live.trl)
    if (s_st_n < ST_MAX) snprintf(s_st_names[s_st_n++], sizeof(s_st_names[0]), "%s", de->d_name);
  }
  closedir(d);
  // Newest first: the names carry the date.
  qsort(s_st_names, s_st_n, sizeof(s_st_names[0]), [](const void* a, const void* b) {
    return -strcmp((const char*)a, (const char*)b);
  });
}

}  // namespace navmap

static void onSavedTrail(lv_event_t* e) { s_ui->savedTrailPopup((int)(intptr_t)lv_event_get_user_data(e)); }

static void onNavTools(lv_event_t* e)       { (void)e; s_ui->navToolsPopup(); }
static void onTrails(lv_event_t* e) {
  s_ui->setTrails(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
static void onVectorMap(lv_event_t* e) {
  s_ui->setVectorMap(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
static void onLiveTiles(lv_event_t* e) {
  s_ui->setLiveTiles(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
static void onNavTool(lv_event_t* e)        { s_ui->navToolAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onNavShareTarget(lv_event_t* e) {
  s_ui->navSetShareTarget(choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}

// A map tool's button (UITask.cpp's barButton); returns its label.
static lv_obj_t* toolButton(lv_obj_t* bar, const char* text, uint8_t act, bool accent) {
  return lv_obj_get_child(barButton(bar, text, onNavTool, act, accent), 0);
}

void UITask::navToolsPopup() {
  lv_obj_t* panel = navPopupPanel("Map tools", true);
  lv_obj_t* list = scrollList(panel);

  sectionTitle(list, "TRAIL");
  _nav_trail_lbl = label(list, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_t* r = buttonBar(list);
  _nav_trail_btn = toolButton(r, "", navmap::TL_TRAIL_TOGGLE, true);
  toolButton(r, LV_SYMBOL_SAVE " Save", navmap::TL_TRAIL_SAVE, false);
  toolButton(r, LV_SYMBOL_DIRECTORY " Load", navmap::TL_TRAIL_LOAD, false);
  r = buttonBar(list);
  _nav_tb_btn = toolButton(r, "", navmap::TL_TRACKBACK, false);
  toolButton(r, LV_SYMBOL_SD_CARD " GPX", navmap::TL_TRAIL_GPX, false);
  _nav_reset_lbl = toolButton(r, LV_SYMBOL_TRASH " Reset", navmap::TL_TRAIL_RESET, false);
  listRow(group(list, nullptr), "Trail options", "Point spacing, auto-pause, low battery", onNavTool,
          (void*)(uintptr_t)navmap::TL_OPT_TRAIL);

  sectionTitle(list, "LIVE SHARE");
  _nav_share_lbl = noteLabel(list, "", THEME_FONT_SMALL, theme::TEXT);
  // Target: channels (as named -- "#" marks a hashtag channel), then
  // favourite contacts (starred, so the two stay apart).
  char opts[MAX_GROUP_CHANNELS * 24 + 16 * 40];
  int o = 0, sel = 0;
  navmap::s_share_n = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS && navmap::s_share_n < navmap::SHARE_TARGETS; i++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
    if (_prefs->loc_share_target_type == 0 && _prefs->loc_share_channel_idx == i) sel = navmap::s_share_n;
    navmap::s_share_kind[navmap::s_share_n] = 0;
    navmap::s_share_ch[navmap::s_share_n++] = (uint8_t)i;
    o += snprintf(opts + o, sizeof(opts) - o, "%s%s", o ? "\n" : "", ch.name);
  }
  int favs = 0;
  for (int i = 0; i < the_mesh.getNumContacts() && favs < 16 && navmap::s_share_n < navmap::SHARE_TARGETS; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (c.type != ADV_TYPE_CHAT || !contactctl::favourite(c)) continue;
    if (_prefs->loc_share_target_type == 1 &&
        memcmp(_prefs->loc_share_dm_prefix, c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) sel = navmap::s_share_n;
    navmap::s_share_kind[navmap::s_share_n] = 1;
    memcpy(navmap::s_share_key[navmap::s_share_n++], c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
    o += snprintf(opts + o, sizeof(opts) - o, "%s" UI_SYMBOL_STAR " %s", o ? "\n" : "", c.name);
    favs++;
  }
  choiceRow(group(list, nullptr), "Send to", nullptr, o ? opts : "(no channels)", sel, onNavShareTarget);
  r = buttonBar(list);
  _nav_share_btn = toolButton(r, "", navmap::TL_SHARE_TOGGLE, true);
  toolButton(r, LV_SYMBOL_UPLOAD " Send once", navmap::TL_SHARE_ONCE, false);

  listRow(group(list, nullptr), "Share options", "How often, for how long", onNavTool, (void*)(uintptr_t)navmap::TL_OPT_SHARE);

  lv_obj_t* g = group(list, "ARRIVAL ALERT");
  schemaRow(g, SETTING(locator_enabled));
  char al[48], rad[12];
  uint16_t m = NodePrefs::locatorRadiusMeters(_prefs->locator_radius_idx);
  geo::fmtDist(rad, sizeof(rad), m / 1000.0f, _prefs->units_imperial);
  snprintf(al, sizeof(al), "%s radius, %s", rad, NodePrefs::locatorModeLabel(_prefs->locator_mode));
  listRow(g, "Alert options", al, onNavTool, (void*)(uintptr_t)navmap::TL_OPT_ALERT);

  g = group(list, "OFFLINE MAPS");
  char ar[48];
  int na = mapview::s_areas.count();
  snprintf(ar, sizeof(ar), na ? "%d on the card - new, rename, refresh, delete" : "None yet - pick one with a frame", na);
  listRow(g, "Map areas", ar, onNavTool, (void*)(uintptr_t)navmap::TL_AREAS);
  lv_obj_t* lsw = switchRow(g, "Live tiles", "Load missing tiles over WiFi", nullptr);
  if (lvport::liveTiles()) lv_obj_add_state(lsw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(lsw, onLiveTiles, LV_EVENT_VALUE_CHANGED, NULL);

  g = group(list, "LAYERS");
  lv_obj_t* tsw = switchRow(g, "Hiking trails", "Marked routes in their colours", nullptr);
  if (lvport::trailsOn()) lv_obj_add_state(tsw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(tsw, onTrails, LV_EVENT_VALUE_CHANGED, NULL);
#ifdef MAP_VECTOR   // unfinished (LvglPort.h vectorOn)
  lv_obj_t* vsw = switchRow(g, "Vector map (test)", "Drawn on the device: trails, contours", nullptr);
  if (lvport::vectorOn()) lv_obj_add_state(vsw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(vsw, onVectorMap, LV_EVENT_VALUE_CHANGED, NULL);
  char vr[48];
  int nv = mapview::s_vpacks.count();
  snprintf(vr, sizeof(vr), nv ? "%d on the card" : "None - packs go in /vmap", nv);
  listRow(g, "Vector regions", vr, onNavTool, (void*)(uintptr_t)navmap::TL_REGIONS);
#endif

  refreshNavTools();
}

void UITask::refreshNavTools() {
  if (!_nav_trail_lbl) return;
  TrailStore& ts = _core->trail.store();
  bool imperial = _prefs && _prefs->units_imperial;
  char dist[12], dur[12];
  geo::fmtDist(dist, sizeof(dist), ts.totalDistanceMeters() / 1000.0f, imperial);
  navmap::fmtDuration(dur, sizeof(dur), ts.elapsedSeconds());
  const char* state = !ts.isActive() ? (ts.empty() ? "Not recording" : "Stopped")
                    : ts.isPaused() ? "Paused (standing still)" : "Recording";
  if (ts.empty()) setText(_nav_trail_lbl, state);
  else setTextFmt(_nav_trail_lbl, "%s  -  %s, %s, %d points", state, dist, dur, ts.count());
  setText(_nav_trail_btn, ts.isActive() ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Record");
  setText(_nav_tb_btn, navmap::s_tb.active() ? LV_SYMBOL_STOP " Stop back" : LV_SYMBOL_LOOP " Track back");

  if (_prefs->loc_share_enabled) {
    char left[12];
    navmap::fmtDuration(left, sizeof(left), _core->live_share.remainingSecs());
    setTextFmt(_nav_share_lbl, "Sharing your position  -  %s left", left);
  } else {
    setText(_nav_share_lbl, "Off. Sends your position while you move, then stops by itself.");
  }
  setText(_nav_share_btn, _prefs->loc_share_enabled ? LV_SYMBOL_STOP " Stop sharing" : LV_SYMBOL_PLAY " Share live");
}

void UITask::navSetShareTarget(int sel) {
  if (sel < 0 || sel >= navmap::s_share_n) return;
  _prefs->loc_share_target_type = navmap::s_share_kind[sel];
  if (navmap::s_share_kind[sel] == 0) _prefs->loc_share_channel_idx = navmap::s_share_ch[sel];
  else memcpy(_prefs->loc_share_dm_prefix, navmap::s_share_key[sel], NodePrefs::FAVOURITE_PREFIX_LEN);
  prefsSave();
}

// A new file in /sdcard/trails named by the local time, "trail-YYYYMMDD-HHMM"
// (seconds since boot before the clock is set), with `ext`; a second save in
// the same minute gets "-2", "-3"...
static void trailFilePath(char* path, size_t n, const char* ext, const NodePrefs* p) {
  char stem[40];
  uint32_t now = rtc_clock.getCurrentTime();
  struct tm ti;
  if (localTime(p, ti, now)) {
    snprintf(stem, sizeof(stem), "%s/trail-%04d%02d%02d-%02d%02d", navmap::TRAILS_DIR, ti.tm_year + 1900,
             ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min);
  } else {
    snprintf(stem, sizeof(stem), "%s/trail-%lu", navmap::TRAILS_DIR, (unsigned long)(millis() / 1000));
  }
  snprintf(path, n, "%s%s", stem, ext);
  struct stat st;
  for (int k = 2; k < 100 && stat(path, &st) == 0; k++) snprintf(path, n, "%s-%d%s", stem, k, ext);
  mapview::makeParents(path);
}

// GPX of the live trail (with the waypoints) to /sdcard/trails/.
bool UITask::exportTrailGpx(char* name_out, size_t n) {
  if (!lvport::mountStorage()) return false;
  char path[64];
  trailFilePath(path, sizeof(path), ".gpx", _prefs);
  FILE* f = fopen(path, "w");
  if (!f) return false;
  navmap::FilePrint out(f);
  _core->trail.store().exportGpx(out, _core->waypoints.store());
  bool ok = out.ok;
  ok = (fclose(f) == 0) && ok;
  snprintf(name_out, n, "%s", strrchr(path, '/') + 1);
  return ok;
}

// The live trail to a new file in /sdcard/trails (named by the local time).
bool UITask::saveTrailToCard(char* name_out, size_t n) {
  char path[64];
  trailFilePath(path, sizeof(path), ".trl", _prefs);
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  navmap::FileRW io{ f };
  bool ok = _core->trail.store().writeTo(io);
  ok = (fclose(f) == 0) && ok;
  if (!ok) remove(path);
  snprintf(name_out, n, "%s", strrchr(path, '/') + 1);
  return ok;
}

// ── The live trail's copy on the card ──
// While there's a trail, /sdcard/trails/.live.trl holds a copy of it: the new
// points appended once a minute and at power-off, so a flat battery, a crash
// or a reboot doesn't lose the route. At boot the copy comes back, recording
// again if it was. A saved trail's format, the header's reserved byte (5)
// saying "recording"; count and time (6, 8) are patched in place. A full
// ring (the oldest points dropping off) is rewritten, at most every 10 min.
namespace navmap {
static const char* const LIVE_TRAIL = "/sdcard/trails/.live.trl";
static const long TRAIL_HDR = 12;   // a trail file's header (TrailStore::writeTo): magic, version, flags, count (uint16), time (uint32)
static uint32_t s_j_gen = 0xFFFFFFFF, s_j_seq = 0, s_j_next = 0, s_j_full_at = 0;
static int s_j_n = 0;
static uint8_t s_j_rec = 0;   // the "recording" byte last written
}

bool UITask::trailJournalTick(bool now) {
  using namespace navmap;
  if (!now && (int32_t)(millis() - s_j_next) < 0) return true;
  s_j_next = millis() + 60000;
  TrailStore& ts = _core->trail.store();
  int n = ts.count();
  uint8_t rec = ts.isActive() ? 1 : 0;
  bool same = ts.gen() == s_j_gen && ts.seq() == s_j_seq && n == s_j_n;
  if (same && (n == 0 || (!rec && s_j_rec == rec))) return true;   // nothing to write (and no card mount tried)
  if (!lvport::mountStorage()) return false;
  if (n == 0) {   // reset: no copy
    if (s_j_n) remove(LIVE_TRAIL);   // only a copy this run wrote (or restored)
    s_j_gen = ts.gen(); s_j_seq = ts.seq(); s_j_n = 0;
    return true;
  }
  bool all = ts.gen() != s_j_gen || n < s_j_n || n - s_j_n != (int)(ts.seq() - s_j_seq);
  if (all && ts.gen() == s_j_gen && n == TrailStore::CAPACITY && !now && millis() - s_j_full_at < 600000) return true;
  FILE* f = all ? nullptr : fopen(LIVE_TRAIL, "r+b");
  bool ok;
  if (!f) {   // the whole trail
    mapview::makeParents(LIVE_TRAIL);
    f = fopen(LIVE_TRAIL, "wb");
    if (!f) return false;
    navmap::FileRW io{ f };
    ok = ts.writeTo(io);
    s_j_full_at = millis();
  } else {    // just what's new
    ok = fseek(f, 0, SEEK_END) == 0;
    for (int i = s_j_n; ok && i < n; i++) ok = fwrite(&ts.at(i), sizeof(TrailPoint), 1, f) == 1;
  }
  uint16_t cnt = (uint16_t)n;
  uint32_t accum = ts.currentAccumulatedMs();
  ok = ok && fseek(f, 5, SEEK_SET) == 0 && fwrite(&rec, 1, 1, f) == 1 && fwrite(&cnt, 2, 1, f) == 1 && fwrite(&accum, 4, 1, f) == 1;
  ok = (fclose(f) == 0) && ok;
  if (ok) { s_j_gen = ts.gen(); s_j_seq = ts.seq(); s_j_n = n; s_j_rec = rec; }
  else s_j_gen = 0xFFFFFFFF;   // next time the whole trail again
  return ok;
}

void UITask::trailJournalRestore() {
  using namespace navmap;
  TrailStore& ts = _core->trail.store();
  if (!ts.empty() || !lvport::mountStorage()) return;
  FILE* f = fopen(LIVE_TRAIL, "rb");
  if (!f) return;
  uint8_t rec = 0;
  navmap::FileRW io{ f };
  bool ok = ts.readFrom(io) && fseek(f, 5, SEEK_SET) == 0 && fread(&rec, 1, 1, f) == 1;
  fclose(f);
  if (!ok || ts.empty()) return;
  if (rec & 1) ts.setActive(true);   // it was recording: carries on (a gap for the time off)
  s_j_gen = ts.gen(); s_j_seq = ts.seq(); s_j_n = ts.count(); s_j_rec = rec & 1;
}

// Map tools > Load, with a card: the saved trails, newest first, and the
// device's own slot (manual saves without a card, the low-battery auto-save).
void UITask::savedTrailsPopup() {
  lv_obj_t* panel = navPopupPanel("Saved trails", true);
  lv_obj_t* list = scrollList(panel);
  navmap::scanTrails();
  for (int i = 0; i < navmap::s_st_n; i++) {
    char title[32], sub[48], path[64];
    navmap::trailTitle(navmap::s_st_names[i], title, sizeof(title));
    snprintf(path, sizeof(path), "%s/%s", navmap::TRAILS_DIR, navmap::s_st_names[i]);
    struct stat st;
    long pts = stat(path, &st) == 0 ? ((long)st.st_size - navmap::TRAIL_HDR) / (long)sizeof(TrailPoint) : 0;
    snprintf(sub, sizeof(sub), "%ld points", pts > 0 ? pts : 0);
    listRow(list, title, sub, onSavedTrail, (void*)(intptr_t)i);
  }
  if (TrailEngine::savedExists()) listRow(list, "Saved on the device", "Without a card, or on low battery", onSavedTrail, (void*)(intptr_t)-1);
  if (lv_obj_get_child_count(list) == 0) label(list, "No saved trails yet - Save one first.", THEME_FONT_BODY, theme::TEXT_MUTED);
}

// One saved trail: what it holds, Load / GPX / Delete.
void UITask::savedTrailPopup(int idx) {
  if (idx >= navmap::s_st_n) return;
  navmap::s_st_sel = idx;
  char title[32];
  if (idx < 0) snprintf(title, sizeof(title), "Saved on the device");
  else navmap::trailTitle(navmap::s_st_names[idx], title, sizeof(title));
  lv_obj_t* panel = navPopupPanel(title, false);

  int pts = 0; float m = 0; uint32_t secs = 0; bool ok = false;
  if (idx >= 0) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%s", navmap::TRAILS_DIR, navmap::s_st_names[idx]);
    if (FILE* f = fopen(path, "rb")) { navmap::FileRW io{ f }; ok = navmap::trailSummary(io, pts, m, secs); fclose(f); }
  } else if (DataStore* ds = the_mesh.getDataStore()) {
    File f = ds->openRead(TrailEngine::TRAIL_FILE);
    if (f) { ok = navmap::trailSummary(f, pts, m, secs); f.close(); }
  }
  char dist[12], dur[12], info[64];
  geo::fmtDist(dist, sizeof(dist), m / 1000.0f, _prefs && _prefs->units_imperial);
  navmap::fmtDuration(dur, sizeof(dur), secs);
  if (ok) snprintf(info, sizeof(info), "%s  -  %s  -  %d points", dist, dur, pts);
  else snprintf(info, sizeof(info), "Can't read this file");
  label(panel, info, THEME_FONT_BODY, theme::TEXT);
  if (!_core->trail.store().empty())
    label(panel, "Loading replaces the trail on the map.", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_t* r = buttonBar(panel);
  lv_obj_t* lb = toolButton(r, LV_SYMBOL_DIRECTORY " Load", navmap::TL_ST_LOAD, true);
  stylePrimary(lv_obj_get_parent(lb));
  if (idx >= 0) toolButton(r, LV_SYMBOL_SD_CARD " GPX", navmap::TL_ST_GPX, false);
  navmap::s_st_del_lbl = toolButton(r, LV_SYMBOL_TRASH " Delete", navmap::TL_ST_DELETE, false);
}

void UITask::savedTrailAction(uint8_t act) {
  int idx = navmap::s_st_sel;
  if (idx >= navmap::s_st_n || idx < -1) return;
  char path[64] = "";
  if (idx >= 0) snprintf(path, sizeof(path), "%s/%s", navmap::TRAILS_DIR, navmap::s_st_names[idx]);
  if (act == navmap::TL_ST_DELETE) {
    if (!tapConfirmed(navmap::s_st_del_lbl, "Delete?")) return;
    bool ok = idx >= 0 ? remove(path) == 0
                       : (the_mesh.getDataStore() && the_mesh.getDataStore()->removeFile(TrailEngine::TRAIL_FILE));
    showToast(ok ? "Trail deleted" : "Delete failed");
    savedTrailsPopup();
    return;
  }
  if (act == navmap::TL_ST_GPX && idx >= 0) {
    char gpx[64];
    snprintf(gpx, sizeof(gpx), "%.*s.gpx", (int)(strlen(path) - 4), path);
    FILE* in = fopen(path, "rb");
    FILE* out = in ? fopen(gpx, "w") : nullptr;
    bool ok = false;
    if (in && out) {
      navmap::FileRW io{ in };
      navmap::FilePrint pr(out);
      ok = TrailStore::exportGpxFromFile(io, pr, _core->waypoints.store()) > 0 && pr.ok;
    }
    if (in) fclose(in);
    if (out) ok = (fclose(out) == 0) && ok;
    char t[64];
    if (ok) { snprintf(t, sizeof(t), "Saved trails/%s", strrchr(gpx, '/') + 1); showToast(t, 3500); }
    else showToast("Can't write to the SD card");
    return;
  }
  // Load: replaces the live trail (recording stops).
  bool ok = false;
  if (idx >= 0) {
    if (FILE* f = fopen(path, "rb")) { navmap::FileRW io{ f }; ok = _core->trail.store().readFrom(io); fclose(f); }
  } else {
    ok = _core->trail.load() == TrailEngine::FILE_OK;
  }
  navClosePopup();
  showToast(ok ? "Trail loaded" : "Load failed");
  rebuildMapMarkers();
  layoutMap();
  if (ok) navFrameTrail();
}

void UITask::navToolAction(uint8_t act) {
  TrailEngine& tr = _core->trail;
  switch (act) {
    case navmap::TL_TRAIL_TOGGLE:
      if (!tr.isActive()) {
        tr.setActive(true);
        int32_t lat, lon;
        if (_core->course.currentLocation(lat, lon)) showToast("Recording the trail");
        else ensureGps();   // turns GPS on, or says it's waiting
      } else {
        tr.setActive(false);
        showToast("Trail stopped");
      }
      break;
    case navmap::TL_TRAIL_SAVE: {
      if (tr.store().empty()) { showToast("No trail to save"); break; }
      char name[32], t[64];
      if (lvport::mountStorage()) {   // on the card, one file per save
        if (saveTrailToCard(name, sizeof(name))) { snprintf(t, sizeof(t), "Saved trails/%s", name); showToast(t, 3000); }
        else showToast("Can't write to the SD card");
        break;
      }
      TrailEngine::FileResult r = tr.save();
      showToast(r == TrailEngine::FILE_OK ? "Trail saved" : "Save failed");
      break;
    }
    case navmap::TL_TRAIL_LOAD: {
      if (lvport::mountStorage()) { savedTrailsPopup(); return; }
      TrailEngine::FileResult r = tr.load();
      showToast(r == TrailEngine::FILE_OK ? "Trail loaded" : r == TrailEngine::FILE_MISSING ? "No saved trail" : "Load failed");
      rebuildMapMarkers();
      layoutMap();
      break;
    }
    case navmap::TL_ST_LOAD:
    case navmap::TL_ST_GPX:
    case navmap::TL_ST_DELETE:
      savedTrailAction(act);
      return;
    case navmap::TL_TRAIL_RESET:
      if (!tapConfirmed(_nav_reset_lbl, "Reset?")) return;
      tr.reset();
      rebuildMapMarkers();
      layoutMap();
      break;
    case navmap::TL_TRACKBACK: {
      if (navmap::s_tb.active()) { navmap::s_tb.stop(); showToast("Track back stopped"); break; }
      int32_t lat = 0, lon = 0;
      bool fix = _core->course.currentLocation(lat, lon);
      if (!navmap::s_tb.start(tr.store(), fix, lat, lon)) { showToast("No trail to walk back"); break; }
      navmap::s_eta.reset();
      if (!fix) ensureGps();
      navClosePopup();
      navFrameTarget();
      refreshNavBar();
      return;
    }
    case navmap::TL_TRAIL_GPX: {
      if (tr.store().empty()) { showToast("No trail to export"); break; }
      char name[40], t[64];
      if (exportTrailGpx(name, sizeof(name))) { snprintf(t, sizeof(t), "Saved trails/%s", name); showToast(t, 3500); }
      else showToast("Can't write to the SD card");
      break;
    }
    case navmap::TL_SHARE_TOGGLE:
      if (_prefs->loc_share_enabled) {
        _prefs->loc_share_enabled = 0;
        showToast("Live share stopped");
      } else {
        _prefs->loc_share_enabled = 1;
        _core->live_share.restartSession();
        int32_t lat, lon;
        if (_core->course.currentLocation(lat, lon)) showToast("Sharing your position");
        else ensureGps();
      }
      prefsSave();
      break;
    case navmap::TL_SHARE_ONCE: {
      int32_t lat, lon;
      if (!ensureGps() || !_core->course.currentLocation(lat, lon)) break;
      // With a session on, straight to its target; otherwise pick a conversation.
      if (_prefs->loc_share_enabled && _core->live_share.send(lat, lon)) { showToast("Position sent"); break; }
      char text[40];
      snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f", lat / 1e6, lon / 1e6);
      navClosePopup();
      shareToMessage(text);
      return;
    }
    case navmap::TL_DOWNLOAD:
      navClosePopup();
      mapDownloadPopup();
      return;
    case navmap::TL_AREAS:
      mapAreasPopup();
      return;
    case navmap::TL_REGIONS:
      mapRegionsPopup();
      return;
    case navmap::TL_OPT_TRAIL:   // one section of the Map options; back returns to the map
    case navmap::TL_OPT_SHARE:
    case navmap::TL_OPT_ALERT:
      showMapOptions(act == navmap::TL_OPT_TRAIL ? settings::SEC_TRAIL
                     : act == navmap::TL_OPT_SHARE ? settings::SEC_LIVE_SHARE : settings::SEC_LOCATOR);
      return;
    case navmap::TL_WP_HERE:
      navClosePopup();
      navMarkHere();
      return;
    case navmap::TL_WP_COORDS:
      navCoordsPopup();
      return;
    case navmap::TL_SPOT_ADD:
      navClosePopup();
      navAddWaypoint(_nav_spot_lat, _nav_spot_lon);
      return;
    case navmap::TL_SPOT_GO:
      navClosePopup();
      navSetTarget(0, nullptr, _nav_spot_lat, _nav_spot_lon, "Map point");
      navFrameTarget();
      refreshNavBar();
      return;
  }
  refreshNavTools();
  refreshNavBar();
}

// ── Waypoint list, add by coordinates, long-press spot ────────────────────────

static void onNavCoordsKb(lv_event_t* e) { s_ui->navCoordsDone(lv_event_get_code(e) == LV_EVENT_READY); }

// The pin button: every waypoint (tap for its menu), plus the two ways to add
// one that aren't a long-press -- here (GPS, averaged per Settings) or typed.
void UITask::navWaypointsPopup() {
  const WaypointModel& wp = _core->waypoints;
  char title[32];
  snprintf(title, sizeof(title), "Waypoints  %d/%d", wp.count(), WaypointStore::CAPACITY);
  lv_obj_t* panel = navPopupPanel(title, true);
  lv_obj_t* r = buttonBar(panel);
  toolButton(r, UI_SYMBOL_PIN " Here (GPS)", navmap::TL_WP_HERE, true);
  toolButton(r, LV_SYMBOL_KEYBOARD " Coordinates", navmap::TL_WP_COORDS, false);

  lv_obj_t* list = scrollList(panel);
  int32_t lat = 0, lon = 0;
  bool gps = _core->course.currentLocation(lat, lon);
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    char t[WAYPOINT_LABEL_LEN + 8], sub[40];
    snprintf(t, sizeof(t), UI_SYMBOL_FLAG "  %s", w.label[0] ? w.label : "(unnamed)");
    snprintf(sub, sizeof(sub), "%.5f, %.5f", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);
    lv_obj_t* row = listRow(list, t, sub, onNavWpMenu, (void*)(uintptr_t)i);
    if (gps) {
      char d[12];
      geo::fmtDist(d, sizeof(d), geo::haversineKm(lat, lon, w.lat_1e6, w.lon_1e6), _prefs && _prefs->units_imperial);
      navRowRight(row, d, theme::TEXT_MUTED, theme::PAD);
    }
  }
  if (wp.count() == 0) {
    noteLabel(list, "None yet. Add one here, type its coordinates, or hold the map on a spot.");
  }
}

// "50.06142, 19.93721 Name" (decimal degrees; comma or space between them; the
// name is optional) -> a new waypoint.
void UITask::navCoordsPopup() {
  if (_core->waypoints.full()) { showToast(waypointsFull()); return; }
  lv_obj_t* panel = navPopupPanel("Add by coordinates", false);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // above the keyboard
  _nav_ta = textField(panel);
  lv_textarea_set_placeholder_text(_nav_ta, "50.06142, 19.93721 Name");
  lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);   // draws the cursor

  _nav_kb = kb::create(_nav_overlay, _prefs);
  lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
  lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(_nav_kb, _nav_ta);
  kb::apply(_nav_kb, kb::L_SYM);   // digits first; "abc" gives letters for the name
  lv_obj_add_event_cb(_nav_kb, onNavCoordsKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_nav_kb, onNavCoordsKb, LV_EVENT_CANCEL, NULL);
}

void UITask::navCoordsDone(bool ok) {
  if (!ok || !_nav_ta) { navWaypointsPopup(); return; }
  // parseLatLon wants "lat,lon"; accept a space too, and read what follows as
  // the name the way a [WAY] share carries it.
  char buf[80] = WAYPOINT_MSG_TAG;
  size_t o = strlen(buf);
  const char* in = lv_textarea_get_text(_nav_ta);
  while (*in == ' ') in++;
  bool sep = strchr(in, ',') != nullptr;
  for (; *in && o < sizeof(buf) - 1; in++) {
    if (!sep && *in == ' ') { buf[o++] = ','; sep = true; while (in[1] == ' ') in++; continue; }
    buf[o++] = *in;
  }
  buf[o] = '\0';
  int32_t lat, lon;
  char name[WAYPOINT_LABEL_LEN * 2];
  if (!geo::parseLatLon(buf, lat, lon, name, sizeof(name))) {
    showToast("Use decimal degrees, e.g. 50.06142, 19.93721");
    return;   // keep the field for a fix
  }
  navClosePopup();
  if (!_core->waypoints.add(lat, lon, rtc_clock.getCurrentTime(), name)) { showToast(waypointsFull()); return; }
  const Waypoint& w = _core->waypoints.at(_core->waypoints.count() - 1);
  char t[40];
  snprintf(t, sizeof(t), "Saved %s", w.label);
  showToast(t);
  // Show where it is.
  double n = (double)(1 << _map_z);
  _map_follow = false;
  _map_cx = navmap::normX(lon) * n;
  _map_cy = navmap::normY(lat) * n + mapCenterBias();
  rebuildMapMarkers();
  layoutMap();
}

void UITask::navSpotPopup(int32_t lat, int32_t lon) {
  _nav_spot_lat = lat;
  _nav_spot_lon = lon;
  lv_obj_t* panel = navPopupPanel("This spot", false);
  placeCard(panel, lat, lon);
  lv_obj_t* r = buttonBar(panel);
  toolButton(r, UI_SYMBOL_FLAG " Add waypoint", navmap::TL_SPOT_ADD, true);
  toolButton(r, UI_SYMBOL_COMPASS " Go here", navmap::TL_SPOT_GO, false);
}
