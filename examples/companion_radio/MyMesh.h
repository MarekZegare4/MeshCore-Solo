#pragma once

#include <Arduino.h>
#include <Mesh.h>
#include "AbstractUITask.h"
#include <helpers/ui/DisplayDriver.h>

// Forward declaration for UITask
class UITask;

/*------------ Frame Protocol --------------*/
#define FIRMWARE_VER_CODE 13

#ifndef FIRMWARE_BUILD_DATE
#define FIRMWARE_BUILD_DATE "12 Aug 2026"
#endif

// Fallback only -- every real build (local or CI) goes through build.sh, which
// always injects FIRMWARE_VERSION itself (the pushed tag name for a release,
// "dev-<commit>" otherwise; see build-solo-firmwares.yml). This default only
// shows up for a `pio run` invoked directly, bypassing build.sh entirely.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "v1.25-dev"
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
#include <InternalFileSystem.h>
#elif defined(RP2040_PLATFORM)
#include <LittleFS.h>
#elif defined(ESP32)
#include <SPIFFS.h>
#endif

#include "DataStore.h"
#include "NodePrefs.h"

#include <RTClib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>
#include <helpers/StaticPoolPacketManager.h>
#include <target.h>

/* ---------------------------------- CONFIGURATION ------------------------------------- */

// LORA_FREQ/BW/SF/CR fallbacks now live in NodePrefs.h (DataStore.cpp needs them too).
#ifndef LORA_TX_POWER
#define LORA_TX_POWER 20
#endif
#ifndef MAX_LORA_TX_POWER
#define MAX_LORA_TX_POWER LORA_TX_POWER
#endif

#ifndef MAX_CONTACTS
#define MAX_CONTACTS 100
#endif

#ifndef OFFLINE_QUEUE_SIZE
#define OFFLINE_QUEUE_SIZE 16
#endif

#ifndef BLE_NAME_PREFIX
#define BLE_NAME_PREFIX "MeshCore-"
#endif

#include <helpers/BaseChatMesh.h>
#include <helpers/TransportKeyStore.h>

/* -------------------------------------------------------------------------------------- */

#define REQ_TYPE_GET_STATUS             0x01 // same as _GET_STATS
#define REQ_TYPE_KEEP_ALIVE             0x02
#define REQ_TYPE_GET_TELEMETRY_DATA     0x03

struct AdvertPath {
  uint8_t pubkey_prefix[7];
  uint8_t path_len;
  char    name[32];
  uint32_t recv_timestamp;
  uint8_t path[MAX_PATH_SIZE];
};

struct DiscoverResult {
  char    name[32];   // contact name if known, "" if unknown (use type label)
  uint8_t type;       // ADV_TYPE_REPEATER / ADV_TYPE_SENSOR / ADV_TYPE_ROOM
  bool    is_known;   // true = in contacts[], false = new unknown node
  int8_t  rssi;       // RSSI of the response as received by us (dBm)
  int8_t  snr_x4;     // SNR of the response as received by us (dB × 4)
  int8_t  remote_snr_x4; // SNR at which responder heard our request (dB × 4)
  uint8_t pub_key[PUB_KEY_SIZE];
  uint32_t timestamp;
};

#define EXPECTED_ACK_TABLE_SIZE 8

class MyMesh : public BaseChatMesh, public DataStoreHost {
public:
  MyMesh(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables, DataStore& store, AbstractUITask* ui=NULL);

  void begin(bool has_display);
  void startInterface(BaseSerialInterface &serial);

  const char *getNodeName();
  NodePrefs *getNodePrefs();
  uint32_t getBLEPin();

  void loop();
  void handleCmdFrame(size_t len);
  bool advert();
  void sendNodeDiscoverReq();
  void enterCLIRescue();

  int  getRecentlyHeard(AdvertPath dest[], int max_num);
  int  getDiscoverResults(DiscoverResult dest[], int max_count);

  // On-device contact management — lets Nearby Nodes add a discovered node or
  // delete a contact without the phone app. Mirrors the CMD_ADD/REMOVE paths.
  bool addDiscoveredContact(const uint8_t* pub_key, const char* name, uint8_t type);
  bool deleteContactByKey(const uint8_t* pub_key);

