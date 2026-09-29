#pragma once
// On-device map download: fetches every tile of an area over a zoom range
// into <root>/{z}/{x}/{y}.png, the layout RasterTileProvider reads (the PC
// alternative is tools/maps/fetch_tiles.py). Tiles already on the card are
// skipped, so a download can be repeated to fill gaps or extend an area.
//
// The job (area + zoom range) is kept in <root>/.job until it completes, so a
// download cut short by a power-off, a lost network or Stop can be resumed:
// resuming re-runs the same job, and the tiles already written are skipped.
//
// Driven by loop() from the UI loop: one small step per call, the HTTP GET
// itself runs asynchronously in the board's fetcher (lvport::fetch*), so the
// mesh keeps being serviced during a long download.
//
// The server comes from <root>/source.txt (line 1: URL template with {z} {x}
// {y}, line 2: attribution, optional line 3: the server's last zoom level,
// default 19 -- or 17 for OpenTopoMap); without it, OpenTopoMap. Whatever the source,
// its attribution is written to <root>/attribution.txt for the map to show.
//
// Hiking trails (s_trails_on): each tile's trails overlay is fetched right
// after its base tile, into TRAILS_ROOT (TileProvider.h) -- empty ones as
// 0-byte files, so re-running an area already on the card adds just the
// trails.
//
// Live tiles: while the map is open, single tiles it is missing are fetched
// the same way (one at a time, between the job's steps when there is no job),
// the WiFi connected on the first miss and dropped after a minute with
// nothing missing, or by the caller (liveEnd) once the map has been closed
// for a while. They go to LIVE_ROOT, not
// <root>, and only up to the Storage limit (LiveCache.h).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only (after LvglPort.h).

#include <math.h>
#include <sys/stat.h>
#include <errno.h>

namespace mapview {

static const char* const DEFAULT_TILE_URL = "https://a.tile.opentopomap.org/{z}/{x}/{y}.png";
static const char* const DEFAULT_TILE_ATTR = "\xC2\xA9 OpenStreetMap contributors, SRTM | \xC2\xA9 OpenTopoMap (CC-BY-SA)";

struct TileArea {
  double lon0, lat0, lon1, lat1;   // west, south, east, north
  int zmin, zmax;
};

static void tileRange(const TileArea& a, int z, int& x0, int& y0, int& x1, int& y1) {
  int n = 1 << z;
  auto tx = [n](double lon) { int x = (int)floor((lon + 180.0) / 360.0 * n); return x < 0 ? 0 : x >= n ? n - 1 : x; };
  auto ty = [n](double lat) {
    double r = lat * M_PI / 180.0;
    int y = (int)floor((1.0 - asinh(tan(r)) / M_PI) / 2.0 * n);
    return y < 0 ? 0 : y >= n ? n - 1 : y;
  };
  x0 = tx(a.lon0); x1 = tx(a.lon1);
  y0 = ty(a.lat1); y1 = ty(a.lat0);   // tile y grows southwards
}

static uint32_t countTiles(const TileArea& a) {
  uint32_t total = 0;
  for (int z = a.zmin; z <= a.zmax; z++) {
    int x0, y0, x1, y1;
    tileRange(a, z, x0, y0, x1, y1);
    total += (uint32_t)(x1 - x0 + 1) * (uint32_t)(y1 - y0 + 1);
  }
  return total;
}

// mkdir -p for the directories of `path` (not the file itself).
static void makeParents(const char* path) {
  char tmp[96];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char* p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    mkdir(tmp, 0775);   // EEXIST is fine
    *p = '/';
  }
}

class TileDownloader {
public:
  enum State : uint8_t { IDLE, CONNECTING, RUNNING, DONE, FAILED, CANCELLED };
  static const uint32_t MAX_TILES = 40000;   // ~1 GB; beyond that, the PC tool

  explicit TileDownloader(const char* root) : _root(root) {}

  State state() const       { return _state; }
  const TileArea& area() const { return _area; }   // of the current / last job
  bool withTrails() const   { return _trails; }
  bool  active() const      { return _state == CONNECTING || _state == RUNNING; }
  uint32_t total() const    { return _total; }
  uint32_t processed() const { return _done + _skipped + _failed; }
  uint32_t downloaded() const { return _done; }
  uint32_t failed() const   { return _failed; }
  const char* message() const { return _msg; }
  const char* sourceHost() { loadSource(); return _host; }
  // The server's last zoom level: past it, the map magnifies the parent tile.
  int sourceMaxZ() { loadSource(); return _src_zmax; }

