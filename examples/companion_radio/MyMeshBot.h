#pragma once
// Bot logic — not part of upstream MyMesh.cpp.
// Included at the bottom of MyMesh.cpp after all class definitions.

// Minimum gap between two auto-replies to the same contact (DM), on the bot
// channel, or on the bot room. Guards against floods and, with
// botTriggerMatches()'s loop check, against bot↔bot ping-pong on a shared
// channel/room.
static const unsigned long BOT_REPLY_COOLDOWN_MS = 10000UL;
// Scratch buffer size for trigger matching / reply expansion. Covers the full
// incoming message (MAX_TEXT_LEN ~160) plus the 140-byte reply templates.
static const int BOT_SCRATCH = 200;

// Case-insensitive (ASCII) substring test: does `body` contain any one of
// `trigger`'s comma-separated phrases? A lone "*" trigger (the whole field,
// not one comma-separated phrase) means "match everything" (away / reply-to-
// all mode); honoured only where allow_wildcard is set. DM, channel and room
// each pass their own trigger string.
bool MyMesh::botTriggerMatches(const char* trigger, const char* body, bool allow_wildcard) const {
  if (!trigger[0]) return false;             // empty trigger would match everything
  if (trigger[0] == '*' && trigger[1] == '\0')
    return allow_wildcard;

  char low[BOT_SCRATCH];
  size_t n = strlen(body);
  if (n >= sizeof(low)) n = sizeof(low) - 1;
  for (size_t i = 0; i < n; i++) low[i] = (char)tolower((uint8_t)body[i]);
  low[n] = '\0';

  // Multiple phrases can be packed into one Trigger field, comma-separated
  // ("hi,hello there,yo") -- any single phrase matching is enough. Each
  // phrase is trimmed of surrounding spaces so "foo, bar" and "foo,bar"
  // behave the same; empty phrases (a stray/trailing comma) are skipped.
  for (const char* p = trigger; *p; ) {
    while (*p == ' ' || *p == ',') p++;        // skip separators / leading space
    if (!*p) break;
    const char* start = p;
    while (*p && *p != ',') p++;               // phrase runs to the next comma (or end)
    const char* end = p;
    while (end > start && end[-1] == ' ') end--;  // trim trailing space
    size_t plen = (size_t)(end - start);
    if (plen == 0) continue;

    char low_trigger[64];
    if (plen >= sizeof(low_trigger)) plen = sizeof(low_trigger) - 1;
    for (size_t i = 0; i < plen; i++) low_trigger[i] = (char)tolower((uint8_t)start[i]);
    low_trigger[plen] = '\0';

    if (strstr(low, low_trigger)) return true;
  }
  return false;
}

// DM allow-list gate: when bot_dm_scope is favourites-only, ignore triggers/
// commands from any contact that isn't starred (ContactInfo::flags bit 0 —
// the same "favourite" bit the Messages screen's DM list filter reads), so a
// bot left on can't be spammed by strangers while still answering people the
// user has starred. Channel and room bots have no equivalent — they're
// inherently public to whoever's already on that channel/room.
bool MyMesh::botDmSenderAllowed(const ContactInfo& from) const {
  if (_prefs.bot_dm_scope == 0) return true;   // all (default)
  return (from.flags & 0x01) != 0;             // favourites only
}

// Resolves a room post's signed author (a 4-byte pubkey prefix carried
// alongside the post — `from` is the room server itself, not the poster) to a
// display name for the {name} placeholder. Falls back to a generic label when
// the poster isn't a known contact (unverified beyond the 4-byte prefix).
void MyMesh::botRoomSenderName(const uint8_t* sender_prefix, char* out, int out_len) {
  ContactInfo* sc = sender_prefix ? lookupContactByPubKey(sender_prefix, 4) : nullptr;
  if (sc && sc->name[0]) snprintf(out, out_len, "%s", sc->name);
  else                   snprintf(out, out_len, "someone");
}

void MyMesh::botChannelSenderSplit(const char* text, char* sender_name, int sender_name_len, const char** msg_out) {
  *msg_out = text;
  snprintf(sender_name, sender_name_len, "someone");
  const char* sep = strstr(text, ": ");
  if (sep) {
    *msg_out = sep + 2;
    int n = (int)(sep - text);
    if (n > 0 && n < sender_name_len) { memcpy(sender_name, text, n); sender_name[n] = '\0'; }
  }
}

// Per-contact DM throttle: true if enough time has passed (or we've never
// replied) to this contact. Only the first 4 key bytes are compared — ample to
// tell local contacts apart.
bool MyMesh::botDmAllowed(const uint8_t* pubkey) {
  unsigned long now = millis();
  for (int i = 0; i < BOT_DM_LOG_SIZE; i++) {
    if (_bot_dm_log[i].used && memcmp(_bot_dm_log[i].key, pubkey, 4) == 0)
      return now - _bot_dm_log[i].t_ms > BOT_REPLY_COOLDOWN_MS;
  }
  return true;
}

