#pragma once
// Planned routes: a GPX track or route planned beforehand, loaded onto the
// device to be followed (ui-core/RouteGuide.h). Tools > Trail > Routes.
//
// The site's Tools page reads the GPX, simplifies it to what the device holds
// and sends the points over USB (MyMeshRoutes.h, CMD_ROUTE). Here they are
// files on DataStore::getBulkFS(): the QSPI flash of the L1s and the Watch13
// (where the kept messages are, ui-core/HistoryLog.h), the spiffs partition of
// an ESP32, the sim's disk. Never the nRF52's internal 28 KB.
//
// A route is a slot, /rt0 .. /rt<SLOTS-1>, flat names (SPIFFS has no
// directories). One more, TRAIL_SLOT, isn't listed or reachable over USB: Track
// back's copy of the trail, followed backwards as any route is. The file is its points and then a trailer, so it's written
// start to end with nothing gone back over; it's written as /rt.tmp and
// renamed over the slot once whole, so a cut-short upload leaves the slot as it
// was. A file is good when its size is exactly count points + the trailer.
//
// A point carries its distance along the route from the start (cum_m), worked
// out here as the points are written, so the guide knows how far is left
// without walking the route. Points are read a block at a time; nothing keeps a
// route in RAM.

#include <Arduino.h>
#include <string.h>
#include "DataStore.h"
#include "GeoUtils.h"

#ifndef ROUTE_SLOTS
#define ROUTE_SLOTS 8
#endif
#ifndef ROUTE_MAX_POINTS
#define ROUTE_MAX_POINTS 4000   // 64 KB a route at 16 B a point
#endif

static const int16_t ROUTE_ELE_NONE = -32768;
static const uint8_t ROUTE_NAME_LEN = 24;   // incl. NUL

struct RoutePoint {
  int32_t  lat_1e6, lon_1e6;
  uint32_t cum_m;      // metres along the route from its first point
  int16_t  ele_m;      // ROUTE_ELE_NONE if the GPX had none
  uint16_t _pad;
};
static_assert(sizeof(RoutePoint) == 16, "RoutePoint is the file's record");

struct RouteInfo {
  uint32_t magic;
  uint8_t  version;
  uint8_t  flags;      // FLAG_ELE: points carry heights
  uint16_t count;
  uint32_t length_m;
  int32_t  min_lat, min_lon, max_lat, max_lon;
  char     name[ROUTE_NAME_LEN];
};
static_assert(sizeof(RouteInfo) == 52, "RouteInfo is the file's trailer");

class PlannedRoutes {
public:
  static const int      SLOTS = ROUTE_SLOTS;
  static const int      TRAIL_SLOT = ROUTE_SLOTS;   // Track back's (ui-core/UiCore.h)
  static bool validSlot(int slot) { return slot >= 0 && slot <= TRAIL_SLOT; }
  static const int      MAX_POINTS = ROUTE_MAX_POINTS;
  static const uint32_t MAGIC = 0x31455452;   // "RTE1" little-endian
  static const uint8_t  VERSION = 1;
  static const uint8_t  FLAG_ELE = 0x01;

  // Bumped on every change to the slots (an upload, a delete), so a guide
  // following one, or a list showing them, knows to look again.
  static uint16_t& generation() { static uint16_t g = 0; return g; }

  static FILESYSTEM* fs() {
    DataStore* ds = the_mesh.getDataStore();
    return ds ? ds->getBulkFS() : nullptr;
  }
  static void slotPath(int slot, char* out, int n) { snprintf(out, n, "/rt%d", slot); }

