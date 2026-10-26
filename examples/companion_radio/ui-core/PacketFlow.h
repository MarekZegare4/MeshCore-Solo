#pragma once
// Packet flow: what the radio hears, as lines on the map -- a picture of the
// mesh at work, nothing more. Every packet heard (MyMesh::logRx) that can be
// placed becomes a flow: the node it came from, the repeaters it went
// through, this device; a packet sent is a pulse here. The frontends draw the
// recent flows over their map (ui-lvgl animates them, ui-new draws the lines).
//
// Placing a packet:
//   - its path (a flood's repeaters, in order) -- each hash is matched to a
//     repeater contact with a position; with several matches, the one nearest
//     the point before it;
//   - its origin, when the packet says: an advert carries the sender's key
//     and position, a direct message / request / response / path the
//     sender's one-byte hash (matched like a hop, to any contact);
//   - its end: this device (the GPS, else the position set in Privacy).
// Points without a position are skipped, so a flow may be shorter than its
// path. A packet nothing of which can be placed (a group message from a node
// in direct range, a sender with no position) is still a pulse here: every
// packet heard shows. A direct packet heard on its way elsewhere carries the
// hops still ahead: a pulse at the next one (here when it isn't known).
// Each kind is counted over the last minute (count()), for a legend.
// One packet is often heard several times (direct, then via each repeater):
// its flows share one fading clock, restarted by every copy (t_fade), so the
// whole picture of it goes at once.
//
// Off (NodePrefs::flow_show 0) nothing is resolved: a contact scan per hop is
// cheap, but not free on every packet.
//
// Header-only Core, single TU (see UiCore.h).

class PacketFlow {
public:
#if defined(ESP32)
  static const int MAX_FLOWS = 32;   // a busy mesh: a few seconds of it
#else
  static const int MAX_FLOWS = 12;   // the nRF52's heap is short
#endif
  static const int MAX_PTS = 10;   // origin, up to 8 repeaters, this device

  // What a packet is (its payload type), and one of our own.
  enum Kind : uint8_t { K_ADVERT, K_MSG, K_GROUP, K_REQ, K_ACK, K_OTHER, K_SENT, K_COUNT };

  struct Flow {
    uint32_t t_ms;        // when it was heard
    uint32_t t_fade;      // when its fading clock starts: the last copy of the same packet heard
    uint32_t id;          // the packet (its hash): copies heard via other repeaters go together
    uint8_t  kind;
    uint8_t  n;           // points (1 for a pulse)
    bool     origin;      // point 0 is the sender (else the first repeater placed)
    bool     here;        // a pulse here: nothing of it could be placed
    int32_t  lat[MAX_PTS], lon[MAX_PTS];
    int16_t  node[MAX_PTS];   // the contact at each point (getContactByIdx), -1 this device / unknown
  };

  static uint8_t kindOf(uint8_t type) {
    switch (type) {
      case PAYLOAD_TYPE_ADVERT:   return K_ADVERT;
      case PAYLOAD_TYPE_TXT_MSG:  return K_MSG;
      case PAYLOAD_TYPE_GRP_TXT:
      case PAYLOAD_TYPE_GRP_DATA: return K_GROUP;
      case PAYLOAD_TYPE_REQ:
      case PAYLOAD_TYPE_RESPONSE:
      case PAYLOAD_TYPE_ANON_REQ: return K_REQ;
      case PAYLOAD_TYPE_ACK:
      case PAYLOAD_TYPE_PATH:     return K_ACK;
      default:                    return K_OTHER;   // trace, control, multipart, raw
    }
  }

  // Packets of `kind` heard (K_SENT: sent) in the last minute.
  int count(uint8_t kind) const {
    if (kind >= K_COUNT) return 0;
    const uint32_t now = millis() / BUCKET_MS;
    int n = 0;
    for (int b = 0; b < BUCKETS; b++)
      if (now - _bucket_at[b] < BUCKETS) n += _cnt[b][kind];
    return n;
  }

  void begin(NodePrefs* prefs, CourseEngine* course, SensorManager* sensors) {
    _prefs = prefs; _course = course; _sensors = sensors;
  }

  bool enabled() const { return _prefs && _prefs->flow_show; }

