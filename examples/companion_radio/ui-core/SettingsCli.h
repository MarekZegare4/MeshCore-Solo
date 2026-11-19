#pragma once
// The companion CLI's settings keys, after MyMesh's own upstream-named
// commands (MyMeshCli.h): "get <key>", "set <key> <value>", "keys [page]".
// Each key names a row of the settings schema (SettingsSchema.h), so the CLI
// reads and writes what the Settings screens show, with the same values and
// side effects. A value is set by its label as `get` prints it ("2 h",
// "Everyone"; case doesn't matter), "on" / "off" for a switch, or a number:
// the index for a list, one of the stored values for a table (seconds, mV ...).

#include "SettingsSchema.h"
#include "Battery.h"
#include "BotCommands.h"

namespace settingscli {

struct Key { const char* key; uint16_t offset; uint8_t bit; };
#define K(name, f)        { name, (uint16_t)offsetof(NodePrefs, f), 0 }
#define KB(name, f, mask) { name, (uint16_t)offsetof(NodePrefs, f), mask }
#define KX(name, id)      { name, XKEY, id }   // not a schema row: XKEYS[id]
static const uint16_t XKEY = 0xFFFF;

// Keys the schema has no row kind for (#62): a text (a bot's trigger and
// reply), a list of names picked from a set (the commands a bot answers), or
// one name picked (the channel or room a bot answers in). "opts" gives them as
// "> Page/SECTION|Label|#text:<max bytes>", "...|#list|a|b|c" or
// "...|#pick|a|b|c" -- the value is "get"'s, a text being too long to fit
// beside the rest -- and "set" takes the text, "a,b" or a name.
enum XKind : uint8_t { XK_TEXT, XK_LIST, XK_PICK };
enum XId : uint8_t {
  XB_CH1_CHANNEL, XB_CH1_ALLOWED, XB_CH1_TRIGGER, XB_CH1_REPLY,
  XB_CH2_CHANNEL, XB_CH2_ALLOWED, XB_CH2_TRIGGER, XB_CH2_REPLY,
  XB_ROOM, XB_ROOM_ALLOWED, XB_ROOM_TRIGGER, XB_ROOM_REPLY,
  XB_DM_ALLOWED, XB_DM_TRIGGER, XB_DM_REPLY,
  XS_CHANNELS, XS_DM1, XS_DM2, XS_DM3, XS_MSG, XS_ACK,
  XA_TIME, XA_DAYS, XC_HOME,
};
struct XKey { uint8_t section; const char* label; uint8_t kind; };
static const XKey XKEYS[] = {
  { settings::SEC_BOT_CH1, "Channel", XK_PICK }, { settings::SEC_BOT_CH1, "Allowed", XK_LIST },
  { settings::SEC_BOT_CH1, "Trigger", XK_TEXT }, { settings::SEC_BOT_CH1, "Reply", XK_TEXT },
  { settings::SEC_BOT_CH2, "Channel", XK_PICK }, { settings::SEC_BOT_CH2, "Allowed", XK_LIST },
  { settings::SEC_BOT_CH2, "Trigger", XK_TEXT }, { settings::SEC_BOT_CH2, "Reply", XK_TEXT },
  { settings::SEC_BOT_ROOM, "Room", XK_PICK },   { settings::SEC_BOT_ROOM, "Allowed", XK_LIST },
  { settings::SEC_BOT_ROOM, "Trigger", XK_TEXT }, { settings::SEC_BOT_ROOM, "Reply", XK_TEXT },
  { settings::SEC_BOT_DM, "Allowed", XK_LIST },
  { settings::SEC_BOT_DM, "Trigger", XK_TEXT },   { settings::SEC_BOT_DM, "Reply", XK_TEXT },
  { settings::SEC_SOS_TO, "Channels", XK_LIST },   { settings::SEC_SOS_TO, "Contact 1", XK_PICK },
  { settings::SEC_SOS_TO, "Contact 2", XK_PICK },  { settings::SEC_SOS_TO, "Contact 3", XK_PICK },
  { settings::SEC_SOS_TO, "Message", XK_TEXT },    { settings::SEC_SOS_RUN, "ACK word", XK_TEXT },
  { settings::SEC_ALARM, "Time", XK_TEXT },         { settings::SEC_ALARM, "Days", XK_LIST },
  { settings::SEC_COVERAGE, "Home", XK_TEXT },
};
static const Key KEYS[] = {
  K("advert.interval", advert_auto_interval_sec), K("advert.loc", advert_loc_policy),
  K("advert.loc.precision", advert_loc_prec),
  K("telemetry.base", telemetry_mode_base), K("telemetry.loc", telemetry_mode_loc), K("telemetry.env", telemetry_mode_env),
  K("autoadd", manual_add_contacts), KB("autoadd.chat", autoadd_config, 0x02), KB("autoadd.repeater", autoadd_config, 0x04),
  KB("autoadd.room", autoadd_config, 0x08), KB("autoadd.sensor", autoadd_config, 0x10),
  KB("autoadd.overwrite", autoadd_config, 0x01), K("autoadd.max.hops", autoadd_max_hops),
  K("contacts.expiry", contact_expiry_idx), K("contacts.fav.first", fav_sort), K("contacts.channel.order", ch_sort), K("map.flow", flow_show), K("dm.resend", dm_resend_count), K("messages.keep", msg_keep), K("messages.expire", msg_expire),
  K("display.brightness", display_brightness), K("display.off", auto_off_secs), K("display.wake", msg_wake),
  K("display.alert", msg_alert), K("lock", auto_lock), K("lock.clock", lock_compact),
  K("display.text.size", ui_text_scale), K("display.icon.size", ui_icon_scale),
  K("batt.display", batt_display_mode), K("display.signal", signal_show), K("batt.shutdown", low_batt_mv), K("gps.interval", gps_interval),
  K("clock.12h", clock_12h), K("clock.summer", dst_rule), K("clock.seconds", clock_hide_seconds), K("clock.gps", gps_time_off), K("units.imperial", units_imperial),
  K("sound.volume", buzzer_volume), K("quiet", quiet_hours), K("quiet.from", quiet_from), K("quiet.until", quiet_to),
  K("melody.dm", notif_melody_dm), K("melody.channel", notif_melody_ch), K("melody.advert", notif_melody_ad),
  K("advert.sound", advert_sound_scope),
  K("trail.spacing", trail_min_delta_idx), K("trail.pause", trail_autopause_idx), K("trail.lowbatt", trail_autosave_lowbatt),
  K("gps.avg", gps_avg_idx),
  K("share.others", track_shared_loc), K("share.stop", loc_share_duration_idx), K("share.move", loc_share_move_idx),
  K("share.every", loc_share_interval_idx), K("share.heartbeat", loc_share_heartbeat_idx),
  K("alert", locator_enabled), K("alert.radius", locator_radius_idx), K("alert.on", locator_mode),
  K("alert.beeper", locator_beeper),
  K("repeater", client_repeat), K("repeater.network", repeater_use_profile),
  K("repeater.skip.adverts", repeat_skip_adverts), K("repeater.max.hops", repeat_max_hops),
  K("repeater.yield", repeat_delay_boost), K("repeater.min.snr", repeat_min_snr),
  K("repeater.skip.dups", repeat_suppress_dup), K("repeater.scope.only", repeat_scope_only),
  K("bot.ch1", bot_channel_enabled), KX("bot.ch1.channel", XB_CH1_CHANNEL), K("bot.ch1.commands", bot_commands_ch),
  KX("bot.ch1.allowed", XB_CH1_ALLOWED), K("bot.ch1.actions", bot_actions_ch),
  KX("bot.ch1.trigger", XB_CH1_TRIGGER), KX("bot.ch1.reply", XB_CH1_REPLY),
  K("bot.ch2", bot_ch2_enabled), KX("bot.ch2.channel", XB_CH2_CHANNEL), K("bot.ch2.commands", bot_commands_ch2),
  KX("bot.ch2.allowed", XB_CH2_ALLOWED), K("bot.ch2.actions", bot_actions_ch2),
  KX("bot.ch2.trigger", XB_CH2_TRIGGER), KX("bot.ch2.reply", XB_CH2_REPLY),
  K("bot.room", bot_room_enabled), KX("bot.room.room", XB_ROOM), K("bot.room.commands", bot_commands_room),
  KX("bot.room.allowed", XB_ROOM_ALLOWED), K("bot.room.actions", bot_actions_room),
  KX("bot.room.trigger", XB_ROOM_TRIGGER), KX("bot.room.reply", XB_ROOM_REPLY),
  K("bot.dm", bot_enabled), K("bot.dm.allow", bot_dm_scope), K("bot.dm.position", bot_pos_scope),
  K("bot.dm.commands", bot_commands_enabled), KX("bot.dm.allowed", XB_DM_ALLOWED), K("bot.dm.actions", bot_actions_dm),
  KX("bot.dm.trigger", XB_DM_TRIGGER), KX("bot.dm.reply", XB_DM_REPLY),
  K("bot.quiet.from", bot_quiet_start), K("bot.quiet.to", bot_quiet_end), K("bot.actions.by", bot_actions_scope),
  K("bot.gap", bot_gap_idx),
  K("sos", sos_enabled), K("sos.countdown", sos_countdown_idx),
  KX("sos.channels", XS_CHANNELS), KX("sos.contact1", XS_DM1), KX("sos.contact2", XS_DM2), KX("sos.contact3", XS_DM3),
  KX("sos.msg", XS_MSG), K("sos.repeat", sos_repeat_idx), K("sos.advert", sos_advert), K("sos.beep", sos_beep_idx),
  KX("sos.ack", XS_ACK),
  K("alarm", alarm_on), KX("alarm.time", XA_TIME), KX("alarm.days", XA_DAYS),
#if defined(ESP32) || defined(SIM_PLATFORM)
  K("coverage", cov_on), KX("coverage.home", XC_HOME), K("coverage.home.radius", cov_home_r), K("coverage.upload", cov_auto),
#endif
#if defined(PIN_GPIO1)
  K("gpio.1", gpio1_mode), K("gpio.2", gpio2_mode), K("gpio.3", gpio3_mode), K("gpio.4", gpio4_mode),
#endif
  K("display.rotation", display_rotation), K("display.joystick", joystick_rotation),
  K("display.full.refresh", eink_full_refresh_every), K("display.panel.sleep", eink_panel_sleep),
  K("keyboard.type", keyboard_type), K("keyboard.alphabet", keyboard_main_alphabet),
  K("keyboard.additional", keyboard_alt_alphabet), K("keyboard.cardkb", keyboard_cardkb_compact),
  K("chats.dm", dm_show_all), K("chats.channels", ch_fav_only), K("chats.rooms", room_fav_only),
  K("radio.powersave", rx_powersave),
};
#undef K
#undef KB
#undef KX
static const int KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);

