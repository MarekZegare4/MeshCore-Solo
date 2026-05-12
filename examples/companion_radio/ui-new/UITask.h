#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
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
  char _alert[80];
  unsigned long _alert_expiry;
  int _msgcount;
  int _room_unread;
  int _last_notif_ch_idx;
  unsigned long ui_started_at, next_batt_chck;
  uint16_t _batt_mv;  // EMA-filtered battery voltage
  int next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  int next_led_change = 0;
  int last_led_increment = 0;
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
  UIScreen* curr;

  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  void setCurrScreen(UIScreen* c);

public:

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : AbstractUITask(board, serial), _display(NULL), _sensors(NULL), _node_prefs(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    _batt_mv = 0;
    _msgcount = _room_unread = 0;
    _last_notif_ch_idx = -1;
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  NodePrefs* getNodePrefs() const { return _node_prefs; }
  uint16_t getBattMilliVolts() const { return _batt_mv > 0 ? _batt_mv : AbstractUITask::getBattMilliVolts(); }
  void gotoHomeScreen() { setCurrScreen(home); }
  void gotoSettingsScreen();
  void gotoQuickMsgScreen();
  void gotoToolsScreen();
  void gotoRingtoneEditor();
  void gotoBotScreen();
  void playMelody(const char* melody);
  void stopMelody();
  bool isMelodyPlaying();
  void showAlert(const char* text, int duration_millis);
  void addChannelMsg(uint8_t channel_idx, const char* text) override;
  int  getMsgCount() const { return _msgcount; }
  int  getChannelUnreadCount() const;
  int  getRoomUnreadCount() const { return _room_unread; }
  void clearRoomUnread() { _room_unread = 0; }
  bool hasDisplay() const { return _display != NULL; }
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
  void applyGPSInterval();
  uint32_t autoOffMillis() const {
    if (!_node_prefs || _node_prefs->auto_off_secs == 0) return 0;
    return (uint32_t)_node_prefs->auto_off_secs * 1000UL;
  }


  // from AbstractUITask
  void msgRead(int msgcount) override;
  void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t contact_type = 0) override;
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;

  void shutdown(bool restart = false);
};
