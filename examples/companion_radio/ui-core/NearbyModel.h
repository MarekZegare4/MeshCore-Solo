#pragma once
// Nearby-nodes list model, shared by every frontend's Nearby screen: builds one
// list of entries from two sources and applies the type filter and sort.
//   SRC_STORED -- contacts, with live [LOC] shares overlaid (fresher position,
//                 plus senders who aren't contacts) and, under the All filter,
//                 passively heard adverts that aren't listed yet
//   SRC_SCAN   -- NODE_DISCOVER_REQ results (RSSI / SNR), strongest first
// Selection, scrolling and actions stay in the frontend. Each screen owns its
// model (ui-new inherits it, so the entry array lives with the screen as
// before) and binds it to the Core for own position and live shares.
//
// Header-only; include after UiCore.h (needs the_mesh, rtc_clock, geo::).

#include "../GeoUtils.h"

class NearbyModel {
public:
  enum Filter : uint8_t { F_ALL, F_FAV, F_COMP, F_RPT, F_ROOM, F_SNSR, F_COUNT };
  enum Sort : uint8_t { SORT_DIST, SORT_TIME };
  enum Source : uint8_t { SRC_STORED, SRC_SCAN };

  struct Entry {
    char     name[32];
    uint8_t  type;
    uint8_t  pub_key[PUB_KEY_SIZE];
    // has_key: the full 32-byte pubkey is present -- what Ping and the base64
    // key view need. has_prefix: at least the leading FAVOURITE_PREFIX_LEN
    // bytes are, which is all an identity-keyed reference needs (a Locator
    // person target). Every has_key row also has_prefix; the reverse doesn't
    // hold -- a [LOC] share and a heard advert carry a prefix and nothing more.
    bool     has_key;
    bool     has_prefix;
    // stored-source fields
    int32_t  lat_e6, lon_e6;
    float    dist_km;        // -1 = unknown (no own fix or no node position)
    uint32_t lastmod;
    int      contact_idx;    // raw contact-table index, -1 = not a contact
    // scan-source fields
    int8_t   rssi, snr_x4, remote_snr_x4;
    bool     is_known;
    // live-track ([LOC] share) overlay: this row's position/age came from a
    // shared-location message. live_verified: a DM (pubkey) share, not a
    // channel (name, best-effort) one.
    bool     is_live;
    bool     live_verified;
    // Favourite (ContactInfo::flags bit 0) -- only a contact carries it.
    bool     fav;
  };

#ifndef NEARBY_MAX
#define NEARBY_MAX 32   // the L2 raises it (-D NEARBY_MAX=64)
#endif
  static const int MAX_NEARBY = NEARBY_MAX;

  static const char* const* filterLabels() {
    static const char* const L[F_COUNT] = { "All", "Fav", "Comp", "Rpt", "Room", "Snsr" };
    return L;
  }
  static const char* filterLabel(uint8_t f) { return f < F_COUNT ? filterLabels()[f] : ""; }

  static const char* typeName(uint8_t t) {
    switch (t) {
      case ADV_TYPE_CHAT:     return "Companion";
      case ADV_TYPE_REPEATER: return "Repeater";
      case ADV_TYPE_ROOM:     return "Room";
      case ADV_TYPE_SENSOR:   return "Sensor";
      default:                return "Unknown";
    }
  }

  void bindModel(UiCore* core, NodePrefs* prefs) { _core = core; _prefs = prefs; }

  int          count() const          { return _count; }
  const Entry& at(int i) const        { return _entries[i]; }
  Source       source() const         { return _source; }
  uint8_t      filter() const         { return _filter; }
  uint8_t      sortMode() const       { return _sort; }
  bool         ownPosition(int32_t& lat, int32_t& lon) const { lat = _own_lat; lon = _own_lon; return _own_gps; }
  void setSource(Source s)            { _source = s; }
  void setFilter(uint8_t f)           { _filter = f < F_COUNT ? f : F_ALL; }
  void setSortMode(uint8_t s)         { _sort = s; }

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

