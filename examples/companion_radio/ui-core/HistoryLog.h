#pragma once
// Message history on the QSPI flash, for a board without an SD card (the L1s,
// HIST_FLASH): Settings > Messages > Keep messages, NodePrefs::msg_keep. Two
// append-only logs, channels and direct messages (a busy channel must not push
// the DMs out), each a run of SEGMENTS segments of which the oldest is dropped
// whole. The limit is a total: 2/3 of it for channels, 1/3 for DMs and rooms.
//
// Shaped by LittleFS 1.x, unlike the SD card's HistoryStore.h:
// - Changing a file in the middle rewrites everything after that point, so
//   records are only ever appended.
// - Every file commit erases one of its directory's two blocks, and those stay
//   put: nothing moves them to spread the wear. So a segment gets a directory
//   of its own, and dropping it lets a later one land on other blocks. A commit
//   here is one batch, so the two blocks see at most the segment's record count.
// - A commit happens only on close, so a power cut mid-batch (the L1's switch
//   gives no warning) leaves the log as it was before the batch.
//
// A batch is written SAVE_DELAY_MS after the first change not yet saved -- not
// after the last, or a busy channel would never get there -- and at shutdown().
// A record is written once, so a message waits for its final delivery state;
// until then the entries behind it wait too, to keep the log in order.
//
// A record: Rec, the path, the text, then its own total length as one byte, so
// the log can be walked from the end. restore() fills the RAM rings from there.
// A channel's messages carry channelKey(), so they follow the channel, not the
// slot; those of a channel since deleted aren't restored.

class HistoryLog : public HistArchive {
public:
  static const uint8_t  SEGMENTS = 8;
  static const uint32_t SAVE_DELAY_MS = 60000;
  static const uint32_t SETTLE_SECS = 60;   // an own channel post no repeater has echoed yet

  void begin(NodePrefs* prefs, MessageHistory* history) {
    _prefs = prefs;
    _history = history;
    DataStore* ds = the_mesh.getDataStore();
    _fs = ds ? ds->getSecondaryFS() : nullptr;
    _on = _fs && prefs && prefs->msg_keep;
    if (_on) {
      scan();
      restore();
    }
    history->setArchive(this);
  }

  // ── HistArchive ────────────────────────────────────────────────────────────
  // arc_id orders the entries of this run; a restored one keeps 0, already saved.
  void chAppend(ChHistEntry& e) override { e.arc_id = ++_seq; touch(); }
  void chUpdate(const ChHistEntry&) override { touch(); }
  void dmAppend(DmHistEntry& e) override { e.arc_id = ++_seq; touch(); }
  void dmUpdate(const DmHistEntry&) override { touch(); }

  void loop() {
    bool on = _fs && _prefs && _prefs->msg_keep;
    if (on != _on) {
      _on = on;
      if (on) touch(); else forget();
    }
    if (_dirty && (int32_t)(millis() - _due) >= 0) flush(false);
  }

  void onShutdown() { if (_dirty && _prefs->msg_keep) flush(true); }

private:
  static constexpr const char* ROOT = "/h";

  struct __attribute__((packed)) Rec {
    uint32_t key;         // channelKey(), or a DM's 4-byte public key prefix
    uint32_t timestamp;
    uint32_t msg_ts;      // DM: the sender's timestamp, for dropping a replayed copy
    uint8_t  flags;       // AckState, | OUTGOING
    uint8_t  path_len;    // packed, as mesh::Packet::path_len
    uint8_t  text_len;
  };
  static const uint8_t OUTGOING = 0x80, STATUS_MASK = 0x03;
  static const int REC_MAX = sizeof(Rec) + MAX_HIST_PATH_BYTES + MSG_TEXT_BUF + 1;

  // A log: segments first..last (0 = none yet), in_last records in the newest.
  struct Log {
    char     kind;
    uint32_t first, last;
    uint16_t in_last;
  };

  void touch() {
    if (!_on || _dirty) return;
    _dirty = true;
    _due = millis() + SAVE_DELAY_MS;
  }

  static void dirPath(const Log& l, uint32_t seg, char* out, size_t n) {
    snprintf(out, n, "%s/%c%lu", ROOT, l.kind, (unsigned long)seg);
  }
  static void filePath(const Log& l, uint32_t seg, char* out, size_t n) {
    snprintf(out, n, "%s/%c%lu/l", ROOT, l.kind, (unsigned long)seg);
  }
  uint16_t segRecords(const Log& l) const {
    uint32_t total = NodePrefs::msgKeepTotal(_prefs->msg_keep);
    return (uint16_t)((l.kind == 'c' ? total * 2 / 3 : total / 3) / SEGMENTS);
  }
  static int pathBytes(uint8_t path_len) {
    int n = ((path_len >> 6) + 1) * (path_len & 63);
    return n < MAX_HIST_PATH_BYTES ? n : MAX_HIST_PATH_BYTES;
  }
  static bool sane(const Rec& r, int len) {
    return r.text_len < MSG_TEXT_BUF && len == (int)sizeof(Rec) + pathBytes(r.path_len) + r.text_len + 1;
  }

