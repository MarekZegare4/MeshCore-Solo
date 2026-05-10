#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include <time.h>
#include "../MyMesh.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 22, _version_info);

    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 42, FIRMWARE_BUILD_DATE);

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

class SettingsScreen : public UIScreen {
  UITask* _task;

  enum SettingItem {
    BRIGHTNESS,
    BUZZER,
    TX_POWER,
    AUTO_OFF,
#if ENV_INCLUDE_GPS == 1
    GPS_INTERVAL,
#endif
    TIMEZONE,
    LOW_BAT,
    BATT_DISPLAY,
    Count
  };

  static const int VISIBLE_ITEMS = 4;
  static const int ITEM_H = 11;
  static const int START_Y = 12;
  static const int VAL_X = 80;

  int _selected;
  int _scroll;

  static const uint16_t AUTO_OFF_OPTS[5];
  static const char* AUTO_OFF_LABELS[5];
  static const int AUTO_OFF_COUNT = 5;
#if ENV_INCLUDE_GPS == 1
  static const uint32_t GPS_INTERVAL_OPTS[6];
  static const char* GPS_INTERVAL_LABELS[6];
  static const int GPS_INTERVAL_COUNT = 6;
#endif
  static const uint16_t LOW_BAT_OPTS[7];
  static const char* LOW_BAT_LABELS[7];
  static const int LOW_BAT_COUNT = 7;
  static const char* BATT_DISPLAY_LABELS[3];
  static const int BATT_DISPLAY_COUNT = 3;

  int lowBatIndex() {
    uint16_t v = _task->getNodePrefs()->low_batt_mv;
    for (int i = 0; i < LOW_BAT_COUNT; i++)
      if (LOW_BAT_OPTS[i] == v) return i;
    return 0; // default: off
  }

  void renderBar(DisplayDriver& display, int x, int y, int value, int max_val) {
    const int box_w = 7, box_h = 7, gap = 2;
    for (int i = 0; i < max_val; i++) {
      int bx = x + i * (box_w + gap);
      if (i < value) display.fillRect(bx, y, box_w, box_h);
      else           display.drawRect(bx, y, box_w, box_h);
    }
  }

  int autoOffIndex() {
    uint16_t v = _task->getNodePrefs()->auto_off_secs;
    for (int i = 0; i < AUTO_OFF_COUNT; i++)
      if (AUTO_OFF_OPTS[i] == v) return i;
    return 1; // default: 15s
  }

#if ENV_INCLUDE_GPS == 1
  int gpsIntervalIndex() {
    uint32_t v = _task->getNodePrefs()->gps_interval;
    for (int i = 0; i < GPS_INTERVAL_COUNT; i++)
      if (GPS_INTERVAL_OPTS[i] == v) return i;
    return 0;
  }
#endif

