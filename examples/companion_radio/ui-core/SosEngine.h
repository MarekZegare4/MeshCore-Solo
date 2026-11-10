#pragma once
// SOS: a countdown that can be cancelled, then a position message to the chosen
// channels and contacts (UiCore::sendSos()), repeated until someone answers
// with the ACK word, the sends run out or the user stops it. Once more as soon
// as the GPS gets a fix, if none has gone with one yet, even past the last
// send. A beacon (a zero-hop advert with the position) and an optional beep
// keep going until it's stopped. Only the timing and the
// matching live here: no radio, no clock, so it's tested on the host
// (test/test_sos_engine). Not kept across a restart.

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <helpers/UTF8Helpers.h>
#include "../NodePrefs.h"
#include "../GeoUtils.h"   // SOS_MSG_TAG

class SosEngine {
public:
  static const int RESPONDERS_MAX = 3;
  struct Responder {
    char     name[24];
    char     text[48];   // what they wrote; "" when it was only the ACK word
    uint32_t at;         // millis() of their last message
  };
  static const uint8_t  MAX_SENDS = 12;       // a flood to every target: never without end
  static const uint32_t RETRY_MS  = 10000UL;  // nothing went out: soon again, still counted
  static const uint32_t LAST_MAX_SECS = 3600; // an older fix isn't sent: you could be anywhere by then
  static const uint32_t SCREEN_MS = 30000UL;  // while one runs the screen goes off by this, whatever auto-off says
  static const uint32_t BEEP_FIRST_MS = 3000UL;   // after the first send's own sound
  static const uint32_t BEACON_MS = 5UL * 60000UL; // the beacon's gap with Repeat off

  void begin(const NodePrefs* prefs) { _prefs = prefs; }

  bool  idle() const        { return _state == IDLE; }
  bool  counting() const    { return _state == COUNTDOWN; }
  bool  acked() const       { return _acked; }
  int   responders() const  { return _responders; }
  const Responder& responder(int i) const { return _resp[i]; }   // the latest first
  uint8_t sent() const      { return _sent; }     // sends that went out
  // The GPS must stay awake, past any duty cycle: the beacon gives the
  // position until the end, and someone on the way needs it current.
  bool  gpsWanted() const { return _state != IDLE; }
  // The screen's auto-off while one runs. ms: the user's, 0 = never.
  uint32_t screenMs(uint32_t ms) const {
    if (_state != IDLE && (ms == 0 || ms > SCREEN_MS)) return SCREEN_MS;
    return ms;
  }

  void start(uint32_t now) {
    if (_state != IDLE) return;
    stop();
    _state = COUNTDOWN;
    _t0 = now;
  }
  void stop() {
    _state = IDLE;
    _attempts = _sent = _responders = 0;
    _acked = _with_fix = _failed = _beeped = false;
  }

  // Whole seconds left of the countdown, rounded up: 0 when not counting.
  int secsLeft(uint32_t now) const {
    if (_state != COUNTDOWN) return 0;
    uint32_t total = countdownMs(), el = now - _t0;
    return el >= total ? 0 : (int)((total - el + 999) / 1000);
  }
  // Seconds to the next send, -1 when there won't be one.
  int nextSecs(uint32_t now) const {
    if (_state != ACTIVE || !repeating()) return -1;
    int32_t left = (int32_t)(_sent_at + nextGap() - now);
    return left <= 0 ? 0 : (left + 999) / 1000;
  }

  // How it's going, for the status views.
  void sentLine(char* out, int n) const {
    if (_sent) snprintf(out, n, "Sent %u x", (unsigned)_sent);
    else       snprintf(out, n, "Not sent");
  }
  // `minutes` for a slow panel, where a line ticking every second is a refresh a second.
  void nextLine(char* out, int n, uint32_t now, bool minutes = false) const {
    const int next = nextSecs(now);
    if (next < 0 && !advertMode()) snprintf(out, n, "No more sends");
    else if (next < 0)  snprintf(out, n, "Advert every %u min", (unsigned)(beaconMs() / 60000UL));
    else if (next < 60) snprintf(out, n, minutes ? "Next in <1 min" : "Next in %d s", next);
    else                snprintf(out, n, "Next in %d min", (next + 59) / 60);
  }

