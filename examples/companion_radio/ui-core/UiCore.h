#pragma once
// UI Core: hardware-independent UI state and logic shared by every frontend
// (ui-new on the OLED / e-ink boards, ui-lvgl on the L2). See
// docs/solo/developer/ui-core.md.
//
// The Core is MyMesh's Listener: it files incoming/outgoing messages into the
// history, keeps the unread counters, runs the engines, and tells the frontend
// what happened through `events` (drained from the frontend's loop()). The
// frontend implements UiCoreHost for the few things the Core still asks of it.
//
// For now the Core is header-only and compiled as part of the frontend's single
// translation unit (ui-new/UITask.cpp includes this file), so no platformio.ini
// needs a new source filter or include path. It relies on the same globals the
// frontend does (the_mesh, rtc_clock).

#include "MessageHistory.h"
#include "DmUnreadTable.h"
#include "UiEvents.h"
#include "UiCoreHost.h"
#include "ClockEngine.h"
#include "PingEngine.h"
#include "CourseEngine.h"
#include "LiveShareEngine.h"
#include "LocatorEngine.h"
#include "TrailEngine.h"
#include "WaypointModel.h"
#include "ChannelControl.h"
#include "AdminSession.h"
#include "RoomSessions.h"
#include "ContactControl.h"

class UiCore : public MyMesh::Listener {
public:
  void begin(NodePrefs* prefs, SensorManager* sensors, UiCoreHost* host) {
    _host = host;
    _prefs = prefs;
    _sensors = sensors;
    clock.begin(prefs, &events);
    ping.begin(prefs);
    course.begin(sensors);
    live_share.begin(prefs, &course, &events);
    locator.begin(prefs, &course, &live_share, &events);
    trail.begin(prefs, &course);
    waypoints.begin(&locator);   // loads /waypoints
  }

  // Driven from the frontend's loop(), before it drains `events`.
  void loop() {
    // Background delivery: resend pending on-device DMs whose ACK timed out,
    // and finalise the ✗ marker; free DM unread slots the ring no longer backs.
    history.tickDmResends();
    dm_unread.reconcile(history);
    clock.loop();
    course.loop();
    live_share.loop();
    locator.loop();
    trail.loop();
    admin.loop();
    rooms.loop();
  }

  UiEventQueue events;      // Core → frontend; drained by the frontend's loop()

  // ── Engines ───────────────────────────────────────────────────────────────
  ClockEngine clock;        // alarm / countdown / ring
  PingEngine  ping;         // single in-flight ping + last result
  CourseEngine course;      // GPS position + course-over-ground ring
  LiveShareEngine live_share; // [LOC] auto-share session + peers' shared positions
  LocatorEngine locator;    // active target, geofence crossings, proximity beeper
  TrailEngine  trail;       // GPS trail store, sampling, auto-pause, shutdown save
  AdminSession admin;       // remote repeater / room admin: login + CLI round trips
  RoomSessions rooms;       // room server logins for posting (messages screens)

  // ── Models ────────────────────────────────────────────────────────────────
  MessageHistory history;   // channel + DM rings, delivery state, channel unread
  DmUnreadTable  dm_unread; // per-contact DM unread counters
  WaypointModel  waypoints; // saved places, persisted to /waypoints

  // ── Unread ────────────────────────────────────────────────────────────────
  // Messages waiting in the companion-app offline queue (0 = the app synced).
  int     msgCount() const                        { return _queue_len; }
  int     roomUnread() const                      { return _room_unread; }
  void    clearRoomUnread()                       { _room_unread = 0; }
  // DM unread, clamped to what the DM ring still holds.
  int     dmUnreadTotal() const                   { return dm_unread.total(history); }
  uint8_t dmUnread(const uint8_t* pub_key) const  { return dm_unread.get(history, pub_key); }
  bool    dmUnreadOverflow(const uint8_t* pub_key) const { return dm_unread.overflow(pub_key); }
  bool    anyDMUnreadOverflow() const             { return dm_unread.anyOverflow(); }
  void    clearDMUnread(const uint8_t* pub_key)   { dm_unread.clear(pub_key); }
  void    clearAllDMUnread()                      { dm_unread.clearAll(); }
  // Every conversation read: direct, channels, rooms.
  void    markAllRead() { _room_unread = 0; dm_unread.clearAll(); history.clearAllChannelUnread(); }

