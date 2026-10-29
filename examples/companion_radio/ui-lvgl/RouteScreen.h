#pragma once
// Route: the repeaters between this node and one contact, edited, traced and
// saved (node detail > Route). The list, the out-and-back trace, the search
// for where a route stops and the save are the UI Core's PathEditor
// (`_core->route`); ui-new's Path screen is a view over the same. This draws
// the chain with each link's signal, a picker of repeaters by distance, and
// -- from the map -- picking the hops by tapping them (MapScreen.h,
// `_route_pick`).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after AdminScreen.h.

namespace routeview {

enum : uint8_t { RT_ADD, RT_MAP, RT_TRACE, RT_SAVE, RT_PIN, RT_PROBE };
enum : uint8_t { OP_UP, OP_DOWN, OP_DEL, OP_SEL };

static const int PICK_MAX = 96;
struct Pick { uint16_t idx; float km; };   // raw contact index, distance (<0: no position)
static Pick* s_pick = psramBuf<Pick>(PICK_MAX);
static int s_pick_n = 0;

static int cmpPick(const void* a, const void* b) {
  float x = ((const Pick*)a)->km, y = ((const Pick*)b)->km;
  if ((x < 0) != (y < 0)) return x < 0 ? 1 : -1;   // those with no position last
  return x < y ? -1 : x > y ? 1 : 0;
}

// A link's colour from its weaker direction (SNR x4): good, fair, weak.
static uint32_t linkColour(int16_t out_x4, int16_t back_x4) {
  int16_t m = out_x4 < back_x4 ? out_x4 : back_x4;
  return m >= 0 ? theme::OK : m >= -7 * 4 ? theme::ACCENT : theme::FAIL;
}

// "6.5 / 4.0": how the node heard the one before it, and back (dB).
static void linkText(char* out, size_t n, int16_t out_x4, int16_t back_x4) {
  snprintf(out, n, "%.1f / %.1f", out_x4 / 4.0f, back_x4 / 4.0f);
}

static int s_sel = -1;            // the hop showing its move / remove buttons
static const int ROW_TARGET = 0xFE;   // a row's tap code: the node (trace to it or not)

// Discover: how many tries (TRIES[s_tries]), kept while the device runs.
static const char* TRIES_MAP[] = { "1", "3", "5", "" };
static const int TRIES[] = { 1, 3, 5 };
static int s_tries = 1;

}  // namespace routeview