void MyMesh::botDmRecord(const uint8_t* pubkey) {
  unsigned long now = millis();
  for (int i = 0; i < BOT_DM_LOG_SIZE; i++) {  // refresh existing entry
    if (_bot_dm_log[i].used && memcmp(_bot_dm_log[i].key, pubkey, 4) == 0) {
      _bot_dm_log[i].t_ms = now;
      return;
    }
  }
  int target = -1;                             // else take a free slot...
  for (int i = 0; i < BOT_DM_LOG_SIZE; i++)
    if (!_bot_dm_log[i].used) { target = i; break; }
  if (target < 0) {                            // ...or evict the oldest
    target = 0;                                // compare by elapsed-since-now (wraparound-safe),
    for (int i = 1; i < BOT_DM_LOG_SIZE; i++)   // not raw t_ms -- a bare '<' picks the wrong slot
      if ((int32_t)(now - _bot_dm_log[i].t_ms) >   // right after millis() rolls over (new entries
          (int32_t)(now - _bot_dm_log[target].t_ms)) // then have small raw values, not large ones)
        target = i;
  }
  memcpy(_bot_dm_log[target].key, pubkey, 4);
  _bot_dm_log[target].t_ms = now;
  _bot_dm_log[target].used = true;
}

// Quiet hours: when the local hour is inside [start, end) the auto-reply paths
// stay silent (commands are explicitly requested, so they ignore this). A zero-
// width window (start == end) disables the feature; a window where start > end
// wraps past midnight.
bool MyMesh::botInQuietHours() const {
  int h;
  if (!localHour(getRTCClock()->getCurrentTime(), _prefs.tz_offset_hours, h)) return false;   // clock not set -- don't suppress
  return hourInWindow(h, _prefs.bot_quiet_start, _prefs.bot_quiet_end);
}

// How the message about to be answered came in, for {path} / {snr} / {rssi}
// and !path / !snr / !dist. The route: hop hashes oldest first, ':' between
// hops, each hash as wide as the packet's own path hash mode ("3f:a1:c8",
// "3fa1:c802"); "direct" if heard directly. The sender's position: its
// contact's, when it has one (on a channel, the contact of that name).
static char       s_bot_path[MAX_PATH_SIZE * 3 + 8];
static BotInbound s_bot_in = { s_bot_path, 0, 0 };
static bool       s_bot_from_pos = false;
static int32_t    s_bot_from_lat = 0, s_bot_from_lon = 0;

void MyMesh::setBotInbound(const mesh::Packet* pkt, const ContactInfo* sender) {
  const uint8_t n = pkt ? pkt->getPathHashCount() : 0, w = pkt ? pkt->getPathHashSize() : 1;
  s_bot_in.snr = pkt ? pkt->getSNR() : 0;
  s_bot_in.rssi = _radio->getLastRSSI();
  s_bot_from_pos = sender && (sender->gps_lat || sender->gps_lon);
  if (s_bot_from_pos) { s_bot_from_lat = sender->gps_lat; s_bot_from_lon = sender->gps_lon; }
  if (n == 0) { strcpy(s_bot_path, "direct"); return; }
  char* o = s_bot_path;
  for (uint8_t h = 0; h < n; h++) {
    if (h) *o++ = ':';
    for (uint8_t b = 0; b < w; b++) { sprintf(o, "%02x", pkt->path[h * w + b]); o += 2; }
  }
}

// The contact named `name` (a channel sender: only a name, unverified).
const ContactInfo* MyMesh::botContactByName(const char* name) {
  for (int idx = 0; ; idx++) {
    ContactInfo c;
    if (!getContactByIdx(idx, c)) return nullptr;
    if (c.name[0] && strcmp(c.name, name) == 0) return lookupContactByPubKey(c.id.pub_key, PUB_KEY_SIZE);
  }
}

// Actions by (bot_actions_scope): everyone, or favourites only. On a channel
// the sender is only a name anyone can type, so there it's a weak check.
bool MyMesh::botActionsFrom(const ContactInfo* c) const {
  if (_prefs.bot_actions_scope == 0) return true;
  return c && (c->flags & 0x01);
}

void MyMesh::tryBotReplyDM(const ContactInfo& from, const char* text, uint8_t hops) {
  if (from.type != ADV_TYPE_CHAT) return;
  if (!(_prefs.bot_enabled && _prefs.bot_reply_dm[0])) return;
  if (!botDmSenderAllowed(from)) return;
  if (botInQuietHours()) return;
  if (!botTriggerMatches(_prefs.bot_trigger, text, true)) return;  // wildcard "*" allowed
  if (!botDmAllowed(from.id.pub_key)) return;        // already answered this contact recently

  uint32_t ts = getRTCClock()->getCurrentTime();
  char expanded[BOT_SCRATCH];
  expandMsg(_prefs.bot_reply_dm, expanded, sizeof(expanded),
            sensors.node_lat, sensors.node_lon,
            sensors.node_lat != 0.0 || sensors.node_lon != 0.0,
            ts, _prefs.tz_offset_hours,
            &sensors, (float)board.getBattMilliVolts() / 1000.0f,
            from.name, hops, board.getMCUTemperature(), &s_bot_in);
  uint32_t expected_ack, est_timeout;
  if (sendMessage(from, ts, 0, expanded, expected_ack, est_timeout) != MSG_SEND_FAILED) {
    botDmRecord(from.id.pub_key);
    _bot_reply_count++;
#ifdef DISPLAY_CLASS
    if (_listener) _listener->addDMMsg(from.id.pub_key, true, expanded);
#endif
  }
}

