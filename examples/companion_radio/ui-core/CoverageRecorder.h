#pragma once
// MatterMap coverage recording (docs/solo/coverage-map.md): voluntary, off
// unless the user turns it on in Settings > Coverage map. While on, the device
// only listens -- it sends nothing for this -- and writes a small CSV file:
// one row per packet its own radio hears, stamped with the GPS fix, and one
// "nothing heard" row every 30 s without a packet. A finished file (recording
// turned off, or 1.5 MB) gets #RPT lines for the repeaters it heard and #END;
// the frontend uploads it (ui-lvgl/CoverageUpload.h) and moves it to sent/.
//
// Format and rules: MatterMap for developers, https://mattermeshos.com/coverage/developers/
// (MatterMesh OS, closed beta). Header "#MMCOV1,<board>,<fw>,<device id>", rows
// "t,lat,lon,acc,age,src,kind,snr,rssi,hop,hb,len,rt".
//
// Nothing is written: without a GPS fix (or one older than FIX_MAX_AGE_S),
// with the clock not set, with an accuracy over 100 m, inside the home zone
// (NodePrefs::cov_home_*), or while the storage isn't there. Plain stdio under
// a directory the frontend names (the L2's SD card), so the sim runs it too.

#include <Arduino.h>
#include <Packet.h>
#include "../NodePrefs.h"

#if defined(ESP32) || defined(SIM_PLATFORM)
#define COVERAGE_RECORDER 1
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <SHA256.h>
#include <helpers/UTF8Helpers.h>
#include "../NodePrefs.h"
#include "../GeoUtils.h"
#include "LoopWdt.h"

class CoverageRecorder {
public:
  static const uint32_t FILE_MAX = 1536UL * 1024;   // a new file past 1.5 MB (the server takes 2 MB)
  static const uint32_t QUIET_S = 30;               // a "nothing heard" row after this long without a row
  static const uint32_t FIX_MAX_AGE_S = 10;         // speed unknown: a fix reused at most this long
  static const int      ACC_MAX_M = 100;            // rows less accurate than this aren't written
  static const uint32_t FLUSH_MS = 15000;           // rows reach the card at least this often
  static const int      NAME_LEN = 16;              // "20261113-101500"

  // The fix a row is stamped with: 1e6 degrees, accuracy in m (-1 unknown), age in s.
  struct Fix { int32_t lat, lon; int acc; uint32_t age; };

  // `dir`: where the files go ("/sdcard/meshcore/coverage"); `ready`: whether
  // the storage is there now (a card can go, or be lent to a computer).
  // `mac`: 6 bytes the device id is made from (hashed, never sent as it is).
  void begin(NodePrefs* prefs, const char* dir, bool (*ready)(), const char* board, const char* fw,
             const uint8_t mac[6]) {
    _prefs = prefs;
    _ready = ready;
    snprintf(_dir, sizeof(_dir), "%s", dir);
    snprintf(_board, sizeof(_board), "%s", board);
    snprintf(_fw, sizeof(_fw), "%s", fw);
    for (char* c = _fw; *c; c++) if (*c == ',' || *c == '\n' || *c == '"' || *c == '\\') *c = ' ';
    deviceId(mac, _dev);
    _on = true;
    _started = false;   // the files are looked at once the storage answers (tick)
  }
  bool available() const { return _on; }
  bool recording() const { return _file != nullptr; }
  const char* deviceIdHex() const { return _dev; }
  const char* board() const { return _board; }
  const char* fw() const { return _fw; }
  const char* dir() const { return _dir; }
  const char* lastRow() const { return _last; }   // the newest row written (tests)

  // What the settings page shows.
  struct Status { bool storage; bool fix_ok; bool home; uint32_t rows; uint32_t bytes; char name[NAME_LEN]; };
  Status status() const {
    Status s = {};
    s.storage = _on && _ready && _ready();
    s.fix_ok = _fix_ok;
    s.home = _in_home;
    s.rows = _rows;
    s.bytes = _bytes;
    snprintf(s.name, sizeof(s.name), "%s", _file ? _name : "");
    return s;
  }

