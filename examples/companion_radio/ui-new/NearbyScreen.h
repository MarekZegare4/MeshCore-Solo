#pragma once
#include "InfoKit.h"
#include "../Features.h"
#include "../GeoUtils.h"
#include "NavView.h"
#include "TabBar.h"

// ── Nearby Nodes ──────────────────────────────────────────────────────────────
// One list / detail / action-menu interaction path over two sources:
//   SRC_STORED — contacts known to the mesh (distance / bearing / last-heard)
//   SRC_SCAN   — live NODE_DISCOVER_REQ results (RSSI / SNR / status)
// Filter (type) and sort are independent axes and combine freely. The action
// menu (Hold Enter) is identical everywhere; only the per-row column and the
// detail fields differ between sources.
class NearbyScreen : public UIScreen, protected NearbyModel {
  UITask* _task;

  // List data, filter / sort / source and the refresh logic live in
  // ui-core/NearbyModel.h (shared with ui-lvgl); this class adds selection,
  // detail / navigate views, scan timing and the action menu.

  // ── action-menu actions (matched by id, not by row index) ────────────────────
  enum Action : uint8_t { ACT_NAV, ACT_PING, ACT_WAYPOINT, ACT_LOCATOR,
                          ACT_ADD, ACT_DELETE, ACT_FAV, ACT_PIN, ACT_ADMIN, ACT_SORT, ACT_SCAN, ACT_MAP };

  // Set by UITask::pickAdminTarget() (Tools > Admin, which is remote-only):
  // while true, ENTER on an eligible row (a stored repeater/room contact) hands
  // the node straight to Admin instead of opening the detail view -- everything
  // else (filters, scan, ping, sort, the Hold-Enter menu) behaves identically to
  // normal Nodes browsing, so picking a node for Admin looks exactly like using
  // this screen for anything else. Mirrors MessagesScreen's
  // startPickBotChannel()/startPickBotRoom() pick-mode idiom.
  bool _pick_admin_target = false;

  int     _sel;
  int     _scroll;
  bool    _detail;
  bool    _nav = false;     // full-screen navigate-to-node view (over detail)
  bool    _map = false;     // the nodes with a position on a map (under detail)
  int     _map_zoom = 0;    // 0: all of them in view; each step halves it, on the selected
  navview::EtaTracker _nav_eta;  // closing-speed/ETA for the navigate view
  unsigned long _detail_refresh_ms;
  unsigned long _list_refresh_ms = 0;
  static const unsigned long DETAIL_REFRESH_MS    = 10000UL;
  static const unsigned long NAV_REFRESH_MS       = 1000UL;   // navigate view tracks a moving target
  static const unsigned long TIME_LIST_REFRESH_MS = 3000UL;

  // ── live-scan state ──────────────────────────────────────────────────────────
  bool          _scanning;
  unsigned long _scan_started_ms;
  static const unsigned long SCAN_DURATION_MS = 8000UL;

  // ── popups ────────────────────────────────────────────────────────────────────
  PopupMenu _menu;            // unified action menu (Hold Enter), list + detail
  PopupMenu _ping_menu;       // ping (special: read-only result rows)
  PopupMenu _confirm;         // delete-contact confirmation (destructive → 2-step)

  Action  _menu_actions[12];  // parallel to _menu rows — stable action ids
  int     _menu_action_count;
  char    _sort_label[16];    // dynamic label for the Sort row

  // ── ping state ───────────────────────────────────────────────────────────────
  char _ping_time_str[24];
  char _ping_snr_out_str[24];
  char _ping_snr_back_str[24];
  bool _pinging;
  unsigned long _ping_started_ms;
  static const unsigned long PING_TIMEOUT_MS = 3000UL;

