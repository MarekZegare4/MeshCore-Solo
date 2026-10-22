#include <helpers/UTF8Helpers.h>
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

#include "CardKB.h"
#if defined(SIMPSON)
  #include <string.h>
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
  #define MESHCORE_VERSION    "1.18.0"
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
#include "SplashScreen.h"

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
#include "../ui-core/SettingsCli.h"      // and their companion CLI keys
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
#include "FlashlightScreen.h"
#include "ToolsScreen.h"
#include "CalendarView.h"       // the month under a tall clock, Clock tools › Calendar
#include "ClockToolsScreen.h"   // Alarm / Timer / Stopwatch / Calendar (Clock page › Enter)

#include "../ui-core/Battery.h"

#include "HomeScreen.h"


void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _node_prefs = node_prefs;
  _kb.prefs = node_prefs;
#if defined(SIMPSON)
  if (!_simpson_keypad.begin()) {
    MESH_DEBUG_PRINTLN("Simpson keypad initialization failed");
  }
  _kb.input_mode_hint = "Ctl";
  _kb.hide_keyboard_grid = true;
#endif
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
  if (!Features::IS_EINK) flashlight = new FlashlightScreen(this);
#if defined(PIN_GPIO1)
  gpio_screen        = new GpioScreen(this, node_prefs);
#endif
  startCardKBCapture();   // after every screen: an optional task must not starve one
  applyBrightness();
  applyRotation();
  applyFullRefreshInterval();
  applyUiScale();   // Settings > Display > Text size / Icon size
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
bool UITask::openNodesMap() {
  setCurrScreen(nearby_screen);
  return ((NearbyScreen*)nearby_screen)->openMap();
}
void UITask::openRouteFor(const uint8_t* pub_key) {
  setCurrScreen(nearby_screen);   // its onShow() reset first
  ((NearbyScreen*)nearby_screen)->openRoute(pub_key);
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
void UITask::gotoFlashlight()      { setCurrScreen(flashlight); }
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
  return _core->ping.start(pub_key) == PingEngine::STARTED;
}
uint32_t UITask::pingTimeoutMs() const { return _core->ping.timeoutMs(); }
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

#if defined(SIMPSON)
void UITask::gotoBluetoothHomePage() {
  setCurrScreen(home);
  static_cast<HomeScreen*>(home)->showBluetoothPage();
  _next_refresh = 0;
}

void UITask::gotoGpsHomePage() {
  setCurrScreen(home);
  static_cast<HomeScreen*>(home)->showGpsPage();
  _next_refresh = 0;
}
#endif

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

// Sim tests: a walked trail ending at the GPS fix of simNodes(), and two
// waypoints beside it, for the maps.
void UITask::simTrail() {
  TrailStore& tr = _core->trail.store();
  const uint32_t now = rtc_clock.getCurrentTime();
  for (int i = 0; i <= 12; i++) {
    const float f = (12 - i) / 12.0f;
    tr.addPoint((int32_t)((50.0614f - 0.010f * f + 0.002f * (i % 3)) * 1e6f),
                (int32_t)((19.9372f - 0.018f * f * f) * 1e6f), now - (12 - i) * 60, 1);
  }
  waypoints().add((int32_t)(50.0560f * 1e6f), (int32_t)(19.9250f * 1e6f), now, "Car");
  waypoints().add((int32_t)(50.0680f * 1e6f), (int32_t)(19.9600f * 1e6f), now, "Hut");
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
    int cut = (int)mesh::validUtf8PrefixLength(ev.text, 20);   // never half a letter
    snprintf(alert_buf, sizeof(alert_buf), "Msg: %.*s", cut, ev.text);
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
  _display->setColor(UIColor::window_bkg);
  _display->fillRect(0, y, W, h);
  _display->setColor(UIColor::primary_txt);
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
  d.setColor(UIColor::window_bkg);
  d.fillRect(x - 2 * s, 0, w + 4 * s, h);
  d.setColor(UIColor::primary_txt);
  if (msgBadgeLit()) drawMailCount(d, x, 0, h, _alert_count);
}