// The schema row of a key, or -1 (this board leaves it out).
static int row(const Key& k) {
  if (k.offset == XKEY) return -1;
  for (int i = 0; i < settings::COUNT; i++)
    if (settings::ALL[i].offset == k.offset && settings::ALL[i].bit == k.bit) return i;
  return -1;
}

// ── XKEYS: texts, lists and picks ────────────────────────────────────────────

static char* xText(NodePrefs* p, uint8_t id, int& cap) {
  switch (id) {
    case XB_CH1_TRIGGER:  cap = sizeof(p->bot_trigger_ch);   return p->bot_trigger_ch;
    case XB_CH1_REPLY:    cap = sizeof(p->bot_reply_ch);     return p->bot_reply_ch;
    case XB_CH2_TRIGGER:  cap = sizeof(p->bot_trigger_ch2);  return p->bot_trigger_ch2;
    case XB_CH2_REPLY:    cap = sizeof(p->bot_reply_ch2);    return p->bot_reply_ch2;
    case XB_ROOM_TRIGGER: cap = sizeof(p->bot_trigger_room); return p->bot_trigger_room;
    case XB_ROOM_REPLY:   cap = sizeof(p->bot_reply_room);   return p->bot_reply_room;
    case XB_DM_TRIGGER:   cap = sizeof(p->bot_trigger);      return p->bot_trigger;
    case XB_DM_REPLY:     cap = sizeof(p->bot_reply_dm);     return p->bot_reply_dm;
    case XS_MSG:          cap = sizeof(p->sos_msg);          return p->sos_msg;
    case XS_ACK:          cap = sizeof(p->sos_ack);          return p->sos_ack;
  }
  cap = 0;
  return nullptr;
}
static uint16_t* xCmdOff(NodePrefs* p, uint8_t id) {
  switch (id) {
    case XB_CH1_ALLOWED:  return &p->bot_cmd_off_ch;
    case XB_CH2_ALLOWED:  return &p->bot_cmd_off_ch2;
    case XB_ROOM_ALLOWED: return &p->bot_cmd_off_room;
    case XB_DM_ALLOWED:   return &p->bot_cmd_off_dm;
  }
  return nullptr;
}

