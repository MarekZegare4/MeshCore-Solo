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

static PSRAM_BSS RasterTileProvider s_raster("/sdcard/maps", LIVE_ROOT);
static PSRAM_BSS VectorTileProvider s_vector(s_raster);   // spike: vector data where there is some, raster elsewhere
static TileProvider*      s_provider = &s_raster;   // Map tools > Vector map (test) swaps it
static TileCache&         s_cache = *new (psramBuf<TileCache>(1)) TileCache();   // decoded tiles, in PSRAM
static PSRAM_BSS TileDownloader     s_dl("/sdcard/maps");
static PSRAM_BSS AreaStore          s_areas("/sdcard/maps");

// The base layer `i` (MapLayers.h) for everything that reads or writes tiles;
// false while a download or an area's deletion works in the current one's folder.
static bool applyLayer(int i) {
  if (i < 0 || i >= LAYER_COUNT) i = 0;
  if (i == s_layer) return true;
  if (s_dl.active() || s_areas.deleting()) return false;
  s_layer = (uint8_t)i;
  s_raster.setRoots(LAYERS[i].root, LAYERS[i].live);
  s_dl.setLayer(s_layer);
  s_areas.setRoot(LAYERS[i].root);
  s_cache.invalidate();
  return true;
}
static const uint32_t     AVG_TILE_BYTES = 22 * 1024;   // OpenTopoMap-ish, for the size estimate

// Hand a tile to the decoder (on the other core); false while one is in flight.
static bool loadTile(int z, int x, int y) { return s_cache.request(*s_provider, z, x, y); }

// A decoded tile placed in the cache: true when a view has something new.
// With live tiles on, a base tile shown without its trails overlay has the
// overlay fetched.
static bool pollTiles() {
  TileCache::Done d;
  if (!s_cache.poll(d)) return false;
  if (s_trails_on && d.src == &s_raster && d.overlay_missed) s_dl.liveRequest(d.z, d.x, d.y, true);
  return true;
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
  bool tiled = false;         // every visible cell holds a tile: grid and fill are under them
};
static Grid s_grid;           // the main map's

// The view moved (or the step changed): the grid with it, and the area
// redrawn -- not when a refresh finds it where it was.
static void setGrid(lv_obj_t* area, Grid& g, double left, double top, double step_px) {
  if (g.left == left && g.top == top && g.step_px == step_px) return;
  g.left = left; g.top = top; g.step_px = step_px;
  lv_obj_invalidate(area);
}
static lv_obj_t* s_scale_bar = nullptr;
static lv_obj_t* s_scale_lbl = nullptr;
static lv_obj_t* s_going = nullptr;   // what's going on (recording, download): a column, top centre

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
  if (!g || g->step_px < 8 || g->tiled) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  lv_draw_line_dsc_t d;
  lv_draw_line_dsc_init(&d);
  d.color = lv_color_hex(theme::SURFACE_2);
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

// A map area whose visible cells all hold a tile: its fill and grid lie
// under them (~5 ms a frame on the main map), so it drops both -- and with
// tiledCovers still tells LVGL it hides what's below, so that isn't drawn
// either.
static void showTiled(lv_obj_t* area, Grid& g, bool tiled) {
  g.tiled = tiled;
  lv_opa_t opa = tiled ? LV_OPA_TRANSP : LV_OPA_COVER;
  if (lv_obj_get_style_bg_opa(area, LV_PART_MAIN) != opa) lv_obj_set_style_bg_opa(area, opa, 0);
}
static void tiledCovers(lv_event_t* e) {   // LV_EVENT_COVER_CHECK, user data the Grid
  if (((const Grid*)lv_event_get_user_data(e))->tiled) coversOwnArea(e);
}

// The screen under the main map: the map area and the status bar hide all of
// it but a strip the area uncovers as it drifts in (Anim.h screenIn). Filled
// whole it cost ~2 ms a frame; now only what shows is. User data: the area.
static void fillAroundMap(lv_event_t* e) {
  lv_obj_t* scr = (lv_obj_t*)lv_event_get_target(e);
  lv_area_t s, m;
  lv_obj_get_coords(scr, &s);
  lv_obj_get_coords((lv_obj_t*)lv_event_get_user_data(e), &m);
  s.y1 = belowStatusBar();
  const lv_area_t strips[] = { { s.x1, s.y1, s.x2, m.y1 - 1 }, { s.x1, m.y2 + 1, s.x2, s.y2 },
                               { s.x1, m.y1, m.x1 - 1, m.y2 }, { m.x2 + 1, m.y1, s.x2, m.y2 } };
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_obj_get_style_bg_color(scr, LV_PART_MAIN);
  lv_layer_t* layer = lv_event_get_layer(e);
  for (const lv_area_t& a : strips)
    if (a.x1 <= a.x2 && a.y1 <= a.y2) lv_draw_rect(layer, &d, &a);
}

// Markers: one drawn layer, not an object per marker (64 nodes were ~190
// objects to lay out and invalidate on every pan). `idx` is the Nearby row
// (MK_NODE), the waypoint index (MK_WAYPOINT) or the live-share slot (MK_LIVE).
// layoutMapMarks() fills the placement (dot centre, name pill) from the view;
// the layer's draw callback paints it; a tap is hit-tested by hitMark().
enum : uint8_t { MK_NODE, MK_WAYPOINT, MK_LIVE };
static const int MARK_NAME = 31;
struct Mark {
  int32_t lat_e6, lon_e6; int idx; uint8_t kind; uint32_t col, text_hash;
  char name[MARK_NAME + 1];
  int16_t tw;                 // the name's width in px
  int16_t x, y;               // dot centre, layer px (valid when vis)
  int16_t lx, ly, lw, lh;     // name pill, layer px (lw 0: no room for it)
  uint8_t order;              // 1.. while it is a hop of the route being picked, else 0
  bool vis;                   // inside the view
};
// What was drawn for a slot last time, to repaint only what changed.
struct MarkSig { int16_t x, y, lx, ly, lw; uint32_t col, hash; uint8_t order; bool vis; };
static const int MAX_MARKS = NearbyModel::MAX_NEARBY > WaypointStore::CAPACITY + LiveTrackStore::CAPACITY
                             ? NearbyModel::MAX_NEARBY : WaypointStore::CAPACITY + LiveTrackStore::CAPACITY;   // Nearby map, or waypoints + live shares
static Mark* s_marks = psramBuf<Mark>(MAX_MARKS);
static MarkSig* s_sigs = psramBuf<MarkSig>(MAX_MARKS);
static int  s_sig_n = 0;
static lv_obj_t* s_mark_layer = nullptr;
// The route being picked (Route > Map): its points as lat/lon, and in layer px
// from the last layout, drawn as a line under the markers.
static const int ROUTE_PTS = HopRoute::MAX_PTS;   // a picked route has 10 at most
static int32_t s_route_lat[ROUTE_PTS], s_route_lon[ROUTE_PTS];
static int s_route_n = 0;
static lv_point_precise_t s_route_px[ROUTE_PTS];
static int8_t  s_route_hop[ROUTE_PTS];      // a shown message route's: each point's hop (-1 none)
static uint8_t s_route_key[ROUTE_PTS][4];   // and its repeater's key prefix
static bool s_route_on = false;
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

// ── Packet flow (ui-core/PacketFlow.h) ──
// Each packet heard runs along its line, sender -> repeaters -> here, as a
// dot leaving a trail that fades; a packet sent (and one heard on its way
// elsewhere) is a ring spreading from where it is. Drawn on the marker
// layer, under the names and dots. Only the area the flows cover is
// repainted while they move.
static PacketFlow* s_flow = nullptr;
static double s_view_left = 0, s_view_top = 0;   // the view, from the last layout
static int s_view_z = 0;
static bool s_view_ok = false;
static const uint32_t FLOW_LIFE = 7000;     // ms a flow stays on the map
static const uint32_t FLOW_TRAVEL = 1500;   // ms the dot takes from end to end
static const uint32_t PULSE_MS = 1400;
static bool s_flow_nodes = false;           // no node markers on this map: a flow draws its own, while it lasts
static lv_area_t s_flow_area;               // what the flows covered at the last tick
static bool s_flow_area_on = false;

// Fixed colours, not the accent: a kind looks the same whatever the theme.
static uint32_t flowColour(uint8_t kind) {
  switch (kind) {
    case PacketFlow::K_ADVERT: return 0x4FC3F7;   // blue
    case PacketFlow::K_MSG:    return 0x6FCF6F;   // green
    case PacketFlow::K_GROUP:  return 0xFFB000;   // amber
    case PacketFlow::K_REQ:    return 0xB39DDB;   // violet
    case PacketFlow::K_ACK:    return 0x80CBC4;   // teal
    case PacketFlow::K_SENT:   return theme::TEXT;
    default:                   return 0xF48FB1;   // pink: trace, control ...
  }
}
static const char* const FLOW_NAMES[PacketFlow::K_COUNT] = { "Adv", "DM", "Ch", "Req", "Ack", "Other", "Sent" };
static const char* const FLOW_KINDS[PacketFlow::K_COUNT] = { "Advert", "Direct message", "Channel", "Request", "Ack", "Other", "Sent" };
static lv_obj_t* s_flow_legend = nullptr;   // top centre: each kind's count over the last minute; a tap: the recent packets

