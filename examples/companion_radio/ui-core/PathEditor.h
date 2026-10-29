#pragma once
// Path editor: the route to one node as a list of repeaters, traced and saved.
// Shared by both frontends (ui-new's Path screen, ui-lvgl's Route screen and
// its "pick on the map" mode); the view only draws rows and calls the actions
// below.
//
//   open(contact)     -- loads the contact's stored route (out_path), if any
//   addRepeater(key)  -- appends a repeater (a contact of type repeater)
//   remove / move     -- edit the list
//   trace()           -- a TRACE out and back over the list
//   probe()           -- after a trace with no reply: traces to the 1st, 2nd,
//                        ... repeater, to find the link where it stops
//   poll()            -- advance a running trace (call every UI pass)
//   save()            -- the list becomes the contact's out_path
//   discover(tries)   -- asks the node by flood which way it's reached (path
//                        discovery), `tries` times with a pause between; the
//                        routes found, useFound() puts one in the list -- the
//                        way there, or the reply's way back turned round
//
// A TRACE is answered only by the node that hears the last retransmission, so
// a route is traced there and back (A B dest B A): every node of it has to
// retransmit. Repeaters do; the node at the end only when it is a repeater
// too, or a companion with repeat on -- toNode() off traces just the
// repeaters (A B A), which also tells whether the way to the node is good.
//
// Results are per link, between node k-1 and node k of the chain (node 0 is
// this device): how well k heard k-1 (out) and k-1 heard k (back).
//
// A hop keeps up to 4 bytes of the repeater's key, so the route can be traced
// with 1, 2 or 4 byte hashes and stored with 1, 2 or 3; a hop loaded from a
// stored path whose repeater isn't a contact keeps only the bytes it had.
//
// Header-only Core, single TU (see UiCore.h).

class PathEditor {
public:
  static const int MAX_HOPS = 7;   // repeaters; out and back is 2n+1 <= TRACE_SNR_MAX entries
  static const int MAX_NODES = MAX_HOPS + 1;
  static const int NAME_LEN = 24;

  // RUNNING covers a probe's every step (probing()); PROBED: the probe is
  // over, reached() nodes answered.
  enum State : uint8_t { IDLE, RUNNING, DONE, TIMEOUT, FAILED, PROBED };
  enum Fail : uint8_t { FAIL_NONE, FAIL_NO_KEY, FAIL_BUSY, FAIL_SEND, FAIL_EMPTY };

  struct Hop {
    uint8_t key[4];
    uint8_t known;            // bytes of key that are real (1..4)
    char    name[NAME_LEN];
  };

  void begin(NodePrefs* prefs, PingEngine* ping) { _prefs = prefs; _ping = ping; }

