#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SensorPlaceholders.h is defined.

#include "../Features.h"
#include "../RadioPresets.h"
#include "RadioParamsEditor.h"
#include "RadioPresetPicker.h"
#include "AccordionList.h"
#include "PopupMenu.h"   // scope list management's per-row action menu
#include "InfoKit.h"     // value / switch rows
#include "../ui-core/Battery.h"   // the Battery curve row

class SettingsScreen : public UIScreen {
  UITask* _task;

  enum SettingItem {
    // Display section
    SECTION_DISPLAY,
    SCHEMA_DISPLAY,   // the schema's display and time settings (ui-core/SettingsSchema.h)
#if FEAT_DISPLAY_ROTATION_SETTING
    ROTATION,
#endif
#if FEAT_JOYSTICK_ROTATION_SETTING
    JOY_ROTATION,
#endif
#if FEAT_FULL_REFRESH_SETTING
    EINK_FULL_REFRESH,
    UI_TEXT_SCALE,    // Text size: 1x..5x the 6x9 font
#if STATUS_ICONS_SCALED
    UI_ICON_SCALE,    // Icon size: Auto, 1x..8x
#endif
#endif
    // Sound section
    SECTION_SOUND,
    BUZZER,
    SCHEMA_SOUND,     // volume, quiet hours, what plays for what
    // Home pages section
    SECTION_HOME_PAGES,
    HOME_CLOCK, HOME_FAVOURITES, HOME_STATUS, HOME_BT, HOME_ADVERT,
    HOME_SETTINGS, HOME_QUICK_MSG,
    HOME_TOOLS, HOME_SHUTDOWN, HOME_MAP,
    // Radio section
    SECTION_RADIO,
    TX_POWER,
    RADIO_PRESET,
    CUSTOM_FREQ, CUSTOM_SF, CUSTOM_BW, CUSTOM_CR,
#if FEAT_RX_POWERSAVE
    POWER_SAVE,
#endif
    TX_APC,
    SCOPE_NAME,
    // System section
    SECTION_SYSTEM,
    DEVICE_NAME,
    SCHEMA_SYSTEM,    // power, units
    BATT_CURVE,
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
    SECTION_CONTACTS, DM_FILTER, CH_FILTER, ROOM_FILTER, SCHEMA_CONTACTS, PRUNE_NOW,
    // Messages section
    SECTION_MESSAGES,
    SCHEMA_MESSAGES,
    MSG_SLOT_0, MSG_SLOT_1, MSG_SLOT_2, MSG_SLOT_3, MSG_SLOT_4,
    MSG_SLOT_5, MSG_SLOT_6, MSG_SLOT_7, MSG_SLOT_8, MSG_SLOT_9,
    Count,
    // Not walked from the enum: placed right after the schema's "Lock screen" row.
    LOCK_PIN
  };
  // A schema setting's row: SCHEMA_ITEM + its index in settings::ALL.
  static const int SCHEMA_ITEM = 128;

  // Cursor + scroll, fold state and the flattened visible list are owned by the
  // shared AccordionList helper. We keep only the section→SettingItem mapping it
  // needs (sections are walked once from the enum, honouring the #if guards).
  int  _selected = 0;   // SettingItem under the cursor, resolved per input/render
  int  _reserve = 0;    // right-edge px reserved for the scrollbar (0 when list fits)
  bool _dirty = false;
  bool _keep_place = false;   // set when opening a screen from a row, so Back lands on it

  AccordionList _acc;
  static const int NUM_SECTIONS = 8;
  static const int MAX_PER_SEC  = 16;
  uint8_t _sec_items[NUM_SECTIONS][MAX_PER_SEC]; // SettingItem per (section, row)
  uint8_t _sec_count[NUM_SECTIONS];
  uint8_t _sec_header[NUM_SECTIONS];             // the SECTION_* enum for each section
  int     _num_sections = 0;

#if FEAT_FULL_REFRESH_SETTING
  static const char* EINK_FULL_REFRESH_LABELS[5];
  static const int   EINK_FULL_REFRESH_COUNT = 5;
#endif

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

