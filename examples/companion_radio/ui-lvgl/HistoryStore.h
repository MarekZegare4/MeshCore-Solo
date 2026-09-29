#pragma once
// Message history on the SD card (roadmap stage 3): one file per conversation
// under /sdcard/meshcore/history -- c_<key>.bin for a channel (the key hashes
// its name and secret, so a channel deleted and added again finds its history,
// and a new channel in a reused slot doesn't), d_<prefix>.bin for a contact or
// room (the 4-byte public key prefix the history ring keys DMs by).
//
// A file is a ring of fixed-size records -- the history entries themselves
// (ChHistEntry / DmHistEntry) -- holding the newest "kept per conversation"
// (Settings > Storage) of them. Record ids run on without gaps, so the record
// with id X sits at slot (head + X - first id) % capacity: a later change to
// an entry (a relay echo, a delivery) finds its record by the entry's arc_id.
// Every change is written straight away (a message is a few hundred bytes).
//
// The RAM ring (MessageHistory) stays what everything else reads -- unread
// counts, the Messages list, resends; this copy survives a reboot (restore()
// puts the newest entries back) and holds far more than the ring, which the
// conversation view pages through (UITask::refreshThread).
//
// Plain stdio on /sdcard: the same code runs in the sim (Emscripten's MEMFS).

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