  // ── helpers ──────────────────────────────────────────────────────────────────
  static void pubKeyToBase64(const uint8_t* key, char* out, int out_len) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int j = 0;
    for (int i = 0; i < PUB_KEY_SIZE && j + 5 < out_len; i += 3) {
      uint32_t b = ((uint32_t)key[i] << 16)
                 | (i+1 < PUB_KEY_SIZE ? (uint32_t)key[i+1] << 8 : 0)
                 | (i+2 < PUB_KEY_SIZE ? (uint32_t)key[i+2]      : 0);
      out[j++] = T[(b >> 18) & 63];
      out[j++] = T[(b >> 12) & 63];
      if (i+1 < PUB_KEY_SIZE) out[j++] = T[(b >> 6) & 63];
      if (i+2 < PUB_KEY_SIZE) out[j++] = T[b & 63];
    }
    out[j] = '\0';
  }

  bool useImperial() const { return _task && _task->useImperial(); }

  // Full "X ago" form for the detail view, built on the shared short-age tag
  // so there's one bucket ladder (see geo::fmtAgeShort).
  static void fmtAge(char* buf, int n, uint32_t lastmod) {
    char s[8];
    geo::fmtAgeShort(s, sizeof(s), rtc_clock.getCurrentTime(), lastmod);
    if (!s[0]) { snprintf(buf, n, "unknown"); return; }
    snprintf(buf, n, "%s ago", s);
  }

  // The selected entry, or nullptr when the list is empty.
  const Entry* selected() const {
    return (_count > 0 && _sel < _count) ? &_entries[_sel] : nullptr;
  }

  // ── data refresh (NearbyModel) + selection clamping ─────────────────────────
  void refreshStored() { NearbyModel::refreshStored(); clampSelection(); }
  void refreshScan()   { NearbyModel::refreshScan();   clampSelection(); }
  void refresh()       { refreshModel(); clampSelection(); }

  // Rebuild the stored list (re-merge live shares + re-sort) while keeping the
  // currently highlighted node selected across the rebuild — by contact index
  // for a stored node, or by name for a non-contact live sender. Returns false
  // if that node is no longer in the list (caller decides whether to drop the
  // detail/nav view). Shared by the list, detail and navigate refresh paths.
  bool refreshKeepingSelection() {
    int  saved_idx  = (_sel < _count) ? _entries[_sel].contact_idx : -1;
    bool saved_live = (_sel < _count) && _entries[_sel].is_live && saved_idx < 0;
    char saved_name[sizeof(_entries[0].name)]; saved_name[0] = '\0';
    if (_sel < _count) {
      strncpy(saved_name, _entries[_sel].name, sizeof(saved_name) - 1);
      saved_name[sizeof(saved_name) - 1] = '\0';
    }
    refreshStored();
    int i = saved_idx >= 0 ? findContact(saved_idx)
          : (saved_live && saved_name[0]) ? findLiveByName(saved_name) : -1;
    if (i >= 0) { _sel = i; return true; }
    return false;
  }

  void clampSelection() {
    if (_count == 0)            { _sel = _scroll = 0; }
    else if (_sel >= _count)    { _sel = _count - 1; if (_scroll > _sel) _scroll = _sel; }
  }

  // ── live scan ────────────────────────────────────────────────────────────────
  void enterScan() {
    _source        = SRC_SCAN;
    _detail        = false;
    _nav           = false;
    _scanning      = true;
    _scan_started_ms = millis();
    _sel = _scroll = 0;
    the_mesh.sendNodeDiscoverReq();
    refreshScan();
  }

  void leaveScan() {
    _source = SRC_STORED;
    _detail = false;
    _nav    = false;
    _sel = _scroll = 0;
    refreshStored();
  }

  // Save the currently-selected entry as a waypoint (its name as label).
  void saveSelectedWaypoint() {
    const Entry* e = selected();
    if (!e) return;
    if (e->lat_e6 == 0 && e->lon_e6 == 0) { _task->showAlert("No node GPS", 1000); return; }
    _task->addWaypoint(e->lat_e6, e->lon_e6, e->name);   // WaypointStore truncates the label
  }

  // Toggle the given contact in the Favourites dial: unpin if already pinned, else
  // pin to the first empty slot. Persisted immediately (same as the Messages picker).
  // Unlike the Messages lists, this one doesn't ask which slot -- it takes the
  // first free one. The menu here is already long, and the dial itself can
  // rearrange afterwards.
  void togglePinToDial(const uint8_t* pub_key) {
    int slot = _task->findFavouriteSlot(pub_key);
    if (slot >= 0) {
      _task->clearFavouriteSlot(slot);
      the_mesh.savePrefs();
      char alert[24];
      snprintf(alert, sizeof(alert), "Unpinned (slot %d)", slot + 1);
      _task->showAlert(alert, 1000);
      return;
    }
    for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) {
      if (_task->isFavouriteSlotEmpty(s)) {
        _task->setFavouriteSlot(s, pub_key);
        the_mesh.savePrefs();
        char alert[24];
        snprintf(alert, sizeof(alert), "Pinned to slot %d", s + 1);
        _task->showAlert(alert, 1000);
        return;
      }
    }
    _task->showAlert("Dial full", 1200);
  }

  // Deleting a contact is destructive → confirm first (default highlight = Cancel).
  void startDeleteConfirm() {
    const Entry* e = selected();
    if (!e || !e->has_key || !entryIsContact(e)) return;
    _confirm.beginConfirm("Delete contact?", "Delete");
  }

  void doDeleteSelected() {
    const Entry* e = selected();
    if (!e || !e->has_key || !entryIsContact(e)) return;
    uint8_t key[PUB_KEY_SIZE];
    memcpy(key, e->pub_key, PUB_KEY_SIZE);
    if (the_mesh.deleteContactByKey(key)) {
      _detail = false;   // the node this detail/nav view showed is gone
      _nav    = false;
      refresh();
      clampSelection();
    }
  }

  // ── ping ──────────────────────────────────────────────────────────────────────
  void resetPingLines() {
    _ping_time_str[0] = '\0';
    _ping_snr_out_str[0] = '\0';
    _ping_snr_back_str[0] = '\0';
  }

  int pingRowCount() const {
    return 1 + (_ping_time_str[0] ? 1 : 0)
             + (_ping_snr_out_str[0] ? 1 : 0)
             + (_ping_snr_back_str[0] ? 1 : 0);
  }

  void rebuildPingMenu() {
    int keep = _ping_menu.selectedIndex();  // preserve selection across a rebuild
    _ping_menu.begin("Ping", 4);
    _ping_menu.addItem("Send");
    if (_ping_time_str[0])      _ping_menu.addItem(_ping_time_str);
    if (_ping_snr_out_str[0])   _ping_menu.addItem(_ping_snr_out_str);
    if (_ping_snr_back_str[0])  _ping_menu.addItem(_ping_snr_back_str);
    _ping_menu.setSelected(keep);
  }

  void closePingMenu(bool clear_task = true) {
    _ping_menu.active = false;
    _pinging = false;
    if (clear_task && _task) _task->clearPing();
    resetPingLines();
  }

  void startPingForKey(const uint8_t* pub_key) {
    resetPingLines();
    snprintf(_ping_time_str, sizeof(_ping_time_str), "RTT: ...");
    if (_task && _task->startPing(pub_key)) {
      _pinging = true;
      _ping_started_ms = millis();
    } else {
      snprintf(_ping_time_str, sizeof(_ping_time_str), "RTT: send fail");
    }
    if (_ping_menu.active) rebuildPingMenu();   // surface "RTT: ..." immediately
  }

  void updatePingMenuState() {
    if (!_ping_menu.active || !_task || (!_pinging && !_task->isPingActive())) return;

    int16_t snr_out = 0, snr_back = 0;
    uint32_t rtt = 0;
    _task->getPingResult(snr_out, snr_back, rtt);

    if (_pinging && (snr_out != 0 || snr_back != 0 || rtt != 0)) {
      if (rtt > 0 && rtt < 10000) {
        snprintf(_ping_time_str, sizeof(_ping_time_str), "RTT: %lums", rtt);
      } else {
        snprintf(_ping_time_str, sizeof(_ping_time_str), "RTT: timeout");
      }
      if (snr_out != 0)
        snprintf(_ping_snr_out_str, sizeof(_ping_snr_out_str), "SNR out: %.1f", snr_out / 4.0f);
      if (snr_back != 0)
        snprintf(_ping_snr_back_str, sizeof(_ping_snr_back_str), "SNR back: %.1f", snr_back / 4.0f);
      _pinging = false;
    } else if (_pinging && millis() - _ping_started_ms >= PING_TIMEOUT_MS) {
      snprintf(_ping_time_str, sizeof(_ping_time_str), "RTT: timeout");
      _ping_snr_out_str[0] = '\0';
      _ping_snr_back_str[0] = '\0';
      _pinging = false;
      if (_task) _task->clearPing();
    }
    if (pingRowCount() != _ping_menu.count()) rebuildPingMenu();
  }

  // Ping popup input. Result rows are read-only, so UP/DOWN are swallowed to keep
  // the highlight on "Send".
  void handlePingMenuInput(char c) {
    if (c == KEY_UP || c == KEY_DOWN) return;
    auto res = _ping_menu.handleInput(c);
    if (res == PopupMenu::SELECTED) {
      const Entry* e = selected();
      if (!_pinging && e && e->has_key) startPingForKey(e->pub_key);
      _ping_menu.active = true;   // stay open so Ping can be repeated
    } else if (res == PopupMenu::CANCELLED) {
      closePingMenu();
    }
  }

  // ── action menu (Hold Enter) — same everywhere ──────────────────────────────
  void buildSortLabel() {
    snprintf(_sort_label, sizeof(_sort_label),
             "Sort: %s", _sort == SORT_TIME ? "Recent" : "Dist");
  }

  // A row is already a contact when it carries a contacts[] index (stored source)
  // or the live-scan flagged it known. Full 32-byte key required to add/delete.
  bool entryIsContact(const Entry* e) const {
    return e && ((e->contact_idx >= 0) || (_source == SRC_SCAN && e->is_known));
  }

  char _fav_label[12];   // "Fav: ON" / "Fav: OFF" -- rewritten in place by L/R
  char _pin_label[24];   // "Pin to dial" / "Unpin (slot N)" -- _menu stores the pointer

  void openActionMenu() {
    const Entry* e = selected();
    bool stored  = (_source == SRC_STORED);
    bool has_gps = e && stored && (e->lat_e6 != 0 || e->lon_e6 != 0);
    bool has_key = e && e->has_key;
    bool is_contact = entryIsContact(e);
    bool can_add = e && has_key && !is_contact;   // a new node we can save
    bool is_pinned = e && has_key && _task->findFavouriteSlot(e->pub_key) >= 0;
    // Admin needs a real saved contact (repeater/room), not a scan result or a
    // name-only live-share row -- same gating as startPickAdminTarget()'s ENTER.
    bool is_admin_target = e && stored && e->contact_idx >= 0
                           && (e->type == ADV_TYPE_REPEATER || e->type == ADV_TYPE_ROOM);

    buildSortLabel();
    _menu_action_count = 0;
    _menu.begin("Options", 10);
    auto add = [&](const char* label, Action a) {
      _menu.addItem(label);
      _menu_actions[_menu_action_count++] = a;
    };
    auto addValue = [&](const char* label, Action a) {
      _menu.addValueItem(label);
      _menu_actions[_menu_action_count++] = a;
    };

    if (stored && !_map && positioned(0, 1) >= 0) add("Map", ACT_MAP);
    if (has_gps) add("Navigate",      ACT_NAV);
    if (has_key) add("Ping",          ACT_PING);
    if (has_gps) add("Save waypoint", ACT_WAYPOINT);
    // Only a position is needed: with an identity prefix this becomes a person
    // target that follows them, without one a place target pinned where they were.
    if (has_gps) add("Set as target", ACT_LOCATOR);
    if (can_add)            add("Add contact", ACT_ADD);
    if (is_contact && has_key) {
      snprintf(_fav_label, sizeof(_fav_label), e->fav ? "Fav: ON" : "Fav: OFF");
      addValue(_fav_label, ACT_FAV);
    }
    if (is_contact && has_key) {
      if (is_pinned) snprintf(_pin_label, sizeof(_pin_label), "Unpin (slot %d)",
                              _task->findFavouriteSlot(e->pub_key) + 1);
      else           snprintf(_pin_label, sizeof(_pin_label), "Pin to dial");
      add(_pin_label, ACT_PIN);
    }
    if (is_admin_target)       add("Admin", ACT_ADMIN);
    if (is_contact && has_key) add("Delete contact", ACT_DELETE);
    if (stored) addValue(_sort_label, ACT_SORT);   // sort is meaningless for live-scan rows
    add(stored ? "Discover scan" : "Rescan", ACT_SCAN);
  }

  // Flip the selected contact's favourite flag and retitle the open menu row.
  // The list re-sorts underneath (favourites first), but refreshKeepingSelection()
  // re-finds this node, so the popup stays anchored to it.
  void toggleFavSelected() {
    const Entry* e = selected();
    if (!e || !e->has_key) return;
    bool now_fav = !e->fav;
    if (!contactctl::setFavourite(e->pub_key, now_fav)) return;
    snprintf(_fav_label, sizeof(_fav_label), now_fav ? "Fav: ON" : "Fav: OFF");
    refreshKeepingSelection();
  }

  // Advance the value on the menu's value rows (Sort, Fav). Both are two-state,
  // so LEFT and RIGHT do the same thing here and Enter joins them.
  void cycleMenuValue(int i) {
    if (i < 0 || i >= _menu_action_count) return;
    if (_menu_actions[i] == ACT_SORT) {
      _sort = (_sort == SORT_DIST) ? SORT_TIME : SORT_DIST;
      buildSortLabel();
      refresh();
    } else if (_menu_actions[i] == ACT_FAV) {
      toggleFavSelected();
    }
  }

  void runAction(Action a) {
    switch (a) {
      case ACT_NAV: {
        const Entry* e = selected();
        if (e && (e->lat_e6 != 0 || e->lon_e6 != 0)) { _nav = true; _nav_eta.reset(); }
        else _task->showAlert("No node GPS", 1000);
        break;
      }
      case ACT_PING: {
        const Entry* e = selected();
        rebuildPingMenu();
        _ping_menu.active = true;
        if (e && e->has_key) startPingForKey(e->pub_key);
        break;
      }
      case ACT_WAYPOINT: saveSelectedWaypoint(); break;
      case ACT_LOCATOR: {
        const Entry* e = selected();
        if (!e || (e->lat_e6 == 0 && e->lon_e6 == 0)) break;
        // A prefix is enough to keep re-resolving someone who is moving; without
        // one (a channel share, matched by name) the honest target is the place
        // they were last seen, snapshotted like a waypoint.
        if (e->has_prefix) _task->setTargetNow(1, e->pub_key, e->lat_e6, e->lon_e6, e->name);
        else               _task->setTargetNow(0, nullptr,    e->lat_e6, e->lon_e6, e->name);
        break;
      }
      case ACT_ADD: {
        const Entry* e = selected();
        if (e && e->has_key) {
          if (the_mesh.addDiscoveredContact(e->pub_key, e->name, e->type)) {
            refresh();   // now a known contact — re-sort / re-mark this pass
          } else {
            _task->showAlert("Contacts full", 1200);
          }
        }
        break;
      }
      case ACT_FAV:      break;  // value rows -- see cycleMenuValue()
      case ACT_PIN: {
        const Entry* e = selected();
        if (e && e->has_key) togglePinToDial(e->pub_key);
        break;
      }
      case ACT_DELETE:   startDeleteConfirm(); break;
      case ACT_ADMIN: {
        const Entry* e = selected();
        ContactInfo ci;
        if (e && e->contact_idx >= 0 && the_mesh.getContactByIdx(e->contact_idx, ci))
          _task->openAdminFor(ci, false);   // direct from Nodes -- Cancel should return here, not to a pick-list
        break;
      }
      case ACT_SORT:     break;  // value rows -- see cycleMenuValue()
      case ACT_SCAN:     enterScan();            break;
      case ACT_MAP: {
        _map = true; _map_zoom = 0;
        _detail_refresh_ms = millis();
        const Entry* e = selected();
        if (!e || (e->lat_e6 == 0 && e->lon_e6 == 0)) { int k = positioned(_sel, 1); if (k >= 0) _sel = k; }
        break;
      }
    }
  }

  // ── detail rendering ──────────────────────────────────────────────────────────
  void renderStoredDetail(DisplayDriver& display) {
    const Entry& e = _entries[_sel];
    display.drawInvertedHeader(e.name, true, ctxMenuOpen());
    drawStoredFields(display, e, 0, display.listStart(), display.width(), display.height());
  }

  // A stored node's position, distance, type, age and path, in the box from
  // x0 / top, w wide down to bottom: the detail view's body, and on a wide
  // screen the pane beside the list.
  void drawStoredFields(DisplayDriver& display, const Entry& e, int x0, int top, int w, int bottom) {
    const int tx = x0 + 2;
    int step = display.lineStep();
    if (step * 5 > bottom - top) step = (bottom - top) / 5;
    char buf[32];
    // Without a line to spare for the path (the OLED), the position takes one
    // row, unlabelled: then there are five rows either way.
    const bool one_pos = top + step * 5 + display.getLineHeight() > bottom;
    if (one_pos) {
      snprintf(buf, sizeof(buf), "%.5f,%.5f", e.lat_e6 / 1e6, e.lon_e6 / 1e6);
      display.drawTextEllipsized(tx, top, w - 4, buf);
    } else {
      snprintf(buf, sizeof(buf), "Lat: %.5f", e.lat_e6 / 1e6);
      display.setCursor(tx, top); display.print(buf);
      snprintf(buf, sizeof(buf), "Lon: %.5f", e.lon_e6 / 1e6);
      display.setCursor(tx, top + step); display.print(buf);
    }
    const int r0 = one_pos ? -1 : 0;   // the rows under the position move up one

    if (e.dist_km >= 0.0f) {
      char dist[12];
      geo::fmtDist(dist, sizeof(dist), e.dist_km, useImperial());
      int az = geo::bearingDeg(_own_lat, _own_lon, e.lat_e6, e.lon_e6);
      snprintf(buf, sizeof(buf), "Dist: %s %s", dist, geo::bearingCardinal(az));
    } else {
      snprintf(buf, sizeof(buf), "Dist: no GPS");
    }
    display.setCursor(tx, top + step * (2 + r0)); display.print(buf);
    snprintf(buf, sizeof(buf), "Type: %s", typeName(e.type));
    display.setCursor(tx, top + step * (3 + r0)); display.print(buf);
    char age[16];
    fmtAge(age, sizeof(age), e.lastmod);
    // For a live [LOC] row, label the timestamp as a position share and note
    // whether the sender's identity is verified (DM) or name-only (channel).
    if (e.is_live) snprintf(buf, sizeof(buf), "Sharing pos: %s %s", age, e.live_verified ? "(DM)" : "(chan)");
    else           snprintf(buf, sizeof(buf), "Seen: %s", age);
    display.drawTextEllipsized(tx, top + step * (4 + r0), w - 4, buf);

    const int lh = display.getLineHeight();
    int rows = 5 + r0;
    // How a message gets there: a chain from you to it, a ring per repeater
    // on the way.
    ContactInfo ci;
    if (top + step * rows + lh <= bottom && e.contact_idx >= 0 && the_mesh.getContactByIdx(e.contact_idx, ci)) {
      const int y = top + step * rows;
      display.setCursor(tx, y);
      display.print("Path:");
      int x = tx + display.getTextWidth("Path: ");
      if (ci.out_path_len == 0xFF) {
        display.print(" flood");
      } else {
        const int hops = ci.out_path_len & 63;
        const int r = lh / 4 > 2 ? lh / 4 : 2, cy = y + lh / 2 - 1, gap = r + 3;
        // up to 5 rings, as many as leave room for the count after them
        const int fit = (x0 + w - 2 - x - display.getTextWidth("5 hops")) / (2 * r + 1 + gap) - 2;
        const int cap = fit < 5 ? (fit < 1 ? 1 : fit) : 5, shown = hops > cap ? cap : hops;
        for (int k = 0; k <= shown + 1; k++) {   // you and it filled, the hops as rings
          if (k) for (int dx = x - gap + 1; dx < x; dx += 2) display.fillRect(dx, cy, 1, 1);
          info::circle(display, x + r, cy, r);
          if (k == 0 || k == shown + 1) display.fillRect(x + 1, cy - r + 1, 2 * r - 1, 2 * r - 1);
          x += 2 * r + 1 + gap;
        }
        if (hops == 0) snprintf(buf, sizeof(buf), "direct");
        else snprintf(buf, sizeof(buf), "%d hop%s", hops, hops == 1 ? "" : "s");
        display.setCursor(x, y);
        display.print(buf);
      }
      rows++;
    }

    // Where it is, on a compass rose (north up), where the screen has the room
    // (e-ink): beside the rows on a wide one, under them on a tall one.
    if (!Features::IS_EINK || e.dist_km < 0.0f) return;
    const int below = top + step * rows + lh;
    int r, cx, cy;
    if (bottom - below > w / 2) { r = (w < bottom - below ? w : bottom - below) / 2 - lh; cx = x0 + w / 2; cy = below + lh / 2 + r; }
    else { r = (bottom - top) / 2 - lh / 2 - 1; cx = x0 + w - r - 3; cy = top + lh / 2 + r; }
    const int text_r = tx + display.getTextWidth("Lon: -000.00000");
    if (r < 2 * lh || (cy - r < below - lh && cx - r < text_r)) return;
    info::rose(display, cx, cy, r, geo::bearingDeg(_own_lat, _own_lon, e.lat_e6, e.lon_e6));
  }

  // ── map view ────────────────────────────────────────────────────────────────
  static bool hasPos(const Entry& e) { return e.lat_e6 != 0 || e.lon_e6 != 0; }
  // The next entry with a position from `from` on in direction `dir` (from
  // itself included), wrapping; -1 when none has one.
  int positioned(int from, int dir) const {
    for (int k = 0; k < _count; k++) {
      int i = ((from + dir * k) % _count + _count) % _count;
      if (hasPos(_entries[i])) return i;
    }
    return -1;
  }

  // Every node with a position (and you) fitted in; zoomed in, centred on the
  // selected one. Dots, the selected one boxed, live shares as diamonds;
  // under the map its name and distance, "+n" for others under the same spot.
  void renderMap(DisplayDriver& d) {
    const int W = d.width(), H = d.height(), lh = d.getLineHeight(), s = miniIconScale(d);
    const int bar_y = H - lh - 1, ax = 1, ay = 1, aw = W - 2, ah = bar_y - 3;
    d.setColor(DisplayDriver::LIGHT);
    int32_t tla = 0, tlo = 0;
    const bool target = _task->activeTargetPos(tla, tlo);
    int32_t mnla = 0, mxla = 0, mnlo = 0, mxlo = 0;
    bool init = false;
    auto fold = [&](int32_t la, int32_t lo) {
      if (!init) { mnla = mxla = la; mnlo = mxlo = lo; init = true; return; }
      if (la < mnla) mnla = la;  if (la > mxla) mxla = la;
      if (lo < mnlo) mnlo = lo;  if (lo > mxlo) mxlo = lo;
    };
    for (int i = 0; i < _count; i++) if (hasPos(_entries[i])) fold(_entries[i].lat_e6, _entries[i].lon_e6);
    if (_own_gps) fold(_own_lat, _own_lon);
    if (!init || _sel >= _count || !hasPos(_entries[_sel])) {
      d.drawTextCentered(W / 2, H / 2 - lh / 2, "No node positions");
      return;
    }
    const Entry& sel = _entries[_sel];
    float kx = cosf(((mnla + mxla) / 2.0e6f) * (float)M_PI / 180.0f);
    if (kx < 0.05f) kx = 0.05f;
    float span_la = (float)(mxla - mnla), span_lo = (float)(mxlo - mnlo) * kx;
    if (span_la < 2000.0f) span_la = 2000.0f;   // ~200 m: one node alone still has a scale
    if (span_lo < 2000.0f) span_lo = 2000.0f;
    // Fitted inside a margin clear of the north mark and the scale's label.
    const int mx = ICON_MAP_NORTH.w * s + 2, my = lh + 2;
    const float fw = (float)(aw - 2 * mx), fh = (float)(ah - 2 * my);
    float scale = fh / span_la < fw / span_lo ? fh / span_la : fw / span_lo;
    float cla = (mnla + mxla) / 2.0f, clo = (mnlo + mxlo) / 2.0f;
    if (_map_zoom > 0) { scale *= (float)(1 << _map_zoom); cla = sel.lat_e6; clo = sel.lon_e6; }
    auto project = [&](int32_t la, int32_t lo, int& x, int& y) {
      x = ax + aw / 2 + (int)(((float)lo - clo) * kx * scale);
      y = ay + ah / 2 - (int)(((float)la - cla) * scale);
    };
    auto inside = [&](int x, int y) { return x >= ax && x < ax + aw && y >= ay && y < ay + ah; };

    int sx, sy, near = 0;
    project(sel.lat_e6, sel.lon_e6, sx, sy);
    for (int i = 0; i < _count; i++) {
      if (!hasPos(_entries[i])) continue;
      int x, y; project(_entries[i].lat_e6, _entries[i].lon_e6, x, y);
      if (i != _sel && abs(x - sx) <= 2 * s && abs(y - sy) <= 2 * s) near++;
      if (!inside(x, y)) continue;
      miniIconDrawCentered(d, x, y, _entries[i].is_live ? ICON_MAP_CONTACT : ICON_MAP_DOT);
    }
    if (_own_gps) { int x, y; project(_own_lat, _own_lon, x, y); if (inside(x, y)) miniIconDrawCentered(d, x, y, ICON_MAP_CURRENT); }
    if (target)   { int x, y; project(tla, tlo, x, y);           if (inside(x, y)) miniIconDrawCentered(d, x, y, ICON_MAP_TARGET); }
    const int r = 4 * s;
    d.drawRect(sx - r, sy - r, 2 * r + 1, 2 * r + 1);
    miniIconDrawTop(d, ax + aw - ICON_MAP_NORTH.w * s - 1, ay, ICON_MAP_NORTH);

    // The scale: a round distance about a quarter of the width, bottom right.
    const float m_px = 0.111f / scale;   // 1e-6 degree of latitude is 0.111 m
    static const float STEPS[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000 };
    float m = STEPS[0];
    for (float st : STEPS) { m = st; if (st / m_px >= aw / 5) break; }
    const int len = (int)(m / m_px);
    char sc[12];
    geo::fmtDist(sc, sizeof(sc), m / 1000.0f, useImperial());
    if (len > 2 && len < aw) {
      const int ly = ay + ah - 2, lx = ax + aw - len - 1;
      d.fillRect(lx, ly, len, s);
      d.setCursor(lx + len - d.getTextWidth(sc), ly - lh);
      d.print(sc);
    }

    d.fillRect(0, bar_y - 1, W, d.sepH());
    char right[16] = "";
    if (sel.dist_km >= 0.0f) geo::fmtDist(right, sizeof(right), sel.dist_km, useImperial(), true);
    char name[40];
    if (near > 0) snprintf(name, sizeof(name), "%s +%d", sel.name, near);
    else          snprintf(name, sizeof(name), "%s", sel.name);
    const int rw = right[0] ? d.getTextWidth(right) + d.getCharWidth() : 0;
    d.drawTextEllipsized(1, bar_y + 1, W - 2 - rw, name);
    if (right[0]) { d.setCursor(W - 1 - d.getTextWidth(right), bar_y + 1); d.print(right); }
  }

  void renderScanDetail(DisplayDriver& display) {
    const Entry& e = _entries[_sel];
    const int hdr = display.listStart();   // content top (gap below the header separator)

    char label[32];
    if (e.name[0]) { strncpy(label, e.name, 31); label[31] = '\0'; }
    else           { snprintf(label, sizeof(label), "[%s]", typeName(e.type)); }
    display.drawInvertedHeader(label, true, ctxMenuOpen());

    char b64[48];
    pubKeyToBase64(e.pub_key, b64, sizeof(b64));
    int max_chars = (display.width() - 4) / display.getCharWidth();
    char b64_line[48];
    if (max_chars < 4) {
      b64_line[0] = '\0';
    } else if ((int)strlen(b64) > max_chars) {
      strncpy(b64_line, b64, max_chars - 3);
      b64_line[max_chars - 3] = '\0';
      strcat(b64_line, "...");
    } else {
      strncpy(b64_line, b64, sizeof(b64_line) - 1);
      b64_line[sizeof(b64_line) - 1] = '\0';
    }
    if (b64_line[0]) { display.setCursor(2, hdr); display.print(b64_line); }

    int step = display.lineStep();
    if (step * 5 > display.height() - hdr) step = (display.height() - hdr) / 5;
    char buf[32];
    snprintf(buf, sizeof(buf), "RSSI: %d dBm", (int)e.rssi);
    display.setCursor(2, hdr + step);     display.print(buf);
    snprintf(buf, sizeof(buf), "SNR:  %.1f dB", e.snr_x4 / 4.0f);
    display.setCursor(2, hdr + step * 2); display.print(buf);
    drawSignalBars(display, display.width() - 2, hdr + step * 2, e.snr_x4);          // how we hear them
    snprintf(buf, sizeof(buf), "Rem:  %.1f dB", e.remote_snr_x4 / 4.0f);
    display.setCursor(2, hdr + step * 3); display.print(buf);
    drawSignalBars(display, display.width() - 2, hdr + step * 3, e.remote_snr_x4);   // how they hear us
    display.setCursor(2, hdr + step * 4);
    display.print(e.is_known ? "Status: known" : "Status: new");
  }

  // Any Hold-Enter popup on screen → highlight the ≡ hint so it reads as the
  // source of the open menu.
  bool ctxMenuOpen() const { return _menu.active || _confirm.active || _ping_menu.active; }

  // Draw whichever popup is active over the current view. Returns true if one was.
  bool renderActivePopup(DisplayDriver& display) {
    updatePingMenuState();
    if (_ping_menu.active)   { _ping_menu.render(display);   return true; }
    if (_confirm.active)     { _confirm.render(display);     return true; }
    if (_menu.active)        { _menu.render(display);        return true; }
    return false;
  }

  // Header as a circular filter tab bar (shared geometry — see TabBar.h): the
  // active filter sits centred as a filled pill, neighbours fan out either
  // side and wrap around (the tab before "All" is "Snsr", and vice-versa),
  // matching the wrap-around LEFT/RIGHT cycle.
  void drawFilterTabs(DisplayDriver& display) {
    tabbar::draw(display, filterLabels(), F_COUNT, _filter, display.menuHintWidth());
    display.drawContextMenuHint(DisplayDriver::LIGHT, ctxMenuOpen());   // Nodes list has a Hold-Enter menu
  }