  void renderItem(DisplayDriver& display, int item, int y) {
    NodePrefs* p = _task->getNodePrefs();
    bool sel = (item == _selected);

    display.setColor(sel ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
    display.setCursor(0, y);
    display.print(sel ? ">" : " ");
    display.setCursor(8, y);

    if (item == BRIGHTNESS) {
      display.print("Bright");
      display.setColor(DisplayDriver::GREEN);
      renderBar(display, VAL_X, y, (p ? p->display_brightness : 2) + 1, 5);
    } else if (item == BUZZER) {
      display.print("Buzzer");
#ifdef PIN_BUZZER
      bool quiet = _task->isBuzzerQuiet();
      display.setColor(quiet ? DisplayDriver::RED : DisplayDriver::GREEN);
      display.setCursor(VAL_X, y);
      display.print(quiet ? "OFF" : "ON");
#else
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(VAL_X, y); display.print("N/A");
#endif
    } else if (item == TX_POWER) {
      display.print("TX Pwr");
      display.setColor(DisplayDriver::GREEN);
      char buf[8];
      sprintf(buf, "%ddBm", p ? p->tx_power_dbm : 0);
      display.setCursor(VAL_X, y);
      display.print(buf);
    } else if (item == AUTO_OFF) {
      display.print("AutoOff");
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(VAL_X, y);
      display.print(AUTO_OFF_LABELS[autoOffIndex()]);
#if ENV_INCLUDE_GPS == 1
    } else if (item == GPS_INTERVAL) {
      display.print("GPS upd");
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(VAL_X, y);
      display.print(GPS_INTERVAL_LABELS[gpsIntervalIndex()]);
#endif
    } else if (item == TIMEZONE) {
      display.print("TimeZone");
      display.setColor(DisplayDriver::GREEN);
      char buf[8];
      int8_t tz = p ? p->tz_offset_hours : 0;
      if (tz >= 0) sprintf(buf, "UTC+%d", (int)tz);
      else         sprintf(buf, "UTC%d",  (int)tz);
      display.setCursor(VAL_X, y);
      display.print(buf);
    } else if (item == LOW_BAT) {
      display.print("LowBat");
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(VAL_X, y);
      display.print(LOW_BAT_LABELS[lowBatIndex()]);
    } else if (item == BATT_DISPLAY) {
      display.print("BattDisp");
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(VAL_X, y);
      uint8_t mode = p ? p->batt_display_mode : 0;
      display.print(BATT_DISPLAY_LABELS[mode < BATT_DISPLAY_COUNT ? mode : 0]);
    }
  }

public:
  SettingsScreen(UITask* task) : _task(task), _selected(0), _scroll(0) { }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(display.width() / 2, 0, "SETTINGS");
    display.fillRect(0, 10, display.width(), 1);

    for (int i = 0; i < VISIBLE_ITEMS && (_scroll + i) < SettingItem::Count; i++) {
      renderItem(display, _scroll + i, START_Y + i * ITEM_H);
    }

    // scroll indicators
    if (_scroll > 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - 6, START_Y);
      display.print("^");
    }
    if (_scroll + VISIBLE_ITEMS < SettingItem::Count) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - 6, START_Y + (VISIBLE_ITEMS - 1) * ITEM_H);
      display.print("v");
    }

    return 300;
  }

  bool handleInput(char c) override {
    NodePrefs* p = _task->getNodePrefs();

    if (c == KEY_UP) {
      if (_selected > 0) {
        _selected--;
        if (_selected < _scroll) _scroll = _selected;
      }
      return true;
    }
    if (c == KEY_DOWN) {
      if (_selected < SettingItem::Count - 1) {
        _selected++;
        if (_selected >= _scroll + VISIBLE_ITEMS) _scroll = _selected - VISIBLE_ITEMS + 1;
      }
      return true;
    }
    if (c == KEY_CANCEL) {
      the_mesh.savePrefs();
      _task->gotoHomeScreen();
      return true;
    }

    bool right = (c == KEY_RIGHT || c == KEY_NEXT);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
    bool enter = (c == KEY_ENTER);

    if (_selected == BRIGHTNESS) {
      uint8_t lvl = _task->getBrightnessLevel();
      if (right && lvl < 4) _task->setBrightnessLevel(lvl + 1);
      if (left  && lvl > 0) _task->setBrightnessLevel(lvl - 1);
      return right || left;
    }
    if (_selected == BUZZER && (left || right || enter)) {
      _task->toggleBuzzer();
      return true;
    }
    if (_selected == TX_POWER && p) {
      if (right && p->tx_power_dbm < 22) { p->tx_power_dbm++; _task->applyTxPower(); return true; }
      if (left  && p->tx_power_dbm > 2)  { p->tx_power_dbm--; _task->applyTxPower(); return true; }
    }
    if (_selected == AUTO_OFF && p) {
      int idx = autoOffIndex();
      if (right) idx = (idx + 1) % AUTO_OFF_COUNT;
      if (left)  idx = (idx + AUTO_OFF_COUNT - 1) % AUTO_OFF_COUNT;
      if (left || right) { p->auto_off_secs = AUTO_OFF_OPTS[idx]; return true; }
    }
#if ENV_INCLUDE_GPS == 1
    if (_selected == GPS_INTERVAL && p) {
      int idx = gpsIntervalIndex();
      if (right) idx = (idx + 1) % GPS_INTERVAL_COUNT;
      if (left)  idx = (idx + GPS_INTERVAL_COUNT - 1) % GPS_INTERVAL_COUNT;
      if (left || right) {
        p->gps_interval = GPS_INTERVAL_OPTS[idx];
        _task->applyGPSInterval();
        return true;
      }
    }
#endif
    if (_selected == TIMEZONE && p) {
      if (right && p->tz_offset_hours < 14) { p->tz_offset_hours++; return true; }
      if (left  && p->tz_offset_hours > -12) { p->tz_offset_hours--; return true; }
    }
    if (_selected == LOW_BAT && p) {
      int idx = lowBatIndex();
      if (right) idx = (idx + 1) % LOW_BAT_COUNT;
      if (left)  idx = (idx + LOW_BAT_COUNT - 1) % LOW_BAT_COUNT;
      if (left || right) { p->low_batt_mv = LOW_BAT_OPTS[idx]; return true; }
    }
    if (_selected == BATT_DISPLAY && p) {
      int idx = p->batt_display_mode < BATT_DISPLAY_COUNT ? p->batt_display_mode : 0;
      if (right) idx = (idx + 1) % BATT_DISPLAY_COUNT;
      if (left)  idx = (idx + BATT_DISPLAY_COUNT - 1) % BATT_DISPLAY_COUNT;
      if (left || right) { p->batt_display_mode = idx; return true; }
    }
    return false;
  }
};