  // Opens the editor for a node: its key and name, and its stored route.
  bool open(const uint8_t* pub_key) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (!c) return false;
    cancelTrace();
    stopDiscover();
    _found_n = 0;
    _disc_done = _disc_answered = 0;
    memcpy(_target_key, c->id.pub_key, PUB_KEY_SIZE);
    snprintf(_target_name, sizeof(_target_name), "%s", c->name);
    _target_repeats = c->type == ADV_TYPE_REPEATER;
    _to_node = _target_repeats;
    _n = 0;
    _state = IDLE;
    _fail = FAIL_NONE;
    _links = 0;
    _stored = c->out_path_len != OUT_PATH_UNKNOWN;
    _pinned = _stored && (c->flags & CONTACT_FLAG_ROUTE_PINNED) != 0;
    _too_long = false;
    if (_stored) {
      uint8_t size = (c->out_path_len >> 6) + 1;
      uint8_t cnt = c->out_path_len & 63;
      _too_long = cnt > MAX_HOPS;
      for (uint8_t i = 0; i < cnt && _n < MAX_HOPS; i++) loadHash(&c->out_path[i * size], size);
    }
    _edited = false;
    return true;
  }

  const uint8_t* targetKey() const { return _target_key; }
  const char* targetName() const { return _target_name; }
  bool hasStoredPath() const { return _stored; }
  // The list differs from the stored route (or there is none and hops were added).
  bool edited() const { return _edited; }
  // A kept route isn't replaced by a path heard from the node or dropped after a
  // failed send (CONTACT_FLAG_ROUTE_PINNED); written with save(), or at once
  // when the route is already stored. True when it took effect now.
  bool pinned() const { return _pinned; }
  bool setPinned(bool on) {
    _pinned = on;
    if (!_stored) return false;
    the_mesh.setContactPinned(_target_key, on);
    return true;
  }

  // ── the list ────────────────────────────────────────────────────────────────
  int count() const { return _n; }
  const Hop& hop(int i) const { return _hops[i]; }
  bool full() const { return _n >= MAX_HOPS; }

  // Appends a repeater by its key; false if it isn't a repeater, the list is
  // full or it is the one just before (a hop twice in a row can't be heard).
  bool addRepeater(const uint8_t* pub_key) {
    if (_n >= MAX_HOPS) return false;
    ContactInfo* c = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
    if (!c || c->type != ADV_TYPE_REPEATER) return false;
    if (_n > 0 && _hops[_n - 1].known >= 1 && memcmp(_hops[_n - 1].key, c->id.pub_key, _hops[_n - 1].known) == 0) return false;
    if (memcmp(c->id.pub_key, _target_key, 4) == 0) return false;   // the node itself
    Hop& h = _hops[_n++];
    memcpy(h.key, c->id.pub_key, 4);
    h.known = 4;
    snprintf(h.name, sizeof(h.name), "%s", c->name);
    changed();
    return true;
  }

  void remove(int i) {
    if (i < 0 || i >= _n) return;
    for (int k = i; k + 1 < _n; k++) _hops[k] = _hops[k + 1];
    _n--;
    changed();
  }

  void removeLast() { remove(_n - 1); }

  // Moves hop i one place (dir -1 up, +1 down); false at either end.
  bool move(int i, int dir) {
    int j = i + dir;
    if (i < 0 || i >= _n || j < 0 || j >= _n) return false;
    Hop t = _hops[i];
    _hops[i] = _hops[j];
    _hops[j] = t;
    changed();
    return true;
  }

  void clear() {
    _n = 0;
    changed();
  }

  // Is this repeater (by key) already in the list? (map picking marks it)
  int indexOf(const uint8_t* pub_key) const {
    for (int i = 0; i < _n; i++)
      if (memcmp(_hops[i].key, pub_key, _hops[i].known) == 0) return i;
    return -1;
  }

  // ── trace ───────────────────────────────────────────────────────────────────
  State state() const { return _state; }
  Fail failure() const { return _fail; }
  bool probing() const { return _state == RUNNING && _probe; }
  uint32_t timeoutMs() const { return _ping ? _ping->timeoutMs() : 3000; }

  // Whether the trace goes on to the node (else it turns at the last repeater).
  // On by default for a repeater: other nodes retransmit only with repeat on.
  bool toNode() const { return _to_node || _n == 0; }
  void setToNode(bool on) {
    if (_to_node == on) return;
    _to_node = on;
    invalidate();
  }
  bool targetRepeats() const { return _target_repeats; }

  // The hash width a trace uses: the node's path hash mode, narrowed to what
  // every hop has bytes for. 0 if a hop has nothing usable.
  uint8_t traceWidth() const {
    uint8_t want = _prefs ? (_prefs->path_hash_mode >= 2 ? 4 : _prefs->path_hash_mode + 1) : 1;
    uint8_t known = 4;
    for (int i = 0; i < _n; i++) if (_hops[i].known < known) known = _hops[i].known;
    while (want > known) want = want == 4 ? 2 : 1;
    return known >= 1 ? want : 0;
  }

  // Nodes of the chain a trace() goes to (not counting this device).
  int chainNodes() const { return toNode() ? _n + 1 : _n; }

  bool trace() {
    if (_state == RUNNING) return false;
    _probe = false;
    _links = 0;
    if (chainNodes() == 0) return fail(FAIL_EMPTY);
    return send(chainNodes());
  }

  // After a trace with no reply: the repeaters one by one (to the 1st, then
  // to the 2nd, ...), until one doesn't answer. reached() is then how many
  // nodes of the chain did; all repeaters answering with toNode() on puts the
  // break at the node itself.
  bool canProbe() const { return _state == TIMEOUT && _n > 0; }
  bool probe() {
    if (_state == RUNNING || _n == 0) return false;
    _probe = true;
    _reached = 0;
    _links = 0;
    return send(1);
  }
  int reached() const { return _reached; }

  // Call every UI pass while a trace may be running.
  void poll() {
    pollDiscover();
    if (_state != RUNNING || !_ping) return;
    if (!_ping->isActive()) {   // the reply came (the engine released its slot), or it was dropped
      if (_ping->hopCount() > 0) {
        capture();
        if (!_probe) { _state = DONE; return; }
        _reached = _sent_nodes;
        if (_sent_nodes < _n && send(_sent_nodes + 1)) return;
        _probe = false;
        _state = PROBED;
      } else {
        stepTimedOut();
      }
    } else if (millis() - _started_ms > _ping->timeoutMs()) {
      _ping->clear();
      stepTimedOut();
    }
  }

  void cancelTrace() {
    if (_state == RUNNING && _ping) _ping->clear();
    if (_state == RUNNING) _state = IDLE;
    _probe = false;
  }

  // ── results ─────────────────────────────────────────────────────────────────
  // Links with a result: after a trace, chainNodes(); after a probe, reached().
  int links() const { return _links; }
  // Link k (1..links()): node k heard node k-1 at `out`, node k-1 heard node k at `back` (SNR x4).
  bool linkSnrX4(int k, int16_t& out, int16_t& back) const {
    if (k < 1 || k > _links) return false;
    out = _out_x4[k - 1];
    back = _back_x4[k - 1];
    return true;
  }
  // Name of node k of the chain (0: this device -> "").
  const char* nodeName(int k) const {
    if (k >= 1 && k <= _n) return _hops[k - 1].name;
    if (k == _n + 1) return _target_name;
    return "";
  }
  uint32_t rttMs() const { return _rtt_ms; }
  // The weakest SNR of any link either way, x4; false without a result.
  bool worstX4(int16_t& v) const {
    for (int k = 0; k < _links; k++) {
      int16_t m = _out_x4[k] < _back_x4[k] ? _out_x4[k] : _back_x4[k];
      if (k == 0 || m < v) v = m;
    }
    return _links > 0;
  }

  // ── discover ────────────────────────────────────────────────────────────────
  // The node is asked by flood (MyMesh::sendPathDiscovery()); its reply says
  // which repeaters the flood went through to reach it, and which the reply
  // took back -- learned from the mesh, not from anyone's position. A flood
  // goes everywhere, so several tries wait for each other's reply (or its
  // timeout) and then DISCOVER_GAP_MS more. Each different route is kept
  // with how many times it came; the fastest is not always the best, so the
  // user picks one (useFound()) and can trace it.
  static const int FOUND_MAX = 4;
  static const int FOUND_HOPS = 8;   // hops kept of a route (one longer than the list holds: "too long")
  static const uint32_t DISCOVER_GAP_MS = 20000;
  struct Found {
    uint8_t out_len, back_len;   // encoded path lengths (hop count | (hash size - 1) << 6)
    uint8_t out[FOUND_HOPS * 3], back[FOUND_HOPS * 3];
    uint8_t times;
  };

  bool discover(int tries) {
    if (tries < 1) tries = 1;
    stopDiscover();
    _found_n = 0;
    _disc_tries = (uint8_t)tries;
    _disc_done = _disc_answered = 0;
    return discoverSend();
  }
  void stopDiscover() {
    if (_disc_waiting) the_mesh.cancelPathDiscovery();
    _disc_waiting = false;
    _disc_tries = 0;
  }
  bool discovering() const { return _disc_tries > 0 && _disc_done < _disc_tries; }
  bool discoverWaiting() const { return _disc_waiting; }   // a try is out, its reply awaited
  int discoverTry() const { return _disc_done + (_disc_waiting ? 1 : 0); }   // 1-based while waiting
  int discoverTries() const { return _disc_tries; }
  int discoverDone() const { return _disc_done; }
  int discoverAnswered() const { return _disc_answered; }
  bool discoverFailed() const { return _disc_failed; }
  // Seconds to the next try, between tries.
  int discoverNextIn() const {
    if (!discovering() || _disc_waiting) return 0;
    int32_t ms = (int32_t)(_disc_at - millis());
    return ms > 0 ? (ms + 999) / 1000 : 0;
  }

  int foundCount() const { return _found_n; }
  const Found& found(int i) const { return _found[i]; }
  static int hopsOf(uint8_t len) { return len & 63; }
  // Route i's hops, "Name > Name" (hash "?3F" for one not a contact), cut to
  // fit. The way back as the reply came (from the node), or with `as_route`
  // turned round, from here: as useFound(i, true) lists it.
  void foundText(int i, bool back, char* out, size_t n, bool as_route = false) const {
    const Found& f = _found[i];
    const uint8_t len = back ? f.back_len : f.out_len;
    const uint8_t* p = back ? f.back : f.out;
    const int hops = hopsOf(len), size = (len >> 6) + 1;
    const int kept = hops < FOUND_HOPS ? hops : FOUND_HOPS;
    const bool rev = back && as_route;
    size_t o = 0;
    out[0] = '\0';
    if (hops == 0) { snprintf(out, n, "direct"); return; }
    for (int h = 0; h < kept && o + 4 < n; h++) {
      char nm[NAME_LEN];
      hashName(&p[(rev ? kept - 1 - h : h) * size], size, nm, sizeof(nm));
      o += snprintf(out + o, n - o, "%s%s", h ? " > " : "", nm);
    }
    if (hops > FOUND_HOPS && o + 4 < n) snprintf(out + o, n - o, " ...");
  }
  // The reply's way back, turned round, is route i itself: one way both ways.
  bool foundBackSame(int i) const {
    const Found& f = _found[i];
    if (f.back_len != f.out_len) return false;
    const int hops = hopsOf(f.out_len) < FOUND_HOPS ? hopsOf(f.out_len) : FOUND_HOPS, size = (f.out_len >> 6) + 1;
    for (int h = 0; h < hops; h++)
      if (memcmp(&f.out[h * size], &f.back[(hops - 1 - h) * size], size) != 0) return false;
    return true;
  }
  static int foundHops(const Found& f, bool back) { return hopsOf(back ? f.back_len : f.out_len); }
  // Route i as the list (its repeaters by key where they're contacts): the
  // way there, or the way back turned round -- a route there too, which a
  // trace then checks out and back like any.
  bool useFound(int i, bool back = false) {
    if (i < 0 || i >= _found_n) return false;
    const Found& f = _found[i];
    const uint8_t len = back ? f.back_len : f.out_len;
    const uint8_t* p = back ? f.back : f.out;
    const int hops = hopsOf(len), size = (len >> 6) + 1;
    cancelTrace();
    _n = 0;
    for (int h = 0; h < hops && h < MAX_HOPS; h++) loadHash(&p[(back ? hops - 1 - h : h) * size], size);
    _too_long = hops > MAX_HOPS;
    _edited = true;
    invalidate();
    return true;
  }

  // ── save ────────────────────────────────────────────────────────────────────
  // The list as the contact's stored route; an empty list stores "direct"
  // (no repeaters, sent straight to the node). False when the hops share no
  // usable width.
  // A stored route longer than MAX_HOPS loads only its first hops; saving it
  // would cut it short, so it can't be saved until its hops are all removed.
  bool tooLong() const { return _too_long; }
  bool canSave() const { return !_too_long && saveWidth() > 0; }
  bool save() {
    uint8_t w = saveWidth();
    if (w == 0 || _too_long) return false;
    uint8_t path[MAX_HOPS * 3];
    for (int i = 0; i < _n; i++) memcpy(&path[i * w], _hops[i].key, w);
    if (!the_mesh.setContactPath(_target_key, path, w, (uint8_t)_n, _pinned)) return false;
    _stored = true;
    _edited = false;
    return true;
  }