  // ── Writing ────────────────────────────────────────────────────────────────
  void flush(bool all) {
    uint32_t t0 = millis();
    _dirty = false;
    bool more = flushCh(all);
    if (flushDm(all)) more = true;
    MESH_DEBUG_PRINTLN("HistoryLog: flush %lu ms", (unsigned long)(millis() - t0));
    if (more) touch();
  }

  bool flushCh(bool all) {
    File f(*_fs);
    bool more = false;
    uint32_t now = rtc_clock.getCurrentTime();
    for (int j = _history->chHistCount() - 1; j >= 0; j--) {
      const ChHistEntry& e = _history->chAtPos(_history->chHistPosNewest(j));
      if (e.arc_id <= _ch_done) continue;
      if (!all && e.relay_status == ACK_PENDING && now - e.timestamp < SETTLE_SECS) { more = true; break; }
      ChannelDetails ch;
      if (the_mesh.getChannel(e.ch_idx, ch) && ch.name[0] &&
          !put(_ch, f, channelKey(ch), e.timestamp, 0, e.relay_status, e.path_len, e.path, e.text)) break;
      _ch_done = e.arc_id;
    }
    if (f) f.close();
    return more;
  }

  bool flushDm(bool all) {
    File f(*_fs);
    bool more = false;
    for (int j = _history->dmHistCount() - 1; j >= 0; j--) {
      const DmHistEntry& e = _history->dmAtPos(_history->dmHistPosNewest(j));
      if (e.arc_id <= _dm_done) continue;
      if (!all && e.ack_status == ACK_PENDING) { more = true; break; }
      uint32_t key;
      memcpy(&key, e.prefix, 4);
      if (!put(_dm, f, key, e.timestamp, e.msg_ts, e.ack_status | (e.outgoing ? OUTGOING : 0),
               e.path_len, e.path, e.text)) break;
      _dm_done = e.arc_id;
    }
    if (f) f.close();
    return more;
  }

  bool put(Log& l, File& f, uint32_t key, uint32_t ts, uint32_t msg_ts, uint8_t flags, uint8_t path_len,
           const uint8_t* path, const char* text) {
    if (!f || !l.last || l.in_last >= segRecords(l)) {
      if (f) f.close();
      if (!l.last || l.in_last >= segRecords(l)) nextSegment(l);
      char p[24];
      filePath(l, l.last, p, sizeof(p));
      f = _fs->open(p, FILE_O_WRITE);   // at the end of the file
      if (!f) return false;
    }
    uint8_t buf[REC_MAX];
    Rec r = { key, ts, msg_ts, flags, path_len, (uint8_t)strnlen(text, MSG_TEXT_BUF - 1) };
    int pn = pathBytes(path_len), n = 0;
    memcpy(buf, &r, sizeof(r));                  n += sizeof(r);
    memcpy(buf + n, path, pn);                   n += pn;
    memcpy(buf + n, text, r.text_len);           n += r.text_len;
    buf[n] = (uint8_t)(n + 1);                   n++;
    if ((int)f.write(buf, n) != n) return false;
    l.in_last++;
    return true;
  }

  void nextSegment(Log& l) {
    char p[24];
    l.last++;
    if (!l.first) l.first = l.last;
    l.in_last = 0;
    _fs->mkdir(ROOT);
    dirPath(l, l.last, p, sizeof(p));
    _fs->mkdir(p);
    while (l.last - l.first >= SEGMENTS) {
      dirPath(l, l.first++, p, sizeof(p));
      _fs->rmdir_r(p);
    }
  }

  void forget() {
    _fs->rmdir_r(ROOT);
    _dirty = false;
    _ch = { 'c', 0, 0, 0 };
    _dm = { 'd', 0, 0, 0 };
    _ch_done = _dm_done = 0;   // on again: what the rings hold goes back in
  }

  // ── Reading ────────────────────────────────────────────────────────────────
  // Finds each log's segments in ROOT.
  void scan() {
    File dir = _fs->open(ROOT, FILE_O_READ);
    if (!dir) return;
    File e = dir.openNextFile(FILE_O_READ);
    while (e) {
      const char* name = e.name();
      Log* l = name[0] == 'c' ? &_ch : name[0] == 'd' ? &_dm : nullptr;
      uint32_t seg = l ? strtoul(name + 1, nullptr, 10) : 0;
      if (seg) {
        if (!l->first || seg < l->first) l->first = seg;
        if (seg > l->last) l->last = seg;
      }
      e.close();
      e = dir.openNextFile(FILE_O_READ);
    }
    dir.close();
    _ch.in_last = countLast(_ch);
    _dm.in_last = countLast(_dm);
  }

