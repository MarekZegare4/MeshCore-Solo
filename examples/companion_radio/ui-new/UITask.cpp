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
    display.drawTextCentered(display.width()/2, 54, "Plus for Wio");
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

// On-screen keyboard layout (4 rows × 10 cols)
static const char KB_CHARS[4][10] = {
  {'a','b','c','d','e','f','g','h','i','j'},
  {'k','l','m','n','o','p','q','r','s','t'},
  {'u','v','w','x','y','z','.',' ','!','?'},
  {'1','2','3','4','5','6','7','8','9','0'},
};
static const int KB_ROWS_CHAR  = 4;
static const int KB_COLS_CHAR  = 10;
static const int KB_SPECIAL    = 5;   // [^] [Sp] [Del] [{}] [OK]
static const char* KB_PH_LIST[] = { "{loc}", "{time}" };
static const int KB_PH_COUNT   = 2;
static const int KB_MAX_LEN    = 139;
static const int KB_CELL_W     = 12;  // 10 cols × 12px = 120px of 128px display
static const int KB_CELL_H     = 9;
static const int KB_TEXT_Y     = 0;
static const int KB_SEP_Y      = 9;
static const int KB_CHARS_Y    = 11;  // first char row
static const int KB_SPECIAL_Y  = 48;

class SettingsScreen : public UIScreen {
  UITask* _task;

  enum SettingItem {
    // Display section
    SECTION_DISPLAY,
    BRIGHTNESS,
    AUTO_OFF,
    BATT_DISPLAY,
    // Sound section
    SECTION_SOUND,
    BUZZER,
    BUZZER_VOLUME,
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
      if (i < value) display.fillRect(bx, y, box_w, box_h);
      else           display.drawRect(bx, y, box_w, box_h);
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
           item == SECTION_RADIO   || item == SECTION_SYSTEM ||
           item == SECTION_CONTACTS || item == SECTION_MESSAGES
#if ENV_INCLUDE_GPS == 1
           || item == SECTION_GPS
#endif
    ;
  }

  const char* sectionName(int item) const {
    if (item == SECTION_DISPLAY)  return "Display";
    if (item == SECTION_SOUND)    return "Sound";
    if (item == SECTION_RADIO)    return "Radio";
    if (item == SECTION_SYSTEM)   return "System";
    if (item == SECTION_CONTACTS) return "Contacts";
    if (item == SECTION_MESSAGES) return "Messages";
#if ENV_INCLUDE_GPS == 1
    if (item == SECTION_GPS)      return "GPS";
#endif
    return "";
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
      display.print(_task->isBuzzerQuiet() ? "OFF" : "ON");
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
      snprintf(label, sizeof(label), "M%d:", slot + 1);
      display.print(label);
      const char* tmpl = (p && p->custom_msgs[slot][0]) ? p->custom_msgs[slot] : "(empty)";
      display.drawTextEllipsized(VAL_X - 20, y, display.width() - VAL_X + 18, tmpl);
    }
  }

  // Keyboard state for editing message slots
  int  _edit_slot;              // -1 = not editing, 0..9 = slot being edited
  char _edit_buf[KB_MAX_LEN + 1];
  int  _edit_len;
  int  _edit_kb_row, _edit_kb_col;
  bool _edit_caps;
  bool _edit_ph_mode;
  int  _edit_ph_sel;

