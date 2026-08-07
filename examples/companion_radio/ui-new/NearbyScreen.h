#pragma once
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
class NearbyScreen : public UIScreen {
  UITask* _task;

  // ── filter (type axis) ──────────────────────────────────────────────────────
  enum Filter : uint8_t { F_ALL, F_FAV, F_COMP, F_RPT, F_ROOM, F_SNSR, F_COUNT };
  static const char* FILTER_LABELS[F_COUNT];

  // ── sort axis ───────────────────────────────────────────────────────────────
  enum Sort : uint8_t { SORT_DIST, SORT_TIME };

  // ── source ──────────────────────────────────────────────────────────────────
  enum Source : uint8_t { SRC_STORED, SRC_SCAN };

  // ── action-menu actions (matched by id, not by row index) ────────────────────
  enum Action : uint8_t { ACT_NAV, ACT_PING, ACT_WAYPOINT, ACT_LOCATOR,
                          ACT_ADD, ACT_DELETE, ACT_FAV, ACT_ADMIN, ACT_SORT, ACT_SCAN };

  // Set by UITask::pickAdminTarget() (Tools > Admin, which is remote-only):
  // while true, ENTER on an eligible row (a stored repeater/room contact) hands
  // the node straight to Admin instead of opening the detail view -- everything
  // else (filters, scan, ping, sort, the Hold-Enter menu) behaves identically to
  // normal Nodes browsing, so picking a node for Admin looks exactly like using
  // this screen for anything else. Mirrors MessagesScreen's
  // startPickBotChannel()/startPickBotRoom() pick-mode idiom.
  bool _pick_admin_target = false;

  // ── unified list entry ───────────────────────────────────────────────────────
  struct Entry {
    char     name[32];
    uint8_t  type;
    uint8_t  pub_key[PUB_KEY_SIZE];
    bool     has_key;
    // stored-source fields
    int32_t  lat_e6, lon_e6;
    float    dist_km;
    uint32_t lastmod;
    int      contact_idx;
    // scan-source fields
    int8_t   rssi, snr_x4, remote_snr_x4;
    bool     is_known;
    // live-track ([LOC] share) overlay: this row's position/age came from a
    // shared-location message. live_verified == true for a DM (pubkey) share,
    // false for a channel (name, best-effort) share.
    bool     is_live;
    bool     live_verified;
  };

  static const int MAX_NEARBY = 32;
  Entry   _entries[MAX_NEARBY];
  int     _count;
  int     _sel;
  int     _scroll;
  bool    _detail;
  bool    _nav = false;     // full-screen navigate-to-node view (over detail)
  navview::EtaTracker _nav_eta;  // closing-speed/ETA for the navigate view
  int32_t _own_lat, _own_lon;
  bool    _own_gps;

  Source  _source;
  uint8_t _filter;
  uint8_t _sort;

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

  Action  _menu_actions[10];  // parallel to _menu rows — stable action ids
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

  static const char* typeName(uint8_t t) {
    switch (t) {
      case ADV_TYPE_CHAT:     return "Companion";
      case ADV_TYPE_REPEATER: return "Repeater";
      case ADV_TYPE_ROOM:     return "Room";
      case ADV_TYPE_SENSOR:   return "Sensor";
      default:                return "Unknown";
    }
  }

  static const char* typeShort(uint8_t t) {
    switch (t) {
      case ADV_TYPE_REPEATER: return "Rpt";
      case ADV_TYPE_SENSOR:   return "Snsr";
      case ADV_TYPE_ROOM:     return "Room";
      case ADV_TYPE_CHAT:     return "Comp";
      default:                return "?";
    }
  }

  // The selected entry, or nullptr when the list is empty.
  const Entry* selected() const {
    return (_count > 0 && _sel < _count) ? &_entries[_sel] : nullptr;
  }

  bool typeMatchesFilter(uint8_t type, uint8_t flags, bool have_flags) const {
    switch (_filter) {
      case F_FAV:  return have_flags && (flags & 0x01);
      case F_COMP: return type == ADV_TYPE_CHAT;
      case F_RPT:  return type == ADV_TYPE_REPEATER;
      case F_ROOM: return type == ADV_TYPE_ROOM;
      case F_SNSR: return type == ADV_TYPE_SENSOR;
      case F_ALL:
      default:     return true;
    }
  }

