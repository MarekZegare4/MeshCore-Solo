#pragma once
// Map areas: picking an area to download with a frame on the map, and the
// list of downloaded areas (map/AreaStore.h). Picking one frames it on the
// map above a small sheet -- rename, fill gaps, refresh, trails on / off,
// delete; closing the sheet puts the map back where it was.
//
// The frame: four corner handles on the map, dragged to size it; the map
// pans and zooms under it as usual, so an area can be larger than the screen.
// It is kept in world coordinates (tiles at zoom 0), so it stays on the same
// ground while the view moves. A bar along the bottom gives its size, the
// zoom range, the trails switch and Download; the areas already on the card
// are outlined meanwhile.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after NavMap.h.

namespace areas {

enum : uint8_t { A_RENAME, A_FILL, A_REFRESH, A_TRAILS, A_DELETE };

static bool      s_sel = false;          // the frame is up
static double    s_fx0, s_fy0, s_fx1, s_fy1;   // the frame, world units (tiles at z 0)
static int       s_zmax = 16;
static bool      s_trails = false;
static lv_obj_t* s_layer = nullptr;      // draws the frame / outlines, over the tiles
static lv_obj_t* s_handle[4];            // TL, TR, BL, BR
static lv_obj_t* s_bar = nullptr;
static lv_obj_t* s_info = nullptr;
static lv_obj_t* s_zoom = nullptr;
static lv_obj_t* s_trails_btn = nullptr;
static int       s_show = -1;            // area outlined while its sheet is open
static bool      s_drawn = false;        // the layer's last draw put something on the map
static bool      s_preview = false;      // the map is framing it; the view before:
static double    s_prev_cx, s_prev_cy;
static int       s_prev_z;
static bool      s_prev_follow;
static int       s_idx = -1;             // area in the open popup
static lv_obj_t* s_del_lbl = nullptr;
static const int BAR_H = 84;
static const int HANDLE = 26;

static double worldX(double lon) { return (lon + 180.0) / 360.0; }
static double worldY(double lat) { double r = lat * M_PI / 180.0; return (1.0 - asinh(tan(r)) / M_PI) / 2.0; }
static double lonOf(double wx) { return wx * 360.0 - 180.0; }
static double latOf(double wy) { return atan(sinh(M_PI * (1.0 - 2.0 * wy))) * 180.0 / M_PI; }

// Deepest zoom at which a box `ww` x `wh` (world units) fits `w` x `h` px.
static int fitZoom(double ww, double wh, int w, int h) {
  int z = mapview::MIN_Z;
  for (int k = mapview::MAX_Z; k >= mapview::MIN_Z; k--) {
    double s = (double)(1 << k) * mapview::TILE_PX;
    if (ww * s <= w && wh * s <= h) { z = k; break; }
  }
  return z;
}

static mapview::TileArea frameArea(int w, int h) {
  mapview::TileArea a;
  a.lon0 = lonOf(s_fx0); a.lon1 = lonOf(s_fx1);
  a.lat1 = latOf(s_fy0); a.lat0 = latOf(s_fy1);
  int zfit = fitZoom(s_fx1 - s_fx0, s_fy1 - s_fy0, w, h);
  a.zmin = zfit - 4 < 5 ? (zfit < 5 ? zfit : 5) : zfit - 4;   // a few overview levels (cheap)
  a.zmax = s_zmax < a.zmin ? a.zmin : s_zmax;
  return a;
}

static void boxWorld(const mapview::TileArea& b, double& x0, double& y0, double& x1, double& y1) {
  x0 = worldX(b.lon0); x1 = worldX(b.lon1);
  y0 = worldY(b.lat1); y1 = worldY(b.lat0);
}

static void fmtDate(char* out, size_t n, uint32_t t, const NodePrefs* p) {
  struct tm tm;
  if (!localTime(p, tm, t)) { out[0] = '\0'; return; }
  snprintf(out, n, "%d %s %d", tm.tm_mday, MONTHS[tm.tm_mon], tm.tm_year + 1900);
}

static void onHandle(lv_event_t* e) {
  lv_point_t v;
  lv_indev_get_vect(lv_indev_active(), &v);
  if (v.x || v.y) s_ui->areaHandleDrag((int)(uintptr_t)lv_event_get_user_data(e), v.x, v.y);
}
static void onCancel(lv_event_t* e)   { (void)e; s_ui->areaSelectEnd(); }
static void onGo(lv_event_t* e)       { (void)e; s_ui->areaSelectDownload(); }
static void onZMinus(lv_event_t* e)   { (void)e; s_ui->areaSelectZmax(-1); }
static void onZPlus(lv_event_t* e)    { (void)e; s_ui->areaSelectZmax(+1); }
static void onTrailsChip(lv_event_t* e) { (void)e; s_trails = !s_trails; s_ui->areaLayout(); }
static void onArea(lv_event_t* e)     { s_ui->mapAreaPopup((int)(intptr_t)lv_event_get_user_data(e)); }
static void onNewArea(lv_event_t* e)  { (void)e; s_ui->areaSelectBegin(); }
static void onDlOpen(lv_event_t* e)   { (void)e; s_ui->navClosePopup(); s_ui->mapDownloadPopup(); }
static void onAct(lv_event_t* e)      { s_ui->mapAreaAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onRenameKb(lv_event_t* e) { s_ui->mapAreaRenameDone(lv_event_get_code(e) == LV_EVENT_READY); }

// LV_EVENT_DRAW_MAIN of the layer: outlines, the frame and the dim outside it.
static void drawLayer(lv_event_t* e) {
  s_drawn = s_sel || s_show >= 0;
  if (!s_drawn) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  double scale = 0;   // world -> px
  double left = mapview::s_left, top = mapview::s_top;
  int z = s_ui->mapZoomLevel();
  scale = (double)(1 << z) * mapview::TILE_PX;
  auto rect = [&](double x0, double y0, double x1, double y1, lv_area_t& r) {
    r.x1 = a.x1 + (int32_t)lround(x0 * scale - left);
    r.y1 = a.y1 + (int32_t)lround(y0 * scale - top);
    r.x2 = a.x1 + (int32_t)lround(x1 * scale - left);
    r.y2 = a.y1 + (int32_t)lround(y1 * scale - top);
  };
  lv_draw_rect_dsc_t d;
  // Areas on the card: thin outlines (the shown one in the accent colour).
  for (int i = 0; i < mapview::s_areas.count(); i++) {
    if (!s_sel && i != s_show) continue;
    double x0, y0, x1, y1;
    boxWorld(mapview::s_areas.at(i).box, x0, y0, x1, y1);
    lv_area_t r;
    rect(x0, y0, x1, y1, r);
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_width = i == s_show ? 2 : 1;
    d.border_color = lv_color_hex(i == s_show ? theme::ACCENT : 0x9A958C);
    d.border_opa = LV_OPA_COVER;
    lv_draw_rect(layer, &d, &r);
  }
  if (!s_sel) return;
  lv_area_t f;
  rect(s_fx0, s_fy0, s_fx1, s_fy1, f);
  lv_draw_rect_dsc_init(&d);   // dim everything outside the frame
  d.bg_color = lv_color_hex(theme::BG);
  d.bg_opa = LV_OPA_50;
  lv_area_t r;
  r = { a.x1, a.y1, a.x2, f.y1 - 1 };            if (r.y2 >= r.y1) lv_draw_rect(layer, &d, &r);
  r = { a.x1, f.y2 + 1, a.x2, a.y2 };            if (r.y2 >= r.y1) lv_draw_rect(layer, &d, &r);
  r = { a.x1, f.y1, f.x1 - 1, f.y2 };            if (r.x2 >= r.x1) lv_draw_rect(layer, &d, &r);
  r = { f.x2 + 1, f.y1, a.x2, f.y2 };            if (r.x2 >= r.x1) lv_draw_rect(layer, &d, &r);
  lv_draw_rect_dsc_init(&d);
  d.bg_opa = LV_OPA_TRANSP;
  d.border_width = 2;
  d.border_color = lv_color_hex(theme::ACCENT);
  d.border_opa = LV_OPA_COVER;
  lv_draw_rect(layer, &d, &f);
}

static lv_obj_t* barButton(lv_obj_t* row, const char* text, lv_event_cb_t cb, int w) {
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_size(b, w, 34);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, text, THEME_FONT_SMALL, theme::TEXT));
  return b;
}

