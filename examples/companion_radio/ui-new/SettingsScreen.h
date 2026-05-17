#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SensorPlaceholders.h is defined.

class SettingsScreen : public UIScreen {
  UITask* _task;

  enum SettingItem {
    // Display section
    SECTION_DISPLAY,
    BRIGHTNESS,
    AUTO_OFF,
    AUTO_LOCK,
    BATT_DISPLAY,
    CLOCK_SECONDS,
    // Sound section
    SECTION_SOUND,
    BUZZER,
    BUZZER_VOLUME,
    DM_MELODY,
    CH_MELODY,
    // Home pages section
    SECTION_HOME_PAGES,
    HOME_CLOCK, HOME_RECENT, HOME_RADIO, HOME_BT, HOME_ADVERT,
#if ENV_INCLUDE_GPS == 1
    HOME_GPS,
#endif
#if UI_SENSORS_PAGE == 1
    HOME_SENSORS,
#endif
    HOME_TOOLS, HOME_SHUTDOWN,
    // Radio section
    SECTION_RADIO,
    TX_POWER,
    // System section
    SECTION_SYSTEM,
    TIMEZONE,
    LOW_BAT,
#if ENV_INCLUDE_GPS == 1
    // GPS section
    SECTION_GPS,
    GPS_INTERVAL,
#endif
    // Contacts section
    SECTION_CONTACTS, DM_FILTER, ROOM_FILTER,
    // Messages section
    SECTION_MESSAGES,
    MSG_SLOT_0, MSG_SLOT_1, MSG_SLOT_2, MSG_SLOT_3, MSG_SLOT_4,
    MSG_SLOT_5, MSG_SLOT_6, MSG_SLOT_7, MSG_SLOT_8, MSG_SLOT_9,
    Count
  };

  static const int VISIBLE_ITEMS = 4;
  static const int ITEM_H = 11;
  static const int START_Y = 12;
  static const int VAL_X = 80;

  int _selected;
  int _scroll;
  bool _dirty;

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
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    for (int i = 0; i < LOW_BAT_COUNT; i++)
      if (LOW_BAT_OPTS[i] == p->low_batt_mv) return i;
    return 0;
  }

  void renderBar(DisplayDriver& display, int x, int y, int value, int max_val) {
    const int box_w = 7, box_h = 7, gap = 2;
    for (int i = 0; i < max_val; i++) {
      int bx = x + i * (box_w + gap);
      display.drawRect(bx, y, box_w, box_h);
      if (i < value)
        display.fillRect(bx + 1, y + 1, box_w - 2, box_h - 2);
    }
  }

  int autoOffIndex() {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 1;
    for (int i = 0; i < AUTO_OFF_COUNT; i++)
      if (AUTO_OFF_OPTS[i] == p->auto_off_secs) return i;
    return 1;
  }

#if ENV_INCLUDE_GPS == 1
  int gpsIntervalIndex() {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    for (int i = 0; i < GPS_INTERVAL_COUNT; i++)
      if (GPS_INTERVAL_OPTS[i] == p->gps_interval) return i;
    return 0;
  }
