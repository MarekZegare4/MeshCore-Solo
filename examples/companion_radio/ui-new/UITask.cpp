#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "../MsgExpand.h"
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
  char _plus_ver[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    // extract plus version: v1.15-plus.1.4-SHA -> "1.4"
    _plus_ver[0] = '\0';
    const char *plus = strstr(ver, "plus.");
    if (plus) {
      plus += 5;  // skip "plus."
      const char *end = strchr(plus, '-');
      int plen = end ? end - plus : strlen(plus);
      if (plen >= (int)sizeof(_plus_ver)) plen = sizeof(_plus_ver) - 1;
      memcpy(_plus_ver, plus, plen);
      _plus_ver[plen] = '\0';
    }

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::LIGHT);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 22, _version_info);

    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 42, FIRMWARE_BUILD_DATE);

#ifdef FIRMWARE_PLUS_BUILD
    display.fillRect(0, 53, display.width(), 10);
    display.setColor(DisplayDriver::DARK);
    char plus_label[24];
    if (_plus_ver[0])
      snprintf(plus_label, sizeof(plus_label), "Plus %s for Wio", _plus_ver);
    else
      snprintf(plus_label, sizeof(plus_label), "Plus for Wio");
    display.drawTextCentered(display.width()/2, 54, plus_label);
    display.setColor(DisplayDriver::LIGHT);
#endif

    return 1000;
  }

  void poll() override {
    if (millis() >= dismiss_after) {
      _task->gotoHomeScreen();
    }
  }
};

static const int QUICK_MSGS_MAX = 10;

// Bit positions for NodePrefs::home_pages_mask.
// Bit=1 means page is shown. 0 in the field means ALL visible (default/unset).
static const uint16_t HP_CLOCK     = 1 << 0;
static const uint16_t HP_RECENT    = 1 << 1;
static const uint16_t HP_RADIO     = 1 << 2;
static const uint16_t HP_BLUETOOTH = 1 << 3;
static const uint16_t HP_ADVERT    = 1 << 4;
static const uint16_t HP_GPS       = 1 << 5;
static const uint16_t HP_SENSORS   = 1 << 6;
static const uint16_t HP_TOOLS     = 1 << 7;
static const uint16_t HP_SHUTDOWN  = 1 << 8;
static const uint16_t HP_ALL       = 0x01FF;

#include "KeyboardWidget.h"
#include "FullscreenMsgView.h"
#include "SensorPlaceholders.h"
// placeholder to find end of conflict
#include "SettingsScreen.h"
#include "QuickMsgScreen.h"

// ── Custom screens (separate files to ease upstream merges) ───────────────────
#include "RingtoneEditorScreen.h"
#include "BotScreen.h"
#include "NearbyScreen.h"
#include "DashboardConfigScreen.h"
#include "AutoAdvertScreen.h"
#include "ToolsScreen.h"

// ── HomeScreen ────────────────────────────────────────────────────────────────
class HomeScreen : public UIScreen {
  enum HomePage {
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
    TOOLS,
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

  int pageBit(int page) const {
    if (page == CLOCK)     return 0;
    if (page == RECENT)    return 1;
    if (page == RADIO)     return 2;
    if (page == BLUETOOTH) return 3;
    if (page == ADVERT)    return 4;
#if ENV_INCLUDE_GPS == 1
    if (page == GPS)       return 5;
#endif
#if UI_SENSORS_PAGE == 1
    if (page == SENSORS)   return 6;
#endif
    if (page == TOOLS)     return 7;
    if (page == SHUTDOWN)  return 8;
    return -1;  // SETTINGS, QUICK_MSG always visible
  }

  bool isPageVisible(int page) const {
    int bit = pageBit(page);
    if (bit < 0) return true;
    uint16_t mask = (_node_prefs && _node_prefs->home_pages_mask) ? _node_prefs->home_pages_mask : HP_ALL;
    return (mask >> bit) & 1;
  }

  int navPage(int from, int dir) const {
    for (int i = 1; i < (int)Count; i++) {
      int next = ((from + dir * i) % (int)Count + (int)Count) % (int)Count;
      if (isPageVisible(next)) return next;
    }
    return from;
  }

  int renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3200
#endif
    // LiPo discharge curve: voltage (mV) → raw capacity (%)
    static const struct { uint16_t mv; uint8_t pct; } CURVE[] = {
      {3200,  0}, {3300,  3}, {3400,  8}, {3500, 15},
      {3600, 25}, {3650, 33}, {3700, 45}, {3750, 58},
      {3800, 68}, {3900, 77}, {4000, 86}, {4100, 93}, {4200, 100}
    };
    static const int CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);

    auto curveAt = [&](int mv) -> int {
      if (mv <= (int)CURVE[0].mv) return CURVE[0].pct;
      if (mv >= (int)CURVE[CURVE_LEN-1].mv) return CURVE[CURVE_LEN-1].pct;
      for (int i = 1; i < CURVE_LEN; i++) {
        if (mv <= (int)CURVE[i].mv) {
          int span_mv  = CURVE[i].mv  - CURVE[i-1].mv;
          int span_pct = CURVE[i].pct - CURVE[i-1].pct;
          return CURVE[i-1].pct + (mv - (int)CURVE[i-1].mv) * span_pct / span_mv;
        }
      }
      return 100;
    };