// "Adv 3  Ch 5 ...", each in its colour; with nothing to count, the way to
// the recent packets when there are some, else hidden.
static void flowLegendRefresh() {
  if (!s_flow_legend) return;
  char t[160];
  int o = 0;
  for (int k = 0; s_flow && s_flow->enabled() && k < PacketFlow::K_COUNT; k++) {
    const int n = s_flow->count(k);
    if (n > 0 && o < (int)sizeof(t) - 24)
      o += snprintf(t + o, sizeof(t) - o, "%s#%06lX %s %d#", o ? "  " : "", (unsigned long)flowColour(k), FLOW_NAMES[k], n);
  }
  t[o] = '\0';
  if (!o && s_flow && s_flow->enabled() && s_flow->historyCount() > 0) o = snprintf(t, sizeof(t), LV_SYMBOL_LIST " Recent packets");
  if (!o) { lv_obj_add_flag(s_flow_legend, LV_OBJ_FLAG_HIDDEN); return; }
  setText(s_flow_legend, t);
  lv_obj_remove_flag(s_flow_legend, LV_OBJ_FLAG_HIDDEN);
}

static void flowPx(int32_t lat, int32_t lon, int32_t& x, int32_t& y);
// Point k of a flow on the layer, where its node and you are now (PacketFlow::pointNow).
static void flowPt(const PacketFlow::Flow& f, int k, int32_t& x, int32_t& y) {
  int32_t lat, lon;
  s_flow->pointNow(f, k, lat, lon);
  flowPx(lat, lon, x, y);
}

static void flowPx(int32_t lat, int32_t lon, int32_t& x, int32_t& y) {
  double fx = lonToTileX(lon / 1e6, s_view_z) * TILE_PX - s_view_left;
  double fy = latToTileY(lat / 1e6, s_view_z) * TILE_PX - s_view_top;
  x = (int32_t)lround(fx < -30000 ? -30000 : fx > 30000 ? 30000 : fx);
  y = (int32_t)lround(fy < -30000 ? -30000 : fy > 30000 ? 30000 : fy);
}

// The area (layer px) the live flows cover, with room for a ring; false: none.
static bool flowArea(lv_area_t& r) {
  if (!s_flow || !s_view_ok || !s_flow->enabled()) return false;
  const uint32_t now = millis();
  bool any = false;
  for (int i = 0; i < s_flow->count(); i++) {
    const PacketFlow::Flow& f = s_flow->at(i);
    if (now - f.t_fade >= FLOW_LIFE) continue;   // (an older one may live on with a newer copy)
    for (int k = 0; k < f.n; k++) {
      int32_t x, y;
      flowPt(f, k, x, y);
      if (!any) { r = { x, y, x, y }; any = true; }
      r.x1 = min(r.x1, x); r.y1 = min(r.y1, y); r.x2 = max(r.x2, x); r.y2 = max(r.y2, y);
    }
  }
  if (any) { r.x1 -= 28; r.y1 -= 28; r.x2 += s_flow_nodes ? 120 : 28; r.y2 += 28; }   // (a node's name to its right)
  return any;
}