  // An nRF52 File has no default constructor: an unopened one belongs to an FS.
  static File noFile(FILESYSTEM* f) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    return File(*f);
#else
    (void)f;
    return File();
#endif
  }
  // Opened only when it's there: an ESP32 logs an error for a missing one.
  // The caller checks fs() first.
  static File openRead(const char* path) {
    FILESYSTEM* f = fs();
    DataStore* ds = the_mesh.getDataStore();
    if (!f->exists(path)) return noFile(f);
    return ds->openRead(f, path);
  }

  // The slot's trailer; false when it's empty or not a whole route.
  static bool info(int slot, RouteInfo& out) {
    if (!validSlot(slot)) return false;
    if (!fs()) return false;
    char p[8]; slotPath(slot, p, sizeof(p));
    File f = openRead(p);
    if (!f) return false;
    bool ok = readInfo(f, out);
    f.close();
    return ok;
  }
  static bool readInfo(File& f, RouteInfo& out) {
    const uint32_t size = f.size();
    if (size < sizeof(RouteInfo)) return false;
    if (!f.seek(size - sizeof(RouteInfo))) return false;
    if (f.read((uint8_t*)&out, sizeof(out)) != (int)sizeof(out)) return false;
    if (out.magic != MAGIC || out.version != VERSION || out.count < 2) return false;
    if (size != (uint32_t)out.count * sizeof(RoutePoint) + sizeof(RouteInfo)) return false;
    out.name[ROUTE_NAME_LEN - 1] = '\0';
    return true;
  }

  // Bit i set: slot i holds a route.
  static uint32_t usedMask() {
    uint32_t m = 0;
    RouteInfo ri;
    for (int i = 0; i < SLOTS; i++) if (info(i, ri)) m |= (1u << i);
    return m;
  }

  static bool remove(int slot) {
    FILESYSTEM* f = fs();
    if (!f || !validSlot(slot)) return false;
    char p[8]; slotPath(slot, p, sizeof(p));
    bool ok = f->remove(p);
    generation()++;
    return ok;
  }

  // Points [start, start + n) of an open route file into out; how many were read.
  static int readPoints(File& f, int start, int n, RoutePoint* out) {
    if (n <= 0 || !f.seek((uint32_t)start * sizeof(RoutePoint))) return 0;
    int got = f.read((uint8_t*)out, (size_t)n * sizeof(RoutePoint));
    return got > 0 ? got / (int)sizeof(RoutePoint) : 0;
  }

  // fn(const RoutePoint&, int index) for every point of a slot, a block at a time.
  template <class Fn>
  static bool forEach(int slot, Fn fn) {
    if (!fs() || !validSlot(slot)) return false;
    char p[8]; slotPath(slot, p, sizeof(p));
    File f = openRead(p);
    if (!f) return false;
    RouteInfo ri;
    if (!readInfo(f, ri)) { f.close(); return false; }
    RoutePoint buf[32];
    for (int i = 0; i < ri.count; ) {
      int n = readPoints(f, i, (ri.count - i) < 32 ? (ri.count - i) : 32, buf);
      if (n <= 0) break;
      for (int k = 0; k < n; k++) fn(buf[k], i + k);
      i += n;
    }
    f.close();
    return true;
  }

  // ── Writing (one upload at a time) ─────────────────────────────────────────
  // begin() -> add() count times -> finish(). abort() drops it.
  bool begin(int slot, uint16_t count, uint8_t flags, const char* name) {
    abort();
    FILESYSTEM* f = fs();
    DataStore* ds = the_mesh.getDataStore();
    if (!f || !ds || !validSlot(slot) || count < 2 || count > MAX_POINTS) return false;
    _slot = slot;
    _file = new File(ds->openWrite(f, tmp()));
    if (!*_file) { delete _file; _file = nullptr; return false; }
    memset(&_info, 0, sizeof(_info));
    _info.magic = MAGIC;
    _info.version = VERSION;
    _info.flags = flags & FLAG_ELE;
    _info.count = count;
    size_t n = name ? strlen(name) : 0;
    if (n > ROUTE_NAME_LEN - 1) {
      n = ROUTE_NAME_LEN - 1;
      while (n > 0 && ((uint8_t)name[n] & 0xC0) == 0x80) n--;   // not mid-letter
    }
    if (n) memcpy(_info.name, name, n);
    _written = 0;
    _open = true;
    return true;
  }

  bool add(int32_t lat, int32_t lon, int16_t ele) {
    if (!_open || _written >= _info.count) return false;
    if (lat < -90000000 || lat > 90000000 || lon < -180000000 || lon > 180000000) return false;
    RoutePoint p;
    p.lat_1e6 = lat; p.lon_1e6 = lon; p.ele_m = ele; p._pad = 0;
    if (_written == 0) {
      _info.min_lat = _info.max_lat = lat;
      _info.min_lon = _info.max_lon = lon;
      _cum = 0;
    } else {
      _cum += geo::haversineKm(_last_lat, _last_lon, lat, lon) * 1000.0f;
      if (lat < _info.min_lat) _info.min_lat = lat;  if (lat > _info.max_lat) _info.max_lat = lat;
      if (lon < _info.min_lon) _info.min_lon = lon;  if (lon > _info.max_lon) _info.max_lon = lon;
    }
    p.cum_m = (uint32_t)(_cum + 0.5f);
    _last_lat = lat; _last_lon = lon;
    if (_file->write((uint8_t*)&p, sizeof(p)) != sizeof(p)) { abort(); return false; }
    _written++;
    return true;
  }

  uint16_t written() const { return _written; }
  bool     open() const    { return _open; }
  int      slot() const    { return _slot; }

  bool finish() {
    if (!_open || _written != _info.count) { abort(); return false; }
    _info.length_m = (uint32_t)(_cum + 0.5f);
    bool ok = _file->write((uint8_t*)&_info, sizeof(_info)) == sizeof(_info);
    closeFile();
    DataStore* ds = the_mesh.getDataStore();
    char p[8]; slotPath(_slot, p, sizeof(p));
    if (ok) ok = ds->commitFile(fs(), tmp(), p);
    if (!ok) fs()->remove(tmp());
    generation()++;
    return ok;
  }

  void abort() {
    if (!_open) return;
    closeFile();
    if (fs()) fs()->remove(tmp());
  }

  // The first empty slot, else -1.
  static int freeSlot() {
    RouteInfo ri;
    for (int i = 0; i < SLOTS; i++) if (!info(i, ri)) return i;
    return -1;
  }

private:
  static constexpr const char* TMP = "/rt.tmp";
  // Track back's own, so it can't meet a USB upload's half-written file.
  const char* tmp() const { return _slot == TRAIL_SLOT ? "/rtt.tmp" : TMP; }
  void closeFile() {
    if (_file) { _file->close(); delete _file; _file = nullptr; }
    _open = false;
  }
  File*     _file = nullptr;
  RouteInfo _info;
  int       _slot = -1;
  uint16_t  _written = 0;
  bool      _open = false;
  float     _cum = 0;
  int32_t   _last_lat = 0, _last_lon = 0;
};