  uint16_t countLast(const Log& l) {
    if (!l.last) return 0;
    char p[24];
    filePath(l, l.last, p, sizeof(p));
    File f = _fs->open(p, FILE_O_READ);
    if (!f) return 0;
    uint16_t n = 0;
    uint8_t len;
    for (uint32_t pos = f.size(); pos > 0 && stepBack(f, pos, len); pos -= len) n++;
    f.close();
    return n;
  }

  // The record ending at pos: its length, false when that makes no sense.
  static bool stepBack(File& f, uint32_t pos, uint8_t& len) {
    return f.seek(pos - 1) && f.read(&len, 1) == 1 && len > sizeof(Rec) && len <= pos;
  }

  void restore() {
    uint32_t keys[MAX_GROUP_CHANNELS];
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      keys[i] = the_mesh.getChannel(i, ch) && ch.name[0] ? channelKey(ch) : 0;
    }
    restoreLog(_ch, MessageHistory::CH_HIST_MAX, keys);
    restoreLog(_dm, MessageHistory::DM_HIST_MAX, nullptr);
  }

  static int chSlot(const uint32_t* keys, uint32_t key) {
    for (int i = 0; key && i < MAX_GROUP_CHANNELS; i++) if (keys[i] == key) return i;
    return -1;
  }

  // The newest `want` usable records into the ring, oldest first: back from
  // the end to where they start, then forward from there.
  void restoreLog(const Log& l, int want, const uint32_t* keys) {
    if (!l.last) return;
    char p[24];
    uint32_t seg0 = l.last, off0 = 0;
    int got = 0;
    for (uint32_t seg = l.last; seg >= l.first && got < want; seg--) {
      filePath(l, seg, p, sizeof(p));
      File f = _fs->open(p, FILE_O_READ);
      if (!f) continue;
      uint32_t pos = f.size();
      uint8_t len;
      while (pos > 0 && got < want && stepBack(f, pos, len)) {
        pos -= len;
        Rec r;
        if (!f.seek(pos) || f.read(&r, sizeof(r)) != (int)sizeof(r) || !sane(r, len)) { pos += len; break; }
        if (!keys || chSlot(keys, r.key) >= 0) got++;
      }
      seg0 = seg;
      off0 = pos;
      f.close();
    }
    for (uint32_t seg = seg0; seg <= l.last; seg++) {
      filePath(l, seg, p, sizeof(p));
      File f = _fs->open(p, FILE_O_READ);
      if (!f) continue;
      uint32_t size = f.size();
      for (uint32_t pos = seg == seg0 ? off0 : 0; pos < size; ) {
        uint8_t buf[REC_MAX];
        Rec r;
        if (!f.seek(pos) || f.read(&r, sizeof(r)) != (int)sizeof(r)) break;
        int rest = pathBytes(r.path_len) + r.text_len + 1;
        if (r.text_len >= MSG_TEXT_BUF || f.read(buf, rest) != rest) break;
        if (buf[rest - 1] != (uint8_t)(sizeof(r) + rest)) break;
        restoreRec(r, buf, keys);
        pos += sizeof(r) + rest;
      }
      f.close();
    }
  }

  void restoreRec(const Rec& r, const uint8_t* buf, const uint32_t* keys) {
    int pn = pathBytes(r.path_len);
    if (keys) {
      int slot = chSlot(keys, r.key);
      if (slot < 0) return;
      ChHistEntry e;
      memset(&e, 0, sizeof(e));
      e.ch_idx = (uint8_t)slot;
      e.timestamp = r.timestamp;
      e.relay_status = r.flags & STATUS_MASK;
      e.path_len = r.path_len;
      memcpy(e.path, buf, pn);
      memcpy(e.text, buf + pn, r.text_len);
      _history->restoreCh(e);
    } else {
      DmHistEntry e;
      memset(&e, 0, sizeof(e));
      memcpy(e.prefix, &r.key, 4);
      e.outgoing = (r.flags & OUTGOING) ? 1 : 0;
      e.timestamp = r.timestamp;
      e.msg_ts = r.msg_ts;
      e.ack_status = r.flags & STATUS_MASK;
      e.path_len = r.path_len;
      memcpy(e.path, buf, pn);
      memcpy(e.text, buf + pn, r.text_len);
      _history->restoreDm(e);
    }
  }

  NodePrefs*      _prefs = nullptr;
  MessageHistory* _history = nullptr;
  FILESYSTEM*     _fs = nullptr;
  bool     _on = false, _dirty = false;
  uint32_t _due = 0;
  uint32_t _seq = 0, _ch_done = 0, _dm_done = 0;   // arc_id: the last given, the last saved
  Log      _ch = { 'c', 0, 0, 0 };
  Log      _dm = { 'd', 0, 0, 0 };
};