const uint16_t SettingsScreen::AUTO_OFF_OPTS[5]   = { 5, 15, 30, 60, 0 };
const char*    SettingsScreen::AUTO_OFF_LABELS[5]  = { "5s", "15s", "30s", "60s", "never" };
#if ENV_INCLUDE_GPS == 1
const uint32_t SettingsScreen::GPS_INTERVAL_OPTS[6]  = { 0, 30, 60, 300, 900, 1800 };
const char*    SettingsScreen::GPS_INTERVAL_LABELS[6] = { "off", "30s", "1min", "5min", "15min", "30min" };
#endif
const uint16_t SettingsScreen::LOW_BAT_OPTS[7]   = { 0, 3000, 3100, 3200, 3300, 3400, 3500 };
const char*    SettingsScreen::LOW_BAT_LABELS[7]  = { "off", "3.0V", "3.1V", "3.2V", "3.3V", "3.4V", "3.5V" };
const char*    SettingsScreen::BATT_DISPLAY_LABELS[3] = { "icon", "%", "V" };

static const char* const QUICK_MSG_LABELS[] = {
  "My location",
  "I'm OK",
  "Need help!",
  "On my way",
  "ETA 10min",
  "ETA 30min",
};
static const int QUICK_MSG_COUNT = 6;

class QuickMsgScreen : public UIScreen {
  UITask* _task;

  enum Phase { CONTACT_PICK, MSG_PICK };
  Phase _phase;
  int _contact_sel, _contact_scroll;
  int _msg_sel, _msg_scroll;
  int _num_contacts;
  uint8_t _sorted[MAX_CONTACTS];  // contact indices, favourites sorted first
  ContactInfo _sel_contact;

  static const int VISIBLE = 4;
  static const int ITEM_H  = 12;
  static const int START_Y = 12;