  // Unfinished job from <root>/.job (read once, then tracked in memory).
  bool savedJob(TileArea& a) {
    if (!_job_checked) { _job_checked = true; _has_job = readJob(_job); }
    if (_has_job) a = _job;
    return _has_job;
  }
  void discardJob() {
    char path[64];
    jobPath(path, sizeof(path));
    remove(path);
    _has_job = false;
    _job_checked = true;
  }

  // `force`: fetch every tile again (a refresh), not only the missing ones.
  // `trails`: 1 / 0 with or without the trails overlay, -1 as the map's toggle.
  bool start(const TileArea& a, const char* ssid, const char* pass, bool force = false, int trails = -1) {
    if (active()) return false;
    if (_lv_fetching) { lvport::fetchAbandon(); _lv_fetching = false; }   // the job takes the fetcher
    _lv_state = LV_IDLE;
    loadSource();
    _area = a;
    if (_area.zmax > _src_zmax) _area.zmax = _src_zmax;   // the server has nothing finer
    if (_area.zmin > _area.zmax) { fail("The server has no finer zoom: zoom out"); return false; }
    _trails = trails < 0 ? s_trails_on : trails > 0;
    _force = force;
    _layer = 0;
    _total = countTiles(_area);
    if (_trails && _area.zmin <= TRAILS_MAX_Z) {
      TileArea t = _area;
      if (t.zmax > TRAILS_MAX_Z) t.zmax = TRAILS_MAX_Z;
      _total += countTiles(t);
    }
    _done = _skipped = _failed = _consec_fail = 0;
    _tries = 0;
    _gap_ms = GAP_MS;
    _ok_run = 0;
    _fl_n = 0;
    _fl_i = -1;
    _refused = 0;
    _last_ok_ms = millis();
    _placeholder_hash = 0; _placeholder_hits = 0;
    _msg[0] = '\0';
    if (_total == 0 || _total > (_trails ? 2 : 1) * MAX_TILES) { fail("Area too large: use the PC tool"); return false; }
    writeAttribution();
    writeJob(_area);
    _z = _area.zmin;
    tileRange(_area, _z, _x0, _y0, _x1, _y1);
    _x = _x0; _y = _y0;
    _fetching = false;
    _state = CONNECTING;
    _connect_started = millis();
    lvport::netBegin(ssid, pass);
    return true;
  }

  void cancel() {
    if (!active()) return;
    if (_fetching) { lvport::fetchAbandon(); _fetching = false; }
    finish(CANCELLED, "Cancelled");
  }

  // ── Live tiles ──
  // Enabled with the credentials; nothing connects until a tile is requested.
  void liveBegin(const char* ssid, const char* pass) {
    snprintf(_lv_ssid, sizeof(_lv_ssid), "%s", ssid);
    snprintf(_lv_pass, sizeof(_lv_pass), "%s", pass);
    if (!_live) { loadSource(); writeAttribution(); }   // also creates <root> for the provider
    _live = true;
  }
  void liveEnd() {
    if (!_live) return;
    _live = false;
    if (_lv_fetching) { lvport::fetchAbandon(); _lv_fetching = false; }
    _lv_n = 0;
    if (_lv_state != LV_IDLE && !active()) lvport::netEnd();
    _lv_state = LV_IDLE;
  }
  bool liveOn() const { return _live; }
  bool liveConnecting() const { return _live && _lv_state == LV_CONNECTING; }
  int  liveQueued() const { return _live ? _lv_n + (_lv_fetching ? 1 : 0) : 0; }
  // A tile the map is missing (duplicates, recent failures and a full queue ignored).
  // `trails`: the tile's trails overlay (its base is on the card already).
  void liveRequest(int z, int x, int y, bool trails = false) {
    if (!_live || active() || z > (trails ? TRAILS_MAX_Z : _src_zmax)) return;   // past the server's zoom: the map magnifies
    uint8_t layer = trails ? 1 : 0;
    auto same = [&](const LiveTile& t) { return t.z == z && t.x == x && t.y == y && t.layer == layer; };
    if (_lv_fetching && same(_lv_cur)) return;
    for (int i = 0; i < _lv_n; i++) if (same(_lv_q[i])) return;
    for (const LiveTile& f : _lv_failed) if (same(f)) return;
    if (_lv_n >= LV_QUEUE) return;
    LiveTile& t = _lv_q[_lv_n++];
    t.z = (int16_t)z; t.x = x; t.y = y; t.layer = layer;
  }
  // Next tile written since the last call (the map forgets it was missing).
  bool liveTake(int& z, int& x, int& y) {
    if (!_lv_done_n) return false;
    const LiveTile& t = _lv_done[--_lv_done_n];
    z = t.z; x = t.x; y = t.y;
    return true;
  }