  // ── data refresh ──────────────────────────────────────────────────────────────
  void refreshStored() {
    _count = 0;
    _own_lat = _own_lon = 0;
    _own_gps = _task->currentLocation(_own_lat, _own_lon);

    int nc = the_mesh.getNumContacts();
    for (int i = 0; i < nc && _count < MAX_NEARBY; i++) {
      ContactInfo ci;
      if (!the_mesh.getContactByIdx(i, ci)) continue;
      if (!typeMatchesFilter(ci.type, ci.flags, true)) continue;

      Entry& e = _entries[_count++];
      strncpy(e.name, ci.name, sizeof(e.name) - 1);
      e.name[sizeof(e.name) - 1] = '\0';
      memcpy(e.pub_key, ci.id.pub_key, PUB_KEY_SIZE);
      e.has_key = true;
      e.lat_e6  = ci.gps_lat;
      e.lon_e6  = ci.gps_lon;
      bool remote_gps = (ci.gps_lat != 0 || ci.gps_lon != 0);
      e.dist_km = (_own_gps && remote_gps)
                    ? geo::haversineKm(_own_lat, _own_lon, ci.gps_lat, ci.gps_lon)
                    : -1.0f;
      e.type        = ci.type;
      e.contact_idx = i;
      e.lastmod     = ci.lastmod;
      e.is_known    = true;
      e.is_live     = false;
      e.live_verified = false;
    }

    mergeLiveTrack();
    mergeRecentlyHeard();
    sortStored();
    clampSelection();
  }

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
    if (saved_idx >= 0) {
      for (int i = 0; i < _count; i++)
        if (_entries[i].contact_idx == saved_idx) { _sel = i; return true; }
    } else if (saved_live && saved_name[0]) {
      for (int i = 0; i < _count; i++)
        if (_entries[i].is_live && strncmp(_entries[i].name, saved_name, sizeof(saved_name) - 1) == 0)
          { _sel = i; return true; }
    }
    return false;
  }

  // Overlay live [LOC] shares onto the stored list: refresh a matching contact
  // with the fresher shared position, and append senders who aren't contacts so
  // a group sharing on a channel still shows up. DM shares match by pubkey
  // prefix (verified); channel shares match by name (best-effort).
  void mergeLiveTrack() {
    if (!_task) return;
    LiveTrackStore& lt = _task->liveTrack();
    uint32_t now = rtc_clock.getCurrentTime();
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
      if (!lt.isActive(i, now)) continue;
      const LiveTrackStore::Entry& s = lt.slotAt(i);

      int m = -1;
      for (int j = 0; j < _count; j++) {
        if (s.verified && _entries[j].has_key
            && memcmp(_entries[j].pub_key, s.key, LiveTrackStore::KEY_LEN) == 0) { m = j; break; }
        if (!s.verified && strncmp(_entries[j].name, s.name, sizeof(_entries[j].name) - 1) == 0) { m = j; break; }
      }

      if (m >= 0) {
        Entry& e = _entries[m];
        // A [LOC] share is an explicit "here I am now", so it defines the pin and
        // the distance (used for proximity sorting). Recency for the time sort is
        // the most recent of the advert and the share.
        e.lat_e6  = s.lat_1e6;
        e.lon_e6  = s.lon_1e6;
        e.dist_km = _own_gps ? geo::haversineKm(_own_lat, _own_lon, s.lat_1e6, s.lon_1e6) : -1.0f;
        if (s.ts > e.lastmod) e.lastmod = s.ts;
        e.is_live       = true;
        e.live_verified = s.verified;
      } else if (_count < MAX_NEARBY && typeMatchesFilter(ADV_TYPE_CHAT, 0, false)) {
        // A sender we don't have as a contact: treat it as a companion (the only
        // node type that shares position), so the type filter still applies —
        // e.g. it must not appear under the Repeater/Room/Sensor filters.
        Entry& e = _entries[_count++];
        memset(&e, 0, sizeof(e));
        strncpy(e.name, s.name, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        // We only keep a key *prefix* for shares, not the full pubkey, so Ping
        // and the base64 key view (which need 32 bytes) stay unavailable for a
        // non-contact live entry. Navigate / Save-waypoint work off lat/lon.
        e.has_key       = false;
        e.type          = ADV_TYPE_CHAT;
        e.lat_e6        = s.lat_1e6;
        e.lon_e6        = s.lon_1e6;
        e.dist_km       = _own_gps ? geo::haversineKm(_own_lat, _own_lon, s.lat_1e6, s.lon_1e6) : -1.0f;
        e.lastmod       = s.ts;
        e.contact_idx   = -1;
        e.is_known      = false;
        e.is_live       = true;
        e.live_verified = s.verified;
      }
    }
  }

  // Fold passively-heard adverts that aren't already listed (contact or live) into
  // the list as name+age rows — no full key/GPS, so informational only. This is
  // what absorbs the old standalone "Recent adverts" home page. Gated to the All
  // filter because AdvertPath carries no node type to filter or sort-by-dist on.
  void mergeRecentlyHeard() {
    if (_filter != F_ALL || !_task) return;
    static AdvertPath heard[8];   // scratch — refreshStored isn't reentrant
    int n = the_mesh.getRecentlyHeard(heard, 8);
    for (int i = 0; i < n && _count < MAX_NEARBY; i++) {
      AdvertPath& a = heard[i];
      if (a.name[0] == 0) continue;
      bool dup = false;
      for (int j = 0; j < _count; j++) {
        if (_entries[j].has_key &&
            memcmp(_entries[j].pub_key, a.pubkey_prefix, sizeof(a.pubkey_prefix)) == 0) { dup = true; break; }
        if (strncmp(_entries[j].name, a.name, sizeof(a.name) - 1) == 0) { dup = true; break; }
      }
      if (dup) continue;
      Entry& e = _entries[_count++];
      memset(&e, 0, sizeof(e));
      strncpy(e.name, a.name, sizeof(e.name) - 1);
      e.name[sizeof(e.name) - 1] = '\0';
      e.type        = ADV_TYPE_CHAT;   // unknown from AdvertPath — best-effort label
      e.has_key     = false;
      e.dist_km     = -1.0f;
      e.lastmod     = a.recv_timestamp;
      e.contact_idx = -1;
      e.is_known    = false;
      e.is_live     = false;
    }
  }

  void sortStored() {
    uint32_t now_ts = rtc_clock.getCurrentTime();
    for (int i = 0; i < _count - 1; i++) {
      int best = i;
      for (int j = i + 1; j < _count; j++) {
        if (_sort == SORT_TIME) {
          // lastmod=0 or lastmod>now (RTC not synced) → "unknown" → sort to bottom.
          uint32_t tj = (_entries[j].lastmod > 0 && now_ts >= _entries[j].lastmod) ? _entries[j].lastmod : 0;
          uint32_t tb = (_entries[best].lastmod > 0 && now_ts >= _entries[best].lastmod) ? _entries[best].lastmod : 0;
          if (tj > 0 && (tb == 0 || tj > tb)) best = j;  // descending — most recent first
        } else {
          float dj = _entries[j].dist_km, db = _entries[best].dist_km;
          if (dj >= 0.0f && (db < 0.0f || dj < db)) best = j;  // ascending — closest first
        }
      }
      if (best != i) { Entry tmp = _entries[i]; _entries[i] = _entries[best]; _entries[best] = tmp; }
    }
  }

  void refreshScan() {
    static DiscoverResult dr[DISCOVER_RESULTS_MAX];  // scratch — refresh isn't reentrant
    int n = the_mesh.getDiscoverResults(dr, DISCOVER_RESULTS_MAX);
    _count = 0;
    for (int i = 0; i < n && _count < MAX_NEARBY; i++) {
      if (!typeMatchesFilter(dr[i].type, 0, false)) continue;
      Entry& e = _entries[_count++];
      strncpy(e.name, dr[i].name, sizeof(e.name) - 1);
      e.name[sizeof(e.name) - 1] = '\0';
      e.type          = dr[i].type;
      memcpy(e.pub_key, dr[i].pub_key, PUB_KEY_SIZE);
      e.has_key       = true;
      e.rssi          = dr[i].rssi;
      e.snr_x4        = dr[i].snr_x4;
      e.remote_snr_x4 = dr[i].remote_snr_x4;
      e.is_known      = dr[i].is_known;
      e.lat_e6 = e.lon_e6 = 0;
      e.dist_km = -1.0f;
      e.lastmod = 0;
      e.contact_idx = -1;
      e.is_live = false;
      e.live_verified = false;
    }
    // strongest first
    for (int i = 0; i < _count - 1; i++) {
      int best = i;
      for (int j = i + 1; j < _count; j++)
        if (_entries[j].rssi > _entries[best].rssi) best = j;
      if (best != i) { Entry tmp = _entries[i]; _entries[i] = _entries[best]; _entries[best] = tmp; }
    }
    clampSelection();
  }

  void refresh() { if (_source == SRC_SCAN) refreshScan(); else refreshStored(); }

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
  void toggleFavourite(const uint8_t* pub_key) {
    int slot = _task->findFavouriteSlot(pub_key);
    if (slot >= 0) {
      _task->clearFavouriteSlot(slot);
      the_mesh.savePrefs();
      _task->showAlert("Unfavourited", 1000);
      return;
    }
    for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) {
      if (_task->isFavouriteSlotEmpty(s)) {
        _task->setFavouriteSlot(s, pub_key);
        the_mesh.savePrefs();
        _task->showAlert("Favourited", 1000);
        return;
      }
    }
    _task->showAlert("Favourites full", 1200);
  }

  // Deleting a contact is destructive → confirm first (default highlight = Cancel).
  void startDeleteConfirm() {
    const Entry* e = selected();
    if (!e || !e->has_key || !entryIsContact(e)) return;
    _confirm.begin("Delete contact?", 2);
    _confirm.addItem("Delete");
    _confirm.addItem("Cancel");
    _confirm.setSelected(1);
    _confirm.active = true;
  }

  void doDeleteSelected() {
    const Entry* e = selected();
    if (!e || !e->has_key || !entryIsContact(e)) return;
    uint8_t key[PUB_KEY_SIZE];
    memcpy(key, e->pub_key, PUB_KEY_SIZE);
    if (the_mesh.deleteContactByKey(key)) {
      _task->showAlert("Contact deleted", 1200);
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

  void openActionMenu() {
    const Entry* e = selected();
    bool stored  = (_source == SRC_STORED);
    bool has_gps = e && stored && (e->lat_e6 != 0 || e->lon_e6 != 0);
    bool has_key = e && e->has_key;
    bool is_contact = entryIsContact(e);
    bool can_add = e && has_key && !is_contact;   // a new node we can save
    bool is_fav  = e && has_key && _task->findFavouriteSlot(e->pub_key) >= 0;
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

    if (has_gps) add("Navigate",      ACT_NAV);
    if (has_key) add("Ping",          ACT_PING);
    if (has_gps) add("Save waypoint", ACT_WAYPOINT);
    // Needs both a position and a stable identity — a person target is keyed
    // by pubkey prefix, so a name-only live-scan/channel row can't offer this.
    if (has_gps && has_key) add("Set as target", ACT_LOCATOR);
    if (can_add)            add("Add contact", ACT_ADD);
    if (is_contact && has_key) add(is_fav ? "Unfavourite" : "Favourite", ACT_FAV);
    if (is_admin_target)       add("Admin", ACT_ADMIN);
    if (is_contact && has_key) add("Delete contact", ACT_DELETE);
    if (stored) add(_sort_label, ACT_SORT);   // sort is meaningless for live-scan rows
    add(stored ? "Discover scan" : "Rescan", ACT_SCAN);
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
        if (e && e->has_key && (e->lat_e6 != 0 || e->lon_e6 != 0))
          _task->setTargetNow(1, e->pub_key, e->lat_e6, e->lon_e6, e->name);
        break;
      }
      case ACT_ADD: {
        const Entry* e = selected();
        if (e && e->has_key) {
          if (the_mesh.addDiscoveredContact(e->pub_key, e->name, e->type)) {
            _task->showAlert("Contact added", 1200);
            refresh();   // now a known contact — re-sort / re-mark this pass
          } else {
            _task->showAlert("Contacts full", 1200);
          }
        }
        break;
      }
      case ACT_FAV: {
        const Entry* e = selected();
        if (e && e->has_key) toggleFavourite(e->pub_key);
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
      case ACT_SORT:     break;  // adjusted in-place via LEFT/RIGHT, not ENTER
      case ACT_SCAN:     enterScan();            break;
    }
  }

  // ── detail rendering ──────────────────────────────────────────────────────────
  void renderStoredDetail(DisplayDriver& display) {
    const Entry& e = _entries[_sel];
    const int hdr  = display.listStart();   // content top (gap below the header separator)
    display.drawInvertedHeader(e.name, true, ctxMenuOpen());

    int step = display.lineStep();
    if (step * 5 > display.height() - hdr) step = (display.height() - hdr) / 5;
    char buf[32];
    snprintf(buf, sizeof(buf), "Lat: %.5f", e.lat_e6 / 1e6);
    display.setCursor(2, hdr); display.print(buf);
    snprintf(buf, sizeof(buf), "Lon: %.5f", e.lon_e6 / 1e6);
    display.setCursor(2, hdr + step); display.print(buf);

    if (e.dist_km >= 0.0f) {
      char dist[12];
      geo::fmtDist(dist, sizeof(dist), e.dist_km, useImperial());
      int az = geo::bearingDeg(_own_lat, _own_lon, e.lat_e6, e.lon_e6);
      snprintf(buf, sizeof(buf), "Dist: %s %s", dist, geo::bearingCardinal(az));
    } else {
      snprintf(buf, sizeof(buf), "Dist: no GPS");
    }
    display.setCursor(2, hdr + step * 2); display.print(buf);
    snprintf(buf, sizeof(buf), "Type: %s", typeName(e.type));
    display.setCursor(2, hdr + step * 3); display.print(buf);
    char age[16];
    fmtAge(age, sizeof(age), e.lastmod);
    // For a live [LOC] row, label the timestamp as a position share and note
    // whether the sender's identity is verified (DM) or name-only (channel).
    if (e.is_live) snprintf(buf, sizeof(buf), "Sharing pos: %s %s", age, e.live_verified ? "(DM)" : "(chan)");
    else           snprintf(buf, sizeof(buf), "Seen: %s", age);
    display.drawTextEllipsized(2, hdr + step * 4, display.width() - 4, buf);
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
    snprintf(buf, sizeof(buf), "Rem:  %.1f dB", e.remote_snr_x4 / 4.0f);
    display.setCursor(2, hdr + step * 3); display.print(buf);
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
    tabbar::draw(display, FILTER_LABELS, F_COUNT, _filter, display.menuHintWidth());
    display.drawContextMenuHint(DisplayDriver::LIGHT, ctxMenuOpen());   // Nodes list has a Hold-Enter menu
  }

public:
  NearbyScreen(UITask* task)
    : _task(task), _count(0), _sel(0), _scroll(0), _detail(false),
      _own_lat(0), _own_lon(0), _own_gps(false),
      _source(SRC_STORED), _filter(F_ALL), _sort(SORT_DIST),
      _detail_refresh_ms(0), _scanning(false), _scan_started_ms(0),
      _menu_action_count(0), _pinging(false), _ping_started_ms(0) {
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

    int item_h   = display.lineStep();
    int dist_col = display.width() - display.getCharWidth() * 7;

    display.setColor(DisplayDriver::LIGHT);
    const char* flt = (_filter != F_ALL) ? FILTER_LABELS[_filter] : nullptr;
    if (_source == SRC_SCAN) {
      char title[28];
      const char* base = _scanning ? "SCANNING" : "SCAN";
      if (flt) {
        if (!_scanning && _count == 0) snprintf(title, sizeof(title), "SCAN %s: none", flt);
        else                           snprintf(title, sizeof(title), "%s %s (%d)", base, flt, _count);
      } else if (_scanning)            snprintf(title, sizeof(title), "SCANNING (%d)", _count);
      else if (_count == 0)            snprintf(title, sizeof(title), "SCAN: none");
      else                             snprintf(title, sizeof(title), "SCAN (%d)", _count);
      display.drawCenteredHeader(title, true, ctxMenuOpen());
    } else {
      // Stored list: the filter is a visible tab strip (LEFT/RIGHT switches tabs).
      drawFilterTabs(display);
    }

    if (_count == 0) {
      char empty[24];
      if (_source == SRC_SCAN) {
        const char* msg;
        if (_scanning)   msg = "Waiting for replies...";
        else if (flt)  { snprintf(empty, sizeof(empty), "No %s nodes", flt); msg = empty; }
        else             msg = "No nodes found";
        display.drawTextCentered(display.width() / 2, display.height() / 2, msg);
      } else {
        const char* hint;
        if (flt) { snprintf(empty, sizeof(empty), "No %s contacts", flt); hint = "[<>] change filter"; }
        else     { snprintf(empty, sizeof(empty), "No contacts found");   hint = "[Enter]=Discover";   }
        display.drawTextCentered(display.width() / 2, display.height() / 2 - display.lineStep() / 2, empty);
        display.drawTextCentered(display.width() / 2, display.height() / 2 + display.lineStep() / 2, hint);
      }
    } else {
      drawList(display, _count, _sel, _scroll, [&](int idx, int y, bool sel, int reserve) {
        const Entry& e = _entries[idx];

        drawRowSelection(display, y, sel, reserve);

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
        // Star = pinned to the Favourites dial (same glyph as the Favourites page
        // icon). Shown next to any live diamond so both states read at once.
        if (e.has_key && _task->findFavouriteSlot(e.pub_key) >= 0) {
          int iw = ICON_PG_STAR.w * miniIconScale(display);
          miniIconDrawCentered(display, tx + iw / 2, y + display.getLineHeight() / 2 - 1, ICON_PG_STAR);
          tx += iw + 2;
        }
        display.translateUTF8ToBlocks(filt, e.name, sizeof(filt));
        if (_source == SRC_SCAN && !e.name[0]) {  // unknown node → "[Type]"
          snprintf(filt, sizeof(filt), "[%s]", typeName(e.type));
        }
        display.drawTextEllipsized(tx, y, dist_col - tx - 2, filt);

        display.setColor(sel ? DisplayDriver::DARK : DisplayDriver::LIGHT);
        char right[10];
        if (_source == SRC_SCAN) {
          snprintf(right, sizeof(right), "%d", (int)e.rssi);
        } else if (_sort == SORT_TIME) {
          geo::fmtAgeShort(right, sizeof(right), rtc_clock.getCurrentTime(), e.lastmod);
          if (!right[0]) snprintf(right, sizeof(right), "?");   // unknown / RTC not synced
        } else {
          if (e.dist_km >= 0.0f) geo::fmtDist(right, sizeof(right), e.dist_km, useImperial());
          else                   strncpy(right, "?GPS", sizeof(right));
        }
        display.drawTextRightAlign(display.width() - reserve - 2, y, right);
      });
    }

    if (renderActivePopup(display)) return 50;
    if (_source == SRC_SCAN) return _scanning ? 200 : 2000;
    return _count == 0 ? 3000 : 2000;
  }

  bool handleInput(char c) override {
    // ── navigate-to-node view — any nav key returns to detail ─────────────────
    if (_nav) {
      if (c == KEY_CANCEL || keyIsPrev(c) || keyIsNext(c)) _nav = false;
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
      // LEFT/RIGHT on the Sort row toggles the value in-place and rebuilds the
      // label; the popup stays open so the user can keep tapping. Other rows
      // swallow L/R. ENTER on Sort just closes (value changes via L/R only).
      if (keyIsPrev(c) || keyIsNext(c)) {
        int i = _menu.selectedIndex();
        if (i >= 0 && i < _menu_action_count && _menu_actions[i] == ACT_SORT) {
          _sort = (_sort == SORT_DIST) ? SORT_TIME : SORT_DIST;
          buildSortLabel();
          refresh();
        }
        return true;
      }
      auto res = _menu.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        int i = _menu.selectedIndex();
        if (i >= 0 && i < _menu_action_count && _menu_actions[i] != ACT_SORT)
          runAction(_menu_actions[i]);
      }
      return true;
    }

    // ── detail view ───────────────────────────────────────────────────────────
    if (_detail) {
      if (c == KEY_CANCEL)            { _detail = false; closePingMenu(); return true; }
      if (c == KEY_CONTEXT_MENU)      { openActionMenu(); return true; }
      return true;
    }

    // ── list view ───────────────────────────────────────────────────────────
    if (c == KEY_CANCEL) {
      if (_pick_admin_target) { _pick_admin_target = false; _task->gotoToolsScreen(); return true; }
      if (_source == SRC_SCAN) leaveScan();
      else                     _task->gotoToolsScreen();
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

const char* NearbyScreen::FILTER_LABELS[F_COUNT] = { "All", "Fav", "Comp", "Rpt", "Room", "Snsr" };
