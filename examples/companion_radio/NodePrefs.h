#pragma once
#include <cstdint>
#include <stdio.h>

// Firmware's own boot-default radio params — used both as the companion's
// initial freq/sf/bw/cr (MyMesh.cpp) and, here, as the seed for a never-
// configured repeater profile (DataStore.cpp). Kept above any per-board
// target.h include so a board can still override via -D before this file
// is reached.
#ifndef LORA_FREQ
#define LORA_FREQ 915.0
#endif
#ifndef LORA_BW
#define LORA_BW 250
#endif
#ifndef LORA_SF
#define LORA_SF 10
#endif
#ifndef LORA_CR
#define LORA_CR 5
#endif

// Bucket a companion frequency into whichever of the three license-exempt
// bands MeshCore's app-driven repeat toggle historically restricted to (see
// repeat_freq_ranges in MyMesh.cpp: 433.000 / 869.495 / 918.000 MHz). Used to
// seed a never-configured repeater profile in the same legal band as the
// companion's own network (MyMesh.cpp begin(), DataStore.cpp) — a flat
// single frequency could land outside the bands allowed where the operator
// actually lives.
static inline float defaultRepeaterFreqForBand(float companion_freq) {
  if (companion_freq < 500.0f) return 433.000f;
  if (companion_freq < 890.0f) return 869.495f;
  return 918.000f;
}

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

#define ADVERT_SOUND_SCOPE_ALL        0
#define ADVERT_SOUND_SCOPE_ZERO_HOP   1

struct NodePrefs {  // persisted to file
  // Fields below are grouped thematically for readability. This grouping is
  // purely a source-level convenience: on-disk order is defined solely by the
  // explicit rd()/wr() call sequence in DataStore::loadPrefsInt()/savePrefs(),
  // not by this struct's declaration order, so reordering fields here never
  // touches the file format. That file format is still strictly append-only
  // (see the serialization tripwire below) — a handful of fields below note
  // that their on-disk position is at the struct's tail even though they're
  // declared here beside their logical siblings.

  // ── Identity & scope ──────────────────────────────────────────────────
  char node_name[32];
  char default_scope_name[31];
  uint8_t default_scope_key[16];

  // ── Radio (companion's own link) ──────────────────────────────────────
  float airtime_factor;
  float freq;
  uint8_t sf;
  uint8_t cr;
  float bw;
  int8_t tx_power_dbm;
  float rx_delay_base;
  uint8_t multi_acks;
  uint8_t path_hash_mode;    // which path mode to use when sending
  // Hardware duty-cycle receive (battery saver): 0=continuous RX (default), 1=on.
  // The SX126x cycles RX↔sleep on its own and wakes on a preamble — cuts average
  // RX current at the cost of a little receive latency. See RadioLibWrapper
  // power-save (startReceiveDutyCycleAuto).
  uint8_t  rx_powersave;
  uint8_t rx_boosted_gain; // SX126x RX boosted gain mode (0=power saving, 1=boosted)
  // Adaptive Power Control: 0=off (fixed tx_power_dbm, default), 1=on. When on,
  // tx_power_dbm is treated as a ceiling and the radio's actual power is lowered
  // at runtime on strong links (good ACK SNR), saving TX energy. Never persisted
  // below the ceiling, so disabling restores the user's configured power.
  uint8_t  tx_apc;
  // RSSI-based interference detection (relative to the radio's own noise
  // floor) and hardware Channel Activity Detection before TX. See
  // MyMesh::getInterferenceThreshold()/getCADEnabled() — CAD is also
  // auto-enabled whenever rx_powersave is active, regardless of this flag,
  // since duty-cycle sleep leaves the noise floor stale. Both default 0/off,
  // and neither has a Settings/CLI toggle yet on companion_radio — persisted
  // and wired for a future manual override.
  uint8_t  interference_threshold;
  uint8_t  cad_enabled;
  // Auto-resend for on-device DMs: number of extra send attempts (0..5) made when
  // no end-to-end ACK arrives before the deadline, before the delivery marker
  // shows ✗. 0 = no auto-resend (single attempt). Default 2.
  uint8_t  dm_resend_count;
  // External LoRa FEM gain (LNA/PA), from upstream companion-v1.17.1 —
  // board-level only right now (BaseCustomBoard::setLoRaFemLnaEnabled/
  // PaGainEnabled, both default no-op false), no companion CLI/UI to set
  // these yet, so — matching upstream's own decision — NOT persisted here:
  // always reset to the constructor default (MyMesh.cpp) on boot. Not part
  // of the DataStore save/load tripwire below.
  uint8_t radio_fem_rxgain;
  uint8_t radio_fem_txgain;
  // User-saved radio presets, written by the "Save current..." entry in the
  // shared preset picker (Settings > Radio and Tools > Repeater both populate
  // these same slots). name[0] == '\0' marks an empty slot.
  struct UserRadioPreset {
    char name[16];
    float freq;
    float bw;
    uint8_t sf;
    uint8_t cr;
  };
  static const uint8_t USER_RADIO_PRESET_MAX = 4;
  UserRadioPreset user_radio_presets[USER_RADIO_PRESET_MAX];