// A list's members by bit: the bot's commands, or the channels by slot (an
// empty slot has no name: false). Its mask: of those on.
// The alarm's days: bit i = 1 << tm_wday, Sunday first.
static const char* const DAYS[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };
static int xListCount(uint8_t id) { return id == XS_CHANNELS ? 64 : id == XA_DAYS ? 7 : botcmd::COUNT; }
static bool xListName(uint8_t id, int i, char* out, size_t n) {
  if (id == XA_DAYS) { snprintf(out, n, "%s", DAYS[i]); return true; }
  if (id != XS_CHANNELS) { snprintf(out, n, "%s", botcmd::CMDS[i].name); return true; }
  ChannelDetails ch;
  if (!the_mesh.getChannel(i, ch) || !ch.name[0]) return false;
  snprintf(out, n, "%s", ch.name);
  return true;
}
static uint64_t xListGet(const NodePrefs* p, uint8_t id) {
  if (id == XS_CHANNELS) return p->sos_ch_mask;
  if (id == XA_DAYS) return p->alarm_repeat_mask;
  return (uint16_t)~*xCmdOff((NodePrefs*)p, id) & (botcmd::flag(botcmd::COUNT) - 1);
}
static void xListSet(NodePrefs* p, uint8_t id, uint64_t on) {
  if (id == XS_CHANNELS) p->sos_ch_mask = on;
  else if (id == XA_DAYS) p->alarm_repeat_mask = (uint8_t)(on & 0x7F);
  else *xCmdOff(p, id) = (uint16_t)(~on & (botcmd::flag(botcmd::COUNT) - 1));
}
static uint8_t* xDm(NodePrefs* p, uint8_t id) {
  return id >= XS_DM1 && id <= XS_DM3 ? p->sos_dm[id - XS_DM1] : nullptr;
}

