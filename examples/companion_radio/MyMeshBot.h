#pragma once
// Bot logic — not part of upstream MyMesh.cpp.
// Included at the bottom of MyMesh.cpp after all class definitions.

void MyMesh::tryBotReplyDM(const ContactInfo& from, const char* text) {
  if (!(_prefs.bot_enabled && _prefs.bot_target_type == 1 &&
        _prefs.bot_trigger[0] && _prefs.bot_reply[0] &&
        millis() - _bot_last_reply_ms > 10000UL))
    return;

  // all-zero pubkey = any sender; otherwise match first 4 bytes
  bool key_ok = (_prefs.bot_dm_pubkey[0] == 0 && _prefs.bot_dm_pubkey[1] == 0 &&
                 _prefs.bot_dm_pubkey[2] == 0 && _prefs.bot_dm_pubkey[3] == 0) ||
                memcmp(from.id.pub_key, _prefs.bot_dm_pubkey, 4) == 0;
  if (!key_ok) return;

  const char* tr = _prefs.bot_trigger;
  int tlen = strlen(tr);
  bool matched = false;
  for (const char* t = text; *t && !matched; t++) {
    int i = 0;
    while (i < tlen && tolower((uint8_t)t[i]) == tolower((uint8_t)tr[i])) i++;
    if (i == tlen) matched = true;
  }
  if (!matched) return;

  uint32_t ts = getRTCClock()->getCurrentTime();
  char expanded[200];
  expandMsg(_prefs.bot_reply, expanded, sizeof(expanded),
            sensors.node_lat, sensors.node_lon,
            sensors.node_lat != 0.0 || sensors.node_lon != 0.0,
            ts, _prefs.tz_offset_hours);
  uint32_t expected_ack, est_timeout;
  if (sendMessage(from, ts, 0, expanded, expected_ack, est_timeout) != MSG_SEND_FAILED)
    _bot_last_reply_ms = millis();
}

void MyMesh::tryBotReplyChannel(uint8_t channel_idx, const char* text) {
  if (!(_prefs.bot_enabled && _prefs.bot_trigger[0] && _prefs.bot_reply[0] &&
        channel_idx == _prefs.bot_channel_idx &&
        millis() - _bot_last_reply_ms > 10000UL))
    return;

  const char* tr = _prefs.bot_trigger;
  int tlen = strlen(tr);
  bool matched = false;
  for (const char* t = text; *t && !matched; t++) {
    int i = 0;
    while (i < tlen && tolower((uint8_t)t[i]) == tolower((uint8_t)tr[i])) i++;
    if (i == tlen) matched = true;
  }
  if (!matched) return;

  ChannelDetails ch;
  if (!getChannel(channel_idx, ch)) return;

  uint32_t ts = getRTCClock()->getCurrentTime();
  char expanded[200];
  expandMsg(_prefs.bot_reply, expanded, sizeof(expanded),
            sensors.node_lat, sensors.node_lon,
            sensors.node_lat != 0.0 || sensors.node_lon != 0.0,
            ts, _prefs.tz_offset_hours);
  int rlen = strlen(expanded);
  if (sendGroupMessage(ts, ch.channel, _prefs.node_name, expanded, rlen))
    _bot_last_reply_ms = millis();
}
