#pragma once
// Labels of the vector map: named points (places, peaks with their height,
// huts, springs...) from the {dz}/{x}/{y}.vp files beside the vector tiles
// (tools/maps/osm_vector.py). Drawn over the tiles, not into them, so a name
// is never cut at a tile edge and stays crisp at any zoom: a layer under the
// markers, placed again on every layout -- by priority (towns, the highest
// peaks, huts first), each where it doesn't cover one already placed or the
// map's controls. No street names (the raster map has those).
//
// Files are read one per map loop pass while the map rests (they're small),
// never inside a layout, so panning doesn't wait for the card.
//
// Single-TU fragment: included by ui-lvgl/MapScreen.h after VectorTileProvider.h.

namespace mapview {
namespace labels {

enum : uint8_t { T_TOWN = 60, T_VILLAGE, T_HUT, T_PEAK, T_LAKE, T_HAMLET, T_PASS, T_SHELTER, T_SPRING, T_VIEW, T_CAVE };
static const int LABEL_MAX = 40;
static const int16_t NO_ELE = -32768;

struct Poi { uint8_t cls; int16_t ele; uint16_t u, v; char name[LABEL_MAX + 1]; };

// Points of the data tiles read last (PSRAM), least recently used replaced.
struct PoiTile { int dz = -1, x = 0, y = 0; Poi* p = nullptr; int n = 0, cap = 0; uint32_t used = 0; };
static const int SLOTS = 12;
static PoiTile s_tiles[SLOTS];
static uint32_t s_tick = 0;

static PoiTile* find(int dz, int x, int y) {
  for (PoiTile& t : s_tiles) if (t.dz == dz && t.x == x && t.y == y) { t.used = ++s_tick; return &t; }
  return nullptr;
}

// Reads a tile's points (none if it has no file: remembered too).
static void load(int dz, int x, int y) {
  PoiTile* t = &s_tiles[0];
  for (PoiTile& s : s_tiles) if (s.used < t->used) t = &s;
  t->dz = dz; t->x = x; t->y = y; t->n = 0; t->used = ++s_tick;
  VectorPacks::Src src;
  if (!s_vpacks.find(VectorPacks::K_POINTS, dz, x, y, src)) return;
  uint8_t* buf = src.len >= 6 && src.len < 256 * 1024 ? psramBuf<uint8_t>(src.len) : nullptr;
  bool ok = buf && readFast(src.f, buf, src.len);
  s_vpacks.done(src);
  if (!ok || memcmp(buf, "VP1", 3) != 0) { free(buf); return; }
  const uint8_t* q = buf + 6;
  const uint8_t* end = buf + src.len;
  int count = buf[4] | (buf[5] << 8);
  if (count > t->cap) {
    free(t->p);
    t->p = psramBuf<Poi>(count);
    t->cap = t->p ? count : 0;
  }
  while (t->n < t->cap && t->n < count && q + 8 <= end) {
    Poi& p = t->p[t->n];
    p.cls = q[0];
    p.u = q[1] | (q[2] << 8);
    p.v = q[3] | (q[4] << 8);
    p.ele = (int16_t)(q[5] | (q[6] << 8));
    int nl = q[7];
    q += 8;
    if (q + nl > end) break;
    int len = nl > LABEL_MAX ? LABEL_MAX : nl;
    memcpy(p.name, q, len);
    p.name[len] = '\0';
    q += nl;
    t->n++;
  }
  free(buf);
}

// What layout() placed, for the draw callback (names copied: a later read
// may reuse the tile's memory before LVGL draws).
struct Placed {
  int16_t px, py;   // the point, layer px
  int16_t tx, ty;   // the text's top-left
  uint8_t cls;
  char text[LABEL_MAX + 8];
};
static const int MAX_PLACED = 40;
static Placed* s_placed = psramBuf<Placed>(MAX_PLACED);
static int s_n = 0;
static int s_want_dz = -1, s_want_x = 0, s_want_y = 0;   // a tile to read next, dz -1: none
static lv_obj_t* s_layer = nullptr;

static const lv_font_t* fontFor(uint8_t cls) { return cls == T_TOWN || cls == T_VILLAGE ? THEME_FONT_BODY : THEME_FONT_SMALL; }
static uint32_t colourFor(uint8_t cls) {
  switch (cls) {
    case T_LAKE: case T_SPRING: return 0x2A6A9A;
    case T_HUT: case T_SHELTER: return 0x8A2A1A;
    case T_PEAK: case T_PASS:   return 0x5A3A20;
    case T_HAMLET:              return 0x55524C;
  }
  return 0x2A2824;
}
static bool hasIcon(uint8_t cls) { return cls >= T_HUT && cls != T_LAKE && cls != T_HAMLET; }

// Candidates of the view, highest priority first.
struct Cand { const Poi* p; int16_t x, y; };
static const int MAX_CAND = 512;
static Cand* s_cand = psramBuf<Cand>(MAX_CAND);

// Placing order: places, then peaks (the highest first), then huts... -- in
// the mountains the peaks are what you find your way by.
static int priority(uint8_t cls) { return cls == T_PEAK ? 2 * T_VILLAGE + 1 : cls == T_HUT ? 2 * T_VILLAGE + 2 : 2 * cls; }
static int cmpCand(const void* a, const void* b) {
  const Poi* p = ((const Cand*)a)->p;
  const Poi* q = ((const Cand*)b)->p;
  if (p->cls != q->cls) return priority(p->cls) - priority(q->cls);
  return q->ele - p->ele;   // higher peaks first
}

static void clear() {
  s_n = 0;
  s_want_dz = -1;
}

// Places the labels for a view (world px of its top-left at zoom z, size;
// `bottom` px taken by a bar along the bottom).
static void layout(double left, double top, int w, int h, int z, bool on, int bottom) {
  int had = s_n;
  clear();
  if (!on || z < 10 || !s_layer || !s_placed || !s_cand) {   // none: those shown go (none shown, nothing to redraw)
    if (s_layer && had) lv_obj_invalidate(s_layer);
    return;
  }
  int dz = z >= 14 ? 14 : z >= 12 ? 12 : 10, k = z - dz;
  double span = (double)(TILE_PX << k);   // a data tile, in px at zoom z
  int x0 = (int)floor(left / span), x1 = (int)floor((left + w) / span);
  int y0 = (int)floor(top / span), y1 = (int)floor((top + h) / span);
  int nc = 0;
  for (int ty = y0; ty <= y1; ty++)
    for (int tx = x0; tx <= x1; tx++) {
      PoiTile* t = find(dz, tx, ty);
      if (!t) {   // read in mapLoop, one per pass
        if (s_want_dz < 0) { s_want_dz = dz; s_want_x = tx; s_want_y = ty; }
        continue;
      }
      for (int i = 0; i < t->n && nc < MAX_CAND; i++) {
        const Poi& p = t->p[i];
        double px = (tx + p.u / 4096.0) * span - left, py = (ty + p.v / 4096.0) * span - top;
        if (px < 0 || py < 0 || px >= w || py >= h) continue;
        s_cand[nc++] = { &p, (int16_t)lround(px), (int16_t)lround(py) };
      }
    }
  qsort(s_cand, nc, sizeof(Cand), cmpCand);

  // Greedy placement: the icon, then the text right / left / above / below it.
  struct Box { int16_t x1, y1, x2, y2; };
  static Box used[MAX_PLACED * 2 + 8];
  int nu = 0;
  // The map's controls: button columns, the zoom pill between them, scale /
  // credit and the centre button above the bottom bar, the bar.
  const int16_t W = (int16_t)w, H = (int16_t)(h - bottom);
  used[nu++] = { 0, 0, 50, 144 };
  used[nu++] = { (int16_t)(W - 50), 0, W, 144 };
  used[nu++] = { 0, 0, W, 36 };
  used[nu++] = { 0, (int16_t)(H - 34), 180, (int16_t)h };
  used[nu++] = { (int16_t)(W - 50), (int16_t)(H - 50), W, (int16_t)h };
  if (bottom) used[nu++] = { 0, H, W, (int16_t)h };
  auto clearOf = [&](const Box& b) {
    if (b.x1 < 2 || b.y1 < 2 || b.x2 > w - 2 || b.y2 > h - 2) return false;
    for (int i = 0; i < nu; i++)
      if (b.x1 <= used[i].x2 && b.x2 >= used[i].x1 && b.y1 <= used[i].y2 && b.y2 >= used[i].y1) return false;
    return true;
  };
  for (int c = 0; c < nc && s_n < MAX_PLACED; c++) {
    const Poi& p = *s_cand[c].p;
    int px = s_cand[c].x, py = s_cand[c].y;
    Placed& L = s_placed[s_n];
    if (p.name[0] && p.ele != NO_ELE && (p.cls == T_PEAK || p.cls == T_PASS)) snprintf(L.text, sizeof(L.text), "%s %d", p.name, p.ele);
    else if (p.name[0]) snprintf(L.text, sizeof(L.text), "%s", p.name);
    else if (p.ele != NO_ELE) snprintf(L.text, sizeof(L.text), "%d", p.ele);
    else continue;
    const lv_font_t* font = fontFor(p.cls);
    lv_point_t sz;
    lv_text_get_size(&sz, L.text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int tw = sz.x, th = sz.y;
    bool icon = hasIcon(p.cls);
    Box ib = { (int16_t)(px - 5), (int16_t)(py - 5), (int16_t)(px + 5), (int16_t)(py + 5) };
    if (icon && !clearOf(ib)) continue;
    struct { int x, y; } at[4];
    int na = 0;
    if (icon) {
      at[na++] = { px + 7, py - th / 2 };
      at[na++] = { px - 7 - tw, py - th / 2 };
      at[na++] = { px - tw / 2, py - 6 - th };
      at[na++] = { px - tw / 2, py + 6 };
    } else {
      at[na++] = { px - tw / 2, py - th / 2 };   // a place name sits on the place
    }
    int pick = -1;
    Box tb = {};
    for (int i = 0; i < na && pick < 0; i++) {
      tb = { (int16_t)(at[i].x - 1), (int16_t)(at[i].y), (int16_t)(at[i].x + tw + 1), (int16_t)(at[i].y + th - 1) };
      if (clearOf(tb)) pick = i;
    }
    if (pick < 0) {
      if (!icon) continue;
      if (p.cls != T_HUT && dz < 14) continue;   // far out: a bare icon is just clutter
      if (p.cls >= T_SPRING) continue;           // minor: no room, not shown
      L.text[0] = '\0';                          // the icon alone still tells there's a peak / hut
    }
    L.px = px; L.py = py;
    L.tx = pick < 0 ? 0 : at[pick].x;
    L.ty = pick < 0 ? 0 : at[pick].y;
    L.cls = p.cls;
    if (icon) used[nu++] = ib;
    if (pick >= 0) used[nu++] = tb;
    s_n++;
  }
  lv_obj_invalidate(s_layer);
}

// The packs changed: what was read (or found missing) goes.
static void flush() {
  for (PoiTile& t : s_tiles) t.dz = -1;
  clear();
}

// A tile layout() found missing, read now; true if one was.
static bool loadOne() {
  if (s_want_dz < 0) return false;
  load(s_want_dz, s_want_x, s_want_y);
  s_want_dz = -1;
  return true;
}

static void icon(lv_layer_t* layer, uint8_t cls, int32_t x, int32_t y) {
  lv_draw_rect_dsc_t r;
  lv_draw_rect_dsc_init(&r);
  r.bg_opa = LV_OPA_COVER;
  r.border_color = lv_color_hex(0xFFFFFF);
  r.border_opa = LV_OPA_COVER;
  lv_draw_triangle_dsc_t t;
  lv_draw_triangle_dsc_init(&t);
  t.opa = LV_OPA_COVER;
  lv_area_t a;
  auto tri = [&](uint32_t col, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    t.color = lv_color_hex(col);
    t.p[0] = { (lv_value_precise_t)x0, (lv_value_precise_t)y0 };
    t.p[1] = { (lv_value_precise_t)x1, (lv_value_precise_t)y1 };
    t.p[2] = { (lv_value_precise_t)x2, (lv_value_precise_t)y2 };
    lv_draw_triangle(layer, &t);
  };
  switch (cls) {
    case T_PEAK:   // a triangle with a light rim
      tri(0xFFFFFF, x, y - 6, x - 6, y + 4, x + 6, y + 4);
      tri(0x5A3A20, x, y - 4, x - 4, y + 3, x + 4, y + 3);
      return;
    case T_PASS:   // a short bar
      r.bg_color = lv_color_hex(0x5A3A20);
      r.border_width = 1;
      a = { x - 4, y - 2, x + 4, y + 1 };
      lv_draw_rect(layer, &r, &a);
      return;
    case T_HUT: case T_SHELTER: {   // a house: roof over a square
      uint32_t c = cls == T_HUT ? 0xC0301E : 0x8A5A2A;
      r.bg_color = lv_color_hex(c);
      r.border_width = 1;
      a = { x - 4, y - 1, x + 4, y + 5 };
      lv_draw_rect(layer, &r, &a);
      tri(c, x, y - 6, x - 6, y, x + 6, y);
      return;
    }
    case T_SPRING: case T_VIEW: case T_CAVE:
      r.bg_color = lv_color_hex(cls == T_SPRING ? 0x2A7AC8 : cls == T_VIEW ? 0xE08A1C : 0x302A26);
      r.radius = LV_RADIUS_CIRCLE;
      r.border_width = 1;
      a = { x - 3, y - 3, x + 3, y + 3 };
      lv_draw_rect(layer, &r, &a);
      return;
  }
}

// LV_EVENT_DRAW_MAIN of the layer.
static void draw(lv_event_t* e) {
  if (!s_n) return;
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  for (int i = 0; i < s_n; i++) {
    const Placed& L = s_placed[i];
    if (hasIcon(L.cls)) icon(layer, L.cls, a.x1 + L.px, a.y1 + L.py);
    if (!L.text[0]) continue;
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = fontFor(L.cls);
    d.text = L.text;
    lv_area_t t = { a.x1 + L.tx, a.y1 + L.ty, a.x1 + L.tx + 400, a.y1 + L.ty + 40 };
    d.color = lv_color_hex(0xF7F4EE);   // a halo: the text in paper colour around it
    static const int8_t OFF[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (const auto& off : OFF) {
      lv_area_t h = t;
      lv_area_move(&h, off[0], off[1]);
      lv_draw_label(layer, &d, &h);
    }
    d.color = lv_color_hex(colourFor(L.cls));
    lv_draw_label(layer, &d, &t);
  }
}

static void buildLayer(lv_obj_t* body) {
  clear();
  s_layer = lv_obj_create(body);
  lv_obj_remove_style_all(s_layer);
  lv_obj_set_size(s_layer, LV_PCT(100), LV_PCT(100));
  lv_obj_remove_flag(s_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_layer, draw, LV_EVENT_DRAW_MAIN, NULL);
}

}  // namespace labels
}  // namespace mapview