  // True when a message is due: the caller sends it and calls markSent().
  bool loop(uint32_t now, bool has_fix) {
    if (_state == COUNTDOWN) {
      if (now - _t0 < countdownMs()) return false;
      _state = ACTIVE;
      _active_at = now;
      _beeped = false;
      return true;
    }
    if (_state != ACTIVE || _acked) return false;
    if (has_fix && !_with_fix && _sent && !_failed) return true;   // the position they don't have yet
    return repeating() && now - _sent_at >= nextGap();
  }
  // True when the beep is due, for whoever lies there unable to answer: past
  // the sends and an ACK too, help still has to find you. Until it's stopped.
  // The gaps here are the settings' now: one changed while it runs counts at once.
  bool beepDue(uint32_t now) {
    const uint32_t gap = _prefs ? (uint32_t)NodePrefs::sosBeepSecs(_prefs->sos_beep_idx) * 1000UL : 0;
    if (_state != ACTIVE || gap == 0) return false;
    if (_beeped ? now - _beep_at < gap : now - _active_at < BEEP_FIRST_MS) return false;
    _beep_at = now;
    _beeped = true;
    return true;
  }
  // True when the beacon is due: from the first send to the end, past the
  // sends and an ACK too, the messages for people, it for the map of whoever
  // is in range. The caller adverts and calls markBeacon(); a send does too.
  bool beaconDue(uint32_t now) const {
    return advertMode() && _state == ACTIVE && _attempts && now - _beacon_at >= beaconMs();
  }
  // NodePrefs::sos_advert: off, zero-hop (whoever is in range) or flood (the mesh).
  uint8_t advertMode() const { return _prefs ? _prefs->sos_advert : NodePrefs::SOS_ADVERT_ZERO_HOP; }
  void markBeacon(uint32_t now) { _beacon_at = now; }
  void markSent(uint32_t now, bool with_fix, bool ok) {
    _attempts++;
    if (ok) _sent++;
    if (ok && with_fix) _with_fix = true;
    _failed = !ok;
    _sent_at = now;
  }

  // A message came in. True when it's an answer: one holding the ACK word, from
  // anyone, or anything further from someone who has answered, who is the one
  // talking to you now. Repeats stop at the first, and the screen says who
  // answered and what they said.
  bool onReply(const char* from, const char* text, uint32_t now = 0) {
    if (_state != ACTIVE || !_sent || !_prefs || !text) return false;
    if (!from) from = "";
    int i = 0;
    while (i < _responders && strncmp(_resp[i].name, from, sizeof(_resp[i].name) - 1) != 0) i++;
    const bool known = i < _responders;
    const bool word = hasWord(text, _prefs->sos_ack);
    if (!known && !word) return false;
    _acked = true;
    if (!known) {
      if (_responders < RESPONDERS_MAX) _responders++;
      i = _responders - 1;   // full: over the one quiet the longest
    }
    Responder r = _resp[i];
    for (; i > 0; i--) _resp[i] = _resp[i - 1];   // to the front
    _resp[0] = r;
    snprintf(_resp[0].name, sizeof(_resp[0].name), "%s", from);
    const bool bare = word && strlen(text) == strlen(_prefs->sos_ack);
    if (!bare || !known) {   // the word alone adds nothing to what they said before
      snprintf(_resp[0].text, sizeof(_resp[0].text), "%s", bare ? "" : text);
      _resp[0].text[mesh::validUtf8PrefixLength(_resp[0].text, sizeof(_resp[0].text) - 1)] = '\0';   // cut off mid-character
    }
    _resp[0].at = now;
    return true;
  }

  // ── Text ──────────────────────────────────────────────────────────────────
  // `word` on its own in `text`, whatever the case: ACK in "ack, on our way",
  // not in "back". A byte past ASCII counts as a letter (UTF-8).
  static bool hasWord(const char* text, const char* word) {
    int n = word ? (int)strlen(word) : 0;
    if (!text || n == 0) return false;
    for (const char* p = text; *p; p++) {
      if (p != text && isWordByte(p[-1])) continue;
      if (strncasecmp(p, word, n) == 0 && !isWordByte(p[n])) return true;
    }
    return false;
  }

  enum Where : uint8_t { FIX, LAST_KNOWN, NOWHERE };
  // What the message text may hold (NodePrefs::sos_msg), offered by the editors.
  static const int PLACEHOLDER_COUNT = 4;
  static const char* placeholder(int i) {
    static const char* const PH[PLACEHOLDER_COUNT] = { "{loc}", "{alt}", "{time}", "{batt}" };
    return PH[i];
  }

