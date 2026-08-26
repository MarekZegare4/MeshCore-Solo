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
// Upstream MeshCore version, shown on the splash screen. Most variants set it in
// their platformio.ini; the ones that don't used to fail to compile this file
// outright rather than fall back, which quietly made every ui-new env on those
// boards unbuildable (heltec v3/v4, thinknode m1/m5, mesh pocket, techo).
#ifndef MESHCORE_VERSION
  #define MESHCORE_VERSION    "1.17"
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
#include "GfxUtils.h"   // gfx::drawLine — connects trail points on the Home map preview

// Blinking status indicators: on for the first half of a 4 s cycle, but e-ink
// can't repaint fast enough to blink, so it shows them steadily.
static inline bool blinkOn() {
  return Features::BLINK_INDICATORS ? ((millis() % 4000) < 2000) : true;
}

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

    // Solo firmware version: strip the commit-hash suffix build.sh always
    // appends as the LAST dash-segment (v1.15-solo.1-abcdef -> v1.15-solo.1).
    // Must be the last dash, not the first: a tag like v1.21-rc1 has a dash
    // of its own before the commit hash gets appended.
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strrchr(ver, '-');
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
      snprintf(solo_label, sizeof(solo_label), "Solo %s", _solo_ver);
    else
      snprintf(solo_label, sizeof(solo_label), "Solo");
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


// ── Screen fragments — included into THIS translation unit only ───────────────
// These headers are not standalone: they are compiled solely as part of
// UITask.cpp, in the order below. Two consequences a new screen must respect:
//   • Order matters. A `static inline` helper (drawList, msgReplyBody, geo::…)
//     or a shared scratch buffer (FullscreenMsgView's s_wrap_*) is only visible
//     to fragments included *after* the one that defines it. Add new screens
//     after their dependencies.
//   • Single-TU only. Some fragments define external-linkage symbols at file
//     scope (e.g. NearbyScreen::FILTER_LABELS), so including any of them from a
//     second .cpp is a duplicate-symbol link error. Keep them UITask-internal;
//     anything genuinely shareable belongs in a real header (icons.h, GeoUtils.h).
#include "FullscreenMsgView.h"
#include "SensorPlaceholders.h"
#include "SettingsScreen.h"
#include "MessageHistory.h"   // RAM history rings (DM + channel) used by MessagesScreen
#include "MessagesScreen.h"

// ── Custom screens (separate files to ease upstream merges) ───────────────────
#include "RingtoneEditorScreen.h"
#include "BotScreen.h"
#include "AdminScreen.h"
#include "NearbyScreen.h"
#include "DashboardConfigScreen.h"
#include "AutoAdvertScreen.h"
#include "LiveShareScreen.h"
#include "LocatorScreen.h"
#include "TrailScreen.h"
#include "CompassScreen.h"
#include "DiagnosticsScreen.h"
#include "RepeaterScreen.h"
#if defined(PIN_GPIO1)
#include "GpioScreen.h"
#endif
#include "ToolsScreen.h"
#include "ClockToolsScreen.h"   // Alarm / Timer / Stopwatch (Clock page › Enter)

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
    MAP,
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
    if (page == MAP)       return NodePrefs::HPB_MAP;
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
      case NodePrefs::HPB_MAP:       return MAP;
      default: return -1;
    }
  }

  bool isPageVisible(int page) const {
    if (page == RECENT) return false;  // Recent adverts folded into Nearby Nodes; page retired
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
    const int ind_h   = display.isSingleFont() ? lh - 2 : lh;
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
    } else {  // icon — scales with lh, same box height as the status icons beside it (ind_h)
      const int iconH = ind_h;
      const int iconW = lh * 2;
      const int bm = display.isLandscape() ? 3 : 2;  // inner margin: 3px on landscape e-ink, 2px on OLED/portrait
      battLeftX = display.width() - iconW - 3;
      display.drawRect(battLeftX, 0, iconW, iconH);
      // Nub height/2, vertically centred by remaining-space/2 rather than a flat
      // iconH/4 margin — the flat form only centres when iconH is a multiple of
      // 4 (true for the old built-in font's lh=8, false for misc-fixed's 7/9),
      // so it visibly drifted off-centre once the box height changed.
      const int nub_h = iconH / 2;
      display.fillRect(battLeftX + iconW, (iconH - nub_h) / 2, 2, nub_h);
      int fillW = (pct * (iconW - 2 * bm)) / 100;
      display.fillRect(battLeftX + bm, bm, fillW, iconH - 2 * bm);
    }

    // Secondary status icons, laid out right→left in PRIORITY order so a crowded
    // bar sheds its least-important cues instead of crushing the node name. Once
    // an icon won't fit above the reserved name area, every lower-priority icon
    // after it is dropped too (the list is ordered high→low). A blinking icon
    // still reserves its slot while off, so the name width doesn't flicker.
    //
    // Priority: BT > GPS fix > alarm > mute > auto-advert > trail > live-share >
    // repeater. Battery (drawn above) is always rightmost. The background modes
    // (advert / trail / live-share / repeater) stay outside any BT gate — they
    // keep running with Bluetooth off, so their cue must not vanish with it.
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    bool gps_on  = loc && _node_prefs && _node_prefs->gps_enabled;
    // Blinks while GPS is napping between duty-cycle wakes -- same convention
    // the background-mode icons below already use for "running, but not busy
    // right this instant".
    bool gps_napping = _sensors && _sensors->isGpsDutySleeping();
    bool mute_on = false;
#ifdef PIN_BUZZER
    mute_on = _task->isBuzzerQuiet();