  // ── Actions ───────────────────────────────────────────────────────────────
  // Send a DM composed on the device and file it (with end-to-end ACK tracking
  // and auto-resend) into the history. False if it couldn't be sent.
  bool sendDirectText(const ContactInfo& to, const char* text) {
    uint32_t send_ts = rtc_clock.getCurrentTime();
    uint32_t expected_ack = 0, est_timeout = 0;
    if (the_mesh.sendMessage(to, send_ts, 0, text, expected_ack, est_timeout) <= 0) return false;
    uint32_t tag = 0, deadline = 0;
    if (expected_ack) {
      tag = expected_ack;
      // Generous margin over the base estimate so a slow multi-hop ACK isn't
      // prematurely shown as failed.
      deadline = millis() + est_timeout + 4000;
    }
    history.storeDMMsg(to.id.pub_key, true, text, tag, deadline, tag ? send_ts : 0,
                       _prefs ? _prefs->dm_resend_count : 0);
    return true;
  }

  // Send a channel post composed on the device, file it as "Me: …" with the
  // relay marker armed, and treat the channel as read. False if not sent.
  bool sendChannelText(uint8_t channel_idx, const char* text) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(channel_idx, ch)) return false;
    if (!the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                   the_mesh.getNodeName(), text, strlen(text))) return false;
    char entry[MSG_TEXT_BUF];
    snprintf(entry, sizeof(entry), "Me: %s", text);
    int pos = addChannelMsg(channel_idx, entry, 0, nullptr, 0, true);
    if (pos >= 0) history.armChannelRelay(pos, the_mesh.lastChannelRelaySeq());
    history.setChUnread(channel_idx, 0);
    return true;
  }

  // GPS power: the sensor manager's "gps" setting, persisted as
  // NodePrefs::gps_enabled (MyMesh::applyGpsPrefs() restores it at boot).
  bool gpsAvailable() const { return gpsSettingIndex() >= 0; }
  bool gpsEnabled() const {
    int i = gpsSettingIndex();
    return i >= 0 && strcmp(_sensors->getSettingValue(i), "1") == 0;
  }
  // False when the board has no GPS.
  // save = false: the caller has the prefs written later (the L2 on leaving
  // the screen -- a flash write mid-tap stalls the switch's animation).
  bool setGpsEnabled(bool on, bool save = true) {
    if (gpsSettingIndex() < 0) return false;
    _sensors->setSettingValue("gps", on ? "1" : "0");
    if (_prefs) _prefs->gps_enabled = on ? 1 : 0;
    if (save) the_mesh.savePrefs();
    return true;
  }

  // GPS duty cycle (NodePrefs::gps_interval, seconds; 0 = always on) to the sensor layer.
  void applyGpsInterval() {
    if (!_sensors || !_prefs) return;
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)_prefs->gps_interval);
    _sensors->setSettingValue("gps_interval", buf);
  }

  UiCoreHost* host() { return _host; }
  NodePrefs*  prefs() { return _prefs; }

  // ════ MyMesh::Listener ════════════════════════════════════════════════════

  void onQueueSizeChanged(int msgcount) override {
    _queue_len = msgcount;
    if (msgcount == 0) markAllRead();   // the app drained the queue: nothing is unread any more
  }

  void onACKRecv(uint32_t ack_crc) override { history.markDmDelivered(ack_crc); }
  void onAdvertHeard(bool was_flood) override { events.push(UiEventType::AdvertHeard, nullptr, was_flood); }
  bool requestShutdown(bool restart) override { _host->shutdown(restart); return true; }

  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp,
                     const char* text) override {
    onMessageRecvEx(pkt, from, txt_type, sender_timestamp, nullptr, 0, text);
  }
  void onMessageRecvEx(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp,
                       const uint8_t* extra, int extra_len, const char* text) override {
    // we only want to show text messages on display, not cli data
    if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;
    if (from.type == ADV_TYPE_ROOM && _room_unread < _queue_len) _room_unread++;
    if (from.type == ADV_TYPE_CHAT) dm_unread.onIncoming(from.id.pub_key);   // before the ring insert below
    UiEvent& ev = pushMessageArrived(from.type == ADV_TYPE_ROOM ? UIEventType::roomMessage : UIEventType::contactMessage,
                                     from.name, -1);
    if (from.type == ADV_TYPE_CHAT) { memcpy(ev.key, from.id.pub_key, 4); ev.flag = true; }
    // Add to the on-device conversation history. Room servers (ADV_TYPE_ROOM) are
    // viewed through the same history list as chat contacts (keyed by the server's
    // pubkey), so their posts must be stored too — otherwise an incoming room
    // message fires the notification and reaches the app via the offline queue but
    // never shows when the room is opened directly on the device.
    if (from.type == ADV_TYPE_CHAT) {
      addDMMsg(from.id.pub_key, false, text, sender_timestamp, 0, 0, 0, pkt->path, (uint8_t)pkt->path_len);
    } else if (from.type == ADV_TYPE_ROOM) {
      // A room carries many guests, so prefix the post with its author so the UI
      // can attribute each line. The signed message's `extra` holds the sender's
      // pubkey prefix; resolve it to a contact name, falling back to a short hex.
      char labeled[MAX_TEXT_LEN + 40];  // room text + "Sender: " (history store truncates)
      if (extra && extra_len >= 4) {
        ContactInfo* sc = the_mesh.lookupContactByPubKey(extra, extra_len);
        if (sc && sc->name[0])
          snprintf(labeled, sizeof(labeled), "%s: %s", sc->name, text);
        else
          snprintf(labeled, sizeof(labeled), "%02X%02X: %s", extra[0], extra[1], text);
      } else {
        snprintf(labeled, sizeof(labeled), "%s", text);
      }
      addDMMsg(from.id.pub_key, false, labeled, sender_timestamp, 0, 0, 0, pkt->path, (uint8_t)pkt->path_len);
    }
  }

  // Upstream-shaped entry point without the channel slot: MyMesh itself always
  // calls the Ex variant below, so this only serves a caller that doesn't know
  // the slot -- notify, but there's no history ring to file it under.
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override {
    pushMessageArrived(UIEventType::channelMessage, channel_details.name, -1);
  }
  void onChannelMessageRecvEx(mesh::Packet *pkt, uint8_t channel_idx, ChannelDetails& channel_details,
                              uint32_t timestamp, const char* text) override {
    addChannelMsg(channel_idx, text, timestamp, pkt->path, (uint8_t)pkt->path_len);
    pushMessageArrived(UIEventType::channelMessage, channel_details.name, channel_idx);
  }

  // Also the entry point for our own sends mirrored from the app / the bot
  // (own_message) and for on-device composes. Returns the ring position.
  int addChannelMsg(uint8_t channel_idx, const char* text, uint32_t timestamp = 0,
                    const uint8_t* path = nullptr, uint8_t path_len = 0,
                    bool own_message = false) override {
    bool viewing = _host->isViewingChannel(channel_idx);
    int pos = history.addChannelMsg(channel_idx, text, viewing, timestamp, path, path_len, own_message);
    if (viewing && pos >= 0) _host->onViewedHistoryGrew(true);
    return pos;
  }
