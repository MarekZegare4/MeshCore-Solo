#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/ContactInfo.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../Trail.h"
#include "../Waypoint.h"
#include "KeyboardWidget.h"

class UITask : public AbstractUITask {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  bool _locked;
  unsigned long _lock_wake_until;  // when to blank screen again after locked wake (5s)
  int  _lock_seq_count;            // Enter presses while Back held (lock/unlock sequence)
  unsigned long _lock_seq_ms;      // millis() of last lock-sequence press (for timeout)
  bool _lock_seq_used;             // true = suppress next back_btn CLICK (post-sequence release)
  char _alert[80];
  char _notif_mel_buf[220];  // persistent RTTTL buffer for custom notification melodies
  KeyboardWidget _kb;        // shared across all screens — only one active at a time
  unsigned long _alert_expiry;
  int _msgcount;
  int _room_unread;
  int _last_notif_ch_idx;
  uint8_t _last_notif_dm_prefix[4];
  bool _last_notif_dm_valid;
  struct DMUnreadEntry { uint8_t prefix[4]; uint8_t count; };
  static const int DM_UNREAD_TABLE_SIZE = 16;
  DMUnreadEntry _dm_unread_table[DM_UNREAD_TABLE_SIZE];
  unsigned long ui_started_at, next_batt_chck;
  uint16_t _batt_mv;  // EMA-filtered battery voltage
  unsigned long next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  unsigned long next_led_change = 0;
  unsigned long last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  UIScreen* splash;
  UIScreen* home;
  UIScreen* settings;
  UIScreen* quick_msg;
  UIScreen* tools_screen;
  UIScreen* ringtone_edit;
  UIScreen* bot_screen;
  UIScreen* nearby_screen;
  UIScreen* dashboard_config;
  UIScreen* auto_advert_screen;
  UIScreen* trail_screen;
  UIScreen* compass_screen;
  UIScreen* curr;
  CayenneLPP _dash_lpp;
  TrailStore _trail;
  WaypointStore _waypoints;
  uint32_t _next_trail_sample_ms = 0;

  // Course-over-ground ring — a heading source independent of trail recording.
  // Filled from the same periodic GPS poll regardless of _trail.isActive().
  // Heading = bearing across the window (oldest→newest) once the cumulative
  // movement clears COG_MIN_MOVE_M; gross GPS jumps are rejected on insert.
  static const int COG_RING = 5;
  struct CogFix { int32_t lat, lon; uint32_t ms; };
  CogFix   _cog[COG_RING];
  uint8_t  _cog_head = 0, _cog_count = 0;
  int      _cog_deg = -1;            // last good heading, -1 = none yet
  uint32_t _next_cog_sample_ms = 0;
  void pushCogFix(int32_t lat, int32_t lon);