// On a map without node markers, the nodes a flow passes -- its sender and
// repeaters, a dot and the name -- shown while it lasts and fading with it,
// each once however many flows share it (the most visible one counts).
static void drawFlowNodes(lv_layer_t* layer, const lv_area_t& a, uint32_t now) {
  struct Node { int16_t idx; lv_opa_t opa; int32_t x, y; uint8_t kind; };
  Node nodes[24];
  int nn = 0;
  for (int i = 0; i < s_flow->count(); i++) {   // newest first
    const PacketFlow::Flow& f = s_flow->at(i);
    const uint32_t age = now - (f.n == 1 ? f.t_ms : f.t_fade);   // a pulse's node: as long as the pulse
    const uint32_t life = f.n == 1 ? PULSE_MS : FLOW_LIFE;
    if (age >= life) continue;
    const lv_opa_t opa = (lv_opa_t)(age < FLOW_TRAVEL ? 255 : 255 - 255 * (age - FLOW_TRAVEL) / (life - FLOW_TRAVEL + 1));
    for (int k = 0; k < f.n; k++) {
      if (f.node[k] < 0) continue;
      int j = 0;
      while (j < nn && nodes[j].idx != f.node[k]) j++;
      if (j < nn) { if (opa > nodes[j].opa) nodes[j].opa = opa; continue; }
      if (nn == (int)(sizeof(nodes) / sizeof(nodes[0]))) continue;
      int32_t x, y;
      flowPt(f, k, x, y);
      nodes[nn++] = { f.node[k], opa, a.x1 + x, a.y1 + y, f.kind };
    }
  }
  lv_draw_rect_dsc_t dot, pill;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.border_color = lv_color_hex(theme::BG);
  dot.border_width = 2;
  lv_draw_rect_dsc_init(&pill);
  pill.bg_color = lv_color_hex(theme::BG);
  pill.radius = 4;
  lv_draw_label_dsc_t ld;
  lv_draw_label_dsc_init(&ld);
  ld.font = THEME_FONT_SMALL;
  ld.color = lv_color_hex(theme::TEXT);
  ld.text_local = true;
  const int32_t lh = lv_font_get_line_height(THEME_FONT_SMALL);
  for (int j = 0; j < nn; j++) {
    const Node& n = nodes[j];
    if (n.x < a.x1 - 120 || n.x > a.x2 + 8 || n.y < a.y1 - 20 || n.y > a.y2 + 20) continue;
    dot.bg_color = lv_color_hex(flowColour(n.kind));
    dot.bg_opa = dot.border_opa = n.opa;
    lv_area_t da = { n.x - 5, n.y - 5, n.x + 5, n.y + 5 };
    lv_draw_rect(layer, &dot, &da);
    ContactInfo c;
    if (!the_mesh.getContactByIdx(n.idx, c) || !c.name[0]) continue;
    char name[20];
    snprintf(name, sizeof(name), "%s", c.name);
    lv_point_t sz;
    lv_text_get_size(&sz, name, THEME_FONT_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_area_t pa = { n.x + 8, n.y - lh / 2 - 1, n.x + 8 + sz.x + 9, n.y + lh / 2 + 1 };
    pill.bg_opa = (lv_opa_t)(n.opa * 4 / 5);
    lv_draw_rect(layer, &pill, &pa);
    lv_area_t ta = { pa.x1 + 5, pa.y1 + 1, pa.x2 - 4, pa.y2 };
    ld.text = name;
    ld.opa = n.opa;
    lv_draw_label(layer, &ld, &ta);
  }
}

static void drawFlows(lv_layer_t* layer, const lv_area_t& a) {
  if (!s_flow || !s_view_ok || !s_flow->enabled()) return;
  const uint32_t now = millis();
  lv_draw_line_dsc_t ln, halo;   // the line, over a dark halo that lifts it off the map
  lv_draw_line_dsc_init(&ln);
  ln.width = 3;
  ln.round_start = ln.round_end = 1;
  halo = ln;
  halo.width = 6;
  halo.color = lv_color_hex(theme::BG);
  lv_draw_arc_dsc_t ring;
  lv_draw_arc_dsc_init(&ring);
  ring.start_angle = 0; ring.end_angle = 360;
  ring.width = 3;
  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.radius = LV_RADIUS_CIRCLE;
  dot.bg_opa = LV_OPA_COVER;
  dot.border_color = lv_color_hex(theme::BG);
  dot.border_width = 2;
  dot.border_opa = LV_OPA_COVER;
  for (int i = s_flow->count() - 1; i >= 0; i--) {   // oldest first: the newest on top
    const PacketFlow::Flow& f = s_flow->at(i);
    const uint32_t age = now - f.t_ms;      // the dot's run
    const uint32_t fade = now - f.t_fade;   // the fading: with every copy of the packet
    if (fade >= FLOW_LIFE) continue;
    const lv_color_t col = lv_color_hex(flowColour(f.kind));
    int32_t px[PacketFlow::MAX_PTS], py[PacketFlow::MAX_PTS];
    for (int k = 0; k < f.n; k++) { flowPt(f, k, px[k], py[k]); px[k] += a.x1; py[k] += a.y1; }
    if (f.n == 1) {   // a pulse
      if (age >= PULSE_MS) continue;
      ring.color = col;
      ring.center.x = px[0]; ring.center.y = py[0];
      ring.radius = (uint16_t)(5 + 22 * age / PULSE_MS);
      ring.opa = (lv_opa_t)(255 - 255 * age / PULSE_MS);
      lv_draw_arc(layer, &ring);
      continue;
    }
    // The trail behind the dot, fading over the flow's life.
    float len[PacketFlow::MAX_PTS];
    float total = 0;
    for (int k = 1; k < f.n; k++) {
      float dx = px[k] - px[k - 1], dy = py[k] - py[k - 1];
      len[k] = sqrtf(dx * dx + dy * dy);
      total += len[k];
    }
    const float t = age >= FLOW_TRAVEL ? 1.0f : (float)age / FLOW_TRAVEL;
    float left = total * t;
    ln.color = col;
    ln.opa = (lv_opa_t)(fade < FLOW_TRAVEL ? 255 : 255 - 215 * (fade - FLOW_TRAVEL) / (FLOW_LIFE - FLOW_TRAVEL));
    halo.opa = (lv_opa_t)(ln.opa * 2 / 3);
    int32_t hx = px[0], hy = py[0];   // the dot
    int32_t ex_[PacketFlow::MAX_PTS], ey_[PacketFlow::MAX_PTS];
    int segs = 0;
    for (int k = 1; k < f.n && left > 0; k++, segs++) {
      float part = left >= len[k] ? 1.0f : left / len[k];
      ex_[k] = px[k - 1] + (int32_t)lroundf((px[k] - px[k - 1]) * part);
      ey_[k] = py[k - 1] + (int32_t)lroundf((py[k] - py[k - 1]) * part);
      hx = ex_[k]; hy = ey_[k];
      left -= len[k];
    }
    for (int pass = 0; pass < 2; pass++)   // all the halo, then all the line: joints stay clean
      for (int k = 1; k <= segs; k++) {
        lv_draw_line_dsc_t& d = pass ? ln : halo;
        d.p1 = { px[k - 1], py[k - 1] };
        d.p2 = { ex_[k], ey_[k] };
        lv_draw_line(layer, &d);
      }
    if (age < FLOW_TRAVEL) {
      dot.bg_color = col;
      lv_area_t d = { hx - 5, hy - 5, hx + 5, hy + 5 };
      lv_draw_rect(layer, &dot, &d);
    } else if (age < FLOW_TRAVEL + PULSE_MS) {   // arrived: a ring here
      uint32_t p = age - FLOW_TRAVEL;
      ring.color = col;
      ring.center.x = px[f.n - 1]; ring.center.y = py[f.n - 1];
      ring.radius = (uint16_t)(5 + 16 * p / PULSE_MS);
      ring.opa = (lv_opa_t)(220 - 220 * p / PULSE_MS);
      lv_draw_arc(layer, &ring);
    }
  }
  if (s_flow_nodes) drawFlowNodes(layer, a, now);
}

static const int MARK_D = 12;   // marker dot diameter
static const int MARK_LABELS_MAX = 20;   // names shown at once: the rest stay dots

// LV_EVENT_DRAW_MAIN of the marker layer: the name pills, then the dots over them.
static void drawMarks(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  if (s_route_on && s_route_n >= 2) {
    lv_draw_line_dsc_t ln;
    lv_draw_line_dsc_init(&ln);
    ln.color = lv_color_hex(theme::ACCENT);
    ln.width = 3;
    ln.round_start = ln.round_end = 1;
    for (int i = 0; i + 1 < s_route_n; i++) {
      ln.p1 = { s_route_px[i].x + a.x1, s_route_px[i].y + a.y1 };
      ln.p2 = { s_route_px[i + 1].x + a.x1, s_route_px[i + 1].y + a.y1 };
      lv_draw_line(layer, &ln);
    }
  }
  drawFlows(layer, a);
  lv_draw_rect_dsc_t pill;
  lv_draw_rect_dsc_init(&pill);
  pill.bg_color = lv_color_hex(theme::BG);
  pill.bg_opa = LV_OPA_COVER;
  pill.radius = 4;
  lv_draw_label_dsc_t ld;
  lv_draw_label_dsc_init(&ld);
  ld.font = THEME_FONT_SMALL;
  ld.color = lv_color_hex(theme::TEXT);
  for (int k = 0; k < s_mark_count; k++) {
    const Mark& m = s_marks[k];
    if (!m.vis || !m.lw) continue;
    lv_area_t pa = { a.x1 + m.lx, a.y1 + m.ly, a.x1 + m.lx + m.lw - 1, a.y1 + m.ly + m.lh - 1 };
    lv_draw_rect(layer, &pill, &pa);
    lv_area_t ta = { pa.x1 + 7, pa.y1 + 1, pa.x2 - 7, pa.y2 - 1 };
    ld.text = m.name;
    lv_draw_label(layer, &ld, &ta);
  }
  lv_draw_rect_dsc_t dot;
  lv_draw_rect_dsc_init(&dot);
  dot.bg_opa = LV_OPA_COVER;
  dot.radius = LV_RADIUS_CIRCLE;
  dot.border_color = lv_color_hex(theme::BG);
  dot.border_opa = LV_OPA_COVER;
  dot.border_width = 2;
  const int half = MARK_D / 2;
  for (int k = 0; k < s_mark_count; k++) {
    const Mark& m = s_marks[k];
    if (!m.vis) continue;
    dot.bg_color = lv_color_hex(m.col);
    lv_area_t da = { a.x1 + m.x - half, a.y1 + m.y - half, a.x1 + m.x + half, a.y1 + m.y + half };   // odd: centred on (x, y), as the lines are
    lv_draw_rect(layer, &dot, &da);
  }
  // Hop numbers of the route being picked: a badge at the dot's top right.
  lv_draw_rect_dsc_t badge;
  lv_draw_rect_dsc_init(&badge);
  badge.bg_color = lv_color_hex(theme::ACCENT);
  badge.bg_opa = LV_OPA_COVER;
  badge.radius = LV_RADIUS_CIRCLE;
  lv_draw_label_dsc_t bl;
  lv_draw_label_dsc_init(&bl);
  bl.font = THEME_FONT_SMALL;
  bl.color = lv_color_hex(theme::ON_ACCENT);
  bl.align = LV_TEXT_ALIGN_CENTER;
  for (int k = 0; k < s_mark_count; k++) {
    const Mark& m = s_marks[k];
    if (!m.vis || !m.order) continue;
    lv_area_t ba = { a.x1 + m.x + 1, a.y1 + m.y - 17, a.x1 + m.x + 15, a.y1 + m.y - 3 };
    lv_draw_rect(layer, &badge, &ba);
    char t[4];
    snprintf(t, sizeof(t), "%d", m.order);
    bl.text = t;
    bl.text_local = true;   // t is on this stack
    lv_area_t ta = { ba.x1, ba.y1 + 0, ba.x2, ba.y2 };
    lv_draw_label(layer, &bl, &ta);
  }
}

// The marker at a point of the map area (px relative to it): a name pill under
// the point, else the nearest dot within a finger's reach; -1 if none.
static int hitMark(int px, int py) {
  for (int k = 0; k < s_mark_count; k++) {
    const Mark& m = s_marks[k];
    if (m.vis && m.lw && px >= m.lx - 3 && px < m.lx + m.lw + 3 && py >= m.ly - 3 && py < m.ly + m.lh + 3) return k;
  }
  int best = -1, best_d = 18 * 18 + 1;
  for (int k = 0; k < s_mark_count; k++) {
    const Mark& m = s_marks[k];
    if (!m.vis) continue;
    int d = (m.x - px) * (m.x - px) + (m.y - py) * (m.y - py);
    if (d < best_d) { best_d = d; best = k; }
  }
  return best;
}

}  // namespace mapview

namespace navmap {   // shared with NavMap.h (included after this file)
static const int BAR_H = 44;   // target bar along the bottom of the Navigation map, while there is a target
static lv_obj_t* s_scale_pill = nullptr;   // the bottom-left pill (credit, scale, zoom): moves up over a bar
static bool s_ready = false;               // the map is built: a bar coming or going lays it out again
static lv_obj_t* s_tools_list = nullptr;   // Map tools' list while it's up
static int32_t s_tools_y = 0;              // where it was scrolled, leaving for a page of options
// Navigation targets as one int (event user data): type << 8 | index.
enum : uint8_t { T_CLEAR, T_WAYPOINT, T_TRAILSTART, T_LIVE };
static int code(uint8_t type, int idx) { return (type << 8) | (idx & 0xFF); }
}  // namespace navmap

// A press only pans once it has moved DRAG_SLOP px: a tap's jitter (a pixel
// or two on the touch panel) would otherwise stop the map following you.
static const int DRAG_SLOP = 8;
static void onMapPress(lv_event_t* e) {
  static lv_point_t held;   // movement within the slop, applied once it's a pan
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED) { mapview::s_drag = 0; held = { 0, 0 }; return; }
  lv_point_t v;
  lv_indev_get_vect(lv_indev_active(), &v);
  if (v.x == 0 && v.y == 0) return;
  mapview::s_drag += abs(v.x) + abs(v.y);
  held.x += v.x; held.y += v.y;
  if (mapview::s_drag <= DRAG_SLOP) return;
  mapview::s_last_pan_ms = millis();
  s_ui->mapPan(held.x, held.y);
  held = { 0, 0 };
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
static void onDlResume(lv_event_t* e)    { (void)e; s_ui->mapDownloadResume(); }
static void onDlDiscard(lv_event_t* e)   { (void)e; s_ui->mapDownloadDiscard(); }
static void onNavTools(lv_event_t* e);   // NavMap.h
static void onMapCredits(lv_event_t* e) { (void)e; s_ui->showToast(creditText(mapview::s_provider->attribution()), 4000); }
static void onMapClick(lv_event_t* e) {
  if (mapview::s_drag > 8) return;   // the press was a pan
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  lv_area_t a;
  lv_obj_get_coords((lv_obj_t*)lv_event_get_target(e), &a);
  int k = mapview::hitMark(p.x - a.x1, p.y - a.y1);
  if (k >= 0) s_ui->mapOpenMarker(k);
}

// The centre-on-me button in the accent colour while the map follows you.
static void followShown(lv_obj_t* btn, bool on) {
  if (btn) setTextColor(lv_obj_get_child(btn, 0), on ? theme::ACCENT : theme::TEXT);
}

