#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#if defined(ESP32)
  #include <esp_heap_caps.h>
  // The UI runs on Arduino's loop task: LVGL's renderer, FAT on the card and
  // the map's decoders all nest on its stack, and the stock 8 KB left a few
  // hundred bytes at the deepest point (Diagnostics > Stack free, the lowest
  // ever) -- a screenshot's file write went past it and reset the device.
  SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif
#include <new>
#include <src/core/lv_obj_event_private.h>   // lv_cover_check_info_t (coversOwnArea)
#include <src/widgets/label/lv_label_private.h>   // lv_label_t (labelShows)
#include <src/misc/lv_area_private.h>   // lv_area_intersect (clockview::wheelDraw)
#include <stdarg.h>
#include <sys/stat.h>

// Flags: LVGL draws text one codepoint at a time, so a flag (a pair of regional
// indicator letters) would show as two letter tiles. Every label and span of
// this UI (all of it is this one file and the headers it includes) goes through
// these, which swap each pair for the codepoint fonts/ui_emoji.c keeps the
// flag's image under. Display only: what's stored and sent stays as it was.
// A label given the text it already shows is left alone: the status bar, the
// clocks and the live values are set every refresh, and each set is a redraw.
extern "C" char* ui_emoji_flags(const char* s);
static void ui_label_set_text(lv_obj_t* o, const char* t) {
  char* f = t ? ui_emoji_flags(t) : nullptr;   // t == NULL: LVGL's "redraw the current text"
  const char* s = f ? f : t;
  const char* cur = lv_label_get_text(o);
  if (!s || !cur || strcmp(cur, s) != 0) lv_label_set_text(o, s);
  lv_free(f);
}
static void ui_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void ui_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[160];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < (int)sizeof(buf)) { ui_label_set_text(o, buf); return; }
  char* big = (char*)lv_malloc(n + 1);
  if (!big) return;
  va_start(ap, fmt);
  vsnprintf(big, n + 1, fmt, ap);
  va_end(ap);
  ui_label_set_text(o, big);
  lv_free(big);
}
static void ui_span_set_text(lv_span_t* s, const char* t) {
  char* f = ui_emoji_flags(t);
  lv_span_set_text(s, f ? f : t);
  lv_free(f);
}
#define lv_label_set_text ui_label_set_text
#define lv_label_set_text_fmt ui_label_set_text_fmt
#define lv_span_set_text ui_span_set_text