  void buildLocMsg(char* buf, int len) {
#if ENV_INCLUDE_GPS == 1
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc && loc->isValid()) {
      snprintf(buf, len, "Loc: %.5f,%.5f",
               loc->getLatitude() / 1000000.0,
               loc->getLongitude() / 1000000.0);
      return;
    }
#endif
    snprintf(buf, len, "Loc: no GPS fix");
  }

  void renderScrollHints(DisplayDriver& display, int scroll, int count) {
    display.setColor(DisplayDriver::LIGHT);
    if (scroll > 0) {
      display.setCursor(display.width() - 6, START_Y);
      display.print("^");
    }
    if (scroll + VISIBLE < count) {
      display.setCursor(display.width() - 6, START_Y + (VISIBLE-1)*ITEM_H);
      display.print("v");
    }
  }

public:
  QuickMsgScreen(UITask* task)
    : _task(task), _phase(CONTACT_PICK),
      _contact_sel(0), _contact_scroll(0),
      _msg_sel(0), _msg_scroll(0), _num_contacts(0) {}

  void reset() {
    _phase = CONTACT_PICK;
    _contact_sel = _contact_scroll = 0;
    _msg_sel = _msg_scroll = 0;
    _num_contacts = the_mesh.getNumContacts();
    // only show chat companions (not repeaters/rooms/sensors), favourites first
    ContactInfo c;
    int nfavs = 0;
    for (int i = 0; i < _num_contacts; i++)
      if (the_mesh.getContactByIdx(i, c) && c.type == ADV_TYPE_CHAT && (c.flags & 0x01)) nfavs++;
    int fpos = 0, npos = nfavs;
    _num_contacts = 0;
    int total = the_mesh.getNumContacts();
    for (int i = 0; i < total; i++) {
      if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_CHAT) continue;
      if (c.flags & 0x01) _sorted[fpos++] = i;
      else                _sorted[npos++] = i;
      _num_contacts++;
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);

    if (_phase == CONTACT_PICK) {
      display.drawTextCentered(display.width()/2, 0, "SELECT CONTACT");
      display.fillRect(0, 10, display.width(), 1);

      if (_num_contacts == 0) {
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width()/2, 32, "No contacts");
        return 5000;
      }

      for (int i = 0; i < VISIBLE && (_contact_scroll+i) < _num_contacts; i++) {
        int list_idx = _contact_scroll + i;
        int mesh_idx = _sorted[list_idx];
        bool sel = (list_idx == _contact_sel);
        int y = START_Y + i * ITEM_H;
        display.setColor(sel ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);

        ContactInfo c;
        if (the_mesh.getContactByIdx(mesh_idx, c)) {
          bool fav = (c.flags & 0x01);
          display.setCursor(0, y);
          display.print(sel ? ">" : (fav ? "*" : " "));
          char filtered[sizeof(c.name)];
          display.translateUTF8ToBlocks(filtered, c.name, sizeof(filtered));
          display.drawTextEllipsized(8, y, display.width() - 24, filtered);
          display.setColor(DisplayDriver::GREEN);
          char hop[5];
          snprintf(hop, sizeof(hop), c.out_path_len == 0xFF ? "D" : "%dh", (int)c.out_path_len);
          display.setCursor(display.width() - display.getTextWidth(hop) - 1, y);
          display.print(hop);
        }
      }
      renderScrollHints(display, _contact_scroll, _num_contacts);

    } else {
      char title[24];
      snprintf(title, sizeof(title), "TO:%.14s", _sel_contact.name);
      display.drawTextCentered(display.width()/2, 0, title);
      display.fillRect(0, 10, display.width(), 1);

      for (int i = 0; i < VISIBLE && (_msg_scroll+i) < QUICK_MSG_COUNT; i++) {
        int idx = _msg_scroll + i;
        bool sel = (idx == _msg_sel);
        int y = START_Y + i * ITEM_H;
        display.setColor(sel ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");

        if (idx == 0) {
          char loc[36];
          buildLocMsg(loc, sizeof(loc));
          display.drawTextEllipsized(8, y, display.width() - 10, loc);
        } else {
          display.setCursor(8, y);
          display.print(QUICK_MSG_LABELS[idx]);
        }
      }
      renderScrollHints(display, _msg_scroll, QUICK_MSG_COUNT);
    }
    return 300;
  }

  bool handleInput(char c) override {
    if (_phase == CONTACT_PICK) {
      if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
      if ((c == KEY_UP) && _contact_sel > 0) {
        _contact_sel--;
        if (_contact_sel < _contact_scroll) _contact_scroll = _contact_sel;
        return true;
      }
      if ((c == KEY_DOWN) && _contact_sel < _num_contacts - 1) {
        _contact_sel++;
        if (_contact_sel >= _contact_scroll + VISIBLE) _contact_scroll = _contact_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER && _num_contacts > 0) {
        if (the_mesh.getContactByIdx(_sorted[_contact_sel], _sel_contact)) {
          _phase = MSG_PICK;
          _msg_sel = _msg_scroll = 0;
        }
        return true;
      }
    } else {
      if (c == KEY_CANCEL) { _phase = CONTACT_PICK; return true; }
      if ((c == KEY_UP) && _msg_sel > 0) {
        _msg_sel--;
        if (_msg_sel < _msg_scroll) _msg_scroll = _msg_sel;
        return true;
      }
      if ((c == KEY_DOWN) && _msg_sel < QUICK_MSG_COUNT - 1) {
        _msg_sel++;
        if (_msg_sel >= _msg_scroll + VISIBLE) _msg_scroll = _msg_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER) {
        char msg[80];
        if (_msg_sel == 0) {
          buildLocMsg(msg, sizeof(msg));
        } else {
          strncpy(msg, QUICK_MSG_LABELS[_msg_sel], sizeof(msg)-1);
          msg[sizeof(msg)-1] = '\0';
        }
        uint32_t expected_ack = 0, est_timeout = 0;
        int result = the_mesh.sendMessage(_sel_contact, rtc_clock.getCurrentTime(), 0,
                                          msg, expected_ack, est_timeout);
        _task->showAlert(result > 0 ? "Sent!" : "Send failed", 1500);
        _task->gotoHomeScreen();
        return true;
      }
    }
    return false;
  }
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    CLOCK,
    RECENT,
    RADIO,
    BLUETOOTH,
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
    SETTINGS,
    QUICK_MSG,
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3200
#endif
#ifndef BATT_MAX_MILLIVOLTS
  #define BATT_MAX_MILLIVOLTS 4200