  // Recording switched in Settings: a file finished when it goes off.
  void apply() {
    if (!_on || !_prefs) return;
    if (!_prefs->cov_on && _file) finish();
  }
  // "Upload now": the file being written is finished too, so it goes with
  // the others (recording, if on, goes on in a new one).
  void finishNow() { if (_on && _file) finish(); }

  // A packet the radio heard (MyMesh::logRx), with the fix now (`have`: one at all).
  void onPacket(const mesh::Packet* pkt, float snr, float rssi, bool have, const Fix& f, uint32_t t) {
    if (!wanted(have, f, t)) return;
    if (!_file && !open(t)) return;   // before the hop is kept: a new file starts with none
    char row[112];
    char hop[8] = "", hb[2] = "";
    const uint8_t hs = pkt->getPathHashSize(), hc = pkt->getPathHashCount();
    // The repeater that passed it on: the last hash of a flood's path. A direct
    // packet's path is the way still ahead, so it says nothing of that.
    if (pkt->isRouteFlood() && hc > 0 && hs >= 1 && hs <= 3) {
      const uint8_t* h = &pkt->path[(hc - 1) * hs];
      for (int i = 0; i < hs; i++) snprintf(hop + i * 2, 3, "%02x", h[i]);
      hb[0] = (char)('0' + hs);
      addHop(_hops, _nh, h, hs);   // for the #RPT lines when the file is finished
    }
    int n = rowStart(row, sizeof(row), t, f);
    snprintf(row + n, sizeof(row) - n, "R,P,%.1f,%d,%s,%s,%u,%c\n", snr, (int)(rssi < 0 ? rssi - 0.5f : rssi + 0.5f),
             hop, hb, (unsigned)hc, pkt->isRouteFlood() ? 'F' : 'D');
    write(row, t);
  }

  // Every loop pass: opens / resumes / finishes files, the "nothing heard"
  // rows, flushes. `have` and `f`: the fix now, as for onPacket().
  void tick(bool have, const Fix& f, uint32_t t) {
    if (!_on || !_prefs) return;
    const bool ready = _ready && _ready();
    if (!ready) {
      if (_file) { fclose(_file); _file = nullptr; }   // the card went: what's written stays, resumed when it's back
      _started = false;
      return;
    }
    if (!_started) { _started = true; startup(); }
    // fflush() alone hands the rows to FAT but leaves the file's size in its
    // directory entry as it was: a hang or a reset then found the file empty
    // (0 B -- the rows on the card, out of reach) and deleted it. fsync()
    // writes the entry too.
    if (_file && (int32_t)(millis() - _flush_ms) >= 0) { fflush(_file); fsync(fileno(_file)); _flush_ms = millis() + FLUSH_MS; }
    if (!_prefs->cov_on) return;
    if (!wanted(have, f, t)) return;
    if (_last_row && t < _last_row + QUIET_S) return;
    char row[80];
    int n = rowStart(row, sizeof(row), t, f);
    snprintf(row + n, sizeof(row) - n, "R,N,,,,,,\n");
    write(row, t);
  }