private:
  // The editor a discovery reply goes to (one editor, one discovery at a time).
  static PathEditor*& discOwner() { static PathEditor* p = nullptr; return p; }

  bool discoverSend() {
    uint32_t est = 0;
    discOwner() = this;
    _disc_failed = false;
    if (!the_mesh.sendPathDiscovery(_target_key, onDiscovered, &est)) {
      _disc_failed = true;
      _disc_tries = 0;
      return false;
    }
    _disc_waiting = true;
    uint32_t wait = est + 4000;   // the flood there and the reply's flood back
    if (wait < 8000) wait = 8000;
    _disc_at = millis() + wait;
    return true;
  }

  void pollDiscover() {
    if (_disc_tries == 0) return;
    if (_disc_waiting) {
      if ((int32_t)(millis() - _disc_at) < 0) return;
      the_mesh.cancelPathDiscovery();   // no reply in time
      tryOver();
    } else if (_disc_done < _disc_tries && (int32_t)(millis() - _disc_at) >= 0) {
      discoverSend();
    }
  }

  // A try answered or timed out: the next one after the pause.
  void tryOver() {
    _disc_waiting = false;
    _disc_done++;
    _disc_at = millis() + DISCOVER_GAP_MS;
  }

  static void onDiscovered(const uint8_t* key, const uint8_t* out, uint8_t out_len, const uint8_t* back, uint8_t back_len) {
    PathEditor* e = discOwner();
    if (!e || !e->_disc_waiting || memcmp(key, e->_target_key, PUB_KEY_SIZE) != 0) return;
    e->record(out, out_len, back, back_len);
    e->_disc_answered++;
    e->tryOver();
  }

  // A route found: another time for one already found (the latest way back
  // with it), else a new one while there's room.
  void record(const uint8_t* out, uint8_t out_len, const uint8_t* back, uint8_t back_len) {
    const int osize = (out_len >> 6) + 1, bsize = (back_len >> 6) + 1;
    if (osize > 3 || bsize > 3) return;
    const int obytes = (hopsOf(out_len) < FOUND_HOPS ? hopsOf(out_len) : FOUND_HOPS) * osize;
    const int bbytes = (hopsOf(back_len) < FOUND_HOPS ? hopsOf(back_len) : FOUND_HOPS) * bsize;
    Found* f = nullptr;
    for (int i = 0; i < _found_n && !f; i++)
      if (_found[i].out_len == out_len && memcmp(_found[i].out, out, obytes) == 0) f = &_found[i];
    if (f) {
      if (f->times < 255) f->times++;
    } else {
      if (_found_n >= FOUND_MAX) return;
      f = &_found[_found_n++];
      f->out_len = out_len;
      memcpy(f->out, out, obytes);
      f->times = 1;
    }
    f->back_len = back_len;
    memcpy(f->back, back, bbytes);
  }

  // A hash's repeater by name, else "?3F".
  static void hashName(const uint8_t* hash, uint8_t size, char* out, size_t n) {
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (c.type == ADV_TYPE_REPEATER && c.id.isHashMatch(hash, size)) { snprintf(out, n, "%s", c.name); return; }
    }
    char hex[9] = {0};
    for (uint8_t k = 0; k < size && k < 4; k++) snprintf(hex + k * 2, 3, "%02X", hash[k]);
    snprintf(out, n, "?%s", hex);
  }

  uint8_t saveWidth() const {   // 1..3 bytes of a stored path, the hash mode narrowed to what every hop has
    uint8_t want = _prefs ? _prefs->path_hash_mode + 1 : 1;
    if (want > 3) want = 3;
    uint8_t known = 4;
    for (int i = 0; i < _n; i++) if (_hops[i].known < known) known = _hops[i].known;
    if (known > 3) known = 3;
    return want < known ? want : known;
  }

  void changed() {
    _edited = true;
    if (_n == 0) _too_long = false;   // emptied: what's saved now is the user's own list
    invalidate();
  }

  void invalidate() {
    if (_state == RUNNING && _ping) _ping->clear();
    _state = IDLE;
    _probe = false;
    _links = 0;
  }

  bool fail(Fail f) {
    _fail = f;
    _state = FAILED;
    _probe = false;
    return false;
  }

  // A trace to node `nodes` of the chain and back: nodes 1..nodes out, then
  // nodes-1..1. The chain's last node is the target when it reaches past the
  // repeaters.
  bool send(int nodes) {
    if (!_ping) return fail(FAIL_BUSY);
    uint8_t w = traceWidth();
    if (w == 0) return fail(FAIL_NO_KEY);
    uint8_t path[TRACE_SNR_MAX * 4];
    int e = 0;
    auto key = [&](int k) -> const uint8_t* { return k <= _n ? _hops[k - 1].key : _target_key; };
    for (int k = 1; k <= nodes; k++) memcpy(&path[w * e++], key(k), w);
    for (int k = nodes - 1; k >= 1; k--) memcpy(&path[w * e++], key(k), w);
    PingEngine::StartResult r = _ping->startTrace(path, (uint8_t)e, w);
    if (r != PingEngine::STARTED) return fail(r == PingEngine::BUSY ? FAIL_BUSY : FAIL_SEND);
    _fail = FAIL_NONE;
    _state = RUNNING;
    _started_ms = millis();
    _sent_nodes = nodes;
    return true;
  }

  void stepTimedOut() {
    if (_probe) {
      _probe = false;
      _state = PROBED;   // _reached nodes answered, the next one didn't
    } else {
      _state = TIMEOUT;
    }
  }

  // The reply's per-entry SNRs as links. Over M nodes the path has 2M-1
  // entries: entry k-1 is node k hearing k-1 on the way out; node k-1 hearing
  // node k on the way back is entry 2M-k (k >= 2), and for k = 1 it is this
  // device hearing the reply.
  void capture() {
    const int M = _sent_nodes;
    int16_t o, back_us; uint32_t rtt;
    _ping->getResult(o, back_us, rtt);
    _rtt_ms = rtt;
    _links = 0;
    if (_ping->hopCount() < 2 * M - 1) return;
    for (int k = 1; k <= M; k++) {
      _out_x4[k - 1] = _ping->hopSnrX4(k - 1);
      _back_x4[k - 1] = k == 1 ? back_us : _ping->hopSnrX4(2 * M - k);
    }
    _links = M;
  }

  // A hash of a stored path: the contact it belongs to by key prefix, else the bytes.
  void loadHash(const uint8_t* hash, uint8_t size) {
    Hop& h = _hops[_n++];
    memset(h.key, 0, sizeof(h.key));
    h.known = size > 4 ? 4 : size;
    memcpy(h.key, hash, h.known);
    h.name[0] = '\0';
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (c.type == ADV_TYPE_REPEATER && c.id.isHashMatch(hash, size)) {
        memcpy(h.key, c.id.pub_key, 4);
        h.known = 4;
        snprintf(h.name, sizeof(h.name), "%s", c.name);
        return;
      }
    }
    char hex[9] = {0};
    for (uint8_t k = 0; k < h.known; k++) snprintf(hex + k * 2, 3, "%02X", h.key[k]);
    snprintf(h.name, sizeof(h.name), "?%s", hex);
  }

  NodePrefs*  _prefs = nullptr;
  PingEngine* _ping = nullptr;
  uint8_t  _target_key[PUB_KEY_SIZE];
  char     _target_name[32];
  bool     _target_repeats = false;
  bool     _to_node = false;
  Hop      _hops[MAX_HOPS];
  int      _n = 0;
  State    _state = IDLE;
  Fail     _fail = FAIL_NONE;
  bool     _stored = false;
  bool     _too_long = false;     // the stored route has more hops than the list holds
  bool     _pinned = false;
  bool     _edited = false;
  bool     _probe = false;
  int      _reached = 0;
  int      _sent_nodes = 0;
  uint32_t _started_ms = 0;
  uint32_t _rtt_ms = 0;
  int      _links = 0;
  int16_t  _out_x4[MAX_NODES];
  int16_t  _back_x4[MAX_NODES];
  Found    _found[FOUND_MAX];
  int      _found_n = 0;
  uint8_t  _disc_tries = 0, _disc_done = 0, _disc_answered = 0;
  bool     _disc_waiting = false, _disc_failed = false;
  uint32_t _disc_at = 0;   // waiting: the reply's deadline; between tries: the next one's time
};