  // ── Repeater ───────────────────────────────────────────────────────────
  uint8_t client_repeat;
  // Repeater forwarding filters — only consulted when client_repeat is on, via
  // MyMesh::allowPacketForward(). Both default to off (0) so behaviour is
  // unchanged until the user opts in (Tools > Repeater).
  //  repeat_skip_adverts: 1 = don't re-flood ADVERT packets (the highest-volume
  //    flood traffic); messages/acks still relay.
  //  repeat_max_hops: drop a flood packet once it has already travelled this
  //    many hops. 0 = no hop limit.
  //  repeat_delay_boost: extra retransmit-delay multiplier for FORWARDED floods
  //    only (own sends are unaffected) — a mobile companion yields to better-sited
  //    fixed repeaters. Effective delay = base * (1 + repeat_delay_boost). 0 = off.
  //  repeat_min_snr: drop a flood packet received below this SNR (dB), so marginal
  //    fringe traffic isn't re-flooded. REPEAT_SNR_DISABLED (-128) = off.
  //  repeat_suppress_dup: 1 = cancel a queued retransmit when the same flood is
  //    overheard from another node first (less redundant airtime in dense mesh).
  uint8_t  repeat_skip_adverts;
  uint8_t  repeat_max_hops;
  uint8_t  repeat_delay_boost;
  int8_t   repeat_min_snr;
  static const int8_t REPEAT_SNR_DISABLED = -128;
  uint8_t  repeat_suppress_dup;
  // On-disk position is at the struct's append-only tail (see the
  // serialization tripwire below), even though grouped here with the rest of
  // repeat_* for readability.
  //  repeat_scope_only: 1 = only forward flood packets matching this device's
  //    own scope (Settings > Radio > Scope, default_scope_key) or one of the
  //    repeat_extra_scopes below — drops unscoped floods and floods tagged for
  //    a different community. A no-op (forwards everything, unchanged) while
  //    no scope is configured at all, so enabling this on an unconfigured
  //    device can't silently blackhole all flood traffic.
  uint8_t  repeat_scope_only;
  // Extra region names this repeater also relays for, beyond its own
  // Settings > Radio > Scope (comma-separated, e.g. "eu,de") — see
  // MyMesh::rebuildRepeatScopes(). Relay-only: never affects what scope the
  // companion's own messages send under, only what repeat_scope_only accepts.
  // Superseded by repeat_extra_scope_mask below (a scope-list toggle,
  // replacing free-typed names); left allocated/unused rather than removed
  // so the on-disk layout of every field after it stays put.
  char     repeat_extra_scopes[24];
  // On-disk position is at the struct's append-only tail (0xC0DE002B), even
  // though grouped here with the rest of repeat_*. Bit i = scope-list index
  // (i+1) is in this repeater's accept set (index 0, "*", isn't a real scope
  // so isn't toggleable here) — see ScopeList/MyMesh::rebuildRepeatScopes().
  // Same MAX_REPEAT_SCOPES=4 runtime cap as before, just resolved from the
  // shared named-scope list instead of comma-tokenizing repeat_extra_scopes.
  uint16_t repeat_extra_scope_mask;
  // Optional dedicated radio profile for repeater mode. When repeater_use_profile
  // is 1, enabling the repeater switches the radio to repeater_freq/bw/sf/cr and
  // disabling restores the companion's freq/bw/sf/cr (the fields above). 0 = the
  // repeater stays on the current companion frequency. Default (a never-configured
  // device) is 1, seeded via defaultRepeaterFreqForBand(freq) + LORA_SF/BW/CR —
  // repeating on whatever private network the companion later joins isn't the
  // community norm, and the seed stays in the same legal band as the companion's
  // own network rather than a flat frequency.
  uint8_t  repeater_use_profile;
  float    repeater_freq;
  float    repeater_bw;
  uint8_t  repeater_sf;
  uint8_t  repeater_cr;

  // ── Telemetry ──────────────────────────────────────────────────────────
  uint8_t telemetry_mode_base;
  uint8_t telemetry_mode_loc;
  uint8_t telemetry_mode_env;

