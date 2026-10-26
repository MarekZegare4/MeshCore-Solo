#pragma once
// A route written in a message -- a bot's {path} / !path reply ("3f:a1:c8",
// "3fa1:c802") -- placed on the map: this device, then each hop's repeater,
// then the node the message came from when it has a position. The route a
// DM reply carries is the way our own message went to the bot, so it starts
// here; on a channel it's whoever asked's, so the caller leaves `own` out.
//
// Finding one: two or more hex groups of one width (2, 4 or 6 characters,
// i.e. 1- to 3-byte hashes) joined by ':', standing alone. A clock time or a
// score (12:30, 45:30:10) reads the same, so two or three all-decimal 2-digit
// groups aren't one. A single hop ("3f") is too like an ordinary word to tell.
//
// Placing a hop: a repeater with a position whose key starts with the hash
// (PacketFlow::placeHash); of several, the nearest the point before it. A hop
// that can't be placed is left out of the line, still counted and named.
//
// Header-only Core, single TU (see UiCore.h).

class HopRoute {
public:
  static const int MAX_HOPS = 16;
  static const int MAX_PTS = MAX_HOPS + 2;   // here, the hops, the sender

  struct Pt {
    int32_t lat, lon;
    int16_t node;   // the contact (getContactByIdx), -1 this device
    int8_t  hop;    // its hop (0-based), -1 here, -2 the sender
  };

  // The first route in `text`: its hashes (w bytes each) into `hashes`
  // (MAX_HOPS * 3 bytes), the count into `n`.
  static bool find(const char* text, uint8_t* hashes, uint8_t& n, uint8_t& w) {
    for (const char* p = text; p && *p; p++) {
      if (p != text && (isalnum((unsigned char)p[-1]) || p[-1] == ':')) continue;
      int gw = 0;
      while (isxdigit((unsigned char)p[gw])) gw++;
      if (gw != 2 && gw != 4 && gw != 6) continue;
      uint8_t k = 0;
      bool decimal = true, ok = true;
      const char* q = p;
      for (;;) {
        for (int i = 0; i < gw; i++) if (!isxdigit((unsigned char)q[i])) { ok = false; break; }
        if (!ok || isalnum((unsigned char)q[gw])) { ok = false; break; }
        if (k < MAX_HOPS) {
          for (int b = 0; b < gw / 2; b++) hashes[k * (gw / 2) + b] = (uint8_t)((hexv(q[2 * b]) << 4) | hexv(q[2 * b + 1]));
        }
        for (int i = 0; i < gw; i++) if (!isdigit((unsigned char)q[i])) decimal = false;
        k++;
        q += gw;
        if (*q != ':') break;
        q++;
      }
      if (!ok || k < 2) continue;
      if (decimal && gw == 2 && k <= 3) continue;   // a time, a score
      n = k > MAX_HOPS ? MAX_HOPS : k;
      w = (uint8_t)(gw / 2);
      return true;
    }
    return false;
  }
  static bool has(const char* text) {
    uint8_t h[MAX_HOPS * 3], n, w;
    return find(text, h, n, w);
  }

  // Places the route in `text` (see find()), from (own_lat, own_lon) when
  // `own`, to the contact keyed `sender_key` (prefix of 4+ bytes) or named
  // `sender_name` when one has a position. False when the text holds none.
  bool show(const char* text, bool own, int32_t own_lat, int32_t own_lon,
            const uint8_t* sender_key, const char* sender_name) {
    if (!find(text, _hash, _n, _w)) return false;
    _pts = 0;
    _placed = 0;
    if (own) addPt(own_lat, own_lon, -1, -1);
    for (uint8_t i = 0; i < _n; i++) {
      const bool near = _pts > 0;
      int32_t la, lo;
      int16_t idx;
      if (PacketFlow::placeHash(&_hash[i * _w], _w, true, near ? _pt[_pts - 1].lat : 0, near ? _pt[_pts - 1].lon : 0,
                                near, la, lo, idx)) {
        addPt(la, lo, idx, (int8_t)i);
        _placed++;
      }
    }
    ContactInfo* c = sender_key ? the_mesh.lookupContactByPubKey(sender_key, 4) : nullptr;
    if (!c && sender_name && sender_name[0]) c = byName(sender_name);
    if (c && (c->gps_lat || c->gps_lon)) addPt(c->gps_lat, c->gps_lon, -1, -2);
    _active = true;
    return true;
  }
  void close() { _active = false; }
  bool active() const { return _active; }

  uint8_t hops() const { return _n; }
  uint8_t placed() const { return _placed; }
  int points() const { return _pts; }
  const Pt& point(int i) const { return _pt[i]; }

  // A hop's name: its repeater's (as contactctl::hopName), else "?3F".
  void hopName(int i, char* out, size_t len) const {
    contactctl::hopName((uint8_t)((_w - 1) << 6 | _n), _hash, i, out, len);
  }

  // "3 of 4 hops on the map" (or "4 hops on the map").
  void summary(char* out, size_t len) const {
    if (_placed == _n) snprintf(out, len, "%u hop%s on the map", (unsigned)_n, _n == 1 ? "" : "s");
    else               snprintf(out, len, "%u of %u hops on the map", (unsigned)_placed, (unsigned)_n);
  }

private:
  static int hexv(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

  void addPt(int32_t lat, int32_t lon, int16_t node, int8_t hop) {
    if (_pts < MAX_PTS) _pt[_pts++] = { lat, lon, node, hop };
  }

  static ContactInfo* byName(const char* name) {
    for (int idx = MAX_ANON_CONTACTS; ; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) return nullptr;
      if (strcmp(c.name, name) == 0) return the_mesh.lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
    }
  }

  uint8_t _hash[MAX_HOPS * 3];
  uint8_t _n = 0, _w = 1, _placed = 0;
  Pt      _pt[MAX_PTS];
  int     _pts = 0;
  bool    _active = false;
};
