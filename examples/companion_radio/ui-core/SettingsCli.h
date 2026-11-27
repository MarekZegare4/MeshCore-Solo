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
#include "Favourites.h"   // Home's favourite slots

namespace settingscli {

struct Key { const char* key; uint16_t offset; uint8_t bit; };
#define K(name, f)        { name, (uint16_t)offsetof(NodePrefsFields, f), 0 }
#define KB(name, f, mask) { name, (uint16_t)offsetof(NodePrefsFields, f), mask }
#define KX(name, id)      { name, XKEY, id }   // not a schema row: XKEYS[id]
static const uint16_t XKEY = 0xFFFF;

// Keys the schema has no row kind for (#62): a text (a bot's trigger and
// reply), a list of names picked from a set (the commands a bot answers), or
// one name picked (the channel or room a bot answers in). "opts" gives them as
// "> Page/SECTION|Label|#text:<max bytes>", "...|#list|a|b|c" or
// "...|#pick|a|b|c" -- the value is "get"'s, a text being too long to fit
// beside the rest -- and "set" takes the text, "a,b" or a name.
enum XKind : uint8_t { XK_TEXT, XK_LIST, XK_PICK };
// Each one once: its id, section, label and kind (the XId enum and XKEYS
// are both made from this list, so they can't fall out of step).
#define XKEY_LIST(X) \
  X(XB_CH1_CHANNEL, SEC_BOT_CH1, "Channel", XK_PICK)  X(XB_CH1_ALLOWED, SEC_BOT_CH1, "Allowed", XK_LIST) \
  X(XB_CH1_TRIGGER, SEC_BOT_CH1, "Trigger", XK_TEXT)  X(XB_CH1_REPLY, SEC_BOT_CH1, "Reply", XK_TEXT) \
  X(XB_CH2_CHANNEL, SEC_BOT_CH2, "Channel", XK_PICK)  X(XB_CH2_ALLOWED, SEC_BOT_CH2, "Allowed", XK_LIST) \
  X(XB_CH2_TRIGGER, SEC_BOT_CH2, "Trigger", XK_TEXT)  X(XB_CH2_REPLY, SEC_BOT_CH2, "Reply", XK_TEXT) \
  X(XB_ROOM, SEC_BOT_ROOM, "Room", XK_PICK)           X(XB_ROOM_ALLOWED, SEC_BOT_ROOM, "Allowed", XK_LIST) \
  X(XB_ROOM_TRIGGER, SEC_BOT_ROOM, "Trigger", XK_TEXT) X(XB_ROOM_REPLY, SEC_BOT_ROOM, "Reply", XK_TEXT) \
  X(XB_DM_ALLOWED, SEC_BOT_DM, "Allowed", XK_LIST) \
  X(XB_DM_TRIGGER, SEC_BOT_DM, "Trigger", XK_TEXT)    X(XB_DM_REPLY, SEC_BOT_DM, "Reply", XK_TEXT) \
  X(XS_CHANNELS, SEC_SOS_TO, "Channels", XK_LIST)     X(XS_DM1, SEC_SOS_TO, "Contact 1", XK_PICK) \
  X(XS_DM2, SEC_SOS_TO, "Contact 2", XK_PICK)         X(XS_DM3, SEC_SOS_TO, "Contact 3", XK_PICK) \
  X(XS_MSG, SEC_SOS_TO, "Message", XK_TEXT)           X(XS_ACK, SEC_SOS_RUN, "ACK word", XK_TEXT) \
  X(XA_TIME, SEC_ALARM, "Time", XK_TEXT)              X(XA_DAYS, SEC_ALARM, "Days", XK_LIST) \
  X(XC_HOME, SEC_COVERAGE, "Home", XK_TEXT) \
  X(XL_TO, SEC_LIVE_SHARE, "Share to", XK_PICK)       X(XL_SCOPE, SEC_LIVE_SHARE, "Scope", XK_PICK) \
  X(XN_FAV, SEC_CHATS, "Favourite channels", XK_LIST) X(XN_UNSAVED, SEC_MESSAGES, "Channels not kept", XK_LIST) \
  X(XN_MUTED, SEC_SOUND_FOR, "Muted channels", XK_LIST) X(XN_ALWAYS, SEC_SOUND_FOR, "Channels always heard", XK_LIST) \
  X(XN_MEL1, SEC_SOUND_FOR, "Channels with melody 1", XK_LIST) X(XN_MEL2, SEC_SOUND_FOR, "Channels with melody 2", XK_LIST) \
  X(XP_FAV, SEC_CHATS, "Favourite contacts", XK_LIST) \
  X(XP_MUTED, SEC_SOUND_FOR, "Muted contacts", XK_LIST) X(XP_ALWAYS, SEC_SOUND_FOR, "Contacts always heard", XK_LIST) \
  X(XP_MEL1, SEC_SOUND_FOR, "Contacts with melody 1", XK_LIST) X(XP_MEL2, SEC_SOUND_FOR, "Contacts with melody 2", XK_LIST) \
  X(XH_FAV1, SEC_HOME, "Favourite 1", XK_PICK) X(XH_FAV2, SEC_HOME, "Favourite 2", XK_PICK) \
  X(XH_FAV3, SEC_HOME, "Favourite 3", XK_PICK) X(XH_FAV4, SEC_HOME, "Favourite 4", XK_PICK) \
  X(XH_FAV5, SEC_HOME, "Favourite 5", XK_PICK) X(XH_FAV6, SEC_HOME, "Favourite 6", XK_PICK) \
  X(XR_PRE1, SEC_RADIO_ADV, "Preset 1", XK_TEXT) X(XR_PRE2, SEC_RADIO_ADV, "Preset 2", XK_TEXT) \
  X(XR_PRE3, SEC_RADIO_ADV, "Preset 3", XK_TEXT) X(XR_PRE4, SEC_RADIO_ADV, "Preset 4", XK_TEXT) \
  X(XK_PIN, SEC_DISPLAY, "New screen PIN (empty: none)", XK_TEXT) \
  X(XW_NETS, SEC_WIFI, "Saved networks", XK_LIST)     X(XW_ADD, SEC_WIFI, "Add a network (name,password)", XK_TEXT) \
  /* the frontend's (UiCoreHost::FrontKey), in its order */ \
  X(XF_BLUETOOTH, SEC_FEATURES, "Bluetooth", XK_PICK) X(XF_SOUND, SEC_FEATURES, "Sound", XK_PICK) \
  X(XF_TAP_WAKE, SEC_SCREEN, "Tap to wake", XK_PICK)  X(XF_THEME, SEC_SCREEN, "Theme", XK_PICK) \
  X(XF_ACCENT, SEC_SCREEN, "Accent colour", XK_PICK)  X(XF_WIFI, SEC_WIFI, "WiFi", XK_PICK)
#define XKEY_ID(id, sec, label, kind) id,
enum XId : uint8_t { XKEY_LIST(XKEY_ID) XKEY_N, XF_FIRST = XF_BLUETOOTH, XF_LAST = XF_WIFI };
#undef XKEY_ID
static_assert(XH_FAV6 - XH_FAV1 + 1 == NodePrefs::FAVOURITES_COUNT, "a Home favourite key a slot");
static_assert(XR_PRE4 - XR_PRE1 + 1 == NodePrefs::USER_RADIO_PRESET_MAX, "a preset key a slot");
static_assert(XF_LAST - XF_FIRST + 1 == UiCoreHost::FK_COUNT && XF_LAST + 1 == XKEY_N, "the front keys last, in FrontKey's order");
static_assert(XF_SOUND - XF_FIRST == (int)UiCoreHost::FK_SOUND && XF_TAP_WAKE - XF_FIRST == (int)UiCoreHost::FK_TAP_WAKE &&
              XF_THEME - XF_FIRST == (int)UiCoreHost::FK_THEME && XF_ACCENT - XF_FIRST == (int)UiCoreHost::FK_ACCENT &&
              XF_WIFI - XF_FIRST == (int)UiCoreHost::FK_WIFI, "each front key where its FrontKey is");
static bool frontKey(uint8_t id) { return id >= XF_FIRST && id <= XF_LAST; }
static bool homeFav(uint8_t id) { return id >= XH_FAV1 && id <= XH_FAV6; }
static bool radioPreset(uint8_t id) { return id >= XR_PRE1 && id <= XR_PRE4; }
// A number as the console shows radio settings: no trailing zeroes ("869.4625", "62.5", "250").
static void fmtNum(char* b, int n, float v) {
  snprintf(b, n, "%.4f", v);
  char* e = b + strlen(b) - 1;
  while (e > b && *e == '0') *e-- = 0;
  if (*e == '.') *e = 0;
}
static UiCoreHost* s_host = nullptr;   // the frontend, for its keys (set by handle())
struct XKey { uint8_t section; const char* label; uint8_t kind; };
#define XKEY_ROW(id, sec, label, kind) { settings::sec, label, kind },
static const XKey XKEYS[] = { XKEY_LIST(XKEY_ROW) };
#undef XKEY_ROW
#undef XKEY_LIST
static const Key KEYS[] = {
  K("advert.interval", advert_auto_interval_sec), K("advert.loc", advert_loc_policy),
  K("advert.loc.precision", advert_loc_prec),
  K("telemetry.base", telemetry_mode_base), K("telemetry.loc", telemetry_mode_loc), K("telemetry.env", telemetry_mode_env),
  K("autoadd", manual_add_contacts), KB("autoadd.chat", autoadd_config, 0x02), KB("autoadd.repeater", autoadd_config, 0x04),
  KB("autoadd.room", autoadd_config, 0x08), KB("autoadd.sensor", autoadd_config, 0x10),
  KB("autoadd.overwrite", autoadd_config, 0x01), K("autoadd.max.hops", autoadd_max_hops),
  K("contacts.expiry", contact_expiry_idx), K("contacts.fav.first", fav_sort), K("contacts.channel.order", ch_sort), K("map.flow", flow_show), K("map.view", map_view), K("dm.resend", dm_resend_count), K("messages.keep", msg_keep), K("messages.expire", msg_expire),
  K("display.brightness", display_brightness), K("display.off", auto_off_secs), K("display.wake", msg_wake),
  K("display.alert", msg_alert), K("lock", auto_lock), K("lock.clock", lock_compact),
  K("display.text.size", ui_text_scale), K("display.icon.size", ui_icon_scale),
  K("batt.display", batt_display_mode), K("display.signal", signal_show), K("batt.shutdown", low_batt_mv),
#if BATT_LEARN
  K("batt.learn", batt_learn),
#endif
  K("gps.interval", gps_interval),
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
  K("gps", gps_enabled), K("share", loc_share_enabled), K("trail.pace", trail_show_pace),
  KX("bluetooth", XF_BLUETOOTH), KX("sound", XF_SOUND),
  KX("display.tap.wake", XF_TAP_WAKE), KX("display.theme", XF_THEME),
  KX("display.accent", XF_ACCENT),
  KX("wifi", XF_WIFI), KX("wifi.networks", XW_NETS), KX("wifi.add", XW_ADD),
  KX("share.to", XL_TO), KX("share.scope", XL_SCOPE), KX("channels.favourite", XN_FAV),
#ifdef HIST_FLASH   // the flash log is what a channel is kept out of
  KX("channels.unsaved", XN_UNSAVED),
#endif
#ifdef PIN_BUZZER
  KX("channels.muted", XN_MUTED), KX("channels.always", XN_ALWAYS),
  KX("channels.melody1", XN_MEL1), KX("channels.melody2", XN_MEL2),
#endif
  K("home.field1", dashboard_fields[0]), K("home.field2", dashboard_fields[1]), K("home.field3", dashboard_fields[2]),
  KX("home.fav1", XH_FAV1), KX("home.fav2", XH_FAV1 + 1), KX("home.fav3", XH_FAV1 + 2),
  KX("home.fav4", XH_FAV1 + 3), KX("home.fav5", XH_FAV1 + 4), KX("home.fav6", XH_FAV1 + 5),
  KX("radio.preset1", XR_PRE1), KX("radio.preset2", XR_PRE1 + 1), KX("radio.preset3", XR_PRE1 + 2),
  KX("radio.preset4", XR_PRE1 + 3), KX("lock.pin", XK_PIN),
  KX("contacts.favourite", XP_FAV),
#ifdef PIN_BUZZER
  KX("contacts.muted", XP_MUTED), KX("contacts.always", XP_ALWAYS),
  KX("contacts.melody1", XP_MEL1), KX("contacts.melody2", XP_MEL2),
#endif
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
// The lists of channels by slot: the SOS's, and the channels' own options.
static bool chanList(uint8_t id) { return id == XS_CHANNELS || (id >= XN_FAV && id <= XN_MEL2); }
static int xListCount(uint8_t id) {
  if (id == XW_NETS) return s_host && s_host->savedWifiCount() > 0 ? s_host->savedWifiCount() : 0;
  return chanList(id) ? 64 : id == XA_DAYS ? 7 : botcmd::COUNT;
}
static bool xListName(uint8_t id, int i, char* out, size_t n) {
  if (id == XA_DAYS) { snprintf(out, n, "%s", DAYS[i]); return true; }
  if (id == XW_NETS) { const char* w = s_host ? s_host->savedWifiName(i) : nullptr; snprintf(out, n, "%s", w ? w : ""); return w != nullptr; }
  if (!chanList(id)) { snprintf(out, n, "%s", botcmd::CMDS[i].name); return true; }
  ChannelDetails ch;
  if (!the_mesh.getChannel(i, ch) || !ch.name[0]) return false;
  snprintf(out, n, "%s", ch.name);
  return true;
}
static uint64_t xListGet(const NodePrefs* p, uint8_t id) {
  if (id == XS_CHANNELS) return p->sos_ch_mask;
  if (id == XA_DAYS) return p->alarm_repeat_mask;
  if (id == XN_FAV) return p->ch_fav_bitmask;
  if (id == XW_NETS) return xListCount(id) ? ((uint64_t)1 << xListCount(id)) - 1 : 0;   // every one saved
  if (id == XN_UNSAVED) return p->ch_no_save;
  if (id == XN_MUTED) return p->ch_notif_override & p->ch_notif_muted;
  if (id == XN_ALWAYS) return p->ch_notif_override & ~p->ch_notif_muted;
  if (id == XN_MEL1) return p->ch_notif_melody_set & ~p->ch_notif_melody_2;
  if (id == XN_MEL2) return p->ch_notif_melody_set & p->ch_notif_melody_2;
  return (uint16_t)~*xCmdOff((NodePrefs*)p, id) & (botcmd::flag(botcmd::COUNT) - 1);
}
static void xListSet(NodePrefs* p, uint8_t id, uint64_t on) {
  if (id == XS_CHANNELS) p->sos_ch_mask = on;
  else if (id == XA_DAYS) p->alarm_repeat_mask = (uint8_t)(on & 0x7F);
  else if (id == XN_FAV) p->ch_fav_bitmask = on;
  else if (id == XW_NETS) {   // only forgets: wifi.add adds
    for (int i = xListCount(id) - 1; i >= 0; i--) if (!(on >> i & 1)) s_host->forgetSavedWifi(i);
  }
  else if (id == XN_UNSAVED) p->ch_no_save = on;
  else if (id == XN_MUTED || id == XN_ALWAYS) {   // one state per channel: listed in one, out of the other
    const uint8_t st = id == XN_MUTED ? chanctl::NOTIF_MUTED : chanctl::NOTIF_ALWAYS;
    for (uint8_t i = 0; i < 64; i++)
      if (on >> i & 1) chanctl::setNotif(p, i, st);
      else if (chanctl::notif(p, i) == st) chanctl::setNotif(p, i, chanctl::NOTIF_DEFAULT);
  } else if (id == XN_MEL1 || id == XN_MEL2) {   // the same: melody 1 or 2, never both
    const uint8_t slot = id == XN_MEL1 ? 1 : 2;
    for (uint8_t i = 0; i < 64; i++)
      if (on >> i & 1) chanctl::setMelody(p, i, slot);
      else if (chanctl::melody(p, i) == slot) chanctl::setMelody(p, i, 0);
  }
  else *xCmdOff(p, id) = (uint16_t)(~on & (botcmd::flag(botcmd::COUNT) - 1));
}
static uint8_t* xDm(NodePrefs* p, uint8_t id) {
  return id >= XS_DM1 && id <= XS_DM3 ? p->sos_dm[id - XS_DM1] : nullptr;
}

// The i-th channel with a name, and the i-th contact of a type, in list order.
static bool nthChannel(int i, char* out, size_t n) {
  for (int slot = 0, seen = 0; slot < 64; slot++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(slot, ch)) break;
    if (!ch.name[0]) continue;
    if (seen++ == i) { snprintf(out, n, "%s", ch.name); return true; }
  }
  return false;
}
static bool nthContact(uint8_t type, int i, char* out, size_t n) {
  ContactInfo c;
  int seen = 0;
  for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c); idx++) {
    if (c.type != type) continue;
    if (seen++ == i) { snprintf(out, n, "%s", c.name); return true; }
  }
  return false;
}
static int channelCount() {
  char nm[2];
  int k = 0;
  while (nthChannel(k, nm, sizeof(nm))) k++;
  return k;
}
// A channel's slot / a contact's key prefix, by name (case ignored).
static int channelSlot(const char* name) {
  for (int slot = 0; slot < 64; slot++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(slot, ch)) break;
    if (ch.name[0] && !strcasecmp(ch.name, name)) return slot;
  }
  return -1;
}
static bool contactKey(uint8_t type, const char* name, uint8_t* key) {
  ContactInfo c;
  for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c); idx++)
    if (c.type == type && !strcasecmp(c.name, name)) { memcpy(key, c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN); return true; }
  return false;
}
static bool contactName(const uint8_t* key, char* out, size_t n) {
  static const uint8_t ZERO[NodePrefs::FAVOURITE_PREFIX_LEN] = {0};
  if (!memcmp(key, ZERO, sizeof(ZERO))) return false;
  ContactInfo* c = the_mesh.lookupContactByPubKey(key, NodePrefs::FAVOURITE_PREFIX_LEN);
  if (!c || !c->name[0]) return false;
  snprintf(out, n, "%s", c->name);
  return true;
}
// Live share's scope: 0 follows the target, n the scope list's entry n-1 ("*" first).
static const char* const SCOPE_TARGET = "Target's";

