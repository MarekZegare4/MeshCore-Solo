#include "UITask.h"
#include "SoundNotifier.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "../MsgExpand.h"
#include "../Features.h"
#include "../GeoUtils.h"
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
  char _solo_ver[12];

public:
  SplashScreen(UITask* task) : _task(task) {
    // MeshCore upstream version shown large (e.g. "1.15")
    strncpy(_version_info, MESHCORE_VERSION, sizeof(_version_info) - 1);
    _version_info[sizeof(_version_info) - 1] = '\0';

    // Solo firmware version: strip commit hash suffix (v1.15-solo.1-abcdef -> v1.15)
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');
    int plen = dash ? (int)(dash - ver) : (int)strlen(ver);
    if (plen >= (int)sizeof(_solo_ver)) plen = sizeof(_solo_ver) - 1;
    memcpy(_solo_ver, ver, plen);
    _solo_ver[plen] = '\0';

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    const int lh = display.getLineHeight();
    const int step = display.lineStep();

    // meshcore logo
    display.setColor(DisplayDriver::LIGHT);
    int logoWidth = 128;
    int logo_y = 3;
    display.drawXbm((display.width() - logoWidth) / 2, logo_y, meshcore_logo, logoWidth, 13);

    // version info at sz2
    int ver_y = logo_y + 13 + 2;
    display.setTextSize(2);
    int lh2 = display.getLineHeight();
    display.drawTextCentered(display.width()/2, ver_y, _version_info);

    // build date at sz1, below sz2 version
    int date_y = ver_y + lh2 + 2;
    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, date_y, FIRMWARE_BUILD_DATE);

#ifdef FIRMWARE_SOLO_BUILD
    int solo_y = date_y + step;
    display.fillRect(0, solo_y - 1, display.width(), lh + 2);
    display.setColor(DisplayDriver::DARK);
    char solo_label[24];
    if (_solo_ver[0])
      snprintf(solo_label, sizeof(solo_label), "Solo %s for Wio", _solo_ver);
    else
      snprintf(solo_label, sizeof(solo_label), "Solo for Wio");
    display.drawTextCentered(display.width()/2, solo_y, solo_label);
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


#include "FullscreenMsgView.h"
#include "SensorPlaceholders.h"
#include "SettingsScreen.h"
#include "QuickMsgScreen.h"

// ── Custom screens (separate files to ease upstream merges) ───────────────────
#include "RingtoneEditorScreen.h"
#include "BotScreen.h"
#include "NearbyScreen.h"
#include "DashboardConfigScreen.h"
#include "AutoAdvertScreen.h"
#include "TrailScreen.h"
#include "CompassScreen.h"
#include "ToolsScreen.h"

#ifndef BATT_MIN_MILLIVOLTS
  #define BATT_MIN_MILLIVOLTS 3200
#endif

// LiPo discharge curve: voltage (mV) → raw capacity (%). Shared by the top-bar
// battery indicator and the dashboard Batt% field so both report the same
// number for the same voltage. low_mv (typically NodePrefs.low_batt_mv, the
// user-configurable auto-shutdown threshold in Settings) is rescaled to 0%
// so the bar empties at the cutoff the user actually cares about.
static int battMvToPercent(int mv, int low_mv) {
  static const struct { uint16_t mv; uint8_t pct; } CURVE[] = {
    {3200,  0}, {3300,  3}, {3400,  8}, {3500, 15},
    {3600, 25}, {3650, 33}, {3700, 45}, {3750, 58},
    {3800, 68}, {3900, 77}, {4000, 86}, {4100, 93}, {4170, 100}
  };
  static const int CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);
  auto curveAt = [&](int v) -> int {
    if (v <= (int)CURVE[0].mv) return CURVE[0].pct;
    if (v >= (int)CURVE[CURVE_LEN-1].mv) return CURVE[CURVE_LEN-1].pct;
    for (int i = 1; i < CURVE_LEN; i++) {
      if (v <= (int)CURVE[i].mv) {
        int span_mv  = CURVE[i].mv  - CURVE[i-1].mv;
        int span_pct = CURVE[i].pct - CURVE[i-1].pct;
        return CURVE[i-1].pct + (v - (int)CURVE[i-1].mv) * span_pct / span_mv;
      }
    }
    return 100;
  };
  if (low_mv <= 0) low_mv = BATT_MIN_MILLIVOLTS;
  int raw_pct = curveAt(mv);
  int low_pct = curveAt(low_mv);
  int pct = (low_pct >= 100) ? 0 : (raw_pct - low_pct) * 100 / (100 - low_pct);
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}

