#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// RAM-only table of positions other nodes have shared via [LOC] messages
// (see geo::parseLocShare / LOCATION_MSG_TAG). Lets the device show a live
// bearing/distance to people who broadcast their location on a DM or channel,
// independent of how often they advert.
//
// Keying:
//   DM share      → keyed by pubkey prefix, verified == true  (reliable).
//   Channel share → keyed by sender name,   verified == false (best-effort:
//                    channel sender names are unsigned and not unique).
//
// The table never persists (positions go stale fast) and entries expire after
// EXPIRY_SECS without a fresh update. When full, the oldest entry is evicted.
class LiveTrackStore {
public:
  static const int      CAPACITY    = 16;            // max simultaneously tracked nodes
  static const uint32_t EXPIRY_SECS = 20UL * 60UL;   // drop a track 20 min after its last update
  static const uint32_t SOS_EXPIRY_SECS = 24UL * 3600UL;   // a call for help: a day, whatever came after it
  static const int      NAME_LEN    = 16;            // display name buffer (incl. NUL)
  static const int      KEY_LEN     = 6;             // pubkey prefix bytes (matches favourites)

  struct Entry {
    uint8_t  key[KEY_LEN];      // pubkey prefix (verified DM); all-zero for channel entries
    char     name[NAME_LEN];    // display name, always set
    int32_t  lat_1e6, lon_1e6;
    uint32_t ts;                // RTC epoch seconds of the share
    bool     verified;          // true = DM/pubkey, false = channel/name
    bool     sos;               // from an SOS: kept longer, evicted last
    bool     used;
  };

  // Insert or refresh a shared position. `key` may be null/ignored when not
  // verified. `name` is required (used as the channel key and for display).
  void update(const uint8_t* key, const char* name,
              int32_t lat_1e6, int32_t lon_1e6, uint32_t ts, bool verified, bool sos = false) {
    int slot = find(key, name, verified);
    const bool was_sos = slot >= 0 && _e[slot].sos;
    if (slot < 0) slot = allocSlot();
    Entry& e = _e[slot];
    if (verified && key) memcpy(e.key, key, KEY_LEN);
    else                 memset(e.key, 0, KEY_LEN);
    if (name) { strncpy(e.name, name, NAME_LEN - 1); e.name[NAME_LEN - 1] = '\0'; }
    else      { e.name[0] = '\0'; }
    e.lat_1e6  = lat_1e6;
    e.lon_1e6  = lon_1e6;
    e.ts       = ts;
    e.verified = verified;
    e.sos      = sos || was_sos;
    e.used     = true;
  }

  // An advert from someone whose SOS is on the table moves it: their beacon
  // (UiCore::sosBeacon). Matched by key (a DM's SOS), or by "SOS <name>" (a
  // channel's, unverified like the message itself). False when there's none.
  bool followSos(const uint8_t* key, const char* name, int32_t lat_1e6, int32_t lon_1e6, uint32_t ts) {
    char label[NAME_LEN];
    snprintf(label, sizeof(label), "SOS %s", name ? name : "");
    bool moved = false;
    for (int i = 0; i < CAPACITY; i++) {
      Entry& e = _e[i];
      if (!e.used || !e.sos) continue;
      if (e.verified ? !(key && memcmp(e.key, key, KEY_LEN) == 0) : strcmp(e.name, label) != 0) continue;
      e.lat_1e6 = lat_1e6;
      e.lon_1e6 = lon_1e6;
      e.ts = ts;
      moved = true;
    }
    return moved;
  }

  // Drop entries not refreshed within EXPIRY_SECS. `now` is RTC epoch seconds.
  void expire(uint32_t now) {
    for (int i = 0; i < CAPACITY; i++)
      if (_e[i].used && expired(_e[i], now)) _e[i].used = false;
  }

  void clear() { for (int i = 0; i < CAPACITY; i++) _e[i].used = false; }

  // Slot-wise access (caller skips inactive slots via isActive()).
  const Entry& slotAt(int i) const { return _e[i]; }
  bool isActive(int i, uint32_t now) const {
    return _e[i].used && !expired(_e[i], now);
  }

  // Number of currently non-expired entries.
  int active(uint32_t now) const {
    int n = 0;
    for (int i = 0; i < CAPACITY; i++) if (isActive(i, now)) n++;
    return n;
  }

  // Latest active position for a verified (DM/pubkey) share, or null if that
  // node hasn't shared recently. Used by the locator engine to follow a
  // moving contact.
  const Entry* activeByKey(const uint8_t* key, uint32_t now) const {
    if (!key) return nullptr;
    for (int i = 0; i < CAPACITY; i++) {
      if (!isActive(i, now)) continue;
      const Entry& e = _e[i];
      if (e.verified && memcmp(e.key, key, KEY_LEN) == 0) return &e;
    }
    return nullptr;
  }

  // Latest active position for a channel share by sender name. `name` may be
  // cut short (the Locator keeps 11 bytes): then it matches as a prefix.
  const Entry* activeByName(const char* name, size_t stored_max, uint32_t now) const {
    if (!name || !name[0]) return nullptr;
    size_t n = strlen(name);
    for (int i = 0; i < CAPACITY; i++) {
      if (!isActive(i, now)) continue;
      const Entry& e = _e[i];
      if (e.verified) continue;
      if (n >= stored_max ? strncmp(e.name, name, n) == 0 : strcmp(e.name, name) == 0) return &e;
    }
    return nullptr;
  }

private:
  Entry _e[CAPACITY] = {};

  // An entry is stale once EXPIRY_SECS have passed since its last update.
  // Guarded against now < ts (RTC stepped backwards) so a clock fix can't
  // mass-expire the table.
  static bool expired(const Entry& e, uint32_t now) {
    return now > e.ts && (now - e.ts) > (e.sos ? SOS_EXPIRY_SECS : EXPIRY_SECS);
  }

  // Match an existing entry: verified shares by key, channel shares by name.
  int find(const uint8_t* key, const char* name, bool verified) const {
    for (int i = 0; i < CAPACITY; i++) {
      if (!_e[i].used) continue;
      if (verified) {
        if (_e[i].verified && key && memcmp(_e[i].key, key, KEY_LEN) == 0) return i;
      } else {
        if (!_e[i].verified && name && strncmp(_e[i].name, name, NAME_LEN - 1) == 0) return i;
      }
    }
    return -1;
  }

  // First free slot, else evict the oldest (smallest ts), an SOS only if all are.
  int allocSlot() {
    int oldest = 0;
    for (int i = 0; i < CAPACITY; i++) {
      if (!_e[i].used) return i;
      if (_e[oldest].sos != _e[i].sos ? _e[oldest].sos : _e[i].ts < _e[oldest].ts) oldest = i;
    }
    return oldest;
  }
};