    int low_mv = (_node_prefs && _node_prefs->low_batt_mv > 0)
                   ? (int)_node_prefs->low_batt_mv : BATT_MIN_MILLIVOLTS;
    int raw_pct = curveAt((int)batteryMilliVolts);
    int low_pct = curveAt(low_mv);
    // rescale so low_mv = 0% and 4200mV = 100%
    int pct = (low_pct >= 100) ? 0
              : (raw_pct - low_pct) * 100 / (100 - low_pct);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    uint8_t mode = (_node_prefs && _node_prefs->batt_display_mode < 3)
                     ? _node_prefs->batt_display_mode : 0;

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

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
      const int iconWidth = 24, iconHeight = 8;
      battLeftX = display.width() - iconWidth - 5;
      display.drawRect(battLeftX, 0, iconWidth, iconHeight);
      display.fillRect(battLeftX + iconWidth, iconHeight / 4, 3, iconHeight / 2);
      int fillWidth = (pct * (iconWidth - 4)) / 100;
      display.fillRect(battLeftX + 2, 2, fillWidth, iconHeight - 4);
    }

#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm(battLeftX - 9, 0, muted_icon, 8, 8);
    }
#endif

    // BT connection indicator (left of muted/battery icons)
    int leftmostX = battLeftX;
    if (_task->isSerialEnabled()) {
#ifdef PIN_BUZZER
      int btX = battLeftX - 18;
#else
      int btX = battLeftX - 9;
#endif
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(btX - 1, 0, 7, 7);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(btX, 0);
        display.print("B");
        display.setColor(DisplayDriver::LIGHT);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(btX, 0);
        display.print("b");
      }
      leftmostX = btX - 1;

      // "A" indicator — left of BT, blinks 50% duty at 1s period
      if (_node_prefs && _node_prefs->advert_auto_interval_sec > 0) {
        int aX = leftmostX - 8;
        if ((millis() % 4000) < 2000) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(aX - 1, 0, 7, 7);
          display.setColor(DisplayDriver::DARK);
          display.setCursor(aX, 0);
          display.print("A");
          display.setColor(DisplayDriver::LIGHT);
        }
        leftmostX = aX - 1;
      }
    }
    return leftmostX;
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
    // node name + battery — hidden on CLOCK page (full screen used for dashboard)
    if (_page != CLOCK) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      char filtered_name[sizeof(_node_prefs->node_name)];
      display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
      int rightEdge = renderBatteryIndicator(display, _task->getBattMilliVolts());
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextEllipsized(0, 0, rightEdge - 2, filtered_name);
    }

    // ensure current page is visible (e.g. after settings change)
    if (!isPageVisible(_page)) _page = navPage(_page, +1);

    // curr page indicator — hidden on CLOCK page (full screen used for dashboard)
    if (_page != CLOCK) {
      int vis_count = 0, curr_vis = 0;
      for (int i = 0; i < (int)Count; i++) {
        if (!isPageVisible(i)) continue;
        if (i == _page) curr_vis = vis_count;
        vis_count++;
      }
      int y = 14;
      int x = display.width() / 2 - 5 * (vis_count - 1);
      int vi = 0;
      for (int i = 0; i < (int)Count; i++) {
        if (!isPageVisible(i)) continue;
        if (vi == curr_vis) display.fillRect(x-1, y-1, 3, 3);
        else                 display.fillRect(x, y, 1, 1);
        x += 10; vi++;
      }
    }

    if (_page == HomePage::CLOCK) {
      uint32_t unix_ts = _rtc->getCurrentTime();
      if (unix_ts < 1000000000UL) {
        display.setColor(DisplayDriver::LIGHT);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 25, "! No time sync");
        display.drawTextCentered(display.width() / 2, 40, "Enable GPS or");
        display.drawTextCentered(display.width() / 2, 51, "connect app");
      } else {
        int8_t tz = _node_prefs ? _node_prefs->tz_offset_hours : 0;
        unix_ts += (int32_t)tz * 3600;
        time_t t = (time_t)unix_ts;
        struct tm* ti = gmtime(&t);

        char buf[24];
        display.setColor(DisplayDriver::LIGHT);
        display.setTextSize(2);
        bool show_sec = !_node_prefs || !_node_prefs->clock_hide_seconds;
        if (show_sec)
          sprintf(buf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
        else
          sprintf(buf, "%02d:%02d", ti->tm_hour, ti->tm_min);
        display.drawTextCentered(display.width() / 2, 0, buf);

        display.setTextSize(1);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        sprintf(buf, "%s %d %s %d", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon], 1900 + ti->tm_year);
        display.drawTextCentered(display.width() / 2, 19, buf);

        display.fillRect(0, 28, display.width(), 1);

        // dashboard data fields
        if (_node_prefs) {
          refresh_sensors();
          static const int FIELD_Y[] = { 31, 41, 51 };
          for (int fi = 0; fi < 3; fi++) {
            uint8_t field = _node_prefs->dashboard_fields[fi];
            if (field == DASH_NONE) continue;

            char label[10], val[20];
            label[0] = '\0';
            val[0] = '\0';

            if (field == DASH_BATT) {
              strcpy(label, "Batt");
              uint16_t mv = _task->getBattMilliVolts();
              if (mv > 0) snprintf(val, sizeof(val), "%u.%02uV", mv/1000, (mv%1000)/10);
              else strcpy(val, "--");
            } else if (field == DASH_GPS) {
              strcpy(label, "GPS");
#if ENV_INCLUDE_GPS == 1
              LocationProvider* loc = sensors.getLocationProvider();
              if (loc && loc->isValid())
                snprintf(val, sizeof(val), "%.3f %.3f",
                  loc->getLatitude()/1000000.0f, loc->getLongitude()/1000000.0f);
              else
                strcpy(val, "no fix");
#else
              strcpy(val, "--");
#endif
            } else if (field == DASH_NODES) {
              strcpy(label, "Nodes");
              snprintf(val, sizeof(val), "%d", the_mesh.getNumContacts());
            } else if (field == DASH_MSGS) {
              strcpy(label, "Msgs");
              int unread = _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount();
              snprintf(val, sizeof(val), "%d", unread);
            } else {
              uint8_t lpp_type = 0;
              switch (field) {
                case DASH_TEMP: strcpy(label, "Temp"); lpp_type = LPP_TEMPERATURE;        break;
                case DASH_HUM:  strcpy(label, "Hum");  lpp_type = LPP_RELATIVE_HUMIDITY;  break;
                case DASH_PRES: strcpy(label, "Pres"); lpp_type = LPP_BAROMETRIC_PRESSURE; break;
                case DASH_ALT:  strcpy(label, "Alt");  lpp_type = LPP_ALTITUDE;           break;
                case DASH_LUX:  strcpy(label, "Lux");  lpp_type = LPP_LUMINOSITY;         break;
                case DASH_CO2:  strcpy(label, "CO2");  lpp_type = LPP_CONCENTRATION;      break;
              }
              if (lpp_type) {
                LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());
                uint8_t ch, type;
                while (r.readHeader(ch, type)) {
                  if (type == lpp_type) {
                    float v;
                    switch (lpp_type) {
                      case LPP_TEMPERATURE:         r.readTemperature(v);      snprintf(val, sizeof(val), "%.1f\xf8""C", v); break;
                      case LPP_RELATIVE_HUMIDITY:   r.readRelativeHumidity(v); snprintf(val, sizeof(val), "%.0f%%", v);      break;
                      case LPP_BAROMETRIC_PRESSURE: r.readPressure(v);         snprintf(val, sizeof(val), "%.0fhPa", v);     break;
                      case LPP_ALTITUDE:            r.readAltitude(v);         snprintf(val, sizeof(val), "%.0fm", v);       break;
                      case LPP_LUMINOSITY:          r.readLuminosity(v);       snprintf(val, sizeof(val), "%.0flux", v);     break;
                      case LPP_CONCENTRATION:       r.readConcentration(v);    snprintf(val, sizeof(val), "%.0fppm", v);     break;
                    }
                    break;
                  }
                  r.skipData(type);
                }
              }
              if (!val[0]) strcpy(val, "--");
            }

            if (val[0] && label[0]) {
              display.setColor(DisplayDriver::LIGHT);
              display.setCursor(0, FIELD_Y[fi]);
              display.print(label);
              int vw = display.getTextWidth(val);
              display.setCursor(display.width() - vw - 1, FIELD_Y[fi]);
              display.print(val);
            }
          }
        }
      }
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::LIGHT);
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
      display.setColor(DisplayDriver::LIGHT);
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
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm((display.width() - 32) / 2, 14,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setTextSize(1);
      if (_task->isSerialEnabled() && !_task->hasConnection() && the_mesh.getBLEPin() != 0) {
        char pin_buf[16];
        snprintf(pin_buf, sizeof(pin_buf), "PIN: %d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 49, pin_buf);
      }
      display.drawTextCentered(display.width() / 2, 57, "toggle: " PRESS_LABEL);
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::LIGHT);
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

      uint8_t avail_types[16];
      int avail_count = _sensors ? _sensors->getAvailableLPPTypes(avail_types, 16) : 0;
      bool need_scroll = avail_count > UI_RECENT_LIST_SIZE;
      int offset = need_scroll ? (sensors_scroll_offset % avail_count) : 0;
      int show_n = need_scroll ? UI_RECENT_LIST_SIZE : avail_count;

      for (int i = 0; i < show_n; i++) {
        uint8_t target = avail_types[(offset + i) % avail_count];

        // scan LPP buffer for this type
        LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());
        uint8_t ch, type;
        bool found = false;
        char buf[22] = "--";
        while (r.readHeader(ch, type)) {
          if (type == target) {
            float v, v2, v3;
            switch (type) {
              case LPP_GPS:
                r.readGPS(v, v2, v3);
                if (v != 0 || v2 != 0) snprintf(buf, sizeof(buf), "%.4f %.4f", v, v2);
                break;
              case LPP_VOLTAGE:    r.readVoltage(v);          snprintf(buf, sizeof(buf), "%.2fV", v); break;
              case LPP_CURRENT:    r.readCurrent(v);          snprintf(buf, sizeof(buf), "%.3fA", v); break;
              case LPP_POWER:      r.readPower(v);            snprintf(buf, sizeof(buf), "%.1fW", v); break;
              case LPP_TEMPERATURE:r.readTemperature(v);      snprintf(buf, sizeof(buf), "%.1f\xf8""C", v); break;
              case LPP_RELATIVE_HUMIDITY: r.readRelativeHumidity(v); snprintf(buf, sizeof(buf), "%.0f%%", v); break;
              case LPP_BAROMETRIC_PRESSURE: r.readPressure(v); snprintf(buf, sizeof(buf), "%.1fhPa", v); break;
              case LPP_ALTITUDE:   r.readAltitude(v);         snprintf(buf, sizeof(buf), "%.0fm", v); break;
              case LPP_LUMINOSITY: r.readLuminosity(v);       snprintf(buf, sizeof(buf), "%.0flux", v); break;
              case LPP_PERCENTAGE: r.readPercentage(v);       snprintf(buf, sizeof(buf), "%.0f%%", v); break;
              case LPP_DISTANCE:   r.readDistance(v);         snprintf(buf, sizeof(buf), "%.2fm", v); break;
              case LPP_CONCENTRATION: r.readConcentration(v); snprintf(buf, sizeof(buf), "%.0fppm", v); break;
              default:             r.skipData(type); continue;
            }
            found = true;
            break;
          }
          r.skipData(type);
        }
        (void)found;

        static const struct { uint8_t type; const char* name; } TYPE_NAMES[] = {
          { LPP_VOLTAGE,            "voltage"  },
          { LPP_GPS,                "gps"      },
          { LPP_TEMPERATURE,        "temp"     },
          { LPP_RELATIVE_HUMIDITY,  "humidity" },
          { LPP_BAROMETRIC_PRESSURE,"pressure" },
          { LPP_ALTITUDE,           "altitude" },
          { LPP_CURRENT,            "current"  },
          { LPP_POWER,              "power"    },
          { LPP_LUMINOSITY,         "light"    },
          { LPP_PERCENTAGE,         "moisture" },
          { LPP_DISTANCE,           "distance" },
          { LPP_CONCENTRATION,      "CO2"      },
        };
        const char* name = "sensor";
        for (auto& tn : TYPE_NAMES) { if (tn.type == target) { name = tn.name; break; } }

        display.setCursor(0, y);
        display.print(name);
        display.setCursor(display.width() - display.getTextWidth(buf) - 1, y);
        display.print(buf);
        y += 12;
      }
      if (need_scroll) sensors_scroll_offset = (sensors_scroll_offset + 1) % avail_count;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SETTINGS) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Settings");
      display.drawTextCentered(display.width() / 2, 50, PRESS_LABEL " to open");
    } else if (_page == HomePage::TOOLS) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Tools");
      display.drawTextCentered(display.width() / 2, 50, PRESS_LABEL " to open");
    } else if (_page == HomePage::QUICK_MSG) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Messages");
      int total_unread = _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount();
      if (total_unread > 0) {
        char badge[20];
        snprintf(badge, sizeof(badge), "%d unread", total_unread);
        display.drawTextCentered(display.width() / 2, 35, badge);
      }
      display.drawTextCentered(display.width() / 2, 50, PRESS_LABEL " to open");
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }
    bool auto_adv = _node_prefs && _node_prefs->advert_auto_interval_sec > 0;
    if (_page == HomePage::CLOCK) {
      bool show_sec = !_node_prefs || !_node_prefs->clock_hide_seconds;
      return auto_adv ? 1000 : (show_sec ? 1000 : 60000);
    }
    return auto_adv ? 1000 : 5000;
  }

  bool handleInput(char c) override {
    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = navPage(_page, -1);
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      _page = navPage(_page, +1);
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
    if (c == KEY_ENTER && _page == HomePage::TOOLS) {
      _task->gotoToolsScreen();
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
    if (c == KEY_CONTEXT_MENU && _page == HomePage::CLOCK) {
      _task->gotoDashboardConfig();
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
  buzzer.setVolume(_node_prefs->buzzer_volume);
  buzzer.begin();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  // Set default quick message if slot 0 is empty (first boot)
  if (_node_prefs && _node_prefs->custom_msgs[0][0] == '\0') {
    strncpy(_node_prefs->custom_msgs[0], "OK", sizeof(_node_prefs->custom_msgs[0]) - 1);
  }

  ui_started_at = millis();
  _alert_expiry = 0;
  _batt_mv = AbstractUITask::getBattMilliVolts();  // seed EMA with first reading

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  settings = new SettingsScreen(this);
  quick_msg = new QuickMsgScreen(this);
  tools_screen  = new ToolsScreen(this);
  ringtone_edit = new RingtoneEditorScreen(this, node_prefs);
  bot_screen    = new BotScreen(this, node_prefs);
  nearby_screen = new NearbyScreen(this);
  dashboard_config = new DashboardConfigScreen(this, node_prefs);
  auto_advert_screen = new AutoAdvertScreen(this, node_prefs);
  setCurrScreen(splash);

  applyBrightness();
}

void UITask::gotoSettingsScreen() {
  ((SettingsScreen*)settings)->markClean();
  setCurrScreen(settings);
}

void UITask::gotoToolsScreen() {
  setCurrScreen(tools_screen);
}

void UITask::gotoRingtoneEditor(int slot) {
  ((RingtoneEditorScreen*)ringtone_edit)->enter(slot);
  setCurrScreen(ringtone_edit);
}

void UITask::gotoBotScreen() {
  ((BotScreen*)bot_screen)->enter();
  setCurrScreen(bot_screen);
}

void UITask::gotoNearbyScreen() {
  ((NearbyScreen*)nearby_screen)->enter();
  setCurrScreen(nearby_screen);
}

void UITask::gotoDashboardConfig() {
  ((DashboardConfigScreen*)dashboard_config)->enter();
  setCurrScreen(dashboard_config);
}

void UITask::gotoAutoAdvertScreen() {
  ((AutoAdvertScreen*)auto_advert_screen)->enter();
  setCurrScreen(auto_advert_screen);
}

void UITask::playMelody(const char* melody) {
#ifdef PIN_BUZZER
  buzzer.playForced(melody);
#endif
}

void UITask::stopMelody() {
#ifdef PIN_BUZZER
  buzzer.stop();
#endif
}

bool UITask::isMelodyPlaying() {
#ifdef PIN_BUZZER
  return buzzer.isPlaying();
#else
  return false;
#endif
}

void UITask::gotoQuickMsgScreen() {
  ((QuickMsgScreen*)quick_msg)->reset();
  setCurrScreen(quick_msg);
}

void UITask::addChannelMsg(uint8_t channel_idx, const char* text) {
  _last_notif_ch_idx = (int)channel_idx;
  ((QuickMsgScreen*)quick_msg)->addChannelMsg(channel_idx, text);
}

int UITask::getChannelUnreadCount() const {
  return ((QuickMsgScreen*)quick_msg)->getTotalChannelUnread();
}

void UITask::addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text) {
  ((QuickMsgScreen*)quick_msg)->addDMMsg(pub_key, outgoing, text);
}

int UITask::getDMUnreadTotal() const {
  int total = 0;
  for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++)
    total += _dm_unread_table[i].count;
  return total;
}