#endif
    struct Sicon { bool active; const MiniIcon* icon; bool boxed; bool blink; };
    const Sicon icons[] = {
      { _task->isSerialEnabled(), &ICON_BLUETOOTH, _task->isSerialEnabled() && _task->isBLEConnected(), false },
      { gps_on,                   &ICON_GPS,        gps_on && loc->isValid(),                            gps_napping },
      { _node_prefs && _node_prefs->alarm_on,                      &ICON_ALARM,       true, false },
      { mute_on,                                                   &ICON_MUTE,        true, false },
      { _node_prefs && _node_prefs->advert_auto_interval_sec > 0,  &ICON_ADVERT,      true, true  },
      { _task->trail().isActive(),                                 &ICON_TRAIL,       true, true  },
      { _node_prefs && _node_prefs->loc_share_enabled,             &ICON_MAP_CONTACT, true, true  },
      { _node_prefs && _node_prefs->client_repeat,                 &ICON_REPEATER,    true, true  },
    };

    int x = battLeftX;
    const int name_min = display.getCharWidth() * 5;   // always keep ~5 chars for the name
    for (const Sicon& s : icons) {
      if (!s.active) continue;
      int ix = x - ind - ind_gap;
      if (ix < name_min) break;                        // out of room — drop this + all lower priority
      if (!s.blink || blinkOn()) {
        if (s.boxed) drawBoxedIcon(display, ix, ind, ind_h, *s.icon);
        else         drawSlotIcon(display, ix, ind, ind_h, *s.icon);
      }
      x = ix;
    }
    return x;
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
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

  // Compact map preview for the Home "Map" page: own position, the GPS trail,
  // and live-tracked contacts (◆) folded into one auto-scaled box. A simplified
  // cousin of TrailScreen's map (no grid/labels, no break markers) so the home
  // carousel stays light. Returns false (and draws nothing) when there's
  // nothing to show.
  bool drawMapPreview(DisplayDriver& display, int ax, int ay, int aw, int ah) {
    if (aw < 8 || ah < 8) return false;
    bool init = false;
    int32_t mnla = 0, mxla = 0, mnlo = 0, mxlo = 0;
    auto fold = [&](int32_t la, int32_t lo) {
      if (!init) { mnla = mxla = la; mnlo = mxlo = lo; init = true; }
      else { if (la < mnla) mnla = la; if (la > mxla) mxla = la;
             if (lo < mnlo) mnlo = lo; if (lo > mxlo) mxlo = lo; }
    };
    TrailStore& tr = _task->trail();
    if (!tr.empty()) { int32_t a, b, c, d; tr.boundingBox(a, b, c, d); fold(a, b); fold(c, d); }
    LiveTrackStore& lt = _task->liveTrack();
    uint32_t now = rtc_clock.getCurrentTime();
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++)
      if (lt.isActive(i, now)) fold(lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6);
    int32_t mla, mlo;
    bool have_gps = _task->currentLocation(mla, mlo);
    if (have_gps) fold(mla, mlo);
    int32_t tla, tlo;
    bool have_tgt = _task->activeTargetPos(tla, tlo);   // active Locator/Nav target
    if (have_tgt) fold(tla, tlo);
    if (!init) return false;

    // North marker — top-right, the same mini-icon as the full Trail map.
    display.setColor(DisplayDriver::LIGHT);
    {
      const int ns = miniIconScale(display);
      miniIconDrawTop(display, ax + aw - ICON_MAP_NORTH.w * ns - 1, ay + 1, ICON_MAP_NORTH);
    }

    int cx = ax + aw / 2, cy = ay + ah / 2;
    // Degenerate: one coincident point — just centre the markers.
    if (mnla == mxla && mnlo == mxlo) {
      for (int i = 0; i < LiveTrackStore::CAPACITY; i++)
        if (lt.isActive(i, now)) { miniIconDrawCentered(display, cx, cy, ICON_MAP_CONTACT); break; }
      if (have_gps || !tr.empty()) miniIconDrawCentered(display, cx, cy, ICON_MAP_CURRENT);
      if (have_tgt) miniIconDrawCentered(display, cx, cy, ICON_MAP_TARGET);   // highlight on top
      return true;
    }
    float avg_lat_rad = ((mnla + mxla) / 2.0e6f) * (float)M_PI / 180.0f;
    float lon_scale = cosf(avg_lat_rad); if (lon_scale < 0.05f) lon_scale = 0.05f;
    float lat_span = (float)(mxla - mnla);
    float lon_span = (float)(mxlo - mnlo) * lon_scale;
    float slat = (float)ah / (lat_span > 0 ? lat_span : 1.0f);
    float slon = (float)aw / (lon_span > 0 ? lon_span : 1.0f);
    float scale = (slat < slon) ? slat : slon;
    int off_x = ax + (aw - (int)(lon_span * scale)) / 2;
    int off_y = ay + (ah - (int)(lat_span * scale)) / 2;
    auto project = [&](int32_t la, int32_t lo, int& px, int& py) {
      px = off_x + (int)((float)(lo - mnlo) * lon_scale * scale);
      py = off_y + (int)((float)(mxla - la) * scale);
    };
    // Trail as a connected line, matching the full Trail map (shared helper —
    // see gfx::drawTrail); no break marker here, just a silent gap.
    gfx::drawTrail(display, tr, project, [](int, int, int, int) {});
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
      if (!lt.isActive(i, now)) continue;
      int px, py; project(lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6, px, py);
      miniIconDrawCentered(display, px, py, ICON_MAP_CONTACT);
    }
    if (have_gps) { int px, py; project(mla, mlo, px, py); miniIconDrawCentered(display, px, py, ICON_MAP_CURRENT); }
    // Active target flag drawn last so it stays legible even atop a contact/own dot.
    if (have_tgt) { int px, py; project(tla, tlo, px, py); miniIconDrawCentered(display, px, py, ICON_MAP_TARGET); }

    // Bottom-left scale reference, always shown — distance to the active
    // target (or else the nearest live-tracked contact) now lives on the
    // status line below instead (see statusDistanceKm() / render()), so this
    // corner is free for it.
    {
      display.setColor(DisplayDriver::LIGHT);
      int ty = ay + ah - display.getLineHeight();
      static const float M_PER_1E6 = 0.11132f;            // metres per 1e-6° lat
      float ppm = scale / M_PER_1E6;                       // pixels per metre
      if (ppm > 0.0f) {
        bool imp = _task->useImperial();
        static const float MET_M[] = { 5,10,25,50,100,250,500,1000,2000,5000,10000,25000,50000 };
        static const char* MET_L[] = { "5m","10m","25m","50m","100m","250m","500m","1km","2km","5km","10km","25km","50km" };
        static const float IMP_M[] = { 4.572f,15.24f,30.48f,76.2f,152.4f,402.34f,804.67f,1609.34f,4828.0f,16093.4f,80467.2f };
        static const char* IMP_L[] = { "15ft","50ft","100ft","250ft","500ft","1/4mi","1/2mi","1mi","3mi","10mi","50mi" };
        const float* M = imp ? IMP_M : MET_M;
        const char* const* L = imp ? IMP_L : MET_L;
        int N = imp ? (int)(sizeof(IMP_M) / sizeof(IMP_M[0])) : (int)(sizeof(MET_M) / sizeof(MET_M[0]));
        float target = 8.0f / ppm;                         // short reference tick, not 1/3 of the width
        int sel = 0;
        for (int i = N - 1; i >= 0; i--) if (M[i] <= target) { sel = i; break; }
        int barpx = (int)(M[sel] * ppm + 0.5f);
        if (barpx < 5)        barpx = 5;
        if (barpx > aw / 4)   barpx = aw / 4;
        int bx = ax + 1, mid = ty + display.getLineHeight() / 2;
        display.fillRect(bx, mid, barpx, 1);                // single tick, on the text baseline
        display.setCursor(bx + barpx + 2, ty);
        display.print(L[sel]);
      }
    }
    return true;
  }

  // Distance shown on the MAP status line. The active Locator/Nav target
  // takes priority — that's what the flag on the mini-map is pointing at,
  // and it's the only way a waypoint target ever gets a distance readout
  // here (a waypoint isn't a live-tracked contact). Falls back to the
  // nearest live-tracked ([LOC]-sharing) contact when no target is set.
  // -1 when we don't have a fix or nothing to measure against.
  float statusDistanceKm() {
    int32_t mla, mlo;
    if (!_task->currentLocation(mla, mlo)) return -1.0f;
    int32_t tla, tlo;
    if (_task->activeTargetPos(tla, tlo)) return geo::haversineKm(mla, mlo, tla, tlo);
    LiveTrackStore& lt = _task->liveTrack();
    uint32_t now = rtc_clock.getCurrentTime();
    float nearest_km = -1.0f;
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
      if (!lt.isActive(i, now)) continue;
      float d = geo::haversineKm(mla, mlo, lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6);
      if (nearest_km < 0.0f || d < nearest_km) nearest_km = d;
    }
    return nearest_km;
  }

  // Small 5x5 glyph shown in the page-indicator row for each HomePage.
  static const MiniIcon* pageIcon(int page) {
    switch (page) {
      case CLOCK:      return &ICON_PG_CLOCK;
      case FAVOURITES: return &ICON_PG_STAR;
      case RECENT:     return &ICON_PG_RECENT;
      case RADIO:      return &ICON_PG_RADIO;
      case BLUETOOTH:  return &ICON_PG_BT;
      case ADVERT:     return &ICON_PG_ADVERT;
#if ENV_INCLUDE_GPS == 1
      case GPS:        return &ICON_PG_GPS;
#endif
#if UI_SENSORS_PAGE == 1
      case SENSORS:    return &ICON_PG_SENSORS;
#endif
      case SETTINGS:   return &ICON_PG_SETTINGS;
      case MAP:        return &ICON_PG_MAP;
      case TOOLS:      return &ICON_PG_TOOLS;
      case QUICK_MSG:  return &ICON_PG_MSG;
      case SHUTDOWN:   return &ICON_PG_POWER;
    }
    return nullptr;
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    display.setTextSize(1);
    const int lh      = display.getLineHeight();  // line height at sz1
    const int step    = display.lineStep();        // lh + 2
    // Page-indicator row: small (5px) page icons replace the old dots. Centre and
    // gap scale with the font so the band clears the header above and content
    // below (identical to the old lh+4 / +6 dots layout at 1x).
    const int pg_half   = (5 * miniIconScale(display) + 1) / 2;
    const int dots_y    = lh + pg_half + 1;       // icon-row centre, below the header
    const int content_y = dots_y + pg_half + 3;   // first content row, below the icons

    // node name + battery — hidden on CLOCK page (full screen used for dashboard)
    if (_page != CLOCK) {
      display.setColor(DisplayDriver::LIGHT);
      char filtered_name[sizeof(_node_prefs->node_name)];
      display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
      int rightEdge = renderBatteryIndicator(display, _task->getBattMilliVolts());
      display.setColor(DisplayDriver::LIGHT);
      // Only show the live-power readout when APC is actually controlling power —
      // not merely when the pref is set. While repeating APC is suppressed and
      // power is pinned to the ceiling, so apcActive() is false and the name bar
      // drops the readout (matching the "--" lock in Settings).
      if (the_mesh.apcActive()) {
        char pwr_buf[8];
        snprintf(pwr_buf, sizeof(pwr_buf), "%ddB", (int)radio_driver.getTxPower());
        int pwr_w = display.getTextWidth(pwr_buf);
        display.drawTextEllipsized(0, 0, rightEdge - 2 - pwr_w - 2, filtered_name);
        display.drawTextRightAlign(rightEdge - 2, 0, pwr_buf);
      } else {
        display.drawTextEllipsized(0, 0, rightEdge - 2, filtered_name);
      }
    }

    // ensure current page is visible (e.g. after settings change)
    if (!isPageVisible(_page)) _page = navPage(_page, +1);

    // curr page indicator — a row of small page icons, one per visible page, with
    // the current page underlined. Hidden on CLOCK (full screen used for dashboard).
    if (_page != CLOCK) {
      int order[(int)Count]; int n = buildVisibleOrder(order);
      int curr_vis = 0;
      for (int i = 0; i < n; i++) if (order[i] == _page) { curr_vis = i; break; }
      const int s        = miniIconScale(display);
      const int icon_w   = 5 * s;
      int pitch = icon_w + 5 * s;                       // comfortable spacing
      if (n > 1) {                                      // shrink to fit if many pages
        int fit = (display.width() - icon_w) / (n - 1);
        if (fit < pitch) pitch = fit;
      }
      int x = display.width() / 2 - pitch * (n - 1) / 2;
      for (int i = 0; i < n; i++) {
        const MiniIcon* ic = pageIcon(order[i]);
        if (ic) miniIconDrawCentered(display, x, dots_y, *ic);
        if (i == curr_vis)                              // underline the current page
          display.fillRect(x - icon_w / 2, dots_y + pg_half + 1, icon_w, s);
        x += pitch;
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

        // Alarm armed: a small bell in the top-left corner. The status bar (and
        // its bell) is hidden on this page, so signal the armed alarm here. Just
        // the glyph — no time text — so it stays clear of the centred clock
        // digits (which can reach the corner when seconds are shown), matching
        // the icon-only status-bar indicator. The exact time is in Clock Tools.
        if (_node_prefs && _node_prefs->alarm_on) {
          display.setColor(DisplayDriver::LIGHT);
          miniIconDrawTop(display, 0, 0, ICON_ALARM);
        }

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
      snprintf(tmp, sizeof(tmp),"TX: %ddBm", radio_driver.getTxPower());   // live value (reflects APC)
      display.print(tmp);
      display.setCursor(0, content_y + step * 3);
      if (radio_driver.getPowerSaving()) {   // duty-cycle RX doesn't sample the floor
        snprintf(tmp, sizeof(tmp),"Noise floor: n/a");
      } else {
        snprintf(tmp, sizeof(tmp),"Noise floor: %d", radio_driver.getNoiseFloor());
      }
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
    } else if (_page == HomePage::MAP) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      // Mini-map preview filling the page, with one status line at the bottom.
      int info_y = display.height() - step;
      int area_h = info_y - content_y - 2;
      bool drew = drawMapPreview(display, 2, content_y, display.width() - 4, area_h);
      char left[20], right[16] = {0};
      uint32_t now_m = rtc_clock.getCurrentTime();
      LiveTrackStore& lt = _task->liveTrack();
      int trk = lt.active(now_m);
      // Fix state lives in the top-bar GPS icon. Track count plus an arrow +
      // distance (to the active target, else the nearest live-tracked
      // contact) share this one status line.
      snprintf(left, sizeof(left), "Track:%d", trk);
      float nearest_km = statusDistanceKm();
      if (nearest_km >= 0.0f) geo::fmtDist(right, sizeof(right), nearest_km, _task->useImperial());
      display.setColor(DisplayDriver::LIGHT);
      if (!drew)
        display.drawTextCentered(display.width() / 2, content_y + area_h / 2, "No GPS / no trail");
      if (right[0]) {
        // Manual layout (not drawTextCentered) so the arrow mini-icon sits
        // inline between the two text runs.
        const int s = miniIconScale(display);
        const int gap = 3;
        int lw = display.getTextWidth(left);
        int iw = ICON_MAP_ARROW.w * s;
        int rw = display.getTextWidth(right);
        int x = display.width() / 2 - (lw + gap + iw + gap + rw) / 2;
        display.setCursor(x, info_y);
        display.print(left);
        miniIconDrawTop(display, x + lw + gap, info_y + (lh - ICON_MAP_ARROW.h * s) / 2, ICON_MAP_ARROW);
        display.setCursor(x + lw + gap + iw + gap, info_y);
        display.print(right);
      } else {
        display.drawTextCentered(display.width() / 2, info_y, left);
      }
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

      bool fav_changed = false;   // a stale (gone) slot was pruned this pass → persist once after the loop
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

        ContactInfo ci;
        bool has_contact = false;
        if (filled) {
          for (int idx = 0; ; idx++) {
            ContactInfo c;
            if (!the_mesh.getContactByIdx(idx, c)) break;
            if (memcmp(c.id.pub_key, prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
              ci = c; has_contact = true; break;
            }
          }
          if (!has_contact && _node_prefs) {
            // Pinned contact is gone — prefs outlived the contact list (e.g. a
            // wiped /contacts3; onContactRemoved only catches a live delete).
            // Clear the stale slot so it renders as an empty "+" tile (below)
            // instead of "(gone)". Persisted once after the loop.
            memset(_node_prefs->favourite_contacts[i], 0, NodePrefs::FAVOURITE_PREFIX_LEN);
            fav_changed = true;
          }
        }

        if (has_contact) {
          char name[24];
          display.translateUTF8ToBlocks(name, ci.name, sizeof(name));

          // Reserve space for the unread badge so the name's ellipsis lands
          // before it instead of underneath. Badge and name share one baseline.
          uint8_t unread = _task->getDMUnread(ci.id.pub_key);
          int  bw = unread > 0 ? display.unreadBadgeWidth(unread) + 3 : 0;  // badge + 3 px gap
          int name_y     = cy + (cell_h - line_h) / 2;
          int name_max_w = cell_w - 4 - bw;
          if (name_max_w < 6) name_max_w = 6;
          display.drawTextEllipsized(cx + 2, name_y, name_max_w, name);
          if (unread > 0)
            display.drawUnreadBadge(cx + cell_w - 2, name_y, unread, sel);
        } else {
          int plus_y = cy + (cell_h - line_h) / 2;
          display.drawTextCentered(cx + cell_w / 2, plus_y, "+");
        }
        if (sel) display.setColor(DisplayDriver::LIGHT);
      }
      // Persist any pruned slots once, outside the loop — a render pass can clear
      // several gone tiles but only one flash write is needed. Self-healing: once
      // cleared, the slot is empty next frame so this can't re-fire per frame.
      if (fav_changed) the_mesh.savePrefs();
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
    // Any blinking status-bar indicator needs a 1 s refresh to animate evenly —
    // but the status bar (and its icons) is hidden on the CLOCK page, so don't
    // pay the 1 s cadence there for icons that aren't drawn.
    bool repeating  = _node_prefs && _node_prefs->client_repeat;
    bool loc_sharing = _node_prefs && _node_prefs->loc_share_enabled;
    bool need_blink = (_page != HomePage::CLOCK) &&
                       (auto_adv || _task->trail().isActive() || repeating || loc_sharing);
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
    if (c == KEY_ENTER && _page == HomePage::MAP) {
      _task->gotoMapScreen();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::TOOLS) {
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::QUICK_MSG) {
      _task->gotoMessagesScreen();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;  // need to wait for button to be released
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::CLOCK) {
      _task->gotoClockTools();   // Alarm / Timer / Stopwatch
      return true;
    }
    if (c == KEY_CONTEXT_MENU && _page == HomePage::CLOCK) {
      _task->gotoDashboardConfig();
      return true;
    }
    if (c == KEY_CONTEXT_MENU && _page == HomePage::MAP) {
      _task->quickShareMyLocation();
      return true;
    }
    return false;
  }
};