// A sheet button: its icon over a short word.
static lv_obj_t* actButton(lv_obj_t* row, const char* icon, const char* text, uint8_t act) {
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_height(b, 46);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_width(b, 1);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, onAct, LV_EVENT_CLICKED, (void*)(uintptr_t)act);
  lv_obj_align(label(b, icon, THEME_FONT_BODY, theme::ACCENT), LV_ALIGN_TOP_MID, 0, 3);
  lv_obj_t* l = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -2);
  return l;
}

}  // namespace areas

int UITask::mapZoomLevel() const { return _map_z; }

// From buildMap(): the layer and the handles, hidden until wanted.
void UITask::areaBuildLayer(lv_obj_t* body) {
  using namespace areas;
  s_sel = false;
  s_bar = s_info = s_zoom = s_trails_btn = nullptr;
  s_layer = lv_obj_create(body);
  lv_obj_remove_style_all(s_layer);
  lv_obj_set_size(s_layer, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(s_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_layer, drawLayer, LV_EVENT_DRAW_MAIN, NULL);
  for (int i = 0; i < 4; i++) {
    lv_obj_t* h = lv_obj_create(body);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, HANDLE, HANDLE);
    lv_obj_set_style_radius(h, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(h, lv_color_hex(theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(h, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_width(h, 3, 0);
    lv_obj_set_ext_click_area(h, 10);
    lv_obj_add_flag(h, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(h, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(h, onHandle, LV_EVENT_PRESSING, (void*)(uintptr_t)i);
    lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN);
    s_handle[i] = h;
  }
}

// Map tools > Download an area (or the Nodes map's download button): the
// frame over the middle of the view.
void UITask::areaSelectBegin() {
  using namespace areas;
  if (_nav_overlay) navClosePopup();
  if (_dl_overlay) mapDownloadClose();
  if (!_map_area || !s_layer || s_sel) return;
  s_sel = true;
  s_show = -1;
  // Between the button columns and above the bar, the handles clear of the buttons.
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area) - BAR_H;
  double scale = (double)(1 << _map_z) * mapview::TILE_PX;
  s_fx0 = (mapview::s_left + 70) / scale; s_fx1 = (mapview::s_left + w - 70) / scale;
  s_fy0 = (mapview::s_top + 30) / scale;  s_fy1 = (mapview::s_top + h - 22) / scale;
  int src_max = mapview::s_dl.sourceMaxZ();
  s_zmax = _map_z + 3 > src_max ? src_max : _map_z + 3;
  s_trails = lvport::trailsOn();

  s_bar = lv_obj_create(_map_area);
  lv_obj_remove_style_all(s_bar);
  lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_bar, LV_OBJ_FLAG_CLICKABLE);   // presses on it don't pan the map
  lv_obj_set_size(s_bar, LV_PCT(100), BAR_H);
  lv_obj_align(s_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(s_bar, 6, 0);
  lv_obj_set_style_pad_row(s_bar, 4, 0);
  lv_obj_set_flex_flow(s_bar, LV_FLEX_FLOW_COLUMN);
  s_info = label(s_bar, "", THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(s_info, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_info, LV_PCT(100));
  lv_obj_t* row = lv_obj_create(s_bar);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 34);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 4, 0);
  barButton(row, LV_SYMBOL_CLOSE, onCancel, 34);
  barButton(row, LV_SYMBOL_MINUS, onZMinus, 30);
  s_zoom = label(row, "", THEME_FONT_SMALL, theme::ACCENT);
  lv_obj_set_width(s_zoom, 34);
  lv_obj_set_style_text_align(s_zoom, LV_TEXT_ALIGN_CENTER, 0);
  barButton(row, LV_SYMBOL_PLUS, onZPlus, 30);
  s_trails_btn = barButton(row, "Trails", onTrailsChip, 58);
  lv_obj_t* go = barButton(row, LV_SYMBOL_DOWNLOAD " Get", onGo, 10);
  lv_obj_set_flex_grow(go, 1);
  stylePrimary(go);
  areaLayout();
}

void UITask::areaSelectEnd() {
  using namespace areas;
  s_sel = false;
  if (s_bar) lv_obj_delete_async(s_bar);
  s_bar = s_info = s_zoom = s_trails_btn = nullptr;
  for (lv_obj_t* h : s_handle) if (h && _map_area) lv_obj_add_flag(h, LV_OBJ_FLAG_HIDDEN);
  if (s_layer) lv_obj_invalidate(s_layer);
}

bool UITask::areaSelecting() const { return areas::s_sel; }

void UITask::areaSelectZmax(int d) {
  using namespace areas;
  int z = s_zmax + d;
  if (z < mapview::MIN_Z || z > mapview::MAX_Z || z > mapview::s_dl.sourceMaxZ()) return;
  s_zmax = z;
  areaLayout();
}

// A corner dragged by (dx, dy) px; the opposite one stays.
void UITask::areaHandleDrag(int corner, int dx, int dy) {
  using namespace areas;
  double scale = (double)(1 << _map_z) * mapview::TILE_PX;
  double min = 30 / scale;   // not smaller than that on screen
  if (corner == 0 || corner == 2) s_fx0 = fmin(s_fx0 + dx / scale, s_fx1 - min);
  else                            s_fx1 = fmax(s_fx1 + dx / scale, s_fx0 + min);
  if (corner == 0 || corner == 1) s_fy0 = fmin(s_fy0 + dy / scale, s_fy1 - min);
  else                            s_fy1 = fmax(s_fy1 + dy / scale, s_fy0 + min);
  areaLayout();
}

// From layoutMap(): handles and the bar follow the view.
void UITask::areaLayout() {
  using namespace areas;
  if (!s_layer || !_map_area) return;
  if (s_sel || s_show >= 0 || s_drawn) lv_obj_invalidate(s_layer);   // not a full-map redraw each refresh for nothing
  if (!s_sel) return;
  double scale = (double)(1 << _map_z) * mapview::TILE_PX;
  double xs[2] = { s_fx0 * scale - mapview::s_left, s_fx1 * scale - mapview::s_left };
  double ys[2] = { s_fy0 * scale - mapview::s_top, s_fy1 * scale - mapview::s_top };
  for (int i = 0; i < 4; i++) {
    lv_obj_set_pos(s_handle[i], (int)lround(xs[i & 1]) - HANDLE / 2, (int)lround(ys[i >> 1]) - HANDLE / 2);
    lv_obj_remove_flag(s_handle[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_handle[i]);
  }
  if (s_bar) lv_obj_move_foreground(s_bar);
  if (!s_info) return;
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area);
  mapview::TileArea a = frameArea(w, h);
  uint32_t n = mapview::countTiles(a);
  // Its size on the ground, at the frame's middle latitude.
  double midlat = latOf((s_fy0 + s_fy1) / 2);
  double km_w = (s_fx1 - s_fx0) * 40075.0 * cos(midlat * M_PI / 180.0);
  double km_h = (s_fy1 - s_fy0) * 40075.0 * cos(midlat * M_PI / 180.0);
  bool imperial = _prefs && _prefs->units_imperial;
  char dim[32], size[16];
  if (imperial) snprintf(dim, sizeof(dim), "%.1f x %.1f mi", km_w * 0.621371, km_h * 0.621371);
  else snprintf(dim, sizeof(dim), km_w < 10 ? "%.1f x %.1f km" : "%.0f x %.0f km", km_w, km_h);
  uint64_t bytes = (uint64_t)n * mapview::AVG_TILE_BYTES;
  if (bytes < 1024 * 1024) snprintf(size, sizeof(size), "%lu KB", (unsigned long)(bytes / 1024));
  else snprintf(size, sizeof(size), "%lu MB", (unsigned long)((bytes + 512 * 1024) >> 20));
  bool too = n > mapview::TileDownloader::MAX_TILES;
  lv_label_set_text_fmt(s_info, "%s  -  z%d-%d  -  %lu tiles, %s%s", dim, a.zmin, a.zmax, (unsigned long)n, size,
                        too ? "  -  too large" : "");
  lv_obj_set_style_text_color(s_info, lv_color_hex(too ? theme::FAIL : theme::TEXT), 0);
  lv_label_set_text_fmt(s_zoom, "z%d", a.zmax);
  if (s_trails_btn) {
    lv_obj_set_style_bg_color(s_trails_btn, lv_color_hex(s_trails ? theme::ACCENT_DIM : theme::SURFACE), 0);
    lv_label_set_text(lv_obj_get_child(s_trails_btn, 0), s_trails ? LV_SYMBOL_OK " Trails" : "Trails");
  }
}

// Start a download of `box` (a new area, or one on the list with `idx`).
bool UITask::areaStartDownload(const mapview::TileArea& box, bool force, bool trails) {
  char ssid[33], pass[65];
  if (mapview::s_dl.active()) { showToast("A download is running"); return false; }
  if (!lvport::wifiAllowed()) { showToast("WiFi is off - Settings > WiFi"); return false; }
  if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) { showToast("Pick a WiFi network first - Settings > WiFi", 3000); return false; }
  if (!lvport::mountStorage()) { showToast("No SD card"); return false; }
  if (!mapview::s_dl.start(box, ssid, pass, force, trails ? 1 : 0)) { showToast(mapview::s_dl.message()); return false; }
  return true;
}

void UITask::areaSelectDownload() {
  using namespace areas;
  if (!_map_area) return;
  mapview::TileArea a = frameArea(lv_obj_get_width(_map_area), lv_obj_get_height(_map_area));
  if (mapview::countTiles(a) > mapview::TileDownloader::MAX_TILES) { showToast("Too large - make it smaller or lower the zoom", 3000); return; }
  if (!areaStartDownload(a, false, s_trails)) return;
  int i = mapview::s_areas.add(mapview::s_dl.area(), s_trails, rtc_clock.getCurrentTime());
  char t[64];
  snprintf(t, sizeof(t), "Downloading %s - rename it in Map areas", mapview::s_areas.at(i).name);
  showToast(t, 3000);
  areaSelectEnd();
}

// ── The list ──

void UITask::mapAreasPopup() {
  using namespace areas;
  lv_obj_t* panel = navPopupPanel("Map areas", true);
  lv_obj_t* list = scrollList(panel);
  int n = mapview::s_areas.count();
  if (mapview::s_areas.deleting()) {
    char t[48];
    snprintf(t, sizeof(t), "Deleting... %lu files", (unsigned long)mapview::s_areas.deleted());
    label(list, t, THEME_FONT_SMALL, theme::ACCENT);
  }
  mapview::TileArea job;   // a download running or left unfinished: its progress / Resume
  if (mapview::s_dl.active() || mapview::s_dl.savedJob(job)) {
    char sub[48];
    if (mapview::s_dl.active())
      snprintf(sub, sizeof(sub), "%lu of %lu tiles", (unsigned long)mapview::s_dl.processed(), (unsigned long)mapview::s_dl.total());
    listRow(group(list, nullptr), mapview::s_dl.active() ? LV_SYMBOL_DOWNLOAD "  Downloading" : LV_SYMBOL_PAUSE "  Unfinished download",
            mapview::s_dl.active() ? sub : "Resume or discard it", onDlOpen, nullptr);
  }
  if (n) {
    lv_obj_t* g = group(list, nullptr);
    for (int i = n - 1; i >= 0; i--) {   // newest first
      const mapview::MapArea& m = mapview::s_areas.at(i);
      char sub[64], date[16];
      fmtDate(date, sizeof(date), m.created, _prefs);
      snprintf(sub, sizeof(sub), "z%d-%d%s%s%s%s", m.box.zmin, m.box.zmax,
               (m.flags & mapview::AreaStore::F_TRAILS) ? "  -  trails" : "",
               (m.flags & mapview::AreaStore::F_COMPLETE) ? "" : "  -  unfinished", date[0] ? "  -  " : "", date);
      listRow(g, m.name, sub, onArea, (void*)(intptr_t)i);
    }
  } else {
    noteLabel(list, "No areas yet. Maps downloaded before this list are still on the card.");
  }
  lv_obj_t* add = lv_button_create(list);
  lv_obj_set_size(add, LV_PCT(100), 38);
  lv_obj_set_style_radius(add, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(add, 0, 0);
  lv_obj_add_event_cb(add, onNewArea, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(add, LV_SYMBOL_PLUS "  New area", THEME_FONT_BODY, theme::TEXT));
  stylePrimary(add);
}

void UITask::mapAreaPopup(int idx) {
  using namespace areas;
  if (idx < 0 || idx >= mapview::s_areas.count()) return;
  const mapview::MapArea& m = mapview::s_areas.at(idx);
  lv_obj_t* panel = navPopupPanel(m.name, false, true);   // ends an earlier preview first
  s_idx = idx;
  char info[96], date[16];
  fmtDate(date, sizeof(date), m.created, _prefs);
  uint32_t n = mapview::countTiles(m.box);
  snprintf(info, sizeof(info), "Zoom %d-%d, %lu tiles%s%s%s%s", m.box.zmin, m.box.zmax, (unsigned long)n,
           (m.flags & mapview::AreaStore::F_TRAILS) ? ", trails" : "", date[0] ? "  -  " : "", date,
           (m.flags & mapview::AreaStore::F_COMPLETE) ? "" : "\nUnfinished - Fill gaps completes it");
  noteLabel(panel, info);
  lv_obj_t* r = buttonBar(panel);
  actButton(r, LV_SYMBOL_EDIT, "Rename", A_RENAME);
  actButton(r, LV_SYMBOL_DOWNLOAD, "Fill", A_FILL);
  actButton(r, LV_SYMBOL_REFRESH, "Refresh", A_REFRESH);
  actButton(r, (m.flags & mapview::AreaStore::F_TRAILS) ? LV_SYMBOL_MINUS : LV_SYMBOL_PLUS, "Trails", A_TRAILS);
  s_del_lbl = actButton(r, LV_SYMBOL_TRASH, "Delete", A_DELETE);

  // Framed in the map above the sheet: the view is kept to go back to.
  layoutNow(panel);
  int w = _map_area ? lv_obj_get_width(_map_area) : 320, h = _map_area ? lv_obj_get_height(_map_area) : 218;
  int sheet = lv_obj_get_height(panel) + 8;   // its inset
  int above = h - sheet;
  if (above < 60) above = 60;
  s_prev_cx = _map_cx; s_prev_cy = _map_cy; s_prev_z = _map_z; s_prev_follow = _map_follow;
  s_preview = true;
  double x0, y0, x1, y1;
  boxWorld(m.box, x0, y0, x1, y1);
  _map_z = fitZoom(x1 - x0, y1 - y0, w - 24, above - 16);
  double s = (double)(1 << _map_z);
  _map_cx = (x0 + x1) / 2 * s;
  _map_cy = (y0 + y1) / 2 * s + (double)(h - above) / 2 / mapview::TILE_PX;   // centred in the part left showing
  _map_follow = false;
  s_show = idx;
  layoutMap();
}

void UITask::areaPreviewEnd() {
  using namespace areas;
  if (!s_preview) return;
  s_preview = false;
  s_show = -1;
  _map_cx = s_prev_cx; _map_cy = s_prev_cy; _map_z = s_prev_z; _map_follow = s_prev_follow;
  if (_screen == SCR_MAP && _map_area) layoutMap();
}

void UITask::mapAreaAction(uint8_t act) {
  using namespace areas;
  if (s_idx < 0 || s_idx >= mapview::s_areas.count()) return;
  mapview::MapArea m = mapview::s_areas.at(s_idx);
  bool trails = m.flags & mapview::AreaStore::F_TRAILS;
  switch (act) {
    case A_RENAME: {
      lv_obj_t* panel = navPopupPanel("Area name", false);
      lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // above the keyboard
      _nav_ta = textField(panel);
      lv_textarea_set_max_length(_nav_ta, sizeof(m.name) - 1);
      lv_textarea_set_text(_nav_ta, m.name);
      lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);
      _nav_kb = kb::create(_nav_overlay, _prefs);
      lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
      lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_keyboard_set_textarea(_nav_kb, _nav_ta);
      lv_obj_add_event_cb(_nav_kb, onRenameKb, LV_EVENT_READY, NULL);
      lv_obj_add_event_cb(_nav_kb, onRenameKb, LV_EVENT_CANCEL, NULL);
      return;
    }
    case A_FILL:
    case A_REFRESH:
      if (!areaStartDownload(m.box, act == A_REFRESH, trails)) return;
      navClosePopup();
      showToast(act == A_REFRESH ? "Downloading it all again" : "Fetching what's missing");
      return;
    case A_TRAILS:
      if (trails) {   // off: its trail tiles go (unless another area has them)
        if (mapview::s_areas.deleting()) { showToast("Still deleting - try again shortly"); return; }
        mapview::s_areas.remove(s_idx, false, true);
        navClosePopup();
        showToast("Removing its trails");
        return;
      }
      if (!areaStartDownload(m.box, false, true)) return;
      mapview::s_areas.setFlag(s_idx, mapview::AreaStore::F_TRAILS, true);
      navClosePopup();
      showToast("Fetching its trails");
      return;
    case A_DELETE:
      if (!tapConfirmed(s_del_lbl, "Delete?")) return;
      if (mapview::s_areas.deleting()) { showToast("Still deleting - try again shortly"); return; }
      if (mapview::s_dl.active() && mapview::s_areas.find(mapview::s_dl.area()) == s_idx) mapview::s_dl.cancel();
      if (s_show == s_idx) s_show = -1;
      else if (s_show > s_idx) s_show--;
      mapview::s_areas.remove(s_idx, true, true);
      navClosePopup();
      showToast("Deleting the area");
      return;
  }
}

void UITask::mapAreaRenameDone(bool ok) {
  using namespace areas;
  if (ok && _nav_ta) mapview::s_areas.rename(s_idx, lv_textarea_get_text(_nav_ta));
  navClosePopup();
  mapAreasPopup();
}
