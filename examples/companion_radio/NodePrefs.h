#pragma once
#include <cstdint>
#include <stdio.h>

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
  uint8_t  bot_channel_idx;     // channel index for channel bot
  char     bot_trigger[64];     // trigger phrase (case-insensitive contains match)
  char     bot_reply_dm[140];   // auto-reply text for DM
  char     bot_reply_ch[140];   // auto-reply text for channel
  uint8_t  clock_hide_seconds; // 0=show HH:MM:SS/refresh 1s (default), 1=hide/refresh 60s
  uint8_t  clock_12h;          // 0=24h (default), 1=12h with AM/PM
  uint8_t  buzzer_auto;        // 0=manual (default), 1=auto-mute when BT connected
  struct DmNotifEntry { uint8_t prefix[4]; uint8_t state; }; // state: 0=default,1=muted,2=force-on
  static const int DM_NOTIF_TABLE_MAX = 16;
  DmNotifEntry dm_notif[DM_NOTIF_TABLE_MAX]; // 16*5 = 80 bytes
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
  uint64_t ch_notif_melody_set;  // bit i = channel i has explicit melody
  uint64_t ch_notif_melody_2;    // bit i = use melody 2 (else melody 1, when set bit is set)
  // Per-DM melody table
  struct DmMelodyEntry { uint8_t prefix[4]; uint8_t slot; }; // slot: 0=global,1=melody1,2=melody2
  static const int DM_MELODY_TABLE_MAX = 16;
  DmMelodyEntry dm_melody[DM_MELODY_TABLE_MAX];
  uint8_t  use_lemon_font;      // 0=default Adafruit font, 1=Lemon font (Unicode, pixel-accurate wrap)
  uint8_t  display_rotation;    // 0-3; only used on e-ink displays
  // Home screen page order: each byte = HomePageBit + 1. 0 terminates the list.
  // Validity gated by page_order_set magic (see below) — not by entry value range,
  // so a junk byte in 1..HPB_COUNT cannot trigger custom-order mode.
  // (Array length = HPB_COUNT, declared as literal so the field offset is stable
  // across builds that add new HomePageBit entries.)
  uint8_t  page_order[11];
  uint8_t  joystick_rotation;   // 0-3 steps CW; independent of display_rotation
  uint8_t  eink_full_refresh_every; // index into {0,5,10,20,30}: full refresh every N partials (0=off)
  uint8_t  page_order_set;      // 0xA5 = page_order is user-configured; anything else = use default
  static const uint8_t PAGE_ORDER_MAGIC = 0xA5;

  // Favourites dial: 6 pinned contacts, stored as first 6 bytes of pub_key per slot.
  // All-zero = empty slot (probability a real pub_key starts with 6 zero bytes is 2^-48).
  // Layout transposes between landscape (3×2) and portrait (2×3).
  static const uint8_t FAVOURITES_COUNT = 6;
  static const uint8_t FAVOURITE_PREFIX_LEN = 6;
  uint8_t favourite_contacts[FAVOURITES_COUNT][FAVOURITE_PREFIX_LEN];

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
  // CAD-windowed receive (battery saver): 0=continuous RX (default), 1=on.
  // Periodically scans for activity and idles between scans instead of
  // listening continuously — cuts average RX current at the cost of a little
  // receive latency. See RadioLibWrapper power-save.
  uint8_t  rx_powersave;

  // Tail sentinel written at the end of /new_prefs. Bump the low byte when
  // adding/removing/reordering fields in DataStore::savePrefs/loadPrefsInt so
  // older saves are detected on load and skipped (zero-init defaults kept).
  // High 24 bits identify the file format; low byte is the schema revision.
  static const uint32_t SCHEMA_SENTINEL = 0xC0DE0008;

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
    HPB_COUNT      = 12,
  };

  // Length of the persisted page_order[] array. Stable across firmware versions
  // (don't grow without a schema migration). When HPB_COUNT exceeds this, extra
  // visible pages are appended at navigation time via buildVisibleOrder fallback.
  static const uint8_t PAGE_ORDER_LEN = 11;

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
  static const uint16_t HP_ALL        = 0x01FF | HP_FAVOURITES;

  // Label for home page by bit-index; returns "" for out-of-range.
  // Array indices match HomePageBit values.
  static const char* homePageLabel(uint8_t bit) {
    static const char* labels[HPB_COUNT] = {
      "Clock", "Recent", "Radio", "Bluetooth", "Advert",
      "GPS", "Sensors", "Tools", "Shutdown", "Settings", "Messages", "Favourites"
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