  // Ping/Trace functionality
  #define PING_RESULT_MAX 4
  typedef void (*PingCallback)(uint32_t tag, int16_t snr_out_x4, int16_t snr_back_x4, uint32_t rtt_ms);
  
  struct PingResult {
    uint32_t tag;
    uint32_t auth_code;
    int16_t snr_out_x4;    // SNR out (to first hop) × 4
    int16_t snr_back_x4;   // SNR back (from last hop to us) × 4
    uint32_t rtt_ms;       // Round-trip time in milliseconds
    bool received;
    unsigned long sent_ms;
  };
  
  uint32_t sendPing(const uint8_t* dest_pubkey, uint8_t hash_width = 1);
  void setPingCallback(PingCallback cb, void* arg);
  void clearPingResult(uint32_t tag);
  PingResult* getPingResult(uint32_t tag);
  PingCallback getPingCallback() const { return _ping_callback; }
  AbstractUITask* getUITask() const { return _ui; }

protected:
  float getAirtimeBudgetFactor() const override;
  int getInterferenceThreshold() const override;
  bool getCADEnabled() const override;
  int calcRxDelay(float score, uint32_t air_time) const override;
  uint32_t getRetransmitDelay(const mesh::Packet *packet) override;
  uint32_t getDirectRetransmitDelay(const mesh::Packet *packet) override;
  uint8_t getExtraAckTransmitCount() const override;
  bool filterRecvFloodPacket(mesh::Packet* packet) override;
  bool allowPacketForward(const mesh::Packet* packet) override;
  bool isRepeatLooped(const mesh::Packet* packet) const;
  // Overhear suppression only makes sense while repeating; gated behind its own
  // opt-in pref (Tools > Repeater > Suppress dup).
  bool wantsOverhearSuppress() const override { return _prefs.client_repeat && _prefs.repeat_suppress_dup; }

  void sendFloodScoped(const TransportKey& scope, mesh::Packet* pkt, uint32_t delay_millis);
  void sendFloodScoped(const ContactInfo& recipient, mesh::Packet* pkt, uint32_t delay_millis=0) override;
  void sendFloodScoped(const mesh::GroupChannel& channel, mesh::Packet* pkt, uint32_t delay_millis=0) override;

  void logRxRaw(float snr, float rssi, const uint8_t raw[], int len) override;
  bool isAutoAddEnabled() const override;
  bool shouldAutoAddContactType(uint8_t type) const override;
  bool shouldOverwriteWhenFull() const override;
  uint8_t getAutoAddMaxHops() const override;
  void onContactsFull() override;
  void onContactOverwrite(const uint8_t* pub_key) override;
  bool onContactPathRecv(ContactInfo& from, uint8_t* in_path, uint8_t in_path_len, uint8_t* out_path, uint8_t out_path_len, uint8_t extra_type, uint8_t* extra, uint8_t extra_len) override;
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onDiscoveredAdvert(bool was_flood) override;
  void onContactPathUpdated(const ContactInfo &contact) override;
  ContactInfo* processAck(const uint8_t *data) override;
  void queueMessage(const ContactInfo &from, uint8_t txt_type, mesh::Packet *pkt, uint32_t sender_timestamp,
                    const uint8_t *extra, int extra_len, const char *text);