// A zeroed buffer of `n` T in PSRAM when the board has it: internal RAM is what
// BLE, WiFi and TLS need. Allocated once (at startup or first use), never freed.
template <class T> static T* psramBuf(size_t n) {
#if defined(ESP32)
  if (void* p = heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return (T*)p;
#endif
  return (T*)calloc(n, sizeof(T));
}

static bool s_chats_stale = false;   // Messages: a refresh waits for a held row to be let go

#include "../ui-core/UiCore.h"   // shared UI Core (header-only, this TU)
#include "../ui-core/NearbyModel.h"
#include "../ui-core/EtaTracker.h"
#include "../ui-core/SettingsSchema.h"
#include "../ui-core/SettingsCli.h"   // the companion CLI's settings keys
#include "../ui-core/ContactQr.h"
#include "../ui-core/GpsAverager.h"
#include "../ui-core/TrackBack.h"
#include "../ui-core/TrailProfile.h"
#include "../ui-core/StatusHistory.h"
#include "../ui-core/CalendarMath.h"
#include "../ui-core/RadioControl.h"
#include "../ui-core/RepeaterControl.h"
#include "../ui-core/Diagnostics.h"
#include "../ui-core/Battery.h"
#include "../ui-core/Telemetry.h"
#include "../ui-core/ChannelControl.h"
#include "../ui-core/BotConfig.h"
#include "../ui-core/SoundControl.h"
#include "../ui-core/SoundNotifier.h"
#include "../ui-core/MessageText.h"
#include "../ui-core/SiteQr.h"
#include "Theme.h"
#include "Anim.h"
#include "LvglPort.h"
#include "lv_mem_psram.h"
#include "HistoryStore.h"
#include "map/MapLayers.h"
#include "map/LiveCache.h"
static histstore::SdArchive s_archive;   // message history on the SD card
namespace storeview { static void stopWalk(); }   // StorageScreen.h
#include "../ui-core/KeyboardData.h"
#include "Keyboard.h"

static const char* waypointsFull() {
  static char t[24];
  snprintf(t, sizeof(t), "Waypoints full (%d)", WaypointStore::CAPACITY);
  return t;
}

// ui-lvgl skeleton (docs/solo/developer/ui-core.md, step 5): status bar, home,
// conversation list, contact picker, conversation view with compose. Every
// piece of state it shows comes from the UI Core; this file only draws it.

static UITask* s_ui = nullptr;   // for LVGL's C callbacks
static bool s_hdr_fit;           // the title bar's contents changed: fitHeader() before the next frame

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// The board's one button, pressed from the simulator page (web/lvgl.html).
static bool s_sim_btn_click = false, s_sim_btn_hold = false, s_sim_wake = false;
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button() { s_sim_btn_click = true; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button_hold() { s_sim_btn_hold = true; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_wake_button() { s_sim_wake = true; }
static bool s_sim_shot = false;   // the side + top button combo
extern "C" EMSCRIPTEN_KEEPALIVE void sim_screenshot() { s_sim_shot = true; }
// The speaker, polled every frame by the page's Web Audio oscillator.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_is_playing() { return s_ui && s_ui->isBuzzerPlaying() ? 1 : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_freq_hz() { return s_ui ? (int)s_ui->buzzerFreqHz() : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_get_volume() { return s_ui ? (int)s_ui->buzzerVolume() : 0; }
#endif

#include "Widgets.h"

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void UITask::begin(DisplayDriver* display_drv, SensorManager* sensors, NodePrefs* node_prefs) {
  s_ui = this;
  _ui_started_ms = millis();
  _display = display_drv;
  _sensors = sensors;
  _prefs = node_prefs;
  s_prefs = node_prefs;
  if (node_prefs) battery::useCurve(node_prefs->batt_curve_mv);   // Settings > Battery curve

  _core = new UiCore();
  _core->begin(node_prefs, sensors, this);
  _core->flow.keepHistory();   // the map's recent packets, to play again
  {  // history kept on the SD card: back into the ring, then every new entry to it
    int hk = lvport::loadHistKeep();
    s_archive.setKeep(histstore::KEEP[hk >= 0 && hk < histstore::KEEP_COUNT ? hk : histstore::KEEP_DEFAULT]);
    int lc = lvport::loadLiveCap();
    mapview::s_live_cache.setLimit((uint64_t)mapview::LIVE_CAP_MB[lc >= 0 && lc < mapview::LIVE_CAP_COUNT ? lc : mapview::LIVE_CAP_DEFAULT] << 20);
    s_archive.restore(_core->history);
    _core->history.setArchive(&s_archive);
  }
  trailJournalRestore();   // a trail cut off by a restart or a flat battery
  _tap_wake = lvport::loadTapWake();
  msgtext::seedQuick(node_prefs);   // "OK" in quick message 1 on first boot
  _nearby = new NearbyModel();
  _nearby->bindModel(_core, node_prefs);
  _scan = new NearbyModel();
  _scan->bindModel(_core, node_prefs);
  _scan->setSource(NearbyModel::SRC_SCAN);

#ifdef PIN_USER_BTN
  user_btn.begin();
#endif
#ifdef PIN_BUZZER
  soundctl::applyMode(_prefs, _buzzer, isClientConnected(), rtc_clock.getCurrentTime());
  _buzzer.setVolume(_prefs ? _prefs->buzzer_volume : 4);
  _buzzer.begin();   // plays the startup sound unless muted
#endif

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
  if (!lvport::begin()) {
    Serial.println("ui-lvgl: no memory for display buffers");
    return;
  }
  theme::setAccent(lvport::loadAccent());
  theme::setLight(lvport::loadLight());
  theme::install(lv_display_get_default());
  lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t*) {
    rowsFitPending();
    if (s_hdr_fit && s_ui) { s_hdr_fit = false; s_ui->fitHeader(); }
  }, LV_EVENT_REFR_START, NULL);

  buildStatusBar();
  quickPanelBuild();   // QuickPanel.h: ready before the first pull
  applyDisplayPrefs();   // a slider percentage overrides the level main.cpp set
  { char c[24]; lvport::crashSummary(c, sizeof(c)); }   // the dump in flash read now (~45 ms), not on the first Diagnostics
  showHome();
  if (pinSet()) lockScreen();   // a reboot doesn't get round the PIN
  showSplash();                // over both; fades out by itself
}

MyMesh::Listener* UITask::meshListener() { return _core; }

// The USER button's hold: sound on / off (leaves Auto, as the Sound page's
// On / Off would).
void UITask::toggleMute() {
#ifdef PIN_BUZZER
  if (!_prefs) return;
  bool on = _buzzer.isQuiet();   // muted now, however (mode, Auto, quiet hours): sound on
  soundctl::setMode(_prefs, _buzzer, on ? soundctl::MODE_ON : soundctl::MODE_OFF, isClientConnected());
  prefsSave();
  if (on) _buzzer.playForced(soundctl::MEL_VOLUME);
  if (!_asleep) { showToast(on ? "Sound on" : "Sound off", 1200); refreshStatusBar(); }
#endif
}

static constexpr uint32_t LOCK_OFF_MS = 30000;   // the lock screen's own auto-off cap

#include "PerfTest.h"   // -D UI_PERF_TEST only: draw profiles and control timing


void UITask::loop() {
  pollConnection();
  drainCoreEvents();
  sampleHistory();

  // USER (BOOT, side) button: Home's clock page; held and let go, mutes / unmutes, also with the
  // screen off, which it doesn't wake. WAKE (top) button: screen on at once, off on letting
  // go (a press that silenced the alarm, took a screenshot or woke the screen doesn't
  // also switch it off when let go).
  // Either one silences a ringing alarm first. Held in the first 8 s (once the
  // screen is up -- held at power-on it's the ESP32's download mode): CLI rescue.
  bool btn_click = false, btn_hold = false, wake_press = false, wake_release = false;
#ifdef PIN_USER_BTN
  int ev = user_btn.check();
  btn_click = ev == BUTTON_EVENT_CLICK;
  btn_hold = ev == BUTTON_EVENT_LONG_PRESS;
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  btn_click = s_sim_btn_click;
  btn_hold = s_sim_btn_hold;
  wake_press = wake_release = s_sim_wake;   // the page's button is a click
  s_sim_btn_click = s_sim_btn_hold = s_sim_wake = false;
#endif
#if defined(SEEED_WIO_TRACKER_L2)
  if ((int32_t)(millis() - _next_wake_poll_ms) >= 0) {   // an I2C read on the expander
    _next_wake_poll_ms = millis() + 50;
    bool down = board.readWakeButton();
    wake_press = down && !_wake_down;
    wake_release = !down && _wake_down;
    _wake_down = down;
  }
#endif
  // Side button held + top button: a screenshot (and neither does its own thing).
  // A long hold only arms the mute, which happens on letting go -- so holding
  // the side button a bit long before the top one doesn't mute as well.
  static bool mute_armed = false;
  static bool wake_used = false;   // this press of the top button did something already
  bool shot = false, mute = false;
#ifdef PIN_USER_BTN
  bool side_down = user_btn.isPressed();
  if (wake_press && side_down) { user_btn.cancelClick(); shot = true; mute_armed = false; wake_used = true; }
  if (mute_armed && !side_down) { mute_armed = false; mute = true; }
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  shot = s_sim_shot;
  s_sim_shot = false;
  if (mute_armed) { mute_armed = false; mute = true; }   // the page sends the hold on release
#endif
  if (wake_press && _core->clock.isRinging()) wake_used = true;
  const bool wake_now = wake_press && _asleep && !wake_used;
  if (wake_now) wake_used = true;   // its release must not put it back to sleep
  const bool sleep_now = wake_release && !wake_used && !_asleep;
  if (wake_release) wake_used = false;
  if (shot) { if (!_asleep) takeScreenshot(); }
  else if ((btn_click || btn_hold || wake_press) && _core->clock.isRinging()) dismissRing();
  else if (wake_now) wake();
  else if (sleep_now) sleep();
  else if (btn_hold && millis() - _ui_started_ms < 8000) {   // held in the first 8 s: CLI rescue, as on the L1
    the_mesh.enterCLIRescue();
    showToast("CLI rescue on the USB serial", 4000);
  }
  else if (btn_hold) mute_armed = true;
  else if (mute) toggleMute();
  else if (btn_click) {   // the side button doesn't wake: pockets
    if (flashlightOn()) flashlightClose();
    else if (quickPanelOpen()) quickPanelClose(true);
    else if (!_asleep && !locked()) goHome();
  }

  if (_asleep) {
    // Tap to wake (off: the top button only). An I2C read on the touch panel.
    if (_tap_wake && (int32_t)(millis() - _next_touch_poll_ms) >= 0) {
      _next_touch_poll_ms = millis() + 50;
      if (lvport::touched()) { lvport::swallowTouch(); wake(); }
    }
  } else {
    if (lv_display_get_inactive_time(NULL) < 1000) the_mesh.noteUserInput();
    // Locked, nobody's using it: the lock screen goes dark after LOCK_OFF_MS
    // even with auto-off at Never (a woken-up pocket, a boot PIN, a message).
    uint32_t aoff = autoOffMillis();
    if (locked() && (aoff == 0 || aoff > LOCK_OFF_MS)) aoff = LOCK_OFF_MS;
    if (aoff > 0 && lv_display_get_inactive_time(NULL) > aoff && !_core->clock.isRinging() && !otaBusy() && !flashlightOn()) sleep();
  }

  _core->loop();
  drainCoreEvents();
  prefsFlush();
  usbPoll();
#if defined(UI_HEAP_REPORT) && defined(ESP32)
  // -D UI_HEAP_REPORT: internal / PSRAM heap once, 20 s after boot.
  static bool heap_done = false;
  if (!heap_done && millis() > 20000) {
    heap_done = true;
    Serial.printf("HEAP internal free %u largest %u min %u | psram free %u | idf %s\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), esp_get_idf_version());
  }
#endif
#if defined(UI_PERF_TEST) && defined(ESP32)
  perfWalk();   // PerfTest.h
#endif
#ifdef PIN_BUZZER
  _buzzer.loop();
  if (soundctl::tick(_prefs, _buzzer, isClientConnected(), rtc_clock.getCurrentTime()) && !_asleep) refreshStatusBar();
  // The alarm melody repeats until dismissed or the ring window ends.
  if (_core->clock.isRinging() && !_buzzer.isPlaying()) playMelody(soundctl::MEL_ALARM);
#endif
  checkLowBattery();
  mapDownloadTick();   // a map download keeps running on any screen, and asleep
  wifiClockTick();
  otaTick();           // so does a firmware update
  trailJournalTick();   // the live trail's copy on the card, once a minute
  if ((int32_t)(millis() - _next_trackback_ms) >= 0) {   // walking the trail back, on any screen
    _next_trackback_ms = millis() + 1000;
    navPollTrackBack();
  }

  uint32_t lv_next = 0;
  if (!_asleep) {
    if ((int32_t)(millis() - _next_status_ms) >= 0) {
      _next_status_ms = millis() + 1000;
      refreshStatusBar();
      refreshLock();
      if (_screen == SCR_HOME) refreshHome();
      if (s_chats_stale && !_nav_overlay) refreshChats();   // a row was held when it came
      refreshDiag();
      refreshCompass();
      refreshGps();
      refreshBattCurve();
      if (quickPanelOpen()) quickPanelRefresh();
    }
    if (locked()) lockPoll();
    else if (_screen == SCR_HOME) { homeSwipePoll(); homeMapLoop(); }
    else if (_screen == SCR_GALLERY) galleryTick();
    if ((_screen == SCR_NEARBY || _screen == SCR_NODE) && (int32_t)(millis() - _next_nearby_ms) >= 0) {
      // Scan results trickle in over a few seconds: poll fast while scanning.
      _next_nearby_ms = millis() + (_scanning ? 250 : 2000);
      if (_scanning && (int32_t)(millis() - _scan_until_ms) >= 0) _scanning = false;
      if (_screen == SCR_NEARBY) {
        refreshNearbyList();
        if (_scan_overlay) refreshScanPopup();
      } else {
        refreshNode();
      }
    }
    fillTick();
    if (_screen == SCR_MAP) mapLoop();
    if (_screen == SCR_WIFI) pollWifiScan();
    if (_screen == SCR_STORAGE) pollStorage();
    if (_screen == SCR_ADMIN) adminPoll();
    if (_screen == SCR_ROUTE && (int32_t)(millis() - _next_route_ms) >= 0) {
      _next_route_ms = millis() + 200;
      routePoll();
    }
    roomPoll();
    if (_screen == SCR_CLOCK && (int32_t)(millis() - _next_clock_ms) >= 0) {
      _next_clock_ms = millis() + 100;   // stopwatch tenths
      refreshClock();
    }
    if (_screen == SCR_MELODY && (int32_t)(millis() - _next_clock_ms) >= 0) {
      _next_clock_ms = millis() + 30;    // the playing note's highlight keeps up
      refreshMelody();
    }
    if (_screen == SCR_THREAD && (int32_t)(millis() - _next_thread_check_ms) >= 0) {
      _next_thread_check_ms = millis() + 500;
      uint32_t sig = threadSignature();
      if (_thread_dirty || sig != _thread_sig) refreshThread();
      else if ((int32_t)(millis() - _next_thread_ages_ms) >= 0) refreshThreadAges();
      messagePopupTick();
    }
    lvport::cpuSlow(!cpuNeeded());
    lv_next = lv_timer_handler();
  }
  lvport::idle(idleMillis(lv_next));
}

// How long loop() may sleep: nothing is due before then. Not at all while the
// mesh has packets queued; briefly while a melody plays (its notes are timed
// here) or frames to or from the app wait (read one a pass, sent spaced out).
// A frame arriving from the app over BLE wakes the loop (serialFrameArrived).
uint32_t UITask::idleMillis(uint32_t lv_next) {
  if (the_mesh.hasPendingWork()) return 0;
#ifdef PIN_BUZZER
  if (_buzzer.isPlaying()) return 1;
#endif
  if (isClientConnected() && _serial->hasPendingFrames()) return 2;
  if (_asleep) return 50;   // as often as the buttons and tap to wake are polled
  return lv_next < 10 ? lv_next : 10;   // LVGL's next timer: a refresh, an animation, input
}

void UITask::shutdown(bool restart) {
  s_prefs_dirty_ms = s_prefs_save_at = 0;
  saveBeforeShutdown();
  if (!trailJournalTick(true)) _core->trail.onShutdown();   // no card: the internal slot, if enabled
#ifdef PIN_BUZZER
  _buzzer.shutdown();   // the goodbye sound, unless muted
#ifndef SIM_PLATFORM   // the sim's single thread would freeze the page
  for (uint32_t t0 = millis(); _buzzer.isPlaying() && millis() - t0 < 2500; ) { _buzzer.loop(); delay(10); }
#endif
#endif
  powerDown(restart, _display, _sensors);
}

// Settings > Power > Battery shutdown (NodePrefs::low_batt_mv), as
// ui-new does it: a smoothed reading every 8 s, never while on USB power.
void UITask::checkLowBattery() {
  if ((int32_t)(millis() - _next_batt_ms) < 0) return;
  _next_batt_ms = millis() + 8000;
  uint16_t raw = getBattMilliVolts();
  if (raw > 0) _batt_mv = battEma(_batt_mv, raw);
  if (!lowBatteryShutdownDue(_batt_mv, _prefs)) return;
  wake();
  showToast("Low battery - shutting down", 3000);
  lv_timer_handler();
  delay(2000);
  shutdown();
}

uint32_t UITask::autoOffMillis() const {
  if (!_prefs || _prefs->auto_off_secs == 0) return 0;
  return (uint32_t)_prefs->auto_off_secs * 1000UL;
}

static void bannerDismiss();   // below, with the banner

void UITask::sleep() {
  if (_asleep) return;
  _asleep = true;
  quickPanelClose(false);
  flashlightClose();
  bannerDismiss();
  if (_toast) {   // same: the toast's timer won't run while asleep
    if (_toast_timer) lv_timer_pause(_toast_timer);
    lv_anim_delete(_toast, NULL);
    lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  }
  if (_display) _display->turnOff();
  prefsSaveSoon(0);   // nothing to stall now
  lvport::powerSave(true, _tap_wake);
  if ((_prefs && _prefs->auto_lock) || pinSet()) lockScreen();   // Lock screen, or a screen PIN
}

void UITask::wake() {
  lv_display_trigger_activity(NULL);
  if (!_asleep) return;
  _asleep = false;
  lvport::powerSave(false, _tap_wake);
  if (_display) _display->turnOn();   // dark until the frame below is on the panel
  refreshStatusBar();
  if (_screen == SCR_HOME) refreshHome();
  if (_screen == SCR_THREAD) { if (locked()) refreshThread(); else reopenThreadIfUnread(); }   // locked: on unlocking
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);
  lvport::backlightFadeIn();
}

// ── Core events ───────────────────────────────────────────────────────────────

void UITask::drainCoreEvents() {
  UiEvent ev;
  while (_core->events.pop(ev)) {
    switch (ev.type) {
    case UiEventType::MessageArrived:
      onMessageArrived(ev);
      break;
    case UiEventType::ClockAlert:
      showRing(ev.text);
      playMelody(soundctl::MEL_ALARM);
      break;
    case UiEventType::ClockRingEnded:
      stopMelody();
      hideRing();
      break;
    case UiEventType::LiveShareEnded:
      showToast("Live share ended");
      break;
    case UiEventType::LocatorCrossed:
      showToast(ev.text, 3000);
#ifdef PIN_BUZZER
      if (!_buzzer.isQuiet()) playMelody(ev.flag ? soundctl::MEL_ARRIVE : soundctl::MEL_LEAVE);
#endif
      break;
    case UiEventType::LocatorBeep:   // Settings > Map > Proximity beeper
      playMelody(soundctl::MEL_TICK);
      break;
    case UiEventType::AdvertHeard:
      notify(ev.flag ? UIEventType::advertReceivedFlood : UIEventType::advertReceivedZeroHop);
      break;
    default:
      break;
    }
  }
}

// ── Message banner ──
// An incoming message slides down from under the status bar: an icon, the
// sender (or channel) and the start of the text. A tap opens the conversation
// (not while the screen is locked), a swipe up puts it away, and it goes by
// itself after BANNER_MS. The toast at the bottom stays for everything else.
static lv_obj_t*   s_banner = nullptr;
static lv_obj_t*   s_banner_icon = nullptr;
static lv_obj_t*   s_banner_title = nullptr;
static lv_obj_t*   s_banner_text = nullptr;
static lv_timer_t* s_banner_timer = nullptr;
static UIEventType s_banner_kind = UIEventType::none;
static int16_t     s_banner_ch = -1;
static uint8_t     s_banner_key[4];
static const uint32_t BANNER_MS = 4000;

static void bannerHidden(lv_anim_t* a) { lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN); }
static void bannerHide() {
  if (!s_banner || lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN)) return;
  if (s_banner_timer) lv_timer_pause(s_banner_timer);
  lv_anim_delete(s_banner, NULL);
  int32_t off = -(lv_obj_get_height(s_banner) + theme::STATUS_H);
  anim::run(s_banner, anim::setTy, lv_obj_get_style_translate_y(s_banner, LV_PART_MAIN), off, anim::OUT_MS, bannerHidden);
}
static void bannerTimerCb(lv_timer_t* t) { (void)t; bannerHide(); }
// Gone at once, no slide: LVGL's timers and animations stand still while the
// screen sleeps, so a banner left up would still be there on waking.
static void bannerDismiss() {
  if (!s_banner) return;
  if (s_banner_timer) lv_timer_pause(s_banner_timer);
  lv_anim_delete(s_banner, NULL);
  lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
}
static void onBanner(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) bannerHide();
    return;
  }
  bannerHide();
  s_ui->bannerOpen();
}

