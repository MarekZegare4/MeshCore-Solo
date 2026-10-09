#pragma once
// Declarative settings (docs/solo/developer/ui-core.md, step 4): one table of
// NodePrefs options with their labels, value lists and side effects, so every
// frontend renders the same settings without its own per-item code. ui-lvgl
// draws it as switches and dropdowns, one page per Page below; ui-new still has
// its hand-written screens and moves over later.
//
// Each entry picks one of `count` values. By default the NodePrefs field is a
// uint8_t holding that index; with a `values` table the field (1, 2 or 4 bytes,
// `size`) holds values[index] instead -- seconds, millivolts, an hour offset,
// or {1, 0} for a switch whose field is stored inverted. count == 2 with no
// option labeller is an on/off switch. A `bit` mask makes it a switch over that
// one bit of a uint8_t field; size FLOAT_X100 is a float field with values in
// hundredths.

#include <stddef.h>
#include "../NodePrefs.h"
#include "../Trail.h"
#include "SoundControl.h"

namespace settings {

// Pages (one screen each) and the sections on them, in display order.
// PG_NAV is the Map's options (its trail / live share / target tools),
// PG_ADVERT sits in Nearby's advert popup; the rest are Settings pages.
// PG_PRIVACY and PG_RADIO (Radio's advanced options) are Settings pages too.
enum Page : uint8_t { PG_NAV, PG_ADVERT, PG_DEVICE, PG_MESSAGES, PG_SOUND, PG_PRIVACY, PG_RADIO, PG_COUNT };
enum Section : uint8_t { SEC_TRAIL, SEC_LIVE_SHARE, SEC_LOCATOR, SEC_ADVERT,
                         SEC_DISPLAY, SEC_POWER, SEC_TIME, SEC_UNITS,
                         SEC_MESSAGES, SEC_CONTACTS, SEC_AUTO_ADD,
                         SEC_SOUND, SEC_SOUND_FOR, SEC_QUIET,
                         SEC_PRIVACY, SEC_TELEMETRY, SEC_RADIO_ADV, SEC_COUNT };

static const char* pageTitle(uint8_t p) {
  static const char* T[PG_COUNT] = { "Map options", "Advert", "Display & power", "Messages & contacts", "Sound",
                                     "Privacy", "Advanced radio" };
  return p < PG_COUNT ? T[p] : "";
}
static const char* sectionTitle(uint8_t s) {
  static const char* T[SEC_COUNT] = { "TRAIL", "LIVE SHARE", "ARRIVAL ALERT", "AUTOMATIC",
                                      "DISPLAY", "POWER", "TIME", "UNITS",
                                      "MESSAGES", "CONTACTS", "ADDING CONTACTS",
                                      "SOUND", "PLAYS FOR", "QUIET HOURS",
                                      "ADVERTS", "WHO CAN ASK FOR", "ADVANCED" };
  return s < SEC_COUNT ? T[s] : "";
}
static uint8_t sectionPage(uint8_t s) {
  static const uint8_t P[SEC_COUNT] = { PG_NAV, PG_NAV, PG_NAV, PG_ADVERT,
                                        PG_DEVICE, PG_DEVICE, PG_DEVICE, PG_DEVICE,
                                        PG_MESSAGES, PG_MESSAGES, PG_MESSAGES,
                                        PG_SOUND, PG_SOUND, PG_SOUND,
                                        PG_PRIVACY, PG_PRIVACY, PG_RADIO };
  return s < SEC_COUNT ? P[s] : PG_DEVICE;
}

struct Setting {
  const char* label;
  const char* hint;       // one short line under the label, or nullptr
  uint8_t     section;
  uint16_t    offset;     // offsetof(NodePrefs, field)
  uint8_t     count;      // number of values
  // Label for value v (nullptr: an on/off switch).
  void (*option)(uint8_t v, char* buf, int n, const NodePrefs& p);
  // Side effect after a change (nullptr: none). The caller saves the prefs.
  void (*changed)(UiCore& core);
  const int32_t* values;  // stored value per index; nullptr: the index itself
  uint8_t     size;       // field bytes (1, 2, 4), or FLOAT_X100
  uint8_t     bit;        // nonzero: a switch over this bit of a uint8_t field
};
static const uint8_t FLOAT_X100 = 0x84;   // size: a float, values in hundredths

// ── Value labels ──────────────────────────────────────────────────────────────
#ifndef TRAIL_FIXED_MIN_DELTA_M
static void optMinDelta(uint8_t v, char* b, int n, const NodePrefs& p) {
  snprintf(b, n, "%s", TrailStore::minDeltaLabel(v, p.units_imperial));
}
#endif
static void optAutoPause(uint8_t v, char* b, int n, const NodePrefs&) {
  uint16_t s = NodePrefs::trailAutoPauseSecs(v);
  if (s == 0) snprintf(b, n, "Off"); else snprintf(b, n, "%u min", (unsigned)(s / 60));
}
static void optGpsAvg(uint8_t v, char* b, int n, const NodePrefs&) {
  uint16_t s = NodePrefs::gpsAvgSecs(v);
  if (s == 0) snprintf(b, n, "Off"); else snprintf(b, n, "%u s", (unsigned)s);
}
static void optDuration(uint8_t v, char* b, int n, const NodePrefs&) {
  snprintf(b, n, "%u h", (unsigned)(NodePrefs::locShareDurationMins(v) / 60));
}
static void optMove(uint8_t v, char* b, int n, const NodePrefs&) {
  snprintf(b, n, "%u m", (unsigned)NodePrefs::locShareMoveMeters(v));
}
static void optGap(uint8_t v, char* b, int n, const NodePrefs&) {
  uint16_t s = NodePrefs::locShareIntervalSecs(v);
  if (s < 60) snprintf(b, n, "%u s", (unsigned)s); else snprintf(b, n, "%u min", (unsigned)(s / 60));
}
static void optHeartbeat(uint8_t v, char* b, int n, const NodePrefs&) {
  uint16_t s = NodePrefs::locShareHeartbeatSecs(v);
  if (s == 0) snprintf(b, n, "Off"); else snprintf(b, n, "%u min", (unsigned)(s / 60));
}
static void optRadius(uint8_t v, char* b, int n, const NodePrefs&) {
  uint16_t m = NodePrefs::locatorRadiusMeters(v);
  if (m < 1000) snprintf(b, n, "%u m", (unsigned)m); else snprintf(b, n, "%u km", (unsigned)(m / 1000));
}
static void optLocMode(uint8_t v, char* b, int n, const NodePrefs&) {
  snprintf(b, n, "%s", NodePrefs::locatorModeLabel(v));
}
static void optBrightness(uint8_t v, char* b, int n, const NodePrefs&) {
  static const char* L[5] = { "Lowest", "Low", "Medium", "High", "Max" };
  snprintf(b, n, "%s", L[v < 5 ? v : 2]);
}
// Seconds -> "30 s" / "2 min" / "1 h"; 0 reads as `zero`.
static void fmtSecs(char* b, int n, int32_t s, const char* zero) {
  if (s == 0) snprintf(b, n, "%s", zero);
  else if (s < 60) snprintf(b, n, "%ld s", (long)s);
  else if (s < 3600) snprintf(b, n, "%ld min", (long)(s / 60));
  else snprintf(b, n, "%ld h", (long)(s / 3600));
}
static const int32_t AUTO_OFF[] = { 5, 15, 30, 60, 120, 300, 0 };
static void optAutoOff(uint8_t v, char* b, int n, const NodePrefs&) { fmtSecs(b, n, AUTO_OFF[v], "Never"); }
static const int32_t GPS_DUTY[] = { 0, 60, 300, 900, 1800, 3600 };
static void optGpsDuty(uint8_t v, char* b, int n, const NodePrefs&) { fmtSecs(b, n, GPS_DUTY[v], "Always on"); }
static const int32_t LOW_BATT[] = { 0, 3000, 3100, 3200, 3300, 3400, 3500 };
static void optLowBatt(uint8_t v, char* b, int n, const NodePrefs&) {
  if (LOW_BATT[v] == 0) snprintf(b, n, "Off");
  else snprintf(b, n, "%ld.%ld V", (long)(LOW_BATT[v] / 1000), (long)(LOW_BATT[v] % 1000 / 100));
}
static const int32_t TZ[] = { -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0,
                              1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14 };
static void optTz(uint8_t v, char* b, int n, const NodePrefs&) {
  if (TZ[v] == 0) snprintf(b, n, "UTC"); else snprintf(b, n, "UTC%+ld", (long)TZ[v]);
}
// The same steps as ui-new's Tools > Auto-Advert.
static const int32_t AUTO_ADVERT[] = { 0, 30, 60, 120, 300, 600, 1800, 3600 };
static void optAutoAdvert(uint8_t v, char* b, int n, const NodePrefs&) { fmtSecs(b, n, AUTO_ADVERT[v], "Off"); }
static const int32_t INVERTED[] = { 1, 0 };   // switch over a field stored as "off" flag
static void optResend(uint8_t v, char* b, int n, const NodePrefs&) {
  if (v == 0) snprintf(b, n, "Off"); else snprintf(b, n, "%u more", (unsigned)v);
}
static void optBattDisplay(uint8_t v, char* b, int n, const NodePrefs&) {
  static const char* L[] = { "Icon", "Percent", "Voltage" };
  snprintf(b, n, "%s", L[v < 3 ? v : 0]);
}
static void optMsgAlert(uint8_t v, char* b, int n, const NodePrefs&) {
  static const char* const L[] = { "Normal", "Compact", "Off" };
  snprintf(b, n, "%s", L[v < 3 ? v : 0]);
}
static void optLockLook(uint8_t v, char* b, int n, const NodePrefs&) { snprintf(b, n, "%s", v ? "Compact" : "Big"); }
static void optExpiry(uint8_t v, char* b, int n, const NodePrefs&) {
  snprintf(b, n, "%s", NodePrefs::contactExpiryLabel(v));
}
static void optVolume(uint8_t v, char* b, int n, const NodePrefs&) {
  static const char* L[5] = { "Quietest", "Quiet", "Medium", "Loud", "Loudest" };
  snprintf(b, n, "%s", L[v < 5 ? v : 4]);
}
static void optHour(uint8_t v, char* b, int n, const NodePrefs& p) {   // "22:00", or "10 PM" with the 12-hour clock
  if (!p.clock_12h) snprintf(b, n, "%02u:00", (unsigned)v);
  else snprintf(b, n, "%u %s", (unsigned)(v % 12 ? v % 12 : 12), v < 12 ? "AM" : "PM");
}
static void optSound(uint8_t v, char* b, int n, const NodePrefs&) { snprintf(b, n, "%s", soundctl::soundLabel(v)); }
static void optAdvertScope(uint8_t v, char* b, int n, const NodePrefs&) {
  snprintf(b, n, "%s", v == ADVERT_SOUND_SCOPE_ZERO_HOP ? "Direct" : "All");
}

static void optTelemetry(uint8_t v, char* b, int n, const NodePrefs&) {
  static const char* const L[] = { "Nobody", "Allowed", "Everyone" };   // TELEM_MODE_*
  snprintf(b, n, "%s", L[v < 3 ? v : 0]);
}
static void optAutoAdd(uint8_t v, char* b, int n, const NodePrefs&) { snprintf(b, n, "%s", v ? "By type" : "All"); }
// autoadd_max_hops: 0 = any, 1 = direct only, N = up to N-1 hops.
static const int32_t MAX_HOPS[] = { 0, 1, 2, 3, 4, 6, 9 };
static void optMaxHops(uint8_t v, char* b, int n, const NodePrefs&) {
  int32_t h = MAX_HOPS[v];
  if (h == 0) snprintf(b, n, "Any");
  else if (h == 1) snprintf(b, n, "Direct");
  else snprintf(b, n, "%ld hop%s", (long)(h - 1), h == 2 ? "" : "s");
}
static void optHashSize(uint8_t v, char* b, int n, const NodePrefs&) { snprintf(b, n, "%u byte%s", v + 1, v ? "s" : ""); }
static const int32_t INT_THRESH[] = { 0, 8, 10, 12, 14, 16, 20 };
static void optIntThresh(uint8_t v, char* b, int n, const NodePrefs&) {
  if (INT_THRESH[v] == 0) snprintf(b, n, "Off"); else snprintf(b, n, "%ld dB", (long)INT_THRESH[v]);
}
static const int32_t RX_DELAY[] = { 0, 200, 500, 1000, 2000 };   // rx_delay_base, x100
static void optRxDelay(uint8_t v, char* b, int n, const NodePrefs&) {
  if (RX_DELAY[v] == 0) snprintf(b, n, "Off"); else snprintf(b, n, "%ld", (long)(RX_DELAY[v] / 100));
}
static const int32_t AIRTIME[] = { 0, 50, 100, 200, 300, 500, 900 };   // airtime_factor, x100
static void optAirtime(uint8_t v, char* b, int n, const NodePrefs&) {
  snprintf(b, n, "%ld.%ld x", (long)(AIRTIME[v] / 100), (long)(AIRTIME[v] % 100 / 10));
}

// ── Side effects ──────────────────────────────────────────────────────────────
static void rearmLocator(UiCore& c)      { c.locator.reset(); }
static void restartShareClock(UiCore& c) { c.live_share.restartClock(); }   // a new length starts over
static void applyDisplay(UiCore& c)      { c.host()->applyDisplayPrefs(); }
static void applyGpsDuty(UiCore& c)      { c.applyGpsInterval(); }
static void applySound(UiCore& c)        { c.host()->applySoundPrefs(); }
static void applyRxGain(UiCore& c)       { if (c.prefs()) radio_driver.setRxBoostedGainMode(c.prefs()->rx_boosted_gain); }

#define NP_OFF(f)  (uint16_t)offsetof(NodePrefs, f)
#define NP_SIZE(f) (uint8_t)sizeof(((NodePrefs*)0)->f)
#define COUNT_OF(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
// Index fields (uint8_t index), switches, and fields holding values[index].
#define IDX(label, hint, sec, f, count, opt, chg) { label, hint, sec, NP_OFF(f), count, opt, chg, nullptr, 1 }
#define SW(label, hint, sec, f, chg)             { label, hint, sec, NP_OFF(f), 2, nullptr, chg, nullptr, 1 }
#define MAP(label, hint, sec, f, vals, opt, chg) { label, hint, sec, NP_OFF(f), COUNT_OF(vals), opt, chg, vals, NP_SIZE(f) }
#define MAPF(label, hint, sec, f, vals, opt)     { label, hint, sec, NP_OFF(f), COUNT_OF(vals), opt, nullptr, vals, FLOAT_X100 }
#define BIT(label, hint, sec, f, mask)           { label, hint, sec, NP_OFF(f), 2, nullptr, nullptr, nullptr, 1, mask }
static const Setting ALL[] = {
#ifndef TRAIL_FIXED_MIN_DELTA_M   // else every sample past a fixed jitter gate is kept
  IDX("Point spacing", "Between trail points", SEC_TRAIL, trail_min_delta_idx,
      TrailStore::MIN_DELTA_COUNT, optMinDelta, nullptr),
#endif
  IDX("Auto-pause", "Pause when standing still", SEC_TRAIL, trail_autopause_idx,
      NodePrefs::TRAIL_AUTOPAUSE_COUNT, optAutoPause, nullptr),
  SW("Save on low battery", "Save it on low battery", SEC_TRAIL, trail_autosave_lowbatt, nullptr),
  IDX("Waypoint averaging", "Average fixes when marking", SEC_TRAIL, gps_avg_idx,
      NodePrefs::GPS_AVG_COUNT, optGpsAvg, nullptr),

  SW("Show others' positions", "Shared positions on the map", SEC_LIVE_SHARE, track_shared_loc, nullptr),
  IDX("Stop sharing after", nullptr, SEC_LIVE_SHARE, loc_share_duration_idx,
      NodePrefs::LOC_SHARE_DURATION_COUNT, optDuration, restartShareClock),
  IDX("Send after moving", nullptr, SEC_LIVE_SHARE, loc_share_move_idx,
      NodePrefs::LOC_SHARE_MOVE_COUNT, optMove, nullptr),
  IDX("At most every", nullptr, SEC_LIVE_SHARE, loc_share_interval_idx,
      NodePrefs::LOC_SHARE_INTERVAL_COUNT, optGap, nullptr),
  IDX("Heartbeat", "Resend while standing still", SEC_LIVE_SHARE, loc_share_heartbeat_idx,
      NodePrefs::LOC_SHARE_HEARTBEAT_COUNT, optHeartbeat, nullptr),

  SW("Arrival alert", "When you reach the target", SEC_LOCATOR, locator_enabled, rearmLocator),
  IDX("Radius", nullptr, SEC_LOCATOR, locator_radius_idx,
      NodePrefs::LOCATOR_RADIUS_COUNT, optRadius, rearmLocator),
  IDX("Alert on", nullptr, SEC_LOCATOR, locator_mode,
      NodePrefs::LOCATOR_MODE_COUNT, optLocMode, rearmLocator),
  SW("Proximity beeper", "Ticks faster when closer", SEC_LOCATOR, locator_beeper, nullptr),

  MAP("Auto-advert", "Your advert, on a timer", SEC_ADVERT, advert_auto_interval_sec, AUTO_ADVERT,
      optAutoAdvert, nullptr),

  IDX("Brightness", nullptr, SEC_DISPLAY, display_brightness, 5, optBrightness, applyDisplay),
  MAP("Screen off after", "Without a touch", SEC_DISPLAY, auto_off_secs, AUTO_OFF, optAutoOff, nullptr),
  SW("Wake on message", "Screen on for new messages", SEC_DISPLAY, msg_wake, nullptr),
  IDX("Message alert", "Banner, corner envelope or none", SEC_DISPLAY, msg_alert, 3, optMsgAlert, nullptr),
  SW("Lock screen", "Slide to unlock after sleep", SEC_DISPLAY, auto_lock, nullptr),
  IDX("Lock clock", "Big, or compact with the clock fields", SEC_DISPLAY, lock_compact, 2, optLockLook, nullptr),
  IDX("Battery display", "In the status bar", SEC_DISPLAY, batt_display_mode, 3, optBattDisplay, nullptr),

  MAP("Battery shutdown", "Power off below this voltage", SEC_POWER, low_batt_mv, LOW_BATT, optLowBatt, nullptr),
  MAP("GPS power saving", "Sleep between fixes", SEC_POWER, gps_interval, GPS_DUTY, optGpsDuty, applyGpsDuty),

  MAP("Time zone", "For the clock and the alarm", SEC_TIME, tz_offset_hours, TZ, optTz, nullptr),
  SW("12-hour clock", "AM / PM instead of 24 h", SEC_TIME, clock_12h, nullptr),
  MAP("Clock seconds", "On the home clock", SEC_TIME, clock_hide_seconds, INVERTED, nullptr, nullptr),

  SW("Imperial units", "Miles and feet", SEC_UNITS, units_imperial, nullptr),

  IDX("Resend direct messages", "Retries without a tick", SEC_MESSAGES, dm_resend_count, 6,
      optResend, nullptr),

  IDX("Contact expiry", "Prune after inactivity", SEC_CONTACTS, contact_expiry_idx,
      NodePrefs::CONTACT_EXPIRY_COUNT, optExpiry, nullptr),
  SW("Favourites first", "In contact and node lists", SEC_CONTACTS, fav_sort, nullptr),

  // manual_add_contacts bit 0: 0 adds every node heard, 1 only the types below.
  IDX("Add heard nodes", "Every node, or only these types", SEC_AUTO_ADD, manual_add_contacts, 2, optAutoAdd, nullptr),
  BIT("Companions", nullptr, SEC_AUTO_ADD, autoadd_config, 0x02),
  BIT("Repeaters", nullptr, SEC_AUTO_ADD, autoadd_config, 0x04),
  BIT("Rooms", nullptr, SEC_AUTO_ADD, autoadd_config, 0x08),
  BIT("Sensors", nullptr, SEC_AUTO_ADD, autoadd_config, 0x10),
  MAP("Up to", "How far away a node may be", SEC_AUTO_ADD, autoadd_max_hops, MAX_HOPS, optMaxHops, nullptr),
  BIT("Replace oldest", "When the list is full; not favourites", SEC_AUTO_ADD, autoadd_config, 0x01),

  // On / Off / Auto spans two fields (soundctl::setMode): the frontend's own row.
  IDX("Volume", nullptr, SEC_SOUND, buzzer_volume, 5, optVolume, applySound),
  SW("Quiet hours", "Muted, screen dark; alarms ring", SEC_QUIET, quiet_hours, nullptr),
  IDX("From", nullptr, SEC_QUIET, quiet_from, 24, optHour, nullptr),
  IDX("Until", nullptr, SEC_QUIET, quiet_to, 24, optHour, nullptr),

  IDX("Direct messages", nullptr, SEC_SOUND_FOR, notif_melody_dm, soundctl::SOUND_COUNT, optSound, nullptr),
  IDX("Channels", nullptr, SEC_SOUND_FOR, notif_melody_ch, soundctl::SOUND_COUNT, optSound, nullptr),
  IDX("Adverts", "A node announcing itself", SEC_SOUND_FOR, notif_melody_ad, soundctl::SOUND_COUNT, optSound, nullptr),
  IDX("Advert sound for", "Direct: no repeaters", SEC_SOUND_FOR, advert_sound_scope, 2, optAdvertScope, nullptr),

  SW("Position in adverts", "Your adverts carry your location", SEC_PRIVACY, advert_loc_policy, nullptr),
  // TELEM_MODE_*: "Allowed" is the contacts given that permission.
  IDX("Status", "Battery; the two below need it", SEC_TELEMETRY, telemetry_mode_base, 3, optTelemetry, nullptr),
  IDX("Location", nullptr, SEC_TELEMETRY, telemetry_mode_loc, 3, optTelemetry, nullptr),
  IDX("Sensors", nullptr, SEC_TELEMETRY, telemetry_mode_env, 3, optTelemetry, nullptr),

  IDX("Path hash size", "Bytes per repeater in a path", SEC_RADIO_ADV, path_hash_mode, 3, optHashSize, nullptr),
  SW("Double ACK", "Send each acknowledgement twice", SEC_RADIO_ADV, multi_acks, nullptr),
  SW("Listen before talk", "Wait for a quiet channel (CAD)", SEC_RADIO_ADV, cad_enabled, nullptr),
  MAP("Interference", "Busy above the noise floor by", SEC_RADIO_ADV, interference_threshold, INT_THRESH,
      optIntThresh, nullptr),
#if defined(USE_SX1262) || defined(USE_SX1268) || defined(USE_LR2021)
  SW("Boosted RX gain", "Hears more, uses a little more power", SEC_RADIO_ADV, rx_boosted_gain, applyRxGain),
#endif
  MAPF("RX delay", "Repeaters wait to pick the best path", SEC_RADIO_ADV, rx_delay_base, RX_DELAY, optRxDelay),
  MAPF("Airtime factor", "Pause after sending, times airtime", SEC_RADIO_ADV, airtime_factor, AIRTIME, optAirtime),
};
static const int COUNT = (int)(sizeof(ALL) / sizeof(ALL[0]));

// The label on a small screen (ui-new: about ten characters beside an
// eight-character value); a setting not listed uses its own.
static const struct { uint16_t offset; const char* text; } SHORT_LABELS[] = {
  { NP_OFF(display_brightness), "Bright" },      { NP_OFF(auto_off_secs), "Auto off" },
  { NP_OFF(msg_wake), "Msg wake" },              { NP_OFF(auto_lock), "Auto lock" },
  { NP_OFF(msg_alert), "Msg alert" },
  { NP_OFF(batt_display_mode), "Batt disp" },    { NP_OFF(low_batt_mv), "Low batt" },
  { NP_OFF(gps_interval), "GPS pwr" },           { NP_OFF(clock_12h), "12h clock" },
  { NP_OFF(clock_hide_seconds), "Seconds" },     { NP_OFF(units_imperial), "Imperial" },
  { NP_OFF(dm_resend_count), "Resend" },         { NP_OFF(contact_expiry_idx), "Expire" },
  { NP_OFF(fav_sort), "Favs top" },              { NP_OFF(buzzer_volume), "Buzzer vol" },
  { NP_OFF(quiet_hours), "Quiet hrs" },          { NP_OFF(quiet_from), " from" },
  { NP_OFF(quiet_to), " until" },                { NP_OFF(notif_melody_dm), "DM sound" },
  { NP_OFF(notif_melody_ch), "Ch sound" },       { NP_OFF(notif_melody_ad), "AD sound" },
  { NP_OFF(advert_sound_scope), "AD scope" },
  { NP_OFF(manual_add_contacts), "Auto-add" },   { NP_OFF(autoadd_max_hops), " up to" },
  { NP_OFF(advert_loc_policy), "Advert pos" },   { NP_OFF(telemetry_mode_base), "Ask status" },
  { NP_OFF(telemetry_mode_loc), "Ask loc" },     { NP_OFF(telemetry_mode_env), "Ask sens" },
  { NP_OFF(path_hash_mode), "Hash size" },       { NP_OFF(multi_acks), "Double ACK" },
  { NP_OFF(cad_enabled), "LBT (CAD)" },          { NP_OFF(interference_threshold), "Interf." },
  { NP_OFF(rx_boosted_gain), "RX boost" },       { NP_OFF(rx_delay_base), "RX delay" },
  { NP_OFF(airtime_factor), "Airtime" },
};
static const char* shortLabel(const Setting& s) {
  if (s.bit) {   // the auto-add types share one field
    static const char* const B[] = { "Replace old", " companions", " repeaters", " rooms", " sensors" };
    for (int i = 0; i < 5; i++) if (s.bit == (1 << i)) return B[i];
  }
  for (auto& l : SHORT_LABELS) if (l.offset == s.offset) return l.text;
  return s.label;
}
// The index of the setting over NodePrefs field `offset`; -1 if none.
static int indexOf(uint16_t offset) {
  for (int i = 0; i < COUNT; i++) if (ALL[i].offset == offset) return i;
  return -1;
}
#undef IDX
#undef SW
#undef MAP
#undef MAPF
#undef BIT
#undef COUNT_OF
#undef NP_SIZE
#undef NP_OFF

static int32_t readRaw(const NodePrefs& p, const Setting& s) {
  const uint8_t* f = (const uint8_t*)&p + s.offset;
  if (s.bit) return (f[0] & s.bit) ? 1 : 0;
  if (s.size == FLOAT_X100) { float v; memcpy(&v, f, 4); return (int32_t)(v * 100 + (v < 0 ? -0.5f : 0.5f)); }
  bool sgn = false;   // a table with negative values is over a signed field
  for (uint8_t i = 0; s.values && i < s.count; i++) if (s.values[i] < 0) sgn = true;
  if (s.size == 2) { uint16_t v; memcpy(&v, f, 2); return sgn ? (int32_t)(int16_t)v : (int32_t)v; }
  if (s.size == 4) { uint32_t v; memcpy(&v, f, 4); return (int32_t)v; }
  return sgn ? (int32_t)(int8_t)f[0] : (int32_t)f[0];
}
static void writeRaw(NodePrefs& p, const Setting& s, int32_t v) {
  uint8_t* f = (uint8_t*)&p + s.offset;
  if (s.bit) { f[0] = v ? (f[0] | s.bit) : (f[0] & ~s.bit); return; }
  if (s.size == FLOAT_X100) { float w = v / 100.0f; memcpy(f, &w, 4); return; }
  if (s.size == 2) { uint16_t w = (uint16_t)v; memcpy(f, &w, 2); }
  else if (s.size == 4) { uint32_t w = (uint32_t)v; memcpy(f, &w, 4); }
  else f[0] = (uint8_t)v;
}

// Current index. A stored value outside the table reads as the nearest entry
// (e.g. an auto-off time set on another frontend).
static uint8_t get(const NodePrefs& p, const Setting& s) {
  int32_t raw = readRaw(p, s);
  if (!s.values) return raw >= 0 && raw < s.count ? (uint8_t)raw : 0;
  uint8_t best = 0;
  int32_t best_d = INT32_MAX;
  for (uint8_t i = 0; i < s.count; i++) {
    int32_t d = s.values[i] > raw ? s.values[i] - raw : raw - s.values[i];
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}
static void set(NodePrefs& p, const Setting& s, uint8_t v) {
  if (v >= s.count) return;
  writeRaw(p, s, s.values ? s.values[v] : v);
}
// The current value as text ("UTC+2", "3.4 V"); a switch as `on` / `off`.
static const char* text(const NodePrefs& p, const Setting& s, char* buf, int n,
                        const char* on = "On", const char* off = "Off") {
  uint8_t v = get(p, s);
  if (s.option) s.option(v, buf, n, p);
  else snprintf(buf, n, "%s", v ? on : off);
  return buf;
}
// One step along the values, wrapping; then the setting's side effect.
static void step(NodePrefs& p, const Setting& s, int dir, UiCore& core) {
  set(p, s, (uint8_t)((get(p, s) + s.count + dir) % s.count));
  if (s.changed) s.changed(core);
}

}  // namespace settings