  // Level boxes ending at the row's right edge, like every other value.
  void renderBar(DisplayDriver& display, int x, int y, int value, int max_val) {
    const int gap     = 2;
    const int right   = display.width() - _reserve - 2;
    const int avail   = right - x;
    const int raw     = (avail - (max_val - 1) * gap) / max_val;
    const int cap     = display.getLineHeight() - 2;
    const int box_h   = raw < cap ? (raw < 2 ? 2 : raw) : cap;
    const int box_w   = box_h;
    x = right - (max_val * box_w + (max_val - 1) * gap);
    for (int i = 0; i < max_val; i++) {
      int bx = x + i * (box_w + gap);
      display.drawRect(bx, y, box_w, box_h);
      if (i < value)
        display.fillRect(bx + 1, y + 1, box_w - 2, box_h - 2);
    }
  }



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
  // A SCHEMA_* placeholder becomes the schema's settings of its sections, in
  // the schema's order -- so a setting added there shows here too.
  void buildSections() {
    int cur = -1;
    for (int i = 0; i < (int)Count; i++) {
      if (isSection(i)) {
        if (++cur >= NUM_SECTIONS) break;
        _sec_header[cur] = (uint8_t)i;
        _sec_count[cur]  = 0;
      } else if (cur >= 0 && isSchemaGroup(i)) {
        for (int k = 0; k < settings::COUNT && _sec_count[cur] < MAX_PER_SEC; k++)
          if (schemaIn(i, settings::ALL[k].section) && schemaShown(settings::ALL[k])) {
            _sec_items[cur][_sec_count[cur]++] = (uint8_t)(SCHEMA_ITEM + k);
            if (settings::ALL[k].offset == offsetof(NodePrefs, auto_lock) && _sec_count[cur] < MAX_PER_SEC)
              _sec_items[cur][_sec_count[cur]++] = (uint8_t)LOCK_PIN;
          }
      } else if (cur >= 0 && _sec_count[cur] < MAX_PER_SEC) {
        _sec_items[cur][_sec_count[cur]++] = (uint8_t)i;
      }
    }
    _num_sections = cur + 1;
  }

  static bool isSchemaGroup(int item) {
    return item == SCHEMA_DISPLAY || item == SCHEMA_SOUND || item == SCHEMA_SYSTEM ||
           item == SCHEMA_CONTACTS || item == SCHEMA_MESSAGES;
  }
  // Which schema sections a placeholder stands for.
  static bool schemaIn(int group, uint8_t sec) {
    using namespace settings;
    switch (group) {
      case SCHEMA_DISPLAY:  return sec == SEC_DISPLAY || sec == SEC_TIME;
      case SCHEMA_SOUND:    return sec == SEC_SOUND || sec == SEC_QUIET || sec == SEC_SOUND_FOR;
      case SCHEMA_SYSTEM:   return sec == SEC_POWER || sec == SEC_UNITS;
      case SCHEMA_CONTACTS: return sec == SEC_CONTACTS;
      case SCHEMA_MESSAGES: return sec == SEC_MESSAGES;
    }
    return false;
  }
  // Settings this board has no use for.
  static bool schemaShown(const settings::Setting& st) {
    uint16_t o = st.offset;
    (void)o;
#if !FEAT_BRIGHTNESS_SETTING
    if (o == offsetof(NodePrefs, display_brightness)) return false;
#endif
#if AUTO_OFF_MILLIS == 0
    if (o == offsetof(NodePrefs, auto_off_secs)) return false;
#endif
#if !FEAT_CLOCK_SECONDS_SETTING
    if (o == offsetof(NodePrefs, clock_hide_seconds)) return false;
#endif
#if ENV_INCLUDE_GPS != 1
    if (o == offsetof(NodePrefs, gps_interval)) return false;
#endif
#ifndef PIN_BUZZER
    if (o == offsetof(NodePrefs, buzzer_volume) || o == offsetof(NodePrefs, notif_melody_dm) ||
        o == offsetof(NodePrefs, notif_melody_ch) || o == offsetof(NodePrefs, notif_melody_ad) ||
        o == offsetof(NodePrefs, advert_sound_scope)) return false;
#endif
    return true;
  }

  // The number pad for the lock PIN, with what it's asking for.
  void beginPinEntry(const char* prompt) {
    _kb->beginPin("", KeyboardWidget::PIN_MAX_LEN, false, prompt);
    _kb->clearPlaceholders();   // a PIN is literal, not a template message
  }

