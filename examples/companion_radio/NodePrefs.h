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
  float airtime_factor;
  char node_name[32];
  float freq;
  uint8_t sf;
  uint8_t cr;
  uint8_t multi_acks;
  uint8_t manual_add_contacts;
  float bw;
  int8_t tx_power_dbm;
  uint8_t telemetry_mode_base;
  uint8_t telemetry_mode_loc;
  uint8_t telemetry_mode_env;
  float rx_delay_base;
  uint32_t ble_pin;
  uint8_t  advert_loc_policy;
  uint8_t  buzzer_quiet;
  uint8_t  buzzer_volume;   // 0=min..4=max, default 4
  uint8_t  gps_enabled;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval;     // GPS read interval in seconds
  uint8_t autoadd_config;    // bitmask for auto-add contacts config
  uint8_t rx_boosted_gain; // SX126x RX boosted gain mode (0=power saving, 1=boosted)
  uint8_t client_repeat;
  uint8_t path_hash_mode;    // which path mode to use when sending
  uint8_t autoadd_max_hops;  // 0 = no limit, 1 = direct (0 hops), N = up to N-1 hops (max 64)
  char default_scope_name[31];
  uint8_t default_scope_key[16];
  uint8_t display_brightness;   // 0=min..4=max, default 2 (medium)
  uint16_t auto_off_secs;       // display auto-off: 0=never, else seconds (default 15)
  uint8_t  auto_lock;           // 0=disabled, 1=lock screen when display turns off
  int8_t tz_offset_hours;       // timezone offset from UTC, -12..+14 (default 0)
  uint16_t low_batt_mv;         // auto-shutdown threshold: 0=disabled, 3000-3500 mV
  uint8_t batt_display_mode;   // 0=icon, 1=percent, 2=voltage
  char custom_msgs[10][140];   // user-defined quick messages (supports {loc}, {time})
  uint64_t ch_notif_override;  // bitmask: bit i = channel i has explicit notification setting
  uint64_t ch_notif_muted;     // bitmask: bit i = channel i muted (only if override bit set)
  uint8_t  dm_show_all;        // 0=favourites only (default), 1=all chat contacts
  uint8_t  room_fav_only;      // 0=all room servers (default), 1=favourites only
  uint8_t  ringtone_bpm_idx;   // index into {60,90,120,150,180}
  uint8_t  ringtone_len;        // number of notes in custom ringtone (0 = use default)
  uint8_t  ringtone_notes[32]; // packed: bits0-2=pitch, bits3-4=octave-4, bits5-6=dur_idx
  uint16_t home_pages_mask;    // bitmask of visible home pages (bit0=Clock..bit8=Shutdown); 0=all visible
  uint8_t  bot_enabled;         // 0=disabled, 1=DM bot active (responds to all DMs)
  uint8_t  bot_channel_enabled; // 0=disabled, 1=channel bot active for bot_channel_idx
  uint8_t  bot_channel_idx;     // channel index for channel bot [del→onChannelRemoved]
  char     bot_trigger[64];     // DM trigger phrase (case-insensitive contains; "*" = any DM)
  char     bot_reply_dm[140];   // auto-reply text for DM
  char     bot_reply_ch[140];   // auto-reply text for channel
  char     bot_trigger_ch[64];  // channel trigger phrase (independent of DM; "*" = any channel msg)
  uint8_t  bot_commands_enabled; // 0=off, 1=answer !ping/!batt/!loc/!time/!help DM commands
  uint8_t  bot_quiet_start;     // quiet-hours start hour, local 0-23 (start==end → disabled)
  uint8_t  bot_quiet_end;       // quiet-hours end hour, local 0-23
  uint8_t  clock_hide_seconds; // 0=show HH:MM:SS/refresh 1s (default), 1=hide/refresh 60s
  uint8_t  clock_12h;          // 0=24h (default), 1=12h with AM/PM
  uint8_t  buzzer_auto;        // 0=manual (default), 1=auto-mute when BT connected
  struct DmNotifEntry { uint8_t prefix[4]; uint8_t state; }; // state: 0=default,1=muted,2=force-on
  static const int DM_NOTIF_TABLE_MAX = 16;
  DmNotifEntry dm_notif[DM_NOTIF_TABLE_MAX]; // 16*5 = 80 bytes [del→onContactRemoved]
  uint8_t  dashboard_fields[3]; // 0=None,1=Batt V,2=Temp,3=Hum,4=Pres,5=GPS,6=Alt,7=Lux,8=CO2,9=Nodes,10=Msgs,11=Batt %
  uint32_t advert_auto_interval_sec; // periodic 0-hop advert with GPS: 0=off, else seconds
  // Second melody slot (same packing as ringtone_*)
  uint8_t  ringtone2_bpm_idx;
  uint8_t  ringtone2_len;
  uint8_t  ringtone2_notes[32];
  // Global melodies for notifications: 0=built-in, 1=melody1, 2=melody2, 3=none
  uint8_t  notif_melody_dm;
  uint8_t  notif_melody_ch;
  uint8_t  notif_melody_ad;
  // Advert sound filter: 0=all adverts, 1=zero-hop only
  uint8_t  advert_sound_scope;
  // Per-channel melody override (2 bitmasks, 1 bit per channel)
  uint64_t ch_notif_melody_set;  // bit i = channel i has explicit melody [del→onChannelRemoved]
  uint64_t ch_notif_melody_2;    // bit i = use melody 2 (else melody 1, when set bit is set)
  // Per-DM melody table
  struct DmMelodyEntry { uint8_t prefix[4]; uint8_t slot; }; // slot: 0=global,1=melody1,2=melody2
  static const int DM_MELODY_TABLE_MAX = 16;
  DmMelodyEntry dm_melody[DM_MELODY_TABLE_MAX]; // [del→onContactRemoved]
  uint8_t  use_lemon_font;      // 0=default Adafruit font, 1=Lemon font (Unicode, pixel-accurate wrap)
  uint8_t  display_rotation;    // 0-3; only used on e-ink displays
  // Home screen page order: each byte = HomePageBit + 1. 0 terminates the list.
  // Validity gated by page_order_set magic (see below) — not by entry value range,
  // so a junk byte in 1..HPB_COUNT cannot trigger custom-order mode.
  // Declared as a literal (not HPB_COUNT) so the field offset stays stable across
  // builds that add HomePageBit entries. The first PAGE_ORDER_LEN_V1 bytes persist
  // at this offset (backward-compatible); the remaining slots live at the file
  // tail (see DataStore) so pre-0x0019 saves still load without shifting.
  uint8_t  page_order[13];
  uint8_t  joystick_rotation;   // 0-3 steps CW; independent of display_rotation
  uint8_t  eink_full_refresh_every; // index into {0,5,10,20,30}: full refresh every N partials (0=off)
  uint8_t  page_order_set;      // 0xA5 = page_order is user-configured; anything else = use default
  static const uint8_t PAGE_ORDER_MAGIC = 0xA5;

  // Favourites dial: 6 pinned contacts, stored as first 6 bytes of pub_key per slot.
  // All-zero = empty slot (probability a real pub_key starts with 6 zero bytes is 2^-48).
  // Layout transposes between landscape (3×2) and portrait (2×3).
  static const uint8_t FAVOURITES_COUNT = 6;
  static const uint8_t FAVOURITE_PREFIX_LEN = 6;
  uint8_t favourite_contacts[FAVOURITES_COUNT][FAVOURITE_PREFIX_LEN]; // [del→onContactRemoved]

  // GPS trail cadence. Logging on/off is a runtime state (Tools › Trail),
  // not a persisted preference.
  uint8_t trail_interval_idx;   // reserved — sampling cadence is now fixed at TrailStore::SAMPLING_SECS
  uint8_t trail_min_delta_idx;  // min-distance gate level (0=finest..3); metres or feet per units_imperial
  uint8_t trail_units_idx;      // legacy: old combined speed/pace+unit index (km/h, mph, min/km, min/mi)

  uint64_t ch_fav_bitmask;      // bit i = channel i is marked as favourite
  uint8_t  ch_fav_only;         // 0=show all channels (default), 1=show favourites only

  // Global measurement system for every distance/speed shown in the UI
  // (Nearby, Trail, navigate-to-point). 0=metric (default), 1=imperial.
  uint8_t  units_imperial;
  // Trail Summary readout: 0=speed (km/h or mph), 1=pace (min/km or min/mi).
  // The km-vs-mi choice now comes from units_imperial, so this is just the mode.
  uint8_t  trail_show_pace;
  // Hardware duty-cycle receive (battery saver): 0=continuous RX (default), 1=on.
  // The SX126x cycles RX↔sleep on its own and wakes on a preamble — cuts average
  // RX current at the cost of a little receive latency. See RadioLibWrapper
  // power-save (startReceiveDutyCycleAuto).
  uint8_t  rx_powersave;
  // Adaptive Power Control: 0=off (fixed tx_power_dbm, default), 1=on. When on,
  // tx_power_dbm is treated as a ceiling and the radio's actual power is lowered
  // at runtime on strong links (good ACK SNR), saving TX energy. Never persisted
  // below the ceiling, so disabling restores the user's configured power.
  uint8_t  tx_apc;

  // Auto-resend for on-device DMs: number of extra send attempts (0..5) made when
  // no end-to-end ACK arrives before the deadline, before the delivery marker
  // shows ✗. 0 = no auto-resend (single attempt). Default 2.
  uint8_t  dm_resend_count;

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

  // Trail auto-pause — when tracking, automatically freeze the trail (timer +
  // sampling) after the device has sat still for this long, and resume on the
  // next real movement. 0 = off. Index into trailAutoPauseSecs.
  uint8_t  trail_autopause_idx;

  // Locator proximity beeper — when on (and the alert is armed with a target),
  // the device ticks while inside the radius and shortens the gap between ticks
  // the closer it gets to the target, like a homing beeper. Independent of the
  // discrete arrive/leave alert (locator_mode).
  uint8_t  locator_beeper;       // 0=off (default), 1=on

  // GPS averaging for waypoint marking — when set, "Mark here" samples the GPS
  // fix for this many seconds and stores the mean position, for a more accurate
  // mark than a single instantaneous fix. 0 = off (instant mark, the default).
  // Index into gpsAvgSecs(). [Tools › Trail › Settings › Mark avg]
  uint8_t  gps_avg_idx;

  // Alarm clock — a single one-shot wake alarm, configured from the Clock page
  // (Enter › Alarm). Stored as a local time-of-day; the actual fire instant is
  // (re)computed as an absolute time in tickBackground(), which is what makes it
  // robust to RTC re-syncs: the mesh (every inbound packet), the companion app,
  // GPS and the CLI can all jump getCurrentTime() at any moment, but an absolute
  // target instant stays correct across small corrections and still fires (late)
  // if the clock jumps over it. Fires once, then alarm_on clears. The minutnik
  // (countdown) and stoper (stopwatch) are runtime-only and not persisted.
  uint8_t  alarm_on;    // 0=off (default), 1=armed (one-shot)
  uint8_t  alarm_hour;  // 0-23, local time
  uint8_t  alarm_min;   // 0-59

  // Pomodoro timer — Work/Short-Break/Long-Break cycle, configured and driven
  // from the Clock page (Enter › Pomodoro). Only the durations are persisted;
  // the running phase/cycle/deadline are runtime-only, same as the countdown
  // Timer. loadPrefsInt() clamps 0/out-of-range (never configured, or a
  // pre-0x1A file) back to these defaults.
  uint8_t  pomodoro_work_min;        // 1-99, default 25
  uint8_t  pomodoro_short_break_min; // 1-99, default 5
  uint8_t  pomodoro_long_break_min;  // 1-99, default 15
  uint8_t  pomodoro_cycles;          // work sessions before a long break, 1-9, default 4

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

  // Tail sentinel written at the end of /new_prefs. Bump the low byte when
  // adding/removing/reordering fields in DataStore::savePrefs/loadPrefsInt so
  // older saves are detected on load and skipped (zero-init defaults kept).
  // High 24 bits identify the file format; low byte is the schema revision.
  static const uint32_t SCHEMA_SENTINEL = 0xC0DE001A;

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
//   1. add the field's rd(...)   in DataStore::loadPrefsInt(), in struct order
//   2. add the field's write(...) in DataStore::savePrefs(),   in struct order
//   3. clamp it on load (an upgrader's file lacks it → stray bytes)
//   4. bump SCHEMA_SENTINEL's low byte
// (Padding can also shift sizeof; a "false" trip just means re-check + rebump.)
static_assert(sizeof(NodePrefs) == 2504,
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