  // ── data refresh ──────────────────────────────────────────────────────────
  void refreshStored() {
    _count = 0;
    _own_lat = _own_lon = 0;
    _own_gps = _core && _core->course.currentLocation(_own_lat, _own_lon);

    int nc = the_mesh.getNumContacts();
    for (int i = 0; i < nc && _count < MAX_NEARBY; i++) {
      ContactInfo ci;
      // getContactByIdx() indexes the RAW contact table, whose first
      // MAX_ANON_CONTACTS slots are reserved for anon-request bookkeeping
      // (see BaseChatMesh::resetContacts()/ContactsIterator) -- getNumContacts()
      // already excludes them from the count, so real contact 0 lives at raw
      // index MAX_ANON_CONTACTS, not 0. Reading from 0 pulled those reserved
      // (blank, type=ADV_TYPE_NONE) slots into the list as bogus "Unknown"
      // rows, and silently dropped the same number of real contacts off the end.
      if (!the_mesh.getContactByIdx(i + MAX_ANON_CONTACTS, ci)) continue;
      if (!typeMatchesFilter(ci.type, ci.flags, true)) continue;

      Entry& e = _entries[_count++];
      strncpy(e.name, ci.name, sizeof(e.name) - 1);
      e.name[sizeof(e.name) - 1] = '\0';
      memcpy(e.pub_key, ci.id.pub_key, PUB_KEY_SIZE);
      e.has_key = true;
      e.has_prefix = true;
      e.lat_e6  = ci.gps_lat;
      e.lon_e6  = ci.gps_lon;
      bool remote_gps = (ci.gps_lat != 0 || ci.gps_lon != 0);
      e.dist_km = (_own_gps && remote_gps)
                    ? geo::haversineKm(_own_lat, _own_lon, ci.gps_lat, ci.gps_lon)
                    : -1.0f;
      e.type        = ci.type;
      e.fav         = (ci.flags & 0x01) != 0;
      e.contact_idx = i + MAX_ANON_CONTACTS;   // raw index -- other lookups re-key off this directly
      e.lastmod     = ci.lastmod;
      e.is_known    = true;
      e.is_live     = false;
      e.live_verified = false;
    }

    mergeLiveTrack();
    mergeRecentlyHeard();
    sortStored();
  }

  void refreshScan() {
    static DiscoverResult dr[DISCOVER_RESULTS_MAX];  // scratch -- refresh isn't reentrant
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
      e.has_prefix    = true;
      e.rssi          = dr[i].rssi;
      e.snr_x4        = dr[i].snr_x4;
      e.remote_snr_x4 = dr[i].remote_snr_x4;
      e.is_known      = dr[i].is_known;
      e.fav           = false;
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
  }

  void refreshModel() { if (_source == SRC_SCAN) refreshScan(); else refreshStored(); }

  // Index of the row for raw contact index `contact_idx`, or of the non-contact
  // live row named `name` -- how a screen keeps its selection across a rebuild.
  int findContact(int contact_idx) const {
    for (int i = 0; i < _count; i++) if (_entries[i].contact_idx == contact_idx) return i;
    return -1;
  }
  int findLiveByName(const char* name) const {
    for (int i = 0; i < _count; i++)
      if (_entries[i].is_live && strncmp(_entries[i].name, name, sizeof(_entries[i].name) - 1) == 0) return i;
    return -1;
  }

protected:
  UiCore*    _core = nullptr;
  NodePrefs* _prefs = nullptr;

  Entry   _entries[MAX_NEARBY];
  int     _count = 0;
  int32_t _own_lat = 0, _own_lon = 0;
  bool    _own_gps = false;

  Source  _source = SRC_STORED;
  uint8_t _filter = F_ALL;
  uint8_t _sort = SORT_DIST;

  // Overlay live [LOC] shares onto the stored list: refresh a matching contact
  // with the fresher shared position, and append senders who aren't contacts so
  // a group sharing on a channel still shows up. DM shares match by pubkey
  // prefix (verified); channel shares match by name (best-effort).
  void mergeLiveTrack() {
    if (!_core) return;
    LiveTrackStore& lt = _core->live_share.track();
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
        // node type that shares position), so the type filter still applies.
        Entry& e = _entries[_count++];
        memset(&e, 0, sizeof(e));
        strncpy(e.name, s.name, sizeof(e.name) - 1);
        e.name[sizeof(e.name) - 1] = '\0';
        // Only a key *prefix* is kept for shares, so Ping and the key view stay
        // unavailable for a non-contact live entry; Navigate / waypoint work off
        // lat/lon. A DM share's prefix is real (a Locator person target); a
        // channel share is matched by name and carries no identity at all.
        e.has_key       = false;
        e.has_prefix    = s.verified;
        if (s.verified) memcpy(e.pub_key, s.key, LiveTrackStore::KEY_LEN);
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
  // the list as name+age rows -- no full key/GPS, so informational only. Gated to
  // the All filter because AdvertPath carries no node type to filter or sort on.
  void mergeRecentlyHeard() {
    if (_filter != F_ALL) return;
    static AdvertPath heard[8];   // scratch -- refreshStored isn't reentrant
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
      e.type        = ADV_TYPE_CHAT;   // unknown from AdvertPath -- best-effort label
      e.has_key     = false;
      e.has_prefix  = true;
      memcpy(e.pub_key, a.pubkey_prefix, sizeof(a.pubkey_prefix));
      e.dist_km     = -1.0f;
      e.lastmod     = a.recv_timestamp;
      e.contact_idx = -1;
      e.is_known    = false;
      e.is_live     = false;
    }
  }

  void sortStored() {
    uint32_t now_ts = rtc_clock.getCurrentTime();
    const bool fav_first = (!_prefs || _prefs->fav_sort);
    for (int i = 0; i < _count - 1; i++) {
      int best = i;
      for (int j = i + 1; j < _count; j++) {
        if (fav_first && _entries[j].fav != _entries[best].fav) {
          if (_entries[j].fav) best = j;   // favourites outrank the time/distance key
          continue;
        }
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
};