#endif

  bool isSection(int item) const {
    return item == SECTION_DISPLAY || item == SECTION_SOUND ||
           item == SECTION_HOME_PAGES ||
           item == SECTION_RADIO   || item == SECTION_SYSTEM ||
           item == SECTION_CONTACTS || item == SECTION_MESSAGES
#if ENV_INCLUDE_GPS == 1
           || item == SECTION_GPS
#endif
    ;
  }

  const char* sectionName(int item) const {
    if (item == SECTION_DISPLAY)    return "Display";
    if (item == SECTION_SOUND)      return "Sound";
    if (item == SECTION_HOME_PAGES) return "Home Pages";
    if (item == SECTION_RADIO)      return "Radio";
    if (item == SECTION_SYSTEM)     return "System";
    if (item == SECTION_CONTACTS)   return "Contacts";
    if (item == SECTION_MESSAGES)   return "Messages";
#if ENV_INCLUDE_GPS == 1
    if (item == SECTION_GPS)        return "GPS";
#endif
    return "";
  }

  bool isHomePage(int item) const {
    return item == HOME_CLOCK || item == HOME_RECENT || item == HOME_RADIO ||
           item == HOME_BT    || item == HOME_ADVERT || item == HOME_TOOLS ||
           item == HOME_SHUTDOWN
#if ENV_INCLUDE_GPS == 1
           || item == HOME_GPS
#endif
#if UI_SENSORS_PAGE == 1
           || item == HOME_SENSORS
#endif
    ;
  }

  uint16_t homePageBit(int item) const {
    if (item == HOME_CLOCK)    return HP_CLOCK;
    if (item == HOME_RECENT)   return HP_RECENT;
    if (item == HOME_RADIO)    return HP_RADIO;
    if (item == HOME_BT)       return HP_BLUETOOTH;
    if (item == HOME_ADVERT)   return HP_ADVERT;
    if (item == HOME_TOOLS)    return HP_TOOLS;
    if (item == HOME_SHUTDOWN) return HP_SHUTDOWN;
#if ENV_INCLUDE_GPS == 1
    if (item == HOME_GPS)      return HP_GPS;
#endif
#if UI_SENSORS_PAGE == 1
    if (item == HOME_SENSORS)  return HP_SENSORS;
#endif
    return 0;
  }

  const char* homePageLabel(int item) const {
    if (item == HOME_CLOCK)    return "Clock";
    if (item == HOME_RECENT)   return "Recent";
    if (item == HOME_RADIO)    return "Radio";
    if (item == HOME_BT)       return "Bluetooth";
    if (item == HOME_ADVERT)   return "Advert";
    if (item == HOME_TOOLS)    return "Tools";
    if (item == HOME_SHUTDOWN) return "Shutdown";
#if ENV_INCLUDE_GPS == 1
    if (item == HOME_GPS)      return "GPS";
#endif
#if UI_SENSORS_PAGE == 1
    if (item == HOME_SENSORS)  return "Sensors";
#endif
    return "";
  }

  bool homePageVisible(int item, const NodePrefs* p) const {
    uint16_t bit = homePageBit(item);
    if (!bit) return false;
    uint16_t mask = (p && p->home_pages_mask) ? p->home_pages_mask : HP_ALL;
    return (mask & bit) != 0;
  }

  bool isMsgSlot(int item) const {
    return item >= MSG_SLOT_0 && item <= MSG_SLOT_9;
  }

  int msgSlotIndex(int item) const {
    return item - MSG_SLOT_0;
  }

  int nextSelectable(int from) const {
    for (int i = from + 1; i < (int)Count; i++)
      if (!isSection(i)) return i;
    return from;
  }

  int prevSelectable(int from) const {
    for (int i = from - 1; i >= 0; i--)
      if (!isSection(i)) return i;
    return from;
  }

  void renderItem(DisplayDriver& display, int item, int y) {
    NodePrefs* p = _task->getNodePrefs();

    if (isSection(item)) {
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(0, y, display.width(), 1);
      display.setCursor(2, y + 2);
      display.print(sectionName(item));
      return;
    }

    bool sel = (item == _selected);

    if (sel) {
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(0, y - 1, display.width(), ITEM_H);
      display.setColor(DisplayDriver::DARK);
    } else {
      display.setColor(DisplayDriver::LIGHT);
    }

    display.setCursor(0, y);
    display.print(sel ? ">" : " ");
    display.setCursor(8, y);

    if (item == BRIGHTNESS) {
      display.print("Bright");
      renderBar(display, VAL_X, y, (p ? p->display_brightness : 2) + 1, 5);
    } else if (item == BUZZER) {
      display.print("Buzzer");
      display.setCursor(VAL_X, y);
#ifdef PIN_BUZZER
      { static const char* labels[] = { "ON", "OFF", "Auto" };
        int m = _task->getBuzzerMode();
        display.print(labels[m < 3 ? m : 0]); }
#else
      display.print("N/A");
#endif
    } else if (item == BUZZER_VOLUME) {
      display.print("BzrVol");
#ifdef PIN_BUZZER
      renderBar(display, VAL_X, y, _task->getBuzzerVolume() + 1, 5);
#else
      display.setCursor(VAL_X, y);
      display.print("N/A");
#endif
    } else if (item == DM_MELODY) {
      display.print("DM sound");
      display.setCursor(VAL_X, y);
      { static const char* L[] = { "built-in", "M1", "M2" };
        uint8_t v = p ? p->notif_melody_dm : 0;
        display.print(L[v < 3 ? v : 0]); }
    } else if (item == CH_MELODY) {
      display.print("Ch sound");
      display.setCursor(VAL_X, y);
      { static const char* L[] = { "built-in", "M1", "M2" };
        uint8_t v = p ? p->notif_melody_ch : 0;
        display.print(L[v < 3 ? v : 0]); }
    } else if (isHomePage(item)) {
      display.print(homePageLabel(item));
      display.setCursor(VAL_X, y);
      display.print(homePageVisible(item, p) ? "ON" : "OFF");
    } else if (item == TX_POWER) {
      display.print("TX Pwr");
      char buf[8];
      sprintf(buf, "%ddBm", p ? p->tx_power_dbm : 0);
      display.setCursor(VAL_X, y);
      display.print(buf);
    } else if (item == AUTO_OFF) {
      display.print("AutoOff");
      display.setCursor(VAL_X, y);
      display.print(AUTO_OFF_LABELS[autoOffIndex()]);
    } else if (item == AUTO_LOCK) {
      display.print("AutoLock");
      display.setCursor(VAL_X, y);
      display.print((p && p->auto_lock) ? "ON" : "OFF");
#if ENV_INCLUDE_GPS == 1
    } else if (item == GPS_INTERVAL) {
      display.print("GPS upd");
      display.setCursor(VAL_X, y);
      display.print(GPS_INTERVAL_LABELS[gpsIntervalIndex()]);
#endif
    } else if (item == TIMEZONE) {
      display.print("TimeZone");
      char buf[8];
      int8_t tz = p ? p->tz_offset_hours : 0;
      if (tz >= 0) sprintf(buf, "UTC+%d", (int)tz);
      else         sprintf(buf, "UTC%d",  (int)tz);
      display.setCursor(VAL_X, y);
      display.print(buf);
    } else if (item == LOW_BAT) {
      display.print("LowBat");
      display.setCursor(VAL_X, y);
      display.print(LOW_BAT_LABELS[lowBatIndex()]);
    } else if (item == BATT_DISPLAY) {
      display.print("BattDisp");
      display.setCursor(VAL_X, y);
      uint8_t mode = p ? p->batt_display_mode : 0;
      display.print(BATT_DISPLAY_LABELS[mode < BATT_DISPLAY_COUNT ? mode : 0]);
    } else if (item == CLOCK_SECONDS) {
      display.print("Seconds");
      display.setCursor(VAL_X, y);
      display.print((p && p->clock_hide_seconds) ? "OFF" : "ON");
    } else if (item == DM_FILTER) {
      display.print("DM");
      display.setCursor(VAL_X, y);
      display.print((p && p->dm_show_all) ? "all" : "fav");
    } else if (item == ROOM_FILTER) {
      display.print("Rooms");
      display.setCursor(VAL_X, y);
      display.print((p && p->room_fav_only) ? "fav" : "all");
    } else if (isMsgSlot(item)) {
      int slot = msgSlotIndex(item);
      char label[5];
      snprintf(label, sizeof(label), "Q%d:", slot + 1);
      display.print(label);
      const char* tmpl = (p && p->custom_msgs[slot][0]) ? p->custom_msgs[slot] : "(empty)";
      display.drawTextEllipsized(VAL_X - 20, y, display.width() - VAL_X + 18, tmpl);
    }
  }

  // Keyboard state for editing message slots
  int           _edit_slot;  // -1 = not editing, 0..9 = slot being edited
  KeyboardWidget _kb;

