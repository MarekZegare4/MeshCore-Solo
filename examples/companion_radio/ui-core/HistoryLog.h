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
// after the last, or a busy channel would never get there -- at shutdown(), and
// after SAVE_BURST new messages: a room's backlog can arrive faster than the
// delay, and what leaves the ring unsaved is gone.
// A record is written once, so a message waits for its final delivery state;
// until then the entries behind it wait too, to keep the log in order.
//
// A record: Rec, the path, the text, then its own total length as one byte, so
// the log can be walked from the end. restore() fills the RAM rings from there.
// A channel's messages carry channelKey(), so they follow the channel, not the
// slot; those of a channel since deleted aren't restored.
//
// Older messages, past what the rings hold, are read a page at a time
// (pageBegin*(), pageOlder(), pageNewer()): the messages screen measures its
// whole list every frame, so it gets a short one, MessageHistory::older, never
// the log. A page is found by walking the log, as a conversation's records lie
// among all the others; a place in it (Spot) stays good as the log grows.
//
// Settings > Messages > Delete after hides what's older at once; the flash is
// freed a whole segment at a time, once the newest record in it is that old.

class HistoryLog : public HistArchive {
public:
  static const uint8_t  SEGMENTS = 8;
  static const uint32_t SAVE_DELAY_MS = 60000;
  static const uint32_t SETTLE_SECS = 60;   // an own channel post no repeater has echoed yet
  static const uint8_t  SAVE_BURST = 12;
  static const uint32_t PRUNE_EVERY_MS = 3600000;
  static const int      PAGE = 12;

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
    _next_prune = millis() + SAVE_DELAY_MS;
    history->setArchive(this);
  }

  // ── HistArchive ────────────────────────────────────────────────────────────
  // arc_id orders the entries of this run; a restored one keeps 0, already saved.
  void chAppend(ChHistEntry& e) override { e.arc_id = ++_seq; appended(); }
  void chUpdate(const ChHistEntry&) override { touch(); }
  void dmAppend(DmHistEntry& e) override { e.arc_id = ++_seq; appended(); }
  void dmUpdate(const DmHistEntry&) override { touch(); }

  void loop() {
    bool on = _fs && _prefs && _prefs->msg_keep;
    if (on != _on) {
      _on = on;
      if (on) touch(); else forget();
    }
    if (_dirty && (int32_t)(millis() - _due) >= 0) flush(false);
    if (_on && (int32_t)(millis() - _next_prune) >= 0) {
      _next_prune = millis() + PRUNE_EVERY_MS;
      prune(_ch);
      prune(_dm);
    }
  }

  void onShutdown() { if (_dirty && _prefs->msg_keep) flush(true); }

  // Settings > Messages > Delete history: the log and the rings.
  void erase() {
    pageEnd();
    if (_fs) forget();
    _history->clearAll();
  }

  // ── Older messages ─────────────────────────────────────────────────────────
  // Start reading a conversation from just past what the ring holds of it.
  // False when the log has nothing more; true: a page is in MessageHistory::older.
  bool pageBeginCh(int slot) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(slot, ch) || !ch.name[0]) return false;
    int in_ring = 0;
    for (int j = 0; j < _history->chHistCount(); j++) {
      const ChHistEntry& e = _history->chAtPos(_history->chHistPosNewest(j));
      if (e.ch_idx == slot && e.arc_id <= _ch_done) in_ring++;
    }
    return pageBegin(&_ch, channelKey(ch), (uint8_t)slot, in_ring);
  }
  bool pageBeginDm(const uint8_t* prefix) {
    int in_ring = 0;
    for (int j = 0; j < _history->dmHistCount(); j++) {
      const DmHistEntry& e = _history->dmAtPos(_history->dmHistPosNewest(j));
      if (memcmp(e.prefix, prefix, 4) == 0 && e.arc_id <= _dm_done) in_ring++;
    }
    uint32_t key;
    memcpy(&key, prefix, 4);
    return pageBegin(&_dm, key, 0, in_ring);
  }
  int pageCount() const { return _pg_n; }
  void pageEnd() {
    free(_history->older);
    _history->older = nullptr;
    _pg = nullptr;
    _pg_n = 0;
  }

  // The next page back; false, and the page as it was, at the end of the log.
  bool pageOlder() {
    if (!_pg) return false;
    int got = 0;
    Spot start = _lo;
    Spot end = back(start, [&](File& f, uint32_t, uint32_t pos, const Rec& r) {
      uint8_t buf[REC_MAX];
      if (readBody(f, pos, r, buf)) fill(got++, r, buf);
      return got < PAGE;
    });
    if (!got) return false;
    _pg_n = got;
    _hi = start;
    _lo = end;
    return true;
  }

  // The next page forward, up to where the ring takes over; false at that point.
  bool pageNewer() {
    if (!_pg) return false;
    int got = 0;
    Spot first = { 0, 0 }, past = _hi;
    char p[24];
    for (uint32_t seg = _hi.seg; seg && seg <= _pg->last && seg <= _ceil.seg && got < PAGE; seg++) {
      filePath(*_pg, seg, p, sizeof(p));
      File f = _fs->open(p, FILE_O_READ);
      if (!f) continue;
      uint32_t size = f.size(), pos = seg == _hi.seg ? _hi.pos : 0;
      uint32_t stop = seg == _ceil.seg && _ceil.pos < size ? _ceil.pos : size;
      while (pos < stop && got < PAGE) {
        uint8_t buf[REC_MAX];
        Rec r;
        if (!f.seek(pos) || f.read(&r, sizeof(r)) != (int)sizeof(r) || !readBody(f, pos, r, buf)) break;
        uint32_t next = pos + sizeof(r) + pathBytes(r.path_len) + r.text_len + 1;
        if (mine(r, cutoff())) {
          if (!got) first = { seg, pos };
          fill(got++, r, buf);
          past = { seg, next };
        }
        pos = next;
      }
      f.close();
    }
    if (!got) return false;
    for (int i = 0, j = got - 1; i < j; i++, j--) {   // index 0 is the newest
      OlderSlot t = _history->older[i];
      _history->older[i] = _history->older[j];
      _history->older[j] = t;
    }
    _pg_n = got;
    _lo = first;
    _hi = past;
    return true;
  }

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
  struct Spot { uint32_t seg, pos; };   // a place in a log; seg 0: before its start

  void touch() {
    if (!_on || _dirty) return;
    _dirty = true;
    _due = millis() + SAVE_DELAY_MS;
  }
  void appended() {
    touch();
    if (_on && ++_unsaved >= SAVE_BURST) _due = millis();
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

  // Timestamps older than this are past Settings > Messages > Delete after (0:
  // keep all). Never with a clock not set, or behind the log's newest message:
  // a wrong clock must not cost messages.
  uint32_t cutoff() const {
    uint32_t days = NodePrefs::contactExpiryDays(_prefs->msg_expire), now = rtc_clock.getCurrentTime();
    return days && now >= 1000000000UL && now >= _newest ? now - days * 86400UL : 0;
  }

  // ── Writing ────────────────────────────────────────────────────────────────
  void flush(bool all) {
    uint32_t t0 = millis();
    _dirty = false;
    _unsaved = 0;
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
    if (ts > _newest) _newest = ts;
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

  // Drops the segments that have gone past Delete after, never the newest.
  void prune(Log& l) {
    uint32_t cut = cutoff();
    char p[24];
    while (cut && l.first && l.first < l.last) {
      Rec r;
      if (lastRec(l, l.first, r) && r.timestamp >= cut) break;
      dirPath(l, l.first++, p, sizeof(p));
      _fs->rmdir_r(p);
    }
  }

  void forget() {
    _fs->rmdir_r(ROOT);
    _dirty = false;
    _newest = 0;
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
    Rec r;
    if (n && lastRec(l, l.last, r) && r.timestamp > _newest) _newest = r.timestamp;
    return n;
  }

  // The last record of a segment.
  bool lastRec(const Log& l, uint32_t seg, Rec& r) {
    char p[24];
    filePath(l, seg, p, sizeof(p));
    File f = _fs->open(p, FILE_O_READ);
    if (!f) return false;
    uint32_t pos = f.size();
    uint8_t len;
    bool ok = pos > 0 && stepBack(f, pos, len) && f.seek(pos - len) &&
              f.read(&r, sizeof(r)) == (int)sizeof(r) && sane(r, len);
    f.close();
    return ok;
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
    uint32_t seg0 = l.last, off0 = 0, cut = cutoff();
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
        if ((!keys || chSlot(keys, r.key) >= 0) && r.timestamp >= cut) got++;
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
        if (r.timestamp >= cut) restoreRec(r, buf, keys);
        pos += sizeof(r) + rest;
      }
      f.close();
    }
  }

  void restoreRec(const Rec& r, const uint8_t* buf, const uint32_t* keys) {
    if (keys) {
      int slot = chSlot(keys, r.key);
      if (slot < 0) return;
      ChHistEntry e;
      fillCh(e, r, buf, (uint8_t)slot);
      _history->restoreCh(e);
    } else {
      DmHistEntry e;
      fillDm(e, r, buf);
      _history->restoreDm(e);
    }
  }

  static void fillCh(ChHistEntry& e, const Rec& r, const uint8_t* buf, uint8_t slot) {
    int pn = pathBytes(r.path_len);
    memset(&e, 0, sizeof(e));
    e.ch_idx = slot;
    e.timestamp = r.timestamp;
    e.relay_status = r.flags & STATUS_MASK;
    e.path_len = r.path_len;
    memcpy(e.path, buf, pn);
    memcpy(e.text, buf + pn, r.text_len);
  }
  static void fillDm(DmHistEntry& e, const Rec& r, const uint8_t* buf) {
    int pn = pathBytes(r.path_len);
    memset(&e, 0, sizeof(e));
    memcpy(e.prefix, &r.key, 4);
    e.outgoing = (r.flags & OUTGOING) ? 1 : 0;
    e.timestamp = r.timestamp;
    e.msg_ts = r.msg_ts;
    e.ack_status = r.flags & STATUS_MASK;
    e.path_len = r.path_len;
    memcpy(e.path, buf, pn);
    memcpy(e.text, buf + pn, r.text_len);
  }

  // ── Older messages ─────────────────────────────────────────────────────────
  bool pageBegin(Log* l, uint32_t key, uint8_t slot, int in_ring) {
    pageEnd();
    if (!_on || !l->last) return false;
    _history->older = (OlderSlot*)malloc(PAGE * sizeof(OlderSlot));
    if (!_history->older) return false;
    _pg = l;
    _pg_key = key;
    _pg_slot = slot;
    Spot from = { l->last, 0xFFFFFFFFUL };
    int skip = in_ring;
    if (skip > 0) from = back(from, [&](File&, uint32_t, uint32_t, const Rec&) { return --skip > 0; });
    _ceil = _lo = _hi = from;
    if (pageOlder()) return true;
    pageEnd();
    return false;
  }

  bool mine(const Rec& r, uint32_t cut) const { return r.key == _pg_key && r.timestamp >= cut; }

  // Walks the conversation's records back from `from` until fn says stop:
  // where it stopped, or the log's start. fn(file, seg, pos, record).
  template <class F> Spot back(Spot from, F fn) {
    const Log& l = *_pg;
    uint32_t cut = cutoff();
    char p[24];
    for (uint32_t seg = from.seg; seg && seg >= l.first; seg--) {
      filePath(l, seg, p, sizeof(p));
      File f = _fs->open(p, FILE_O_READ);
      if (!f) continue;
      uint32_t pos = f.size();
      if (seg == from.seg && from.pos < pos) pos = from.pos;
      uint8_t len;
      while (pos > 0 && stepBack(f, pos, len)) {
        pos -= len;
        Rec r;
        if (!f.seek(pos) || f.read(&r, sizeof(r)) != (int)sizeof(r) || !sane(r, len)) break;
        if (mine(r, cut) && !fn(f, seg, pos, r)) {
          f.close();
          return { seg, pos };
        }
      }
      f.close();
    }
    return { 0, 0 };
  }

  static bool readBody(File& f, uint32_t pos, const Rec& r, uint8_t* buf) {
    int rest = pathBytes(r.path_len) + r.text_len + 1;
    return r.text_len < MSG_TEXT_BUF && f.seek(pos + sizeof(r)) && f.read(buf, rest) == rest &&
           buf[rest - 1] == (uint8_t)(sizeof(r) + rest);
  }

  void fill(int i, const Rec& r, const uint8_t* buf) {
    OlderSlot& o = _history->older[i];
    if (_pg == &_ch) {
      fillCh(o.ch, r, buf, _pg_slot);
      if (o.ch.relay_status == ACK_PENDING) o.ch.relay_status = ACK_NONE;
    } else {
      fillDm(o.dm, r, buf);
      if (o.dm.ack_status == ACK_PENDING) o.dm.ack_status = ACK_NONE;
    }
  }

  NodePrefs*      _prefs = nullptr;
  MessageHistory* _history = nullptr;
  FILESYSTEM*     _fs = nullptr;
  bool     _on = false, _dirty = false;
  uint32_t _due = 0, _next_prune = 0;
  uint8_t  _unsaved = 0;
  uint32_t _newest = 0;   // the newest timestamp in the log
  uint32_t _seq = 0, _ch_done = 0, _dm_done = 0;   // arc_id: the last given, the last saved
  Log      _ch = { 'c', 0, 0, 0 };
  Log      _dm = { 'd', 0, 0, 0 };
  // The page being read: its log and conversation, where it starts and where
  // the one after it does, and where the ring takes over from the log.
  Log*     _pg = nullptr;
  uint32_t _pg_key = 0;
  uint8_t  _pg_slot = 0;
  int      _pg_n = 0;
  Spot     _lo = { 0, 0 }, _hi = { 0, 0 }, _ceil = { 0, 0 };
};