// A pick's choices by index (channels by slot, rooms in the contact list's
// order; live share's target the channels, then the chat contacts).
// A front key's choices: the same on every frontend but the accent colours.
static const char* frontChoice(uint8_t k, int i) {
  static const char* const ON_OFF[] = { "On", "Off" };
  static const char* const THEMES[] = { "Dark", "Light" };
  if (i < 0) return nullptr;
  switch (k) {
    case UiCoreHost::FK_BLUETOOTH: case UiCoreHost::FK_TAP_WAKE: case UiCoreHost::FK_WIFI: return i < 2 ? ON_OFF[i] : nullptr;
    case UiCoreHost::FK_SOUND: return i < soundctl::MODE_COUNT ? soundctl::modeLabel((uint8_t)i) : nullptr;
    case UiCoreHost::FK_THEME: return i < 2 ? THEMES[i] : nullptr;
  }
  return s_host ? s_host->frontChoice(k, i) : nullptr;
}
static bool xPickName(uint8_t id, int i, char* out, size_t n) {
  if (frontKey(id)) {
    const char* c = frontChoice(id - XF_FIRST, i);
    if (!c) return false;
    snprintf(out, n, "%s", c);
    return true;
  }
  if (id == XB_ROOM || (id >= XS_DM1 && id <= XS_DM3)) return nthContact(id == XB_ROOM ? ADV_TYPE_ROOM : ADV_TYPE_CHAT, i, out, n);
  if (id == XL_TO) {
    const int nch = channelCount();
    return i < nch ? nthChannel(i, out, n) : nthContact(ADV_TYPE_CHAT, i - nch, out, n);
  }
  if (homeFav(id)) {   // the channels, then chat contacts and rooms
    const int nch = channelCount();
    if (i < nch) return nthChannel(i, out, n);
    ContactInfo c;
    int seen = nch;
    for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c); idx++) {
      if ((c.type != ADV_TYPE_CHAT && c.type != ADV_TYPE_ROOM) || !c.name[0]) continue;
      if (seen++ == i) { snprintf(out, n, "%s", c.name); return true; }
    }
    return false;
  }
  if (id == XL_SCOPE) {
    const ScopeList& sl = the_mesh.scopeList();
    if (i > sl.count + 1) return false;
    snprintf(out, n, "%s", i == 0 ? SCOPE_TARGET : sl.name((uint8_t)(i - 1)));
    return true;
  }
  return nthChannel(i, out, n);
}
static bool xPickGet(const NodePrefs* p, uint8_t id, char* out, size_t n) {
  if (frontKey(id)) return s_host && xPickName(id, s_host->frontValue(id - XF_FIRST), out, n);
  if (id == XB_ROOM || (id >= XS_DM1 && id <= XS_DM3))
    return contactName(id == XB_ROOM ? p->bot_room_prefix : p->sos_dm[id - XS_DM1], out, n);
  if (id == XL_TO && p->loc_share_target_type == 1) return contactName(p->loc_share_dm_prefix, out, n);
  if (homeFav(id)) {
    const int slot = id - XH_FAV1;
    if (favslots::isEmpty(p, slot)) return false;
    if (favslots::kind(p, slot) != NodePrefs::FAV_KIND_CHANNEL) return contactName(p->favourite_contacts[slot], out, n);
    ChannelDetails ch;
    if (!the_mesh.getChannel(p->favourite_contacts[slot][0], ch) || !ch.name[0]) return false;
    snprintf(out, n, "%s", ch.name);
    return true;
  }
  if (id == XL_SCOPE) {
    const ScopeList& sl = the_mesh.scopeList();
    snprintf(out, n, "%s", p->loc_share_scope == 0 ? SCOPE_TARGET : sl.name((uint8_t)(p->loc_share_scope - 1)));
    return true;
  }
  ChannelDetails ch;
  const uint8_t slot = id == XL_TO ? p->loc_share_channel_idx : id == XB_CH1_CHANNEL ? p->bot_channel_idx : p->bot_ch2_idx;
  if (!the_mesh.getChannel(slot, ch) || !ch.name[0]) return false;
  snprintf(out, n, "%s", ch.name);
  return true;
}
// Picks by name (case ignored); false when there is none of that name.
static bool xPickSet(NodePrefs* p, uint8_t id, const char* name) {
  if (frontKey(id)) {
    char nm[24];
    for (int i = 0; xPickName(id, i, nm, sizeof(nm)); i++)
      if (!strcasecmp(nm, name)) { s_host->setFrontValue(id - XF_FIRST, i); return true; }
    return false;
  }
  if (id == XB_ROOM || xDm(p, id)) {
    uint8_t* key = id == XB_ROOM ? p->bot_room_prefix : xDm(p, id);
    if (!strcasecmp(name, "none")) { memset(key, 0, NodePrefs::FAVOURITE_PREFIX_LEN); return true; }
    return contactKey(id == XB_ROOM ? ADV_TYPE_ROOM : ADV_TYPE_CHAT, name, key);
  }
  if (id == XL_SCOPE) {
    if (!strcasecmp(name, SCOPE_TARGET)) { p->loc_share_scope = 0; return true; }
    const ScopeList& sl = the_mesh.scopeList();
    for (uint8_t i = 0; i <= sl.count; i++)
      if (!strcasecmp(sl.name(i), name)) { p->loc_share_scope = (uint8_t)(i + 1); return true; }
    return false;
  }
  const int slot = channelSlot(name);
  if (homeFav(id)) {   // a channel of that name first, else a chat contact or room; moved here if pinned elsewhere
    const int at = id - XH_FAV1;
    if (!strcasecmp(name, "none")) { favslots::clear(p, at); return true; }
    if (slot >= 0) { favslots::pinChannel(p, at, (uint8_t)slot); return true; }
    uint8_t key[NodePrefs::FAVOURITE_PREFIX_LEN];
    if (!contactKey(ADV_TYPE_CHAT, name, key) && !contactKey(ADV_TYPE_ROOM, name, key)) return false;
    favslots::pinContact(p, at, key);
    return true;
  }
  if (id == XL_TO) {   // a channel of that name first, else a contact
    if (slot >= 0) { p->loc_share_target_type = 0; p->loc_share_channel_idx = (uint8_t)slot; return true; }
    if (!contactKey(ADV_TYPE_CHAT, name, p->loc_share_dm_prefix)) return false;
    p->loc_share_target_type = 1;
    return true;
  }
  if (slot < 0) return false;
  (id == XB_CH1_CHANNEL ? p->bot_channel_idx : p->bot_ch2_idx) = (uint8_t)slot;
  return true;
}