public:
  SettingsScreen(UITask* task)
    : _task(task), _selected(BRIGHTNESS), _scroll(0), _dirty(false),
      _edit_slot(-1), _edit_len(0), _edit_kb_row(0), _edit_kb_col(0),
      _edit_caps(false), _edit_ph_mode(false), _edit_ph_sel(0) {
    _edit_buf[0] = '\0';
  }

  void markClean() { _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    if (_edit_slot >= 0) {
      // Keyboard mode for editing a message slot
      display.setColor(DisplayDriver::LIGHT);
      // preview line with cursor
      const char* disp_start = _edit_buf;
      int disp_len = _edit_len;
      if (disp_len > 20) { disp_start = _edit_buf + (disp_len - 20); disp_len = 20; }
      char preview[26];
      snprintf(preview, sizeof(preview), "M%d:%.*s_", _edit_slot + 1, disp_len, disp_start);
      display.setCursor(0, KB_TEXT_Y);
      display.print(preview);
      display.fillRect(0, KB_SEP_Y, display.width(), 1);

      // char rows
      for (int row = 0; row < KB_ROWS_CHAR; row++) {
        int y = KB_CHARS_Y + row * KB_CELL_H;
        for (int col = 0; col < KB_COLS_CHAR; col++) {
          bool sel = (_edit_kb_row == row && _edit_kb_col == col);
          char ch = KB_CHARS[row][col];
          if (_edit_caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
          char ch_buf[2] = { ch, '\0' };
          if (ch_buf[0] == ' ') ch_buf[0] = '_';
          int cx = col * KB_CELL_W;
          if (sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(cx, y - 1, KB_CELL_W - 1, KB_CELL_H);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(cx + 3, y);
          display.print(ch_buf);
        }
      }

      // special row: [^] [Sp] [Del] [{}] [OK]  — 5 buttons at 25px each
      const char* spec[] = { "[^]", "[Sp]", "[Del]", "[{}]", "[OK]" };
      for (int i = 0; i < KB_SPECIAL; i++) {
        bool sel = (_edit_kb_row == KB_ROWS_CHAR && _edit_kb_col == i);
        bool active = (i == 0 && _edit_caps);
        int sx = i * 25;
        if (sel || active) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(sx, KB_SPECIAL_Y - 1, 24, KB_CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(sx + 1, KB_SPECIAL_Y);
        display.print(spec[i]);
        display.setColor(DisplayDriver::LIGHT);
      }

      // placeholder picker overlay
      if (_edit_ph_mode) {
        display.setColor(DisplayDriver::DARK);
        display.fillRect(20, 20, 88, 12 + KB_PH_COUNT * 10);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(20, 20, 88, 12 + KB_PH_COUNT * 10);
        display.setCursor(24, 21);
        display.print("Placeholder:");
        display.fillRect(20, 30, 88, 1);
        for (int i = 0; i < KB_PH_COUNT; i++) {
          int py = 33 + i * 10;
          if (i == _edit_ph_sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(21, py - 1, 86, 10);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(24, py);
          display.print(KB_PH_LIST[i]);
        }
        display.setColor(DisplayDriver::LIGHT);
      }

      return 50;
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

    return 300;
  }

  bool handleInput(char c) override {
    NodePrefs* p = _task->getNodePrefs();

    // Keyboard editing mode for message slots
    if (_edit_slot >= 0) {
      if (c == KEY_UP) {
        if (_edit_kb_row > 0) {
          _edit_kb_row--;
          if (_edit_kb_row == KB_ROWS_CHAR - 1)  // leaving special row upward
            _edit_kb_col = _edit_kb_col * KB_COLS_CHAR / KB_SPECIAL;
        }
        return true;
      }
      if (c == KEY_DOWN) {
        if (_edit_kb_row < KB_ROWS_CHAR) {
          _edit_kb_row++;
          if (_edit_kb_row == KB_ROWS_CHAR)  // entering special row
            _edit_kb_col = _edit_kb_col * KB_SPECIAL / KB_COLS_CHAR;
        }
        return true;
      }
      if (c == KEY_LEFT) {
        if (_edit_kb_col > 0) _edit_kb_col--;
        return true;
      }
      if (c == KEY_RIGHT) {
        int max_col = (_edit_kb_row == KB_ROWS_CHAR) ? KB_SPECIAL - 1 : KB_COLS_CHAR - 1;
        if (_edit_kb_col < max_col) _edit_kb_col++;
        return true;
      }
      if (_edit_ph_mode) {
        if (c == KEY_UP   && _edit_ph_sel > 0) { _edit_ph_sel--; return true; }
        if (c == KEY_DOWN && _edit_ph_sel < KB_PH_COUNT - 1) { _edit_ph_sel++; return true; }
        if (c == KEY_ENTER) {
          const char* ph = KB_PH_LIST[_edit_ph_sel];
          int ph_len = strlen(ph);
          if (_edit_len + ph_len <= KB_MAX_LEN) {
            memcpy(_edit_buf + _edit_len, ph, ph_len);
            _edit_len += ph_len;
            _edit_buf[_edit_len] = '\0';
          }
          _edit_ph_mode = false;
          return true;
        }
        if (c == KEY_CANCEL) { _edit_ph_mode = false; return true; }
        return true;
      }
      if (c == KEY_ENTER) {
        if (_edit_kb_row < KB_ROWS_CHAR) {
          if (_edit_len < KB_MAX_LEN) {
            char ch = KB_CHARS[_edit_kb_row][_edit_kb_col];
            if (_edit_caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            _edit_buf[_edit_len++] = ch;
            _edit_buf[_edit_len] = '\0';
          }
        } else {
          if (_edit_kb_col == 0) {
            // Caps Lock toggle
            _edit_caps = !_edit_caps;
          } else if (_edit_kb_col == 1) {
            // Space
            if (_edit_len < KB_MAX_LEN) {
              _edit_buf[_edit_len++] = ' ';
              _edit_buf[_edit_len] = '\0';
            }
          } else if (_edit_kb_col == 2) {
            // Del
            if (_edit_len > 0) _edit_buf[--_edit_len] = '\0';
          } else if (_edit_kb_col == 3) {
            // Placeholder picker
            _edit_ph_mode = true;
            _edit_ph_sel  = 0;
          } else {
            // OK — save
            if (p) {
              strncpy(p->custom_msgs[_edit_slot], _edit_buf, sizeof(p->custom_msgs[0]) - 1);
              p->custom_msgs[_edit_slot][sizeof(p->custom_msgs[0]) - 1] = '\0';
              the_mesh.savePrefs();
            }
            _edit_slot = -1;
          }
        }
        return true;
      }
      if (c == KEY_CANCEL) {
        _edit_slot = -1;
        return true;
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
      _task->toggleBuzzer();  // saves immediately internally
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
      _edit_len = 0;
      _edit_buf[0] = '\0';
      // Pre-fill with existing text
      if (p && p->custom_msgs[slot][0]) {
        int existing = strlen(p->custom_msgs[slot]);
        if (existing > KB_MAX_LEN) existing = KB_MAX_LEN;
        memcpy(_edit_buf, p->custom_msgs[slot], existing);
        _edit_buf[existing] = '\0';
        _edit_len = existing;
      }
      _edit_kb_row = 0;
      _edit_kb_col = 0;
      _edit_caps = false;
      _edit_ph_mode = false;
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

class QuickMsgScreen : public UIScreen {
  UITask* _task;

  enum Phase { MODE_SELECT, CONTACT_PICK, MSG_PICK, CHANNEL_PICK, CHANNEL_HIST, KEYBOARD };
  Phase _phase;

  // MODE_SELECT
  int _mode_sel;  // 0=Direct, 1=Channel, 2=Room Servers

  // CONTACT_PICK
  int _contact_sel, _contact_scroll;
  int _num_contacts;
  uint8_t _sorted[MAX_CONTACTS];
  ContactInfo _sel_contact;
  bool _room_mode;  // true = picking a room server, false = picking a DM contact

  // CHANNEL_PICK
  int _channel_sel, _channel_scroll;
  int _num_channels;
  uint8_t _channel_indices[MAX_GROUP_CHANNELS];
  uint8_t _ch_unread[MAX_GROUP_CHANNELS];
  int _sel_channel_idx;
  bool _sending_to_channel;

  // MSG_PICK (shared)
  int _msg_sel, _msg_scroll;
  int _active_msgs[QUICK_MSGS_MAX];
  int _active_msg_count;

  // CHANNEL_HIST
  int _hist_sel, _hist_scroll;
  bool _hist_fullscreen;
  int  _hist_fs_scroll;
  int  _unread_at_entry;    // _ch_unread value when entering CHANNEL_HIST
  int  _viewing_max_seen;  // highest _hist_sel reached in current session
  static const int CH_HIST_MAX = 32;

  static const int FS_CHARS   = 21;
  static const int FS_LINE_H  = 9;
  static const int FS_START_Y = 12;
  static const int FS_VISIBLE = (64 - FS_START_Y) / FS_LINE_H;

  int wrapLines(const char* text, char out[][FS_CHARS + 1], int max_lines) const {
    int count = 0;
    const char* p = text;
    while (*p && count < max_lines) {
      int len = strlen(p);
      if (len <= FS_CHARS) {
        strncpy(out[count++], p, FS_CHARS);
        out[count-1][len] = '\0';
        break;
      }
      int brk = FS_CHARS;
      for (int i = FS_CHARS - 1; i > 0; i--) {
        if (p[i] == ' ') { brk = i; break; }
      }
      strncpy(out[count], p, brk);
      out[count++][brk] = '\0';
      p += brk + (p[brk] == ' ' ? 1 : 0);
    }
    return count;
  }

  // KEYBOARD
  int _kb_row, _kb_col;
  char _kb_text[KB_MAX_LEN + 1];
  int _kb_len;
  bool _kb_caps;
  bool _kb_ph_mode;
  int  _kb_ph_sel;

  // Context menu (opened by long-press ENTER in CHANNEL_PICK)
  bool _ctx_open;
  bool _ctx_dirty;
  int  _ctx_sel;

  struct ChHistEntry { uint8_t ch_idx; char text[140]; };
  ChHistEntry _hist[CH_HIST_MAX];
  int _hist_head, _hist_count;

  static const int VISIBLE      = 4;
  static const int ITEM_H       = 12;
  static const int START_Y      = 12;
  static const int HIST_VISIBLE = 2;   // 2-line boxed history entries
  static const int HIST_ITEM_H  = 21;  // box(19) + gap(2)
  static const int HIST_BOX_H   = 19;
  static const int HIST_START_Y = 11;

  void expandMsg(const char* tmpl, char* out, int out_len) const {
    int oi = 0;
    const char* p = tmpl;
    while (*p && oi < out_len - 1) {
      if (strncmp(p, "{loc}", 5) == 0) {
#if ENV_INCLUDE_GPS == 1
        LocationProvider* loc = sensors.getLocationProvider();
        if (loc && loc->isValid()) {
          char lb[32];
          snprintf(lb, sizeof(lb), "%.5f,%.5f",
            loc->getLatitude() / 1000000.0, loc->getLongitude() / 1000000.0);
          int ll = strlen(lb);
          if (oi + ll < out_len - 1) { memcpy(out + oi, lb, ll); oi += ll; }
        } else {
          const char* s = "no GPS"; int sl = 6;
          if (oi + sl < out_len - 1) { memcpy(out + oi, s, sl); oi += sl; }
        }
#else
        const char* s = "no GPS"; int sl = 6;
        if (oi + sl < out_len - 1) { memcpy(out + oi, s, sl); oi += sl; }
#endif
        p += 5;
      } else if (strncmp(p, "{time}", 6) == 0) {
        uint32_t ts = rtc_clock.getCurrentTime();
        if (ts > 1000000000UL) {
          NodePrefs* np = _task->getNodePrefs();
          ts += (int32_t)(np ? np->tz_offset_hours : 0) * 3600;
          time_t t = (time_t)ts;
          struct tm* ti = gmtime(&t);
          char tb[8];
          snprintf(tb, sizeof(tb), "%02d:%02d", ti->tm_hour, ti->tm_min);
          int tl = strlen(tb);
          if (oi + tl < out_len - 1) { memcpy(out + oi, tb, tl); oi += tl; }
        }
        p += 6;
      } else {
        out[oi++] = *p++;
      }
    }
    out[oi] = '\0';
  }

  void setupMsgPick() {
    _msg_sel = _msg_scroll = 0;
    _active_msg_count = 0;
    NodePrefs* p = _task->getNodePrefs();
    if (p) {
      for (int i = 0; i < QUICK_MSGS_MAX; i++) {
        if (p->custom_msgs[i][0] != '\0')
          _active_msgs[_active_msg_count++] = i;
      }
    }
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

  // count history entries for a specific channel
  int histCountForChannel(int ch_idx) const {
    int n = 0;
    for (int i = 0; i < _hist_count; i++) {
      if (_hist[(_hist_head + i) % CH_HIST_MAX].ch_idx == (uint8_t)ch_idx) n++;
    }
    return n;
  }

  // get ring-buffer position of j-th history entry for channel (newest first)
  int histEntryForChannel(int ch_idx, int j) const {
    int n = 0;
    for (int i = _hist_count - 1; i >= 0; i--) {
      int pos = (_hist_head + i) % CH_HIST_MAX;
      if (_hist[pos].ch_idx == (uint8_t)ch_idx) {
        if (n == j) return pos;
        n++;
      }
    }
    return -1;
  }

  void afterSend(bool ok, const char* msg) {
    if (ok && _sending_to_channel) {
      _hist_sel = 0;
      _hist_scroll = 0;
      _phase = CHANNEL_HIST;  // set before addChannelMsg so viewing=true, no unread bump
      char entry[sizeof(ChHistEntry::text)];
      snprintf(entry, sizeof(entry), "Me: %s", msg);
      addChannelMsg(_sel_channel_idx, entry);
      // After inserting sent msg at index 0, the unread index range is stale.
      // User is active in this channel — treat as fully read.
      _ch_unread[_sel_channel_idx] = 0;
      _unread_at_entry = 0;
      _viewing_max_seen = 0;
      _task->showAlert("Sent!", 600);
    } else {
      _task->showAlert(ok ? "Sent!" : "Send failed", 1500);
      _task->gotoHomeScreen();
    }
  }

  bool sendText(const char* msg) {
    if (_sending_to_channel) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(_sel_channel_idx, ch)) return false;
      return the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                       the_mesh.getNodeName(), msg, strlen(msg));
    } else {
      uint32_t expected_ack = 0, est_timeout = 0;
      return the_mesh.sendMessage(_sel_contact, rtc_clock.getCurrentTime(), 0,
                                  msg, expected_ack, est_timeout) > 0;
    }
  }

  void buildContactList() {
    NodePrefs* p = _task->getNodePrefs();
    ContactInfo c;
    int total = the_mesh.getNumContacts();
    _num_contacts = 0;
    if (_room_mode) {
      bool fav_only = (p && p->room_fav_only);
      for (int i = 0; i < total; i++) {
        if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_ROOM) continue;
        if (fav_only && !(c.flags & 0x01)) continue;
        _sorted[_num_contacts++] = i;
      }
    } else {
      bool show_all = (p && p->dm_show_all);
      for (int i = 0; i < total; i++) {
        if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_CHAT) continue;
        if (!show_all && !(c.flags & 0x01)) continue;
        _sorted[_num_contacts++] = i;
      }
    }
  }

  void buildChannelList() {
    _num_channels = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        _channel_indices[_num_channels++] = (uint8_t)i;
      }
    }
  }

  // Returns per-channel notification state: 0=follow global, 1=muted, 2=force-on
  uint8_t chNotifState(uint8_t ch_idx) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    uint64_t mask = 1ULL << ch_idx;
    if (!(p->ch_notif_override & mask)) return 0;
    return (p->ch_notif_muted & mask) ? 1 : 2;
  }

  void setChNotifState(uint8_t ch_idx, uint8_t state) {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return;
    uint64_t mask = 1ULL << ch_idx;
    if (state == 0) {
      p->ch_notif_override &= ~mask;
      p->ch_notif_muted    &= ~mask;
    } else if (state == 1) {
      p->ch_notif_override |= mask;
      p->ch_notif_muted    |= mask;
    } else {
      p->ch_notif_override |= mask;
      p->ch_notif_muted    &= ~mask;
    }
  }

public:
  QuickMsgScreen(UITask* task)
    : _task(task), _phase(MODE_SELECT), _mode_sel(0),
      _contact_sel(0), _contact_scroll(0), _num_contacts(0), _room_mode(false),
      _channel_sel(0), _channel_scroll(0), _num_channels(0),
      _sel_channel_idx(0), _sending_to_channel(false),
      _msg_sel(0), _msg_scroll(0), _active_msg_count(0),
      _hist_sel(0), _hist_scroll(0),
      _hist_fullscreen(false), _hist_fs_scroll(0),
      _unread_at_entry(0), _viewing_max_seen(0),
      _hist_head(0), _hist_count(0),
      _kb_row(0), _kb_col(0), _kb_len(0),
      _kb_caps(false), _kb_ph_mode(false), _kb_ph_sel(0),
      _ctx_open(false), _ctx_dirty(false), _ctx_sel(0) {
    _kb_text[0] = '\0';
    memset(_ch_unread, 0, sizeof(_ch_unread));
  }

  void addChannelMsg(uint8_t ch_idx, const char* text) {
    int pos;
    if (_hist_count < CH_HIST_MAX) {
      pos = (_hist_head + _hist_count) % CH_HIST_MAX;
      _hist_count++;
    } else {
      pos = _hist_head;
      _hist_head = (_hist_head + 1) % CH_HIST_MAX;
    }
    _hist[pos].ch_idx = ch_idx;
    strncpy(_hist[pos].text, text, sizeof(_hist[pos].text) - 1);
    _hist[pos].text[sizeof(_hist[pos].text) - 1] = '\0';

    if (ch_idx < MAX_GROUP_CHANNELS) {
      bool viewing = (_phase == CHANNEL_HIST && _sel_channel_idx == (int)ch_idx);
      if (!viewing && _ch_unread[ch_idx] < 99) _ch_unread[ch_idx]++;
    }
  }

  int getTotalChannelUnread() const {
    int total = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) total += _ch_unread[i];
    return total;
  }

  void updateChannelUnread() {
    if (_hist_sel < 0 || _sel_channel_idx < 0 || _sel_channel_idx >= MAX_GROUP_CHANNELS) return;
    if (_hist_sel > _viewing_max_seen) _viewing_max_seen = _hist_sel;
    // histEntryForChannel is newest-first: index 0 = newest (unread), higher = older.
    // Each step down from 0 sees one more message; seen count = max_seen + 1.
    int remaining = _unread_at_entry - (_viewing_max_seen + 1);
    _ch_unread[_sel_channel_idx] = (uint8_t)(remaining > 0 ? remaining : 0);
  }

  void reset() {
    _phase = MODE_SELECT;
    _mode_sel = 0;
    _contact_sel = _contact_scroll = 0;
    _msg_sel = _msg_scroll = 0;
    _channel_sel = _channel_scroll = 0;
    _sending_to_channel = false;
    _kb_row = _kb_col = _kb_len = 0;
    _kb_caps = false; _kb_ph_mode = false; _kb_ph_sel = 0;
    _kb_text[0] = '\0';

    _room_mode = false;
    buildContactList();
    buildChannelList();

    _ctx_open = false;
    _ctx_dirty = false;
    _ctx_sel = 0;
    _unread_at_entry = 0;
    _viewing_max_seen = 0;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_phase == MODE_SELECT) {
      display.drawTextCentered(display.width()/2, 0, "MESSAGE");
      display.fillRect(0, 10, display.width(), 1);
      const char* opts[] = { "Direct message", "Channels", "Room Servers" };
      int badges[3] = {
        _task->getMsgCount() - _task->getRoomUnreadCount(),  // DM only
        _task->getChannelUnreadCount(),
        _task->getRoomUnreadCount()
      };
      if (badges[0] < 0) badges[0] = 0;
      for (int i = 0; i < 3; i++) {
        int y = START_Y + i * ITEM_H;
        bool sel = (i == _mode_sel);
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");
        display.setCursor(8, y);
        display.print(opts[i]);
        if (badges[i] > 0) {
          char badge[5];
          snprintf(badge, sizeof(badge), "%d", badges[i]);
          int bw = display.getTextWidth(badge) + 2;
          display.setCursor(display.width() - bw, y);
          display.print(badge);
        }
      }
      display.setColor(DisplayDriver::LIGHT);

    } else if (_phase == CONTACT_PICK) {
      display.drawTextCentered(display.width()/2, 0, _room_mode ? "SELECT ROOM" : "SELECT CONTACT");
      display.fillRect(0, 10, display.width(), 1);

      if (_num_contacts == 0) {
        display.drawTextCentered(display.width()/2, 32, _room_mode ? "No room servers" : "No favourites");
        return 5000;
      }

      for (int i = 0; i < VISIBLE && (_contact_scroll+i) < _num_contacts; i++) {
        int list_idx = _contact_scroll + i;
        int mesh_idx = _sorted[list_idx];
        bool sel = (list_idx == _contact_sel);
        int y = START_Y + i * ITEM_H;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        ContactInfo c;
        if (the_mesh.getContactByIdx(mesh_idx, c)) {
          display.setCursor(0, y);
          display.print(sel ? ">" : " ");
          char filtered[sizeof(c.name)];
          display.translateUTF8ToBlocks(filtered, c.name, sizeof(filtered));
          display.drawTextEllipsized(8, y, display.width() - 24, filtered);
          char hop[5];
          snprintf(hop, sizeof(hop), c.out_path_len == 0xFF ? "D" : "%dh", (int)c.out_path_len);
          display.setCursor(display.width() - display.getTextWidth(hop) - 1, y);
          display.print(hop);
        }
      }
      display.setColor(DisplayDriver::LIGHT);
      renderScrollHints(display, _contact_scroll, _num_contacts);

    } else if (_phase == CHANNEL_PICK) {
      display.drawTextCentered(display.width()/2, 0, "SELECT CHANNEL");
      display.fillRect(0, 10, display.width(), 1);

      if (_num_channels == 0) {
        display.drawTextCentered(display.width()/2, 32, "No channels");
        return 5000;
      }

      for (int i = 0; i < VISIBLE && (_channel_scroll+i) < _num_channels; i++) {
        int list_idx = _channel_scroll + i;
        bool sel = (list_idx == _channel_sel);
        int y = START_Y + i * ITEM_H;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");
        ChannelDetails ch;
        if (the_mesh.getChannel(_channel_indices[list_idx], ch)) {
          uint8_t unread = _ch_unread[_channel_indices[list_idx]];
          char badge[5] = "";
          int bw = 0;
          if (unread > 0) {
            snprintf(badge, sizeof(badge), "%d", (int)unread);
            bw = display.getTextWidth(badge) + 2;
          }
          display.drawTextEllipsized(8, y, display.width() - 10 - bw, ch.name);
          if (unread > 0) {
            display.setCursor(display.width() - bw, y);
            display.print(badge);
          }
        }
      }
      display.setColor(DisplayDriver::LIGHT);
      renderScrollHints(display, _channel_scroll, _num_channels);

      // Context menu overlay (long-press ENTER)
      if (_ctx_open && _num_channels > 0) {
        static const char* NOTIF_LABELS[] = { "default", "OFF", "ON" };
        uint8_t ch_idx = _channel_indices[_channel_sel];
        uint8_t nstate = chNotifState(ch_idx);
        char notif_item[22];
        snprintf(notif_item, sizeof(notif_item), "Notif: %s", NOTIF_LABELS[nstate]);
        const char* items[] = { "Mark all read", notif_item };
        const int CTX_COUNT = 2;

        display.setColor(DisplayDriver::DARK);
        display.fillRect(15, 14, 98, 34);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(15, 14, 98, 34);
        display.setCursor(19, 15);
        display.print("Channel options");
        display.fillRect(15, 24, 98, 1);
        for (int i = 0; i < CTX_COUNT; i++) {
          int py = 27 + i * 10;
          if (i == _ctx_sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(16, py - 1, 96, 10);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(19, py);
          display.print(items[i]);
        }
        display.setColor(DisplayDriver::LIGHT);
      }

    } else if (_phase == CHANNEL_HIST) {
      if (_hist_fullscreen && _hist_sel >= 0) {
        int fs_hist_count = histCountForChannel(_sel_channel_idx);
        int ring_pos = histEntryForChannel(_sel_channel_idx, _hist_sel);
        if (ring_pos >= 0) {
          const char* ftext = _hist[ring_pos].text;
          const char* fsep = strstr(ftext, ": ");
          char fsender[22], fmsg[79];
          if (fsep) {
            int nl = fsep - ftext; if (nl > 21) nl = 21;
            strncpy(fsender, ftext, nl); fsender[nl] = '\0';
            strncpy(fmsg, fsep + 2, sizeof(fmsg) - 1); fmsg[sizeof(fmsg)-1] = '\0';
          } else {
            strncpy(fsender, "?", sizeof(fsender));
            strncpy(fmsg, ftext, sizeof(fmsg) - 1); fmsg[sizeof(fmsg)-1] = '\0';
          }
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, 0, display.width(), 10);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(2, 1, display.width() - 4, fsender);
          display.setColor(DisplayDriver::LIGHT);

          char lines[12][FS_CHARS + 1];
          int lcount = wrapLines(fmsg, lines, 12);
          int max_scroll = lcount > FS_VISIBLE ? lcount - FS_VISIBLE : 0;
          if (_hist_fs_scroll > max_scroll) _hist_fs_scroll = max_scroll;
          for (int i = 0; i < FS_VISIBLE && (_hist_fs_scroll + i) < lcount; i++) {
            display.setCursor(0, FS_START_Y + i * FS_LINE_H);
            display.print(lines[_hist_fs_scroll + i]);
          }
          if (_hist_fs_scroll > 0) {
            display.setCursor(display.width() - 6, FS_START_Y);
            display.print("^");
          }
          if (_hist_fs_scroll < max_scroll) {
            display.setCursor(display.width() - 6, FS_START_Y + (FS_VISIBLE - 1) * FS_LINE_H);
            display.print("v");
          }
          if (_hist_sel > 0) {
            display.setCursor(0, 56);
            display.print("<");
          }
          if (_hist_sel < fs_hist_count - 1) {
            display.setCursor(display.width() - 6, 56);
            display.print(">");
          }
        }
        return 300;
      }

      ChannelDetails ch;
      the_mesh.getChannel(_sel_channel_idx, ch);
      char title[24];
      snprintf(title, sizeof(title), "#%.21s", ch.name);
      display.drawTextCentered(display.width()/2, 0, title);
      display.fillRect(0, 9, display.width(), 1);

      int ch_hist_count = histCountForChannel(_sel_channel_idx);

      for (int i = 0; i < HIST_VISIBLE && (_hist_scroll + i) < ch_hist_count; i++) {
        int item = _hist_scroll + i;
        bool sel = (item == _hist_sel);
        int y = HIST_START_Y + i * HIST_ITEM_H;

        int ring_pos = histEntryForChannel(_sel_channel_idx, item);
        if (ring_pos < 0) continue;

        const char* text = _hist[ring_pos].text;
        const char* sep = strstr(text, ": ");
        char sender[22], msg_part[79];
        if (sep) {
          int nl = sep - text; if (nl > 21) nl = 21;
          strncpy(sender, text, nl); sender[nl] = '\0';
          strncpy(msg_part, sep + 2, sizeof(msg_part) - 1);
          msg_part[sizeof(msg_part) - 1] = '\0';
        } else {
          strncpy(sender, "?", sizeof(sender));
          strncpy(msg_part, text, sizeof(msg_part) - 1);
          msg_part[sizeof(msg_part) - 1] = '\0';
        }

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y, display.width(), HIST_BOX_H);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(3, y + 1, display.width() - 6, sender);
          display.drawTextEllipsized(3, y + 10, display.width() - 6, msg_part);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(0, y, display.width(), HIST_BOX_H);
          display.fillRect(1, y + 1, display.width() - 2, 8);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(3, y + 1, display.width() - 6, sender);
          display.setColor(DisplayDriver::LIGHT);
          display.drawTextEllipsized(3, y + 10, display.width() - 6, msg_part);
        }
      }

      if (ch_hist_count == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width()/2, 32, "No messages yet");
      }

      // scroll hints
      display.setColor(DisplayDriver::LIGHT);
      if (_hist_scroll > 0) {
        display.setCursor(display.width() - 6, HIST_START_Y + 1);
        display.print("^");
      }
      if (_hist_scroll + HIST_VISIBLE < ch_hist_count) {
        display.setCursor(display.width() - 6, HIST_START_Y + HIST_VISIBLE * HIST_ITEM_H - 10);
        display.print("v");
      }

      // small compose button (bottom-left, always bordered, inverted when selected)
      bool compose_sel = (_hist_sel == -1);
      const char* ctxt = "[+ send]";
      int ctw = display.getTextWidth(ctxt);
      int cbx = 1, cby = 55;
      if (compose_sel) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(cbx - 1, cby - 1, ctw + 4, 10);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(cbx - 1, cby - 1, ctw + 4, 10);
      }
      display.setCursor(cbx + 1, cby);
      display.print(ctxt);
      display.setColor(DisplayDriver::LIGHT);

    } else if (_phase == KEYBOARD) {
      // text preview with cursor
      display.setColor(DisplayDriver::LIGHT);
      const char* disp_start = _kb_text;
      int disp_len = _kb_len;
      if (disp_len > 20) { disp_start = _kb_text + (disp_len - 20); disp_len = 20; }
      char preview[24];
      snprintf(preview, sizeof(preview), "%.*s_", disp_len, disp_start);
      display.setCursor(0, KB_TEXT_Y);
      display.print(preview);
      display.fillRect(0, KB_SEP_Y, display.width(), 1);

      // char rows
      for (int row = 0; row < KB_ROWS_CHAR; row++) {
        int y = KB_CHARS_Y + row * KB_CELL_H;
        for (int col = 0; col < KB_COLS_CHAR; col++) {
          bool sel = (_kb_row == row && _kb_col == col);
          char ch = KB_CHARS[row][col];
          if (_kb_caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
          char ch_buf[2] = { ch, '\0' };
          if (ch_buf[0] == ' ') ch_buf[0] = '_';
          int cx = col * KB_CELL_W;
          if (sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(cx, y - 1, KB_CELL_W - 1, KB_CELL_H);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(cx + 3, y);
          display.print(ch_buf);
        }
      }

      // special row: [^] [Sp] [Del] [{}] [OK]  — 5 buttons at 25px each
      const char* spec[] = { "[^]", "[Sp]", "[Del]", "[{}]", "[OK]" };
      for (int i = 0; i < KB_SPECIAL; i++) {
        bool sel = (_kb_row == KB_ROWS_CHAR && _kb_col == i);
        bool active = (i == 0 && _kb_caps);  // caps indicator
        int sx = i * 25;
        if (sel || active) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(sx, KB_SPECIAL_Y - 1, 24, KB_CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(sx + 1, KB_SPECIAL_Y);
        display.print(spec[i]);
        display.setColor(DisplayDriver::LIGHT);
      }

      // placeholder picker overlay
      if (_kb_ph_mode) {
        // Black box with white border
        display.setColor(DisplayDriver::DARK);
        display.fillRect(20, 20, 88, 12 + KB_PH_COUNT * 10);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(20, 20, 88, 12 + KB_PH_COUNT * 10);
        display.setCursor(24, 21);
        display.print("Placeholder:");
        display.fillRect(20, 30, 88, 1);  // separator
        for (int i = 0; i < KB_PH_COUNT; i++) {
          int py = 33 + i * 10;
          if (i == _kb_ph_sel) {
            // Selected: white fill + black text
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(21, py - 1, 86, 10);
            display.setColor(DisplayDriver::DARK);
          } else {
            // Unselected: white text on black
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(24, py);
          display.print(KB_PH_LIST[i]);
        }
        display.setColor(DisplayDriver::LIGHT);
      }

    } else { // MSG_PICK
      char title[24];
      if (_sending_to_channel) {
        ChannelDetails ch;
        the_mesh.getChannel(_sel_channel_idx, ch);
        snprintf(title, sizeof(title), "#%.21s", ch.name);
      } else {
        snprintf(title, sizeof(title), "TO:%.14s", _sel_contact.name);
      }
      display.drawTextCentered(display.width()/2, 0, title);
      display.fillRect(0, 10, display.width(), 1);

      int total_msg_items = 1 + _active_msg_count;
      for (int i = 0; i < VISIBLE && (_msg_scroll+i) < total_msg_items; i++) {
        int idx = _msg_scroll + i;
        bool sel = (idx == _msg_sel);
        int y = START_Y + i * ITEM_H;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");

        if (idx == 0) {
          display.setCursor(8, y);
          display.print("Custom message...");
        } else {
          NodePrefs* p = _task->getNodePrefs();
          int slot = _active_msgs[idx - 1];
          const char* tmpl = p ? p->custom_msgs[slot] : "";
          display.drawTextEllipsized(8, y, display.width() - 10, tmpl);
        }
      }
      display.setColor(DisplayDriver::LIGHT);
      renderScrollHints(display, _msg_scroll, total_msg_items);
    }
    return 300;
  }

  bool handleInput(char c) override {
    if (_phase == MODE_SELECT) {
      if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
      if (c == KEY_UP && _mode_sel > 0) { _mode_sel--; return true; }
      if (c == KEY_DOWN && _mode_sel < 2) { _mode_sel++; return true; }
      if (c == KEY_ENTER) {
        if (_mode_sel == 1) {
          _phase = CHANNEL_PICK;
        } else {
          _room_mode = (_mode_sel == 2);
          if (_room_mode) _task->clearRoomUnread();
          buildContactList();
          _contact_sel = _contact_scroll = 0;
          _phase = CONTACT_PICK;
        }
        return true;
      }

    } else if (_phase == CONTACT_PICK) {
      if (c == KEY_CANCEL) { _room_mode = false; _phase = MODE_SELECT; return true; }
      if (c == KEY_UP && _contact_sel > 0) {
        _contact_sel--;
        if (_contact_sel < _contact_scroll) _contact_scroll = _contact_sel;
        return true;
      }
      if (c == KEY_DOWN && _contact_sel < _num_contacts - 1) {
        _contact_sel++;
        if (_contact_sel >= _contact_scroll + VISIBLE) _contact_scroll = _contact_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER && _num_contacts > 0) {
        if (the_mesh.getContactByIdx(_sorted[_contact_sel], _sel_contact)) {
          _sending_to_channel = false;
          setupMsgPick();
          _phase = MSG_PICK;
        }
        return true;
      }

    } else if (_phase == CHANNEL_PICK) {
      // Context menu consumes all input while open
      if (_ctx_open) {
        if (c == KEY_CANCEL) {
          if (_ctx_dirty) { the_mesh.savePrefs(); _ctx_dirty = false; }
          _ctx_open = false;
          return true;
        }
        if (c == KEY_UP   && _ctx_sel > 0) { _ctx_sel--; return true; }
        if (c == KEY_DOWN && _ctx_sel < 1) { _ctx_sel++; return true; }
        if (c == KEY_ENTER && _num_channels > 0) {
          uint8_t ch_idx = _channel_indices[_channel_sel];
          if (_ctx_sel == 0) {
            _ch_unread[ch_idx] = 0;
          } else {
            uint8_t nstate = chNotifState(ch_idx);
            setChNotifState(ch_idx, (nstate + 1) % 3);
            _ctx_dirty = true;
          }
          return true;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _phase = MODE_SELECT; return true; }
      if (c == KEY_UP && _channel_sel > 0) {
        _channel_sel--;
        if (_channel_sel < _channel_scroll) _channel_scroll = _channel_sel;
        return true;
      }
      if (c == KEY_DOWN && _channel_sel < _num_channels - 1) {
        _channel_sel++;
        if (_channel_sel >= _channel_scroll + VISIBLE) _channel_scroll = _channel_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER && _num_channels > 0) {
        _sel_channel_idx = _channel_indices[_channel_sel];
        int hc = histCountForChannel(_sel_channel_idx);
        _unread_at_entry = (int)_ch_unread[_sel_channel_idx];
        _hist_scroll = 0;
        _hist_sel = hc > 0 ? 0 : -1;
        _viewing_max_seen = _hist_sel >= 0 ? _hist_sel : 0;
        _phase = CHANNEL_HIST;
        updateChannelUnread();
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _num_channels > 0) {
        _ctx_sel = 0;
        _ctx_dirty = false;
        _ctx_open = true;
        return true;
      }

    } else if (_phase == CHANNEL_HIST) {
      int ch_hist_count = histCountForChannel(_sel_channel_idx);
      if (_hist_fullscreen) {
        if (c == KEY_UP)   { if (_hist_fs_scroll > 0) _hist_fs_scroll--; return true; }
        if (c == KEY_DOWN) { _hist_fs_scroll++; return true; }
        if (c == KEY_LEFT) {
          if (_hist_sel > 0) { _hist_sel--; _hist_fs_scroll = 0; updateChannelUnread(); }
          return true;
        }
        if (c == KEY_RIGHT) {
          int count = histCountForChannel(_sel_channel_idx);
          if (_hist_sel < count - 1) { _hist_sel++; _hist_fs_scroll = 0; updateChannelUnread(); }
          return true;
        }
        if (c == KEY_ENTER || c == KEY_CANCEL) { _hist_fullscreen = false; return true; }
        return true;
      }
      if (c == KEY_CANCEL) { _phase = CHANNEL_PICK; return true; }
      if (c == KEY_UP) {
        if (_hist_sel > 0) { _hist_sel--; if (_hist_sel < _hist_scroll) _hist_scroll = _hist_sel; }
        else if (_hist_sel == 0) _hist_sel = -1;
        updateChannelUnread();
        return true;
      }
      if (c == KEY_DOWN) {
        if (_hist_sel == -1 && ch_hist_count > 0) { _hist_sel = 0; _hist_scroll = 0; }
        else if (_hist_sel >= 0 && _hist_sel < ch_hist_count - 1) {
          _hist_sel++;
          if (_hist_sel >= _hist_scroll + HIST_VISIBLE) _hist_scroll = _hist_sel - HIST_VISIBLE + 1;
        }
        updateChannelUnread();
        return true;
      }
      if (c == KEY_ENTER) {
        if (_hist_sel >= 0) {
          _hist_fullscreen = true;
          _hist_fs_scroll = 0;
          updateChannelUnread();
        } else {
          _sending_to_channel = true;
          setupMsgPick();
          _phase = MSG_PICK;
        }
        return true;
      }

    } else if (_phase == KEYBOARD) {
      if (_kb_ph_mode) {
        if (c == KEY_UP   && _kb_ph_sel > 0) { _kb_ph_sel--; return true; }
        if (c == KEY_DOWN && _kb_ph_sel < KB_PH_COUNT - 1) { _kb_ph_sel++; return true; }
        if (c == KEY_ENTER) {
          const char* ph = KB_PH_LIST[_kb_ph_sel];
          int ph_len = strlen(ph);
          if (_kb_len + ph_len <= KB_MAX_LEN) {
            memcpy(_kb_text + _kb_len, ph, ph_len);
            _kb_len += ph_len;
            _kb_text[_kb_len] = '\0';
          }
          _kb_ph_mode = false;
          return true;
        }
        if (c == KEY_CANCEL) { _kb_ph_mode = false; return true; }
        return true;
      }
      if (c == KEY_CANCEL) {
        _phase = MSG_PICK;
        return true;
      }
      if (c == KEY_UP) {
        if (_kb_row > 0) {
          _kb_row--;
          if (_kb_row == KB_ROWS_CHAR - 1)  // leaving special row upward
            _kb_col = _kb_col * KB_COLS_CHAR / KB_SPECIAL;
        }
        return true;
      }
      if (c == KEY_DOWN) {
        if (_kb_row < KB_ROWS_CHAR) {
          _kb_row++;
          if (_kb_row == KB_ROWS_CHAR)  // entering special row
            _kb_col = _kb_col * KB_SPECIAL / KB_COLS_CHAR;
        }
        return true;
      }
      if (c == KEY_LEFT) {
        if (_kb_col > 0) _kb_col--;
        return true;
      }
      if (c == KEY_RIGHT) {
        int max_col = (_kb_row == KB_ROWS_CHAR) ? KB_SPECIAL - 1 : KB_COLS_CHAR - 1;
        if (_kb_col < max_col) _kb_col++;
        return true;
      }
      if (c == KEY_ENTER) {
        if (_kb_row < KB_ROWS_CHAR) {
          if (_kb_len < KB_MAX_LEN) {
            char ch = KB_CHARS[_kb_row][_kb_col];
            if (_kb_caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            _kb_text[_kb_len++] = ch;
            _kb_text[_kb_len] = '\0';
          }
        } else {
          if (_kb_col == 0) {
            // Caps Lock toggle
            _kb_caps = !_kb_caps;
          } else if (_kb_col == 1) {
            // Space
            if (_kb_len < KB_MAX_LEN) {
              _kb_text[_kb_len++] = ' ';
              _kb_text[_kb_len] = '\0';
            }
          } else if (_kb_col == 2) {
            // Delete
            if (_kb_len > 0) _kb_text[--_kb_len] = '\0';
          } else if (_kb_col == 3) {
            // Placeholder picker
            _kb_ph_mode = true;
            _kb_ph_sel  = 0;
          } else {
            // OK — send (expand placeholders first)
            if (_kb_len > 0) {
              char expanded[KB_MAX_LEN + 1];
              expandMsg(_kb_text, expanded, sizeof(expanded));
              bool ok = sendText(expanded);
              afterSend(ok, expanded);
            }
          }
        }
        return true;
      }

    } else { // MSG_PICK
      int total_msg_items = 1 + _active_msg_count;
      if (c == KEY_CANCEL) {
        _phase = _sending_to_channel ? CHANNEL_HIST : CONTACT_PICK;
        return true;
      }
      if (c == KEY_UP && _msg_sel > 0) {
        _msg_sel--;
        if (_msg_sel < _msg_scroll) _msg_scroll = _msg_sel;
        return true;
      }
      if (c == KEY_DOWN && _msg_sel < total_msg_items - 1) {
        _msg_sel++;
        if (_msg_sel >= _msg_scroll + VISIBLE) _msg_scroll = _msg_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER) {
        if (_msg_sel == 0) {
          _kb_row = _kb_col = _kb_len = 0;
          _kb_caps = false; _kb_ph_mode = false; _kb_ph_sel = 0;
          _kb_text[0] = '\0';
          _phase = KEYBOARD;
          return true;
        }
        NodePrefs* p = _task->getNodePrefs();
        int slot = _active_msgs[_msg_sel - 1];
        const char* tmpl = p ? p->custom_msgs[slot] : "OK";
        char msg[80];
        expandMsg(tmpl, msg, sizeof(msg));
        bool ok = sendText(msg);
        afterSend(ok, msg);
        return true;
      }
    }
    return false;
  }
};

// ── RingtoneEditorScreen ──────────────────────────────────────────────────────
class RingtoneEditorScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  static const int MAX_NOTES     = 32;
  static const int VISIBLE_NOTES = 7;
  static const int CELL_W        = 18;
  static const int NOTES_Y       = 12;
  static const int CELL_H        = 14;
  static const int MENU_VISIBLE  = 5;

  static const uint16_t BPM_OPTS[5];
  static const uint8_t  DUR_VALS[4];
  static const char*    DUR_LABELS[4];
  static const char     PITCH_NAMES[8];  // lowercase rtttl names

  uint8_t _notes[MAX_NOTES];
  uint8_t _len;
  uint8_t _bpm_idx;
  int     _cursor;
  int     _scroll;
  bool    _menu_open;
  int     _menu_sel;
  int     _menu_scroll;

  enum MenuOpt {
    M_PLAY_STOP, M_DURATION, M_BPM_UP, M_BPM_DOWN,
    M_INSERT, M_DELETE, M_SAVE, M_DISCARD, M_COUNT
  };
  static const char* MENU_LABELS[M_COUNT];

  static uint8_t notePitch(uint8_t b)  { return b & 0x07; }
  static uint8_t noteOctave(uint8_t b) { return ((b >> 3) & 0x03) + 4; }
  static uint8_t noteDurIdx(uint8_t b) { return (b >> 5) & 0x03; }
  static uint8_t packNote(uint8_t pitch, uint8_t octave, uint8_t dur_idx) {
    return (pitch & 0x07) | (((octave - 4) & 0x03) << 3) | ((dur_idx & 0x03) << 5);
  }

  void clampScroll() {
    if (_cursor < _scroll)                  _scroll = _cursor;
    if (_cursor >= _scroll + VISIBLE_NOTES) _scroll = _cursor - VISIBLE_NOTES + 1;
    if (_scroll < 0) _scroll = 0;
  }

  void buildRTTTL(char* buf, int buf_len) {
    uint16_t bpm = BPM_OPTS[_bpm_idx < 5 ? _bpm_idx : 2];
    int pos = snprintf(buf, buf_len, "Ring:d=8,o=5,b=%u:", bpm);
    for (int i = 0; i < _len && pos < buf_len - 8; i++) {
      if (i > 0 && pos < buf_len - 1) buf[pos++] = ',';
      uint8_t pitch   = notePitch(_notes[i]);
      uint8_t octave  = noteOctave(_notes[i]);
      uint8_t dur_val = DUR_VALS[noteDurIdx(_notes[i])];
      if (pitch == 0)
        pos += snprintf(buf + pos, buf_len - pos, "%dp", dur_val);
      else
        pos += snprintf(buf + pos, buf_len - pos, "%d%c%d", dur_val, PITCH_NAMES[pitch], octave);
    }
    if (pos < buf_len) buf[pos] = '\0';
  }

public:
  RingtoneEditorScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void enter() {
    _bpm_idx    = (_prefs && _prefs->ringtone_bpm_idx < 5) ? _prefs->ringtone_bpm_idx : 2;
    _len        = (_prefs && _prefs->ringtone_len <= (uint8_t)MAX_NOTES) ? _prefs->ringtone_len : 0;
    if (_prefs) memcpy(_notes, _prefs->ringtone_notes, sizeof(_notes));
    _cursor     = 0;
    _scroll     = 0;
    _menu_open  = false;
    _menu_sel   = 0;
    _menu_scroll = 0;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "BPM:%u  %d/%d", BPM_OPTS[_bpm_idx], _len, MAX_NOTES);
    display.setCursor(0, 0);
    display.print(hdr);
    display.fillRect(0, 10, display.width(), 1);

    for (int i = 0; i < VISIBLE_NOTES; i++) {
      int ni = _scroll + i;
      int cx = i * CELL_W;
      bool sel = (ni == _cursor);
      if (ni < _len) {
        uint8_t pitch  = notePitch(_notes[ni]);
        uint8_t octave = noteOctave(_notes[ni]);
        char label[3];
        if (pitch == 0) {
          label[0] = '-'; label[1] = '-'; label[2] = '\0';
        } else {
          label[0] = PITCH_NAMES[pitch] - 32;  // uppercase
          label[1] = '0' + octave;
          label[2] = '\0';
        }
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(cx, NOTES_Y, CELL_W - 1, CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(cx, NOTES_Y, CELL_W - 1, CELL_H);
        }
        display.setCursor(cx + 3, NOTES_Y + 3);
        display.print(label);
        display.setColor(DisplayDriver::LIGHT);
      } else if (ni == _len && _len < MAX_NOTES) {
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(cx, NOTES_Y, CELL_W - 1, CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(cx + 6, NOTES_Y + 3);
        display.print("+");
        display.setColor(DisplayDriver::LIGHT);
      }
    }

    int info_y = NOTES_Y + CELL_H + 2;
    if (_scroll > 0) { display.setCursor(0, info_y); display.print("<"); }
    if (_scroll + VISIBLE_NOTES <= _len) { display.setCursor(display.width() - 6, info_y); display.print(">"); }
    if (_cursor < _len) {
      char info[24];
      snprintf(info, sizeof(info), "oct:%d dur:%s",
               noteOctave(_notes[_cursor]), DUR_LABELS[noteDurIdx(_notes[_cursor])]);
      display.setCursor(10, info_y);
      display.print(info);
    } else if (_cursor == _len) {
      display.setCursor(10, info_y);
      display.print("U/D to add note");
    }

    display.setCursor(0, 54);
    display.print("ENT:oct MENU:opts");

    if (_menu_open) {
      static const int mw = 100, mh = MENU_VISIBLE * 10 + 4;
      int mx = (128 - mw) / 2;
      int my = (64  - mh) / 2;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(mx, my, mw, mh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(mx, my, mw, mh);
      for (int i = 0; i < MENU_VISIBLE; i++) {
        int item = _menu_scroll + i;
        if (item >= M_COUNT) break;
        int iy = my + 2 + i * 10;
        bool sel = (item == _menu_sel);
        if (sel) {
          display.fillRect(mx + 1, iy - 1, mw - 2, 10);
          display.setColor(DisplayDriver::DARK);
        }
        display.setCursor(mx + 4, iy);
        if (item == M_PLAY_STOP)
          display.print(_task->isMelodyPlaying() ? "Stop" : "Play");
        else
          display.print(MENU_LABELS[item]);
        display.setColor(DisplayDriver::LIGHT);
      }
      if (_menu_scroll > 0) { display.setCursor(mx + mw - 7, my + 2); display.print("^"); }
      if (_menu_scroll + MENU_VISIBLE < M_COUNT) { display.setCursor(mx + mw - 7, my + mh - 10); display.print("v"); }
    }

    return 200;
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
    bool right = (c == KEY_RIGHT || c == KEY_NEXT);
    bool enter = (c == KEY_ENTER);
    bool menu_key = (c == KEY_CONTEXT_MENU);
    bool cancel   = (c == KEY_CANCEL);

    if (_menu_open) {
      if (cancel) { _menu_open = false; return true; }
      if (up && _menu_sel > 0) {
        _menu_sel--;
        if (_menu_sel < _menu_scroll) _menu_scroll = _menu_sel;
        return true;
      }
      if (down && _menu_sel < M_COUNT - 1) {
        _menu_sel++;
        if (_menu_sel >= _menu_scroll + MENU_VISIBLE) _menu_scroll = _menu_sel - MENU_VISIBLE + 1;
        return true;
      }
      if (!enter) return true;

      _menu_open = false;
      switch ((MenuOpt)_menu_sel) {
        case M_PLAY_STOP:
          if (_task->isMelodyPlaying()) {
            _task->stopMelody();
          } else if (_len > 0) {
            char buf[220];
            buildRTTTL(buf, sizeof(buf));
            _task->playMelody(buf);
          }
          break;
        case M_DURATION:
          if (_cursor < _len) {
            uint8_t p  = notePitch(_notes[_cursor]);
            uint8_t o  = noteOctave(_notes[_cursor]);
            uint8_t di = (noteDurIdx(_notes[_cursor]) + 1) & 0x03;
            _notes[_cursor] = packNote(p, o, di);
          }
          break;
        case M_BPM_UP:   if (_bpm_idx < 4) _bpm_idx++; break;
        case M_BPM_DOWN: if (_bpm_idx > 0) _bpm_idx--; break;
        case M_INSERT:
          if (_len < MAX_NOTES) {
            int ins = (_cursor < _len) ? _cursor + 1 : _cursor;
            for (int i = _len; i > ins; i--) _notes[i] = _notes[i - 1];
            _notes[ins] = packNote(1, 5, 1);
            _len++;
            _cursor = ins;
            clampScroll();
          }
          break;
        case M_DELETE:
          if (_len > 0 && _cursor < _len) {
            for (int i = _cursor; i < _len - 1; i++) _notes[i] = _notes[i + 1];
            _len--;
            if (_cursor >= _len && _len > 0) _cursor = _len - 1;
            else if (_len == 0) _cursor = 0;
            clampScroll();
          }
          break;
        case M_SAVE:
          if (_prefs) {
            _prefs->ringtone_bpm_idx = _bpm_idx;
            _prefs->ringtone_len     = _len;
            memcpy(_prefs->ringtone_notes, _notes, sizeof(_notes));
            the_mesh.savePrefs();
          }
          _task->stopMelody();
          _task->gotoHomeScreen();
          break;
        case M_DISCARD:
          _task->stopMelody();
          _task->gotoHomeScreen();
          break;
        default: break;
      }
      return true;
    }

    if (cancel)   { _task->stopMelody(); _task->gotoHomeScreen(); return true; }
    if (menu_key) { _menu_open = true; _menu_sel = 0; _menu_scroll = 0; return true; }

    if (left && _cursor > 0) { _cursor--; clampScroll(); return true; }
    if (right) {
      int max_cur = (_len < MAX_NOTES) ? _len : _len - 1;
      if (_cursor < max_cur) { _cursor++; clampScroll(); return true; }
    }

    if ((up || down) && _cursor == _len && _len < MAX_NOTES) {
      _notes[_len] = packNote(1, 5, 1);
      _len++;
      clampScroll();
    }
    if ((up || down) && _cursor < _len) {
      uint8_t p  = notePitch(_notes[_cursor]);
      uint8_t o  = noteOctave(_notes[_cursor]);
      uint8_t di = noteDurIdx(_notes[_cursor]);
      if (up)   p = (p + 1) & 0x07;
      if (down) p = (p + 7) & 0x07;
      _notes[_cursor] = packNote(p, o, di);
      return true;
    }

    if (enter && _cursor < _len) {
      uint8_t p  = notePitch(_notes[_cursor]);
      uint8_t o  = noteOctave(_notes[_cursor]);
      uint8_t di = noteDurIdx(_notes[_cursor]);
      if (p != 0) o = (o < 6) ? o + 1 : 4;
      _notes[_cursor] = packNote(p, o, di);
      return true;
    }
    if (enter && _cursor == _len && _len < MAX_NOTES) {
      _notes[_len] = packNote(1, 5, 1);
      _len++;
      clampScroll();
      return true;
    }
    return false;
  }
};

const uint16_t RingtoneEditorScreen::BPM_OPTS[5]   = { 60, 90, 120, 150, 180 };
const uint8_t  RingtoneEditorScreen::DUR_VALS[4]    = { 4, 8, 16, 32 };
const char*    RingtoneEditorScreen::DUR_LABELS[4]  = { "1/4", "1/8", "1/16", "1/32" };
const char     RingtoneEditorScreen::PITCH_NAMES[8] = { 'p', 'c', 'd', 'e', 'f', 'g', 'a', 'b' };
const char*    RingtoneEditorScreen::MENU_LABELS[RingtoneEditorScreen::M_COUNT] = {
  "Play/Stop", "Duration", "BPM+", "BPM-", "Insert", "Delete", "Save & Exit", "Discard"
};

// ── ToolsScreen ───────────────────────────────────────────────────────────────
class ToolsScreen : public UIScreen {
  UITask* _task;

public:
  ToolsScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 0, "TOOLS");
    display.fillRect(0, 10, display.width(), 1);

    display.fillRect(0, 12, display.width(), 11);
    display.setColor(DisplayDriver::DARK);
    display.setCursor(4, 13);
    display.print("Ringtone Editor");
    display.setColor(DisplayDriver::LIGHT);

    display.setCursor(0, 54);
    display.print("ENT:open  CANCEL:back");
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) { _task->gotoHomeScreen(); return true; }
    if (c == KEY_ENTER) { _task->gotoRingtoneEditor(); return true; }
    return false;
  }
};

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


  void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
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
      const int iconWidth = 24, iconHeight = 10;
      battLeftX = display.width() - iconWidth - 5;
      display.drawRect(battLeftX, 0, iconWidth, iconHeight);
      display.fillRect(battLeftX + iconWidth, iconHeight / 4, 3, iconHeight / 2);
      int fillWidth = (pct * (iconWidth - 4)) / 100;
      display.fillRect(battLeftX + 2, 2, fillWidth, iconHeight - 4);
    }