public:
  SettingsScreen(UITask* task)
    : _task(task), _selected(BRIGHTNESS), _scroll(0), _dirty(false), _edit_slot(-1) {}


  void markClean() { _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    if (_edit_slot >= 0) {
      return _kb.render(display);
    }

    display.setColor(DisplayDriver::LIGHT);
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

    return 2000;
  }

  bool handleInput(char c) override {
    NodePrefs* p = _task->getNodePrefs();

    // Keyboard editing mode for message slots
    if (_edit_slot >= 0) {
      auto res = _kb.handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (p) {
          strncpy(p->custom_msgs[_edit_slot], _kb.buf, sizeof(p->custom_msgs[0]) - 1);
          p->custom_msgs[_edit_slot][sizeof(p->custom_msgs[0]) - 1] = '\0';
          _dirty = true;
        }
        _edit_slot = -1;
      } else if (res == KeyboardWidget::CANCELLED) {
        _edit_slot = -1;
      }
      return true;
    }

    if (c == KEY_UP) {
      int prev = prevSelectable(_selected);
      if (prev != _selected) {
        _selected = prev;
        if (_selected < _scroll) _scroll = _selected;
      }
      return true;
    }
    if (c == KEY_DOWN) {
      int next = nextSelectable(_selected);
      if (next != _selected) {
        _selected = next;
        if (_selected >= _scroll + VISIBLE_ITEMS) _scroll = _selected - VISIBLE_ITEMS + 1;
      }
      return true;
    }
    if (c == KEY_CANCEL) {
      if (_dirty) the_mesh.savePrefs();
      _task->gotoHomeScreen();
      return true;
    }

    bool right = (c == KEY_RIGHT || c == KEY_NEXT);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
    bool enter = (c == KEY_ENTER);

    if (_selected == BRIGHTNESS) {
      uint8_t lvl = _task->getBrightnessLevel();
      if (right && lvl < 4) { _task->setBrightnessLevel(lvl + 1); _dirty = true; return true; }
      if (left  && lvl > 0) { _task->setBrightnessLevel(lvl - 1); _dirty = true; return true; }
      return right || left;
    }
    if (_selected == BUZZER && (left || right || enter)) {
      _task->cycleBuzzerMode();
      _dirty = true;
      return true;
    }
    if (_selected == BUZZER_VOLUME) {
#ifdef PIN_BUZZER
      uint8_t lvl = _task->getBuzzerVolume();
      if (right && lvl < 4) { _task->setBuzzerVolumeLevel(lvl + 1); _dirty = true; return true; }
      if (left  && lvl > 0) { _task->setBuzzerVolumeLevel(lvl - 1); _dirty = true; return true; }
#endif
      return right || left;
    }
    if (_selected == DM_MELODY && p && (left || right || enter)) {
      p->notif_melody_dm = (p->notif_melody_dm + (left ? 2 : 1)) % 3;
      _dirty = true; return true;
    }
    if (_selected == CH_MELODY && p && (left || right || enter)) {
      p->notif_melody_ch = (p->notif_melody_ch + (left ? 2 : 1)) % 3;
      _dirty = true; return true;
    }
    if (isHomePage(_selected) && p && (left || right || enter)) {
      p->home_pages_mask ^= homePageBit(_selected);
      _dirty = true;
      return true;
    }
    if (_selected == TX_POWER && p) {
      if (right && p->tx_power_dbm < 22) { p->tx_power_dbm++; _task->applyTxPower(); _dirty = true; return true; }
      if (left  && p->tx_power_dbm > 2)  { p->tx_power_dbm--; _task->applyTxPower(); _dirty = true; return true; }
    }
    if (_selected == AUTO_OFF && p) {
      int idx = autoOffIndex();
      if (right) idx = (idx + 1) % AUTO_OFF_COUNT;
      if (left)  idx = (idx + AUTO_OFF_COUNT - 1) % AUTO_OFF_COUNT;
      if (left || right) { p->auto_off_secs = AUTO_OFF_OPTS[idx]; _dirty = true; return true; }
    }
    if (_selected == AUTO_LOCK && p && (left || right || enter)) {
      p->auto_lock ^= 1;
      _dirty = true;
      return true;
    }