namespace histstore {

static const char* const ROOT = "/sdcard/meshcore";
static const char* const DIR_PATH = "/sdcard/meshcore/history";
static const uint32_t MAGIC   = 0x5453484D;   // "MHST"
static const uint16_t VERSION = 1;

// Settings > Storage > Kept per conversation.
static const uint16_t KEEP[] = { 100, 250, 500, 1000, 2000 };
static const int KEEP_COUNT = sizeof(KEEP) / sizeof(KEEP[0]);
static const int KEEP_DEFAULT = 2;   // 500
static const char* const KEEP_OPTS = "100\n250\n500\n1000\n2000";

struct Hdr {
  uint32_t magic;
  uint16_t ver, rec_size;
  uint32_t cap, head, count, next_id;
};

// FNV-1a over the channel's name and secret.
static uint32_t chKey(const ChannelDetails& ch) {
  uint32_t h = 2166136261u;
  for (const char* p = ch.name; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  for (size_t i = 0; i < sizeof(ch.channel.secret); i++) { h ^= ch.channel.secret[i]; h *= 16777619u; }
  return h;
}

class SdArchive : public HistArchive {
public:
  // SD mounted and the folder there. Checked again at most every 30 s while
  // it isn't (a card put in later is picked up).
  bool ready() {
    if (_ok) return true;
    uint32_t now = millis();
    if (_tried && now - _last_try < 30000) return false;
    _tried = true;
    _last_try = now;
    if (!lvport::mountStorage()) return false;
    mkdir("/sdcard", 0777);   // the sim's MEMFS has it only once maps are loaded (the card: fails, harmless)
    mkdir(ROOT, 0777);
    mkdir(DIR_PATH, 0777);
    struct stat st;
    _ok = stat(DIR_PATH, &st) == 0 && S_ISDIR(st.st_mode);
    return _ok;
  }

  void     setKeep(uint32_t n) { _keep = n; }
  uint32_t keep() const        { return _keep; }

  bool chPath(int ch_idx, char* out, size_t n) {
    ChannelDetails ch;
    if (ch_idx < 0 || !the_mesh.getChannel(ch_idx, ch) || !ch.name[0]) return false;
    snprintf(out, n, "%s/c_%08lx.bin", DIR_PATH, (unsigned long)chKey(ch));
    return true;
  }
  static void dmPath(const uint8_t* prefix, char* out, size_t n) {
    snprintf(out, n, "%s/d_%02x%02x%02x%02x.bin", DIR_PATH, prefix[0], prefix[1], prefix[2], prefix[3]);
  }

  // ── HistArchive ──────────────────────────────────────────────────────────
  void chAppend(ChHistEntry& e) override {
    char p[64];
    if (ready() && chPath(e.ch_idx, p, sizeof(p))) append(p, e);
  }
  void chUpdate(const ChHistEntry& e) override {
    char p[64];
    if (ready() && chPath(e.ch_idx, p, sizeof(p))) update(p, e);
  }
  void dmAppend(DmHistEntry& e) override {
    char p[64];
    if (!ready()) return;
    dmPath(e.prefix, p, sizeof(p));
    append(p, e);
  }
  void dmUpdate(const DmHistEntry& e) override {
    char p[64];
    if (!ready()) return;
    dmPath(e.prefix, p, sizeof(p));
    update(p, e);
  }

  // ── Reading ──────────────────────────────────────────────────────────────
  // How many records `path` holds (0 when missing or unreadable).
  template <typename E>
  int total(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    Hdr h;
    int n = readHdr<E>(f, h) ? (int)h.count : 0;
    fclose(f);
    return n;
  }

  // A window of the conversation, oldest first: `n` records ending `skip`
  // before the newest. Returns how many were read; *total = records in the
  // file (0 when missing or unreadable).
  template <typename E>
  int window(const char* path, int skip, int n, E* out, int* total) {
    *total = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    Hdr h;
    int got = 0;
    if (readHdr<E>(f, h)) {
      *total = (int)h.count;
      if (skip < (int)h.count) {
        int avail = (int)h.count - skip;
        int m = n < avail ? n : avail;
        got = (int)readRun(f, h, h.count - skip - m, (uint32_t)m, out);
      }
    }
    fclose(f);
    return got;
  }

  // ── After a reboot ───────────────────────────────────────────────────────
  // Puts the newest entries back in the RAM ring, as many as it holds, in
  // time order across all conversations. Channels that no longer exist are
  // skipped; a channel's entries get its current slot.
  void restore(MessageHistory& hist) {
    if (!ready()) return;
    uint32_t keys[MAX_GROUP_CHANNELS];
    bool have[MAX_GROUP_CHANNELS];
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      have[i] = the_mesh.getChannel(i, ch) && ch.name[0];
      keys[i] = have[i] ? chKey(ch) : 0;
    }
    restoreKind<ChHistEntry>(hist, 'c', MessageHistory::CH_HIST_MAX, keys, have);
    restoreKind<DmHistEntry>(hist, 'd', MessageHistory::DM_HIST_MAX, keys, have);
  }

  // ── Settings > Storage ───────────────────────────────────────────────────
  // Every file to the current "kept per conversation" (the newest stay).
  void applyKeep() {
    if (!ready()) return;
    forEachFile([this](const char* path, char kind) {
      if (kind == 'c') resize<ChHistEntry>(path, _keep);
      else resize<DmHistEntry>(path, _keep);
    });
  }
  void clearAll() {
    if (!ready()) return;
    forEachFile([](const char* path, char) { remove(path); });
  }

private:
  bool     _ok = false, _tried = false;
  uint32_t _last_try = 0;
  uint32_t _keep = KEEP[KEEP_DEFAULT];

  template <typename E>
  bool readHdr(FILE* f, Hdr& h) {
    if (fseek(f, 0, SEEK_SET) != 0 || fread(&h, sizeof(h), 1, f) != 1) return false;
    return h.magic == MAGIC && h.ver == VERSION && h.rec_size == sizeof(E) && h.cap > 0 && h.count <= h.cap;
  }
  // `m` records from the k0-th oldest into `out`: one read, two where the
  // ring wraps. Returns how many were read.
  template <typename E>
  static uint32_t readRun(FILE* f, const Hdr& h, uint32_t k0, uint32_t m, E* out) {
    uint32_t got = 0;
    while (got < m) {
      uint32_t slot = (h.head + k0 + got) % h.cap;
      uint32_t run = m - got < h.cap - slot ? m - got : h.cap - slot;
      if (fseek(f, sizeof(Hdr) + (long)slot * sizeof(E), SEEK_SET) != 0) break;
      size_t r = fread(&out[got], sizeof(E), run, f);
      got += (uint32_t)r;
      if (r != run) break;
    }
    return got;
  }
  static bool writeHdr(FILE* f, const Hdr& h) {
    return fseek(f, 0, SEEK_SET) == 0 && fwrite(&h, sizeof(h), 1, f) == 1;
  }

  template <typename E>
  void append(const char* path, E& e) {
    FILE* f = fopen(path, "r+b");
    Hdr h;
    if (f && readHdr<E>(f, h) && h.cap != _keep) {   // the setting changed since this file was written
      fclose(f);
      resize<E>(path, _keep);
      f = fopen(path, "r+b");
    }
    if (!f || !readHdr<E>(f, h)) {   // new (or unreadable: start it again)
      if (f) fclose(f);
      f = fopen(path, "w+b");
      if (!f) return;
      h = { MAGIC, VERSION, (uint16_t)sizeof(E), _keep, 0, 0, 1 };
    }
    uint32_t slot;
    if (h.count < h.cap) { slot = (h.head + h.count) % h.cap; h.count++; }
    else { slot = h.head; h.head = (h.head + 1) % h.cap; }
    e.arc_id = h.next_id++;
    if (fseek(f, sizeof(Hdr) + (long)slot * sizeof(E), SEEK_SET) == 0) fwrite(&e, sizeof(E), 1, f);
    writeHdr(f, h);
    fclose(f);
  }

  template <typename E>
  void update(const char* path, const E& e) {
    FILE* f = fopen(path, "r+b");
    if (!f) return;
    Hdr h;
    if (readHdr<E>(f, h)) {
      uint32_t first = h.next_id - h.count;
      if (e.arc_id >= first && e.arc_id < h.next_id) {
        uint32_t slot = (h.head + (e.arc_id - first)) % h.cap;
        if (fseek(f, sizeof(Hdr) + (long)slot * sizeof(E), SEEK_SET) == 0) fwrite(&e, sizeof(E), 1, f);
      }
    }
    fclose(f);
  }

  // Rewrites `path` with room for `cap` records, keeping the newest.
  template <typename E>
  void resize(const char* path, uint32_t cap) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    Hdr h;
    if (!readHdr<E>(f, h) || h.cap == cap) { fclose(f); return; }
    uint32_t m = h.count < cap ? h.count : cap;
    E* buf = m ? (E*)malloc((size_t)m * sizeof(E)) : nullptr;   // PSRAM on the L2 (large block)
    if (m && !buf) { fclose(f); return; }
    uint32_t got = m ? readRun(f, h, h.count - m, m, buf) : 0;
    fclose(f);
    char tmp[72];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* o = fopen(tmp, "wb");
    if (o) {
      Hdr nh = { MAGIC, VERSION, (uint16_t)sizeof(E), cap, 0, got, h.next_id };   // the kept records keep their ids
      bool ok = fwrite(&nh, sizeof(nh), 1, o) == 1 && (got == 0 || fwrite(buf, sizeof(E), got, o) == got);
      fclose(o);
      if (ok) { remove(path); rename(tmp, path); }   // FAT's rename doesn't replace
      else remove(tmp);
    }
    free(buf);
  }