  // ── Contacts (auto-add) ───────────────────────────────────────────────
  uint8_t manual_add_contacts;
  uint8_t autoadd_config;    // bitmask for auto-add contacts config
  uint8_t autoadd_max_hops;  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)

  // ── Connectivity (BLE) ────────────────────────────────────────────────
  uint32_t ble_pin;

  // ── Power & battery ───────────────────────────────────────────────────
  uint16_t low_batt_mv;         // auto-shutdown threshold: 0=disabled, 3000-3500 mV
  uint8_t batt_display_mode;   // 0=icon, 1=percent, 2=voltage

  // ── Display & keyboard ────────────────────────────────────────────────
  uint8_t display_brightness;   // 0=min..4=max, default 2 (medium)
  uint16_t auto_off_secs;       // display auto-off: 0=never, else seconds (default 15)
  uint8_t  auto_lock;           // 0=disabled, 1=lock screen when display turns off
  int8_t tz_offset_hours;       // timezone offset from UTC, -12..+14 (default 0)
  uint8_t  use_lemon_font;      // 0=default Adafruit font, 1=Lemon font (Unicode, pixel-accurate wrap)
  uint8_t  display_rotation;    // 0-3; only used on e-ink displays
  uint8_t  joystick_rotation;   // 0-3 steps CW; independent of display_rotation
  uint8_t  eink_full_refresh_every; // index into {0,5,10,20,30}: full refresh every N partials (0=off)
  uint16_t home_pages_mask;    // bitmask of visible home pages (bit0=Clock..bit8=Shutdown); 0=all visible
  uint8_t  dashboard_fields[3]; // 0=None,1=Batt V,2=Temp,3=Hum,4=Pres,5=GPS,6=Alt,7=Lux,8=CO2,9=Nodes,10=Msgs,11=Batt %
  // Home screen page order: each byte = HomePageBit + 1. 0 terminates the list.
  // Validity gated by page_order_set magic (see below) — not by entry value range,
  // so a junk byte in 1..HPB_COUNT cannot trigger custom-order mode.
  // Declared as a literal (not HPB_COUNT) so the field offset stays stable across
  // builds that add HomePageBit entries. The first PAGE_ORDER_LEN_V1 bytes persist
  // at this offset (backward-compatible); the remaining slots live at the file
  // tail (see DataStore) so pre-0x0019 saves still load without shifting.
  uint8_t  page_order[13];
  uint8_t  page_order_set;      // 0xA5 = page_order is user-configured; anything else = use default
  static const uint8_t PAGE_ORDER_MAGIC = 0xA5;
  // On-screen keyboard layout, shared across every text-entry screen (Settings >
  // Keyboard). 0=ABC grid, alphabetical order (default), 1=T9 multi-tap
  // (phone-keypad groups, cycled with repeated Enter presses — see KeyboardWidget.h).
  uint8_t  keyboard_type;
  // Additional (non-Latin) keyboard alphabet, orthogonal to keyboard_type above
  // — either layout style (ABC grid or T9) can show any alphabet's characters.
  // 0 = Latin only (default): the keyboard's existing #@/abc page-cycle key
  // toggles between just the Latin letters page and the symbols page, as
  // today. A non-zero value adds that alphabet's own page to the same cycle
  // key (Latin → alphabet → symbols → Latin), so composing in it needs no new
  // key, just Settings › Keyboard › Alphabet to pick which one is available.
  // See KeyboardWidget.h for the per-alphabet grids (KB_CYRILLIC_CHARS etc.)
  // and the misc-fixed font (src/helpers/ui/MiscFixedFont.h) for on-screen
  // rendering — every alphabet here must be in its U+0020-04FF range.
  //
  // Latin-diacritic letters (Polish/Czech/Slovak/German/French/Spanish/
  // Portuguese/Nordic) used to each be their own full alt-alphabet page here;
  // they're now reached instead by holding Enter on a Latin letter that has
  // accented variants (see KeyboardWidget.h's KB_ACCENT_VARIANTS), so this
  // enum only covers actual non-Latin scripts. Not a schema change: an old
  // saved value of 3+ (one of the removed languages) just clamps to 0 (Latin)
  // via DataStore.cpp's existing `>= KB_ALPHABET_COUNT` range check.
  static const uint8_t KB_ALPHABET_LATIN_ONLY = 0;
  static const uint8_t KB_ALPHABET_CYRILLIC   = 1;
  static const uint8_t KB_ALPHABET_GREEK      = 2;
  static const uint8_t KB_ALPHABET_COUNT      = 3;
  static const char* keyboardAlphabetLabel(uint8_t idx) {
    static const char* L[KB_ALPHABET_COUNT] = { "Latin", "Cyrillic", "Greek" };
    return L[idx < KB_ALPHABET_COUNT ? idx : 0];
  }
  // On-disk position is at the struct's append-only tail (see the
  // serialization tripwire below); declared here with keyboard_type for
  // readability.
  uint8_t  keyboard_alt_alphabet;
  // Which script (KB_ALPHABET_LATIN_ONLY/CYRILLIC/GREEK) occupies the on-screen
  // keyboard's page 0 -- its default/opening page -- vs. keyboard_alt_alphabet
  // above, which occupies page 1. Settings > Keyboard's Main/Additional rows.
  // Equal to keyboard_alt_alphabet means no second page (see KeyboardWidget's
  // hasAltAlphabet()). On-disk position is at the tail (see the serialization
  // tripwire below); default 0 (Latin) matches the keyboard's original always-
  // Latin-main behaviour for upgraders.
  uint8_t  keyboard_main_alphabet;
  // Settings > Keyboard's "Ext. KB" row (boards with a second I2C bus for an
  // optional CardKB, see ENV_PIN_SDA/ENV_PIN_SCL, only). When on, the
  // on-screen keyboard skips drawing its full letter grid + special-row icons
  // -- an external-keyboard typist never looks at them -- and shows a compact
  // one-line status (current script/page, caps) instead; the accent and
  // placeholder popups still render on top exactly as before (see
  // KeyboardWidget::render()). Manual toggle rather than auto-detected, so it
  // stays put even if the module is briefly unplugged. Default 0 (full grid,
  // unchanged behaviour) on upgrade.
  uint8_t  keyboard_cardkb_compact;

  // ── Clock & alarm ─────────────────────────────────────────────────────
  uint8_t  clock_hide_seconds; // 0=show HH:MM:SS/refresh 1s (default), 1=hide/refresh 60s
  uint8_t  clock_12h;          // 0=24h (default), 1=12h with AM/PM
  // Alarm clock — a wake alarm, configured from the Clock page (Enter › Alarm).
  // Stored as a local time-of-day; the actual fire instant is (re)computed as an
  // absolute time in tickBackground(), which is what makes it robust to RTC
  // re-syncs: the mesh (every inbound packet), the companion app, GPS and the
  // CLI can all jump getCurrentTime() at any moment, but an absolute target
  // instant stays correct across small corrections and still fires (late) if
  // the clock jumps over it. With alarm_repeat_mask == 0 it's one-shot: fires
  // once, then alarm_on clears. A non-zero mask instead re-arms it for the next
  // matching weekday (see UITask::computeAlarmNextFire/evaluateAlarm) and
  // alarm_on stays set. The minutnik (countdown) and stoper (stopwatch) are
  // runtime-only and not persisted.
  uint8_t  alarm_on;    // 0=off (default), 1=armed
  uint8_t  alarm_hour;  // 0-23, local time
  uint8_t  alarm_min;   // 0-59
  // Repeat-days bitmask — see the alarm doc comment above for the full
  // explanation (bit i = 1<<tm_wday). On-disk position is at the struct's
  // append-only tail (see the serialization tripwire below); grouped here
  // with alarm_on/hour/min purely for source readability.
  uint8_t  alarm_repeat_mask;
  static const uint8_t ALARM_REPEAT_NONE     = 0x00;  // one-shot (default)
  static const uint8_t ALARM_REPEAT_DAILY    = 0x7F;  // every day
  static const uint8_t ALARM_REPEAT_WEEKDAYS = 0x3E;  // Mon-Fri
  static const uint8_t ALARM_REPEAT_WEEKENDS = 0x41;  // Sat+Sun
  static const uint8_t ALARM_REPEAT_COUNT    = 4;
  static uint8_t alarmRepeatMaskForIdx(uint8_t idx) {
    static const uint8_t M[ALARM_REPEAT_COUNT] =
      { ALARM_REPEAT_NONE, ALARM_REPEAT_DAILY, ALARM_REPEAT_WEEKDAYS, ALARM_REPEAT_WEEKENDS };
    return M[idx < ALARM_REPEAT_COUNT ? idx : 0];
  }
  // Reverse lookup for display: an arbitrary mask (e.g. loaded from a future
  // custom-day picker) that doesn't match a preset just reads as "OFF" here —
  // it still fires correctly, computeAlarmNextFire() reads the raw mask.
  static uint8_t alarmRepeatIdxForMask(uint8_t mask) {
    switch (mask) {
      case ALARM_REPEAT_DAILY:    return 1;
      case ALARM_REPEAT_WEEKDAYS: return 2;
      case ALARM_REPEAT_WEEKENDS: return 3;
      default:                    return 0;
    }
  }
  static const char* alarmRepeatLabel(uint8_t idx) {
    static const char* L[ALARM_REPEAT_COUNT] = { "OFF", "Daily", "Weekdays", "Weekends" };
    return L[idx < ALARM_REPEAT_COUNT ? idx : 0];
  }

  // ── Buzzer & notifications ────────────────────────────────────────────
  uint8_t  buzzer_quiet;
  uint8_t  buzzer_volume;   // 0=min..4=max, default 4
  uint8_t  buzzer_auto;        // 0=manual (default), 1=auto-mute when BT connected
  // Settings > Display > "Msg wake". Stored inverted (same reason as
  // fav_sort_off below) so both a fresh memset and an older prefs file (no
  // bytes here at all) mean "on" -- today's behaviour, where an incoming
  // message turns the display on (UITask::newMsg()) if it was off and no
  // companion app is already showing it.
  uint8_t  msg_wake_screen_off; // 0=wake display for incoming msgs (default), 1=disabled
  uint8_t  ringtone_bpm_idx;   // index into {60,90,120,150,180}
  uint8_t  ringtone_len;        // number of notes in custom ringtone (0 = use default)
  uint8_t  ringtone_notes[32]; // packed: bits0-2=pitch, bits3-4=octave-4, bits5-6=dur_idx
  // Second melody slot (same packing as ringtone_*)
  uint8_t  ringtone2_bpm_idx;
  uint8_t  ringtone2_len;
  uint8_t  ringtone2_notes[32];
  // Global melodies for notifications: 0=built-in, 1=melody1, 2=melody2, 3=none
  uint8_t  notif_melody_dm;
  uint8_t  notif_melody_ch;
  uint8_t  notif_melody_ad;
  uint64_t ch_notif_override;  // bitmask: bit i = channel i has explicit notification setting [del→onChannelRemoved]
  uint64_t ch_notif_muted;     // bitmask: bit i = channel i muted (only if override bit set) [del→onChannelRemoved]
  // Per-channel melody override (2 bitmasks, 1 bit per channel)
  uint64_t ch_notif_melody_set;  // bit i = channel i has explicit melody [del→onChannelRemoved]
  uint64_t ch_notif_melody_2;    // bit i = use melody 2 (else melody 1, when set bit is set)
  // On-disk position is at the struct's append-only tail (0xC0DE002B), even
  // though grouped here with the other per-channel overrides. Scope-list
  // index per channel (see ScopeList) -- 0 ("*"/unscoped) is the correct
  // zero-init default, matching today's unconfigured behaviour exactly, so
  // no migration is needed for this field itself. Fixed at 64 slots (not
  // MAX_GROUP_CHANNELS, which varies by board/variant and would make
  // sizeof(NodePrefs) variant-dependent) -- same implicit channel-count cap
  // every ch_notif_*/ch_fav_bitmask uint64_t bitmask above already has.
  static const uint8_t MAX_SCOPED_CHANNELS = 64;
  uint8_t  ch_scope_idx[MAX_SCOPED_CHANNELS]; // [del→onChannelRemoved]
  struct DmNotifEntry { uint8_t prefix[4]; uint8_t state; }; // state: 0=default,1=muted,2=force-on
  static const int DM_NOTIF_TABLE_MAX = 16;
  DmNotifEntry dm_notif[DM_NOTIF_TABLE_MAX]; // 16*5 = 80 bytes [del→onContactRemoved]
  // Per-DM melody table
  struct DmMelodyEntry { uint8_t prefix[4]; uint8_t slot; }; // slot: 0=global,1=melody1,2=melody2
  static const int DM_MELODY_TABLE_MAX = 16;
  DmMelodyEntry dm_melody[DM_MELODY_TABLE_MAX]; // [del→onContactRemoved]

  // ── Bot ────────────────────────────────────────────────────────────────
  uint8_t  bot_enabled;         // 0=disabled, 1=DM trigger-reply active — DM only; channel/room have their own bot_channel_enabled/bot_room_enabled and don't depend on this
  char     bot_trigger[64];     // DM trigger phrase (case-insensitive contains; "*" = any DM)
  char     bot_reply_dm[140];   // auto-reply text for DM
  uint8_t  bot_commands_enabled; // 0=off, 1=answer !ping/!batt/!loc/!time/!help DM commands — DM only, see bot_commands_ch/bot_commands_room below for the other two targets
  uint8_t  bot_actions_dm;
  // Bot DM allow-list: who is allowed to trigger a DM auto-reply or run a
  // command. 0 = all (default, matches the original bot_enabled behaviour).
  // 1 = favourites only — gate on ContactInfo::flags bit 0, the same
  // "favourite"/starred bit the Messages screen's DM list filter (dm_show_all)
  // already reads, so no separate allow-list storage is needed.
  uint8_t  bot_dm_scope;
  uint8_t  bot_channel_enabled; // 0=disabled, 1=channel bot active for bot_channel_idx
  uint8_t  bot_channel_idx;     // channel index for channel bot [del→onChannelRemoved]
  char     bot_reply_ch[140];   // auto-reply text for channel
  char     bot_trigger_ch[64];  // channel trigger phrase (independent of DM; "*" = any channel msg)
  uint8_t  bot_commands_ch;
  uint8_t  bot_actions_ch;
  // Room-server auto-reply bot — same trigger/reply/command shape as the DM
  // and channel bots above, but targets a single room server (like the
  // channel bot targets a single channel) since posting requires that room's
  // own login session (see MyMesh::sendRoomLogin/logoutRoom). If the device
  // has never logged into this room, replies silently fail to send — same
  // as a manual post would — so log in at least once from Messages > Rooms
  // before relying on the bot there.
  uint8_t  bot_room_enabled;                          // 0=disabled, 1=room bot active for bot_room_prefix
  uint8_t  bot_room_prefix[6];                        // target room's pubkey prefix [del→onContactRemoved]
  char     bot_trigger_room[64];                      // room trigger phrase (independent of DM/channel; "*" = any post)
  char     bot_reply_room[140];                       // auto-reply text for the room
  // Per-target Commands toggle for channel/room, splitting what used to be
  // one bot_commands_enabled shared across all three (see that field's doc
  // comment) — e.g. answer !ping in DM but stay quiet on a public channel.
  // Upgraders: DataStore seeds both from the old shared bot_commands_enabled
  // on first load past the schema bump, so existing behaviour is preserved
  // until the user deliberately splits them apart.
  uint8_t  bot_commands_room;
  // Per-target toggle for bot *action* commands (!buzz/!gps/!advert) --
  // separate from bot_commands_ch/bot_commands_room above, which only gate
  // the read-only query commands (!ping/!batt/...). Nested under that
  // Commands toggle (Commands=off means neither queries nor actions run for
  // that target); Actions=on additionally lets the state-changing commands
  // through. Default 0 (off) -- these change device behaviour remotely, so
  // upgraders don't get them silently enabled.
  uint8_t  bot_actions_room;
  uint8_t  bot_quiet_start;     // quiet-hours start hour, local 0-23 (start==end → disabled)
  uint8_t  bot_quiet_end;       // quiet-hours end hour, local 0-23

  // ── GPS, trail & location sharing ─────────────────────────────────────
  uint8_t  gps_enabled;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval;     // GPS duty-cycle sleep window in seconds (0 = disabled, GPS stays continuous)
  // Global measurement system for every distance/speed shown in the UI
  // (Nearby, Trail, navigate-to-point). 0=metric (default), 1=imperial.
  uint8_t  units_imperial;
  // GPS trail cadence. Logging on/off is a runtime state (Tools › Trail),
  // not a persisted preference.
  uint8_t trail_interval_idx;   // reserved — sampling cadence is now fixed at TrailStore::SAMPLING_SECS
  uint8_t trail_min_delta_idx;  // min-distance gate level (0=finest..3); metres or feet per units_imperial
  uint8_t trail_units_idx;      // legacy: old combined speed/pace+unit index (km/h, mph, min/km, min/mi)
  // Trail Summary readout: 0=speed (km/h or mph), 1=pace (min/km or min/mi).
  // The km-vs-mi choice now comes from units_imperial, so this is just the mode.
  uint8_t  trail_show_pace;
  // Trail auto-pause — when tracking, automatically freeze the trail (timer +
  // sampling) after the device has sat still for this long, and resume on the
  // next real movement. 0 = off. Index into trailAutoPauseSecs.
  uint8_t  trail_autopause_idx;
  // Auto-save the live GPS trail to /trail on shutdown (covers the low-battery
  // auto-shutdown, which otherwise loses the whole route). Only writes when the
  // trail has points and the toggle is on. 0 = off (default), 1 = on.
  // [Tools › Trail › Settings › Auto-save]
  uint8_t  trail_autosave_lowbatt;
  // GPS averaging for waypoint marking — when set, "Mark here" samples the GPS
  // fix for this many seconds and stores the mean position, for a more accurate
  // mark than a single instantaneous fix. 0 = off (instant mark, the default).
  // Index into gpsAvgSecs(). [Tools › Trail › Settings › Mark avg]
  uint8_t  gps_avg_idx;
  // Track positions shared by other nodes via [LOC] messages (LiveTrackStore).
  // 0 = ignore shared positions (default), 1 = parse incoming DM/channel [LOC]
  // shares and update the live-track table (Nearby "Live" view / map).
  uint8_t  track_shared_loc;
  // Live location sharing — a message-based "beacon". When enabled, the device
  // periodically sends a [LOC] message to the chosen target while it moves.
  // Configured from the Map (Trail screen) "Live share" menu.
  uint8_t  loc_share_enabled;       // 0=off (default), 1=auto-sharing on
  uint8_t  loc_share_target_type;   // 0=channel, 1=DM contact
  uint8_t  loc_share_channel_idx;   // target channel index (when target_type==0) [del→onChannelRemoved]
  uint8_t  loc_share_dm_prefix[6];  // target contact pubkey prefix (when target_type==1) [del→onContactRemoved]
  uint8_t  loc_share_move_idx;      // movement gate level (index into locShareMoveMeters)
  uint8_t  loc_share_interval_idx;  // min send interval (index into locShareIntervalSecs)
  uint8_t  loc_share_heartbeat_idx; // stationary heartbeat (index into locShareHeartbeatSecs)
  // Scope used for these [LOC] sends only. 0 = follow the target (a channel's own
  // scope pick / the list default for a DM -- the behaviour before this field);
  // n>0 = scope-list index (n-1), so 1 is "*" (unscoped). [del→MyMesh::removeScope]
  uint8_t  loc_share_scope;
  // How long one auto-share session runs before it switches itself off (index into
  // locShareDurationMins). There is deliberately no "never" option. 0 = the
  // shortest/default, so a zeroed field is already valid.
  uint8_t  loc_share_duration_idx;
  // Locator — a single geofence around a saved point. When enabled the device
  // watches its own GPS fix and beeps + shows an alert when it crosses into
  // (arrive) or out of (leave) the radius. The target coordinate/label is a
  // snapshot of a chosen waypoint, so the alert survives the waypoint being
  // edited or deleted. Configured from Tools › Locator.
  uint8_t  locator_enabled;      // 0=off (default), 1=armed
  uint8_t  locator_has_target;   // 0=no target chosen yet, 1=target set
  uint8_t  locator_radius_idx;   // index into locatorRadiusMeters
  uint8_t  locator_mode;         // 0=arrive, 1=leave, 2=both
  int32_t  locator_lat_1e6;      // target latitude  (1e6-scaled; last-known for a contact)
  int32_t  locator_lon_1e6;      // target longitude (1e6-scaled; last-known for a contact)
  char     locator_label[12];    // target name for the alert text (WAYPOINT_LABEL_LEN)
  // Target can be a static waypoint or a live contact: for a contact the engine
  // re-reads the latest [LOC] position each evaluation (keyed by pubkey prefix),
  // so the geofence follows a moving person ("alert when my friend is near").
  uint8_t  locator_target_kind;  // 0=waypoint (static), 1=live contact
  uint8_t  locator_key[6];       // contact pubkey prefix when target_kind==1 [del→onContactRemoved]
  // Locator proximity beeper — when on (and the alert is armed with a target),
  // the device ticks while inside the radius and shortens the gap between ticks
  // the closer it gets to the target, like a homing beeper. Independent of the
  // discrete arrive/leave alert (locator_mode).
  uint8_t  locator_beeper;       // 0=off (default), 1=on

  // ── Contacts, channels & favourites ───────────────────────────────────
  uint8_t  dm_show_all;        // 0=favourites only, 1=all chat contacts (default)
  uint8_t  room_fav_only;      // 0=all room servers (default), 1=favourites only
  uint64_t ch_fav_bitmask;      // bit i = channel i is marked as favourite [del→onChannelRemoved]
  uint8_t  ch_fav_only;         // 0=show all channels (default), 1=show favourites only
  // Favourites dial: 6 pinned targets. Layout transposes between landscape
  // (3×2) and portrait (2×3). What a slot holds depends on favourite_kinds[]
  // below:
  //   FAV_KIND_CONTACT — first 6 bytes of pub_key; covers chat contacts and
  //     room servers alike. All-zero = empty slot (a real pub_key starts with
  //     6 zero bytes with probability 2^-48).
  //   FAV_KIND_CHANNEL — channel index in byte 0, rest zero. Never empty:
  //     channel 0 is all-zero, so emptiness is decided by the kind first.
  static const uint8_t FAVOURITES_COUNT = 6;
  static const uint8_t FAVOURITE_PREFIX_LEN = 6;
  static const uint8_t FAV_KIND_CONTACT = 0;
  static const uint8_t FAV_KIND_CHANNEL = 1;
  static const uint8_t FAV_KIND_MAX     = 1;
  uint8_t favourite_contacts[FAVOURITES_COUNT][FAVOURITE_PREFIX_LEN]; // [del→onContactRemoved/onChannelRemoved]
  // What each favourite_contacts[] slot holds (see FAV_KIND_* above).
  // On-disk position is at the struct's append-only tail (see the
  // serialization tripwire below), even though declared here beside
  // favourite_contacts for readability. Zero-init = every slot is a contact,
  // which is what pre-0x28 saves are.
  uint8_t  favourite_kinds[FAVOURITES_COUNT];  // [del→onContactRemoved/onChannelRemoved] -- clearFavouriteSlot() clears both fields together, called from either handler depending on the slot's kind
  // Settings > Contacts > "Favs top". Stored inverted so that both a fresh
  // memset and an older prefs file (no bytes here at all) mean "on", which is
  // the default -- a positive flag would read back as off for every upgrader.
  uint8_t  fav_sort_off;       // 0 = favourites first in every list (default), 1 = natural order
  // Settings > Contacts > "Expire" + "Prune now". Index into
  // contactExpiryDays()/contactExpiryLabel() below (0=Off/never, 1=7d, 2=30d,
  // 3=90d) -- a contact whose ContactInfo::lastmod is older than this is
  // eligible for the manual Prune-now sweep. Favourites are always exempt
  // regardless of age. On-disk position is the struct's append-only tail (see
  // the serialization tripwire below), same as fav_sort_off above.
  uint8_t  contact_expiry_idx;   // 0 = off (default)

  // ── Advert ─────────────────────────────────────────────────────────────
  uint8_t  advert_loc_policy;
  uint32_t advert_auto_interval_sec; // periodic 0-hop advert with GPS: 0=off, else seconds
  // Advert sound filter: 0=all adverts, 1=zero-hop only
  uint8_t  advert_sound_scope;

  // ── GPIO ───────────────────────────────────────────────────────────────
  // User-assignable GPIO pins (board-specific, see PIN_GPIO1..4 in the
  // variant header -- currently Wio Tracker L1 only). One byte per pin packs
  // direction + output level (+ analog, gpio1/2 only) into one field:
  // 0=Off 1=Input 2=Output(low) 3=Output(high) 4=Analog. Mode 4 is only
  // meaningful for gpio1/gpio2 (the nRF52840's AIN0/AIN5 -- gpio3/gpio4 have
  // no ADC channel), so those two fields are clamped to 0-3 on load, not 0-4
  // (see DataStore::loadPrefsInt). Default 0 (off/disconnected) on upgrade.
  uint8_t  gpio1_mode;
  uint8_t  gpio2_mode;
  uint8_t  gpio3_mode;
  uint8_t  gpio4_mode;

  // ── Custom messages ────────────────────────────────────────────────────
  char custom_msgs[10][140];   // user-defined quick messages (supports {loc}, {time})

  // Lock-screen password, empty by default. If set, a password is required to
  // unlock the lock screen.
  static const uint8_t LOCK_PASSWORD_MAX_LEN = 32;
  static const uint8_t lock_screen_password_salt_LEN = 16;
  uint8_t lock_screen_password[LOCK_PASSWORD_MAX_LEN];
  uint8_t lock_screen_password_salt[lock_screen_password_salt_LEN];

  // Single source of truth for the live-share option tables (shared by the Map
  // UI labels and the auto-send engine in UITask).
  static const uint8_t LOC_SHARE_MOVE_COUNT = 4;
  static uint16_t locShareMoveMeters(uint8_t idx) {
    static const uint16_t M[LOC_SHARE_MOVE_COUNT] = { 50, 100, 250, 500 };
    return M[idx < LOC_SHARE_MOVE_COUNT ? idx : 1];
  }
  static const uint8_t LOC_SHARE_INTERVAL_COUNT = 4;
  static uint16_t locShareIntervalSecs(uint8_t idx) {
    static const uint16_t S[LOC_SHARE_INTERVAL_COUNT] = { 30, 60, 120, 300 };
    return S[idx < LOC_SHARE_INTERVAL_COUNT ? idx : 1];
  }
  static const uint8_t LOC_SHARE_DURATION_COUNT = 5;
  static uint16_t locShareDurationMins(uint8_t idx) {
    static const uint16_t D[LOC_SHARE_DURATION_COUNT] = { 60, 120, 240, 480, 720 };  // 1 / 2 / 4 / 8 / 12 h
    return D[idx < LOC_SHARE_DURATION_COUNT ? idx : 0];
  }
  static const uint8_t LOC_SHARE_HEARTBEAT_COUNT = 3;
  static uint16_t locShareHeartbeatSecs(uint8_t idx) {
    static const uint16_t H[LOC_SHARE_HEARTBEAT_COUNT] = { 0, 300, 900 };  // off / 5 min / 15 min
    return H[idx < LOC_SHARE_HEARTBEAT_COUNT ? idx : 0];
  }

  // Locator option tables (shared by the Tools › Locator UI labels and the
  // evaluation engine in UITask).
  static const uint8_t LOCATOR_RADIUS_COUNT = 5;
  static uint16_t locatorRadiusMeters(uint8_t idx) {
    static const uint16_t R[LOCATOR_RADIUS_COUNT] = { 50, 100, 250, 500, 1000 };
    return R[idx < LOCATOR_RADIUS_COUNT ? idx : 1];
  }
  static const uint8_t LOCATOR_MODE_COUNT = 3;  // 0=arrive, 1=leave, 2=both
  static const char* locatorModeLabel(uint8_t m) {
    static const char* L[LOCATOR_MODE_COUNT] = { "Arrive", "Leave", "Both" };
    return L[m < LOCATOR_MODE_COUNT ? m : 0];
  }

  // GPS-averaging durations for waypoint marking (seconds). 0 = off (instant).
  static const uint8_t GPS_AVG_COUNT = 4;
  static uint16_t gpsAvgSecs(uint8_t idx) {
    static const uint16_t S[GPS_AVG_COUNT] = { 0, 5, 10, 30 };
    return S[idx < GPS_AVG_COUNT ? idx : 0];
  }
  static const char* gpsAvgLabel(uint8_t idx) {
    static const char* L[GPS_AVG_COUNT] = { "OFF", "5s", "10s", "30s" };
    return L[idx < GPS_AVG_COUNT ? idx : 0];
  }

  // Trail auto-pause delays (seconds). 0 = off.
  static const uint8_t TRAIL_AUTOPAUSE_COUNT = 4;
  static uint16_t trailAutoPauseSecs(uint8_t idx) {
    static const uint16_t S[TRAIL_AUTOPAUSE_COUNT] = { 0, 60, 120, 300 };  // off / 1 / 2 / 5 min
    return S[idx < TRAIL_AUTOPAUSE_COUNT ? idx : 0];
  }
  static const char* trailAutoPauseLabel(uint8_t idx) {
    static const char* L[TRAIL_AUTOPAUSE_COUNT] = { "OFF", "1m", "2m", "5m" };
    return L[idx < TRAIL_AUTOPAUSE_COUNT ? idx : 0];
  }
  // Movement under this many metres counts as "stationary" for auto-pause.
  // Deliberately coarser than the trail min-delta gate (and independent of it)
  // so GPS jitter while parked doesn't keep resetting the idle timer. Engine
  // tuning only — not persisted.
  static const uint16_t TRAIL_AUTOPAUSE_MOVE_M = 15;

  // Contact-expiry thresholds (days) for contact_expiry_idx. Single source of
  // truth for both the Settings > Contacts "Expire" row's label and the age
  // MyMesh::countStaleContacts()/pruneStaleContacts() actually applies, so the
  // number the user picks and the one enforced can't drift apart. 0 = off,
  // which is also the clamp target for any out-of-range saved index.
  static const uint8_t CONTACT_EXPIRY_COUNT = 4;
  static uint16_t contactExpiryDays(uint8_t idx) {
    static const uint16_t D[CONTACT_EXPIRY_COUNT] = { 0, 7, 30, 90 };
    return D[idx < CONTACT_EXPIRY_COUNT ? idx : 0];
  }
  static const char* contactExpiryLabel(uint8_t idx) {
    static const char* L[CONTACT_EXPIRY_COUNT] = { "Off", "7d", "30d", "90d" };
    return L[idx < CONTACT_EXPIRY_COUNT ? idx : 0];
  }

  // Tail sentinel written at the end of /new_prefs. Bump the low byte when
  // adding/removing/reordering fields in DataStore::savePrefs/loadPrefsInt so
  // older saves are detected on load and skipped (zero-init defaults kept).
  // High 24 bits identify the file format; low byte is the schema revision.
  // 0xC0DE0026 is BURNED — it briefly named a layout that put repeat_scope_only
  // + repeat_extra_scopes in the middle of the stream (next to the other
  // repeat_* fields) instead of at the tail, which shifted every field after
  // them by 25 bytes when loading an older file. Never released, but a dev
  // build wrote it, so the number must not be reused for anything else.
  static const uint32_t SCHEMA_SENTINEL = 0xC0DE0030;

  // Bit-index for each home page. Used by page_order (entries store bit+1) and
  // by home_pages_mask. Single source of truth — both HomeScreen::pageBit/bitToPage
  // and SettingsScreen::homePageBitIndex route their per-enum lookups through these.
  enum HomePageBit : uint8_t {
    HPB_CLOCK      = 0,
    HPB_RECENT     = 1,
    HPB_RADIO      = 2,
    HPB_BLUETOOTH  = 3,
    HPB_ADVERT     = 4,
    HPB_GPS        = 5,
    HPB_SENSORS    = 6,
    HPB_TOOLS      = 7,
    HPB_SHUTDOWN   = 8,
    HPB_SETTINGS   = 9,
    HPB_QUICK_MSG  = 10,
    HPB_FAVOURITES = 11,
    HPB_MAP        = 12,
    HPB_COUNT      = 13,
  };

  // Number of usable page_order[] slots — one per home page (== HPB_COUNT), so
  // every page can be reordered. Any page still missing from a saved order is
  // appended at navigation time via buildVisibleOrder's fallback.
  static const uint8_t PAGE_ORDER_LEN = 13;
  // Bytes of page_order persisted at the original file offset. Slots beyond this
  // (PAGE_ORDER_LEN - PAGE_ORDER_LEN_V1) are stored at the file tail, so a
  // pre-0x0019 save — whose order was exactly this long — loads without shifting
  // every field written after page_order.
  static const uint8_t PAGE_ORDER_LEN_V1 = 11;

  // Bitmasks for home_pages_mask (bit=1 → page visible; 0 field = all visible).
  // SETTINGS and QUICK_MSG have no mask bit — they're always visible.
  static const uint16_t HP_CLOCK      = 1 << HPB_CLOCK;
  static const uint16_t HP_RECENT     = 1 << HPB_RECENT;
  static const uint16_t HP_RADIO      = 1 << HPB_RADIO;
  static const uint16_t HP_BLUETOOTH  = 1 << HPB_BLUETOOTH;
  static const uint16_t HP_ADVERT     = 1 << HPB_ADVERT;
  static const uint16_t HP_GPS        = 1 << HPB_GPS;
  static const uint16_t HP_SENSORS    = 1 << HPB_SENSORS;
  static const uint16_t HP_TOOLS      = 1 << HPB_TOOLS;
  static const uint16_t HP_SHUTDOWN   = 1 << HPB_SHUTDOWN;
  static const uint16_t HP_FAVOURITES = 1 << HPB_FAVOURITES;
  static const uint16_t HP_MAP        = 1 << HPB_MAP;
  static const uint16_t HP_ALL        = 0x01FF | HP_FAVOURITES | HP_MAP;

  // Label for home page by bit-index; returns "" for out-of-range.
  // Array indices match HomePageBit values.
  static const char* homePageLabel(uint8_t bit) {
    static const char* labels[HPB_COUNT] = {
      "Clock", "Recent", "Radio", "Bluetooth", "Advert",
      "GPS", "Sensors", "Tools", "Shutdown", "Settings", "Messages", "Favourites", "Map"
    };
    return (bit < HPB_COUNT) ? labels[bit] : "";
  }

  static void buildRTTTLString(const uint8_t* notes, uint8_t len, uint8_t bpm_idx,
                                char* buf, int size) {
    static const uint16_t BPM_OPTS[] = { 60, 90, 120, 150, 180 };
    static const uint8_t  DUR_VALS[] = { 4, 8, 16, 32 };
    static const char     PITCHES[]  = { 'p', 'c', 'd', 'e', 'f', 'g', 'a', 'b' };
    if (len == 0) { buf[0] = '\0'; return; }
    uint16_t bpm = BPM_OPTS[bpm_idx < 5 ? bpm_idx : 2];
    int pos = snprintf(buf, size, "Ring:d=8,o=5,b=%u:", bpm);
    for (int i = 0; i < len && pos < size - 8; i++) {
      if (i > 0 && pos < size - 1) buf[pos++] = ',';
      uint8_t pitch   = notes[i] & 0x07;
      uint8_t octave  = ((notes[i] >> 3) & 0x03) + 4;
      uint8_t dur_val = DUR_VALS[(notes[i] >> 5) & 0x03];
      if (pitch == 0) pos += snprintf(buf + pos, size - pos, "%dp", dur_val);
      else            pos += snprintf(buf + pos, size - pos, "%d%c%d", dur_val, PITCHES[pitch], octave);
    }
    if (pos < size) buf[pos] = '\0';
  }
};

