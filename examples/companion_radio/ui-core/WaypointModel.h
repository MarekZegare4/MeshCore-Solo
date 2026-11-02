#pragma once
// Saved waypoints: the ../Waypoint.h table plus its persistence (/waypoints on
// the node's DataStore, loaded at boot, rewritten on every change) and the one
// rule that ties it to the Locator: deleting the waypoint that is the active
// target clears the target.

#include "../Waypoint.h"
#include "LocatorEngine.h"

class WaypointModel {
public:
  void begin(LocatorEngine* locator) {
    _locator = locator;
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) return;
    File f = ds->openRead("/waypoints");
    if (f) { _store.readFrom(f); f.close(); }
  }

  WaypointStore&       store()       { return _store; }
  const WaypointStore& store() const { return _store; }
  int  count() const { return _store.count(); }
  bool full() const  { return _store.full(); }
  const Waypoint& at(int i) const { return _store.at(i); }

  void save() {
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) return;
    File f = ds->openWrite("/waypoints.tmp");   // temp + rename: a cut-short write keeps the old file
    if (!f) return;
    bool ok = _store.writeTo(f);
    f.close();
    if (ok) ds->commitFile("/waypoints.tmp", "/waypoints");
    else ds->removeFile("/waypoints.tmp");
  }

  // `label` cut to what a waypoint holds (WAYPOINT_LABEL_LEN - 1 bytes) at a
  // UTF-8 character boundary, so a two-byte letter is never split.
  static void fitLabel(char* out, const char* label) {
    size_t n = label ? strlen(label) : 0;
    if (n > WAYPOINT_LABEL_LEN - 1) {
      n = WAYPOINT_LABEL_LEN - 1;
      while (n > 0 && ((uint8_t)label[n] & 0xC0) == 0x80) n--;   // back off a continuation byte
    }
    if (n) memcpy(out, label, n);
    out[n] = '\0';
  }

  // Add and persist. An empty label becomes "WP<n>". False when the table is full.
  bool add(int32_t lat, int32_t lon, uint32_t ts, const char* label) {
    char lbl[WAYPOINT_LABEL_LEN];
    fitLabel(lbl, label);
    if (!lbl[0]) snprintf(lbl, sizeof(lbl), "WP%d", _store.count() + 1);
    if (!_store.add(lat, lon, ts, lbl)) return false;
    save();
    return true;
  }

  void rename(int i, const char* label) {
    if (i < 0 || i >= _store.count()) return;
    char lbl[WAYPOINT_LABEL_LEN];
    fitLabel(lbl, label);
    _store.rename(i, lbl);
    save();
  }

  void remove(int i) {
    if (i < 0 || i >= _store.count()) return;
    const Waypoint& w = _store.at(i);
    if (_locator) _locator->clearTargetIfWaypoint(w.lat_1e6, w.lon_1e6);
    _store.remove(i);
    save();
  }

  // "[WAY]lat,lon label" -- the message form other Solo nodes turn back into a waypoint.
  void shareText(int i, char* out, size_t n) const {
    if (i < 0 || i >= _store.count()) { out[0] = '\0'; return; }
    const Waypoint& w = _store.at(i);
    int o = snprintf(out, n, WAYPOINT_MSG_TAG);
    geo::fmtLatLon(out + o, (int)n - o, w.lat_1e6, w.lon_1e6);
    if (w.label[0]) { size_t l = strlen(out); snprintf(out + l, n - l, " %s", w.label); }
  }

private:
  WaypointStore  _store;
  LocatorEngine* _locator = nullptr;
};