#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm(battLeftX - 9, 1, muted_icon, 8, 8);
    }
#endif

    // BT connection indicator (left of muted/battery icons)
    if (_task->isSerialEnabled()) {
#ifdef PIN_BUZZER
      int btX = battLeftX - 18;
#else
      int btX = battLeftX - 9;
#endif
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(btX - 1, 0, 8, 9);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(btX, 1);
        display.print("B");
        display.setColor(DisplayDriver::LIGHT);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(btX, 1);
        display.print("b");
      }
    }
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
    display.setColor(DisplayDriver::LIGHT);
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
        sprintf(buf, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
        display.drawTextCentered(display.width() / 2, 18, buf);

        display.setTextSize(1);
        const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        sprintf(buf, "%s %d %s %d", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon], 1900 + ti->tm_year);
        display.drawTextCentered(display.width() / 2, 40, buf);

        if (tz >= 0) sprintf(buf, "UTC+%d", (int)tz);
        else         sprintf(buf, "UTC%d",  (int)tz);
        display.drawTextCentered(display.width() / 2, 54, buf);
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
      display.drawTextCentered(display.width() / 2, 30, "Settings");
      display.drawTextCentered(display.width() / 2, 46, PRESS_LABEL " to open");
    } else if (_page == HomePage::TOOLS) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 30, "Tools");
      display.drawTextCentered(display.width() / 2, 46, PRESS_LABEL " to open");
    } else if (_page == HomePage::QUICK_MSG) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 22, "Messages");
      int total_unread = _task->getMsgCount() + _task->getChannelUnreadCount();
      // getMsgCount() already includes room msgs via offline_queue_len; avoid double-count
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