  // "[SOS] 50.06143,19.93658 212m 14:32": the tag (geo::SOS_MSG_TAG), then
  // `tmpl` with its placeholders filled. The position goes first when `tmpl`
  // has no {loc}: an SOS never goes without it, and it's what a receiver's
  // Navigate and map read (geo::parseSos).
  //   {loc}  "50.06143,19.93658", "last 50.06143,19.93658 12min ago", "no GPS"
  //   {alt}  "212m", with a fix only     {time} "14:32"     {batt} "18%"
  // One that has nothing to give takes a space with it. alt: nullptr without
  // one, age_min: how old a LAST_KNOWN is, -1 not given, time: "" when the
  // clock isn't set, batt_pct: -1 unknown. An unknown {x} stays as written.
  static void compose(char* out, int n, const char* tmpl, Where where, int32_t lat, int32_t lon,
                      const int16_t* alt, int age_min, const char* time, int batt_pct) {
    if (n <= 0) return;
    char loc[48];
    if (where == NOWHERE) snprintf(loc, sizeof(loc), "no GPS");
    else {
      int l = snprintf(loc, sizeof(loc), "%s%.5f,%.5f", where == LAST_KNOWN ? "last " : "", lat / 1e6, lon / 1e6);
      if (where == LAST_KNOWN && age_min >= 0) snprintf(loc + l, sizeof(loc) - l, " %dmin ago", age_min);
    }
    char alt_s[8] = "", batt_s[8] = "";
    if (alt && where == FIX) snprintf(alt_s, sizeof(alt_s), "%dm", (int)*alt);
    if (batt_pct >= 0) snprintf(batt_s, sizeof(batt_s), "%d%%", batt_pct);
    if (!time) time = "";
    if (!tmpl) tmpl = "";

    int o = 0;
    auto put = [&](const char* s, int len) {
      while (len-- > 0 && o < n - 1) out[o++] = *s++;
    };
    put(SOS_MSG_TAG, (int)strlen(SOS_MSG_TAG));
    put(" ", 1);
    if (!strstr(tmpl, "{loc}")) { put(loc, (int)strlen(loc)); put(" ", 1); }
    for (const char* p = tmpl; *p; ) {
      const char* val = nullptr;
      int skip = 0;
      if (*p == '{') {
        if      (!strncmp(p, "{loc}", 5))  { val = loc;    skip = 5; }
        else if (!strncmp(p, "{alt}", 5))  { val = alt_s;  skip = 5; }
        else if (!strncmp(p, "{time}", 6)) { val = time;   skip = 6; }
        else if (!strncmp(p, "{batt}", 6)) { val = batt_s; skip = 6; }
      }
      if (!val) { put(p++, 1); continue; }
      p += skip;
      if (val[0]) put(val, (int)strlen(val));
      else while (*p == ' ') p++;
    }
    out[o] = '\0';
    o = mesh::validUtf8PrefixLength(out, o);   // too long: cut on a character
    while (o > 0 && out[o - 1] == ' ') o--;
    out[o] = '\0';
  }

  // Anywhere to send to. A channel or a contact that has gone clears its own
  // target (chanctl::onRemoved, UiCore::cleanupContactPrefs).
  static bool hasTargets(const NodePrefs* p) {
    if (!p) return false;
    return p->sos_ch_mask || contacts(p) > 0;
  }
  static int channels(const NodePrefs* p) {
    int on = 0;
    for (uint64_t m = p->sos_ch_mask; m; m &= m - 1) on++;
    return on;
  }
  static int contacts(const NodePrefs* p) {
    int on = 0;
    for (int i = 0; i < NodePrefs::SOS_DM_MAX; i++) if (hasDm(p, i)) on++;
    return on;
  }
  static bool hasDm(const NodePrefs* p, int slot) {
    for (int b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++) if (p->sos_dm[slot][b]) return true;
    return false;
  }

private:
  enum State : uint8_t { IDLE, COUNTDOWN, ACTIVE };
  static bool isWordByte(char c) { return isalnum((uint8_t)c) || (uint8_t)c >= 0x80; }
  uint32_t countdownMs() const {
    return (uint32_t)NodePrefs::sosCountdownSecs(_prefs ? _prefs->sos_countdown_idx : 1) * 1000UL;
  }
  uint32_t repeatMs() const {
    return _prefs ? (uint32_t)NodePrefs::sosRepeatMins(_prefs->sos_repeat_idx) * 60000UL : 0;
  }
  // A send that didn't go out is tried again soon; otherwise Repeat's gap (Off: none).
  bool repeating() const { return !_acked && _attempts && _attempts < MAX_SENDS && (_failed || repeatMs()); }
  uint32_t nextGap() const { return _failed ? RETRY_MS : repeatMs(); }
  uint32_t beaconMs() const {
    const uint32_t gap = repeatMs();
    return gap ? gap : BEACON_MS;
  }

  const NodePrefs* _prefs = nullptr;
  State    _state = IDLE;
  uint32_t _t0 = 0, _active_at = 0, _sent_at = 0, _beep_at = 0, _beacon_at = 0;   // millis() of each last
  uint8_t  _attempts = 0, _sent = 0;
  bool     _acked = false, _with_fix = false, _failed = false, _beeped = false;
  Responder _resp[RESPONDERS_MAX];
  uint8_t  _responders = 0;
};