// A pick's choices by index (channels by slot, rooms in the contact list's
// order) and the one picked, -1 for none.
static bool xPickName(uint8_t id, int i, char* out, size_t n) {
  if (id == XB_ROOM || (id >= XS_DM1 && id <= XS_DM3)) {
    const uint8_t type = id == XB_ROOM ? ADV_TYPE_ROOM : ADV_TYPE_CHAT;
    ContactInfo c;
    int seen = 0;
    for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c); idx++) {
      if (c.type != type) continue;
      if (seen++ == i) { snprintf(out, n, "%s", c.name); return true; }
    }
    return false;
  }
  for (int slot = 0, seen = 0; slot < 64; slot++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(slot, ch)) break;
    if (!ch.name[0]) continue;
    if (seen++ == i) { snprintf(out, n, "%s", ch.name); return true; }
  }
  return false;
}
static bool xPickGet(const NodePrefs* p, uint8_t id, char* out, size_t n) {
  if (id == XB_ROOM || (id >= XS_DM1 && id <= XS_DM3)) {
    const uint8_t* key = id == XB_ROOM ? p->bot_room_prefix : p->sos_dm[id - XS_DM1];
    static const uint8_t ZERO[NodePrefs::FAVOURITE_PREFIX_LEN] = {0};
    if (!memcmp(key, ZERO, sizeof(ZERO))) return false;
    ContactInfo* c = the_mesh.lookupContactByPubKey(key, NodePrefs::FAVOURITE_PREFIX_LEN);
    if (!c || !c->name[0]) return false;
    snprintf(out, n, "%s", c->name);
    return true;
  }
  ChannelDetails ch;
  if (!the_mesh.getChannel(id == XB_CH1_CHANNEL ? p->bot_channel_idx : p->bot_ch2_idx, ch) || !ch.name[0]) return false;
  snprintf(out, n, "%s", ch.name);
  return true;
}
// Picks by name (case ignored); false when there is none of that name.
static bool xPickSet(NodePrefs* p, uint8_t id, const char* name) {
  if (id == XB_ROOM || xDm(p, id)) {
    uint8_t* key = id == XB_ROOM ? p->bot_room_prefix : xDm(p, id);
    if (!strcasecmp(name, "none")) { memset(key, 0, NodePrefs::FAVOURITE_PREFIX_LEN); return true; }
    const uint8_t type = id == XB_ROOM ? ADV_TYPE_ROOM : ADV_TYPE_CHAT;
    ContactInfo c;
    for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c); idx++)
      if (c.type == type && !strcasecmp(c.name, name)) {
        memcpy(key, c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
        return true;
      }
    return false;
  }
  for (int slot = 0; slot < 64; slot++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(slot, ch)) break;
    if (ch.name[0] && !strcasecmp(ch.name, name)) {
      (id == XB_CH1_CHANNEL ? p->bot_channel_idx : p->bot_ch2_idx) = (uint8_t)slot;
      return true;
    }
  }
  return false;
}

static void xGet(NodePrefs* p, uint8_t id, char* reply, int n) {
  const XKey& x = XKEYS[id];
  if (id == XA_TIME) { snprintf(reply, n, "> %02u:%02u", (unsigned)p->alarm_hour, (unsigned)p->alarm_min); return; }
  if (id == XC_HOME) {
    if (!p->cov_home_lat && !p->cov_home_lon) { snprintf(reply, n, "> none"); return; }
    snprintf(reply, n, "> %.6f,%.6f", p->cov_home_lat / 1e6, p->cov_home_lon / 1e6);
    return;
  }
  if (x.kind == XK_TEXT) { int cap; snprintf(reply, n, "> %s", xText(p, id, cap)); return; }
  if (x.kind == XK_PICK) {
    char nm[40];
    snprintf(reply, n, "> %s", xPickGet(p, id, nm, sizeof(nm)) ? nm : "none");
    return;
  }
  const uint64_t on = xListGet(p, id);
  int o = snprintf(reply, n, "> ");
  bool any = false;
  char nm[40];
  for (int i = 0; i < xListCount(id) && o < n - 1; i++)
    if ((on >> i & 1) && xListName(id, i, nm, sizeof(nm))) { o += snprintf(reply + o, n - o, "%s%s", any ? "," : "", nm); any = true; }
  if (!any) snprintf(reply + o, n - o, "none");
}