// The next name of a list "a, b,c" at `at` (moved past it): where it starts
// and its length, spaces round it left out; 0 when only commas were left.
static int nextName(const char*& at, const char*& name) {
  while (*at == ' ' || *at == ',') at++;
  name = at;
  while (*at && *at != ',') at++;
  const char* t = at;
  while (t > name && t[-1] == ' ') t--;
  return (int)(t - name);
}
static bool sameName(const char* s, const char* name, int len) {
  return (int)strlen(s) == len && !strncasecmp(s, name, len);
}

// Lists of contacts (favourites, and the per-contact sound overrides): too
// many for a mask, so by name. A favourite is any contact; the rest chat ones.
static bool contactList(uint8_t id) { return id >= XP_FAV && id <= XP_MEL2; }
static uint8_t contactListType(uint8_t id) { return id == XP_FAV ? 0 : ADV_TYPE_CHAT; }   // 0: any
static bool inContactList(const NodePrefs* p, uint8_t id, const ContactInfo& c) {
  switch (id) {
    case XP_FAV:    return contactctl::favourite(c);
    case XP_MUTED:  return contactctl::notif(p, c.id.pub_key) == contactctl::NOTIF_MUTED;
    case XP_ALWAYS: return contactctl::notif(p, c.id.pub_key) == contactctl::NOTIF_ALWAYS;
    case XP_MEL1:   return contactctl::melody(p, c.id.pub_key) == 1;
    case XP_MEL2:   return contactctl::melody(p, c.id.pub_key) == 2;
  }
  return false;
}
static void setInContactList(NodePrefs* p, uint8_t id, const ContactInfo& c, bool on) {
  switch (id) {
    case XP_FAV: contactctl::setFavourite(c.id.pub_key, on); break;
    case XP_MUTED: case XP_ALWAYS: {
      const uint8_t st = id == XP_MUTED ? contactctl::NOTIF_MUTED : contactctl::NOTIF_ALWAYS;
      if (on) contactctl::setNotif(p, c.id.pub_key, st);
      else if (contactctl::notif(p, c.id.pub_key) == st) contactctl::setNotif(p, c.id.pub_key, contactctl::NOTIF_DEFAULT);
      break;
    }
    case XP_MEL1: case XP_MEL2: {
      const uint8_t slot = id == XP_MEL1 ? 1 : 2;
      if (on) contactctl::setMelody(p, c.id.pub_key, slot);
      else if (contactctl::melody(p, c.id.pub_key) == slot) contactctl::setMelody(p, c.id.pub_key, 0);
      break;
    }
  }
}
static bool contactOfList(uint8_t id, const ContactInfo& c) {
  return c.name[0] && (!contactListType(id) || c.type == contactListType(id));
}
static void contactListGet(const NodePrefs* p, uint8_t id, char* reply, int n) {
  int o = snprintf(reply, n, "> ");
  bool any = false;
  ContactInfo c;
  for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c) && o < n - 1; idx++)
    if (contactOfList(id, c) && inContactList(p, id, c)) { o += snprintf(reply + o, n - o, "%s%s", any ? "," : "", c.name); any = true; }
  if (!any) snprintf(reply + o, n - o, "none");
}
// "a,b": these on, the others off. Every name is checked before anything changes.
static bool contactListSet(NodePrefs* p, uint8_t id, const char* v, char* reply, int n) {
  const bool none = !*v || !strcasecmp(v, "none");
  for (const char *a = none ? "" : v, *nm; *a;) {
    const int len = nextName(a, nm);
    if (!len) continue;
    bool hit = false;
    ContactInfo c;
    for (int idx = MAX_ANON_CONTACTS; !hit && the_mesh.getContactByIdx(idx, c); idx++)
      hit = contactOfList(id, c) && sameName(c.name, nm, len);
    if (!hit) { snprintf(reply, n, "Error, no contact %.*s", len, nm); return false; }
  }
  ContactInfo c;
  for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c); idx++) {
    if (!contactOfList(id, c)) continue;
    bool want = false;
    for (const char *a = none ? "" : v, *nm; *a && !want;) {
      const int len = nextName(a, nm);
      want = len && sameName(c.name, nm, len);
    }
    if (want != inContactList(p, id, c)) setInContactList(p, id, c, want);
  }
  return true;
}

