#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SensorPlaceholders.h is defined.

#include "../Features.h"

class SettingsScreen : public UIScreen {
  UITask* _task;

  enum SettingItem {
    // Display section
    SECTION_DISPLAY,
#if FEAT_BRIGHTNESS_SETTING
    BRIGHTNESS,
#endif
#if AUTO_OFF_MILLIS > 0
    AUTO_OFF,
#endif
    AUTO_LOCK,
    BATT_DISPLAY,
#if FEAT_CLOCK_SECONDS_SETTING
    CLOCK_SECONDS,
#endif
    CLOCK_FORMAT,
    FONT,
#if FEAT_DISPLAY_ROTATION_SETTING
    ROTATION,
#endif
#if FEAT_JOYSTICK_ROTATION_SETTING
    JOY_ROTATION,
#endif
#if FEAT_FULL_REFRESH_SETTING
    EINK_FULL_REFRESH,
#endif
    // Sound section
    SECTION_SOUND,
    BUZZER,
    BUZZER_VOLUME,
    DM_MELODY,
    CH_MELODY,
    AD_SOUND,
    AD_SOUND_SCOPE,
    // Home pages section
    SECTION_HOME_PAGES,
    HOME_CLOCK, HOME_FAVOURITES, HOME_RECENT, HOME_RADIO, HOME_BT, HOME_ADVERT,
#if ENV_INCLUDE_GPS == 1
    HOME_GPS,
#endif
#if UI_SENSORS_PAGE == 1
    HOME_SENSORS,
#endif
    HOME_SETTINGS, HOME_QUICK_MSG,
    HOME_TOOLS, HOME_SHUTDOWN,
    // Radio section
    SECTION_RADIO,
    TX_POWER,
    POWER_SAVE,
    // System section
    SECTION_SYSTEM,
    TIMEZONE,
    LOW_BAT,
    UNITS,
    // Contacts section
    SECTION_CONTACTS, DM_FILTER, CH_FILTER, ROOM_FILTER,
    // Messages section
    SECTION_MESSAGES,
    MSG_SLOT_0, MSG_SLOT_1, MSG_SLOT_2, MSG_SLOT_3, MSG_SLOT_4,
    MSG_SLOT_5, MSG_SLOT_6, MSG_SLOT_7, MSG_SLOT_8, MSG_SLOT_9,
    Count
  };

  int _selected;
  int _scroll;
  int _visible;   // items fitting on screen; updated each render, used by handleInput
  bool _dirty;
  uint8_t _collapsed = 0x7F; // bit N set = section N collapsed (Display=0..Messages=6)
  static const int MAX_VIS = 60;
  uint8_t _vis[MAX_VIS];    // filtered list of visible SettingItem values
  int _vis_count = 0;

#if AUTO_OFF_MILLIS > 0
  static const uint16_t AUTO_OFF_OPTS[5];
  static const char* AUTO_OFF_LABELS[5];
  static const int AUTO_OFF_COUNT = 5;
#endif
// GPS update interval tables are no longer surfaced in Settings — the sensor
// manager defaults to 1 s when nothing else sets it. Pref byte _prefs.gps_interval
// is retained for backwards compatibility.
  static const uint16_t LOW_BAT_OPTS[7];
  static const char* LOW_BAT_LABELS[7];
  static const int LOW_BAT_COUNT = 7;
  static const char* BATT_DISPLAY_LABELS[3];
  static const int BATT_DISPLAY_COUNT = 3;
  static const char* SOUND_LABELS[4];
  static const int SOUND_COUNT = 4;
  static const char* AD_SCOPE_LABELS[2];
  static const int AD_SCOPE_COUNT = 2;
#if FEAT_FULL_REFRESH_SETTING
  static const char* EINK_FULL_REFRESH_LABELS[5];
  static const int   EINK_FULL_REFRESH_COUNT = 5;
#endif

  int lowBatIndex() {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    for (int i = 0; i < LOW_BAT_COUNT; i++)
      if (LOW_BAT_OPTS[i] == p->low_batt_mv) return i;
    return 0;
  }