  void loop() {
    if (!active() && _live) { liveLoop(); return; }
    if (_state == CONNECTING) {
      int ns = lvport::netState();
      if (ns == lvport::NET_UP) { _state = RUNNING; return; }
      if (ns == lvport::NET_FAILED || millis() - _connect_started > 20000) finish(FAILED, "WiFi: can't connect");
      return;
    }
    if (_state != RUNNING) return;

    if (_fetching) {
      int r = lvport::fetchPoll();
      if (r == 0) {   // still in flight
        if (millis() - _last_start < 60000) return;
        lvport::fetchAbandon();   // hung past every timeout: give up on this tile
        _fetching = false;
        tileFailed("Request hung (60 s)");
        return;
      }
      _fetching = false;
      const char* reason = nullptr;
      if (r > 0) {
        size_t len = 0;
        const uint8_t* data = lvport::fetchData(len);
        if (!looksLikeImage(data, len)) reason = "server didn't send a PNG tile";   // an error page: passing too
        else {
          if (_layer == 1) {   // trails: the overlay, or an empty marker
            if (!writeTrails(_z, _x, _y, data, len)) { lvport::fetchRelease(); finish(FAILED, "Can't write to the SD card"); return; }
            tileOk();
          }
          else if (isNoTilePicture(data, len)) { _skipped++; if (_fl_i >= 0) _failed--; _consec_fail = 0; _tries = 0; _last_ok_ms = millis(); }   // "no tile here": nothing to keep
          else if (isPlaceholder(data, len)) { lvport::fetchRelease(); finish(FAILED, "Server sends a placeholder (blocked / key?)"); return; }
          else if (!writeTile(_z, _x, _y, data, len)) { lvport::fetchRelease(); finish(FAILED, "Can't write to the SD card"); return; }
          else tileOk();
          lvport::fetchRelease();
          advance();
          return;
        }
      }
      if (!reason) reason = r == -403 || r == -401 ? "server refused (403)" : lvport::fetchError()[0] ? lvport::fetchError() : "download failed";
      char why[48];
      snprintf(why, sizeof(why), "%s%s", _layer ? "Trails: " : "", reason);
      lvport::fetchRelease();
      if (r == -403 || r == -401) {   // refused: no point asking again
        if (++_refused >= 10) { finish(FAILED, "Server refused (403)"); return; }
        tileFailed(why);
        return;
      }
      _refused = 0;
      // Anything else is taken as passing: both tile servers answer every
      // tile of the world, but a busy one sends 503s (and OpenTopoMap the odd
      // 404 for a tile it hasn't rendered yet), and a keep-alive connection
      // drops. An HTTP error gets one more try; no answer at all (r == -1:
      // the connection, TLS, a timeout) up to three, 2, 8 and 30 s apart. A
      // server that says it's overloaded also gets the next requests further apart.
      if (r == -503 || r == -429 || r == -502 || r == -504) {
        _gap_ms = _gap_ms * 2 < 500 ? 500 : _gap_ms * 2 > GAP_MAX_MS ? GAP_MAX_MS : _gap_ms * 2;
        _ok_run = 0;
      }
      if (_tries < (r == -1 ? RETRIES : 1)) {
        static const uint8_t WAIT_S[RETRIES] = { 2, 8, 30 };
        _last_start = millis() + WAIT_S[_tries] * 1000u;
        _tries++;
        snprintf(_msg, sizeof(_msg), "%s - retrying", why);
        return;
      }
      tileFailed(why);
      return;
    }

    // Skip tiles already on the card (a few stat()s per pass), then start the next GET.
    for (int i = 0; i < 16 && _state == RUNNING; i++) {
      if (_z > _area.zmax) {
        // The tiles that failed get a second go once the server has had a
        // break; after that, the same download again fetches what's still
        // missing (tiles on the card are skipped).
        if (_fl_i < 0 && _fl_n > 0) {
          _fl_i = 0;
          _last_start = millis() + RETRY_PASS_WAIT_MS;
          snprintf(_msg, sizeof(_msg), "Retrying %u missing tiles", (unsigned)_fl_n);
          nextRetry();
          return;
        }
        char m[48];
        if (_failed) snprintf(m, sizeof(m), "Done, %lu missing - download again", (unsigned long)_failed);
        finish(DONE, _failed ? m : "Done");
        return;
      }
      char path[96];
      tilePath(path, sizeof(path), _z, _x, _y, _layer);
      struct stat st;
      if (!_force && stat(path, &st) == 0 && (st.st_size > 0 || _layer == 1)) { _skipped++; advance(); continue; }   // an empty trails file counts
      if ((int32_t)(millis() - _last_start) < (int32_t)_gap_ms) return;   // be gentle with the server (a retry waits longer)
      char url[200];
      buildUrl(url, sizeof(url), _z, _x, _y, _layer);
      int r = lvport::fetchStart(url);
      if (r > 0) { _fetching = true; _last_start = millis(); }
      else if (r < 0) finish(FAILED, "Out of memory (download task)");
      return;
    }
  }

private:
  const char* _root;
  State    _state = IDLE;
  TileArea _area = {0, 0, 0, 0, 0, 0};
  uint32_t _total = 0, _done = 0, _skipped = 0, _failed = 0;
  uint8_t  _consec_fail = 0;
  int      _z = 0, _x = 0, _y = 0, _x0 = 0, _y0 = 0, _x1 = 0, _y1 = 0;
  bool     _fetching = false;
  bool     _trails = false;    // this job fetches the trails overlay too
  bool     _force = false;     // refetches tiles already on the card
  uint8_t  _layer = 0;         // of the current tile: 0 the base map, 1 its trails
  // A tile that doesn't come is asked for again (RETRIES, spaced out), then
  // noted in _fl for a second pass at the end of the job.
  static const uint8_t RETRIES = 3;
  static const uint16_t GAP_MS = 150, GAP_MAX_MS = 2000;   // between requests; wider while the server is busy
  static const uint32_t RETRY_PASS_WAIT_MS = 20000;
  static const int FAIL_LIST = 512;
  struct FailedTile { int32_t x, y; int16_t z; uint8_t layer; };
  uint8_t  _tries = 0;         // retries of the current tile so far
  uint16_t _gap_ms = GAP_MS;
  uint16_t _ok_run = 0;        // tiles in a row since the server was last busy
  uint8_t  _refused = 0;       // 403s in a row
  uint32_t _last_ok_ms = 0;    // the last tile that came
  static const uint32_t GIVE_UP_MS = 10 * 60000;   // nothing at all for this long: stop (resumable)
  FailedTile* _fl = nullptr;   // (PSRAM, on the first failure)
  int      _fl_n = 0, _fl_i = -1;   // _fl_i >= 0: the second pass, the next to retry
  uint32_t _last_start = 0, _connect_started = 0;
  uint32_t _placeholder_hash = 0;
  uint8_t  _placeholder_hits = 0;
  char     _url_tpl[160] = "";
  char     _attr[96] = "";
  char     _host[48] = "";
  int      _src_zmax = 17;
  char     _msg[64] = "";
  TileArea _job = {0, 0, 0, 0, 0, 0};
  bool     _has_job = false, _job_checked = false;