void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  _kb.prefs = node_prefs;
  uint32_t aoff = autoOffMillis();
  _auto_off = millis() + (aoff > 0 ? aoff : AUTO_OFF_MILLIS);

#if defined(ENV_PIN_SDA) && defined(ENV_PIN_SCL)
  // Wire1 is already brought up by sensors.begin() (EnvironmentSensorManager),
  // which runs before this -- just probe for a CardKB sitting on it.
  Wire1.beginTransmission(0x5F);
  _has_cardkb = (Wire1.endTransmission() == 0);
#endif

#if defined(PIN_HALL_SENSOR)
  // Internal pull matches the default polarity: pulled up so an active-low
  // sensor reads HIGH at rest, pulled down so an active-high one reads LOW at
  // rest. Most reed/Hall breakouts are open-drain, active-low -- HALL_ACTIVE_HIGH
  // is only for modules wired the other way.
  pinMode(PIN_HALL_SENSOR, HALL_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  _hall_magnet_present = HALL_ACTIVE_HIGH ? (digitalRead(PIN_HALL_SENSOR) == HIGH)
                                           : (digitalRead(PIN_HALL_SENSOR) == LOW);
  if (_hall_magnet_present) _locked = true;   // booting with the cover already closed
#endif

#if defined(PIN_USER_BTN)
  user_btn.begin();
#endif
#if UI_HAS_JOYSTICK
  // The directional joystick + Back share the same MomentaryButton machinery as
  // user_btn but were never begin()'d — they only worked because the pins
  // default to INPUT and the board has external pulls. That left them on the
  // polling path: with BUTTON_USE_INTERRUPTS (e-ink) they'd silently never
  // attach a GPIOTE channel, so edges landing during a blocking panel refresh
  // were lost. begin() sets pinMode and claims an IRQ slot for each.
  joystick_left.begin();
  joystick_right.begin();
  back_btn.begin();
#if UI_HAS_JOYSTICK_UPDOWN
  joystick_up.begin();
  joystick_down.begin();
#endif
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
  messages_screen = new MessagesScreen(this, &_kb);
  tools_screen  = new ToolsScreen(this);
  ringtone_edit = new RingtoneEditorScreen(this, node_prefs);
  bot_screen    = new BotScreen(this, node_prefs, &_kb);
  admin_screen  = new AdminScreen(this);
  nearby_screen = new NearbyScreen(this);
  dashboard_config = new DashboardConfigScreen(this, node_prefs);
  auto_advert_screen = new AutoAdvertScreen(this, node_prefs);
  live_share_screen = new LiveShareScreen(this, node_prefs);
  locator_screen  = new LocatorScreen(this, node_prefs);
  trail_screen       = new TrailScreen(this, &_trail);
  compass_screen     = new CompassScreen(this);
  diag_screen        = new DiagnosticsScreen(this);
  repeater_screen    = new RepeaterScreen(this);
  clock_tools        = new ClockToolsScreen(this, node_prefs);
#if defined(PIN_GPIO1)
  gpio_screen        = new GpioScreen(this, node_prefs);
#endif
  applyBrightness();
  applyRotation();
  applyFullRefreshInterval();
  applyAllGpioModes();   // restore persisted pin modes to hardware before any UI/bot use
  setCurrScreen(splash);
}

// onShow() is invoked by setCurrScreen(), so most navigators are just that.
void UITask::gotoSettingsScreen()  { setCurrScreen(settings); }
void UITask::gotoToolsScreen()     { setCurrScreen(tools_screen); }
void UITask::gotoBotScreen()       { setCurrScreen(bot_screen); }
void UITask::gotoNearbyScreen()    { setCurrScreen(nearby_screen); }

void UITask::pickAdminTarget() {
  setCurrScreen(nearby_screen);   // runs NearbyScreen::onShow()'s reset first
  ((NearbyScreen*)nearby_screen)->startPickAdminTarget();
}

void UITask::openAdminFor(const ContactInfo& ci, bool from_picker) {
  setCurrScreen(admin_screen);   // runs AdminScreen::onShow()'s reset first
  ((AdminScreen*)admin_screen)->startFor(ci, from_picker);
}
void UITask::gotoDashboardConfig() { setCurrScreen(dashboard_config); }
void UITask::gotoTrailScreen()     { setCurrScreen(trail_screen); }
void UITask::gotoCompassScreen()   { setCurrScreen(compass_screen); }
void UITask::gotoDiagnosticsScreen() { setCurrScreen(diag_screen); }
void UITask::gotoRepeaterScreen()  { setCurrScreen(repeater_screen); }
void UITask::gotoClockTools()      { setCurrScreen(clock_tools); }
void UITask::gotoGpioScreen() {
#if defined(PIN_GPIO1)
  setCurrScreen(gpio_screen);
#endif
}
void UITask::gotoLiveShareScreen() { setCurrScreen(live_share_screen); }

// ── Clock tools engine (alarm / countdown / ring) ───────────────────────────
// Lives here, not in ClockToolsScreen, so it fires regardless of the current
// screen. The melody overrides mute (playMelody → buzzer.playForced); the ring
// auto-stops after CLOCK_RING_MS if no key dismisses it (see UITask::loop).
static const char*    CLOCK_ALARM_MELODY       = "alarm:d=8,o=6,b=125:c,c,c,c,p,c,c,c,c,p";
static const uint32_t CLOCK_RING_MS            = 60000;
static const uint32_t CLOCK_ALARM_CATCHUP_SECS = 6 * 3600;  // fire late up to 6 h, else reschedule

void UITask::wakeForAlarm() {
  if (_display != NULL) _display->turnOn();
  // Locked: the lock-screen blanking check (loop()) turns the display straight
  // back off once _lock_wake_until is in the past — which it always is by the
  // time an alarm fires. Hold the wake window open for the whole ring so the
  // lock screen (and its alert overlay) stays visible while ringing.
  if (_locked) _lock_wake_until = millis() + CLOCK_RING_MS;
  _next_refresh = 0;   // draw the alert overlay immediately
}

// Next absolute wall instant matching alarm_hour:alarm_min in local time,
// strictly after now_wall (an alarm set to the current minute waits a day).
// With alarm_repeat_mask == 0 that's just tomorrow's occurrence (one-shot).
// With a repeat mask set, scan today..+6 days for the next weekday whose bit
// is set (struct tm's tm_wday convention, same as the mask) — today counts
// only if its time hasn't already passed.
uint32_t UITask::computeAlarmNextFire(uint32_t now_wall) const {
  int tz = _node_prefs ? _node_prefs->tz_offset_hours : 0;
  int64_t now_local = (int64_t)now_wall + (int64_t)tz * 3600;
  time_t t = (time_t)now_local;
  struct tm* ti = gmtime(&t);
  int64_t sod = ti->tm_hour * 3600 + ti->tm_min * 60 + ti->tm_sec;  // secs since local midnight
  int64_t midnight = now_local - sod;
  int64_t time_of_day = (int64_t)_node_prefs->alarm_hour * 3600 + (int64_t)_node_prefs->alarm_min * 60;
  uint8_t mask = _node_prefs->alarm_repeat_mask;
  if (mask != 0) {
    for (int d = 0; d < 7; d++) {
      if (mask & (1 << ((ti->tm_wday + d) % 7))) {
        int64_t target = midnight + (int64_t)d * 86400 + time_of_day;
        if (target > now_local) return (uint32_t)(target - (int64_t)tz * 3600);
      }
    }
    // Mask had no bit set (shouldn't happen — the UI only offers non-empty
    // presets) — fall through to the one-shot calculation so it still fires.
  }
  int64_t target = midnight + time_of_day;
  if (target <= now_local) target += 86400;
  return (uint32_t)(target - (int64_t)tz * 3600);
}

void UITask::fireClockAlert(const char* label) {
  snprintf(_ring_label, sizeof(_ring_label), "%s", label);
  _ringing = true;
  _ring_until_ms = millis() + CLOCK_RING_MS;
  wakeForAlarm();
  showAlert(label, CLOCK_RING_MS);
  playMelody(CLOCK_ALARM_MELODY);
}

void UITask::evaluateAlarm() {
  if (!_node_prefs || !_node_prefs->alarm_on) return;
  uint32_t now_ms = millis();
  if (now_ms - _alarm_check_ms < 500) return;   // ~2 Hz is plenty for a minute alarm
  _alarm_check_ms = now_ms;
  uint32_t now_wall = rtc_clock.getCurrentTime();
  if (now_wall < 1000000000UL) return;           // need a real time sync first
  if (_alarm_next_fire == 0) _alarm_next_fire = computeAlarmNextFire(now_wall);
  if (now_wall < _alarm_next_fire) return;
  if (now_wall - _alarm_next_fire < CLOCK_ALARM_CATCHUP_SECS) {
    char lbl[20];
    snprintf(lbl, sizeof(lbl), "Alarm %02d:%02d", _node_prefs->alarm_hour, _node_prefs->alarm_min);
    if (_node_prefs->alarm_repeat_mask == 0) {
      _node_prefs->alarm_on = 0;                  // one-shot
      bool dirty = true; savePrefsIfDirty(dirty);
    }
    // Repeating: alarm_on stays set: computeAlarmNextFire() re-arms it for the
    // next matching weekday below.
    _alarm_next_fire = 0;
    fireClockAlert(lbl);
  } else {
    // Clock jumped implausibly far past the target — reschedule rather than
    // ringing absurdly late.
    _alarm_next_fire = computeAlarmNextFire(now_wall);
  }
}

void UITask::tickClockTools() {
  uint32_t now_ms = millis();
  // Ring maintenance: repeat the melody until dismissed or the window elapses.
  // Signed-difference compares (like the trail/loc-share timers) so deadlines
  // landing past the millis() rollover don't read as already elapsed.
  if (_ringing) {
    if ((int32_t)(now_ms - _ring_until_ms) >= 0) { stopMelody(); _ringing = false; clearAlert(); }
    else if (!isMelodyPlaying())  playMelody(CLOCK_ALARM_MELODY);
  }
  // Countdown timer (millis — sync-immune).
  if (_timer_running && (int32_t)(now_ms - _timer_deadline_ms) >= 0) {
    _timer_running = false;
    fireClockAlert("Timer done");
  }
  // Alarm (wall clock — absolute schedule for sync robustness).
  evaluateAlarm();
}

// Ringtone takes a slot argument that onShow() can't carry — pass it after the
// reset (setCurrScreen's onShow runs first, then this layers the slot on top).
void UITask::gotoRingtoneEditor(int slot) {
  setCurrScreen(ringtone_edit);
  ((RingtoneEditorScreen*)ringtone_edit)->selectSlot(slot);
}

// Map is a sub-view variant of the Trail screen: reset via onShow(), then
// switch into the map view.
void UITask::gotoMapScreen() {
  setCurrScreen(trail_screen);
  ((TrailScreen*)trail_screen)->showMapView();
}

void UITask::gotoLocatorScreen()    { setCurrScreen(locator_screen); }
void UITask::gotoAutoAdvertScreen() { setCurrScreen(auto_advert_screen); }

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

void UITask::gotoMessagesScreen() {
  ((MessagesScreen*)messages_screen)->reset();
  setCurrScreen(messages_screen);
}

void UITask::openContactDM(const ContactInfo& ci) {
  ((MessagesScreen*)messages_screen)->reset();
  ((MessagesScreen*)messages_screen)->enterDM(ci);
  setCurrScreen(messages_screen);
}

