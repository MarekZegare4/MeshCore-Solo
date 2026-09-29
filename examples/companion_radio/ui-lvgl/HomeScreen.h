#pragma once
// Home, laid out like a phone's: pages side by side, swiped between, dots
// underneath. From the left:
//   favourites   -- the 3x2 card of favourite chats (DeviceScreen.h)
//   clock        -- where Home opens and the side button returns: the time
//                   (tap: Clock), the date, and three telemetry fields the
//                   user picks (hold one), as on L1 (NodePrefs::dashboard_fields)
//   map          -- a minimap round your position, framing anyone sharing
//                   theirs and the target; tap for the Navigation map
//   apps         -- the tools, 3x2 to a page, in the user's order with some
//                   maybe hidden (Settings > Home apps, or hold an app)
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after NavMap.h (the
// minimap draws with the map's tile cache).

#include <helpers/sensors/LPPDataHelpers.h>

namespace home {

enum : int { FAVS, CLOCK, MAP, APPS };   // pages; APPS and after: the apps, PER_PAGE each

// Each app has a letter of its own, so the saved order survives apps being
// added or moved in this list (a new one joins at the end, shown).
struct App { char id; const char* icon; const char* text; lv_event_cb_t cb; bool unread; };
static const App APP_LIST[] = {
  { 'M', LV_SYMBOL_ENVELOPE, "Messages",    onOpenChats,     true  },
  { 'N', UI_SYMBOL_USERS,    "Nodes",       onOpenNearby,    false },
  { 'S', LV_SYMBOL_SETTINGS, "Settings",    onOpenSettings,  false },
  { 'C', UI_SYMBOL_COMPASS,  "Compass",     onOpenCompass,   false },
  { 'K', UI_SYMBOL_CLOCK,    "Clock",       onOpenClock,     false },
  { 'G', LV_SYMBOL_GPS,      "GPS",         onOpenGps,       false },
  { 'B', LV_SYMBOL_CHARGE,   "Bot",         onOpenBot,       false },
  { 'R', LV_SYMBOL_LOOP,     "Repeater",    onOpenRepeater,  false },
  { 'A', UI_SYMBOL_KEY,      "Admin",       onOpenAdminPick, false },
  { 'D', UI_SYMBOL_CHART,    "Diagnostics", onOpenDiag,      false },
};
static const int APP_COUNT = sizeof(APP_LIST) / sizeof(APP_LIST[0]);
static const int PER_PAGE = 6;
static const char ALWAYS = 'S';   // Settings can't be hidden: it's the way back

// ── The user's order ──
// The letters in order, a hidden app's in lower case. Arranged on Home itself
// (hold an app, or Settings > Home apps); saved on Done or on leaving Home,
// not per change.
static char s_order[APP_COUNT + 1];
static bool s_order_loaded = false, s_order_dirty = false;
static bool s_edit = false;   // Home's apps being arranged (every app shown, hidden ones dimmed)

static int appById(char c) {
  c = toupper((unsigned char)c);
  for (int i = 0; i < APP_COUNT; i++) if (APP_LIST[i].id == c) return i;
  return -1;
}
static bool orderShown(int pos) { return isupper((unsigned char)s_order[pos]); }
static int orderApp(int pos) { return appById(s_order[pos]); }

static void loadOrder() {
  if (s_order_loaded) return;
  s_order_loaded = true;
  char saved[32];
  lvport::loadHomeApps(saved, sizeof(saved));
  int n = 0;
  for (const char* p = saved; *p && n < APP_COUNT; p++) {
    int i = appById(*p);
    if (i < 0 || memchr(s_order, APP_LIST[i].id, n) || memchr(s_order, tolower(APP_LIST[i].id), n)) continue;
    s_order[n++] = APP_LIST[i].id == ALWAYS ? ALWAYS : *p;
  }
  for (int i = 0; i < APP_COUNT; i++)
    if (!memchr(s_order, APP_LIST[i].id, n) && !memchr(s_order, tolower(APP_LIST[i].id), n)) s_order[n++] = APP_LIST[i].id;
  s_order[n] = '\0';
}
static void flushOrder() {
  if (!s_order_dirty) return;
  s_order_dirty = false;
  lvport::saveHomeApps(s_order);
}

static int shownCount() {
  loadOrder();
  int n = 0;
  for (int p = 0; p < APP_COUNT; p++) if (orderShown(p)) n++;
  return n;
}
// The k-th app shown on Home; -1 past the last.
static int shownApp(int k) {
  loadOrder();
  for (int p = 0; p < APP_COUNT; p++) if (orderShown(p) && k-- == 0) return orderApp(p);
  return -1;
}
static int pageCount() { return APPS + ((s_edit ? APP_COUNT : shownCount()) + PER_PAGE - 1) / PER_PAGE; }

static lv_obj_t* s_page_box = nullptr;   // the current page's content
static lv_obj_t* s_dots = nullptr;
static int s_page = CLOCK;   // kept across rebuilds (the home screen is rebuilt on return)
static int s_unread_sig = -1;   // unread total the favourites / apps page was drawn with

// A horizontal swipe anywhere on Home turns the page, tracked from the touch
// itself: LVGL's gesture detector drops a slow swipe (its sum resets on every
// read without movement). Tapping a dot also goes to that page.
static bool     s_touching = false, s_swiped = false;
static lv_point_t s_start;
static const int SWIPE_PX = 40;
static void onDot(lv_event_t* e) { s_ui->setHomePage((int)(uintptr_t)lv_event_get_user_data(e)); }
// Holding an app starts arranging them (after the event: the page is rebuilt).
static void editOn(void*) { s_ui->homeEdit(true); }
static void onAppHold(lv_event_t* e) {
  (void)e;
  lv_indev_wait_release(lv_indev_active());   // the hold isn't also a tap
  lv_async_call(editOn, nullptr);
}

// ── Telemetry fields ──
// NodePrefs::dashboard_fields, shared with L1: ui-core/Telemetry.h.
static const int FIELDS = 3;
static lv_obj_t* s_field_val[FIELDS];
static lv_obj_t* s_field_pick[FIELDS];   // hidden choices: holding a field opens its picker
static CayenneLPP s_lpp(160);
static uint32_t s_lpp_ms = 0;

static void onFieldHold(lv_event_t* e) {
  lv_indev_wait_release(lv_indev_active());   // the hold isn't also a tap
  pickerOpen(s_field_pick[(int)(uintptr_t)lv_event_get_user_data(e)]);
}
static void onFieldPicked(lv_event_t* e) {
  s_ui->homeFieldSet((int)(uintptr_t)lv_event_get_user_data(e), choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onClockTap(lv_event_t* e) { (void)e; s_ui->showClock(); }

// ── Minimap ──
// The map's tiles (its cache, one decode per loop pass) in a small view:
// centred on you, zoomed out until whoever shares a position and the target
// fit too. Re-framed every few seconds while the page is up.
namespace mini {
static const int COLS = 3, ROWS = 2;   // 256 px tiles over a ~300 x 170 view at any offset
static const int MAX_Z = 16, SOLO_Z = 15, MARGIN = 24;
static lv_obj_t* s_area = nullptr;
static lv_obj_t* s_cells[COLS * ROWS];
static lv_obj_t* s_imgs[COLS * ROWS];
static lv_obj_t* s_hint = nullptr;
static lv_obj_t* s_caption = nullptr;
static const int MAX_PTS = LiveTrackStore::CAPACITY + 2;
enum : uint8_t { P_ME, P_LAST, P_TARGET, P_LIVE };   // how a point's dot looks
struct Pt { int32_t lat, lon; uint8_t kind; lv_obj_t* obj; };
static Pt s_pts[MAX_PTS];   // [0] you, then the target, then live shares
static int s_npts = 0;
static bool s_fitted = false;   // homeMapFit() ran for this map
static int s_z = mapview::DEFAULT_Z;
static double s_cx = 0, s_cy = 0;   // centre, in tiles at s_z
static bool s_pending = false;
static uint32_t s_next_fit_ms = 0;
static mapview::Grid s_grid;   // under the tiles, as on the map

static void onTap(lv_event_t* e) { (void)e; s_ui->openMap(true); }

static lv_obj_t* dot(uint8_t kind) {
  static const struct { uint8_t d; uint32_t col, border; } LOOK[] = {
    { 14, theme::ACCENT, theme::BG }, { 14, theme::BG, theme::ACCENT },   // you; no fix: the last position, hollow
    { 12, theme::BG, theme::ACCENT }, { 12, theme::OK, theme::BG },       // the target; live shares
  };
  int d = LOOK[kind].d;
  uint32_t col = LOOK[kind].col, border = LOOK[kind].border;
  lv_obj_t* o = lv_obj_create(s_area);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}
}  // namespace mini

}  // namespace home

// Home tile: an app or a favourite, icon over name, a badge for unread.
static lv_obj_t* homeTile(lv_obj_t* parent, const char* icon, const char* text, lv_event_cb_t cb, int badge_n) {
  int w = (lv_display_get_horizontal_resolution(NULL) - 2 * theme::PAD - 2 * theme::GAP) / 3;
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, 76);
  lv_obj_set_style_pad_all(b, 4, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_align(label(b, icon, THEME_FONT_LARGE, theme::ACCENT), LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_t* n = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(n, w - 10);
  lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -8);
  tileBadge(b, badge_n);
  return b;
}

// ── Arranging the apps ──
// Every app is on the pages, a hidden one dimmed with a "+" (the others a
// "-"). A tap hides / shows it; a drag lifts a copy of the tile on the top
// layer (the tile dims in place) and drops the app into the slot under the
// finger; held at a screen edge, it goes to that side's page. Done, or the
// side button, ends it.
namespace home {
static lv_obj_t* s_ghost = nullptr;
static int s_drag_pos = -1;          // order position of the tile under the finger
static bool s_dragged = false;       // past the slop: the release is a drop, not a tap
static lv_point_t s_drag_start, s_ghost_org;
static uint32_t s_edge_since = 0;
static const int DRAG_SLOP = 8, EDGE_PX = 20;
static const uint32_t EDGE_MS = 600;

static void dragEnd() {
  if (s_ghost) { lv_obj_delete(s_ghost); s_ghost = nullptr; }
  s_drag_pos = -1;
  s_edge_since = 0;
}
// Leaving Home: arranging ends and the order is saved (UITask::newScreen).
static void leave() {
  dragEnd();
  s_edit = false;
  flushOrder();
}

// The tile on this page nearest the point, as a slot 0..PER_PAGE-1.
static int slotAt(lv_point_t p) {
  int best = 0;
  int32_t bd = INT32_MAX;
  for (int i = 0; i < (int)lv_obj_get_child_count(s_page_box); i++) {
    lv_area_t a;
    lv_obj_get_coords(lv_obj_get_child(s_page_box, i), &a);
    int32_t dx = p.x - (a.x1 + a.x2) / 2, dy = p.y - (a.y1 + a.y2) / 2;
    if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = i; }
  }
  return best;
}

// The page is rebuilt after the event that changed it, not inside it.
static int s_pending_from, s_pending_to, s_pending_page;
static void applyDrop(void*) { s_ui->homeAppDrop(s_pending_from, s_pending_to); if (s_pending_page >= 0) s_ui->setHomePage(s_pending_page); }
static void applyToggle(void*) { s_ui->homeAppToggle(s_pending_from); }
static void later(int from, int to, int page, bool toggle) {
  s_pending_from = from; s_pending_to = to; s_pending_page = page;
  lv_async_call(toggle ? applyToggle : applyDrop, nullptr);
}

static void onEditTile(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
  lv_obj_t* t = (lv_obj_t*)lv_event_get_current_target(e);
  int pos = (int)(uintptr_t)lv_event_get_user_data(e);
  lv_indev_t* in = lv_indev_active();
  lv_point_t p = { 0, 0 };
  if (in) lv_indev_get_point(in, &p);
  if (code == LV_EVENT_PRESSED) {
    dragEnd();
    s_drag_pos = pos;
    s_dragged = false;
    s_drag_start = p;
    return;
  }
  if (s_drag_pos != pos) return;
  if (code == LV_EVENT_PRESSING) {
    if (!s_dragged) {
      if (abs(p.x - s_drag_start.x) < DRAG_SLOP && abs(p.y - s_drag_start.y) < DRAG_SLOP) return;
      s_dragged = true;
      s_swiped = true;   // this touch is a drag, not a page swipe, to its end
      const App& a = APP_LIST[orderApp(pos)];
      s_ghost = homeTile(lv_layer_top(), a.icon, a.text, nullptr, 0);
      lv_obj_remove_flag(s_ghost, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_border_width(s_ghost, 2, 0);
      lv_obj_set_style_border_color(s_ghost, lv_color_hex(theme::ACCENT), 0);
      lv_area_t c;
      lv_obj_get_coords(t, &c);
      s_ghost_org = { c.x1, c.y1 };
      lv_obj_set_style_opa(t, LV_OPA_20, 0);
    }
    lv_obj_set_pos(s_ghost, s_ghost_org.x + p.x - s_drag_start.x, s_ghost_org.y + p.y - s_drag_start.y);
    int w = lv_display_get_horizontal_resolution(NULL);
    int side = p.x < EDGE_PX ? -1 : p.x >= w - EDGE_PX ? 1 : 0;
    int page = s_page + side;
    if (!side || page < APPS || page >= pageCount()) { s_edge_since = 0; return; }
    if (!s_edge_since) { s_edge_since = millis() | 1; return; }
    if (millis() - s_edge_since < EDGE_MS) return;
    // Onto that page's nearest slot; the finger lifts to drag it on from there.
    int to = side > 0 ? (page - APPS) * PER_PAGE : (page - APPS) * PER_PAGE + PER_PAGE - 1;
    dragEnd();
    lv_indev_wait_release(in);
    later(pos, to < APP_COUNT ? to : APP_COUNT - 1, page, false);
    return;
  }
  bool dropped = s_dragged && s_ghost;
  dragEnd();
  if (code == LV_EVENT_PRESS_LOST) { lv_obj_set_style_opa(t, LV_OPA_COVER, 0); return; }
  if (!dropped) { later(pos, 0, -1, true); return; }   // a tap
  int to = (s_page - APPS) * PER_PAGE + slotAt(p);
  later(pos, to < APP_COUNT ? to : APP_COUNT - 1, -1, false);
}

static void editDone(void*) { s_ui->homeEdit(false); }
static void onEditDone(lv_event_t* e) { (void)e; lv_async_call(editDone, nullptr); }

// A tile being arranged: dimmed if hidden, a round "+" / "-" in its corner.
static void editTile(lv_obj_t* box, int pos) {
  const App& a = APP_LIST[orderApp(pos)];
  bool on = orderShown(pos);
  lv_obj_t* t = homeTile(box, a.icon, a.text, nullptr, 0);
  lv_obj_add_flag(t, LV_OBJ_FLAG_PRESS_LOCK);   // still its press when the finger leaves it
  lv_obj_add_event_cb(t, onEditTile, LV_EVENT_ALL, (void*)(uintptr_t)pos);
  if (!on) {
    lv_obj_set_style_bg_color(t, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_color(t, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_set_style_border_width(t, 1, 0);
    for (int i = 0; i < 2; i++) lv_obj_set_style_text_opa(lv_obj_get_child(t, i), LV_OPA_40, 0);
  }
  if (a.id == ALWAYS) return;
  lv_obj_t* b = lv_obj_create(t);
  lv_obj_remove_style_all(b);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, 20, 20);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? theme::SURFACE_2 : theme::ACCENT), 0);
  lv_obj_align(b, LV_ALIGN_TOP_RIGHT, 2, -2);
  lv_obj_center(label(b, on ? LV_SYMBOL_MINUS : LV_SYMBOL_PLUS, THEME_FONT_SMALL, on ? theme::TEXT : theme::BG));
}
}  // namespace home

void UITask::homeEdit(bool on) {
  using namespace home;
  dragEnd();
  s_edit = on;
  if (on) {
    loadOrder();
    if (s_page < APPS) s_page = APPS;
  } else {
    flushOrder();
    if (s_page >= pageCount()) s_page = pageCount() - 1;
  }
  if (_screen == SCR_HOME) buildHome();   // in place
  else { _fade_next = true; showHome(); }
  if (on) showToast("Drag to move, tap to hide or show");
}

void UITask::homeAppDrop(int from, int to) {
  using namespace home;
  if (from == to || from < 0 || to < 0 || from >= APP_COUNT || to >= APP_COUNT) { setHomePage(s_page); return; }
  char c = s_order[from];
  if (from < to) memmove(s_order + from, s_order + from + 1, to - from);
  else memmove(s_order + to + 1, s_order + to, from - to);
  s_order[to] = c;
  s_order_dirty = true;
  setHomePage(s_page);
}

void UITask::homeAppToggle(int pos) {
  using namespace home;
  if (pos < 0 || pos >= APP_COUNT || APP_LIST[orderApp(pos)].id == ALWAYS) return;
  s_order[pos] = orderShown(pos) ? tolower((unsigned char)s_order[pos]) : toupper((unsigned char)s_order[pos]);
  s_order_dirty = true;
  setHomePage(s_page);
}

void UITask::showHome() {
  _screen = SCR_HOME;
  _share_text[0] = '\0';   // a share not sent to anyone
  buildHome();
  refreshHome();
}

// The side button: back to the clock page, from wherever.
void UITask::goHome() {
  if (_nav_overlay) navClosePopup();
  pickerClose();
  int was = home::s_page;
  home::s_page = home::CLOCK;
  if (_screen == SCR_HOME && home::s_edit) {   // arranging ends; the page dots change with it
    home::leave();
    buildHome();
    return;
  }
  if (_screen == SCR_HOME) {   // another page: the clock fades up in its place
    if (was == home::CLOCK) return;
    setHomePage(home::CLOCK);
    if (home::s_page_box) anim::fadeIn(home::s_page_box);
    return;
  }
  _fade_next = true;   // from a screen: Home dissolves in over it
  showHome();
}

void UITask::buildHome() {
  lv_obj_t* body = newScreen(NULL, false);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);   // a drag is a page swipe, not a scroll
  lv_obj_set_style_pad_bottom(body, 26, 0);           // the dots
  home::s_page_box = lv_obj_create(body);
  styleSurface(home::s_page_box, theme::BG);
  lv_obj_remove_flag(home::s_page_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_width(home::s_page_box, LV_PCT(100));
  lv_obj_set_flex_grow(home::s_page_box, 1);

  home::s_dots = lv_obj_create(body);
  lv_obj_remove_style_all(home::s_dots);
  lv_obj_add_flag(home::s_dots, LV_OBJ_FLAG_IGNORE_LAYOUT);
  lv_obj_set_size(home::s_dots, LV_SIZE_CONTENT, 22);
  lv_obj_align(home::s_dots, LV_ALIGN_BOTTOM_MID, 0, 22);
  lv_obj_set_flex_flow(home::s_dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(home::s_dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(home::s_dots, 12, 0);
  int pages = home::pageCount();
  if (home::s_page >= pages) home::s_page = pages - 1;   // apps hidden since
  for (int p = 0; p < pages; p++) {
    lv_obj_t* d = lv_obj_create(home::s_dots);
    lv_obj_remove_style_all(d);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_ext_click_area(d, 8);
    lv_obj_add_flag(d, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(d, home::onDot, LV_EVENT_CLICKED, (void*)(uintptr_t)p);
  }
  if (home::s_edit) {   // Done, right of the dots
    lv_obj_t* b = lv_button_create(body);
    lv_obj_add_flag(b, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 26);
    lv_obj_set_style_pad_hor(b, 14, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 24);
    lv_obj_add_event_cb(b, home::onEditDone, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(b, "Done", THEME_FONT_SMALL, theme::BG));
  }
  setHomePage(home::s_page);
}

// From loop() on Home: turn the page once a press has moved SWIPE_PX sideways.
void UITask::homeSwipePoll() {
  lv_indev_t* in = lv_indev_get_next(NULL);
  if (!in) return;
  bool down = lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  if (!down) { home::s_touching = false; return; }
  if (!home::s_touching) { home::s_touching = true; home::s_swiped = false; home::s_start = p; return; }
  if (home::s_swiped || home::s_drag_pos >= 0) return;   // a tile being dragged isn't a swipe
  int dx = p.x - home::s_start.x, dy = p.y - home::s_start.y;
  if (abs(dx) < home::SWIPE_PX || abs(dx) < 2 * abs(dy)) return;
  home::s_swiped = true;
  lv_indev_wait_release(in);   // the swipe isn't also a tap on the tile it started on
  setHomePage(home::s_page + (dx < 0 ? 1 : -1));
}

void UITask::setHomePage(int page) {
  using namespace home;
  if (_screen != SCR_HOME || !s_page_box) return;   // s_page_box went with an older screen
  if (page < (s_edit ? APPS : 0) || page >= pageCount()) return;
  int from = s_page;
  s_page = page;
  lv_obj_clean(s_page_box);
  _home_clock = _home_date = _home_unread = nullptr;
  for (lv_obj_t*& v : s_field_val) v = nullptr;
  mini::s_area = nullptr;
  s_unread_sig = unreadTotal();
  lv_obj_set_layout(s_page_box, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_page_box, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(s_page_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_column(s_page_box, theme::GAP, 0);
  lv_obj_set_style_pad_row(s_page_box, theme::GAP, 0);

  if (page == FAVS) favGrid(s_page_box);   // DeviceScreen.h
  else if (page == CLOCK) buildHomeClock(s_page_box);
  else if (page == MAP) buildHomeMap(s_page_box);
  else {
    lv_obj_set_flex_align(s_page_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    int first = (page - APPS) * PER_PAGE;
    if (s_edit) for (int pos = first; pos < first + PER_PAGE && pos < APP_COUNT; pos++) editTile(s_page_box, pos);
    else for (int k = first, i; k < first + PER_PAGE && (i = shownApp(k)) >= 0; k++) {
      const App& a = APP_LIST[i];
      lv_obj_t* t = homeTile(s_page_box, a.icon, a.text, a.cb, a.unread ? unreadTotal() : 0);
      lv_obj_add_event_cb(t, onAppHold, LV_EVENT_LONG_PRESSED, NULL);
    }
  }
  for (int i = 0; s_dots && i < (int)lv_obj_get_child_count(s_dots); i++)
    lv_obj_set_style_bg_color(lv_obj_get_child(s_dots, i), lv_color_hex(i == page ? theme::ACCENT : theme::SURFACE_2), 0);
  if (page != from) anim::slideIn(s_page_box, page > from ? 48 : -48);
  refreshHome();
}

// The clock page: time (a tap opens Clock), date, name, the three fields.
void UITask::buildHomeClock(lv_obj_t* box) {
  using namespace home;
  lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(box, 0, 0);
  lv_obj_t* clk = flexBox(box, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(clk, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(clk, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(clk, LV_OPA_70, LV_STATE_PRESSED);
  lv_obj_add_event_cb(clk, onClockTap, LV_EVENT_CLICKED, NULL);
  _home_clock = clockFace(clk, THEME_FONT_CLOCK, THEME_FONT_TITLE);
  _home_date = label(clk, "", THEME_FONT_BODY, theme::TEXT_MUTED);
  label(clk, the_mesh.getNodeName(), THEME_FONT_BODY, theme::ACCENT);

  lv_obj_t* row = flexBox(box, LV_FLEX_FLOW_ROW);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_column(row, theme::GAP, 0);
  lv_obj_set_style_pad_top(row, 12, 0);
  char opts[200];
  int o = 0;
  for (int k = 0; k < telemetry::COUNT; k++) o += snprintf(opts + o, sizeof(opts) - o, "%s%s", k ? "\n" : "", telemetry::NAME[k]);
  for (int i = 0; i < FIELDS; i++) {
    uint8_t f = _prefs ? _prefs->dashboard_fields[i] : telemetry::NONE;
    if (f >= telemetry::COUNT) f = telemetry::NONE;
    lv_obj_t* c = lv_obj_create(row);
    styleSurface(c, f ? theme::SURFACE : theme::BG);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(c, 52);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_radius(c, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
    if (!f) {   // empty: an outline to fill
      lv_obj_set_style_border_color(c, lv_color_hex(theme::SURFACE_2), 0);
      lv_obj_set_style_border_width(c, 1, 0);
    }
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 2, 0);
    s_field_pick[i] = choiceCreate(c, opts, f, "Field on the clock page");
    lv_obj_add_flag(s_field_pick[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_field_pick[i], onFieldPicked, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
    if (f) {
      lv_obj_add_event_cb(c, onFieldHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)i);
      label(c, telemetry::NAME[f], THEME_FONT_SMALL, theme::TEXT_MUTED);
      s_field_val[i] = label(c, "--", THEME_FONT_TITLE, theme::TEXT);
    } else {   // a tap is enough on an empty one
      lv_obj_add_event_cb(c, onFieldHold, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
      label(c, LV_SYMBOL_PLUS, THEME_FONT_BODY, theme::TEXT_MUTED);
    }
  }
}

void UITask::homeFieldSet(int slot, int f) {
  if (!_prefs || slot < 0 || slot >= home::FIELDS || f < 0 || f >= telemetry::COUNT) return;
  _prefs->dashboard_fields[slot] = (uint8_t)f;
  prefsSave();
  setHomePage(home::CLOCK);
}

// One field's value, as short as fits its card.
void UITask::homeFieldText(uint8_t f, char* v, int n) {
  using namespace home;
  static const telemetry::Style STYLE = { "\xC2\xB0", true, false, 3 };   // UTF-8 degree
  telemetry::Inputs in;
  in.batt_mv = _batt_mv ? _batt_mv : getBattMilliVolts();
  in.low_batt_mv = _prefs ? _prefs->low_batt_mv : 0;
  in.nodes = the_mesh.getNumContacts();
  in.unread = unreadTotal();
  in.imperial = _prefs && _prefs->units_imperial;
  in.loc = _sensors ? _sensors->getLocationProvider() : nullptr;
  in.gps_on = _core->gpsEnabled();
  if (telemetry::isSensor(f)) {   // the sensors on the bus: read every few seconds, not per field
    if (_sensors && (s_lpp_ms == 0 || millis() - s_lpp_ms > 5000)) {
      s_lpp_ms = millis() | 1;
      s_lpp.reset();
      _sensors->querySensors(0xFF, s_lpp);
    }
    in.lpp = s_lpp.getBuffer();
    in.lpp_len = s_lpp.getSize();
  }
  telemetry::text(f, in, STYLE, v, n);
}

// ── Minimap page ──

void UITask::buildHomeMap(lv_obj_t* box) {
  using namespace home::mini;
  lv_obj_set_layout(box, LV_LAYOUT_NONE);
  s_area = lv_obj_create(box);
  styleSurface(s_area, 0x1A1A1E);   // unloaded / missing tiles
  lv_obj_set_size(s_area, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_radius(s_area, theme::RADIUS, 0);
  lv_obj_set_style_clip_corner(s_area, true, 0);
  lv_obj_remove_flag(s_area, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_area, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(s_area, onTap, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(s_area, mapview::drawGrid, LV_EVENT_DRAW_MAIN_END, &s_grid);
  for (int i = 0; i < COLS * ROWS; i++) {
    lv_obj_t* cell = lv_obj_create(s_area);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, mapview::TILE_PX, mapview::TILE_PX);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
    s_cells[i] = cell;
    s_imgs[i] = lv_image_create(cell);
    lv_image_set_pivot(s_imgs[i], 0, 0);
  }
  s_npts = 0;
  s_fitted = false;
  s_hint = label(s_area, "", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_set_style_bg_color(s_hint, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(s_hint, LV_OPA_80, 0);
  lv_obj_set_style_radius(s_hint, theme::RADIUS_SM, 0);
  lv_obj_set_style_pad_hor(s_hint, 8, 0);
  lv_obj_set_style_pad_ver(s_hint, 4, 0);
  lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(s_hint);
  lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
  s_caption = label(s_area, "", THEME_FONT_SMALL, theme::TEXT);   // who's on it
  lv_obj_set_style_bg_color(s_caption, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(s_caption, LV_OPA_80, 0);
  lv_obj_set_style_radius(s_caption, theme::RADIUS_SM, 0);
  lv_obj_set_style_pad_hor(s_caption, 6, 0);
  lv_obj_set_style_pad_ver(s_caption, 2, 0);
  lv_obj_align(s_caption, LV_ALIGN_BOTTOM_LEFT, 6, -6);
  mapview::s_available = lvport::mountStorage() && mapview::s_provider->available();
  mapview::s_trails_on = lvport::trailsOn();
  lv_obj_update_layout(s_area);
  homeMapFit();
  homeMapLayout();
}

// Picks the points, the centre and the zoom that fits them. False when
// nothing changed since the last call (no re-layout, no redraw): the dots
// stay, moved or restyled only when their points did.
bool UITask::homeMapFit() {
  using namespace home::mini;
  if (!s_area) return false;
  Pt pts[MAX_PTS];
  int np = 0;
  int32_t lat, lon;
  bool me = _core->course.currentLocation(lat, lon);
  int live = 0;
  bool target = false;
  bool last = false;   // no fix: the saved / app-set position, hollow
  if (me) pts[np++] = { lat, lon, P_ME, nullptr };
  else if (_sensors && (_sensors->node_lat != 0 || _sensors->node_lon != 0)) {
    last = true;
    pts[np++] = { (int32_t)lround(_sensors->node_lat * 1e6), (int32_t)lround(_sensors->node_lon * 1e6), P_LAST, nullptr };
  }
  if (_core->locator.activeTargetPos(lat, lon)) {
    target = true;
    pts[np++] = { lat, lon, P_TARGET, nullptr };
  }
  const LiveTrackStore& lt = _core->live_share.track();
  uint32_t now = rtc_clock.getCurrentTime();
  for (int i = 0; i < LiveTrackStore::CAPACITY && np < MAX_PTS; i++) {
    if (!lt.isActive(i, now)) continue;
    const LiveTrackStore::Entry& e = lt.slotAt(i);
    pts[np++] = { e.lat_1e6, e.lon_1e6, P_LIVE, nullptr };
    live++;
  }
  bool same = np == s_npts;
  for (int i = 0; i < np && same; i++)
    same = pts[i].kind == s_pts[i].kind && pts[i].lat == s_pts[i].lat && pts[i].lon == s_pts[i].lon && s_pts[i].obj;
  if (same && s_fitted) return false;
  s_fitted = true;
  bool made = false;
  for (int i = 0; i < np; i++) {   // the same dot where the kind matches
    lv_obj_t* o = i < s_npts && s_pts[i].kind == pts[i].kind ? s_pts[i].obj : nullptr;
    if (!o) {
      if (i < s_npts && s_pts[i].obj) lv_obj_delete(s_pts[i].obj);
      o = dot(pts[i].kind);
      made = true;
    }
    pts[i].obj = o;
  }
  for (int i = np; i < s_npts; i++) if (s_pts[i].obj) lv_obj_delete(s_pts[i].obj);
  memcpy(s_pts, pts, np * sizeof(Pt));
  s_npts = np;
  if (made) {
  lv_obj_move_foreground(s_hint);
  lv_obj_move_foreground(s_caption);
  }

  char cap[48];
  int o = snprintf(cap, sizeof(cap), "%s", me ? "You" : last ? "Last position" : "No position yet");
  if (live) o += snprintf(cap + o, sizeof(cap) - o, "  " LV_SYMBOL_GPS " %d sharing", live);
  if (target) snprintf(cap + o, sizeof(cap) - o, "  " UI_SYMBOL_FLAG " target");
  setText(s_caption, cap);

  if (s_npts == 0) {   // nothing to show: where the map was left, else the default view
    if (_map_z) {
      s_z = _map_z;
      s_cx = _map_cx;
      s_cy = _map_cy;
    } else {
      s_z = mapview::DEFAULT_Z;
      s_cx = mapview::lonToTileX(mapview::DEFAULT_LON, s_z);
      s_cy = mapview::latToTileY(mapview::DEFAULT_LAT, s_z);
    }
    return true;
  }
  // Bounds in world units (tiles at z 0), then the deepest zoom they fit at.
  double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
  for (int i = 0; i < s_npts; i++) {
    double x = mapview::lonToTileX(s_pts[i].lon / 1e6, 0), y = mapview::latToTileY(s_pts[i].lat / 1e6, 0);
    x0 = fmin(x0, x); x1 = fmax(x1, x); y0 = fmin(y0, y); y1 = fmax(y1, y);
  }
  int w = lv_obj_get_width(s_area) - 2 * MARGIN, h = lv_obj_get_height(s_area) - 2 * MARGIN - 20;
  s_z = s_npts == 1 ? SOLO_Z : mapview::MIN_Z;
  for (int z = MAX_Z; s_npts > 1 && z >= mapview::MIN_Z; z--) {
    double k = (double)(1 << z) * mapview::TILE_PX;
    if ((x1 - x0) * k <= w && (y1 - y0) * k <= h) { s_z = z; break; }
  }
  s_cx = (x0 + x1) / 2 * (1 << s_z);
  s_cy = (y0 + y1) / 2 * (1 << s_z);
  return true;
}

// Places tiles and points; notes whether a tile still has to be decoded.
void UITask::homeMapLayout() {
  using namespace home::mini;
  if (!s_area) return;
  int w = lv_obj_get_width(s_area), h = lv_obj_get_height(s_area);
  double left = s_cx * mapview::TILE_PX - w / 2.0, top = s_cy * mapview::TILE_PX - h / 2.0;
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << s_z;
  s_pending = false;
  int shown = 0;
  char sl[16];
  double lat = atan(sinh(M_PI * (1 - 2 * s_cy / (double)n))) * 180.0 / M_PI;
  s_grid = { left, top, mapview::gridStep(s_z, lat, _prefs && _prefs->units_imperial, 48, sl, sizeof(sl)) };
  lv_obj_invalidate(s_area);
  for (int j = 0; j < ROWS; j++) {
    for (int i = 0; i < COLS; i++) {
      lv_obj_t* cell = s_cells[j * COLS + i];
      lv_obj_t* img = s_imgs[j * COLS + i];
      int tx = tx0 + i, ty = ty0 + j, wx = ((tx % n) + n) % n;
      int px = (int)lround(tx * (double)mapview::TILE_PX - left), py = (int)lround(ty * (double)mapview::TILE_PX - top);
      mapview::TileCache::Slot* show = nullptr;
      int k = 0;
      if (mapview::s_available && ty >= 0 && ty < n && px < w && py < h) {
        mapview::TileCache::Slot* s = mapview::s_cache.find(s_z, wx, ty);
        if (s && s->present) show = s;
        else if (s) {   // none at this zoom: a coarser one, magnified
          int wz, ax, ay;
          show = mapview::ancestorFor(s_z, wx, ty, k, wz, ax, ay);
          if (!show && wz >= 0) s_pending = true;
        } else {
          s_pending = true;
          show = mapview::cachedAncestor(s_z, wx, ty, k);
        }
      }
      if (!show) { lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN); continue; }
      if (lv_image_get_src(img) != &show->dsc) lv_image_set_src(img, &show->dsc);
      lv_image_set_scale(img, LV_SCALE_NONE << k);
      int m = (1 << k) - 1;
      lv_obj_set_pos(img, -(wx & m) * mapview::TILE_PX, -(ty & m) * mapview::TILE_PX);
      lv_obj_set_pos(cell, px, py);
      lv_obj_remove_flag(cell, LV_OBJ_FLAG_HIDDEN);
      shown++;
    }
  }
  for (int i = 0; i < s_npts; i++) {
    double x = mapview::lonToTileX(s_pts[i].lon / 1e6, s_z) * mapview::TILE_PX - left;
    double y = mapview::latToTileY(s_pts[i].lat / 1e6, s_z) * mapview::TILE_PX - top;
    int d = lv_obj_get_width(s_pts[i].obj);
    lv_obj_set_pos(s_pts[i].obj, (int)lround(x) - d / 2, (int)lround(y) - d / 2);
  }
  const char* hint = !mapview::s_available ? "No map on the SD card"
                   : !shown && !s_pending ? "No map tiles for this area" : nullptr;
  if (hint) { setText(s_hint, hint); lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN); }
  else lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
}

// From loop() while the map page is up: one tile decode per pass, a re-frame
// every few seconds.
void UITask::homeMapLoop() {
  using namespace home::mini;
  if (!s_area || home::s_touching) return;
  if ((int32_t)(millis() - s_next_fit_ms) >= 0) {
    s_next_fit_ms = millis() + 5000;
    if (homeMapFit()) homeMapLayout();
  }
  if (!s_pending) return;
  int w = lv_obj_get_width(s_area), h = lv_obj_get_height(s_area);
  double left = s_cx * mapview::TILE_PX - w / 2.0, top = s_cy * mapview::TILE_PX - h / 2.0;
  int tx0 = (int)floor(left / mapview::TILE_PX), ty0 = (int)floor(top / mapview::TILE_PX);
  int n = 1 << s_z;
  for (int j = 0; j < ROWS; j++) {
    for (int i = 0; i < COLS; i++) {
      int tx = tx0 + i, ty = ty0 + j, wx = ((tx % n) + n) % n;
      int px = (int)lround(tx * (double)mapview::TILE_PX - left), py = (int)lround(ty * (double)mapview::TILE_PX - top);
      if (ty < 0 || ty >= n || px >= w || py >= h) continue;
      mapview::TileCache::Slot* s = mapview::s_cache.find(s_z, wx, ty);
      if (!s) { mapview::s_cache.load(*mapview::s_provider, s_z, wx, ty); homeMapLayout(); return; }
      if (s->present) continue;
      int k, wz, ax, ay;
      if (!mapview::ancestorFor(s_z, wx, ty, k, wz, ax, ay) && wz >= 0) {
        mapview::s_cache.load(*mapview::s_provider, wz, ax, ay);
        homeMapLayout();
        return;
      }
    }
  }
  homeMapLayout();   // everything looked up
}

int UITask::unreadTotal() {
  return _core->dmUnreadTotal() + _core->history.getTotalChannelUnread() + _core->roomUnread();
}

// Once a second on Home: the clock, the fields; the favourites and apps
// pages redrawn when the unread count moved (their badges).
void UITask::refreshHome() {
  using namespace home;
  if ((s_page == FAVS || (s_page >= APPS && !s_edit)) && s_page_box && unreadTotal() != s_unread_sig) {
    setHomePage(s_page);
    return;
  }
  if (!_home_clock) return;
  struct tm ti;
  bool known = localTime(_prefs, ti);
  clockFaceSet(_home_clock, known ? &ti : nullptr, _prefs);
  if (known) {
    char date[48];
    fmtDate(date, sizeof(date), ti);
    setText(_home_date, date);
  } else {
    setText(_home_date, "time not synced");
  }
  for (int i = 0; i < FIELDS; i++) {
    if (!s_field_val[i] || !_prefs) continue;
    char v[24];
    homeFieldText(_prefs->dashboard_fields[i], v, sizeof(v));
    setText(s_field_val[i], v);
  }
}
