#pragma once
#include <cstdint>
#include <stdio.h>
#include <time.h>
#include <helpers/ConfigSerializer.h>
#include <helpers/DynamicConfigSerializer.h>

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

// The settings' fields. NodePrefs (below) adds how they're saved: /prefs.json,
// one key per field (NodePrefs::structure()). A new field goes anywhere here
// and gets a key there; a file without it leaves the default (MyMesh's
// constructor), and a key no field has any more is skipped.
struct NodePrefsFields {

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
  // since duty-cycle sleep leaves the noise floor stale. Both default 0/off;
  // set from Settings > Radio and the CLI (ui-core/SettingsSchema.h).
  uint8_t  interference_threshold;
  uint8_t  cad_enabled;
  // Auto-resend for on-device DMs: number of extra send attempts (0..5) made when
  // no end-to-end ACK arrives before the deadline, before the delivery marker
  // shows ✗. 0 = no auto-resend (single attempt). Default 2.
  uint8_t  dm_resend_count;
  // External LoRa FEM gain (LNA/PA), from upstream companion-v1.17.1 —
  // board-level only right now (BaseCustomBoard::setLoRaFemLnaEnabled/
  // PaGainEnabled, both default no-op false), no companion CLI/UI to set
  // these yet: they keep the constructor default (MyMesh.cpp).
  uint8_t radio_fem_rxgain;
  uint8_t radio_fem_txgain;
  // Retransmit delay factors (x airtime, the random window for a forwarded
  // flood / direct packet) and the AGC reset interval (seconds / 4, 0 = off),
  // as upstream's (CLI txdelay / direct.txdelay / agc.reset.interval).
  float tx_delay_factor;
  float direct_tx_delay_factor;
  uint8_t agc_reset_interval;
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
  //  repeat_scope_only: 1 = only forward flood packets matching this device's
  //    own scope (Settings > Radio > Scope, default_scope_key) or one of the
  //    repeat_extra_scopes below — drops unscoped floods and floods tagged for
  //    a different community. A no-op (forwards everything, unchanged) while
  //    no scope is configured at all, so enabling this on an unconfigured
  //    device can't silently blackhole all flood traffic.
  uint8_t  repeat_scope_only;
  // The scopes this repeater also relays for, beyond its own. Bit i = scope-list index
  // (i+1) is in this repeater's accept set (index 0, "*", isn't a real scope
  // so isn't toggleable here) — see ScopeList/MyMesh::rebuildRepeatScopes().
  // At most MAX_REPEAT_SCOPES (4) of them count.
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
  // 1 = the user switched Bluetooth off: it stays off after a reboot or
  // hibernate (main.cpp). USB keeps working either way.
  uint8_t  ble_off;
#ifdef ENABLE_WIFI_INTERFACE
  // The app over WiFi (TCP), upstream's: an empty SSID uses the build's WIFI_SSID.
  char wifi_ssid[33];
  char wifi_pwd[64];
  uint8_t wifi_enabled;
#endif

  // ── Power & battery ───────────────────────────────────────────────────
  uint16_t low_batt_mv;         // auto-shutdown threshold: 0=disabled, 3000-3500 mV
  uint8_t batt_display_mode;   // 0=icon, 1=percent, 2=voltage
  // The cell voltage (mV) at 0, 10, ... 100 %, for the battery percentage
  // (ui-core/Battery.h). All zeros = the built-in LiPo curve. Settings >
  // Battery curve edits it; anything not rising point to point reads as unset.
  static const uint8_t BATT_CURVE_PTS = 11;
  uint16_t batt_curve_mv[BATT_CURVE_PTS];

