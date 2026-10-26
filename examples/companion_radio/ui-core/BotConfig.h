#pragma once
// The auto-reply bot's settings as a table the frontends render (ui-new's
// Tools > Bot, ui-lvgl's Bot screen). The bot itself runs in MyMesh; these
// are only its NodePrefs fields, grouped by where it answers: a channel, a
// room server, direct messages, plus quiet hours.
//
// Row kinds: switches (a uint8_t flag), text (trigger / reply, with a
// capacity), a target (channel / room: a picker in the frontend), an hour
// (quiet from / to; equal hours = quiet hours off).

namespace botcfg {

// Log: the bot's recent replies (the_mesh.botLog()), where there's the
// memory for them (BOT_LOG, BotLog.h). A tab of its own, no setting rows.
#ifdef BOT_LOG
enum Tab : uint8_t { TAB_CHANNEL, TAB_ROOM, TAB_DIRECT, TAB_OTHER, TAB_LOG, TAB_COUNT };
static const char* const TAB_LABELS[TAB_COUNT] = { "Channel", "Room", "Direct", "Other", "Log" };
static bool isLog(int tab) { return tab == TAB_LOG; }
#else
enum Tab : uint8_t { TAB_CHANNEL, TAB_ROOM, TAB_DIRECT, TAB_OTHER, TAB_COUNT };
static const char* const TAB_LABELS[TAB_COUNT] = { "Channel", "Room", "Direct", "Other" };
static bool isLog(int) { return false; }
#endif

enum Kind : uint8_t { ENABLE_DM, DM_SCOPE, COMMANDS_DM, ACTIONS_DM, TRIGGER_DM, REPLY_DM,
                      ENABLE_CH, CHANNEL, COMMANDS_CH, ACTIONS_CH, TRIGGER_CH, REPLY_CH,
                      ENABLE_ROOM, ROOM, COMMANDS_ROOM, ACTIONS_ROOM, TRIGGER_ROOM, REPLY_ROOM,
                      QUIET_FROM, QUIET_TO, ACTIONS_SCOPE };

// hint: a one-line explanation for frontends with room for it.
struct Row { Kind kind; const char* label; const char* hint; };

static const Row CHANNEL_ROWS[] = {
  { ENABLE_CH,   "Enable",   "Answer on one channel" },
  { CHANNEL,     "Channel",  nullptr },
  { COMMANDS_CH, "Commands", "!ping, !help, ..." },
  { ACTIONS_CH,  "Actions",  "!gps, !gpio (this device)" },
  { TRIGGER_CH,  "Trigger",  "Reply trigger; * = any" },
  { REPLY_CH,    "Reply",    "{name} {loc} {time} ..." },
};
static const Row ROOM_ROWS[] = {
  { ENABLE_ROOM,   "Enable",   "Answer in one room server" },
  { ROOM,          "Room",     nullptr },
  { COMMANDS_ROOM, "Commands", "!ping, !help, ..." },
  { ACTIONS_ROOM,  "Actions",  "!gps, !gpio (this device)" },
  { TRIGGER_ROOM,  "Trigger",  "Reply trigger; * = any" },
  { REPLY_ROOM,    "Reply",    "{name} {loc} {time} ..." },
};
static const Row DIRECT_ROWS[] = {
  { ENABLE_DM,   "Enable",   "Answer direct messages" },
  { DM_SCOPE,    "DM allow", "Everyone, or favourites only" },
  { COMMANDS_DM, "Commands", "!ping, !help, ..." },
  { ACTIONS_DM,  "Actions",  "!gps, !gpio (this device)" },
  { TRIGGER_DM,  "Trigger",  "Reply trigger; * = any" },
  { REPLY_DM,    "Reply",    "{name} {loc} {time} ..." },
};
static const Row OTHER_ROWS[] = {
  { QUIET_FROM, "Quiet from", "No replies in these hours" },
  { QUIET_TO,   "Quiet to",   "Same hour = always on" },
  { ACTIONS_SCOPE, "Actions by", "Fav: favourites only (a channel name can be faked)" },
};

static int rowCount(int tab) {
  switch (tab) {
    case TAB_CHANNEL: return sizeof(CHANNEL_ROWS) / sizeof(Row);
    case TAB_ROOM:    return sizeof(ROOM_ROWS) / sizeof(Row);
    case TAB_DIRECT:  return sizeof(DIRECT_ROWS) / sizeof(Row);
    case TAB_OTHER:   return sizeof(OTHER_ROWS) / sizeof(Row);
    default:          return 0;   // Log
  }
}

static const Row& row(int tab, int idx) {
  switch (tab) {
    case TAB_CHANNEL: return CHANNEL_ROWS[idx];
    case TAB_ROOM:    return ROOM_ROWS[idx];
    case TAB_DIRECT:  return DIRECT_ROWS[idx];
    default:          return OTHER_ROWS[idx];
  }
}

// On / off rows: the flag; nullptr for other kinds.
static uint8_t* flag(NodePrefs* p, Kind k) {
  switch (k) {
    case ENABLE_DM:     return &p->bot_enabled;
    case ENABLE_CH:     return &p->bot_channel_enabled;
    case ENABLE_ROOM:   return &p->bot_room_enabled;
    case DM_SCOPE:      return &p->bot_dm_scope;          // 1 = favourites only
    case COMMANDS_DM:   return &p->bot_commands_enabled;
    case COMMANDS_CH:   return &p->bot_commands_ch;
    case COMMANDS_ROOM: return &p->bot_commands_room;
    case ACTIONS_DM:    return &p->bot_actions_dm;
    case ACTIONS_CH:    return &p->bot_actions_ch;
    case ACTIONS_ROOM:  return &p->bot_actions_room;
    case ACTIONS_SCOPE: return &p->bot_actions_scope;     // 1 = favourites only
    default:            return nullptr;
  }
}

// Text rows: the buffer and its size (incl. NUL); nullptr for other kinds.
static char* text(NodePrefs* p, Kind k, int& cap) {
  switch (k) {
    case TRIGGER_DM:   cap = sizeof(p->bot_trigger);      return p->bot_trigger;
    case REPLY_DM:     cap = sizeof(p->bot_reply_dm);     return p->bot_reply_dm;
    case TRIGGER_CH:   cap = sizeof(p->bot_trigger_ch);   return p->bot_trigger_ch;
    case REPLY_CH:     cap = sizeof(p->bot_reply_ch);     return p->bot_reply_ch;
    case TRIGGER_ROOM: cap = sizeof(p->bot_trigger_room); return p->bot_trigger_room;
    case REPLY_ROOM:   cap = sizeof(p->bot_reply_room);   return p->bot_reply_room;
    default:           cap = 0; return nullptr;
  }
}

// All / Fav rows (a flag shown as who, not on / off).
static bool isScope(Kind k)   { return k == DM_SCOPE || k == ACTIONS_SCOPE; }
static bool isTrigger(Kind k) { return k == TRIGGER_DM || k == TRIGGER_CH || k == TRIGGER_ROOM; }
static bool isHour(Kind k)    { return k == QUIET_FROM || k == QUIET_TO; }

static uint8_t& hour(NodePrefs* p, Kind k) { return k == QUIET_FROM ? p->bot_quiet_start : p->bot_quiet_end; }
static bool quietOff(const NodePrefs* p) { return p->bot_quiet_start == p->bot_quiet_end; }

// Placeholders a reply can use: the bot fills {name} / {hops} / {path} from the
// message it answers; {loc} / {time} and the sensor ones come from here.
// Most useful first (a frontend short of room shows a prefix).
// A log entry's one-line form: "14:30 Bob: !snr > SNR 6.5dB ...", the time
// local (tz hours); a room / channel entry marked with # / @.
static void logLine(const BotLogEntry& e, int8_t tz, char* out, size_t n) {
  uint32_t t = e.ts + (int32_t)tz * 3600;
  const char* mark = e.where == BotLog::CHANNEL ? "#" : e.where == BotLog::ROOM ? "@" : "";
  snprintf(out, n, "%02u:%02u %s%s: %s > %s", (unsigned)(t / 3600 % 24), (unsigned)(t / 60 % 60), mark,
           e.who[0] ? e.who : "?", e.req, e.reply);
}

static const char* const REPLY_PLACEHOLDERS[] = { "{name}", "{hops}", "{path}", "{snr}", "{loc}", "{time}", "{batt}", "{rssi}" };
static const int REPLY_PLACEHOLDER_COUNT = sizeof(REPLY_PLACEHOLDERS) / sizeof(REPLY_PLACEHOLDERS[0]);

// Shown value of a text row ("(none)", "(any msg)" for the "*" trigger).
static const char* shownText(const char* t, bool trigger) {
  if (!t[0]) return "(none)";
  if (trigger && t[0] == '*' && !t[1]) return "(any msg)";   // wildcard / away mode
  return t;
}

// Target names: false when none is set / it no longer exists.
static bool channelName(const NodePrefs* p, char* out, size_t n) {
  ChannelDetails ch;
  if (!the_mesh.getChannel(p->bot_channel_idx, ch) || !ch.name[0]) return false;
  snprintf(out, n, "%s", ch.name);
  return true;
}
static bool roomName(const NodePrefs* p, char* out, size_t n) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(p->bot_room_prefix, NodePrefs::FAVOURITE_PREFIX_LEN);
  if (!c || !c->name[0]) return false;
  snprintf(out, n, "%s", c->name);
  return true;
}
static void setRoom(NodePrefs* p, const uint8_t* pub_key) {
  memcpy(p->bot_room_prefix, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
}

}  // namespace botcfg
