#include "UITask.h"
#include "../ui-core/SoundNotifier.h"
#include "../ui-core/SoundControl.h"
#include "../ui-core/Telemetry.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "../MsgExpand.h"
#include "../Features.h"
#include "../GeoUtils.h"
#include "target.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
#endif
#ifdef SIM_PLATFORM
  #include <sys/select.h>
  #include <unistd.h>
  #ifdef __EMSCRIPTEN__
    #include <emscripten.h>
    // The single UITask instance is a file-scope global in
    // examples/companion_radio/main.cpp (`UITask ui_task(...)`, only under
    // `#ifdef DISPLAY_CLASS`, which the sim build always defines) -- not
    // reachable from here by name, so UITask::begin() stashes `this` here
    // (see below) the same way every other single-instance sim glue point
    // does. Declared up here (rather than next to its use near enqueueKey(),
    // further down this file) since UITask::begin() -- also further down,
    // but earlier in the file -- needs it too.
    static UITask* g_sim_ui_task_for_js = nullptr;
  #endif
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
// Upstream MeshCore version, shown on the splash screen. Most variants set it in
// their platformio.ini; the ones that don't used to fail to compile this file
// outright rather than fall back, which quietly made every ui-new env on those
// boards unbuildable (heltec v3/v4, thinknode m1/m5, mesh pocket, techo).
#ifndef MESHCORE_VERSION
  #define MESHCORE_VERSION    "1.17.1"
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


#include "icons.h"
#include "../ui-core/Lettering.h"   // boot splash wordmark + lettering (shared with the L2)
#include "GfxUtils.h"   // gfx::drawLine — connects trail points on the Home map preview

// Blinking status indicators: on for the first half of a 4 s cycle, but e-ink
// can't repaint fast enough to blink, so it shows them steadily.
static inline bool blinkOn() {
  return Features::BLINK_INDICATORS ? ((millis() % 4000) < 2000) : true;
}

// Boot splash, as on the L2 (ui-lvgl/Splash.h, same lettering from
// ui-core/Lettering.h): the MeshCore wordmark rising into place, "solo" in its
// lettering once it has landed, the Solo version, upstream version + build date, and three dots
// rising in turn while the device starts. E-ink gets the still final frame.
class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long _start, dismiss_after;
  char _solo_ver[24];
  char _line2[32];

  // A word in the wordmark's lettering, 1:1, top-left at (x, y).
  static void drawLettering(DisplayDriver& d, const char* text, int x, int y, int gap) {
    for (const char* p = text; *p; p++) {
      char c = (char)tolower((unsigned char)*p);
      int cw = lettering::charW(c);
      if (cw < 0) continue;
      for (int r = 0; r < lettering::LOGO_H; r++)
        for (int col = 0; col < cw; ) {   // runs of inked pixels as one rect
          if (!lettering::inked(c, col, r)) { col++; continue; }
          int e = col;
          while (e + 1 < cw && lettering::inked(c, e + 1, r)) e++;
          d.fillRect(x + col, y + r, e - col + 1, 1);
          col = e + 1;
        }
      x += cw + gap;
    }
  }

public:
  SplashScreen(UITask* task) : _task(task) {
    lettering::soloVersion(_solo_ver, sizeof(_solo_ver));
    char date[16];
    lettering::buildDate(date, sizeof(date));
#ifdef MESHCORE_VERSION
    snprintf(_line2, sizeof(_line2), "%s  %s", MESHCORE_VERSION, date);
#else
    snprintf(_line2, sizeof(_line2), "%s", date);
#endif
    _start = millis();
    dismiss_after = _start + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    const bool anim = !display.isEink();
    const unsigned long t = anim ? millis() - _start : 100000UL;   // e-ink: the settled frame
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    const int lh = display.getLineHeight();
    const int cx = display.width() / 2;

    // Block from the wordmark down to the second text line, centred above the dots.
    const int dots_h = 8;
    const int block_h = lettering::LOGO_H * 2 + 5 + lh * 2 + 2;
    int top = (display.height() - dots_h - block_h) / 2;
    if (top < 0) top = 0;

    // Wordmark: rises 6 px into place over the first 400 ms (ease-out).
    int rise = 0;
    if (t < 400) { int k = 400 - (int)t; rise = (6 * k * k) / (400 * 400); }
    display.drawXbm((display.width() - lettering::LOGO_W) / 2, top + rise, lettering::LOGO, lettering::LOGO_W, lettering::LOGO_H);

    int y = top + lettering::LOGO_H + 3;
    const int solo_w = lettering::textW("solo", 2);
    if (t >= 400) drawLettering(display, "solo", cx - solo_w / 2, y, 2);   // once the wordmark has landed
    y += lettering::LOGO_H + 2;
    display.drawTextCentered(cx, y, _solo_ver[0] ? _solo_ver : "dev");
    y += lh + 2;
    display.drawTextCentered(cx, y, _line2);

    drawLoadingDots(display, cx, display.height() - 2);   // as the L2's
    return anim ? 40 : 1000;
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
#include "../ui-core/MessageText.h"
#include "SensorPlaceholders.h"
#include "../ui-core/UiCore.h"   // shared UI Core: history rings + unread models (MessagesScreen views them)
#include "../ui-core/SettingsSchema.h"   // the settings both frontends share (SettingsScreen.h)
#include "SettingsScreen.h"
#include "MessagesScreen.h"

// ── Custom screens (separate files to ease upstream merges) ───────────────────
#include "RingtoneEditorScreen.h"
#include "BotScreen.h"
#include "AdminScreen.h"
#include "../ui-core/NearbyModel.h"
#include "../ui-core/RadioControl.h"
#include "NearbyScreen.h"
#include "DashboardConfigScreen.h"
#include "AutoAdvertScreen.h"
#include "LiveShareScreen.h"
#include "LocatorScreen.h"
#include "TrailScreen.h"
#include "CompassScreen.h"
#if ENV_INCLUDE_GPS == 1 && defined(GPS_SKYVIEW)
  #include "SatellitesScreen.h"
#endif
#include "DiagnosticsScreen.h"
#include "RepeaterScreen.h"
#if defined(PIN_GPIO1)
#include "GpioScreen.h"
#endif
#include "ToolsScreen.h"
#include "ClockToolsScreen.h"   // Alarm / Timer / Stopwatch (Clock page › Enter)

#include "../ui-core/Battery.h"

// Voltage -> battery %, from the Core (top bar and dashboard Batt% alike).
static int battMvToPercent(int mv, int low_mv) { return battery::percent(mv, low_mv); }

// Render the time starting at top_y; returns the y just below the time block
// so the caller can flow the date / dashboard rows beneath it.
//
// On a tall portrait panel (e-ink in portrait — height > width) HH and MM are
// stacked on two lines in the huge built-in font (size 4, ~56 px tall) so the
// digits fill the narrow width. On wide panels (OLED, landscape e-ink) the
// classic single-line "HH:MM" at size 2 is kept.
static int drawClockTime(DisplayDriver& d, int top_y, const struct tm* ti,
                         bool h12, bool show_sec, bool alignCenter = false) {
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
      if (alignCenter) {
        int w = (int)d.getTextWidth(s) - trail;
        d.setCursor(cx - w / 2, yy);
        d.print(s);
      } else {
        d.setCursor(0, yy);
        d.print(s);
      }
    };
    drawBig(hbuf, y);  y += lhb + 2;
    drawBig(mbuf, y);  y += lhb + 2;
    if (ap) {
      d.setTextSize(2);
      if (alignCenter) {
        d.drawTextCentered(cx, y, ap);
      } else {
        d.setCursor(0, y);
        d.print(ap);
      }
      y += d.getLineHeight() + 1;
    }
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
  if (alignCenter) {
    d.drawTextCentered(d.width() / 2, top_y, buf);
  } else {
    d.setCursor(0, top_y);
    d.print(buf);
  }
  d.setTextSize(1);
  return top_y + lh2 + 2;
}

// ── HomeScreen ────────────────────────────────────────────────────────────────
// Forward declaration to be able to call formatDashVal from HomeScreen::render()
static void formatDashVal(uint8_t field, char* val, int val_len, uint16_t batt_mv, uint16_t low_batt_mv,
                          int unread, bool unread_overflow, bool imperial, CayenneLPP* lpp, bool nouns);