void MyMesh::tryBotReplyChannel(uint8_t channel_idx, const char* text, uint8_t hops) {
  // bot_channel_enabled is this target's own on/off switch — independent of
  // bot_enabled (the DM tab's Enable), matching how the commands paths and
  // the tabbed BotScreen UI already treat DM/channel/room as separate targets.
  if (!(_prefs.bot_channel_enabled && _prefs.bot_reply_ch[0] &&
        channel_idx == _prefs.bot_channel_idx &&
        millis() - _bot_last_ch_reply_ms > BOT_REPLY_COOLDOWN_MS))
    return;
  if (botInQuietHours()) return;

  // Split off the "SenderName: " prefix so the trigger is matched against the
  // message body only, and so the name is available for the {name}
  // placeholder. Unsigned/unverified — channel messages carry no identity
  // beyond this text convention.
  const char* msg;
  char sender_name[32];
  botChannelSenderSplit(text, sender_name, sizeof(sender_name), &msg);

  if (!botTriggerMatches(_prefs.bot_trigger_ch, msg, true)) return;  // own trigger; "*" = any msg

  ChannelDetails ch;
  if (!getChannel(channel_idx, ch)) return;

  uint32_t ts = getRTCClock()->getCurrentTime();
  char expanded[BOT_SCRATCH];
  expandMsg(_prefs.bot_reply_ch, expanded, sizeof(expanded),
            sensors.node_lat, sensors.node_lon,
            sensors.node_lat != 0.0 || sensors.node_lon != 0.0,
            ts, _prefs.tz_offset_hours,
            &sensors, (float)board.getBattMilliVolts() / 1000.0f,
            sender_name, hops, board.getMCUTemperature(), &s_bot_in);
  // Anti-loop: don't echo a message identical to our own reply (e.g. another bot
  // on the channel sending the same text), which would ping-pong forever. The
  // per-channel cooldown caps any residual back-and-forth between mismatched bots.
  if (strcmp(msg, expanded) == 0) return;
  int rlen = strlen(expanded);
  if (sendGroupMessage(ts, ch.channel, _prefs.node_name, expanded, rlen)) {
    _bot_last_ch_reply_ms = millis();
    _bot_reply_count++;
#ifdef DISPLAY_CLASS
    mirrorOwnChannelMsg(channel_idx, expanded);
#endif
  }
}