static lv_obj_t* mapButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 40, 40);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);   // opaque: blended over the tiles, each cost ~1.6 ms a frame
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
  lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(l, 7, 0);   // 22 px tall, round ends: every pill over the map alike
  lv_obj_set_style_pad_ver(l, 3, 0);
  lv_obj_set_style_radius(l, 11, 0);
  return l;
}

// Centring on a point puts it mid-way down the visible map: above the nav bar
// on the Navigation map (the bias is in tiles, added to the view centre).
double UITask::mapCenterBias() const {
  return mapBottomH() / 2.0 / mapview::TILE_PX;
}

// The bar along the map's bottom edge: the target's (Navigation map, while
// there is one) or the route pick's.
int UITask::mapBottomH() const {
  if (_route_pick || _route_show) return navmap::BAR_H;
  return _map_nav && _nav_bar && !lv_obj_has_flag(_nav_bar, LV_OBJ_FLAG_HIDDEN) ? navmap::BAR_H : 0;
}

// The controls along the bottom edge, clear of that bar.
void UITask::mapPlaceBottom() {
  const int bottom = mapBottomH();
  if (_map_center_btn) lv_obj_align(_map_center_btn, LV_ALIGN_BOTTOM_RIGHT, -6, -6 - bottom);
  if (navmap::s_scale_pill) lv_obj_align(navmap::s_scale_pill, LV_ALIGN_BOTTOM_LEFT, 6, -6 - bottom);
}