#if ENV_INCLUDE_GPS == 1
    if (_selected == GPS_INTERVAL && p) {
      int idx = gpsIntervalIndex();
      if (right) idx = (idx + 1) % GPS_INTERVAL_COUNT;
      if (left)  idx = (idx + GPS_INTERVAL_COUNT - 1) % GPS_INTERVAL_COUNT;
      if (left || right) {
        p->gps_interval = GPS_INTERVAL_OPTS[idx];
        _task->applyGPSInterval();
        _dirty = true;
        return true;
      }
    }
#endif
    if (_selected == TIMEZONE && p) {
      if (right && p->tz_offset_hours < 14)  { p->tz_offset_hours++; _dirty = true; return true; }
      if (left  && p->tz_offset_hours > -12) { p->tz_offset_hours--; _dirty = true; return true; }
    }
    if (_selected == LOW_BAT && p) {
      int idx = lowBatIndex();
      if (right) idx = (idx + 1) % LOW_BAT_COUNT;
      if (left)  idx = (idx + LOW_BAT_COUNT - 1) % LOW_BAT_COUNT;
      if (left || right) { p->low_batt_mv = LOW_BAT_OPTS[idx]; _dirty = true; return true; }
    }
    if (_selected == BATT_DISPLAY && p) {
      int idx = p->batt_display_mode < BATT_DISPLAY_COUNT ? p->batt_display_mode : 0;
      if (right) idx = (idx + 1) % BATT_DISPLAY_COUNT;
      if (left)  idx = (idx + BATT_DISPLAY_COUNT - 1) % BATT_DISPLAY_COUNT;
      if (left || right) { p->batt_display_mode = idx; _dirty = true; return true; }
    }
    if (_selected == CLOCK_SECONDS && p && (left || right || enter)) {
      p->clock_hide_seconds ^= 1;
      _dirty = true;
      return true;
    }
    if (_selected == DM_FILTER && p && (left || right || enter)) {
      p->dm_show_all = p->dm_show_all ? 0 : 1;
      _dirty = true;
      return true;
    }
    if (_selected == ROOM_FILTER && p && (left || right || enter)) {
      p->room_fav_only = p->room_fav_only ? 0 : 1;
      _dirty = true;
      return true;
    }
    if (isMsgSlot(_selected) && enter) {
      int slot = msgSlotIndex(_selected);
      _edit_slot = slot;
      _kb.begin(p ? p->custom_msgs[slot] : "");
      kbAddSensorPlaceholders(_kb, &sensors);
      return true;
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
