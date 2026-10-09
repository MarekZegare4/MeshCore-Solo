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

// The CardKB background poll task (see UITask::cardkbTask) shares its I2C bus
// with the environment sensors, and some screens query those sensors while
// rendering -- exactly when that task is active. Holding this lock around a
// sensor query makes the two take turns. No-op on builds without the task.
#if defined(CARDKB_I2C) && defined(NRF52_PLATFORM)
static void* g_cardkb_bus_mutex = nullptr;   // SemaphoreHandle_t, set by startCardKBCapture()
struct CardKBBusLock {
  CardKBBusLock()  { if (g_cardkb_bus_mutex) xSemaphoreTake((SemaphoreHandle_t)g_cardkb_bus_mutex, portMAX_DELAY); }
  ~CardKBBusLock() { if (g_cardkb_bus_mutex) xSemaphoreGive((SemaphoreHandle_t)g_cardkb_bus_mutex); }
};
#else
struct CardKBBusLock { };
#endif
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


#include "icons.h"
#include "../ui-core/Lettering.h"   // boot splash wordmark + lettering (shared with the L2)
#include "GfxUtils.h"   // gfx::drawLine — connects trail points on the Home map preview

// Boot splash, as on the L2 (ui-lvgl/Splash.h, same lettering from
// ui-core/Lettering.h): the MeshCore wordmark rising into place, "solo" in its
// lettering once it has landed, the Solo version, upstream version + build date, and three dots
// rising in turn while the device starts. E-ink gets the still final frame.
class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long _start, dismiss_after;
  char _solo_ver[24];
  char _line2[32];

  // A word in the wordmark's lettering at `sc` px a pixel, top-left at (x, y).
  static void drawLettering(DisplayDriver& d, const char* text, int x, int y, int gap, int sc) {
    for (const char* p = text; *p; p++) {
      char c = (char)tolower((unsigned char)*p);
      int cw = lettering::charW(c);
      if (cw < 0) continue;
      for (int r = 0; r < lettering::LOGO_H; r++)
        for (int col = 0; col < cw; ) {   // runs of inked pixels as one rect
          if (!lettering::inked(c, col, r)) { col++; continue; }
          int e = col;
          while (e + 1 < cw && lettering::inked(c, e + 1, r)) e++;
          d.fillRect(x + col * sc, y + r * sc, (e - col + 1) * sc, sc);
          col = e + 1;
        }
      x += cw * sc + gap;
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

    // The wordmark and "solo" grow in whole pixels with the panel (1x on 128 px,
    // 3x on the 4.2"), as far as the block still fits over the dots.
    const int dots_h = 8;
    int sc = display.width() / lettering::LOGO_W;
    while (sc > 1 && (lettering::LOGO_H * 2 + 5) * sc + lh * 2 + 2 > display.height() - dots_h) sc--;
    if (sc < 1) sc = 1;

    // Block from the wordmark down to the second text line, centred above the dots.
    const int logo_h = lettering::LOGO_H * sc;
    const int block_h = logo_h * 2 + 5 * sc + lh * 2 + 2;
    int top = (display.height() - dots_h - block_h) / 2;
    if (top < 0) top = 0;

    // Wordmark: rises 6 px into place over the first 400 ms (ease-out).
    int rise = 0;
    if (t < 400) { int k = 400 - (int)t; rise = (6 * sc * k * k) / (400 * 400); }
    const int lx = (display.width() - lettering::LOGO_W * sc) / 2;
    for (int r = 0; r < lettering::LOGO_H; r++)
      for (int col = 0; col < lettering::LOGO_W; ) {   // runs of inked pixels as one rect
        if (!lettering::logoBit(col, r)) { col++; continue; }
        int e = col;
        while (e + 1 < lettering::LOGO_W && lettering::logoBit(e + 1, r)) e++;
        display.fillRect(lx + col * sc, top + rise + r * sc, (e - col + 1) * sc, sc);
        col = e + 1;
      }

    int y = top + logo_h + 3 * sc;
    const int solo_w = lettering::textW("solo", 0) * sc + 2 * sc * 3;
    if (t >= 400) drawLettering(display, "solo", cx - solo_w / 2, y, 2 * sc, sc);   // once the wordmark has landed
    y += logo_h + 2 * sc;
    display.drawTextCentered(cx, y, _solo_ver[0] ? _solo_ver : "dev");
    y += lh + 2;
    display.drawTextCentered(cx, y, _line2);

    drawLoadingDots(display, cx, display.height() - 2);   // as the L2's
    return anim ? 40 : 1000;
  }

  void poll() override {
    if ((int32_t)(millis() - dismiss_after) >= 0) {
      _task->gotoHomeScreen();
    }
  }
};

static const int QUICK_MSGS_MAX = 10;


// Telemetry (ui-core/Telemetry.h) in this display's font and width: tight for
// the dashboard's short fields, spaced ("49 m") on the Status screen's rows.
static const telemetry::Style L1_TELEMETRY = { "\xc2\xb0", false, false, 3 };
static const telemetry::Style L1_INFO      = { "\xc2\xb0", true,  false, 5 };

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
#include "BatteryCurveScreen.h"
#include "LiveShareScreen.h"
#include "LocatorScreen.h"
#include "TrailScreen.h"
#include "CompassScreen.h"
#include "StatusScreen.h"   // Home › Status (was Tools › Diagnostics + Satellites)
#include "RepeaterScreen.h"
#if defined(PIN_GPIO1)
#include "GpioScreen.h"
#endif
#include "ToolsScreen.h"
#include "CalendarView.h"       // the month under a tall clock, Clock tools › Calendar
#include "ClockToolsScreen.h"   // Alarm / Timer / Stopwatch / Calendar (Clock page › Enter)

#include "../ui-core/Battery.h"

// The time on a tall portrait panel (e-ink in portrait -- height > width): HH
// and MM stacked on two lines in the huge built-in font (size 4, ~56 px tall)
// so the digits fill the narrow width. Returns the y just below it.
static int drawClockTall(DisplayDriver& d, int top_y, const struct tm* ti, bool h12) {
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
    d.setCursor(cx - ((int)d.getTextWidth(s) - trail) / 2, yy);
    d.print(s);
  };
  drawBig(hbuf, y);  y += lhb + 2;
  drawBig(mbuf, y);  y += lhb + 2;
  if (ap) {
    d.setTextSize(2);
    d.drawTextCentered(cx, y, ap);
    y += d.getLineHeight() + 1;
  }
  d.setTextSize(1);
  return y;
}

// The big clock on wide panels (Home clock page, lock screen): HH:MM in the
// splash's slanted lettering (ui-core/Lettering.h) at `sc`, centred on cx. A
// 12-hour clock drops the leading zero, closes the gaps up and sets AM / PM
// beside the digits on their baseline -- "12:59 PM" still fits 128 px.
struct BigClock { int sc, h; };
static BigClock bigClockSize(const DisplayDriver& d) {
  int sc = d.width() / 64;
  if (sc > d.height() / 32) sc = d.height() / 32;
  if (sc < 1) sc = 1;
  return { sc, lettering::LOGO_H * sc };
}
// The big clock's text, gaps and AM / PM, measured: its full width (w), the
// digits' (dw). draw() puts its left edge at x.
struct BigClockText {
  char t[8];
  const char* ap = nullptr;
  int sc, gap, dw, w;
  BigClockText(DisplayDriver& d, const struct tm* ti, bool h12, int scale) : sc(scale), gap(scale + 1) {
    if (h12) {
      int hh = ti->tm_hour % 12; if (hh == 0) hh = 12;
      snprintf(t, sizeof(t), "%d:%02d", hh, ti->tm_min);
      ap = ti->tm_hour < 12 ? "AM" : "PM";
      gap = sc / 2;
    } else {
      snprintf(t, sizeof(t), "%02d:%02d", ti->tm_hour, ti->tm_min);
    }
    dw = lettering::textW(t, 0) * sc + gap * ((int)strlen(t) - 1);
    d.setTextSize(1);
    w = dw + (ap ? (int)d.getTextWidth(ap) + 2 * sc : 0);
  }
  void draw(DisplayDriver& d, int x, int y) const {
    info::lettering(d, x, y, t, sc, gap);
    if (!ap) return;
    d.setCursor(x + dw + 2 * sc, y + lettering::LOGO_H * sc - d.getLineHeight() + 1);
    d.print(ap);
  }
};
static void drawBigClock(DisplayDriver& d, int cx, int y, const struct tm* ti, bool h12, int sc) {
  BigClockText bt(d, ti, h12, sc);
  bt.draw(d, cx - bt.w / 2, y);
}

