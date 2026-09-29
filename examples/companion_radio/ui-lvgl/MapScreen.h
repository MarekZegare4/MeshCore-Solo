#pragma once
// Map screen: offline Web-Mercator tiles from the card (map/TileProvider.h,
// cached by map/TileCache.h) under own position. Two modes over the same tile
// view: the Navigation map (Home > Map, NavMap.h) shows waypoints, live
// shares, the trail and the active target; the Nodes map (Nearby > map
// button) marks every node Nearby knows a position for, tap one for its
// detail. Drag to pan, +/- to zoom, the crosshair re-centres and follows the
// GPS again. The download button fetches the visible area over WiFi
// (map/TileDownloader.h).
//
// Tiles decode one per loop pass (a PNG takes tens of ms), never while a drag
// is moving the map, so panning stays smooth and the radio keeps being
// serviced; an idle map decodes the tiles around the view ahead of time.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after the Nearby section.

#include <math.h>
#include "map/TileProvider.h"
#include "map/VectorPacks.h"
#include "map/VectorTileProvider.h"
#include "map/VectorLabels.h"
#include "map/TileCache.h"
#include "map/LiveCache.h"
#include "map/TileDownloader.h"
#include "map/AreaStore.h"

namespace mapview {

static RasterTileProvider s_raster("/sdcard/maps", LIVE_ROOT);
static VectorTileProvider s_vector(s_raster);   // spike: vector data where there is some, raster elsewhere
static TileProvider*      s_provider = &s_raster;   // Map tools > Vector map (test) swaps it
static TileCache&         s_cache = *new (psramBuf<TileCache>(1)) TileCache();   // decoded tiles, in PSRAM
static TileDownloader     s_dl("/sdcard/maps");
static AreaStore          s_areas("/sdcard/maps");
static const uint32_t     AVG_TILE_BYTES = 22 * 1024;   // OpenTopoMap-ish, for the size estimate

// Decode a tile into the cache; with live tiles on, a base tile shown without
// its trails overlay has the overlay fetched.
static TileCache::Slot* loadTile(int z, int x, int y) {
  TileCache::Slot* s = s_cache.load(*s_provider, z, x, y);
  if (s_trails_on && s_provider == &s_raster && s_raster.overlayMissed()) s_dl.liveRequest(z, x, y, true);
  return s;
}

static const int MIN_Z = 3, MAX_Z = 18;
static const int GRID_COLS = 3, GRID_ROWS = 2;   // covers 320x218 at any offset

// Fallback centre with no GPS fix and no saved position: Poland.
static const double DEFAULT_LAT = 52.0, DEFAULT_LON = 19.4;
static const int    DEFAULT_Z = 6;

static double lonToTileX(double lon, int z) { return (lon + 180.0) / 360.0 * (double)(1 << z); }
static double latToTileY(double lat, int z) {
  double r = lat * M_PI / 180.0;
  return (1.0 - asinh(tan(r)) / M_PI) / 2.0 * (double)(1 << z);
}

// ── Grid ──
// A square grid under the tiles, its step a round distance (the scale bar
// says which), so the map gives position and scale where no tile is loaded:
// the trail, marks and own position are drawn over it anyway. The step is
// measured at the view's centre (Web Mercator stretches away from it), and
// the lines sit at multiples of it in world pixels, so they move with a pan.
struct Grid {
  double left = 0, top = 0;   // world px of the view's top-left
  double step_px = 0;         // 0: none
};
static Grid s_grid;           // the main map's
static lv_obj_t* s_scale_bar = nullptr;
static lv_obj_t* s_scale_lbl = nullptr;

// The step for zoom `z` at latitude `lat`: at least `min_px` apart. Label out.
static double gridStep(int z, double lat, bool imperial, int min_px, char* lbl, size_t n) {
  double mpp = 156543.034 * cos(lat * M_PI / 180.0) / (double)(1 << z);   // metres per px
  static const double M[] = { 10, 20, 50, 100, 200, 500, 1e3, 2e3, 5e3, 1e4, 2e4, 5e4, 1e5, 2e5, 5e5, 1e6, 2e6 };
  static const double FT[] = { 50, 100, 200, 500, 1000, 2640, 5280, 10560, 26400, 52800, 105600, 264000, 528000,
                               1056000, 2640000, 5280000 };
  const double* v = imperial ? FT : M;
  int count = imperial ? sizeof(FT) / sizeof(FT[0]) : sizeof(M) / sizeof(M[0]);
  double unit = imperial ? 0.3048 : 1.0;   // metres per step unit
  int i = 0;
  while (i < count - 1 && v[i] * unit / mpp < min_px) i++;
  double u = v[i];
  if (!imperial) snprintf(lbl, n, u >= 1000 ? "%.0f km" : "%.0f m", u >= 1000 ? u / 1000 : u);
  else if (u < 2640) snprintf(lbl, n, "%.0f ft", u);
  else snprintf(lbl, n, u == 2640 ? "0.5 mi" : "%.0f mi", u / 5280);
  return u * unit / mpp;
}

// LV_EVENT_DRAW_MAIN_END of the map area (before its children, the tiles):
// user data is the Grid.
static void drawGrid(lv_event_t* e) {
  const Grid* g = (const Grid*)lv_event_get_user_data(e);
  if (!g || g->step_px < 8) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(0x3C3C46);
  d.width = 1;
  lv_layer_t* layer = lv_event_get_layer(e);
  int w = lv_area_get_width(&a), h = lv_area_get_height(&a);
  for (double x = ceil(g->left / g->step_px) * g->step_px - g->left; x < w; x += g->step_px) {
    d.p1.x = d.p2.x = a.x1 + (lv_value_precise_t)lround(x);
    d.p1.y = a.y1; d.p2.y = a.y2;
    lv_draw_line(layer, &d);
  }
  for (double y = ceil(g->top / g->step_px) * g->step_px - g->top; y < h; y += g->step_px) {
    d.p1.y = d.p2.y = a.y1 + (lv_value_precise_t)lround(y);
    d.p1.x = a.x1; d.p2.x = a.x2;
    lv_draw_line(layer, &d);
  }
}

// Markers, remembered so layout can reposition them. `idx` is the Nearby row
// (MK_NODE), the waypoint index (MK_WAYPOINT) or the live-share slot (MK_LIVE).
enum : uint8_t { MK_NODE, MK_WAYPOINT, MK_LIVE };
struct Mark { int32_t lat_e6, lon_e6; int idx; uint8_t kind; uint32_t col, text_hash; lv_obj_t* obj; lv_obj_t* dot; };
static const int MAX_MARKS = NearbyModel::MAX_NEARBY > WaypointStore::CAPACITY + LiveTrackStore::CAPACITY
                             ? NearbyModel::MAX_NEARBY : WaypointStore::CAPACITY + LiveTrackStore::CAPACITY;   // Nearby map, or waypoints + live shares
static Mark* s_marks = psramBuf<Mark>(MAX_MARKS);
static int  s_mark_count = 0;
static int  s_mark_prev = 0;   // during a rebuild: how many the last one left, reused where unchanged
static int  s_drag = 0;   // px moved in the current press: a drag isn't a tap
static bool s_available = false;   // provider has data; checked when the map opens, not per frame
static double s_left = 0, s_top = 0;   // world px (at the current zoom) of the view's top-left
static lv_obj_t* s_cells[GRID_COLS * GRID_ROWS];   // clip each grid tile (a magnified parent overflows)

// Overzoom: a tile the card doesn't have at this zoom is drawn as the matching
// part of the nearest coarser tile it does have, magnified -- so the map stays
// usable a little past the downloaded detail. Only two levels (x4): beyond
// that a raster tile is just big pixels, and the map says to download more.
// Returns that tile (k = levels up) or nullptr; want_z/x/y is the nearest
// coarser tile not looked up yet (decode it next), want_z = -1 if none.
static const int OVERZOOM = 2;
static TileCache::Slot* ancestorFor(int z, int x, int y, int& k, int& want_z, int& want_x, int& want_y) {
  want_z = -1;
  for (int d = 1; d <= OVERZOOM && z - d >= 0; d++) {
    int ax = x >> d, ay = y >> d;
    TileCache::Slot* s = s_cache.find(z - d, ax, ay);
    if (!s) { want_z = z - d; want_x = ax; want_y = ay; return nullptr; }
    if (s->present) { k = d; return s; }
  }
  return nullptr;
}

// Stand-in while z/x/y hasn't been decoded yet: a coarser tile already in the
// cache (typically the view you just zoomed in from), so a newly revealed part
// of the map shows something at once instead of a blank. Never decodes.
static TileCache::Slot* cachedAncestor(int z, int x, int y, int& k) {
  for (int d = 1; d <= 3 && z - d >= 0; d++) {
    TileCache::Slot* s = s_cache.find(z - d, x >> d, y >> d);
    if (s && s->present) { k = d; return s; }
  }
  return nullptr;
}

// Decoding a tile holds the loop for tens of ms, which makes a drag stutter.
// So nothing is decoded while the finger is moving the map; the view fills in
// once it rests, and when idle the ring of tiles around the view is decoded
// ahead of the next pan.
static uint32_t s_last_pan_ms = 0;
static const uint32_t PAN_SETTLE_MS = 150, PREFETCH_IDLE_MS = 400;

// Idle read-ahead: decode the nearest not-yet-looked-up tile in the ring just
// outside the view (one per call). False when the ring is complete.
static bool prefetchOne(double left, double top, int w, int h, int z) {
  int tx0 = (int)floor(left / TILE_PX), ty0 = (int)floor(top / TILE_PX);
  int n = 1 << z;
  int bx = 0, by = 0;
  double best = 1e18;
  for (int j = -1; j <= GRID_ROWS; j++) {
    for (int i = -1; i <= GRID_COLS; i++) {
      int tx = tx0 + i, ty = ty0 + j;
      double px = tx * (double)TILE_PX - left, py = ty * (double)TILE_PX - top;
      if (px < w && py < h && px + TILE_PX > 0 && py + TILE_PX > 0) continue;   // on screen: not ours
      if (ty < 0 || ty >= n) continue;
      int wx = ((tx % n) + n) % n;
      if (s_cache.find(z, wx, ty)) continue;   // also keeps a decoded neighbour from being evicted
      double d = fabs(px + TILE_PX / 2 - w / 2.0) + fabs(py + TILE_PX / 2 - h / 2.0);
      if (d < best) { best = d; bx = wx; by = ty; }
    }
  }
  if (best >= 1e18) return false;
  loadTile(z, bx, by);
  return true;
}

static const int MARK_D = 12;   // marker dot diameter

}  // namespace mapview

