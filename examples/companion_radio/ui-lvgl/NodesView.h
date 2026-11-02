#pragma once
// Included into UITask.cpp only, where it stands (it uses what is defined above it).
// Nodes: the nearby list and a node's detail card.

// ── Nearby ────────────────────────────────────────────────────────────────────
// Contacts / live shares / heard adverts from NearbyModel, filtered by type
// chips, sorted by distance or recency. Tap a row for detail. Scan (nodes that
// answer a discover request right now) is a popup over the list with its own
// model, since it is a different set: who is in range, not who is known.

static NearbyModel::Entry s_node;   // the node open in SCR_NODE (a copy: the list re-sorts)
// Its info card's values (UITask::_node_info is the card).
static lv_obj_t *s_nd_type, *s_nd_status, *s_nd_dist, *s_nd_pos, *s_nd_heard, *s_nd_signal, *s_nd_id;
enum : uint8_t { NODE_MSG, NODE_PING, NODE_FAV, NODE_ADD, NODE_DELETE, NODE_NAV, NODE_ADMIN, NODE_WAYPOINT, NODE_PIN,
                 NODE_RESET_PATH, NODE_SHARE, NODE_ROUTE };

static void onNearbyChip(lv_event_t* e) { s_ui->setNearbyFilter((uint8_t)choiceSelected((lv_obj_t*)lv_event_get_target(e))); }
static void onNearbySort(lv_event_t* e) { s_ui->setNearbySort(choiceSelected((lv_obj_t*)lv_event_get_target(e)) == 1); }
static void onNearbyScan(lv_event_t* e) { (void)e; s_ui->startNearbyScan(); }
static void onNearbyRow(lv_event_t* e)  { s_ui->openNode((int)vlist::arg(e)); }
static void onScanRow(lv_event_t* e)    { s_ui->openScanNode((int)vlist::arg(e)); }
static void onScanClose(lv_event_t* e)  { (void)e; s_ui->closeScanPopup(); }
static void onNodeAction(lv_event_t* e) { s_ui->nodeAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

// A button in a title bar -- an icon, or an icon and a word -- in the row
// after the title, in the order made (`right` is ignored there); in a popup's
// own header, `right` px in from its right edge.
static lv_obj_t* headerButton(lv_obj_t* hdr, const char* text, lv_event_cb_t cb, int right, lv_obj_t** label_out) {
  const bool bar = lv_obj_get_style_height(hdr, LV_PART_MAIN) == theme::APPBAR_H;
  lv_obj_t* b = lv_button_create(hdr);
  lv_obj_set_size(b, LV_SIZE_CONTENT, bar ? theme::BAR_BTN_H : 28);
  lv_obj_set_style_min_width(b, 40, 0);   // an icon alone: still a finger's width
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -right, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* l = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_center(l);
  if (label_out) *label_out = l;
  return b;
}

void UITask::showNearby() {
  _screen = SCR_NEARBY;
  _scanning = false;
  buildNearby();   // filter / sort persist
}

void UITask::buildNearby() {
  lv_obj_t* body = newScreen("Nodes", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  if (_header) {   // where they are, then who answers now (the advert: the quick panel)
    headerButton(_header, UI_SYMBOL_MAP, onOpenNodesMap, 0, NULL);
    headerButton(_header, LV_SYMBOL_REFRESH " Scan", onNearbyScan, 0, NULL);
  }

  // One line over the list: how many, then which and in what order.
  _nearby_status = toolbarRow(body);
  lv_obj_t* bar = lv_obj_get_parent(_nearby_status);
  lv_obj_add_event_cb(toolbarChoice(bar, "All\nFavourites\nCompanions\nRepeaters\nRooms\nSensors", _nearby->filter(), "Show"),
                      onNearbyChip, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(toolbarChoice(bar, "Nearest\nLast heard", _nearby->sortMode() == NearbyModel::SORT_TIME ? 1 : 0, "Order"),
                      onNearbySort, LV_EVENT_VALUE_CHANGED, NULL);

  _nearby_list = scrollList(body);
  vlist::attach(_nearby_list);

  _nearby_sig = 0;
  refreshNearbyList();
}

void UITask::setNearbyFilter(uint8_t f) {
  _nearby->setFilter(f);
  buildNearby();
}

void UITask::setNearbySort(bool last_heard) {
  _nearby->setSortMode(last_heard ? NearbyModel::SORT_TIME : NearbyModel::SORT_DIST);
  _nearby_sig = 0;
  refreshNearbyList();
}

void UITask::startNearbyScan() {
  _scanning = true;
  _scan_until_ms = millis() + 8000;
  _next_nearby_ms = millis() + 250;
  the_mesh.sendNodeDiscoverReq();
  if (!_scan_overlay) showScanPopup();
  else { _scan_sig = 0; refreshScanPopup(); }
}

// Dimmed full-screen overlay (swallows taps) holding a panel with the results.
// A child of the current screen, so it goes away with it.
void UITask::showScanPopup() {
  lv_obj_t* panel = popupOpen(screen(), POP_FULL, _scan_overlay);

  lv_obj_t* hdr = lv_obj_create(panel);
  styleSurface(hdr, theme::BG);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 28);
  lv_obj_align(label(hdr, "In range now", THEME_FONT_TITLE, theme::TEXT), LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(hdr, LV_SYMBOL_CLOSE, onScanClose, 0, NULL);
  headerButton(hdr, LV_SYMBOL_REFRESH " Again", onNearbyScan, 44, NULL);

  _scan_status = label(panel, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  _scan_list = scrollList(panel);
  vlist::attach(_scan_list);

  _scan_sig = 0;
  refreshScanPopup();
}

void UITask::closeScanPopup() {
  if (_scan_overlay) lv_obj_delete_async(_scan_overlay);   // may be closing from its own button
  _scan_overlay = _scan_list = _scan_status = nullptr;
  _scanning = false;
}

void UITask::refreshScanPopup() {
  if (!_scan_list) return;
  _scan->refreshScan();
  int n = _scan->count();
  if (_scanning) setTextFmt(_scan_status, LV_SYMBOL_REFRESH "  Listening for replies... %d", n);
  else setTextFmt(_scan_status, "%d node%s answered the discover request", n, n == 1 ? "" : "s");

  uint32_t sig = (uint32_t)n + (_scanning ? 0x10000u : 0);
  for (int i = 0; i < n; i++) {
    const NearbyModel::Entry& e = _scan->at(i);
    sig = sig * 31 + e.rssi * 7 + e.snr_x4 + e.remote_snr_x4 * 3 + e.type * 5 + e.is_known;
    for (int k = 0; k < 4; k++) sig = sig * 31 + e.pub_key[k];
    for (const char* p = e.name; *p; p++) sig = sig * 31 + (uint8_t)*p;
  }
  if (sig == _scan_sig || vlist::pressed(_scan_list)) return;   // held: once it's let go
  _scan_sig = sig;

  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRowValue(list, onScanRow); },
    [](lv_obj_t* row, intptr_t i) {
      const NearbyModel::Entry& e = s_ui->_scan->at((int)i);
      char title[40], sub[48], right[12];
      if (e.name[0]) snprintf(title, sizeof(title), "%s", e.name);
      else snprintf(title, sizeof(title), "%s %02X%02X%02X%02X", NearbyModel::typeName(e.type),
                    e.pub_key[0], e.pub_key[1], e.pub_key[2], e.pub_key[3]);
      snprintf(sub, sizeof(sub), "%s  -  SNR %.1f / %.1f%s", NearbyModel::typeName(e.type),
               e.snr_x4 / 4.0f, e.remote_snr_x4 / 4.0f, e.is_known ? "" : "  -  new");
      snprintf(right, sizeof(right), "%d dBm", e.rssi);
      rowSet(row, title, sub);
      rowValue(row, right, theme::TEXT_MUTED);
    },
  };
  vlist::begin(_scan_list);
  for (int i = 0; i < n; i++) vlist::add(_scan_list, &ROW, i);
  if (n == 0) {
    lv_obj_t* l = noteLabel(_scan_list, _scanning ? "Repeaters and rooms in range will answer."
                            : "Nobody answered. Try again later or move.",
                            THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_obj_set_style_pad_top(l, 8, 0);
    vlist::own(_scan_list, l);
  }
  vlist::end(_scan_list);
}

void UITask::openScanNode(int row) {
  if (row < 0 || row >= _scan->count()) return;
  s_node = _scan->at(row);
  _node_from_scan = true;
  _node_from_map = false;
  _scanning = false;
  _pinging = false;
  _screen = SCR_NODE;
  buildNode();
}

// Changes whenever a rebuild would show something different.
uint32_t UITask::nearbySignature() const {
  uint32_t sig = (uint32_t)_nearby->count() * 2654435761u + _nearby->sortMode() + (_scanning ? 7 : 0)
               + (_prefs && _prefs->units_imperial ? 13 : 0);
  for (int i = 0; i < _nearby->count(); i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    sig = sig * 31 + e.contact_idx + (uint32_t)e.lastmod + (uint32_t)(e.dist_km * 100) + e.rssi + e.fav
        + e.type * 5 + (e.is_live ? 3 : 0) + (e.is_sos ? 17 : 0);
    for (const char* p = e.name; *p; p++) sig = sig * 31 + (uint8_t)*p;
  }
  // Ages are shown in minutes, so let the list re-render once a minute anyway.
  return sig + rtc_clock.getCurrentTime() / 60;
}

// A Nearby row's texts: the name (a star on a favourite), what it is and how
// long ago it was heard, and on the right its distance (or the age).
struct NearbyText { char title[48], sub[48], right[16]; uint32_t right_col; };
static void nearbyText(const NearbyModel::Entry& e, uint32_t now, bool imperial, NearbyText& t) {
  const char* name = e.name[0] ? e.name : "(unknown)";
  snprintf(t.title, sizeof(t.title), "%s%s", e.fav ? UI_SYMBOL_STAR " " : "", name);
  char age[8];
  geo::fmtAgeShort(age, sizeof(age), now, e.lastmod);
  t.right[0] = 0;
  if (e.dist_km >= 0.0f) geo::fmtDist(t.right, sizeof(t.right), e.dist_km, imperial);
  else if (age[0]) snprintf(t.right, sizeof(t.right), "%s", age);
  const char* kind = e.contact_idx >= 0 ? NearbyModel::typeName(e.type) : "not a contact";
  snprintf(t.sub, sizeof(t.sub), "%s%s%s%s", kind, e.is_sos ? "  -  SOS" : e.is_live ? "  -  live" : "",
           (e.dist_km >= 0.0f && age[0]) ? "  -  " : "", (e.dist_km >= 0.0f && age[0]) ? age : "");
  t.right_col = e.is_sos ? theme::FAIL : e.is_live ? theme::OK : theme::TEXT_MUTED;
}

// A small arrow towards the node, drawn in the value label's left padding:
// its user data is the bearing + 1 (0: none).
static const int ROW_ARROW_W = 14;
static void onRowArrowDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  const int b = (int)(intptr_t)lv_obj_get_user_data(o) - 1;
  if (b < 0) return;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const float cx = a.x1 + ROW_ARROW_W / 2 - 1, cy = (a.y1 + a.y2) / 2.0f;
  const float ang = b * (float)M_PI / 180.0f, sx = sinf(ang), sy = -cosf(ang);
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = lv_obj_get_style_text_color(o, LV_PART_MAIN);
  tri.opa = LV_OPA_COVER;
  tri.p[0].x = cx + sx * 6;           tri.p[0].y = cy + sy * 6;                 // the tip
  tri.p[1].x = cx - sx * 5 + sy * 4;  tri.p[1].y = cy - sy * 5 - sx * 4;
  tri.p[2].x = cx - sx * 5 - sy * 4;  tri.p[2].y = cy - sy * 5 + sx * 4;
  lv_draw_triangle(lv_event_get_layer(e), &tri);
}

void UITask::refreshNearbyList() {
  if (!_nearby_list) return;
  _nearby->refreshModel();
  uint32_t sig = nearbySignature();
  int n = _nearby->count();

  int32_t lat, lon;
  bool gps = _nearby->ownPosition(lat, lon);
  setTextFmt(_nearby_status, "%d node%s%s", n, n == 1 ? "" : "s", gps ? "" : ", no GPS");
  if (sig == _nearby_sig || vlist::pressed(_nearby_list)) return;   // held: once it's let go
  _nearby_sig = sig;

  // The rows in view rewritten from the model (ages, distances, a new
  // order); the list stays where it was scrolled.
  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) {
      lv_obj_t* row = vRowValue(list, onNearbyRow);
      lv_obj_add_event_cb(lv_obj_get_child(row, 2), onRowArrowDraw, LV_EVENT_DRAW_MAIN_END, NULL);
      return row;
    },
    [](lv_obj_t* row, intptr_t i) {
      const NearbyModel::Entry& e = s_ui->_nearby->at((int)i);
      NearbyText t;
      nearbyText(e, rtc_clock.getCurrentTime(), s_ui->_prefs && s_ui->_prefs->units_imperial, t);
      rowSet(row, t.title, t.sub, e.fav ? theme::ACCENT : theme::TEXT);
      rowValue(row, t.right, t.right_col);
      // By the distance, an arrow towards it
      int b = -1;
      int32_t lat, lon;
      if (e.dist_km >= 0.0f && s_ui->_nearby->ownPosition(lat, lon)) b = geo::bearingDeg(lat, lon, e.lat_e6, e.lon_e6);
      lv_obj_t* v = lv_obj_get_child(row, 2);
      if ((intptr_t)lv_obj_get_user_data(v) != b + 1) {
        lv_obj_set_user_data(v, (void*)(intptr_t)(b + 1));
        lv_obj_set_style_pad_left(v, b >= 0 ? ROW_ARROW_W : 0, 0);
        lv_obj_invalidate(v);
      }
    },
  };
  vlist::begin(_nearby_list);
  for (int i = 0; i < n; i++) vlist::add(_nearby_list, &ROW, i);
  if (n == 0) {
    lv_obj_t* l = label(_nearby_list, "Nobody here yet. Tap Scan to look around.",
                        THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_obj_set_style_pad_top(l, 12, 0);
    vlist::own(_nearby_list, l);
  }
  vlist::end(_nearby_list);
}

// ── Node detail ───────────────────────────────────────────────────────────────

void UITask::openNode(int row) {
  if (row < 0 || row >= _nearby->count()) return;
  s_node = _nearby->at(row);
  _node_from_scan = false;
  _node_from_map = false;
  _pinging = false;
  _screen = SCR_NODE;
  buildNode();
}

static lv_obj_t* s_nd_fav = nullptr;
static void nodeFavText() {
  if (s_nd_fav) lv_label_set_text(s_nd_fav, s_node.fav ? "Remove from favourites" : "Add to favourites");   // the star in its icon column
}

// One of the node's "Can ask for" switches; user data = its TELEM_PERM_* bit.
static void onNodeTelemSwitch(lv_event_t* e) {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(s_node.pub_key, PUB_KEY_SIZE);
  if (!ci) return;
  uint8_t bit = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  uint8_t perms = contactctl::telemetry(*ci);
  if (lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED)) perms |= bit; else perms &= ~bit;
  contactctl::setTelemetry(s_node.pub_key, perms);
}