// Room-server counterpart to tryBotReplyChannel: a room also carries many
// posters, so it shares that function's "public reply, respects quiet hours +
// a single shared cooldown" shape, but the reply is a direct sendMessage() to
// the room server (same call used for a room's normal chat compose), like a
// DM, since the room server relays it to its members itself. Requires the
// device to already have an active login session with this room server (see
// NodePrefs::bot_room_prefix's doc comment) — otherwise sendMessage() still
// "succeeds" locally but the server silently drops the unauthorized post.
void MyMesh::tryBotReplyRoom(const ContactInfo& from, const uint8_t* sender_prefix, const char* text, uint8_t hops) {
  if (from.type != ADV_TYPE_ROOM) return;
  // bot_room_enabled is this target's own on/off switch — independent of
  // bot_enabled, same reasoning as tryBotReplyChannel above.
  if (!(_prefs.bot_room_enabled && _prefs.bot_reply_room[0] &&
        memcmp(from.id.pub_key, _prefs.bot_room_prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0 &&
        millis() - _bot_last_room_reply_ms > BOT_REPLY_COOLDOWN_MS))
    return;
  if (botInQuietHours()) return;
  if (!botTriggerMatches(_prefs.bot_trigger_room, text, true)) return;  // "*" = any post

  char sender_name[32];
  botRoomSenderName(sender_prefix, sender_name, sizeof(sender_name));

  uint32_t ts = getRTCClock()->getCurrentTime();
  char expanded[BOT_SCRATCH];
  expandMsg(_prefs.bot_reply_room, expanded, sizeof(expanded),
            sensors.node_lat, sensors.node_lon,
            sensors.node_lat != 0.0 || sensors.node_lon != 0.0,
            ts, _prefs.tz_offset_hours,
            &sensors, (float)board.getBattMilliVolts() / 1000.0f,
            sender_name, hops, board.getMCUTemperature(), &s_bot_in);
  // Anti-loop, mirrors the channel path: don't echo a message identical to
  // our own reply (e.g. re-synced back from the room server).
  if (strcmp(text, expanded) == 0) return;

  uint32_t expected_ack, est_timeout;
  if (sendMessage(from, ts, 0, expanded, expected_ack, est_timeout) != MSG_SEND_FAILED) {
    _bot_last_room_reply_ms = millis();
    _bot_reply_count++;
#ifdef DISPLAY_CLASS
    if (_listener) _listener->addDMMsg(from.id.pub_key, true, expanded);
#endif
  }
}

// Builds the reply text for a single command word into out. Returns false for an
// unknown command. !hops is dynamic (per received message; separate from the
// general {hops} placeholder so plain "!hops" keeps its terse "direct"/"N hops"
// reply without needing a template); the rest expand a static template via
// expandMsg's placeholders, including {name}/{hops} if the sender wrote them
// into a custom reply — not used by any of the built-in templates below today.
bool MyMesh::botCommandReply(const char* cmd, const char* arg, const char* arg2, bool actions_allowed,
                              uint8_t hops, uint32_t ts, char* out, int out_len, const char* sender_name) {
  if (!strcmp(cmd, "path")) {
    strncpy(out, s_bot_path, out_len);   // "direct" or the hop hashes, oldest first
    out[out_len - 1] = '\0';
    return true;
  }
  if (!strcmp(cmd, "snr")) {   // how the last hop was heard here
    snprintf(out, out_len, "SNR %.1fdB RSSI %.0fdBm", s_bot_in.snr, s_bot_in.rssi);
    return true;
  }
  if (!strcmp(cmd, "dist")) {   // from here to the sender, when both are known
    int32_t lat, lon;
    LocationProvider* lp = sensors.getLocationProvider();
    if (lp && lp->isValid()) { lat = lp->getLatitude(); lon = lp->getLongitude(); }
    else { lat = (int32_t)lround(sensors.node_lat * 1e6); lon = (int32_t)lround(sensors.node_lon * 1e6); }
    if (!lat && !lon)        { snprintf(out, out_len, "dist: no position here"); return true; }
    if (!s_bot_from_pos)     { snprintf(out, out_len, "dist: no position for you"); return true; }
    char d[16];
    geo::fmtDist(d, sizeof(d), geo::haversineKm(lat, lon, s_bot_from_lat, s_bot_from_lon), _prefs.units_imperial);
    snprintf(out, out_len, "%s %s", d, geo::bearingCardinal(geo::bearingDeg(lat, lon, s_bot_from_lat, s_bot_from_lon)));
    return true;
  }
  if (!strcmp(cmd, "hops")) {
    if (hops == 0) strncpy(out, "direct", out_len);     // heard directly (0 repeaters)
    else           snprintf(out, out_len, "%u hops", (unsigned)hops);
    out[out_len - 1] = '\0';
    return true;
  }

  // Action commands change device state, unlike every query below — gated by
  // this target's own bot_actions_* toggle (nested under Commands being on),
  // not by the templating/expandMsg mechanism the queries share.
  if (actions_allowed) {
    if (!strcmp(cmd, "gps")) {
      // "fix [seconds]" -- single-shot: turn GPS on (if not already), wait for
      // a stabilised position, send it, then restore GPS to whatever it was
      // before. This command can't reply synchronously like every other one
      // here (a fix takes seconds-to-minutes), so it acks immediately and
      // sets _locfix_requested; the tryBot*Command() wrapper that called us
      // knows the destination and starts the actual wait (see MyMesh.h).
      // Optional 2nd argument overrides the default LOCFIX_TIMEOUT_MS budget
      // -- useful under poor sky view, where 90s isn't always enough to reach
      // isLocFixReady()'s HDOP/satellite bar. Clamped to a sane range: floor
      // covers LOCFIX_AVERAGE_MS (no point requesting less than one averaging
      // window), ceiling keeps a forgotten/misfired request from parking GPS
      // on indefinitely.
      if (!strcmp(arg, "fix")) {
        if (_loc_fix.active) { snprintf(out, out_len, "GPS: fix already pending"); return true; }
        uint32_t timeout_ms = LOCFIX_TIMEOUT_MS;
        if (arg2[0]) {
          int secs = atoi(arg2);
          if (secs < 15) secs = 15;
          if (secs > 300) secs = 300;
          timeout_ms = (uint32_t)secs * 1000;
        }
        _locfix_requested = true;
        _locfix_requested_timeout_ms = timeout_ms;
        snprintf(out, out_len, "GPS: acquiring fix...");
        return true;
      }
      bool on  = !strcmp(arg, "on");   // arg was lowercased while parsing (see botScanCommands)
      bool off = !strcmp(arg, "off");
      if (!on && !off) { snprintf(out, out_len, "GPS: on|off|fix?"); return true; }
      // Deferred: applyPendingBotActions() actually flips the GPS state, once
      // quiet-hours/cooldown/throttle have passed (see MyMesh.h).
      _bot_gps_action_pending = true;
      _bot_gps_action_on = on;
      snprintf(out, out_len, "GPS: %s", on ? "on" : "off");
      return true;
    }
    if (!strcmp(cmd, "buzz")) {
      int secs = arg[0] ? atoi(arg) : 5;
      if (secs < 1) secs = 5;
      if (secs > 30) secs = 30;
      _bot_buzz_action_secs = secs;   // deferred -- see applyPendingBotActions()
      snprintf(out, out_len, "Buzzing");
      return true;
    }
    if (!strcmp(cmd, "advert")) {
      // Deferred -- can no longer report advert()'s own success/failure here
      // (it doesn't run until applyPendingBotActions()), so the ack is just
      // an acknowledgement, same spirit as !gps fix's "acquiring fix...".
      _bot_advert_action_pending = true;
      snprintf(out, out_len, "Advert requested");
      return true;
    }
    // !gpio1..!gpio4 -- user-assignable pins (see UITask::botSetGPIO/
    // botGetGPIO/botGetGPIOAnalog; no-op on boards without them). Bare (no
    // arg) reports current mode+level (millivolts if in Analog mode, GPIO1/
    // GPIO2 only); on/off only takes effect if that pin's Tools > GPIO mode
    // is currently Output (Analog/Input/Off all reply "not output"). The
    // Output check itself is read-only (botGetGPIO), so it's safe to do now
    // for an accurate reply; only the actual pin write is deferred.
    if (!strncmp(cmd, "gpio", 4) && cmd[4] >= '1' && cmd[4] <= '4' && cmd[5] == '\0') {
      int idx = cmd[4] - '0';
      if (arg[0]) {
        bool on  = !strcmp(arg, "on");
        bool off = !strcmp(arg, "off");
        if (!on && !off) { snprintf(out, out_len, "gpio%d: on|off?", idx); return true; }
        bool is_out = false, val = false;
        if (_listener && _listener->botGetGPIO(idx, is_out, val) && is_out) {
          _bot_gpio_action[idx - 1] = on ? 1 : 0;   // deferred -- see applyPendingBotActions()
          snprintf(out, out_len, "gpio%d: %s", idx, on ? "on" : "off");
        } else {
          snprintf(out, out_len, "gpio%d: not output", idx);
        }
        return true;
      }
      int mv = 0;
      bool is_out = false, val = false;
      if (_listener && _listener->botGetGPIOAnalog(idx, mv)) {
        snprintf(out, out_len, "gpio%d: %dmV", idx, mv);
      } else if (_listener && _listener->botGetGPIO(idx, is_out, val)) {
        snprintf(out, out_len, "gpio%d: %s %s", idx, is_out ? "out" : "in", val ? "on" : "off");
      } else {
        snprintf(out, out_len, "gpio%d: off", idx);
      }
      return true;
    }
  }

  const char* tmpl = nullptr;
  if      (!strcmp(cmd, "ping"))   tmpl = "pong";
  else if (!strcmp(cmd, "batt"))   tmpl = "{batt}";
  else if (!strcmp(cmd, "loc"))    tmpl = "{loc}";
  else if (!strcmp(cmd, "time"))   tmpl = "{time}";
  else if (!strcmp(cmd, "temp"))   tmpl = "{temp}";
  else if (!strcmp(cmd, "status")) tmpl = "batt {batt} loc {loc} at {time}";
  else if (!strcmp(cmd, "help"))   tmpl = actions_allowed
                                             ? "cmds: !ping !batt !loc !time !temp !hops !path !snr !dist !status !gps !buzz !advert !gpio1-4"
                                             : "cmds: !ping !batt !loc !time !temp !hops !path !snr !dist !status";
  else return false;                             // unknown — let the trigger bot try

  expandMsg(tmpl, out, out_len,
            sensors.node_lat, sensors.node_lon,
            sensors.node_lat != 0.0 || sensors.node_lon != 0.0,
            ts, _prefs.tz_offset_hours,
            &sensors, (float)board.getBattMilliVolts() / 1000.0f,
            sender_name, hops, board.getMCUTemperature(), &s_bot_in);
  if (strchr(out, '{')) strcpy(out, "n/a");      // requested sensor not present
  return true;
}

// Scans body for any number of space-separated "!word" tokens, building a single
// combined reply (segments joined by " | ") into out. Returns how many commands
// were recognised. Shared by the DM, channel and room command paths.
// actions_allowed gates the state-changing commands (!gps/!buzz/!advert) --
// the read-only queries are always reachable once a target's Commands is on.
int MyMesh::botScanCommands(const char* body, uint8_t hops, uint32_t ts, char* out, int out_len,
                             const char* sender_name, bool actions_allowed) {
  resetPendingBotActions();   // pending actions/flags below are set by tokens in this scan;
                              // caller applies or clears them once it knows if the reply sent
  // Reads one space-delimited token at p into buf (lowercased, truncated to
  // cap-1 chars with any overflow consumed too), leaving p on the space/NUL
  // that ended it. Shared by the command name and its two optional args.
  auto readToken = [](const char*& p, char* buf, int cap) {
    int i = 0;
    while (*p && *p != ' ' && i < cap - 1) buf[i++] = (char)tolower((uint8_t)*p++);
    buf[i] = '\0';
    while (*p && *p != ' ') p++;
  };
  int oi = 0;
  int matched = 0;
  for (const char* p = body; *p; ) {
    while (*p == ' ') p++;                        // skip whitespace
    if (*p != '!') {                              // not a command token
      while (*p && *p != ' ') p++;
      continue;
    }
    p++;                                          // step past '!'
    char cmd[16];
    readToken(p, cmd, sizeof(cmd));
    if (!cmd[0]) continue;

    // Up to two optional arguments: the next one or two space-delimited
    // tokens, unless a token is itself the start of another command ("!").
    // E.g. "!gps fix 120 !batt" -- "fix"/"120" are !gps's arguments, "!batt"
    // starts the next command. Almost every command only reads arg1 (arg2 is
    // always "" for them) -- a stray second word after e.g. "!gpio1 on foo"
    // just gets consumed as arg2 and ignored, same net effect as before this
    // supported two tokens. Only "!gps fix" currently reads arg2 (an optional
    // timeout override, see botCommandReply).
    while (*p == ' ') p++;
    char arg1[16] = "", arg2[16] = "";
    if (*p && *p != '!') {
      readToken(p, arg1, sizeof(arg1));
      while (*p == ' ') p++;
      if (*p && *p != '!') readToken(p, arg2, sizeof(arg2));
    }

    char seg[80];
    if (!botCommandReply(cmd, arg1, arg2, actions_allowed, hops, ts, seg, sizeof(seg), sender_name)) continue;  // unknown

    if (matched > 0 && oi + 3 < out_len) {        // " | " separator
      out[oi++] = ' '; out[oi++] = '|'; out[oi++] = ' ';
    }
    int sl = strlen(seg);
    if (oi + sl >= out_len) sl = out_len - 1 - oi;
    if (sl > 0) { memcpy(out + oi, seg, sl); oi += sl; }
    matched++;
  }
  out[oi] = '\0';
  return matched;
}

// DM query commands. The reply is a pull, not a push, so it ignores quiet hours;
// the per-contact throttle still applies. Returns true when at least one command
// was found (replied or throttled), so the caller skips the trigger-reply path.
bool MyMesh::tryBotCommand(const ContactInfo& from, const char* text, uint8_t hops) {
  if (!_prefs.bot_commands_enabled) return false;
  if (from.type != ADV_TYPE_CHAT) return false;
  if (!botDmSenderAllowed(from)) return false;

  uint32_t ts = getRTCClock()->getCurrentTime();
  char out[BOT_SCRATCH];
  if (botScanCommands(text, hops, ts, out, sizeof(out), from.name, _prefs.bot_actions_dm != 0 && botActionsFrom(&from)) == 0) return false;  // no commands
  if (!botDmAllowed(from.id.pub_key)) { resetPendingBotActions(); return true; }  // throttled

  uint32_t expected_ack, est_timeout;
  if (sendMessage(from, ts, 0, out, expected_ack, est_timeout) != MSG_SEND_FAILED) {
    botDmRecord(from.id.pub_key);
    _bot_reply_count++;
#ifdef DISPLAY_CLASS
    if (_listener) _listener->addDMMsg(from.id.pub_key, true, out);
#endif
    if (_locfix_requested) startLocFix(LOCFIX_DEST_CONTACT, from.id.pub_key, 0);
    applyPendingBotActions();
  }
  resetPendingBotActions();
  return true;
}

// Channel query commands — only on the bot's monitored channel. The reply is
// broadcast to everyone on the channel, so unlike DM commands it respects quiet
// hours and uses the global per-channel cooldown (shared with the trigger reply).
bool MyMesh::tryBotChannelCommand(uint8_t channel_idx, const char* text, uint8_t hops) {
  if (!(_prefs.bot_commands_ch && _prefs.bot_channel_enabled &&
        channel_idx == _prefs.bot_channel_idx))
    return false;

  // Skip "sender_name: " prefix so commands are read from the message body only.
  const char* msg;
  char sender_name[32];
  botChannelSenderSplit(text, sender_name, sizeof(sender_name), &msg);

  uint32_t ts = getRTCClock()->getCurrentTime();
  char out[BOT_SCRATCH];
  if (botScanCommands(msg, hops, ts, out, sizeof(out), sender_name, _prefs.bot_actions_ch != 0 && botActionsFrom(botContactByName(sender_name))) == 0) return false;  // no commands
  if (botInQuietHours()) { resetPendingBotActions(); return true; }                                       // quiet hours
  if (millis() - _bot_last_ch_reply_ms <= BOT_REPLY_COOLDOWN_MS) { resetPendingBotActions(); return true; }  // throttled

  ChannelDetails ch;
  if (!getChannel(channel_idx, ch)) { resetPendingBotActions(); return true; }
  int rlen = strlen(out);
  if (sendGroupMessage(ts, ch.channel, _prefs.node_name, out, rlen)) {
    _bot_last_ch_reply_ms = millis();
    _bot_reply_count++;
#ifdef DISPLAY_CLASS
    mirrorOwnChannelMsg(channel_idx, out);
#endif
    if (_locfix_requested) startLocFix(LOCFIX_DEST_CHANNEL, nullptr, channel_idx);
    applyPendingBotActions();
  }
  resetPendingBotActions();
  return true;
}

// Room query commands — mirrors tryBotChannelCommand: the reply posts to the
// room (visible to every member), so it respects quiet hours and the shared
// per-room cooldown, unlike the DM command path's private-pull exemption.
bool MyMesh::tryBotRoomCommand(const ContactInfo& from, const uint8_t* sender_prefix, const char* text, uint8_t hops) {
  if (from.type != ADV_TYPE_ROOM) return false;
  if (!(_prefs.bot_commands_room && _prefs.bot_room_enabled &&
        memcmp(from.id.pub_key, _prefs.bot_room_prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0))
    return false;

  char sender_name[32];
  botRoomSenderName(sender_prefix, sender_name, sizeof(sender_name));

  uint32_t ts = getRTCClock()->getCurrentTime();
  char out[BOT_SCRATCH];
  if (botScanCommands(text, hops, ts, out, sizeof(out), sender_name, _prefs.bot_actions_room != 0 &&
                      botActionsFrom(sender_prefix ? lookupContactByPubKey(sender_prefix, 4) : nullptr)) == 0) return false;  // no commands
  if (botInQuietHours()) { resetPendingBotActions(); return true; }                          // quiet hours
  if (millis() - _bot_last_room_reply_ms <= BOT_REPLY_COOLDOWN_MS) { resetPendingBotActions(); return true; }   // throttled

  uint32_t expected_ack, est_timeout;
  if (sendMessage(from, ts, 0, out, expected_ack, est_timeout) != MSG_SEND_FAILED) {
    _bot_last_room_reply_ms = millis();
    _bot_reply_count++;
#ifdef DISPLAY_CLASS
    if (_listener) _listener->addDMMsg(from.id.pub_key, true, out);
#endif
    if (_locfix_requested) startLocFix(LOCFIX_DEST_CONTACT, from.id.pub_key, 0);
    applyPendingBotActions();
  }
  resetPendingBotActions();
  return true;
}

// Actually runs the state-changing bot actions botCommandReply() recorded
// this scan (!gps on|off, !buzz, !advert, !gpio1..4 on|off) -- called by the
// tryBot*Command() wrappers only after quiet-hours/cooldown/per-contact
// throttle passed and the ack itself sent, so those gates genuinely block the
// action, not just the reply text (see the _bot_*_action_* fields in
// MyMesh.h). !gps fix's startLocFix() is armed separately by the caller (it
// needs the destination, which this function doesn't have).
void MyMesh::applyPendingBotActions() {
  if (_bot_gps_action_pending && _listener) _listener->botSetGPS(_bot_gps_action_on);
  if (_bot_buzz_action_secs > 0 && _listener) _listener->botBuzz(_bot_buzz_action_secs);
  if (_bot_advert_action_pending) advert();
  for (int i = 0; i < 4; i++) {
    if (_bot_gpio_action[i] >= 0 && _listener) _listener->botSetGPIO(i + 1, _bot_gpio_action[i] != 0);
  }
}

// Clears every pending-action flag botCommandReply() may have set this scan,
// without running them -- used both at scan start and on every throttled/
// aborted path, so a suppressed reply never leaves a stale action armed for
// next time.
void MyMesh::resetPendingBotActions() {
  _locfix_requested = false;
  _locfix_requested_timeout_ms = LOCFIX_TIMEOUT_MS;
  _bot_gps_action_pending = false;
  _bot_buzz_action_secs = 0;
  _bot_advert_action_pending = false;
  for (int i = 0; i < 4; i++) _bot_gpio_action[i] = -1;
}

// Arms the "!gps fix" wait: turns GPS on (remembering whether it already was,
// so tickLocFix() restores rather than assumes "off"), and resets the
// averaging accumulator. pub_key is only read for LOCFIX_DEST_CONTACT (DM or
// room -- both reply via sendMessage, see sendLocFixResult()); pass nullptr
// for LOCFIX_DEST_CHANNEL.
void MyMesh::startLocFix(uint8_t dest_type, const uint8_t* pub_key, uint8_t channel_idx) {
  _loc_fix.active = true;
  _loc_fix.dest_type = dest_type;
  if (pub_key) memcpy(_loc_fix.pub_key, pub_key, PUB_KEY_SIZE);
  _loc_fix.channel_idx = channel_idx;
  _loc_fix.sum_lat = 0.0;
  _loc_fix.sum_lon = 0.0;
  _loc_fix.sample_count = 0;
  _loc_fix.averaging_until_ms = 0;
  _loc_fix.deadline_ms = futureMillis(_locfix_requested_timeout_ms);
  _loc_fix.gps_was_on = (_prefs.gps_enabled != 0);
  if (!_loc_fix.gps_was_on && _listener) _listener->botSetGPS(true);
}

// !gps fix state machine, ticked every MyMesh::loop() while _loc_fix.active.
// Phase 1 (acquire): wait for a valid fix good enough to trust -- HDOP (fix
// geometry) when the provider exposes it, satellite count as a cruder
// fallback when it doesn't (see isLocFixReady()). Phase 2 (average): once
// that's first met, keep summing node_lat/node_lon for LOCFIX_AVERAGE_MS more
// -- only on ticks where the threshold still holds, so a momentary dip just
// skips a sample instead of aborting the whole wait. A hard LOCFIX_TIMEOUT_MS
// deadline covers both phases; on timeout with zero samples collected the
// result is a plain failure (no stale-cache fallback -- a cached node_lat/lon
// from long before this request would be actively misleading here).
bool MyMesh::isLocFixReady(LocationProvider* loc) {
  if (!loc || !loc->isValid()) return false;
  long hdop = loc->getHDOP();
  if (hdop >= 0) return hdop <= LOCFIX_MAX_HDOP;    // preferred: real fix-quality signal
  return loc->satellitesCount() >= LOCFIX_MIN_SATS; // provider has no HDOP -- cruder fallback
}

void MyMesh::tickLocFix() {
  if (!_loc_fix.active) return;

  LocationProvider* loc = sensors.getLocationProvider();
  bool ready = isLocFixReady(loc);

  if (_loc_fix.averaging_until_ms == 0 && ready) {
    _loc_fix.averaging_until_ms = futureMillis(LOCFIX_AVERAGE_MS);
  }
  if (_loc_fix.averaging_until_ms != 0 && ready) {
    _loc_fix.sum_lat += sensors.node_lat;
    _loc_fix.sum_lon += sensors.node_lon;
    _loc_fix.sample_count++;
  }

  bool averaging_done = _loc_fix.averaging_until_ms != 0 && millisHasNowPassed(_loc_fix.averaging_until_ms);
  bool timed_out = millisHasNowPassed(_loc_fix.deadline_ms);
  if (!averaging_done && !timed_out) return;   // still waiting

  char msg[BOT_SCRATCH];
  if (_loc_fix.sample_count > 0) {
    double avg_lat = _loc_fix.sum_lat / _loc_fix.sample_count;
    double avg_lon = _loc_fix.sum_lon / _loc_fix.sample_count;
    snprintf(msg, sizeof(msg), averaging_done ? "loc: %.5f,%.5f" : "loc: %.5f,%.5f (partial fix)", avg_lat, avg_lon);
  } else {
    snprintf(msg, sizeof(msg), "GPS: no fix (timeout)");
  }
  sendLocFixResult(msg);

  if (!_loc_fix.gps_was_on && _listener) _listener->botSetGPS(false);
  _loc_fix.active = false;
}

// Delivers tickLocFix()'s result to wherever the original "!gps fix" came
// from. Not gated by quiet hours/cooldown -- that gate already ran once, on
// the immediate ack (see the tryBot*Command() wrappers); this is the tail of
// an already-approved interaction, not a new proactive reply. Still updates
// the same cooldown timestamps/counters on success so subsequent trigger/
// command replies stay correctly throttled afterward.
void MyMesh::sendLocFixResult(const char* msg) {
  uint32_t ts = getRTCClock()->getCurrentTime();
  if (_loc_fix.dest_type == LOCFIX_DEST_CHANNEL) {
    ChannelDetails ch;
    if (!getChannel(_loc_fix.channel_idx, ch)) return;
    int mlen = strlen(msg);
    if (sendGroupMessage(ts, ch.channel, _prefs.node_name, msg, mlen)) {
      _bot_last_ch_reply_ms = millis();
      _bot_reply_count++;
#ifdef DISPLAY_CLASS
      mirrorOwnChannelMsg(_loc_fix.channel_idx, msg);
#endif
    }
  } else {   // LOCFIX_DEST_CONTACT -- DM or room, both go through sendMessage
    ContactInfo* c = lookupContactByPubKey(_loc_fix.pub_key, PUB_KEY_SIZE);
    if (!c) return;   // contact removed while we were waiting
    uint32_t expected_ack, est_timeout;
    if (sendMessage(*c, ts, 0, msg, expected_ack, est_timeout) != MSG_SEND_FAILED) {
      if (c->type == ADV_TYPE_ROOM) _bot_last_room_reply_ms = millis();
      else botDmRecord(c->id.pub_key);
      _bot_reply_count++;
#ifdef DISPLAY_CLASS
      if (_listener) _listener->addDMMsg(c->id.pub_key, true, msg);
#endif
    }
  }
}