  // Changes with every flow added: a view repaints when it moves.
  uint32_t seq() const { return _seq; }
  int count() const { return _n; }
  // i = 0 is the newest.
  const Flow& at(int i) const { return _flows[(_head + MAX_FLOWS - 1 - i) % MAX_FLOWS]; }
  // Where point k of a flow is now: a contact where it is now, the end here
  // (no contact) where you are now -- not where they were when it was heard,
  // so the lines meet the map's marks after a node moved or the GPS wandered.
  void pointNow(const Flow& f, int k, int32_t& lat, int32_t& lon) const {
    lat = f.lat[k]; lon = f.lon[k];
    if (f.node[k] >= 0) {
      ContactInfo c;
      if (the_mesh.getContactByIdx(f.node[k], c) && (c.gps_lat || c.gps_lon)) { lat = c.gps_lat; lon = c.gps_lon; }
    } else if (k == f.n - 1) {
      int32_t a, b;
      if (ownPosition(a, b)) { lat = a; lon = b; }
    }
  }

  // Anything younger than `life_ms`?
  bool active(uint32_t life_ms) const { return _n > 0 && millis() - at(0).t_ms < life_ms; }

  void onHeard(const mesh::Packet* pkt) {
    if (!enabled() || !pkt) return;
    Flow f;
    f.t_ms = millis();
    f.n = 0;
    f.origin = false;
    f.here = false;
    f.id = idOf(pkt);
    const uint8_t type = pkt->getPayloadType();
    f.kind = kindOf(type);
    tally(f.kind);
    const uint8_t hs = pkt->getPathHashSize(), hc = pkt->getPathHashCount();
    int32_t ola, olo;
    const bool own = ownPosition(ola, olo);

    // On its way elsewhere: a pulse at the next hop. (A trace's path is its
    // SNRs, not hashes.)
    if (pkt->isRouteDirect() || type == PAYLOAD_TYPE_TRACE) {
      int32_t lat, lon;
      int16_t idx;
      if (type != PAYLOAD_TYPE_TRACE && hc > 0 && placeHash(pkt->path, hs, false, 0, 0, false, lat, lon, idx)) add(f, lat, lon, idx);
      else if (own) { add(f, ola, olo); f.here = true; }
      else return;
      push(f);
      return;
    }

    // Back from this device: each hash nearest the point after it, which
    // it was heard by -- the last repeater is the one heard here.
    int32_t lat[MAX_PTS], lon[MAX_PTS];
    int16_t node[MAX_PTS];
    int k = MAX_PTS;
    if (own) { k--; lat[k] = ola; lon[k] = olo; node[k] = -1; }
    for (int i = hc - 1; i >= 0 && k > 1; i--) {
      int32_t a, b;
      int16_t idx;
      const bool near = k < MAX_PTS;
      if (placeHash(&pkt->path[i * hs], hs, true, near ? lat[k] : 0, near ? lon[k] : 0, near, a, b, idx)) {
        k--; lat[k] = a; lon[k] = b; node[k] = idx;
      }
    }
    int32_t a, b;
    int16_t idx;
    if (k > 0 && origin(pkt, k < MAX_PTS, k < MAX_PTS ? lat[k] : 0, k < MAX_PTS ? lon[k] : 0, a, b, idx)) {
      k--; lat[k] = a; lon[k] = b; node[k] = idx;
      f.origin = true;
    }
    for (int i = k; i < MAX_PTS; i++) add(f, lat[i], lon[i], node[i]);
    if (f.n < 2) {   // only here, or nothing: a pulse here
      if (!own) return;
      f.n = 0;
      add(f, ola, olo);
      f.here = true;
    }
    push(f);
  }

  void onSent(const mesh::Packet* pkt) {
    if (!enabled() || !pkt) return;
    tally(K_SENT);
    int32_t lat, lon;
    if (!ownPosition(lat, lon)) return;
    Flow f;
    f.t_ms = millis();
    f.kind = K_SENT;
    f.n = 0;
    f.origin = true;
    f.here = false;
    f.id = idOf(pkt);
    add(f, lat, lon);
    push(f);
  }

  void clear() { _n = 0; _head = 0; _seq++; memset(_cnt, 0, sizeof(_cnt)); }

private:
  static const int BUCKETS = 6;               // the minute, in 10 s steps
  static const uint32_t BUCKET_MS = 10000;

  void tally(uint8_t kind) {
    const uint32_t now = millis() / BUCKET_MS;
    const int b = now % BUCKETS;
    if (_bucket_at[b] != now) { _bucket_at[b] = now; memset(_cnt[b], 0, sizeof(_cnt[b])); }
    if (_cnt[b][kind] < 0xFFFF) _cnt[b][kind]++;
  }