void UITask::buildNode() {
  const NearbyModel::Entry& e = s_node;
  lv_obj_t* body = newScreen(e.name[0] ? e.name : "(unknown)", true);

  lv_obj_t* info = scrollList(body);   // the actions, then the info: the whole screen scrolls
  s_nd_fav = nullptr;
  _node_path_lbl = nullptr;

  // The bar: what the node is for -- a message, its admin, adding it -- a
  // ping and the way there. The rest, less often, in a group under the info.
  lv_obj_t* acts = buttonBar(info);
  bool contact = e.contact_idx >= 0;
  bool admin = contact && (e.type == ADV_TYPE_REPEATER || e.type == ADV_TYPE_ROOM);
  bool pos = e.lat_e6 != 0 || e.lon_e6 != 0;
  if (contact && e.type == ADV_TYPE_CHAT) barButton(acts, LV_SYMBOL_ENVELOPE " Message", onNodeAction, NODE_MSG, true);
  if (admin) barButton(acts, LV_SYMBOL_SETTINGS " Admin", onNodeAction, NODE_ADMIN, true);
  if (!contact && e.has_key && !e.is_known) barButton(acts, LV_SYMBOL_PLUS " Add", onNodeAction, NODE_ADD, true);
  if (e.has_key) barButton(acts, LV_SYMBOL_LOOP " Ping", onNodeAction, NODE_PING);
  if (pos) barButton(acts, UI_SYMBOL_COMPASS " Navigate", onNodeAction, NODE_NAV);
  if (!lv_obj_get_child_count(acts)) lv_obj_delete(acts);
  _node_ping = label(info, "", THEME_FONT_BODY, theme::ACCENT);   // under the buttons, once there's a ping
  lv_obj_add_flag(_node_ping, LV_OBJ_FLAG_HIDDEN);

  nodeVizBuild(info);
  nodeviz::s_bearing = nodeviz::s_hops = -2;   // nothing drawn yet: the first set fills both
  _node_info = infoCard(info);
  s_nd_type = infoRow(_node_info, "Type", "");
  s_nd_status = infoRow(_node_info, "Status", "");
  s_nd_dist = infoRow(_node_info, "Distance", "");
  s_nd_pos = infoRow(_node_info, "Position", "");
  s_nd_heard = infoRow(_node_info, "Heard", "");
  s_nd_signal = infoRow(_node_info, "Signal", "");
  s_nd_id = infoRow(_node_info, "ID", "");

  if (pos || contact) {
    lv_obj_t* g = group(info, "MORE");
    if (pos) actionRow(g, UI_SYMBOL_FLAG "  Save as waypoint", onNodeAction, (void*)(uintptr_t)NODE_WAYPOINT);
    if (contact) s_nd_fav = actionRow(g, UI_SYMBOL_STAR "  ", onNodeAction, (void*)(uintptr_t)NODE_FAV);
    nodeFavText();
    if (contact && e.has_key) actionRow(g, UI_SYMBOL_TACK "  Pin to Home", onNodeAction, (void*)(uintptr_t)NODE_PIN);
    ContactInfo* ci = (contact && e.has_key) ? the_mesh.lookupContactByPubKey(e.pub_key, PUB_KEY_SIZE) : nullptr;
    if (ci) actionRow(g, UI_SYMBOL_ROUTE "  Route and trace", onNodeAction, (void*)(uintptr_t)NODE_ROUTE);
    if (ci && contactctl::hasPath(*ci))
      _node_path_lbl = actionRow(g, LV_SYMBOL_REFRESH "  Reset path", onNodeAction, (void*)(uintptr_t)NODE_RESET_PATH);
    if (ci) actionRow(g, LV_SYMBOL_UPLOAD "  Share with nodes in range", onNodeAction, (void*)(uintptr_t)NODE_SHARE);
    if (contact) _node_delete_lbl = actionRow(g, LV_SYMBOL_TRASH "  Delete contact", onNodeAction,
                                              (void*)(uintptr_t)NODE_DELETE, theme::FAIL);
  }

  // Where Settings > Privacy says "Allowed", what this contact may ask for.
  ContactInfo* ci = (contact && e.has_key) ? the_mesh.lookupContactByPubKey(e.pub_key, PUB_KEY_SIZE) : nullptr;
  if (ci && contactctl::telemetryPerContact(_prefs)) {
    lv_obj_t* g = group(info, "CAN ASK FOR");
    static const struct { const char* text; const char* sub; uint8_t bit; } T[] = {
      { "Status", "Battery; the two below need it", TELEM_PERM_BASE },
      { "Location", nullptr, TELEM_PERM_LOCATION },
      { "Sensors", nullptr, TELEM_PERM_ENVIRONMENT },
    };
    uint8_t perms = contactctl::telemetry(*ci);
    for (const auto& t : T) {
      switchRow(g, t.text, t.sub, perms & t.bit, onNodeTelemSwitch, (void*)(uintptr_t)t.bit);
    }
  }

  refreshNode();
}