  // Finished files waiting to go (not the one being written), oldest first.
  int pending(char names[][NAME_LEN + 4], int max) const {
    if (!_on || !_ready || !_ready()) return 0;
    DIR* d = opendir(_dir);
    if (!d) return 0;
    int k = 0;
    struct dirent* e;
    while ((e = readdir(d)) != nullptr) {
      const char* nm = e->d_name;
      size_t l = strlen(nm);
      if (l != NAME_LEN - 1 + 4 || strcmp(nm + l - 4, ".csv") != 0) continue;
      if (_file && strncmp(nm, _name, NAME_LEN - 1) == 0) continue;
      // The `max` oldest (lowest names), whatever order the directory gives.
      const int slot = k < max ? k : (max > 0 && strcmp(nm, names[max - 1]) < 0 ? max - 1 : -1);
      if (names && slot >= 0) {
        snprintf(names[slot], NAME_LEN + 4, "%s", nm);
        for (int j = slot; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
          char tmp[NAME_LEN + 4];
          memcpy(tmp, names[j], sizeof(tmp)); memcpy(names[j], names[j - 1], sizeof(tmp)); memcpy(names[j - 1], tmp, sizeof(tmp));
        }
      }
      k++;
    }
    closedir(d);
    return k;
  }
  // The home zone set or moved: rows already on the card inside it go, from
  // the file being written too, and from every finished one whose upload
  // hasn't begun (begun(): a resumed upload needs the same bytes -- that one
  // is scrubbed no more). A file left with no rows is deleted. The rows taken
  // out, -1 when the storage isn't there.
  int scrubHome() {
    if (!_on || !_prefs || !_ready || !_ready()) return -1;
    if (!homeSet()) return 0;
    const bool active = _file != nullptr;
    char name[NAME_LEN];
    snprintf(name, sizeof(name), "%s", _name);
    if (active) { fclose(_file); _file = nullptr; }
    int gone = 0;
    DIR* d = opendir(_dir);
    if (d) {
      char names[24][NAME_LEN + 4];
      int n = 0;
      struct dirent* e;
      while ((e = readdir(d)) != nullptr && n < 24) {
        size_t l = strlen(e->d_name);
        if (l == NAME_LEN - 1 + 4 && strcmp(e->d_name + l - 4, ".csv") == 0) snprintf(names[n++], NAME_LEN + 4, "%s", e->d_name);
      }
      closedir(d);
      for (int i = 0; i < n; i++) {
        const bool cur = active && strncmp(names[i], name, NAME_LEN - 1) == 0;
        if (!cur && begun(names[i])) continue;
        const int r = scrubFile(names[i], !cur);
        if (r > 0) gone += r;
      }
    }
    if (active) {   // on with the same file, as after a restart
      char path[96];
      snprintf(path, sizeof(path), "%s/%s.csv", _dir, name);
      _file = fopen(path, "a");
      if (_file) {
        snprintf(_name, sizeof(_name), "%s", name);
        _nh = 0;
        _rows = scanFile(path, _hops, _nh, &_bytes);
        _flush_ms = millis() + FLUSH_MS;
      }
    }
    return gone;
  }

  // One finished file, just before its upload begins (the upload task): rows
  // in the home zone out, then marked begun. -1: not read. The upload goes
  // on to send it only when it's still there (a file of home rows only isn't),
  // and not at all on -1: its home rows may still be in it (the card full?),
  // so it stays waiting, unmarked, to be scrubbed again next time.
  int scrubForUpload(const char* name) {
    if (begun(name)) return 0;
    const int r = homeSet() ? scrubFile(name, true) : 0;
    if (r < 0) return -1;
    char mark[96];
    snprintf(mark, sizeof(mark), "%s/%.*s.up", _dir, NAME_LEN - 1, name);
    if (FILE* m = fopen(mark, "w")) fclose(m);
    return r;
  }

  // Everything recorded on the card goes: the file being written (recording
  // goes on in a new one), those waiting, and the sent/ and failed/ copies.
  // What the map already has stays there. False when the storage isn't there.
  bool clearAll() {
    if (!_on || !_ready || !_ready()) return false;
    if (_file) { fclose(_file); _file = nullptr; }
    _rows = _bytes = 0;
    _nh = 0;
    _last_row = 0;
    _last[0] = '\0';
    for (const char* sub : { "", "/sent", "/failed" }) {
      char dir[80];
      snprintf(dir, sizeof(dir), "%s%s", _dir, sub);
      DIR* d = opendir(dir);
      if (!d) continue;
      struct dirent* e;
      char names[16][NAME_LEN + 8];
      bool more = true;
      for (int round = 0; more && round < 64; round++) {   // a directory isn't changed while it's read: a batch at a time
        int n = 0;
        rewinddir(d);
        while ((e = readdir(d)) != nullptr && n < 16) {
          size_t l = strlen(e->d_name);
          if (l == NAME_LEN - 1 + 4 && (strcmp(e->d_name + l - 4, ".csv") == 0 || strcmp(e->d_name + l - 4, ".tmp") == 0))
            snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
          else if (l == NAME_LEN - 1 + 3 && strcmp(e->d_name + l - 3, ".up") == 0)
            snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
        }
        more = n == 16;
        for (int i = 0; i < n; i++) {
          char p[112];
          snprintf(p, sizeof(p), "%s/%s", dir, names[i]);
          remove(p);
        }
        if (n == 0) break;
      }
      closedir(d);
    }
    return true;
  }

