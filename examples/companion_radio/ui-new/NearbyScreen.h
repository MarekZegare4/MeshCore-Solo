#pragma once
#include "InfoKit.h"
#include "../Features.h"
#include "../GeoUtils.h"
#include "NavView.h"
#include "TabBar.h"

// ── Path (Nodes > a contact > Path) ───────────────────────────────────────────
// The repeaters between this node and a contact as a list: edit it, trace it
// out and back (each link's signal both ways), find where a route stops,
// save it as the contact's route. The list, the trace and the save are the UI
// Core's PathEditor (`core().route`); ui-lvgl's Route screen is a view over
// the same. Picking a repeater to add hands the Nodes list over for a moment
// (NearbyScreen: `_route_pick`).
class RouteView {
  UITask* _task = nullptr;
  bool _active = false;
  bool _pick = false;       // the Add row was chosen: NearbyScreen takes over the pick
  int  _sel = 0, _scroll = 0;
  PopupMenu _menu;          // one hop: up / down / remove
  int  _menu_hop = 0;
  uint8_t _seen_state = PathEditor::IDLE;   // a result's alert, once
  uint8_t _sel_kind = 0xFF;                 // the action row under the cursor, kept as rows come and go

  enum Row : uint8_t { R_HOP, R_NODE, R_ADD, R_TRACE, R_PROBE, R_TO_NODE, R_SAVE, R_KEEP };

  PathEditor& R() { return _task->core().route; }
  // The rows: the hops, the node, then the actions (Find break only after a
  // trace with no reply).
  int rows(Row* out) {
    PathEditor& r = R();
    int k = 0;
    for (int i = 0; i < r.count(); i++) out[k++] = R_HOP;
    out[k++] = R_NODE;
    out[k++] = R_ADD;
    out[k++] = R_TRACE;
    if (r.canProbe()) out[k++] = R_PROBE;
    if (r.count() > 0) out[k++] = R_TO_NODE;   // with no repeaters the trace is to the node
    out[k++] = R_SAVE;
    out[k++] = R_KEEP;
    return k;
  }
  static const int ROWS_MAX = PathEditor::MAX_HOPS + 7;

  void openHopMenu(int i) {
    _menu_hop = i;
    _menu.begin("Hop", 3);
    _menu.addItem("Move up");
    _menu.addItem("Move down");
    _menu.addItem("Remove");
  }

  // A link's signal, the weaker way rounded: "6/4" (out/back dB).
  static void linkText(char* out, size_t n, int16_t o, int16_t b) {
    snprintf(out, n, "%d/%d", (int)lroundf(o / 4.0f), (int)lroundf(b / 4.0f));
  }

  // What a result means, said once when it comes.
  void announce() {
    PathEditor& r = R();
    if (r.state() == _seen_state) return;
    _seen_state = r.state();
    char t[48];
    switch (r.state()) {
      case PathEditor::DONE: {
        int16_t w = 0;
        if (r.worstX4(w)) snprintf(t, sizeof(t), "Weakest %d dB", (int)lroundf(w / 4.0f));
        else return;
        break;
      }
      case PathEditor::TIMEOUT:
        snprintf(t, sizeof(t), "%s", r.count() ? "No reply: Find break" : "No reply");
        break;
      case PathEditor::PROBED:
        if (r.reached() < r.count()) snprintf(t, sizeof(t), "Stops at %.14s", r.nodeName(r.reached() + 1));
        else if (r.toNode()) snprintf(t, sizeof(t), "Node not repeating?");
        else snprintf(t, sizeof(t), "All answered");
        break;
      case PathEditor::FAILED:
        snprintf(t, sizeof(t), "%s", r.failure() == PathEditor::FAIL_NO_KEY ? "Repeater key unknown"
                                   : r.failure() == PathEditor::FAIL_BUSY ? "Busy, try again"
                                   : r.failure() == PathEditor::FAIL_EMPTY ? "Nothing to trace" : "Send failed");
        break;
      default: return;
    }
    _task->showAlert(t, 1500);
  }

public:
  bool active() const { return _active; }
  bool takePick() { bool p = _pick; _pick = false; return p; }

  bool open(UITask* task, const uint8_t* pub_key) {
    _task = task;
    if (!task->core().route.open(pub_key)) return false;
    _active = true;
    _pick = false;
    _sel = _scroll = 0;
    _menu.active = false;
    _seen_state = PathEditor::IDLE;
    _sel_kind = 0xFF;
    return true;
  }
  // Back from picking a repeater: the cursor on Add, for the next one.
  void show() {
    _active = true;
    _sel = R().count() + 1;
    _sel_kind = R_ADD;
  }
  void close() {
    if (_task) R().cancelTrace();
    _active = false;
    _menu.active = false;
  }