// Built once, behind the status bar (made just after it on the top layer).
static void bannerBuild() {
  const int32_t w = lv_display_get_horizontal_resolution(NULL);
  s_banner = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(s_banner, w - 12, LV_SIZE_CONTENT);
  lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 2);
  // Raised off the page: lighter than it on the dark theme, white on the light.
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(theme::s_light ? theme::SURFACE : theme::SURFACE_2), 0);
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(theme::s_light ? theme::SURFACE_2 : theme::SURFACE), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(s_banner, lv_color_hex(theme::EDGE), 0);
  lv_obj_set_style_border_width(s_banner, 1, 0);
  lv_obj_set_style_radius(s_banner, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(s_banner, 12, 0);
  lv_obj_set_style_shadow_color(s_banner, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(s_banner, theme::s_light ? LV_OPA_20 : LV_OPA_60, 0);
  lv_obj_set_style_pad_all(s_banner, 8, 0);
  lv_obj_set_style_pad_column(s_banner, 10, 0);
  lv_obj_set_flex_flow(s_banner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_banner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(s_banner, onBanner, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(s_banner, onBanner, LV_EVENT_GESTURE, NULL);
  s_banner_icon = label(s_banner, LV_SYMBOL_ENVELOPE, THEME_FONT_TITLE, theme::ACCENT);
  lv_obj_set_width(s_banner_icon, 20);   // the text lines up whichever icon
  lv_obj_set_style_text_align(s_banner_icon, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* col = flexBox(s_banner, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(col, 1);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_style_pad_row(col, 1, 0);
  s_banner_title = label(col, "", THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(s_banner_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_banner_title, LV_PCT(100));
  s_banner_text = label(col, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(s_banner_text, LV_LABEL_LONG_DOT);   // (LONG_DOT cuts at the max height)
  lv_obj_set_width(s_banner_text, LV_PCT(100));
  lv_obj_set_style_max_height(s_banner_text, lv_font_get_line_height(THEME_FONT_SMALL) * 2, 0);   // two lines, then "..."
  lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
}

static void bannerShow(const char* icon, const char* title, const char* text) {
  if (!s_banner) return;
  lv_label_set_text(s_banner_icon, icon);
  lv_label_set_text(s_banner_title, title);
  lv_label_set_text(s_banner_text, text);
  bool shown = !lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(s_banner, NULL);
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  if (!shown) {   // a new one while it's up just changes the text
    lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, belowTopBar() + 2);
    layoutNow(s_banner);
    anim::run(s_banner, anim::setTy, -(lv_obj_get_height(s_banner) + theme::STATUS_H), 0, anim::SCREEN_MS);
  } else {
    lv_obj_set_style_translate_y(s_banner, 0, 0);
  }
  if (!s_banner_timer) s_banner_timer = lv_timer_create(bannerTimerCb, BANNER_MS, NULL);
  lv_timer_set_period(s_banner_timer, BANNER_MS);
  lv_timer_reset(s_banner_timer);
  lv_timer_resume(s_banner_timer);
}

void UITask::bannerOpen() {
  if (locked()) return;   // the lock screen first
  if (s_banner_kind == UIEventType::channelMessage && s_banner_ch >= 0) openChannel((uint8_t)s_banner_ch);
  else if (s_banner_kind == UIEventType::contactMessage) openDM(s_banner_key);
  else showChats();   // a room post: the list
}

void UITask::onMessageArrived(const UiEvent& ev) {
  if (ev.kind == UIEventType::contactMessage && ev.flag) {   // the sender's alert / melody overrides
    memcpy(_notif_dm_prefix, ev.key, 4);
    _notif_dm_valid = true;
  }
  if (ev.kind == UIEventType::channelMessage) _notif_ch_idx = ev.idx;
  notify(ev.kind);
  // The banner: the name from the event, the text from the newest history entry.
  const char* text = "";
  const char* icon = LV_SYMBOL_ENVELOPE;
  s_banner_kind = ev.kind;
  s_banner_ch = ev.idx;
  memcpy(s_banner_key, ev.key, 4);
  if (ev.kind == UIEventType::channelMessage) {
    icon = "#";
    int pos = ev.idx >= 0 ? _core->history.histEntryForChannel(ev.idx, 0) : -1;
    if (pos >= 0) text = _core->history.chAtPos(pos).text;
  } else if (ev.kind == UIEventType::contactMessage) {
    int pos = _core->history.dmHistEntryForContact(ev.key, 0);
    if (pos >= 0) text = _core->history.dmAtPos(pos).text;
  } else {
    icon = LV_SYMBOL_LIST;   // a room
  }
  // Wake for the message unless an app is already showing it, or the user
  // turned message-wake off.
  bool wake_disabled = _prefs && (!_prefs->msg_wake || inQuietHours(*_prefs, rtc_clock.getCurrentTime()));   // quiet hours: dark too
  if (_asleep && !wake_disabled && !isClientConnected()) wake();
  else if (!_asleep) lv_display_trigger_activity(NULL);
  bool open_here = _screen == SCR_THREAD &&
      (ev.kind == UIEventType::channelMessage ? _thread_is_channel && ev.idx == _thread_channel
                                              : !_thread_is_channel && memcmp(_thread_key, ev.key, 4) == 0);
  bool banner = !_prefs || _prefs->msg_alert != NodePrefs::MSG_ALERT_OFF;
  if (!open_here && banner && !_asleep) bannerShow(icon, ev.text, text);   // not over the very conversation it's in
  if (_screen == SCR_CHATS && !_nav_overlay) refreshChats();   // new unread counts (not under an open popup)
}

// On screen: a conversation left open under the lock or a dark display isn't read.
bool UITask::threadInView() const {
  return !_asleep && !locked() && _screen == SCR_THREAD;
}

bool UITask::isViewingChannel(uint8_t channel_idx) {
  return threadInView() && _thread_is_channel && _thread_channel == channel_idx;
}

bool UITask::isViewingDM(const uint8_t* pub_key) {
  return threadInView() && !_thread_is_channel && memcmp(_thread_key, pub_key, 4) == 0;
}

// ── Status bar + toast (top layer, over every screen) ─────────────────────────

static void onStatusBarTouch(lv_event_t* e);   // QuickPanel.h

void UITask::buildStatusBar() {
  // The banner and the toast first: the status bar, made after them, covers
  // them as they slide in and out from under it.
  bannerBuild();
  _toast = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  // On the light theme a dark pill, as a phone's: a light one was lost on the page.
  lv_obj_set_style_bg_color(_toast, lv_color_hex(theme::s_light ? theme::TEXT : theme::SURFACE_2), 0);
  lv_obj_set_style_border_color(_toast, lv_color_hex(theme::EDGE), 0);
  lv_obj_set_style_border_width(_toast, theme::s_light ? 0 : 1, 0);
  lv_obj_set_style_radius(_toast, theme::RADIUS, 0);
  lv_obj_set_style_pad_hor(_toast, 12, 0);
  lv_obj_set_style_pad_ver(_toast, 6, 0);
  lv_obj_align(_toast, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // slides down from under the status bar
  lv_obj_t* tl = label(_toast, "", THEME_FONT_BODY, theme::s_light ? theme::SURFACE : theme::TEXT);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);   // long texts wrap instead of running off the screen
  lv_obj_set_style_max_width(tl, lv_display_get_horizontal_resolution(NULL) - 40, 0);
  lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* bar = lv_obj_create(lv_layer_top());
  s_status_bar = bar;
  styleOpaque(bar, theme::BG);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_PRESS_LOCK);   // pulled down past it: still its touch (the quick panel)
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(bar, onStatusBarTouch, LV_EVENT_ALL, NULL);
  lv_obj_set_size(bar, LV_PCT(100), theme::STATUS_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_pad_hor(bar, theme::PAD, 0);

  _status_time = label(bar, "--:--", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_align(_status_time, LV_ALIGN_LEFT_MID, 0, 0);
  _status_batt = label(bar, "", THEME_FONT_ICONS, theme::TEXT_MUTED);
  lv_obj_align(_status_batt, LV_ALIGN_RIGHT_MID, 0, 0);
  _status_chg = label(bar, LV_SYMBOL_CHARGE, THEME_FONT_ICONS, theme::OK);
  lv_obj_add_flag(_status_chg, LV_OBJ_FLAG_HIDDEN);
  _status_icons = lv_obj_create(bar);
  lv_obj_remove_style_all(_status_icons);
  lv_obj_remove_flag(_status_icons, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(_status_icons, 200, LV_PCT(100));   // fixed: packed to its right edge
  lv_obj_set_flex_flow(_status_icons, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_status_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(_status_icons, 2, 0);


  refreshStatusBar();
}

// lv_obj_align_to(o, base, LV_ALIGN_OUT_LEFT_MID, dx, 0), `base` a sibling,
// for what's refreshed every second: LVGL restyles the box on every call, so
// it redraws moved or not -- here it's only moved.
static void placeLeftOf(lv_obj_t* o, lv_obj_t* base, int32_t dx) {
  layoutNow(o);
  int32_t x = lv_obj_get_x(base) - lv_obj_get_width(o) + dx;
  int32_t y = lv_obj_get_y(base) + (lv_obj_get_height(base) - lv_obj_get_height(o)) / 2;
  if (lv_obj_get_style_align(o, LV_PART_MAIN) != LV_ALIGN_TOP_LEFT || lv_obj_get_x(o) != x || lv_obj_get_y(o) != y)
    lv_obj_align_to(o, base, LV_ALIGN_OUT_LEFT_MID, dx, 0);
}

// Whether the title fits its bar. The bar is a row where the title takes what
// the rest leave, so the rest gives way: the words of the screen's buttons
// (their icons stay, last button first). Only when that is not enough is the
// title cut with "...".
// A label with a space after its icon is collapsible: clipped to the icon's
// width, its text untouched, so a screen may rewrite it later.
static void collapseLabel(lv_obj_t* l, const lv_font_t* font, bool on) {
  const char* t = lv_label_get_text(l);
  const char* sp = strchr(t, ' ');
  if (!on || !sp) {
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    return;
  }
  char icon[8];
  size_t n = (size_t)(sp - t) < sizeof(icon) - 1 ? (size_t)(sp - t) : sizeof(icon) - 1;
  memcpy(icon, t, n);
  icon[n] = '\0';
  lv_point_t sz;
  lv_text_get_size(&sz, icon, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(l, sz.x);
}

void UITask::fitHeader() {
  if (!_header || !_hdr_clock || !_hdr_title) return;
  lv_obj_t* lbl[8];   // the collapsible labels, in the row's order
  int n = 0;
  const int cnt = (int)lv_obj_get_child_count(_header);
  for (int i = 0; i < cnt && n < 8; i++) {
    lv_obj_t* b = lv_obj_get_child(_header, i);
    if (!lv_obj_check_type(b, &lv_button_class) || lv_obj_get_child_count(b) == 0) continue;
    lv_obj_t* l = lv_obj_get_child(b, 0);
    if (lv_obj_check_type(l, &lv_label_class)) lbl[n++] = l;
  }
  lv_point_t tsz;
  lv_text_get_size(&tsz, lv_label_get_text(_hdr_title), THEME_FONT_TITLE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  auto place = [&]() {   // the row stops short of the clock pill
    layoutNow(_hdr_clock);
    const int32_t w = lv_obj_get_width(_hdr_clock);
    if (lv_obj_get_style_pad_right(_header, LV_PART_MAIN) != w) {
      lv_obj_set_style_pad_right(_header, w, 0);
      lv_obj_align(_hdr_clock, LV_ALIGN_RIGHT_MID, w, 0);   // aligned inside the padding: out into it
    }
    layoutNow(_header);
    return lv_obj_get_width(_hdr_title) >= tsz.x;
  };
  for (int i = 0; i < n; i++) collapseLabel(lbl[i], THEME_FONT_SMALL, false);
  if (place()) return;
  for (int i = n - 1; i >= 0; i--) {
    collapseLabel(lbl[i], THEME_FONT_SMALL, true);
    if (place()) return;
  }
}

void UITask::refreshStatusBar() {
  if (!_status_time) return;
  struct tm ti;
  if (localTime(_prefs, ti)) {
    char clk[12];
    fmtClock(clk, sizeof(clk), ti, _prefs, true);
    setText(_status_time, clk);
  } else {
    setText(_status_time, "--:--");
  }

  // Battery: an icon, % or volts per Settings > Power > Battery display. The
  // smoothed reading (checkLowBattery): an ADC read blocks the loop for 10 ms.
  uint16_t mv = _batt_mv ? _batt_mv : getBattMilliVolts();
  int pct = battery::percent(mv, _prefs ? _prefs->low_batt_mv : 0);
  char batt[12];
  switch (battery::mode(_prefs ? _prefs->batt_display_mode : 0)) {
    case battery::PERCENT: snprintf(batt, sizeof(batt), "%d%%", pct); break;
    case battery::VOLTAGE: snprintf(batt, sizeof(batt), "%u.%02uV", mv / 1000, (mv % 1000) / 10); break;
    default:
      snprintf(batt, sizeof(batt), "%s", pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                                       : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1
                                       : LV_SYMBOL_BATTERY_EMPTY);
  }
  setText(_status_batt, batt);
  const bool chg = _board->isExternalPowered();
  if (_hdr_clock) {   // the title bar's: the time over the battery, green on the charger
    // the pill's width changes with these: fitHeader() before the next frame
    if (!labelShows(_hdr_time, lv_label_get_text(_status_time)) || !labelShows(_hdr_batt, batt)) s_hdr_fit = true;
    setText(_hdr_time, lv_label_get_text(_status_time));
    setText(_hdr_batt, batt);
    setTextColor(_hdr_batt, chg ? theme::OK : theme::TEXT_MUTED);
  }
  lv_obj_t* left_of = _status_batt;   // the icons pack up to this
  if (chg) {
    lv_obj_remove_flag(_status_chg, LV_OBJ_FLAG_HIDDEN);
    placeLeftOf(_status_chg, _status_batt, -2);
    left_of = _status_chg;
  } else lv_obj_add_flag(_status_chg, LV_OBJ_FLAG_HIDDEN);

  // Status icons, as the original's status bar (ui-new): Bluetooth (accent
  // when the app is connected), WiFi (when switched on, accent connected), GPS (green with a fix), the alarm, mute, then
  // the modes that keep running in the background -- auto-advert, trail, live
  // share, repeater, arrival alert -- in the accent colour. Right to left in
  // that order, next to the battery.
  struct Icon { const char* sym; uint32_t col; };
  Icon icons[11];
  int n = 0;
  if (isBluetoothEnabled()) icons[n++] = { LV_SYMBOL_BLUETOOTH, hasConnection() ? theme::ACCENT : theme::TEXT_MUTED };
  if (lvport::wifiAllowed())   // WiFi switched on: accent while connected (map tiles, update)
    icons[n++] = { LV_SYMBOL_WIFI, lvport::netRadio() == lvport::NET_UP ? theme::ACCENT : theme::TEXT_MUTED };
  int32_t lat, lon;
  if (_core->gpsEnabled()) {   // switched off, the module still reports its last fix as valid
    bool fix = _core->course.currentLocation(lat, lon);
    icons[n++] = { LV_SYMBOL_GPS, fix ? theme::OK : theme::TEXT_MUTED };
  }
  if (_prefs && _prefs->alarm_on) icons[n++] = { LV_SYMBOL_BELL, theme::TEXT_MUTED };
#ifdef PIN_BUZZER
  if (_buzzer.isQuiet()) icons[n++] = { UI_SYMBOL_MUTE, theme::TEXT_MUTED };
#endif
  if (_prefs && _prefs->advert_auto_interval_sec > 0) icons[n++] = { UI_SYMBOL_RADIO, theme::ACCENT };
  if (_core->trail.isActive()) icons[n++] = { UI_SYMBOL_ROUTE, theme::ACCENT };
  if (_prefs && _prefs->loc_share_enabled) icons[n++] = { UI_SYMBOL_PIN, theme::ACCENT };
  if (_prefs && _prefs->client_repeat) icons[n++] = { LV_SYMBOL_LOOP, theme::ACCENT };
  if (_prefs && _prefs->locator_enabled && _prefs->locator_has_target) icons[n++] = { UI_SYMBOL_FLAG, theme::ACCENT };
  char sig[sizeof(_status_sig)];
  int o = 0;
  for (int i = 0; i < n && o < (int)sizeof(sig) - 12; i++) o += snprintf(sig + o, sizeof(sig) - o, "%s%lx", icons[i].sym, (unsigned long)icons[i].col);
  sig[o] = '\0';
  if (strcmp(sig, _status_sig) != 0) {   // rebuilt only when something changed
    strcpy(_status_sig, sig);
    lv_obj_clean(_status_icons);
    for (int i = n - 1; i >= 0; i--)   // the icon font gives each the same size and cell
      label(_status_icons, icons[i].sym, THEME_FONT_ICONS, icons[i].col);
  }
  placeLeftOf(_status_icons, left_of, -5);
}

// The status bar is up over a screen without a title bar (Home, the map, a
// conversation typing), and over the quick panel and the lock whatever the
// screen; a title bar carries the time and battery itself.
void UITask::statusBarSync() {
  if (!s_status_bar) return;
  const bool titled = _header && !lv_obj_has_flag(_header, LV_OBJ_FLAG_HIDDEN);
  const bool hide = titled && !s_sb_panel && !s_sb_lock;
  if (hide != lv_obj_has_flag(s_status_bar, LV_OBJ_FLAG_HIDDEN)) lv_obj_set_flag(s_status_bar, LV_OBJ_FLAG_HIDDEN, hide);
}

void UITask::setGps(bool on) {
  if (!_core->setGpsEnabled(on, false)) { showToast("No GPS on this device"); return; }
  prefsSave();   // written on leaving the screen, as the other switches
  refreshStatusBar();
}

void UITask::botSetGPS(bool on) { if (_core->setGpsEnabled(on)) refreshStatusBar(); }

bool UITask::ensureGps() {
  int32_t lat, lon;
  if (_core->gpsAvailable() && !_core->gpsEnabled()) { setGps(true); return false; }   // off, its last fix still reads valid
  if (_core->course.currentLocation(lat, lon)) return true;
  showToast("Waiting for a GPS fix");
  return false;
}

// One persistent timer, paused between toasts: hides the toast when it fires.
static void toastHidden(lv_anim_t* a) { lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN); }
static void toastTimerCb(lv_timer_t* t) {   // back up under the status bar
  lv_obj_t* o = (lv_obj_t*)lv_timer_get_user_data(t);
  anim::run(o, anim::setTy, 0, -(lv_obj_get_y(o) + lv_obj_get_height(o)), anim::OUT_MS, toastHidden);
  lv_timer_pause(t);
}

// A deliberate restart (the card formatted, the USB drive given back): a full
// screen saying so -- drawn at once, the loop stops here -- unsaved settings
// written, then the restart. Without it the screen just froze.
static void restartScreen(const char* why) {
  lv_obj_t* o = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(o, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(o, 8, 0);
  label(o, LV_SYMBOL_REFRESH, THEME_FONT_LARGE, theme::ACCENT);
  label(o, "Restarting...", THEME_FONT_LARGE, theme::TEXT);
  lv_obj_t* w = label(o, why, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_invalidate(o);
  lv_refr_now(NULL);
  prefsSaveSoon(0);
  prefsFlush();
  delay(1200);   // long enough to read
  lvport::restart();
  lv_obj_delete(o);   // the sim carries on
}

// The screen as it is, toasts and popups included, re-rendered once into a
// buffer the flush callback fills (lvport::shotCopy).
uint16_t* UITask::captureFrame(int& w, int& h) {
  lv_display_t* d = lv_display_get_default();
  w = lv_display_get_horizontal_resolution(d);
  h = lv_display_get_vertical_resolution(d);
#ifdef ESP32
  uint16_t* px = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
#else
  uint16_t* px = (uint16_t*)malloc(w * h * 2);
#endif
  if (!px) return nullptr;
  lvport::s_shot = px;
  lv_obj_invalidate(lv_screen_active());   // the whole display, every layer
  lv_refr_now(d);
  lvport::s_shot = nullptr;
  return px;
}

// The screen, written to the card as a 24-bit BMP: /sdcard/screenshots/scr_NNNN.bmp.
void UITask::takeScreenshot() {
  if (!lvport::mountStorage()) { showToast("No SD card"); return; }
  mkdir("/sdcard", 0777);   // the sim's MEMFS may not have it yet (the card: fails, harmless)
  mkdir("/sdcard/screenshots", 0777);
  char path[48];
  struct stat st;
  int n = 1;
  for (; n <= 9999; n++) {
    snprintf(path, sizeof(path), "/sdcard/screenshots/scr_%04d.bmp", n);
    if (stat(path, &st) != 0) break;
  }
  if (n > 9999) { showToast("Screenshots folder full"); return; }
  int W, H;
  uint16_t* px = captureFrame(W, H);
  if (!px) { showToast("Out of memory"); return; }

  bool ok = false;
  if (FILE* f = fopen(path, "wb")) {
    const uint32_t row = (uint32_t)W * 3, size = 54 + row * H;   // W*3 is a multiple of 4 at 320
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) hdr[at + i] = (uint8_t)(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
    hdr[26] = 1; hdr[28] = 24; put32(34, row * H);
    ok = fwrite(hdr, 1, 54, f) == 54;
    static PSRAM_BSS uint8_t line[320 * 3];   // not on the loop task's stack
    for (int32_t y = H - 1; ok && y >= 0; y--) {   // bottom-up, BGR
      const uint16_t* s = px + y * W;
      for (int32_t x = 0; x < W && x < 320; x++) {
        uint16_t c = s[x];
        uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
        line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
        line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
      }
      ok = fwrite(line, 1, row, f) == row;
    }
    ok = (fclose(f) == 0) && ok;
  }
  free(px);
  if (!ok) { remove(path); showToast("Couldn't save the screenshot"); return; }

  // A white frame flashing round the edges, then where it went. Four thin
  // strips, not a full-screen layer: each frame of the fade redraws only
  // them (a whole 320x240 frame per step tore and stuttered on the panel).
  const int32_t T = 6;
  const int32_t strip[4][4] = { { 0, 0, W, T }, { 0, H - T, W, T }, { 0, T, T, H - 2 * T }, { W - T, T, T, H - 2 * T } };
  for (const auto& r : strip) {
    lv_obj_t* f = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(f);
    lv_obj_set_pos(f, r[0], r[1]);
    lv_obj_set_size(f, r[2], r[3]);
    lv_obj_set_style_bg_color(f, lv_color_white(), 0);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    anim::run(f, anim::setBgOpa, LV_OPA_COVER, LV_OPA_TRANSP, 400, anim::coverDone);
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "Saved scr_%04d.bmp", n);
  showToast(msg);
}

void UITask::showToast(const char* text, uint32_t ms) {
  if (!_toast) return;
  lv_label_set_text(lv_obj_get_child(_toast, 0), text);
  bool shown = !lv_obj_has_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(_toast, NULL);   // on its way out
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  // Under the message banner while that's up, else right under the top bar.
  int32_t y = belowTopBar() + 4;
  if (s_banner && !lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN)) y = lv_obj_get_y(s_banner) + lv_obj_get_height(s_banner) + 4;
  lv_obj_align(_toast, LV_ALIGN_TOP_MID, 0, y);
  layoutNow(_toast);
  if (!shown) anim::run(_toast, anim::setTy, -(y + lv_obj_get_height(_toast)), 0, anim::POP_MS);   // a replaced text just changes
  else lv_obj_set_style_translate_y(_toast, 0, 0);
  if (!_toast_timer) _toast_timer = lv_timer_create(toastTimerCb, ms, _toast);
  lv_timer_set_period(_toast_timer, ms);
  lv_timer_reset(_toast_timer);
  lv_timer_resume(_toast_timer);
}

// ── Screens ───────────────────────────────────────────────────────────────────

static void onBack(lv_event_t* e) { (void)e; s_ui->back(); }

// A fresh screen below the status bar, loaded in place of the current one;
// returns its content area (flex column). The old screen is deleted
// asynchronously -- this usually runs from a click on one of its own widgets.
static uint32_t s_wifi_test_ms = 0;   // Settings > WiFi checking a just-saved network (WifiScreen.h)

namespace home { static void leave(); }   // HomeScreen.h

static int s_th_rows_n = 0;   // conversation bubbles built (refreshThread)

// A screen builds in pieces over several loop passes: what shows first at
// once, the rest as time allows. A widget costs ~1 ms on the L2 (they live in
// PSRAM), so 48 nodes, a few hundred contacts or a long settings page built
// in one go held the screen for up to a second. The pieces queue in order:
// fill(fn, from, to) makes fn(from) .. fn(to - 1), each from the data as it
// is then; then(fn, arg) makes its piece at once unless others still wait,
// keeping its place after them. A new screen drops what is left.
struct FillJob { void (UITask::*fn)(int); int next, end; };
static const int FILL_JOBS = 12;
static FillJob s_fill[FILL_JOBS];
static int s_fill_n = 0;
static const uint32_t FILL_BUDGET_US = 25000;   // pieces made per loop pass: about a frame's worth

void UITask::fill(void (UITask::*fn)(int), int from, int to) {
  if (from >= to) return;
  if (s_fill_n == FILL_JOBS) { while (from < to) (this->*fn)(from++); return; }   // no room: now
  s_fill[s_fill_n++] = { fn, from, to };
}

void UITask::then(void (UITask::*fn)(int), int arg) {
  if (s_fill_n) fill(fn, arg, arg + 1);
  else (this->*fn)(arg);
}

// Rows 0 .. n-1 of a list: the first `first` at once, the rest queued.
void UITask::fillStart(int n, int first, void (UITask::*row)(int)) {
  int now = first < n ? first : n;
  for (int i = 0; i < now; i++) (this->*row)(i);
  fill(row, now, n);
}

void UITask::fillStep() {
  FillJob& j = s_fill[0];
  (this->*j.fn)(j.next++);   // may queue more, behind
  if (!s_fill_n) return;     // the piece built a new screen: the queue went with the old one
  if (j.next >= j.end) memmove(s_fill, s_fill + 1, --s_fill_n * sizeof(FillJob));
}

void UITask::fillTick() {
  // Not while the screen moves in: its frames came a budget apart (Nodes,
  // 57 rows: 33 ms a frame, up to 65). What it shows first is built already.
  if (!s_fill_n || (_body && lv_anim_get(_body, NULL)) || lv_display_get_screen_prev(NULL)) return;
  uint32_t t0 = micros();
  while (s_fill_n) {
    fillStep();
    if (micros() - t0 >= FILL_BUDGET_US) break;
  }
}

// Everything still queued, now: before scrolling to where the screen was.
void UITask::fillFlush() { while (s_fill_n) fillStep(); }

int UITask::fillPending() const {
  int n = 0;
  for (int k = 0; k < s_fill_n; k++) n += s_fill[k].end - s_fill[k].next;
  return n;
}

#include "NodeViz.h"   // the node detail's rose and path (newScreen() drops its card)

lv_obj_t* UITask::newScreen(const char* title, bool with_back) {
  quickPanelClose(false);
  s_fill_n = 0;   // what was still to be built goes with the screen
  _home_clock = _home_date = _home_unread = nullptr;
  _thread_list = _compose_ta = _keyboard = nullptr;
  s_th_rows_n = 0;   // the bubbles went with the screen
  if (_shown_screen == SCR_SETTINGS && _body) s_settings_y = lv_obj_get_scroll_y(_body);
  pickerClose();
  _header = _body = nullptr;
  _nearby_list = _nearby_status = nullptr;
  _node_info = _node_ping = _node_delete_lbl = nullptr;
  nodeviz::s_card = nullptr;
  _scan_overlay = _scan_list = _scan_status = nullptr;   // the popup went with the old screen
  _map_area = _map_marks = _map_me = _map_zoom_lbl = _map_hint = _map_dl_pill = _map_center_btn = nullptr;
  _dl_overlay = _dl_info = _dl_sub = _dl_start_lbl = _dl_job_row = _dl_job_lbl = nullptr;
  _nav_bar = _nav_title = _nav_info = _nav_clear = nullptr;
  _nav_overlay = _nav_ta = _nav_kb = _nav_del_lbl = _nav_rec = _nav_avg_pill = nullptr;
  _nav_trail_lbl = _nav_trail_btn = _nav_reset_lbl = _nav_share_lbl = _nav_share_btn = _nav_tb_btn = nullptr;
  _wifi_ssid = _wifi_pass = _wifi_kb = _wifi_list = _wifi_status = nullptr;
  if ((_wifi_scanning || s_wifi_test_ms) && !wifiInUse()) lvport::netEnd();   // left mid-scan / check: the radio goes off
  _wifi_scanning = false;
  s_wifi_test_ms = 0;
  _ota_status = _ota_bar = _ota_btn = _ota_btn_lbl = nullptr;
  for (lv_obj_t*& t : _map_tiles) t = nullptr;
  lv_obj_t* prev = _scr;
  lv_obj_t* scr = lv_obj_create(NULL);
  s_fit_screen = scr;   // rowsFit()
  s_fit_pending = true;
  styleOpaque(scr, theme::BG);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // The title bar, at the very top in place of the status bar: back, the
  // title, the screen's actions (headerButton(), after the title in a row),
  // and at the right end the time and battery. A drag down on it brings the
  // quick panel, as on the status bar.
  int top = theme::STATUS_H;
  _hdr_clock = _hdr_time = _hdr_batt = _hdr_title = nullptr;
  if (title) {
    lv_obj_t* hdr = lv_obj_create(scr);
    styleSurface(hdr, theme::BG);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, LV_PCT(100), theme::APPBAR_H);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(hdr, with_back ? 4 : theme::PAD, 0);
    lv_obj_set_style_pad_column(hdr, theme::GAP, 0);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(hdr, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(hdr, onStatusBarTouch, LV_EVENT_ALL, (void*)1);   // a drag only: a tap on the title is nothing
    _header = hdr;
    if (with_back) {
      lv_obj_t* b = lv_button_create(hdr);
      lv_obj_set_size(b, 40, theme::BAR_BTN_H);
      lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
      lv_obj_set_style_shadow_width(b, 0, 0);
      lv_obj_add_event_cb(b, onBack, LV_EVENT_CLICKED, NULL);
      lv_obj_center(label(b, LV_SYMBOL_LEFT, THEME_FONT_BODY, theme::TEXT));
    }
    lv_obj_t* t = label(hdr, title, THEME_FONT_TITLE, theme::TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, 1);
    lv_obj_set_flex_grow(t, 1);   // what the actions leave
    _hdr_title = t;
    s_hdr_fit = true;
    // The time and battery on a pill as the bar's buttons are, out of the
    // row: the row stops short of it (the bar's right padding, refreshStatusBar()).
    lv_obj_t* c = lv_obj_create(hdr);
    lv_obj_remove_style_all(c);
    lv_obj_add_flag(c, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(c, onStatusBarTouch, LV_EVENT_ALL, NULL);   // a tap opens the panel, as the status bar's
    lv_obj_set_size(c, LV_SIZE_CONTENT, LV_PCT(100));
    lv_obj_set_style_pad_left(c, theme::GAP, 0);
    lv_obj_set_style_pad_right(c, 4, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(c, LV_ALIGN_RIGHT_MID, 0, 0);
    _hdr_clock = c;
    lv_obj_t* p = lv_obj_create(c);
    lv_obj_remove_style_all(p);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(p, LV_SIZE_CONTENT, theme::BAR_BTN_H);
    lv_obj_set_flex_flow(p, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(p, 6, 0);
    lv_obj_set_style_bg_color(p, lv_color_hex(theme::SURFACE), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(p, theme::RADIUS, 0);
    lv_obj_set_style_pad_hor(p, 8, 0);
    _hdr_time = label(p, "", THEME_FONT_BODY, theme::TEXT);
    _hdr_batt = label(p, "", THEME_FONT_ICONS, theme::TEXT_MUTED);
    top = theme::APPBAR_H;
  }

  lv_obj_t* body = lv_obj_create(scr);
  styleSurface(body, theme::BG);
  lv_obj_set_size(body, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL) - top);
  lv_obj_set_pos(body, 0, top);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(body, theme::PAD, 0);
  lv_obj_set_style_pad_row(body, theme::GAP, 0);
  _body = body;
  // The new screen emerges from the middle (Anim.h); a screen rebuilt in
  // place (same screen, same title) swaps without motion.
  bool same = _screen == _shown_screen && strncmp(title ? title : "", _shown_title, sizeof(_shown_title) - 1) == 0;
  bool backward = _nav_back || screenDepth(_screen) < screenDepth(_shown_screen);
  _nav_back = false;
  if (!same) prefsSaveSoon(anim::FADE_MS + 60);   // left the screen they were changed on
  if (_shown_screen == SCR_HOME && _screen != SCR_HOME) home::leave();   // arranging the apps ends, saved
  _shown_screen = _screen;
  snprintf(_shown_title, sizeof(_shown_title), "%s", title ? title : "");
  _scr = scr;
  if (_fade_next && prev && !_asleep && prev == lv_screen_active()) {
    // A cross-fade: the new screen dissolves in over the old one, deleted after.
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, anim::FADE_MS, 0, true);
  } else {
    lv_screen_load(scr);
    if (prev) lv_obj_delete_async(prev);   // this usually runs from one of its own widgets
    if (prev && !same && !_asleep) anim::screenIn(body, backward);
  }
  _fade_next = false;
  statusBarSync();
  refreshStatusBar();   // the title bar's time and battery
  return body;
}

// How deep a screen sits below Home, for the slide direction.
int UITask::screenDepth(Screen s) {
  switch (s) {
    case SCR_HOME: return 0;
    case SCR_CHATS: case SCR_NEARBY: case SCR_MAP: case SCR_SETTINGS:
    case SCR_COMPASS: case SCR_GALLERY: case SCR_CLOCK: case SCR_BOT: case SCR_REPEATER: case SCR_ADMIN_PICK:
    case SCR_DIAG: case SCR_GPS: return 1;
    case SCR_THREAD: case SCR_CONTACTS: case SCR_CHANNEL_EDIT: case SCR_NODE:
    case SCR_SETTINGS_NAV: case SCR_ADMIN: return 2;
    default: return 3;   // pages under a settings page
  }
}

void UITask::back() {
  if (quickPanelOpen()) { quickPanelClose(true); return; }
  _nav_back = true;   // the screen shown from here slides back
  switch (_screen) {
    case SCR_THREAD:   if (_nav_overlay) navClosePopup(); else showChats(); break;
    case SCR_CONTACTS: showChats(); break;
    case SCR_CHANNEL_EDIT: showChats(); break;
    case SCR_BOT:      if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_DIAG:     if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_ADMIN_PICK: showHome(); break;
    case SCR_ROUTE:      // back to the node; a trace in flight is dropped
      _core->route.cancelTrace();
      _screen = SCR_NODE;
      buildNode();
      break;
    case SCR_ROUTE_PICK: _screen = SCR_ROUTE; buildRoute(); break;
    case SCR_OTA:      otaLeave(); break;
    case SCR_COMPASS:  showHome(); break;
    case SCR_GALLERY:  galleryBack(); break;
    case SCR_BATT:     showSchemaSettings(PG_POWER); break;
    case SCR_GPS:      if (_gps_from_settings) showSettings(); else showHome(); break;
    case SCR_ADMIN:    if (_nav_overlay) navClosePopup(); else adminLeave(); break;
    case SCR_SETTINGS: if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_SETTINGS_NAV:   // the map's options go back to the map
      if (_settings_page == settings::PG_NAV && s_opts_from_map) {
        // The map as it was left -- its mode and view, Map tools over it,
        // scrolled where it was -- dissolving in over the page.
        const bool tools = s_nav_section >= 0;
        _fade_next = true;
        showMap(false);
        if (tools) navToolsPopup(true);
      } else if (_settings_page == settings::PG_RADIO) {
        showRadio();
      } else {
        showSettings();
      }
      break;
    case SCR_QUICK:    // back to Messages & contacts' bottom, where the row is
      if (_nav_overlay) { navClosePopup(); break; }
      showSchemaSettings(settings::PG_MESSAGES);
      if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_MELODY:   // back to the Sound page's bottom, where the melodies are
      melodySave();
      stopMelody();
      showSchemaSettings(settings::PG_SOUND);
      if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_CLOCK:    showHome(); break;
    case SCR_RADIO:
      if (radioPopupOpen()) radioCloseFreq();
      else if (_nav_overlay) navClosePopup();
      else showSettings();
      break;
    case SCR_REPEATER:
      if (radioPopupOpen()) radioCloseFreq();
      else if (_nav_overlay) navClosePopup();
      else showHome();
      break;
    case SCR_SCOPES:   // back to the Radio screen's bottom, where Scopes is
      if (_nav_overlay) { navClosePopup(); break; }
      showRadio();
      if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_CHATS:    if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_NEARBY:
      if (_nav_overlay) navClosePopup();   // the advert popup
      else if (_scan_overlay) closeScanPopup();
      else showHome();
      break;
    case SCR_NODE:     // back to where the node was picked: map, the list, or the scan popup over it
      if (_node_from_map) { _map_nodes = true; showMap(false); break; }   // the nodes map as it was left
      _screen = SCR_NEARBY;
      buildNearby();
      if (_node_from_scan) showScanPopup();
      break;
    case SCR_MAP:
      if (_nav_overlay) { navClosePopup(); break; }   // a popup over the map first
      if (_route_pick) { routeMapDone(); break; }
      if (_route_show) { routeShowDone(); break; }
      if (areaSelecting()) areaSelectEnd();
      else if (_dl_overlay) mapDownloadClose();
      else if (_map_nodes || !_map_nav) showNearby();   // opened from Nodes
      else showHome();
      break;
    case SCR_WIFI:     showSettings(); break;
    case SCR_STORAGE:  storeview::stopWalk(); showSettings(); break;
    default:           break;
  }
  _nav_back = false;   // only closed a popup
}

// ── Home ──────────────────────────────────────────────────────────────────────

static void onOpenChats(lv_event_t* e) { (void)e; s_ui->showChats(); }
static void onOpenSettings(lv_event_t* e) { (void)e; s_ui->showSettings(); }

static void onOpenNearby(lv_event_t* e) { (void)e; s_ui->showNearby(); }
static void onOpenNodesMap(lv_event_t* e) { (void)e; s_ui->openMap(false); }
static void onOpenClock(lv_event_t* e);   // ClockScreen.h
static void onOpenBot(lv_event_t* e);     // BotScreen.h
static void onOpenCompass(lv_event_t* e);   // CompassScreen.h
static void onOpenGallery(lv_event_t* e);   // GalleryScreen.h
static void onOpenFlashlight(lv_event_t* e);   // Flashlight.h
static void onOpenDiag(lv_event_t* e);      // DiagScreen.h
static void onOpenGps(lv_event_t* e);       // GpsScreen.h
static void onOpenGpsFromSettings(lv_event_t* e);
static void onOpenRepeater(lv_event_t* e);  // RepeaterScreen.h
static void onOpenAdminPick(lv_event_t* e); // AdminScreen.h
static void onNodeName(lv_event_t* e);
static void onPowerRow(lv_event_t* e);
static void onFactoryReset(lv_event_t* e) { s_ui->factoryResetStep((int)(uintptr_t)lv_event_get_user_data(e)); }

// Home itself (pages, clock, minimap, apps) is HomeScreen.h.

#include "VirtualList.h"

#include "ChatList.h"

#include "NodesView.h"

#include "MapScreen.h"
#include "Chart.h"
#include "ProfileView.h"
#include "NavMap.h"
#include "GalleryScreen.h"
#include "MapAreas.h"
#include "MapRegions.h"
#include "HomeScreen.h"
#include "ClockScreen.h"
static void onOpenSchemaPage(lv_event_t* e);   // Radio > Advanced
#include "RadioScreen.h"
#include "WifiScreen.h"
#include "ChannelScreen.h"
#include "AdminScreen.h"
#include "RouteScreen.h"
#include "BotScreen.h"

#include "ThreadView.h"

#include "SettingsPages.h"

#include "ConversationScreen.h"
#include "DeviceScreen.h"
#include "DiagScreen.h"
#include "CompassScreen.h"
#include "Flashlight.h"
#include "BatteryScreen.h"
#include "GpsScreen.h"
#include "RadioExtras.h"
#include "RepeaterScreen.h"
#include "SoundScreen.h"
#include "QuickScreen.h"
#include "QuickPanel.h"
#include "OtaScreen.h"
#include "StorageScreen.h"
#include "Splash.h"

// The screen on, the full CPU clock while it's worth having: touched in the
// last CPU_IDLE_MS, something moving, map tiles to decode, a download or an
// update on WiFi. Otherwise 80 MHz (lvport::cpuSlow) -- a clock ticking over,
// a message being read.
static constexpr uint32_t CPU_IDLE_MS = 2000;
bool UITask::cpuNeeded() {
#ifdef UI_PERF_TEST
  return true;   // measured at the full clock
#endif
  if (lv_display_get_inactive_time(NULL) < CPU_IDLE_MS || lv_anim_count_running() > 0) return true;
  if (wifiInUse() || mapview::s_dl.liveQueued() > 0) return true;
  if (_screen == SCR_MAP && _map_pending) return true;
  if (_screen == SCR_HOME && home::mini::s_area && home::mini::s_pending) return true;
  return false;
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
void UITask::simCompose(const char* text) {
  if (!_compose_ta) return;
  setKeyboardVisible(true);
  lv_textarea_set_text(_compose_ta, text);
}

void UITask::simDm() {
  for (int i = 0; i < the_mesh.getNumContacts(); i++) {
    ContactInfo c;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) && c.type == ADV_TYPE_CHAT) { openDM(c.id.pub_key); return; }
  }
}

void UITask::simNavTarget(int32_t lat_1e6, int32_t lon_1e6) {
  _core->locator.setTarget(0, nullptr, lat_1e6, lon_1e6, "Rynek");
  if (_nav_bar) refreshNavBar();
}

void UITask::simUnread() {
  _core->simUnreadDemo();
  if (_screen == SCR_CHATS) refreshChats();
}

extern "C" EMSCRIPTEN_KEEPALIVE void sim_nav_target(int lat_1e6, int lon_1e6) { if (s_ui) s_ui->simNavTarget(lat_1e6, lon_1e6); }

// Sim tests: straight to a screen by name.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_open(const char* name) {
  if (!s_ui) return;
  struct { const char* n; void (UITask::*fn)(); } const SCREENS[] = {
    { "home", &UITask::showHome }, { "chats", &UITask::showChats }, { "settings", &UITask::showSettings },
    { "nodes", &UITask::showNearby }, { "ota", &UITask::showOta }, { "wifi", &UITask::showWifi },
    { "clock", &UITask::showClock }, { "radio", &UITask::showRadio }, { "scopes", &UITask::showScopes },
    { "repeater", &UITask::showRepeater }, { "bot", &UITask::showBot }, { "storage", &UITask::showStorage },
    { "diag", &UITask::showDiag }, { "compass", &UITask::showCompass }, { "gallery", &UITask::showGallery }, { "admin", &UITask::showAdminPick },
    { "quick", &UITask::showQuickMsgs }, { "contacts", &UITask::showContacts },
  };
  for (auto& s : SCREENS) if (!strcmp(s.n, name)) { (s_ui->*s.fn)(); return; }
  if (!strncmp(name, "page", 4)) { s_ui->showSchemaSettings(atoi(name + 4)); return; }
  if (!strcmp(name, "map")) { s_ui->openMap(true); return; }
  if (!strcmp(name, "gps")) { s_ui->showGps(false); return; }
  if (!strcmp(name, "nodesmap")) { s_ui->openMap(false); return; }   // as from Nodes
  if (!strcmp(name, "battcurve")) { s_ui->showBattCurve(); return; }
  if (!strcmp(name, "homeedit")) { s_ui->showHome(); s_ui->homeEdit(true); return; }   // arranging the apps
  if (!strncmp(name, "accent@", 7)) { theme::setAccent(atoi(name + 7)); return; }   // "accent@4": Cyan
  if (!strncmp(name, "map@", 4)) { s_ui->simMapAt(name + 4); return; }   // "map@lat,lon,z"
  if (!strncmp(name, "channel@", 8)) { s_ui->openChannel(atoi(name + 8)); return; }
  if (!strncmp(name, "compose@", 8)) { s_ui->simCompose(name + 8); return; }
  if (!strcmp(name, "dm")) { s_ui->simDm(); return; }
  if (!strcmp(name, "send")) { s_ui->simSend(); return; }
  if (!strcmp(name, "unread")) { s_ui->simUnread(); return; }
  if (!strcmp(name, "sleep")) { s_ui->simSleep(true); return; }
  if (!strcmp(name, "wake")) { s_ui->simSleep(false); return; }
  if (!strcmp(name, "lock")) { s_ui->simLock(true); return; }
  if (!strcmp(name, "unlock")) { s_ui->simLock(false); return; }
  if (!strcmp(name, "vector")) { s_ui->setVectorMap(true); return; }
  if (!strcmp(name, "maptools")) { s_ui->navToolsPopup(); return; }
  if (!strcmp(name, "areasel")) { s_ui->areaSelectBegin(); return; }
  if (!strcmp(name, "areas")) { s_ui->mapAreasPopup(); return; }
  if (!strcmp(name, "regions")) { s_ui->mapRegionsPopup(); return; }
  if (!strcmp(name, "region0")) { s_ui->mapRegionPopup(0); return; }
  if (!strcmp(name, "advert")) { s_ui->advertPopup(); return; }
  if (!strcmp(name, "trailsave")) { s_ui->navToolAction(navmap::TL_TRAIL_SAVE); return; }   // Map tools > Save
  if (!strcmp(name, "histdemo")) {   // 8 h of made-up history, for the charts
    using namespace diagview;
    for (int i = 0; i < 96; i++) {
      s_hist.batt.push((int16_t)(4150 - i * 4 + (i % 7) * 3));
      s_hist.noise.push((int16_t)(-112 + (i % 11 == 0 ? 6 : 0) + (i / 24)));
      s_hist.traffic.push((int16_t)((i * 37) % 13 + (i % 16 == 0 ? 20 : 0)));
    }
    for (int i = 0; i < 32; i++) s_hist.snr.push((int16_t)(4 * (8 - (i * 7) % 19)));
    s_hist.version++;
    s_near.f0 = 869.525f - s_near.step * (s_near.n / 2);   // and a noise sweep
    for (int i = 0; i < s_near.n; i++) s_near.v[i] = (int16_t)(-118 + (i % 9 == 0 ? 7 : (i * 5) % 4));
    s_floor_med = -116; s_floor_lo = -118;
    s_noise_have = true;
    return;
  }
  if (!strcmp(name, "profdemo")) {   // a made-up height profile in a popup, for the chart
    using namespace profileview;
    s_prof = trailprofile::Sampler();
    for (int i = 0; i < 100; i++) {
      s_prof.dist[i] = i * 62.0f;
      s_prof.alt[i] = (int16_t)(420 + 80 * sinf(i / 14.0f) + i * 1.5f);
      s_prof.st.add(s_prof.alt[i]);
    }
    s_prof.n = 100; s_prof.total = 99 * 62.0f;
    lv_obj_t* box = lv_obj_create(lv_layer_top());
    styleSurface(box, theme::BG);
    lv_obj_set_size(box, 304, 132);
    lv_obj_center(box);
    profileChart(box, 120);
    return;
  }
  if (!strcmp(name, "savedtrail0")) { navmap::scanTrails(); s_ui->savedTrailPopup(0); return; }
  if (!strncmp(name, "trail@", 6)) {   // "trail@name.trl" from /sdcard/trails: Load, as the saved-trail popup does
    navmap::scanTrails();
    for (int i = 0; i < navmap::s_st_n; i++)
      if (!strcmp(navmap::s_st_names[i], name + 6)) { navmap::s_st_sel = i; s_ui->savedTrailAction(navmap::TL_ST_LOAD); }
    return;
  }
}
// Scrolls the screen's (or a popup's) main list by dy; returns what was left to scroll.
static void dbgScrollable(lv_obj_t* o, lv_obj_t*& best, int& h) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || lv_obj_check_type(o, &lv_roller_class) || lv_obj_check_type(o, &lv_dropdownlist_class)) return;
  int v = lv_obj_get_scroll_bottom(o) + lv_obj_get_scroll_y(o);
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) && v > h) { h = v; best = o; }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dbgScrollable(lv_obj_get_child(o, i), best, h);
}
extern "C" EMSCRIPTEN_KEEPALIVE int sim_scroll(int dy) {
  lv_obj_t* o = nullptr; int h = 10;
  dbgScrollable(lv_layer_top(), o, h);
  if (!o) dbgScrollable(lv_screen_active(), o, h);
  if (!o) return -1;
  int left = lv_obj_get_scroll_bottom(o);
  lv_obj_scroll_by(o, 0, -(dy < left ? dy : left), LV_ANIM_OFF);
  return left;
}
#endif

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// The simulator page opens a screen by name (screenshots of the layout):
// home, chats, contacts, settings, settings:<page>, nearby, map, navmap, places, clock, radio,
// gps, diag, compass, storage, repeater, wifi, bot, quick, node, channel, layer:<n>.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_ui_show(const char* name) {
  if (!s_ui || !name) return 0;
  UITask& u = *s_ui;
  if (!strcmp(name, "home")) u.showHome();
  else if (!strcmp(name, "chats")) u.showChats();
  else if (!strcmp(name, "contacts")) u.showContacts();
  else if (!strcmp(name, "settings")) u.showSettings();
  else if (!strncmp(name, "settings:", 9)) u.showSchemaSettings(atoi(name + 9));
  else if (!strcmp(name, "nearby")) u.showNearby();
  else if (!strcmp(name, "map")) u.openMap(false);
  else if (!strcmp(name, "navmap")) u.openMap(true);
  else if (!strcmp(name, "places")) { u.openMap(true); u.navTargetsPopup(); }
  else if (!strncmp(name, "layer:", 6)) u.setMapLayer(atoi(name + 6));   // the map's base layer (MapLayers.h)
  else if (!strcmp(name, "clock")) u.showClock();
  else if (!strcmp(name, "radio")) u.showRadio();
  else if (!strcmp(name, "gps")) u.showGps(false);
  else if (!strcmp(name, "diag")) u.showDiag();
  else if (!strcmp(name, "compass")) u.showCompass();
  else if (!strcmp(name, "storage")) u.showStorage();
  else if (!strcmp(name, "repeater")) u.showRepeater();
  else if (!strcmp(name, "wifi")) u.showWifi();
  else if (!strcmp(name, "bot")) u.showBot();
  else if (!strcmp(name, "quick")) u.showQuickMsgs();
  else if (!strcmp(name, "node")) { u.showNearby(); u.openNode(0); }
  else if (!strcmp(name, "channel")) u.openChannel(0);
  else return 0;
  return 1;
}
#endif