  // Live tiles
  struct LiveTile { int16_t z = -1; uint8_t layer = 0; int32_t x = 0, y = 0; };   // layer: 0 base, 1 trails
  enum LiveState : uint8_t { LV_IDLE, LV_CONNECTING, LV_UP, LV_BACKOFF };
  static const int LV_QUEUE = 8, LV_FAILED = 16, LV_DONE = 8;
  static const uint32_t LV_IDLE_MS = 60000;
  bool      _live = false;
  LiveState _lv_state = LV_IDLE;
  char      _lv_ssid[33] = "", _lv_pass[65] = "";
  LiveTile  _lv_q[LV_QUEUE], _lv_failed[LV_FAILED], _lv_done[LV_DONE], _lv_cur;
  int       _lv_n = 0, _lv_failed_next = 0, _lv_done_n = 0;
  bool      _lv_fetching = false;
  uint8_t   _lv_consec_fail = 0;
  uint32_t  _lv_since = 0;   // connect start / back-off end / last activity while up

  void liveFailed(const LiveTile& t) {
    _lv_failed[_lv_failed_next++ % LV_FAILED] = t;
    if (++_lv_consec_fail >= 3) {   // the server or the network is down: pause
      _lv_consec_fail = 0;
      _lv_state = LV_BACKOFF;
      _lv_since = millis() + 30000;
      lvport::netEnd();
    }
  }