  int render(DisplayDriver& display) {
    PathEditor& r = R();
    r.poll();
    announce();
    const int n = r.count();
    char title[32];
    switch (r.state()) {
      case PathEditor::RUNNING:
        if (r.probing()) snprintf(title, sizeof(title), "Probe %d/%d", r.links() + 1, n);
        else snprintf(title, sizeof(title), "Tracing...");
        break;
      case PathEditor::DONE:    snprintf(title, sizeof(title), "OK %lums", (unsigned long)r.rttMs()); break;
      case PathEditor::TIMEOUT: snprintf(title, sizeof(title), "No reply"); break;
      case PathEditor::PROBED:  snprintf(title, sizeof(title), "Reached %d/%d", r.reached(), n); break;
      case PathEditor::FAILED:  snprintf(title, sizeof(title), "Can't trace"); break;
      default:                  snprintf(title, sizeof(title), "%s%s", r.edited() ? "*" : "", r.targetName());
    }
    drawScreenHeader(display, title, -1, 0, true, false);
    Row kind[ROWS_MAX];
    const int nrows = rows(kind);
    followSelection(kind, nrows);
    const int stop = r.state() == PathEditor::PROBED ? r.reached() + 1 : 0;   // the node that didn't answer
    drawList(display, nrows, _sel, _scroll, [&](int idx, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      char left[40], right[16] = "";
      int16_t o, b;
      switch (kind[idx]) {
        case R_HOP:
          snprintf(left, sizeof(left), "%d %s", idx + 1, r.hop(idx).name);
          if (r.linkSnrX4(idx + 1, o, b)) linkText(right, sizeof(right), o, b);
          else if (idx + 1 == stop) snprintf(right, sizeof(right), "x");
          else if (r.hop(idx).known < 4) snprintf(right, sizeof(right), "?");
          break;
        case R_NODE:
          snprintf(left, sizeof(left), "> %s", r.targetName());
          if (!r.toNode()) snprintf(right, sizeof(right), "-");
          else if (r.linkSnrX4(n + 1, o, b)) linkText(right, sizeof(right), o, b);
          else if (stop == n + 1) snprintf(right, sizeof(right), "x");
          break;
        case R_ADD:     snprintf(left, sizeof(left), "+ Add repeater"); break;
        case R_TRACE:   snprintf(left, sizeof(left), "%s", r.state() == PathEditor::RUNNING ? "Stop" : "Trace"); break;
        case R_PROBE:   snprintf(left, sizeof(left), "Find break"); break;
        case R_TO_NODE: snprintf(left, sizeof(left), "To node"); snprintf(right, sizeof(right), "%s", r.toNode() ? "ON" : "OFF"); break;
        case R_SAVE:    snprintf(left, sizeof(left), "%s", n ? "Save path" : "Save direct"); break;
        case R_KEEP:    snprintf(left, sizeof(left), "Keep path"); snprintf(right, sizeof(right), "%s", r.pinned() ? "ON" : "OFF"); break;
      }
      const int rw = right[0] ? display.getTextWidth(right) + display.getCharWidth() : 0;
      display.drawTextEllipsized(2, y, display.width() - reserve - 4 - rw, left, sel);
      if (right[0]) {
        display.setColor(sel ? UIColor::window_bkg : UIColor::primary_txt);
        display.drawTextRightAlign(display.width() - reserve - 2, y, right);
      }
    });
    if (_menu.active) _menu.render(display);
    return r.state() == PathEditor::RUNNING ? 100 : 2000;
  }

  // Returns true: the key was used (the view is modal).
  bool handleInput(char c) {
    PathEditor& r = R();
    if (_menu.active) {
      auto res = _menu.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        int i = _menu.selectedIndex();
        if (i == 0) { if (r.move(_menu_hop, -1)) _sel = _menu_hop - 1; }
        else if (i == 1) { if (r.move(_menu_hop, +1)) _sel = _menu_hop + 1; }
        else r.remove(_menu_hop);
        _menu.active = false;
      }
      return true;
    }
    Row kind[ROWS_MAX];
    const int nrows = rows(kind);
    if (c == KEY_CANCEL) { close(); return true; }
    if (c == KEY_UP)   { _sel = _sel > 0 ? _sel - 1 : nrows - 1; _sel_kind = kind[_sel]; return true; }
    if (c == KEY_DOWN) { _sel = _sel < nrows - 1 ? _sel + 1 : 0; _sel_kind = kind[_sel]; return true; }
    if (_sel >= nrows) _sel = nrows - 1;
    const Row k = kind[_sel];
    if (keyIsPrev(c) || keyIsNext(c)) {
      const int dir = keyIsNext(c) ? 1 : -1;
      if (k == R_HOP) { if (r.move(_sel, dir)) _sel += dir; }   // reorder in place
      else if (k == R_TO_NODE) r.setToNode(!r.toNode());
      else if (k == R_KEEP) toggleKeep();
      return true;
    }
    if (c != KEY_ENTER && c != KEY_CONTEXT_MENU) return true;
    switch (k) {
      case R_HOP: openHopMenu(_sel); break;
      case R_NODE: break;
      case R_ADD:
        if (r.full()) _task->showAlert("Path is full", 1000);
        else { r.cancelTrace(); _pick = true; _active = false; }
        break;
      case R_TRACE:
        if (r.state() == PathEditor::RUNNING) r.cancelTrace();
        else { _seen_state = PathEditor::RUNNING; r.trace(); }
        break;
      case R_PROBE:
        _seen_state = PathEditor::RUNNING;
        r.probe();
        break;
      case R_TO_NODE: r.setToNode(!r.toNode()); break;
      case R_SAVE:
        if (!r.save()) _task->showAlert(r.tooLong() ? "Too long to edit" : "Can't save", 1000);
        else _task->showAlert(r.count() ? "Path saved" : "Saved: direct", 1000);
        break;
      case R_KEEP: toggleKeep(); break;
    }
    return true;
  }