#endif
    // 0% anchor: low_batt_mv if set, otherwise BATT_MIN_MILLIVOLTS
    int minMv = (_node_prefs && _node_prefs->low_batt_mv > 0)
                  ? (int)_node_prefs->low_batt_mv : BATT_MIN_MILLIVOLTS;
    int pct = ((int)batteryMilliVolts - minMv) * 100 / (BATT_MAX_MILLIVOLTS - minMv);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    uint8_t mode = (_node_prefs && _node_prefs->batt_display_mode < 3)
                     ? _node_prefs->batt_display_mode : 0;

    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);

    int battLeftX;
    if (mode == 1) {  // percent
      char buf[6];
      sprintf(buf, "%d%%", pct);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, 0);
      display.print(buf);
    } else if (mode == 2) {  // voltage
      char buf[8];
      sprintf(buf, "%u.%02uV", batteryMilliVolts / 1000, (batteryMilliVolts % 1000) / 10);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, 0);
      display.print(buf);
    } else {  // icon
      const int iconWidth = 24, iconHeight = 10;
      battLeftX = display.width() - iconWidth - 5;
      display.drawRect(battLeftX, 0, iconWidth, iconHeight);
      display.fillRect(battLeftX + iconWidth, iconHeight / 4, 3, iconHeight / 2);
      int fillWidth = (pct * (iconWidth - 4)) / 100;
      display.fillRect(battLeftX + 2, 2, fillWidth, iconHeight - 4);
    }

#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::RED);
      display.drawXbm(battLeftX - 9, 1, muted_icon, 8, 8);
    }