#ifdef SIM_PLATFORM
  // Sim tests: channel 0 and the first chat contact get 12 read messages, then 5 new.
  void simUnreadDemo() {
    uint32_t now = rtc_clock.getCurrentTime();
    static const char* const W[] = { "Alice", "Bob", "Me", "Carol" };
    const uint8_t* dm = nullptr;
    ContactInfo c;
    for (int i = 0; i < the_mesh.getNumContacts() && !dm; i++)
      if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) && c.type == ADV_TYPE_CHAT) dm = c.id.pub_key;
    for (int i = 0; i < 17; i++) {
      char t[48];
      snprintf(t, sizeof(t), "%s: message %d", i < 12 ? W[i % 4] : W[i % 2], i + 1);
      addChannelMsg(0, t, now - (17 - i) * 120);
      if (dm) {
        bool out = i < 12 && i % 3 == 2;
        if (!out) dm_unread.onIncoming(dm);
        addDMMsg(dm, out, strstr(t, ": ") + 2, now - (17 - i) * 120);
      }
      if (i == 11) { history.setChUnread(0, 0); if (dm) clearDMUnread(dm); }
    }
  }
#endif
  void armChannelRelay(int pos, uint32_t seq) override { history.armChannelRelay(pos, seq); }
  void onChannelRelayed(uint32_t seq, const uint8_t* repeater_hash = nullptr, uint8_t hash_size = 0) override {
    history.markChannelRelayed(seq, repeater_hash, hash_size);
  }

  void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text, uint32_t sender_timestamp = 0,
                uint32_t ack_tag = 0, uint32_t ack_deadline_ms = 0, uint8_t resends = 0,
                const uint8_t* path = nullptr, uint8_t path_len = 0) override {
    bool viewing = _host->isViewingDM(pub_key);
    history.addDMMsg(pub_key, outgoing, text, sender_timestamp, ack_tag, ack_deadline_ms, resends, path, path_len);
    if (viewing) _host->onViewedHistoryGrew(false);
    dm_unread.afterInsert(history);
  }

  void onSharedLocation(const uint8_t* pub_key, const char* name, int32_t lat_1e6, int32_t lon_1e6,
                        uint32_t ts, bool verified) override {
    live_share.onSharedLocation(pub_key, name, lat_1e6, lon_1e6, ts, verified);
  }

  // Login answers go to the admin session when it's the one waiting, else to
  // the room sessions (logins from the messages screen), then the frontend.
  void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) override {
    if (!admin.onLoginResult(pub_key, success, permissions)) {
      rooms.onLoginResult(pub_key, success);
      _host->onRoomLoginResult(pub_key, success, permissions);
    }
    _host->onAdminStateChanged();
  }
  void onAdminReply(const uint8_t* pub_key, const char* text) override {
    admin.onReply(pub_key, text);
    _host->onAdminStateChanged();
  }
  // A contact / channel went away (deleted here, from the app, or evicted):
  // drop every pref keyed on it, then let the frontend refresh.
  void onContactRemoved(const uint8_t* pub_key) override {
    if (pub_key) {
      clearDMUnread(pub_key);
      if (cleanupContactPrefs(pub_key)) the_mesh.savePrefs();
    }
    _host->onContactRemoved(pub_key);
  }
  void onChannelRemoved(uint8_t channel_idx) override {
    if (chanctl::onRemoved(_prefs, channel_idx)) the_mesh.savePrefs();
    _host->onChannelRemoved(channel_idx);
  }
  void botSetGPS(bool on) override { _host->botSetGPS(on); }
  void botBuzz(int seconds) override { _host->botBuzz(seconds); }
  bool botSetGPIO(int idx, bool on) override { return _host->botSetGPIO(idx, on); }
  bool botGetGPIO(int idx, bool& is_output, bool& value) override { return _host->botGetGPIO(idx, is_output, value); }
  bool botGetGPIOAnalog(int idx, int& millivolts) override { return _host->botGetGPIOAnalog(idx, millivolts); }