  template <typename F>
  void forEachFile(F fn) {
    DIR* d = opendir(DIR_PATH);
    if (!d) return;
    // Collected first: removing / renaming while readdir() walks the folder
    // isn't safe on FAT.
    static const int MAX_FILES = 512;
    char (*names)[20] = (char(*)[20])malloc(MAX_FILES * 20);
    int n = 0;
    if (names) {
      while (struct dirent* de = readdir(d)) {
        const char* nm = de->d_name;
        if ((nm[0] == 'c' || nm[0] == 'd') && nm[1] == '_' && strlen(nm) == 14 && !strcmp(nm + 10, ".bin") && n < MAX_FILES)
          snprintf(names[n++], 20, "%s", nm);
      }
    }
    closedir(d);
    for (int i = 0; i < n; i++) {
      char p[64];
      snprintf(p, sizeof(p), "%s/%s", DIR_PATH, names[i]);
      fn(p, names[i][0]);
    }
    free(names);
  }

  struct Pick { uint32_t ts; uint32_t id; uint16_t file; };

  template <typename E>
  void restoreKind(MessageHistory& hist, char kind, int ring, const uint32_t* keys, const bool* have) {
    static const int MAX_FILES = 256;
    char (*paths)[64] = (char(*)[64])malloc(MAX_FILES * 64);
    int8_t* slot_of = (int8_t*)malloc(MAX_FILES);
    int files = 0;
    if (!paths || !slot_of) { free(paths); free(slot_of); return; }
    forEachFile([&](const char* path, char k) {
      if (k != kind || files >= MAX_FILES) return;
      int8_t ch = -1;
      if (kind == 'c') {
        uint32_t key = strtoul(strrchr(path, '_') + 1, nullptr, 16);
        for (int i = 0; i < MAX_GROUP_CHANNELS; i++) if (have[i] && keys[i] == key) { ch = (int8_t)i; break; }
        if (ch < 0) return;   // that channel is gone
      }
      snprintf(paths[files], 64, "%s", path);
      slot_of[files++] = ch;
    });

    // The newest `ring` of each file by timestamp, then the newest `ring`
    // over all of them.
    Pick* picks = (Pick*)malloc((size_t)files * ring * sizeof(Pick) + sizeof(Pick));
    int np = 0;
    if (picks) {
      for (int fi = 0; fi < files; fi++) {
        FILE* f = fopen(paths[fi], "rb");
        if (!f) continue;
        Hdr h;
        if (readHdr<E>(f, h)) {
          uint32_t m = h.count < (uint32_t)ring ? h.count : (uint32_t)ring;
          uint32_t k0 = h.count - m, first = h.next_id - h.count;
          for (uint32_t i = 0; i < m; i++) {
            uint32_t slot = (h.head + k0 + i) % h.cap;
            uint32_t ts;
            if (fseek(f, sizeof(Hdr) + (long)slot * sizeof(E) + offsetof(E, timestamp), SEEK_SET) != 0 ||
                fread(&ts, 4, 1, f) != 1) break;
            picks[np++] = { ts, first + k0 + i, (uint16_t)fi };
          }
        }
        fclose(f);
      }
      std::stable_sort(picks, picks + np, [](const Pick& a, const Pick& b) { return a.ts < b.ts; });
      int from = np > ring ? np - ring : 0, cnt = np - from;
      // The chosen records read file by file (each opened once), then put
      // back in time order.
      E* recs = cnt ? (E*)malloc((size_t)cnt * sizeof(E)) : nullptr;
      bool* ok = cnt ? (bool*)calloc(cnt, 1) : nullptr;
      int* order = cnt ? (int*)malloc((size_t)cnt * sizeof(int)) : nullptr;
      if (recs && ok && order) {
        for (int i = 0; i < cnt; i++) order[i] = from + i;
        std::sort(order, order + cnt, [&](int a, int b) { return picks[a].file != picks[b].file ? picks[a].file < picks[b].file : picks[a].id < picks[b].id; });
        for (int j = 0; j < cnt;) {
          int fi = picks[order[j]].file, j1 = j;
          while (j1 < cnt && picks[order[j1]].file == fi) j1++;
          FILE* f = fopen(paths[fi], "rb");
          Hdr h;
          if (f && readHdr<E>(f, h)) {
            uint32_t first = h.next_id - h.count;
            for (; j < j1; j++) {
              int k = order[j];
              ok[k - from] = picks[k].id >= first && readRun(f, h, picks[k].id - first, 1, &recs[k - from]) == 1;
            }
          }
          if (f) fclose(f);
          j = j1;
        }
        for (int i = 0; i < cnt; i++) if (ok[i]) put(hist, recs[i], slot_of[picks[from + i].file]);
      }
      free(recs); free(ok); free(order);
    }
    free(picks);
    free(paths);
    free(slot_of);
  }
  static void put(MessageHistory& h, ChHistEntry& e, int8_t ch) { e.ch_idx = (uint8_t)ch; h.restoreCh(e); }
  static void put(MessageHistory& h, DmHistEntry& e, int8_t) { h.restoreDm(e); }
};

}  // namespace histstore