// The value after "set <key> " (empty: none / an empty text).
static void xSet(UiCore& core, NodePrefs* p, uint8_t id, const char* v, char* reply, int n) {
  const XKey& x = XKEYS[id];
  if (id == XA_TIME) {   // "7:30" / "07:30"
    char* e;
    const long h = strtol(v, &e, 10);
    const long m = *e == ':' ? strtol(e + 1, &e, 10) : -1;
    if (*e || h < 0 || h > 23 || m < 0 || m > 59) { snprintf(reply, n, "Error, a time as HH:MM"); return; }
    p->alarm_hour = (uint8_t)h; p->alarm_min = (uint8_t)m;
  } else if (id == XC_HOME) {   // "50.06143,19.93658" or none
    if (!strcasecmp(v, "none") || !*v) { p->cov_home_lat = p->cov_home_lon = 0; }
    else {
      char* e;
      const double la = strtod(v, &e);
      const double lo = *e == ',' ? strtod(e + 1, &e) : 999;
      if (*e || la < -90 || la > 90 || lo < -180 || lo > 180) { snprintf(reply, n, "Error, lat,lon in degrees or none"); return; }
      p->cov_home_lat = (int32_t)(la * 1e6 + (la < 0 ? -0.5 : 0.5));
      p->cov_home_lon = (int32_t)(lo * 1e6 + (lo < 0 ? -0.5 : 0.5));
    }
  } else if (x.kind == XK_TEXT) {
    int cap;
    char* t = xText(p, id, cap);
    if ((int)strlen(v) >= cap) { snprintf(reply, n, "Error, at most %d bytes", cap - 1); return; }
    snprintf(t, cap, "%s", v);
  } else if (x.kind == XK_PICK) {
    if (!xPickSet(p, id, v)) {
      int o = snprintf(reply, n, "Error, one of: ");
      char nm[40];
      for (int i = 0; xPickName(id, i, nm, sizeof(nm)) && o < n - 1; i++) o += snprintf(reply + o, n - o, "%s%s", i ? ", " : "", nm);
      if (!o || reply[o - 2] == ':') snprintf(reply + o, n - o, "none here");
      return;
    }
  } else {   // a list: what's named is on, the rest off ("none": all off)
    uint64_t on = 0;
    char nm[40];
    if (*v && strcasecmp(v, "none")) {
      while (*v) {
        while (*v == ' ' || *v == ',') v++;
        const char* e = v;
        while (*e && *e != ',') e++;
        const char* t = e;
        while (t > v && t[-1] == ' ') t--;
        if (t == v) { v = e; continue; }
        int hit = -1;
        for (int i = 0; i < xListCount(id); i++)
          if (xListName(id, i, nm, sizeof(nm)) && (int)strlen(nm) == (int)(t - v) && !strncasecmp(nm, v, t - v)) { hit = i; break; }
        if (hit < 0) {
          int o = snprintf(reply, n, "Error, of: ");
          bool first = true;
          for (int i = 0; i < xListCount(id) && o < n - 1; i++)
            if (xListName(id, i, nm, sizeof(nm))) { o += snprintf(reply + o, n - o, "%s%s", first ? "" : ", ", nm); first = false; }
          return;
        }
        on |= (uint64_t)1 << hit;
        v = e;
      }
    }
    xListSet(p, id, on);
  }
  if (id == XA_TIME || id == XA_DAYS) core.clock.onAlarmChanged();
  the_mesh.savePrefs();
  snprintf(reply, n, "OK");
}

static void xOpts(NodePrefs* p, uint8_t id, char* reply, int n) {
  const XKey& x = XKEYS[id];
  int o = snprintf(reply, n, "> %s/%s|%s|", settings::pageTitle(settings::sectionPage(x.section)),
                   settings::sectionTitle(x.section), x.label);
  if (x.kind == XK_TEXT) {
    int cap = id == XA_TIME ? 6 : id == XC_HOME ? 24 : 0;
    if (!cap) xText(p, id, cap);
    snprintf(reply + o, n - o, "#text:%d", cap - 1);
    return;
  }
  o += snprintf(reply + o, n - o, x.kind == XK_LIST ? "#list" : "#pick");
  if (id == XB_ROOM || (id >= XS_DM1 && id <= XS_DM3)) o += snprintf(reply + o, n - o, "|none");   // a contact pick may be cleared
  char nm[40];
  for (int i = 0; o < n - 1; i++) {
    if (x.kind == XK_LIST) { if (i >= xListCount(id)) break; if (!xListName(id, i, nm, sizeof(nm))) continue; }
    else if (!xPickName(id, i, nm, sizeof(nm))) break;
    if (o + 1 + (int)strlen(nm) >= n - 5) { snprintf(reply + o, n - o, "|..."); break; }
    o += snprintf(reply + o, n - o, "|%s", nm);
  }
}