  // Brightness and volume: a row of level boxes, stopping at both ends.
  static bool isBar(const settings::Setting& st) {
    return st.offset == offsetof(NodePrefs, display_brightness) || st.offset == offsetof(NodePrefs, buzzer_volume);
  }

  // A schema setting: its short label, the value flush right (a switch for
  // on / off, brightness and volume as bars); a value too long for the room
  // beside the label is cut, and scrolls while selected.
  int renderSchema(DisplayDriver& display, const settings::Setting& st, NodePrefs* p, int y, bool sel) {
    const char* label = settings::shortLabel(st);
    if (!p) { display.setCursor(2, y); display.print(label); return 0; }
    if (isBar(st)) {
      display.setCursor(2, y);
      display.print(label);
      renderBar(display, valCol(display), y, settings::get(*p, st) + 1, st.count);
      return 0;
    }
    if (!st.option) {   // a switch
      info::switchRow(display, y, label, settings::get(*p, st) != 0, sel, _reserve);
      return 0;
    }
    char v[24];
    settings::text(*p, st, v, sizeof(v));
    return info::valueRow(display, y, label, v, sel, _reserve);
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
    return item == HOME_CLOCK    || item == HOME_STATUS     || item == HOME_BT      ||
           item == HOME_ADVERT   || item == HOME_TOOLS      ||
           item == HOME_SHUTDOWN || item == HOME_SETTINGS   || item == HOME_QUICK_MSG ||
           item == HOME_FAVOURITES || item == HOME_MAP;
  }