namespace navmap {   // shared with NavMap.h (included after this file)
static const int BAR_H = 44;   // target bar along the bottom of the Navigation map
// Navigation targets as one int (event user data): type << 8 | index.
enum : uint8_t { T_CLEAR, T_WAYPOINT, T_TRAILSTART, T_LIVE };
static int code(uint8_t type, int idx) { return (type << 8) | (idx & 0xFF); }
}  // namespace navmap

static void onMapPress(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) { mapview::s_drag = 0; return; }
  lv_point_t v;
  lv_indev_get_vect(lv_indev_active(), &v);
  if (v.x == 0 && v.y == 0) return;
  mapview::s_drag += abs(v.x) + abs(v.y);
  mapview::s_last_pan_ms = millis();
  s_ui->mapPan(v.x, v.y);
}
static void onMapLongPress(lv_event_t* e) {
  if (mapview::s_drag > 8) return;   // held after a pan: not a long-press on a spot
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a;
  lv_obj_get_coords((lv_obj_t*)lv_event_get_target(e), &a);
  s_ui->mapLongPress(p.x - a.x1, p.y - a.y1);
}
static void onMapZoomIn(lv_event_t* e)  { (void)e; s_ui->mapZoom(+1); }
static void onMapZoomOut(lv_event_t* e) { (void)e; s_ui->mapZoom(-1); }
static void onMapCenter(lv_event_t* e)  { (void)e; s_ui->mapCenterOnMe(); }
static void onMapDownload(lv_event_t* e) { (void)e; s_ui->mapDownloadPopup(); }
static void onDlClose(lv_event_t* e)     { (void)e; s_ui->mapDownloadClose(); }
static void onDlStart(lv_event_t* e)     { (void)e; s_ui->mapDownloadStart(); }
static void onDlZoomMinus(lv_event_t* e) { (void)e; s_ui->mapDownloadZmax(-1); }
static void onDlZoomPlus(lv_event_t* e)  { (void)e; s_ui->mapDownloadZmax(+1); }
static void onDlResume(lv_event_t* e)    { (void)e; s_ui->mapDownloadResume(); }
static void onDlDiscard(lv_event_t* e)   { (void)e; s_ui->mapDownloadDiscard(); }
static void onNavTools(lv_event_t* e);   // NavMap.h
static void onMapCredits(lv_event_t* e) { (void)e; s_ui->showToast(creditText(mapview::s_provider->attribution()), 4000); }
static void onMapMarker(lv_event_t* e) {
  if (mapview::s_drag > 8) return;   // the press was a pan that ended on a marker
  s_ui->mapOpenMarker((int)(uintptr_t)lv_event_get_user_data(e));
}

static lv_obj_t* mapButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 40, 40);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(b, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, text, THEME_FONT_TITLE, theme::TEXT));
  return b;
}