// Coverage's keys are only where a frontend has begun the recorder (ui-lvgl
// with a card): on another ESP32 there is nothing to record or upload. The
// keyboard's not where one button types on its strip (a CardKB brings them).
static bool hereKey(UiCore& core, const Key& k) {
  if (!strncmp(k.key, "coverage", 8)) return core.coverage.available();
  if (!strncmp(k.key, "keyboard.", 9)) return !core.host() || core.host()->hasKeyboard();
  return true;
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

// The quick messages (NodePrefs::custom_msgs, sent from a conversation's
// message list): "msgs" lists the slots in use, "get msg.3" one of them,
// "set msg.3 On my way" sets it ({loc}, {time} ... expand when sent),
// "set msg.3" alone empties it. Numbered from 1, as the device shows them.
static bool quickMessages(NodePrefs* p, const char* cmd, char* reply, int n) {
  static const int COUNT = (int)(sizeof(p->custom_msgs) / sizeof(p->custom_msgs[0]));
  static const int LEN = (int)sizeof(p->custom_msgs[0]);
  if (!strcmp(cmd, "msgs")) {
    int o = 0;
    reply[0] = 0;
    for (int i = 0; i < COUNT && o < n - 1; i++) {
      if (!p->custom_msgs[i][0]) continue;
      const char* m = p->custom_msgs[i];
      int cut = (int)strlen(m) > 24 ? 24 : (int)strlen(m);
      while (cut > 0 && ((uint8_t)m[cut] & 0xC0) == 0x80) cut--;   // not inside a UTF-8 character
      o += snprintf(reply + o, n - o, "%s%d: %.*s%s", o ? " | " : "", i + 1, cut, m, m[cut] ? "..." : "");
    }
    if (!o) snprintf(reply, n, "No quick messages (set msg.1 <text>)");
    return true;
  }
  const bool set = !strncmp(cmd, "set msg.", 8);
  if (!set && strncmp(cmd, "get msg.", 8)) return false;
  char* end = nullptr;
  const long i = strtol(cmd + 8, &end, 10) - 1;
  if (end == cmd + 8 || (*end && *end != ' ')) return false;   // "msg.x": not ours
  if (i < 0 || i >= COUNT) { snprintf(reply, n, "Error, msg.1 to msg.%d", COUNT); return true; }
  if (!set) { snprintf(reply, n, "> %s", p->custom_msgs[i]); return true; }
  const char* text = *end ? end + 1 : "";
  if ((int)strlen(text) >= LEN) { snprintf(reply, n, "Error, at most %d bytes", LEN - 1); return true; }
  snprintf(p->custom_msgs[i], LEN, "%s", text);
  the_mesh.savePrefs();
  snprintf(reply, n, "OK");
  return true;
}

// The two user melodies (the Ringtone editor's), for a configurator: "get
// melody.1" is "> <bpm> <notes>", the notes a byte each in hex (SoundControl.h:
// bits 0-2 pitch, 0 a rest, then c d e f g a b; 3-4 octave - 4; 5-6 length
// 1/4 .. 1/32), "set melody.1 120 2931..." writes one (an empty one with no
// notes). BPM is one of 60 90 120 150 180.
static bool melodies(NodePrefs* p, const char* cmd, char* reply, int n) {
  const bool set = !strncmp(cmd, "set melody.", 11);
  if (!set && strncmp(cmd, "get melody.", 11)) return false;
  const char* a = cmd + 11;
  if ((a[0] != '1' && a[0] != '2') || (a[1] && a[1] != ' ')) return false;   // melody.dm ...: a key
  const int slot = a[0] - '1';
  soundctl::Melody m;
  if (!set) {
    soundctl::load(p, slot, m);
    int o = snprintf(reply, n, "> %u ", soundctl::bpm(m.bpm_idx));
    for (int i = 0; i < m.len && o < n - 2; i++) o += snprintf(reply + o, n - o, "%02x", m.notes[i]);
    return true;
  }
  memset(&m, 0, sizeof(m));
  char* end = nullptr;
  const long bpm = a[1] ? strtol(a + 2, &end, 10) : -1;
  m.bpm_idx = 0xFF;
  for (uint8_t i = 0; i < soundctl::BPM_COUNT; i++) if (soundctl::bpm(i) == bpm) m.bpm_idx = i;
  if (m.bpm_idx == 0xFF) { snprintf(reply, n, "Error, set melody.N <bpm 60/90/120/150/180> <hex notes>"); return true; }
  const char* h = end;
  while (*h == ' ') h++;
  for (; h[0] && h[1]; h += 2) {
    if (m.len >= soundctl::MAX_NOTES) { snprintf(reply, n, "Error, at most %d notes", soundctl::MAX_NOTES); return true; }
    char b[3] = { h[0], h[1], 0 };
    char* e;
    const long v = strtol(b, &e, 16);
    if (*e || v < 0 || v > 0x7F || ((v >> 3) & 3) > soundctl::OCT_MAX - 4) { snprintf(reply, n, "Error, notes are hex bytes 00-7f"); return true; }
    m.notes[m.len++] = (uint8_t)v;
  }
  if (*h) { snprintf(reply, n, "Error, notes are hex bytes 00-7f"); return true; }
  soundctl::store(p, slot, m);
  the_mesh.savePrefs();
  snprintf(reply, n, "OK");
  return true;
}

// The waypoints, for a configurator: "wpts" is "> <count>/<capacity>", "get
// wpt.N" "> <lat>,<lon> <label>", "set wpt.N <lat>,<lon> <label>" replaces one
// or, N one past the last, adds one; "set wpt.N" alone deletes it (those after
// move up). Numbered from 1, as the list shows them.
static bool waypoints(UiCore& core, const char* cmd, char* reply, int n) {
  WaypointModel& wm = core.waypoints;
  if (!strcmp(cmd, "wpts")) { snprintf(reply, n, "> %d/%d", wm.count(), WaypointStore::CAPACITY); return true; }
  const bool set = !strncmp(cmd, "set wpt.", 8);
  if (!set && strncmp(cmd, "get wpt.", 8)) return false;
  char* end = nullptr;
  const long i = strtol(cmd + 8, &end, 10) - 1;
  if (end == cmd + 8 || (*end && *end != ' ')) return false;
  const int cnt = wm.count();
  if (!set && cnt == 0) { snprintf(reply, n, "Error, no waypoints"); return true; }
  if (i < 0 || i > cnt || (!set && i == cnt)) { snprintf(reply, n, "Error, wpt.1 to wpt.%d", set ? cnt + 1 : cnt); return true; }
  if (!set) {
    const Waypoint& w = wm.at(i);
    snprintf(reply, n, "> %.6f,%.6f %s", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6, w.label);
    return true;
  }
  if (!*end) {
    if (i == cnt) { snprintf(reply, n, "Error, no wpt.%ld", i + 1); return true; }
    wm.remove(i);
    snprintf(reply, n, "OK");
    return true;
  }
  char tagged[80], label[WAYPOINT_LABEL_LEN + 16];
  snprintf(tagged, sizeof(tagged), WAYPOINT_MSG_TAG "%s", end + 1);   // "[WAY]lat,lon label", as a message carries it
  int32_t lat, lon;
  if (!geo::parseLatLon(tagged, lat, lon, label, sizeof(label))) { snprintf(reply, n, "Error, set wpt.N <lat>,<lon> <label>"); return true; }
  if (i == cnt) {
    if (!wm.add(lat, lon, rtc_clock.getCurrentTime(), label)) { snprintf(reply, n, "Error, full (%d)", WaypointStore::CAPACITY); return true; }
  } else {
    wm.replace(i, lat, lon, label);
  }
  snprintf(reply, n, "OK");
  return true;
}

// The battery curve (Settings > Battery curve): "get batt.curve" is "> <mV at
// 0 %>,<10 %>,...,<100 %>" (11 values) or "> builtin"; "set batt.curve" takes
// the same list -- rising, 2500..4500 mV -- or "builtin". The whole curve in one
// line: one measured on one device pasted into another.
static bool battCurve(NodePrefs* p, const char* cmd, char* reply, int n) {
  const bool set = !strncmp(cmd, "set batt.curve", 14);
  if (!set && strcmp(cmd, "get batt.curve")) return false;
  if (set && cmd[14] && cmd[14] != ' ') return false;
  if (!set) {
    if (!battery::validCurve(p->batt_curve_mv)) { snprintf(reply, n, "> builtin"); return true; }
    int o = snprintf(reply, n, "> ");
    for (int i = 0; i < battery::CURVE_PTS && o < n; i++) o += snprintf(reply + o, n - o, i ? ",%u" : "%u", p->batt_curve_mv[i]);
    return true;
  }
  const char* a = cmd + 14;
  while (*a == ' ') a++;
  uint16_t c[battery::CURVE_PTS] = {};
  if (!strcmp(a, "builtin")) {
    memset(p->batt_curve_mv, 0, sizeof(p->batt_curve_mv));
  } else {
    int k = 0;
    while (*a && k < battery::CURVE_PTS) {
      char* e;
      const long v = strtol(a, &e, 10);
      if (e == a || v < 0 || v > 65535) break;
      c[k++] = (uint16_t)v;
      a = e;
      while (*a == ' ') a++;
      if (*a == ',') a++;
      while (*a == ' ') a++;
    }
    if (k != battery::CURVE_PTS || *a || !battery::validCurve(c)) {
      snprintf(reply, n, "Error, set batt.curve <11 mV values for 0..100 %%, rising, %d-%d> or builtin",
               battery::CURVE_MIN_MV, battery::CURVE_MAX_MV);
      return true;
    }
    memcpy(p->batt_curve_mv, c, sizeof(c));
  }
  the_mesh.savePrefs();
  snprintf(reply, n, "OK");
  return true;
}

// The scopes the repeater relays besides the default (Repeater > Extra
// scopes, repeat_extra_scope_mask): "get repeater.scopes" is "> a,b" (or
// "> none"), "set repeater.scopes a,b" picks those by name, "none" clears.
// The names are the scope list's (Settings > Radio), which differs per device.
static bool repeaterScopes(NodePrefs* p, const char* cmd, char* reply, int n) {
  const bool set = !strncmp(cmd, "set repeater.scopes", 19) && (cmd[19] == ' ' || !cmd[19]);
  if (!set && strcmp(cmd, "get repeater.scopes")) return false;
  const ScopeList& sl = the_mesh.scopeList();
  if (!set) {
    int o = snprintf(reply, n, "> ");
    bool any = false;
    for (uint8_t i = 0; i < sl.count && o < n - 1; i++)
      if (rptctl::extraScope(p, i)) { o += snprintf(reply + o, n - o, "%s%s", any ? "," : "", sl.name(i + 1)); any = true; }
    if (!any) snprintf(reply + o, n - o, "none");
    return true;
  }
  const char* a = cmd[19] ? cmd + 20 : "";
  uint16_t mask = 0;
  if (*a && strcmp(a, "none")) {
    while (*a) {
      while (*a == ' ' || *a == ',') a++;
      const char* e = a;
      while (*e && *e != ',') e++;
      const char* t = e;
      while (t > a && t[-1] == ' ') t--;   // "a, b": the spaces round a name aren't part of it
      if (t == a) { a = e; continue; }
      int hit = -1;
      for (uint8_t i = 0; i < sl.count; i++) {
        const char* nm = sl.name(i + 1);
        if ((int)strlen(nm) == (int)(t - a) && !strncasecmp(nm, a, t - a)) { hit = i; break; }
      }
      if (hit < 0) {
        int o = snprintf(reply, n, "Error, scopes: ");
        if (!sl.count) snprintf(reply + o, n - o, "none set up (Settings > Radio)");
        for (uint8_t i = 0; i < sl.count && o < n - 1; i++) o += snprintf(reply + o, n - o, "%s%s", i ? ", " : "", sl.name(i + 1));
        return true;
      }
      mask |= (uint16_t)(1u << hit);
      a = e;
    }
  }
  p->repeat_extra_scope_mask = mask;
  the_mesh.rebuildRepeatScopes();
  the_mesh.savePrefs();
  snprintf(reply, n, "OK");
  return true;
}

// True when the command was a settings one (reply set either way).
static bool handle(UiCore& core, const char* cmd, char* reply, int n) {
  NodePrefs* p = core.prefs();
  if (!p) return false;
  if (quickMessages(p, cmd, reply, n)) return true;
  if (melodies(p, cmd, reply, n)) return true;
  if (waypoints(core, cmd, reply, n)) return true;
  if (battCurve(p, cmd, reply, n)) return true;
  if (repeaterScopes(p, cmd, reply, n)) return true;
  if (!strcmp(cmd, "keys") || !strncmp(cmd, "keys ", 5)) {   // the key names, a frame's worth a page
    int page = cmd[4] ? atoi(cmd + 5) : 1, pg = 1, o = 0;
    bool more = false;
    reply[0] = 0;
    for (int k = 0; k < KEY_COUNT; k++) {
      if ((KEYS[k].offset != XKEY && row(KEYS[k]) < 0) || !hereKey(core, KEYS[k])) continue;
      int len = (o ? 1 : 0) + (int)strlen(KEYS[k].key);   // as printed: a space before all but the first
      if (o + len > n - 20) {   // room left for " (more: keys N)"
        if (pg == page) { more = true; break; }
        pg++;
        o = 0;
        len--;   // first on its page: no space
      }
      if (pg == page) snprintf(reply + o, n - o, "%s%s", o ? " " : "", KEYS[k].key);
      o += len;
    }
    if (more) snprintf(reply + o, n - o, " (more: keys %d)", page + 1);
    else if (pg < page || page < 1) snprintf(reply, n, "No more keys");
    return true;
  }
  // "opts <key>": the key as the Settings screen shows it, for a form:
  // "> page/SECTION|label|value|choice|choice...", "on|off" for a switch.
  if (!strncmp(cmd, "opts ", 5)) {
    for (int k = 0; k < KEY_COUNT; k++) {
      if (strcmp(KEYS[k].key, cmd + 5)) continue;
      if (!hereKey(core, KEYS[k])) { snprintf(reply, n, "Error, not on this device"); return true; }
      if (KEYS[k].offset == XKEY) { xOpts(p, KEYS[k].bit, reply, n); return true; }
      int i = row(KEYS[k]);
      if (i < 0) { snprintf(reply, n, "Error, not on this device"); return true; }
      const settings::Setting& st = settings::ALL[i];
      char buf[24];
      int o = snprintf(reply, n, "> %s/%s|%s|%s", settings::pageTitle(settings::sectionPage(st.section)),
                       settings::sectionTitle(st.section), st.label, settings::text(*p, st, buf, sizeof(buf), "on", "off"));
      if (!st.option) { snprintf(reply + o, n - o, "|on|off"); return true; }
      for (int v = 0; v < st.count && o < n - 1; v++) {
        st.option((uint8_t)v, buf, sizeof(buf), *p);
        if (o + 1 + (int)strlen(buf) >= n - 1) { snprintf(reply + o, n - o, "|..."); break; }
        o += snprintf(reply + o, n - o, "|%s", buf);
      }
      return true;
    }
    snprintf(reply, n, "Error, no key %s (see keys)", cmd + 5);
    return true;
  }
  bool set = !strncmp(cmd, "set ", 4);
  if (!set && strncmp(cmd, "get ", 4)) return false;
  const char* name = cmd + 4;
  const char* value = set ? strchr(name, ' ') : nullptr;
  size_t name_len = value ? (size_t)(value - name) : strlen(name);
  for (int k = 0; k < KEY_COUNT; k++) {
    if (strlen(KEYS[k].key) != name_len || strncmp(KEYS[k].key, name, name_len)) continue;
    if (!hereKey(core, KEYS[k])) { snprintf(reply, n, "Error, not on this device"); return true; }
    if (KEYS[k].offset == XKEY) {
      if (set) xSet(core, p, KEYS[k].bit, value ? value + 1 : "", reply, n);
      else xGet(p, KEYS[k].bit, reply, n);
      return true;
    }
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