// Telemetry (ui-core/Telemetry.h) in this display's font and width.
static const telemetry::Style L1_TELEMETRY = { "\xf8", false, false, 3 };
// Altitude (baro or GPS) in Settings > System > Units, always the small unit.
static void fmtAlt(char* buf, int n, float meters, bool imperial) {
  telemetry::altText(meters, imperial, L1_TELEMETRY, buf, n);
}

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
    Count,   // keep before LOCK — navigable-page count
    LOCK     // lock screen: full clock + dashboard view, not a navigable page
  };

  // Selected slot on the Favourites page (0..FAVOURITES_COUNT - 1).
  uint8_t _fav_sel = 0;

  // Slot payload (pubkey prefix, or channel index in byte 0 — see the slot's
  // kind), or nullptr when the slot is empty.
  const uint8_t* favSlotPrefix(int slot) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p || _task->isFavouriteSlotEmpty(slot)) return nullptr;
    return p->favourite_contacts[slot];
  }

  // Unpin/Replace menu for a filled tile. Choosing what to pin is the Messages
  // screen's job (see UITask::pickFavouriteTarget) -- the dial doesn't carry a
  // second browser for contacts, rooms and channels.
  PopupMenu _tile_menu;
  int       _pin_target_slot = -1;

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  uint8_t _prev_page;   // home page restored when the device unlocks
  bool _shutdown_init;

  // Sideways slide between pages (OLED; drivers that can't read their frame
  // back just cut over). _slide_dir is +1 (new page from the right), -1, or 0.
  static const unsigned long SLIDE_MS = 220;
  int8_t        _slide_dir = 0;
  uint8_t       _slide_from = 0;
  unsigned long _slide_t0 = 0;

  // First row under the header and the page-icon row (see render()).
  static int contentTop(DisplayDriver& d) {
    const int pg_half = (5 * miniIconScale(d) + 1) / 2;
    return d.getLineHeight() + 2 * pg_half + 4;
  }
  // 0..1 eased progress of the running slide, or -1 when none.
  float slideProgress() {
    if (!_slide_dir) return -1;
    unsigned long el = millis() - _slide_t0;
    if (el >= SLIDE_MS) { _slide_dir = 0; return -1; }
    float t = 1.0f - (float)el / SLIDE_MS;
    return 1.0f - t * t * t;           // ease-out: quick start, soft landing
  }
  void turnPage(int dir) {
    const uint8_t from = _page;
    _page = navPage(_page, dir);
    if (_page == from) return;
    DisplayDriver* d = _task->getDisplay();
    // Pages with the header slide below it; the full-screen clock slides whole.
    const bool whole = from == CLOCK || _page == CLOCK;
    if (d && d->slideBegin(whole ? 0 : contentTop(*d))) {
      _slide_dir = (int8_t)dir; _slide_from = from; _slide_t0 = millis();
    }
  }

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

  // reserve_left: how much width from x=0 must stay clear of status icons --
  // the node name on every other page (name_min below), or the LOCK page's
  // own clock (see the LOCK branch in render(), which passes its actual
  // footprint here so the icon row sheds low-priority icons instead of
  // drawing over the clock). -1 = use the normal name reserve.
  int renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts, int reserve_left = -1) {
    int low_mv = _node_prefs ? (int)_node_prefs->low_batt_mv : 0;
    int pct = battMvToPercent((int)batteryMilliVolts, low_mv);

    uint8_t mode = battery::mode(_node_prefs ? _node_prefs->batt_display_mode : 0);

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int ind     = cw + 2;    // single-char indicator width
    const int ind_h   = display.isSingleFont() ? lh - 2 : lh;
    const int ind_gap = display.isLandscape() ? 3 : 1;  // gap between indicator boxes

    int battLeftX;
    if (mode == battery::PERCENT) {
      char buf[6];
      snprintf(buf, sizeof(buf),"%d%%", pct);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, 0);
      display.print(buf);
    } else if (mode == battery::VOLTAGE) {
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
    const int name_min = (reserve_left >= 0) ? reserve_left : display.getCharWidth() * 5;
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
       _prev_page(0), _shutdown_init(false), sensors_lpp(200) {  }

  // Flips home screen between LOCK page and the page that was showing before locking
  void setLocked(bool locked) {
    if (locked) {
      if (_page != LOCK) _prev_page = _page;
      _page = LOCK;
    } else {
      _page = _prev_page;
    }
  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      // _shutdown_init is never cleared elsewhere -- on real hardware
      // that's harmless because _board->powerOff() halts the MCU, so
      // there is no next poll() tick to matter. In the sim, powerOff()
      // is a deliberate no-op (no real hardware to power off, see
      // SimMainBoard.h), so without this the instance keeps running and
      // this branch re-fires shutdown() -> _display->turnOff() on every
      // single tick forever, repeatedly blacking out its canvas -- which
      // fights with a freshly reset instance's own boot splash trying to
      // render onto that same (simInstanceTag-keyed) canvas element.
      _shutdown_init = false;
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
    int mq_delay = 0;   // >0 while a selected row's name is marquee-scrolling
    int anim_ms = 0;    // >0 while something animates: a hovering page icon, a page turn
    display.setTextSize(1);
    const int lh      = display.getLineHeight();  // line height at sz1
    const int step    = display.lineStep();        // lh + 2
    // Page-indicator row: small (5px) page icons replace the old dots. Centre and
    // gap scale with the font so the band clears the header above and content
    // below (identical to the old lh+4 / +6 dots layout at 1x).
    const int pg_half   = (5 * miniIconScale(display) + 1) / 2;
    const int dots_y    = lh + pg_half + 1;       // icon-row centre, below the header
    const int content_y = dots_y + pg_half + 3;   // first content row, below the icons (= contentTop())
    const float slide   = slideProgress();        // -1, or how far a page turn has got

    // Title bar displaying node name (except on lock screen), status icons and battery.
    // Hidden on fullscreen pages (CLOCK).
    if (_page != CLOCK) {
      display.setColor(DisplayDriver::LIGHT);
      int lock_reserve = -1;
      if (_page == LOCK) {
        // The lock screen's own clock shares row 0 with this title bar --
        // reserve its real footprint instead of the usual name_min, so the
        // (already priority-ordered) status icons shed low-priority ones as
        // needed and never draw over it. Built-in font is fixed-width, so a
        // worst-case digit string measures this without the actual time.
        bool tall = display.height() > display.width();
        display.setTextSize(tall ? 4 : 2);
        lock_reserve = display.getTextWidth(tall ? "88" : "88:88");
        display.setTextSize(1);
      }
      int rightEdge = renderBatteryIndicator(display, _task->getBattMilliVolts(), lock_reserve);
      display.setColor(DisplayDriver::LIGHT);

      if (_page != LOCK) {
        // The time, once the clock is set (the node name before that): the
        // name is what others see, the time is what you glance at here.
        char filtered_name[sizeof(_node_prefs->node_name)];
        uint32_t now_ts = _rtc->getCurrentTime();
        if (now_ts >= 1000000000UL) {
          struct tm lt;
          localTm(now_ts, _node_prefs->tz_offset_hours, lt);
          int hh = lt.tm_hour;
          if (_node_prefs->clock_12h) { hh %= 12; if (hh == 0) hh = 12; }
          snprintf(filtered_name, sizeof(filtered_name), "%d:%02d", hh, lt.tm_min);
        } else {
          display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
        }

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
    }

    // ensure current page is visible (e.g. after settings change)
    if (!isPageVisible(_page)) _page = navPage(_page, +1);

    // curr page indicator — a row of small page icons, one per visible page, with
    // the current page underlined. Hidden on CLOCK and LOCK (full screen used for
    // the clock/dashboard).
    if (_page != CLOCK && _page != LOCK) {
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
      // The current page: its icon knocked out of a soft pill. During a page
      // turn the pill glides over from the page being left.
      int pw = icon_w + 4;
      if (pw > pitch - 1) pw = pitch - 1;
      int pill_x = x + pitch * curr_vis;
      if (slide >= 0) {
        for (int i = 0; i < n; i++)
          if (order[i] == _slide_from) { pill_x = x + pitch * i + (int)((pill_x - x - pitch * i) * slide + 0.5f); break; }
      }
      display.setColor(DisplayDriver::LIGHT);
      display.fillSoftRect(pill_x - pw / 2, dots_y - pg_half - 1, pw, pg_half * 2 + 3);
      for (int i = 0; i < n; i++) {
        const MiniIcon* ic = pageIcon(order[i]);
        const int d2 = 2 * (x - pill_x < 0 ? pill_x - x : x - pill_x);
        if (d2 <= pw - icon_w)       display.setColor(DisplayDriver::DARK);   // inside the pill
        else if (d2 < pw + icon_w) { x += pitch; continue; }                   // half under the gliding pill
        else                         display.setColor(DisplayDriver::LIGHT);
        if (ic) miniIconDrawCentered(display, x, dots_y, *ic);
        x += pitch;
      }
      display.setColor(DisplayDriver::LIGHT);
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
        struct tm lt;
        localTm(unix_ts, _node_prefs ? _node_prefs->tz_offset_hours : 0, lt);
        struct tm* ti = &lt;

        char buf[24];
        display.setColor(DisplayDriver::LIGHT);
        bool show_sec = !Features::IS_EINK && (!_node_prefs || !_node_prefs->clock_hide_seconds);
        bool h12 = _node_prefs && _node_prefs->clock_12h;
        int date_y = drawClockTime(display, 0, ti, h12, show_sec, true);

        display.setTextSize(1);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(buf, sizeof(buf),"%s %d %s %d", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon], 1900 + ti->tm_year);
        display.setCursor(0, date_y);
        display.print(buf);

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
            if (field == telemetry::NONE) continue;

            const char* label = telemetry::LABEL[field < telemetry::COUNT ? field : telemetry::NONE];
            char val[20];
            formatDashVal(field, val, sizeof(val), _task->getBattMilliVolts(), _node_prefs->low_batt_mv,
                          _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount(),
                          _task->getAnyUnreadOverflow(), _node_prefs->units_imperial, &sensors_lpp, false);

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
    } else if (_page == HomePage::LOCK) {
      // Lock screen: clock + two dashboard spots + unlock-hint popup
      uint32_t unix_ts = _rtc->getCurrentTime();
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      if (unix_ts < 1000000000UL) {
        display.drawTextCentered(display.width() / 2, display.height() / 2 - step, "No time sync");
      } else {
        struct tm lt;
        localTm(unix_ts, _node_prefs ? _node_prefs->tz_offset_hours : 0, lt);
        struct tm* ti = &lt;
        char buf[12];
        bool h12 = _node_prefs && _node_prefs->clock_12h;
        // Left-aligned on purpose: the rest of the top row is left for status icons.
        int date_y = drawClockTime(display, 0, ti, h12, /*show_sec*/false);
        display.setTextSize(1);
        static const char* wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        static const char* mo[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(buf, sizeof(buf),"%s %d %s", wd[ti->tm_wday], ti->tm_mday, mo[ti->tm_mon]);
        display.setCursor(0, date_y);
        display.print(buf);

        // Two sensor values side by side (dashboard_fields[0] and [1])
        if (_node_prefs) {
          char v0[20] = "", v1[20] = "";
          CayenneLPP* lpp_ptr = nullptr;
          uint8_t f0 = _node_prefs->dashboard_fields[0], f1 = _node_prefs->dashboard_fields[1];
          auto isLPP = [](uint8_t f) {
            return f==telemetry::TEMP||f==telemetry::HUM||f==telemetry::PRES||f==telemetry::ALT||f==telemetry::LUX||f==telemetry::CO2;
          };
          if (isLPP(f0) || isLPP(f1)) {
            sensors_lpp.reset(); sensors.querySensors(0xFF, sensors_lpp); lpp_ptr = &sensors_lpp;
          }
          bool show_msgs = f0 == telemetry::MSGS || f1 == telemetry::MSGS;
          int unread = show_msgs
                     ? _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount() : 0;
          bool unread_overflow = show_msgs && _task->getAnyUnreadOverflow();
          uint16_t batt_mv = _task->getBattMilliVolts();
          formatDashVal(f0, v0, sizeof(v0), batt_mv, _node_prefs->low_batt_mv, unread, unread_overflow, _node_prefs->units_imperial, lpp_ptr, true);
          formatDashVal(f1, v1, sizeof(v1), batt_mv, _node_prefs->low_batt_mv, unread, unread_overflow, _node_prefs->units_imperial, lpp_ptr, true);
          if (v0[0] || v1[0]) {
            int sv_y = date_y + step;
            display.setColor(DisplayDriver::LIGHT);
            if (v0[0] && v1[0]) {
              display.setCursor(0, sv_y);
              display.print(v0);
              int vw = display.getTextWidth(v1);
              display.setCursor(display.width() - vw, sv_y);
              display.print(v1);
            } else {
              const char* sv = v0[0] ? v0 : v1;
              display.drawTextCentered(display.width() / 2, sv_y, sv);
            }
          }
        }
      }
      // Unlock-hint popup at the bottom (like alert style)
      display.setTextSize(1);
      const int lk_lh = display.getLineHeight();
#if defined(CARDKB_I2C)
      const char* hint = _task->lockSeqCount() == 0 ? (_task->hasCardKB() ? "Back+3xEnter/Fn+Esc" : "Hold Back + 3xEnter") :
                         _task->lockSeqCount() == 1 ? "Enter x2 more..."   : "Enter x1 more...";
#else
      const char* hint = _task->lockSeqCount() == 0 ? "Hold Back + 3xEnter" :
                         _task->lockSeqCount() == 1 ? "Enter x2 more..."   : "Enter x1 more...";
#endif
      const int p = 3;
      const int hy = display.height() - lk_lh - p * 2;
      const int hw = display.getTextWidth(hint);
      const int hx = (display.width() - hw) / 2;
      display.drawPanel(hx - p, hy - p, hw + p*2, lk_lh + p*2);
      display.setCursor(hx, hy);
      display.print(hint);
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
      // Was gated to "n/a" while duty-cycle RX (Pwr save) was active, on the
      // assumption that the floor only gets sampled during continuous RX --
      // stale since RadioLibWrapper's periodic recalibration (noiseFloorCalibCheck(),
      // NF_CALIB_INTERVAL_MS) started keeping it fresh even under duty-cycle,
      // same live value Diagnostics already showed unconditionally.
      snprintf(tmp, sizeof(tmp),"Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      anim_ms = drawHoverIcon(display, display.width() / 2, content_y, BIG_BLUETOOTH, !_task->isSerialEnabled());
      const int text_y = content_y + BIG_BLUETOOTH.h + HOVER_GAP;
      // The pairing PIN is BLE-specific: show it while BLE is on but not yet
      // bonded. (Gating on a plain isConnected() broke this on dual builds,
      // where it's hardcoded true.)
      const bool waiting_for_pair = _task->isSerialEnabled() && !_task->isBLEConnected() && the_mesh.getBLEPin() != 0;
      if (waiting_for_pair && !display.isLandscape()) {
        char pin_buf[16];
        snprintf(pin_buf, sizeof(pin_buf), "PIN: %d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, text_y, pin_buf);
      } else if (waiting_for_pair) {   // the pairing PIN takes the title's place
        char pin_buf[16];
        snprintf(pin_buf, sizeof(pin_buf), "PIN: %d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, text_y, pin_buf);
      } else {
        // Each icon page carries just its title; Enter acting on it is the same everywhere.
        display.drawTextCentered(display.width() / 2, text_y, "Bluetooth");
      }
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::LIGHT);
      anim_ms = drawHoverIcon(display, display.width() / 2, content_y, BIG_ADVERT);
      display.drawTextCentered(display.width() / 2, content_y + BIG_ADVERT.h + HOVER_GAP, "Advert");
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
        // The driver reports 999 deg until a first fix: show a dash, not the
        // placeholder. After a fix the last known position stays.
        if (labs(nmea->getLatitude()) > 90000000L) strcpy(buf, "-");
        else snprintf(buf, sizeof(buf),"%.4f %.4f",
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y += step;
        display.drawTextLeftAlign(0, y, "alt");
        fmtAlt(buf, sizeof(buf), nmea->getAltitude() / 1000.0f, _node_prefs && _node_prefs->units_imperial);
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
            telemetry::lppText(r, type, _node_prefs && _node_prefs->units_imperial, L1_TELEMETRY, buf, sizeof(buf));
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
      anim_ms = drawHoverIcon(display, display.width() / 2, content_y, BIG_SETTINGS);
      display.drawTextCentered(display.width() / 2, content_y + BIG_SETTINGS.h + HOVER_GAP, "Settings");
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
      anim_ms = drawHoverIcon(display, display.width() / 2, content_y, BIG_TOOLS);
      display.drawTextCentered(display.width() / 2, content_y + BIG_TOOLS.h + HOVER_GAP, "Tools");
    } else if (_page == HomePage::QUICK_MSG) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      const int ix = (display.width() - BIG_MESSAGES.w) / 2;
      anim_ms = drawHoverIcon(display, display.width() / 2, content_y, BIG_MESSAGES);
      // Unread count as the usual pill on the bubble's top-right corner.
      int total_unread = _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount();
      if (total_unread > 0) {
        int bw = display.unreadBadgeWidth(total_unread, _task->getAnyUnreadOverflow());
        int bx = ix + BIG_MESSAGES.w + bw / 2;
        display.setColor(DisplayDriver::DARK);   // a dark ring so it reads over the bubble's border
        const int by = content_y - hoverLift(display);   // rides on the hovering bubble
        display.fillRect(bx - bw - 1, by + 1, bw + 2, lh + 2);
        display.drawUnreadBadge(bx, by + 2, total_unread, false, _task->getAnyUnreadOverflow());
      }
      display.drawTextCentered(display.width() / 2, content_y + BIG_MESSAGES.h + HOVER_GAP, "Messages");
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

        const uint8_t* prefix = favSlotPrefix(i);
        char    name[26];
        uint8_t unread   = 0;
        bool    overflow = false;
        bool    resolved = false;

        if (prefix && _task->favouriteSlotKind(i) == NodePrefs::FAV_KIND_CHANNEL) {
          uint8_t ch_idx = prefix[0];
          ChannelDetails ch;
          if (the_mesh.getChannel(ch_idx, ch) && ch.name[0]) {
            // '#' marks a channel apart from a contact tile — the two share the
            // grid and Enter does something different on each.
            name[0] = '#';
            display.translateUTF8ToBlocks(name + 1, ch.name, sizeof(name) - 1);
            unread   = _task->getChannelUnread(ch_idx);
            overflow = unread > 0 && _task->getChannelUnreadOverflow(ch_idx);
            resolved = true;
          }
        } else if (prefix) {
          for (int idx = 0; ; idx++) {
            ContactInfo c;
            if (!the_mesh.getContactByIdx(idx, c)) break;
            if (memcmp(c.id.pub_key, prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
              display.translateUTF8ToBlocks(name, c.name, sizeof(name));
              unread   = _task->getDMUnread(c.id.pub_key);
              overflow = unread > 0 && _task->getDMUnreadOverflow(c.id.pub_key);
              resolved = true;
              break;
            }
          }
        }

        if (prefix && !resolved) {
          // The pinned target is gone — prefs outlived the contact/channel list
          // (e.g. a wiped /contacts3; onContactRemoved/onChannelRemoved only
          // catch a live delete). Clear the stale slot so it renders as an empty
          // "+" tile instead of a blank one. Persisted once after the loop.
          _task->clearFavouriteSlot(i);
          fav_changed = true;
        }

        if (resolved) {
          // Reserve space for the unread badge so the name's ellipsis lands
          // before it instead of underneath. Badge and name share one baseline.
          int  bw = unread > 0 ? display.unreadBadgeWidth(unread, overflow) + 3 : 0;  // badge + 3 px gap
          int name_y     = cy + (cell_h - line_h) / 2;
          int name_max_w = cell_w - 4 - bw;
          if (name_max_w < 6) name_max_w = 6;
          int r = display.drawTextEllipsized(cx + 2, name_y, name_max_w, name, sel);
          if (sel && r > 0) mq_delay = r;
          if (unread > 0)
            display.drawUnreadBadge(cx + cell_w - 2, name_y, unread, sel, overflow);
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
      if (_tile_menu.active) _tile_menu.render(display);
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, content_y + step, "hibernating...");
      } else {
        anim_ms = drawHoverIcon(display, display.width() / 2, content_y, BIG_POWER);
        const int text_y = content_y + BIG_POWER.h + HOVER_GAP;
        const int lh1 = display.getLineHeight();
        if (text_y + lh1 <= display.height())
          display.drawTextCentered(display.width() / 2, text_y, "Hibernate");
      }
    }
    if (slide >= 0) {   // page turn: the page left behind slides out, this one in
      display.slideCompose(_slide_dir * (int)(display.width() * slide + 0.5f));
      anim_ms = 15;
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
      return (mq_delay > 0 && mq_delay < Features::HOME_REFRESH_MS) ? mq_delay : Features::HOME_REFRESH_MS;
    }
    if (_page == HomePage::CLOCK) {
      bool show_sec = !_node_prefs || !_node_prefs->clock_hide_seconds;
      int ret = need_blink ? 1000 : (show_sec ? 1000 : 60000);
      if (anim_ms > 0 && anim_ms < ret) ret = anim_ms;
      return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret;
    }
    int ret = need_blink ? 1000 : 5000;
    if (anim_ms > 0 && anim_ms < ret) ret = anim_ms;
    return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret;
  }

  bool handleInput(char c) override {
    // Favourites grid claims joystick UP/DOWN and inner LEFT/RIGHT; LEFT at the
    // left column and RIGHT at the right column fall through to page nav so the
    // user can still leave the page sideways.
    if (_page == HomePage::FAVOURITES) {
      // The tile menu consumes all input while open.
      if (_tile_menu.active) {
        auto res = _tile_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _pin_target_slot >= 0) {
          if (_tile_menu.selectedIndex() == 1) {          // Replace
            _task->pickFavouriteTarget(_pin_target_slot);
            _pin_target_slot = -1;
            return true;
          }
          _task->clearFavouriteSlot(_pin_target_slot);
          the_mesh.savePrefs();
          char alert[24];
          snprintf(alert, sizeof(alert), "Unpinned (slot %d)", _pin_target_slot + 1);
          _task->showAlert(alert, 800);
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
      if (c == KEY_CONTEXT_MENU) {
        // Filled tile → Unpin / Replace. An empty tile has nothing to offer:
        // its Enter already opens the picker.
        if (favSlotPrefix(_fav_sel)) {
          _pin_target_slot = _fav_sel;
          _tile_menu.begin("Slot options", 2);
          _tile_menu.addItem("Unpin");
          _tile_menu.addItem("Replace");
        }
        return true;
      }
      if (c == KEY_ENTER) {
        // Filled slot → open its conversation, empty slot → in-place pin picker.
        const uint8_t* pfx = favSlotPrefix(_fav_sel);
        if (!pfx) { _task->pickFavouriteTarget(_fav_sel); return true; }
        if (_task->favouriteSlotKind(_fav_sel) == NodePrefs::FAV_KIND_CHANNEL) {
          _task->openChannelHistory(pfx[0]);
          return true;
        }
        for (int idx = 0; ; idx++) {
          ContactInfo c2;
          if (!the_mesh.getContactByIdx(idx, c2)) break;
          if (memcmp(c2.id.pub_key, pfx, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
            // A room opens through its own entry point: posting to one needs a
            // login handshake that a plain DM view would skip.
            if (c2.type == ADV_TYPE_ROOM) _task->openRoomServer(c2);
            else                          _task->openContactDM(c2);
            return true;
          }
        }
        _task->showAlert("Contact not found", 800);
        return true;
      }
      // Edge LEFT/RIGHT and unhandled keys fall through to page nav below.
    }

    if (c == KEY_LEFT || c == KEY_PREV) {
      turnPage(-1);
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      turnPage(+1);
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
        _task->showAlert("Advert sent", 1000);
      } else {
        _task->showAlert("Advert failed", 1000);
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

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  g_sim_ui_task_for_js = this;   // see sim_enqueue_key() below
#endif

#if defined(CARDKB_I2C)
  // On the ENV_PIN_SDA/SCL path, CARDKB_I2C is Wire1, already brought up by
  // sensors.begin() (EnvironmentSensorManager), which runs before this. On
  // boards that set CARDKB_I2C=Wire directly in platformio.ini, that bus is
  // brought up by the board's own begin() instead -- also before this.
  // Either way, just probe for a CardKB sitting on it.
  CARDKB_I2C.beginTransmission(0x5F);
  _has_cardkb = (CARDKB_I2C.endTransmission() == 0);
#endif

#if defined(PIN_HALL_SENSOR)
  // Internal pull matches the default polarity: pulled up so an active-low
  // sensor reads HIGH at rest, pulled down so an active-high one reads LOW at
  // rest. Most reed/Hall breakouts are open-drain, active-low -- HALL_ACTIVE_HIGH
  // is only for modules wired the other way.
  pinMode(PIN_HALL_SENSOR, HALL_ACTIVE_HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
  _hall_magnet_present = HALL_ACTIVE_HIGH ? (digitalRead(PIN_HALL_SENSOR) == HIGH)
                                           : (digitalRead(PIN_HALL_SENSOR) == LOW);

  // Handle booting with the cover already closed
  if (_hall_magnet_present) {
    _locked = true;
    syncLockToHome();
  }
#endif

  // Lock device on boot if password is enabled to prevent bypassing it by resetting device
  if (passwordLockEnabled()) {
    _locked = true;
    // Add BOOT_SCREEN_MILLIS to make sure splash screen still shows
    _lock_wake_until = millis() + BOOT_SCREEN_MILLIS + 5000;
  }

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
  soundctl::applyMode(_node_prefs, buzzer, false, rtc_clock.getCurrentTime());
  buzzer.setVolume(_node_prefs->buzzer_volume);
  buzzer.begin();
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  msgtext::seedQuick(_node_prefs);   // "OK" in quick message 1 on first boot

  ui_started_at = millis();
  _alert_expiry = 0;
  _batt_mv = UITaskBase::getBattMilliVolts();  // seed EMA with first reading

  _core = new UiCore();   // also loads the persisted waypoints   // before any screen -- MessagesScreen binds to its history
  _core->begin(node_prefs, sensors, this);
  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  syncLockToHome();   // booted locked (e.g. cover closed) → home starts on the LOCK page
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
  trail_screen       = new TrailScreen(this, &_core->trail.store());
  compass_screen     = new CompassScreen(this);
#if ENV_INCLUDE_GPS == 1 && defined(GPS_SKYVIEW)
  satellites_screen  = new SatellitesScreen(this);
#endif
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
  // Locked above (a PIN, or the cover closed) before Home existed to be told.
  if (_locked) syncLockToHome();
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
void UITask::gotoSatellitesScreen() { if (satellites_screen) setCurrScreen(satellites_screen); }
void UITask::gotoDiagnosticsScreen() { setCurrScreen(diag_screen); }
void UITask::gotoRepeaterScreen()  { setCurrScreen(repeater_screen); }
void UITask::gotoClockTools()      { setCurrScreen(clock_tools); }
void UITask::gotoGpioScreen() {
#if defined(PIN_GPIO1)
  setCurrScreen(gpio_screen);
#endif
}
void UITask::gotoLiveShareScreen() { setCurrScreen(live_share_screen); }

// ── Clock tools (engine in ui-core/ClockEngine.h) ───────────────────────────
// The melody (soundctl::MEL_ALARM) overrides mute (playMelody →
// buzzer.playForced); the ring auto-stops after ClockEngine::RING_MS if no key
// dismisses it (see loop()).

void UITask::wakeForAlarm() {
  if (_display != NULL) _display->turnOn();
  // Locked: the lock-screen blanking check (loop()) turns the display straight
  // back off once _lock_wake_until is in the past — which it always is by the
  // time an alarm fires. Hold the wake window open for the whole ring so the
  // lock screen (and its alert overlay) stays visible while ringing.
  if (_locked) _lock_wake_until = millis() + ClockEngine::RING_MS;
  _next_refresh = 0;   // draw the alert overlay immediately
}

void UITask::onAlarmChanged()                 { _core->clock.onAlarmChanged(); }
void UITask::startTimer(uint32_t duration_ms) { _core->clock.startTimer(duration_ms); }
void UITask::stopTimer()                      { _core->clock.stopTimer(); }
bool UITask::isTimerRunning() const           { return _core->clock.isTimerRunning(); }
uint32_t UITask::timerRemainingMs() const     { return _core->clock.timerRemainingMs(); }
bool UITask::isRinging() const                { return _core->clock.isRinging(); }
void UITask::dismissRing()                    { stopMelody(); _core->clock.dismissRing(); clearAlert(); }

void UITask::tickCore() {
  _core->loop();
  drainCoreEvents();
  // Repeat the ring melody until dismissed or the ring window elapses.
  if (_core->clock.isRinging() && !isMelodyPlaying()) playMelody(soundctl::MEL_ALARM);
}

void UITask::drainCoreEvents() {
  UiEvent ev;
  while (_core->events.pop(ev)) {
    switch (ev.type) {
    case UiEventType::ClockAlert:
      wakeForAlarm();
      showAlert(ev.text, ClockEngine::RING_MS);
      playMelody(soundctl::MEL_ALARM);
      break;
    case UiEventType::ClockRingEnded:
      stopMelody();
      clearAlert();
      break;
    case UiEventType::LiveShareEnded:
      showAlert("Live share ended", 2500);
      break;
    case UiEventType::LocatorCrossed:
      showAlert(ev.text, 3000);
      if (!isBuzzerQuiet())
        playMelody(ev.flag ? soundctl::MEL_ARRIVE : soundctl::MEL_LEAVE);
      break;
    case UiEventType::LocatorBeep:
      playMelody(soundctl::MEL_TICK);
      break;
    case UiEventType::MessageArrived:
      onMessageArrived(ev);
      break;
    case UiEventType::AdvertHeard:
      notify(ev.flag ? UIEventType::advertReceivedFlood : UIEventType::advertReceivedZeroHop);
      break;
    default:
      break;
    }
  }
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

bool UITask::startPing(const uint8_t* pub_key) {
  PingEngine::StartResult r = _core->ping.start(pub_key);
  if (r == PingEngine::UNSUPPORTED) showAlert("Ping not supported with 3-byte path hashes", 3000);
  return r == PingEngine::STARTED;
}
bool UITask::isPingActive() const { return _core->ping.isActive(); }
void UITask::getPingResult(int16_t& snr_out_x4, int16_t& snr_back_x4, uint32_t& rtt_ms) const {
  _core->ping.getResult(snr_out_x4, snr_back_x4, rtt_ms);
}
void UITask::clearPing() { _core->ping.clear(); }

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

void UITask::openChannelHistory(uint8_t channel_idx) {
  ((MessagesScreen*)messages_screen)->reset();
  ((MessagesScreen*)messages_screen)->enterChannel(channel_idx);
  setCurrScreen(messages_screen);
}

void UITask::openRoomServer(const ContactInfo& ci) {
  ((MessagesScreen*)messages_screen)->reset();
  ((MessagesScreen*)messages_screen)->enterRoom(ci);
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

void UITask::pickFavouriteTarget(int slot) {
  ((MessagesScreen*)messages_screen)->startPickFavourite(slot);
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

// ── UI Core wiring ──────────────────────────────────────────────────────────
MyMesh::Listener* UITask::meshListener() { return _core; }

bool UITask::isViewingChannel(uint8_t channel_idx) {
  return ((MessagesScreen*)messages_screen)->isViewingChannel(channel_idx);
}
bool UITask::isViewingDM(const uint8_t* pub_key) {
  return ((MessagesScreen*)messages_screen)->isViewingDM(pub_key);
}
void UITask::onViewedHistoryGrew(bool channel) {
  ((MessagesScreen*)messages_screen)->onViewedHistoryGrew(channel);
}

int  UITask::getMsgCount() const        { return _core->msgCount(); }
int  UITask::getRoomUnreadCount() const { return _core->roomUnread(); }
void UITask::clearRoomUnread()          { _core->clearRoomUnread(); }

int UITask::getChannelUnreadCount() const {
  return ((MessagesScreen*)messages_screen)->getTotalChannelUnread();
}

uint8_t UITask::getChannelUnread(uint8_t channel_idx) const {
  return ((MessagesScreen*)messages_screen)->chUnread(channel_idx);
}

bool UITask::getChannelUnreadOverflow(uint8_t channel_idx) const {
  return ((MessagesScreen*)messages_screen)->chUnreadOverflow(channel_idx);
}

bool UITask::getAnyChannelUnreadOverflow() const {
  return ((MessagesScreen*)messages_screen)->anyChannelUnreadOverflow();
}

bool UITask::getAnyUnreadOverflow() const {
  return getAnyChannelUnreadOverflow() || getAnyDMUnreadOverflow();
}

void UITask::onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {
  // Admin logins are answered to the Core's AdminSession; what reaches here is
  // MessagesScreen's room login.
  ((MessagesScreen*)messages_screen)->onRoomLoginResult(pub_key, success, permissions);
  // Unlike the keypress-driven showAlert() calls elsewhere, this fires from a
  // background mesh response with no keypress to schedule a redraw — without
  // forcing one, the alert's short expiry can lapse before the next scheduled
  // refresh ever draws it.
  _next_refresh = 0;
}

void UITask::onAdminStateChanged() {
  _next_refresh = 0;   // same reasoning as onRoomLoginResult above
}

int UITask::getDMUnreadTotal() const { return _core->dmUnreadTotal(); }
uint8_t UITask::getDMUnread(const uint8_t* pub_key) const { return _core->dmUnread(pub_key); }
bool UITask::getDMUnreadOverflow(const uint8_t* pub_key) const { return _core->dmUnreadOverflow(pub_key); }
bool UITask::getAnyDMUnreadOverflow() const { return _core->anyDMUnreadOverflow(); }
void UITask::clearDMUnread(const uint8_t* pub_key) { _core->clearDMUnread(pub_key); }
void UITask::clearAllDMUnread() { _core->clearAllDMUnread(); }

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


// Incoming message (UiEventType::MessageArrived, filed by the Core already):
// alert overlay, wake the display, and the per-contact / per-channel sound.
void UITask::onMessageArrived(const UiEvent& ev) {
  if (ev.kind == UIEventType::contactMessage && ev.flag) {
    memcpy(_last_notif_dm_prefix, ev.key, 4);
    _last_notif_dm_valid = true;
  }
  if (ev.kind == UIEventType::channelMessage) _last_notif_ch_idx = ev.idx;

  // No toast when the message lands in the thread that is open on screen:
  // it already shows up there, and the box would only cover it.
  bool in_view = curr == messages_screen && _display && _display->isOn() && !_locked &&
                 ((ev.kind == UIEventType::contactMessage && ev.flag && isViewingDM(ev.key)) ||
                  (ev.kind == UIEventType::channelMessage && ev.idx >= 0 && isViewingChannel((uint8_t)ev.idx)));
  if (!in_view) {
    char alert_buf[80];
    snprintf(alert_buf, sizeof(alert_buf), "Msg: %.20s", ev.text);
    showAlert(alert_buf, 3000);
  }

  if (_display != NULL && !_locked) {
    bool wake_disabled = _node_prefs && (_node_prefs->msg_wake_screen_off || inQuietHours(*_node_prefs, rtc_clock.getCurrentTime()));
    if (!wake_disabled && !_display->isOn() && !isClientConnected()) {   // wake for the msg unless an app (BLE/USB) is already showing it, or the user disabled msg-wake
      _display->turnOn();
    }
    if (_display->isOn()) {
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
      _next_refresh = 100;
    }
  }
  notify(ev.kind);
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  unsigned long cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_core->msgCount() > 0) {
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
  _display->drawPanel(box_x, box_y, box_w, box_h);
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

void UITask::syncLockToHome() {
  // Lock/unlock switches the home screen's page to LOCK (or back)
  if (home) static_cast<HomeScreen*>(home)->setLocked(_locked);
}

bool UITask::passwordLockEnabled() const {
  return _node_prefs && screenlock::isSet(*_node_prefs);
}

void UITask::setNodeLockPassword(const char* plain) {
  if (_node_prefs) screenlock::set(*_node_prefs, plain, the_mesh.getRNG());
}

void UITask::beginUnlockPrompt() {
  _unlock_kb = true; // Track that keyboard is visible and is waiting for input
  _kb.beginPin("", KeyboardWidget::PIN_MAX_LEN, true, "PIN"); // masked
  _kb.clearPlaceholders();
  _lock_wake_until = millis() + 5000; // keep the display on while typing
  _next_refresh = 0;
}

void UITask::cancelUnlockPrompt() {
  _unlock_kb = false;
  _kb.pin_mode = false;
  _kb.buf[0] = '\0'; // clear input
  _kb.len = 0;
  _kb.cursor_pos = 0;
  _next_refresh = 100;
}

void UITask::handleUnlockKey(char c) {
  auto res = _kb.handleInput(c);
  if (res == KeyboardWidget::DONE && _pin_tries.blocked()) {
    char msg[32];
    snprintf(msg, sizeof(msg), "Wait %lu s", (unsigned long)_pin_tries.secondsLeft());
    showAlert(msg, 1200);
  } else if (res == KeyboardWidget::DONE) { // Process input on submit
    if (_node_prefs && screenlock::check(*_node_prefs, _kb.buf)) {
      // Match: Unlock
      _pin_tries.ok();
      _unlock_kb = false;
      _kb.pin_mode = false;
      _locked = false;
      if (_display && !_display->isOn()) _display->turnOn();
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
      syncLockToHome();
    } else {
      // Invalid: Clear input and reset keyboard; TRIES misses pause the entry
      char msg[32];
      if (_pin_tries.miss()) snprintf(msg, sizeof(msg), "Wait %lu s", (unsigned long)(screenlock::PAUSE_MS / 1000));
      else snprintf(msg, sizeof(msg), "Wrong PIN, %u left", (unsigned)_pin_tries.left());
      showAlert(msg, 1200);
      _kb.len = 0;
      _kb.buf[0] = '\0';
      _kb.cursor_pos = 0;
      _kb.page = _kb.row = _kb.col = 0;
      _kb.caps = _kb.caps_lock = false;
      _next_refresh = 0;
    }
  } else if (res == KeyboardWidget::CANCELLED) {
    cancelUnlockPrompt();
  }
  _lock_wake_until = millis() + 5000; // keep the display on while typing
}

void UITask::toggleLock() {
  if (_unlock_kb) {
    cancelUnlockPrompt();
    return;
  }
  if (_locked) {
    if (passwordLockEnabled()) { // Prompt for password
      beginUnlockPrompt();
    } else {                     // ...or unlock instantly
      _locked = false;
      if (_display && !_display->isOn()) _display->turnOn();
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
    }
  } else { // Device is currently unlocked -> lock it
    _locked = true;
    _lock_wake_until = millis() + 2000; // Briefly show lockscreen before blanking
  }
  syncLockToHome();
  _next_refresh = 0;
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
  // Every screen that edits NodePrefs (Settings, Bot, Trail, Locator, GPS
  // sharing, etc.) only persists on its OWN "Cancel"/exit path (see each
  // screen's own savePrefsIfDirty(_dirty) call) -- there was previously no
  // flush here at all. A user who edits a setting and then triggers a
  // reboot/power-off WITHOUT first backing out of that screen (e.g. the
  // display auto-offs while still inside Settings, then the device is
  // later hard-reset or its battery pulled; or a low-battery auto-shutdown
  // fires mid-edit) silently lost that change on the next boot -- this was
  // the actual mechanism behind reports of "settings don't survive a
  // reboot." Unconditional and cheap: an unchanged NodePrefs still writes
  // identical bytes, same as this codebase's many other direct
  // the_mesh.savePrefs() call sites already do without a dirty check.
  the_mesh.savePrefs();
  the_mesh.saveRTCTime();
  the_mesh.flushDirtyContacts();

  // Auto-save the live GPS trail if enabled (covers low-battery auto-shutdown).
  _core->trail.onShutdown();

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
#ifdef SIM_PLATFORM
  // The sim runs single-threaded on the browser's main JS thread (no real
  // hardware to actually shut down) -- up to 2.5s of a real, synchronous
  // busy-wait here blocks that thread and freezes the whole page for the
  // duration, same class of issue as the low-battery pre-shutdown pause
  // skipped below. Skip the wait entirely in the sim.
#else
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - buzzer_timer) < 2500)
    buzzer.loop();
#endif

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

// A dashboard field in this display's font (ui-core/Telemetry.h); `nouns`
// when it stands without its label (the lock screen). "" for none.
static void formatDashVal(uint8_t field, char* val, int val_len, uint16_t batt_mv, uint16_t low_batt_mv,
                          int unread, bool unread_overflow, bool imperial, CayenneLPP* lpp, bool nouns) {
  val[0] = '\0';
  if (field == telemetry::NONE || field >= telemetry::COUNT) return;
  telemetry::Style st = L1_TELEMETRY;
  st.nouns = nouns;
  if (nouns) st.gps_dp = 2;   // no label: room for the position is shorter
  telemetry::Inputs in;
  in.batt_mv = batt_mv;
  in.low_batt_mv = low_batt_mv;
  in.nodes = the_mesh.getNumContacts();
  in.unread = unread;
  in.unread_more = unread_overflow;
  in.imperial = imperial;
#if ENV_INCLUDE_GPS == 1
  in.loc = sensors.getLocationProvider();
#endif
  if (telemetry::isSensor(field)) {
    if (!lpp) { static CayenneLPP s_lpp(200); s_lpp.reset(); sensors.querySensors(0xFF, s_lpp); lpp = &s_lpp; }
    in.lpp = lpp->getBuffer();
    in.lpp_len = lpp->getSize();
  }
  telemetry::text(field, in, st, val, val_len);
}

void UITask::enqueueKey(char c) {
  if (c == 0) return;
  uint8_t next = (_kq_head + 1) % KEY_QUEUE_SIZE;
  if (next == _kq_tail) return;  // full: drop newest rather than clobber unprocessed keys
  _key_queue[_kq_head] = c;
  _kq_head = next;
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
void UITask::injectSimKey(char c) {
  enqueueKey(checkDisplayOn(c));
}

void UITask::injectSimKeyLongPress(char c) {
  enqueueKey(handleLongPress(c));
}

// Called directly from a host HTML page's JS (button onclick / keydown
// listener) -- e.g. `Module._sim_enqueue_key(keyCode)` -- to drive the real
// on-device menu. `c` is one of the KEY_* codes in src/helpers/ui/
// UIScreen.h (KEY_UP/DOWN/LEFT/RIGHT/ENTER/CANCEL/NEXT/PREV/SELECT), the
// exact same values the native build's stdin-poll branch above already
// enqueues -- so the host page owns key-mapping (arrow keys, on-screen
// D-pad buttons, whatever), not this function.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_enqueue_key(char c) {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->injectSimKey(c);
}

// Long-press counterpart -- the host page's own press-and-hold timer (see
// web/index.html/mesh.html) calls this instead of sim_enqueue_key() once a
// button/key has been held past the same ~1000ms threshold every real
// board's MomentaryButton uses. Real hardware never gets both a short-press
// AND a long-press event for the same physical press (MomentaryButton fires
// one or the other), so the host page's timer must do the same: fire this
// on hold-past-threshold and suppress the plain click that would otherwise
// follow on release.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_enqueue_key_longpress(char c) {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->injectSimKeyLongPress(c);
}

#ifdef PIN_BUZZER
// Polled by the host page every ~20ms (see web/index.html/mesh.html) to
// drive a Web Audio oscillator standing in for the real piezo buzzer --
// genericBuzzer's own #ifdef SIM_PLATFORM branch (src/helpers/ui/buzzer.cpp)
// tracks (is a note sounding, at what frequency) instead of touching real
// PWM/timer hardware; these two exports are just the read side of that.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_is_playing() {
  return (g_sim_ui_task_for_js && g_sim_ui_task_for_js->isBuzzerPlaying()) ? 1 : 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_freq_hz() {
  return g_sim_ui_task_for_js ? (int)g_sim_ui_task_for_js->buzzerFreqHz() : 0;
}
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_get_volume() {
  return g_sim_ui_task_for_js ? (int)g_sim_ui_task_for_js->buzzerVolume() : 0;
}

// Write side: a host page's own mute control (a button next to the d-pad,
// say) calling this fires UITask::toggleBuzzer() -- the literal function
// the real on-device Settings > Buzzer mute toggle calls, not a
// re-implementation of it. That single call already does everything the
// real toggle does: buzzer.quiet(), writes NodePrefs.buzzer_quiet, clears
// buzzer_auto (manual mute always wins over auto-mute-on-BT-connect,
// same as pressing it on the device would), the_mesh.savePrefs(), and
// the real on-screen "Buzzer: ON/OFF" alert -- so muting from the host
// page's button is visibly the same event as muting from the keypad.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_buzzer_toggle_quiet() {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->toggleBuzzer();
}
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_get_quiet() {
  return (g_sim_ui_task_for_js && g_sim_ui_task_for_js->isBuzzerQuiet()) ? 1 : 0;
}
#endif
#endif

bool UITask::dequeueKey(char& c) {
  if (_kq_tail == _kq_head) return false;
  c = _key_queue[_kq_tail];
  _kq_tail = (_kq_tail + 1) % KEY_QUEUE_SIZE;
  return true;
}

#if defined(CARDKB_I2C)
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

// Poll an optional CardKB (I2C keyboard, addr 0x5F) on CARDKB_I2C, feeding
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
#if defined(CARDKB_I2C)
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
  CARDKB_I2C.requestFrom(0x5F, 1);
  if (!CARDKB_I2C.available()) return;
  uint8_t raw = CARDKB_I2C.read();
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
    toggleLock();
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
//
// Debounced against a mechanical reed switch chattering for a few ms as the
// magnet crosses the trigger distance -- a raw flip only becomes the new
// _hall_magnet_present once it's been steady for HALL_DEBOUNCE_MS, so a bounce
// can't fire the lock/unlock actions (each including a full display
// off/on -- slow and disruptive on e-ink) more than once per real transition.
void UITask::pollHallSensor() {
#if defined(PIN_HALL_SENSOR)
  bool raw = HALL_ACTIVE_HIGH ? (digitalRead(PIN_HALL_SENSOR) == HIGH)
                               : (digitalRead(PIN_HALL_SENSOR) == LOW);
  if (raw != _hall_candidate) {
    _hall_candidate = raw;
    _hall_candidate_since = millis();
  }
  if (_hall_candidate == _hall_magnet_present) return;   // no debounced change yet
  if (millis() - _hall_candidate_since < HALL_DEBOUNCE_MS) return;   // not steady long enough

  bool present = _hall_candidate;
  _hall_magnet_present = present;

  if (present) {   // cover closed
    cancelUnlockPrompt();   // a cover can't be over the password prompt
    _locked = true;
    syncLockToHome();
    _lock_wake_until = 0;
    if (_display) _display->turnOff();
#ifdef PIN_LED
    digitalWrite(PIN_LED, LOW);   // same as the auto-off path -- one less thing lit under a closed cover
#endif
  } else {   // cover opened
    if (passwordLockEnabled()) { // Redirect flow to usual unlock prompt when password is enabled
      _locked = true;
      if (_display) _display->turnOn();
      beginUnlockPrompt();
    } else {
      _locked = false;
      syncLockToHome();
      if (_display && !_display->isOn()) _display->turnOn();
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
    }
  }
  _next_refresh = 0;
#endif
}

#if defined(UI_PERF_L1) && defined(NRF52_PLATFORM)
// -D UI_PERF_L1: walks the main screens by itself (keys fed into the queue) and
// prints, per step over USB serial: frames drawn, frames the display skipped
// as unchanged, render/flush time (avg/max us), heap (used/free/largest
// block) and every FreeRTOS task's stack headroom. Measurement only.
#include "FreeRTOS.h"
#include "task.h"
#include <malloc.h>
extern unsigned char __HeapBase[];
extern unsigned char __HeapLimit[];
extern uint32_t g_disp_flushes;
namespace perfl1 {
  // Each step opens a screen directly (so the walk doesn't depend on which
  // Home pages are enabled), then types keys: R/L/U/D/E/X, H = hold Enter.
  enum Open : uint8_t { NONE, HOME, SETTINGS, MESSAGES, TOOLS, NEARBY, TRAIL, MAP, COMPASS, SATS,
                        DIAG, LIVE, LOCATOR, ADVERT, REPEATER, BOT, CLOCKTOOLS, CHANNEL0 };
  struct Step { const char* name; Open open; const char* keys; uint16_t dwell_ms; };
  static const Step STEPS[] = {
    {"boot-idle", NONE, "", 3000},
    {"home", HOME, "", 2000},
    {"home-R1", NONE, "R", 1500}, {"home-R2", NONE, "R", 1500}, {"home-R3", NONE, "R", 1500},
    {"home-R4", NONE, "R", 1500}, {"home-R5", NONE, "R", 1500}, {"home-R6", NONE, "R", 1500},
    {"home-R7", NONE, "R", 1500}, {"home-R8", NONE, "R", 1500},
    {"settings", SETTINGS, "", 2000}, {"set-display", NONE, "E", 2000},
    {"set-scroll", NONE, "DDDDDDDDDDDD", 2000},
    {"messages", MESSAGES, "", 2000}, {"dm-list", NONE, "E", 2000},
    {"dm-scroll", NONE, "DDDDDDDDDDDDDDDDDDDD", 2000},
    {"dm-thread", NONE, "E", 3000}, {"dm-thread-up", NONE, "UUUU", 2000},
    {"channel", CHANNEL0, "", 3000}, {"channel-up", NONE, "UUUUUU", 2000},
    {"compose", NONE, "EE", 2000}, {"kbd-type", NONE, "RRDRRDRRE", 2000},
    {"kbd-close", NONE, "XXX", 1500},
    {"nearby", NEARBY, "", 3000}, {"nearby-scroll", NONE, "DDDDDDDDDDDDDDDDDDDD", 2000},
    {"tools", TOOLS, "", 1500}, {"trail", TRAIL, "", 2000}, {"map", MAP, "", 3000},
    {"compass", COMPASS, "", 3000}, {"sats", SATS, "", 4000}, {"sats-signal", NONE, "R", 3000},
    {"diag", DIAG, "", 4000}, {"diag-scroll", NONE, "DDDD", 3000},
    {"liveshare", LIVE, "", 1500}, {"locator", LOCATOR, "", 1500}, {"autoadvert", ADVERT, "", 1500},
    {"repeater", REPEATER, "", 1500}, {"bot", BOT, "", 1500}, {"clocktools", CLOCKTOOLS, "", 1500},
    {"home-end", HOME, "", 3000},
    {"done", NONE, "", 500},
  };
  static int step = -1;
  static uint32_t step_at = 0, frames = 0, r_sum = 0, r_max = 0, f_sum = 0, f_max = 0, fl0 = 0;
  static uint32_t heap_min_free = 0xFFFFFFFF;

  static uint32_t largestBlock() {   // bisect the biggest malloc that succeeds
    uint32_t lo = 0, hi = (uint32_t)(__HeapLimit - __HeapBase);
    while (hi - lo > 64) {
      uint32_t mid = (lo + hi) / 2;
      void* q = malloc(mid);
      if (q) { free(q); lo = mid; } else hi = mid;
    }
    return lo;
  }
  static void heap(uint32_t& used, uint32_t& freeb) {
    uint32_t total = (uint32_t)(__HeapLimit - __HeapBase);
    used = (uint32_t)mallinfo().uordblks;
    freeb = used < total ? total - used : 0;
  }
  void frame(uint32_t render_us, uint32_t flush_us) {
    frames++; r_sum += render_us; f_sum += flush_us;
    if (render_us > r_max) r_max = render_us;
    if (flush_us > f_max) f_max = flush_us;
  }
  static void report(const char* name) {
    uint32_t used, freeb; heap(used, freeb);
    if (freeb < heap_min_free) heap_min_free = freeb;
    uint32_t fl = g_disp_flushes - fl0;
    Serial.printf("PERF %-12s frames %3lu sent %3lu render avg %5lu max %6lu  flush avg %5lu max %6lu us | heap used %lu free %lu largest %lu min %lu\n",
      name, frames, fl, frames ? r_sum / frames : 0, r_max, frames ? f_sum / frames : 0, f_max,
      used, freeb, largestBlock(), heap_min_free);
  }
  static void tasks() {
    TaskStatus_t st[16];
    UBaseType_t n = uxTaskGetSystemState(st, 16, NULL);
    for (UBaseType_t i = 0; i < n; i++)
      Serial.printf("TASK %-10s stack free %5lu B\n", st[i].pcTaskName, (unsigned long)st[i].usStackHighWaterMark * sizeof(StackType_t));
  }
  static DisplayDriver* disp = nullptr;
  static void shot(const char* name) {   // the 1 KB page buffer as hex, for a PNG on the host
    if (!disp || !disp->getBuffer()) return;
    const uint8_t* b = disp->getBuffer();
    Serial.printf("SHOT %s ", name);
    for (uint16_t i = 0; i < disp->getBufferSize(); i++) Serial.printf("%02x", b[i]);
    Serial.println();
  }
  static char keyOf(char c) {
    switch (c) { case 'R': return KEY_RIGHT; case 'L': return KEY_LEFT; case 'U': return KEY_UP;
      case 'D': return KEY_DOWN; case 'E': return KEY_ENTER; case 'X': return KEY_CANCEL; case 'H': return KEY_CONTEXT_MENU; }
    return 0;
  }
  static const char* pending = nullptr;
  static uint32_t next_key_at = 0;
  // Returns a key to enqueue (0 = none).
  Open open_now = NONE;
  char tick() {
    uint32_t now = millis();
    if (step >= (int)(sizeof(STEPS) / sizeof(STEPS[0]))) return 0;
    if (pending && *pending) {
      if ((int32_t)(now - next_key_at) < 0) return 0;
      next_key_at = now + 300;
      char k = keyOf(*pending++);
      if (!*pending) { step_at = now; frames = r_sum = r_max = f_sum = f_max = 0; fl0 = g_disp_flushes; }
      return k;
    }
    if (step >= 0 && (int32_t)(now - step_at) < (int32_t)STEPS[step].dwell_ms) return 0;
    if (step >= 0) { report(STEPS[step].name); shot(STEPS[step].name); }
    step++;
    if (step >= (int)(sizeof(STEPS) / sizeof(STEPS[0]))) { tasks(); Serial.println("PERF end"); return 0; }
    if (step == 0) tasks();
    pending = STEPS[step].keys;
    open_now = STEPS[step].open;
    step_at = now; frames = r_sum = r_max = f_sum = f_max = 0; fl0 = g_disp_flushes;
    next_key_at = now;
    return 0;
  }
}
// micros() ticks in ~1 ms steps here (RTC-based), so time with the CPU cycle counter.
static inline uint32_t perfUs() {
  static bool on = false;
  if (!on) { CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; on = true; }
  return DWT->CYCCNT;   // cycles; differences stay right across the 67 s wrap
}
#define PERF_T0() uint32_t _pf0 = perfUs()
#define PERF_T1() uint32_t _pf1 = perfUs()
#define PERF_T2() perfl1::frame((_pf1 - _pf0) / (SystemCoreClock / 1000000), (perfUs() - _pf1) / (SystemCoreClock / 1000000))
#else
#define PERF_T0()
#define PERF_T1()
#define PERF_T2()
#endif

void UITask::loop() {
#if defined(UI_PERF_L1) && defined(NRF52_PLATFORM)
  { static uint32_t start = millis();
    perfl1::disp = _display;
    { static bool sz = false;
      if (!sz && millis() - start > 7000) { sz = true;
        Serial.printf("SIZE UiCore %u Splash %u Home %u Settings %u Messages %u Tools %u Ringtone %u Bot %u Admin %u Nearby %u Dash %u AutoAdv %u LiveSh %u Locator %u Trail %u Compass %u Sats %u Diag %u Repeater %u ClockTools %u Kbd %u UITask %u StaticPool32 %u Packet %u\n",
          sizeof(UiCore), sizeof(SplashScreen), sizeof(HomeScreen), sizeof(SettingsScreen), sizeof(MessagesScreen), sizeof(ToolsScreen),
          sizeof(RingtoneEditorScreen), sizeof(BotScreen), sizeof(AdminScreen), sizeof(NearbyScreen), sizeof(DashboardConfigScreen),
          sizeof(AutoAdvertScreen), sizeof(LiveShareScreen), sizeof(LocatorScreen), sizeof(TrailScreen), sizeof(CompassScreen),
#if ENV_INCLUDE_GPS == 1 && defined(GPS_SKYVIEW)
          sizeof(SatellitesScreen),
#else
          0u,
#endif
          sizeof(DiagnosticsScreen), sizeof(RepeaterScreen), sizeof(ClockToolsScreen), sizeof(KeyboardWidget), sizeof(UITask),
          sizeof(StaticPoolPacketManager), sizeof(mesh::Packet));
        TaskStatus_t st[16]; uint32_t tot = 0;
        UBaseType_t n = uxTaskGetSystemState(st, 16, NULL);
        (void)n; (void)tot;
      } }
    if (millis() - start > 8000) {
      char k = perfl1::tick();
      if (k) enqueueKey(checkDisplayOn(k));
      if (perfl1::open_now != perfl1::NONE) {
        checkDisplayOn(0);
        switch (perfl1::open_now) {
          case perfl1::HOME: gotoHomeScreen(); break;
          case perfl1::SETTINGS: gotoSettingsScreen(); break;
          case perfl1::MESSAGES: gotoMessagesScreen(); break;
          case perfl1::TOOLS: gotoToolsScreen(); break;
          case perfl1::NEARBY: gotoNearbyScreen(); break;
          case perfl1::TRAIL: gotoTrailScreen(); break;
          case perfl1::MAP: gotoMapScreen(); break;
          case perfl1::COMPASS: gotoCompassScreen(); break;
          case perfl1::SATS: gotoSatellitesScreen(); break;
          case perfl1::DIAG: gotoDiagnosticsScreen(); break;
          case perfl1::LIVE: gotoLiveShareScreen(); break;
          case perfl1::LOCATOR: gotoLocatorScreen(); break;
          case perfl1::ADVERT: gotoAutoAdvertScreen(); break;
          case perfl1::REPEATER: gotoRepeaterScreen(); break;
          case perfl1::BOT: gotoBotScreen(); break;
          case perfl1::CLOCKTOOLS: gotoClockTools(); break;
          case perfl1::CHANNEL0: openChannelHistory(0); break;
          default: break;
        }
        perfl1::open_now = perfl1::NONE;
        _next_refresh = 0;
      }
    } }
#endif
  pollConnection();   // BLE link state -> hasConnection() (see UITaskBase)
  drainCoreEvents();  // react to what the Core filed during mesh processing (alerts, wake, sounds)
#if UI_HAS_JOYSTICK
  uint8_t joy_rot = _node_prefs ? _node_prefs->joystick_rotation : JOYSTICK_ROTATION;
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (back_btn.isPressed() && !_unlock_kb) {
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
        toggleLock();
      }
      // eat the Enter — don't pass to curr
    } else {
      // While the password keyboard is open, Back+Enter is just typing
      if (_unlock_kb) {
        _lock_seq_count = 0;
        _lock_seq_ms = 0;
      }
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
#elif defined(SIM_PLATFORM) && !defined(__EMSCRIPTEN__)
  // Native terminal input ONLY -- this branch previously had no
  // __EMSCRIPTEN__ exclusion, so it also compiled into the wasm build
  // (SIM_PLATFORM is defined there too, and UI_HAS_JOYSTICK/PIN_USER_BTN
  // are both unset for variants/sim). Every tick it called real select()/
  // read() on fd 0; under Emscripten, with no stdin ever wired up, that
  // hits the runtime's default TTY device, which falls back to a real,
  // blocking window.prompt("Input: ") -- so every single browser tab
  // running the wasm build was popping a native dialog on nearly every
  // frame, discovered by seeing Playwright's page 'dialog' event fire
  // continuously from the moment the module boots. The wasm build's own
  // input already comes through sim_enqueue_key()/injectSimKey() (see
  // above, in the #if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  // block) -- this stdin-poll branch was only ever meant for Phase 1's
  // native terminal target.
  //
  // stdin is put into raw/non-canonical mode by
  // variants/sim/sim_main.cpp's main(), so keys arrive here one at a time
  // with no Enter-to-submit line buffering. Non-blocking select() on fd 0
  // (VMIN=0/VTIME=0 on the fd itself would also work, but select() keeps
  // the intent -- "is there a key waiting?" -- explicit) takes the place of
  // every concrete MomentaryButton/GPIO poll above. Every real board maps
  // its own physical buttons down to the same enqueueKey() choke point;
  // this is the sim's one input source instead.
  //   Arrow keys   -> KEY_UP/DOWN/LEFT/RIGHT
  //   Enter/Space  -> KEY_ENTER
  //   Esc/Backspace-> KEY_CANCEL
  //   w/a/s/d      -> up/left/down/right (arrow keys need a real terminal;
  //                   WASD works even through a dumb pipe/redirected stdin)
  //   n / p        -> KEY_NEXT / KEY_PREV
  {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    struct timeval tv = {0, 0};
    if (select(1, &fds, NULL, NULL, &tv) > 0) {
      uint8_t buf[16];
      int n = (int)read(0, buf, sizeof(buf));
      int i = 0;
      while (i < n) {
        uint8_t c = buf[i++];
        char key = 0;
        if (c == 0x1b && i + 1 < n && buf[i] == '[') {
          uint8_t code = buf[i + 1];
          i += 2;
          switch (code) {
            case 'A': key = KEY_UP;    break;
            case 'B': key = KEY_DOWN;  break;
            case 'C': key = KEY_RIGHT; break;
            case 'D': key = KEY_LEFT;  break;
            default:  key = 0;         break;
          }
        } else if (c == 0x1b) {
          key = KEY_CANCEL;
        } else if (c == '\r' || c == '\n' || c == ' ') {
          key = KEY_ENTER;
        } else if (c == 127 || c == 8) {
          key = KEY_CANCEL;
        } else if (c == 'w' || c == 'W') {
          key = KEY_UP;
        } else if (c == 's' || c == 'S') {
          key = KEY_DOWN;
        } else if (c == 'a' || c == 'A') {
          key = KEY_LEFT;
        } else if (c == 'd' || c == 'D') {
          key = KEY_RIGHT;
        } else if (c == 'n') {
          key = KEY_NEXT;
        } else if (c == 'p') {
          key = KEY_PREV;
        }
        if (key) enqueueKey(checkDisplayOn(key));
      }
    }
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
    if (_unlock_kb) {
      // Lock-screen password keyboard: Consume every key press
      char k;
      while (dequeueKey(k)) handleUnlockKey(k);
      _next_refresh = 100;  // redraw immediately after key press
    } else if (!_locked && curr) {
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
  if (soundctl::tick(_node_prefs, buzzer, isClientConnected(), rtc_clock.getCurrentTime()))   // BLE bonded or an open USB port
    _next_refresh = 0;
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();


  if (_display != NULL && _display->isOn()) {
    // Lock-screen password prompt
    if (_locked && _unlock_kb && (int32_t)(millis() - _lock_wake_until) >= 0) {
      cancelUnlockPrompt(); // Cancel unlock attempt on idle
      _next_refresh = 0;
    }
    if (_locked && !_unlock_kb && (int32_t)(millis() - _lock_wake_until) >= 0) {
      _display->turnOff();
    } else if (_locked && _unlock_kb && millis() >= _next_refresh) {
      // While the prompt is up the password keyboard replaces the lockscreen view
      PERF_T0();
      _display->startFrame();
      _kb.beginFrame();
      int delay_millis = _kb.render(*_display);
      if (millis() < _alert_expiry) renderAlertOverlay();   // "Wrong PIN", and a ringing alarm
      PERF_T1();
      _display->endFrame();
      PERF_T2();
      _next_refresh = millis() + delay_millis;
    } else if (_locked && millis() >= _next_refresh && home) {
      PERF_T0();
      _display->startFrame();
      if (curr && curr != home && (millis() - ui_started_at < BOOT_SCREEN_MILLIS)) {
        // Boot splash is still up on a boot-locked device
        _next_refresh = millis() + curr->render(*_display);
      } else {
        home->render(*_display);
        _next_refresh = millis() + Features::LOCKSCREEN_REFRESH_MS;
      }
      // Alert overlay on top — without this a ringing alarm on a locked device
      // played its melody against a screen that never said what was ringing.
      if (millis() < _alert_expiry) renderAlertOverlay();
      PERF_T1();
      _display->endFrame();
      PERF_T2();
    } else if (!_locked && millis() >= _next_refresh && curr) {
      PERF_T0();
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
      PERF_T1();
      _display->endFrame();
      PERF_T2();
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
        cancelUnlockPrompt();   // idle-lock isn't a password prompt
        _locked = true;
        _lock_wake_until = 0;
        syncLockToHome();
      }
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

  if ((int32_t)(millis() - next_batt_chck) >= 0) {
    uint16_t raw = UITaskBase::getBattMilliVolts();
    if (raw > 0) {
#ifdef SIM_PLATFORM
      // SimMainBoard::getBattMilliVolts() returns exactly whatever value the
      // host page's JS last set (see sim_battery_set_mv() in
      // variants/sim/SimMainBoard.h) -- a clean, instantaneous number, not a
      // noisy ADC reading. Real hardware needs the EMA below to smooth a
      // voltage divider's jitter under load; applying that same filter here
      // just makes a value typed into the demo UI visibly crawl toward its
      // target over several 8s samples, which reads as the whole sim being
      // laggy for no benefit the sim actually needs.
      _batt_mv = raw;
#else
      // EMA filter: alpha=0.2 (80% old, 20% new) — smooths ADC noise from uneven load
      _batt_mv = (_batt_mv == 0) ? raw : (uint16_t)((_batt_mv * 4u + raw) / 5u);
#endif
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
#ifdef SIM_PLATFORM
        // Skip the pre-shutdown UX pause in the sim.
#else
        if (_display->isEink() == false) { delay(2000); }
#endif
      }
      shutdown();
    }
#ifdef SIM_PLATFORM
    // A real ADC read has a real cost, worth spacing out 8s apart; the sim's
    // "read" is just returning a JS-set integer, so there's no reason to sit
    // on a stale value for up to 8s after Set was clicked. 250ms keeps this
    // a poll (not a push wired to the input's own event, which would need
    // its own plumbing) while feeling immediate.
    next_batt_chck = millis() + 250;
#else
    next_batt_chck = millis() + 8000;
#endif
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
        _core->trail.isRecording()
        || (_node_prefs && _node_prefs->loc_share_enabled)
        || (_node_prefs && _node_prefs->locator_enabled && _node_prefs->locator_has_target)
        || curr == compass_screen
        || (satellites_screen && curr == satellites_screen)
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

  // UI Core engines (alarm + countdown, COG, live share, locator, trail, …) run
  // regardless of the current screen / display state. Last in the loop, after
  // the GPS keep-awake check above (a fresh GPS wake re-seeds the locator first).
  tickCore();
}

// Locator (engine in ui-core/LocatorEngine.h).
void UITask::resetLocator() { _core->locator.reset(); }
bool UITask::resolvePersonPos(const uint8_t* key, int32_t& lat, int32_t& lon,
                              bool* live, uint32_t* ts) const {
  return _core->locator.resolvePersonPos(key, lat, lon, live, ts);
}
bool UITask::activeTargetPos(int32_t& lat, int32_t& lon) const { return _core->locator.activeTargetPos(lat, lon); }
void UITask::setTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  _core->locator.setTarget(kind, key, lat, lon, name);
}

void UITask::setTargetNow(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name) {
  if (!_node_prefs) return;
  setTarget(kind, key, lat, lon, name);
  the_mesh.savePrefs();
  showAlert("Target set", 1200);
}

void UITask::clearTarget() { _core->locator.clearTarget(); }
void UITask::clearTargetIfWaypoint(int32_t lat_1e6, int32_t lon_1e6) { _core->locator.clearTargetIfWaypoint(lat_1e6, lon_1e6); }

// Homing beeper: while armed with a target and inside the radius, emit a short
// tick whose interval shrinks linearly with distance — slow at the edge, rapid
// near the centre. Polls distance a few times a second; silent outside the
// radius. The beeper has its own toggle (locator_beeper), so turning it on is
// an explicit "I want to hear this" — it deliberately overrides the global
// buzzer mute (playMelody → buzzer.playForced ignores the quiet flag).
TrailStore& UITask::trail() { return _core->trail.store(); }

bool UITask::currentCourse(int& deg_out) const { return _core->course.currentCourse(deg_out); }
bool UITask::currentLocation(int32_t& lat, int32_t& lon) const { return _core->course.currentLocation(lat, lon); }

bool UITask::sendLocationShare(int32_t lat, int32_t lon) { return _core->live_share.send(lat, lon); }
void UITask::restartLocShareSession() { _core->live_share.restartSession(); }
void UITask::restartLocShareClock()   { _core->live_share.restartClock(); }
LiveTrackStore& UITask::liveTrack()   { return _core->live_share.track(); }

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

WaypointStore& UITask::waypoints() { return _core->waypoints.store(); }
void UITask::saveWaypoints() { _core->waypoints.save(); }

bool UITask::addWaypoint(int32_t lat, int32_t lon, uint32_t ts, const char* label) {
  if (_core->waypoints.full()) { showAlert("Waypoints full", 1000); return false; }
  if (_core->waypoints.add(lat, lon, ts, label)) {
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
  if (!_core->setGpsEnabled(on)) return;   // no GPS on this board
  notify(UIEventType::ack);
  showAlert(on ? "GPS: Enabled" : "GPS: Disabled", 800);
  _next_refresh = 0;
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
  radioctl::applyTxPower(_node_prefs);
}

void UITask::applyPowerSave() {
  rptctl::applyPowerSave(_node_prefs);   // forced off while repeating (ui-core/RepeaterControl.h)
}

void UITask::applyApc() {
  radioctl::applyApc();
}

void UITask::applyRadioParams() {
  if (_node_prefs == NULL) return;
  radioctl::applyParams();   // companion params, or the repeater profile if relaying with one set
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

void UITask::applySoundPrefs() {
#ifdef PIN_BUZZER
  if (_node_prefs) setBuzzerVolumeLevel(_node_prefs->buzzer_volume);   // with a sample at the new level
#endif
}

void UITask::setBuzzerVolumeLevel(uint8_t level) {
#ifdef PIN_BUZZER
  if (_node_prefs == NULL) return;
  soundctl::setVolume(_node_prefs, buzzer, level);
  _next_refresh = 0;
#endif
}

void UITask::toggleBuzzer() {
  #ifdef PIN_BUZZER
    if (!_node_prefs) return;
    bool on = buzzer.isQuiet();   // leaves Auto too
    soundctl::setMode(_node_prefs, buzzer, on ? soundctl::MODE_ON : soundctl::MODE_OFF, isClientConnected());
    if (on) notify(UIEventType::ack);
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
    _next_refresh = 0;
  #endif
}

int UITask::getBuzzerMode() {
#ifdef PIN_BUZZER
  return soundctl::mode(_node_prefs);
#else
  return 1;
#endif
}

void UITask::cycleBuzzerMode() {
#ifdef PIN_BUZZER
  if (!_node_prefs) return;
  int mode = getBuzzerMode();
  mode = (mode + 1) % soundctl::MODE_COUNT;  // ON → OFF → Auto → ON
  soundctl::setMode(_node_prefs, buzzer, (uint8_t)mode, isClientConnected());
  if (mode == soundctl::MODE_ON) notify(UIEventType::ack);
  static const char* labels[] = { "Buzzer: ON", "Buzzer: OFF", "Buzzer: Auto" };
  showAlert(labels[mode], 800);
  _next_refresh = 0;
#endif
}
