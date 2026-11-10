#pragma once
// Sound settings and melodies, shared by ui-new (Settings > Sound, Tools >
// Ringtone) and ui-lvgl (Settings > Sound, Melodies): the On / Off / Auto mode
// (buzzer_quiet + buzzer_auto), the volume, the built-in sounds, the labels of
// the notification sound choices, and the two user melodies (ringtone_* /
// ringtone2_*) as an editable note list. Quiet hours mute on a schedule. Which sound a message plays is
// SoundNotifier.h.

#include "../NodePrefs.h"
#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

namespace soundctl {

// ── Built-in sounds (RTTTL) ───────────────────────────────────────────────────
static const char* const MEL_ALARM  = "alarm:d=8,o=6,b=125:c,c,c,c,p,c,c,c,c,p";   // clock alarm / timer ring
static const char* const MEL_ARRIVE = "locarr:d=8,o=6,b=140:c,e,g";   // arrival alert: reached the target
static const char* const MEL_LEAVE  = "loclv:d=8,o=6,b=140:g,e,c";    // ... left its radius
static const char* const MEL_TICK   = "locp:d=32,o=7,b=200:c";        // proximity beeper tick
static const char* const MEL_SOS_TICK = "sostick:d=16,o=7,b=200:e";   // the SOS countdown: higher and longer than a tick, near a piezo's loudest
static const char* const MEL_SOS    = "sos:d=16,o=7,b=200:e,p,e,p,e,8p,8e,p,8e,p,8e,8p,e,p,e,p,e";   // ... --- ... : to be found by
static const char* const MEL_VOLUME = "Vol:d=16,o=6,b=120:c";         // volume preview

// ── Mode ──────────────────────────────────────────────────────────────────────
// Auto: muted while an app is connected (it notifies instead).
enum Mode : uint8_t { MODE_ON, MODE_OFF, MODE_AUTO, MODE_COUNT };

static uint8_t mode(const NodePrefs* p) {
  if (!p) return MODE_ON;
  if (p->buzzer_auto) return MODE_AUTO;
  return p->buzzer_quiet ? MODE_OFF : MODE_ON;
}
static const char* modeLabel(uint8_t m) {
  static const char* L[MODE_COUNT] = { "On", "Off", "Auto" };
  return L[m < MODE_COUNT ? m : 0];
}

// notif_melody_dm / _ch / _ad: 0 = built-in, 1 / 2 = the user melodies, 3 = none.
static const uint8_t SOUND_COUNT = 4;
static const char* soundLabel(uint8_t v) {
  static const char* L[SOUND_COUNT] = { "Built-in", "Melody 1", "Melody 2", "None" };
  return L[v < SOUND_COUNT ? v : 0];
}
// A contact's / channel's melody override: 0 = its global sound.
static const uint8_t OVERRIDE_COUNT = 3;

#ifdef PIN_BUZZER
// Quiet hours (NodePrefs::inQuietHours) mute like the mode does. A mute or
// unmute by hand inside the window stands until the window ends.
static bool s_quiet_now = false;      // inside the window, as of the last tick
static bool s_quiet_override = false; // set by hand since the window began

// The mute that goes with the mode and quiet hours; `connected`: an app is
// connected.
static bool wantQuiet(const NodePrefs* p, bool connected) {
  uint8_t m = mode(p);
  if (m == MODE_OFF || (m == MODE_AUTO && connected)) return true;
  return s_quiet_now && !s_quiet_override;
}
static void applyMode(const NodePrefs* p, genericBuzzer& b, bool connected, uint32_t utc) {
  s_quiet_now = p && inQuietHours(*p, utc);
  b.quiet(wantQuiet(p, connected));
}

// Auto keeps buzzer_quiet (the manual choice it goes back to). The caller
// saves the prefs.
static void setMode(NodePrefs* p, genericBuzzer& b, uint8_t m, bool connected) {
  if (!p || m >= MODE_COUNT) return;
  p->buzzer_auto = m == MODE_AUTO;
  if (m != MODE_AUTO) p->buzzer_quiet = m == MODE_OFF;
  s_quiet_override = s_quiet_now;   // a choice made during quiet hours wins until they end
  b.quiet(wantQuiet(p, connected));
}

// From the loop: Auto follows the app connection, quiet hours the clock.
// True when the mute changed.
static bool tick(const NodePrefs* p, genericBuzzer& b, bool connected, uint32_t utc) {
  if (!p) return false;
  bool q = inQuietHours(*p, utc);
  if (q != s_quiet_now) { s_quiet_now = q; s_quiet_override = false; }
  bool want = wantQuiet(p, connected);
  if (b.isQuiet() == want) return false;
  b.quiet(want);
  return true;
}
// Level 0..4, with a preview beep (through mute: it answers the user's own
// change). The caller saves the prefs.
static void setVolume(NodePrefs* p, genericBuzzer& b, uint8_t level) {
  if (level > 4) level = 4;
  if (p) p->buzzer_volume = level;
  b.setVolume(level);
  b.playForced(MEL_VOLUME);
}
#endif

// ── User melodies ─────────────────────────────────────────────────────────────
// A note is one byte: bits 0-2 pitch (0 = rest, 1..7 = c d e f g a b), bits
// 3-4 octave - 4, bits 5-6 duration (1/4, 1/8, 1/16, 1/32).
static const int MAX_NOTES = 32;
static const uint8_t BPM_COUNT = 5, DUR_COUNT = 4, PITCH_COUNT = 8;
static const uint8_t OCT_MIN = 4, OCT_MAX = 6;

static uint16_t bpm(uint8_t idx) {
  static const uint16_t B[BPM_COUNT] = { 60, 90, 120, 150, 180 };
  return B[idx < BPM_COUNT ? idx : 2];
}
static const char* durLabel(uint8_t d) {
  static const char* L[DUR_COUNT] = { "1/4", "1/8", "1/16", "1/32" };
  return L[d & 3];
}
static char pitchChar(uint8_t p) {   // lowercase RTTTL name, 'p' = rest
  static const char P[PITCH_COUNT] = { 'p', 'c', 'd', 'e', 'f', 'g', 'a', 'b' };
  return P[p & 7];
}

static uint8_t notePitch(uint8_t b)  { return b & 0x07; }
static uint8_t noteOctave(uint8_t b) { return ((b >> 3) & 0x03) + 4; }
static uint8_t noteDur(uint8_t b)    { return (b >> 5) & 0x03; }
static uint8_t packNote(uint8_t pitch, uint8_t octave, uint8_t dur) {
  return (pitch & 0x07) | (((octave - 4) & 0x03) << 3) | ((dur & 0x03) << 5);
}
static const uint8_t NEW_NOTE = (1 & 0x07) | ((5 - 4) << 3) | (1 << 5);   // C5, 1/8

// "C5", or "--" for a rest.
static void noteName(uint8_t b, char out[3]) {
  uint8_t p = notePitch(b);
  out[0] = p ? (char)(pitchChar(p) - 32) : '-';
  out[1] = p ? (char)('0' + noteOctave(b)) : '-';
  out[2] = '\0';
}

struct Melody {
  uint8_t notes[MAX_NOTES];
  uint8_t len;
  uint8_t bpm_idx;
};

// slot 0 = melody 1, 1 = melody 2.
static void load(const NodePrefs* p, int slot, Melody& m) {
  memset(&m, 0, sizeof(m));
  m.bpm_idx = 2;
  if (!p) return;
  bool s2 = slot == 1;
  uint8_t b = s2 ? p->ringtone2_bpm_idx : p->ringtone_bpm_idx;
  uint8_t n = s2 ? p->ringtone2_len : p->ringtone_len;
  m.bpm_idx = b < BPM_COUNT ? b : 2;
  m.len = n <= MAX_NOTES ? n : 0;
  memcpy(m.notes, s2 ? p->ringtone2_notes : p->ringtone_notes, MAX_NOTES);
}

// The caller saves the prefs.
static void store(NodePrefs* p, int slot, const Melody& m) {
  if (!p) return;
  if (slot == 1) {
    p->ringtone2_bpm_idx = m.bpm_idx; p->ringtone2_len = m.len;
    memcpy(p->ringtone2_notes, m.notes, MAX_NOTES);
  } else {
    p->ringtone_bpm_idx = m.bpm_idx; p->ringtone_len = m.len;
    memcpy(p->ringtone_notes, m.notes, MAX_NOTES);
  }
}

static void toRtttl(const Melody& m, char* buf, int n) {
  NodePrefs::buildRTTTLString(m.notes, m.len, m.bpm_idx, buf, n);
}

// A short blip of one note, for editing ("" for a rest).
static void notePreview(uint8_t b, char* buf, int n) {
  uint8_t p = notePitch(b);
  if (!p) { if (n) buf[0] = '\0'; return; }
  snprintf(buf, n, "P:d=16,o=5,b=240:%c%d", pitchChar(p), noteOctave(b));
}

// `note` at index `at` (0..len). False when full.
static bool insertNote(Melody& m, int at, uint8_t note) {
  if (m.len >= MAX_NOTES || at < 0 || at > m.len) return false;
  for (int i = m.len; i > at; i--) m.notes[i] = m.notes[i - 1];
  m.notes[at] = note;
  m.len++;
  return true;
}

static bool removeNote(Melody& m, int at) {
  if (at < 0 || at >= m.len) return false;
  for (int i = at; i < m.len - 1; i++) m.notes[i] = m.notes[i + 1];
  m.len--;
  return true;
}

}  // namespace soundctl