void UITask::shareToMessage(const char* text) {
  ((MessagesScreen*)messages_screen)->startShare(text);
  setCurrScreen(messages_screen);
}

void UITask::pickLocShareTarget() {
  ((MessagesScreen*)messages_screen)->startPickTarget();
  setCurrScreen(messages_screen);
}

void UITask::pickBotChannelTarget() {
  ((MessagesScreen*)messages_screen)->startPickBotChannel();
  setCurrScreen(messages_screen);
}

void UITask::pickBotRoomTarget() {
  ((MessagesScreen*)messages_screen)->startPickBotRoom();
  setCurrScreen(messages_screen);
}

int UITask::getRecentDMContacts(uint8_t out[][NodePrefs::FAVOURITE_PREFIX_LEN], int max) const {
  return ((MessagesScreen*)messages_screen)->getRecentDMContacts(out, max);
}

void UITask::addChannelMsg(uint8_t channel_idx, const char* text, uint32_t timestamp) {
  _last_notif_ch_idx = (int)channel_idx;
  ((MessagesScreen*)messages_screen)->addChannelMsg(channel_idx, text, timestamp);
}

int UITask::getChannelUnreadCount() const {
  return ((MessagesScreen*)messages_screen)->getTotalChannelUnread();
}

void UITask::onMsgAck(uint32_t ack_crc) {
  ((MessagesScreen*)messages_screen)->markDmDelivered(ack_crc);
}

void UITask::onChannelRelayed(uint32_t seq) {
  ((MessagesScreen*)messages_screen)->markChannelRelayed(seq);
}

void UITask::onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {
  // Only one on-device login can be in flight at a time (MyMesh::ui_pending_login
  // is a single slot) -- route the result to whichever of the two screens that
  // can trigger a login is currently active, rather than always MessagesScreen.
  if (curr == admin_screen) ((AdminScreen*)admin_screen)->onRoomLoginResult(pub_key, success, permissions);
  else                      ((MessagesScreen*)messages_screen)->onRoomLoginResult(pub_key, success, permissions);
  // Unlike the keypress-driven showAlert() calls elsewhere, this fires from a
  // background mesh response with no keypress to schedule a redraw — without
  // forcing one, the alert's short expiry can lapse before the next scheduled
  // refresh ever draws it.
  _next_refresh = 0;
}

void UITask::onAdminReply(const uint8_t* pub_key, const char* text) {
  ((AdminScreen*)admin_screen)->onAdminReply(pub_key, text);
  _next_refresh = 0;   // same reasoning as onRoomLoginResult above
}

void UITask::addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text, uint32_t sender_timestamp) {
  ((MessagesScreen*)messages_screen)->addDMMsg(pub_key, outgoing, text, sender_timestamp);
}

int UITask::getDMUnreadTotal() const {
  int total = 0;
  for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++) {
    if (_dm_unread_table[i].count == 0) continue;
    int held = ((MessagesScreen*)messages_screen)->dmHistCountForContact(_dm_unread_table[i].prefix);
    total += (_dm_unread_table[i].count < held) ? _dm_unread_table[i].count : held;
  }
  return total;
}

uint8_t UITask::getDMUnread(const uint8_t* pub_key) const {
  for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++) {
    if (_dm_unread_table[i].count > 0 && memcmp(_dm_unread_table[i].prefix, pub_key, 4) == 0) {
      int held = ((MessagesScreen*)messages_screen)->dmHistCountForContact(pub_key);
      return _dm_unread_table[i].count < held ? _dm_unread_table[i].count : (uint8_t)held;
    }
  }
  return 0;
}

void UITask::reconcileDMUnread() {
  for (int i = 0; i < DM_UNREAD_TABLE_SIZE; i++) {
    if (_dm_unread_table[i].count == 0) continue;
    if (((MessagesScreen*)messages_screen)->dmHistCountForContact(_dm_unread_table[i].prefix) == 0)
      _dm_unread_table[i].count = 0;   // ring no longer holds anything for this sender -- free the slot
  }
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
  case UIEventType::roomMessage:
    // Rooms have many authors and no per-room melody pref, so use the default DM
    // notification (no per-sender melody/mute lookup — the author varies per post).
    sn.playDM(false, nullptr);
    break;
  case UIEventType::advertReceivedFlood:
  case UIEventType::advertReceivedZeroHop:
    sn.playAD(t == UIEventType::advertReceivedFlood);
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
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
    ((MessagesScreen*)messages_screen)->clearAllChannelUnread();
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

// Centred alert box. Long text used to be drawn as one drawTextCentered line
// that overflowed the border on both sides (e.g. "GPS on, tracking started"
// is already wider than a 128 px OLED); wrap it to up to three lines inside
// the box instead. Uses the shared wrap scratch (s_wrap_*) — single-threaded
// render path, same contract as the message views.
void UITask::renderAlertOverlay() {
  _display->setTextSize(1);
  const int lh    = _display->getLineHeight();
  const int pad   = 3;
  const int box_w = _display->width() - 8;
  const int box_x = 4;
  _display->translateUTF8ToBlocks(s_wrap_trans, _alert, sizeof(s_wrap_trans));
  int nl = FullscreenMsgView::wrapLines(*_display, s_wrap_trans, box_w - pad * 2, s_wrap_lines, 3);
  if (nl < 1) nl = 1;
  int box_h = nl * lh + pad * 2;
  int box_y = (_display->height() - box_h) / 2;
  _display->setColor(DisplayDriver::DARK);
  _display->fillRect(box_x, box_y, box_w, box_h);
  _display->setColor(DisplayDriver::LIGHT);
  _display->drawRect(box_x, box_y, box_w, box_h);
  for (int i = 0; i < nl; i++)
    _display->drawTextCentered(_display->width() / 2, box_y + pad + i * lh, s_wrap_lines[i]);
}

void UITask::setCurrScreen(UIScreen* c) {
  // Fail safe on a null target: a screen pointer left uninitialised (member
  // declared + navigator wired, but the `new XScreen()` line forgotten in
  // begin()) stays nullptr thanks to the in-class initialisers. Bail here so
  // that mistake is an inert no-op instead of a null deref in render()/poll().
  if (!c) return;
  curr = c;
  c->onShow();          // central per-visit reset hook (see UIScreen::onShow)
  _next_refresh = 100;
}

bool UITask::savePrefsIfDirty(bool& dirty) {
  if (!dirty) return false;
  the_mesh.savePrefs();
  dirty = false;
  return true;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){
  the_mesh.saveRTCTime();

  // Auto-save the live GPS trail before power-off when the user enabled it
  // (Tools › Trail › Settings › Auto-save). This covers the low-battery
  // auto-shutdown, which otherwise loses the whole route. Overwrites /trail
  // (same file as the manual Trail › Save); guarded on count()>0 so an empty
  // trail can't wipe a previously saved one.
  if (_node_prefs && _node_prefs->trail_autosave_lowbatt && _trail.count() > 0) {
    DataStore* ds = the_mesh.getDataStore();
    if (ds) {
      File f = ds->openWrite("/trail");
      if (f) { _trail.writeTo(f); f.close(); }
    }
  }

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
    // Power GPS down through its provider before SYSTEMOFF — GPIO pins retain
    // state in NRF52 SYSTEMOFF, so otherwise the module keeps draining the
    // battery. The provider handles the enable + reset pins and the correct
    // active level. gps_enabled is persisted; applyGpsPrefs() restores it on
    // the next boot.
    if (_sensors) {
      LocationProvider* loc = _sensors->getLocationProvider();
      if (loc) loc->stop();
    }
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
                          uint16_t low_batt_mv, int unread, CayenneLPP* lpp = nullptr) {
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
    case DASH_MSGS:
      snprintf(val, val_len, "%d msgs", unread);
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

void UITask::enqueueKey(char c) {
  if (c == 0) return;
  uint8_t next = (_kq_head + 1) % KEY_QUEUE_SIZE;
  if (next == _kq_tail) return;  // full: drop newest rather than clobber unprocessed keys
  _key_queue[_kq_head] = c;
  _kq_head = next;
}

bool UITask::dequeueKey(char& c) {
  if (_kq_tail == _kq_head) return false;
  c = _key_queue[_kq_tail];
  _kq_tail = (_kq_tail + 1) % KEY_QUEUE_SIZE;
  return true;
}

#if defined(ENV_PIN_SDA) && defined(ENV_PIN_SCL)
// CardKB's "fn" column (key_map in M5Stack's unit_CardKB.cpp): Fn+<physical
// key> sends 0x80 + that key's row index, entirely disjoint from every other
// code this UI recognises. Indexed by (raw - 0x80); non-letter slots (digits,
// arrows, enter, tab, bs, space -- handled separately or unused) are 0.
static const char CARDKB_FN_BASE[48] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,                                     // esc,1-0,bs,tab
  'q','w','e','r','t','y','u','i','o','p', 0, 0,0,                // q-p, (unused), LEFT,UP
  'a','s','d','f','g','h','j','k','l', 0, 0,0,                    // a-l, enter, DOWN,RIGHT
  'z','x','c','v','b','n','m', 0,0,0,                             // z-m, comma,period,space
};
#endif

// Poll an optional CardKB (I2C keyboard, addr 0x5F) on Wire1/Grove, feeding
// the same key queue as every physical button. Most of its output needs no
// translation at all: CardKB's own arrow/Enter/Esc byte codes are already
// identical to this UI's KEY_LEFT/UP/DOWN/RIGHT/ENTER/CANCEL (0xB4-0xB7, 13,
// 27), and Backspace (0x08) / printable ASCII (0x20-0x7E) collide with
// nothing that existed before. Plain Enter/arrows act like the physical
// centre button/joystick (grid commit/navigate) -- except in Compact mode's
// plain grid state (see below), which is designed to need no joystick at all.
// Tab (0x09, otherwise unused) is the Hold-Enter equivalent everywhere,
// including the ~30 non-keyboard Hold-Enter menus (message reply/navigate,
// Bot/Admin/Repeater, ...) and inside the on-screen keyboard itself (shift-
// lock, clear-all, accent popup on whatever cell is selected) -- it used to
// need a separate Fn+Tab for the latter, but that was pure redundancy: plain
// Tab already covered every case Fn+Tab did, just not while the keyboard was
// showing, so the carve-out was dropped instead of the shortcut. In Compact
// mode's plain grid state Tab means something more useful instead (opens the
// placeholder picker directly -- see below). Fn still gives two other clean,
// stateless modifiers:
//  - Fn+Enter (0xA3) submits the field (KEY_KB_ENTER) without needing to
//    navigate to the special row's DONE cell. The placeholder/accent popups
//    are modal and consume it first (dismiss them with Enter/Esc), same as
//    they consume every other key.
//  - Fn+<letter> opens the accent popup for that base letter directly
//    (KeyboardWidget::openAccentFor()) -- no arrow-hunting across the grid.
// CardKB is level-triggered (it keeps returning the held key's byte, not just
// once), so _cardkb_last_raw debounces it into one press per physical
// keypress, same as a MomentaryButton's CLICK event.
void UITask::pollCardKB() {
#if defined(ENV_PIN_SDA) && defined(ENV_PIN_SCL)
  if (!_has_cardkb) return;
  // No artificial throttle: unlike a MomentaryButton (BUTTON_USE_INTERRUPTS
  // latches every edge in an ISR ring buffer, so it survives a blocking e-ink
  // refresh untouched), CardKB is plain I2C polling with no interrupt line on
  // the Grove cable and no onboard queue -- it only ever reports "what's held
  // right now". A press that starts and fully releases while curr->render()
  // is blocked is physically unobservable, no software fix can recover it.
  // Polling every loop() iteration (same as a digital button's check(), which
  // has no throttle either) just shrinks that miss window down to exactly the
  // render() duration instead of render()+30ms.
  Wire1.requestFrom(0x5F, 1);
  if (!Wire1.available()) return;
  uint8_t raw = Wire1.read();
  if (raw == _cardkb_last_raw) return;   // still held (or still released) -- no new edge
  _cardkb_last_raw = raw;
  if (raw == 0) return;   // key just released, nothing to enqueue

  // Compact mode (Settings > Keyboard's "Ext. KB" row) hides the letter grid
  // entirely, and is meant to guarantee joystick-free operation: while it's
  // the active surface (KeyboardWidget::inPlainGridState() -- showing, no
  // popup open, not already mid cursor-move) arrows drive the text cursor
  // directly instead of a grid selection nobody could see anyway, and plain
  // Tab opens the placeholder picker directly instead of the row/col-dependent
  // Hold-Enter dispatch (which would be meaningless here -- row/col are never
  // deliberately navigated to in this mode). Cursor mode / the accent /
  // placeholder popups all render their own visible feedback regardless of
  // Compact, so none of this applies once inPlainGridState() is false --
  // arrows/Tab fall through to their normal meaning there (e.g. arrows drive
  // the placeholder/accent popup's own selection).
  bool compact_grid = _node_prefs && _node_prefs->keyboard_cardkb_compact && _kb.inPlainGridState();

  char key;
  if (raw == 0xA3) {          // Fn+Enter -- submit the field
    key = KEY_KB_ENTER;
  } else if (compact_grid && (raw == (uint8_t)KEY_LEFT || raw == (uint8_t)KEY_UP ||
                              raw == (uint8_t)KEY_DOWN || raw == (uint8_t)KEY_RIGHT)) {
    char woke = checkDisplayOn((char)raw);   // already sets _next_refresh=0 when the display was on
    if (woke && !_locked) _kb.moveCursorDirect((char)raw);
    return;
  } else if (raw == 0x09) {   // Tab -- Hold-Enter equivalent, always (single shortcut: there used to
    if (compact_grid) {       // also be a separate Fn+Tab for this, but plain Tab already covers every
      char woke = checkDisplayOn((char)raw);   // case Fn+Tab did -- outside the keyboard, and now inside it
      if (woke && !_locked) _kb.openPlaceholders();   // too -- so the modifier was pure redundancy)
      return;
    }
    key = KEY_CONTEXT_MENU;
  } else if (raw == 0x80) {
    // Fn+Esc -- CardKB's lock/unlock gesture: a single press toggles _locked
    // directly (unlike the physical Hold-Back+3xEnter combo's 3-press
    // sequence), so it works to unlock a locked device too, where every
    // other CardKB key is correctly discarded (see the Fn+<letter> branch
    // below). Esc, not the adjacent Fn+Backspace, on purpose: Fn and
    // Backspace sit right next to each other on CardKB's layout, making that
    // combo too easy to hit by accident; Esc is on the opposite side of the
    // keyboard. One press is enough -- Fn+Esc is already a deliberate
    // two-key combo, so it doesn't need the physical combo's extra 3x
    // repetition to guard against accidental triggering.
    if (_display && !_display->isOn()) _display->turnOn();
    _locked = !_locked;
    if (_locked) {
      _lock_wake_until = millis() + 2000;
    } else {
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
    }
    _next_refresh = 0;
    return;
  } else if (raw >= 0x80 && raw <= 0xAF) {   // Fn+<letter> -- open its accent popup
    char base = CARDKB_FN_BASE[raw - 0x80];
    if (base == 0) return;   // Fn+digit/symbol/arrow -- not used by this UI
    char woke = checkDisplayOn(base);
    // Every other key here goes through enqueueKey(), so it's naturally eaten
    // while locked (see the dequeue-time "if (!_locked && curr)" gate in
    // loop()). This path calls into the keyboard widget directly instead, so
    // it needs its own _locked check -- otherwise a stray Fn+letter (e.g. the
    // keyboard was left open before the device locked, or brushed against in
    // a pocket) could pop the accent popup while the screen is supposed to
    // ignore all input.
    if (woke && !_locked) _kb.openAccentFor(base);
    return;
  } else {
    // Plain Enter would otherwise commit whatever grid cell row/col happen to
    // be frozen at (there's no grid navigation to have deliberately landed on
    // one in Compact) -- submit instead, same as Fn+Enter. Backspace/ASCII
    // passthrough is unaffected by Compact either way.
    key = (compact_grid && raw == (uint8_t)KEY_ENTER) ? KEY_KB_ENTER : (char)raw;
  }
  enqueueKey(checkDisplayOn(key));
#endif
}

// Level-triggered, like pollCardKB() -- a magnet held near the sensor reads the
// same way every tick, so this only acts on the two edges (closed/opened), not
// on every poll. Fully autonomous: closing locks and blanks the display
// immediately (no wake grace -- the cover is physically over the screen, so
// there's nothing to show), opening unlocks and wakes it, with no combo or
// keypress either way. Independent of Auto-lock (Settings > Display), which is
// a timeout-driven setting -- this is a direct physical event.
void UITask::pollHallSensor() {
#if defined(PIN_HALL_SENSOR)
  bool present = HALL_ACTIVE_HIGH ? (digitalRead(PIN_HALL_SENSOR) == HIGH)
                                   : (digitalRead(PIN_HALL_SENSOR) == LOW);
  if (present == _hall_magnet_present) return;
  _hall_magnet_present = present;

  if (present) {   // cover closed
    _locked = true;
    _lock_wake_until = 0;
    if (_display) _display->turnOff();
#ifdef PIN_LED
    digitalWrite(PIN_LED, LOW);   // same as the auto-off path -- one less thing lit under a closed cover
#endif
  } else {   // cover opened
    _locked = false;
    if (_display && !_display->isOn()) _display->turnOn();
    uint32_t aoff = autoOffMillis();
    if (aoff > 0) _auto_off = millis() + aoff;
  }
  _next_refresh = 0;
#endif
}

void UITask::loop() {
  // Background delivery: resend pending on-device DMs whose ACK timed out, and
  // finalise the ✗ marker — runs regardless of which screen is active.
  ((MessagesScreen*)messages_screen)->tickDmResends();
  reconcileDMUnread();
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
      enqueueKey(checkDisplayOn(KEY_ENTER));
    }
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    enqueueKey(handleLongPress(KEY_ENTER));  // REVISIT: could be mapped to different key code
  }
  // Drain each direction fully: a burst of taps captured during a blocking
  // refresh replays as several CLICKs, queued here and applied before one
  // redraw (see enqueueKey / the dispatch at the end of loop()).