  void renderBar(DisplayDriver& display, int x, int y, int value, int max_val) {
    const int gap     = 2;
    const int avail   = display.width() - x;
    const int raw     = (avail - (max_val - 1) * gap) / max_val;
    const int cap     = display.getLineHeight() - 2;
    const int box_h   = raw < cap ? (raw < 2 ? 2 : raw) : cap;
    const int box_w   = box_h;
    for (int i = 0; i < max_val; i++) {
      int bx = x + i * (box_w + gap);
      display.drawRect(bx, y, box_w, box_h);
      if (i < value)
        display.fillRect(bx + 1, y + 1, box_w - 2, box_h - 2);
    }
  }

#if AUTO_OFF_MILLIS > 0
  int autoOffIndex() {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 1;
    for (int i = 0; i < AUTO_OFF_COUNT; i++)
      if (AUTO_OFF_OPTS[i] == p->auto_off_secs) return i;
    return 1;
  }
#endif


  bool isSection(int item) const {
    return item == SECTION_DISPLAY || item == SECTION_SOUND ||
           item == SECTION_HOME_PAGES ||
           item == SECTION_RADIO   || item == SECTION_SYSTEM ||
           item == SECTION_CONTACTS || item == SECTION_MESSAGES;
  }

  const char* sectionName(int item) const {
    if (item == SECTION_DISPLAY)    return "Display";
    if (item == SECTION_SOUND)      return "Sound";
    if (item == SECTION_HOME_PAGES) return "Home Pages";
    if (item == SECTION_RADIO)      return "Radio";
    if (item == SECTION_SYSTEM)     return "System";
    if (item == SECTION_CONTACTS)   return "Contacts";
    if (item == SECTION_MESSAGES)   return "Messages";
    return "";
  }

  int sectionIndex(int item) const {
    if (item == SECTION_DISPLAY)    return 0;
    if (item == SECTION_SOUND)      return 1;
    if (item == SECTION_HOME_PAGES) return 2;
    if (item == SECTION_RADIO)      return 3;
    if (item == SECTION_SYSTEM)     return 4;
    if (item == SECTION_CONTACTS)   return 5;
    if (item == SECTION_MESSAGES)   return 6;
    return -1;
  }

  int visIndexOf(int item) const {
    for (int i = 0; i < _vis_count; i++)
      if (_vis[i] == (uint8_t)item) return i;
    return 0;
  }

  void buildVis() {
    _vis_count = 0;
    int cur_sec = -1;
    bool cur_collapsed = false;
    for (int i = 0; i < (int)Count; i++) {
      if (isSection(i)) {
        cur_sec++;
        cur_collapsed = (_collapsed >> cur_sec) & 1;
        if (_vis_count < MAX_VIS) _vis[_vis_count++] = (uint8_t)i;
      } else if (!cur_collapsed && _vis_count < MAX_VIS) {
        _vis[_vis_count++] = (uint8_t)i;
      }
    }
  }

  bool isHomePage(int item) const {
    return item == HOME_CLOCK    || item == HOME_RECENT     || item == HOME_RADIO   ||
           item == HOME_BT       || item == HOME_ADVERT     || item == HOME_TOOLS   ||
           item == HOME_SHUTDOWN || item == HOME_SETTINGS   || item == HOME_QUICK_MSG ||
           item == HOME_FAVOURITES
#if ENV_INCLUDE_GPS == 1
           || item == HOME_GPS
#endif
#if UI_SENSORS_PAGE == 1
           || item == HOME_SENSORS
#endif
    ;
  }

  uint16_t homePageBit(int item) const {
    int bit = homePageBitIndex(item);
    // SETTINGS and QUICK_MSG are always visible (no mask bit). All other pages
    // — including FAVOURITES — toggle via home_pages_mask.
    if (bit < 0 || bit == NodePrefs::HPB_SETTINGS || bit == NodePrefs::HPB_QUICK_MSG) return 0;
    return (uint16_t)(1 << bit);
  }

  const char* homePageLabel(int item) const {
    int bit = homePageBitIndex(item);
    return NodePrefs::homePageLabel((uint8_t)(bit >= 0 ? bit : NodePrefs::HPB_COUNT));
  }

  bool homePageVisible(int item, const NodePrefs* p) const {
    if (item == HOME_SETTINGS || item == HOME_QUICK_MSG) return true;
    uint16_t bit = homePageBit(item);
    if (!bit) return false;
    uint16_t mask = (p && p->home_pages_mask) ? p->home_pages_mask : NodePrefs::HP_ALL;
    return (mask & bit) != 0;
  }

  bool homePageToggleable(int item) const {
    return item != HOME_SETTINGS && item != HOME_QUICK_MSG;
  }