  uint16_t homePageBit(int item) const {
    int bit = homePageBitIndex(item);
    // SETTINGS and QUICK_MSG are always visible (no mask bit). All other pages
    // — including FAVOURITES — toggle via home_pages_mask.
    if (bit < 0 || bit == NodePrefs::HPB_SETTINGS || bit == NodePrefs::HPB_QUICK_MSG) return 0;
    // Status took over the Radio, GPS and Sensors pages: it owns all three bits.
    if (item == HOME_STATUS) return NodePrefs::HP_RADIO | NodePrefs::HP_GPS | NodePrefs::HP_SENSORS;
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
    if (item == HOME_STATUS)    return NodePrefs::HPB_RADIO;   // the old Radio slot
    if (item == HOME_BT)        return NodePrefs::HPB_BLUETOOTH;
    if (item == HOME_ADVERT)    return NodePrefs::HPB_ADVERT;
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
  // Also repairs a partially-initialised order where CLOCK is absent, and
  // appends any pages absent from a saved order (pages a later firmware added).
  void ensurePageOrderInit(NodePrefs* p) const {
    if (!p) return;
    if (p->page_order_set == NodePrefs::PAGE_ORDER_MAGIC) {
      bool has_clock = false;
      for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
        uint8_t v = p->page_order[i];
        if (v < 1 || v > NodePrefs::HPB_COUNT) break;
        if ((int)(v - 1) == NodePrefs::HPB_CLOCK) has_clock = true;
      }
      if (!has_clock) {
        // Corrupted/partial — full re-init below.
        memset(p->page_order, 0, sizeof(p->page_order));
      } else {
        // Append any pages that are absent from the saved order (e.g. added by a
        // later firmware version).
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
          // are required — any missing from a saved order is appended into the
          // free tail slots below.
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
    // MAP TOOLS MESSAGES SHUTDOWN. Every bit keeps a slot (PAGE_ORDER_LEN ==
    // HPB_COUNT), retired ones too: RECENT shows nowhere, and Status stands in
    // the RADIO slot for the old Radio, GPS and Sensors pages.
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

  // Returns 0, or the ms until the selected row's value should next redraw
  // to keep a marquee animation going (see DisplayDriver::drawTextEllipsized).
  int renderItem(DisplayDriver& display, int item, int y, bool sel) {
    NodePrefs* p = _task->getNodePrefs();
    int mq = 0;
    char buf[24];
    auto val = [&](const char* label, const char* v) { mq = info::valueRow(display, y, label, v, sel, _reserve); };
    auto sw  = [&](const char* label, bool on) { info::switchRow(display, y, label, on, sel, _reserve); };

    drawRowSelection(display, y, sel, _reserve);
    display.setCursor(2, y);

    if (item >= SCHEMA_ITEM) {
      mq = renderSchema(display, settings::ALL[item - SCHEMA_ITEM], p, y, sel);
    } else if (item == BUZZER) {
#ifdef PIN_BUZZER
      static const char* labels[] = { "On", "Off", "Auto" };
      int m = _task->getBuzzerMode();
      val("Buzzer", labels[m < 3 ? m : 0]);
#else
      val("Buzzer", "N/A");
#endif
    } else if (isHomePage(item)) {
      if (p) ensurePageOrderInit(p);
      int pos = homePagePosition(item, p);
      char label[24];
      if (pos > 0) snprintf(label, sizeof(label), "%2d %s", pos, homePageLabel(item));
      else         snprintf(label, sizeof(label), "%s", homePageLabel(item));
      if (!homePageToggleable(item)) val(label, "always");
      else                           sw(label, homePageVisible(item, p));
    } else if (item == TX_POWER) {
      snprintf(buf, sizeof(buf), "%d dBm", p ? p->tx_power_dbm : 0);
      val("TX Pwr", buf);
    } else if (item == RADIO_PRESET) {
      val("Preset", p ? _picker.currentName(p, radioTarget(p)) : "Custom");
    } else if (item == CUSTOM_FREQ) {
      if (sel && _editor.active()) {   // the digit editor where the value stands
        display.print("Freq");
        _editor.render(display, display.width() - _reserve - 2 - display.getTextWidth("000.000"), y);
      } else {
        snprintf(buf, sizeof(buf), "%.3f", p ? p->freq : 0.0f);
        val("Freq", buf);
      }
    } else if (item == CUSTOM_SF) {
      snprintf(buf, sizeof(buf), "%d", p ? (int)p->sf : 0);
      val("SF", buf);
    } else if (item == CUSTOM_BW) {
      snprintf(buf, sizeof(buf), "%.1f kHz", p ? p->bw : 0.0f);
      val("BW", buf);
    } else if (item == CUSTOM_CR) {
      snprintf(buf, sizeof(buf), "%d", p ? (int)p->cr : 0);
      val("CR", buf);
#if FEAT_RX_POWERSAVE
    } else if (item == POWER_SAVE) {
      // Forced off (and locked) while the repeater is on — it must hear all traffic.
      if (p && p->client_repeat) val("Pwr save", "--");
      else sw("Pwr save", p && p->rx_powersave);
#endif
    } else if (item == TX_APC) {
      // Suppressed (and locked) while repeating — a repeater holds full TX power.
      if (p && p->client_repeat) val("Auto pwr", "--");
      else sw("Auto pwr", p && p->tx_apc);
    } else if (item == SCOPE_NAME) {
      const ScopeList& sl = the_mesh.scopeList();
      val("Scope", sl.name(sl.default_idx));
    } else if (item == LOCK_PIN) {
      sw("Lock PIN", _task->passwordLockEnabled());
    } else if (item == DEVICE_NAME) {
      val("Name", the_mesh.getNodeName());
    } else if (item == BATT_CURVE) {
      val("Batt curve", p && battery::validCurve(p->batt_curve_mv) ? "Custom" : "LiPo");
    } else if (item == REBOOT) {
      display.print("Reboot");   // action row: Enter reboots this device
    } else if (item == KEYBOARD_TYPE) {
      val("Type", (p && p->keyboard_type) ? "T9" : "ABC");
    } else if (item == KEYBOARD_MAIN_ALPHABET) {
      val("Main", NodePrefs::keyboardAlphabetLabel(p ? p->keyboard_main_alphabet : 0));
    } else if (item == KEYBOARD_ALPHABET) {
      val("Additional", NodePrefs::keyboardAlphabetLabel(p ? p->keyboard_alt_alphabet : 0));
#if defined(CARDKB_I2C)
    } else if (item == KEYBOARD_CARDKB_COMPACT) {
      val("Ext. KB", (p && p->keyboard_cardkb_compact) ? "Compact" : "Full");
#endif
#if FEAT_DISPLAY_ROTATION_SETTING
    } else if (item == ROTATION) {
      val("Rotation", rotLabel(p ? p->display_rotation - DISPLAY_ROTATION : 0));
#endif
#if FEAT_JOYSTICK_ROTATION_SETTING
    } else if (item == JOY_ROTATION) {
      val("Joystick", rotLabel(p ? p->joystick_rotation : 0));
#endif
#if FEAT_FULL_REFRESH_SETTING
    } else if (item == EINK_FULL_REFRESH) {
      uint8_t idx = p ? p->eink_full_refresh_every : 0;
      if (idx >= EINK_FULL_REFRESH_COUNT) idx = 0;
      val("Full rfsh", EINK_FULL_REFRESH_LABELS[idx]);
    } else if (item == UI_TEXT_SCALE) {
      char b[4];
      snprintf(b, sizeof(b), "%ux", (unsigned)_task->getUiFontScale());
      val("Text size", b);
#if STATUS_ICONS_SCALED
    } else if (item == UI_ICON_SCALE) {
      char b[12];
      const uint8_t v = _task->getStatusIconScale();
      if (v == 0) snprintf(b, sizeof(b), "Auto");
      else        snprintf(b, sizeof(b), "%ux", (unsigned)v);
      val("Icon size", b);
#endif
#endif
    } else if (item == DM_FILTER) {
      val("DMs", (p && p->dm_show_all) ? "All" : "Fav");
    } else if (item == CH_FILTER) {
      val("Channels", (p && p->ch_fav_only) ? "Fav" : "All");
    } else if (item == ROOM_FILTER) {
      val("Rooms", (p && p->room_fav_only) ? "Fav" : "All");
    } else if (item == PRUNE_NOW) {
      display.print("Prune now");   // action row: Enter counts + confirms + removes
    } else if (isMsgSlot(item)) {
      // A message template reads from the left, cut at the edge (scrolls when selected).
      int slot = msgSlotIndex(item);
      char label[5];
      snprintf(label, sizeof(label), "Q%d:", slot + 1);
      display.print(label);
      const char* tmpl = (p && p->custom_msgs[slot][0]) ? p->custom_msgs[slot] : "(empty)";
      int xm = 8 + display.getCharWidth() * 4;
      int r = display.drawTextEllipsized(xm, y, display.width() - xm - _reserve - 2, tmpl, sel);
      if (sel && r > 0) mq = r;
    }
    return mq;
  }

  // Keyboard state for editing message slots
  int            _edit_slot = -1;  // -1 = not editing, 0..9 = slot being edited
  bool           _edit_name = false;  // editing DEVICE_NAME via the keyboard
  bool           _edit_lock_pass = false; // setting the screen PIN via the keyboard
  char           _lock_pass_first[KeyboardWidget::PIN_MAX_LEN + 1] = "";   // the first entry, awaiting its repeat
  KeyboardWidget* _kb;

  // Scope list management (SCOPE_NAME row -> a full-screen add/rename/
  // delete/set-default list over the shared named-scope list, ScopeList.h).
  // Row 0 is always "*", rows 1..count are named entries, the trailing row
  // is a synthetic "+ Add scope" -- same shape as MessagesScreen's channel
  // picker. _scope_action_menu is the per-row Set default/Rename/Delete
  // popup (Enter on a row); the keyboard (_kb) is reused for both Add and
  // Rename's name entry, told apart by _scope_rename_idx (-1 = adding new).
  bool     _scope_mgmt_active = false;
  int      _scope_mgmt_sel = 0, _scope_mgmt_scroll = 0;
  PopupMenu _scope_action_menu;
  int      _scope_action_idx = -1;   // list index the open action menu targets
  // -2 = _kb not open for a scope name; -1 = _kb is adding a new scope;
  // >=0 = _kb is renaming that list index.
  int      _scope_rename_idx = -2;
  bool     _scope_delete_confirm_active = false;

  // Contacts > "Prune now" confirm -- a separate PopupMenu from
  // _scope_action_menu since this one pops up directly over the flat
  // Settings list (PRUNE_NOW's own row), not nested inside a sub-screen.
  PopupMenu _prune_confirm;
  char      _prune_confirm_title[40];

  int renderScopeMgmt(DisplayDriver& display) {
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Scope", -1, 0, true, _scope_action_menu.active);
    const ScopeList& sl = the_mesh.scopeList();
    int total = sl.totalCount() + 1;   // +1 synthetic "+ Add scope" row
    drawList(display, total, _scope_mgmt_sel, _scope_mgmt_scroll, [&](int idx, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      display.setCursor(2, y);
      if (idx == sl.totalCount()) {
        display.print("+ Add scope");
      } else {
        display.print(sl.name((uint8_t)idx));
        if ((uint8_t)idx == sl.default_idx)
          display.drawTextRightAlign(display.width() - reserve - 2, y, "default");
      }
      display.setColor(DisplayDriver::LIGHT);
    });
    if (_scope_action_menu.active) _scope_action_menu.render(display);
    return _scope_action_menu.active ? 50 : 500;
  }

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
    const bool keep = _keep_place;   // back from a screen opened here: same row
    _keep_place = false;
    _task->savePrefsIfDirty(_dirty);   // a change made before a screen opened from here
    _edit_name = false;
    _scope_mgmt_active = false;
    _scope_rename_idx = -2;
    _scope_action_menu.active = false;
    _scope_delete_confirm_active = false;
    _prune_confirm.active = false;
    _edit_lock_pass = false;
    _lock_pass_first[0] = '\0';
    if (!keep) resetList();
    _editor.freq.active = false;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);

    if (_edit_slot >= 0 || _edit_name || _edit_lock_pass || _scope_rename_idx != -2 || _picker.saving) {
      return _kb->render(display);
    }

    if (_scope_mgmt_active) return renderScopeMgmt(display);

    drawScreenHeader(display, "Settings");

    int mq_delay = 0;
    _acc.render(display,
      // Section header: disclosure mark + name
      [&](int sec, int y, bool sel, int reserve, bool collapsed) {
        _reserve = reserve;
        display.setColor(DisplayDriver::LIGHT);
        drawRowSelection(display, y, sel, reserve);
        drawDisclosure(display, 2, y, !collapsed);
        display.setCursor(2 + 2 * display.getCharWidth(), y);
        display.print(sectionName(_sec_header[sec]));
      },
      // Item row
      [&](int sec, int item, int y, bool sel, int reserve) {
        _reserve = reserve;
        int r = renderItem(display, _sec_items[sec][item], y, sel);
        if (r > 0) mq_delay = r;
      });

    if (_picker.menu.active) _picker.menu.render(display);
    if (_prune_confirm.active) _prune_confirm.render(display);

    return (mq_delay > 0 && mq_delay < 2000) ? mq_delay : 2000;
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

    // Keyboard editing mode for the lock-screen password
    if (_edit_lock_pass) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        // Entered twice: a typo here would lock the owner out.
        if (strlen(_kb->buf) < 4) {
          _kb->prompt = "Min 4";
        } else if (!_lock_pass_first[0]) {
          strncpy(_lock_pass_first, _kb->buf, sizeof(_lock_pass_first) - 1);
          _lock_pass_first[sizeof(_lock_pass_first) - 1] = '\0';
          beginPinEntry("Again");
        } else if (strcmp(_lock_pass_first, _kb->buf) != 0) {
          _lock_pass_first[0] = '\0';
          beginPinEntry("No match");
        } else {
          if (p) {
            _task->setNodeLockPassword(_kb->buf);
            the_mesh.savePrefs();   // now, not on leaving: a PIN must survive a power-off right after
          }
          _edit_lock_pass = false;
          _lock_pass_first[0] = '\0';
        }
      } else if (res == KeyboardWidget::CANCELLED) {
        _edit_lock_pass = false;
        _lock_pass_first[0] = '\0';
      }
      return true;
    }