#endif
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;
  
  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), 
       _shutdown_init(false), sensors_lpp(200) {  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    // node name
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, 0);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::CLOCK) {
      uint32_t unix_ts = _rtc->getCurrentTime();
      if (unix_ts < 1000000000UL) {
        display.setColor(DisplayDriver::YELLOW);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 25, "No time sync");
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, 40, "Enable GPS or");
        display.drawTextCentered(display.width() / 2, 51, "connect app");
      } else {
        int8_t tz = _node_prefs ? _node_prefs->tz_offset_hours : 0;
        unix_ts += (int32_t)tz * 3600;
        time_t t = (time_t)unix_ts;
        struct tm* ti = gmtime(&t);

        char buf[24];
        display.setColor(DisplayDriver::YELLOW);
        display.setTextSize(2);
        sprintf(buf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
        display.drawTextCentered(display.width() / 2, 18, buf);

        display.setTextSize(1);
        display.setColor(DisplayDriver::GREEN);
        const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        sprintf(buf, "%s %d %s %d", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon], 1900 + ti->tm_year);
        display.drawTextCentered(display.width() / 2, 40, buf);

        display.setColor(DisplayDriver::LIGHT);
        if (tz >= 0) sprintf(buf, "UTC+%d", (int)tz);
        else         sprintf(buf, "UTC%d",  (int)tz);
        display.drawTextCentered(display.width() / 2, 54, buf);
      }
    } else if (_page == HomePage::FIRST) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, 20, tmp);

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 54, tmp); 
      #endif
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 43, "< Connected >");

      } else if (the_mesh.getBLEPin() != 0) { // BT pin
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 43, tmp);
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::GREEN);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, "%dm", secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }
        
        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1;
        
        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
    } else if (_page == HomePage::RADIO) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;
      bool gps_state = _task->getGPSState();
#ifdef PIN_GPS_SWITCH
      bool hw_gps_state = digitalRead(PIN_GPS_SWITCH);
      if (gps_state != hw_gps_state) {
        strcpy(buf, gps_state ? "gps off(hw)" : "gps off(sw)");
      } else {
        strcpy(buf, gps_state ? "gps on" : "gps off");
      }
#else
      strcpy(buf, gps_state ? "gps on" : "gps off");
#endif
      display.drawTextLeftAlign(0, y, buf);
      if (nmea == NULL) {
        y = y + 12;
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "pos");
        sprintf(buf, "%.4f %.4f", 
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "alt");
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.print(name);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SETTINGS) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Settings");
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, 38, "brightness, buzzer");
      display.drawTextCentered(display.width() / 2, 52, PRESS_LABEL " to open");
    } else if (_page == HomePage::QUICK_MSG) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Quick Message");
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, 38, "send to contact");
      display.drawTextCentered(display.width() / 2, 52, PRESS_LABEL " to open");
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }
    return (_page == HomePage::CLOCK) ? 1000 : 5000;
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
      } else {
        _task->enableSerial();
      }
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::SETTINGS) {
      _task->gotoSettingsScreen();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::QUICK_MSG) {
      _task->gotoQuickMsgScreen();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  int head = MAX_UNREAD_MSGS - 1; // index of latest unread message
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    head = (head + 1) % MAX_UNREAD_MSGS;
    if (num_unread < MAX_UNREAD_MSGS) num_unread++;

    auto p = &unread[head];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[head];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(DisplayDriver::YELLOW);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      head = (head + MAX_UNREAD_MSGS - 1) % MAX_UNREAD_MSGS;
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  uint32_t aoff = autoOffMillis();
  _auto_off = millis() + (aoff > 0 ? aoff : AUTO_OFF_MILLIS);

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.quiet(_node_prefs->buzzer_quiet);
  buzzer.begin();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  ui_started_at = millis();
  _alert_expiry = 0;
  _batt_mv = AbstractUITask::getBattMilliVolts();  // seed EMA with first reading

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  settings = new SettingsScreen(this);
  quick_msg = new QuickMsgScreen(this);
  setCurrScreen(splash);

  applyBrightness();
}