void UITask::setCurrScreen(UIScreen* c) {
  // Fail safe on a null target: a screen pointer left uninitialised (member
  // declared + navigator wired, but the `new XScreen()` line forgotten in
  // begin()) stays nullptr thanks to the in-class initialisers. Bail here so
  // that mistake is an inert no-op instead of a null deref in render()/poll().
  if (!c) return;
  if (flashlight && curr == flashlight && c != flashlight) applyBrightness();   // its full brightness goes with it
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
  saveBeforeShutdown();

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

  powerDown(restart, _display, _sensors);
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
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

extern "C" EMSCRIPTEN_KEEPALIVE void sim_trail() {
  if (g_sim_ui_task_for_js) g_sim_ui_task_for_js->simTrail();
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

#if defined(SIMPSON)
void UITask::pollSimpsonKeypad() {
  const uint8_t key = _simpson_keypad.poll();
  if (key != SIMPSON_KEY_NONE) handleSimpsonKeypadKey(key);
}

void UITask::handleSimpsonKeypadKey(uint8_t key) {
  if (key == SIMPSON_KEY_ENTER_LONG) {
    if (_locked && !_unlock_kb) return;
    enqueueKey(handleLongPress(KEY_ENTER));
    return;
  }

  if (checkDisplayOn(KEY_ENTER) == 0 || (_locked && !_unlock_kb)) return;

  const uint32_t auto_off = autoOffMillis();
  if (auto_off > 0) _auto_off = millis() + auto_off;

  if (key == SIMPSON_KEY_MODE_CHANGED) {
    static const char* const modes[] = { "Ctl", "abc", "ABC", "123" };
    _kb.input_mode_hint = modes[_simpson_keypad.mode()];
    _kb.hide_keyboard_grid = true;
    showAlert(modes[_simpson_keypad.mode()], 1200);
    _next_refresh = 0;
    return;
  }

  switch (key) {
    case SIMPSON_KEY_CANCEL:
      enqueueKey(KEY_CANCEL);
      break;
    case SIMPSON_KEY_UP:
      enqueueKey(KEY_UP);
      break;
    case SIMPSON_KEY_DOWN:
      enqueueKey(KEY_DOWN);
      break;
    case SIMPSON_KEY_LEFT:
      enqueueKey(KEY_LEFT);
      break;
    case SIMPSON_KEY_RIGHT:
      enqueueKey(KEY_RIGHT);
      break;
    case SIMPSON_KEY_ENTER:
      enqueueKey((_unlock_kb || _kb.isVisible()) ? KEY_KB_ENTER : KEY_ENTER);
      break;
    case SIMPSON_KEY_BLUETOOTH:
      if (!_unlock_kb) gotoBluetoothHomePage();
      break;
    case SIMPSON_KEY_GPS:
      if (!_unlock_kb) gotoGpsHomePage();
      break;
    case SIMPSON_KEY_MESSAGES:
      if (!_unlock_kb) gotoMessagesScreen();
      break;
    case SIMPSON_KEY_BRIGHTNESS_UP:
    case SIMPSON_KEY_BRIGHTNESS_DOWN:
      if (!_unlock_kb && _node_prefs) {
        uint8_t& brightness = _node_prefs->display_brightness;
        if (key == SIMPSON_KEY_BRIGHTNESS_UP && brightness < 4) ++brightness;
        if (key == SIMPSON_KEY_BRIGHTNESS_DOWN && brightness > 0) --brightness;
        applyBrightness();
        bool dirty = true;
        savePrefsIfDirty(dirty);
      }
      break;
    case SIMPSON_KEY_SYMBOLS:
      if (_unlock_kb) break;
      if (_kb.isVisible()) _kb.openSymbolsPage();
      else showAlert("Open a text field first", 1200);
      break;
    case SIMPSON_KEY_BACKSPACE:
      if (_kb.isVisible()) {
        if (_unlock_kb) handleUnlockKey(0x08);
        else _kb.backspaceTyped();
      } else {
        enqueueKey(KEY_CANCEL);
      }
      break;
    default:
      if (_kb.isVisible()) {
        const auto typeInput = [this](char c) {
          if (_unlock_kb) handleUnlockKey(c);
          else _kb.handleInput(c);
        };
        switch (_simpson_keypad.mode()) {
          case SIMPSON_KEYPAD_ABC:
          case SIMPSON_KEYPAD_ABC_UPPER:
            if (key == ',') {
              _kb.typePhysicalT9Cell(0, _simpson_keypad.mode() == SIMPSON_KEYPAD_ABC_UPPER);
            } else {
              static const char t9_keys[] = "ADGJMPTW";
              const char* found = strchr(t9_keys, (char)key);
              if (found) {
                _kb.typePhysicalT9Cell((int)(found - t9_keys) + 1,
                  _simpson_keypad.mode() == SIMPSON_KEYPAD_ABC_UPPER);
              } else if (key == ' ') {
                typeInput(' ');
              }
            }
            break;
          case SIMPSON_KEYPAD_NUMERIC:
            if (key >= '0' && key <= '9') typeInput((char)key);
            break;
          case SIMPSON_KEYPAD_CONTROL:
            break;
        }
      }
      break;
  }
  _next_refresh = 0;
}
#endif

#include "PerfL1.h"

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
  {  // a conversation in view again (display on, unlocked): on to what came in meanwhile.
     // Only on the display or lock changing -- not on opening the screen, which
     // positions itself (a second pass would re-anchor on what the first frame
     // had just marked read, and the list jumped).
    bool on = _display && _display->isOn() && !_locked;
    if (on && !_display_was_on && curr == messages_screen) ((MessagesScreen*)messages_screen)->onDisplayOn();
    _display_was_on = on;
  }
  if (status_screen) ((StatusScreen*)status_screen)->sample();   // Status history lines, once a minute
#if UI_HAS_JOYSTICK
  uint8_t joy_rot = _node_prefs ? _node_prefs->joystick_rotation : JOYSTICK_ROTATION;
#if FEAT_DISPLAY_ROTATION_SETTING && defined(JOYSTICK_UPRIGHT_ROTATION)
  if (_node_prefs) joy_rot += (JOYSTICK_UPRIGHT_ROTATION - _node_prefs->display_rotation) & 3;
#endif
  auto enterClick = [&]() {
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
  };
  int ev = user_btn.check();
#ifdef UI_ENTER_DOUBLE_BACK
  // For a joystick without a reachable Back button: a double click on Enter goes
  // back. Multi-click delays a single Enter by the click window. Where text is
  // typed (on-screen keyboard, PIN pad) or the lock sequence is counting, a
  // double is just two Enters, so a repeated letter still works.
  if (ev == BUTTON_EVENT_DOUBLE_CLICK || ev == BUTTON_EVENT_TRIPLE_CLICK) {
    int n = (ev == BUTTON_EVENT_DOUBLE_CLICK) ? 2 : 3;
    if (_unlock_kb || _kb.isVisible() || back_btn.isPressed()) {
      for (int i = 0; i < n; i++) enterClick();
    } else {
      enqueueKey(checkDisplayOn(KEY_CANCEL));
    }
    ev = BUTTON_EVENT_NONE;
  }
#endif
  if (ev == BUTTON_EVENT_CLICK) {
    enterClick();
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
#if defined(SIMPSON)
  pollSimpsonKeypad();
#endif
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
      if (tabbar::endFrame(*_display) && delay_millis > 15) delay_millis = 15;   // a tab turn sliding
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
    if (flashlight && curr == flashlight) {   // a torch stays lit; after it, the usual timeout
      uint32_t aoff = autoOffMillis();
      _auto_off = millis() + (aoff > 0 ? aoff : AUTO_OFF_MILLIS);
    }
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
      _batt_mv = battEma(_batt_mv, raw);
#endif
    }
    if (lowBatteryShutdownDue(_batt_mv, _node_prefs)) {
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(1);
        _display->setColor(UIColor::primary_txt);
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

bool UITask::getGPSState() { return _core && _core->gpsEnabled(); }
bool UITask::hasGPS()      { return _core && _core->gpsAvailable(); }

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

// Text size and Icon size (Settings > Display, NodePrefs ui_text_scale /
// ui_icon_scale), on the builds that offer them. Elsewhere the fields stay 0:
// the build's font scale and Auto icons, as stock.
void UITask::applyUiScale() {
  if (!_node_prefs) return;
#ifdef EINK_FONT_SCALE
  if (_display != NULL) _display->setUiScale(_node_prefs->ui_text_scale);
#endif
#if STATUS_ICONS_SCALED
  statusIconScaleSetting() = _node_prefs->ui_icon_scale;
#endif
}

void UITask::applySoundPrefs() {
#ifdef PIN_BUZZER
  if (_node_prefs) setBuzzerVolumeLevel(_node_prefs->buzzer_volume);   // with a sample at the new level
#endif
}

// The companion CLI's settings keys (ui-core/SettingsCli.h).
bool UITask::cliCommand(const char* command, char* reply, int n) {
  if (!settingscli::handle(*_core, command, reply, n)) return false;
  _next_refresh = 0;   // a screen showing the value draws it anew
  return true;
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