void UITask::showAlert(const char* text, int duration_millis) {
  snprintf(_alert, sizeof(_alert), "%s", text);
  _alert_expiry = millis() + duration_millis;
}

static void buildMelodyFromPrefs(const NodePrefs* p, int slot, char* buf, int size) {
  static const uint16_t BPM_OPTS[]  = { 60, 90, 120, 150, 180 };
  static const uint8_t  DUR_VALS[]  = { 4, 8, 16, 32 };
  static const char     PITCHES[]   = { 'p', 'c', 'd', 'e', 'f', 'g', 'a', 'b' };
  const uint8_t* notes  = (slot == 2) ? p->ringtone2_notes  : p->ringtone_notes;
  uint8_t        len    = (slot == 2) ? p->ringtone2_len     : p->ringtone_len;
  uint8_t        bpm_i  = (slot == 2) ? p->ringtone2_bpm_idx : p->ringtone_bpm_idx;
  if (len == 0) { buf[0] = '\0'; return; }
  uint16_t bpm = BPM_OPTS[bpm_i < 5 ? bpm_i : 2];
  int pos = snprintf(buf, size, "Ring:d=8,o=5,b=%u:", bpm);
  for (int i = 0; i < len && pos < size - 8; i++) {
    if (i > 0 && pos < size - 1) buf[pos++] = ',';
    uint8_t pitch   = notes[i] & 0x07;
    uint8_t octave  = ((notes[i] >> 3) & 0x03) + 4;
    uint8_t dur_val = DUR_VALS[(notes[i] >> 5) & 0x03];
    if (pitch == 0) pos += snprintf(buf + pos, size - pos, "%dp", dur_val);
    else            pos += snprintf(buf + pos, size - pos, "%d%c%d", dur_val, PITCHES[pitch], octave);
  }
  if (pos < size) buf[pos] = '\0';
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage: {
    bool play = false;
    bool force = false;
    if (_last_notif_dm_valid && _node_prefs) {
      uint8_t state = 0;
      for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++) {
        if (_node_prefs->dm_notif[i].state &&
            memcmp(_node_prefs->dm_notif[i].prefix, _last_notif_dm_prefix, 4) == 0) {
          state = _node_prefs->dm_notif[i].state; break;
        }
      }
      if (state == 2) { play = true; force = true; }   // force-on
      else if (state == 1) { /* muted */ }
      else { play = !buzzer.isQuiet(); }               // default: follow global
    } else {
      play = !buzzer.isQuiet();
    }
    _last_notif_dm_valid = false;
    if (play) {
      int slot = _node_prefs ? (int)_node_prefs->notif_melody_dm : 0;
      if (_node_prefs) {
        for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++)
          if (_node_prefs->dm_melody[i].slot &&
              memcmp(_node_prefs->dm_melody[i].prefix, _last_notif_dm_prefix, 4) == 0)
            { slot = _node_prefs->dm_melody[i].slot; break; }
      }
      bool custom_played = false;
      if (slot > 0 && _node_prefs) {
        buildMelodyFromPrefs(_node_prefs, slot, _notif_mel_buf, sizeof(_notif_mel_buf));
        if (_notif_mel_buf[0]) {
          if (force) buzzer.playForced(_notif_mel_buf); else buzzer.play(_notif_mel_buf);
          custom_played = true;
        }
      }
      if (!custom_played) {
        if (force) buzzer.playForced("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
        else       buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
      }
    }
    break;
  }
  case UIEventType::channelMessage: {
    bool play = false;
    bool force = false;
    if (_last_notif_ch_idx >= 0 && _last_notif_ch_idx < 64 && _node_prefs) {
      uint64_t mask = 1ULL << _last_notif_ch_idx;
      if (_node_prefs->ch_notif_override & mask) {
        if (!(_node_prefs->ch_notif_muted & mask)) { play = true; force = true; }
      } else {
        play = !buzzer.isQuiet();
      }
    } else {
      play = !buzzer.isQuiet();
    }
    if (play) {
      int slot = _node_prefs ? (int)_node_prefs->notif_melody_ch : 0;
      if (_last_notif_ch_idx >= 0 && _last_notif_ch_idx < 64 && _node_prefs) {
        uint64_t mask = 1ULL << _last_notif_ch_idx;
        if (_node_prefs->ch_notif_melody_set & mask)
          slot = (_node_prefs->ch_notif_melody_2 & mask) ? 2 : 1;
      }
      bool custom_played = false;
      if (slot > 0 && _node_prefs) {
        buildMelodyFromPrefs(_node_prefs, slot, _notif_mel_buf, sizeof(_notif_mel_buf));
        if (_notif_mel_buf[0]) {
          if (force) buzzer.playForced(_notif_mel_buf); else buzzer.play(_notif_mel_buf);
          custom_played = true;
        }
      }
      if (!custom_played) {
        if (force) buzzer.playForced("kerplop:d=16,o=6,b=120:32g#,32c#");
        else       buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
      }
    }
    _last_notif_ch_idx = -1;
    break;
  }
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
    _room_unread = 0;
    memset(_dm_unread_table, 0, sizeof(_dm_unread_table));
    ((QuickMsgScreen*)quick_msg)->clearAllChannelUnread();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t contact_type, const uint8_t* pub_key) {
  _msgcount = msgcount;
  if (contact_type == ADV_TYPE_ROOM && _room_unread < _msgcount) _room_unread++;
  if (contact_type == ADV_TYPE_CHAT && pub_key != nullptr) {
    memcpy(_last_notif_dm_prefix, pub_key, 4);
    _last_notif_dm_valid = true;
    int slot = -1, empty_slot = -1;
    for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++) {
      if (_dm_unread_table[i].count > 0 && memcmp(_dm_unread_table[i].prefix, pub_key, 4) == 0) { slot = i; break; }
      if (empty_slot < 0 && _dm_unread_table[i].count == 0) empty_slot = i;
    }
    if (slot >= 0) {
      if (_dm_unread_table[slot].count < 99) _dm_unread_table[slot].count++;
    } else if (empty_slot >= 0) {
      memcpy(_dm_unread_table[empty_slot].prefix, pub_key, 4);
      _dm_unread_table[empty_slot].count = 1;
    }
  }

  char alert_buf[80];
  snprintf(alert_buf, sizeof(alert_buf), "Msg: %.20s", from_name);
  showAlert(alert_buf, 3000);

  if (_display != NULL && !_locked) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
      _next_refresh = 100;
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
  while (buzzer.isPlaying() && (millis() - buzzer_timer) < 2500)
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

