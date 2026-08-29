#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SensorPlaceholders.h is defined.

#include "../Features.h"
#include "../RadioPresets.h"
#include "RadioParamsEditor.h"
#include "RadioPresetPicker.h"
#include "AccordionList.h"

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
    HOME_CLOCK, HOME_FAVOURITES, HOME_RADIO, HOME_BT, HOME_ADVERT,
#if ENV_INCLUDE_GPS == 1
    HOME_GPS,
#endif
#if UI_SENSORS_PAGE == 1
    HOME_SENSORS,
#endif
    HOME_SETTINGS, HOME_QUICK_MSG,
    HOME_TOOLS, HOME_SHUTDOWN, HOME_MAP,
    // Radio section
    SECTION_RADIO,
    TX_POWER,
    RADIO_PRESET,
    CUSTOM_FREQ, CUSTOM_SF, CUSTOM_BW, CUSTOM_CR,
    POWER_SAVE,
    TX_APC,
    SCOPE_NAME,
    // System section
    SECTION_SYSTEM,
    DEVICE_NAME,
    TIMEZONE,
    LOW_BAT,
#if ENV_INCLUDE_GPS == 1
    GPS_DUTY_CYCLE,
#endif
    UNITS,
    REBOOT,
    // Keyboard section
    SECTION_KEYBOARD,
    KEYBOARD_TYPE,
    KEYBOARD_MAIN_ALPHABET,
    KEYBOARD_ALPHABET,
#if defined(CARDKB_I2C)
    KEYBOARD_CARDKB_COMPACT,
#endif
    // Contacts section
    SECTION_CONTACTS, DM_FILTER, CH_FILTER, ROOM_FILTER,
    // Messages section
    SECTION_MESSAGES,
    DM_RESEND,
    MSG_SLOT_0, MSG_SLOT_1, MSG_SLOT_2, MSG_SLOT_3, MSG_SLOT_4,
    MSG_SLOT_5, MSG_SLOT_6, MSG_SLOT_7, MSG_SLOT_8, MSG_SLOT_9,
    Count
  };

  // Cursor + scroll, fold state and the flattened visible list are owned by the
  // shared AccordionList helper. We keep only the section→SettingItem mapping it
  // needs (sections are walked once from the enum, honouring the #if guards).
  int  _selected = 0;   // SettingItem under the cursor, resolved per input/render
  int  _reserve = 0;    // right-edge px reserved for the scrollbar (0 when list fits)
  bool _dirty = false;

  AccordionList _acc;
  static const int NUM_SECTIONS = 8;
  static const int MAX_PER_SEC  = 16;
  uint8_t _sec_items[NUM_SECTIONS][MAX_PER_SEC]; // SettingItem per (section, row)
  uint8_t _sec_count[NUM_SECTIONS];
  uint8_t _sec_header[NUM_SECTIONS];             // the SECTION_* enum for each section
  int     _num_sections = 0;

#if AUTO_OFF_MILLIS > 0
  static const uint16_t AUTO_OFF_OPTS[5];
  static const char* AUTO_OFF_LABELS[5];
  static const int AUTO_OFF_COUNT = 5;
#endif
  static const uint16_t LOW_BAT_OPTS[7];
  static const char* LOW_BAT_LABELS[7];
  static const int LOW_BAT_COUNT = 7;
#if ENV_INCLUDE_GPS == 1
  // GPS duty-cycle sleep window: how long GPS naps between fix acquisitions.
  // "OFF" (0) keeps it continuously on, today's behaviour. Backed by
  // NodePrefs::gps_interval, seconds.
  static const uint32_t GPS_DUTY_OPTS[6];
  static const char* GPS_DUTY_LABELS[6];
  static const int GPS_DUTY_COUNT = 6;
  int gpsDutyIndex() {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    for (int i = 0; i < GPS_DUTY_COUNT; i++)
      if (GPS_DUTY_OPTS[i] == p->gps_interval) return i;
    return 0;
  }