#ifdef UI_PERF_TEST
// UI_PERF_TEST (UITask::loop): reset = before the map opens; else report the
// tiles decoded since, then time a drag and a full redraw.
void UITask::perfMap(bool reset) {
  using namespace mapview;
  if (reset) { s_perf_tile_us = s_perf_tile_max = s_perf_tile_n = 0; return; }
  while (s_cache.busy()) { delay(5); pollTiles(); }   // a prefetch still decoding would share PSRAM with the drawing
  Serial.printf("PERF tiles %lu decoded, avg %5.1f ms, max %5.1f ms (pending %d)\n", (unsigned long)s_perf_tile_n,
                s_perf_tile_n ? s_perf_tile_us / 1000.0f / s_perf_tile_n : 0.0f, s_perf_tile_max / 1000.0f, (int)_map_pending);
  if (s_perf_tile_n) Serial.printf("PERF   of which read %5.1f ms (%lu B), decode %5.1f ms, trails %5.1f ms\n", s_raster.perf_read_us / 1000.0f / s_perf_tile_n,
                                  (unsigned long)(s_raster.perf_bytes / s_perf_tile_n), s_raster.perf_dec_us / 1000.0f / s_perf_tile_n,
                                  s_raster.perf_ovl_us / 1000.0f / s_perf_tile_n);
  s_raster.perf_read_us = s_raster.perf_dec_us = s_raster.perf_bytes = s_raster.perf_ovl_us = 0;
  uint32_t fl0 = lvport::s_flush_us, t = micros();
  for (int i = 0; i < 20; i++) {   // a drag: 4 px a frame, tiles already decoded
    _map_cx += (i < 10 ? 4.0 : -4.0) / TILE_PX;
    layoutMap();
    lv_refr_now(NULL);
  }
  Serial.printf("PERF map pan frame %5.1f ms, flush %5.1f ms\n", (micros() - t) / 20000.0f, (lvport::s_flush_us - fl0) / 20000.0f);
  fl0 = lvport::s_flush_us; t = micros();
  for (int i = 0; i < 10; i++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
  Serial.printf("PERF map full redraw %5.1f ms, flush %5.1f ms\n", (micros() - t) / 10000.0f, (lvport::s_flush_us - fl0) / 10000.0f);
  perfDrawProfile("map");
  s_perf_tile_us = s_perf_tile_max = s_perf_tile_n = 0;
}
#endif

void UITask::openMap(bool nav) {
  // One map everywhere (trail, target, tools); from Nodes it adds the nodes.
  _map_nav = true;
  _map_nodes = !nav;
  _route_show = false;   // a message's route is shown by its own way in (messageAction)
  _core->hop_route.close();
  _core->flow.replayStop();   // a Play all left running last time
  showMap();
}

void UITask::showMap(bool frame) {
  _screen = SCR_MAP;
  mapview::s_trails_on = lvport::trailsOn();
  mapview::applyLayer(lvport::mapLayer());   // (a download running in another: that one, until it's done)
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
  if (frame && _map_nav && _prefs && _prefs->locator_has_target) navFrameTarget();   // show where we're going
}

void UITask::buildMap() {
  lv_obj_t* body = newScreen(NULL, false);
  lv_obj_set_layout(body, LV_LAYOUT_NONE);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(body, lv_color_hex(theme::SURFACE), 0);   // unloaded / missing tiles
  lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(body, mapview::drawGrid, LV_EVENT_DRAW_MAIN_END, &mapview::s_grid);
  lv_obj_add_event_cb(body, mapview::tiledCovers, LV_EVENT_COVER_CHECK, &mapview::s_grid);
  lv_obj_t* scr = lv_obj_get_screen(body);
  lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
  lv_obj_add_event_cb(scr, mapview::fillAroundMap, LV_EVENT_DRAW_MAIN, body);
  lv_obj_add_flag(body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(body, onMapPress, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(body, onMapPress, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(body, onMapLongPress, LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_add_event_cb(body, onMapClick, LV_EVENT_CLICKED, NULL);   // a marker, hit-tested
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
  lv_obj_set_size(_map_me, 17, 17);   // odd: its middle on the pixel a route line starts from
  lv_obj_set_style_radius(_map_me, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(_map_me, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_bg_opa(_map_me, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(_map_me, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_width(_map_me, 3, 0);
  lv_obj_remove_flag(_map_me, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(_map_me, LV_OBJ_FLAG_HIDDEN);

  mapview::s_mark_layer = lv_obj_create(_map_marks);   // markers over the trail, target and own position
  lv_obj_remove_style_all(mapview::s_mark_layer);
  lv_obj_set_size(mapview::s_mark_layer, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(mapview::s_mark_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(mapview::s_mark_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(mapview::s_mark_layer, mapview::drawMarks, LV_EVENT_DRAW_MAIN, NULL);
  mapview::s_flow = &_core->flow;
  mapview::s_view_ok = false;
  mapview::s_flow_area_on = false;
  mapview::s_sig_n = 0;

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
  navmap::s_ready = false;
  _map_center_btn = mapButton(body, LV_SYMBOL_GPS, onMapCenter);   // bottom right (mapPlaceBottom)
  // Top centre: what's going on, one pill under another -- the trail /
  // live share (NavMap.h), the download. Empty most of the time.
  mapview::s_going = lv_obj_create(body);
  lv_obj_remove_style_all(mapview::s_going);
  lv_obj_remove_flag(mapview::s_going, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(mapview::s_going, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(mapview::s_going, 212, LV_SIZE_CONTENT);   // between the button columns
  lv_obj_set_flex_flow(mapview::s_going, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mapview::s_going, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(mapview::s_going, 4, 0);
  lv_obj_align(mapview::s_going, LV_ALIGN_TOP_MID, 0, 15);
  _map_dl_pill = mapPill(mapview::s_going, "");   // tap: the download popup
  lv_obj_set_style_text_color(_map_dl_pill, lv_color_hex(theme::ACCENT), 0);
  lv_obj_add_flag(_map_dl_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(_map_dl_pill, 6);
  lv_obj_add_event_cb(_map_dl_pill, onMapDownload, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(_map_dl_pill, LV_OBJ_FLAG_HIDDEN);
  mapview::s_flow_legend = mapPill(mapview::s_going, "");   // packet flow: what's been heard
  lv_label_set_recolor(mapview::s_flow_legend, true);
  lv_obj_set_style_max_width(mapview::s_flow_legend, 212, 0);   // a busy minute wraps to a second line
  lv_obj_set_style_text_align(mapview::s_flow_legend, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_radius(mapview::s_flow_legend, 8, 0);
  lv_obj_add_flag(mapview::s_flow_legend, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(mapview::s_flow_legend, LV_OBJ_FLAG_CLICKABLE);   // tap: the recent packets
  lv_obj_set_ext_click_area(mapview::s_flow_legend, 6);
  lv_obj_add_event_cb(mapview::s_flow_legend, [](lv_event_t*) { s_ui->flowHistoryPopup(); }, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(mapview::s_flow_legend, [](lv_event_t*) { mapview::s_flow_legend = nullptr; }, LV_EVENT_DELETE, NULL);
  mapview::flowLegendRefresh();
  // Bottom left, one pill about the map itself: the map data credit (the
  // licences require it on the map; a tap shows the full line, also in
  // Settings > About), a bar one grid step long with its distance, the zoom.
  lv_obj_t* sc = lv_obj_create(body);   // a pill (mapPill's look) holding a row
  lv_obj_remove_style_all(sc);
  lv_obj_remove_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(sc, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(sc, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(sc, 8, 0);
  lv_obj_set_style_pad_ver(sc, 3, 0);
  lv_obj_set_style_radius(sc, 11, 0);
  lv_obj_set_size(sc, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(sc, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(sc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(sc, 6, 0);
  lv_obj_add_flag(sc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(sc, 8);
  lv_obj_add_event_cb(sc, onMapCredits, LV_EVENT_CLICKED, NULL);
  label(sc, "\xC2\xA9", THEME_FONT_SMALL, theme::TEXT_MUTED);
  mapview::s_scale_bar = lv_obj_create(sc);
  lv_obj_remove_style_all(mapview::s_scale_bar);
  lv_obj_remove_flag(mapview::s_scale_bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(mapview::s_scale_bar, 60, 6);
  lv_obj_set_style_border_color(mapview::s_scale_bar, lv_color_hex(theme::TEXT), 0);
  lv_obj_set_style_border_width(mapview::s_scale_bar, 2, 0);
  lv_obj_set_style_border_side(mapview::s_scale_bar, (lv_border_side_t)(LV_BORDER_SIDE_BOTTOM | LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT), 0);
  mapview::s_scale_lbl = label(sc, "", THEME_FONT_SMALL, theme::TEXT);
  _map_zoom_lbl = label(sc, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  navmap::s_scale_pill = sc;   // bottom left (mapPlaceBottom)
  _map_hint = mapPill(body, "");
  lv_label_set_long_mode(_map_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_map_hint, 200);
  lv_obj_set_style_text_align(_map_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_radius(_map_hint, 8, 0);   // may wrap
  lv_obj_align(_map_hint, LV_ALIGN_TOP_MID, 0, 50);   // clear of own position, mid-map
  lv_obj_add_flag(_map_hint, LV_OBJ_FLAG_HIDDEN);
  if (_map_nav) buildNavControls(body);
  _route_bar = _route_lbl = nullptr;
  if (_route_pick || _route_show) buildRouteBar(body);
  mapPlaceBottom();

  layoutNow(body);
  mapview::s_mark_count = 0;   // the previous markers went with the previous screen
  rebuildMapMarkers();
  layoutMap();
  navmap::s_ready = true;
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
    mapview::setGrid(_map_area, mapview::s_grid, left, top, mapview::gridStep(_map_z, lat, _prefs && _prefs->units_imperial, 56, sl, sizeof(sl)));
    if (mapview::s_scale_lbl) {
      setText(mapview::s_scale_lbl, sl);
      lv_obj_set_width(mapview::s_scale_bar, (int)lround(mapview::s_grid.step_px));
    }
  }
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << _map_z;
  bool have_provider = mapview::s_available;
  int shown = 0, missing = 0, over = 0;
  bool tiled = true;
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
        if (on_screen) tiled = false;
      }
    }
  }
  mapview::showTiled(_map_area, mapview::s_grid, tiled);

  mapview::labels::layout(left, top, w, h, _map_z, have_provider && mapview::s_provider == &mapview::s_vector && mapview::s_vector.hasData(),
                          mapBottomH());

  // Own position
  int32_t lat, lon;
  if (_core->course.currentLocation(lat, lon)) {
    double x = mapview::lonToTileX(lon / 1e6, _map_z) * mapview::TILE_PX - left;
    double y = mapview::latToTileY(lat / 1e6, _map_z) * mapview::TILE_PX - top;
    lv_obj_set_pos(_map_me, (int)lround(x) - 8, (int)lround(y) - 8);
    lv_obj_remove_flag(_map_me, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(_map_me, LV_OBJ_FLAG_HIDDEN);
  }
  followShown(_map_center_btn, _map_follow);
  layoutMapMarks(left, top, w, h);
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
  const char* hint = !have_provider ? (mapview::s_layer ? "Nothing of this map on the card -\ndownload an area or turn Live tiles on"
                                                        : "No map on the SD card.\nPut tiles in /maps (tools/maps).")
                   : mapview::s_dl.liveQueued() > 0 ? nullptr   // being fetched
                   : (!_map_pending && shown == 0 && missing > 0)
                       ? "No map here at this zoom -\nzoom out or download the area" : nullptr;
  if (hint && !areaSelecting()) { setText(_map_hint, hint); lv_obj_remove_flag(_map_hint, LV_OBJ_FLAG_HIDDEN); }
  else lv_obj_add_flag(_map_hint, LV_OBJ_FLAG_HIDDEN);
  areaLayout();
}

// One tile decode per call, nearest to the centre first.
void UITask::mapLoop() {
  mapFlowTick();
  if (_map_follow && (int32_t)(millis() - _next_follow_ms) >= 0) {   // following you: every fix
    _next_follow_ms = millis() + 1000;
    int32_t lat, lon;
    if (_core->course.currentLocation(lat, lon)) {
      double cx = mapview::lonToTileX(lon / 1e6, _map_z), cy = mapview::latToTileY(lat / 1e6, _map_z) + mapCenterBias();
      if (cx != _map_cx || cy != _map_cy) { _map_cx = cx; _map_cy = cy; layoutMap(); }
    }
  }
  if ((int32_t)(millis() - _next_map_marks_ms) >= 0) {
    _next_map_marks_ms = millis() + 3000;
    rebuildMapMarkers();
    layoutMap();
  }
  if (_map_nav && navTrailSync()) layoutMap();   // a new trail point: the line keeps up with you
  if (_map_nav) navPollAveraging();
  if (_map_nav && (int32_t)(millis() - _next_nav_bar_ms) >= 0) {
    _next_nav_bar_ms = millis() + 1000;
    refreshNavBar();
  }
  if (!_map_area) return;
  if (mapview::pollTiles()) layoutMap();   // a tile decoded on the other core
  uint32_t since_pan = millis() - mapview::s_last_pan_ms;
  if (since_pan < mapview::PAN_SETTLE_MS) return;   // mid-drag: keep it smooth, decode after
  if (mapview::labels::loadOne()) { layoutMap(); return; }   // names of the view first: a small file
  if (mapview::s_cache.busy()) return;   // one tile at a time
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
    else layoutMap();   // nothing left to decode: pending settles
  }
}

void UITask::rebuildMapMarkers() {
  if (!_map_marks) return;
  mapview::s_mark_prev = mapview::s_mark_count;
  mapview::s_mark_count = 0;
  if (_map_nav) rebuildNavMarkers();
  const bool nodes = _map_nodes || !_map_nav;
  if (nodes) rebuildNodeMarkers();
  mapview::s_flow_nodes = !nodes;   // the flows bring theirs
  mapview::s_mark_prev = 0;
}

void UITask::rebuildNodeMarkers() {
  _nearby->refreshStored();
  for (int i = 0; i < _nearby->count() && mapview::s_mark_count < mapview::MAX_MARKS; i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    if (e.lat_e6 == 0 && e.lon_e6 == 0) continue;
    if (e.is_live && _map_nav) continue;   // the live share's own marker shows it
    uint32_t col = e.is_live ? theme::OK : e.type == ADV_TYPE_CHAT ? theme::TEXT : theme::TEXT_MUTED;
    addMapMark(mapview::MK_NODE, i, e.lat_e6, e.lon_e6, col, e.name[0] ? e.name : "?");
  }
}

// A marker is a record: where it is, its colour and name. Placement and
// painting are layoutMapMarks() and mapview::drawMarks(). The name's width is
// measured here, not per pan: a slot whose name is unchanged keeps it.
void UITask::addMapMark(uint8_t kind, int idx, int32_t lat_e6, int32_t lon_e6, uint32_t col, const char* text) {
  if (!_map_marks || mapview::s_mark_count >= mapview::MAX_MARKS) return;
  uint32_t h = 2166136261u;   // FNV-1a of the name
  for (const char* c = text; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
  int k = mapview::s_mark_count++;
  mapview::Mark& mk = mapview::s_marks[k];
  bool same_name = k < mapview::s_mark_prev && mk.text_hash == h;
  mk.lat_e6 = lat_e6; mk.lon_e6 = lon_e6; mk.idx = idx; mk.kind = kind; mk.col = col; mk.text_hash = h;
  mk.vis = false; mk.lw = 0; mk.order = 0;
  if (same_name) return;
  size_t n = strlen(text);
  if (n > (size_t)mapview::MARK_NAME) {   // cut at a character boundary
    n = mapview::MARK_NAME;
    while (n > 0 && ((uint8_t)text[n] & 0xC0) == 0x80) n--;
  }
  memcpy(mk.name, text, n);
  mk.name[n] = '\0';
  lv_point_t sz;
  lv_text_get_size(&sz, mk.name, THEME_FONT_SMALL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  mk.tw = (int16_t)sz.x;
}

// Places the markers for the view: a dot on each spot inside it, and the name
// beside it (right, left, above, below) where it covers neither the map's
// controls, nor another name, nor another dot -- in marker order, so
// waypoints and live shares keep their names before the many nodes. Only the
// markers that changed since the last layout are repainted: a rebuild that
// changed nothing leaves the tiles under them alone.
void UITask::layoutMapMarks(double left, double top, int w, int h) {
  using namespace mapview;
  if (!s_mark_layer) return;
  if (s_view_ok && (s_view_left != left || s_view_top != top || s_view_z != _map_z) && flowArea(s_flow_area))
    lv_obj_invalidate(s_mark_layer);   // the view moved under live flows
  s_view_left = left; s_view_top = top; s_view_z = _map_z; s_view_ok = true;
  const int half = MARK_D / 2;
  const int lh = lv_font_get_line_height(THEME_FONT_SMALL) + 2;
  struct Box { int16_t x1, y1, x2, y2; };
  static PSRAM_BSS Box used[6 + MAX_MARKS + MARK_LABELS_MAX];   // the map's controls, the dots, the names
  int nu = 0;
  const int bottom = mapBottomH();   // the target's bar, or the route's
  const int16_t W = (int16_t)w, H = (int16_t)(h - bottom);
  used[nu++] = { 0, 0, 50, 144 };                                      // button columns
  used[nu++] = { (int16_t)(W - 50), 0, W, 144 };
  used[nu++] = { 0, 0, W, 36 };                                        // the going pills
  used[nu++] = { 0, (int16_t)(H - 34), 180, (int16_t)h };              // credit / scale
  used[nu++] = { (int16_t)(W - 50), (int16_t)(H - 50), W, (int16_t)h };   // centre button
  if (bottom) used[nu++] = { 0, H, W, (int16_t)h };

  for (int k = 0; k < s_mark_count; k++) {
    Mark& m = s_marks[k];
    double x = lonToTileX(m.lon_e6 / 1e6, _map_z) * TILE_PX - left;
    double y = latToTileY(m.lat_e6 / 1e6, _map_z) * TILE_PX - top;
    m.vis = x > -half - 4 && x < w + half + 4 && y > -half - 4 && y < h + half + 4;
    m.lw = 0;
    m.order = 0;
    if (_route_pick && m.vis && m.kind == MK_NODE && m.idx < _nearby->count()) {
      const NearbyModel::Entry& ne = _nearby->at(m.idx);
      int at = ne.has_key ? _core->route.indexOf(ne.pub_key) : -1;
      if (at >= 0) m.order = (uint8_t)(at + 1);
    }
    if (_route_show && m.vis && m.kind == MK_NODE && m.idx < _nearby->count()) {   // a message's route: its hops numbered
      const NearbyModel::Entry& ne = _nearby->at(m.idx);
      for (int i = 0; ne.has_key && i < s_route_n; i++)
        if (s_route_hop[i] >= 0 && memcmp(s_route_key[i], ne.pub_key, 4) == 0) { m.order = (uint8_t)(s_route_hop[i] + 1); break; }
    }
    if (!m.vis) continue;
    m.x = (int16_t)lround(x);
    m.y = (int16_t)lround(y);
    used[nu++] = { (int16_t)(m.x - half), (int16_t)(m.y - half), (int16_t)(m.x + half), (int16_t)(m.y + half) };
  }
  auto clearOf = [&](int x1, int y1, int x2, int y2) {
    if (x1 < 2 || y1 < 2 || x2 > w - 2 || y2 > h - 2) return false;
    for (int i = 0; i < nu; i++)
      if (x1 <= used[i].x2 && x2 >= used[i].x1 && y1 <= used[i].y2 && y2 >= used[i].y1) return false;
    return true;
  };
  int named = 0;
  for (int k = 0; k < s_mark_count && named < MARK_LABELS_MAX; k++) {
    Mark& m = s_marks[k];
    if (!m.vis || !m.name[0]) continue;
    int lw = m.tw + 14;
    const struct { int x, y; } at[4] = {
      { m.x + half + 3, m.y - lh / 2 }, { m.x - half - 3 - lw, m.y - lh / 2 },
      { m.x - lw / 2, m.y - half - 2 - lh }, { m.x - lw / 2, m.y + half + 2 } };
    for (const auto& c : at) {
      if (!clearOf(c.x, c.y, c.x + lw - 1, c.y + lh - 1)) continue;
      m.lx = (int16_t)c.x; m.ly = (int16_t)c.y; m.lw = (int16_t)lw; m.lh = (int16_t)lh;
      used[nu++] = { m.lx, m.ly, (int16_t)(m.lx + lw - 1), (int16_t)(m.ly + lh - 1) };
      named++;
      break;
    }
  }

  // What changed since the last layout, repainted (a pan changes all: the layer at once).
  lv_area_t la;
  lv_obj_get_coords(s_mark_layer, &la);
  auto bbox = [&](int x, int y, int lx, int ly, int lw, lv_area_t& a) {
    int x1 = x - half, y1 = y - half, x2 = x + half, y2 = y + half;
    if (lw) { x1 = min(x1, lx); y1 = min(y1, ly); x2 = max(x2, lx + lw); y2 = max(y2, ly + lh); }
    a = { la.x1 + x1 - 1, la.y1 + y1 - 1, la.x1 + x2 + 1, la.y1 + y2 + 1 };
  };
  s_route_on = _route_pick || _route_show;
  if (s_route_on) {   // the route's points, in px (any distance away: the line is clipped by the layer)
    for (int i = 0; i < s_route_n; i++) {
      double rx = lonToTileX(s_route_lon[i] / 1e6, _map_z) * TILE_PX - left;
      double ry = latToTileY(s_route_lat[i] / 1e6, _map_z) * TILE_PX - top;
      s_route_px[i] = { (lv_value_precise_t)lround(max(-30000.0, min(30000.0, rx))), (lv_value_precise_t)lround(max(-30000.0, min(30000.0, ry))) };
    }
    lv_obj_invalidate(s_mark_layer);   // the line may have moved: a pan repaints it anyway
  }
  int changed = 0;
  const int n = max(s_mark_count, s_sig_n);
  for (int k = 0; k < n; k++) {
    bool have = k < s_mark_count, had = k < s_sig_n;
    const Mark* m = have ? &s_marks[k] : nullptr;
    MarkSig* g = had ? &s_sigs[k] : nullptr;
    bool now_vis = have && m->vis, was_vis = had && g->vis;
    bool same = now_vis == was_vis && (!now_vis || (m->x == g->x && m->y == g->y && m->lx == g->lx && m->ly == g->ly &&
                                                    m->lw == g->lw && m->col == g->col && m->text_hash == g->hash && m->order == g->order));
    if (!same) changed++;
  }
  if (changed > 6) {
    lv_obj_invalidate(s_mark_layer);
  } else if (changed) {
    for (int k = 0; k < n; k++) {
      bool have = k < s_mark_count, had = k < s_sig_n;
      const Mark* m = have ? &s_marks[k] : nullptr;
      MarkSig* g = had ? &s_sigs[k] : nullptr;
      bool now_vis = have && m->vis, was_vis = had && g->vis;
      if (now_vis == was_vis && (!now_vis || (m->x == g->x && m->y == g->y && m->lx == g->lx && m->ly == g->ly &&
                                              m->lw == g->lw && m->col == g->col && m->text_hash == g->hash && m->order == g->order))) continue;
      lv_area_t a;
      if (was_vis) { bbox(g->x, g->y, g->lx, g->ly, g->lw, a); lv_obj_invalidate_area(s_mark_layer, &a); }
      if (now_vis) { bbox(m->x, m->y, m->lx, m->ly, m->lw, a); lv_obj_invalidate_area(s_mark_layer, &a); }
    }
  }
  for (int k = 0; k < s_mark_count; k++) {
    const Mark& m = s_marks[k];
    s_sigs[k] = { m.x, m.y, m.lx, m.ly, m.lw, m.col, m.text_hash, m.order, m.vis };
  }
  s_sig_n = s_mark_count;
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
  if (!ensureGps() || !_core->course.currentLocation(lat, lon)) { _map_follow = true; followShown(_map_center_btn, true); return; }   // centres once a fix comes
  _map_follow = true;
  _map_cx = mapview::lonToTileX(lon / 1e6, _map_z);
  _map_cy = mapview::latToTileY(lat / 1e6, _map_z) + mapCenterBias();
  layoutMap();
}

void UITask::mapOpenMarker(int idx) {
  if (idx < 0 || idx >= mapview::s_mark_count) return;
  const mapview::Mark& m = mapview::s_marks[idx];
  if (_route_pick) {   // Route > Map: a repeater tapped in (or out again)
    if (m.kind != mapview::MK_NODE || m.idx >= _nearby->count()) return;
    const NearbyModel::Entry& e = _nearby->at(m.idx);
    if (!e.has_key) { showToast("Pick repeaters"); return; }
    routeMapTap(e.pub_key, e.type);
    return;
  }
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
  if (_map_nav && !_route_pick) navDropAt(x, y);
}


// Packet flow: while flows move, the area they cover (and covered) is
// repainted ~20 times a second; nothing at all when none is live.
void UITask::mapFlowTick() {
  using namespace mapview;
  if (!s_mark_layer || (int32_t)(millis() - _next_flow_ms) < 0) return;
  _next_flow_ms = millis() + 50;
  _core->flow.tick();   // Play all: the next one when it's due
  if (++_flow_legend_tick >= 10) { _flow_legend_tick = 0; flowLegendRefresh(); }   // twice a second
  lv_area_t r;
  bool now_on = flowArea(r);
  if (!now_on && !s_flow_area_on) return;
  lv_area_t la;
  lv_obj_get_coords(s_mark_layer, &la);
  auto inval = [&](const lv_area_t& b) {
    lv_area_t x = { la.x1 + b.x1, la.y1 + b.y1, la.x1 + b.x2, la.y1 + b.y2 };
    lv_obj_invalidate_area(s_mark_layer, &x);
  };
  if (s_flow_area_on) inval(s_flow_area);
  if (now_on) inval(r);
  s_flow_area = r;
  s_flow_area_on = now_on;
}

// ── Recent packets (PacketFlow history) ──────────────────────────────────────

static void onFlowReplay(lv_event_t* e) { s_ui->flowReplay((int)(intptr_t)lv_event_get_user_data(e)); }
static void onFlowReplayAll(lv_event_t* e) { (void)e; s_ui->flowReplay(-1); }

// The flow legend's tap: the last packets heard with a line, newest first --
// what, from whom, when, how far it came; a tap plays one again on the map.
void UITask::flowHistoryPopup() {
  using namespace mapview;
  const PacketFlow& F = _core->flow;
  lv_obj_t* panel = navPopupPanel("Recent packets", true);
  lv_obj_t* list = scrollList(panel);
  if (F.historyCount() == 0) {
    noteLabel(list, "None yet: a packet is kept when its sender or a repeater on its way has a position.");
    return;
  }
  lv_obj_t* g = group(list, nullptr);
  for (int i = 0; i < F.historyCount(); i++) {
    const PacketFlow::Heard& h = F.history(i);
    const PacketFlow::Flow f = F.wayNow(h, 0);
    char title[64], sub[64];
    ContactInfo c;
    const bool named = f.node[0] >= 0 && the_mesh.getContactByIdx(f.node[0], c) && c.name[0];
    snprintf(title, sizeof(title), "%s%s%s%.24s", FLOW_KINDS[h.kind < PacketFlow::K_COUNT ? h.kind : PacketFlow::K_OTHER],
             named ? "  " : "", named && !f.origin ? "via " : "", named ? c.name : "");
    const uint32_t t = h.ts + (int32_t)_prefs->tz_offset_hours * 3600;
    int o = snprintf(sub, sizeof(sub), "%02u:%02u:%02u  ", (unsigned)(t / 3600 % 24), (unsigned)(t / 60 % 60), (unsigned)(t % 60));
    if (h.hops == 0) o += snprintf(sub + o, sizeof(sub) - o, "direct");
    else             o += snprintf(sub + o, sizeof(sub) - o, "%u hop%s", (unsigned)h.hops, h.hops == 1 ? "" : "s");
    if (h.heard > 1) snprintf(sub + o, sizeof(sub) - o, ", heard %u times", (unsigned)h.heard);
    lv_obj_t* row = groupLine(g, true);
    lv_obj_t* dot = lv_obj_create(row);   // its line's colour on the map
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 11, 11);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(flowColour(h.kind)), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    groupText(row, title, sub);
    label(row, LV_SYMBOL_PLAY, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_obj_add_event_cb(row, onFlowReplay, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  lv_obj_t* acts = buttonBar(list);
  barButton(acts, LV_SYMBOL_PLAY " Play all", onFlowReplayAll, 0, true);
}

// Packet i of the history (-1: all, one after another) on the map again; the
// map moves to take it in when part of it is off screen.
void UITask::flowReplay(int i) {
  using namespace mapview;
  PacketFlow& F = _core->flow;
  navClosePopup();
  if (i >= F.historyCount()) return;
  bool any = false, off = false;
  int32_t la0 = 0, la1 = 0, lo0 = 0, lo1 = 0;
  const int w = _map_area ? lv_obj_get_width(_map_area) : 0, h = _map_area ? lv_obj_get_height(_map_area) - mapBottomH() : 0;
  for (int j = i < 0 ? 0 : i; j < (i < 0 ? F.historyCount() : i + 1); j++) {
    const PacketFlow::Heard& p = F.history(j);
    for (int wy = 0; wy < p.ways; wy++) {
      const PacketFlow::Flow f = F.wayNow(p, wy);
      for (int k = 0; k < f.n; k++) {
        int32_t lat, lon;
        F.pointNow(f, k, lat, lon);
        if (!any) { la0 = la1 = lat; lo0 = lo1 = lon; any = true; }
        la0 = min(la0, lat); la1 = max(la1, lat); lo0 = min(lo0, lon); lo1 = max(lo1, lon);
        int32_t x, y;
        flowPx(lat, lon, x, y);
        if (!s_view_ok || x < 12 || x > w - 12 || y < 12 || y > h - 12) off = true;
      }
    }
  }
  if (any && off) mapFit(la0, la1, lo0, lo1);
  if (i < 0) F.replayAll();
  else       F.replay(i);
}

// ── Picking a route (Route > Map) ─────────────────────────────────────────────

static void onRouteUndo(lv_event_t* e) { (void)e; s_ui->routeMapUndo(); }
static void onRouteDone(lv_event_t* e) { (void)e; if (s_ui->routeShowing()) s_ui->routeShowDone(); else s_ui->routeMapDone(); }

// A bar over the nav bar: the route so far, Undo (last hop out) and Done.
void UITask::buildRouteBar(lv_obj_t* body) {
  _route_bar = lv_obj_create(body);
  lv_obj_set_size(_route_bar, LV_PCT(100), navmap::BAR_H);
  lv_obj_align(_route_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_remove_flag(_route_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(_route_bar, 0, 0);
  lv_obj_set_style_bg_color(_route_bar, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(_route_bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(_route_bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(_route_bar, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(_route_bar, 2, 0);
  lv_obj_set_style_pad_all(_route_bar, 4, 0);
  _route_lbl = label(_route_bar, "", THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(_route_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_route_lbl, 150);
  lv_obj_align(_route_lbl, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_t* done = lv_button_create(_route_bar);
  lv_obj_set_size(done, 70, 34);
  lv_obj_align(done, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_shadow_width(done, 0, 0);
  lv_obj_set_style_radius(done, theme::RADIUS, 0);
  lv_obj_add_event_cb(done, onRouteDone, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(done, LV_SYMBOL_OK " Done", THEME_FONT_SMALL, theme::TEXT));
  stylePrimary(done);
  if (_route_show) {   // only looked at: the label has the room Undo would take
    lv_obj_set_width(_route_lbl, lv_pct(75));
    lv_label_set_long_mode(_route_lbl, LV_LABEL_LONG_DOT);
    routeMapRefresh();
    return;
  }
  lv_obj_t* undo = lv_button_create(_route_bar);
  lv_obj_set_size(undo, 64, 34);
  lv_obj_align(undo, LV_ALIGN_RIGHT_MID, -76, 0);
  lv_obj_set_style_shadow_width(undo, 0, 0);
  lv_obj_set_style_radius(undo, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(undo, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(undo, onRouteUndo, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(undo, LV_SYMBOL_CLOSE " Undo", THEME_FONT_SMALL, theme::TEXT));
  routeMapRefresh();
}

// Done (or Back) on a message's route: back to the conversation.
void UITask::routeShowDone() {
  _route_show = false;
  _core->hop_route.close();
  _screen = SCR_THREAD;
  _nav_back = true;
  buildThread();
  _nav_back = false;
}

void UITask::routeMapUndo() {
  _core->route.removeLast();
  routeMapRefresh();
}

// The route changed: its points for the line, the text, the markers.
void UITask::routeMapRefresh() {
  using namespace mapview;
  s_route_n = 0;
  if (_route_show) {   // a message's route: here, its hops placed, the sender
    const HopRoute& H = _core->hop_route;
    for (int i = 0; i < H.points() && s_route_n < ROUTE_PTS; i++) {
      const HopRoute::Pt& p = H.point(i);
      s_route_lat[s_route_n] = p.lat; s_route_lon[s_route_n] = p.lon;
      s_route_hop[s_route_n] = p.hop;
      ContactInfo c;
      if (p.node >= 0 && the_mesh.getContactByIdx(p.node, c)) memcpy(s_route_key[s_route_n], c.id.pub_key, 4);
      else s_route_hop[s_route_n] = -1;
      s_route_n++;
    }
    if (_route_lbl) {
      char t[128], nm[33];
      int o = 0;
      H.summary(t, sizeof(t));
      o = strlen(t);
      for (int i = 0; i < H.hops() && o < (int)sizeof(t) - 8; i++) {
        H.hopName(i, nm, sizeof(nm));
        o += snprintf(t + o, sizeof(t) - o, "%s%.10s", i ? " > " : ": ", nm);
      }
      lv_label_set_text(_route_lbl, t);
    }
    if (_map_area) layoutMap();
    return;
  }
  PathEditor& R = _core->route;
  auto add = [&](int32_t lat, int32_t lon) {
    if ((lat != 0 || lon != 0) && s_route_n < ROUTE_PTS) { s_route_lat[s_route_n] = lat; s_route_lon[s_route_n] = lon; s_route_n++; }
  };
  int32_t mlat, mlon;
  if (_core->course.currentLocation(mlat, mlon)) add(mlat, mlon);
  for (int i = 0; i < R.count(); i++) {
    if (R.hop(i).known < 4) continue;
    ContactInfo* c = the_mesh.lookupContactByPubKey(R.hop(i).key, 4);
    if (c) add(c->gps_lat, c->gps_lon);
  }
  ContactInfo* d = the_mesh.lookupContactByPubKey(R.targetKey(), PUB_KEY_SIZE);
  if (d) add(d->gps_lat, d->gps_lon);
  if (_route_lbl) {
    char t[96];
    int o = snprintf(t, sizeof(t), "%d hop%s: ", R.count(), R.count() == 1 ? "" : "s");
    for (int i = 0; i < R.count() && o < (int)sizeof(t) - 8; i++)
      o += snprintf(t + o, sizeof(t) - o, "%s%.10s", i ? " > " : "", R.hop(i).name);
    if (R.count() == 0) snprintf(t, sizeof(t), "Tap the repeaters in order, towards %.12s", R.targetName());
    lv_label_set_text(_route_lbl, t);
  }
  if (_map_area) layoutMap();
}

// Frames this device and the node, so the hops between them are on screen.
void UITask::routeMapFrame() {
  using namespace mapview;
  routeMapRefresh();
  if (s_route_n == 0 || !_map_area) return;
  int32_t la0 = s_route_lat[0], la1 = s_route_lat[0], lo0 = s_route_lon[0], lo1 = s_route_lon[0];
  for (int i = 1; i < s_route_n; i++) {
    la0 = min(la0, s_route_lat[i]); la1 = max(la1, s_route_lat[i]);
    lo0 = min(lo0, s_route_lon[i]); lo1 = max(lo1, s_route_lon[i]);
  }
  mapFit(la0, la1, lo0, lo1);
}

// The map on a box (lat la0..la1, lon lo0..lo1): the closest zoom it fits in
// with a margin, above the bottom bar.
void UITask::mapFit(int32_t la0, int32_t la1, int32_t lo0, int32_t lo1) {
  using namespace mapview;
  if (!_map_area) return;
  int w = lv_obj_get_width(_map_area), h = lv_obj_get_height(_map_area) - mapBottomH();
  int z = MAX_Z;
  for (; z > MIN_Z; z--) {
    double dx = fabs(lonToTileX(lo1 / 1e6, z) - lonToTileX(lo0 / 1e6, z)) * TILE_PX;
    double dy = fabs(latToTileY(la0 / 1e6, z) - latToTileY(la1 / 1e6, z)) * TILE_PX;
    if (dx < w * 0.7 && dy < h * 0.6) break;
  }
  _map_z = z;
  _map_follow = false;
  _map_cx = lonToTileX((lo0 + lo1) / 2e6, z);
  _map_cy = latToTileY((la0 + la1) / 2e6, z) + mapCenterBias();
  layoutMap();
}


// ── Area download ─────────────────────────────────────────────────────────────

// The download button: progress / an unfinished job in a popup, else the
// frame to pick a new area (MapAreas.h), which sets the zoom range and shows
// the size.
void UITask::mapDownloadPopup() {
  if (_dl_overlay) return;
  mapview::TileArea job;
  if (!mapview::s_dl.active() && !mapview::s_dl.savedJob(job)) { areaSelectBegin(); return; }

  lv_obj_t* panel = popupOpen(screen(), POP_FIT, _dl_overlay);
  lv_obj_set_style_pad_row(panel, 6, 0);

  // The title in the column, the close button floating over its right end:
  // no box of their own to lay out and draw.
  lv_obj_set_style_pad_top(label(panel, "Map download", THEME_FONT_TITLE, theme::TEXT), 4, 0);
  lv_obj_t* x = headerButton(panel, LV_SYMBOL_CLOSE, onDlClose, 0, NULL);
  lv_obj_add_flag(x, LV_OBJ_FLAG_FLOATING);
  lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 0);

  // Unfinished job (power-off, lost WiFi, Stop): resume or drop it.
  _dl_job_row = lv_obj_create(panel);
  styleSurface(_dl_job_row, theme::SURFACE);
  lv_obj_remove_flag(_dl_job_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(_dl_job_row, LV_PCT(100), 44);
  lv_obj_set_style_radius(_dl_job_row, theme::RADIUS, 0);
  _dl_job_lbl = label(_dl_job_row, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_set_width(_dl_job_lbl, 150);
  lv_label_set_long_mode(_dl_job_lbl, LV_LABEL_LONG_WRAP);
  lv_obj_align(_dl_job_lbl, LV_ALIGN_LEFT_MID, theme::PAD, 0);
  stylePrimary(headerButton(_dl_job_row, LV_SYMBOL_PLAY " Resume", onDlResume, 48, NULL));
  headerButton(_dl_job_row, LV_SYMBOL_TRASH, onDlDiscard, 4, NULL);
  lv_obj_add_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);

  _dl_info = noteLabel(panel, "", THEME_FONT_BODY, theme::TEXT);   // what's going on
  _dl_bar = lv_bar_create(panel);
  lv_obj_set_size(_dl_bar, LV_PCT(100), 6);
  lv_obj_set_style_bg_color(_dl_bar, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_bg_color(_dl_bar, lv_color_hex(theme::ACCENT), LV_PART_INDICATOR);
  _dl_sub = noteLabel(panel, "");                                      // the details, muted
  _dl_err = noteLabel(panel, "", THEME_FONT_SMALL, theme::FAIL);      // tiles missing / WiFi

  lv_obj_t* go = barButton(buttonBar(panel), "", onDlStart, 0, true);
  _dl_start_lbl = lv_obj_get_child(go, 0);
  stylePrimary(go);

  refreshDownloadPopup();
}

void UITask::mapDownloadClose() {
  if (_dl_overlay) lv_obj_delete_async(_dl_overlay);   // may be closing from its own button
  _dl_overlay = _dl_info = _dl_bar = _dl_sub = _dl_err = _dl_start_lbl = _dl_job_row = _dl_job_lbl = nullptr;
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

static int dlPercent(const mapview::TileDownloader& dl) {
  return dl.total() ? (int)((uint64_t)dl.processed() * 100 / dl.total()) : 0;
}

// A label's text, or the label hidden when there's none.
static void textOrHide(lv_obj_t* l, const char* text) {
  if (!text || !text[0]) { lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
  setText(l, text);
}

void UITask::refreshDownloadPopup() {
  if (!_dl_info) return;
  using mapview::TileDownloader;
  TileDownloader& dl = mapview::s_dl;
  const char* host = shortHost(dl.sourceHost());
  char info[64] = "", sub[96] = "", err[96] = "";
  if (dl.active()) {
    lv_obj_add_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);
    bool connecting = dl.state() == TileDownloader::CONNECTING;
    if (connecting) {
      snprintf(info, sizeof(info), "Connecting to WiFi...");
    } else {
      snprintf(info, sizeof(info), "Downloading %d%%", dlPercent(dl));
      snprintf(sub, sizeof(sub), "%lu new tiles from %s\nCarries on in the background", (unsigned long)dl.downloaded(), host);
    }
    if (dl.retryRound())
      snprintf(err, sizeof(err), "Trying %lu missing tiles again (%d of %d)", (unsigned long)dl.failed(),
               dl.retryRound(), TileDownloader::RETRY_ROUNDS);
    else if (dl.failed())
      snprintf(err, sizeof(err), "%lu missing, tried again at the end (%s)", (unsigned long)dl.failed(), dl.message());
    uint32_t total = dl.total();
    lv_bar_set_range(_dl_bar, 0, total > 0 ? (int32_t)total : 1);
    lv_bar_set_value(_dl_bar, (int32_t)dl.processed(), LV_ANIM_OFF);
    if (connecting) lv_obj_add_flag(_dl_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(_dl_bar, LV_OBJ_FLAG_HIDDEN);
    setText(_dl_start_lbl, LV_SYMBOL_STOP " Stop");
  } else {
    lv_obj_add_flag(_dl_bar, LV_OBJ_FLAG_HIDDEN);
    mapview::TileArea job;
    bool has_job = dl.savedJob(job);
    if (has_job) {
      setTextFmt(_dl_job_lbl, "Unfinished: z%d-%d,\n%lu tiles", job.zmin, job.zmax, (unsigned long)mapview::countTiles(job));
      lv_obj_remove_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(_dl_job_row, LV_OBJ_FLAG_HIDDEN);
    }
    uint8_t st = dl.state();
    if (st == TileDownloader::DONE || st == TileDownloader::FAILED || st == TileDownloader::CANCELLED)
      snprintf(sub, sizeof(sub), "Last download: %s, %lu new tiles", dl.message(), (unsigned long)dl.downloaded());
    // The network itself is set up in Settings > WiFi only.
    char ssid[33], pass[65];
    if (!lvport::wifiAllowed()) snprintf(err, sizeof(err), "WiFi is off - Settings > WiFi");
    else if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) snprintf(err, sizeof(err), "No WiFi network - Settings > WiFi");
    setText(_dl_start_lbl, LV_SYMBOL_PLUS " New area");
  }
  textOrHide(_dl_info, info);
  textOrHide(_dl_sub, sub);
  textOrHide(_dl_err, err);
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

// Map tools > Map: another base layer, its tiles from its own folder.
void UITask::setMapLayer(int i) {
  if (!mapview::applyLayer(i)) { showToast("Wait for the download to finish"); return; }
  lvport::setMapLayer(i);
  mapLiveBegin();   // live tiles: from the new layer's server
  mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
  if (_screen == SCR_MAP) layoutMap();
  if (!lvport::liveTiles() || !lvport::wifiAllowed())
    showToast("Shows what is downloaded for this map - or turn Live tiles on", 3000);
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
    if (st != mapview::TileDownloader::CANCELLED && !_dl_overlay) {   // Stop was theirs; the popup says it itself
      char t[64];
      snprintf(t, sizeof(t), "Map download: %s, %lu new tiles", dl.message(), (unsigned long)dl.downloaded());
      showToast(t, 4000);
    }
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
      if (dl.retryRound())   // the count stands still: the tiles still missing instead
        setTextFmt(_map_dl_pill, LV_SYMBOL_REFRESH " %lu missing", (unsigned long)dl.failed());
      else if (dl.failed())
        setTextFmt(_map_dl_pill, LV_SYMBOL_DOWNLOAD " %d%%  " LV_SYMBOL_WARNING " %lu", dlPercent(dl), (unsigned long)dl.failed());
      else
        setTextFmt(_map_dl_pill, LV_SYMBOL_DOWNLOAD " %d%%", dlPercent(dl));
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