  // Ping state
  bool _ping_active = false;
  uint32_t _ping_tag = 0;
  unsigned long _ping_sent_ms = 0;
  int16_t _ping_snr_out_x4 = 0;
  int16_t _ping_snr_back_x4 = 0;
  uint32_t _ping_rtt_ms = 0;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL), _node_prefs(NULL), _dash_lpp(200) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    _batt_mv = 0;
    _msgcount = _room_unread = 0;
    _locked = false;
    _lock_wake_until = 0;
    _lock_seq_count = 0; _lock_seq_ms = 0; _lock_seq_used = false;
    _last_notif_ch_idx = -1;
    _last_notif_dm_valid = false;
    memset(_last_notif_dm_prefix, 0, sizeof(_last_notif_dm_prefix));
    memset(_dm_unread_table, 0, sizeof(_dm_unread_table));
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  NodePrefs* getNodePrefs() const { return _node_prefs; }
  uint16_t getBattMilliVolts() const { return _batt_mv > 0 ? _batt_mv : AbstractUITask::getBattMilliVolts(); }
  void gotoHomeScreen() { setCurrScreen(home); }
  void gotoSettingsScreen();
  void gotoQuickMsgScreen();
  void openContactDM(const ContactInfo& ci);
  void shareToMessage(const char* text);   // open Messages pre-loaded to share `text`
  int  getRecentDMContacts(uint8_t out[][NodePrefs::FAVOURITE_PREFIX_LEN], int max) const;
  void gotoToolsScreen();
  void gotoRingtoneEditor(int slot = 0);
  void gotoBotScreen();
  void gotoNearbyScreen();
  void gotoDashboardConfig();
  void gotoAutoAdvertScreen();
  void gotoTrailScreen();
  void gotoCompassScreen();
  TrailStore& trail() { return _trail; }
  WaypointStore& waypoints() { return _waypoints; }
  void saveWaypoints();
  // Add a waypoint, persist, and show the standard "Waypoint saved" / "Waypoints
  // full" alert. Returns true on success. The ts-less overload uses current RTC time.
  bool addWaypoint(int32_t lat, int32_t lon, uint32_t ts, const char* label);
  bool addWaypoint(int32_t lat, int32_t lon, const char* label);
  // Current course over ground in degrees (0..359), or false if not enough
  // recent movement to derive a stable heading. Independent of trail logging.
  bool currentCourse(int& deg_out) const;
  // Current GPS position (1e6-scaled degrees), false when there's no usable
  // fix. Single source of truth for "where am I", shared by the nav / compass
  // / map screens so the LocationProvider lookup isn't duplicated per screen.
  bool currentLocation(int32_t& lat, int32_t& lon) const;
  void playMelody(const char* melody);
  void stopMelody();
  bool isMelodyPlaying();
  void showAlert(const char* text, int duration_millis);
  void addChannelMsg(uint8_t channel_idx, const char* text) override;
  void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text) override;
  int  getDMUnreadTotal() const;
  int  getMsgCount() const { return _msgcount; }
  int  getChannelUnreadCount() const;
  int  getRoomUnreadCount() const { return _room_unread; }
  void clearRoomUnread() { _room_unread = 0; }
  uint8_t getDMUnread(const uint8_t* pub_key) const {
    for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++)
      if (_dm_unread_table[i].count > 0 && memcmp(_dm_unread_table[i].prefix, pub_key, 4) == 0)
        return _dm_unread_table[i].count;
    return 0;
  }
  void clearDMUnread(const uint8_t* pub_key) {
    for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++)
      if (_dm_unread_table[i].count > 0 && memcmp(_dm_unread_table[i].prefix, pub_key, 4) == 0)
        { _dm_unread_table[i].count = 0; return; }
  }
  void clearAllDMUnread() { memset(_dm_unread_table, 0, sizeof(_dm_unread_table)); }
  bool hasDisplay() const { return _display != NULL; }
  DisplayDriver* getDisplay() const { return _display; }

  // Ping helpers
  bool startPing(const uint8_t* pub_key);
  bool isPingActive() const { return _ping_active; }
  void getPingResult(int16_t& snr_out_x4, int16_t& snr_back_x4, uint32_t& rtt_ms) const {
    snr_out_x4 = _ping_snr_out_x4;
    snr_back_x4 = _ping_snr_back_x4;
    rtt_ms = _ping_rtt_ms;
  }
  void clearPing();
  void handlePingResult(uint32_t tag, int16_t snr_out_x4, int16_t snr_back_x4, uint32_t rtt_ms);

  // Favourites dial helpers. Slot index 0..FAVOURITES_COUNT-1.
  int findFavouriteSlot(const uint8_t* pub_key) const {
    if (!_node_prefs || !pub_key) return -1;
    for (int i = 0; i < NodePrefs::FAVOURITES_COUNT; i++) {
      if (memcmp(_node_prefs->favourite_contacts[i], pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
        // All-zero prefix is "empty" — never matches a real key.
        bool any = false;
        for (uint8_t b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++)
          if (_node_prefs->favourite_contacts[i][b]) { any = true; break; }
        if (any) return i;
      }
    }
    return -1;
  }
  bool isFavouriteSlotEmpty(int slot) const {
    if (!_node_prefs || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return true;
    for (uint8_t b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++)
      if (_node_prefs->favourite_contacts[slot][b]) return false;
    return true;
  }
  void setFavouriteSlot(int slot, const uint8_t* pub_key) {
    if (!_node_prefs || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT || !pub_key) return;
    memcpy(_node_prefs->favourite_contacts[slot], pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
  }
  void clearFavouriteSlot(int slot) {
    if (!_node_prefs || slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return;
    memset(_node_prefs->favourite_contacts[slot], 0, NodePrefs::FAVOURITE_PREFIX_LEN);
  }
  bool isButtonPressed() const;

  bool isBuzzerQuiet() { 
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  void cycleBuzzerMode();   // ON → OFF → Auto → ON
  int  getBuzzerMode(); // 0=ON, 1=OFF, 2=Auto
  bool getGPSState();
  void toggleGPS();
  void applyBrightness();
  void setBrightnessLevel(uint8_t level);
  uint8_t getBrightnessLevel() const { return _node_prefs ? _node_prefs->display_brightness : 2; }
  void setBuzzerVolumeLevel(uint8_t level);
  uint8_t getBuzzerVolume() const { return _node_prefs ? _node_prefs->buzzer_volume : 4; }
  void applyTxPower();
  void applyPowerSave();   // CAD-windowed RX on/off from prefs
  void applyFont();
  void applyRotation();
  void applyFullRefreshInterval();
  uint32_t autoOffMillis() const {
    if (!_node_prefs || _node_prefs->auto_off_secs == 0) return 0;
    return (uint32_t)_node_prefs->auto_off_secs * 1000UL;
  }


  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t contact_type = 0, const uint8_t* pub_key = nullptr) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