void UITask::refreshNode() {
  if (!_node_info) return;
  const NearbyModel::Entry& e = s_node;
  // Refresh position / age / signal from its model while the node is still listed.
  NearbyModel* model = _node_from_scan ? _scan : _nearby;
  model->refreshModel();
  for (int i = 0; i < model->count(); i++) {
    const NearbyModel::Entry& m = model->at(i);
    bool same = e.has_key ? (m.has_key && memcmp(m.pub_key, e.pub_key, PUB_KEY_SIZE) == 0)
              : (e.contact_idx >= 0) ? m.contact_idx == e.contact_idx
              : strncmp(m.name, e.name, sizeof(m.name)) == 0;
    if (same) { s_node = m; break; }
  }

  // A scan row carries no contact index; is_known says whether it's in the contacts.
  bool known = e.contact_idx >= 0 || (_node_from_scan && e.is_known);
  char status[64] = "", dist[40] = "", pos[32] = "", heard[16] = "", sig[40] = "", id[12] = "";
  int o = 0;
  auto tag = [&](const char* t) { o += snprintf(status + o, sizeof(status) - o, "%s%s", o ? ", " : "", t); };
  if (!known) tag("not a contact");
  if (e.is_sos) tag(e.live_verified ? "SOS position" : "SOS position (channel)");
  else if (e.is_live) tag(e.live_verified ? "live position" : "live position (channel)");
  if (e.fav) tag("favourite");
  if (status[0] >= 'a' && status[0] <= 'z') status[0] -= 'a' - 'A';
  int32_t lat, lon;
  int bearing = -1;
  if (e.lat_e6 != 0 || e.lon_e6 != 0) {
    if (_nearby->ownPosition(lat, lon) && e.dist_km >= 0.0f) {
      char d[16];
      geo::fmtDist(d, sizeof(d), e.dist_km, _prefs && _prefs->units_imperial);
      int az = geo::bearingDeg(lat, lon, e.lat_e6, e.lon_e6);
      bearing = az;
      snprintf(dist, sizeof(dist), "%s  %d\xC2\xB0 %s", d, az, geo::bearingCardinal(az));
    }
    geo::fmtLatLon(pos, sizeof(pos), e.lat_e6, e.lon_e6, true);
  } else if (!_node_from_scan) {
    snprintf(pos, sizeof(pos), "Not shared");
  }
  char age[8];
  geo::fmtAgeShort(age, sizeof(age), rtc_clock.getCurrentTime(), e.lastmod);
  if (age[0]) snprintf(heard, sizeof(heard), "%s ago", age);
  if (_node_from_scan)
    snprintf(sig, sizeof(sig), "%d dBm, SNR %.1f / %.1f", e.rssi, e.snr_x4 / 4.0f, e.remote_snr_x4 / 4.0f);
  if (e.has_prefix) snprintf(id, sizeof(id), "%02X%02X%02X%02X", e.pub_key[0], e.pub_key[1], e.pub_key[2], e.pub_key[3]);
  infoSet(s_nd_type, NearbyModel::typeName(e.type));
  infoSet(s_nd_status, status);
  infoSet(s_nd_dist, dist);
  infoSet(s_nd_pos, pos);
  infoSet(s_nd_heard, heard);
  infoSet(s_nd_signal, sig);
  infoSet(s_nd_id, id);
  ContactInfo ci;
  int hops = -1;
  if (!_node_from_scan && e.contact_idx >= 0 && the_mesh.getContactByIdx(e.contact_idx, ci))
    hops = ci.out_path_len == 0xFF ? 0xFF : (ci.out_path_len & 63);
  nodeVizSet(bearing, hops);

  // Ping result (PingEngine releases its slot on reply; the view owns the timeout)
  if (_pinging) {
    int16_t out = 0, back = 0; uint32_t rtt = 0;
    _core->ping.getResult(out, back, rtt);
    if (!_core->ping.isActive() && (out || back || rtt)) {
      setTextFmt(_node_ping, "Ping %lu ms  -  SNR out %.1f  back %.1f", (unsigned long)rtt,
                            out / 4.0f, back / 4.0f);
      lv_obj_remove_flag(_node_ping, LV_OBJ_FLAG_HIDDEN);
      _pinging = false;
    } else if (millis() - _ping_started_ms > _core->ping.timeoutMs()) {
      _core->ping.clear();
      setText(_node_ping, "Ping: no reply");
      lv_obj_remove_flag(_node_ping, LV_OBJ_FLAG_HIDDEN);
      _pinging = false;
    }
  }
}