  // ── Display & keyboard ────────────────────────────────────────────────
  uint8_t display_brightness;   // 0=min..4=max, default 2 (medium)
  uint16_t auto_off_secs;       // display auto-off: 0=never, else seconds (default 15)
  uint8_t  auto_lock;           // 0=disabled, 1=lock screen when display turns off
  int8_t tz_offset_hours;       // timezone offset from UTC, -12..+14 (default 0)
  uint8_t  display_rotation;    // 0-3; only used on e-ink displays
  uint8_t  joystick_rotation;   // 0-3 steps CW; independent of display_rotation
  uint8_t  eink_full_refresh_every; // index into {0,5,10,20,30}: full refresh every N partials (0=off)
  // Text and icon size (ui-new, big e-ink panels): Settings > Display, only on
  // builds that opt in (EINK_FONT_SCALE / STATUS_ICON_SCALE). ui_text_scale:
  // the font's pixel multiplier, 1-5; 0 is the build's own (DataStore sanitize
  // sets EINK_FONT_SCALE there). ui_icon_scale: Home's status icons, 0 = Auto
  // (1.5x the text), else 1-8.
  static const uint8_t UI_TEXT_SCALE_MAX = 5, UI_ICON_SCALE_MAX = 8;
  uint8_t  ui_text_scale;
  uint8_t  ui_icon_scale;
  uint16_t home_pages_mask;    // bitmask of visible home pages (bit0=Clock..bit8=Shutdown); 0=all visible
  uint8_t  dashboard_fields[3]; // 0=None,1=Batt V,2=Temp,3=Hum,4=Pres,5=GPS,6=Alt,7=Lux,8=CO2,9=Nodes,10=Msgs,11=Batt %
  // Home screen page order: each byte = HomePageBit + 1. 0 terminates the list.
  // Validity gated by page_order_set magic (see below) — not by entry value range,
  // so a junk byte in 1..HPB_COUNT cannot trigger custom-order mode.
  // Declared as a literal (not HPB_COUNT) so the field offset stays stable across
  // builds that add HomePageBit entries.
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
  // enum only covers actual non-Latin scripts.
  static const uint8_t KB_ALPHABET_LATIN_ONLY = 0;
  static const uint8_t KB_ALPHABET_CYRILLIC   = 1;
  static const uint8_t KB_ALPHABET_GREEK      = 2;
  static const uint8_t KB_ALPHABET_COUNT      = 3;
  static const char* keyboardAlphabetLabel(uint8_t idx) {
    static const char* L[KB_ALPHABET_COUNT] = { "Latin", "Cyrillic", "Greek" };
    return L[idx < KB_ALPHABET_COUNT ? idx : 0];
  }
  uint8_t  keyboard_alt_alphabet;
  // Which script (KB_ALPHABET_LATIN_ONLY/CYRILLIC/GREEK) occupies the on-screen
  // keyboard's page 0 -- its default/opening page -- vs. keyboard_alt_alphabet
  // above, which occupies page 1. Settings > Keyboard's Main/Additional rows.
  // Equal to keyboard_alt_alphabet means no second page (see KeyboardWidget's
  // hasAltAlphabet()). Default 0 (Latin).
  uint8_t  keyboard_main_alphabet;
  // Settings > Keyboard's "Ext. KB" row (boards with a second I2C bus for an
  // optional CardKB, see ENV_PIN_SDA/ENV_PIN_SCL, only). When on, the
  // on-screen keyboard skips drawing its full letter grid + special-row icons
  // -- an external-keyboard typist never looks at them -- and shows a compact
  // one-line status (current script/page, caps) instead; the accent and
  // placeholder popups still render on top exactly as before (see
  // KeyboardWidget::render()). Manual toggle rather than auto-detected, so it
  // stays put even if the module is briefly unplugged. Default 0 (full grid).
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
  // explanation (bit i = 1<<tm_wday).
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
    static const char* L[ALARM_REPEAT_COUNT] = { "Once", "Daily", "Weekdays", "Weekends" };   // 0: no repeat (as the L2 shows it)
    return L[idx < ALARM_REPEAT_COUNT ? idx : 0];
  }

  // ── Buzzer & notifications ────────────────────────────────────────────
  uint8_t  buzzer_quiet;
  uint8_t  buzzer_volume;   // 0=min..4=max, default 4
  uint8_t  buzzer_auto;        // 0=manual (default), 1=auto-mute when BT connected
  // Quiet hours: muted from quiet_from to quiet_to (local hours 0-23, wrapping
  // past midnight) while quiet_hours is on; the clock alarm still rings and a
  // message doesn't wake the screen (inQuietHours() below). A manual mute /
  // unmute during the window stands until it ends (soundctl::tick()).
  uint8_t  quiet_hours;         // 0=off (default), 1=on
  uint8_t  quiet_from;          // default 22
  uint8_t  quiet_to;            // default 7
  // Settings > Display > "Msg wake": an incoming message turns the display on
  // (UITask::newMsg()) if it was off and no companion app is already showing it.
  uint8_t  msg_wake;            // 1=wake display for incoming msgs (default), 0=not
  // ui-new's new-message alert: the banner from the top, an envelope with a
  // count in the corner, or nothing (the sound and the wake still happen).
  static const uint8_t MSG_ALERT_NORMAL = 0, MSG_ALERT_COMPACT = 1, MSG_ALERT_OFF = 2;
  uint8_t  msg_alert;
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
  // Scope-list index per channel (see ScopeList): 0 = "*" (unscoped), 1.. a
  // list entry, CH_SCOPE_DEFAULT (the default for every channel) = whatever
  // the list's default is (MyMesh::channelScope()). Fixed at 64 slots (not
  // MAX_GROUP_CHANNELS, which varies by board/variant and would make
  // sizeof(NodePrefs) variant-dependent) -- same implicit channel-count cap
  // every ch_notif_*/ch_fav_bitmask uint64_t bitmask above already has.
  static const uint8_t MAX_SCOPED_CHANNELS = 64;
  static const uint8_t CH_SCOPE_DEFAULT = 0xFF;
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
  uint8_t  bot_commands_room;
  // Per-target toggle for bot *action* commands (!buzz/!gps/!advert) --
  // separate from bot_commands_ch/bot_commands_room above, which only gate
  // the read-only query commands (!ping/!batt/...). Nested under that
  // Commands toggle (Commands=off means neither queries nor actions run for
  // that target); Actions=on additionally lets the state-changing commands
  // through. Default 0 (off) -- these change device behaviour remotely.
  uint8_t  bot_actions_room;
  uint8_t  bot_quiet_start;     // quiet-hours start hour, local 0-23 (start==end → disabled)
  uint8_t  bot_quiet_end;       // quiet-hours end hour, local 0-23
  uint8_t  bot_actions_scope;   // who may run the bot's actions (!gps !buzz !gpio ...): 0 = all, 1 = favourites

  // ── GPS, trail & location sharing ─────────────────────────────────────
  uint8_t  gps_enabled;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval;     // GPS duty-cycle sleep window in seconds (0 = disabled, GPS stays continuous)
  // Global measurement system for every distance/speed shown in the UI
  // (Nearby, Trail, navigate-to-point). 0=metric (default), 1=imperial.
  uint8_t  units_imperial;
  // GPS trail cadence. Logging on/off is a runtime state (Tools › Trail),
  // not a persisted preference.
  uint8_t trail_min_delta_idx;  // min-distance gate level (0=finest..3); metres or feet per units_imperial
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
  // Backlight as a percentage (1-100) for frontends with a brightness slider
  // (ui-lvgl); 0 = use the 0-4 display_brightness level (ui-new, and the default).
  uint8_t  display_brightness_pct;
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
  // Kind 2 is a channel [LOC] share: no key, followed by sender name (label).
  uint8_t  locator_target_kind;  // 0=waypoint (static), 1=live contact, 2=channel live share
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
  // Zero-init = every slot is a contact.
  uint8_t  favourite_kinds[FAVOURITES_COUNT];  // [del→onContactRemoved/onChannelRemoved] -- clearFavouriteSlot() clears both fields together, called from either handler depending on the slot's kind
  // Settings > Contacts > "Favs top".
  uint8_t  fav_sort;           // 1 = favourites first in every list (default), 0 = natural order
  // Settings > Contacts > "Expire" + "Prune now". Index into
  // contactExpiryDays()/contactExpiryLabel() below (0=Off/never, 1=7d, 2=30d,
  // 3=90d) -- a contact whose ContactInfo::lastmod is older than this is
  // eligible for the manual Prune-now sweep. Favourites are always exempt
  // regardless of age.
  uint8_t  contact_expiry_idx;   // 0 = off (default)

  // ── Advert ─────────────────────────────────────────────────────────────
  uint8_t  advert_loc_policy;
  uint32_t advert_auto_interval_sec; // periodic 0-hop advert with GPS: 0=off, else seconds
  // Advert sound filter: 0=all adverts, 1=zero-hop only
  uint8_t  advert_sound_scope;
  // Settings > Privacy: with "Position in adverts" on, the position is snapped
  // to a fixed grid (MyMesh::createOwnAdvert): 0 = exact, 1 = ~100 m,
  // 2 = ~500 m, 3 = ~1 km.
  static const uint8_t ADV_LOC_PREC_COUNT = 4;
  uint8_t  advert_loc_prec;

  // ── GPIO ───────────────────────────────────────────────────────────────
  // User-assignable GPIO pins (board-specific, see PIN_GPIO1..4 in the
  // variant header -- currently Wio Tracker L1 only). One byte per pin packs
  // direction + output level (+ analog, gpio1/2 only) into one field:
  // 0=Off 1=Input 2=Output(low) 3=Output(high) 4=Analog. Mode 4 is only
  // meaningful for gpio1/gpio2 (the nRF52840's AIN0/AIN5 -- gpio3/gpio4 have
  // no ADC channel), so those two fields are clamped to 0-3 on load, not 0-4
  // (see DataStore's sanitize). Default 0 (off/disconnected).
  uint8_t  gpio1_mode;
  uint8_t  gpio2_mode;
  uint8_t  gpio3_mode;
  uint8_t  gpio4_mode;

  // ── Custom messages ────────────────────────────────────────────────────
  char custom_msgs[10][140];   // user-defined quick messages (supports {loc}, {time})

  // ── Screen lock ────────────────────────────────────────────────────────
  // SHA-256 of salt + PIN (see ScreenLock.h); all zeros = no PIN. It locks the
  // screen only: the radio and the app link keep working.
  static const uint8_t LOCK_HASH_LEN = 32;
  static const uint8_t LOCK_SALT_LEN = 16;
  uint8_t lock_screen_password[LOCK_HASH_LEN];
  uint8_t lock_screen_password_salt[LOCK_SALT_LEN];
  // ui-new: 0 = big clock + date + unread pill, 1 = compact clock with the
  // clock fields as rows below. The L2 has its own lock screen.
  uint8_t  lock_compact;
  // Settings > Contacts > "Channel order": how the channel lists are sorted
  // (after favourites, when those go first): 0 = as the slots are (default),
  // 1 = latest message first, 2 = by name. See chanctl::order().
  static const uint8_t CH_SORT_COUNT = 3;
  uint8_t  ch_sort;
  // Map > options > "Packet flow": what the radio hears drawn on the map as
  // lines between the nodes (ui-core/PacketFlow.h). 0 = off (default).
  uint8_t  flow_show;

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
    static const char* L[GPS_AVG_COUNT] = { "Off", "5s", "10s", "30s" };
    return L[idx < GPS_AVG_COUNT ? idx : 0];
  }

  // Trail auto-pause delays (seconds). 0 = off.
  static const uint8_t TRAIL_AUTOPAUSE_COUNT = 4;
  static uint16_t trailAutoPauseSecs(uint8_t idx) {
    static const uint16_t S[TRAIL_AUTOPAUSE_COUNT] = { 0, 60, 120, 300 };  // off / 1 / 2 / 5 min
    return S[idx < TRAIL_AUTOPAUSE_COUNT ? idx : 0];
  }
  static const char* trailAutoPauseLabel(uint8_t idx) {
    static const char* L[TRAIL_AUTOPAUSE_COUNT] = { "Off", "1m", "2m", "5m" };
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

  // /prefs' layout (see the comment at the top). A file of another version
  // isn't read: the defaults stand.
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
      "Clock", "Recent", "Status", "Bluetooth", "Advert",   // Status: the old Radio slot (ui-new)
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

// The settings, saved as /prefs.json (DataStore). What upstream's companion
// has is under its keys -- "radio", "gps", "repeat", "comp", "custom" -- so
// either firmware reads what the other wrote of it; the rest is under "solo".
// Keys are at most 15 characters (ConfigSerializer). Byte arrays and 64-bit
// masks go as hex.
class NodePrefs : public NodePrefsFields, public ConfigSerializer {
  // A section: a ConfigSerializer whose structure() is one of NodePrefs's.
  class Section : public ConfigSerializer {
    NodePrefs* _p;
    void (NodePrefs::*_fn)(Section&);
  protected:
    void structure() override { (_p->*_fn)(*this); }
  public:
    Section(NodePrefs* p, void (NodePrefs::*fn)(Section&)) : _p(p), _fn(fn) { }
    using ConfigSerializer::def;
    template <typename T> void hex(const char* key, T& v) { def(key, (void*)&v, sizeof(v)); }
    void sub(const char* key, Section& s) { def(key, s); }
  };

  // The board's own settings (Board::attachDynamicPrefs()): the LoRa front
  // end's gains are fields here; any other key a board has is kept in
  // `custom` as it is upstream.
  class FemPrefs : public KeyValueStore {
    NodePrefs* _p;
    uint8_t* field(const char* key) {
      if (strcmp(key, "fem_rxgain") == 0) return &_p->radio_fem_rxgain;
      if (strcmp(key, "fem_txgain") == 0) return &_p->radio_fem_txgain;
      return nullptr;
    }
  public:
    FemPrefs(NodePrefs* p) : _p(p) { }
    bool getByKey(const char* key, char* value, size_t max_len) override {
      uint8_t* f = field(key);
      if (!f || max_len < 2) return false;
      value[0] = *f ? '1' : '0';
      value[1] = 0;
      return true;
    }
    bool setByKey(const char* key, const char* value) override {
      uint8_t* f = field(key);
      if (!f) return false;
      *f = atoi(value) ? 1 : 0;
      _p->markDirty();
      return true;
    }
  };

  // ── upstream's sections ──
  void radioKeys(Section& s) {
    s.def("freq", freq); s.def("bw", bw); s.def("sf", sf); s.def("cr", cr);
    s.def("cad", cad_enabled); s.def("int_thr", interference_threshold);
    s.def("rxgain", rx_boosted_gain);
    s.def("fem_rxgain", radio_fem_rxgain); s.def("fem_txgain", radio_fem_txgain);
    s.def("tx", tx_power_dbm); s.def("af", airtime_factor); s.def("rxdelay", rx_delay_base);
    s.def("f_txdelay", tx_delay_factor); s.def("d_txdelay", direct_tx_delay_factor);
    s.def("agc_int", agc_reset_interval);
    s.def("hash_mode", path_hash_mode); s.def("multi_ack", multi_acks);
  }
  void gpsKeys(Section& s) {
    s.def("en", gps_enabled); s.def("int", gps_interval); s.def("adv_loc", advert_loc_policy);
  }
  void repeatKeys(Section& s) {   // upstream keeps "disable", the inverse
    uint8_t off = client_repeat ? 0 : 1;
    s.def("disable", off);
    client_repeat = off ? 0 : 1;
  }
  void compKeys(Section& s) {
    s.def("auto_max", autoadd_max_hops);
    s.def("defs_nm", default_scope_name, sizeof(default_scope_name));
    s.hex("defs_key", default_scope_key);
    s.def("pin", ble_pin); s.def("buzz_q", buzzer_quiet);
    s.def("auto_add", autoadd_config); s.def("man_add", manual_add_contacts);
    s.def("tel_base", telemetry_mode_base); s.def("tel_loc", telemetry_mode_loc);
    s.def("tel_env", telemetry_mode_env);
    s.def("tz_offset", tz_offset_hours);
  }

  // ── this firmware's ("solo") ──
  void radioSolo(Section& s) {
    s.def("rx_ps", rx_powersave); s.def("apc", tx_apc); s.def("dm_resend", dm_resend_count);
    char k[4] = "p0";
    for (int i = 0; i < USER_RADIO_PRESET_MAX; i++) {
      k[1] = '0' + i;
      s.sub(k, preset[i]);
    }
  }
  void presetKeys(Section& s) {
    for (int i = 0; i < USER_RADIO_PRESET_MAX; i++) {
      if (&s != &preset[i]) continue;
      UserRadioPreset& p = user_radio_presets[i];
      s.def("name", p.name, sizeof(p.name));
      s.def("freq", p.freq); s.def("bw", p.bw); s.def("sf", p.sf); s.def("cr", p.cr);
    }
  }
  void repeaterSolo(Section& s) {
    s.def("skip_adv", repeat_skip_adverts); s.def("max_hops", repeat_max_hops);
    s.def("delay_boost", repeat_delay_boost); s.def("min_snr", repeat_min_snr);
    s.def("sup_dup", repeat_suppress_dup); s.def("scope_only", repeat_scope_only);
    s.def("extra_scopes", repeat_extra_scope_mask);
    s.def("use_prof", repeater_use_profile);
    s.def("freq", repeater_freq); s.def("bw", repeater_bw);
    s.def("sf", repeater_sf); s.def("cr", repeater_cr);
  }
  void powerSolo(Section& s) {
    s.def("ble_off", ble_off);
    s.def("low_mv", low_batt_mv); s.def("batt_disp", batt_display_mode);
    s.hex("curve", batt_curve_mv);
  }
  void displaySolo(Section& s) {
    s.def("bright", display_brightness); s.def("bright_pct", display_brightness_pct);
    s.def("auto_off", auto_off_secs); s.def("auto_lock", auto_lock);
    s.def("lock_compact", lock_compact);
    s.def("rot", display_rotation); s.def("joy_rot", joystick_rotation);
    s.def("eink_full", eink_full_refresh_every);
    s.def("text_scale", ui_text_scale); s.def("icon_scale", ui_icon_scale);
    s.def("pages", home_pages_mask); s.hex("dash", dashboard_fields);
    s.hex("order", page_order); s.def("order_set", page_order_set);
    s.def("ch_sort", ch_sort); s.def("flow", flow_show);
    s.def("kb", keyboard_type); s.def("kb_alt", keyboard_alt_alphabet);
    s.def("kb_main", keyboard_main_alphabet); s.def("kb_cardkb", keyboard_cardkb_compact);
  }
  void clockSolo(Section& s) {
    s.def("hide_sec", clock_hide_seconds); s.def("h12", clock_12h);
    s.def("alarm", alarm_on); s.def("alarm_h", alarm_hour); s.def("alarm_m", alarm_min);
    s.def("alarm_rep", alarm_repeat_mask);
  }
  void soundSolo(Section& s) {
    s.def("vol", buzzer_volume); s.def("auto", buzzer_auto);
    s.def("quiet", quiet_hours); s.def("quiet_from", quiet_from); s.def("quiet_to", quiet_to);
    s.def("msg_wake", msg_wake); s.def("msg_alert", msg_alert);
    s.def("rt_bpm", ringtone_bpm_idx); s.def("rt_len", ringtone_len); s.hex("rt", ringtone_notes);
    s.def("rt2_bpm", ringtone2_bpm_idx); s.def("rt2_len", ringtone2_len); s.hex("rt2", ringtone2_notes);
    s.def("mel_dm", notif_melody_dm); s.def("mel_ch", notif_melody_ch); s.def("mel_ad", notif_melody_ad);
    s.def("adv_scope", advert_sound_scope);
    s.hex("ch_over", ch_notif_override); s.hex("ch_muted", ch_notif_muted);
    s.hex("ch_mel_set", ch_notif_melody_set); s.hex("ch_mel_2", ch_notif_melody_2);
    s.hex("dm", dm_notif); s.hex("dm_mel", dm_melody);
  }
  void scopeSolo(Section& s) { s.hex("ch", ch_scope_idx); }
  void botSolo(Section& s) {
    s.def("dm_en", bot_enabled); s.def("dm_trig", bot_trigger, sizeof(bot_trigger));
    s.def("dm_reply", bot_reply_dm, sizeof(bot_reply_dm));
    s.def("dm_cmd", bot_commands_enabled); s.def("dm_act", bot_actions_dm); s.def("dm_scope", bot_dm_scope);
    s.def("ch_en", bot_channel_enabled); s.def("ch_idx", bot_channel_idx);
    s.def("ch_trig", bot_trigger_ch, sizeof(bot_trigger_ch));
    s.def("ch_reply", bot_reply_ch, sizeof(bot_reply_ch));
    s.def("ch_cmd", bot_commands_ch); s.def("ch_act", bot_actions_ch);
    s.def("rm_en", bot_room_enabled); s.hex("rm_pfx", bot_room_prefix);
    s.def("rm_trig", bot_trigger_room, sizeof(bot_trigger_room));
    s.def("rm_reply", bot_reply_room, sizeof(bot_reply_room));
    s.def("rm_cmd", bot_commands_room); s.def("rm_act", bot_actions_room);
    s.def("q_start", bot_quiet_start); s.def("q_end", bot_quiet_end); s.def("act_scope", bot_actions_scope);
  }
  void gpsSolo(Section& s) {
    s.def("imperial", units_imperial);
    s.def("tr_delta", trail_min_delta_idx); s.def("tr_pace", trail_show_pace);
    s.def("tr_pause", trail_autopause_idx); s.def("tr_lowbatt", trail_autosave_lowbatt);
    s.def("avg", gps_avg_idx); s.def("trk_shared", track_shared_loc);
    s.def("adv_auto", advert_auto_interval_sec); s.def("adv_prec", advert_loc_prec);
  }
  void shareSolo(Section& s) {
    s.def("en", loc_share_enabled); s.def("tgt", loc_share_target_type);
    s.def("ch", loc_share_channel_idx); s.hex("dm", loc_share_dm_prefix);
    s.def("move", loc_share_move_idx); s.def("int", loc_share_interval_idx);
    s.def("hb", loc_share_heartbeat_idx); s.def("scope", loc_share_scope);
    s.def("dur", loc_share_duration_idx);
  }
  void locatorSolo(Section& s) {
    s.def("en", locator_enabled); s.def("has", locator_has_target);
    s.def("radius", locator_radius_idx); s.def("mode", locator_mode);
    s.def("lat", locator_lat_1e6); s.def("lon", locator_lon_1e6);
    s.def("label", locator_label, sizeof(locator_label));
    s.def("kind", locator_target_kind); s.hex("key", locator_key);
    s.def("beep", locator_beeper);
  }
  void listsSolo(Section& s) {
    s.def("dm_all", dm_show_all); s.def("room_fav", room_fav_only);
    s.hex("ch_fav", ch_fav_bitmask); s.def("ch_fav_only", ch_fav_only);
    s.hex("favs", favourite_contacts); s.hex("fav_kinds", favourite_kinds);
    s.def("fav_sort", fav_sort); s.def("expiry", contact_expiry_idx);
  }
  void gpioSolo(Section& s) {
    s.def("m1", gpio1_mode); s.def("m2", gpio2_mode); s.def("m3", gpio3_mode); s.def("m4", gpio4_mode);
  }
  void msgsSolo(Section& s) {
    char k[4] = "m0";
    for (int i = 0; i < 10; i++) {
      k[1] = '0' + i;
      s.def(k, custom_msgs[i], sizeof(custom_msgs[i]));
    }
  }
  void lockSolo(Section& s) { s.hex("hash", lock_screen_password); s.hex("salt", lock_screen_password_salt); }
#ifdef ENABLE_WIFI_INTERFACE
  void wifiKeys(Section& s) {
    s.def("ssid", wifi_ssid, sizeof(wifi_ssid));
    s.def("pwd", wifi_pwd, sizeof(wifi_pwd));
    s.def("enabled", wifi_enabled);
  }
#endif
  void soloKeys(Section& s) {
    s.sub("radio", radio_solo); s.sub("repeater", repeater_solo); s.sub("power", power_solo);
    s.sub("display", display_solo); s.sub("clock", clock_solo); s.sub("sound", sound_solo);
    s.sub("scope", scope_solo); s.sub("bot", bot_solo); s.sub("gps", gps_solo);
    s.sub("share", share_solo); s.sub("locator", locator_solo); s.sub("lists", lists_solo);
    s.sub("gpio", gpio_solo); s.sub("msgs", msgs_solo); s.sub("lock", lock_solo);
  }

  Section radio, gps, repeat, comp;
#ifdef ENABLE_WIFI_INTERFACE
  Section wifi;
#endif
  Section solo, radio_solo, preset[USER_RADIO_PRESET_MAX], repeater_solo, power_solo, display_solo,
          clock_solo, sound_solo, scope_solo, bot_solo, gps_solo, share_solo, locator_solo,
          lists_solo, gpio_solo, msgs_solo, lock_solo;
  FemPrefs fem;
  DynamicConfigSerializer custom;

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("gps", gps);
    def("repeat", repeat);
    def("comp", comp);
    def("custom", custom);
#ifdef ENABLE_WIFI_INTERFACE
    def("wifi", wifi);
#endif
    def("solo", solo);
  }

public:
  // The node's position as the file holds it (MyMesh keeps it in `sensors`):
  // DataStore sets it before a save and reads it after a load.
  double node_lat = 0, node_lon = 0;

  NodePrefs()
    : radio(this, &NodePrefs::radioKeys), gps(this, &NodePrefs::gpsKeys),
      repeat(this, &NodePrefs::repeatKeys), comp(this, &NodePrefs::compKeys),
#ifdef ENABLE_WIFI_INTERFACE
      wifi(this, &NodePrefs::wifiKeys),
#endif
      solo(this, &NodePrefs::soloKeys), radio_solo(this, &NodePrefs::radioSolo),
      preset{ {this, &NodePrefs::presetKeys}, {this, &NodePrefs::presetKeys},
              {this, &NodePrefs::presetKeys}, {this, &NodePrefs::presetKeys} },
      repeater_solo(this, &NodePrefs::repeaterSolo), power_solo(this, &NodePrefs::powerSolo),
      display_solo(this, &NodePrefs::displaySolo), clock_solo(this, &NodePrefs::clockSolo),
      sound_solo(this, &NodePrefs::soundSolo), scope_solo(this, &NodePrefs::scopeSolo),
      bot_solo(this, &NodePrefs::botSolo), gps_solo(this, &NodePrefs::gpsSolo),
      share_solo(this, &NodePrefs::shareSolo), locator_solo(this, &NodePrefs::locatorSolo),
      lists_solo(this, &NodePrefs::listsSolo), gpio_solo(this, &NodePrefs::gpioSolo),
      msgs_solo(this, &NodePrefs::msgsSolo), lock_solo(this, &NodePrefs::lockSolo),
      fem(this), custom(&fem) {
    clearFields();
  }
  // The sections point back at this object: it isn't copied.
  NodePrefs(const NodePrefs&) = delete;
  NodePrefs& operator=(const NodePrefs&) = delete;

  // Every field zero (MyMesh then sets the defaults).
  void clearFields() { memset(static_cast<NodePrefsFields*>(this), 0, sizeof(NodePrefsFields)); }

  // The board's settings (upstream's getCustom()), for Board::attachDynamicPrefs().
  KeyValueStore* getCustom() { return &custom; }
#ifdef ENABLE_WIFI_INTERFACE
  #ifndef WIFI_SSID
    #define WIFI_SSID ""
  #endif
  // use ssid from prefs, or fallback to ssid from build flags
  const char* getWifiSSID() const { return wifi_ssid[0] ? wifi_ssid : WIFI_SSID; }
#endif
  bool isRepeatEn() const { return client_repeat != 0; }
  void setRepeatEn(bool en) { client_repeat = en ? 1 : 0; }

  bool isDirty() const override { return ConfigSerializer::isDirty() || custom.isDirty(); }
  void clearDirty() override { ConfigSerializer::clearDirty(); custom.clearDirty(); }
  using ConfigSerializer::markDirty;
};