  // Uploaded (or refused by the server): out of the way, kept under sent/ or failed/.
  void moveDone(const char* name, bool sent) {
    char from[96], to[112];
    snprintf(from, sizeof(from), "%s/%.*s.up", _dir, NAME_LEN - 1, name);
    remove(from);   // the begun mark
    snprintf(from, sizeof(from), "%s/%s", _dir, name);
    snprintf(to, sizeof(to), "%s/%s", _dir, sent ? "sent" : "failed");
    mkdir(to, 0777);
    snprintf(to, sizeof(to), "%s/%s/%s", _dir, sent ? "sent" : "failed", name);
    remove(to);
    rename(from, to);
  }

private:
  NodePrefs* _prefs = nullptr;
  bool (*_ready)() = nullptr;
  bool _on = false, _started = false;
  char _dir[64] = "", _board[25] = "", _fw[32] = "", _dev[13] = "";
  FILE* _file = nullptr;
  char _name[NAME_LEN] = "";
  uint32_t _rows = 0, _bytes = 0, _last_row = 0;
  uint32_t _flush_ms = 0;
  bool _fix_ok = false, _in_home = false;
  char _last[112] = "";

  // The flood hops heard in the file being written, kept as it is written so
  // finishing it doesn't read the file back (up to 1.5 MB) on the main loop.
  struct Hop { uint8_t h[3]; uint8_t n; };
  static const int MAX_HOPS = 64;
  Hop _hops[MAX_HOPS];
  int _nh = 0;
  static void addHop(Hop* hops, int& nh, const uint8_t* h, uint8_t n) {
    for (int i = 0; i < nh; i++) if (hops[i].n == n && memcmp(hops[i].h, h, n) == 0) return;
    if (nh < MAX_HOPS) { hops[nh].n = n; memcpy(hops[nh].h, h, n); nh++; }
  }