// ── Serialization tripwire ───────────────────────────────────────────────────
// NodePrefs is written/read field-by-field, in order, by DataStore::savePrefs()
// and loadPrefsInt(); the on-disk format IS the struct's field layout. There is
// no automatic check that those two hand-written sequences match the struct, so
// a forgotten read/write silently misaligns EVERY field after it.
//
// This assert is the manual checkpoint. Changing a data member changes sizeof
// and trips it. When it trips, do ALL of the following, then update the number:
//   0. put the new field at the TAIL of the struct, even when it logically
//      belongs beside older siblings. loadPrefsInt()'s rd() is a plain
//      sequential reader gated only on file.available() — there is no per-field
//      versioning — so a field inserted mid-stream is read out of an older
//      file's bytes and shifts EVERY field after it (repeat_scope_only +
//      repeat_extra_scopes did exactly that in the burned 0xC0DE0026 layout).
//      "In struct order" below therefore means "appended in both places".
//   1. add the field's rd(...)   in DataStore::loadPrefsInt(), in struct order
//   2. add the field's write(...) in DataStore::savePrefs(),   in struct order
//   3. clamp it on load (an upgrader's file lacks it → stray bytes; the first
//      few are that file's own 4-byte sentinel tail, so a plausible-looking
//      value like 0x23 shows up rather than 0)
//   4. bump SCHEMA_SENTINEL's low byte
// (Padding can also shift sizeof; a "false" trip just means re-check + rebump.)
// keyboard_cardkb_compact (0xC0DE0023) also landed in existing tail padding --
// confirmed via a real build -- leaving sizeof unchanged at 2720.
// keyboard_main_alphabet (added in an earlier bump) landed in existing tail
// padding -- confirmed via a real build's sizeof() -- so that bump left the
// size unchanged. bot_actions_dm/ch/room and gpio1..4_mode (the last two
// bumps, 7 more uint8_t total) added 8 bytes, not 7 -- one byte of tail
// padding got consumed along the way. 2720 confirmed via a real
// WioTrackerL1Eink_companion_solo_dual build.
// interference_threshold + cad_enabled (0xC0DE0024) are the new struct tail --
// added 8 bytes, not 2 -- the 2 real bytes rounded the struct up to its next
// alignment boundary. Confirmed via a real Heltec_v3_companion_radio_ble build.
// repeat_scope_only (0xC0DE0025) landed in the padding left over from the
// 0xC0DE0024 bump -- confirmed via a real build, sizeof unchanged at 2728.
// repeat_extra_scopes[24] (0xC0DE0026) added exactly 24 bytes, no leftover
// padding this time -- confirmed via a real build, sizeof now 2752.
// 0xC0DE0027 moved those same two fields out of the repeat_* group and down to
// the struct tail (the 0xC0DE0026 mid-stream layout was unreadable for older
// files, see the sentinel comment) -- padding worked out identically either
// way, so sizeof stays 2752. Confirmed via a real Heltec_v3 build.
// favourite_kinds[6] (0xC0DE0028) added 8 bytes, not 6 -- the struct had no
// spare tail padding left after 0xC0DE0026, so the 6 real bytes rounded up to
// the next alignment boundary. Confirmed via a real Heltec_v3 build.
// fav_sort_off (0xC0DE0029) landed in the 2 bytes of padding the 0xC0DE0028
// bump left over -- confirmed via a real Heltec_v3 build, sizeof unchanged.
//
// 2026-09: fields reordered into thematic groups for source readability (see
// the comment at the top of the struct) -- NOT a schema change. No field was
// added, removed, resized, or re-sequenced in DataStore's rd()/wr() calls, so
// the on-disk format and SCHEMA_SENTINEL are untouched. The reorder happened
// to let the compiler pack same-size fields together with less padding,
// dropping sizeof from 2760 to 2752 -- confirmed via a real
// Heltec_v3_companion_radio_ble build. Purely a compile-time in-memory
// layout change; this static_assert exists precisely to catch that kind of
// accidental drift, so the trip was expected here and this comment is that
// re-check. dashboard_fields[3] was then moved from the favourites group to
// Display (a better thematic fit), which shifted padding again and put
// sizeof back at 2760 -- also confirmed via a real
// Heltec_v3_companion_radio_ble build. Still no schema change.
// msg_wake_screen_off (0xC0DE002A) landed in the 1 byte of padding the
// 0xC0DE0029 bump left over -- confirmed via a real sim_companion_radio
// (native) build, sizeof unchanged at 2760.