// Local hour `h` inside [start, end): a window with start > end wraps past
// midnight; start == end is no window. Shared by the bot's quiet hours and the
// sound's.
static inline bool hourInWindow(int h, int start, int end) {
  if (start == end) return false;
  return start < end ? (h >= start && h < end) : (h >= start || h < end);
}
// The local hour from a UTC time; false while the clock isn't set.
static inline bool localHour(uint32_t utc, int8_t tz_hours, int& h) {
  if (utc < 1000000000UL) return false;
  h = (int)(((int64_t)utc + (int64_t)tz_hours * 3600) / 3600 % 24);
  return true;
}
// A UTC time as the local calendar date and time; false while the clock isn't set.
static inline bool localTm(uint32_t utc, int8_t tz_hours, struct tm& out) {
  if (utc < 1000000000UL) return false;
  time_t t = (time_t)((int64_t)utc + (int64_t)tz_hours * 3600);
  out = *gmtime(&t);
  return true;
}
// Settings > Sound > Quiet hours in force at `utc` (never with the clock unset).
static inline bool inQuietHours(const NodePrefs& p, uint32_t utc) {
  int h;
  return p.quiet_hours && localHour(utc, p.tz_offset_hours, h) && hourInWindow(h, p.quiet_from, p.quiet_to);
}

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
