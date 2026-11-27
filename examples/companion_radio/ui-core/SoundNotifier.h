#pragma once
// Which sound an incoming message / advert plays, shared by ui-new's and
// ui-lvgl's notify(): the global Sound settings, the per-contact / per-channel
// alert (default / muted / always) and melody overrides, the user melodies
// (slot 1 / 2) with a built-in fallback. Kept out of UITask::notify() itself so
// upstream changes there don't conflict.
#ifdef PIN_BUZZER
#include <helpers/ui/buzzer.h>
#include "../NodePrefs.h"
#include "../AbstractUITask.h"   // UIEventType

class SoundNotifier {
  genericBuzzer&   _buz;
  const NodePrefs* _prefs;
  char*            _mel_buf;
  int              _mel_buf_sz;

  static void buildMelody(const NodePrefs* p, int slot, char* buf, int size) {
    const uint8_t* notes = (slot == 2) ? p->ringtone2_notes   : p->ringtone_notes;
    uint8_t        len   = (slot == 2) ? p->ringtone2_len      : p->ringtone_len;
    uint8_t        bpm_i = (slot == 2) ? p->ringtone2_bpm_idx  : p->ringtone_bpm_idx;
    NodePrefs::buildRTTTLString(notes, len, bpm_i, buf, size);
  }

  // Play custom slot (1/2) or fall back to built-in RTTTL. Slot 3 = explicit silence.
  void playSlot(int slot, bool force, const char* fallback) {
    if (slot == 3) return;
    if (slot > 0 && _prefs) {
      if (_buz.isPlaying()) _buz.stop();
      buildMelody(_prefs, slot, _mel_buf, _mel_buf_sz);
      if (_mel_buf[0]) {
        if (force) _buz.playForced(_mel_buf); else _buz.play(_mel_buf);
        return;
      }
    }
    if (force) _buz.playForced(fallback); else _buz.play(fallback);
  }

public:
  SoundNotifier(genericBuzzer& buz, const NodePrefs* prefs, char* buf, int sz)
    : _buz(buz), _prefs(prefs), _mel_buf(buf), _mel_buf_sz(sz) {}

  void playDM(bool dm_valid, const uint8_t* dm_prefix) {
    bool play = false, force = false;
    if (dm_valid && _prefs) {
      uint8_t state = 0;
      for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++) {
        if (_prefs->dm_notif[i].state &&
            memcmp(_prefs->dm_notif[i].prefix, dm_prefix, 4) == 0)
          { state = _prefs->dm_notif[i].state; break; }
      }
      if (state == 2) { play = true; force = true; }
      else if (state == 1) { /* muted */ }
      else { play = !_buz.isQuiet(); }
    } else {
      play = !_buz.isQuiet();
    }
    if (!play) return;

    int slot = _prefs ? (int)_prefs->notif_melody_dm : 0;
    if (dm_valid && _prefs) {
      for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++)
        if (_prefs->dm_melody[i].slot &&
            memcmp(_prefs->dm_melody[i].prefix, dm_prefix, 4) == 0)
          { slot = _prefs->dm_melody[i].slot; break; }
    }
    playSlot(slot, force, "MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
  }

  void playCH(int ch_idx) {
    bool play = false, force = false;
    if (ch_idx >= 0 && ch_idx < 64 && _prefs) {
      uint64_t mask = 1ULL << ch_idx;
      if (_prefs->ch_notif_override & mask) {
        if (!(_prefs->ch_notif_muted & mask)) { play = true; force = true; }
      } else {
        play = !_buz.isQuiet();
      }
    } else {
      play = !_buz.isQuiet();
    }
    if (!play) return;

    int slot = _prefs ? (int)_prefs->notif_melody_ch : 0;
    if (ch_idx >= 0 && ch_idx < 64 && _prefs) {
      uint64_t mask = 1ULL << ch_idx;
      if (_prefs->ch_notif_melody_set & mask)
        slot = (_prefs->ch_notif_melody_2 & mask) ? 2 : 1;
    }
    playSlot(slot, force, "kerplop:d=16,o=6,b=120:32g#,32c#");
  }

  void playAD(bool is_flood) {
    if (_prefs && _prefs->advert_sound_scope == ADVERT_SOUND_SCOPE_ZERO_HOP && is_flood) return;
    if (_buz.isQuiet()) return;
    int slot = _prefs ? (int)_prefs->notif_melody_ad : 0;
    playSlot(slot, false, "MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
  }

  // The sound for event t. The pending override of the DM (dm_valid,
  // dm_prefix) or channel (ch_idx) it is for is used up here.
  void play(UIEventType t, bool& dm_valid, const uint8_t* dm_prefix, int& ch_idx) {
    switch (t) {
      case UIEventType::contactMessage: playDM(dm_valid, dm_prefix); dm_valid = false; break;
      case UIEventType::channelMessage: playCH(ch_idx); ch_idx = -1; break;
      // Rooms have many authors and no per-room melody: the default DM sound
      // (no per-sender melody/mute lookup -- the author varies per post).
      case UIEventType::roomMessage:    playDM(false, nullptr); break;
      case UIEventType::advertReceivedFlood:
      case UIEventType::advertReceivedZeroHop: playAD(t == UIEventType::advertReceivedFlood); break;
      case UIEventType::ack:            _buz.play("ack:d=32,o=8,b=120:c"); break;
      default: break;
    }
  }
};
#endif