// Render the time starting at top_y; returns the y just below the time block
// so the caller can flow the date / dashboard rows beneath it.
//
// On a tall portrait panel (e-ink in portrait — height > width) HH and MM are
// stacked on two lines in the huge built-in font (size 4, ~56 px tall) so the
// digits fill the narrow width. On wide panels (OLED, landscape e-ink) the
// classic single-line "HH:MM" at size 2 is kept.
static int drawClockTime(DisplayDriver& d, int top_y, const struct tm* ti,
                         bool h12, bool show_sec) {
  const bool tall = d.height() > d.width();   // true only on portrait e-ink

  if (tall) {
    int hh = ti->tm_hour;
    const char* ap = nullptr;
    if (h12) { ap = (hh < 12) ? "AM" : "PM"; hh %= 12; if (hh == 0) hh = 12; }
    const int cx = d.width() / 2;
    char hbuf[4], mbuf[4];
    snprintf(hbuf, sizeof(hbuf), "%02d", hh);
    snprintf(mbuf, sizeof(mbuf), "%02d", ti->tm_min);

    int y = top_y;
    d.setTextSize(4);
    const int lhb = d.getLineHeight();
    // The built-in GFX font advances 6 px per char but the glyph is only 5 px
    // wide, so getTextWidth() over-reports by one trailing blank column and
    // drawTextCentered() would bias the digits ~half a column to the left.
    // Centre on the visible width (minus that trailing column) instead.
    const int trail = d.getCharWidth() / 6;   // one built-in column at this size
    auto drawBig = [&](const char* s, int yy) {
      int w = (int)d.getTextWidth(s) - trail;
      d.setCursor(cx - w / 2, yy);
      d.print(s);
    };
    drawBig(hbuf, y);  y += lhb + 2;
    drawBig(mbuf, y);  y += lhb + 2;
    if (ap) { d.setTextSize(2); d.drawTextCentered(cx, y, ap); y += d.getLineHeight() + 1; }
    d.setTextSize(1);
    return y;
  }

  // Wide layout: single inline line at size 2.
  char buf[16];
  d.setTextSize(2);
  const int lh2 = d.getLineHeight();
  if (h12) {
    int hh = ti->tm_hour % 12; if (hh == 0) hh = 12;
    const char* ap = (ti->tm_hour < 12) ? "AM" : "PM";
    if (show_sec) snprintf(buf, sizeof(buf), "%d:%02d:%02d%s", hh, ti->tm_min, ti->tm_sec, ap);
    else          snprintf(buf, sizeof(buf), "%d:%02d %s", hh, ti->tm_min, ap);
  } else {
    if (show_sec) snprintf(buf, sizeof(buf), "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
    else          snprintf(buf, sizeof(buf), "%02d:%02d", ti->tm_hour, ti->tm_min);
  }
  d.drawTextCentered(d.width() / 2, top_y, buf);
  d.setTextSize(1);
  return top_y + lh2 + 2;
}

// ── HomeScreen ────────────────────────────────────────────────────────────────
class HomeScreen : public UIScreen {
  enum HomePage {
    CLOCK,
    FAVOURITES,
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

  // Selected slot on the Favourites page (0..FAVOURITES_COUNT - 1).
  uint8_t _fav_sel = 0;

  // Build the in-place pin picker list for an empty slot. Favourited chat
  // contacts first (`c.flags & 0x01`), then recent DM contacts deduped
  // against the favourites list. Up to PIN_PICKER_MAX.
  void buildPinPicker(int slot) {
    _pin_target_slot = slot;
    _pin_count = 0;
    // 1) Upstream-favourited chat contacts.
    for (int idx = 0; _pin_count < PIN_PICKER_MAX; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (c.type != ADV_TYPE_CHAT) continue;
      if (!(c.flags & 0x01)) continue;
      memcpy(_pin_keys[_pin_count], c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
      DisplayDriver::translateUTF8Static(_pin_labels[_pin_count], c.name, sizeof(_pin_labels[_pin_count]));
      _pin_count++;
    }
    // 2) Recent DM contacts (deduped).
    uint8_t recent[PIN_PICKER_MAX][NodePrefs::FAVOURITE_PREFIX_LEN];
    int rn = _task->getRecentDMContacts(recent, PIN_PICKER_MAX);
    for (int i = 0; i < rn && _pin_count < PIN_PICKER_MAX; i++) {
      bool dup = false;
      for (int j = 0; j < _pin_count; j++)
        if (memcmp(_pin_keys[j], recent[i], NodePrefs::FAVOURITE_PREFIX_LEN) == 0) { dup = true; break; }
      if (dup) continue;
      for (int idx = 0; ; idx++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(idx, c)) break;
        if (memcmp(c.id.pub_key, recent[i], NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
          memcpy(_pin_keys[_pin_count], recent[i], NodePrefs::FAVOURITE_PREFIX_LEN);
          DisplayDriver::translateUTF8Static(_pin_labels[_pin_count], c.name, sizeof(_pin_labels[_pin_count]));
          _pin_count++;
          break;
        }
      }
    }
    // 3) Fallback: all remaining chat contacts not already in the list.
    if (_pin_count == 0) {
      for (int idx = 0; _pin_count < PIN_PICKER_MAX; idx++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(idx, c)) break;
        if (c.type != ADV_TYPE_CHAT) continue;
        bool dup = false;
        for (int j = 0; j < _pin_count; j++)
          if (memcmp(_pin_keys[j], c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) { dup = true; break; }
        if (dup) continue;
        memcpy(_pin_keys[_pin_count], c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
        DisplayDriver::translateUTF8Static(_pin_labels[_pin_count], c.name, sizeof(_pin_labels[_pin_count]));
        _pin_count++;
      }
    }
    if (_pin_count == 0) {
      _task->showAlert("No contacts", 1000);
      _pin_target_slot = -1;
      return;
    }
    _pin_menu.begin("Pick contact", 3);
    for (int i = 0; i < _pin_count; i++) _pin_menu.addItem(_pin_labels[i]);
  }

  // In-place pin picker (opens when Enter hits an empty Favourites tile).
  static const int PIN_PICKER_MAX = 12;
  PopupMenu _pin_menu;
  uint8_t   _pin_keys[PIN_PICKER_MAX][NodePrefs::FAVOURITE_PREFIX_LEN];
  char      _pin_labels[PIN_PICKER_MAX][22];
  int       _pin_count = 0;
  int       _pin_target_slot = -1;

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  AdvertPath recent[UI_RECENT_LIST_SIZE];

  int pageBit(int page) const {
    if (page == CLOCK)      return NodePrefs::HPB_CLOCK;
    if (page == FAVOURITES) return NodePrefs::HPB_FAVOURITES;
    if (page == RECENT)    return NodePrefs::HPB_RECENT;
    if (page == RADIO)     return NodePrefs::HPB_RADIO;
    if (page == BLUETOOTH) return NodePrefs::HPB_BLUETOOTH;
    if (page == ADVERT)    return NodePrefs::HPB_ADVERT;
#if ENV_INCLUDE_GPS == 1
    if (page == GPS)       return NodePrefs::HPB_GPS;
#endif
#if UI_SENSORS_PAGE == 1
    if (page == SENSORS)   return NodePrefs::HPB_SENSORS;
#endif
    if (page == TOOLS)     return NodePrefs::HPB_TOOLS;
    if (page == SHUTDOWN)  return NodePrefs::HPB_SHUTDOWN;
    return -1;  // SETTINGS, QUICK_MSG always visible (no mask bit)
  }

  // Maps page_order bit-index back to the HomePage enum value for this build.
  // Returns -1 if the page is not compiled in.
  int bitToPage(int bit) const {
    switch (bit) {
      case NodePrefs::HPB_CLOCK:      return CLOCK;
      case NodePrefs::HPB_FAVOURITES: return FAVOURITES;
      case NodePrefs::HPB_RECENT:    return RECENT;
      case NodePrefs::HPB_RADIO:     return RADIO;
      case NodePrefs::HPB_BLUETOOTH: return BLUETOOTH;
      case NodePrefs::HPB_ADVERT:    return ADVERT;
#if ENV_INCLUDE_GPS == 1
      case NodePrefs::HPB_GPS:       return GPS;
#endif
#if UI_SENSORS_PAGE == 1
      case NodePrefs::HPB_SENSORS:   return SENSORS;
#endif
      case NodePrefs::HPB_TOOLS:     return TOOLS;
      case NodePrefs::HPB_SHUTDOWN:  return SHUTDOWN;
      case NodePrefs::HPB_SETTINGS:  return SETTINGS;
      case NodePrefs::HPB_QUICK_MSG: return QUICK_MSG;
      default: return -1;
    }
  }

  bool isPageVisible(int page) const {
    int bit = pageBit(page);
    if (bit < 0) return true;
    uint16_t mask = (_node_prefs && _node_prefs->home_pages_mask) ? _node_prefs->home_pages_mask : NodePrefs::HP_ALL;
    return (mask >> bit) & 1;
  }

  // Build ordered list of all visible pages, respecting page_order when set.
  // Returns count; out[] receives HomePage enum values.
  int buildVisibleOrder(int* out) const {
    int n = 0;
    bool custom = _node_prefs && _node_prefs->page_order_set == NodePrefs::PAGE_ORDER_MAGIC;
    if (custom) {
      for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
        uint8_t v = _node_prefs->page_order[i];
        if (v < 1 || v > NodePrefs::HPB_COUNT) break;
        int pg = bitToPage(v - 1);
        if (pg >= 0 && pg < (int)Count && isPageVisible(pg)) out[n++] = pg;
      }
      // Append any visible page missing from page_order (handles corrupted/migrated prefs)
      for (int pg = 0; pg < (int)Count; pg++) {
        if (!isPageVisible(pg)) continue;
        bool found = false;
        for (int i = 0; i < n; i++) if (out[i] == pg) { found = true; break; }
        if (!found) out[n++] = pg;
      }
    } else {
      for (int pg = 0; pg < (int)Count; pg++)
        if (isPageVisible(pg)) out[n++] = pg;
    }
    return n;
  }

  int navPage(int from, int dir) const {
    int order[(int)Count]; int n = buildVisibleOrder(order);
    if (n == 0) return from;
    int cur = 0;
    for (int i = 0; i < n; i++) if (order[i] == from) { cur = i; break; }
    return order[((cur + dir) % n + n) % n];
  }

  int renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts) {
    int low_mv = _node_prefs ? (int)_node_prefs->low_batt_mv : 0;
    int pct = battMvToPercent((int)batteryMilliVolts, low_mv);

    uint8_t mode = (_node_prefs && _node_prefs->batt_display_mode < 3)
                     ? _node_prefs->batt_display_mode : 0;

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int ind     = cw + 2;    // single-char indicator width
    const int ind_h   = display.isLemonFont() ? lh - 2 : lh;
    const int ind_gap = display.isLandscape() ? 3 : 1;  // gap between indicator boxes

    int battLeftX;
    if (mode == 1) {  // percent
      char buf[6];
      snprintf(buf, sizeof(buf),"%d%%", pct);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, 0);
      display.print(buf);
    } else if (mode == 2) {  // voltage
      char buf[8];
      snprintf(buf, sizeof(buf),"%u.%02uV", batteryMilliVolts / 1000, (batteryMilliVolts % 1000) / 10);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, 0);
      display.print(buf);
    } else {  // icon — scales with lh
      const int iconH = lh;
      const int iconW = lh * 2;
      const int bm = display.isLandscape() ? 3 : 2;  // inner margin: 3px on landscape e-ink, 2px on OLED/portrait
      battLeftX = display.width() - iconW - 3;
      display.drawRect(battLeftX, 0, iconW, iconH);
      display.fillRect(battLeftX + iconW, iconH / 4, 2, iconH / 2);
      int fillW = (pct * (iconW - 2 * bm)) / 100;
      display.fillRect(battLeftX + bm, bm, fillW, iconH - 2 * bm);
    }

#ifdef PIN_BUZZER
    if (_task->isBuzzerQuiet()) {
      display.setColor(DisplayDriver::LIGHT);
      int mx = battLeftX - ind - ind_gap;
      display.fillRect(mx, 0, ind, ind_h);
      display.setColor(DisplayDriver::DARK);
      display.setCursor(mx + 1, 0);
      display.print("M");
      display.setColor(DisplayDriver::LIGHT);
      battLeftX = mx;
    }
#endif

    // BT connection indicator (left of muted/battery icons)
    int leftmostX = battLeftX;
    if (_task->isSerialEnabled()) {
      int btX = battLeftX - ind - ind_gap;
      if (_task->isBLEConnected()) {   // BT icon reflects BLE link, not USB
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(btX, 0, ind, ind_h);
        display.setColor(DisplayDriver::DARK);
        display.setCursor(btX + 1, 0);
        display.print("B");
        display.setColor(DisplayDriver::LIGHT);
      } else {
        display.setCursor(btX + 1, 0);
        display.print("b");
      }
      leftmostX = btX - ind_gap;

      // "A" indicator — left of BT; blinks on OLED, always shown on e-ink
      if (_node_prefs && _node_prefs->advert_auto_interval_sec > 0) {
        int aX = leftmostX - ind;
        bool show_a = Features::BLINK_INDICATORS ? ((millis() % 4000) < 2000) : true;
        if (show_a) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(aX, 0, ind, ind_h);
          display.setColor(DisplayDriver::DARK);
          display.setCursor(aX + 1, 0);
          display.print("A");
          display.setColor(DisplayDriver::LIGHT);
        }
        leftmostX = aX - 1;
      }

      // "G" indicator — GPS trail logging active. Same blink convention.
      if (_task->trail().isActive()) {
        int gX = leftmostX - ind;
        bool show_g = Features::BLINK_INDICATORS ? ((millis() % 4000) < 2000) : true;
        if (show_g) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(gX, 0, ind, ind_h);
          display.setColor(DisplayDriver::DARK);
          display.setCursor(gX + 1, 0);
          display.print("G");
          display.setColor(DisplayDriver::LIGHT);
        }
        leftmostX = gX - 1;
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
    display.setTextSize(1);
    const int lh      = display.getLineHeight();  // line height at sz1
    const int step    = display.lineStep();        // lh + 2
    const int dots_y  = lh + 4;                   // page-dot row: just below header
    const int content_y = dots_y + 6;             // first content row (6px gap keeps dots visible)

    // node name + battery — hidden on CLOCK page (full screen used for dashboard)
    if (_page != CLOCK) {
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
      int order[(int)Count]; int n = buildVisibleOrder(order);
      int curr_vis = 0;
      for (int i = 0; i < n; i++) if (order[i] == _page) { curr_vis = i; break; }
      int x = display.width() / 2 - 5 * (n - 1);
      for (int i = 0; i < n; i++) {
        int ds = display.isLandscape() ? 2 : 1;
        if (i == curr_vis) display.fillRect(x-ds, dots_y-ds, 2*ds+1, 2*ds+1);
        else               display.fillRect(x-ds+1, dots_y-ds+1, 2*ds-1, 2*ds-1);
        x += 10;
      }
    }

    if (_page == HomePage::CLOCK) {
      uint32_t unix_ts = _rtc->getCurrentTime();
      if (unix_ts < 1000000000UL) {
        display.setColor(DisplayDriver::LIGHT);
        display.setTextSize(1);
        int mid_y = display.height() / 2 - step;
        display.drawTextCentered(display.width() / 2, mid_y, "! No time sync");
        display.drawTextCentered(display.width() / 2, mid_y + step, "Enable GPS or");
        display.drawTextCentered(display.width() / 2, mid_y + step * 2, "connect app");
      } else {
        int8_t tz = _node_prefs ? _node_prefs->tz_offset_hours : 0;
        unix_ts += (int32_t)tz * 3600;
        time_t t = (time_t)unix_ts;
        struct tm* ti = gmtime(&t);

        char buf[24];
        display.setColor(DisplayDriver::LIGHT);
        bool show_sec = !Features::IS_EINK && (!_node_prefs || !_node_prefs->clock_hide_seconds);
        bool h12 = _node_prefs && _node_prefs->clock_12h;
        int date_y = drawClockTime(display, 0, ti, h12, show_sec);

        display.setTextSize(1);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(buf, sizeof(buf),"%s %d %s %d", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon], 1900 + ti->tm_year);
        display.drawTextCentered(display.width() / 2, date_y, buf);

        int sep_y  = date_y + lh + 1;
        int dash0  = sep_y + display.sepH() + 2;
        display.fillRect(0, sep_y, display.width(), display.sepH());

        // dashboard data fields
        if (_node_prefs) {
          refresh_sensors();
          const int FIELD_Y[3] = { dash0, dash0 + step, dash0 + step * 2 };
          for (int fi = 0; fi < 3; fi++) {
            uint8_t field = _node_prefs->dashboard_fields[fi];
            if (field == DASH_NONE) continue;

            char label[10], val[20];
            label[0] = '\0';
            val[0] = '\0';

            if (field == DASH_BATT_V) {
              strcpy(label, "Batt");
              uint16_t mv = _task->getBattMilliVolts();
              if (mv > 0) snprintf(val, sizeof(val), "%u.%02uV", mv/1000, (mv%1000)/10);
              else strcpy(val, "--");
            } else if (field == DASH_BATT_PCT) {
              strcpy(label, "Batt");
              uint16_t mv = _task->getBattMilliVolts();
              if (mv > 0) snprintf(val, sizeof(val), "%d%%",
                                   battMvToPercent(mv, _node_prefs->low_batt_mv));
              else        strcpy(val, "--");
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
      int y = content_y;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += step) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          snprintf(tmp, sizeof(tmp),"%ds", secs);
        } else if (secs < 60*60) {
          snprintf(tmp, sizeof(tmp),"%dm", secs / 60);
        } else {
          snprintf(tmp, sizeof(tmp),"%dh", secs / (60*60));
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
      // freq / sf
      display.setCursor(0, content_y);
      snprintf(tmp, sizeof(tmp),"FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, content_y + step);
      snprintf(tmp, sizeof(tmp),"BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power, noise floor
      display.setCursor(0, content_y + step * 2);
      snprintf(tmp, sizeof(tmp),"TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, content_y + step * 3);
      snprintf(tmp, sizeof(tmp),"Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawXbm((display.width() - 32) / 2, content_y,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off, 32, 32);
      const int text_y = content_y + 32 + 3;
      // The pairing PIN is BLE-specific: show it while BLE is on but not yet
      // bonded. (Gating on a plain isConnected() broke this on dual builds,
      // where it's hardcoded true.)
      const bool waiting_for_pair = _task->isSerialEnabled() && !_task->isBLEConnected() && the_mesh.getBLEPin() != 0;
      if (waiting_for_pair && !display.isLandscape()) {
        char pin_buf[16];
        snprintf(pin_buf, sizeof(pin_buf), "PIN: %d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, text_y, pin_buf);
      } else if (waiting_for_pair) {
        char pin_buf[16];
        snprintf(pin_buf, sizeof(pin_buf), "PIN: %d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, text_y, pin_buf);
        display.drawTextCentered(display.width() / 2, text_y + step, "toggle: " PRESS_LABEL);
      } else {
        display.drawTextCentered(display.width() / 2, text_y, "toggle: " PRESS_LABEL);
      }
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::LIGHT);
      display.drawXbm((display.width() - 32) / 2, content_y, advert_icon, 32, 32);
      display.drawTextCentered(display.width() / 2, content_y + 32 + 3, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = content_y;
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
        y += step;
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += step;
        display.drawTextLeftAlign(0, y, "sat");
        snprintf(buf, sizeof(buf),"%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += step;
        display.drawTextLeftAlign(0, y, "pos");
        snprintf(buf, sizeof(buf),"%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += step;
        display.drawTextLeftAlign(0, y, "alt");
        snprintf(buf, sizeof(buf),"%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += step;
      }
#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = content_y;
      refresh_sensors();

      // Enumerate the distinct telemetry types directly from the freshly
      // populated buffer. (Upstream replaced the per-sensor *_initialized flags
      // with a generic registration model, so we derive availability from what
      // querySensors() actually produced instead of asking the manager.)
      uint8_t avail_types[16];
      int avail_count = 0;
      {
        LPPReader er(sensors_lpp.getBuffer(), sensors_lpp.getSize());
        uint8_t ech, etype;
        while (er.readHeader(ech, etype) && avail_count < 16) {
          er.skipData(etype);
          bool dup = false;
          for (int k = 0; k < avail_count; k++) if (avail_types[k] == etype) { dup = true; break; }
          if (!dup) avail_types[avail_count++] = etype;
        }
      }
      bool need_scroll = avail_count > UI_RECENT_LIST_SIZE;
      int offset = need_scroll ? (sensors_scroll_offset % avail_count) : 0;
      int show_n = need_scroll ? UI_RECENT_LIST_SIZE : avail_count;

      for (int i = 0; i < show_n; i++) {
        uint8_t target = avail_types[(offset + i) % avail_count];

        // scan LPP buffer for this type
        LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());
        uint8_t ch, type;
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
            break;
          }
          r.skipData(type);
        }

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
        y += step;
      }
      if (need_scroll) sensors_scroll_offset = (sensors_scroll_offset + 1) % avail_count;
      else sensors_scroll_offset = 0;
#endif
    } else if (_page == HomePage::SETTINGS) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, content_y, "Settings");
      display.drawTextCentered(display.width() / 2, content_y + step * 2, PRESS_LABEL " to open");
    } else if (_page == HomePage::TOOLS) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, content_y, "Tools");
      display.drawTextCentered(display.width() / 2, content_y + step * 2, PRESS_LABEL " to open");
    } else if (_page == HomePage::QUICK_MSG) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, content_y, "Messages");
      int total_unread = _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount();
      if (total_unread > 0) {
        char badge[20];
        snprintf(badge, sizeof(badge), "%d unread", total_unread);
        display.drawTextCentered(display.width() / 2, content_y + step, badge);
      }
      display.drawTextCentered(display.width() / 2, content_y + step * 2, PRESS_LABEL " to open");
    } else if (_page == HomePage::FAVOURITES) {
      // Grid of pinned contacts. Layout transposes to current orientation:
      // landscape → 3×2, portrait → 2×3. Selected tile inverts via drawSelectionRow.
      // No title — node name + battery (top bar) and the page-dots indicator above
      // serve as the page identity.
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);

      const int cols    = display.isLandscape() ? 3 : 2;
      const int rows    = NodePrefs::FAVOURITES_COUNT / cols;
      const int margin  = 2;
      const int grid_y  = content_y + margin;
      const int grid_h  = display.height() - grid_y - margin;
      const int cell_w  = display.width() / cols;
      const int cell_h  = grid_h / rows;
      const int line_h  = display.getLineHeight();

      if (_fav_sel >= NodePrefs::FAVOURITES_COUNT) _fav_sel = 0;

      for (uint8_t i = 0; i < NodePrefs::FAVOURITES_COUNT; i++) {
        int row = i / cols;
        int col = i % cols;
        int cx  = col * cell_w;
        int cy  = grid_y + row * cell_h;
        bool sel = (i == _fav_sel);
        display.drawSelectionRow(cx, cy, cell_w - 1, cell_h - 1, sel);

        // Empty slot → all-zero prefix. Real keys collide with this with probability 2^-48.
        const uint8_t* prefix = _node_prefs ? _node_prefs->favourite_contacts[i] : nullptr;
        bool filled = false;
        if (prefix) {
          for (uint8_t b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++)
            if (prefix[b] != 0) { filled = true; break; }
        }

        if (filled) {
          ContactInfo ci;
          bool found = false;
          for (int idx = 0; ; idx++) {
            ContactInfo c;
            if (!the_mesh.getContactByIdx(idx, c)) break;
            if (memcmp(c.id.pub_key, prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
              ci = c; found = true; break;
            }
          }
          char name[24];
          if (found) display.translateUTF8ToBlocks(name, ci.name, sizeof(name));
          else       strncpy(name, "(gone)", sizeof(name) - 1), name[sizeof(name) - 1] = '\0';

          // Reserve space for the unread badge so the name's ellipsis lands
          // before it instead of underneath. Badge and name share one baseline.
          char badge[5] = "";
          int  bw = 0;
          if (found) {
            uint8_t unread = _task->getDMUnread(ci.id.pub_key);
            if (unread > 0) {
              snprintf(badge, sizeof(badge), "%d", unread);
              bw = display.getTextWidth(badge) + 3;  // badge + 3 px gap
            }
          }
          int name_y     = cy + (cell_h - line_h) / 2;
          int name_max_w = cell_w - 4 - bw;
          if (name_max_w < 6) name_max_w = 6;
          display.drawTextEllipsized(cx + 2, name_y, name_max_w, name);
          if (badge[0]) {
            display.setCursor(cx + cell_w - (bw - 3) - 2, name_y);
            display.print(badge);
          }
        } else {
          int plus_y = cy + (cell_h - line_h) / 2;
          display.drawTextCentered(cx + cell_w / 2, plus_y, "+");
        }
        if (sel) display.setColor(DisplayDriver::LIGHT);
      }
      if (_pin_menu.active) _pin_menu.render(display);
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, content_y + step, "hibernating...");
      } else {
        display.drawXbm((display.width() - 32) / 2, content_y, power_icon, 32, 32);
        const int text_y = content_y + 32 + 3;
        const int lh1 = display.getLineHeight();
        if (text_y + lh1 <= display.height()) {
          char hib_hint[32];
          snprintf(hib_hint, sizeof(hib_hint), "hibernate:%s", PRESS_LABEL);
          if (display.getTextWidth(hib_hint) < display.width()) {
            display.drawTextCentered(display.width() / 2, text_y, hib_hint);
          } else {
            display.drawTextCentered(display.width() / 2, text_y, "hibernate:");
            if (text_y + step + lh1 <= display.height())
              display.drawTextCentered(display.width() / 2, text_y + step, PRESS_LABEL);
          }
        }
      }
    }
    bool auto_adv = _node_prefs && _node_prefs->advert_auto_interval_sec > 0;
    // Any blinking status-bar indicator needs a 1 s refresh to animate evenly.
    bool need_blink = auto_adv || _task->trail().isActive();
    if (Features::IS_EINK) {
      // slow display: poll every 30 s; inbound msgs force immediate refresh via notify()
      return Features::HOME_REFRESH_MS;
    }
    if (_page == HomePage::CLOCK) {
      bool show_sec = !_node_prefs || !_node_prefs->clock_hide_seconds;
      return need_blink ? 1000 : (show_sec ? 1000 : 60000);
    }
    return need_blink ? 1000 : 5000;
  }

  bool handleInput(char c) override {
    // Favourites grid claims joystick UP/DOWN and inner LEFT/RIGHT; LEFT at the
    // left column and RIGHT at the right column fall through to page nav so the
    // user can still leave the page sideways.
    if (_page == HomePage::FAVOURITES) {
      // Pin picker consumes all input while open.
      if (_pin_menu.active) {
        auto res = _pin_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _pin_target_slot >= 0) {
          int idx = _pin_menu.selectedIndex();
          if (idx >= 0 && idx < _pin_count) {
            // If this contact is already pinned elsewhere, vacate that slot first.
            int existing = _task->findFavouriteSlot(_pin_keys[idx]);
            if (existing >= 0 && existing != _pin_target_slot) _task->clearFavouriteSlot(existing);
            _task->setFavouriteSlot(_pin_target_slot, _pin_keys[idx]);
            the_mesh.savePrefs();
            char alert[24];
            snprintf(alert, sizeof(alert), "Pinned to slot %d", _pin_target_slot + 1);
            _task->showAlert(alert, 800);
          }
        }
        if (res != PopupMenu::NONE) _pin_target_slot = -1;
        return true;
      }
      DisplayDriver* d = _task->getDisplay();
      const int cols = (d && d->isLandscape()) ? 3 : 2;
      const int rows = NodePrefs::FAVOURITES_COUNT / cols;
      int col = _fav_sel % cols;
      int row = _fav_sel / cols;
      if ((c == KEY_LEFT  || c == KEY_PREV) && col > 0)        { _fav_sel--;        return true; }
      if ((c == KEY_RIGHT || c == KEY_NEXT) && col < cols - 1) { _fav_sel++;        return true; }
      if (c == KEY_UP    && row > 0)                            { _fav_sel -= cols; return true; }
      if (c == KEY_DOWN  && row < rows - 1)                     { _fav_sel += cols; return true; }
      if (c == KEY_ENTER) {
        // Filled slot → open the DM directly. Empty slot waits for phase 3
        // (mini-picker); for now show the pin hint.
        NodePrefs* p = _task->getNodePrefs();
        const uint8_t* pfx = (p && _fav_sel < NodePrefs::FAVOURITES_COUNT)
                             ? p->favourite_contacts[_fav_sel] : nullptr;
        bool filled = false;
        if (pfx) for (uint8_t b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++)
          if (pfx[b]) { filled = true; break; }
        if (filled) {
          for (int idx = 0; ; idx++) {
            ContactInfo c2;
            if (!the_mesh.getContactByIdx(idx, c2)) break;
            if (memcmp(c2.id.pub_key, pfx, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
              _task->openContactDM(c2);
              return true;
            }
          }
          _task->showAlert("Contact not found", 800);
        } else {
          // Empty slot → open in-place pin picker.
          buildPinPicker(_fav_sel);
        }
        return true;
      }
      // Edge LEFT/RIGHT and unhandled keys fall through to page nav below.
    }

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
      // _task->toggleGPS();
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

  // Load persisted waypoints (table survives reboots, unlike the RAM trail).
  {
    DataStore* ds = the_mesh.getDataStore();
    if (ds) {
      File f = ds->openRead("/waypoints");
      if (f) { _waypoints.readFrom(f); f.close(); }
    }
  }
  
  // Initialize ping state
  _ping_active = false;
  _ping_tag = 0;
  _ping_sent_ms = 0;
  _ping_snr_out_x4 = 0;
  _ping_snr_back_x4 = 0;
  _ping_rtt_ms = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  settings = new SettingsScreen(this, &_kb);
  quick_msg = new QuickMsgScreen(this, &_kb);
  tools_screen  = new ToolsScreen(this);
  ringtone_edit = new RingtoneEditorScreen(this, node_prefs);
  bot_screen    = new BotScreen(this, node_prefs, &_kb);
  nearby_screen = new NearbyScreen(this);
  dashboard_config = new DashboardConfigScreen(this, node_prefs);
  auto_advert_screen = new AutoAdvertScreen(this, node_prefs);
  trail_screen       = new TrailScreen(this, &_trail);
  compass_screen     = new CompassScreen(this);
  applyBrightness();
  applyFont();
  applyRotation();
  applyFullRefreshInterval();
  setCurrScreen(splash);
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

void UITask::gotoTrailScreen() {
  ((TrailScreen*)trail_screen)->enter();
  setCurrScreen(trail_screen);
}

void UITask::gotoCompassScreen() {
  ((CompassScreen*)compass_screen)->enter();
  setCurrScreen(compass_screen);
}

void UITask::gotoAutoAdvertScreen() {
  ((AutoAdvertScreen*)auto_advert_screen)->enter();
  setCurrScreen(auto_advert_screen);
}

// Public method to handle ping result callback
void UITask::handlePingResult(uint32_t tag, int16_t snr_out_x4, int16_t snr_back_x4, uint32_t rtt_ms) {
  if (_ping_active && _ping_tag == tag) {
    _ping_snr_out_x4 = snr_out_x4;
    _ping_snr_back_x4 = snr_back_x4;
    _ping_rtt_ms = rtt_ms;
    // Release the in-flight slot immediately; the UI keeps the result values.
    clearPing();
  }
}

// Static ping callback (for MyMesh)
static void onPingResult(uint32_t tag, int16_t snr_out_x4, int16_t snr_back_x4, uint32_t rtt_ms) {
  AbstractUITask* ui = the_mesh.getUITask();
  if (ui) {
    UITask* task = static_cast<UITask*>(ui);
    task->handlePingResult(tag, snr_out_x4, snr_back_x4, rtt_ms);
  }
}

void UITask::clearPing() {
  if (_ping_tag != 0) {
    the_mesh.clearPingResult(_ping_tag);
  }
  _ping_active = false;
  _ping_tag = 0;
}

bool UITask::startPing(const uint8_t* pub_key) {
  if (_ping_active || !pub_key) return false;
  if (_node_prefs && _node_prefs->path_hash_mode > 1) {
    showAlert("Ping not supported with 3-byte path hashes", 3000);
    return false;
  }

  _ping_active = true;
  _ping_tag = 0;
  _ping_sent_ms = millis();
  _ping_snr_out_x4 = 0;
  _ping_snr_back_x4 = 0;
  _ping_rtt_ms = 0;

  // Always install the callback before sending so the response cannot race it.
  the_mesh.setPingCallback(onPingResult, NULL);
  _ping_tag = the_mesh.sendPing(pub_key, _node_prefs ? _node_prefs->path_hash_mode + 1 : 1);
  if (_ping_tag == 0) {
    clearPing();
    return false;
  }
  return true;
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

void UITask::openContactDM(const ContactInfo& ci) {
  ((QuickMsgScreen*)quick_msg)->reset();
  ((QuickMsgScreen*)quick_msg)->enterDM(ci);
  setCurrScreen(quick_msg);
}

void UITask::shareToMessage(const char* text) {
  ((QuickMsgScreen*)quick_msg)->startShare(text);
  setCurrScreen(quick_msg);
}

int UITask::getRecentDMContacts(uint8_t out[][NodePrefs::FAVOURITE_PREFIX_LEN], int max) const {
  return ((QuickMsgScreen*)quick_msg)->getRecentDMContacts(out, max);
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

void UITask::notify(UIEventType t) {
#if defined(PIN_BUZZER)
{
  SoundNotifier sn(buzzer, _node_prefs, _notif_mel_buf, sizeof(_notif_mel_buf));
  switch(t){
  case UIEventType::contactMessage:
    sn.playDM(_last_notif_dm_valid, _last_notif_dm_prefix);
    _last_notif_dm_valid = false;
    break;
  case UIEventType::channelMessage:
    sn.playCH(_last_notif_ch_idx);
    _last_notif_ch_idx = -1;
    break;
  case UIEventType::advertReceivedFlood:
  case UIEventType::advertReceivedZeroHop:
    sn.playAD(t == UIEventType::advertReceivedFlood);
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::none:
  default:
    break;
  }
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
    if (!_display->isOn() && !isClientConnected()) {   // wake for the msg unless an app (BLE/USB) is already showing it
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
  unsigned long cur_time = millis();
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

static void formatDashVal(uint8_t field, char* val, int val_len, uint16_t batt_mv,
                          uint16_t low_batt_mv, CayenneLPP* lpp = nullptr) {
  val[0] = '\0';
  switch (field) {
    case DASH_NONE: return;
    case DASH_BATT_V:
      if (batt_mv > 0) snprintf(val, val_len, "%u.%02uV", batt_mv/1000, (batt_mv%1000)/10);
      else              strcpy(val, "--");
      return;
    case DASH_BATT_PCT:
      if (batt_mv > 0) snprintf(val, val_len, "%d%%", battMvToPercent(batt_mv, low_batt_mv));
      else             strcpy(val, "--");
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
  // LPP sensor fields
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
    if (!lpp) { static CayenneLPP s_lpp(200); s_lpp.reset(); sensors.querySensors(0xFF, s_lpp); lpp = &s_lpp; }
    LPPReader r(lpp->getBuffer(), lpp->getSize());
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
  uint8_t joy_rot = _node_prefs ? _node_prefs->joystick_rotation : JOYSTICK_ROTATION;
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
    c = checkDisplayOn(rotateJoystickKey(KEY_UP, joy_rot));
  }
  ev = joystick_down.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(rotateJoystickKey(KEY_DOWN, joy_rot));
  }
#endif
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(rotateJoystickKey(KEY_LEFT, joy_rot));
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(rotateJoystickKey(KEY_LEFT, joy_rot));
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(rotateJoystickKey(KEY_RIGHT, joy_rot));
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(rotateJoystickKey(KEY_RIGHT, joy_rot));
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
  if (millis() - _analogue_pin_read_millis > 10) {
    int ev = analog_btn.check();
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
#ifdef PIN_BUZZER
      if (buzzer.isPlaying()) {
        // Keep the next render at least 300 ms away so the blocking e-ink endFrame()
        // doesn't extend the current note.  300 ms covers the slowest note at 120 BPM (1/4).
        unsigned long deadline = millis() + 300;
        if (_next_refresh < deadline) _next_refresh = deadline;
      } else {
        _next_refresh = 100;  // trigger refresh immediately
      }
#else
      _next_refresh = 100;  // trigger refresh
#endif
    } else if (_locked) {
      // Locked: eat all keys — wake window is set only when display first turns on
      _next_refresh = 0;
    }
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (_node_prefs && _node_prefs->buzzer_auto) {
    bool should_quiet = isClientConnected();   // BLE bonded or an open USB port
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
      _display->setTextSize(1);
      const int lk_lh   = _display->getLineHeight();
      const int lk_step = _display->lineStep();
      if (unix_ts < 1000000000UL) {
        _display->drawTextCentered(_display->width() / 2, _display->height() / 2 - lk_step, "No time sync");
      } else {
        int8_t tz = _node_prefs ? _node_prefs->tz_offset_hours : 0;
        unix_ts += (int32_t)tz * 3600;
        time_t t = (time_t)unix_ts;
        struct tm* ti = gmtime(&t);
        char buf[12];
        const int clk_y = 2;
        bool h12 = _node_prefs && _node_prefs->clock_12h;
        int date_y = drawClockTime(*_display, clk_y, ti, h12, /*show_sec*/false);
        _display->setTextSize(1);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(buf, sizeof(buf),"%s %d %s", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon]);
        _display->drawTextCentered(_display->width() / 2, date_y, buf);

        // Two sensor values side by side (dashboard_fields[0] and [1])
        if (_node_prefs) {
          char v0[20] = "", v1[20] = "";
          CayenneLPP* lpp_ptr = nullptr;
          uint8_t f0 = _node_prefs->dashboard_fields[0], f1 = _node_prefs->dashboard_fields[1];
          auto isLPP = [](uint8_t f) {
            return f==DASH_TEMP||f==DASH_HUM||f==DASH_PRES||f==DASH_ALT||f==DASH_LUX||f==DASH_CO2;
          };
          if (isLPP(f0) || isLPP(f1)) {
            _dash_lpp.reset(); sensors.querySensors(0xFF, _dash_lpp); lpp_ptr = &_dash_lpp;
          }
          formatDashVal(f0, v0, sizeof(v0), _batt_mv, _node_prefs->low_batt_mv, lpp_ptr);
          formatDashVal(f1, v1, sizeof(v1), _batt_mv, _node_prefs->low_batt_mv, lpp_ptr);
          if (v0[0] || v1[0]) {
            int sv_y = date_y + lk_step;
            _display->setColor(DisplayDriver::LIGHT);
            if (v0[0] && v1[0]) {
              _display->setCursor(0, sv_y);
              _display->print(v0);
              int vw = _display->getTextWidth(v1);
              _display->setCursor(_display->width() - vw, sv_y);
              _display->print(v1);
            } else {
              const char* sv = v0[0] ? v0 : v1;
              _display->drawTextCentered(_display->width() / 2, sv_y, sv);
            }
          }
        }
      }
      // Hint popup at bottom (like alert style)
      _display->setTextSize(1);
      const char* hint = _lock_seq_count == 0 ? "Hold Back + 3xEnter" :
                         _lock_seq_count == 1 ? "Enter x2 more..."   : "Enter x1 more...";
      int p = 3;
      int hy = _display->height() - lk_lh - p * 2;
      int hw = _display->getTextWidth(hint);
      int hx = (_display->width() - hw) / 2;
      _display->setColor(DisplayDriver::LIGHT);
      _display->fillRect(hx - p, hy - p, hw + p*2, lk_lh + p*2);
      _display->setColor(DisplayDriver::DARK);
      _display->setCursor(hx, hy);
      _display->print(hint);
      _display->endFrame();
      _next_refresh = millis() + Features::LOCKSCREEN_REFRESH_MS;
    } else if (!_locked && millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // alert overlay on top of any screen
        _display->setTextSize(1);
        int lh    = _display->getLineHeight();
        int pad   = 3;
        int box_h = lh + pad * 2;
        int box_w = _display->width() - 8;
        int box_x = 4;
        int box_y = (_display->height() - box_h) / 2;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(box_x, box_y, box_w, box_h);
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(box_x, box_y, box_w, box_h);
        _display->drawTextCentered(_display->width() / 2, box_y + pad, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();
    }
#if AUTO_OFF_MILLIS > 0
#ifdef KEEP_DISPLAY_ON_USB
    // Opt-in: refresh the auto-off deadline while externally powered, so the
    // timer counts from the moment external power is removed. Off by default
    // because OLED panels burn in quickly; only enable for LCD targets or
    // where the display is replaceable.
    if (board.isExternalPowered()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;
    }
#endif
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
    // Don't shut down while on external power (charging) — avoids a shutdown loop.
    if (low_mv > 0 && _batt_mv > 0 && _batt_mv < low_mv && !board.isExternalPowered()) {
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::LIGHT);
        int mid = _display->height() / 2;
        int step = _display->lineStep();
        _display->drawTextCentered(_display->width() / 2, mid - step, "Low Battery");
        _display->drawTextCentered(_display->width() / 2, mid, "Shutting down");
        _display->endFrame();
        if (_display->isEink() == false) { delay(2000); }
      }
      shutdown();
    }
    next_batt_chck = millis() + 8000;
  }

  // GPS trail sampling — runs in the background while the trail is
  // active, independent of which screen is shown. Skips silently if no GPS
  // fix; min-delta gate inside addPoint() avoids near-stationary spam.
  if (_trail.isActive() && _node_prefs != NULL
      && (int32_t)(millis() - _next_trail_sample_ms) >= 0) {
    _next_trail_sample_ms = millis() + (uint32_t)TrailStore::SAMPLING_SECS * 1000UL;
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    if (loc && loc->isValid()) {
      uint16_t md = TrailStore::minDeltaMeters(_node_prefs->trail_min_delta_idx,
                                                _node_prefs->units_imperial);
      _trail.addPoint((int32_t)loc->getLatitude(),
                      (int32_t)loc->getLongitude(),
                      (uint32_t)rtc_clock.getCurrentTime(), md);
    }
  }

  // Course-over-ground sampling — every ~1 s regardless of trail state, so the
  // heading is available to navigation even when not recording a trail.
  if ((int32_t)(millis() - _next_cog_sample_ms) >= 0) {
    _next_cog_sample_ms = millis() + 1000UL;
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    if (loc && loc->isValid()) {
      pushCogFix((int32_t)loc->getLatitude(), (int32_t)loc->getLongitude());
    }
  }
}

// Insert a GPS fix into the course-over-ground ring, rejecting gross outliers
// (a jump implying an impossible speed) so one bad fix can't swing the heading.
void UITask::pushCogFix(int32_t lat, int32_t lon) {
  static const uint32_t COG_MAX_GAP_MS = 15000;  // GPS gap longer than this → window is stale
  uint32_t now = millis();
  if (_cog_count > 0) {
    const CogFix& prev = _cog[(_cog_head + _cog_count - 1) % COG_RING];
    uint32_t dt = now - prev.ms;
    if (dt > COG_MAX_GAP_MS) {
      // GPS was lost for a while: the old fixes are far in the past, so a
      // window spanning them would imply a bogus "teleport" heading. Restart
      // the ring from this fix (the last-good _cog_deg is kept for display).
      _cog_head = 0; _cog_count = 0;
    } else if (dt > 0) {
      float dist_m = geo::haversineKm(prev.lat, prev.lon, lat, lon) * 1000.0f;
      float speed  = dist_m / (dt / 1000.0f);   // m/s
      if (speed > 50.0f) return;                 // > 180 km/h between fixes → reject
    }
  }
  int pos;
  if (_cog_count < COG_RING) { pos = (_cog_head + _cog_count) % COG_RING; _cog_count++; }
  else { pos = _cog_head; _cog_head = (_cog_head + 1) % COG_RING; }
  _cog[pos].lat = lat; _cog[pos].lon = lon; _cog[pos].ms = now;
}

bool UITask::currentCourse(int& deg_out) const {
  static const float COG_MIN_MOVE_M = 6.0f;   // window must span ≥ this to be a real heading
  if (_cog_count < 2) {
    if (_cog_deg >= 0) { deg_out = _cog_deg; return true; }  // hold last good
    return false;
  }
  const CogFix& oldest = _cog[_cog_head];
  const CogFix& newest = _cog[(_cog_head + _cog_count - 1) % COG_RING];
  float span_m = geo::haversineKm(oldest.lat, oldest.lon, newest.lat, newest.lon) * 1000.0f;
  if (span_m < COG_MIN_MOVE_M) {
    if (_cog_deg >= 0) { deg_out = _cog_deg; return true; }  // standing still → hold last
    return false;
  }
  // Cache as last-good (mutable-free: recompute is cheap, but keep _cog_deg fresh).
  const_cast<UITask*>(this)->_cog_deg =
      geo::bearingDeg(oldest.lat, oldest.lon, newest.lat, newest.lon);
  deg_out = _cog_deg;
  return true;
}

bool UITask::currentLocation(int32_t& lat, int32_t& lon) const {
  LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
  if (loc && loc->isValid()) {
    lat = (int32_t)loc->getLatitude();
    lon = (int32_t)loc->getLongitude();
    return true;
  }
  return false;
}

void UITask::saveWaypoints() {
  DataStore* ds = the_mesh.getDataStore();
  if (!ds) return;
  File f = ds->openWrite("/waypoints");
  if (!f) return;
  _waypoints.writeTo(f);
  f.close();
}

bool UITask::addWaypoint(int32_t lat, int32_t lon, uint32_t ts, const char* label) {
  if (_waypoints.full()) { showAlert("Waypoints full", 1000); return false; }
  if (_waypoints.add(lat, lon, ts, label)) {
    saveWaypoints();
    showAlert("Waypoint saved", 800);
    return true;
  }
  showAlert("Waypoints full", 1000);
  return false;
}

bool UITask::addWaypoint(int32_t lat, int32_t lon, const char* label) {
  return addWaypoint(lat, lon, (uint32_t)rtc_clock.getCurrentTime(), label);
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
      _lock_seq_count = 0;
      _lock_seq_used = false;
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
  MESH_DEBUG_PRINTLN("UITask: double-click triggered");
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  checkDisplayOn(c);
  toggleBuzzer();
  return 0;
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
  radio_driver.setTxPower(_node_prefs->tx_power_dbm);
}

void UITask::applyPowerSave() {
  if (_node_prefs == NULL) return;
  radio_driver.setPowerSaving(_node_prefs->rx_powersave);
}

void UITask::applyBrightness() {
  if (_display != NULL && _node_prefs != NULL) {
    _display->setBrightness(_node_prefs->display_brightness);
  }
}

void UITask::applyFont() {
  if (_display != NULL && _node_prefs != NULL) {
    _display->setLemonFont(_node_prefs->use_lemon_font != 0);
    _next_refresh = 0;
  }
}

void UITask::applyRotation() {
  if (_display != NULL && _node_prefs != NULL) {
    _display->setDisplayRotation(_node_prefs->display_rotation);
    _next_refresh = 0;
  }
}

void UITask::applyFullRefreshInterval() {
  if (_display != NULL && _node_prefs != NULL) {
    static const uint8_t OPTS[] = { 0, 5, 10, 20, 30 };
    static const int OPTS_COUNT = 5;
    uint8_t idx = _node_prefs->eink_full_refresh_every;
    if (idx >= OPTS_COUNT) idx = 0;
    _display->setFullRefreshInterval(OPTS[idx]);
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
  if (level > 0) buzzer.playForced("Vol:d=16,o=6,b=120:c");
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
  if (mode == 2) { buzzer.quiet(isClientConnected()); }
  static const char* labels[] = { "Buzzer: ON", "Buzzer: OFF", "Buzzer: Auto" };
  showAlert(labels[mode], 800);
  _next_refresh = 0;
#endif
}
