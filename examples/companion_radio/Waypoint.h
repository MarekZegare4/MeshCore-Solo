#pragma once
// Saved GPS waypoints — small fixed table persisted to /waypoints. Unlike the
// trail (a RAM ring), waypoints are long-lived: loaded at boot, rewritten on
// every add / delete / rename. Used by the navigation feature to mark a spot
// (car, camp, water…) and later get bearing + distance back to it.

#include <Arduino.h>
#include <string.h>
#include "Persist.h"

static const uint8_t WAYPOINT_LABEL_LEN = 12;   // incl. NUL → 11 visible chars

struct Waypoint {
  int32_t  lat_1e6, lon_1e6;
  uint32_t ts;                          // RTC time when marked
  char     label[WAYPOINT_LABEL_LEN];
};

#ifndef WAYPOINT_CAPACITY
#define WAYPOINT_CAPACITY 16   // the L2 raises it (-D WAYPOINT_CAPACITY=64)
#endif

class WaypointStore {
public:
  static const int CAPACITY = WAYPOINT_CAPACITY;

  int  count() const { return _count; }
  bool full()  const { return _count >= CAPACITY; }
  const Waypoint& at(int i) const { return _wp[i]; }

  // Add a waypoint; label may be empty (caller can auto-name). Returns false
  // if the table is full. Trims/copies the label safely.
  bool add(int32_t lat, int32_t lon, uint32_t ts, const char* label) {
    if (_count >= CAPACITY) return false;
    Waypoint& w = _wp[_count++];
    w.lat_1e6 = lat; w.lon_1e6 = lon; w.ts = ts;
    setLabel(w, label);
    return true;
  }

  void remove(int i) {
    if (i < 0 || i >= _count) return;
    for (int j = i; j < _count - 1; j++) _wp[j] = _wp[j + 1];
    _count--;
  }

  void rename(int i, const char* label) {
    if (i < 0 || i >= _count) return;
    setLabel(_wp[i], label);
  }

  void move(int i, int32_t lat, int32_t lon) {
    if (i < 0 || i >= _count) return;
    _wp[i].lat_1e6 = lat; _wp[i].lon_1e6 = lon;
  }

  void clear() { _count = 0; }

  // ── persistence ──────────────────────────────────────────────────────────
  // Layout: 4-byte magic "WYPT", uint8 version, uint8 reserved, uint16 count,
  // then `count` raw Waypoint records.
  static const uint32_t SAVE_MAGIC   = 0x54505957;  // "WYPT" little-endian
  static const uint8_t  SAVE_VERSION = 1;

  template <typename F>
  bool writeTo(F& file) {
    if (!persist::writeHeader(file, SAVE_MAGIC, SAVE_VERSION, (uint16_t)_count)) return false;
    for (int i = 0; i < _count; i++) {
      if (file.write((uint8_t*)&_wp[i], sizeof(Waypoint)) != sizeof(Waypoint)) return false;
    }
    return true;
  }

  template <typename F>
  bool readFrom(F& file) {
    uint16_t cnt = 0;
    if (!persist::readHeader(file, SAVE_MAGIC, SAVE_VERSION, cnt)) return false;
    if (cnt > CAPACITY) cnt = CAPACITY;
    _count = 0;
    for (uint16_t i = 0; i < cnt; i++) {
      Waypoint w;
      if (file.read((uint8_t*)&w, sizeof(Waypoint)) != (int)sizeof(Waypoint)) break;
      w.label[WAYPOINT_LABEL_LEN - 1] = '\0';   // defensive
      _wp[_count++] = w;
    }
    return true;
  }

private:
  Waypoint _wp[CAPACITY];
  int      _count = 0;

  static void setLabel(Waypoint& w, const char* label) {
    if (label && label[0]) {
      strncpy(w.label, label, WAYPOINT_LABEL_LEN - 1);
      w.label[WAYPOINT_LABEL_LEN - 1] = '\0';
    } else {
      w.label[0] = '\0';
    }
  }
};