static void formatDashVal(uint8_t field, char* val, int val_len, uint16_t batt_mv) {
  val[0] = '\0';
  switch (field) {
    case DASH_NONE: return;
    case DASH_BATT:
      if (batt_mv > 0) snprintf(val, val_len, "%u.%02uV", batt_mv/1000, (batt_mv%1000)/10);
      else              strcpy(val, "--");
      return;
    case DASH_NODES:
      snprintf(val, val_len, "%d nodes", the_mesh.getNumContacts());
      return;
#if ENV_INCLUDE_GPS == 1
    case DASH_GPS: {
      LocationProvider* loc = sensors.getLocationProvider();
      if (loc && loc->isValid())
        snprintf(val, val_len, "%.2f %.2f",
                 loc->getLatitude()/1000000.0f, loc->getLongitude()/1000000.0f);
      else strcpy(val, "no fix");
      return;
    }
#endif
    default: break;
  }
  // LPP sensor fields: query sensors into a local buffer
  uint8_t lpp_type = 0;
  switch (field) {
    case DASH_TEMP: lpp_type = LPP_TEMPERATURE;         break;
    case DASH_HUM:  lpp_type = LPP_RELATIVE_HUMIDITY;   break;
    case DASH_PRES: lpp_type = LPP_BAROMETRIC_PRESSURE; break;
    case DASH_ALT:  lpp_type = LPP_ALTITUDE;            break;
    case DASH_LUX:  lpp_type = LPP_LUMINOSITY;          break;
    case DASH_CO2:  lpp_type = LPP_CONCENTRATION;       break;
  }
  if (lpp_type) {
    CayenneLPP lpp(200);
    lpp.reset();
    sensors.querySensors(0xFF, lpp);
    LPPReader r(lpp.getBuffer(), lpp.getSize());
    uint8_t ch, type;
    while (r.readHeader(ch, type)) {
      if (type == lpp_type) {
        float v;
        switch (lpp_type) {
          case LPP_TEMPERATURE:         r.readTemperature(v);      snprintf(val, val_len, "%.1f\xf8""C", v); return;
          case LPP_RELATIVE_HUMIDITY:   r.readRelativeHumidity(v); snprintf(val, val_len, "%.0f%%", v);      return;
          case LPP_BAROMETRIC_PRESSURE: r.readPressure(v);         snprintf(val, val_len, "%.0fhPa", v);     return;
          case LPP_ALTITUDE:            r.readAltitude(v);         snprintf(val, val_len, "%.0fm", v);       return;
          case LPP_LUMINOSITY:          r.readLuminosity(v);       snprintf(val, val_len, "%.0flux", v);     return;
          case LPP_CONCENTRATION:       r.readConcentration(v);    snprintf(val, val_len, "%.0fppm", v);     return;
        }
      }
      r.skipData(type);
    }
    strcpy(val, "--");
  }
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (back_btn.isPressed()) {
      // Enter clicked while Back is held — lock/unlock sequence
      if (_display && !_display->isOn()) {
        _display->turnOn();  // turn on display so hints are visible
      }
      _lock_wake_until = millis() + 5000;  // keep display on during sequence
      if (millis() - _lock_seq_ms > 3000) _lock_seq_count = 0;  // timeout reset
      _lock_seq_count++;
      _lock_seq_ms = millis();
      _next_refresh = 0;  // update hint immediately on each press
      if (_lock_seq_count >= 3) {
        _lock_seq_count = 0;
        _lock_seq_used = true;  // suppress Back release click
        _locked = !_locked;
        if (_locked) {
          _lock_wake_until = millis() + 2000;
        } else {
          if (_display && !_display->isOn()) _display->turnOn();
          uint32_t aoff = autoOffMillis();
          if (aoff > 0) _auto_off = millis() + aoff;
        }
      }
      // eat the Enter — don't pass to curr
    } else {
      c = checkDisplayOn(KEY_ENTER);
    }
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
  if (_lock_seq_used && millis() - _lock_seq_ms > 5000) {
    _lock_seq_used = false;  // safety reset if Back release event was missed
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (_lock_seq_count > 0 || _lock_seq_used) {
      // Back released mid-sequence or after completing it — cancel/suppress
      _lock_seq_count = 0;
      _lock_seq_used = false;
    } else {
      c = checkDisplayOn(KEY_CANCEL);
    }
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    if (!_locked) c = handleTripleClick(KEY_SELECT);
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

  if (c != 0) {
    if (!_locked && curr) {
      curr->handleInput(c);
      { uint32_t aoff = autoOffMillis(); if (aoff > 0) _auto_off = millis() + aoff; }  // extend auto-off timer
      _next_refresh = 100;  // trigger refresh
    } else if (_locked) {
      // Locked: eat all keys — wake window is set only when display first turns on
      _next_refresh = 0;
    }
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (_node_prefs && _node_prefs->buzzer_auto) {
    bool should_quiet = hasConnection();
    if (buzzer.isQuiet() != should_quiet) {
      buzzer.quiet(should_quiet);
      _next_refresh = 0;
    }
  }
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (_locked && millis() > _lock_wake_until) {
      _display->turnOff();
    } else if (_locked && millis() >= _next_refresh) {
      _display->startFrame();
      // Lock screen: clock + unlock hint popup
      uint32_t unix_ts = rtc_clock.getCurrentTime();
      _display->setColor(DisplayDriver::LIGHT);
      if (unix_ts < 1000000000UL) {
        _display->setTextSize(1);
        _display->drawTextCentered(_display->width() / 2, 20, "No time sync");
      } else {
        int8_t tz = _node_prefs ? _node_prefs->tz_offset_hours : 0;
        unix_ts += (int32_t)tz * 3600;
        time_t t = (time_t)unix_ts;
        struct tm* ti = gmtime(&t);
        char buf[12];
        _display->setTextSize(2);
        sprintf(buf, "%02d:%02d", ti->tm_hour, ti->tm_min);
        _display->drawTextCentered(_display->width() / 2, 8, buf);
        _display->setTextSize(1);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        sprintf(buf, "%s %d %s", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon]);
        _display->drawTextCentered(_display->width() / 2, 26, buf);

        // Two sensor values side by side (dashboard_fields[0] and [1])
        if (_node_prefs) {
          char v0[20] = "", v1[20] = "";
          formatDashVal(_node_prefs->dashboard_fields[0], v0, sizeof(v0), _batt_mv);
          formatDashVal(_node_prefs->dashboard_fields[1], v1, sizeof(v1), _batt_mv);
          if (v0[0] || v1[0]) {
            _display->setTextSize(1);
            _display->setColor(DisplayDriver::LIGHT);
            if (v0[0] && v1[0]) {
              _display->setCursor(0, 37);
              _display->print(v0);
              int vw = _display->getTextWidth(v1);
              _display->setCursor(_display->width() - vw, 37);
              _display->print(v1);
            } else {
              const char* sv = v0[0] ? v0 : v1;
              _display->drawTextCentered(_display->width() / 2, 37, sv);
            }
          }
        }
      }
      // Hint popup at bottom (like alert style)
      _display->setTextSize(1);
      const char* hint = _lock_seq_count == 0 ? "Hold Back + 3xEnter" :
                         _lock_seq_count == 1 ? "Enter x2 more..."   : "Enter x1 more...";
      int p = 3;
      int hy = _display->height() - 13;
      int hw = _display->getTextWidth(hint);
      int hx = (_display->width() - hw) / 2;
      _display->setColor(DisplayDriver::LIGHT);
      _display->fillRect(hx - p, hy - p, hw + p*2, 8 + p*2);
      _display->setColor(DisplayDriver::DARK);
      _display->setCursor(hx, hy);
      _display->print(hint);
      _display->endFrame();
      _next_refresh = millis() + 1000;
    } else if (!_locked && millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry && curr == home) {  // render alert only on home screen
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
    if (!_locked && autoOffMillis() > 0 && millis() > _auto_off) {
      _display->turnOff();
#ifdef PIN_LED
      digitalWrite(PIN_LED, LOW);  // turn off status LED with display to save power
#endif
      if (_node_prefs && _node_prefs->auto_lock) {
        _locked = true;
        _lock_wake_until = 0;
      }
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
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawTextCentered(_display->width() / 2, 24, "Low Battery");
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
      if (_locked) {
        _lock_wake_until = millis() + 5000;
        _next_refresh = 0;
        return 0;  // eat the waking key press
      }
      c = 0;
    }
    if (!_locked) {
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;  // extend auto-off timer
    }
    _next_refresh = 0;  // trigger refresh
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    return 0;
  }
  if (c == KEY_ENTER) return KEY_CONTEXT_MENU;
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

void UITask::setBuzzerVolumeLevel(uint8_t level) {
#ifdef PIN_BUZZER
  if (_node_prefs == NULL) return;
  if (level > 4) level = 4;
  _node_prefs->buzzer_volume = level;
  buzzer.setVolume(level);
  if (level > 0) buzzer.playForced("Vol:d=16,o=5,b=120:c");
  _next_refresh = 0;
#endif
}

void UITask::toggleBuzzer() {
  #ifdef PIN_BUZZER
    if (_node_prefs) _node_prefs->buzzer_auto = 0;  // exit auto mode
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    if (_node_prefs) _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;
  #endif
}

int UITask::getBuzzerMode() {
#ifdef PIN_BUZZER
  if (_node_prefs && _node_prefs->buzzer_auto) return 2;
  return buzzer.isQuiet() ? 1 : 0;
#else
  return 1;
#endif
}

void UITask::cycleBuzzerMode() {
#ifdef PIN_BUZZER
  if (!_node_prefs) return;
  int mode = getBuzzerMode();
  mode = (mode + 1) % 3;  // ON(0) → OFF(1) → Auto(2) → ON
  _node_prefs->buzzer_auto = (mode == 2) ? 1 : 0;
  if (mode == 0) { buzzer.quiet(false); _node_prefs->buzzer_quiet = 0; notify(UIEventType::ack); }
  if (mode == 1) { buzzer.quiet(true);  _node_prefs->buzzer_quiet = 1; }
  if (mode == 2) { buzzer.quiet(hasConnection()); }
  static const char* labels[] = { "Buzzer: ON", "Buzzer: OFF", "Buzzer: Auto" };
  showAlert(labels[mode], 800);
  _next_refresh = 0;
#endif
}