#if UI_HAS_JOYSTICK_UPDOWN
  while (joystick_up.check() == BUTTON_EVENT_CLICK)
    enqueueKey(checkDisplayOn(rotateJoystickKey(KEY_UP, joy_rot)));
  while (joystick_down.check() == BUTTON_EVENT_CLICK)
    enqueueKey(checkDisplayOn(rotateJoystickKey(KEY_DOWN, joy_rot)));
#endif
  while ((ev = joystick_left.check()) != BUTTON_EVENT_NONE) {
    if (ev == BUTTON_EVENT_CLICK) enqueueKey(checkDisplayOn(rotateJoystickKey(KEY_LEFT, joy_rot)));
    else { if (ev == BUTTON_EVENT_LONG_PRESS) enqueueKey(handleLongPress(rotateJoystickKey(KEY_LEFT, joy_rot))); break; }
  }
  while ((ev = joystick_right.check()) != BUTTON_EVENT_NONE) {
    if (ev == BUTTON_EVENT_CLICK) enqueueKey(checkDisplayOn(rotateJoystickKey(KEY_RIGHT, joy_rot)));
    else { if (ev == BUTTON_EVENT_LONG_PRESS) enqueueKey(handleLongPress(rotateJoystickKey(KEY_RIGHT, joy_rot))); break; }
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
      enqueueKey(checkDisplayOn(KEY_CANCEL));
    }
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    if (!_locked) enqueueKey(handleTripleClick(KEY_SELECT));
  }
#elif defined(PIN_USER_BTN)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    enqueueKey(checkDisplayOn(KEY_NEXT));
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    enqueueKey(handleLongPress(KEY_ENTER));
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    enqueueKey(handleDoubleClick(KEY_PREV));
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    if (!_locked) enqueueKey(handleTripleClick(KEY_SELECT));
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (millis() - _analogue_pin_read_millis > 10) {
    int ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      enqueueKey(checkDisplayOn(KEY_NEXT));
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      enqueueKey(handleLongPress(KEY_ENTER));
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      enqueueKey(handleDoubleClick(KEY_PREV));
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      if (!_locked) enqueueKey(handleTripleClick(KEY_SELECT));
    }
    _analogue_pin_read_millis = millis();
  }
#endif
  pollCardKB();
  pollHallSensor();
#ifdef ENV_USE_TCA8418
  {
    extern char tca8418_keypad_read();   // provided by the active variant
    char k = tca8418_keypad_read();
    if (k) {
      switch (k) {
      case KEY_UP:    enqueueKey(checkDisplayOn(KEY_UP));    break;
      case KEY_ENTER: enqueueKey(checkDisplayOn(KEY_ENTER)); break;
      case KEY_DOWN:  enqueueKey(checkDisplayOn(KEY_DOWN));  break;
      case KEY_CANCEL:enqueueKey(checkDisplayOn(KEY_CANCEL));break;
      case KEY_HOME:
        checkDisplayOn(k);   // wake/extend same as every other key here, even though Home has no nav action
        #ifdef LILYGO_TECHO_LITE_KEYSHIELD
        extern void techo_keyshield_backlight_toggle();
        techo_keyshield_backlight_toggle();
        #endif
        break;
      default:
        enqueueKey(checkDisplayOn(k));
        break;
      }
    }
  }