void UITask::gotoRingtoneEditor() {
  ((RingtoneEditorScreen*)ringtone_edit)->enter();
  setCurrScreen(ringtone_edit);
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

void UITask::showAlert(const char* text, int duration_millis) {
  snprintf(_alert, sizeof(_alert), "%s", text);
  _alert_expiry = millis() + duration_millis;
}

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage: {
    bool play = false;
    bool force = false;
    if (_last_notif_ch_idx >= 0 && _node_prefs) {
      uint64_t mask = 1ULL << _last_notif_ch_idx;
      if (_node_prefs->ch_notif_override & mask) {
        if (!(_node_prefs->ch_notif_muted & mask)) { play = true; force = true; }  // state 2: force-on
        // state 1: muted — play stays false
      } else {
        play = !buzzer.isQuiet();  // state 0: follow global
      }
    } else {
      play = !buzzer.isQuiet();
    }
    _last_notif_ch_idx = -1;
    if (play) {
      if (force) buzzer.playForced("kerplop:d=16,o=6,b=120:32g#,32c#");
      else       buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    }
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
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t contact_type) {
  _msgcount = msgcount;
  if (contact_type == ADV_TYPE_ROOM && _room_unread < _msgcount) _room_unread++;

  char alert_buf[80];
  snprintf(alert_buf, sizeof(alert_buf), "Msg: %.20s", from_name);
  showAlert(alert_buf, 3000);

  if (_display != NULL) {
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
  _next_refresh = 0;
#endif
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