static const char* const WDAY[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* const MON[]  = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

// ── HomeScreen ────────────────────────────────────────────────────────────────
// Forward declaration to be able to call formatDashVal from HomeScreen::render()
static void formatDashVal(uint8_t field, char* val, int val_len, uint16_t batt_mv, uint16_t low_batt_mv,
                          int unread, bool unread_overflow, bool imperial, CayenneLPP* lpp, bool nouns);


class HomeScreen : public UIScreen {
  enum HomePage {
    CLOCK,
    FAVOURITES,
    STATUS,      // radio, GPS, power, mesh at a glance (took over Radio / GPS / Sensors)
    BLUETOOTH,
    ADVERT,
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
    const int pg_half = (PAGE_ICON_PX * miniIconScale(d) + 1) / 2;
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
    _quick_sel = 0;   // a quick panel starts on its first row
    DisplayDriver* d = _task->getDisplay();
    // Pages with the header slide below it; the full-screen clock slides whole.
    const bool whole = from == CLOCK || _page == CLOCK;
    if (d && d->slideBegin(whole ? 0 : contentTop(*d))) {
      _slide_dir = (int8_t)dir; _slide_from = from; _slide_t0 = millis();
    }
  }

  unsigned long _advert_sent_ms = 0;   // last advert sent from this page (0: none yet)

  // ── Quick panels: an icon page as a few rows with margins round
  // them, the last one opening the full screen. Up/Down picks a row, Enter
  // acts on it. The rows are the Settings list's own (value, switch, bar).
  enum QKind : uint8_t { Q_VALUE, Q_SWITCH, Q_BAR, Q_BADGE, Q_MORE };
  enum QAct  : uint8_t { QA_NONE, QA_MORE, QA_BRIGHTNESS, QA_GPS, QA_CHAT, QA_BT,
                         QA_ADVERT, QA_AUTO_ADVERT, QA_TOOL, QA_LOCK, QA_HIBERNATE };
  struct QRow {
    const char* label;
    char    value[16];
    QKind   kind;
    QAct    act;
    uint8_t n;          // switch on, bar level, unread count, QA_TOOL's action
    bool    overflow;   // unread past what the badge can count
    bool    is_ch;      // QA_CHAT: a channel (ch) or a DM / room (prefix)
    uint8_t ch;
    uint8_t prefix[4];
    char    name[24];   // QA_CHAT: the label's storage
  };
  static const int QUICK_MAX = 8;
  // How many rows a quick panel has room for (at least the 3 a 128x64 OLED
  // fits): a taller screen lists more recent chats.
  int quickFit() const {
    DisplayDriver* d = _task->getDisplay();
    if (!d) return 3;
    const int n = (d->height() - contentTop(*d) - 5) / (d->lineStep() + 1);
    return n < 3 ? 3 : n > QUICK_MAX ? QUICK_MAX : n;
  }
  uint8_t _quick_sel = 0;
  bool    _quick_edit = false;   // the selected row is being changed with Left/Right
  bool    _quick_dirty = false;  // ...and has moved: saved once when it ends
  static bool isQuickPage(int p) {
    return p == SETTINGS || p == QUICK_MSG || p == BLUETOOTH || p == ADVERT || p == TOOLS || p == SHUTDOWN;
  }

  // What a tool is doing now, for its row on the Tools panel ("" when it has
  // nothing running worth telling).
  void toolState(ToolsScreen::Action a, char* buf, int n) {
    buf[0] = 0;
    switch (a) {
      case ToolsScreen::ACT_TRAIL: {
        TrailStore& tr = _task->trail();
        if (tr.isActive()) geo::fmtDist(buf, n, tr.totalDistanceMeters() / 1000.0f, _task->useImperial(), true);
        else strncpy(buf, "Off", n);
        break;
      }
      case ToolsScreen::ACT_LIVESHARE:
        strncpy(buf, _node_prefs && _node_prefs->loc_share_enabled ? "On" : "Off", n); break;
      case ToolsScreen::ACT_LOCATOR:
        strncpy(buf, _node_prefs && _node_prefs->locator_enabled ? "On" : "Off", n); break;
      case ToolsScreen::ACT_REPEATER:
        strncpy(buf, _node_prefs && _node_prefs->client_repeat ? "On" : "Off", n); break;
      case ToolsScreen::ACT_AUTOADVERT:
        strncpy(buf, AutoAdvertScreen::OPT_LABELS[AutoAdvertScreen::indexOf(
                       _node_prefs ? _node_prefs->advert_auto_interval_sec : 0)], n); break;
      case ToolsScreen::ACT_CLOCK:
        if (_task->isTimerRunning()) {
          const uint32_t s = (_task->timerRemainingMs() + 999) / 1000;
          if (s >= 3600) snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
          else           snprintf(buf, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
        }
        break;
      default: break;
    }
    buf[n - 1] = 0;
  }

  // The `max` newest conversations, DM / room or channel, newest first.
  int recentChats(QRow* out, int max) {
    MessageHistory& h = _task->core().history;
    struct Cand { bool is_ch; uint8_t ch; uint8_t prefix[4]; uint32_t ts; } c[2 * QUICK_MAX];
    if (max > QUICK_MAX) max = QUICK_MAX;
    int nc = 0;
    for (int j = 0; j < h.dmHistCount() && nc < max; j++) {
      const DmHistEntry& e = h.dmAtPos(h.dmHistPosNewest(j));
      bool seen = false;
      for (int k = 0; k < nc; k++) if (memcmp(c[k].prefix, e.prefix, 4) == 0) seen = true;
      if (seen) continue;
      c[nc].is_ch = false; c[nc].ch = 0; memcpy(c[nc].prefix, e.prefix, 4); c[nc].ts = e.timestamp; nc++;
    }
    const int nd = nc;
    for (int j = 0; j < h.chHistCount() && nc < nd + max; j++) {
      const ChHistEntry& e = h.chAtPos(h.chHistPosNewest(j));
      bool seen = false;
      for (int k = nd; k < nc; k++) if (c[k].ch == e.ch_idx) seen = true;
      if (seen) continue;
      c[nc].is_ch = true; c[nc].ch = e.ch_idx; c[nc].ts = e.timestamp; nc++;
    }
    for (int a = 0; a < nc; a++)            // newest first
      for (int b = a + 1; b < nc; b++)
        if (c[b].ts > c[a].ts) { Cand t = c[a]; c[a] = c[b]; c[b] = t; }
    const uint32_t now = rtc_clock.getCurrentTime();
    int n = 0;
    for (int k = 0; k < nc && n < max; k++) {
      QRow& r = out[n];
      memset(&r, 0, sizeof(r));
      r.act = QA_CHAT; r.is_ch = c[k].is_ch;
      if (r.is_ch) {
        ChannelDetails cd;
        if (!the_mesh.getChannel(c[k].ch, cd) || !cd.name[0]) continue;
        r.ch = c[k].ch;
        snprintf(r.name, sizeof(r.name), "#%s", chanctl::bareName(cd.name));
        r.n = _task->getChannelUnread(r.ch);
        r.overflow = r.n > 0 && _task->getChannelUnreadOverflow(r.ch);
      } else {
        ContactInfo ci;
        if (!MessageHistory::contactByPrefix(c[k].prefix, ci)) continue;
        memcpy(r.prefix, c[k].prefix, 4);
        snprintf(r.name, sizeof(r.name), "%s", ci.name);
        r.n = _task->getDMUnread(ci.id.pub_key);
        r.overflow = r.n > 0 && _task->getDMUnreadOverflow(ci.id.pub_key);
      }
      r.label = r.name;
      if (r.n > 0) r.kind = Q_BADGE;
      else { r.kind = Q_VALUE; geo::fmtAgeShort(r.value, sizeof(r.value), now, c[k].ts); }
      n++;
    }
    return n;
  }

  int buildQuick(int page, QRow* rows) {
    int n = 0;
    auto add = [&](const char* label, QKind k, QAct a, uint8_t v) {
      QRow& r = rows[n++];
      memset(&r, 0, sizeof(r));
      r.label = label; r.kind = k; r.act = a; r.n = v;
    };
    // A panel with a whole screen behind it lists the way in first, its
    // shortcuts after.
    if (page == SETTINGS) {
      add("All settings", Q_MORE, QA_MORE, 0);
#if FEAT_BRIGHTNESS_SETTING
      add("Brightness", Q_BAR, QA_BRIGHTNESS, _node_prefs ? _node_prefs->display_brightness + 1 : 0);
#endif
#if ENV_INCLUDE_GPS == 1
      if (sensors.getLocationProvider()) add("GPS", Q_SWITCH, QA_GPS, _task->getGPSState() ? 1 : 0);
#endif
    } else if (page == QUICK_MSG) {
      add("All messages", Q_MORE, QA_MORE, 0);
      n += recentChats(rows + n, quickFit() - 1);
    } else if (page == BLUETOOTH) {
      const bool on = _task->isSerialEnabled();
      add("Bluetooth", Q_SWITCH, QA_BT, on ? 1 : 0);
      add("Phone", Q_VALUE, QA_NONE, 0);
      char* v = rows[n - 1].value;
      if (!on)                            strcpy(v, "-");
      else if (_task->isBLEConnected())   strcpy(v, "Connected");
      // The pairing PIN is BLE-specific: show it while BLE is on but not yet
      // bonded. (Gating on a plain isConnected() broke this on dual builds,
      // where it's hardcoded true.)
      else if (the_mesh.getBLEPin() != 0) snprintf(v, sizeof(rows[0].value), "PIN %d", (int)the_mesh.getBLEPin());
      else                                strcpy(v, "Waiting");
    } else if (page == ADVERT) {
      add("Send now", Q_VALUE, QA_ADVERT, 0);
      if (_advert_sent_ms) {   // when this page last sent one
        const unsigned long ago = (millis() - _advert_sent_ms) / 60000UL;
        char* v = rows[n - 1].value;
        if (ago == 0)      strcpy(v, "sent");
        else if (ago < 60) snprintf(v, sizeof(rows[0].value), "%lum ago", ago);
        else               snprintf(v, sizeof(rows[0].value), "%luh ago", ago / 60);
      }
      add("Auto", Q_VALUE, QA_AUTO_ADVERT, 0);
      strcpy(rows[n - 1].value, AutoAdvertScreen::OPT_LABELS[AutoAdvertScreen::indexOf(
                                  _node_prefs ? _node_prefs->advert_auto_interval_sec : 0)]);
    } else if (page == TOOLS) {
      add("All tools", Q_MORE, QA_MORE, 0);
      for (int i = 0; i < ToolsScreen::RECENT_MAX; i++) {
        const ToolsScreen::Action a = ToolsScreen::s_recent[i];
        add(ToolsScreen::labelOf(a), Q_VALUE, QA_TOOL, (uint8_t)a);
        toolState(a, rows[n - 1].value, sizeof(rows[0].value));
      }
    } else if (page == SHUTDOWN) {
      add("Lock screen", Q_VALUE, QA_LOCK, 0);
      add("Hibernate", Q_VALUE, QA_HIBERNATE, 0);
      // The charge left, which is what you weigh before switching off.
      const int mv = _task->getBattMilliVolts();
      snprintf(rows[n - 1].value, sizeof(rows[0].value), "%d%%%s",
               battery::percent(mv, _node_prefs ? (int)_node_prefs->low_batt_mv : 0),
               board.isExternalPowered() ? " USB" : "");
    }
    return n;
  }

  // Small arrows either side of x0..x0+w while Left/Right change a row.
  static void drawEditArrows(DisplayDriver& d, int x0, int w, int cy, int s) {
    for (int i = 0; i < 3; i++) {
      d.fillRect(x0 - 6 * s + i * s, cy - i * s, s, (2 * i + 1) * s);       // ◂
      d.fillRect(x0 + w + 5 * s - i * s, cy - i * s, s, (2 * i + 1) * s);   // ▸
    }
  }

  // The rows, from a gap below the page icons and inset from both sides.
  int drawQuickPanel(DisplayDriver& d, int top, QRow* rows, int n) {
    const int W = d.width(), m = W / 16, lh = d.getLineHeight(), step = d.lineStep() + 1;
    const int reserve = m - 2;                       // so a row's right edge is W - m
    if (_quick_sel >= n) _quick_sel = n - 1;
    int y = top + 5, mq = 0;
    for (int i = 0; i < n; i++, y += step) {
      const QRow& r = rows[i];
      const bool sel = i == _quick_sel;
      d.setColor(DisplayDriver::LIGHT);
      if (sel) { d.fillSoftRect(m - 3, y - 1, W - 2 * m + 6, lh + 1); d.setColor(DisplayDriver::DARK); }
      switch (r.kind) {
        case Q_SWITCH: info::switchRow(d, y, r.label, r.n != 0, sel, reserve, m); break;
        case Q_BAR: {
          d.setCursor(m, y); d.print(r.label);
          const int box = lh - 3, gap = 2, s = miniIconScale(d), bars = 5 * box + 4 * gap;
          const bool editing = sel && _quick_edit;
          const int x0 = W - m - bars - (editing ? 6 * s : 0);
          if (editing) drawEditArrows(d, x0, bars, y + 1 + box / 2, s);
          for (int k = 0; k < 5; k++) {
            const int bx = x0 + k * (box + gap);
            if (k < r.n) d.fillRect(bx, y + 1, box, box); else d.drawRect(bx, y + 1, box, box);
          }
          break;
        }
        case Q_BADGE: {
          const int bw = d.unreadBadgeWidth(r.n, r.overflow);
          d.drawTextEllipsized(m, y, W - 2 * m - bw - 4, r.label);
          d.drawUnreadBadge(W - m, y, r.n, sel, r.overflow);
          break;
        }
        case Q_MORE:
          d.setCursor(m, y); d.print(r.label);
          d.drawTextRightAlign(W - m, y, ">");
          break;
        default:
          if (sel && _quick_edit) {   // a value being changed: arrows round it
            const int s = miniIconScale(d), vw = d.getTextWidth(r.value), x0 = W - m - vw - 6 * s;
            d.setCursor(m, y); d.print(r.label);
            d.setCursor(x0, y); d.print(r.value);
            drawEditArrows(d, x0, vw, y + 1 + (lh - 3) / 2, s);
          } else {
            int q = info::valueRow(d, y, r.label, r.value, sel, reserve, m);
            if (q > 0) mq = q;
          }
      }
      d.setColor(DisplayDriver::LIGHT);
    }
    return mq;
  }

  void quickAct(int page, const QRow& r) {
    switch (r.act) {
      case QA_NONE: break;
      case QA_MORE:
        if (page == SETTINGS)    _task->gotoSettingsScreen();
        else if (page == TOOLS)  _task->gotoToolsScreen();
        else                     _task->gotoMessagesScreen();
        break;
      case QA_BRIGHTNESS:
      case QA_AUTO_ADVERT: _quick_edit = true; break;   // Left/Right change it, Enter / Back keep it
      case QA_BT:
        if (_task->isSerialEnabled()) _task->disableSerial(); else _task->enableSerial();
        break;
      case QA_ADVERT:
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) _advert_sent_ms = millis();   // the row says so
        else _task->showAlert("Advert failed", 1000);
        break;
      case QA_TOOL: ToolsScreen::run(_task, (ToolsScreen::Action)r.n, true); break;
      case QA_LOCK: _task->lockScreen(); break;
      case QA_HIBERNATE: _shutdown_init = true; break;   // waits for the button to be released
      case QA_GPS: _task->toggleGPS(false); break;
      case QA_CHAT:
        if (r.is_ch) { _task->openChannelHistory(r.ch); break; }
        {
          ContactInfo ci;
          if (!MessageHistory::contactByPrefix(r.prefix, ci)) break;
          if (ci.type == ADV_TYPE_ROOM) _task->openRoomServer(ci); else _task->openContactDM(ci);
        }
        break;
    }
  }

  int pageBit(int page) const {
    if (page == CLOCK)      return NodePrefs::HPB_CLOCK;
    if (page == FAVOURITES) return NodePrefs::HPB_FAVOURITES;
    if (page == STATUS)    return NodePrefs::HPB_RADIO;   // the old Radio slot
    if (page == BLUETOOTH) return NodePrefs::HPB_BLUETOOTH;
    if (page == ADVERT)    return NodePrefs::HPB_ADVERT;
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
      case NodePrefs::HPB_RADIO:     return STATUS;
      case NodePrefs::HPB_BLUETOOTH: return BLUETOOTH;
      case NodePrefs::HPB_ADVERT:    return ADVERT;
      case NodePrefs::HPB_TOOLS:     return TOOLS;
      case NodePrefs::HPB_SHUTDOWN:  return SHUTDOWN;
      case NodePrefs::HPB_SETTINGS:  return SETTINGS;
      case NodePrefs::HPB_QUICK_MSG: return QUICK_MSG;
      case NodePrefs::HPB_MAP:       return MAP;
      default: return -1;
    }
  }

  bool isPageVisible(int page) const {
    int bit = pageBit(page);
    if (bit < 0) return true;
    uint16_t mask = (_node_prefs && _node_prefs->home_pages_mask) ? _node_prefs->home_pages_mask : NodePrefs::HP_ALL;
    // Status stands in for the Radio, GPS and Sensors pages: shown if any was.
    if (page == STATUS) return (mask & (NodePrefs::HP_RADIO | NodePrefs::HP_GPS | NodePrefs::HP_SENSORS)) != 0;
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
  // the time / node name on the pages (name_min below); the lock page passes
  // 0, its clock sits under the bar. -1 = use the normal name reserve.
  int renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts, int reserve_left = -1) {
    int low_mv = _node_prefs ? (int)_node_prefs->low_batt_mv : 0;
    int pct = battery::percent((int)batteryMilliVolts, low_mv);

    uint8_t mode = battery::mode(_node_prefs ? _node_prefs->batt_display_mode : 0);

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int ind     = cw + 2;    // single-char indicator width
    const int ind_h   = display.isSingleFont() ? lh - 2 : lh;
    const int ind_gap = display.pixelScale() > 1 ? 3 : 1;  // gap between indicator boxes

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
      const int bm = display.pixelScale() > 1 ? 3 : 2;  // inner margin: 3px on a doubled panel, else 2px
      battLeftX = display.width() - iconW - 3;
      // The outline with its corner pixels left out, softly rounded.
      display.fillRect(battLeftX + 1, 0, iconW - 2, 1);
      display.fillRect(battLeftX + 1, iconH - 1, iconW - 2, 1);
      display.fillRect(battLeftX, 1, 1, iconH - 2);
      display.fillRect(battLeftX + iconW - 1, 1, 1, iconH - 2);
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
    // bar sheds its least-important cues instead of crushing the time. Once an
    // icon won't fit above the reserved area, every lower-priority icon after
    // it is dropped too (the list is ordered high→low).
    //
    // Priority: power > BT > GPS fix > alarm > mute > auto-advert > trail > live-share >
    // repeater. Battery (drawn above) is always rightmost. The background modes
    // (advert / trail / live-share / repeater) stay outside any BT gate — they
    // keep running with Bluetooth off, so their cue must not vanish with it.
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    bool gps_on  = loc && _node_prefs && _node_prefs->gps_enabled;
    bool mute_on = false;
#ifdef PIN_BUZZER
    mute_on = _task->isBuzzerQuiet();
#endif
    // Plain glyphs, no blinking: each one shows a state by its shape.
    // Bluetooth only once a phone is connected (it's on nearly always; the
    // Bluetooth page tells the rest), GPS as a broken reticle until a fix.
    const bool gps_fix = gps_on && loc->isValid();
    struct Sicon { bool active; const MiniIcon* icon; };
    const Sicon icons[] = {
      { board.isExternalPowered(),                                 &ICON_CHARGE },   // USB power: next to the battery
      { _task->isSerialEnabled() && _task->isBLEConnected(),       &ICON_BLUETOOTH },
      { gps_on,                                                    gps_fix ? &ICON_GPS : &ICON_GPS_SEARCH },
      { _node_prefs && _node_prefs->alarm_on,                      &ICON_ALARM },
      { mute_on,                                                   &ICON_MUTE },
      { _node_prefs && _node_prefs->advert_auto_interval_sec > 0,  &ICON_ADVERT },
      { _task->trail().isActive(),                                 &ICON_TRAIL },
      { _node_prefs && _node_prefs->loc_share_enabled,             &ICON_MAP_CONTACT },
      { _node_prefs && _node_prefs->client_repeat,                 &ICON_REPEATER },
    };

    int x = battLeftX;
    const int name_min = (reserve_left >= 0) ? reserve_left : display.getCharWidth() * 5;
    // Settings > Message alert Compact: the envelope and its count, first by the
    // battery; its place is kept through the blink so nothing beside it jumps.
    if (const int n = _task->msgBadgeCount()) {
      const int ix = x - mailCountWidth(display, n) - ind_gap - 2 * miniIconScale(display);   // the icons' boxes pad theirs
      if (ix >= name_min) {
        if (_task->msgBadgeLit()) drawMailCount(display, ix, 0, ind_h, n);
        _task->msgBadgeInBar();
        x = ix;
      }
    }
    for (const Sicon& s : icons) {
      if (!s.active) continue;
      int ix = x - ind - ind_gap;
      if (ix < name_min) break;                        // out of room — drop this + all lower priority
      drawSlotIcon(display, ix, ind, ind_h, *s.icon);
      x = ix;
    }
    return x;
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  unsigned long next_sensors_refresh = 0;   // 0: read on the next call

  void refresh_sensors() {
    if (!next_sensors_refresh || (int32_t)(millis() - next_sensors_refresh) >= 0) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      { CardKBBusLock bus_lock; sensors.querySensors(0xFF, sensors_lpp); }
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = (millis() + 5000) | 1; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = (millis() + 60000) | 1; // refresh sensor values every 1 min
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

  // The Map page: the mini-map in a soft frame on the left, what it shows
  // spelled out in a column on the right (GPS, the trail, live contacts or
  // the target). Without a position the column still says what's missing.
  // A tall screen (portrait e-ink) stacks them: the map, the four rows under it.
  void drawMapPage(DisplayDriver& d, int top) {
    const int W = d.width(), H = d.height(), lh = d.getLineHeight();
    const int s = miniIconScale(d);
    const bool tall = H - top > W;
    // A screen 40 characters wide (the 4.2"): a square map and a column of
    // labelled rows beside it, one line apart.
    const bool wide = !tall && W >= 40 * d.getCharWidth();
    const int rows_h = tall ? 4 * (lh + lh / 2) : 0;
    const int ph = H - top - rows_h;
    const int pw = tall ? W : wide ? (ph < W * 3 / 5 ? ph : W * 3 / 5) : W / 2;   // map panel
    d.setColor(DisplayDriver::LIGHT);
    d.drawSoftRect(0, top, pw, ph);
    if (!drawMapPreview(d, 2, top + 2, pw - 4, ph - 4)) {
      // An empty sheet: a sparse dot grid with the reason over it.
      for (int gy = top + 4; gy < top + ph - 2; gy += 6 * s)
        for (int gx = 4; gx < pw - 2; gx += 6 * s) d.fillRect(gx, gy, s, s);
      const char* why = "No fix";
      const int tw = d.getTextWidth(why), tx = (pw - tw) / 2, ty = top + (ph - lh) / 2;
      d.setColor(DisplayDriver::DARK);
      d.fillRect(tx - 2, ty - 1, tw + 4, lh + 2);
      d.setColor(DisplayDriver::LIGHT);
      d.setCursor(tx, ty);
      d.print(why);
    }

    if (wide) { drawMapColumn(d, pw + 6, top, W - 2); return; }

    // The column: four rows, always, like the Status tiles.
    const int cx = tall ? 2 : pw + 3, cw = W - cx;
    const int row_h = tall ? rows_h / 4 : ph / 4;
    int y = (tall ? top + ph : top) + (row_h - lh) / 2 + 1;
    auto line = [&](const MiniIcon& ic, const char* text) {
      miniIconDraw(d, cx, y, ic);
      const int tx = cx + ic.w * s + 2 * s;
      d.drawTextEllipsized(tx, y, cx + cw - tx, text);
      y += row_h;
    };
    char buf[20];
#if ENV_INCLUDE_GPS == 1
    LocationProvider* loc = sensors.getLocationProvider();
    const bool on = _task->getGPSState();
    if (!loc)                 strcpy(buf, "No GPS");
    else if (!on)             strcpy(buf, "Off");
    else if (!loc->isValid()) strcpy(buf, "No fix");
    else                      snprintf(buf, sizeof(buf), "%ld sats", loc->satellitesCount());
#else
    strcpy(buf, "No GPS");
#endif
    line(ICON_GPS, buf);
    TrailStore& tr = _task->trail();
    if (tr.empty() && !tr.isActive()) strcpy(buf, "No trail");
    else geo::fmtDist(buf, sizeof(buf), tr.totalDistanceMeters() / 1000.0f, _task->useImperial(), true);
    line(ICON_TRAIL, buf);
    const float km = statusDistanceKm();
    int32_t tla, tlo;
    const bool target = _task->activeTargetPos(tla, tlo);
    const int live = _task->liveTrack().active(rtc_clock.getCurrentTime());
    if (live > 0 && km >= 0 && !target) geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
    else                                snprintf(buf, sizeof(buf), "%d live", live);
    line(ICON_MAP_CONTACT, buf);
    if (target) {
      if (km >= 0) geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
      else         strcpy(buf, "-");
      line(ICON_MAP_TARGET, buf);
    } else {
      snprintf(buf, sizeof(buf), "%d wpts", _task->waypoints().count());
      line(ICON_MAP_WAYPOINT, buf);
    }
  }

  // The wide Map page's column, x0..x1 from top: GPS and the position, the
  // trail, then live contacts, the target and waypoints, each an icon, a
  // label and the value at the right, the groups split by a dotted rule.
  void drawMapColumn(DisplayDriver& d, int x0, int top, int x1) {
    const int lh = d.getLineHeight(), s = miniIconScale(d), step = d.lineStep() + 2;
    const int lx = x0 + PAGE_ICON_PX * s + 2 * s;   // labels clear of the widest icon
    int y = top + 1;
    auto row = [&](const MiniIcon* ic, const char* label, const char* val) {
      if (ic) miniIconDraw(d, x0, y + (lh - ic->h * s) / 2 - s, *ic);
      d.setCursor(lx, y); d.print(label);
      d.drawTextRightAlign(x1, y, val);
      y += step;
    };
    auto rule = [&]() { info::rule(d, x0, y + lh / 2 - 2, x1 - x0); y += lh / 2 + 2; };
    char buf[24];
#if ENV_INCLUDE_GPS == 1
    LocationProvider* loc = sensors.getLocationProvider();
    const bool on = _task->getGPSState();
    if (!loc)                 strcpy(buf, "none");
    else if (!on)             strcpy(buf, "off");
    else if (!loc->isValid()) strcpy(buf, "no fix");
    else                      snprintf(buf, sizeof(buf), "%ld sats", loc->satellitesCount());
    row(&ICON_GPS, "GPS", buf);
    int32_t mla, mlo;
    if (_task->currentLocation(mla, mlo)) {
      snprintf(buf, sizeof(buf), "%.5f", mla / 1e6); row(nullptr, "Lat", buf);
      snprintf(buf, sizeof(buf), "%.5f", mlo / 1e6); row(nullptr, "Lon", buf);
    }
#else
    row(&ICON_GPS, "GPS", "none");
#endif
    rule();
    TrailStore& tr = _task->trail();
    if (tr.empty() && !tr.isActive()) strcpy(buf, "-");
    else geo::fmtDist(buf, sizeof(buf), tr.totalDistanceMeters() / 1000.0f, _task->useImperial(), true);
    row(&ICON_TRAIL, "Trail", buf);
    if (!tr.empty() || tr.isActive()) {
      const uint32_t t = tr.elapsedSeconds();
      snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60));
      row(nullptr, tr.isActive() ? (tr.isPaused() ? "Paused" : "Rec") : "Time", buf);
      snprintf(buf, sizeof(buf), "%d", tr.count());
      row(nullptr, "Points", buf);
    }
    rule();
    const float km = statusDistanceKm();
    int32_t tla, tlo;
    const bool target = _task->activeTargetPos(tla, tlo);
    const int live = _task->liveTrack().active(rtc_clock.getCurrentTime());
    snprintf(buf, sizeof(buf), "%d", live);
    row(&ICON_MAP_CONTACT, "Live", buf);
    if (live > 0 && km >= 0 && !target) {
      geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
      row(nullptr, "Nearest", buf);
    }
    if (target) {
      if (km >= 0) geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
      else         strcpy(buf, "-");
      row(&ICON_MAP_TARGET, "Target", buf);
    }
    snprintf(buf, sizeof(buf), "%d", _task->waypoints().count());
    row(&ICON_MAP_WAYPOINT, "Waypoints", buf);
  }

  // The Favourites grid: landscape 3x2, portrait 2x3.
  static int favCols(DisplayDriver& d) { return d.isLandscape() ? 3 : 2; }

  // The clock fields set in Settings (dashboard_fields), in order; their count.
  int clockFields(uint8_t* out) {
    int n = 0;
    if (_node_prefs)
      for (int k = 0; k < 3; k++)
        if (_node_prefs->dashboard_fields[k] != telemetry::NONE && _node_prefs->dashboard_fields[k] < telemetry::COUNT)
          out[n++] = _node_prefs->dashboard_fields[k];
    return n;
  }
  // One field's value without its noun (the label beside it names it); "-" when there is none.
  void clockFieldValue(uint8_t f, char* val, int n) {
    formatDashVal(f, val, n, _task->getBattMilliVolts(), _node_prefs->low_batt_mv,
                  _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount(),
                  _task->getAnyUnreadOverflow(), _node_prefs->units_imperial, &sensors_lpp, false);
    if (!val[0]) snprintf(val, n, "-");
  }

  // ── Clock page: the big clock, a seconds bar under it, the date, and the
  // clock fields (Settings) as columns along the bottom, name over value.
  // Without fields the clock and date sit in the middle.
  // The days left this month the alarm goes off on, as calendar::draw()'s
  // marks: the repeat days, or a one-shot's next (today or tomorrow).
  uint32_t alarmDays(const struct tm& t) const {
    if (!_node_prefs) return 0;
    return calmath::alarmDays(t, _node_prefs->alarm_on, _node_prefs->alarm_repeat_mask,
                              _node_prefs->alarm_hour, _node_prefs->alarm_min);
  }

  void drawClockPage(DisplayDriver& d, uint32_t unix_ts) {
    struct tm ti;
    localTm(unix_ts, _node_prefs ? _node_prefs->tz_offset_hours : 0, ti);
    const bool h12 = _node_prefs && _node_prefs->clock_12h;
    const bool show_sec = !Features::IS_EINK && (!_node_prefs || !_node_prefs->clock_hide_seconds);
    const int W = d.width(), H = d.height();
    d.setColor(DisplayDriver::LIGHT);
    d.setTextSize(1);
    const int lh = d.getLineHeight();

    uint8_t fields[3];
    const int nf = clockFields(fields);

    // The month under the date, where it fits: a tall screen (portrait
    // e-ink), or the 4.2" landscape one with the clock moved to the top.
    const int fields_h = nf ? 2 * lh + 1 + lh / 2 : 0;
    const int cal_h = calendar::height(d, ti);
    bool cal = false;
    int date_y;
    if (d.height() > d.width()) {   // portrait e-ink: stacked digits
      date_y = drawClockTall(d, lh / 2, &ti, h12);
      cal = true;
    } else {
      const BigClock bc = bigClockSize(d);
      const int bar = 3 * bc.sc / 2;              // the seconds bar and its gaps
      const int block = bc.h + bar + 1 + lh;
      cal = bc.sc + block + lh / 2 + 2 + cal_h <= H - fields_h;
      const int y = nf || cal ? bc.sc : (H - block) / 2;
      drawBigClock(d, W / 2, y, &ti, h12, bc.sc);
      const int bar_y = y + bc.h + bar / 2 + 1;
      if (show_sec) {   // fills over the minute: dotted track, solid part
        const int x0 = W / 8, bw = W - 2 * x0, fill = bw * ti.tm_sec / 60;
        d.fillRect(x0, bar_y, fill, 1);
        for (int x = x0 + fill + (fill & 1); x < x0 + bw; x += 2) d.fillRect(x, bar_y, 1, 1);
      }
      // Alarm armed: the bell at the bar's left end (the status bar, which
      // shows it everywhere else, is hidden on this page).
      if (_node_prefs && _node_prefs->alarm_on) miniIconDrawTop(d, W / 32, bar_y - 2, ICON_ALARM);
      date_y = y + bc.h + bar + 1;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d %s %d", WDAY[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon], 1900 + ti.tm_year);
    d.drawTextCentered(W / 2, date_y, buf);

    const int cal_top = date_y + lh + lh / 2 + 2;
    if (cal && cal_h <= H - fields_h - cal_top) {   // no wider than four characters a day
      const int cw = W < 28 * d.getCharWidth() ? W : 28 * d.getCharWidth();
      calendar::draw(d, (W - cw) / 2, cal_top, cw, ti, alarmDays(ti));
    }

    if (nf == 0) return;
    refresh_sensors();
    const int val_y = H - lh, lab_y = val_y - lh - 1, cw = W / nf;
    for (int k = 0; k < nf; k++) {
      char val[20];
      clockFieldValue(fields[k], val, sizeof(val));
      const int x0 = k * cw;
      auto cell = [&](int yy, const char* t) {
        if ((int)d.getTextWidth(t) <= cw - 2) d.drawTextCentered(x0 + cw / 2, yy, t);
        else d.drawTextEllipsized(x0 + 1, yy, cw - 2, t);
      };
      cell(lab_y, telemetry::LABEL[fields[k]]);
      cell(val_y, val);
    }
  }

  // ── Lock screen: the status bar (drawn by render()), then one of two
  // looks (Settings > Display > Lock clock):
  //   big     -- the big clock, the date, unread messages in a pill;
  //   compact -- a small clock with the date beside it, the clock fields as
  //              rows below (plus unread messages when no field shows them).
  // A key press on the lit screen (not the one that wakes it, so a glance
  // shows the data) puts the unlock hint in a pill at the bottom for a few
  // seconds (UITask::lockHintShown); e-ink keeps it up, it has no glance.
  void drawLockPage(DisplayDriver& d) {
    const int W = d.width(), H = d.height();
    d.setColor(DisplayDriver::LIGHT);
    d.setTextSize(1);
    const int lh = d.getLineHeight(), top = lh + 3;
    const bool hint = Features::IS_EINK || _task->lockHintShown();
    const int pill_y = H - lh - 3;   // the bottom pill, clear of what's above
    const int unread = _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount();
    const uint32_t unix_ts = _rtc->getCurrentTime();
    const bool synced = unix_ts >= 1000000000UL;
    struct tm ti;
    if (synced) localTm(unix_ts, _node_prefs ? _node_prefs->tz_offset_hours : 0, ti);
    const bool h12 = _node_prefs && _node_prefs->clock_12h;
    const bool compact = _node_prefs && _node_prefs->lock_compact && W > H;
    // The month and the rows under the big clock: on a tall screen (portrait
    // e-ink), and on one 40 characters wide (the 4.2").
    const bool extras = H > W || W >= 40 * d.getCharWidth();

    char pill[24] = "";
    if (compact) {
      const int m = W / 16, x0 = W / 32;
      int sc = bigClockSize(d).sc / 2;
      if (sc < 1) sc = 1;
      const int ch = lettering::LOGO_H * sc;
      if (!synced) {
        d.setCursor(x0, top + (ch - lh + 1) / 2);
        d.print("No time sync");
      } else {   // the date beside the clock, without the weekday if it won't fit
        BigClockText bt(d, &ti, h12, sc);
        bt.draw(d, x0, top);
        char date[16];
        snprintf(date, sizeof(date), "%s %d %s", WDAY[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon]);
        if (x0 + bt.w + lh / 2 + (int)d.getTextWidth(date) > W - x0)
          snprintf(date, sizeof(date), "%d %s", ti.tm_mday, MON[ti.tm_mon]);
        d.drawTextRightAlign(W - x0, top + (ch - lh + 1) / 2, date);
      }
      uint8_t rows[4];
      int nr = clockFields(rows);
      bool has_msgs = false;
      for (int k = 0; k < nr; k++) if (rows[k] == telemetry::MSGS) has_msgs = true;
      if (!has_msgs && unread > 0) rows[nr++] = telemetry::MSGS;
      if (nr) refresh_sensors();
      const int step = d.lineStep();
      for (int k = 0, y = top + ch + 6; k < nr && y + lh <= H; k++, y += step) {
        if (hint && y + lh >= pill_y) break;   // the hint pill takes the bottom
        char val[20];
        clockFieldValue(rows[k], val, sizeof(val));
        info::valueRow(d, y, telemetry::LABEL[rows[k]], val, false, m - 2, m);
      }
    } else {
      if (!synced) {
        d.drawTextCentered(W / 2, H / 2 - lh, "No time sync");
      } else {
        int date_y;
        if (H > W) {
          date_y = drawClockTall(d, top, &ti, h12);
        } else {
          const BigClock bc = bigClockSize(d);
          drawBigClock(d, W / 2, top, &ti, h12, bc.sc);
          date_y = top + bc.h + 3;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d %s", WDAY[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon]);
        d.drawTextCentered(W / 2, date_y, buf);
        if (extras) drawLockTallExtras(d, date_y + lh + lh / 2 + 2, hint ? pill_y - 3 : H, ti, unread);
      }
      if (!hint && unread > 0 && !(synced && extras)) snprintf(pill, sizeof(pill), "%d%s new", unread, _task->getAnyUnreadOverflow() ? "+" : "");
    }

    if (hint) {
      const int n = _task->lockSeqCount();
#if defined(CARDKB_I2C)
      const char* first = _task->hasCardKB() ? "Back+3xEnter/Fn+Esc" : "Hold Back + 3xEnter";
#else
      const char* first = "Hold Back + 3xEnter";
#endif
      snprintf(pill, sizeof(pill), "%s", n == 0 ? first : n == 1 ? "Enter x2 more..." : "Enter x1 more...");
    }
    if (!pill[0]) return;
    const int pw = d.getTextWidth(pill) + 6;
    d.fillSoftRect((W - pw) / 2, pill_y, pw, lh + 3);
    d.setColor(DisplayDriver::DARK);
    d.drawTextCentered(W / 2, pill_y + 2, pill);
    d.setColor(DisplayDriver::LIGHT);
  }

  // Under the lock clock on a tall or a 4.2" screen, between y and bottom: the month,
  // then a row each for unread messages, the armed alarm and the clock fields
  // -- the rows first, the calendar only if there's room for it as well.
  void drawLockTallExtras(DisplayDriver& d, int y, int bottom, const struct tm& ti, int unread) {
    const int W = d.width(), m = W / 16, step = d.lineStep(), s = miniIconScale(d);
    struct Row { const MiniIcon* ic; const char* label; char value[20]; } rows[6];
    int n = 0;
    auto add = [&](const MiniIcon* ic, const char* label) -> char* {
      rows[n].ic = ic; rows[n].label = label; rows[n].value[0] = 0;
      return rows[n++].value;
    };
    if (unread > 0)
      snprintf(add(&ICON_PG_MSG, "Messages"), sizeof(rows[0].value), "%d%s new", unread, _task->getAnyUnreadOverflow() ? "+" : "");
    if (_node_prefs && _node_prefs->alarm_on)
      snprintf(add(&ICON_ALARM, "Alarm"), sizeof(rows[0].value), "%02d:%02d", _node_prefs->alarm_hour, _node_prefs->alarm_min);
    uint8_t fields[3];
    const int nf = clockFields(fields);
    if (nf) refresh_sensors();
    for (int k = 0; k < nf && n < 6; k++) {
      if (fields[k] == telemetry::MSGS && unread > 0) continue;   // already a row
      clockFieldValue(fields[k], add(nullptr, telemetry::LABEL[fields[k]]), sizeof(rows[0].value));
    }
    const int rows_h = n * step;
    if (calendar::height(d, ti) + d.getLineHeight() / 2 + rows_h <= bottom - y) {
      const int cw = W < 28 * d.getCharWidth() ? W : 28 * d.getCharWidth();   // as on the Clock page
      calendar::draw(d, (W - cw) / 2, y, cw, ti, alarmDays(ti));
      y += calendar::height(d, ti) + d.getLineHeight() / 2;
    }
    for (int k = 0; k < n && y + d.getLineHeight() <= bottom; k++, y += step) {
      int x = m;
      if (rows[k].ic) { miniIconDraw(d, x, y, *rows[k].ic); x += rows[k].ic->w * s + 2 * s; }
      info::valueRow(d, y, rows[k].label, rows[k].value, false, m - 2, x);
    }
  }

  // The Status page: four tiles split by dotted rules, each one subject's
  // headline (icon + main value) over a detail line. A tile with the room
  // (the 4.2" panel) adds a line and a chart of its history under it. Enter
  // opens the Status screen with all of it.
  void drawStatusTiles(DisplayDriver& d, int top) {
    const int W = d.width(), H = d.height() - top;
    const int lh = d.getLineHeight(), s = miniIconScale(d);
    const int mid_x = W / 2, mid_y = top + H / 2;
    const bool roomy = H / 2 >= 7 * lh;   // three lines and a chart a tile
    StatusScreen* st = (StatusScreen*)_task->statusScreen();
    d.setColor(DisplayDriver::LIGHT);
    info::vrule(d, mid_x, top, H);
    info::rule(d, 0, mid_y, W);
    // The tile's box, inside the rules.
    auto box = [&](int col, int row, int& x, int& y, int& w, int& h) {
      x = col ? mid_x + 3 : 1; y = row ? mid_y + 2 : top + 1;
      w = col ? W - x : mid_x - 2 - x;
      h = (row ? top + H : mid_y - 1) - y;
    };
    auto tile = [&](int col, int row, const MiniIcon* ic, float batt, const char* head, const char* detail,
                    const char* more = "") {
      int x, y, w, h;
      box(col, row, x, y, w, h);
      const int iy = y + (lh - info::batteryH(d)) / 2 - s;
      int tx = x;
      if (ic)            { miniIconDraw(d, x, iy, *ic); tx = x + ic->w * s + 2 * s; }
      else if (batt >= 0) { info::battery(d, x, iy, batt); tx = x + info::batteryW(d) + 2 * s; }
      d.drawTextEllipsized(tx, y, x + w - tx, head);
      d.drawTextEllipsized(x, y + lh + 2, w, detail);
      if (roomy && more[0]) d.drawTextEllipsized(x, y + 2 * (lh + 2), w, more);
    };
    // Under a roomy tile's three lines: a history.
    auto tileChart = [&](int col, int row, const info::History<StatusScreen::HIST_N>& hs, int min_span, bool bars) {
      int x, y, w, h;
      box(col, row, x, y, w, h);
      const int cy = y + 3 * (lh + 2) + 2, ch = y + h - cy - 3;
      if (ch >= 2 * lh) info::chart(d, x, cy, w - 4, ch, hs, min_span, bars, nullptr, nullptr);
    };
    char h[20], t[20], m[28];

    snprintf(h, sizeof(h), "%.3f", _node_prefs->freq);
    snprintf(t, sizeof(t), "SF%u %gk", (unsigned)_node_prefs->sf, _node_prefs->bw);
    const int nf = (int)radio_driver.getNoiseFloor();
    if (nf) snprintf(m, sizeof(m), "Noise %d dBm", nf); else m[0] = 0;
    tile(0, 0, &ICON_PG_RADIO, -1, h, t, m);
    if (roomy && st) tileChart(0, 0, st->noiseHistory(), 6, false);

#if ENV_INCLUDE_GPS == 1
    {
      LocationProvider* loc = sensors.getLocationProvider();
      const bool on = _task->getGPSState();
      t[0] = 0;
      if (!loc)               strcpy(h, "No GPS");
      else if (!on)           strcpy(h, "GPS off");
      else if (!loc->isValid()) strcpy(h, "No fix");
      else {
#ifdef STATUS_HAS_SKY
        GpsSky* g = skyview::sky();
        strcpy(h, g && g->fix_mode == 2 ? "2D fix" : "3D fix");
#else
        strcpy(h, "Fix");
#endif
      }
      if (loc && on) snprintf(t, sizeof(t), "%ld sats", loc->satellitesCount());
      int32_t la, lo;
      m[0] = 0;
      if (loc && on && loc->isValid() && _task->currentLocation(la, lo)) snprintf(m, sizeof(m), "%.4f %.4f", la / 1e6, lo / 1e6);
      tile(1, 0, &ICON_GPS, -1, h, t, m);
    }
#else
    tile(1, 0, &ICON_GPS, -1, "No GPS", "");
#endif

    const int mv = _task->getBattMilliVolts();
    const int pct = battery::percent(mv, _node_prefs ? (int)_node_prefs->low_batt_mv : 0);
    snprintf(h, sizeof(h), "%d%%", pct);
    snprintf(t, sizeof(t), "%d.%02d V%s", mv / 1000, (mv % 1000) / 10, board.isExternalPowered() ? " USB" : "");
    tile(0, 1, nullptr, pct / 100.0f, h, t);
    if (st && roomy) tileChart(0, 1, st->battHistory(), 20, false);
    else if (st) {   // the battery's trend beside its %
      const int x0 = 1 + info::batteryW(d) + 2 * s + d.getTextWidth(h) + 3 * s, x1 = mid_x - 3;
      if (x1 - x0 >= 12) info::spark(d, x0, mid_y + 2, x1 - x0, lh - 1, st->battHistory(), 20);
    }

    const int nc = the_mesh.getNumContacts();
    snprintf(h, sizeof(h), "%d node%s", nc, nc == 1 ? "" : "s");
    snprintf(t, sizeof(t), "%d in 1 h", StatusScreen::heardLastHour());
    snprintf(m, sizeof(m), "RX %lu  TX %lu", (unsigned long)StatusScreen::totalRx(), (unsigned long)StatusScreen::totalTx());
    tile(1, 1, &ICON_MAP_CONTACT, -1, h, t, m);
    if (roomy && st) tileChart(1, 1, st->trafficHistory(), 4, true);
  }

  // Small 5x5 glyph shown in the page-indicator row for each HomePage.
  static const MiniIcon* pageIcon(int page) {
    switch (page) {
      case CLOCK:      return &ICON_PG_CLOCK;
      case FAVOURITES: return &ICON_PG_STAR;
      case STATUS:     return &ICON_CHART;
      case BLUETOOTH:  return &ICON_PG_BT;
      case ADVERT:     return &ICON_PG_ADVERT;
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
    int anim_ms = 0;    // >0 while something animates: a page turn
    display.setTextSize(1);
    const int lh      = display.getLineHeight();  // line height at sz1
    const int step    = display.lineStep();        // lh + 2
    // Page-indicator row: small page icons (PAGE_ICON_PX) replace the old dots. Centre and
    // gap scale with the font so the band clears the header above and content
    // below (identical to the old lh+4 / +6 dots layout at 1x).
    const int pg_half   = (PAGE_ICON_PX * miniIconScale(display) + 1) / 2;
    const int dots_y    = lh + pg_half + 1;       // icon-row centre, below the header
    const int content_y = contentTop(display);    // first content row, below the icons
    const float slide   = slideProgress();        // -1, or how far a page turn has got

    // Title bar displaying node name (except on lock screen), status icons and battery.
    // Hidden on fullscreen pages (CLOCK).
    if (_page != CLOCK) {
      display.setColor(DisplayDriver::LIGHT);
      // The lock page has nothing on the left of the bar (its clock sits
      // below it), so the status icons may use the whole row.
      const int lock_reserve = _page == LOCK ? 0 : -1;
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
          if (display.width() >= 40 * display.getCharWidth())   // the 4.2": the date beside it
            snprintf(filtered_name + strlen(filtered_name), sizeof(filtered_name) - strlen(filtered_name),
                     "  %s %d %s", WDAY[lt.tm_wday], lt.tm_mday, MON[lt.tm_mon]);
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
      const int icon_w   = PAGE_ICON_PX * s;
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
        drawClockPage(display, unix_ts);
      }
    } else if (_page == HomePage::LOCK) {
      drawLockPage(display);
    } else if (_page == HomePage::STATUS) {
      drawStatusTiles(display, content_y);
    } else if (isQuickPage(_page) && !(_page == HomePage::SHUTDOWN && _shutdown_init)) {
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);
      QRow rows[QUICK_MAX];
      const int n = buildQuick(_page, rows);
      int q = drawQuickPanel(display, content_y, rows, n);
      if (_page == HomePage::TOOLS && _task->isTimerRunning()) q = 1000;   // the timer's countdown
      if (q > 0 && (mq_delay <= 0 || q < mq_delay)) mq_delay = q;
    } else if (_page == HomePage::MAP) {
      drawMapPage(display, content_y + 4);   // the gap under the page icons
    } else if (_page == HomePage::FAVOURITES) {
      // Grid of pinned contacts. Layout transposes to current orientation:
      // landscape → 3×2, portrait → 2×3. The selected card is filled.
      // No title — node name + battery (top bar) and the page-dots indicator above
      // serve as the page identity.
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(1);

      const int cols    = favCols(display);
      const int rows    = NodePrefs::FAVOURITES_COUNT / cols;
      const int grid_y  = content_y + 4;            // the gap under the page icons, as on every page
      const int grid_h  = display.height() - grid_y;
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
        const uint8_t* prefix = favSlotPrefix(i);
        // Each slot a card: a soft frame round a pinned one, a dotted outline
        // round a free one, filled while selected.
        const int tx = cx + 1, ty = cy, tw = cell_w - 2, th = cell_h - 2;
        display.setColor(DisplayDriver::LIGHT);
        if (sel)         { display.fillSoftRect(tx, ty, tw, th); display.setColor(DisplayDriver::DARK); }
        else if (prefix)   display.drawSoftRect(tx, ty, tw, th);
        else {
          for (int x = tx + 2; x < tx + tw - 1; x += 2) { display.fillRect(x, ty, 1, 1); display.fillRect(x, ty + th - 1, 1, 1); }
          for (int y = ty + 2; y < ty + th - 1; y += 2) { display.fillRect(tx, y, 1, 1); display.fillRect(tx + tw - 1, y, 1, 1); }
        }

        char    name[26];
        uint8_t unread   = 0;
        bool    overflow = false;
        bool    resolved = false;
        uint32_t last_ts = 0;                 // heard from / last post, for a tall card
        int32_t  plat = 0, plon = 0;          // a contact's advertised position
        const char* last_text = nullptr;      // the newest message, for a big card
        bool     last_out = false;

        if (prefix && _task->favouriteSlotKind(i) == NodePrefs::FAV_KIND_CHANNEL) {
          uint8_t ch_idx = prefix[0];
          ChannelDetails ch;
          if (the_mesh.getChannel(ch_idx, ch) && ch.name[0]) {
            // '#' marks a channel apart from a contact tile — the two share the
            // grid and Enter does something different on each.
            name[0] = '#';
            display.translateUTF8ToBlocks(name + 1, chanctl::bareName(ch.name), sizeof(name) - 1);
            unread   = _task->getChannelUnread(ch_idx);
            overflow = unread > 0 && _task->getChannelUnreadOverflow(ch_idx);
            resolved = true;
            MessageHistory& h = _task->core().history;
            for (int j = 0; j < h.chHistCount(); j++) {
              const ChHistEntry& e = h.chAtPos(h.chHistPosNewest(j));
              if (e.ch_idx == ch_idx) { last_ts = e.timestamp; last_text = e.text; break; }
            }
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
              last_ts  = c.lastmod;
              plat = c.gps_lat; plon = c.gps_lon;
              MessageHistory& h = _task->core().history;
              const int rp = h.dmHistEntryForContact(c.id.pub_key, 0);
              if (rp >= 0) { last_text = h.dmAtPos(rp).text; last_out = h.dmAtPos(rp).outgoing; }
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
          // A tall card (e-ink) has the name at the top and lines of detail
          // under it: when it was last heard (a channel: its last post) and,
          // for a contact with a position, how far away it is.
          const bool tall = th >= 3 * line_h + 6;
          int name_y     = tall ? ty + 3 : ty + (th - line_h) / 2;
          int name_max_w = tw - 6 - bw;
          if (name_max_w < 6) name_max_w = 6;
          int r = display.drawTextEllipsized(tx + 3, name_y, name_max_w, name, sel);
          if (sel && r > 0) mq_delay = r;
          if (unread > 0)
            display.drawUnreadBadge(tx + tw - 3, name_y, unread, sel, overflow);
          // A card six lines tall on a screen 40 characters wide (the 4.2"):
          // the newest message under the name, how long ago and how far along
          // the bottom.
          if (th >= 6 * line_h && display.width() >= 40 * display.getCharWidth()) {
            display.setColor(sel ? DisplayDriver::DARK : DisplayDriver::LIGHT);
            const int by = ty + th - line_h - 3;   // the bottom line
            const uint32_t now = rtc_clock.getCurrentTime();
            char buf[16];
            if (last_ts) {
              geo::fmtAgeShort(buf, sizeof(buf), now, last_ts);
              display.setCursor(tx + 3, by); display.print(buf);
            }
            int32_t mla, mlo;
            if ((plat || plon) && _task->currentLocation(mla, mlo)) {
              geo::fmtDist(buf, sizeof(buf), geo::haversineKm(mla, mlo, plat, plon), _task->useImperial(), true);
              display.drawTextRightAlign(tx + tw - 3, by, buf);
            }
            const int my = name_y + line_h + line_h / 3;   // the message, wrapped
            const int nl_max = (by - 2 - my) / line_h;
            if (nl_max > 0) {
              char pv[MSG_TEXT_BUF + 4], tr_pv[MSG_TEXT_BUF + 4];
              if (last_text) snprintf(pv, sizeof(pv), "%s%s", last_out ? "Me: " : "", last_text);
              else           snprintf(pv, sizeof(pv), "No messages");
              display.translateUTF8ToBlocks(tr_pv, pv, sizeof(tr_pv));
              char lines[5][FS_CHARS_MAX];
              const int cap = nl_max < 4 ? nl_max : 4;
              int n = FullscreenMsgView::wrapLines(display, tr_pv, tw - 6, lines, cap + 1);
              if (n > cap) {   // more than fits: the last line shown ends in an ellipsis
                n = cap;
                const size_t l = strlen(lines[n - 1]);
                if (l + 4 < sizeof(lines[0])) strcat(lines[n - 1], " ...");
              }
              for (int k = 0; k < n; k++) display.drawTextEllipsized(tx + 3, my + k * line_h, tw - 6, lines[k]);
            }
          } else if (tall) {
            display.setColor(sel ? DisplayDriver::DARK : DisplayDriver::LIGHT);   // the badge leaves its own
            const int s = miniIconScale(display), gap = line_h / 3;
            int y = name_y + line_h + gap;
            auto detail = [&](const MiniIcon& ic, const char* text) {
              if (y + line_h > ty + th - 2) return;
              miniIconDraw(display, tx + 3, y, ic);
              const int x = tx + 3 + ic.w * s + 2 * s;
              display.drawTextEllipsized(x, y, tx + tw - 3 - x, text);
              y += line_h + gap;
            };
            char buf[16];
            const uint32_t now = rtc_clock.getCurrentTime();
            if (last_ts) {
              geo::fmtAgeShort(buf, sizeof(buf), now, last_ts);
              detail(prefix && _task->favouriteSlotKind(i) == NodePrefs::FAV_KIND_CHANNEL ? ICON_PG_MSG : ICON_PG_CLOCK, buf);
            }
            int32_t mla, mlo;
            if ((plat || plon) && _task->currentLocation(mla, mlo)) {
              // The distance after an arrow towards it (north up).
              geo::fmtDist(buf, sizeof(buf), geo::haversineKm(mla, mlo, plat, plon), _task->useImperial(), true);
              detail(*ICON_ARROWS[((geo::bearingDeg(mla, mlo, plat, plon) + 22) % 360) / 45], buf);
            }
          }
        } else {
          display.drawTextCentered(tx + tw / 2, ty + (th - line_h) / 2, "+");
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
      display.drawTextCentered(display.width() / 2, content_y + step, "hibernating...");   // the panel's Hibernate row
    }
    if (slide >= 0) {   // page turn: the page left behind slides out, this one in
      display.slideCompose(_slide_dir * (int)(display.width() * slide + 0.5f));
      anim_ms = 15;
    }
    if (Features::IS_EINK) {
      // slow display: poll every 30 s; inbound msgs force immediate refresh via notify()
      return (mq_delay > 0 && mq_delay < Features::HOME_REFRESH_MS) ? mq_delay : Features::HOME_REFRESH_MS;
    }
    if (_page == HomePage::CLOCK) {
      bool show_sec = !_node_prefs || !_node_prefs->clock_hide_seconds;
      int ret = show_sec ? 1000 : 60000;
      if (anim_ms > 0 && anim_ms < ret) ret = anim_ms;
      return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret;
    }
    int ret = 5000;
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
        }
        if (res != PopupMenu::NONE) _pin_target_slot = -1;
        return true;
      }
      DisplayDriver* d = _task->getDisplay();
      const int cols = d ? favCols(*d) : 2;
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

    if (isQuickPage(_page) && _quick_edit) {
      // Changing a bar: shown live, saved once on the way out.
      if ((c == KEY_LEFT || c == KEY_RIGHT) && _node_prefs) {
        QRow rows[QUICK_MAX];
        const int n = buildQuick(_page, rows);
        const QAct act = n > 0 ? rows[_quick_sel < n ? _quick_sel : n - 1].act : QA_NONE;
        const int d = c == KEY_RIGHT ? 1 : -1;
        if (act == QA_BRIGHTNESS) {
          uint8_t& b = _node_prefs->display_brightness;
          const uint8_t nb = (uint8_t)constrain((int)b + d, 0, 4);
          if (nb != b) { b = nb; _quick_dirty = true; _task->applyDisplayPrefs(); }
        } else if (act == QA_AUTO_ADVERT) {
          const int i = constrain(AutoAdvertScreen::indexOf(_node_prefs->advert_auto_interval_sec) + d,
                                  0, AutoAdvertScreen::OPT_COUNT - 1);
          if (_node_prefs->advert_auto_interval_sec != AutoAdvertScreen::OPTS[i]) {
            _node_prefs->advert_auto_interval_sec = AutoAdvertScreen::OPTS[i];
            _quick_dirty = true;
          }
        }
        return true;
      }
      if (c == KEY_ENTER || c == KEY_CANCEL) {
        _quick_edit = false;
        _task->savePrefsIfDirty(_quick_dirty);
        _quick_dirty = false;
        return true;
      }
      return true;   // nothing else while editing
    }
    if (isQuickPage(_page)) {
      QRow rows[QUICK_MAX];
      const int n = buildQuick(_page, rows);
      if (c == KEY_UP)   { if (_quick_sel > 0) _quick_sel--; return true; }
      if (c == KEY_DOWN) { if (_quick_sel + 1 < n) _quick_sel++; return true; }
      if (c == KEY_ENTER && n > 0) { quickAct(_page, rows[_quick_sel < n ? _quick_sel : n - 1]); return true; }
    }
    if (c == KEY_LEFT || c == KEY_PREV) {
      turnPage(-1);
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      turnPage(+1);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::STATUS) {
      _task->gotoStatusScreen(StatusScreen::TAB_RADIO);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::MAP) {
      _task->gotoMapScreen();
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
  if (node_prefs) battery::useCurve(node_prefs->batt_curve_mv);   // Settings > Battery curve
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
  batt_curve_screen = new BatteryCurveScreen(this, node_prefs);
  live_share_screen = new LiveShareScreen(this, node_prefs);
  locator_screen  = new LocatorScreen(this, node_prefs);
  trail_screen       = new TrailScreen(this, &_core->trail.store());
  compass_screen     = new CompassScreen(this);
#if ENV_INCLUDE_GPS == 1 && defined(GPS_SKYVIEW)
#endif
  status_screen      = new StatusScreen(this);
  repeater_screen    = new RepeaterScreen(this);
  clock_tools        = new ClockToolsScreen(this, node_prefs);
#if defined(PIN_GPIO1)
  gpio_screen        = new GpioScreen(this, node_prefs);
#endif
  startCardKBCapture();   // after every screen: an optional task must not starve one
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
void UITask::gotoToolsScreen()     { _tool_from_home = false; setCurrScreen(tools_screen); }
void UITask::leaveTool()           { if (_tool_from_home) gotoHomeScreen(); else gotoToolsScreen(); }
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
void UITask::gotoStatusScreen(uint8_t tab) {
  ((StatusScreen*)status_screen)->showTab(tab);
  setCurrScreen(status_screen);
}
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
void UITask::gotoBatteryCurve() { setCurrScreen(batt_curve_screen); }

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

#ifdef SIM_PLATFORM
void UITask::simMessage(const char* text) {
  UiEvent ev = {};
  ev.type = UiEventType::MessageArrived;
  ev.kind = UIEventType::channelMessage;
  ev.idx = 0;
  snprintf(ev.text, sizeof(ev.text), "%s", text);
  onMessageArrived(ev);
}

// Sim tests: read and new messages (UiCore::simUnreadDemo), then channel 0
// (which 0) or the first chat contact (1) opened as from the list.
void UITask::simUnread(int which) {
  if (which < 0) { _core->simUnreadDemo(); return; }
  if (which == 0) { openChannelHistory(0); return; }
  for (int i = 0; i < the_mesh.getNumContacts(); i++) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) && c.type == ADV_TYPE_CHAT) { openContactDM(c); return; }
  }
}

// Sim tests: a GPS fix in Kraków and eight nodes around it, then Nodes.
void UITask::simNodes() {
  sim_location_provider().set(50.0614f, 19.9372f);
  static const struct { const char* name; float dla, dlo; uint8_t type; } N[] = {
    { "Kasia-T1", 0.012f, 0.020f, ADV_TYPE_CHAT },  { "Wawel rpt", -0.006f, -0.004f, ADV_TYPE_REPEATER },
    { "Kopiec", 0.004f, -0.045f, ADV_TYPE_REPEATER }, { "Tomek", 0.0005f, 0.0006f, ADV_TYPE_CHAT },
    { "Nowa Huta", 0.020f, 0.110f, ADV_TYPE_REPEATER }, { "Room Rynek", 0.0008f, 0.0004f, ADV_TYPE_ROOM },
    { "Ola", -0.030f, 0.015f, ADV_TYPE_CHAT },        { "Bielany", 0.000f, -0.080f, ADV_TYPE_REPEATER },
  };
  const uint32_t now = rtc_clock.getCurrentTime();
  for (int i = 0; i < (int)(sizeof(N) / sizeof(N[0])); i++) {
    uint8_t key[PUB_KEY_SIZE];
    for (int k = 0; k < PUB_KEY_SIZE; k++) key[k] = (uint8_t)(0x5A ^ (i * 37 + k * 11));
    the_mesh.addDiscoveredContact(key, N[i].name, N[i].type);
    if (ContactInfo* c = the_mesh.lookupContactByPubKey(key, PUB_KEY_SIZE)) {
      c->gps_lat = (int32_t)((50.0614f + N[i].dla) * 1e6f);
      c->gps_lon = (int32_t)((19.9372f + N[i].dlo) * 1e6f);
      c->lastmod = now - i * 600;
    }
  }
  setCurrScreen(nearby_screen);
}

void UITask::simDisplay(bool on) {
  if (!_display) return;
  if (on) _display->turnOn(); else _display->turnOff();
  _next_refresh = 0;
}

void UITask::simCompose(const char* text) {
  gotoMessagesScreen();
  ((MessagesScreen*)messages_screen)->simCompose(text);
}
#endif

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

// On screen: a conversation left open under the lock or a dark display isn't read.
bool UITask::messagesInView() const {
  return curr == messages_screen && _display && _display->isOn() && !_locked;
}
bool UITask::isViewingChannel(uint8_t channel_idx) {
  return messagesInView() && ((MessagesScreen*)messages_screen)->isViewingChannel(channel_idx);
}
bool UITask::isViewingDM(const uint8_t* pub_key) {
  return messagesInView() && ((MessagesScreen*)messages_screen)->isViewingDM(pub_key);
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
  if (!alertShowing() || _alert_badge) _alert_t0 = millis();   // a banner already down only changes its text
  _alert_badge = false;
  _alert_expiry = (millis() + duration_millis) | 1;   // 0 means none
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
  const uint8_t style = _node_prefs ? _node_prefs->msg_alert : NodePrefs::MSG_ALERT_NORMAL;
  if (!in_view && style == NodePrefs::MSG_ALERT_COMPACT) {
    // The envelope counts the run: each message adds one and blinks it again.
    _alert_count = alertShowing() && _alert_badge ? _alert_count + 1 : 1;
    _alert_badge = true;
    _alert_t0 = millis();
    _alert_expiry = (millis() + BADGE_MS) | 1;
  } else if (!in_view && style == NodePrefs::MSG_ALERT_NORMAL) {
    char alert_buf[80];
    snprintf(alert_buf, sizeof(alert_buf), "Msg: %.20s", ev.text);
    showAlert(alert_buf, 2000);
  }

  if (_display != NULL && !_locked) {
    bool wake_disabled = _node_prefs && (!_node_prefs->msg_wake || inQuietHours(*_node_prefs, rtc_clock.getCurrentTime()));
    if (!wake_disabled && !_display->isOn() && !isClientConnected()) {   // wake for the msg unless an app (BLE/USB) is already showing it, or the user disabled msg-wake
      _display->turnOn();
    }
    if (_display->isOn()) {
      uint32_t aoff = autoOffMillis();
      if (aoff > 0) _auto_off = millis() + aoff;
      _next_refresh = 0;
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

// The alert as a banner over the top edge: it slides down, stays, slides back
// up (e-ink just appears and goes), so what is on screen below it stays
// readable. Long text used to be drawn as one drawTextCentered line that
// overflowed the border on both sides (e.g. "GPS on, tracking started" is
// already wider than a 128 px OLED); it wraps to up to three lines inside the
// banner instead. Uses the shared wrap scratch (s_wrap_*) -- single-threaded
// render path, same contract as the message views.
void UITask::renderAlertOverlay() {
  if (_alert_badge) { renderMsgBadge(); return; }
  _display->setTextSize(1);
  const int lh    = _display->getLineHeight();
  const int pad   = 3;
  const int W     = _display->width();
  _display->translateUTF8ToBlocks(s_wrap_trans, _alert, sizeof(s_wrap_trans));
  int nl = FullscreenMsgView::wrapLines(*_display, s_wrap_trans, W - pad * 2 - 2, s_wrap_lines, 3);
  if (nl < 1) nl = 1;
  const int h = nl * lh + pad * 2;
  // How far down it is, in percent: eased out over MOVE ms in and out.
  const int MOVE = 150;
  int k = 100;
  if (!_display->isEink()) {
    const int el = (int)(millis() - _alert_t0), rem = (int)(_alert_expiry - millis());
    if (el < MOVE) k = el * 100 / MOVE;
    if (rem < MOVE && rem * 100 / MOVE < k) k = rem * 100 / MOVE;
    if (k < 0) k = 0;
    k = 100 - (100 - k) * (100 - k) / 100;
  }
  const int y = -h + h * k / 100;
  _display->setColor(DisplayDriver::DARK);
  _display->fillRect(0, y, W, h);
  _display->setColor(DisplayDriver::LIGHT);
  _display->drawRect(0, y, W, h);
  for (int i = 0; i < nl; i++)
    _display->drawTextCentered(W / 2, y + pad + i * lh, s_wrap_lines[i]);
}

// Settings > Message alert Compact: an envelope and how many messages came. On
// Home it sits among the status icons (HomeScreen::renderBatteryIndicator);
// it blinks for a while after each one (not on e-ink), then holds till it goes.
void UITask::renderMsgBadge() {
  if (_badge_in_bar) return;   // among the Home status icons already
  // Elsewhere in the header's line, at its right, clear of the line under it.
  DisplayDriver& d = *_display;
  const int s = miniIconScale(d), h = d.headerH() - d.sepH(), w = mailCountWidth(d, _alert_count);
  const int x = d.width() - w - 2 * s;
  d.setColor(DisplayDriver::DARK);
  d.fillRect(x - 2 * s, 0, w + 4 * s, h);
  d.setColor(DisplayDriver::LIGHT);
  if (msgBadgeLit()) drawMailCount(d, x, 0, h, _alert_count);
}

void UITask::setCurrScreen(UIScreen* c) {
  // Fail safe on a null target: a screen pointer left uninitialised (member
  // declared + navigator wired, but the `new XScreen()` line forgotten in
  // begin()) stays nullptr thanks to the in-class initialisers. Bail here so
  // that mistake is an inert no-op instead of a null deref in render()/poll().
  if (!c) return;
  curr = c;
  c->onShow();          // central per-visit reset hook (see UIScreen::onShow)
  _next_refresh = 0;
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
  _next_refresh = 0;
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
  { NodePrefs* np = the_mesh.getNodePrefs(); in.gps_on = np && np->gps_enabled; }   // off: no stale satellite count
#endif
  if (telemetry::isSensor(field)) {
    if (!lpp) {
      static CayenneLPP s_lpp(200); s_lpp.reset();
      { CardKBBusLock bus_lock; sensors.querySensors(0xFF, s_lpp); }
      lpp = &s_lpp;
    }
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

// Sim tests: a channel post's keyboard, holding `text`.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_compose(const char* text) {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->simCompose(text);
}

// Sim tests: -1 files read and new messages, 0 opens channel 0, 1 the first DM.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_unread(int which) {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->simUnread(which);
}

extern "C" EMSCRIPTEN_KEEPALIVE void sim_nodes() {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->simNodes();
}

// Sim tests: the display off (0) or on (1), as the auto-off and a key press do.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_display(int on) {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->simDisplay(on != 0);
}

// Sim tests: a channel message arrives, alerted in Settings > Message alert's
// style (0 Normal, 1 Compact, 2 Off).
extern "C" EMSCRIPTEN_KEEPALIVE void sim_message(const char* text, int style) {
  if (!g_sim_ui_task_for_js) return;
  if (NodePrefs* p = g_sim_ui_task_for_js->getNodePrefs()) p->msg_alert = (uint8_t)style;
  g_sim_ui_task_for_js->simMessage(text);
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
// the real on-screen "Buzzer: On/Off" alert -- so muting from the host
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
  // First, keys the background task captured while the display blocked, in order.
  while (_cardkb_q_tail != _cardkb_q_head) {
    uint8_t q = _cardkb_q[_cardkb_q_tail];
    _cardkb_q_tail = (uint8_t)((_cardkb_q_tail + 1) % CARDKB_Q_LEN);
    handleCardKBByte(q);
  }
  uint8_t raw;
  if (!readCardKBEdge(raw)) return;
  handleCardKBByte(raw);
#endif
}

#if defined(CARDKB_I2C)
bool UITask::readCardKBEdge(uint8_t& raw) {
  CARDKB_I2C.requestFrom(0x5F, 1);
  if (!CARDKB_I2C.available()) return false;
  raw = CARDKB_I2C.read();
  if (raw == _cardkb_last_raw) return false;   // still held (or still released) -- no new edge
  _cardkb_last_raw = raw;
  return raw != 0;   // 0 = key just released, nothing to enqueue
}

// Single producer (the poll task) / single consumer (pollCardKB() in the
// loop) ring -- byte-sized indices, so no locking needed.
void UITask::cardkbQueuePush(uint8_t raw) {
  uint8_t next = (uint8_t)((_cardkb_q_head + 1) % CARDKB_Q_LEN);
  if (next == _cardkb_q_tail) return;   // full: drop (16 keys within one refresh)
  _cardkb_q[_cardkb_q_head] = raw;
  _cardkb_q_head = next;
}

#if defined(NRF52_PLATFORM)
void UITask::cardkbTask(void* self) {
  UITask* t = (UITask*)self;
  SemaphoreHandle_t m = (SemaphoreHandle_t)t->_cardkb_mutex;
  for (;;) {
    if (t->_cardkb_bg_active && xSemaphoreTake(m, portMAX_DELAY) == pdTRUE) {
      if (t->_cardkb_bg_active) {          // re-check: loop may have just finished
        uint8_t raw;
        if (t->readCardKBEdge(raw)) t->cardkbQueuePush(raw);
      }
      xSemaphoreGive(m);
    }
    vTaskDelay(ms2tick(10));   // a keypress lasts ~50+ ms, so 10 ms never misses one
  }
}
#endif
#endif

// Keep sampling the CardKB while the display blocks the loop, so keys typed
// during a (long, on big panels) refresh aren't lost. Called LAST in begin():
// the heap is tight on large-panel builds, and every screen is allocated with
// `new` -- a failed allocation leaves that screen silently unopenable. Creating
// the optional poll task afterwards means a short heap costs the task (the
// CardKB is then only read between frames, as without it), never a screen.
void UITask::startCardKBCapture() {
#if defined(CARDKB_I2C) && defined(NRF52_PLATFORM)
  if (!_has_cardkb) return;
  SemaphoreHandle_t m = xSemaphoreCreateMutex();
  if (!m) return;
  // Publish the mutex BEFORE creating the task: at higher priority it starts
  // running inside xTaskCreate() and reads _cardkb_mutex straight away.
  _cardkb_mutex = (void*)m;
  // TASK_PRIO_NORMAL (2) preempts the loop task (TASK_PRIO_LOW, 1); BLE runs higher.
  if (xTaskCreate(&UITask::cardkbTask, "cardkb", 512, this, TASK_PRIO_NORMAL, NULL) == pdPASS) {
    g_cardkb_bus_mutex = m;   // shared with CardKBBusLock (sensor queries)
    return;
  }
  _cardkb_mutex = nullptr;    // no task -> setCardKBBackground() stays a no-op
  vSemaphoreDelete(m);
#endif
}

// Hand CardKB sampling to the background task (on = loop is about to block on
// the display) or take it back (off). Taking the mutex on the way out waits for
// any in-flight background I2C read to finish before the loop touches the bus.
void UITask::setCardKBBackground(bool on) {
#if defined(CARDKB_I2C) && defined(NRF52_PLATFORM)
  if (!_cardkb_mutex) return;
  if (on) {
    _cardkb_bg_active = true;
  } else {
    SemaphoreHandle_t m = (SemaphoreHandle_t)_cardkb_mutex;
    xSemaphoreTake(m, portMAX_DELAY);
    _cardkb_bg_active = false;
    xSemaphoreGive(m);
  }
#else
  (void)on;
#endif
}

void UITask::handleCardKBByte(uint8_t raw) {
#if defined(CARDKB_I2C)

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
  enum Open : uint8_t { NONE, HOME, SETTINGS, MESSAGES, TOOLS, NEARBY, TRAIL, MAP, COMPASS, STATUS,
                        LIVE, LOCATOR, ADVERT, REPEATER, BOT, CLOCKTOOLS, CHANNEL0 };
  struct Step { const char* name; Open open; const char* keys; uint16_t dwell_ms; };
  static const Step STEPS[] = {
    {"boot-idle", NONE, "", 3000},
    {"home", HOME, "", 2000},
    {"home-R1", NONE, "R", 1500}, {"home-R2", NONE, "R", 1500}, {"home-R3", NONE, "R", 1500},
    {"home-R4", NONE, "R", 1500}, {"home-R5", NONE, "R", 1500}, {"home-R6", NONE, "R", 1500},
    {"home-R7", NONE, "R", 1500}, {"home-R8", NONE, "R", 1500},
    {"home-R9", NONE, "R", 1500}, {"home-R10", NONE, "R", 1500}, {"home-R11", NONE, "R", 1500},
    {"home-R12", NONE, "R", 1500}, {"home-R13", NONE, "R", 1500},
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
    {"compass", COMPASS, "", 3000},
    {"st-radio", STATUS, "", 3000}, {"st-radio-dn", NONE, "DD", 2000}, {"st-gps", NONE, "UUR", 3000},
    {"st-gps-dn", NONE, "DDD", 2000}, {"st-sky", NONE, "UUUR", 4000}, {"st-signal", NONE, "E", 3000},
    {"st-power", NONE, "R", 2000}, {"st-mesh", NONE, "R", 2000}, {"st-mesh-dn", NONE, "DDD", 2000},
    {"st-system", NONE, "UUUR", 2000}, {"st-system-dn", NONE, "DDDDDD", 2000},
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
        Serial.printf("SIZE UiCore %u Splash %u Home %u Settings %u Messages %u Tools %u Ringtone %u Bot %u Admin %u Nearby %u Dash %u AutoAdv %u LiveSh %u Locator %u Trail %u Compass %u Status %u Repeater %u ClockTools %u Kbd %u UITask %u StaticPool32 %u Packet %u\n",
          sizeof(UiCore), sizeof(SplashScreen), sizeof(HomeScreen), sizeof(SettingsScreen), sizeof(MessagesScreen), sizeof(ToolsScreen),
          sizeof(RingtoneEditorScreen), sizeof(BotScreen), sizeof(AdminScreen), sizeof(NearbyScreen), sizeof(DashboardConfigScreen),
          sizeof(AutoAdvertScreen), sizeof(LiveShareScreen), sizeof(LocatorScreen), sizeof(TrailScreen), sizeof(CompassScreen),
          sizeof(StatusScreen), sizeof(RepeaterScreen), sizeof(ClockToolsScreen), sizeof(KeyboardWidget), sizeof(UITask),
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
          case perfl1::STATUS: gotoStatusScreen(StatusScreen::TAB_RADIO); break;
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
  {  // a conversation in view again (display on, unlocked): on to what came in meanwhile
    bool on = messagesInView();
    if (on && !_display_was_on) ((MessagesScreen*)messages_screen)->onDisplayOn();
    _display_was_on = on;
  }
  if (status_screen) ((StatusScreen*)status_screen)->sample();   // Status history lines, once a minute
#if UI_HAS_JOYSTICK
  uint8_t joy_rot = _node_prefs ? _node_prefs->joystick_rotation : JOYSTICK_ROTATION;
#if FEAT_DISPLAY_ROTATION_SETTING && defined(JOYSTICK_UPRIGHT_ROTATION)
  if (_node_prefs) joy_rot += (JOYSTICK_UPRIGHT_ROTATION - _node_prefs->display_rotation) & 3;
#endif
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
      _next_refresh = 0;  // redraw immediately after key press
    } else if (!_locked && curr) {
      // Apply the whole queued burst, then redraw once — N taps captured during
      // a blocking refresh become N navigation steps at the cost of one refresh.
      char k;
      while (dequeueKey(k)) curr->handleInput(k);
      { uint32_t aoff = autoOffMillis(); if (aoff > 0) _auto_off = millis() + aoff; }  // extend auto-off timer
      // Note timing no longer depends on render cadence (TIMER1 IRQ advances
      // notes directly — see buzzer.cpp), so a redraw right after a keypress
      // can't clip a note; no need to hold it back while buzzer.isPlaying().
      _next_refresh = 0;  // trigger refresh immediately
    } else {
      _kq_head = _kq_tail = 0;  // locked or no screen: eat all queued keys
      // Locked: wake window is set only when display first turns on
      if (_locked) { _lock_hint_ms = millis(); _next_refresh = 0; }   // the hint; not on the waking press, so a glance shows the data
    }
  }

  userLedHandler();

#ifdef PIN_BUZZER
  if (soundctl::tick(_node_prefs, buzzer, isClientConnected(), rtc_clock.getCurrentTime()))   // BLE bonded or an open USB port
    _next_refresh = 0;
  if (buzzer.isPlaying())  buzzer.loop();
#endif

  if (curr) curr->poll();

  // A cable plugged in or pulled out: redraw at once, so the bolt by the
  // battery follows it instead of waiting for the screen's next refresh.
  { static bool ext = board.isExternalPowered();
    const bool now = board.isExternalPowered();
    if (now != ext) { ext = now; _next_refresh = 0; } }

  if (_display != NULL && _display->isOn()) {
    setCardKBBackground(true);   // keep sampling the keyboard while the frame blocks
    // Lock-screen password prompt
    if (_locked && _unlock_kb && (int32_t)(millis() - _lock_wake_until) >= 0) {
      cancelUnlockPrompt(); // Cancel unlock attempt on idle
      _next_refresh = 0;
    }
    if (_locked && !_unlock_kb && (int32_t)(millis() - _lock_wake_until) >= 0) {
      _display->turnOff();
    } else if (_locked && _unlock_kb && refreshDue()) {
      // While the prompt is up the password keyboard replaces the lockscreen view
      PERF_T0();
      _display->startFrame();
      _badge_in_bar = false;
      _kb.beginFrame();
      int delay_millis = _kb.render(*_display);
      if (alertShowing()) renderAlertOverlay();   // "Wrong PIN", and a ringing alarm
      PERF_T1();
      _display->endFrame();
      PERF_T2();
      _next_refresh = millis() + delay_millis;
      alertFrameCap();
    } else if (_locked && refreshDue() && home) {
      PERF_T0();
      _display->startFrame();
      _badge_in_bar = false;
      if (curr && curr != home && (millis() - ui_started_at < BOOT_SCREEN_MILLIS)) {
        // Boot splash is still up on a boot-locked device
        _next_refresh = millis() + curr->render(*_display);
      } else {
        home->render(*_display);
        _next_refresh = millis() + Features::LOCKSCREEN_REFRESH_MS;
      }
      // Alert overlay on top — without this a ringing alarm on a locked device
      // played its melody against a screen that never said what was ringing.
      if (alertShowing()) {
        renderAlertOverlay();
        alertFrameCap();
      }
      PERF_T1();
      _display->endFrame();
      PERF_T2();
    } else if (!_locked && refreshDue() && curr) {
      PERF_T0();
      _display->startFrame();
      _badge_in_bar = false;
      _kb.beginFrame();
      int delay_millis = curr->render(*_display);
      // Skip the alert overlay (new-message toast) while the keyboard is the
      // thing actually on screen this frame -- it's shared across Messages/
      // Bot/Settings/Admin/etc., so this covers every screen that uses it for
      // full-screen text entry, not just message compose. Otherwise a message
      // arriving mid-typing blanks out the letter grid for 3s with no way to
      // see what's being typed.
      if (alertShowing() && !_kb.isVisible()) {  // alert overlay on top of any (non-keyboard) screen
        renderAlertOverlay();
        // Keep refreshing the underlying screen at its own cadence (capped at the
        // alert's expiry) so layouts that settle over a frame — e.g. the message-
        // history scrollbar reserve — don't stay stuck behind the alert. Unchanged
        // frames are skipped by the display CRC, so e-ink isn't thrashed.
        _next_refresh = millis() + delay_millis;
        alertFrameCap();   // the slide or the blink, a frame at a time
        if ((int32_t)(_next_refresh - _alert_expiry) > 0) _next_refresh = _alert_expiry;
      } else {
        _next_refresh = millis() + delay_millis;
      }
      PERF_T1();
      _display->endFrame();
      PERF_T2();
    }
    setCardKBBackground(false);
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
        || (curr == status_screen && ((StatusScreen*)status_screen)->wantsLiveGps())
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
  the_mesh.noteUserInput();
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

void UITask::toggleGPS(bool announce) {
  if (_node_prefs) applyGpsState(_node_prefs->gps_enabled == 0, announce);
}

// Sets GPS to an absolute state (vs. toggleGPS()'s flip) -- shared by the
// Home-page manual toggle and the bot's !gps on/off command, which needs to
// set a specific state rather than flip whatever it currently is.
void UITask::applyGpsState(bool on, bool announce) {
  if (!_core->setGpsEnabled(on)) return;   // no GPS on this board
  notify(UIEventType::ack);
  if (announce) showAlert(on ? "GPS: Enabled" : "GPS: Disabled", 800);
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
#if FEAT_DISPLAY_ROTATION_SETTING && defined(DISPLAY_ROTATION)
    // Each build is laid out for one shape and only turns it upside down: a
    // rotation of the other shape (a fresh device's 0, another build's, or
    // from before the split) comes back to this build's own.
    uint8_t& rot = _node_prefs->display_rotation;
    if ((rot ^ DISPLAY_ROTATION) & 1) rot = DISPLAY_ROTATION & 3;
#endif
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
    showAlert(buzzer.isQuiet() ? "Buzzer: Off" : "Buzzer: On", 800);
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
  static const char* labels[] = { "Buzzer: On", "Buzzer: Off", "Buzzer: Auto" };
  showAlert(labels[mode], 800);
  _next_refresh = 0;
#endif
}