  // Returns the bit-index used in page_order for this SettingItem, or -1.
  // Bit-index values are defined once in NodePrefs::HomePageBit.
  int homePageBitIndex(int item) const {
    if (item == HOME_CLOCK)     return NodePrefs::HPB_CLOCK;
    if (item == HOME_FAVOURITES) return NodePrefs::HPB_FAVOURITES;
    if (item == HOME_RECENT)    return NodePrefs::HPB_RECENT;
    if (item == HOME_RADIO)     return NodePrefs::HPB_RADIO;
    if (item == HOME_BT)        return NodePrefs::HPB_BLUETOOTH;
    if (item == HOME_ADVERT)    return NodePrefs::HPB_ADVERT;
#if ENV_INCLUDE_GPS == 1
    if (item == HOME_GPS)       return NodePrefs::HPB_GPS;
#endif
#if UI_SENSORS_PAGE == 1
    if (item == HOME_SENSORS)   return NodePrefs::HPB_SENSORS;
#endif
    if (item == HOME_TOOLS)     return NodePrefs::HPB_TOOLS;
    if (item == HOME_SHUTDOWN)  return NodePrefs::HPB_SHUTDOWN;
    if (item == HOME_SETTINGS)  return NodePrefs::HPB_SETTINGS;
    if (item == HOME_QUICK_MSG) return NodePrefs::HPB_QUICK_MSG;
    return -1;
  }

  // Returns 1-based position of item in page_order, or 0 if no custom order / not found.
  int homePagePosition(int item, const NodePrefs* p) const {
    if (!p || p->page_order_set != NodePrefs::PAGE_ORDER_MAGIC) return 0;
    int bit = homePageBitIndex(item);
    if (bit < 0) return 0;
    for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
      uint8_t v = p->page_order[i];
      if (v < 1 || v > NodePrefs::HPB_COUNT) break;
      if ((int)(v - 1) == bit) return i + 1;
    }
    return 0;
  }

  // Initialises page_order to the default display sequence if not already set.
  // Also repairs a partially-initialised order where CLOCK is absent; migrates
  // older orders by inserting FAVOURITES after CLOCK; and appends any pages that
  // are absent from a stale saved order (e.g. TOOLS/MESSAGES added by later firmware).
  void ensurePageOrderInit(NodePrefs* p) const {
    if (!p) return;
    if (p->page_order_set == NodePrefs::PAGE_ORDER_MAGIC) {
      bool has_clock = false;
      bool has_fav   = false;
      int  len       = 0;
      int  clock_at  = -1;
      for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
        uint8_t v = p->page_order[i];
        if (v < 1 || v > NodePrefs::HPB_COUNT) break;
        if ((int)(v - 1) == NodePrefs::HPB_CLOCK)      { has_clock = true; clock_at = i; }
        if ((int)(v - 1) == NodePrefs::HPB_FAVOURITES) { has_fav = true; }
        len = i + 1;
      }
      if (!has_clock) {
        // Corrupted/partial — full re-init below.
        memset(p->page_order, 0, sizeof(p->page_order));
      } else {
        if (!has_fav) {
          // Insert FAVOURITES right after CLOCK. If the array is full, the last
          // entry (typically SHUTDOWN, which still appears via the missing-page
          // fallback at end of nav) is overwritten to make room.
          int insert_at = clock_at + 1;
          int tail = (len < NodePrefs::PAGE_ORDER_LEN) ? len : NodePrefs::PAGE_ORDER_LEN - 1;
          for (int i = tail; i > insert_at; i--) p->page_order[i] = p->page_order[i - 1];
          p->page_order[insert_at] = NodePrefs::HPB_FAVOURITES + 1;
        }
        // Append any pages that are absent from the saved order (e.g. added by a
        // later firmware version). Recount first since the block above may have
        // just inserted FAVOURITES.
        {
          uint16_t present = 0;
          int cur_len = 0;
          for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
            uint8_t v = p->page_order[i];
            if (v < 1 || v > NodePrefs::HPB_COUNT) break;
            present |= (uint16_t)(1u << (v - 1));
            cur_len++;
          }
          static const uint8_t REQUIRED[] = {
            NodePrefs::HPB_CLOCK, NodePrefs::HPB_FAVOURITES,
            NodePrefs::HPB_RECENT, NodePrefs::HPB_RADIO,
            NodePrefs::HPB_BLUETOOTH, NodePrefs::HPB_ADVERT,
#if ENV_INCLUDE_GPS == 1
            NodePrefs::HPB_GPS,
#endif
#if UI_SENSORS_PAGE == 1
            NodePrefs::HPB_SENSORS,
#endif
            NodePrefs::HPB_SETTINGS, NodePrefs::HPB_TOOLS, NodePrefs::HPB_QUICK_MSG,
          };
          // If any required page is missing, evict SHUTDOWN to make room — it is
          // handled by buildVisibleOrder's fallback and need not be in the explicit
          // list. This frees a slot regardless of whether the array is full or not,
          // so multiple missing pages (e.g. TOOLS + QUICK_MSG) can all be appended.
          bool any_missing = false;
          for (int ri = 0; ri < (int)(sizeof(REQUIRED)/sizeof(REQUIRED[0])); ri++) {
            if (!(present & (uint16_t)(1u << REQUIRED[ri]))) { any_missing = true; break; }
          }
          if (any_missing && (present & (uint16_t)(1u << NodePrefs::HPB_SHUTDOWN))) {
            for (int i = 0; i < cur_len; i++) {
              if (p->page_order[i] == NodePrefs::HPB_SHUTDOWN + 1) {
                for (int j = i; j < cur_len - 1; j++) p->page_order[j] = p->page_order[j + 1];
                p->page_order[--cur_len] = 0;
                present &= ~(uint16_t)(1u << NodePrefs::HPB_SHUTDOWN);
                break;
              }
            }
          }
          for (int ri = 0; ri < (int)(sizeof(REQUIRED)/sizeof(REQUIRED[0])); ri++) {
            uint8_t bit = REQUIRED[ri];
            if (!(present & (uint16_t)(1u << bit)) && cur_len < NodePrefs::PAGE_ORDER_LEN)
              p->page_order[cur_len++] = bit + 1;
          }
        }
        return;
      }
    }
    // Default: CLOCK FAVOURITES RECENT RADIO BT ADVERT [GPS] [SENSORS] SETTINGS TOOLS MESSAGES
    // SHUTDOWN is omitted from the explicit list (PAGE_ORDER_LEN = 11 leaves room for the
    // common case GPS+SENSORS+all-others) and appended by buildVisibleOrder's missing-page
    // fallback at the end of the navigation sequence.
    int j = 0;
    p->page_order[j++] = NodePrefs::HPB_CLOCK      + 1;
    p->page_order[j++] = NodePrefs::HPB_FAVOURITES + 1;
    p->page_order[j++] = NodePrefs::HPB_RECENT     + 1;
    p->page_order[j++] = NodePrefs::HPB_RADIO      + 1;
    p->page_order[j++] = NodePrefs::HPB_BLUETOOTH  + 1;
    p->page_order[j++] = NodePrefs::HPB_ADVERT     + 1;