void UITask::nodeAction(uint8_t action) {
  NearbyModel::Entry& e = s_node;
  switch (action) {
    case NODE_MSG:
      openDM(e.pub_key);
      break;
    case NODE_PING: {
      if (_pinging) break;
      PingEngine::StartResult r = _core->ping.start(e.pub_key);
      lv_obj_remove_flag(_node_ping, LV_OBJ_FLAG_HIDDEN);
      if (r == PingEngine::STARTED) {
        _pinging = true;
        _ping_started_ms = millis();
        _next_nearby_ms = millis() + 250;
        lv_label_set_text(_node_ping, "Pinging...");
      } else {
        lv_label_set_text(_node_ping, "Ping failed");
      }
      break;
    }
    case NODE_FAV:
      if (contactctl::setFavourite(e.pub_key, !e.fav)) {   // in place: the list stays where it was scrolled
        e.fav = !e.fav;
        nodeFavText();
        refreshNode();
      }
      break;
    case NODE_ADD:
      if (the_mesh.addDiscoveredContact(e.pub_key, e.name, e.type)) {
        _screen = SCR_NEARBY;
        buildNearby();
      } else {
        showToast("Contacts full");
      }
      break;
    case NODE_NAV:
      // Followed by key when there is one (the Locator resolves live share,
      // then advert position); otherwise a fixed point where it was seen.
      if (e.has_prefix) navToNode(e.pub_key, e.lat_e6, e.lon_e6, e.name[0] ? e.name : "Node");
      else {
        _core->locator.setTarget(0, nullptr, e.lat_e6, e.lon_e6, e.name[0] ? e.name : "Node");
        prefsSave();
        openMap(true);
        navFrameTarget();
      }
      break;
    case NODE_ADMIN:
      openAdmin(e.pub_key);
      break;
    case NODE_WAYPOINT: {
      if (_core->waypoints.full()) { showToast(waypointsFull()); break; }
      char t[48];
      if (_core->waypoints.add(e.lat_e6, e.lon_e6, rtc_clock.getCurrentTime(), e.name[0] ? e.name : "Node")) {
        snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(_core->waypoints.count() - 1).label);
        showToast(t);
      }
      break;
    }
    case NODE_PIN:   // to the favourites dial (Home), as from a chat's options
      pinPopup(false, 0, e.pub_key);
      break;
    case NODE_RESET_PATH:   // the row goes, the chain above turns to flood
      if (contactctl::resetPath(e.pub_key) && _node_path_lbl) {
        lv_obj_add_flag(lv_obj_get_parent(lv_obj_get_parent(_node_path_lbl)), LV_OBJ_FLAG_HIDDEN);   // label < text box < row
        _node_path_lbl = nullptr;
        refreshNode();
      }
      break;
    case NODE_ROUTE:
      showRoute(e.pub_key);
      break;
    case NODE_SHARE:
      showToast(contactctl::share(e.pub_key) ? "Sent to nodes in range" : "No advert saved to send");
      break;
    case NODE_DELETE:
      if (!tapConfirmed(_node_delete_lbl, "Tap again to delete")) break;
      if (the_mesh.deleteContactByKey(e.pub_key)) {
        _screen = SCR_NEARBY;
        buildNearby();
      }
      break;
  }
}