static void onRouteAct(lv_event_t* e)     { s_ui->routeAct((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onRouteOp(lv_event_t* e)      { s_ui->routeOp((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onRoutePickRow(lv_event_t* e) { s_ui->routePick((int)vlist::arg(e)); }

static void onRouteRow(lv_event_t* e)     { s_ui->routeOp(routeview::OP_SEL << 8 | (int)(uintptr_t)lv_event_get_user_data(e)); }
static void onRouteKeep(lv_event_t* e)    { (void)e; s_ui->routeAct(routeview::RT_PIN); }
static void onRouteDiscover(lv_event_t* e) { (void)e; s_ui->routeDiscover(); }
static void onRouteFound(lv_event_t* e)   { s_ui->routeUseFound((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onRouteTries(lv_event_t* e) {
  s_ui->routeDiscTries((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}

// A small square button on a hop's row: user data = op << 8 | hop index.
static lv_obj_t* routeSquare(lv_obj_t* row, const char* sym, int code) {
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_size(b, 30, 26);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onRouteOp, LV_EVENT_CLICKED, (void*)(uintptr_t)code);
  lv_obj_center(label(b, sym, THEME_FONT_SMALL, theme::TEXT));
  return b;
}

// One node of the chain on one line: a number, the name, and on the right
// the link that reaches it (or why there is none). `tap` >= 0: the row is
// tappable with that code (a hop's index, or ROW_TARGET).
static lv_obj_t* routeRow(lv_obj_t* list, int h, const char* num, const char* name, uint32_t name_col,
                          const char* right, uint32_t right_col, int tap) {
  lv_obj_t* r = lv_obj_create(list);
  styleSurface(r, theme::SURFACE);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, LV_PCT(100), h);
  lv_obj_set_style_radius(r, theme::RADIUS, 0);
  lv_obj_set_style_pad_hor(r, 8, 0);
  lv_obj_set_style_pad_ver(r, 0, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 6, 0);
  if (tap >= 0) {
    lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(r, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(r, onRouteRow, LV_EVENT_CLICKED, (void*)(uintptr_t)tap);
  }
  lv_obj_t* n = label(r, num, THEME_FONT_BODY, theme::ACCENT);
  lv_obj_set_width(n, 14);
  lv_obj_t* nm = label(r, name, THEME_FONT_BODY, name_col);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, 1);
  lv_obj_set_flex_grow(nm, 1);
  if (right && right[0]) label(r, right, THEME_FONT_SMALL, right_col);
  return r;
}

// Keep (the header's button, a toggle): restyled, the status line follows.
void UITask::routeKeep() {
  PathEditor& R = _core->route;
  bool on = !R.pinned();
  bool now = R.setPinned(on);
  showToast(!on ? "The route is learned again" : now ? "Route kept" : "Kept once you save it");
  if (_route_keep_btn) styleSelected(_route_keep_btn, R.pinned());
  routeRebuild();
}

// Node detail > Route.
void UITask::showRoute(const uint8_t* pub_key) {
  if (!_core->route.open(pub_key)) { showToast("Not a contact"); return; }
  _route_pick = false;
  _screen = SCR_ROUTE;
  buildRoute();
}

// Keep in the header, the status line, the chain (rebuilt as the trace
// goes), the actions at the foot.
void UITask::buildRoute() {
  PathEditor& R = _core->route;
  lv_obj_t* body = newScreen("Route", true);
  _route_keep_btn = _header ? headerButton(_header, UI_SYMBOL_TACK " Keep", onRouteKeep, 4, NULL) : nullptr;
  if (_route_keep_btn) styleSelected(_route_keep_btn, R.pinned());
  routeview::s_sel = -1;
  // The status line, with "Find where it stops" beside it after a trace with no reply.
  lv_obj_t* top = flexBox(body, LV_FLEX_FLOW_ROW);
  lv_obj_set_width(top, LV_PCT(100));
  lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(top, 6, 0);
  _route_status = noteLabel(top, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_width(_route_status, 1);
  lv_obj_set_flex_grow(_route_status, 1);
  _route_probe_row = pillButton(top, LV_SYMBOL_GPS " Find where", theme::ACCENT, onRouteAct, routeview::RT_PROBE);
  styleSelected(_route_probe_row, true);
  lv_obj_t* scroll = scrollList(body);
  lv_obj_set_style_pad_row(scroll, 4, 0);
  _route_list = flexBox(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(_route_list, LV_PCT(100));
  lv_obj_set_style_pad_row(_route_list, 4, 0);

  _route_note = noteLabel(scroll, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  _route_disc = flexBox(scroll, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(_route_disc, LV_PCT(100));
  lv_obj_set_style_pad_row(_route_disc, theme::GAP, 0);   // Discover's button off the group, as the list's rows
  _route_disc_shown = 0xFFFFFFFF;

  lv_obj_t* bar = buttonBar(body);
  barButton(bar, LV_SYMBOL_PLUS " Add", onRouteAct, routeview::RT_ADD);
  barButton(bar, UI_SYMBOL_MAP " Map", onRouteAct, routeview::RT_MAP);
  _route_trace_btn = barButton(bar, LV_SYMBOL_LOOP " Trace", onRouteAct, routeview::RT_TRACE, true);
  _route_save_btn = barButton(bar, LV_SYMBOL_SAVE " Save", onRouteAct, routeview::RT_SAVE);
  routeRebuild();
  routeDiscRebuild();
}

// What the chain shows: the state, the links with a result, a probe's step.
uint32_t UITask::routeSig() {
  PathEditor& R = _core->route;
  return (uint32_t)R.state() | (uint32_t)R.links() << 4 | (uint32_t)R.reached() << 8 | (uint32_t)R.probing() << 12 |
         (uint32_t)R.edited() << 13 | (uint32_t)R.toNode() << 14 | (uint32_t)R.count() << 16;
}

// The chain, from the editor's state: the repeaters and the node (this
// device before them goes without saying), one line each with the link that
// reaches it ("out / back" dB, coloured by
// its weaker way). A tapped hop shows its move / remove buttons instead; a
// tap on the node traces to it or not.
void UITask::routeRebuild() {
  if (!_route_list || !_route_status) return;
  using namespace routeview;
  PathEditor& R = _core->route;
  lv_obj_clean(_route_list);
  const int n = R.count();
  if (s_sel >= n) s_sel = -1;
  const bool probed = R.state() == PathEditor::PROBED;
  const int stop = probed ? R.reached() + 1 : 0;   // the node that didn't answer the probe
  char right[40];
  uint32_t col;
  auto link = [&](int k) {   // the link to node k, or why there's none
    int16_t o, b;
    right[0] = '\0';
    col = theme::TEXT_MUTED;
    if (R.linkSnrX4(k, o, b)) { linkText(right, sizeof(right), o, b); col = linkColour(o, b); }
    else if (k == stop) { snprintf(right, sizeof(right), "no answer"); col = theme::FAIL; }
  };

  for (int i = 0; i < n; i++) {
    char num[4];
    snprintf(num, sizeof(num), "%d", i + 1);
    link(i + 1);
    if (!right[0] && R.hop(i).known < 4) snprintf(right, sizeof(right), "hash only");
    const bool sel = i == s_sel;
    lv_obj_t* r = routeRow(_route_list, 34, num, R.hop(i).name, theme::TEXT, sel ? NULL : right, col, i);
    if (sel) {
      if (i > 0) routeSquare(r, LV_SYMBOL_UP, OP_UP << 8 | i);
      if (i < n - 1) routeSquare(r, LV_SYMBOL_DOWN, OP_DOWN << 8 | i);
      routeSquare(r, LV_SYMBOL_CLOSE, OP_DEL << 8 | i);
    }
  }
  if (R.toNode()) {
    link(n + 1);
    if (probed && R.reached() == n && !right[0]) { snprintf(right, sizeof(right), "no answer"); col = theme::FAIL; }
  } else {
    snprintf(right, sizeof(right), "not traced");
    col = theme::TEXT_MUTED;
  }
  routeRow(_route_list, 34, "", R.targetName()[0] ? R.targetName() : "(unnamed)", theme::ACCENT, right, col,
           n > 0 ? ROW_TARGET : -1);

  const char* note = "";
  if (n == 0)
    note = R.hasStoredPath() && !R.edited()
               ? "Direct: sent straight to the node. Add the repeaters to route through them."
               : "Add the repeaters the node is reached through, in order (Add, or tap them on the Map). "
                 "Saved empty, messages go straight to the node.";
  else if (R.state() == PathEditor::IDLE && R.links() == 0)
    note = R.toNode() ? "Tap a hop to move or remove it. The trace goes to the node and back: tap it to stop at the last repeater."
                      : "Tap a hop to move or remove it. The node is left out: only one that repeats can answer. Tap it to include it.";
  else if (R.links() > 0)
    note = "Out / back dB: how each one heard the one before it (this device first), and the other way.";
  setText(_route_note, note);
  lv_obj_set_flag(_route_note, LV_OBJ_FLAG_HIDDEN, !note[0]);
  lv_obj_set_flag(_route_probe_row, LV_OBJ_FLAG_HIDDEN, !R.canProbe());
  routeStatus();
}

void UITask::routeStatus() {
  PathEditor& R = _core->route;
  const int n = R.count();
  char t[112];
  switch (R.state()) {
    case PathEditor::RUNNING:
      if (R.probing()) snprintf(t, sizeof(t), "Finding where it stops: to %s...", R.nodeName(R.links() + 1));
      else snprintf(t, sizeof(t), "Tracing %d node%s out and back...", R.chainNodes(), R.chainNodes() == 1 ? "" : "s");
      break;
    case PathEditor::DONE: {
      int16_t worst = 0;
      if (R.worstX4(worst)) snprintf(t, sizeof(t), "Heard back in %lu ms, weakest link %.1f dB", (unsigned long)R.rttMs(), worst / 4.0f);
      else snprintf(t, sizeof(t), "Heard back in %lu ms", (unsigned long)R.rttMs());
      break;
    }
    case PathEditor::TIMEOUT:
      if (n > 0) snprintf(t, sizeof(t), "No reply: one of them didn't answer");
      else snprintf(t, sizeof(t), "No reply. %s answers only if it repeats and hears this device", R.targetName());
      break;
    case PathEditor::PROBED:
      if (R.reached() < n)
        snprintf(t, sizeof(t), "Stops after %s: %s didn't answer", R.reached() ? R.nodeName(R.reached()) : "this device",
                 R.nodeName(R.reached() + 1));
      else if (R.toNode())
        snprintf(t, sizeof(t), "Every repeater answered: %s didn't (it may not repeat)", R.targetName());
      else
        snprintf(t, sizeof(t), "Every repeater answered this time");
      break;
    case PathEditor::FAILED:
      switch (R.failure()) {
        case PathEditor::FAIL_NO_KEY: snprintf(t, sizeof(t), "Can't trace: a repeater has no key known (add it from contacts)"); break;
        case PathEditor::FAIL_BUSY:   snprintf(t, sizeof(t), "A ping is on its way: try again in a moment"); break;
        case PathEditor::FAIL_EMPTY:  snprintf(t, sizeof(t), "Nothing to trace: add a repeater, or trace to the node"); break;
        default:                      snprintf(t, sizeof(t), "The trace couldn't be sent"); break;
      }
      break;
    default:
      if (R.edited()) snprintf(t, sizeof(t), "%d repeater%s, not saved yet", n, n == 1 ? "" : "s");
      else if (!R.hasStoredPath()) snprintf(t, sizeof(t), "No route stored: messages are flooded");
      else snprintf(t, sizeof(t), "Stored route: %d repeater%s%s", n, n == 1 ? "" : "s", R.pinned() ? ", kept" : "");
  }
  setText(_route_status, t);
  if (_route_trace_btn) setText(lv_obj_get_child(_route_trace_btn, 0), R.state() == PathEditor::RUNNING ? LV_SYMBOL_CLOSE " Stop" : LV_SYMBOL_LOOP " Trace");
  if (_route_save_btn) barButtonOn(_route_save_btn, R.edited());
  _route_shown = routeSig();
}

// From loop() on SCR_ROUTE: a running trace's reply or timeout, a probe's steps.
void UITask::routePoll() {
  _core->route.poll();
  if (routeSig() != _route_shown) routeRebuild();
  if (routeDiscSig() != _route_disc_shown) routeDiscRebuild();
}

// ── Discover ──────────────────────────────────────────────────────────────────

uint32_t UITask::routeDiscSig() {
  PathEditor& R = _core->route;
  return (uint32_t)R.discovering() | (uint32_t)R.discoverWaiting() << 1 | (uint32_t)R.discoverDone() << 2 |
         (uint32_t)R.discoverAnswered() << 6 | (uint32_t)R.foundCount() << 10 | (uint32_t)R.discoverNextIn() << 13 |
         (uint32_t)R.discoverFailed() << 20 | (uint32_t)routeview::s_tries << 21 | (uint32_t)(R.discoverTries() > 0) << 23;
}

// Under the chain: how many tries and Discover (Stop while it runs), where
// it is, then each route found -- its repeaters, how often it came, the way
// back -- a tap putting it in the list above.
void UITask::routeDiscRebuild() {
  if (!_route_disc) return;
  using namespace routeview;
  PathEditor& R = _core->route;
  lv_obj_clean(_route_disc);
  lv_obj_t* g = group(_route_disc, "FIND THE ROUTE");
  lv_obj_t* row = groupLine(g, false);
  groupText(row, "Discover", "Ask the node which way it's reached: a flood each try, 20 s apart");
  lv_obj_t* seg = segmented(row, TRIES_MAP, s_tries, 84, 30);
  lv_obj_add_event_cb(seg, onRouteTries, LV_EVENT_VALUE_CHANGED, NULL);

  char t[160];
  t[0] = '\0';
  if (R.discoverFailed()) snprintf(t, sizeof(t), "Couldn't send the request");
  else if (R.discoverWaiting())
    snprintf(t, sizeof(t), "Try %d of %d: waiting for the reply...", R.discoverTry(), R.discoverTries());
  else if (R.discovering())
    snprintf(t, sizeof(t), "%d of %d answered, next try in %d s", R.discoverAnswered(), R.discoverDone(), R.discoverNextIn());
  else if (R.discoverTries() > 0 && R.discoverAnswered())
    snprintf(t, sizeof(t), "%d of %d answered", R.discoverAnswered(), R.discoverDone());
  else if (R.discoverTries() > 0)
    snprintf(t, sizeof(t), "No answer to %d tr%s. A companion answers only if its Settings > Privacy > Status lets you, a repeater after a login",
             R.discoverDone(), R.discoverDone() == 1 ? "y" : "ies");
  if (t[0]) {
    lv_obj_t* l = groupLine(g, false);
    noteLabel(l, t, THEME_FONT_SMALL, R.discoverFailed() ? theme::FAIL : theme::TEXT_MUTED);
  }
  // Code i * 2 + 1: the way back, turned round into a route there to trace.
  for (int i = 0; i < R.foundCount(); i++) {
    const PathEditor::Found& f = R.found(i);
    char route[96], back[64], sub[128];
    R.foundText(i, false, route, sizeof(route));
    R.foundText(i, true, back, sizeof(back));
    const int hops = PathEditor::hopsOf(f.out_len);
    const bool same = R.foundBackSame(i);
    int o = snprintf(sub, sizeof(sub), "%d hop%s%s, found %u time%s. ", hops, hops == 1 ? "" : "s",
                     hops > PathEditor::MAX_HOPS ? " (too long to use)" : "", (unsigned)f.times, f.times == 1 ? "" : "s");
    if (same) snprintf(sub + o, sizeof(sub) - o, "Back the same way");
    else      snprintf(sub + o, sizeof(sub) - o, "Back: %s", back);
    lv_obj_t* r = groupLine(g, same);
    groupText(r, route, sub);
    if (same) {
      label(r, "Use", THEME_FONT_SMALL, theme::ACCENT);
      lv_obj_add_event_cb(r, onRouteFound, LV_EVENT_CLICKED, (void*)(uintptr_t)(i * 2));
    } else {
      lv_obj_t* col = flexBox(r, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_style_pad_row(col, 4, 0);
      lv_obj_set_width(pillButton(col, "There", theme::SURFACE_2, onRouteFound, i * 2), 64);
      lv_obj_set_width(pillButton(col, "Back", theme::SURFACE_2, onRouteFound, i * 2 + 1), 64);
    }
  }
  lv_obj_t* bar = buttonBar(_route_disc);
  barButton(bar, R.discovering() ? LV_SYMBOL_CLOSE " Stop" : LV_SYMBOL_REFRESH " Discover", onRouteDiscover, 0, !R.discovering());
  _route_disc_shown = routeDiscSig();
}

void UITask::routeDiscover() {
  PathEditor& R = _core->route;
  if (R.discovering()) R.stopDiscover();
  else R.discover(routeview::TRIES[routeview::s_tries]);
  routeDiscRebuild();
}

void UITask::routeDiscTries(int sel) {
  if (sel >= 0 && sel < 3) routeview::s_tries = sel;
}

void UITask::routeUseFound(int code) {
  PathEditor& R = _core->route;
  const int i = code >> 1;
  const bool back = code & 1;
  if (i >= R.foundCount()) return;
  if (PathEditor::foundHops(R.found(i), back) > PathEditor::MAX_HOPS) { showToast("Too long: up to 7 repeaters"); return; }
  if (!R.useFound(i, back)) return;
  showToast(back ? "The way back, turned round, in the list: Trace to check it" : "In the list: Trace to check it, Save to use it");
  routeRebuild();
}

void UITask::routeAct(uint8_t act) {
  PathEditor& R = _core->route;
  switch (act) {
    case routeview::RT_ADD:
      R.cancelTrace();
      if (R.full()) { showToast("The route is full"); break; }
      showRoutePick();
      break;
    case routeview::RT_MAP:
      R.cancelTrace();
      _route_pick = true;
      openMap(false);   // the nodes map: repeaters are tapped into the route
      routeMapFrame();
      break;
    case routeview::RT_TRACE:
      if (R.state() == PathEditor::RUNNING) R.cancelTrace();
      else if (R.trace()) _next_route_ms = millis() + 200;
      routeRebuild();
      break;
    case routeview::RT_PROBE:
      if (R.probe()) _next_route_ms = millis() + 200;
      routeRebuild();
      break;
    case routeview::RT_PIN:
      routeKeep();
      break;
    case routeview::RT_SAVE:
      if (!R.save()) { showToast(R.tooLong() ? "Too long to edit - remove all hops first" : "Can't save this route"); break; }
      showToast(R.count() ? (R.pinned() ? "Route saved and kept" : "Route saved") : "Saved: direct to the node");
      routeRebuild();
      break;
  }
}

void UITask::routeOp(int code) {
  PathEditor& R = _core->route;
  int i = code & 0xFF, op = code >> 8;
  int& sel = routeview::s_sel;
  if (op == routeview::OP_SEL) {
    if (i == routeview::ROW_TARGET) R.setToNode(!R.toNode());
    else sel = sel == i ? -1 : i;
  }
  else if (op == routeview::OP_UP) { if (R.move(i, -1)) sel = i - 1; }
  else if (op == routeview::OP_DOWN) { if (R.move(i, +1)) sel = i + 1; }
  else if (op == routeview::OP_DEL) { R.remove(i); sel = -1; }
  routeRebuild();
}

// ── Picker: repeaters by distance ─────────────────────────────────────────────

void UITask::showRoutePick() {
  _screen = SCR_ROUTE_PICK;
  buildRoutePick();
}

void UITask::buildRoutePick() {
  lv_obj_t* body = newScreen("Add repeater", true);
  vlist::attach(body);
  vlist::begin(body);
  int32_t mlat, mlon;
  bool own = _core->course.currentLocation(mlat, mlon);
  routeview::s_pick_n = 0;
  for (int i = 0; i < the_mesh.getNumContacts() && routeview::s_pick_n < routeview::PICK_MAX; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_REPEATER) continue;
    bool has_pos = c.gps_lat != 0 || c.gps_lon != 0;
    routeview::s_pick[routeview::s_pick_n++] = { (uint16_t)(MAX_ANON_CONTACTS + i),
                                                 own && has_pos ? geo::haversineKm(mlat, mlon, c.gps_lat, c.gps_lon) : -1.0f };
  }
  qsort(routeview::s_pick, routeview::s_pick_n, sizeof(routeview::Pick), routeview::cmpPick);
  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onRoutePickRow, NULL); },
    [](lv_obj_t* row, intptr_t i) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(routeview::s_pick[i].idx, c)) { rowSet(row, "?", NULL); return; }
      char sub[40] = "";
      float km = routeview::s_pick[i].km;
      if (km >= 0) geo::fmtDist(sub, sizeof(sub), km, (s_ui->_prefs && s_ui->_prefs->units_imperial));
      else snprintf(sub, sizeof(sub), "no position");
      int at = s_ui->_core->route.indexOf(c.id.pub_key);
      if (at >= 0) { char t[48]; snprintf(t, sizeof(t), "%s  -  hop %d", sub, at + 1); rowSet(row, c.name, t); }
      else rowSet(row, c.name, sub);
    },
  };
  for (int i = 0; i < routeview::s_pick_n; i++) vlist::add(body, &ROW, i);
  if (!routeview::s_pick_n)
    vlist::own(body, noteLabel(body, "No repeaters yet. They show up here once their advert is heard.", THEME_FONT_BODY, theme::TEXT_MUTED));
  vlist::end(body);
}

void UITask::routePick(int row) {
  if (row < 0 || row >= routeview::s_pick_n) return;
  ContactInfo c;
  if (!the_mesh.getContactByIdx(routeview::s_pick[row].idx, c)) return;
  PathEditor& R = _core->route;
  if (R.full()) { showToast("The route is full"); return; }
  if (!R.addRepeater(c.id.pub_key)) { showToast("Already the last hop, or the node itself"); return; }
  _screen = SCR_ROUTE;
  _nav_back = true;
  buildRoute();
  _nav_back = false;
}

// ── From the map ──────────────────────────────────────────────────────────────

// A tap on a repeater of the nodes map while the route is picked: in, or out
// again if it is already a hop.
void UITask::routeMapTap(const uint8_t* pub_key, uint8_t type) {
  PathEditor& R = _core->route;
  if (type != ADV_TYPE_REPEATER) { showToast("Pick repeaters"); return; }
  int at = R.indexOf(pub_key);
  if (at >= 0) R.remove(at);
  else if (R.full()) { showToast("The route is full"); return; }
  else if (!R.addRepeater(pub_key)) { showToast("Already the last hop, or the node itself"); return; }
  routeMapRefresh();
}

void UITask::routeMapDone() {
  _route_pick = false;
  _screen = SCR_ROUTE;
  _nav_back = true;
  buildRoute();
  _nav_back = false;
}