  void onMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                     const char *text) override;
  void onCommandDataRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                         const char *text) override;
  void onSignedMessageRecv(const ContactInfo &from, mesh::Packet *pkt, uint32_t sender_timestamp,
                           const uint8_t *sender_prefix, const char *text) override;
  void onChannelMessageRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint32_t timestamp,
                            const char *text) override;
  void onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                         const uint8_t *data, size_t data_len) override;

  uint8_t onContactRequest(const ContactInfo &contact, uint32_t sender_timestamp, const uint8_t *data,
                           uint8_t len, uint8_t *reply) override;
  void onContactResponse(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;
  void onControlDataRecv(mesh::Packet *packet) override;
  void onRawDataRecv(mesh::Packet *packet) override;
  void onTraceRecv(mesh::Packet *packet, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  uint32_t calcFloodTimeoutMillisFor(uint32_t pkt_airtime_millis) const override;
  uint32_t calcDirectTimeoutMillisFor(uint32_t pkt_airtime_millis, uint8_t path_len) const override;
  void onSendTimeout() override;
  void onAckRecv(mesh::Packet* packet, uint32_t ack_crc) override;          // APC: ACK SNR sample
  // APC internals (see MyMesh.cpp): one reverse-link SNR sample, a lost-confirmation
  // ramp-up, and tracking an originated flood so its echo (or absence) can be scored.
  // The heard-echo sampling itself lives in filterRecvFloodPacket() (declared below).
  void apcSampleSnr(float snr);
  void apcOnFailure();
  void apcTrackFloodSend(const mesh::Packet* pkt);
  void trackRelaySend(const mesh::Packet* pkt);   // arm the UI relayed-into-mesh tracker
public:
  // Seq of the most recently tracked channel send — the UI records it on the
  // outgoing history entry so a heard echo (onChannelRelayed) can match it back.
  uint32_t lastChannelRelaySeq() const { return _last_relay_seq; }
private:

  // DataStoreHost methods
  bool onContactLoaded(const ContactInfo& contact) override { return addContact(contact); }
  bool getContactForSave(uint32_t idx, ContactInfo& contact) override { return getContactByIdx(idx, contact); }
  bool onChannelLoaded(uint8_t channel_idx, const ChannelDetails& ch) override { return setChannel(channel_idx, ch); }
  bool getChannelForSave(uint8_t channel_idx, ChannelDetails& ch) override { return getChannel(channel_idx, ch); }

  void clearPendingReqs() {
    pending_login = pending_status = pending_telemetry = pending_discovery = pending_req = ui_pending_login = 0;
  }

public:
  // On-device UI login to a room/repeater contact — no phone app required.
  // The room server's ACL grants permission per-identity (self_id), not per
  // command source, so this reuses the same sendLogin() the BLE CMD_SEND_LOGIN
  // path uses; the async result lands in onContactResponse() and is pushed to
  // the UI via AbstractUITask::onRoomLoginResult().
  bool sendRoomLogin(const ContactInfo& contact, const char* password, uint32_t& est_timeout) {
    if (sendLogin(contact, password, est_timeout) == MSG_SEND_FAILED) return false;
    clearPendingReqs();
    memcpy(&ui_pending_login, contact.id.pub_key, 4); // match this in onContactResponse()
    return true;
  }

  // Called by a screen that gave up waiting on its own sendRoomLogin() (Cancel
  // or a timeout) so a reply that arrives after doesn't get misrouted:
  // UITask::onRoomLoginResult() dispatches by whichever screen is *currently*
  // showing, not by who actually sent the request, so a late reply landing
  // after the requester navigated away is delivered to whatever screen the
  // user is on instead -- which then persists its own unrelated login state
  // (e.g. a different screen's _login_pw) as if it were that reply's answer.
  // Pubkey-guarded so this is a no-op if a newer request (this screen retrying,
  // or a different screen entirely) has since overwritten ui_pending_login.
  void cancelUiPendingLogin(const uint8_t* pub_key) {
    if (ui_pending_login && memcmp(&ui_pending_login, pub_key, 4) == 0) ui_pending_login = 0;
  }

  // On-device UI logout: drops the local keep-alive tracking (mirrors the app's
  // CMD_LOGOUT) and forgets the saved password, so the next room open prompts
  // for credentials again instead of silently re-using them. No packet is sent
  // to the server -- the room ACL has no session state to tear down, this is
  // purely local "forget this login" bookkeeping.
  void logoutRoom(const uint8_t* pub_key) {
    stopConnection(pub_key);
    forgetRoomPassword(pub_key);
  }

  // On-device-saved room/repeater login passwords, persisted to flash (own
  // small file, independent of /contacts3) so a room that's already been
  // logged into doesn't need its password retyped after reboot.
  bool saveRoomPassword(const uint8_t* pub_key, const char* password);
  bool getRoomPassword(const uint8_t* pub_key, char* out_password, uint8_t max_len);
  void forgetRoomPassword(const uint8_t* pub_key);

  // On-device channel add/edit/delete (Messages > Channels). Shares the exact
  // setChannel + saveChannels + onChannelRemoved-cleanup sequence the
  // CMD_SET_CHANNEL BLE handler already performs, so both paths stay in sync.
  bool setChannelLocal(uint8_t idx, const ChannelDetails& ch);

  // On-device "remote admin" (Tools > Admin): send a CLI command to a node
  // you're logged into with admin permission (see ClientACL::isAdmin()). The
  // reply is a text frame (TXT_TYPE_CLI_DATA) delivered via onCommandDataRecv();
  // this just tracks which contact's reply AdminScreen is currently waiting on.
  bool sendAdminCommand(const ContactInfo& contact, const char* cmd_text, uint32_t& est_timeout) {
    if (sendCommandData(contact, rtc_clock.getCurrentTime(), 0, cmd_text, est_timeout) == MSG_SEND_FAILED)
      return false;
    memcpy(&ui_pending_admin_reply, contact.id.pub_key, 4);
    return true;
  }

  void savePrefs() { _store->savePrefs(_prefs, sensors.node_lat, sensors.node_lon); }
  void saveRTCTime() { _store->saveRTCTime(); }
  DataStore* getDataStore() const { return _store; }
  void applyApc();   // (re)initialise Adaptive Power Control from prefs
  // Adaptive Power Control is suppressed while repeating: a repeater wants full,
  // consistent TX power for relay reach, and its feedback sources (own ACKs /
  // own flood echoes) don't fire on forwarded traffic anyway. applyApc() then
  // pins power to the ceiling.
  bool apcActive() const { return _prefs.tx_apc && !_prefs.client_repeat; }

  // True when the optional repeater radio profile is a valid LoRa config for
  // this radio (freq bounds come from the chip's own validated range).
  bool repeaterProfileValid() const {
    float lo, hi; radio_driver.getFreqBounds(lo, hi);
    return isValidRepeaterProfile(_prefs.repeater_freq, _prefs.repeater_bw, _prefs.repeater_sf, _prefs.repeater_cr, lo, hi);
  }
  // Load the radio with the correct params for the current mode: the repeater
  // profile when relaying with a valid dedicated profile, otherwise the
  // companion's own params. Single source of truth, used at boot and whenever
  // the repeater toggle / network / profile changes.
  void applyRepeaterRadio();

  bool isAckPending(uint32_t expected_ack) const {
    if (expected_ack == 0) return false;   // 0 marks an empty/cleared slot, not a real ACK
    for (int i = 0; i < EXPECTED_ACK_TABLE_SIZE; i++)
      if (expected_ack_table[i].ack != 0 && expected_ack_table[i].ack == expected_ack) return true;
    return false;
  }

#if ENV_INCLUDE_GPS == 1
  void applyGpsPrefs() {
    sensors.setSettingValue("gps", _prefs.gps_enabled ? "1" : "0");
    // gps_interval doubles as the GPS duty-cycle sleep window in seconds
    // (0 = disabled, GPS stays continuous) -- see EnvironmentSensorManager::
    // gpsDutyCycleLoop(). Max: 24 hours = 86400 seconds (5 digits + null).
    char interval_str[12];
    sprintf(interval_str, "%u", _prefs.gps_interval);
    sensors.setSettingValue("gps_interval", interval_str);
  }
#endif

  // To check if there is pending work
  bool hasPendingWork() const;

  // Number of auto-replies sent since boot (DM + channel + room). Shown on BotScreen.
  uint16_t botReplyCount() const { return _bot_reply_count; }

  // Whether a "!gps fix" request is acquiring/averaging right now -- used by
  // UITask's GPS duty-cycle "is anything live using GPS right now" check, so
  // the scheduler doesn't nap GPS out from under an in-flight bot request.
  bool isGpsFixPending() const { return _loc_fix.active; }

private:
  void tryBotReplyDM(const ContactInfo& from, const char* text, uint8_t hops);
  void tryBotReplyChannel(uint8_t channel_idx, const char* text, uint8_t hops);
  void tryBotReplyRoom(const ContactInfo& from, const uint8_t* sender_prefix, const char* text, uint8_t hops);
  bool tryBotCommand(const ContactInfo& from, const char* text, uint8_t hops);          // DM commands
  bool tryBotChannelCommand(uint8_t channel_idx, const char* text, uint8_t hops);       // channel commands
  bool tryBotRoomCommand(const ContactInfo& from, const uint8_t* sender_prefix, const char* text, uint8_t hops); // room commands
  bool botCommandReply(const char* cmd, const char* arg, const char* arg2, bool actions_allowed, uint8_t hops, uint32_t ts, char* out, int out_len, const char* sender_name);  // one command → reply text
  int  botScanCommands(const char* body, uint8_t hops, uint32_t ts, char* out, int out_len, const char* sender_name, bool actions_allowed); // scan "!word"s → combined reply, returns count
  // !gps fix -- single-shot "wait for a stabilised GPS fix, then push a follow-up
  // message" action. botCommandReply() only sets _locfix_requested (it doesn't know
  // the destination); the tryBot*Command() wrappers call startLocFix() with the
  // destination they each already have, but only once the immediate ack actually
  // sent (so a throttled/suppressed ack never starts a fix nobody will hear about).
  void tickLocFix();     // ticked every loop() while _loc_fix.active
  void startLocFix(uint8_t dest_type, const uint8_t* pub_key, uint8_t channel_idx);
  void sendLocFixResult(const char* msg);
  static bool isLocFixReady(LocationProvider* loc);   // HDOP if the provider has it, else satellite count
  bool botTriggerMatches(const char* trigger, const char* body, bool allow_wildcard) const;
  bool botInQuietHours() const;               // true when auto-replies should stay silent
  bool botDmAllowed(const uint8_t* pubkey);   // per-contact DM throttle: ok to reply?
  void botDmRecord(const uint8_t* pubkey);    // remember we just replied to this contact
  // DM allow-list gate (bot_dm_scope): true unless scope is favourites-only
  // and `from` isn't starred. Shared by the DM trigger-reply and command paths.
  bool botDmSenderAllowed(const ContactInfo& from) const;
  // Resolves a room post's signed author prefix to a display name for {name};
  // falls back to a generic label when the poster isn't a known contact.
  void botRoomSenderName(const uint8_t* sender_prefix, char* out, int out_len);
  // Splits a channel message's leading "SenderName: " text convention off its
  // body, for the {name} placeholder and to match triggers/commands against
  // the body only. `*msg_out` points into `text` (no copy); `sender_name`
  // defaults to "someone" if there's no ": " separator.
  void botChannelSenderSplit(const char* text, char* sender_name, int sender_name_len, const char** msg_out);

  void writeOKFrame();
  void writeErrFrame(uint8_t err_code);
  void writeDisabledFrame();
  void writeContactRespFrame(uint8_t code, const ContactInfo &contact);
  void updateContactFromFrame(ContactInfo &contact, uint32_t& last_mod, const uint8_t *frame, int len);
  void addToOfflineQueue(const uint8_t frame[], int len);
  int getFromOfflineQueue(uint8_t frame[]);
  int getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) override { 
    return _store->getBlobByKey(key, key_len, dest_buf);
  }
  bool putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], int len) override {
    return _store->putBlobByKey(key, key_len, src_buf, len);
  }

  void checkCLIRescueCmd();
  void checkSerialInterface();
  bool isValidClientRepeatFreq(uint32_t f) const;