static void xGet(NodePrefs* p, uint8_t id, char* reply, int n) {
  const XKey& x = XKEYS[id];
  if (contactList(id)) { contactListGet(p, id, reply, n); return; }
  if (id == XK_PIN || id == XW_ADD) { snprintf(reply, n, "> "); return; }   // a hash only / nothing kept
  if (radioPreset(id)) {   // "name,freq,bw,sf,cr", empty when the slot is free
    const NodePrefs::UserRadioPreset& u = p->user_radio_presets[id - XR_PRE1];
    if (!u.name[0]) { snprintf(reply, n, "> "); return; }
    char f[16], b[16];
    fmtNum(f, sizeof(f), u.freq); fmtNum(b, sizeof(b), u.bw);
    snprintf(reply, n, "> %s,%s,%s,%u,%u", u.name, f, b, (unsigned)u.sf, (unsigned)u.cr);
    return;
  }
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
  if (contactList(id)) {
    if (!contactListSet(p, id, v, reply, n)) return;
  } else if (id == XW_ADD) {   // "name,password": up to the first comma the network's name
    const char* c = strchr(v, ',');
    const int nl = c ? (int)(c - v) : (int)strlen(v);
    if (!nl || nl > 32 || (c && strlen(c + 1) > 64)) { snprintf(reply, n, "Error, name,password (a name up to 32, a password up to 64)"); return; }
    char ssid[33];
    memcpy(ssid, v, nl); ssid[nl] = '\0';
    s_host->addSavedWifi(ssid, c ? c + 1 : "");
  } else if (id == XK_PIN) {   // 4-8 digits (the L2 takes up to 8), empty: none
    const size_t len = strlen(v);
    bool digits = len >= 4 && len <= 8;
    for (size_t i = 0; digits && i < len; i++) digits = v[i] >= '0' && v[i] <= '9';
    if (len && !digits) { snprintf(reply, n, "Error, 4 to 8 digits, or nothing for none"); return; }
    screenlock::set(*p, v, the_mesh.getRNG());
  } else if (radioPreset(id)) {
    NodePrefs::UserRadioPreset& u = p->user_radio_presets[id - XR_PRE1];
    if (!*v || !strcasecmp(v, "none")) u.name[0] = '\0';
    else {
      const char* c = strchr(v, ',');
      char* e = nullptr;
      const float f = c ? strtof(c + 1, &e) : 0;
      const float b = e && *e == ',' ? strtof(e + 1, &e) : 0;
      const long sf = e && *e == ',' ? strtol(e + 1, &e, 10) : 0;
      const long cr = e && *e == ',' ? strtol(e + 1, &e, 10) : 0;
      const int nl = c ? (int)(c - v) : 0;
      if (!c || !nl || nl >= (int)sizeof(u.name) || *e || f < 100 || f > 1100 || b <= 0 || b > 1625 || sf < 5 || sf > 12 || cr < 5 || cr > 8) {
        snprintf(reply, n, "Error, name,freq,bw,sf,cr (869.525,250,11,5), a name up to %d", (int)sizeof(u.name) - 1);
        return;
      }
      memcpy(u.name, v, nl); u.name[nl] = '\0';
      u.freq = f; u.bw = b; u.sf = (uint8_t)sf; u.cr = (uint8_t)cr;
    }
  } else if (id == XA_TIME) {   // "7:30" / "07:30"
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
      for (const char* name; *v;) {
        const int len = nextName(v, name);
        if (!len) continue;
        int hit = -1;
        for (int i = 0; i < xListCount(id); i++)
          if (xListName(id, i, nm, sizeof(nm)) && sameName(nm, name, len)) { hit = i; break; }
        if (hit < 0) {
          int o = snprintf(reply, n, "Error, of: ");
          bool first = true;
          for (int i = 0; i < xListCount(id) && o < n - 1; i++)
            if (xListName(id, i, nm, sizeof(nm))) { o += snprintf(reply + o, n - o, "%s%s", first ? "" : ", ", nm); first = false; }
          return;
        }
        on |= (uint64_t)1 << hit;
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
    int cap = id == XA_TIME ? 6 : id == XC_HOME ? 24 : id == XK_PIN ? 9 : radioPreset(id) ? 48 : id == XW_ADD ? 98 : 0;
    if (!cap) xText(p, id, cap);
    snprintf(reply + o, n - o, "#text:%d", cap - 1);
    return;
  }
  o += snprintf(reply + o, n - o, x.kind == XK_LIST ? "#list" : "#pick");
  if (id == XB_ROOM || (id >= XS_DM1 && id <= XS_DM3) || homeFav(id)) o += snprintf(reply + o, n - o, "|none");   // a contact pick may be cleared
  char nm[40];
  if (contactList(id)) {
    ContactInfo c;
    for (int idx = MAX_ANON_CONTACTS; the_mesh.getContactByIdx(idx, c) && o < n - 1; idx++) {
      if (!contactOfList(id, c)) continue;
      if (o + 1 + (int)strlen(c.name) >= n - 5) { snprintf(reply + o, n - o, "|..."); break; }
      o += snprintf(reply + o, n - o, "|%s", c.name);
    }
    return;
  }
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
  if (!strcmp(k.key, "gps")) return core.gpsAvailable();
  if (k.offset == XKEY && (k.bit == XW_NETS || k.bit == XW_ADD)) return core.host() && core.host()->savedWifiCount() >= 0;
  if (k.offset == XKEY && frontKey(k.bit)) return core.host() && core.host()->frontValue(k.bit - XF_FIRST) >= 0;
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

// The conversations, for the configurator's Messages card -- read from the
// history the device keeps (never the app's offline queue: what the phone
// hasn't fetched stays for it). A conversation is "ch <idx>" or "dm <first 4
// bytes of the key, 8 hex>"; messages are numbered newest first from 0.
//   "hist list <k>"  the k-th conversation with history, newest first:
//                    "> ch|<idx>|<name>|<count>|<unread>|<last ts>" or
//                    "> dm|<hex8>|<name>|<count>|<unread>|<last ts>"; "> end"
//   "hist <conv> <j> [<off>]"  message j: "> <count>|<ts>|<i|o><ack 0-3>|<bytes>|<text>",
//                    the text from byte off on, cut where the frame ends (on a
//                    character); the rest by asking again from where it stopped
//   "send <conv> <text>"  sent as from the device's keyboard (history, ACK): "OK"
//   "hist read <conv>"  the conversation's unread count to 0, as opening it on the device: "OK"
static bool parseHex8(const char* s, uint8_t out[4]) {
  for (int i = 0; i < 8; i++) {
    const char c = s[i];
    const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    if (v < 0) return false;
    if (i & 1) out[i / 2] |= v; else out[i / 2] = v << 4;
  }
  return s[8] == 0 || s[8] == ' ';
}
// "ch 3 ..." / "dm a1b2c3d4 ...": the conversation, and where the rest begins.
static const char* parseConv(const char* s, bool& dm, int& ch, uint8_t key[4]) {
  if (!strncmp(s, "ch ", 3)) {
    char* e;
    ch = (int)strtol(s + 3, &e, 10);
    if (e == s + 3 || ch < 0 || ch >= MAX_GROUP_CHANNELS || (*e && *e != ' ')) return nullptr;
    dm = false;
    return *e ? e + 1 : e;
  }
  if (!strncmp(s, "dm ", 3) && parseHex8(s + 3, key)) { dm = true; return s[11] ? s + 12 : s + 11; }
  return nullptr;
}
static void textFrom(const char* t, int off, char* out, int room) {   // cut on a character
  const int len = (int)strlen(t);
  if (off > len) off = len;
  int k = len - off;
  if (k > room - 1) {
    k = room - 1;
    while (k > 0 && ((uint8_t)t[off + k] & 0xC0) == 0x80) k--;
  }
  memcpy(out, t + off, k);
  out[k] = 0;
}
static bool chatHistory(UiCore& core, const char* cmd, char* reply, int n) {
  MessageHistory& h = core.history;
  const bool mine = !strncmp(cmd, "hist ", 5) || !strncmp(cmd, "send ", 5);
  if (mine && the_mesh.cliIsRemote()) { snprintf(reply, n, "Error, only over USB or Bluetooth"); return true; }
  if (!strncmp(cmd, "hist list ", 10)) {
    const int want = atoi(cmd + 10);
    // Conversations by their newest message: walk the rings newest first, a
    // conversation counted where it first appears.
    int seen = 0;
    bool ch_done[MAX_GROUP_CHANNELS] = {};
    int ci = 0, di = 0;
    const int cn = h.chHistCount(), dn = h.dmHistCount();
    while (ci < cn || di < dn) {
      const ChHistEntry* c = ci < cn ? &h.chAtPos(h.chHistPosNewest(ci)) : nullptr;
      const DmHistEntry* d = di < dn ? &h.dmAtPos(h.dmHistPosNewest(di)) : nullptr;
      const bool take_ch = c && (!d || c->timestamp >= d->timestamp);
      if (take_ch) {
        ci++;
        if (c->ch_idx >= MAX_GROUP_CHANNELS || ch_done[c->ch_idx]) continue;
        ch_done[c->ch_idx] = true;
        if (seen++ != want) continue;
        ChannelDetails cd;
        const char* name = the_mesh.getChannel(c->ch_idx, cd) ? cd.name : "?";
        snprintf(reply, n, "> ch|%d|%s|%d|%d|%lu", c->ch_idx, name, h.histCountForChannel(c->ch_idx),
                 h.chUnread(c->ch_idx), (unsigned long)c->timestamp);
        return true;
      }
      di++;
      bool dup = false;   // this contact seen at a newer message already?
      for (int k = 0; k < di - 1 && !dup; k++) dup = !memcmp(h.dmAtPos(h.dmHistPosNewest(k)).prefix, d->prefix, 4);
      if (dup || seen++ != want) continue;
      ContactInfo ci_;
      const bool known = MessageHistory::contactByPrefix(d->prefix, ci_);
      snprintf(reply, n, "> dm|%02x%02x%02x%02x|%s|%d|%d|%lu", d->prefix[0], d->prefix[1], d->prefix[2], d->prefix[3],
               known ? ci_.name : "?", h.dmHistCountForContact(d->prefix), known ? core.dmUnread(ci_.id.pub_key) : 0,
               (unsigned long)d->timestamp);
      return true;
    }
    snprintf(reply, n, "> end");
    return true;
  }
  const bool send = !strncmp(cmd, "send ", 5);
  if (!send && strncmp(cmd, "hist ", 5)) return false;
  bool dm = false;
  int ch = 0;
  uint8_t key[4] = {};
  if (!strncmp(cmd, "hist read ", 10)) {
    if (!parseConv(cmd + 10, dm, ch, key)) { snprintf(reply, n, "Error, hist read ch <idx> | dm <key hex8>"); return true; }
    if (dm) {
      ContactInfo c;
      if (!MessageHistory::contactByPrefix(key, c)) { snprintf(reply, n, "Error, no such contact"); return true; }
      core.clearDMUnread(c.id.pub_key);
    } else {
      h.setChUnread(ch, 0);
    }
    snprintf(reply, n, "OK");
    return true;
  }
  const char* rest = parseConv(cmd + 5, dm, ch, key);
  if (!rest) { snprintf(reply, n, "Error, %s ch <idx> | dm <key hex8> %s", send ? "send" : "hist", send ? "<text>" : "<n> [<from byte>]"); return true; }
  if (send) {
    if (!*rest) { snprintf(reply, n, "Error, no text"); return true; }
    if (dm) {
      ContactInfo c;
      if (!MessageHistory::contactByPrefix(key, c)) { snprintf(reply, n, "Error, no such contact"); return true; }
      if (!core.sendDirectText(c, rest)) { snprintf(reply, n, "Error, not sent"); return true; }
    } else if (!core.sendChannelText((uint8_t)ch, rest)) {
      snprintf(reply, n, "Error, not sent");
      return true;
    }
    snprintf(reply, n, "OK");
    return true;
  }
  char* e;
  const int j = (int)strtol(rest, &e, 10);
  const int off = *e == ' ' ? atoi(e + 1) : 0;
  if (e == rest || j < 0) { snprintf(reply, n, "Error, hist <conv> <n> [<from byte>]"); return true; }
  const int count = dm ? h.dmHistCountForContact(key) : h.histCountForChannel(ch);
  const int pos = j < count ? (dm ? h.dmHistEntryForContact(key, j) : h.histEntryForChannel(ch, j)) : -1;
  if (pos < 0) { snprintf(reply, n, "> %d|none", count); return true; }
  const char* text;
  uint32_t ts;
  bool out;
  int ack;
  if (dm) {
    const DmHistEntry& m = h.dmAtPos(pos);
    text = m.text; ts = m.timestamp; out = m.outgoing; ack = out ? (int)h.dmEffectiveStatus(m) : 0;
  } else {
    const ChHistEntry& m = h.chAtPos(pos);
    text = m.text; ts = m.timestamp; out = !strncmp(m.text, "Me: ", 4); ack = out ? (int)m.relay_status : 0;
  }
  const int at = snprintf(reply, n, "> %d|%lu|%c%d|%d|", count, (unsigned long)ts, out ? 'o' : 'i', ack, (int)strlen(text));
  if (at < n) textFrom(text, off, reply + at, n - at);
  return true;
}

// The battery curve (Settings > Battery curve): "get batt.curve" is "> <mV at
// 0 %>,<10 %>,...,<100 %>" (11 values) or "> builtin"; "set batt.curve" takes
// the same list -- rising, 2500..4500 mV -- or "builtin". The whole curve in one
// line: one measured on one device pasted into another. A curve set so counts
// as set by hand (learning leaves it), unless it's the one already in use (a
// backup put back). "get batt.curve.src": "> builtin", "> manual" or
// "> learned <runs>".
static bool battCurve(NodePrefs* p, const char* cmd, char* reply, int n) {
  if (!strcmp(cmd, "get batt.curve.src")) {
    switch (battlearn::source(p->batt_curve_mv, p->batt_curve_src)) {
      case battlearn::LEARNED: snprintf(reply, n, "> learned %u", p->batt_learn_runs); break;
      case battlearn::MANUAL:  snprintf(reply, n, "> manual"); break;
      default:                 snprintf(reply, n, "> builtin"); break;
    }
    return true;
  }
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
    battlearn::markBuiltin(*p);
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
    if (memcmp(p->batt_curve_mv, c, sizeof(c))) battlearn::markManual(*p);
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
    for (const char* name; *a;) {
      const int len = nextName(a, name);
      if (!len) continue;
      int hit = -1;
      for (uint8_t i = 0; i < sl.count; i++)
        if (sameName(sl.name(i + 1), name, len)) { hit = i; break; }
      if (hit < 0) {
        int o = snprintf(reply, n, "Error, scopes: ");
        if (!sl.count) snprintf(reply + o, n - o, "none set up (Settings > Radio)");
        for (uint8_t i = 0; i < sl.count && o < n - 1; i++) o += snprintf(reply + o, n - o, "%s%s", i ? ", " : "", sl.name(i + 1));
        return true;
      }
      mask |= (uint16_t)(1u << hit);
    }
  }
  p->repeat_extra_scope_mask = mask;
  the_mesh.rebuildRepeatScopes();
  the_mesh.savePrefs();
  snprintf(reply, n, "OK");
  return true;
}

// A key "keys" lists: one this board has.
static bool listed(UiCore& core, const Key& k) {
  return (k.offset == XKEY || row(k) >= 0) && hereKey(core, k);
}

// "opts" of key k (listed): "> page/SECTION|label|value|choice|choice...",
// "on|off" for a switch.
static void opts(NodePrefs* p, const Key& k, char* reply, int n) {
  if (k.offset == XKEY) { xOpts(p, k.bit, reply, n); return; }
  const settings::Setting& st = settings::ALL[row(k)];
  char buf[24];
  int o = snprintf(reply, n, "> %s/%s|%s|%s", settings::pageTitle(settings::sectionPage(st.section)),
                   settings::sectionTitle(st.section), st.label, settings::text(*p, st, buf, sizeof(buf), "on", "off"));
  if (!st.option) { snprintf(reply + o, n - o, "|on|off"); return; }
  for (int v = 0; v < st.count && o < n - 1; v++) {
    st.option((uint8_t)v, buf, sizeof(buf), *p);
    if (o + 1 + (int)strlen(buf) >= n - 5) { snprintf(reply + o, n - o, "|..."); break; }   // room for "|..." too
    o += snprintf(reply + o, n - o, "|%s", buf);
  }
}

// True when the command was a settings one (reply set either way).
static bool handle(UiCore& core, const char* cmd, char* reply, int n) {
  NodePrefs* p = core.prefs();
  if (!p) return false;
  s_host = core.host();
  if (quickMessages(p, cmd, reply, n)) return true;
  if (melodies(p, cmd, reply, n)) return true;
  if (waypoints(core, cmd, reply, n)) return true;
  if (chatHistory(core, cmd, reply, n)) return true;
  if (battCurve(p, cmd, reply, n)) return true;
  if (repeaterScopes(p, cmd, reply, n)) return true;
  if (!strcmp(cmd, "keys") || !strncmp(cmd, "keys ", 5)) {   // the key names, a frame's worth a page
    int page = cmd[4] ? atoi(cmd + 5) : 1, pg = 1, o = 0;
    bool more = false;
    reply[0] = 0;
    for (int k = 0; k < KEY_COUNT; k++) {
      if (!listed(core, KEYS[k])) continue;
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
      if (!listed(core, KEYS[k])) { snprintf(reply, n, "Error, not on this device"); return true; }
      opts(p, KEYS[k], reply, n);
      return true;
    }
    snprintf(reply, n, "Error, no key %s (see keys)", cmd + 5);
    return true;
  }
  // "optsat <i>": key i of the table as "@key " and its opts, "-" when this
  // board hasn't it; past the last, "No more keys". What "optsall"
  // (MyMeshCli.h) streams, i from 0 up.
  if (!strncmp(cmd, "optsat ", 7)) {
    const int k = atoi(cmd + 7);
    if (k < 0 || k >= KEY_COUNT) snprintf(reply, n, "No more keys");
    else if (!listed(core, KEYS[k])) snprintf(reply, n, "-");
    else {
      int o = snprintf(reply, n, "@%s ", KEYS[k].key);
      opts(p, KEYS[k], reply + o, n - o);
    }
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