#endif
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

  // The companion's own radio fields, as the shared preset picker's target
  // (Tools › Repeater points the same picker at the dedicated repeater profile).
  RadioPresetPicker::Target radioTarget(NodePrefs* p) const {
    return { &p->freq, &p->bw, &p->sf, &p->cr };
  }

  // Value column start, pulled left by the scrollbar gutter so right-side
  // values never render under the indicator when the list scrolls.
  int valCol(DisplayDriver& display) const { return display.valCol() - _reserve; }

  // Shared 0/90/180/270 labels for display + joystick rotation.
  static const char* rotLabel(uint8_t r) {
    static const char* const L[] = { "0 deg", "90 deg", "180 deg", "270 deg" };
    return L[r & 3];
  }

  void renderBar(DisplayDriver& display, int x, int y, int value, int max_val) {
    const int gap     = 2;
    const int avail   = display.width() - x - _reserve;
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
           item == SECTION_KEYBOARD ||
           item == SECTION_CONTACTS || item == SECTION_MESSAGES;
  }

  const char* sectionName(int item) const {
    if (item == SECTION_DISPLAY)    return "Display";
    if (item == SECTION_SOUND)      return "Sound";
    if (item == SECTION_HOME_PAGES) return "Home Pages";
    if (item == SECTION_RADIO)      return "Radio";
    if (item == SECTION_SYSTEM)     return "System";
    if (item == SECTION_KEYBOARD)   return "Keyboard";
    if (item == SECTION_CONTACTS)   return "Contacts";
    if (item == SECTION_MESSAGES)   return "Messages";
    return "";
  }

  // Walk the SettingItem enum once, bucketing items under their section header.
  // #if-guarded items need no special handling — they simply aren't in the enum.
  void buildSections() {
    int cur = -1;
    for (int i = 0; i < (int)Count; i++) {
      if (isSection(i)) {
        if (++cur >= NUM_SECTIONS) break;
        _sec_header[cur] = (uint8_t)i;
        _sec_count[cur]  = 0;
      } else if (cur >= 0 && _sec_count[cur] < MAX_PER_SEC) {
        _sec_items[cur][_sec_count[cur]++] = (uint8_t)i;
      }
    }
    _num_sections = cur + 1;
  }

  // (Re)load the section sizes into the accordion (folds all, resets the cursor).
  void resetList() {
    uint8_t sizes[NUM_SECTIONS];
    for (int i = 0; i < _num_sections; i++) sizes[i] = _sec_count[i];
    _acc.begin(sizes, _num_sections);
  }

  // Resolve the accordion's selected row to a SettingItem (header → SECTION_*).
  int currentItem() const {
    const AccordionList::Row& r = _acc.selected();
    return (r.item < 0) ? _sec_header[r.sec] : _sec_items[r.sec][r.item];
  }

  bool isHomePage(int item) const {
    return item == HOME_CLOCK    || item == HOME_RADIO      || item == HOME_BT      ||
           item == HOME_ADVERT   || item == HOME_TOOLS      ||
           item == HOME_SHUTDOWN || item == HOME_SETTINGS   || item == HOME_QUICK_MSG ||
           item == HOME_FAVOURITES || item == HOME_MAP
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
    if (item == HOME_MAP)       return NodePrefs::HPB_MAP;
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
          // Insert FAVOURITES right after CLOCK. Real orders are shorter than
          // PAGE_ORDER_LEN so there's room; only a pathologically full order would
          // drop its last entry, which buildVisibleOrder's fallback re-appends.
          int insert_at = clock_at + 1;
          // Guard against a saved order with all PAGE_ORDER_LEN slots already
          // valid and CLOCK in the last one: insert_at would be PAGE_ORDER_LEN,
          // one past the array, and the shift loop below wouldn't run (tail is
          // clamped to the last index) to catch it -- the write would land one
          // byte past page_order, into whatever NodePrefs field follows.
          if (insert_at < NodePrefs::PAGE_ORDER_LEN) {
            int tail = (len < NodePrefs::PAGE_ORDER_LEN) ? len : NodePrefs::PAGE_ORDER_LEN - 1;
            for (int i = tail; i > insert_at; i--) p->page_order[i] = p->page_order[i - 1];
            p->page_order[insert_at] = NodePrefs::HPB_FAVOURITES + 1;
          }
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
          // Every page has a slot now (PAGE_ORDER_LEN == HPB_COUNT), so all pages
          // are required — any missing from a stale saved order (SHUTDOWN and MAP
          // for pre-0x0019 upgraders) is appended into the free tail slots below.
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
            NodePrefs::HPB_SETTINGS, NodePrefs::HPB_MAP, NodePrefs::HPB_TOOLS,
            NodePrefs::HPB_QUICK_MSG, NodePrefs::HPB_SHUTDOWN,
          };
          for (int ri = 0; ri < (int)(sizeof(REQUIRED)/sizeof(REQUIRED[0])); ri++) {
            uint8_t bit = REQUIRED[ri];
            if (!(present & (uint16_t)(1u << bit)) && cur_len < NodePrefs::PAGE_ORDER_LEN)
              p->page_order[cur_len++] = bit + 1;
          }
        }
        return;
      }
    }
    // Default: CLOCK FAVOURITES RECENT RADIO BT ADVERT [GPS] [SENSORS] SETTINGS
    // MAP TOOLS MESSAGES SHUTDOWN — mirrors the home-carousel enum order. Every
    // page has an explicit slot now (PAGE_ORDER_LEN == HPB_COUNT).
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
    p->page_order[j++] = NodePrefs::HPB_MAP        + 1;
    p->page_order[j++] = NodePrefs::HPB_TOOLS      + 1;
    p->page_order[j++] = NodePrefs::HPB_QUICK_MSG  + 1;
    p->page_order[j++] = NodePrefs::HPB_SHUTDOWN   + 1;
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

  void renderItem(DisplayDriver& display, int item, int y, bool sel) {
    NodePrefs* p = _task->getNodePrefs();

    drawRowSelection(display, y, sel, _reserve);

    display.setCursor(2, y);

#if FEAT_BRIGHTNESS_SETTING
    if (item == BRIGHTNESS) {
      display.print("Bright");
      renderBar(display, valCol(display), y, (p ? p->display_brightness : 2) + 1, 5);
    } else
#endif
    if (item == BUZZER) {
      display.print("Buzzer");
      display.setCursor(valCol(display), y);
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
      renderBar(display, valCol(display), y, _task->getBuzzerVolume() + 1, 5);
#else
      display.setCursor(valCol(display), y);
      display.print("N/A");
#endif
    } else if (item == DM_MELODY) {
      display.print("DM sound");
      display.setCursor(valCol(display), y);
      { uint8_t v = p ? p->notif_melody_dm : 0;
        display.print(SOUND_LABELS[v < SOUND_COUNT ? v : 0]); }
    } else if (item == CH_MELODY) {
      display.print("Ch sound");
      display.setCursor(valCol(display), y);
      { uint8_t v = p ? p->notif_melody_ch : 0;
        display.print(SOUND_LABELS[v < SOUND_COUNT ? v : 0]); }
    } else if (item == AD_SOUND) {
      display.print("AD sound");
      display.setCursor(valCol(display), y);
      { uint8_t v = p ? p->notif_melody_ad : 0;
        display.print(SOUND_LABELS[v < SOUND_COUNT ? v : 0]); }
    } else if (item == AD_SOUND_SCOPE) {
      display.print("AD scope");
      display.setCursor(valCol(display), y);
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
      display.setCursor(display.width() - 6 * display.getCharWidth() - _reserve, y);
      if (!homePageToggleable(item))
        display.print("always");
      else
        display.print(homePageVisible(item, p) ? "ON" : "OFF");
    } else if (item == TX_POWER) {
      display.print("TX Pwr");
      char buf[8];
      snprintf(buf, sizeof(buf),"%ddBm", p ? p->tx_power_dbm : 0);
      display.setCursor(valCol(display), y);
      display.print(buf);
    } else if (item == RADIO_PRESET) {
      display.print("Preset");
      const char* name = p ? _picker.currentName(p, radioTarget(p)) : "Custom";
      int xc = valCol(display);
      display.drawTextEllipsized(xc, y, display.width() - xc - _reserve, name);
    } else if (item == CUSTOM_FREQ) {
      display.print("Freq");
      int xc = valCol(display);
      if (sel && _editor.active()) {
        _editor.render(display, xc, y);
      } else {
        char buf[10];
        snprintf(buf, sizeof(buf), "%.3f", p ? p->freq : 0.0f);
        display.setCursor(xc, y);
        display.print(buf);
      }
    } else if (item == CUSTOM_SF) {
      display.print("SF");
      char buf[6];
      snprintf(buf, sizeof(buf), "%d", p ? (int)p->sf : 0);
      display.setCursor(valCol(display), y);
      display.print(buf);
    } else if (item == CUSTOM_BW) {
      display.print("BW");
      char buf[10];
      snprintf(buf, sizeof(buf), "%.1f", p ? p->bw : 0.0f);
      display.setCursor(valCol(display), y);
      display.print(buf);
    } else if (item == CUSTOM_CR) {
      display.print("CR");
      char buf[6];
      snprintf(buf, sizeof(buf), "%d", p ? (int)p->cr : 0);
      display.setCursor(valCol(display), y);
      display.print(buf);
    } else if (item == POWER_SAVE) {
      display.print("Pwr save");
      display.setCursor(valCol(display), y);
      // Forced off (and locked) while the repeater is on — it must hear all traffic.
      if (p && p->client_repeat) display.print("--");
      else display.print((p && p->rx_powersave) ? "ON" : "OFF");
    } else if (item == TX_APC) {
      display.print("Auto pwr");
      display.setCursor(valCol(display), y);
      // Suppressed (and locked) while repeating — a repeater holds full TX power.
      if (p && p->client_repeat) display.print("--");
      else display.print((p && p->tx_apc) ? "ON" : "OFF");
    } else if (item == SCOPE_NAME) {
      display.print("Scope");
      int vx = valCol(display);
      display.drawTextEllipsized(vx, y, display.width() - vx - _reserve,
                                  (p && p->default_scope_name[0]) ? p->default_scope_name : "(none)");
#if AUTO_OFF_MILLIS > 0
    } else if (item == AUTO_OFF) {
      display.print("AutoOff");
      display.setCursor(valCol(display), y);
      display.print(AUTO_OFF_LABELS[autoOffIndex()]);
#endif
    } else if (item == AUTO_LOCK) {
      display.print("AutoLock");
      display.setCursor(valCol(display), y);
      display.print((p && p->auto_lock) ? "ON" : "OFF");
    } else if (item == TIMEZONE) {
      display.print("TimeZone");
      char buf[8];
      int8_t tz = p ? p->tz_offset_hours : 0;
      if (tz >= 0) snprintf(buf, sizeof(buf),"UTC+%d", (int)tz);
      else         snprintf(buf, sizeof(buf),"UTC%d",  (int)tz);
      display.setCursor(valCol(display), y);
      display.print(buf);
    } else if (item == LOW_BAT) {
      display.print("LowBat");
      display.setCursor(valCol(display), y);
      display.print(LOW_BAT_LABELS[lowBatIndex()]);
#if ENV_INCLUDE_GPS == 1
    } else if (item == GPS_DUTY_CYCLE) {
      display.print("GPS pwr");
      display.setCursor(valCol(display), y);
      display.print(GPS_DUTY_LABELS[gpsDutyIndex()]);
#endif
    } else if (item == UNITS) {
      display.print("Units");
      display.setCursor(valCol(display), y);
      display.print((p && p->units_imperial) ? "Imperial" : "Metric");
    } else if (item == DEVICE_NAME) {
      display.print("Name");
      int vx = valCol(display);
      display.drawTextEllipsized(vx, y, display.width() - vx - _reserve, the_mesh.getNodeName());
    } else if (item == REBOOT) {
      display.print("Reboot");   // action row: Enter reboots this device
    } else if (item == KEYBOARD_TYPE) {
      display.print("Type");
      display.setCursor(valCol(display), y);
      display.print((p && p->keyboard_type) ? "T9" : "ABC");
    } else if (item == KEYBOARD_MAIN_ALPHABET) {
      display.print("Main");
      display.setCursor(valCol(display), y);
      display.print(NodePrefs::keyboardAlphabetLabel(p ? p->keyboard_main_alphabet : 0));
    } else if (item == KEYBOARD_ALPHABET) {
      display.print("Additional");
      display.setCursor(valCol(display), y);
      display.print(NodePrefs::keyboardAlphabetLabel(p ? p->keyboard_alt_alphabet : 0));
#if defined(CARDKB_I2C)
    } else if (item == KEYBOARD_CARDKB_COMPACT) {
      display.print("Ext. KB");
      display.setCursor(valCol(display), y);
      display.print((p && p->keyboard_cardkb_compact) ? "Compact" : "Full");
#endif
    } else if (item == BATT_DISPLAY) {
      display.print("BattDisp");
      display.setCursor(valCol(display), y);
      uint8_t mode = p ? p->batt_display_mode : 0;
      display.print(BATT_DISPLAY_LABELS[mode < BATT_DISPLAY_COUNT ? mode : 0]);
#if FEAT_CLOCK_SECONDS_SETTING
    } else if (item == CLOCK_SECONDS) {
      display.print("Seconds");
      display.setCursor(valCol(display), y);
      display.print((p && p->clock_hide_seconds) ? "OFF" : "ON");
#endif
    } else if (item == CLOCK_FORMAT) {
      display.print("Format");
      display.setCursor(valCol(display), y);
      display.print((p && p->clock_12h) ? "12h" : "24h");
#if FEAT_DISPLAY_ROTATION_SETTING
    } else if (item == ROTATION) {
      display.print("Rotation");
      display.setCursor(valCol(display), y);
      display.print(rotLabel(p ? p->display_rotation : 0));
#endif
#if FEAT_JOYSTICK_ROTATION_SETTING
    } else if (item == JOY_ROTATION) {
      display.print("Joystick");
      display.setCursor(valCol(display), y);
      display.print(rotLabel(p ? p->joystick_rotation : 0));
#endif
#if FEAT_FULL_REFRESH_SETTING
    } else if (item == EINK_FULL_REFRESH) {
      display.print("Full rfsh");
      display.setCursor(valCol(display), y);
      { uint8_t idx = p ? p->eink_full_refresh_every : 0;
        if (idx >= EINK_FULL_REFRESH_COUNT) idx = 0;
        display.print(EINK_FULL_REFRESH_LABELS[idx]); }
#endif
    } else if (item == DM_FILTER) {
      display.print("DM");
      display.setCursor(valCol(display), y);
      display.print((p && p->dm_show_all) ? "all" : "fav");
    } else if (item == CH_FILTER) {
      display.print("Channels");
      display.setCursor(valCol(display), y);
      display.print((p && p->ch_fav_only) ? "fav" : "all");
    } else if (item == ROOM_FILTER) {
      display.print("Rooms");
      display.setCursor(valCol(display), y);
      display.print((p && p->room_fav_only) ? "fav" : "all");
    } else if (item == DM_RESEND) {
      display.print("Resend");
      display.setCursor(valCol(display), y);
      uint8_t n = p ? p->dm_resend_count : 0;
      if (n == 0) display.print("OFF");
      else { char buf[6]; snprintf(buf, sizeof(buf), "%ux", (unsigned)n); display.print(buf); }
    } else if (isMsgSlot(item)) {
      int slot = msgSlotIndex(item);
      char label[5];
      snprintf(label, sizeof(label), "Q%d:", slot + 1);
      display.print(label);
      const char* tmpl = (p && p->custom_msgs[slot][0]) ? p->custom_msgs[slot] : "(empty)";
      int xm = 8 + display.getCharWidth() * 4;
      display.drawTextEllipsized(xm, y, display.width() - xm - _reserve, tmpl);
    }
  }

  // Keyboard state for editing message slots
  int            _edit_slot = -1;  // -1 = not editing, 0..9 = slot being edited
  bool           _edit_name = false;  // editing DEVICE_NAME via the keyboard
  bool           _edit_scope = false; // editing SCOPE_NAME via the keyboard
  KeyboardWidget* _kb;

  // Radio preset picker — names are too long for the value column, so Enter on
  // RADIO_PRESET opens it as a full-width scrollable list instead of cycling.
  // Shared with Tools › Repeater (see RadioPresetPicker.h). _picker.saving means
  // _kb is open to name a new preset, not a message slot.
  RadioPresetPicker _picker;

  // Manual radio-parameter editing (digit-by-digit Freq editor + SF/BW/CR
  // stepping), shared with Tools › Repeater — see RadioParamsEditor.h.
  RadioParamsEditor _editor;