// repeat_extra_scope_mask + ch_scope_idx[64] (0xC0DE002B) added 64 bytes,
// not 66 -- the struct had 2 bytes of spare tail padding left over from an
// earlier bump -- confirmed via a real sim_companion_radio (native) build
// and a real WioTrackerL1_companion_solo_dual (nRF52/ARM) build, sizeof
// 2824 on both.
// contact_expiry_idx (0xC0DE002C) landed in existing padding elsewhere in
// the struct -- confirmed via a real sim_companion_radio (native) build and a
// real WioTrackerL1_companion_solo_dual (nRF52/ARM) build, sizeof unchanged
// at 2824 on both.
// loc_share_scope (0xC0DE002D) landed in existing padding next to the other
// loc_share_* bytes -- confirmed via real sim_companion_radio (native),
// WioTrackerL1_companion_solo_dual (nRF52/ARM) and Heltec_v3_companion_radio_ble
// (ESP32) builds, sizeof unchanged at 2824. loc_share_duration_idx (0xC0DE002E)
// likewise (sim build; see the check below).
// 0xC0DE002F 32 byte bump for lock_screen_password
// 0xC0DE0030 16 byte bump for lock_screen_password_salt
static_assert(sizeof(NodePrefs) == 2872,
              "NodePrefs layout changed — sync DataStore save/load + clamp, bump "
              "SCHEMA_SENTINEL, then update this size (see steps above).");

// Bounds for a usable repeater radio profile. The frequency range is passed in
// by the caller from RadioLibWrapper::getFreqBounds() — the radio chip's own
// validated range — so a profile that passes here is guaranteed to actually take
// effect on the radio, instead of a hard-coded cap (was 150-960, the SX1262
// range) that would wrongly reject legal frequencies on any other chip. SF/BW/CR
// are the chip-independent discrete LoRa sets.
static inline bool isValidRepeaterProfile(float freq, float bw, uint8_t sf, uint8_t cr,
                                          float min_freq, float max_freq) {
  return freq >= min_freq && freq <= max_freq
      && sf >= 5 && sf <= 12
      && cr >= 5 && cr <= 8
      && bw >= 7.0f && bw <= 510.0f;
}

// Seed a never-configured repeater profile in the same band as the companion's
// own network (see defaultRepeaterFreqForBand() above for why).
static inline void seedDefaultRepeaterProfile(NodePrefs& prefs) {
  prefs.repeater_use_profile = 1;
  prefs.repeater_freq = defaultRepeaterFreqForBand(prefs.freq);
  prefs.repeater_bw   = LORA_BW;
  prefs.repeater_sf   = LORA_SF;
  prefs.repeater_cr   = LORA_CR;
}