#ifdef ENABLE_SCREENSHOT
  void handleScreenshotRequest();
  void sendScreenshotResponse(DisplayDriver* display, const uint8_t* buffer, uint16_t bufferSize);
#endif

  // helpers, short-cuts
  void saveChannels() { _store->saveChannels(this); }
  void saveContacts();

  DataStore* _store;
  NodePrefs _prefs;
  uint32_t pending_login;
  uint32_t ui_pending_login;  // like pending_login, but triggered by on-device UI instead of BLE/USB app
  char pending_login_pw[16];  // password of the in-flight app/USB login, persisted on success for ADV_TYPE_ROOM (see saveRoomPassword)
  uint32_t ui_pending_admin_reply;  // pub_key prefix of the contact AdminScreen's sendAdminCommand() is awaiting a CLI reply from
  uint32_t pending_status;
  uint32_t pending_telemetry, pending_discovery;   // pending _TELEMETRY_REQ
  uint32_t pending_req;   // pending _BINARY_REQ
  BaseSerialInterface *_serial;
  AbstractUITask* _ui;

  ContactsIterator _iter;
  uint32_t _iter_filter_since;
  uint32_t _most_recent_lastmod;
  uint32_t _active_ble_pin;
  bool _iter_started;
  bool _cli_rescue;
  int8_t _apc_cur_dbm;       // APC current TX power (≤ tx_power_dbm ceiling) when tx_apc on
  float _apc_margin_ewma;    // APC smoothed reverse-link SNR margin above the SF demod floor
  uint8_t _apc_fail_count;   // APC consecutive lost-confirmation count (graduated ramp-up)
  uint8_t _apc_flood_hash[MAX_HASH_SIZE];  // hash of the channel/flood send awaiting a repeater echo
  uint16_t _apc_flood_len;                 // its payload length — cheap pre-filter before hashing
  uint32_t _apc_flood_deadline;            // echo-wait deadline for that send
  bool _apc_flood_pending;                 // a tracked flood send is awaiting its echo
  // UI "relayed into mesh" tracker for channel sends — independent of APC. A small
  // ring so a quick burst of channel sends are each tracked (not just the latest).
  // Hashing on receive only runs while at least one slot is pending, so the hot
  // flood-recv path is untouched otherwise.
  static const int RELAY_RING = 4;
  struct RelaySlot {
    uint8_t  hash[MAX_HASH_SIZE];
    uint16_t len;
    uint32_t deadline;
    uint32_t seq;
    bool     pending;
  };
  RelaySlot _relay[RELAY_RING];
  int      _relay_head;        // next ring slot to overwrite
  int      _relay_active;      // number of slots currently pending (cheap gate)
  uint32_t _relay_seq;         // monotonic id counter for tracked sends
  uint32_t _last_relay_seq;    // seq of the most recent tracked send (for the UI to record)
  bool send_unscoped;   // force un-scoped flood (instead of using send_scope)
  char cli_command[80];
  uint8_t app_target_ver;
  uint8_t *sign_data;
  uint32_t sign_data_len;
  unsigned long dirty_contacts_expiry;
  unsigned long _bot_last_ch_reply_ms;
  unsigned long _bot_last_room_reply_ms;
  unsigned long _next_auto_advert_ms;

  // Per-contact DM reply throttle: a small ring of the most recent recipients so
  // one chatty contact can't be spammed while a different sender is still served.
  struct BotReplyLog { uint8_t key[4]; unsigned long t_ms; bool used; };
  static const int BOT_DM_LOG_SIZE = 8;
  BotReplyLog _bot_dm_log[BOT_DM_LOG_SIZE];
  uint16_t    _bot_reply_count;   // total auto-replies sent since boot

  // !gps fix state -- one global slot (one physical GPS): botCommandReply()
  // rejects a second request outright while one is active, so this never
  // needs to be an array. See tickLocFix()/startLocFix() in MyMeshBot.h.
  // Readiness: HDOP (tenths, lower=better) when the provider exposes it --
  // 20 == HDOP 2.0, the usual "good fix" cutoff -- else satellite count as a
  // cruder fallback (see LocationProvider::getHDOP()'s -1 = "not available").
  static const int LOCFIX_MAX_HDOP = 20;
  static const int LOCFIX_MIN_SATS = 8;            // readiness threshold (HDOP-less fallback)
  static const uint32_t LOCFIX_AVERAGE_MS = 10000;  // once ready, keep averaging this long
  static const uint32_t LOCFIX_TIMEOUT_MS = 90000;  // hard stop covering both phases
  enum { LOCFIX_DEST_CONTACT = 0, LOCFIX_DEST_CHANNEL = 1 };  // CONTACT covers DM and room alike (both reply via sendMessage)
  struct PendingLocFix {
    bool     active;
    bool     gps_was_on;         // restore to this when done, not unconditionally "off"
    uint32_t deadline_ms;
    uint32_t averaging_until_ms; // 0 while still acquiring; set once the sat threshold is first met
    double   sum_lat, sum_lon;
    int      sample_count;
    uint8_t  dest_type;
    uint8_t  pub_key[PUB_KEY_SIZE];  // dest_type == LOCFIX_DEST_CONTACT
    uint8_t  channel_idx;            // dest_type == LOCFIX_DEST_CHANNEL
  };
  PendingLocFix _loc_fix;
  bool _locfix_requested;   // transient: set by botCommandReply() when "!gps fix" was
                            // seen this scan, cleared by the tryBot*Command() wrapper
  uint32_t _locfix_requested_timeout_ms;  // "!gps fix [seconds]" override, see startLocFix()

  // Deferred bot actions (!gps on|off, !buzz, !advert, !gpio1..4 on|off) --
  // botCommandReply() only records what was requested; the actual hardware/
  // radio side effect happens in applyPendingBotActions(), called by the
  // tryBot*Command() wrappers only once quiet-hours/cooldown/per-contact
  // throttle have passed and the ack actually sent. Otherwise those gates
  // would only suppress the reply text while the action fired unconditionally
  // on every matching message (e.g. !buzz still buzzing during quiet hours,
  // or an unthrottled !advert flooding the mesh). Mirrors the _locfix_requested
  // pattern above; resetPendingBotActions() is the throttled/aborted-path
  // twin of applyPendingBotActions(), used wherever _locfix_requested used to
  // be cleared alone.
  bool _bot_gps_action_pending;
  bool _bot_gps_action_on;
  int  _bot_buzz_action_secs;    // 0 = no !buzz requested this scan
  bool _bot_advert_action_pending;
  int8_t _bot_gpio_action[4];    // per pin: -1 none requested, 0 off, 1 on
  void applyPendingBotActions();
  void resetPendingBotActions();

  TransportKey send_scope;

  uint8_t cmd_frame[MAX_FRAME_SIZE + 1];
  uint8_t out_frame[MAX_FRAME_SIZE + 1];
  CayenneLPP telemetry;

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];

    bool isChannelMsg() const;
  };
  int offline_queue_len;
  Frame offline_queue[OFFLINE_QUEUE_SIZE];

  struct AckTableEntry {
    unsigned long msg_sent;
    uint32_t ack;
    ContactInfo* contact;
  };
  AckTableEntry expected_ack_table[EXPECTED_ACK_TABLE_SIZE]; // circular table
  int next_ack_idx;

  #define ADVERT_PATH_TABLE_SIZE   16
  AdvertPath advert_paths[ADVERT_PATH_TABLE_SIZE]; // circular table

  #define DISCOVER_RESULTS_MAX 16
  DiscoverResult  _discover_results[DISCOVER_RESULTS_MAX];
  int             _discover_count;
  uint32_t        _pending_node_discover_tag;
  unsigned long   _pending_node_discover_until;

  // Dedup for NODE_DISCOVER_RESP copies heard more than once: the responder's
  // zero-hop direct copy and a re-flooded copy relayed by another repeater
  // carry different packet hashes, so the mesh duplicate filter passes both.
  // Keyed by (tag, responder pubkey prefix) with a short expiry — covers both
  // the standalone on-device scan and responses forwarded to the app (which
  // would otherwise list the same repeater twice).
  #define DISCOVER_SEEN_MAX 16
  struct DiscoverSeen { uint32_t tag; uint8_t pk[6]; unsigned long until; };
  DiscoverSeen _disc_seen[DISCOVER_SEEN_MAX];
  uint8_t      _disc_seen_head;
  bool isDupDiscoverResp(uint32_t tag, const uint8_t* pub_key);   // records when new

  // ── Ping/Trace state ──────────────────────────────────────────────────────
  PingResult _ping_results[PING_RESULT_MAX];
  PingCallback _ping_callback;
  void* _ping_callback_arg;
};

extern MyMesh the_mesh;