#if ENV_INCLUDE_GPS == 1
    p->page_order[j++] = NodePrefs::HPB_GPS        + 1;
#endif
#if UI_SENSORS_PAGE == 1
    p->page_order[j++] = NodePrefs::HPB_SENSORS    + 1;
#endif
    p->page_order[j++] = NodePrefs::HPB_SETTINGS   + 1;
    p->page_order[j++] = NodePrefs::HPB_TOOLS      + 1;
    p->page_order[j++] = NodePrefs::HPB_QUICK_MSG  + 1;
    while (j < NodePrefs::PAGE_ORDER_LEN) p->page_order[j++] = 0;
    p->page_order_set = NodePrefs::PAGE_ORDER_MAGIC;
  }

  // Swaps item's page_order slot with its neighbour in the given direction (-1=earlier, +1=later).
  void movePageInOrder(int item, int delta, NodePrefs* p) {
    ensurePageOrderInit(p);
    int bit = homePageBitIndex(item);
    if (bit < 0) return;
    int cur = -1, total = 0;
    for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
      uint8_t v = p->page_order[i];
      if (v < 1 || v > NodePrefs::HPB_COUNT) break;
      if ((int)(v - 1) == bit) cur = i;
      total++;
    }
    if (cur < 0) return;
    int next = cur + delta;
    if (next < 0 || next >= total) return;
    uint8_t tmp = p->page_order[cur];
    p->page_order[cur] = p->page_order[next];
    p->page_order[next] = tmp;
  }

  bool isMsgSlot(int item) const {
    return item >= MSG_SLOT_0 && item <= MSG_SLOT_9;
  }

  int msgSlotIndex(int item) const {
    return item - MSG_SLOT_0;
  }

  void renderItem(DisplayDriver& display, int item, int y) {
    NodePrefs* p = _task->getNodePrefs();

    if (isSection(item)) {
      int si = sectionIndex(item);
      bool collapsed = (_collapsed >> si) & 1;
      bool sel = (item == _selected);
      display.setColor(DisplayDriver::LIGHT);
      display.drawSelectionRow(0, y - 1, display.width(), display.lineStep(), sel);
      display.setCursor(0, y);
      display.print(sel ? ">" : " ");
      display.setCursor(display.getCharWidth() + 2, y);
      display.print(collapsed ? "+" : "-");
      display.print(" ");
      display.print(sectionName(item));
      return;
    }

    bool sel = (item == _selected);
    display.drawSelectionRow(0, y - 1, display.width(), display.lineStep(), sel);

    display.setCursor(0, y);
    display.print(sel ? ">" : " ");
    display.setCursor(display.getCharWidth() + 2, y);

#if FEAT_BRIGHTNESS_SETTING
    if (item == BRIGHTNESS) {
      display.print("Bright");
      renderBar(display, display.valCol(), y, (p ? p->display_brightness : 2) + 1, 5);
    } else
#endif
    if (item == BUZZER) {
      display.print("Buzzer");
      display.setCursor(display.valCol(), y);
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
      renderBar(display, display.valCol(), y, _task->getBuzzerVolume() + 1, 5);
#else
      display.setCursor(display.valCol(), y);
      display.print("N/A");
#endif
    } else if (item == DM_MELODY) {
      display.print("DM sound");
      display.setCursor(display.valCol(), y);
      { uint8_t v = p ? p->notif_melody_dm : 0;
        display.print(SOUND_LABELS[v < SOUND_COUNT ? v : 0]); }
    } else if (item == CH_MELODY) {
      display.print("Ch sound");
      display.setCursor(display.valCol(), y);
      { uint8_t v = p ? p->notif_melody_ch : 0;
        display.print(SOUND_LABELS[v < SOUND_COUNT ? v : 0]); }
    } else if (item == AD_SOUND) {
      display.print("AD sound");
      display.setCursor(display.valCol(), y);
      { uint8_t v = p ? p->notif_melody_ad : 0;
        display.print(SOUND_LABELS[v < SOUND_COUNT ? v : 0]); }
    } else if (item == AD_SOUND_SCOPE) {
      display.print("AD scope");
      display.setCursor(display.valCol(), y);
      { uint8_t v = p ? p->advert_sound_scope : ADVERT_SOUND_SCOPE_ALL;
        display.print(AD_SCOPE_LABELS[v < AD_SCOPE_COUNT ? v : 0]); }
    } else if (isHomePage(item)) {
      if (p) ensurePageOrderInit(p);
      int pos = homePagePosition(item, p);
      if (pos > 0) {
        char pb[5]; snprintf(pb, sizeof(pb), "%2d ", pos);
        display.print(pb);
      }
      display.print(homePageLabel(item));
      display.setCursor(display.width() - 6 * display.getCharWidth(), y);
      if (!homePageToggleable(item))
        display.print("always");
      else
        display.print(homePageVisible(item, p) ? "ON" : "OFF");
    } else if (item == TX_POWER) {
      display.print("TX Pwr");
      char buf[8];
      snprintf(buf, sizeof(buf),"%ddBm", p ? p->tx_power_dbm : 0);
      display.setCursor(display.valCol(), y);
      display.print(buf);
    } else if (item == POWER_SAVE) {
      display.print("Pwr save");
      display.setCursor(display.valCol(), y);
      display.print((p && p->rx_powersave) ? "ON" : "OFF");
#if AUTO_OFF_MILLIS > 0
    } else if (item == AUTO_OFF) {
      display.print("AutoOff");
      display.setCursor(display.valCol(), y);
      display.print(AUTO_OFF_LABELS[autoOffIndex()]);
#endif
    } else if (item == AUTO_LOCK) {
      display.print("AutoLock");
      display.setCursor(display.valCol(), y);
      display.print((p && p->auto_lock) ? "ON" : "OFF");
    } else if (item == TIMEZONE) {
      display.print("TimeZone");
      char buf[8];
      int8_t tz = p ? p->tz_offset_hours : 0;
      if (tz >= 0) snprintf(buf, sizeof(buf),"UTC+%d", (int)tz);
      else         snprintf(buf, sizeof(buf),"UTC%d",  (int)tz);
      display.setCursor(display.valCol(), y);
      display.print(buf);
    } else if (item == LOW_BAT) {
      display.print("LowBat");
      display.setCursor(display.valCol(), y);
      display.print(LOW_BAT_LABELS[lowBatIndex()]);
    } else if (item == UNITS) {
      display.print("Units");
      display.setCursor(display.valCol(), y);
      display.print((p && p->units_imperial) ? "Imperial" : "Metric");
    } else if (item == BATT_DISPLAY) {
      display.print("BattDisp");
      display.setCursor(display.valCol(), y);
      uint8_t mode = p ? p->batt_display_mode : 0;
      display.print(BATT_DISPLAY_LABELS[mode < BATT_DISPLAY_COUNT ? mode : 0]);
#if FEAT_CLOCK_SECONDS_SETTING
    } else if (item == CLOCK_SECONDS) {
      display.print("Seconds");
      display.setCursor(display.valCol(), y);
      display.print((p && p->clock_hide_seconds) ? "OFF" : "ON");
#endif
    } else if (item == CLOCK_FORMAT) {
      display.print("Format");
      display.setCursor(display.valCol(), y);
      display.print((p && p->clock_12h) ? "12h" : "24h");
    } else if (item == FONT) {
      display.print("Font");
      display.setCursor(display.valCol(), y);
      display.print((p && p->use_lemon_font) ? "Lemon" : "Default");
#if FEAT_DISPLAY_ROTATION_SETTING
    } else if (item == ROTATION) {
      display.print("Rotation");
      display.setCursor(display.valCol(), y);
      { static const char* ROT_LABELS[] = { "0 deg", "90 deg", "180 deg", "270 deg" };
        uint8_t r = p ? (p->display_rotation & 3) : 0;
        display.print(ROT_LABELS[r]); }
#endif
#if FEAT_JOYSTICK_ROTATION_SETTING
    } else if (item == JOY_ROTATION) {
      display.print("Joystick");
      display.setCursor(display.valCol(), y);
      { static const char* ROT_LABELS[] = { "0 deg", "90 deg", "180 deg", "270 deg" };
        uint8_t r = p ? (p->joystick_rotation & 3) : 0;
        display.print(ROT_LABELS[r]); }
#endif
#if FEAT_FULL_REFRESH_SETTING
    } else if (item == EINK_FULL_REFRESH) {
      display.print("Full rfsh");
      display.setCursor(display.valCol(), y);
      { uint8_t idx = p ? p->eink_full_refresh_every : 0;
        if (idx >= EINK_FULL_REFRESH_COUNT) idx = 0;
        display.print(EINK_FULL_REFRESH_LABELS[idx]); }
#endif
    } else if (item == DM_FILTER) {
      display.print("DM");
      display.setCursor(display.valCol(), y);
      display.print((p && p->dm_show_all) ? "all" : "fav");
    } else if (item == CH_FILTER) {
      display.print("Channels");
      display.setCursor(display.valCol(), y);
      display.print((p && p->ch_fav_only) ? "fav" : "all");
    } else if (item == ROOM_FILTER) {
      display.print("Rooms");
      display.setCursor(display.valCol(), y);
      display.print((p && p->room_fav_only) ? "fav" : "all");
    } else if (isMsgSlot(item)) {
      int slot = msgSlotIndex(item);
      char label[5];
      snprintf(label, sizeof(label), "Q%d:", slot + 1);
      display.print(label);
      const char* tmpl = (p && p->custom_msgs[slot][0]) ? p->custom_msgs[slot] : "(empty)";
      int xm = 8 + display.getCharWidth() * 4;
      display.drawTextEllipsized(xm, y, display.width() - xm, tmpl);
    }
  }

  // Keyboard state for editing message slots
  int            _edit_slot;  // -1 = not editing, 0..9 = slot being edited
  KeyboardWidget* _kb;