public:
  SettingsScreen(UITask* task, KeyboardWidget* kb)
    : _task(task), _kb(kb) {
    buildSections();
    resetList();
  }


  void onShow() override {
    _dirty = false;
    _edit_name = false;
    _edit_scope = false;
    resetList();
    _editor.freq.active = false;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    if (_edit_slot >= 0 || _edit_name || _edit_scope || _picker.saving) {
      return _kb->render(display);
    }

    display.drawCenteredHeader("SETTINGS");

    _acc.render(display,
      // Section header: "[+/-] Name"
      [&](int sec, int y, bool sel, int reserve, bool collapsed) {
        _reserve = reserve;
        display.setColor(DisplayDriver::LIGHT);
        drawRowSelection(display, y, sel, reserve);
        display.setCursor(2, y);
        display.print(collapsed ? "+" : "-");
        display.print(" ");
        display.print(sectionName(_sec_header[sec]));
      },
      // Item row
      [&](int sec, int item, int y, bool sel, int reserve) {
        _reserve = reserve;
        renderItem(display, _sec_items[sec][item], y, sel);
      });

    if (_picker.menu.active) _picker.menu.render(display);

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

    // Keyboard editing mode for the device name
    if (_edit_name) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (p) {
          strncpy(p->node_name, _kb->buf, sizeof(p->node_name) - 1);
          p->node_name[sizeof(p->node_name) - 1] = '\0';
          _dirty = true;   // savePrefsIfDirty on exit; getNodeName()/self-advert read node_name live
        }
        _edit_name = false;
      } else if (res == KeyboardWidget::CANCELLED) {
        _edit_name = false;
      }
      return true;
    }

    // Keyboard editing mode for the scope name
    if (_edit_scope) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        the_mesh.setPrimaryScope(_kb->buf);
        _dirty = true;
        _edit_scope = false;
      } else if (res == KeyboardWidget::CANCELLED) {
        _edit_scope = false;
      }
      return true;
    }

    // Digit-by-digit Freq editor
    if (_editor.active()) {
      if (_editor.handleFreqInput(c) && p) { _task->applyRadioParams(); _dirty = true; }
      return true;
    }

    // Keyboard editing mode for naming a new saved preset
    if (_picker.saving) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (p && _picker.save(p, _kb->buf, radioTarget(p))) {
          _dirty = true;
          _task->showAlert("Preset saved", 800);
        }
        _picker.saving = false;
      } else if (res == KeyboardWidget::CANCELLED) {
        _picker.saving = false;
      }
      return true;
    }

    // Radio preset popup (and its "delete a saved preset" sub-list)
    if (_picker.menu.active) {
      auto res = _picker.menu.handleInput(c);
      if (res == PopupMenu::SELECTED && p) {
        switch (_picker.onSelected(_picker.menu.selectedIndex(), p, radioTarget(p))) {
          case RadioPresetPicker::START_SAVE:
            _kb->begin("", (int)sizeof(p->user_radio_presets[0].name) - 1);
            _kb->clearPlaceholders();   // {loc}/{time} are for messages, not preset names
            break;
          case RadioPresetPicker::APPLIED:
            _task->applyRadioParams();
            _dirty = true;
            break;
          case RadioPresetPicker::DELETED:
            _dirty = true;
            _task->showAlert("Preset deleted", 800);
            break;
          case RadioPresetPicker::NONE:
            break;
        }
      } else if (res == PopupMenu::CANCELLED) {
        _picker.deleting = false;
      }
      return true;
    }

    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoHomeScreen();
      return true;
    }

    // Up/down navigation and section fold/unfold live in the shared helper.
    // Enter on an item returns ACTIVATED and falls through to the per-item logic
    // below; left/right are ignored by the helper and likewise fall through.
    AccordionList::Result ar = _acc.handleInput(c);
    if (ar == AccordionList::HANDLED) return true;
    _selected = currentItem();

    bool right = keyIsNext(c);
    bool left  = keyIsPrev(c);
    bool enter = (c == KEY_ENTER);

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
    if (_selected == RADIO_PRESET && p && enter) {
      _picker.open(p, radioTarget(p), "Radio Preset");
      return true;
    }
    // Enter Freq's digit-by-digit editor. Bounds come from the radio driver
    // itself (RadioLib's own validated range for this chip) so a digit can never
    // be nudged to a value setFrequency() would reject.
    if (_selected == CUSTOM_FREQ && p && enter) {
      float min_mhz, max_mhz;
      radio_driver.getFreqBounds(min_mhz, max_mhz);
      _editor.beginFreq(p->freq, min_mhz, max_mhz);
      return true;
    }
    int dir = right ? 1 : (left ? -1 : 0);
    if (_selected == CUSTOM_SF && p && dir && RadioParamsEditor::stepSF(p->sf, dir)) { _task->applyRadioParams(); _dirty = true; return true; }
    if (_selected == CUSTOM_BW && p && dir && RadioParamsEditor::stepBW(p->bw, dir)) { _task->applyRadioParams(); _dirty = true; return true; }
    if (_selected == CUSTOM_CR && p && dir && RadioParamsEditor::stepCR(p->cr, dir)) { _task->applyRadioParams(); _dirty = true; return true; }
    if (_selected == POWER_SAVE && p && (left || right || enter)) {
      if (p->client_repeat) { _task->showAlert("Off while repeating", 900); return true; }
      p->rx_powersave ^= 1;
      _task->applyPowerSave();
      _dirty = true;
      return true;
    }
    if (_selected == TX_APC && p && (left || right || enter)) {
      if (p->client_repeat) { _task->showAlert("Off while repeating", 900); return true; }
      p->tx_apc ^= 1;
      _task->applyApc();
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
#if ENV_INCLUDE_GPS == 1
    if (_selected == GPS_DUTY_CYCLE && p && (left || right)) {
      int idx = gpsDutyIndex();
      if (right) idx = (idx + 1) % GPS_DUTY_COUNT;
      if (left)  idx = (idx + GPS_DUTY_COUNT - 1) % GPS_DUTY_COUNT;
      p->gps_interval = GPS_DUTY_OPTS[idx];
      _task->applyGpsInterval();
      _dirty = true;
      return true;
    }
#endif
    if (_selected == UNITS && p && (left || right || enter)) {
      p->units_imperial ^= 1;
      _dirty = true;
      return true;
    }
    if (_selected == DEVICE_NAME && p && enter) {
      _edit_name = true;
      _kb->begin(the_mesh.getNodeName(), (int)sizeof(p->node_name) - 1);
      _kb->clearPlaceholders();   // a device name is literal, not a message
      return true;
    }
    if (_selected == SCOPE_NAME && p && enter) {
      _edit_scope = true;
      _kb->begin(p->default_scope_name, (int)sizeof(p->default_scope_name) - 1);
      _kb->clearPlaceholders();   // a scope name is literal, not a message
      return true;
    }
    if (_selected == REBOOT && enter) {
      _task->savePrefsIfDirty(_dirty);   // don't lose pending edits across the restart
      _task->showAlert("Rebooting...", 800);
      board.reboot();
      return true;
    }
    if (_selected == KEYBOARD_TYPE && p && (left || right || enter)) {
      p->keyboard_type ^= 1;
      _dirty = true;
      return true;
    }
    if (_selected == KEYBOARD_MAIN_ALPHABET && p && (left || right || enter)) {
      int idx = p->keyboard_main_alphabet;
      if (right || enter) idx = (idx + 1) % NodePrefs::KB_ALPHABET_COUNT;
      else if (left)      idx = (idx + NodePrefs::KB_ALPHABET_COUNT - 1) % NodePrefs::KB_ALPHABET_COUNT;
      p->keyboard_main_alphabet = (uint8_t)idx;
      _dirty = true;
      return true;
    }
    if (_selected == KEYBOARD_ALPHABET && p && (left || right || enter)) {
      int idx = p->keyboard_alt_alphabet;
      if (right || enter) idx = (idx + 1) % NodePrefs::KB_ALPHABET_COUNT;
      else if (left)      idx = (idx + NodePrefs::KB_ALPHABET_COUNT - 1) % NodePrefs::KB_ALPHABET_COUNT;
      p->keyboard_alt_alphabet = (uint8_t)idx;
      _dirty = true;
      return true;
    }
#if defined(CARDKB_I2C)
    if (_selected == KEYBOARD_CARDKB_COMPACT && p && (left || right || enter)) {
      p->keyboard_cardkb_compact ^= 1;
      _dirty = true;
      return true;
    }
#endif
    if (_selected == DM_RESEND && p) {
      int n = p->dm_resend_count;
      if (right || enter) n = (n + 1) % 6;          // 0..5, wraps
      else if (left)      n = (n + 5) % 6;
      if (left || right || enter) { p->dm_resend_count = (uint8_t)n; _dirty = true; return true; }
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
const char*    SettingsScreen::LOW_BAT_LABELS[7]  = { "OFF", "3.0V", "3.1V", "3.2V", "3.3V", "3.4V", "3.5V" };
#if ENV_INCLUDE_GPS == 1
const uint32_t SettingsScreen::GPS_DUTY_OPTS[6]   = { 0, 60, 300, 900, 1800, 3600 };
const char*    SettingsScreen::GPS_DUTY_LABELS[6] = { "OFF", "1 min", "5 min", "15 min", "30 min", "1 h" };
#endif
const char*    SettingsScreen::BATT_DISPLAY_LABELS[3] = { "icon", "%", "V" };
const char*    SettingsScreen::SOUND_LABELS[4] = { "built-in", "M1", "M2", "None" };
const char*    SettingsScreen::AD_SCOPE_LABELS[2] = { "All", "Zero-hop" };
#if FEAT_FULL_REFRESH_SETTING
const char* SettingsScreen::EINK_FULL_REFRESH_LABELS[5] = { "OFF", "5", "10", "20", "30" };
#endif
