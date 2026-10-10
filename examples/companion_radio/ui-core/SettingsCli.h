#pragma once
// The companion CLI's settings keys, after MyMesh's own upstream-named
// commands (MyMeshCli.h): "get <key>", "set <key> <value>", "keys [page]".
// Each key names a row of the settings schema (SettingsSchema.h), so the CLI
// reads and writes what the Settings screens show, with the same values and
// side effects. A value is set by its label as `get` prints it ("2 h",
// "Everyone"; case doesn't matter), "on" / "off" for a switch, or a number:
// the index for a list, one of the stored values for a table (seconds, mV ...).

#include "SettingsSchema.h"

namespace settingscli {

struct Key { const char* key; uint16_t offset; uint8_t bit; };
#define K(name, f)        { name, (uint16_t)offsetof(NodePrefs, f), 0 }
#define KB(name, f, mask) { name, (uint16_t)offsetof(NodePrefs, f), mask }
static const Key KEYS[] = {
  K("advert.interval", advert_auto_interval_sec), K("advert.loc", advert_loc_policy),
  K("telemetry.base", telemetry_mode_base), K("telemetry.loc", telemetry_mode_loc), K("telemetry.env", telemetry_mode_env),
  K("autoadd", manual_add_contacts), KB("autoadd.chat", autoadd_config, 0x02), KB("autoadd.repeater", autoadd_config, 0x04),
  KB("autoadd.room", autoadd_config, 0x08), KB("autoadd.sensor", autoadd_config, 0x10),
  KB("autoadd.overwrite", autoadd_config, 0x01), K("autoadd.max.hops", autoadd_max_hops),
  K("contacts.expiry", contact_expiry_idx), K("contacts.fav.first", fav_sort), K("dm.resend", dm_resend_count),
  K("display.brightness", display_brightness), K("display.off", auto_off_secs), K("display.wake", msg_wake),
  K("display.alert", msg_alert), K("lock", auto_lock), K("lock.clock", lock_compact),
  K("batt.display", batt_display_mode), K("batt.shutdown", low_batt_mv), K("gps.interval", gps_interval),
  K("clock.12h", clock_12h), K("clock.seconds", clock_hide_seconds), K("units.imperial", units_imperial),
  K("sound.volume", buzzer_volume), K("quiet", quiet_hours), K("quiet.from", quiet_from), K("quiet.until", quiet_to),
  K("melody.dm", notif_melody_dm), K("melody.channel", notif_melody_ch), K("melody.advert", notif_melody_ad),
  K("advert.sound", advert_sound_scope),
  K("trail.spacing", trail_min_delta_idx), K("trail.pause", trail_autopause_idx), K("trail.lowbatt", trail_autosave_lowbatt),
  K("gps.avg", gps_avg_idx),
  K("share.others", track_shared_loc), K("share.stop", loc_share_duration_idx), K("share.move", loc_share_move_idx),
  K("share.every", loc_share_interval_idx), K("share.heartbeat", loc_share_heartbeat_idx),
  K("alert", locator_enabled), K("alert.radius", locator_radius_idx), K("alert.on", locator_mode),
  K("alert.beeper", locator_beeper),
};
#undef K
#undef KB
static const int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);

// The schema row of a key, or -1 (this board leaves it out).
static int row(const Key& k) {
  for (int i = 0; i < settings::COUNT; i++)
    if (settings::ALL[i].offset == k.offset && settings::ALL[i].bit == k.bit) return i;
  return -1;
}

static bool sameText(const char* a, const char* b) {
  for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

// The value text into index v; false when it is none of them.
static bool parse(const NodePrefs& p, const settings::Setting& st, const char* v, int& index) {
  if (!st.option) {   // a switch
    if (sameText(v, "on") || !strcmp(v, "1")) { index = 1; return true; }
    if (sameText(v, "off") || !strcmp(v, "0")) { index = 0; return true; }
    return false;
  }
  char buf[24];
  for (int i = 0; i < st.count; i++) {
    st.option((uint8_t)i, buf, sizeof(buf), p);
    if (sameText(v, buf)) { index = i; return true; }
  }
  char* end;
  double d = strtod(v, &end);
  if (end == v || *end) return false;
  if (!st.values) {   // a list: its index
    if (d < 0 || d >= st.count || d != (int)d) return false;
    index = (int)d;
    return true;
  }
  // A table: one of its stored values (one between would read back as another).
  int32_t raw = (int32_t)(st.size == settings::FLOAT_X100 ? d * 100 + (d < 0 ? -0.5 : 0.5) : d);
  for (int i = 0; i < st.count; i++) if (st.values[i] == raw) { index = i; return true; }
  return false;
}

// The labels a setting takes, "a, b, c" into buf.
static void choices(const NodePrefs& p, const settings::Setting& st, char* out, int n) {
  if (!st.option) { snprintf(out, n, "on, off"); return; }
  int o = 0;
  char buf[24];
  for (int i = 0; i < st.count && o < n - 1; i++) {
    st.option((uint8_t)i, buf, sizeof(buf), p);
    o += snprintf(out + o, n - o, "%s%s", i ? ", " : "", buf);
  }
}

// True when the command was a settings one (reply set either way).
static bool handle(UiCore& core, const char* cmd, char* reply, int n) {
  NodePrefs* p = core.prefs();
  if (!p) return false;
  if (!strcmp(cmd, "keys") || !strncmp(cmd, "keys ", 5)) {   // the key names, a frame's worth a page
    int page = cmd[4] ? atoi(cmd + 5) : 1, pg = 1, o = 0;
    bool more = false;
    reply[0] = 0;
    for (int k = 0; k < KEY_COUNT; k++) {
      if (row(KEYS[k]) < 0) continue;
      int len = (int)strlen(KEYS[k].key) + 1;
      if (o + len > n - 20) {   // room left for " (more: keys N)"
        if (pg == page) { more = true; break; }
        pg++;
        o = 0;
      }
      if (pg == page) o += snprintf(reply + o, n - o, "%s%s", o ? " " : "", KEYS[k].key);
      else o += len;
    }
    if (more) snprintf(reply + o, n - o, " (more: keys %d)", page + 1);
    else if (pg < page || page < 1) snprintf(reply, n, "No more keys");
    return true;
  }
  bool set = !strncmp(cmd, "set ", 4);
  if (!set && strncmp(cmd, "get ", 4)) return false;
  const char* name = cmd + 4;
  const char* value = set ? strchr(name, ' ') : nullptr;
  size_t name_len = value ? (size_t)(value - name) : strlen(name);
  for (int k = 0; k < KEY_COUNT; k++) {
    if (strlen(KEYS[k].key) != name_len || strncmp(KEYS[k].key, name, name_len)) continue;
    int i = row(KEYS[k]);
    if (i < 0) { snprintf(reply, n, "Error, not on this device"); return true; }
    const settings::Setting& st = settings::ALL[i];
    char buf[24];
    if (!set) {
      snprintf(reply, n, "> %s", settings::text(*p, st, buf, sizeof(buf), "on", "off"));
      return true;
    }
    if (!value) { snprintf(reply, n, "Error, no value"); return true; }
    int index = 0;
    if (!parse(*p, st, value + 1, index)) {
      char opts[120];
      choices(*p, st, opts, sizeof(opts));
      snprintf(reply, n, "Error, one of: %s", opts);
      return true;
    }
    settings::set(*p, st, (uint8_t)index);
    if (st.changed) st.changed(core);
    the_mesh.savePrefs();
    snprintf(reply, n, "OK");
    return true;
  }
  return false;   // not a settings key: MyMesh says "Unknown command"
}

}  // namespace settingscli
