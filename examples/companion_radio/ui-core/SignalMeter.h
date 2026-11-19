#pragma once
// Signal in the status bar (issue #60): how well this device has heard the
// repeaters around it lately -- the "bars" of the phone apps. A history of
// what was received, not a two-way link test: a repeater heard well may still
// not hear us.
//
// Every flood packet heard with a path came last from the repeater whose hash
// ends that path (each one appends its own before passing a flood on). Up to
// five repeaters are kept, each with the best SNR of the last ten minutes;
// the level is the best of them, against the floor of the spreading factor in
// use (snrFloorForSF), so SF12's -15 dB counts as well as SF7's 0 dB. Direct
// packets are left out: their path lists the hops still ahead, not who sent
// them, and a node heard with no path at all may not be a repeater.
//
// Nothing heard is not "no signal": a quiet mesh at night sends little but
// adverts hours apart. So the age is part of the answer -- fresh (ten
// minutes), stale (half an hour: the last level, shown as old), or none
// heard lately; before the first packet since boot there is no answer.
//
// Header-only Core, single TU (see UiCore.h).

class SignalMeter {
public:
  static const int SLOTS = 5;
  static const uint32_t FRESH_MS = 10UL * 60 * 1000;
  static const uint32_t STALE_MS = 30UL * 60 * 1000;

  enum Age : uint8_t { NEVER, NONE, STALE, FRESH };
  struct Reading {
    Age age;
    uint8_t level;        // 1-4 when FRESH or STALE, else 0
    int8_t snr_x4;        // the best SNR behind it, quarter-dB
    uint8_t hash[3], hash_len;   // the repeater it came from
    uint32_t ago_ms;      // since that repeater was last heard
  };

  void onHeard(const mesh::Packet* pkt, uint32_t now) {
    if (!pkt->isRouteFlood()) return;
    const uint8_t n = pkt->getPathHashCount(), sz = pkt->getPathHashSize();
    if (n == 0 || sz > 3) return;
    const uint8_t* h = pkt->path + (n - 1) * sz;
    const int snr = (int)(pkt->getSNR() * 4);
    Slot* s = nullptr;
    Slot* spare = &_s[0];   // where a repeater not kept yet goes: an empty slot, else the longest unheard
    for (Slot& c : _s) {
      if (c.len == sz && memcmp(c.hash, h, sz) == 0) { s = &c; break; }
      if (spare->len && (!c.len || (int32_t)(c.ms - spare->ms) < 0)) spare = &c;
    }
    if (!s) { s = spare; s->len = sz; memcpy(s->hash, h, sz); s->best_ms = now - FRESH_MS - 1; }
    // The best of the last ten minutes: a better one, or any once the best is older.
    if (snr >= s->best || (uint32_t)(now - s->best_ms) > FRESH_MS) { s->best = (int8_t)snr; s->best_ms = now; }
    s->ms = now;
    _ever = true;
  }

  // sf: the spreading factor the radio is on now.
  Reading read(uint32_t now, uint8_t sf) const {
    Reading r = {};
    r.age = _ever ? NONE : NEVER;
    const Slot* best = nullptr;
    Age best_age = NONE;
    for (const Slot& c : _s) {
      if (!c.len) continue;
      const uint32_t ago = now - c.ms;
      const Age a = ago <= FRESH_MS ? FRESH : ago <= STALE_MS ? STALE : NONE;
      if (a == NONE) continue;
      if (!best || a > best_age || (a == best_age && c.best > best->best)) { best = &c; best_age = a; }
    }
    if (!best) return r;
    r.age = best_age;
    r.snr_x4 = best->best;
    r.level = levelFor(best->best, sf);
    memcpy(r.hash, best->hash, sizeof(r.hash));
    r.hash_len = best->len;
    r.ago_ms = now - best->ms;
    return r;
  }

  // 1-4 bars from the margin over the SF's demodulation floor: under 2 dB
  // above it a packet only just got through.
  static uint8_t levelFor(int snr_x4, uint8_t sf) {
    if (sf < 7) sf = 7; else if (sf > 12) sf = 12;
    const float fl = -7.5f - 2.5f * (sf - 7);   // RadioLibWrapper::snrFloorForSF (not on every radio)
    const float m = snr_x4 / 4.0f - fl;
    return m >= 12 ? 4 : m >= 7 ? 3 : m >= 2 ? 2 : 1;
  }

private:
  struct Slot { uint8_t hash[3] = {0}, len = 0; int8_t best = 0; uint32_t ms = 0, best_ms = 0; };
  Slot _s[SLOTS];
  bool _ever = false;
};