public:
  SettingsScreen(UITask* task, KeyboardWidget* kb)
    : _task(task), _kb(kb), _selected(SECTION_DISPLAY), _scroll(0), _visible(4), _dirty(false), _edit_slot(-1) {
    buildVis();
  }


  void markClean() {
    _dirty = false;
    _collapsed = 0x7F;
    _selected = SECTION_DISPLAY;
    buildVis();
    _scroll = 0;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    if (_edit_slot >= 0) {
      return _kb->render(display);
    }

    int item_h  = display.lineStep();
    int start_y = display.listStart();
    _visible    = display.listVisible(item_h);

    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 0, "SETTINGS");
    display.fillRect(0, display.headerH() - display.sepH(), display.width(), display.sepH());

    for (int i = 0; i < _visible && (_scroll + i) < _vis_count; i++) {
      renderItem(display, _vis[_scroll + i], start_y + i * item_h);
    }

    // scroll indicators
    if (_scroll > 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - display.getCharWidth(), start_y);
      display.print("^");
    }
    if (_scroll + _visible < _vis_count) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - display.getCharWidth(), start_y + (_visible - 1) * item_h);
      display.print("v");
    }

    return 2000;
  }

  bool handleInput(char c) override {
    NodePrefs* p = _task->getNodePrefs();

    // Keyboard editing mode for message slots
    if (_edit_slot >= 0) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (p) {
          strncpy(p->custom_msgs[_edit_slot], _kb->buf, sizeof(p->custom_msgs[0]) - 1);
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
      int vi = visIndexOf(_selected);
      if (vi > 0) {
        vi--;
        _selected = _vis[vi];
        if (vi < _scroll) _scroll = vi;
      }
      return true;
    }
    if (c == KEY_DOWN) {
      int vi = visIndexOf(_selected);
      if (vi + 1 < _vis_count) {
        vi++;
        _selected = _vis[vi];
        if (vi >= _scroll + _visible) _scroll = vi - _visible + 1;
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

    if (enter && isSection(_selected)) {
      int si = sectionIndex(_selected);
      _collapsed ^= (1 << si);
      buildVis();
      int vi = visIndexOf(_selected);
      if (vi < _scroll) _scroll = vi;
      if (_visible > 0 && vi >= _scroll + _visible) _scroll = vi - _visible + 1;
      return true;
    }

#if FEAT_BRIGHTNESS_SETTING
    if (_selected == BRIGHTNESS) {
      uint8_t lvl = _task->getBrightnessLevel();
      if (right && lvl < 4) { _task->setBrightnessLevel(lvl + 1); _dirty = true; return true; }
      if (left  && lvl > 0) { _task->setBrightnessLevel(lvl - 1); _dirty = true; return true; }
      return right || left;
    }
#endif
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
      p->notif_melody_dm = (p->notif_melody_dm + (left ? SOUND_COUNT - 1 : 1)) % SOUND_COUNT;
      _dirty = true; return true;
    }
    if (_selected == CH_MELODY && p && (left || right || enter)) {
      p->notif_melody_ch = (p->notif_melody_ch + (left ? SOUND_COUNT - 1 : 1)) % SOUND_COUNT;
      _dirty = true; return true;
    }
    if (_selected == AD_SOUND && p && (left || right || enter)) {
      p->notif_melody_ad = (p->notif_melody_ad + (left ? SOUND_COUNT - 1 : 1)) % SOUND_COUNT;
      _dirty = true; return true;
    }
    if (_selected == AD_SOUND_SCOPE && p && (left || right || enter)) {
      p->advert_sound_scope ^= 1;
      _dirty = true; return true;
    }
    if (isHomePage(_selected) && p) {
      if (left || right) {
        movePageInOrder(_selected, left ? -1 : 1, p);
        _dirty = true;
        return true;
      }
      if (enter && homePageToggleable(_selected)) {
        if (!p->home_pages_mask) p->home_pages_mask = NodePrefs::HP_ALL;
        p->home_pages_mask ^= homePageBit(_selected);
        _dirty = true;
        return true;
      }
      return enter;
    }
    if (_selected == TX_POWER && p) {
      if (right && p->tx_power_dbm < 22) { p->tx_power_dbm++; _task->applyTxPower(); _dirty = true; return true; }
      if (left  && p->tx_power_dbm > 2)  { p->tx_power_dbm--; _task->applyTxPower(); _dirty = true; return true; }
    }
    if (_selected == POWER_SAVE && p && (left || right || enter)) {
      p->rx_powersave ^= 1;
      _task->applyPowerSave();
      _dirty = true;
      return true;
    }
#if AUTO_OFF_MILLIS > 0
    if (_selected == AUTO_OFF && p) {
      int idx = autoOffIndex();
      if (right) idx = (idx + 1) % AUTO_OFF_COUNT;
      if (left)  idx = (idx + AUTO_OFF_COUNT - 1) % AUTO_OFF_COUNT;
      if (left || right) { p->auto_off_secs = AUTO_OFF_OPTS[idx]; _dirty = true; return true; }
    }
#endif
    if (_selected == AUTO_LOCK && p && (left || right || enter)) {
      p->auto_lock ^= 1;
      _dirty = true;
      return true;
    }
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
    if (_selected == UNITS && p && (left || right || enter)) {
      p->units_imperial ^= 1;
      _dirty = true;
      return true;
    }
    if (_selected == BATT_DISPLAY && p) {
      int idx = p->batt_display_mode < BATT_DISPLAY_COUNT ? p->batt_display_mode : 0;
      if (right) idx = (idx + 1) % BATT_DISPLAY_COUNT;
      if (left)  idx = (idx + BATT_DISPLAY_COUNT - 1) % BATT_DISPLAY_COUNT;
      if (left || right) { p->batt_display_mode = idx; _dirty = true; return true; }
    }
#if FEAT_CLOCK_SECONDS_SETTING
    if (_selected == CLOCK_SECONDS && p && (left || right || enter)) {
      p->clock_hide_seconds ^= 1;
      _dirty = true;
      return true;
    }
#endif
    if (_selected == CLOCK_FORMAT && p && (left || right || enter)) {
      p->clock_12h ^= 1;
      _dirty = true;
      return true;
    }
    if (_selected == FONT && p && (left || right || enter)) {
      p->use_lemon_font ^= 1;
      _task->applyFont();
      _dirty = true;
      return true;
    }
#if FEAT_DISPLAY_ROTATION_SETTING
    if (_selected == ROTATION && p && (left || right || enter)) {
      p->display_rotation = (p->display_rotation + (left ? 3 : 1)) & 3;
      _task->applyRotation();
      _dirty = true;
      return true;
    }
#endif
#if FEAT_JOYSTICK_ROTATION_SETTING
    if (_selected == JOY_ROTATION && p && (left || right || enter)) {
      p->joystick_rotation = (p->joystick_rotation + (left ? 3 : 1)) & 3;
      _dirty = true;
      return true;
    }
#endif
#if FEAT_FULL_REFRESH_SETTING
    if (_selected == EINK_FULL_REFRESH && p && (left || right || enter)) {
      int idx = p->eink_full_refresh_every;
      if (idx >= EINK_FULL_REFRESH_COUNT) idx = 0;
      idx = (idx + (left ? EINK_FULL_REFRESH_COUNT - 1 : 1)) % EINK_FULL_REFRESH_COUNT;
      p->eink_full_refresh_every = idx;
      _task->applyFullRefreshInterval();
      _dirty = true;
      return true;
    }
#endif
    if (_selected == DM_FILTER && p && (left || right || enter)) {
      p->dm_show_all = p->dm_show_all ? 0 : 1;
      _dirty = true;
      return true;
    }
    if (_selected == CH_FILTER && p && (left || right || enter)) {
      p->ch_fav_only = p->ch_fav_only ? 0 : 1;
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
      // Bound to the custom_msgs store (140 B) so the wider keyboard buffer
      // can't overflow it on save.
      _kb->begin(p ? p->custom_msgs[slot] : "",
                p ? (int)sizeof(p->custom_msgs[slot]) - 1 : KB_MAX_LEN);
      kbAddSensorPlaceholders(*_kb, &sensors);
      return true;
    }
    return false;
  }
};

#if AUTO_OFF_MILLIS > 0
const uint16_t SettingsScreen::AUTO_OFF_OPTS[5]   = { 5, 15, 30, 60, 0 };
const char*    SettingsScreen::AUTO_OFF_LABELS[5]  = { "5s", "15s", "30s", "60s", "never" };
#endif
const uint16_t SettingsScreen::LOW_BAT_OPTS[7]   = { 0, 3000, 3100, 3200, 3300, 3400, 3500 };
const char*    SettingsScreen::LOW_BAT_LABELS[7]  = { "off", "3.0V", "3.1V", "3.2V", "3.3V", "3.4V", "3.5V" };
const char*    SettingsScreen::BATT_DISPLAY_LABELS[3] = { "icon", "%", "V" };
const char*    SettingsScreen::SOUND_LABELS[4] = { "built-in", "M1", "M2", "None" };
const char*    SettingsScreen::AD_SCOPE_LABELS[2] = { "All", "Zero-hop" };
#if FEAT_FULL_REFRESH_SETTING
const char* SettingsScreen::EINK_FULL_REFRESH_LABELS[5] = { "off", "5", "10", "20", "30" };
#endif