  void liveLoop() {
    switch (_lv_state) {
      case LV_IDLE:
      case LV_BACKOFF:
        if (!_lv_n) return;
        if (_lv_state == LV_BACKOFF && (int32_t)(millis() - _lv_since) < 0) return;
        if (!_lv_ssid[0]) return;
        lvport::netBegin(_lv_ssid, _lv_pass);
        _lv_state = LV_CONNECTING;
        _lv_since = millis();
        return;
      case LV_CONNECTING: {
        int ns = lvport::netState();
        if (ns == lvport::NET_UP) { _lv_state = LV_UP; _lv_since = millis(); return; }
        if (ns == lvport::NET_FAILED || millis() - _lv_since > 20000) {
          lvport::netEnd();
          _lv_state = LV_BACKOFF;
          _lv_since = millis() + 60000;
          _lv_n = 0;   // the map asks again for what it still misses
        }
        return;
      }
      case LV_UP:
        break;
    }
    if (lvport::netState() != lvport::NET_UP && !_lv_fetching) { _lv_state = LV_IDLE; return; }   // dropped: reconnect
    if (_lv_fetching) {
      int r = lvport::fetchPoll();
      if (r == 0) {
        if (millis() - _last_start < 30000) return;
        lvport::fetchAbandon();
        _lv_fetching = false;
        liveFailed(_lv_cur);
        return;
      }
      _lv_fetching = false;
      size_t len = 0;
      const uint8_t* data = r > 0 ? lvport::fetchData(len) : nullptr;
      char lpath[64];
      LiveCache::tilePath(lpath, sizeof(lpath), _lv_cur.z, _lv_cur.x, _lv_cur.y);
      if (_lv_cur.layer == 1) {   // trails over a tile already shown
        if (r > 0 && looksLikeImage(data, len) && writeTrails(_lv_cur.z, _lv_cur.x, _lv_cur.y, data, len)) {
          _lv_consec_fail = 0;
          if (_lv_done_n < LV_DONE) _lv_done[_lv_done_n++] = _lv_cur;
        } else {
          liveFailed(_lv_cur);
        }
      } else if (r > 0 && looksLikeImage(data, len) && !isNoTilePicture(data, len) && writeFile(lpath, data, len)) {
        s_live_cache.add(_lv_cur.z, _lv_cur.x, _lv_cur.y, (uint32_t)len);   // kept apart, within the Storage limit
        _lv_consec_fail = 0;
        if (_lv_done_n < LV_DONE) _lv_done[_lv_done_n++] = _lv_cur;
      } else {
        liveFailed(_lv_cur);
      }
      lvport::fetchRelease();
      _lv_since = millis();
      return;
    }
    // Nothing missing for a minute: the WiFi goes (the next miss reconnects).
    if (!_lv_n && millis() - _lv_since > LV_IDLE_MS) { lvport::netEnd(); _lv_state = LV_IDLE; return; }
    if (!_lv_n || millis() - _last_start < 120) return;   // be gentle with the server
    _lv_cur = _lv_q[0];
    for (int i = 1; i < _lv_n; i++) _lv_q[i - 1] = _lv_q[i];
    _lv_n--;
    char url[200];
    buildUrl(url, sizeof(url), _lv_cur.z, _lv_cur.x, _lv_cur.y, _lv_cur.layer);
    if (lvport::fetchStart(url) > 0) { _lv_fetching = true; _last_start = millis(); }
    else _lv_q[_lv_n++] = _lv_cur;   // fetcher busy: back in the queue
  }