#endif
#if defined(BACKLIGHT_BTN)
  if ((int32_t)(millis() - next_backlight_btn_check) >= 0) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  // A ringing alarm/timer is dismissed by ANY key, even when locked or on another
  // screen — and the queued keys are swallowed so they don't also act on the view.
  if (_kq_head != _kq_tail && isRinging()) {
    dismissRing();
    _kq_head = _kq_tail = 0;
    _next_refresh = 0;
    // Locked: wakeForAlarm() held the wake window open for the whole ring;
    // once dismissed, fall back to the usual brief lock-screen glance.
    if (_locked) _lock_wake_until = millis() + 5000;
  }

  if (_kq_head != _kq_tail) {
    if (!_locked && curr) {
      // Apply the whole queued burst, then redraw once — N taps captured during
      // a blocking refresh become N navigation steps at the cost of one refresh.
      char k;
      while (dequeueKey(k)) curr->handleInput(k);
      { uint32_t aoff = autoOffMillis(); if (aoff > 0) _auto_off = millis() + aoff; }  // extend auto-off timer
      // Note timing no longer depends on render cadence (TIMER1 IRQ advances
      // notes directly — see buzzer.cpp), so a redraw right after a keypress
      // can't clip a note; no need to hold it back while buzzer.isPlaying().
      _next_refresh = 100;  // trigger refresh immediately
    } else {
      _kq_head = _kq_tail = 0;  // locked or no screen: eat all queued keys
      // Locked: wake window is set only when display first turns on
      if (_locked) _next_refresh = 0;
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

  // Alarm + countdown run regardless of the current screen / display state, so
  // they're driven here (not via the current screen's poll()).
  tickClockTools();

  if (_display != NULL && _display->isOn()) {
    if (_locked && (int32_t)(millis() - _lock_wake_until) >= 0) {
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
          int unread = (f0 == DASH_MSGS || f1 == DASH_MSGS)
                     ? getDMUnreadTotal() + getChannelUnreadCount() + getRoomUnreadCount() : 0;
          formatDashVal(f0, v0, sizeof(v0), _batt_mv, _node_prefs->low_batt_mv, unread, lpp_ptr);
          formatDashVal(f1, v1, sizeof(v1), _batt_mv, _node_prefs->low_batt_mv, unread, lpp_ptr);
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
#if defined(ENV_PIN_SDA) && defined(ENV_PIN_SCL)
      const char* hint = _lock_seq_count == 0 ? (_has_cardkb ? "Back+3xEnter/Fn+Esc" : "Hold Back + 3xEnter") :
                         _lock_seq_count == 1 ? "Enter x2 more..."   : "Enter x1 more...";
#else
      const char* hint = _lock_seq_count == 0 ? "Hold Back + 3xEnter" :
                         _lock_seq_count == 1 ? "Enter x2 more..."   : "Enter x1 more...";
#endif
      int p = 3;
      int hy = _display->height() - lk_lh - p * 2;
      int hw = _display->getTextWidth(hint);
      int hx = (_display->width() - hw) / 2;
      _display->setColor(DisplayDriver::LIGHT);
      _display->fillRect(hx - p, hy - p, hw + p*2, lk_lh + p*2);
      _display->setColor(DisplayDriver::DARK);
      _display->setCursor(hx, hy);
      _display->print(hint);
      // Alert overlay on top — without this a ringing alarm on a locked device
      // played its melody against a screen that never said what was ringing.
      if (millis() < _alert_expiry) renderAlertOverlay();
      _display->endFrame();
      _next_refresh = millis() + Features::LOCKSCREEN_REFRESH_MS;
    } else if (!_locked && millis() >= _next_refresh && curr) {
      _display->startFrame();
      _kb.beginFrame();
      int delay_millis = curr->render(*_display);
      // Skip the alert overlay (new-message toast) while the keyboard is the
      // thing actually on screen this frame -- it's shared across Messages/
      // Bot/Settings/Admin/etc., so this covers every screen that uses it for
      // full-screen text entry, not just message compose. Otherwise a message
      // arriving mid-typing blanks out the letter grid for 3s with no way to
      // see what's being typed.
      if (millis() < _alert_expiry && !_kb.isVisible()) {  // alert overlay on top of any (non-keyboard) screen
        renderAlertOverlay();
        // Keep refreshing the underlying screen at its own cadence (capped at the
        // alert's expiry) so layouts that settle over a frame — e.g. the message-
        // history scrollbar reserve — don't stay stuck behind the alert. Unchanged
        // frames are skipped by the display CRC, so e-ink isn't thrashed.
        _next_refresh = millis() + delay_millis;
        if (_next_refresh > _alert_expiry) _next_refresh = _alert_expiry;
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
    if (!_locked && autoOffMillis() > 0 && (int32_t)(millis() - _auto_off) >= 0 && !isRinging()) {
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

  if ((int32_t)(millis() - next_batt_chck) >= 0) {
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

  // GPS duty-cycle hold — tells the sensor manager whether *anything* needs
  // an unbroken stream of fixes right now, so it knows it's safe to let GPS
  // nap between reads (EnvironmentSensorManager::gpsDutyCycleLoop()).
  // Deliberately excludes the COG sampler just below: that one runs
  // unconditionally every ~1s specifically so a heading is ready whenever a
  // screen opens, and including it here would keep GPS permanently awake and
  // defeat duty-cycling entirely — it just goes stale during a sleep window
  // and catches up whenever GPS is awake for any other reason.
  if (_sensors) {
    bool gps_needed_live =
        (_trail.isActive() && !_trail.isPaused())
        || (_node_prefs && _node_prefs->loc_share_enabled)
        || (_node_prefs && _node_prefs->locator_enabled && _node_prefs->locator_has_target)
        || curr == compass_screen
        || (curr == nearby_screen && ((NearbyScreen*)nearby_screen)->isNavigating())
        || (curr == trail_screen && ((TrailScreen*)trail_screen)->wpNeedsLiveGps())
        || (curr == messages_screen && ((MessagesScreen*)messages_screen)->navActive())
        || the_mesh.isGpsFixPending();
    _sensors->setGpsKeepAwake(gps_needed_live);
    // A fresh wake (either a duty-cycle wake, or GPS forced continuously back
    // on) may deliver a still-settling first fix — re-seed the locator's
    // crossing state so that doesn't read as a spurious geofence crossing.
    if (_sensors->consumeGpsWakeEvent()) resetLocator();
  }

  // GPS trail sampling — runs in the background while the trail is
  // active, independent of which screen is shown. Skips silently if no GPS
  // fix; min-delta gate inside addPoint() avoids near-stationary spam.
  if (!_trail.isActive()) _trail_pause_has_ref = false;   // fresh ref on next start
  if (_trail.isActive() && _node_prefs != NULL
      && (int32_t)(millis() - _next_trail_sample_ms) >= 0) {
    _next_trail_sample_ms = millis() + (uint32_t)TrailStore::SAMPLING_SECS * 1000UL;
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    if (loc && loc->isValid()) {
      int32_t la = (int32_t)loc->getLatitude();
      int32_t lo = (int32_t)loc->getLongitude();
      uint16_t md = TrailStore::minDeltaMeters(_node_prefs->trail_min_delta_idx,
                                                _node_prefs->units_imperial);
      // Auto-pause: freeze the trail once the device has stayed within
      // TRAIL_AUTOPAUSE_MOVE_M of one spot for the configured delay; resume on
      // the next real move. Its own coarse gate (not the trail min-delta) so
      // GPS jitter while parked doesn't keep the idle timer alive.
      uint16_t ap = NodePrefs::trailAutoPauseSecs(_node_prefs->trail_autopause_idx);
      if (ap > 0) {
        uint32_t now = millis();
        float moved = _trail_pause_has_ref
            ? geo::haversineKm(_trail_pause_ref_lat, _trail_pause_ref_lon, la, lo) * 1000.0f
            : 1e9f;
        if (!_trail_pause_has_ref || moved >= (float)NodePrefs::TRAIL_AUTOPAUSE_MOVE_M) {
          _trail_pause_ref_lat = la; _trail_pause_ref_lon = lo;
          _trail_pause_has_ref = true;
          _trail_last_move_ms  = now;
          if (_trail.isPaused()) _trail.setPaused(false);
        } else if (!_trail.isPaused() && (now - _trail_last_move_ms) >= (uint32_t)ap * 1000UL) {
          _trail.setPaused(true);
        }
      } else if (_trail.isPaused()) {
        _trail.setPaused(false);   // feature turned off → resume
      }
      if (!_trail.isPaused())
        _trail.addPoint(la, lo, (uint32_t)rtc_clock.getCurrentTime(), md);
    }
  }

  // Live-track housekeeping — drop shared positions that have gone stale, so
  // the Nearby "Live" view / map don't show ghosts. Cheap; once a minute.
  if ((int32_t)(millis() - _next_livetrack_expire_ms) >= 0) {
    _next_livetrack_expire_ms = millis() + 60000UL;
    _livetrack.expire((uint32_t)rtc_clock.getCurrentTime());
  }

  // Live location sharing — periodically broadcast my [LOC] to the configured
  // target while moving (Map › Live share). Movement-gated so a stationary
  // device stays quiet unless a heartbeat is configured.
  if (_node_prefs && _node_prefs->loc_share_enabled
      && (int32_t)(millis() - _next_loc_share_check_ms) >= 0) {
    _next_loc_share_check_ms = millis() + 2000UL;
    if (!_loc_share_was_enabled) _loc_share_has_last = false;  // re-announce on enable
    _loc_share_was_enabled = true;
    int32_t lat, lon;
    if (currentLocation(lat, lon)) {
      uint16_t move_m = NodePrefs::locShareMoveMeters(_node_prefs->loc_share_move_idx);
      uint16_t gap_s  = NodePrefs::locShareIntervalSecs(_node_prefs->loc_share_interval_idx);
      uint16_t hb_s   = NodePrefs::locShareHeartbeatSecs(_node_prefs->loc_share_heartbeat_idx);
      uint32_t now = millis();
      bool first = !_loc_share_has_last;
      float moved = first ? 1e9f
                          : geo::haversineKm(_loc_share_last_lat, _loc_share_last_lon, lat, lon) * 1000.0f;
      bool gap_ok = first || (now - _loc_share_last_ms) >= (uint32_t)gap_s * 1000UL;
      bool hb_due = (hb_s > 0) && !first && (now - _loc_share_last_ms) >= (uint32_t)hb_s * 1000UL;
      if ((moved >= (float)move_m && gap_ok) || first || hb_due) {
        if (sendLocationShare(lat, lon)) {
          _loc_share_last_lat = lat;
          _loc_share_last_lon = lon;
          _loc_share_last_ms  = now;
          _loc_share_has_last = true;
        }
      }
    }
  } else if (_node_prefs && !_node_prefs->loc_share_enabled) {
    _loc_share_was_enabled = false;
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

  // Locator — beep + alert when the device crosses into / out of the armed
  // geofence. Cheap; a few seconds of latency at the boundary is fine.
  if ((int32_t)(millis() - _next_locator_ms) >= 0) {
    _next_locator_ms = millis() + 3000UL;
    evaluateLocator();
  }

  // Locator proximity beeper — ticks faster the closer to the target. Runs on
  // its own short cadence (the crossing check above is too coarse for this).
  locatorProximityBeeper();
}

// Evaluate the single geofence against the current GPS fix. Crossing the radius
// fires fireLocator() according to the configured mode; a hysteresis band on
// the "leave" edge stops it chattering at the boundary, and the first reading
// after arming only seeds the inside/outside state (no spurious alert).
// Distance (m) from the current GPS fix to the locator target, plus the
// configured radius (m). Returns false when no target is set or there's no fix
// — the single place the target-distance maths lives, shared by the crossing
// evaluator and the proximity beeper.
// One precedence for a person's position — an active [LOC] live share wins,
// else the last-advertised GPS fix. Not everyone keeps live-sharing on, so the
// fallback lets a rarely-updating but stationary node (a repeater, or someone
// who shared a fix once) still work as a target.
bool UITask::resolvePersonPos(const uint8_t* key, int32_t& lat, int32_t& lon,
                              bool* live, uint32_t* ts) const {
  if (live) *live = false;
  if (ts)   *ts   = 0;
  if (!key) return false;
  const LiveTrackStore::Entry* e =
      _livetrack.activeByKey(key, (uint32_t)rtc_clock.getCurrentTime());
  if (e) {
    lat = e->lat_1e6; lon = e->lon_1e6;
    if (live) *live = true;
    if (ts)   *ts   = e->ts;
    return true;
  }
  ContactInfo* c = the_mesh.lookupContactByPubKey(key, NodePrefs::FAVOURITE_PREFIX_LEN);
  if (c && (c->gps_lat != 0 || c->gps_lon != 0)) {
    lat = c->gps_lat; lon = c->gps_lon;
    if (ts) *ts = c->lastmod;
    return true;
  }
  return false;
}

bool UITask::activeTargetPos(int32_t& lat, int32_t& lon) const {
  if (!_node_prefs || !_node_prefs->locator_has_target) return false;
  if (_node_prefs->locator_target_kind == 1)
    return resolvePersonPos(_node_prefs->locator_key, lat, lon);
  lat = _node_prefs->locator_lat_1e6;
  lon = _node_prefs->locator_lon_1e6;
  return true;
}

bool UITask::locatorDistance(float& dist_m, float& radius_m) const {
  int32_t tlat, tlon;
  if (!activeTargetPos(tlat, tlon)) return false;
  int32_t lat, lon;
  if (!currentLocation(lat, lon)) return false;
  dist_m   = geo::haversineKm(lat, lon, tlat, tlon) * 1000.0f;
  radius_m = (float)NodePrefs::locatorRadiusMeters(_node_prefs->locator_radius_idx);
  return true;
}

void UITask::evaluateLocator() {
  if (!_node_prefs || !_node_prefs->locator_enabled || !_node_prefs->locator_has_target) {
    _locator_known = false;
    return;
  }
  float dist, r;
  if (!locatorDistance(dist, r)) return;   // armed but no fix yet — keep state
  bool inside;
  if (!_locator_known)        inside = dist <= r;            // seed state
  else if (_locator_inside)   inside = dist <= r * 1.25f;    // leave past band
  else                          inside = dist <= r;            // arrive at edge

  if (_locator_known && inside != _locator_inside) {
    uint8_t mode = _node_prefs->locator_mode;  // 0=arrive,1=leave,2=both
    bool fire = inside ? (mode == 0 || mode == 2) : (mode == 1 || mode == 2);
    if (fire) fireLocator(inside);
  }
  _locator_inside = inside;
  _locator_known  = true;
}

void UITask::fireLocator(bool arrived) {
  const char* lbl = _node_prefs->locator_label[0] ? _node_prefs->locator_label : "target";
  bool person = _node_prefs->locator_target_kind == 1;
  char msg[40];
  // "Near/Away" reads naturally for a moving person; "Arrived/Left" for a place.
  snprintf(msg, sizeof(msg),
           arrived ? (person ? "Near: %s"  : "Arrived: %s")
                   : (person ? "Away: %s"  : "Left: %s"), lbl);
  showAlert(msg, 3000);
  if (!isBuzzerQuiet())
    playMelody(arrived ? "locarr:d=8,o=6,b=140:c,e,g" : "loclv:d=8,o=6,b=140:g,e,c");
}

void UITask::setTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  if (!_node_prefs) return;
  _node_prefs->locator_target_kind = kind;
  if (kind == 1 && key) memcpy(_node_prefs->locator_key, key, NodePrefs::FAVOURITE_PREFIX_LEN);
  _node_prefs->locator_lat_1e6 = lat;
  _node_prefs->locator_lon_1e6 = lon;
  snprintf(_node_prefs->locator_label, sizeof(_node_prefs->locator_label), "%s", name);
  _node_prefs->locator_has_target = 1;
  resetLocator();   // re-seed the crossing engine so the change can't fire on a stale state
}

void UITask::setTargetNow(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  if (!_node_prefs) return;
  setTarget(kind, key, lat, lon, name);
  the_mesh.savePrefs();
  showAlert("Target set", 1200);
}

void UITask::clearTarget() {
  if (!_node_prefs) return;
  _node_prefs->locator_has_target = 0;
  resetLocator();
}

void UITask::clearTargetIfWaypoint(int32_t lat_1e6, int32_t lon_1e6) {
  if (!_node_prefs || !_node_prefs->locator_has_target || _node_prefs->locator_target_kind != 0) return;
  if (_node_prefs->locator_lat_1e6 != lat_1e6 || _node_prefs->locator_lon_1e6 != lon_1e6) return;
  clearTarget();
  the_mesh.savePrefs();
}

// CONTRACT: every NodePrefs field that keys on a contact pubkey/prefix is
// cleared here, so a removed contact can't leave a dangling reference. If you
// add such a field, add its cleanup below (and mark the field in NodePrefs.h).
// Currently covered: favourite_contacts, locator_key, loc_share_dm_prefix,
// dm_notif[], dm_melody[]. Also clears _dm_unread_table (RAM-only, not a
// NodePrefs field, so no savePrefs() needed for it) -- same 4-byte-prefix
// shape and same 16-slot starvation risk as dm_notif/dm_melody above. Called
// for both explicit removal and silent auto-eviction (see MyMesh
// CMD_REMOVE_CONTACT / onContactOverwrite).
void UITask::onContactRemoved(const uint8_t* pub_key) {
  if (!_node_prefs || !pub_key) return;
  bool changed = false;

  clearDMUnread(pub_key);

  int slot = findFavouriteSlot(pub_key);
  if (slot >= 0) { clearFavouriteSlot(slot); changed = true; }

  if (_node_prefs->locator_has_target && _node_prefs->locator_target_kind == 1
      && memcmp(_node_prefs->locator_key, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
    clearTarget();
    changed = true;
  }
  // Fail closed rather than guess a new recipient: a contact target that's
  // gone just turns auto-share off, it doesn't fall back to some other target.
  if (_node_prefs->loc_share_target_type == 1
      && memcmp(_node_prefs->loc_share_dm_prefix, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
    _node_prefs->loc_share_enabled = 0;
    changed = true;
  }
  // Same fail-closed rule for the room bot's target: the room contact is
  // gone, disable it rather than risk a re-added contact silently inheriting
  // the old bot config.
  if (_node_prefs->bot_room_enabled
      && memcmp(_node_prefs->bot_room_prefix, pub_key, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
    _node_prefs->bot_room_enabled = 0;
    changed = true;
  }
  // Per-contact mute/melody overrides — only 16 slots shared across every
  // contact, so an orphaned entry isn't just stale, it can eventually starve
  // new overrides for contacts that still exist. Keyed by a 4-byte prefix
  // (narrower than the 6-byte one above), so compare only that many bytes.
  for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++) {
    if (_node_prefs->dm_notif[i].state && memcmp(_node_prefs->dm_notif[i].prefix, pub_key, 4) == 0) {
      memset(&_node_prefs->dm_notif[i], 0, sizeof(_node_prefs->dm_notif[i]));
      changed = true;
    }
  }
  for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++) {
    if (_node_prefs->dm_melody[i].slot && memcmp(_node_prefs->dm_melody[i].prefix, pub_key, 4) == 0) {
      memset(&_node_prefs->dm_melody[i], 0, sizeof(_node_prefs->dm_melody[i]));
      changed = true;
    }
  }

  if (changed) the_mesh.savePrefs();
}

// CONTRACT: every NodePrefs field that keys on a channel index is cleared here,
// so a channel re-added at a freed slot can't inherit the old one's settings.
// If you add such a field, add its cleanup below (and mark it in NodePrefs.h).
// Currently covered: bot_channel_idx, loc_share_channel_idx, ch_notif_melody_*,
// ch_notif_override/ch_notif_muted, ch_fav_bitmask.
void UITask::onChannelRemoved(uint8_t channel_idx) {
  if (!_node_prefs) return;
  bool changed = false;

  if (_node_prefs->bot_channel_enabled && _node_prefs->bot_channel_idx == channel_idx) {
    _node_prefs->bot_channel_enabled = 0;
    changed = true;
  }
  // Fail closed, same policy as onContactRemoved()'s Live Share case.
  if (_node_prefs->loc_share_target_type == 0 && _node_prefs->loc_share_channel_idx == channel_idx) {
    _node_prefs->loc_share_enabled = 0;
    changed = true;
  }
  uint64_t mask = 1ULL << channel_idx;
  if (_node_prefs->ch_notif_melody_set & mask) {
    _node_prefs->ch_notif_melody_set &= ~mask;
    _node_prefs->ch_notif_melody_2   &= ~mask;
    changed = true;
  }
  if (_node_prefs->ch_notif_override & mask) {
    _node_prefs->ch_notif_override &= ~mask;
    _node_prefs->ch_notif_muted    &= ~mask;
    changed = true;
  }
  if (_node_prefs->ch_fav_bitmask & mask) {
    _node_prefs->ch_fav_bitmask &= ~mask;
    changed = true;
  }

  if (changed) the_mesh.savePrefs();
}

// Homing beeper: while armed with a target and inside the radius, emit a short
// tick whose interval shrinks linearly with distance — slow at the edge, rapid
// near the centre. Polls distance a few times a second; silent outside the
// radius. The beeper has its own toggle (locator_beeper), so turning it on is
// an explicit "I want to hear this" — it deliberately overrides the global
// buzzer mute (playMelody → buzzer.playForced ignores the quiet flag).
void UITask::locatorProximityBeeper() {
  static const uint32_t BEEP_MIN_MS = 150;    // fastest cadence (at the target)
  static const uint32_t BEEP_MAX_MS = 2000;   // slowest cadence (at the edge)
  if (!_node_prefs || !_node_prefs->locator_enabled || !_node_prefs->locator_beeper
      || !_node_prefs->locator_has_target || _node_prefs->locator_mode == 1) {  // leave-only mode: no homing
    return;
  }
  if ((int32_t)(millis() - _locator_beep_check_ms) < 0) return;
  _locator_beep_check_ms = millis() + 250UL;

  float dist, r;
  if (!locatorDistance(dist, r)) return;
  if (dist > r) {                       // outside the zone: stay quiet, beep on re-entry
    _locator_beep_next_ms = millis();
    return;
  }
  if ((int32_t)(millis() - _locator_beep_next_ms) < 0) return;
  float frac = (r > 0) ? dist / r : 0;  // 0 at centre, 1 at edge
  if (frac < 0) frac = 0; else if (frac > 1) frac = 1;
  uint32_t interval = BEEP_MIN_MS + (uint32_t)(frac * (BEEP_MAX_MS - BEEP_MIN_MS));
  playMelody("locp:d=32,o=7,b=200:c");
  _locator_beep_next_ms = millis() + interval;
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

// A peer broadcast its position via a [LOC] message (parsed in MyMesh). Record
// it in the live-track table for the Nearby "Live" view / map. Gated on the
// user preference so it stays opt-in.
void UITask::onSharedLocation(const uint8_t* pub_key, const char* name,
                              int32_t lat_1e6, int32_t lon_1e6,
                              uint32_t ts, bool verified) {
  if (!_node_prefs || !_node_prefs->track_shared_loc) return;
  _livetrack.update(pub_key, name, lat_1e6, lon_1e6, ts, verified);
}

bool UITask::sendLocationShare(int32_t lat, int32_t lon) {
  if (!_node_prefs) return false;
  char text[80];
  if (_node_prefs->loc_share_target_type == 0) {
    // Channel: sendGroupMessage prepends "<name>: ", so the payload already
    // names the sender — keep the [LOC] text bare.
    snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f", lat / 1e6, lon / 1e6);
    ChannelDetails ch;
    if (!the_mesh.getChannel(_node_prefs->loc_share_channel_idx, ch)) return false;
    return the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                     the_mesh.getNodeName(), text, strlen(text));
  }
  // DM carries no per-message sender prefix, so embed the name in the text — the
  // share is then self-describing in any chat client (a trailing token after the
  // coordinate, which parseLocShare ignores on the receiving side).
  ContactInfo* c = the_mesh.lookupContactByPubKey(_node_prefs->loc_share_dm_prefix,
                                                  NodePrefs::FAVOURITE_PREFIX_LEN);
  if (!c) return false;
  snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f %s",
           lat / 1e6, lon / 1e6, the_mesh.getNodeName());
  uint32_t expected_ack = 0, est_timeout = 0;
  return the_mesh.sendMessage(*c, rtc_clock.getCurrentTime(), 0, text, expected_ack, est_timeout) > 0;
}

// One-shot "share my position" from the home Map page (Hold Enter). When live
// sharing is already on, push an immediate [LOC] to the same target; otherwise
// hand a [LOC] message to the recipient picker so the user chooses where it
// goes (no accidental broadcast to a default channel).
void UITask::quickShareMyLocation() {
  int32_t lat, lon;
  if (!currentLocation(lat, lon)) { showAlert("No GPS fix", 1000); return; }
  if (_node_prefs && _node_prefs->loc_share_enabled && sendLocationShare(lat, lon)) {
    showAlert("Position shared", 900);
    return;
  }
  char text[40];
  snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f", lat / 1e6, lon / 1e6);
  shareToMessage(text);
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
  // Same checkDisplayOn() gate every other input path goes through (see
  // pollCardKB()'s Fn+letter handling for the same shape) -- without it, a long
  // press while the display is off neither wakes it nor extends auto-off, and
  // while unlocked it delivers KEY_CONTEXT_MENU to the invisible screen (found
  // already open at the next wake instead of the press being consumed as a wake).
  c = checkDisplayOn(c);
  if (c == 0) return 0;
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

bool UITask::hasGPS() {
  if (_sensors != NULL) {
    int num = _sensors->getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(_sensors->getSettingName(i), "gps") == 0) return true;
    }
  }
  return false;
}

void UITask::toggleGPS() {
  if (_node_prefs) applyGpsState(_node_prefs->gps_enabled == 0);
}

// Sets GPS to an absolute state (vs. toggleGPS()'s flip) -- shared by the
// Home-page manual toggle and the bot's !gps on/off command, which needs to
// set a specific state rather than flip whatever it currently is.
void UITask::applyGpsState(bool on) {
  if (_sensors == NULL) return;
  int num = _sensors->getNumSettings();
  for (int i = 0; i < num; i++) {
    if (strcmp(_sensors->getSettingName(i), "gps") == 0) {
      _sensors->setSettingValue("gps", on ? "1" : "0");
      _node_prefs->gps_enabled = on ? 1 : 0;
      notify(UIEventType::ack);
      the_mesh.savePrefs();
      showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
      _next_refresh = 0;
      break;
    }
  }
}

void UITask::botSetGPS(bool on) {
  applyGpsState(on);
}

// Bot !buzz [seconds] -- a find-me signal, so it deliberately uses
// playForced() (bypasses the buzzer_quiet mute) rather than play(): a
// find-me beep that respects mute defeats its own purpose. Builds a simple
// repeating beep/rest RTTTL string sized to the requested duration into the
// persistent _bot_buzz_buf -- the nRF52 RTTTL player keeps a raw pointer into
// whatever buffer it's given and reads from it across loop() calls for the
// whole playback (same constraint as _notif_mel_buf), so this can't be a
// local/stack buffer.
void UITask::botBuzz(int seconds) {
#if defined(PIN_BUZZER)
  if (seconds < 1) seconds = 5;
  if (seconds > 30) seconds = 30;
  int pairs = seconds * 2;   // b=120: an "8c,8p," pair is 250+250 = 500ms
  int n = snprintf(_bot_buzz_buf, sizeof(_bot_buzz_buf), "Buzz:b=120:");
  for (int i = 0; i < pairs && n < (int)sizeof(_bot_buzz_buf) - 7; i++)
    n += snprintf(_bot_buzz_buf + n, sizeof(_bot_buzz_buf) - n, "8c,8p,");
  buzzer.playForced(_bot_buzz_buf);
#endif
}

#if defined(PIN_GPIO1)
static uint32_t gpioPin(int idx) {   // idx 1..4
  static const uint32_t pins[4] = { PIN_GPIO1, PIN_GPIO2, PIN_GPIO3, PIN_GPIO4 };
  return (idx >= 1 && idx <= 4) ? pins[idx - 1] : 0xFFFFFFFF;
}

static uint8_t* gpioModeField(NodePrefs* p, int idx) {   // idx 1..4
  switch (idx) {
    case 1: return &p->gpio1_mode;
    case 2: return &p->gpio2_mode;
    case 3: return &p->gpio3_mode;
    case 4: return &p->gpio4_mode;
    default: return NULL;
  }
}

// Push a saved mode value to the actual pin hardware -- shared by
// setGpioMode() (live edits from the UI) and applyAllGpioModes() (boot
// restore), which differ only in whether the mode gets persisted. Mode 4
// (Analog) uses the same "leave it alone" config as Off: the SAADC reads the
// pin directly regardless of the GPIO block's state, and cfg_default (no
// pull, disconnected buffer) is exactly what Nordic recommends for an ADC
// input to avoid extra leakage current -- there's nothing separate to set up
// here, unlike Input/Output.
static void applyGpioModeToPin(uint32_t pin, uint8_t mode) {
  switch (mode) {
    case 1: nrf_gpio_cfg_input(pin, NRF_GPIO_PIN_PULLUP); break;        // Input
    case 2: nrf_gpio_cfg_output(pin); nrf_gpio_pin_clear(pin); break;   // Output, off
    case 3: nrf_gpio_cfg_output(pin); nrf_gpio_pin_set(pin);   break;   // Output, on
    default: nrf_gpio_cfg_default(pin); break;                         // Off / Analog
  }
}

// GPIO1 (P0.02) = AIN0, GPIO2 (P0.29) = AIN5 -- the only two user pins wired
// to the nRF52840's SAADC (confirmed against wiring_analog_nRF52.c's own
// pin->channel switch). GPIO3/GPIO4 (P0.09/P0.10) have no ADC channel.
static uint32_t gpioAnalogPsel(int idx) {   // idx 1..4; 0 (NC) if unsupported
  if (idx == 1) return SAADC_CH_PSELP_PSELP_AnalogInput0;
  if (idx == 2) return SAADC_CH_PSELP_PSELP_AnalogInput5;
  return SAADC_CH_PSELP_PSELP_NC;
}

// One-shot SAADC read, bypassing Arduino's analogRead() -- that function
// treats its argument as an ARDUINO PIN INDEX (looked up through
// g_ADigitalPinMap[]), not a raw channel, and no Arduino index maps to our
// raw GPIO1/GPIO2 pins (same reason digitalWrite()/pinMode() can't be used
// for these pins either -- see the file-level notes on PIN_GPIO1..4).
// Mirrors wiring_analog_nRF52.c's analogRead_internal() exactly (10-bit,
// 0.6V internal reference, 1/6 gain -> 0-3.6V range) so the numbers read the
// same as a normal analogRead() would, just addressing the SAADC channel
// directly instead of going through the pin-index dispatch.
static uint16_t readAnalogMv(uint32_t psel) {
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_10bit;
  NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Enabled << SAADC_ENABLE_ENABLE_Pos);
  for (int i = 0; i < 8; i++) {
    NRF_SAADC->CH[i].PSELN = SAADC_CH_PSELP_PSELP_NC;
    NRF_SAADC->CH[i].PSELP = SAADC_CH_PSELP_PSELP_NC;
  }
  NRF_SAADC->CH[0].CONFIG =
      ((SAADC_CH_CONFIG_RESP_Bypass     << SAADC_CH_CONFIG_RESP_Pos)   & SAADC_CH_CONFIG_RESP_Msk)
    | ((SAADC_CH_CONFIG_RESP_Bypass     << SAADC_CH_CONFIG_RESN_Pos)   & SAADC_CH_CONFIG_RESN_Msk)
    | ((SAADC_CH_CONFIG_GAIN_Gain1_6    << SAADC_CH_CONFIG_GAIN_Pos)   & SAADC_CH_CONFIG_GAIN_Msk)
    | ((SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) & SAADC_CH_CONFIG_REFSEL_Msk)
    | ((SAADC_CH_CONFIG_TACQ_3us        << SAADC_CH_CONFIG_TACQ_Pos)   & SAADC_CH_CONFIG_TACQ_Msk)
    | ((SAADC_CH_CONFIG_MODE_SE         << SAADC_CH_CONFIG_MODE_Pos)   & SAADC_CH_CONFIG_MODE_Msk);
  NRF_SAADC->CH[0].PSELN = psel;
  NRF_SAADC->CH[0].PSELP = psel;

  volatile int16_t value = 0;
  NRF_SAADC->RESULT.PTR = (uint32_t)&value;
  NRF_SAADC->RESULT.MAXCNT = 1;

  NRF_SAADC->TASKS_START = 1;
  while (!NRF_SAADC->EVENTS_STARTED);
  NRF_SAADC->EVENTS_STARTED = 0;

  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END);
  NRF_SAADC->EVENTS_END = 0;

  NRF_SAADC->TASKS_STOP = 1;
  while (!NRF_SAADC->EVENTS_STOPPED);
  NRF_SAADC->EVENTS_STOPPED = 0;

  NRF_SAADC->ENABLE = (SAADC_ENABLE_ENABLE_Disabled << SAADC_ENABLE_ENABLE_Pos);

  if (value < 0) value = 0;
  // 10-bit, 1/6 gain, 0.6V internal ref -> full-scale = 0.6V / (1/6) = 3.6V
  return (uint16_t)(((uint32_t)value * 3600) / 1024);
}
#endif

// Set a user GPIO pin to a specific mode (0=Off 1=In 2=Out-low 3=Out-high
// 4=Analog), apply it to the actual pin, and persist. The Off->In->Out->...
// cycling itself lives in GpioScreen; the bot's !gpioN on/off and boot
// restore also route through here.
void UITask::setGpioMode(int idx, uint8_t mode) {
#if defined(PIN_GPIO1)
  if (!_node_prefs) return;
  uint8_t* f = gpioModeField(_node_prefs, idx);
  uint32_t pin = gpioPin(idx);
  if (!f || pin == 0xFFFFFFFF) return;
  if (mode == 4 && !gpioSupportsAnalog(idx)) mode = 0;   // no ADC channel on this pin -- fall back to Off
  *f = mode;
  applyGpioModeToPin(pin, mode);
  the_mesh.savePrefs();
#else
  (void)idx; (void)mode;
#endif
}

// Boot-time restore: push each pin's saved mode to hardware before any UI/bot
// interaction (mirrors MyMesh::applyGpsPrefs()'s role for the GPS toggle --
// there's no generic "restore all settings" hook in this codebase, each
// persisted hardware toggle gets its own bespoke boot call). Deliberately
// doesn't call savePrefs() -- nothing changed, just re-applying what's
// already on disk.
void UITask::applyAllGpioModes() {
#if defined(PIN_GPIO1)
  if (!_node_prefs) return;
  for (int i = 1; i <= 4; i++) {
    uint8_t* f = gpioModeField(_node_prefs, i);
    if (f) applyGpioModeToPin(gpioPin(i), *f);
  }
#endif
}

bool UITask::botSetGPIO(int idx, bool on) {
#if defined(PIN_GPIO1)
  if (!_node_prefs) return false;
  uint8_t* f = gpioModeField(_node_prefs, idx);
  if (!f || (*f != 2 && *f != 3)) return false;   // not configured as Output
  setGpioMode(idx, on ? 3 : 2);
  return true;
#else
  (void)idx; (void)on;
  return false;
#endif
}

bool UITask::botGetGPIO(int idx, bool& is_output, bool& value) {
#if defined(PIN_GPIO1)
  if (!_node_prefs) return false;
  uint8_t* f = gpioModeField(_node_prefs, idx);
  uint32_t pin = gpioPin(idx);
  if (!f || *f == 0 || *f == 4 || pin == 0xFFFFFFFF) return false;   // Off / Analog / unsupported
  is_output = (*f == 2 || *f == 3);
  value = is_output ? (nrf_gpio_pin_out_read(pin) != 0) : (nrf_gpio_pin_read(pin) != 0);
  return true;
#else
  (void)idx; (void)is_output; (void)value;
  return false;
#endif
}

bool UITask::gpioSupportsAnalog(int idx) const {
#if defined(PIN_GPIO1)
  return idx == 1 || idx == 2;
#else
  (void)idx;
  return false;
#endif
}

bool UITask::botGetGPIOAnalog(int idx, int& millivolts) {
#if defined(PIN_GPIO1)
  if (!_node_prefs || !gpioSupportsAnalog(idx)) return false;
  uint8_t* f = gpioModeField(_node_prefs, idx);
  if (!f || *f != 4) return false;   // not in Analog mode
  millivolts = readAnalogMv(gpioAnalogPsel(idx));
  return true;
#else
  (void)idx; (void)millivolts;
  return false;
#endif
}

void UITask::applyTxPower() {
  if (_node_prefs == NULL) return;
  // With APC on, tx_power_dbm is the ceiling — re-baseline the controller to it
  // (which also sets the radio) so the live power tracks the new ceiling at once.
  if (_node_prefs->tx_apc) { the_mesh.applyApc(); return; }
  radio_driver.setTxPower(_node_prefs->tx_power_dbm);
}

void UITask::applyPowerSave() {
  if (_node_prefs == NULL) return;
  // A repeater must hear every packet to relay it, so duty-cycle RX (which sleeps
  // between preamble checks) is forced off while repeating — the user's pref is
  // kept and restored when the repeater is switched off.
  radio_driver.setPowerSaving(_node_prefs->rx_powersave && !_node_prefs->client_repeat);
}

void UITask::applyApc() {
  the_mesh.applyApc();   // (re)initialise Adaptive Power Control from prefs
}

#if ENV_INCLUDE_GPS == 1
void UITask::applyGpsInterval() {
  if (_node_prefs == NULL || _sensors == NULL) return;
  char buf[12];
  sprintf(buf, "%u", _node_prefs->gps_interval);
  _sensors->setSettingValue("gps_interval", buf);
}
#endif

void UITask::applyRadioParams() {
  if (_node_prefs == NULL) return;
  the_mesh.applyRepeaterRadio();   // companion params, or the repeater profile if relaying with one set
}

void UITask::applyBrightness() {
  if (_display != NULL && _node_prefs != NULL) {
    _display->setBrightness(_node_prefs->display_brightness);
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