    // Keyboard editing mode for adding/renaming a scope-list entry
    if (_scope_rename_idx != -2) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (_scope_rename_idx == -1) the_mesh.addScope(_kb->buf);
        else                         the_mesh.renameScope((uint8_t)_scope_rename_idx, _kb->buf);
        _scope_rename_idx = -2;
      } else if (res == KeyboardWidget::CANCELLED) {
        _scope_rename_idx = -2;
      }
      return true;
    }

    if (_scope_mgmt_active) {
      const ScopeList& sl = the_mesh.scopeList();
      if (_scope_delete_confirm_active) {
        auto res = _scope_action_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _scope_action_menu.selectedIndex() == 0) {   // "Delete"
          the_mesh.removeScope((uint8_t)_scope_action_idx);
          if (_scope_mgmt_sel > sl.totalCount()) _scope_mgmt_sel = sl.totalCount();
        }
        if (res != PopupMenu::NONE) _scope_delete_confirm_active = false;
        return true;
      }
      if (_scope_action_menu.active) {
        auto res = _scope_action_menu.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          int sel = _scope_action_menu.selectedIndex();
          if (sel == 0) {                                   // Set as default
            // DMs, the relay's primary slot and every channel left on
            // "Default" follow it; the list marks it [default].
            the_mesh.setDefaultScope((uint8_t)_scope_action_idx);
          } else if (sel == 1 && _scope_action_idx >= 1) {  // Rename
            _scope_rename_idx = _scope_action_idx;
            _kb->begin(sl.name((uint8_t)_scope_action_idx), 23);
            _kb->prompt = "Scope";
            _kb->clearPlaceholders();
          } else if (sel == 2 && _scope_action_idx >= 1) {  // Delete -- confirm first
            _scope_action_menu.beginConfirm("Delete scope?", "Delete");
            _scope_delete_confirm_active = true;
          }
        }
        return true;
      }
      if (c == KEY_CANCEL) { _scope_mgmt_active = false; return true; }
      int total = sl.totalCount() + 1;
      if (c == KEY_UP)   { _scope_mgmt_sel = (_scope_mgmt_sel > 0) ? _scope_mgmt_sel - 1 : total - 1; return true; }
      if (c == KEY_DOWN) { _scope_mgmt_sel = (_scope_mgmt_sel + 1 < total) ? _scope_mgmt_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) {
        if (_scope_mgmt_sel == sl.totalCount()) {   // "+ Add scope"
          _scope_rename_idx = -1;
          _kb->begin("", 23);
          _kb->prompt = "Scope";
          _kb->clearPlaceholders();
        } else {
          _scope_action_idx = _scope_mgmt_sel;
          bool is_named = _scope_action_idx >= 1;
          _scope_action_menu.begin("Scope", is_named ? 3 : 1);
          _scope_action_menu.addItem("Set as default");
          if (is_named) { _scope_action_menu.addItem("Rename"); _scope_action_menu.addItem("Delete"); }
        }
        return true;
      }
      return true;   // list has focus -- swallow anything else rather than falling through
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
            _kb->prompt = "Preset name";
            _kb->clearPlaceholders();   // {loc}/{time} are for messages, not preset names
            break;
          case RadioPresetPicker::APPLIED:
            _task->applyRadioParams();
            _dirty = true;
            break;
          case RadioPresetPicker::DELETED:
            _dirty = true;
            break;
          case RadioPresetPicker::NONE:
            break;
        }
      } else if (res == PopupMenu::CANCELLED) {
        _picker.deleting = false;
        _picker.confirm_slot = -1;
      }
      return true;
    }

    // Contacts > "Prune now" confirm
    if (_prune_confirm.active) {
      auto res = _prune_confirm.handleInput(c);
      if (res == PopupMenu::SELECTED && _prune_confirm.selectedIndex() == 0) {   // "Remove"
        int n = the_mesh.pruneStaleContacts();
        char msg[24];
        snprintf(msg, sizeof(msg), "Removed %d contact%s", n, n == 1 ? "" : "s");
        _task->showAlert(msg, 1400);
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

    if (_selected >= SCHEMA_ITEM) {
      if (!p || !(left || right || enter)) return false;
      const settings::Setting& st = settings::ALL[_selected - SCHEMA_ITEM];
      if (isBar(st)) {
        const int v = settings::get(*p, st) + (left ? -1 : 1);
        if (v < 0 || v >= st.count) return true;
        settings::set(*p, st, (uint8_t)v);
        if (st.changed) st.changed(_task->core());
      } else {
        settings::step(*p, st, left ? -1 : 1, _task->core());
      }
      _dirty = true;
      return true;
    }

    if (_selected == BUZZER && (left || right || enter)) {
      _task->cycleBuzzerMode();
      _dirty = true;
      return true;
    }
    if (isHomePage(_selected) && p) {
      if (left || right) {
        movePageInOrder(_selected, left ? -1 : 1, p);
        _dirty = true;
        return true;
      }
      if (enter && homePageToggleable(_selected)) {
        if (!p->home_pages_mask) p->home_pages_mask = NodePrefs::HP_ALL;
        // Set or clear the page's bits together (Status has three).
        if (homePageVisible(_selected, p)) p->home_pages_mask &= ~homePageBit(_selected);
        else                               p->home_pages_mask |= homePageBit(_selected);
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
#if FEAT_RX_POWERSAVE
    if (_selected == POWER_SAVE && p && (left || right || enter)) {
      if (p->client_repeat) { _task->showAlert("Off while repeating", 900); return true; }
      p->rx_powersave ^= 1;
      _task->applyPowerSave();
      _dirty = true;
      return true;
    }
#endif
    if (_selected == TX_APC && p && (left || right || enter)) {
      if (p->client_repeat) { _task->showAlert("Off while repeating", 900); return true; }
      p->tx_apc ^= 1;
      _task->applyApc();
      _dirty = true;
      return true;
    }
    // Lock PIN: Enter sets one, or clears the one set
    if (_selected == LOCK_PIN && p && enter) {
      if (_task->passwordLockEnabled()) {
        _task->setNodeLockPassword("");
        the_mesh.savePrefs();
      } else {
        _edit_lock_pass = true;
        _lock_pass_first[0] = '\0';
        beginPinEntry("New PIN");
      }
      return true;
    }
    if (_selected == DEVICE_NAME && p && enter) {
      _edit_name = true;
      _kb->begin(the_mesh.getNodeName(), (int)sizeof(p->node_name) - 1);
      _kb->prompt = "Node name";
      _kb->clearPlaceholders();   // a device name is literal, not a message
      return true;
    }
    if (_selected == SCOPE_NAME && p && enter) {
      _scope_mgmt_active = true;
      _scope_mgmt_sel = 0;
      _scope_mgmt_scroll = 0;
      return true;
    }
    if (_selected == BATT_CURVE && enter) {
      _keep_place = true;   // back from it to this row, not a folded list
      _task->savePrefsIfDirty(_dirty);
      _task->gotoBatteryCurve();
      return true;
    }
    if (_selected == REBOOT && enter) {
      _task->showAlert("Rebooting...", 800);
      _task->shutdown(true);   // flushes prefs/RTC/contacts/trail, then reboots -- single choke point
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
#if FEAT_DISPLAY_ROTATION_SETTING
    if (_selected == ROTATION && p && (left || right || enter)) {
      p->display_rotation ^= 2;   // the build's shape, either way up
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
    if (_selected == UI_TEXT_SCALE && (left || right || enter)) {
      int v = _task->getUiFontScale();             // 1..5, wrapping
      v = left ? (v <= 1 ? 5 : v - 1) : (v >= 5 ? 1 : v + 1);
      _task->setUiFontScale(v);                    // saved to /ui_scale immediately
      return true;
    }
#if STATUS_ICONS_SCALED
    if (_selected == UI_ICON_SCALE && (left || right || enter)) {
      const int n = STATUS_ICON_SCALE_MAX + 1;     // Auto, 1x..8x
      int v = _task->getStatusIconScale();
      v = (v + (left ? n - 1 : 1)) % n;
      _task->setStatusIconScale(v);                // saved to /ui_scale immediately
      return true;
    }
#endif
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
    if (_selected == PRUNE_NOW && enter) {
      int n = the_mesh.countStaleContacts();
      if (n == 0) {
        // Two different "nothing happened" reasons, told apart so the row
        // doesn't look broken when the threshold simply isn't set yet.
        _task->showAlert(p && p->contact_expiry_idx == 0 ? "Expire is Off" : "No inactive contacts", 1400);
      } else {
        snprintf(_prune_confirm_title, sizeof(_prune_confirm_title), "Remove %d contact%s?", n, n == 1 ? "" : "s");
        _prune_confirm.beginConfirm(_prune_confirm_title, "Remove");
      }
      return true;
    }
    if (isMsgSlot(_selected) && enter) {
      int slot = msgSlotIndex(_selected);
      _edit_slot = slot;
      // Bound to the custom_msgs store (140 B) so the wider keyboard buffer
      // can't overflow it on save.
      _kb->begin(p ? p->custom_msgs[slot] : "",
                p ? (int)sizeof(p->custom_msgs[slot]) - 1 : KB_MAX_LEN);
      _kb->prompt = "Quick msg";
      kbAddSensorPlaceholders(*_kb, &sensors);
      return true;
    }
    return false;
  }
};

#if FEAT_FULL_REFRESH_SETTING
const char* SettingsScreen::EINK_FULL_REFRESH_LABELS[5] = { "Off", "5", "10", "20", "30" };
#endif