  void fail(const char* m) { snprintf(_msg, sizeof(_msg), "%s", m); _state = FAILED; }

  void finish(State s, const char* m) {
    snprintf(_msg, sizeof(_msg), "%s", m);
    _state = s;
    lvport::netEnd();
    if (s == DONE) discardJob();   // anything else stays resumable
  }

  void jobPath(char* out, size_t n) const { snprintf(out, n, "%s/.job", _root); }

  void writeJob(const TileArea& a) {
    char path[64];
    jobPath(path, sizeof(path));
    makeParents(path);
    if (FILE* f = fopen(path, "w")) {
      fprintf(f, "%.6f %.6f %.6f %.6f %d %d\n", a.lon0, a.lat0, a.lon1, a.lat1, a.zmin, a.zmax);
      fclose(f);
    }
    _job = a;
    _has_job = true;
    _job_checked = true;
  }

  bool readJob(TileArea& a) const {
    char path[64];
    jobPath(path, sizeof(path));
    FILE* f = fopen(path, "r");
    if (!f) return false;
    TileArea t;
    int n = fscanf(f, "%lf %lf %lf %lf %d %d", &t.lon0, &t.lat0, &t.lon1, &t.lat1, &t.zmin, &t.zmax);
    fclose(f);
    if (n != 6 || t.zmin < 0 || t.zmax > 20 || t.zmin > t.zmax) return false;
    a = t;
    return true;
  }

  void tileOk() {
    _done++;
    if (_fl_i >= 0) _failed--;   // a second-pass tile made it after all
    _consec_fail = 0;
    _tries = 0;
    _last_ok_ms = millis();
    if (_gap_ms > GAP_MS && ++_ok_run >= 20) { _ok_run = 0; _gap_ms = _gap_ms / 2 < GAP_MS ? GAP_MS : _gap_ms / 2; }
  }

  // Given up on this tile (for now): noted for the second pass, and the job
  // moves on. Tile after tile failing is a busy server or a lost network: the
  // next one waits 5 s, 10 s ... up to a minute, and the job stops (to be
  // resumed) only after GIVE_UP_MS without a single tile.
  void tileFailed(const char* m) {
    snprintf(_msg, sizeof(_msg), "%s", m);
    _tries = 0;
    if (_fl_i < 0) {
      _failed++;
      if (!_fl) _fl = psramBuf<FailedTile>(FAIL_LIST);
      if (_fl && _fl_n < FAIL_LIST) _fl[_fl_n++] = { _x, _y, (int16_t)_z, _layer };
    }
    if (millis() - _last_ok_ms > GIVE_UP_MS) { finish(FAILED, "Server not answering - resume later"); return; }
    if (_consec_fail < 255) _consec_fail++;
    if (_consec_fail >= 3) {
      uint32_t wait_s = 5u << (_consec_fail - 3 < 4 ? _consec_fail - 3 : 4);
      if (wait_s > 60) wait_s = 60;
      _last_start = millis() + wait_s * 1000u;
      size_t o = strlen(_msg);
      snprintf(_msg + o, sizeof(_msg) - o, " - pause %lus", (unsigned long)wait_s);
    }
    advance();
  }

  // Second pass: the next failed tile, or past the last zoom when none is left.
  void nextRetry() {
    if (_fl_i >= _fl_n) { _z = _area.zmax + 1; return; }
    const FailedTile& t = _fl[_fl_i++];
    _z = t.z; _x = t.x; _y = t.y; _layer = t.layer;
  }

  void advance() {
    _tries = 0;
    if (_fl_i >= 0) { nextRetry(); return; }
    if (_layer == 0 && _trails && _z <= TRAILS_MAX_Z) { _layer = 1; return; }   // this tile's trails next
    _layer = 0;
    if (++_y <= _y1) return;
    _y = _y0;
    if (++_x <= _x1) return;
    if (++_z > _area.zmax) return;   // loop() finishes on the next pass
    tileRange(_area, _z, _x0, _y0, _x1, _y1);
    _x = _x0; _y = _y0;
  }