public:
  NearbyScreen(UITask* task)
    : _task(task), _sel(0), _scroll(0), _detail(false),
      _detail_refresh_ms(0), _scanning(false), _scan_started_ms(0),
      _menu_action_count(0), _pinging(false), _ping_started_ms(0) {
    bindModel(&task->core(), task->getNodePrefs());
    resetPingLines();
    _sort_label[0] = '\0';
  }

  // Whether the full-screen navigate-to-node view is up -- used by UITask's
  // GPS duty-cycle "is anything live using GPS right now" check, since this
  // view needs an unbroken stream of fixes for bearing/ETA, not a stale one.
  bool isNavigating() const { return _nav; }

  void onShow() override {
    _sel = _scroll = 0;
    _detail = false;
    _nav = false;
    _map = false;
    _source = SRC_STORED;
    // _filter / _sort persist across enter() — set once in the constructor
    _scanning = false;
    _menu.active = false;
    _ping_menu.active = false;
    _confirm.active = false;
    _pinging = false;
    _pick_admin_target = false;   // stale pick-mode from a previous visit shouldn't linger
    resetPingLines();
    _task->clearPing();
    refreshStored();
  }

  // Entered via UITask::pickAdminTarget() right after setCurrScreen(this) has
  // already run onShow()'s reset above -- just arms the pick-mode flag.
  void startPickAdminTarget() { _pick_admin_target = true; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    int mq_delay = 0;   // >0 while the selected row's name is marquee-scrolling

    // Periodic refresh of the selected entry while in detail or navigate view,
    // preserving the selection across the list rebuild. Navigate refreshes
    // faster so a moving live target (a contact sharing [LOC]) tracks smoothly.
    // Re-selection keys on contact index for stored nodes, and on name for a
    // non-contact live sender — without the latter, navigating to someone who
    // only shares on a channel would drop out on the first refresh.
    unsigned long refresh_due = _nav ? NAV_REFRESH_MS : DETAIL_REFRESH_MS;
    if ((_detail || _nav) && _source == SRC_STORED &&
        millis() - _detail_refresh_ms >= refresh_due) {
      if (!refreshKeepingSelection()) { _detail = false; _nav = false; }  // node/share gone
      _detail_refresh_ms = millis();
    }

    // ── navigate-to-node view ────────────────────────────────────────────────
    if (_nav && _sel < _count) {
      const Entry& e = _entries[_sel];
      int cog; bool cogv = _task->currentCourse(cog);
      navview::draw(display, _own_gps, _own_lat, _own_lon,
                    e.lat_e6, e.lon_e6, e.name, cogv, cog, useImperial(), &_nav_eta);
      return 1000;
    }

    // ── detail view ──────────────────────────────────────────────────────────
    if (_detail && _sel < _count) {
      if (_source == SRC_SCAN) renderScanDetail(display);
      else                     renderStoredDetail(display);
      renderActivePopup(display);
      return _ping_menu.active ? 50 : 2000;
    }

    // ── map view ─────────────────────────────────────────────────────────────
    if (_map) {
      if (millis() - _detail_refresh_ms >= DETAIL_REFRESH_MS) {
        refreshKeepingSelection();
        _detail_refresh_ms = millis();
      }
      renderMap(display);
      return renderActivePopup(display) ? 50 : 2000;
    }

    // ── list view ────────────────────────────────────────────────────────────
    if (_source == SRC_SCAN) {
      refreshScan();
      if (_scanning && millis() - _scan_started_ms >= SCAN_DURATION_MS) _scanning = false;
    } else if (millis() - _list_refresh_ms >= TIME_LIST_REFRESH_MS) {
      // Re-merge + re-sort periodically so newly-heard contacts and fresh live
      // [LOC] shares bubble to the right spot under either sort (distances move
      // as you and they do, not just recency). Keep the highlighted node put.
      refreshKeepingSelection();
      _list_refresh_ms = millis();
    }

    // A screen 40 characters wide (the 4.2" e-ink) keeps the list to the left
    // and shows the highlighted node's detail beside it.
    const bool split = _source == SRC_STORED && _count > 0 && display.width() >= 40 * display.getCharWidth();
    const int lw = split ? display.width() * 11 / 20 : display.width();   // the list's right edge
    // An arrow towards the node before its distance: one more cell.
    const bool arrows = _source == SRC_STORED && _sort != SORT_TIME;
    int dist_col = lw - display.getCharWidth() * (arrows ? 9 : 7);

    display.setColor(DisplayDriver::LIGHT);
    const char* flt = (_filter != F_ALL) ? filterLabel(_filter) : nullptr;
    if (_source == SRC_SCAN) {
      char title[28];
      const char* base = _scanning ? "Scanning" : "Scan";
      if (flt) {
        if (!_scanning && _count == 0) snprintf(title, sizeof(title), "Scan %s: none", flt);
        else                           snprintf(title, sizeof(title), "%s %s (%d)", base, flt, _count);
      } else if (_scanning)            snprintf(title, sizeof(title), "Scanning (%d)", _count);
      else if (_count == 0)            snprintf(title, sizeof(title), "Scan: none");
      else                             snprintf(title, sizeof(title), "Scan (%d)", _count);
      drawScreenHeader(display, title, -1, 0, true, ctxMenuOpen());
    } else {
      // Stored list: the filter is a visible tab strip (LEFT/RIGHT switches tabs).
      drawFilterTabs(display);
    }

    if (_count == 0) {
      char empty[24];
      if (_source == SRC_SCAN) {
        const char* msg;
        if (_scanning) {   // the header already says Scanning: just show it's listening
          display.drawTextCentered(display.width() / 2, display.height() / 2 - display.lineStep() / 2, "No replies yet");
          int r = drawLoadingDots(display, display.width() / 2, display.height() / 2 + display.lineStep() + 2);
          if (mq_delay <= 0 || r < mq_delay) mq_delay = r;
          msg = nullptr;
        }
        else if (flt)  { snprintf(empty, sizeof(empty), "No %s nodes", flt); msg = empty; }
        else             msg = "No nodes found";
        if (msg) display.drawTextCentered(display.width() / 2, display.height() / 2, msg);
      } else {
        const char* hint;
        if (_pick_admin_target && !flt) { snprintf(empty, sizeof(empty), "Nothing to admin"); hint = "Repeaters & rooms"; }
        else if (flt) { snprintf(empty, sizeof(empty), "No %s contacts", flt); hint = "Left/Right: filter"; }
        else          { snprintf(empty, sizeof(empty), "No contacts found");   hint = "Enter: discover";}
        display.drawTextCentered(display.width() / 2, display.height() / 2 - display.lineStep() / 2, empty);
        display.drawTextCentered(display.width() / 2, display.height() / 2 + display.lineStep() / 2, hint);
      }
    } else {
      drawList(display, _count, _sel, _scroll, [&](int idx, int y, bool sel, int reserve) {
        const Entry& e = _entries[idx];

        drawRowSelection(display, y, sel, reserve, lw);

        char filt[32];
        int tx = 2;
        if (e.is_live) {
          // Diamond marker = this node is broadcasting its position ([LOC]),
          // the same marker the map uses for a live-tracked contact. DM-verified
          // vs channel-only is spelled out in the detail view.
          int iw = ICON_MAP_CONTACT.w * miniIconScale(display);
          miniIconDrawCentered(display, tx + iw / 2, y + display.getLineHeight() / 2 - 1, ICON_MAP_CONTACT);
          tx += iw + 2;
        }
        // Star = favourite, the same marker every other list uses. Shown next to
        // any live diamond so both states read at once.
        if (e.fav) {
          int iw = ICON_PG_STAR.w * miniIconScale(display);
          miniIconDrawCentered(display, tx + iw / 2, y + display.getLineHeight() / 2 - 1, ICON_PG_STAR);
          tx += iw + 2;
        }
        display.translateUTF8ToBlocks(filt, e.name, sizeof(filt));
        if (_source == SRC_SCAN && !e.name[0]) {  // unknown node → "[Type]"
          snprintf(filt, sizeof(filt), "[%s]", typeName(e.type));
        }
        int mqr = display.drawTextEllipsized(tx, y, dist_col - tx - 2, filt, sel);
        if (sel && mqr > 0) mq_delay = mqr;

        display.setColor(sel ? DisplayDriver::DARK : DisplayDriver::LIGHT);
        char right[10];
        if (_source == SRC_SCAN) {   // live scan: how well we hear it, as bars
          drawSignalBars(display, lw - reserve - 2, y, e.snr_x4);
          right[0] = '\0';
        } else if (_sort == SORT_TIME) {
          geo::fmtAgeShort(right, sizeof(right), rtc_clock.getCurrentTime(), e.lastmod);
          if (!right[0]) snprintf(right, sizeof(right), "?");   // unknown / RTC not synced
        } else {
          if (e.dist_km >= 0.0f) geo::fmtDist(right, sizeof(right), e.dist_km, useImperial());
          else                   strcpy(right, "-");   // no fix of ours or theirs
        }
        if (right[0]) display.drawTextRightAlign(lw - reserve - 2, y, right);
        if (arrows && e.dist_km >= 0.0f) {
          const int deg = geo::bearingDeg(_own_lat, _own_lon, e.lat_e6, e.lon_e6);
          const MiniIcon& ic = *ICON_ARROWS[((deg + 22) % 360) / 45];
          miniIconDraw(display, lw - reserve - 2 - display.getTextWidth(right) - 3
                                - ic.w * miniIconScale(display), y, ic);
        }
      }, lw);
      if (split && _sel < _count) {
        const int top = display.listStart();
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(lw + 2, top, display.sepH(), display.height() - top);
        drawStoredFields(display, _entries[_sel], lw + 2 + display.sepH() + 2, top,
                         display.width() - lw - 4 - display.sepH(), display.height());
      }
    }

    if (renderActivePopup(display)) return 50;
    int ret;
    if (_source == SRC_SCAN) ret = _scanning ? 200 : 2000;
    else ret = _count == 0 ? 3000 : 2000;
    return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret;
  }

  bool handleInput(char c) override {
    // ── navigate-to-node view — any nav key returns to detail ─────────────────
    if (_nav) {
      if (c == KEY_CANCEL) _nav = false;   // only Back leaves a navigate view
      return true;
    }

    // ── popups (same handling in list and detail) ─────────────────────────────
    if (_confirm.active) {
      auto res = _confirm.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        if (_confirm.selectedIndex() == 0) doDeleteSelected();
        _confirm.active = false;
      } else if (res == PopupMenu::CANCELLED) {
        _confirm.active = false;
      }
      return true;
    }
    if (_ping_menu.active)   { handlePingMenuInput(c); return true; }
    if (_menu.active) {
      // LEFT/RIGHT -- and Enter, which PopupMenu reports as VALUE_NEXT on a
      // value row -- cycle Sort and Fav in place; the popup stays open so the
      // user can keep tapping, and only Back closes it. Other rows swallow L/R.
      if (keyIsPrev(c) || keyIsNext(c)) {
        cycleMenuValue(_menu.selectedIndex());
        return true;
      }
      auto res = _menu.handleInput(c);
      if (res == PopupMenu::VALUE_NEXT) {
        cycleMenuValue(_menu.selectedIndex());
      } else if (res == PopupMenu::SELECTED) {
        int i = _menu.selectedIndex();
        if (i >= 0 && i < _menu_action_count) runAction(_menu_actions[i]);
      }
      return true;
    }

    // ── detail view ───────────────────────────────────────────────────────────
    if (_detail) {
      if (c == KEY_CANCEL)            { _detail = false; closePingMenu(); return true; }
      if (c == KEY_CONTEXT_MENU)      { openActionMenu(); return true; }
      return true;
    }

    // ── map view: Left / Right a node, Up / Down zoom, Enter its detail ──────
    if (_map) {
      if (c == KEY_CANCEL)       { _map = false; return true; }
      if (c == KEY_CONTEXT_MENU) { openActionMenu(); return true; }
      if (_count == 0) return true;
      if (keyIsPrev(c)) { int k = positioned(_sel - 1, -1); if (k >= 0) _sel = k; return true; }
      if (keyIsNext(c)) { int k = positioned(_sel + 1, 1);  if (k >= 0) _sel = k; return true; }
      if (c == KEY_UP)   { if (_map_zoom < 8) _map_zoom++; return true; }
      if (c == KEY_DOWN) { if (_map_zoom > 0) _map_zoom--; return true; }
      if (c == KEY_ENTER) { _detail = true; _detail_refresh_ms = millis(); return true; }
      return true;
    }

    // ── list view ───────────────────────────────────────────────────────────
    if (c == KEY_CANCEL) {
      if (_pick_admin_target) { _pick_admin_target = false; _task->leaveTool(); return true; }
      if (_source == SRC_SCAN) leaveScan();
      else                     _task->leaveTool();
      return true;
    }
    if (c == KEY_CONTEXT_MENU) { openActionMenu(); return true; }
    // drawList() reclamps _scroll from _sel every render.
    if (c == KEY_UP   && _count > 0) { _sel = (_sel > 0) ? _sel - 1 : _count - 1; return true; }
    if (c == KEY_DOWN && _count > 0) { _sel = (_sel < _count - 1) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      if (_pick_admin_target) {
        const Entry* e = selected();
        ContactInfo ci;
        if (e && e->contact_idx >= 0 && (e->type == ADV_TYPE_REPEATER || e->type == ADV_TYPE_ROOM)
            && the_mesh.getContactByIdx(e->contact_idx, ci)) {
          _pick_admin_target = false;
          _task->openAdminFor(ci, true);   // via the picker -- Cancel should return here
        }
        // else: row isn't an eligible admin target -- ignore, stay on the picker.
        return true;
      }
      if (_count == 0) { if (_source == SRC_STORED) enterScan(); return true; }
      _detail = true;
      _detail_refresh_ms = millis();
      return true;
    }
    if (keyIsPrev(c)) { _filter = (_filter + F_COUNT - 1) % F_COUNT; refresh(); return true; }
    if (keyIsNext(c)) { _filter = (_filter + 1) % F_COUNT;          refresh(); return true; }
    return false;
  }
};