  // First 12 hex of SHA-256("mmcov:" + MAC in upper-case hex), as MatterMesh OS does.
  static void deviceId(const uint8_t mac[6], char out[13]) {
    char in[6 + 12 + 1];
    snprintf(in, sizeof(in), "mmcov:%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    SHA256 sha;
    sha.update(in, strlen(in));
    uint8_t h[32];
    sha.finalize(h, sizeof(h));
    for (int i = 0; i < 6; i++) snprintf(out + i * 2, 3, "%02x", h[i]);
  }

  // Whether a row may be written now; keeps what the page shows up to date.
  bool wanted(bool have, const Fix& f, uint32_t t) {
    _fix_ok = have && f.age <= FIX_MAX_AGE_S && f.acc <= ACC_MAX_M;
    const bool was_home = _in_home;
    _in_home = have && inHome(f);
    // Back home: the file so far is finished, so it can go (auto upload) --
    // recording goes on in a new one when you leave.
    if (_in_home && !was_home && _file) finish();
    if (!_on || !_prefs || !_prefs->cov_on || !_started) return false;
    if (!_fix_ok || _in_home || t < 1700000000UL) return false;
    return _ready && _ready();
  }
  bool homeSet() const {
    return _prefs && NodePrefs::covHomeRadiusM(_prefs->cov_home_r) && (_prefs->cov_home_lat || _prefs->cov_home_lon);
  }
  bool inHome(const Fix& f) const { return inHome(f.lat, f.lon); }
  bool inHome(int32_t lat, int32_t lon) const {
    if (!homeSet()) return false;
    const uint32_t r = NodePrefs::covHomeRadiusM(_prefs->cov_home_r);
    return geo::haversineKm(lat, lon, _prefs->cov_home_lat, _prefs->cov_home_lon) * 1000.0f < (float)r;
  }

  // An upload of this file has begun (its .up mark, made by scrubForUpload).
  bool begun(const char* name) const {
    char mark[96];
    snprintf(mark, sizeof(mark), "%s/%.*s.up", _dir, NAME_LEN - 1, name);
    struct stat st;
    return stat(mark, &st) == 0;
  }

  // `name` (in _dir) without its rows in the home zone, through a .tmp copy;
  // the # lines (header, #RPT, #END) as they were. A finished file left with
  // no rows is deleted (`finished`; the one being written stays, header only).
  // The rows taken out, -1 when it can't be read or written.
  int scrubFile(const char* name, bool finished) {
    char path[96], tmp[96];
    snprintf(path, sizeof(path), "%s/%s", _dir, name);
    snprintf(tmp, sizeof(tmp), "%s/%.*s.tmp", _dir, NAME_LEN - 1, name);
    FILE* in = fopen(path, "r");
    if (!in) return -1;
    FILE* out = fopen(tmp, "w");
    if (!out) { fclose(in); return -1; }
    char line[160];
    int gone = 0, kept = 0;
    bool ok = true;
    uint32_t n = 0;
    while (fgets(line, sizeof(line), in)) {
      if ((++n & 255) == 0) loopwdt::feed();   // scrubHome() goes through every file waiting
      if (line[0] != '#') {
        // t,lat,lon,...
        const char* la = strchr(line, ',');
        const char* lo = la ? strchr(la + 1, ',') : nullptr;
        if (la && lo) {
          const int32_t lat = (int32_t)lround(strtod(la + 1, nullptr) * 1e6), lon = (int32_t)lround(strtod(lo + 1, nullptr) * 1e6);
          if (inHome(lat, lon)) { gone++; continue; }
        }
        kept++;
      }
      const size_t l = strlen(line);
      if (fwrite(line, 1, l, out) != l) { ok = false; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = false;
    if (!ok) { remove(tmp); return -1; }
    if (gone == 0) { remove(tmp); return 0; }
    remove(path);
    if (finished && kept == 0) { remove(tmp); return gone; }
    if (rename(tmp, path) != 0) return -1;
    return gone;
  }

  static void fmtDeg(char* b, int n, int32_t v) {   // 1e6 degrees, 5 decimals, rounded
    int32_t r = (v >= 0 ? v + 5 : v - 5) / 10;      // 1e5
    snprintf(b, n, "%s%ld.%05ld", r < 0 ? "-" : "", (long)(labs(r) / 100000), (long)(labs(r) % 100000));
  }
  static int rowStart(char* row, int n, uint32_t t, const Fix& f) {
    char la[16], lo[16], acc[8] = "";
    fmtDeg(la, sizeof(la), f.lat);
    fmtDeg(lo, sizeof(lo), f.lon);
    if (f.acc >= 0) snprintf(acc, sizeof(acc), "%d", f.acc);
    return snprintf(row, n, "%lu,%s,%s,%s,%u,", (unsigned long)t, la, lo, acc, (unsigned)f.age);
  }

  void write(const char* row, uint32_t t) {
    if (!_file && !open(t)) return;
    size_t l = strlen(row);
    if (fwrite(row, 1, l, _file) != l) { fclose(_file); _file = nullptr; return; }   // the card: tried again next row
    _rows++;
    _bytes += l;
    snprintf(_last, sizeof(_last), "%.*s", (int)(l && row[l - 1] == '\n' ? l - 1 : l), row);
    _last_row = t;
    if (_bytes >= FILE_MAX) finish();
  }

  // A new file, named by its UTC start.
  bool open(uint32_t t) {
    mkdir(_dir, 0777);
    time_t tt = (time_t)t;
    struct tm* g = gmtime(&tt);
    snprintf(_name, sizeof(_name), "%04d%02d%02d-%02d%02d%02d", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday,
             g->tm_hour, g->tm_min, g->tm_sec);
    char path[96];
    snprintf(path, sizeof(path), "%s/%s.csv", _dir, _name);
    _file = fopen(path, "w");
    if (!_file) return false;
    char hdr[96];
    int l = snprintf(hdr, sizeof(hdr), "#MMCOV1,%s,%s,%s\n", _board, _fw, _dev);
    fwrite(hdr, 1, l, _file);
    _rows = 0;
    _bytes = l;
    _nh = 0;
    _last_row = 0;
    _flush_ms = millis() + FLUSH_MS;
    return true;
  }

  // At start (and when the card comes back): files without #END are finished,
  // except the newest while recording is on, which goes on (a restart, a
  // lost card: the same file, as the spec asks).
  void startup() {
    char unf[8][NAME_LEN + 4];
    int n = 0;
    DIR* d = opendir(_dir);
    if (!d) return;
    struct dirent* e;
    // A scrubFile() cut off by a restart: a .tmp whose .csv is gone holds the
    // rows kept (the .csv is removed just before the rename) and takes its
    // place; one next to its .csv was never finished and goes.
    while ((e = readdir(d)) != nullptr && n < 8) {
      size_t l = strlen(e->d_name);
      if (l == NAME_LEN - 1 + 4 && strcmp(e->d_name + l - 4, ".tmp") == 0) snprintf(unf[n++], NAME_LEN + 4, "%s", e->d_name);
    }
    for (int i = 0; i < n; i++) {
      char tmp[96], csv[96];
      struct stat st;
      snprintf(tmp, sizeof(tmp), "%s/%s", _dir, unf[i]);
      snprintf(csv, sizeof(csv), "%s/%.*s.csv", _dir, NAME_LEN - 1, unf[i]);
      if (stat(csv, &st) == 0) remove(tmp);
      else rename(tmp, csv);
    }
    if (n) rewinddir(d);
    n = 0;
    while ((e = readdir(d)) != nullptr && n < 8) {
      size_t l = strlen(e->d_name);
      if (l != NAME_LEN - 1 + 4 || strcmp(e->d_name + l - 4, ".csv") != 0) continue;
      char path[96];
      snprintf(path, sizeof(path), "%s/%s", _dir, e->d_name);
      if (!hasEnd(path)) snprintf(unf[n++], NAME_LEN + 4, "%s", e->d_name);
    }
    closedir(d);
    int newest = -1;
    for (int i = 0; i < n; i++) if (newest < 0 || strcmp(unf[i], unf[newest]) > 0) newest = i;
    for (int i = 0; i < n; i++) {
      char path[96];
      snprintf(path, sizeof(path), "%s/%s", _dir, unf[i]);
      if (i == newest && _prefs->cov_on && !_file) {
        _file = fopen(path, "a");
        if (_file) {
          snprintf(_name, sizeof(_name), "%.*s", NAME_LEN - 1, unf[i]);
          _nh = 0;
          _rows = scanFile(path, _hops, _nh, &_bytes);
          _flush_ms = millis() + FLUSH_MS;
          continue;
        }
      }
      finishFile(path);
    }
  }
  static bool hasEnd(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return true;   // can't read it: left alone
    char tail[8] = "";
    if (fseek(f, -5, SEEK_END) == 0) fread(tail, 1, 5, f);
    fclose(f);
    return strncmp(tail, "#END\n", 5) == 0;
  }
  // A file's rows (and its bytes, and the flood hops in it), read back from the card.
  static uint32_t scanFile(const char* path, Hop* hops, int& nh, uint32_t* bytes) {
    uint32_t rows = 0;
    if (bytes) *bytes = 0;
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[160];
    uint32_t n = 0;
    while (fgets(line, sizeof(line), f)) {
      if ((++n & 255) == 0) loopwdt::feed();   // 8 files of 1.5 MB at the start
      if (bytes) *bytes += strlen(line);
      if (line[0] == '#') continue;
      rows++;
      // t,lat,lon,acc,age,src,kind,snr,rssi,hop,hb,len,rt
      char* fld[13];
      int k = 0;
      for (char* p = line; k < 13; ) {
        fld[k++] = p;
        char* c = strchr(p, ',');
        if (!c) break;
        *c = '\0';
        p = c + 1;
      }
      if (k < 13 || fld[12][0] != 'F' || !fld[9][0]) continue;
      const int hb = atoi(fld[10]);   // the hop hash's bytes
      if (hb < 1 || hb > 3 || strlen(fld[9]) != (size_t)hb * 2) continue;
      uint8_t h[3];
      for (int i = 0; i < hb; i++) { char b[3] = { fld[9][i * 2], fld[9][i * 2 + 1], 0 }; h[i] = (uint8_t)strtoul(b, nullptr, 16); }
      addHop(hops, nh, h, (uint8_t)hb);
    }
    fclose(f);
    return rows;
  }

  void finish() {
    if (!_file) return;
    fclose(_file);
    _file = nullptr;
    char path[96];
    snprintf(path, sizeof(path), "%s/%s.csv", _dir, _name);
    endFile(path, _rows, _hops, _nh);
    _rows = _bytes = 0;
    _nh = 0;
    _last_row = 0;
  }

  // A file left unfinished (at start): read back, then finished.
  void finishFile(const char* path) {
    Hop hops[MAX_HOPS];
    int nh = 0;
    const uint32_t rows = scanFile(path, hops, nh, nullptr);
    endFile(path, rows, hops, nh);
  }

  // #RPT for every repeater contact with a position whose hash was a flood
  // hop in the file, then #END. Written once, never rebuilt: a resumed upload
  // needs the same bytes. A file with no rows is deleted instead.
  void endFile(const char* path, uint32_t rows, const Hop* hops, int nh) {
    if (rows == 0) { remove(path); return; }
    FILE* f = fopen(path, "a");
    if (!f) return;
    for (int i = 0; i < the_mesh.getNumContacts(); i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_REPEATER || (!c.gps_lat && !c.gps_lon)) continue;
      bool heard = false;
      for (int j = 0; j < nh && !heard; j++) heard = memcmp(c.id.pub_key, hops[j].h, hops[j].n) == 0;
      if (!heard) continue;
      char key[13], la[16], lo[16], name[33];
      for (int j = 0; j < 6; j++) snprintf(key + j * 2, 3, "%02x", c.id.pub_key[j]);
      fmtDeg(la, sizeof(la), c.gps_lat);
      fmtDeg(lo, sizeof(lo), c.gps_lon);
      int o = 0;
      for (const char* p = c.name; *p && o < 32; p++) if (*p != ',' && *p != '\n' && *p != '\r') name[o++] = *p;
      name[o] = '\0';
      name[mesh::validUtf8PrefixLength(name, o)] = '\0';   // cut on a whole character
      fprintf(f, "#RPT,%s,%s,%s,%s,%lu\n", key, la, lo, name, (unsigned long)c.last_advert_timestamp);
    }
    fputs("#END\n", f);
    fclose(f);
  }
};

#else
// No POSIX files here (nRF52's LittleFS): nothing to record to. available()
// stays false, so nothing calls the rest.
class CoverageRecorder {
public:
  static const int NAME_LEN = 16;
  struct Fix { int32_t lat, lon; int acc; uint32_t age; };
  struct Status { bool storage; bool fix_ok; bool home; uint32_t rows; uint32_t bytes; char name[NAME_LEN]; };
  bool available() const { return false; }
  const char* dir() const { return ""; }
  const char* lastRow() const { return ""; }
  void apply() { }
  void onPacket(const mesh::Packet*, float, float, bool, const Fix&, uint32_t) { }
  void tick(bool, const Fix&, uint32_t) { }
  int pending(char[][NAME_LEN + 4], int) const { return 0; }
  Status status() const { return Status{}; }
  int scrubHome() { return -1; }
  bool clearAll() { return false; }
  void finishNow() {}
};
#endif