  void tilePath(char* out, size_t n, int z, int x, int y, uint8_t layer = 0) const {
    snprintf(out, n, "%s/%d/%d/%d.png", layer ? TRAILS_ROOT : _root, z, x, y);
  }

  void buildUrl(char* out, size_t n, int z, int x, int y, uint8_t layer = 0) const {
    size_t o = 0;
    for (const char* p = layer ? TRAILS_URL : _url_tpl; *p && o + 12 < n; p++) {
      if (p[0] == '{' && p[1] && p[2] == '}') {
        int v = p[1] == 'z' ? z : p[1] == 'x' ? x : p[1] == 'y' ? y : -1;
        if (v >= 0) { o += snprintf(out + o, n - o, "%d", v); p += 2; continue; }
      }
      out[o++] = *p;
    }
    out[o] = '\0';
  }

  void loadSource() {
    snprintf(_url_tpl, sizeof(_url_tpl), "%s", DEFAULT_TILE_URL);
    snprintf(_attr, sizeof(_attr), "%s", DEFAULT_TILE_ATTR);
    _src_zmax = -1;
    char path[64];
    snprintf(path, sizeof(path), "%s/source.txt", _root);
    if (FILE* f = fopen(path, "r")) {
      char line[160];
      if (fgets(line, sizeof(line), f)) { trim(line); if (strstr(line, "{z}")) snprintf(_url_tpl, sizeof(_url_tpl), "%s", line); }
      if (fgets(line, sizeof(line), f)) { trim(line); if (line[0]) snprintf(_attr, sizeof(_attr), "%s", line); }
      if (fgets(line, sizeof(line), f)) { int z = atoi(line); if (z >= 1 && z <= 22) _src_zmax = z; }
      fclose(f);
    }
    if (_src_zmax < 0) _src_zmax = strstr(_url_tpl, "opentopomap.org") ? 17 : 19;
    const char* h = strstr(_url_tpl, "://");
    h = h ? h + 3 : _url_tpl;
    size_t n = strcspn(h, "/");
    if (n >= sizeof(_host)) n = sizeof(_host) - 1;
    memcpy(_host, h, n);
    _host[n] = '\0';
  }

  static void trim(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
  }

  void writeAttribution() {
    char path[64];
    snprintf(path, sizeof(path), "%s/attribution.txt", _root);
    makeParents(path);
    if (FILE* f = fopen(path, "w")) { fprintf(f, "%s\n", _attr); fclose(f); }
  }

  // PNG only for now: RasterTileProvider has no JPEG decoder yet.
  static bool looksLikeImage(const uint8_t* d, size_t n) {
    return d && n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G';
  }

  // A blocked / key-required server answers every tile with the same picture:
  // four identical payloads among the first downloads stop the job. Only on a
  // fresh area (nothing on the card yet -- a resumed one proves the server
  // works) and only for pictures with some content: plain sea / empty land
  // tiles are genuinely identical, and compress to a few hundred bytes.
  bool isPlaceholder(const uint8_t* d, size_t n) {
    if (_done >= 8 || _skipped > 0 || n < 1500) return false;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
    if (h == _placeholder_hash) return ++_placeholder_hits >= 3;
    _placeholder_hash = h;
    _placeholder_hits = 0;
    return false;
  }

  // A trails tile: as fetched when something is drawn on it, else 0 bytes.
  bool writeTrails(int z, int x, int y, const uint8_t* d, size_t n) {
    char path[96];
    tilePath(path, sizeof(path), z, x, y, 1);
    return pngHasInk(d, n) ? writeFile(path, d, n) : writeFile(path, d, 0);
  }

  bool writeTile(int z, int x, int y, const uint8_t* d, size_t n) {
    char path[96];
    tilePath(path, sizeof(path), z, x, y);
    return writeFile(path, d, n);
  }
  static bool writeFile(const char* path, const uint8_t* d, size_t n) {
    char tmp[100];
    makeParents(path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    remove(path);
    return rename(tmp, path) == 0;
  }
};

}  // namespace mapview