private:
  // The cursor stays on the action it was on when a row before it comes or
  // goes (Find break after a trace with no reply); from Find break, once
  // it's gone, to Trace.
  void followSelection(const Row* kind, int nrows) {
    if (_sel >= nrows) _sel = nrows - 1;
    if (_sel_kind != 0xFF && _sel_kind > R_NODE && kind[_sel] != _sel_kind) {
      int at = -1;
      for (int i = 0; i < nrows && at < 0; i++) if (kind[i] == _sel_kind) at = i;
      for (int i = 0; i < nrows && at < 0; i++) if (_sel_kind == R_PROBE && kind[i] == R_TRACE) at = i;
      if (at >= 0) _sel = at;
    }
    _sel_kind = kind[_sel];
  }

  void toggleKeep() {
    PathEditor& r = R();
    bool now = r.setPinned(!r.pinned());
    if (r.pinned() && !now) _task->showAlert("Kept once saved", 1000);
  }
};

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
                          ACT_ADD, ACT_DELETE, ACT_FAV, ACT_PIN, ACT_ADMIN, ACT_SORT, ACT_SCAN, ACT_MAP,
                          ACT_TELEM, ACT_RESET_PATH, ACT_SHARE, ACT_ROUTE, ACT_FLOW };

  // Set by UITask::pickAdminTarget() (Tools > Admin, which is remote-only):
  // while true, ENTER on an eligible row (a stored repeater/room contact) hands
  // the node straight to Admin instead of opening the detail view -- everything
  // else (filters, scan, ping, sort, the Hold-Enter menu) behaves identically to
  // normal Nodes browsing, so picking a node for Admin looks exactly like using
  // this screen for anything else. Mirrors MessagesScreen's
  // startPickBotChannel()/startPickBotRoom() pick-mode idiom.
  bool _pick_admin_target = false;

  // Path view (RouteView above) and its repeater pick: Add hands the list over
  // filtered to repeaters; Enter on one adds it, Back returns without.
  RouteView _route;
  bool    _route_pick = false;
  uint8_t _route_filter = F_ALL;   // the filter to restore after the pick
  uint8_t _route_key[PUB_KEY_SIZE];

  int     _sel;
  int     _scroll;
  bool    _detail;
  bool    _nav = false;     // full-screen navigate-to-node view (over detail)
  bool    _map = false;     // the nodes with a position on a map (under detail)
  static const uint32_t FLOW_SHOW_MS = 60000;   // packet flow lines stay this long
  // and a ring here, for one placed nowhere else: on e-ink longer, each
  // change being a refresh
  static const uint32_t FLOW_HERE_MS = Features::IS_EINK ? 15000 : 3000;
  uint32_t _flow_seen = 0;
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

  Action  _menu_actions[16];  // parallel to _menu rows — stable action ids
  int     _menu_action_count;
  char    _sort_label[16];    // dynamic label for the Sort row

  // ── ping state ───────────────────────────────────────────────────────────────
  char _ping_time_str[24];
  char _ping_snr_out_str[24];
  char _ping_snr_back_str[24];
  bool _pinging;
  unsigned long _ping_started_ms;

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
  // currently highlighted node selected across the rebuild — by public key
  // for a stored node, or by name for a non-contact live sender. Returns false
  // if that node is no longer in the list (caller decides whether to drop the
  // detail/nav view). Shared by the list, detail and navigate refresh paths.
  bool refreshKeepingSelection() {
    bool saved_contact = (_sel < _count) && _entries[_sel].contact_idx >= 0;
    bool saved_live = (_sel < _count) && _entries[_sel].is_live && !saved_contact;
    uint8_t saved_key[PUB_KEY_SIZE];
    if (saved_contact) memcpy(saved_key, _entries[_sel].pub_key, PUB_KEY_SIZE);
    char saved_name[sizeof(_entries[0].name)]; saved_name[0] = '\0';
    if (_sel < _count) {
      strncpy(saved_name, _entries[_sel].name, sizeof(saved_name) - 1);
      saved_name[sizeof(saved_name) - 1] = '\0';
    }
    refreshStored();
    int i = saved_contact ? findContact(saved_key)
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
    } else if (_pinging && millis() - _ping_started_ms >= _task->pingTimeoutMs()) {
      snprintf(_ping_time_str, sizeof(_ping_time_str), "RTT: timeout");
      _ping_snr_out_str[0] = '\0';
      _ping_snr_back_str[0] = '\0';
      _pinging = false;
      if (_task) _task->clearPing();
    }
    if (pingRowCount() != _ping_menu.count()) rebuildPingMenu();
  }

  // Ping popup input. UP/DOWN scroll: on the OLED the popup fits 3 rows, so a
  // full result (Send + RTT + 2 SNR) needs scrolling to show the last one.
  // Result rows are read-only, so Enter on any row sends.
  void handlePingMenuInput(char c) {
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
  char _flow_label[12];  // "Flow: ON" / "Flow: OFF" -- the map's packet flow
  char _pin_label[24];   // "Pin to dial" / "Unpin (slot N)" -- _menu stores the pointer
  char _telem_label[20]; // "Telem: ..." -- rewritten in place by L/R

  // What a contact may ask for, as one cycled value: status first, since
  // location and sensors go unanswered without it (MyMesh::onContactRequest).
  static uint8_t telemPerms(int step) {
    static const uint8_t STEPS[5] = {
      0, TELEM_PERM_BASE, TELEM_PERM_BASE | TELEM_PERM_LOCATION, TELEM_PERM_BASE | TELEM_PERM_ENVIRONMENT,
      TELEM_PERM_BASE | TELEM_PERM_LOCATION | TELEM_PERM_ENVIRONMENT };
    return STEPS[step];
  }
  static int telemStep(uint8_t perms) {
    for (int i = 4; i > 0; i--) if (perms == telemPerms(i)) return i;
    return 0;
  }
  void buildTelemLabel(uint8_t perms) {
    static const char* const NAMES[5] = { "None", "Status", "Stat+loc", "Stat+sens", "All" };
    snprintf(_telem_label, sizeof(_telem_label), "Telem: %s", NAMES[telemStep(perms)]);
  }

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
    if (_map) {   // packet flow: what the radio hears, as lines (ui-core/PacketFlow.h)
      buildFlowLabel();
      addValue(_flow_label, ACT_FLOW);
    }
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
    // A saved contact (not a scan row): its telemetry permissions -- only where
    // Privacy says "Allowed" do they count -- its path and its advert.
    ContactInfo* ci = (is_contact && has_key) ? the_mesh.lookupContactByPubKey(e->pub_key, PUB_KEY_SIZE) : nullptr;
    if (ci && contactctl::telemetryPerContact(_task->getNodePrefs())) {
      buildTelemLabel(contactctl::telemetry(*ci));
      addValue(_telem_label, ACT_TELEM);
    }
    if (ci) add("Path / trace", ACT_ROUTE);
    if (ci && contactctl::hasPath(*ci)) add("Reset path", ACT_RESET_PATH);
    if (ci) add("Share nearby", ACT_SHARE);
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

  // Advance the value on the menu's value rows (Sort, Fav, Telem). Sort and Fav
  // are two-state, so LEFT and RIGHT do the same there; Telem steps by `dir`.
  void buildFlowLabel() {
    NodePrefs* p = _task->getNodePrefs();
    snprintf(_flow_label, sizeof(_flow_label), "Flow: %s", p && p->flow_show ? "ON" : "OFF");
  }

  void cycleMenuValue(int i, int dir = 1) {
    if (i < 0 || i >= _menu_action_count) return;
    if (_menu_actions[i] == ACT_SORT) {
      _sort = (_sort == SORT_DIST) ? SORT_TIME : SORT_DIST;
      buildSortLabel();
      refresh();
    } else if (_menu_actions[i] == ACT_FAV) {
      toggleFavSelected();
    } else if (_menu_actions[i] == ACT_FLOW) {
      NodePrefs* p = _task->getNodePrefs();
      if (!p) return;
      p->flow_show = p->flow_show ? 0 : 1;
      the_mesh.savePrefs();
      if (!p->flow_show) _task->core().flow.clear();
      buildFlowLabel();
    } else if (_menu_actions[i] == ACT_TELEM) {
      const Entry* e = selected();
      ContactInfo* ci = e ? the_mesh.lookupContactByPubKey(e->pub_key, PUB_KEY_SIZE) : nullptr;
      if (!ci) return;
      uint8_t next = telemPerms((telemStep(contactctl::telemetry(*ci)) + dir + 5) % 5);
      if (contactctl::setTelemetry(e->pub_key, next)) buildTelemLabel(next);
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
      case ACT_TELEM:    break;
      case ACT_RESET_PATH: {
        const Entry* e = selected();
        if (e && contactctl::resetPath(e->pub_key)) _task->showAlert("Path reset", 800);
        break;
      }
      case ACT_ROUTE: {
        const Entry* e = selected();
        if (e && e->has_key && _route.open(_task, e->pub_key)) {
          memcpy(_route_key, e->pub_key, PUB_KEY_SIZE);
          _detail = true;   // Back from the path lands on the node
        }
        break;
      }
      case ACT_SHARE: {
        const Entry* e = selected();
        _task->showAlert(e && contactctl::share(e->pub_key) ? "Shared nearby" : "No advert saved", 1000);
        break;
      }
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
      case ACT_FLOW:     break;
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

  // Add on the Path view: the list, repeaters only, until one is picked.
  void startRoutePick() {
    _route_pick = true;
    _route_filter = _filter;
    _filter = F_RPT;
    _detail = false;
    _sel = _scroll = 0;
    refresh();
  }
  void endRoutePick() {
    _route_pick = false;
    _filter = _route_filter;
    refresh();
    int i = findContact(_route_key);   // the node whose path it is, selected again
    if (i >= 0) _sel = i;
    _detail = true;
    _route.show();
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
  // E-ink has the room for more: the nodes' names beside their dots, your
  // trail (dotted) and waypoints; a screen 40 characters wide (4.2", 4.26")
  // keeps the map to the left and shows the selected node and the packet
  // flow's legend beside it, the flow's kinds then told apart by their dashes.
  void renderMap(DisplayDriver& d) {
    const int W = d.width(), H = d.height(), lh = d.getLineHeight(), cw = d.getCharWidth(), s = miniIconScale(d);
    const bool eink = Features::IS_EINK, wide = eink && W >= 40 * cw;
    const int bar_y = H - lh - 1, mw = wide ? W * 3 / 5 : W;   // the map's right edge
    const int ax = 1, ay = 1, aw = mw - 2, ah = wide ? H - 2 : bar_y - 3;
    d.setColor(UIColor::primary_txt);
    int32_t tla = 0, tlo = 0;
    const bool target = _task->activeTargetPos(tla, tlo);
    int32_t mnla = 0, mxla = 0, mnlo = 0, mxlo = 0;
    bool init = false;
    auto fold = [&](int32_t la, int32_t lo) {
      if (!init) { mnla = mxla = la; mnlo = mxlo = lo; init = true; return; }
      if (la < mnla) mnla = la;  if (la > mxla) mxla = la;
      if (lo < mnlo) mnlo = lo;  if (lo > mxlo) mxlo = lo;
    };
    // You: the GPS, else the position set in Privacy -- where the packet
    // flow's lines end too.
    int32_t mla = _own_lat, mlo = _own_lon;
    const bool me = _own_gps || _task->core().fixedPosition(mla, mlo);
    for (int i = 0; i < _count; i++) if (hasPos(_entries[i])) fold(_entries[i].lat_e6, _entries[i].lon_e6);
    if (me) fold(mla, mlo);
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
    // Fitted inside a margin clear of the north mark and the scale's label,
    // and on e-ink of a name beside a dot at the edge.
    const int mx = ICON_MAP_NORTH.w * s + 2 + (eink ? 2 * cw : 0), my = lh + 2 + (eink ? 2 * (lh / 3) + d.sepH() : 0);
    const float fw = (float)(aw - 2 * mx), fh = (float)(ah - 2 * my);
    float scale = fh / span_la < fw / span_lo ? fh / span_la : fw / span_lo;
    float cla = (mnla + mxla) / 2.0f, clo = (mnlo + mxlo) / 2.0f;
    if (_map_zoom > 0) { scale *= (float)(1 << _map_zoom); cla = sel.lat_e6; clo = sel.lon_e6; }
    auto projectF = [&](int32_t la, int32_t lo, float& x, float& y) {   // unrounded, for far ends
      x = ax + aw / 2 + ((float)lo - clo) * kx * scale;
      y = ay + ah / 2 - ((float)la - cla) * scale;
    };
    auto project = [&](int32_t la, int32_t lo, int& x, int& y) {   // clamped far off the screen, never past an int
      float fx, fy; projectF(la, lo, fx, fy);
      x = (int)lroundf(fx < -30000 ? -30000 : fx > 30000 ? 30000 : fx);   // rounded as the lines are
      y = (int)lroundf(fy < -30000 ? -30000 : fy > 30000 ? 30000 : fy);
    };
    auto inside = [&](int x, int y) { return x >= ax && x < ax + aw && y >= ay && y < ay + ah; };
    const int t = d.sepH();

    // E-ink: your trail, dotted, and the waypoints, under everything else.
    if (eink) {
      const TrailStore& tr = _task->core().trail.store();
      float x0 = 0, y0 = 0;
      for (int i = 0; i < tr.count(); i++) {
        float x1, y1; projectF(tr.at(i).lat_1e6, tr.at(i).lon_1e6, x1, y1);
        if (i > 0 && !(tr.at(i).flags & TRAIL_FLAG_SEG_START)) gfx::drawLineClipped(d, x0, y0, x1, y1, ax, ay, aw, ah, trailDash());
        x0 = x1; y0 = y1;
      }
      TrailPoint pend;
      if (tr.count() > 0 && tr.pendingPoint(pend)) {
        float x1, y1; projectF(pend.lat_1e6, pend.lon_1e6, x1, y1);
        gfx::drawLineClipped(d, x0, y0, x1, y1, ax, ay, aw, ah, trailDash());
      }
      const WaypointStore& wps = _task->waypoints();
      for (int i = 0; i < wps.count(); i++) {
        int x, y; project(wps.at(i).lat_1e6, wps.at(i).lon_1e6, x, y);
        if (inside(x, y)) miniIconDrawCentered(d, x, y, ICON_MAP_WAYPOINT);
      }
    }

    int sx, sy, near = 0;
    project(sel.lat_e6, sel.lon_e6, sx, sy);
    for (int i = 0; i < _count; i++) {
      if (!hasPos(_entries[i])) continue;
      int x, y; project(_entries[i].lat_e6, _entries[i].lon_e6, x, y);
      if (i != _sel && abs(x - sx) <= 2 * s && abs(y - sy) <= 2 * s) near++;
      if (!inside(x, y)) continue;
      miniIconDrawCentered(d, x, y, _entries[i].is_live ? ICON_MAP_CONTACT : ICON_MAP_DOT);
    }
    // Packet flow (ui-core/PacketFlow.h): the lines of the packets heard in
    // the last minute, the newest two drawn three strokes wide, centred on
    // the line; a ring for a packet sent, or heard on its way to a repeater.
    // Its points are drawn where the contacts and you are now, not where they
    // were when it was heard, so the lines meet on the marks.
    const PacketFlow& fl = _task->core().flow;
    for (int i = 0, lines = 0; fl.enabled() && i < fl.count(); i++) {
      const PacketFlow::Flow& f = fl.at(i);
      if (millis() - f.t_ms > FLOW_SHOW_MS) break;   // newest first
      float px[PacketFlow::MAX_PTS], py[PacketFlow::MAX_PTS];
      for (int k = 0; k < f.n; k++) {
        int32_t la, lo;
        fl.pointNow(f, k, la, lo);
        projectF(la, lo, px[k], py[k]);
      }
      if (f.n == 1) {   // a packet nothing of which could be placed: a ring here, briefly
        if (f.here && millis() - f.t_ms > FLOW_HERE_MS) continue;
        if (fabsf(px[0]) < 30000 && fabsf(py[0]) < 30000)
          gfx::drawCircleClipped(d, (int)lroundf(px[0]), (int)lroundf(py[0]), 6 * s, ax, ay, aw, ah);
        continue;
      }
      const gfx::Dash dash = wide ? flowDash(f.kind) : gfx::DASH_SOLID;
      const bool bold = lines++ < 2;
      for (int k = 1; k < f.n; k++) {
        gfx::drawLineClipped(d, px[k - 1], py[k - 1], px[k], py[k], ax, ay, aw, ah, dash);
        if (!bold) continue;
        // a stroke either side of the first, across the line's run
        const bool flat = fabsf(px[k] - px[k - 1]) >= fabsf(py[k] - py[k - 1]);
        const float ox = flat ? 0 : t, oy = flat ? t : 0;
        gfx::drawLineClipped(d, px[k - 1] + ox, py[k - 1] + oy, px[k] + ox, py[k] + oy, ax, ay, aw, ah, dash);
        gfx::drawLineClipped(d, px[k - 1] - ox, py[k - 1] - oy, px[k] - ox, py[k] - oy, ax, ay, aw, ah, dash);
      }
    }
    if (me)     { int x, y; project(mla, mlo, x, y);   if (inside(x, y)) miniIconDrawCentered(d, x, y, ICON_MAP_CURRENT); }
    if (target) { int x, y; project(tla, tlo, x, y);   if (inside(x, y)) miniIconDrawCentered(d, x, y, ICON_MAP_TARGET); }
    const int r = 4 * s;
    d.drawRect(sx - r, sy - r, 2 * r + 1, 2 * r + 1);

    // The north mark and the scale, on a background so they stay readable
    // over the lines; their boxes are kept clear of the names below.
    const int nw = ICON_MAP_NORTH.w * s + 2, nh = ICON_MAP_NORTH.h * s + 1;
    d.setColor(UIColor::window_bkg);
    d.fillRect(ax + aw - nw, ay, nw, nh);
    d.setColor(UIColor::primary_txt);
    miniIconDrawTop(d, ax + aw - ICON_MAP_NORTH.w * s - 1, ay, ICON_MAP_NORTH);
    const float m_px = 0.111f / scale;   // 1e-6 degree of latitude is 0.111 m
    static const float STEPS[] = { 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000 };
    float m = STEPS[0];
    for (float st : STEPS) { m = st; if (st / m_px >= aw / 5) break; }
    const int len = (int)(m / m_px);
    char sc[12];
    geo::fmtDist(sc, sizeof(sc), m / 1000.0f, useImperial());
    const bool scale_on = len > 2 && len < aw;
    const int ly = ay + ah - 2, lx = ax + aw - len - 1 - (wide ? 2 * t : 0), stw = d.getTextWidth(sc);
    // On e-ink a map's scale bar: a framed bar, its left half filled, ticks
    // up at the ends, the distance over its middle -- the OLED keeps a line.
    const int bh = lh / 3 + t, tick = lh / 3;   // e-ink: the bar's height, the ticks above it
    if (scale_on && eink) {
      const int by = ay + ah - bh - 1, ty = by - tick - lh;
      int tx = lx + (len - stw) / 2;
      if (tx + stw > ax + aw) tx = ax + aw - stw;
      d.setColor(UIColor::window_bkg);
      d.fillRect(tx - 2, ty - 1, stw + 4, lh + 1);
      d.fillRect(lx - t - 1, by - tick - 1, len + 2 * t + 2, bh + tick + 2);
      d.setColor(UIColor::primary_txt);
      for (int k = 0; k < t; k++) d.drawRect(lx + k, by + k, len - 2 * k, bh - 2 * k);
      d.fillRect(lx, by, len / 2, bh);
      d.fillRect(lx, by - tick, t, tick);
      d.fillRect(lx + len - t, by - tick, t, tick);
      d.setCursor(tx, ty);
      d.print(sc);
    } else if (scale_on) {
      d.setColor(UIColor::window_bkg);
      d.fillRect(lx + len - stw - 1, ly - lh - 1, stw + 2, lh + 1);
      d.fillRect(lx - 1, ly - 1, len + 2, s + 2);
      d.setColor(UIColor::primary_txt);
      d.fillRect(lx, ly, len, s);
      d.setCursor(lx + len - stw, ly - lh);
      d.print(sc);
    }

    // E-ink: names beside the dots, the selected one first, then the others in
    // the list's order, each where it covers no other name, dot or the marks
    // (right of the dot, else left); one that fits nowhere is left out.
    if (eink) {
      struct Box { int16_t x, y, w, h; };
      Box taken[NEARBY_MAX * 2 + 4];   // ~0.5 KB of stack, only while drawing
      int nt = 0;
      auto hit = [&](const Box& b) {
        if (b.x < ax || b.y < ay || b.x + b.w > ax + aw || b.y + b.h > ay + ah) return true;
        for (int k = 0; k < nt; k++)
          if (b.x < taken[k].x + taken[k].w && taken[k].x < b.x + b.w && b.y < taken[k].y + taken[k].h && taken[k].y < b.y + b.h) return true;
        return false;
      };
      const int half = ICON_MAP_DOT.w * s / 2 + 1;
      auto box = [](int x, int y, int w, int h) { return Box{ (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h }; };
      taken[nt++] = box(ax + aw - nw, ay, nw, nh);
      if (scale_on) taken[nt++] = box(lx - t - 1, ay + ah - bh - tick - lh - 2, len + 2 * t + 2, bh + tick + lh + 3);
      for (int i = 0; i < _count && nt < NEARBY_MAX + 2; i++) {
        if (!hasPos(_entries[i])) continue;
        int x, y; project(_entries[i].lat_e6, _entries[i].lon_e6, x, y);
        if (inside(x, y)) taken[nt++] = box(x - half, y - half, 2 * half + 1, 2 * half + 1);
      }
      const int maxw = (wide ? 12 : 8) * cw;
      for (int n = 0; n < _count && nt < (int)(sizeof(taken) / sizeof(taken[0])); n++) {
        const int i = n == 0 ? _sel : (n <= _sel ? n - 1 : n);   // the selected one first
        const Entry& e = _entries[i];
        if (!hasPos(e) || !e.name[0]) continue;
        int x, y; project(e.lat_e6, e.lon_e6, x, y);
        if (!inside(x, y)) continue;
        int tw = d.getTextWidth(e.name);
        if (tw > maxw) tw = maxw;
        const int off = i == _sel ? r + 2 : half + 1;
        Box b = box(x + off, y - lh / 2, tw + 2, lh);
        if (hit(b)) { b.x = (int16_t)(x - off - tw - 2); if (hit(b)) continue; }
        taken[nt++] = b;
        d.setColor(UIColor::window_bkg);
        d.fillRect(b.x, b.y, b.w, b.h);
        d.setColor(UIColor::primary_txt);
        d.drawTextEllipsized(b.x + 1, b.y, tw, e.name);
      }
    }

    // The selected node: in a pane beside the map (wide), else on a bar under it.
    char dist[16] = "";
    float dkm = sel.dist_km;
    if (dkm < 0.0f && me) dkm = geo::haversineKm(mla, mlo, sel.lat_e6, sel.lon_e6);
    if (dkm >= 0.0f) geo::fmtDist(dist, sizeof(dist), dkm, useImperial(), true);
    char name[40];
    if (near > 0) snprintf(name, sizeof(name), "%s +%d", sel.name, near);
    else          snprintf(name, sizeof(name), "%s", sel.name);
    if (wide) {
      renderMapPane(d, sel, name, dist, me ? geo::bearingDeg(mla, mlo, sel.lat_e6, sel.lon_e6) : -1, mw);
      return;
    }
    d.fillRect(0, bar_y - 1, W, d.sepH());
    const int rw = dist[0] ? d.getTextWidth(dist) + d.getCharWidth() : 0;
    d.drawTextEllipsized(1, bar_y + 1, W - 2 - rw, name);
    if (dist[0]) { d.setCursor(W - 1 - d.getTextWidth(dist), bar_y + 1); d.print(dist); }
  }

  // The map's dashes (wide e-ink): each kind of packet its own, the trail dotted.
  static gfx::Dash trailDash() { return { 0x1, 3 }; }
  static gfx::Dash flowDash(uint8_t kind) {
    switch (kind) {
      case PacketFlow::K_ADVERT: return gfx::DASH_SOLID;
      case PacketFlow::K_MSG:    return { 0x3F, 9 };    // long dashes
      case PacketFlow::K_GROUP:  return { 0x07, 6 };    // short dashes
      default:                   return { 0x5F, 9 };    // dash and dot: requests, acks, the rest
    }
  }

  // Beside the map on a wide screen: the selected node (name, distance and
  // bearing, type, last heard, path) and, with the packet flow on, its legend:
  // a sample of each kind's dashes and how many were heard in the last minute.
  void renderMapPane(DisplayDriver& d, const Entry& e, const char* name, const char* dist, int deg, int mw) {
    const int W = d.width(), H = d.height(), lh = d.getLineHeight(), cw = d.getCharWidth(), t = d.sepH();
    const int px = mw + t + 3, pw = W - px - 2, step = d.lineStep();
    d.setColor(UIColor::primary_txt);
    d.fillRect(mw, 0, t, H);
    int y = 1;
    d.drawTextEllipsized(px, y, pw, name);
    y += lh + 1;
    d.fillRect(px, y, pw, t);
    y += t + 2;
    char buf[32];
    if (dist[0]) {
      if (deg >= 0) snprintf(buf, sizeof(buf), "%s %s", dist, geo::bearingCardinal(deg));
      else          snprintf(buf, sizeof(buf), "%s", dist);
      d.drawTextEllipsized(px, y, pw, buf); y += step;
    }
    snprintf(buf, sizeof(buf), "%s", typeName(e.type));
    d.drawTextEllipsized(px, y, pw, buf); y += step;
    char age[16];
    fmtAge(age, sizeof(age), e.lastmod);
    snprintf(buf, sizeof(buf), "%s %s", e.is_live ? "Sharing" : "Seen", age);
    d.drawTextEllipsized(px, y, pw, buf); y += step;
    ContactInfo ci;
    if (e.contact_idx >= 0 && the_mesh.getContactByIdx(e.contact_idx, ci)) {
      if (ci.out_path_len == 0xFF) snprintf(buf, sizeof(buf), "Path: flood");
      else if ((ci.out_path_len & 63) == 0) snprintf(buf, sizeof(buf), "Path: direct");
      else snprintf(buf, sizeof(buf), "Path: %d hop%s", ci.out_path_len & 63, (ci.out_path_len & 63) == 1 ? "" : "s");
      d.drawTextEllipsized(px, y, pw, buf); y += step;
    }

    // The legend, from the bottom up as far as it fits; between it and the
    // node, a compass rose pointing at the node when there is the room.
    const PacketFlow& fl = _task->core().flow;
    const bool trail = _task->core().trail.store().count() > 1;
    struct Row { const char* label; int n; gfx::Dash dash; bool ring; };
    Row rows[7];
    int nr = 0;
    if (fl.enabled()) {
      rows[nr++] = { "Advert",  fl.count(PacketFlow::K_ADVERT), flowDash(PacketFlow::K_ADVERT), false };
      rows[nr++] = { "DM",      fl.count(PacketFlow::K_MSG),    flowDash(PacketFlow::K_MSG),    false };
      rows[nr++] = { "Channel", fl.count(PacketFlow::K_GROUP),  flowDash(PacketFlow::K_GROUP),  false };
      rows[nr++] = { "Other",   fl.count(PacketFlow::K_REQ) + fl.count(PacketFlow::K_ACK) + fl.count(PacketFlow::K_OTHER),
                     flowDash(PacketFlow::K_OTHER), false };
      rows[nr++] = { "Sent",    fl.count(PacketFlow::K_SENT),   gfx::DASH_SOLID, true };
    }
    if (trail) rows[nr++] = { "Trail", -1, trailDash(), false };
    const int head = fl.enabled() ? 1 : 0;
    int top = nr ? H - (nr + head) * step - 1 : H;
    if (top < y + step / 2) { top = H; nr = 0; }   // no room for it under the node
    const int room = top - y - lh / 2, size = room < pw ? room : pw;
    if (deg >= 0 && size >= 5 * lh) info::rose(d, px + pw / 2, y + room / 2, size / 2 - lh, deg);
    if (!nr) return;
    int ry = top;
    if (head) { d.setCursor(px, ry); d.print("Last minute"); ry += step; }
    const int sw = 3 * cw;   // the sample line
    for (int i = 0; i < nr; i++, ry += step) {
      const int cy = ry + lh / 2;
      if (rows[i].ring) gfx::drawCircleClipped(d, px + sw / 2, cy, lh / 3, px, ry, sw, lh);
      else              gfx::drawLine(d, px, cy, px + sw - 1, cy, rows[i].dash);
      d.setCursor(px + sw + cw, ry);
      d.print(rows[i].label);
      if (rows[i].n >= 0) {
        snprintf(buf, sizeof(buf), "%d", rows[i].n);
        d.setCursor(px + pw - d.getTextWidth(buf), ry);
        d.print(buf);
      }
    }
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
    display.drawContextMenuHint(UIColor::primary_txt, ctxMenuOpen());   // Nodes list has a Hold-Enter menu
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
    _route.close();
    _route_pick = false;
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

    // ── path view (over the detail) ──────────────────────────────────────────
    if (_route.active()) return _route.render(display);

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
      if (renderActivePopup(display)) return 50;
      const PacketFlow& fl = _task->core().flow;   // new lines show within a second
      return fl.enabled() && (fl.seq() != _flow_seen || fl.active(FLOW_SHOW_MS)) ? (_flow_seen = fl.seq(), 1000) : 2000;
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
    // An arrow towards the node before its distance.
    const bool arrows = _source == SRC_STORED && _sort != SORT_TIME;

    display.setColor(UIColor::primary_txt);
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
        // The right-hand value first: the name takes whatever it leaves.
        char right[10];
        const MiniIcon* arrow = nullptr;
        int rw;
        if (_source == SRC_SCAN) {   // live scan: how well we hear it, as bars
          right[0] = '\0';
          rw = signalBarsWidth(display);
        } else {
          if (_sort == SORT_TIME) {
            geo::fmtAgeShort(right, sizeof(right), rtc_clock.getCurrentTime(), e.lastmod);
            if (!right[0]) snprintf(right, sizeof(right), "?");   // unknown / RTC not synced
          } else {
            if (e.dist_km >= 0.0f) geo::fmtDist(right, sizeof(right), e.dist_km, useImperial());
            else                   strcpy(right, "-");   // no fix of ours or theirs
          }
          rw = display.getTextWidth(right);
          if (arrows && e.dist_km >= 0.0f) {
            const int deg = geo::bearingDeg(_own_lat, _own_lon, e.lat_e6, e.lon_e6);
            arrow = ICON_ARROWS[((deg + 22) % 360) / 45];
            rw += 3 + arrow->w * miniIconScale(display);
          }
        }

        display.translateUTF8ToBlocks(filt, e.name, sizeof(filt));
        if (_source == SRC_SCAN && !e.name[0]) {  // unknown node → "[Type]"
          snprintf(filt, sizeof(filt), "[%s]", typeName(e.type));
        }
        const int name_right = lw - reserve - 2 - rw - display.getCharWidth();
        int mqr = display.drawTextEllipsized(tx, y, name_right > tx ? name_right - tx : 0, filt, sel);
        if (sel && mqr > 0) mq_delay = mqr;

        display.setColor(sel ? UIColor::window_bkg : UIColor::primary_txt);
        if (_source == SRC_SCAN) drawSignalBars(display, lw - reserve - 2, y, e.snr_x4);
        if (right[0]) display.drawTextRightAlign(lw - reserve - 2, y, right);
        if (arrow)
          miniIconDraw(display, lw - reserve - 2 - display.getTextWidth(right) - 3
                                - arrow->w * miniIconScale(display), y, *arrow);
      }, lw);
      if (split && _sel < _count) {
        const int top = display.listStart();
        display.setColor(UIColor::primary_txt);
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

  // The nodes map, from outside the list (the sim harness).
  bool openMap() {
    int i = positioned(0, 1);
    if (i < 0) return false;
    _sel = i;
    _detail = false;   // the map is drawn from the list
    _map = true; _map_zoom = 0;
    _detail_refresh_ms = millis();
    return true;
  }

  // Path / trace for a contact, from outside the list (the sim harness).
  bool openRoute(const uint8_t* pub_key) {
    int i = findContact(pub_key);
    if (i < 0 || !_route.open(_task, pub_key)) return false;
    _sel = i;
    _detail = true;
    memcpy(_route_key, pub_key, PUB_KEY_SIZE);
    return true;
  }

  bool handleInput(char c) override {
    // ── path view: modal; Add hands the Nodes list over for one repeater ──────
    if (_route.active()) {
      _route.handleInput(c);
      if (_route.takePick()) startRoutePick();
      return true;
    }

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
      // value row -- cycle Sort, Fav and Telem in place; the popup stays open so
      // the user can keep tapping, and only Back closes it. Other rows swallow L/R.
      if (keyIsPrev(c) || keyIsNext(c)) {
        cycleMenuValue(_menu.selectedIndex(), keyIsNext(c) ? 1 : -1);
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
      if (_route_pick) { endRoutePick(); return true; }
      if (_pick_admin_target) { _pick_admin_target = false; _task->leaveTool(); return true; }
      if (_source == SRC_SCAN) leaveScan();
      else                     _task->leaveTool();
      return true;
    }
    if (c == KEY_CONTEXT_MENU) { if (!_route_pick) openActionMenu(); return true; }
    // drawList() reclamps _scroll from _sel every render.
    if (c == KEY_UP   && _count > 0) { _sel = (_sel > 0) ? _sel - 1 : _count - 1; return true; }
    if (c == KEY_DOWN && _count > 0) { _sel = (_sel < _count - 1) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      if (_route_pick) {   // a repeater into the path
        const Entry* e = selected();
        if (e && e->has_key && e->type == ADV_TYPE_REPEATER) {
          if (!_task->core().route.addRepeater(e->pub_key)) _task->showAlert("Already the last hop", 1000);
          endRoutePick();
        }
        return true;
      }
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
    if (_route_pick) return true;   // the filter stays on repeaters
    if (keyIsPrev(c)) { tabbar::turn(_task->getDisplay(), filterLabels(), _filter, -1); _filter = (_filter + F_COUNT - 1) % F_COUNT; refresh(); return true; }
    if (keyIsNext(c)) { tabbar::turn(_task->getDisplay(), filterLabels(), _filter, +1); _filter = (_filter + 1) % F_COUNT;          refresh(); return true; }
    return false;
  }
};