void UITask::gotoQuickMsgScreen() {
  ((QuickMsgScreen*)quick_msg)->reset();
  setCurrScreen(quick_msg);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  setCurrScreen(msg_preview);

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    { uint32_t aoff = autoOffMillis(); if (aoff > 0) _auto_off = millis() + aoff; }  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

void UITask::setCurrScreen(UIScreen* c) {
  curr = c;
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){
  the_mesh.saveRTCTime();

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    _display->turnOff();
    radio_driver.powerOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
#if UI_HAS_JOYSTICK_UPDOWN
  ev = joystick_up.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_UP);
  }
  ev = joystick_down.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_DOWN);
  }
#endif
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_CANCEL);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_NEXT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    curr->handleInput(c);
    { uint32_t aoff = autoOffMillis(); if (aoff > 0) _auto_off = millis() + aoff; }  // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
    if (autoOffMillis() > 0 && millis() > _auto_off) {
      _display->turnOff();
#ifdef PIN_LED
      digitalWrite(PIN_LED, LOW);  // turn off status LED with display to save power
#endif
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

  if (millis() > next_batt_chck) {
    uint16_t raw = AbstractUITask::getBattMilliVolts();
    if (raw > 0) {
      // EMA filter: alpha=0.2 (80% old, 20% new) — smooths ADC noise from uneven load
      _batt_mv = (_batt_mv == 0) ? raw : (uint16_t)((_batt_mv * 4u + raw) / 5u);
    }
    uint16_t low_mv = _node_prefs ? _node_prefs->low_batt_mv : 0;
    if (low_mv > 0 && _batt_mv > 0 && _batt_mv < low_mv) {
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawTextCentered(_display->width() / 2, 16, "Low Battery");
        _display->drawTextCentered(_display->width() / 2, 36, "Shutting down");
        _display->endFrame();
        delay(2000);
      }
      shutdown();
    }
    next_batt_chck = millis() + 8000;
  }
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();
#ifdef PIN_LED
      digitalWrite(PIN_LED, LOW);  // ensure LED is off when waking display (userLedHandler takes over)
#endif
      c = 0;
    }
    { uint32_t aoff = autoOffMillis(); if (aoff > 0) _auto_off = millis() + aoff; }  // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

bool UITask::getGPSState() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        return !strcmp(_sensors->getSettingValue(i), "1");
      }
    }
  } 
  return false;
}

void UITask::toggleGPS() {
    if (_sensors != NULL) {
    // toggle GPS on/off
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
        if (strcmp(_sensors->getSettingValue(i), "1") == 0) {
          _sensors->setSettingValue("gps", "0");
          _node_prefs->gps_enabled = 0;
          notify(UIEventType::ack);
        } else {
          _sensors->setSettingValue("gps", "1");
          _node_prefs->gps_enabled = 1;
          notify(UIEventType::ack);
        }
        the_mesh.savePrefs();
        showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
        _next_refresh = 0;
        break;
      }
    }
  }
}

void UITask::applyTxPower() {
  if (_node_prefs == NULL) return;
  radio_set_tx_power(_node_prefs->tx_power_dbm);
}

void UITask::applyGPSInterval() {
  if (_node_prefs == NULL) return;
  char buf[12];
  sprintf(buf, "%u", _node_prefs->gps_interval);
  sensors.setSettingValue("gps_interval", buf);
}

void UITask::applyBrightness() {
  if (_display != NULL && _node_prefs != NULL) {
    _display->setBrightness(_node_prefs->display_brightness);
  }
}

void UITask::setBrightnessLevel(uint8_t level) {
  if (_node_prefs == NULL) return;
  if (level > 4) level = 4;
  _node_prefs->display_brightness = level;
  applyBrightness();
  _next_refresh = 0;
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}