static lv_obj_t* mapPill(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_set_style_bg_color(l, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(l, 5, 0);
  lv_obj_set_style_pad_ver(l, 1, 0);
  lv_obj_set_style_radius(l, 4, 0);
  return l;
}

// Centring on a point puts it mid-way down the visible map: above the nav bar
// on the Navigation map (the bias is in tiles, added to the view centre).
double UITask::mapCenterBias() const {
  return _map_nav ? navmap::BAR_H / 2.0 / mapview::TILE_PX : 0.0;
}

void UITask::openMap(bool nav) {
  _map_nav = nav;
  showMap();
}

void UITask::showMap() {
  _screen = SCR_MAP;
  mapview::s_trails_on = lvport::trailsOn();
  mapview::s_provider = lvport::vectorOn() ? (mapview::TileProvider*)&mapview::s_vector : &mapview::s_raster;
  mapLiveBegin();
  mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
  if (_map_z == 0) {   // first open: own position, else the node's own advert position, else Poland
    int32_t lat, lon;
    double la = mapview::DEFAULT_LAT, lo = mapview::DEFAULT_LON;
    int z = mapview::DEFAULT_Z;
    if (_core->course.currentLocation(lat, lon)) { la = lat / 1e6; lo = lon / 1e6; z = 15; }
    else if (_sensors && (_sensors->node_lat != 0 || _sensors->node_lon != 0)) {   // saved / app-set position
      la = _sensors->node_lat; lo = _sensors->node_lon; z = 13;
    }
    _map_z = z;
    _map_cx = mapview::lonToTileX(lo, z);
    _map_cy = mapview::latToTileY(la, z);
  }
  buildMap();
  if (_map_nav && _prefs && _prefs->locator_has_target) navFrameTarget();   // show where we're going
}

void UITask::buildMap() {
  lv_obj_t* body = newScreen(NULL, false);
  lv_obj_set_layout(body, LV_LAYOUT_NONE);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(body, lv_color_hex(0x1A1A1E), 0);   // unloaded / missing tiles
  lv_obj_add_event_cb(body, mapview::drawGrid, LV_EVENT_DRAW_MAIN_END, &mapview::s_grid);
  lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(body, onMapPress, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(body, onMapPress, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(body, onMapLongPress, LV_EVENT_LONG_PRESSED, NULL);
  _map_area = body;

  for (int i = 0; i < mapview::GRID_COLS * mapview::GRID_ROWS; i++) {
    lv_obj_t* cell = lv_obj_create(body);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, mapview::TILE_PX, mapview::TILE_PX);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
    mapview::s_cells[i] = cell;
    _map_tiles[i] = lv_image_create(cell);
    lv_image_set_pivot(_map_tiles[i], 0, 0);   // magnify from the top-left (overzoom)
  }

  mapview::labels::buildLayer(body);   // vector map names, over the tiles, under the markers

  // Marker layer: same size as the map, lets presses through to it.
  _map_marks = lv_obj_create(body);
  lv_obj_remove_style_all(_map_marks);
  lv_obj_set_size(_map_marks, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(_map_marks, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(_map_marks, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(_map_marks, LV_OBJ_FLAG_EVENT_BUBBLE);

  if (_map_nav) buildNavLayers();   // trail + line to the target, under the markers

  _map_me = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(_map_me);
  lv_obj_set_size(_map_me, 16, 16);
  lv_obj_set_style_radius(_map_me, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(_map_me, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_bg_opa(_map_me, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_map_me, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_width(_map_me, 3, 0);
  lv_obj_remove_flag(_map_me, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(_map_me, LV_OBJ_FLAG_HIDDEN);

  areaBuildLayer(body);   // download frame / area outlines, over the tiles (MapAreas.h)

  // Controls
  lv_obj_t* back = mapButton(body, LV_SYMBOL_LEFT, onBack);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_align(mapButton(body, LV_SYMBOL_PLUS, onMapZoomIn), LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_align(mapButton(body, LV_SYMBOL_MINUS, onMapZoomOut), LV_ALIGN_TOP_RIGHT, -6, 52);
  // Nav map: trail / live share / download live in its tools panel, in the
  // left column (back, pin, tools) -- the right one has no room above the bar.
  if (_map_nav) lv_obj_align(mapButton(body, LV_SYMBOL_BARS, onNavTools), LV_ALIGN_TOP_LEFT, 6, 98);
  else lv_obj_align(mapButton(body, LV_SYMBOL_DOWNLOAD, onMapDownload), LV_ALIGN_TOP_RIGHT, -6, 98);
  int bottom = _map_nav ? navmap::BAR_H : 0;   // the nav bar takes the bottom edge
  lv_obj_align(mapButton(body, LV_SYMBOL_GPS, onMapCenter), LV_ALIGN_BOTTOM_RIGHT, -6, -6 - bottom);
  _map_dl_pill = mapPill(body, "");   // tap: the download popup
  lv_obj_set_style_text_color(_map_dl_pill, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_pad_ver(_map_dl_pill, 3, 0);
  lv_obj_align(_map_dl_pill, LV_ALIGN_TOP_RIGHT, -52, 14);   // beside +, clear of the zoom pill on the left
  lv_obj_add_flag(_map_dl_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(_map_dl_pill, 6);
  lv_obj_add_event_cb(_map_dl_pill, onMapDownload, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
  _map_zoom_lbl = mapPill(body, "");
  lv_obj_align(_map_zoom_lbl, LV_ALIGN_TOP_LEFT, 52, 16);
  // Map data credit (the licences require it on the map): a small "©" that
  // shows the full line when tapped; it is also in Settings > About.
  lv_obj_t* attr = mapPill(body, "\xC2\xA9");
  lv_obj_set_style_pad_hor(attr, 7, 0);
  lv_obj_set_style_pad_ver(attr, 3, 0);
  lv_obj_add_flag(attr, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(attr, 8);
  lv_obj_add_event_cb(attr, onMapCredits, LV_EVENT_CLICKED, NULL);
  lv_obj_align(attr, LV_ALIGN_BOTTOM_LEFT, 6, -6 - bottom);
  // Scale: a bar one grid step long, the distance beside it.
  lv_obj_t* sc = lv_obj_create(body);
  lv_obj_remove_style_all(sc);
  lv_obj_remove_flag(sc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(sc, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(sc, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_70, 0);
  lv_obj_set_style_radius(sc, 4, 0);
  lv_obj_set_style_pad_hor(sc, 5, 0);
  lv_obj_set_style_pad_ver(sc, 3, 0);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(sc, 5, 0);
  mapview::s_scale_bar = lv_obj_create(sc);
  lv_obj_remove_style_all(mapview::s_scale_bar);
  lv_obj_set_size(mapview::s_scale_bar, 60, 6);
  lv_obj_set_style_border_color(mapview::s_scale_bar, lv_color_hex(theme::TEXT), 0);
  lv_obj_set_style_border_width(mapview::s_scale_bar, 2, 0);
  lv_obj_set_style_border_side(mapview::s_scale_bar, (lv_border_side_t)(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
  mapview::s_scale_lbl = label(sc, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_align(sc, LV_ALIGN_BOTTOM_LEFT, 36, -6 - bottom);
  _map_hint = mapPill(body, "");
  lv_label_set_long_mode(_map_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_map_hint, 200);
  lv_obj_set_style_text_align(_map_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(_map_hint, LV_ALIGN_TOP_MID, 0, 50);   // clear of own position, mid-map
  lv_obj_add_flag(_map_hint, LV_OBJ_FLAG_HIDDEN);
  if (_map_nav) buildNavControls(body);

  lv_obj_update_layout(body);
  mapview::s_mark_count = 0;   // the previous markers went with the previous screen
  rebuildMapMarkers();
  layoutMap();
}

// Places tiles and markers for the current centre / zoom; queues tiles that
// aren't decoded yet for mapLoop().
void UITask::layoutMap() {
  if (!_map_area) return;
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area);
  double left = _map_cx * mapview::TILE_PX - w / 2.0;   // world px of the view's top-left
  double top  = _map_cy * mapview::TILE_PX - h / 2.0;
  mapview::s_left = left; mapview::s_top = top;
  {   // the grid under the tiles, the scale bar
    char sl[16];
    double lat = atan(sinh(M_PI * (1 - 2 * _map_cy / (double)(1 << _map_z)))) * 180.0 / M_PI;
    mapview::s_grid = { left, top, mapview::gridStep(_map_z, lat, _prefs && _prefs->units_imperial, 56, sl, sizeof(sl)) };
    if (mapview::s_scale_lbl) {
      setText(mapview::s_scale_lbl, sl);
      lv_obj_set_width(mapview::s_scale_bar, (int)lround(mapview::s_grid.step_px));
    }
    lv_obj_invalidate(_map_area);
  }
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << _map_z;
  bool have_provider = mapview::s_available;
  int shown = 0, missing = 0, over = 0;
  _map_pending = false;

  for (int j = 0; j < mapview::GRID_ROWS; j++) {
    for (int i = 0; i < mapview::GRID_COLS; i++) {
      lv_obj_t* cell = mapview::s_cells[j * mapview::GRID_COLS + i];
      lv_obj_t* img = _map_tiles[j * mapview::GRID_COLS + i];
      int tx = tx0 + i, ty = ty0 + j, wx = ((tx % n) + n) % n;
      int px = (int)lround(tx * (double)mapview::TILE_PX - left);
      int py = (int)lround(ty * (double)mapview::TILE_PX - top);
      bool on_screen = px < w && py < h && px + mapview::TILE_PX > 0 && py + mapview::TILE_PX > 0;
      mapview::TileCache::Slot* s = nullptr;
      if (have_provider && on_screen && ty >= 0 && ty < n) {
        s = mapview::s_cache.find(_map_z, wx, ty);
        if (!s) _map_pending = true;
      }
      mapview::TileCache::Slot* show = (s && s->present) ? s : nullptr;
      int k = 0;
      bool stand_in = false;
      if (s && !s->present) {   // none at this zoom: magnify a coarser one
        missing++;
        mapview::s_dl.liveRequest(_map_z, wx, ty);   // online: fetch it (no-op when live tiles are off)
        int wz, ax, ay;
        show = mapview::ancestorFor(_map_z, wx, ty, k, wz, ax, ay);
        if (!show && wz >= 0) _map_pending = true;
      } else if (!s && have_provider && on_screen && ty >= 0 && ty < n) {   // not decoded yet
        show = mapview::cachedAncestor(_map_z, wx, ty, k);
        stand_in = show != nullptr;
      }
      if (show) {
        if (lv_image_get_src(img) != &show->dsc) lv_image_set_src(img, &show->dsc);
        lv_image_set_scale(img, LV_SCALE_NONE << k);
        int m = (1 << k) - 1;   // which part of the magnified tile this cell is
        lv_obj_set_pos(img, -(wx & m) * mapview::TILE_PX, -(ty & m) * mapview::TILE_PX);
        lv_obj_set_pos(cell, px, py);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_HIDDEN);
        shown++;
        if (k > over && !stand_in) over = k;
      } else {
        lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  mapview::labels::layout(left, top, w, h, _map_z, have_provider && mapview::s_provider == &mapview::s_vector && mapview::s_vector.hasData(),
                          _map_nav ? navmap::BAR_H : 0);

  // Own position
  int32_t lat, lon;
  if (_core->course.currentLocation(lat, lon)) {
    double x = mapview::lonToTileX(lon / 1e6, _map_z) * mapview::TILE_PX - left;
    double y = mapview::latToTileY(lat / 1e6, _map_z) * mapview::TILE_PX - top;
    lv_obj_set_pos(_map_me, (int)x - 8, (int)y - 8);
    lv_obj_remove_flag(_map_me, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(_map_me, LV_OBJ_FLAG_HIDDEN);
  }
  const int half = mapview::MARK_D / 2;
  for (int k = 0; k < mapview::s_mark_count; k++) {
    const mapview::Mark& m = mapview::s_marks[k];
    int dy = lv_obj_get_y(m.dot);   // the dot within the marker (vertically centred on the name)
    double x = mapview::lonToTileX(m.lon_e6 / 1e6, _map_z) * mapview::TILE_PX - left;
    double y = mapview::latToTileY(m.lat_e6 / 1e6, _map_z) * mapview::TILE_PX - top;
    lv_obj_set_pos(m.obj, (int)lround(x) - half, (int)lround(y) - half - dy);   // the dot's centre on the spot
  }
  if (_map_nav) layoutNav();

  if (over) setTextFmt(_map_zoom_lbl, "z%d  (map z%d)", _map_z, _map_z - over);   // magnified
  else if (mapview::s_provider == &mapview::s_vector)   // spike: how long a tile takes
  {
    uint32_t ld, fl, ln;
    mapview::s_vector.lastSplit(ld, fl, ln);
    setTextFmt(_map_zoom_lbl, "z%d  vt %lu ms (r%lu a%lu l%lu)", _map_z, (unsigned long)mapview::s_vector.lastMs(),
                          (unsigned long)ld, (unsigned long)fl, (unsigned long)ln);
  }
  else setTextFmt(_map_zoom_lbl, "z%d", _map_z);
  const char* hint = !have_provider ? "No map on the SD card.\nPut tiles in /maps (tools/maps)."
                   : mapview::s_dl.liveQueued() > 0 ? nullptr   // being fetched
                   : (!_map_pending && shown == 0 && missing > 0)
                       ? "No map here at this zoom -\nzoom out or download the area" : nullptr;
  if (hint && !areaSelecting()) { setText(_map_hint, hint); lv_obj_remove_flag(_map_hint, LV_OBJ_FLAG_HIDDEN); }
  else lv_obj_add_flag(_map_hint, LV_OBJ_FLAG_HIDDEN);
  areaLayout();
}

// One tile decode per call, nearest to the centre first.
void UITask::mapLoop() {
  if ((int32_t)(millis() - _next_map_marks_ms) >= 0) {
    _next_map_marks_ms = millis() + 3000;
    if (_map_follow) {
      int32_t lat, lon;
      if (_core->course.currentLocation(lat, lon)) {
        _map_cx = mapview::lonToTileX(lon / 1e6, _map_z);
        _map_cy = mapview::latToTileY(lat / 1e6, _map_z) + mapCenterBias();
      }
    }
    rebuildMapMarkers();
    layoutMap();
  }
  if (_map_nav) navPollAveraging();
  if (_map_nav && (int32_t)(millis() - _next_nav_bar_ms) >= 0) {
    _next_nav_bar_ms = millis() + 1000;
    refreshNavBar();
  }
  if (!_map_area) return;
  uint32_t since_pan = millis() - mapview::s_last_pan_ms;
  if (since_pan < mapview::PAN_SETTLE_MS) return;   // mid-drag: keep it smooth, decode after
  if (mapview::labels::loadOne()) { layoutMap(); return; }   // names of the view first: a small file
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area);
  double left = _map_cx * mapview::TILE_PX - w / 2.0, top = _map_cy * mapview::TILE_PX - h / 2.0;
  if (!_map_pending) {   // view complete: when idle, decode the next tile a pan would reveal
    if (since_pan >= mapview::PREFETCH_IDLE_MS && mapview::s_available)
      mapview::prefetchOne(left, top, w, h, _map_z);
    return;
  }
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << _map_z;
  int best_x = 0, best_y = 0;
  double best_d = 1e18;
  for (int j = 0; j < mapview::GRID_ROWS; j++) {
    for (int i = 0; i < mapview::GRID_COLS; i++) {
      int tx = tx0 + i, ty = ty0 + j;
      double px = tx * (double)mapview::TILE_PX - left, py = ty * (double)mapview::TILE_PX - top;
      if (px >= w || py >= h || px + mapview::TILE_PX <= 0 || py + mapview::TILE_PX <= 0) continue;
      if (ty < 0 || ty >= n) continue;
      int wx = ((tx % n) + n) % n;
      if (mapview::s_cache.find(_map_z, wx, ty)) continue;
      double d = fabs(px + 128 - w / 2.0) + fabs(py + 128 - h / 2.0);
      if (d < best_d) { best_d = d; best_x = wx; best_y = ty; }
    }
  }
  if (best_d < 1e18) {
    mapview::loadTile(_map_z, best_x, best_y);
  } else {   // every tile at this zoom looked up: decode a coarser one for a missing tile
    best_d = 1e18;
    int bz = -1;
    for (int j = 0; j < mapview::GRID_ROWS; j++) {
      for (int i = 0; i < mapview::GRID_COLS; i++) {
        int tx = tx0 + i, ty = ty0 + j;
        double px = tx * (double)mapview::TILE_PX - left, py = ty * (double)mapview::TILE_PX - top;
        if (px >= w || py >= h || px + mapview::TILE_PX <= 0 || py + mapview::TILE_PX <= 0) continue;
        if (ty < 0 || ty >= n) continue;
        int wx = ((tx % n) + n) % n;
        mapview::TileCache::Slot* s = mapview::s_cache.find(_map_z, wx, ty);
        if (!s || s->present) continue;
        int k, wz, ax, ay;
        if (mapview::ancestorFor(_map_z, wx, ty, k, wz, ax, ay) || wz < 0) continue;
        double d = fabs(px + 128 - w / 2.0) + fabs(py + 128 - h / 2.0);
        if (d < best_d) { best_d = d; bz = wz; best_x = ax; best_y = ay; }
      }
    }
    if (bz >= 0) mapview::loadTile(bz, best_x, best_y);
  }
  layoutMap();
}

void UITask::rebuildMapMarkers() {
  if (!_map_marks) return;
  mapview::s_mark_prev = mapview::s_mark_count;
  mapview::s_mark_count = 0;
  if (_map_nav) rebuildNavMarkers();
  else rebuildNodeMarkers();
  for (int k = mapview::s_mark_count; k < mapview::s_mark_prev; k++) lv_obj_delete(mapview::s_marks[k].obj);
  mapview::s_mark_prev = 0;
  lv_obj_update_layout(_map_marks);   // so layoutMap() knows where each dot sits in its marker
}

void UITask::rebuildNodeMarkers() {
  _nearby->refreshStored();
  for (int i = 0; i < _nearby->count() && mapview::s_mark_count < mapview::MAX_MARKS; i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    if (e.lat_e6 == 0 && e.lon_e6 == 0) continue;
    uint32_t col = e.is_live ? theme::OK : e.type == ADV_TYPE_CHAT ? theme::TEXT : theme::TEXT_MUTED;
    addMapMark(mapview::MK_NODE, i, e.lat_e6, e.lon_e6, col, e.name[0] ? e.name : "?");
  }
}

// One clickable object per marker: a dot plus the name beside it. The object
// is placed so the dot's centre is on the spot, from where layout actually put
// the dot inside it (Mark::dot) -- the name may be taller than the dot. The
// markers are rebuilt every few seconds: one the same as the last time in its
// slot is kept as it is, so an unchanged map isn't redrawn under it.
void UITask::addMapMark(uint8_t kind, int idx, int32_t lat_e6, int32_t lon_e6, uint32_t col, const char* text) {
  if (!_map_marks || mapview::s_mark_count >= mapview::MAX_MARKS) return;
  uint32_t h = 2166136261u;   // FNV-1a of the name
  for (const char* c = text; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  int k = mapview::s_mark_count;
  if (k < mapview::s_mark_prev) {
    mapview::Mark& old = mapview::s_marks[k];
    if (old.kind == kind && old.idx == idx && old.lat_e6 == lat_e6 && old.lon_e6 == lon_e6 && old.col == col && old.text_hash == h) {
      mapview::s_mark_count++;
      return;
    }
    lv_obj_delete(old.obj);
  }
  lv_obj_t* m = lv_obj_create(_map_marks);
  lv_obj_remove_style_all(m);
  lv_obj_set_size(m, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(m, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(m, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(m, 3, 0);
  lv_obj_add_flag(m, LV_OBJ_FLAG_EVENT_BUBBLE);   // a drag starting here still pans
  lv_obj_add_event_cb(m, onMapMarker, LV_EVENT_CLICKED, (void*)(uintptr_t)mapview::s_mark_count);
  lv_obj_t* dot = lv_obj_create(m);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, mapview::MARK_D, mapview::MARK_D);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(dot, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_width(dot, 2, 0);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* name = mapPill(m, text);
  lv_obj_remove_flag(name, LV_OBJ_FLAG_CLICKABLE);

  mapview::Mark& mk = mapview::s_marks[mapview::s_mark_count++];
  mk.lat_e6 = lat_e6; mk.lon_e6 = lon_e6; mk.idx = idx; mk.kind = kind; mk.col = col; mk.text_hash = h; mk.obj = m; mk.dot = dot;
}

void UITask::mapPan(int dx, int dy) {
  _map_cx -= dx / (double)mapview::TILE_PX;
  _map_cy -= dy / (double)mapview::TILE_PX;
  double n = (double)(1 << _map_z);
  if (_map_cy < 0) _map_cy = 0;
  if (_map_cy > n) _map_cy = n;
  _map_follow = false;
  layoutMap();
}

void UITask::mapZoom(int delta) {
  int z = _map_z + delta;
  if (z < mapview::MIN_Z || z > mapview::MAX_Z) return;
  double f = delta > 0 ? 2.0 : 0.5;
  _map_cx *= f; _map_cy *= f;
  _map_z = z;
  layoutMap();
}

void UITask::mapCenterOnMe() {
  int32_t lat, lon;
  if (!ensureGps() || !_core->course.currentLocation(lat, lon)) { _map_follow = true; return; }   // centres once a fix comes
  _map_follow = true;
  _map_cx = mapview::lonToTileX(lon / 1e6, _map_z);
  _map_cy = mapview::latToTileY(lat / 1e6, _map_z) + mapCenterBias();
  layoutMap();
}

void UITask::mapOpenMarker(int idx) {
  if (idx < 0 || idx >= mapview::s_mark_count) return;
  const mapview::Mark& m = mapview::s_marks[idx];
  if (m.kind == mapview::MK_WAYPOINT) { navWaypointMenu(m.idx); return; }
  if (m.kind == mapview::MK_LIVE) { navPick(navmap::code(navmap::T_LIVE, m.idx)); return; }
  openNode(m.idx);
  _node_from_map = true;
}

// Screen point (relative to the map area) -> lat/lon at the current view.
void UITask::mapPointToLatLon(int x, int y, int32_t& lat_e6, int32_t& lon_e6) const {
  double n = (double)(1 << _map_z);
  double tx = (mapview::s_left + x) / mapview::TILE_PX, ty = (mapview::s_top + y) / mapview::TILE_PX;
  double lon = tx / n * 360.0 - 180.0;
  double lat = atan(sinh(M_PI * (1.0 - 2.0 * ty / n))) * 180.0 / M_PI;
  lat_e6 = (int32_t)lround(lat * 1e6);
  lon_e6 = (int32_t)lround(lon * 1e6);
}

void UITask::mapLongPress(int x, int y) {
  if (_map_nav) navDropAt(x, y);
}


// ── Area download ─────────────────────────────────────────────────────────────

// The visible view as a lon/lat box.
static mapview::TileArea visibleArea(double cx, double cy, int z, int w, int h, int zmin, int zmax) {
  double n = (double)(1 << z);
  auto lon = [n](double tx) { return tx / n * 360.0 - 180.0; };
  auto lat = [n](double ty) { return atan(sinh(M_PI * (1.0 - 2.0 * ty / n))) * 180.0 / M_PI; };
  double hw = w / 2.0 / mapview::TILE_PX, hh = h / 2.0 / mapview::TILE_PX;
  mapview::TileArea a;
  a.lon0 = lon(cx - hw); a.lon1 = lon(cx + hw);
  a.lat1 = lat(cy - hh); a.lat0 = lat(cy + hh);
  a.zmin = zmin; a.zmax = zmax;
  return a;
}

static int dlZmin(int z) { return z - 4 < 5 ? (z < 5 ? z : 5) : z - 4; }   // a few overview levels (cheap)

// The download button: progress / an unfinished job in a popup, else the
// frame to pick a new area (MapAreas.h).
void UITask::mapDownloadPopup() {
  if (_dl_overlay) return;
  mapview::TileArea job;
  if (!mapview::s_dl.active() && !mapview::s_dl.savedJob(job)) { areaSelectBegin(); return; }
  int src_max = mapview::s_dl.sourceMaxZ();
  if (_dl_zmax < _map_z) _dl_zmax = _map_z + 3;
  if (_dl_zmax > src_max) _dl_zmax = src_max;   // the server has nothing finer

  lv_obj_t* panel = popupOpen(screen(), POP_FIT, _dl_overlay);

  lv_obj_t* hdr = lv_obj_create(panel);
  styleSurface(hdr, theme::BG);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 26);
  lv_obj_align(label(hdr, "Download this area", THEME_FONT_TITLE, theme::TEXT), LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(hdr, LV_SYMBOL_CLOSE, onDlClose, 0, NULL);

  // Detail range: from a few overview levels up to the chosen max zoom.
  lv_obj_t* zr = lv_obj_create(panel);
  styleSurface(zr, theme::BG);
  lv_obj_remove_flag(zr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(zr, LV_PCT(100), 30);
  lv_obj_align(label(zr, "Up to zoom", THEME_FONT_BODY, theme::TEXT_MUTED), LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(zr, LV_SYMBOL_PLUS, onDlZoomPlus, 0, NULL);
  _dl_zoom_lbl = label(zr, "", THEME_FONT_TITLE, theme::ACCENT);
  lv_obj_align(_dl_zoom_lbl, LV_ALIGN_RIGHT_MID, -56, 0);
  headerButton(zr, LV_SYMBOL_MINUS, onDlZoomMinus, 96, NULL);

  // Unfinished job (power-off, lost WiFi, Stop): resume or drop it.
  _dl_job_row = lv_obj_create(panel);
  styleSurface(_dl_job_row, theme::SURFACE);
  lv_obj_remove_flag(_dl_job_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(_dl_job_row, LV_PCT(100), 40);
  lv_obj_set_style_radius(_dl_job_row, theme::RADIUS, 0);
  _dl_job_lbl = label(_dl_job_row, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_set_width(_dl_job_lbl, 170);
  lv_label_set_long_mode(_dl_job_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_align(_dl_job_lbl, LV_ALIGN_LEFT_MID, theme::PAD, 0);
  stylePrimary(headerButton(_dl_job_row, LV_SYMBOL_PLAY " Resume", onDlResume, 48, NULL));
  headerButton(_dl_job_row, LV_SYMBOL_TRASH, onDlDiscard, 4, NULL);
  lv_obj_add_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);

  _dl_info = noteLabel(panel, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_set_style_text_line_space(_dl_info, 2, 0);
  _dl_bar = lv_bar_create(panel);
  lv_obj_set_size(_dl_bar, LV_PCT(100), 6);
  lv_obj_set_style_bg_color(_dl_bar, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_bg_color(_dl_bar, lv_color_hex(theme::ACCENT), LV_PART_INDICATOR);
  lv_obj_add_flag(_dl_bar, LV_OBJ_FLAG_HIDDEN);
  _dl_err = label(panel, "", THEME_FONT_SMALL, theme::FAIL);
  lv_label_set_long_mode(_dl_err, LV_LABEL_LONG_DOT);   // one line, whatever the server said
  lv_obj_set_width(_dl_err, LV_PCT(100));
  lv_obj_add_flag(_dl_err, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* go = barButton(buttonBar(panel), "", onDlStart, 0, true);
  _dl_start_lbl = lv_obj_get_child(go, 0);
  stylePrimary(go);

  refreshDownloadPopup();
}

void UITask::mapDownloadClose() {
  if (_dl_overlay) lv_obj_delete_async(_dl_overlay);   // may be closing from its own button
  _dl_overlay = _dl_info = _dl_bar = _dl_err = _dl_zoom_lbl = _dl_start_lbl = _dl_job_row = _dl_job_lbl = nullptr;
}

void UITask::mapDownloadZmax(int delta) {
  if (mapview::s_dl.active()) return;
  int z = _dl_zmax + delta;
  if (z < _map_z || z > mapview::MAX_Z || z > mapview::s_dl.sourceMaxZ()) return;
  _dl_zmax = z;
  refreshDownloadPopup();
}

// "a.tile.opentopomap.org" -> "opentopomap.org": the name people know.
static const char* shortHost(const char* h) {
  const char* p = h;
  for (;;) {
    const char* dot = strchr(p, '.');
    if (!dot || !strchr(dot + 1, '.')) return p;   // two labels left
    p = dot + 1;
  }
}

static void dlSize(char* out, size_t n, uint64_t bytes) {
  if (bytes < 1024 * 1024) snprintf(out, n, "%lu KB", (unsigned long)(bytes / 1024));
  else snprintf(out, n, "%lu MB", (unsigned long)((bytes + 512 * 1024) / (1024 * 1024)));
}

void UITask::refreshDownloadPopup() {
  if (!_dl_info) return;
  mapview::TileDownloader& dl = mapview::s_dl;
  lv_label_set_text_fmt(_dl_zoom_lbl, "z%d", _dl_zmax);
  const char* host = shortHost(dl.sourceHost());
  lv_obj_t* zoom_row = lv_obj_get_parent(_dl_zoom_lbl);
  if (dl.active()) {   // the range is fixed while it runs: room for the progress instead
    lv_obj_add_flag(zoom_row, LV_OBJ_FLAG_HIDDEN);
    if (_dl_job_row) lv_obj_add_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_dl_bar, LV_OBJ_FLAG_HIDDEN);
    uint32_t total = dl.total();
    lv_bar_set_range(_dl_bar, 0, total > 0 ? (int32_t)total : 1);
    lv_bar_set_value(_dl_bar, (int32_t)dl.processed(), LV_ANIM_OFF);
    if (dl.state() == mapview::TileDownloader::CONNECTING)
      lv_label_set_text_fmt(_dl_info, "Connecting to WiFi...  -  from %s", host);
    else
      lv_label_set_text_fmt(_dl_info, "%lu / %lu tiles, %lu new  -  from %s\nYou can close this, it carries on",
                            (unsigned long)dl.processed(), (unsigned long)total,
                            (unsigned long)dl.downloaded(), host);
    if (dl.failed()) {
      lv_label_set_text_fmt(_dl_err, "%lu failed: %s", (unsigned long)dl.failed(), dl.message());
      lv_obj_remove_flag(_dl_err, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_dl_err, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(_dl_start_lbl, LV_SYMBOL_STOP " Stop");
    return;
  }
  lv_obj_remove_flag(zoom_row, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(_dl_bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(_dl_err, LV_OBJ_FLAG_HIDDEN);
  mapview::TileArea job;
  if (_dl_job_row && dl.savedJob(job)) {
    lv_label_set_text_fmt(_dl_job_lbl, "Unfinished: z%d-%d,\n%lu tiles", job.zmin, job.zmax,
                          (unsigned long)mapview::countTiles(job));
    lv_obj_remove_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);
  } else if (_dl_job_row) {
    lv_obj_add_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);
  }
  int w = _map_area ? lv_obj_get_width(_map_area) : 320, h = _map_area ? lv_obj_get_height(_map_area) : 218;
  mapview::TileArea a = visibleArea(_map_cx, _map_cy, _map_z, w, h, dlZmin(_map_z), _dl_zmax);
  uint32_t n = mapview::countTiles(a);
  char size[16];
  dlSize(size, sizeof(size), (uint64_t)n * mapview::AVG_TILE_BYTES);
  // The network itself is set up in Settings > WiFi only; here just which one.
  char ssid[33], pass[65], net[64];
  if (!lvport::wifiAllowed()) snprintf(net, sizeof(net), "WiFi is off - Settings > WiFi");
  else if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) snprintf(net, sizeof(net), "No WiFi network - Settings > WiFi");
  else snprintf(net, sizeof(net), "Over WiFi: %s", ssid);
  char last[64] = "";
  bool job_shown = _dl_job_row && !lv_obj_has_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);   // it says enough
  if (!job_shown && (dl.state() == mapview::TileDownloader::DONE || dl.state() == mapview::TileDownloader::FAILED ||
                     dl.state() == mapview::TileDownloader::CANCELLED))
    snprintf(last, sizeof(last), "\nLast: %s (%lu new)", dl.message(), (unsigned long)dl.downloaded());
  lv_label_set_text_fmt(_dl_info, "z%d-%d: %lu tiles, about %s  -  %s\n%s%s",
                        a.zmin, a.zmax, (unsigned long)n, size, host, net, last);
  (void)n;
  lv_label_set_text(_dl_start_lbl, LV_SYMBOL_PLUS " New area");
}

void UITask::mapDownloadStart() {
  if (mapview::s_dl.active()) { mapDownloadStop(); return; }
  areaSelectBegin();   // a new area: picked with the frame
}

void UITask::mapDownloadResume() {
  mapview::TileArea a;
  if (mapview::s_dl.active() || !mapview::s_dl.savedJob(a)) return;
  char ssid[33], pass[65];
  if (!lvport::wifiAllowed()) { showToast("WiFi is off - Settings > WiFi"); return; }
  if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) { showToast("Pick a WiFi network first - Settings > WiFi", 3000); return; }
  if (!lvport::mountStorage()) { showToast("No SD card"); return; }
  if (!mapview::s_dl.start(a, ssid, pass)) { showToast(mapview::s_dl.message()); return; }
  refreshDownloadPopup();
}

void UITask::mapDownloadDiscard() {
  mapview::s_dl.discardJob();
  refreshDownloadPopup();
}

void UITask::mapDownloadStop() {
  mapview::s_dl.cancel();
  refreshDownloadPopup();
}

static mapview::TileArea s_job_tmp;

// Every UI loop pass, whatever the screen: a download keeps going in the background.
// Live tiles on while the map is open: WiFi allowed, a network saved, a card in.
void UITask::mapLiveBegin() {
  _map_left_ms = 0;
  char ssid[33], pass[65];
  if (!lvport::liveTiles() || !lvport::wifiAllowed() || !lvport::mountStorage()) { mapview::s_dl.liveEnd(); return; }
  if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) return;
  mapview::s_dl.liveBegin(ssid, pass);
}

// Map tools > Hiking trails: the overlay on / off, every decoded tile redrawn.
void UITask::setTrails(bool on) {
  lvport::setTrailsOn(on);
  mapview::s_trails_on = on;
  mapview::s_cache.invalidate();
  if (_screen == SCR_MAP) layoutMap();
  if (!on) return;   // turned on: where the trails come from
  if (lvport::liveTiles() && lvport::wifiAllowed()) showToast("Trails load with the map over WiFi", 3000);
  else showToast("Download an area again to add its trails", 3000);
}

// Map tools > Vector map (test): /sdcard/vmap drawn on the device where it has data.
void UITask::setVectorMap(bool on) {
  lvport::setVectorOn(on);
  mapview::s_provider = on ? (mapview::TileProvider*)&mapview::s_vector : &mapview::s_raster;
  mapview::s_cache.invalidate();
  mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
  if (_screen == SCR_MAP) layoutMap();
  if (on) showToast("Drawn where /vmap has data");
}

void UITask::setLiveTiles(bool on) {
  lvport::setLiveTiles(on);
  if (on) {
    char ssid[33], pass[65];
    if (!lvport::wifiAllowed()) showToast("WiFi is off - Settings > WiFi");
    else if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) showToast("Pick a WiFi network first - Settings > WiFi", 3000);
    mapLiveBegin();
    mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
  } else {
    mapview::s_dl.liveEnd();
  }
  if (_screen == SCR_MAP) layoutMap();
}

void UITask::mapDownloadTick() {
  mapview::TileDownloader& dl = mapview::s_dl;
  dl.loop();
  mapview::s_live_cache.service(6);   // trims live tiles past the Storage limit
  if (mapview::s_areas.service(8)) {   // a deleted area's files are gone
    char t[48];
    snprintf(t, sizeof(t), "Area deleted (%lu files)", (unsigned long)mapview::s_areas.deleted());
    showToast(t);
    mapview::s_cache.invalidate();
    mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
    if (_screen == SCR_MAP) layoutMap();
  }
  if (dl.liveOn()) {
    int z, x, y;
    bool got = false;
    while (dl.liveTake(z, x, y)) { mapview::s_cache.drop(z, x, y); got = true; }   // a new tile, or its trails
    if (got && _screen == SCR_MAP) layoutMap();
    // The WiFi goes 30 s after the map was left or the screen went off (a
    // quick look elsewhere keeps it).
    if (_screen != SCR_MAP || _asleep) {
      if (!_map_left_ms) _map_left_ms = millis() | 1;
      else if (millis() - _map_left_ms > 30000) dl.liveEnd();
    }
  }
  uint8_t st = dl.state();
  bool was_active = _dl_last_state == mapview::TileDownloader::CONNECTING ||
                    _dl_last_state == mapview::TileDownloader::RUNNING;
  if (was_active && !dl.active()) {   // just finished
    if (st == mapview::TileDownloader::DONE)
      mapview::s_areas.setFlag(mapview::s_areas.find(dl.area()), mapview::AreaStore::F_COMPLETE, true);
    char t[64];
    snprintf(t, sizeof(t), "Map: %s, %lu new tiles", dl.message(), (unsigned long)dl.downloaded());
    showToast(t, 4000);
    mapview::s_cache.forgetMissing();   // tiles that were missing may be there now
    mapview::s_available = mapview::s_provider->available();
    if (_screen == SCR_MAP) layoutMap();
  }
  _dl_last_state = st;

  static uint32_t next_ui = 0;
  if ((int32_t)(millis() - next_ui) < 0) return;
  next_ui = millis() + 500;
  if (_map_dl_pill) {
    if (dl.state() == mapview::TileDownloader::CONNECTING) {
      setText(_map_dl_pill, LV_SYMBOL_WIFI " Connecting...");
      lv_obj_remove_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
    } else if (dl.active()) {
      if (dl.failed())
        setTextFmt(_map_dl_pill, LV_SYMBOL_DOWNLOAD " %lu / %lu  " LV_SYMBOL_WARNING " %lu",
                              (unsigned long)dl.processed(), (unsigned long)dl.total(), (unsigned long)dl.failed());
      else
        setTextFmt(_map_dl_pill, LV_SYMBOL_DOWNLOAD " %lu / %lu", (unsigned long)dl.processed(),
                              (unsigned long)dl.total());
      lv_obj_remove_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
    } else if (dl.liveConnecting()) {
      setText(_map_dl_pill, LV_SYMBOL_WIFI " Connecting...");
      lv_obj_remove_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
    } else if (dl.liveQueued() > 0) {
      setTextFmt(_map_dl_pill, LV_SYMBOL_DOWNLOAD " live %d", dl.liveQueued());
      lv_obj_remove_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
    } else if (mapview::s_available && dl.savedJob(s_job_tmp)) {
      setText(_map_dl_pill, LV_SYMBOL_DOWNLOAD " Resume?");
      lv_obj_remove_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (_dl_overlay) refreshDownloadPopup();
  // New tiles in view while downloading: let the map pick them up.
  if (dl.active() && _screen == SCR_MAP && dl.downloaded() > 0) { mapview::s_cache.forgetMissing(); layoutMap(); }
}

// Sim / tests: the Navigation map at "lat,lon,z", not following the GPS.
void UITask::simMapAt(const char* spec) {
  double lat = 0, lon = 0;
  int z = 14;
  if (sscanf(spec, "%lf,%lf,%d", &lat, &lon, &z) < 2) return;
  openMap(true);
  _map_z = z;
  _map_cx = mapview::lonToTileX(lon, z);
  _map_cy = mapview::latToTileY(lat, z);
  _map_follow = false;
  layoutMap();
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// Sim page / tests: a saved WiFi network without going through Settings > WiFi.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_wifi_save(const char* ssid, const char* pass) { lvport::saveWifi(ssid, pass); }
// ...and this device's name (a test mentions it).
extern "C" EMSCRIPTEN_KEEPALIVE const char* sim_node_name() { return the_mesh.getNodeName(); }
#endif
