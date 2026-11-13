#pragma once
// Planned routes on the L2 (../PlannedRoute.h, ui-core/RouteGuide.h): Map
// tools > Route. The list of routes on the device, Follow / Follow reversed /
// Delete, and Import from the SD card (a .gpx in /routes or the card's top
// folder, ui-core/GpxImport.h) -- or over USB from the site / tools/route.py.
//
// Following: the guide's target is the navigation bar's (after a track back,
// before the Locator's), with the distance left on the route in its title and
// the ETA to the route's end; the route is drawn under the trail, every point
// of it (the map zooms in), cut to what's in view and to 2 px steps.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after NavMap.h.

#include "../ui-core/GpxImport.h"

namespace routemap {

enum : uint8_t { RT_LIST = navmap::TL_ROUTE_BASE, RT_STOP, RT_FOLLOW, RT_FOLLOW_REV, RT_DELETE, RT_IMPORT };

static const uint32_t COLOR = 0xD65DB1;
static float* s_nx = nullptr;   // normalised Web-Mercator, offsets from s_ox / s_oy (as the trail's)
static float* s_ny = nullptr;
static double s_ox = 0, s_oy = 0;
static int s_n = 0;
static int s_slot = -2;          // what s_nx holds
static bool s_rev = false;
static uint16_t s_gen = 0xFFFF;
static lv_point_precise_t* s_pts = nullptr;
static int s_pts_n = 0;
static int32_t s_pts_x0 = 0, s_pts_y0 = 0;
static lv_obj_t* s_line = nullptr;
static int s_sel = -1;                 // the route a popup is about
static lv_obj_t* s_del_lbl = nullptr;
static int32_t s_last_tlat = 0, s_last_tlon = 0;
static uint8_t s_last_state = 0xFF;

// .gpx files on the card that can be imported.
static const int GPX_MAX = 40;
static PSRAM_BSS char s_gpx[GPX_MAX][64];
static int s_gpx_n = 0;

static void scanGpx() {
  s_gpx_n = 0;
  static const char* const DIRS[] = { "/sdcard/routes", "/sdcard" };
  for (const char* dir : DIRS) {
    DIR* d = opendir(dir);
    if (!d) continue;
    while (struct dirent* de = readdir(d)) {
      size_t l = strlen(de->d_name);
      if (l < 5 || de->d_name[0] == '.' || strcasecmp(de->d_name + l - 4, ".gpx") != 0) continue;
      if (s_gpx_n < GPX_MAX && strlen(dir) + l + 2 <= sizeof(s_gpx[0]))
        snprintf(s_gpx[s_gpx_n++], sizeof(s_gpx[0]), "%s/%s", dir, de->d_name);
    }
    closedir(d);
  }
}

static void onDraw(lv_event_t* e) {
  if (s_pts_n < 2) return;
  lv_obj_t* obj = lv_event_get_current_target_obj(e);
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  if (a.x1 != s_pts_x0 || a.y1 != s_pts_y0) {
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

// The followed route's points, once per route (and again after a change to the slots).
static bool sync(const RouteGuide& g) {
  const int slot = g.slot();
  if (slot == s_slot && PlannedRoutes::generation() == s_gen) return false;
  s_slot = slot;
  s_gen = PlannedRoutes::generation();
  s_n = 0;
  if (slot < 0) return true;
  if (!s_nx) {
    s_nx = psramBuf<float>(PlannedRoutes::MAX_POINTS);
    s_ny = psramBuf<float>(PlannedRoutes::MAX_POINTS);
    s_pts = psramBuf<lv_point_precise_t>(PlannedRoutes::MAX_POINTS + 8);
  }
  if (!s_nx || !s_ny || !s_pts) return true;
  PlannedRoutes::forEach(slot, [&](const RoutePoint& p, int i) {
    if (i >= PlannedRoutes::MAX_POINTS) return;
    if (i == 0) { s_ox = navmap::normX(p.lon_1e6); s_oy = navmap::normY(p.lat_1e6); }
    s_nx[i] = (float)(navmap::normX(p.lon_1e6) - s_ox);
    s_ny[i] = (float)(navmap::normY(p.lat_1e6) - s_oy);
    s_n = i + 1;
  });
  return true;
}

// Screen points for the view: a segment whose box misses the view breaks the
// line (one crossing it, both ends outside, stays: a long straight stretch
// zoomed in), and within a stretch points closer than 2 px to the last are
// left out but for its end.
static void layout(double scale) {
  s_pts_n = 0;
  if (!s_line) return;
  lv_obj_invalidate(s_line);
  if (!s_pts || s_n < 2) return;
  lv_area_t a;
  lv_obj_get_coords(s_line, &a);
  s_pts_x0 = a.x1; s_pts_y0 = a.y1;
  const float fs = (float)scale;
  const float ox = (float)(s_ox * scale - mapview::s_left) + a.x1;
  const float oy = (float)(s_oy * scale - mapview::s_top) + a.y1;
  const float M = 8, L = a.x1 - M, R = a.x2 + M, T = a.y1 - M, B = a.y2 + M;
  const int cap = PlannedRoutes::MAX_POINTS + 6;
  bool open = false, held = false;
  float px = 0, py = 0, lx = 0, ly = 0, hx = 0, hy = 0;
  auto push = [&](float x, float y) {
    if (s_pts_n >= cap) return;
    s_pts[s_pts_n].x = (lv_value_precise_t)lroundf(x);
    s_pts[s_pts_n].y = (lv_value_precise_t)lroundf(y);
    s_pts_n++;
    lx = x; ly = y;
  };
  auto brk = [&]() {
    if (held) push(hx, hy);
    if (open && s_pts_n < cap) { s_pts[s_pts_n].x = LV_DRAW_LINE_POINT_NONE; s_pts[s_pts_n].y = LV_DRAW_LINE_POINT_NONE; s_pts_n++; }
    open = held = false;
  };
  for (int i = 0; i < s_n; i++) {
    const float x = s_nx[i] * fs + ox, y = s_ny[i] * fs + oy;
    if (i > 0) {
      const bool vis = fminf(px, x) <= R && fmaxf(px, x) >= L && fminf(py, y) <= B && fmaxf(py, y) >= T;
      if (!vis) {
        if (open) brk();
      } else {
        if (!open) { push(px, py); open = true; }
        if (fabsf(x - lx) < 2 && fabsf(y - ly) < 2) { hx = x; hy = y; held = true; }
        else { push(x, y); held = false; }
      }
    }
    px = x; py = y;
  }
  if (held) push(hx, hy);
}

}  // namespace routemap

static void onPlannedRouteRow(lv_event_t* e)  { s_ui->routePopup((int)(intptr_t)lv_event_get_user_data(e)); }
static void onRouteGpx(lv_event_t* e)  { s_ui->routeImport((int)(intptr_t)lv_event_get_user_data(e)); }
static void onRouteImportList(lv_event_t* e) { (void)e; s_ui->routeGpxPopup(); }

void UITask::buildRouteLayer() {
  lv_obj_t* o = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_line_width(o, 4, 0);
  lv_obj_set_style_line_color(o, lv_color_hex(routemap::COLOR), 0);
  lv_obj_set_style_line_opa(o, LV_OPA_80, 0);
  lv_obj_set_style_line_rounded(o, true, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(o, routemap::onDraw, LV_EVENT_DRAW_MAIN, nullptr);
  routemap::s_line = o;
  routemap::s_pts_n = 0;
  routemap::s_slot = -2;   // a new map: the points again
}

void UITask::layoutRoute(double scale) {
  routemap::sync(_core->guide);
  routemap::layout(scale);
}

// Map tools > Route: the routes on the device, then Import.
void UITask::routesPopup() {
  lv_obj_t* panel = navPopupPanel("Routes", true);
  lv_obj_t* list = scrollList(panel);
  const RouteGuide& g = _core->guide;
  const bool imperial = _prefs && _prefs->units_imperial;
  RouteInfo ri;
  for (int s = 0; s < PlannedRoutes::SLOTS; s++) {
    if (!PlannedRoutes::info(s, ri)) continue;
    char title[ROUTE_NAME_LEN + 8], sub[48], d[12];
    snprintf(title, sizeof(title), "%s%s", s == g.slot() ? LV_SYMBOL_GPS " " : "", ri.name[0] ? ri.name : "Route");
    geo::fmtDist(d, sizeof(d), ri.length_m / 1000.0f, imperial);
    snprintf(sub, sizeof(sub), "%s  -  %u points%s", d, (unsigned)ri.count, s == g.slot() ? "  -  following" : "");
    listRow(list, title, sub, onPlannedRouteRow, (void*)(intptr_t)s);
  }
  if (lv_obj_get_child_count(list) == 0)
    noteLabel(list, "No routes yet. Import a GPX from the SD card, or send one over USB from the Solo site's tools.");
  listRow(group(list, nullptr), LV_SYMBOL_SD_CARD "  Import GPX", "From the card: /routes or its top folder", onRouteImportList, nullptr);
}

// One route: what it is, Follow / Follow reversed / Delete (Stop while followed).
void UITask::routePopup(int slot) {
  RouteInfo ri;
  if (!PlannedRoutes::info(slot, ri)) { routesPopup(); return; }
  routemap::s_sel = slot;
  lv_obj_t* panel = navPopupPanel(ri.name[0] ? ri.name : "Route", false);
  char d[12], info[64];
  geo::fmtDist(d, sizeof(d), ri.length_m / 1000.0f, _prefs && _prefs->units_imperial);
  snprintf(info, sizeof(info), "%s  -  %u points", d, (unsigned)ri.count);
  label(panel, info, THEME_FONT_BODY, theme::TEXT);
  lv_obj_t* r = buttonBar(panel);
  if (slot == _core->guide.slot()) {
    stylePrimary(lv_obj_get_parent(toolButton(r, LV_SYMBOL_STOP " Stop", routemap::RT_STOP, true)));
  } else {
    stylePrimary(lv_obj_get_parent(toolButton(r, LV_SYMBOL_PLAY " Follow", routemap::RT_FOLLOW, true)));
    toolButton(r, LV_SYMBOL_LOOP " Reversed", routemap::RT_FOLLOW_REV, false);
  }
  routemap::s_del_lbl = toolButton(r, LV_SYMBOL_TRASH " Delete", routemap::RT_DELETE, false);
}

void UITask::routeGpxPopup() {
  lv_obj_t* panel = navPopupPanel("Import GPX", true);
  lv_obj_t* list = scrollList(panel);
  if (!lvport::mountStorage()) { noteLabel(list, "No SD card."); return; }
  routemap::scanGpx();
  for (int i = 0; i < routemap::s_gpx_n; i++) {
    const char* p = routemap::s_gpx[i];
    struct stat st;
    char sub[32];
    snprintf(sub, sizeof(sub), "%s  -  %ld KB", strncmp(p, "/sdcard/routes/", 15) == 0 ? "routes" : "card",
             stat(p, &st) == 0 ? (long)(st.st_size + 1023) / 1024 : 0L);
    listRow(list, strrchr(p, '/') + 1, sub, onRouteGpx, (void*)(intptr_t)i);
  }
  if (routemap::s_gpx_n == 0) noteLabel(list, "No .gpx files. Put them in /routes on the card (Settings > Storage lends it to a computer over USB).");
}

void UITask::routeImport(int idx) {
  if (idx < 0 || idx >= routemap::s_gpx_n) return;
  showToast("Importing...", 1000);
  lv_refr_now(nullptr);
  int slot, kept;
  float dev;
  const char* path = routemap::s_gpx[idx];
  gpximport::Result r = gpximport::importFile(path, strrchr(path, '/') + 1, 0, slot, kept, dev);
  if (r != gpximport::OK) { showToast(gpximport::message(r), 3000); return; }
  char t[64];
  snprintf(t, sizeof(t), "Imported: %d points (within %d m)", kept, (int)lroundf(dev));
  showToast(t, 3500);
  routePopup(slot);
}

void UITask::routeAction(uint8_t act) {
  RouteGuide& g = _core->guide;
  switch (act) {
    case routemap::RT_LIST:
      routesPopup();
      return;
    case routemap::RT_STOP:
      g.stop();
      showToast("Route stopped");
      navClosePopup();
      break;
    case routemap::RT_FOLLOW:
    case routemap::RT_FOLLOW_REV: {
      if (!g.start(routemap::s_sel, act == routemap::RT_FOLLOW_REV)) { showToast("Can't read the route"); return; }
      if (_core->gpsAvailable() && !_core->gpsEnabled()) setGps(true);
      navmap::s_eta.reset();
      routemap::s_last_state = 0xFF;
      navClosePopup();
      if (_screen != SCR_MAP || !_map_nav) openMap(true);
      navFrameRoute();
      break;
    }
    case routemap::RT_DELETE:
      if (!tapConfirmed(routemap::s_del_lbl, "Delete?")) return;
      if (routemap::s_sel == g.slot()) g.stop();
      showToast(PlannedRoutes::remove(routemap::s_sel) ? "Route deleted" : "Delete failed");
      routesPopup();
      return;
  }
  if (_screen == SCR_MAP && _map_nav) { layoutMap(); }
  refreshNavBar();
}

// Frame the whole followed route.
void UITask::navFrameRoute() {
  const RouteGuide& g = _core->guide;
  if (g.slot() < 0) return;
  const RouteInfo& ri = g.info();
  const double x0 = navmap::normX(ri.min_lon), x1 = navmap::normX(ri.max_lon);
  const double y0 = navmap::normY(ri.max_lat), y1 = navmap::normY(ri.min_lat);
  _map_follow = false;
  int w = _map_area ? lv_obj_get_width(_map_area) : 320;
  int h = (_map_area ? lv_obj_get_height(_map_area) : 218) - mapBottomH();
  double dx = (x1 - x0) * mapview::TILE_PX, dy = (y1 - y0) * mapview::TILE_PX;
  int z = mapview::MAX_Z - 1;
  while (z > mapview::MIN_Z && (dx * (1 << z) > w - 110 || dy * (1 << z) > h - 60)) z--;
  _map_z = z;
  double n = (double)(1 << z);
  _map_cx = (x0 + x1) / 2 * n;
  _map_cy = (y0 + y1) / 2 * n + mapCenterBias();
  layoutMap();
}

// From loop(), once a second: the map and bar follow the guide's target.
void UITask::navPollRoute() {
  const RouteGuide& g = _core->guide;
  int32_t la = 0, lo = 0;
  g.target(la, lo);
  const uint8_t st = (uint8_t)g.state();
  if (la == routemap::s_last_tlat && lo == routemap::s_last_tlon && st == routemap::s_last_state) return;
  routemap::s_last_tlat = la; routemap::s_last_tlon = lo; routemap::s_last_state = st;
  if (_screen == SCR_MAP && _map_nav) { layoutMap(); refreshNavBar(); }
}

void UITask::refreshRouteTools() {
  if (!_nav_route_lbl) return;
  const RouteGuide& g = _core->guide;
  if (g.slot() < 0) { setText(_nav_route_lbl, "Not following a route."); return; }
  char l[28];
  g.label(l, sizeof(l), _prefs && _prefs->units_imperial);
  setTextFmt(_nav_route_lbl, "%s  -  %s", g.name(), l);
}