  static void add(Flow& f, int32_t lat, int32_t lon, int16_t node = -1) {
    if (f.n > 0 && f.lat[f.n - 1] == lat && f.lon[f.n - 1] == lon) return;   // the same spot twice
    f.lat[f.n] = lat; f.lon[f.n] = lon; f.node[f.n] = node; f.n++;
  }

  static uint32_t idOf(const mesh::Packet* pkt) {
    uint8_t h[MAX_HASH_SIZE];
    pkt->calculatePacketHash(h);
    uint32_t id;
    memcpy(&id, h, sizeof(id));
    return id;
  }

  void push(Flow f) {
    f.t_fade = f.t_ms;
    for (int i = 0; i < _n; i++)   // the earlier copies of it: their fade starts again with this one
      if (_flows[i].id == f.id) _flows[i].t_fade = f.t_ms;
    _flows[_head] = f;
    _head = (_head + 1) % MAX_FLOWS;
    if (_n < MAX_FLOWS) _n++;
    _seq++;
  }

  bool ownPosition(int32_t& lat, int32_t& lon) const {
    if (_course && _course->currentLocation(lat, lon)) return true;
    if (!_sensors) return false;
    lat = (int32_t)lround(_sensors->node_lat * 1e6);
    lon = (int32_t)lround(_sensors->node_lon * 1e6);
    return lat != 0 || lon != 0;
  }

  // Where the packet came from, when it says (a one-byte hash: the contact
  // nearest the point after it, when there is one).
  static bool origin(const mesh::Packet* pkt, bool near, int32_t nlat, int32_t nlon, int32_t& lat, int32_t& lon,
                     int16_t& node) {
    const uint8_t type = pkt->getPayloadType();
    node = -1;
    if (type == PAYLOAD_TYPE_ADVERT) {
      ContactInfo* c = the_mesh.lookupContactByPubKey(pkt->payload, PUB_KEY_SIZE);
      if (c) node = indexOf(c->id.pub_key);
      const int app = PUB_KEY_SIZE + 4 + SIGNATURE_SIZE;   // key, timestamp, signature, then the app data
      if (pkt->payload_len > app) {
        AdvertDataParser p(&pkt->payload[app], pkt->payload_len - app);
        if (p.isValid() && p.hasLatLon() && (p.getIntLat() || p.getIntLon())) {
          lat = p.getIntLat(); lon = p.getIntLon();
          return true;
        }
      }
      if (c && (c->gps_lat || c->gps_lon)) { lat = c->gps_lat; lon = c->gps_lon; return true; }
      return false;
    }
    if ((type == PAYLOAD_TYPE_TXT_MSG || type == PAYLOAD_TYPE_REQ || type == PAYLOAD_TYPE_RESPONSE ||
         type == PAYLOAD_TYPE_PATH) && pkt->payload_len >= 2)
      return placeHash(&pkt->payload[1], 1, false, nlat, nlon, near, lat, lon, node);   // [dest hash, src hash, ...]
    return false;
  }

public:   // HopRoute places a route's hops the same way
  // A contact with a position whose key starts with `hash` (`size` bytes):
  // repeaters only when `repeaters`; with several, the nearest to (nlat, nlon)
  // when `near`, else the first.
  static bool placeHash(const uint8_t* hash, uint8_t size, bool repeaters, int32_t nlat, int32_t nlon, bool near,
                        int32_t& lat, int32_t& lon, int16_t& node) {
    bool found = false;
    node = -1;
    float best = 0;
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (repeaters && c.type != ADV_TYPE_REPEATER) continue;
      if ((!c.gps_lat && !c.gps_lon) || memcmp(c.id.pub_key, hash, size) != 0) continue;
      float d = near ? geo::haversineKm(nlat, nlon, c.gps_lat, c.gps_lon) : 0;
      if (!found || d < best) { best = d; lat = c.gps_lat; lon = c.gps_lon; node = (int16_t)idx; found = true; }
      if (!near) break;
    }
    return found;
  }

private:
  // The contact's index (getContactByIdx) by its key; -1 none.
  static int16_t indexOf(const uint8_t* key) {
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) return -1;
      if (memcmp(c.id.pub_key, key, PUB_KEY_SIZE) == 0) return (int16_t)idx;
    }
  }

  NodePrefs*     _prefs = nullptr;
  CourseEngine*  _course = nullptr;
  SensorManager* _sensors = nullptr;
  Flow     _flows[MAX_FLOWS];
  int      _head = 0, _n = 0;
  uint32_t _seq = 0;
  uint16_t _cnt[BUCKETS][K_COUNT] = {};
  uint32_t _bucket_at[BUCKETS] = {};
};