private:
  UiEvent& pushMessageArrived(UIEventType kind, const char* name, int channel_idx) {
    UiEvent& ev = events.push(UiEventType::MessageArrived, name);
    ev.kind = kind;
    ev.idx  = (int16_t)channel_idx;
    return ev;
  }

  int gpsSettingIndex() const {
    if (!_sensors) return -1;
    for (int i = 0; i < _sensors->getNumSettings(); i++)
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) return i;
    return -1;
  }

  // CONTRACT: every NodePrefs field keyed on a contact pubkey / prefix is
  // cleared here (adding one: clear it below, mark it in NodePrefs.h). Runs
  // for explicit removal and silent eviction (CMD_REMOVE_CONTACT /
  // onContactOverwrite). Covers the favourite dial slot, locator
  // target, live share / room bot targets (fail closed: turned off rather than
  // inherited by a re-added contact), and the 16-slot notif / melody override
  // tables, where an orphan would eventually starve live contacts. dm_notif /
  // dm_melody key on 4 bytes, the others on FAVOURITE_PREFIX_LEN.
  bool cleanupContactPrefs(const uint8_t* pub_key) {
    NodePrefs* p = _prefs;
    if (!p) return false;
    bool changed = false;
    int slot = favslots::findContact(p, pub_key);
    if (slot >= 0) { favslots::clear(p, slot); changed = true; }
    if (locator.onContactRemoved(pub_key)) changed = true;
    if (p->loc_share_target_type == 1 &&
        memcmp(p->loc_share_dm_prefix, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
      p->loc_share_enabled = 0;
      changed = true;
    }
    if (p->bot_room_enabled && memcmp(p->bot_room_prefix, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
      p->bot_room_enabled = 0;
      changed = true;
    }
    for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++)
      if (p->dm_notif[i].state && memcmp(p->dm_notif[i].prefix, pub_key, 4) == 0) {
        memset(&p->dm_notif[i], 0, sizeof(p->dm_notif[i]));
        changed = true;
      }
    for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++)
      if (p->dm_melody[i].slot && memcmp(p->dm_melody[i].prefix, pub_key, 4) == 0) {
        memset(&p->dm_melody[i], 0, sizeof(p->dm_melody[i]));
        changed = true;
      }
    return changed;
  }

  UiCoreHost* _host = nullptr;
  NodePrefs*  _prefs = nullptr;
  SensorManager* _sensors = nullptr;
  int _queue_len = 0;     // last onQueueSizeChanged()
  int _room_unread = 0;
};
