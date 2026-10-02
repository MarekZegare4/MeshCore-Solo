#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/ContactInfo.h>
#include <Arduino.h>
#include <helpers/sensors/LPPDataHelpers.h>

#ifndef LED_STATE_ON
  #define LED_STATE_ON 1
#endif

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif
#ifdef PIN_VIBRATION
  #include <helpers/ui/GenericVibration.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../Trail.h"
#include "../ui-core/ScreenLock.h"

// Optional M5Stack CardKB (I2C keyboard, addr 0x5F). CARDKB_I2C names which
// TwoWire it lives on -- set at file scope (not inside the class body, and
// not resolved via an #elif ladder) so both this header and SettingsScreen.h
// see a fully-resolved macro no matter which one gets included first.
//  - Boards with a free second I2C bus define ENV_PIN_SDA/ENV_PIN_SCL
//    (already brought up for EnvironmentSensorManager) and get Wire1 here,
//    same as before.
//  - Boards without a free second bus set -D CARDKB_I2C=Wire directly in
//    platformio.ini, sharing whatever bus the display/RTC already use.
// Either way it's a no-op on boards that define neither, or when nothing
// ACKs 0x5F at boot.
#if !defined(CARDKB_I2C) && defined(ENV_PIN_SDA) && defined(ENV_PIN_SCL)
  #define CARDKB_I2C Wire1
#endif
#include "../Waypoint.h"
#include "../LiveTrack.h"
#include "KeyboardWidget.h"
#include "../ui-core/UiCoreHost.h"
#include "../ui-core/Favourites.h"

class UiCore;
struct UiEvent;

// ui-new is the lite frontend of the UI Core: MyMesh talks to the Core (see
// meshListener()), the Core calls back through UiCoreHost and UiEventQueue.
class UITask : public UITaskBase, public UiCoreHost {
  DisplayDriver* _display;
  SensorManager* _sensors;
#ifdef PIN_BUZZER
  genericBuzzer buzzer;
#endif
#ifdef PIN_VIBRATION
  GenericVibration vibration;
#endif
  unsigned long _next_refresh, _auto_off;
  NodePrefs* _node_prefs;
  bool _locked;
  unsigned long _lock_wake_until;  // when to blank screen again after locked wake (5s)
  int  _lock_seq_count;            // Enter presses while Back held (lock/unlock sequence)
  unsigned long _lock_seq_ms;      // millis() of last lock-sequence press (for timeout)
  bool _lock_seq_used;             // true = suppress next back_btn CLICK (post-sequence release)
  // True while the lock screen shows the on-screen keyboard and waits for submission
  bool _unlock_kb = false;
  screenlock::Attempts _pin_tries;
  char _alert[80];
  char _notif_mel_buf[220];  // persistent RTTTL buffer for custom notification melodies
  // Persistent RTTTL buffer for the bot !buzz command (see botBuzz()) -- sized
  // for the full 30s cap: "Buzz:b=120:" (11B) + up to 60 "8c,8p," pairs (6B
  // each) + NUL = 372B, rounded up with margin.
  char _bot_buzz_buf[400];
  KeyboardWidget _kb;        // shared across all screens — only one active at a time
  unsigned long _alert_expiry;
  int _last_notif_ch_idx;
  uint8_t _last_notif_dm_prefix[4];
  bool _last_notif_dm_valid;
  // Shared UI Core (../ui-core): message history + unread models. Heap-allocated
  // in begin(), before any screen, like the screens themselves.
  UiCore* _core = nullptr;
  unsigned long ui_started_at, next_batt_chck;
  uint16_t _batt_mv;  // EMA-filtered battery voltage
  unsigned long next_backlight_btn_check = 0;
#ifdef PIN_STATUS_LED
  int led_state = 0;
  unsigned long next_led_change = 0;
  unsigned long last_led_increment = 0;
#endif

#ifdef PIN_USER_BTN_ANA
  unsigned long _analogue_pin_read_millis = millis();
#endif

  // Registering a new screen touches 4 sites: (1) the member below, (2) the
  // `new XScreen()` in begin(), (3) the gotoXScreen() declaration further down,
  // (4) its one-line definition in UITask.cpp. Sites 1/3/4 are compile-checked;
  // only a forgotten (2) can slip through — the nullptr initialisers here turn
  // that into an inert no-op (see UITask::setCurrScreen) rather than a crash.
  UIScreen* splash = nullptr;
  UIScreen* home = nullptr;
  UIScreen* settings = nullptr;
  UIScreen* messages_screen = nullptr;
  UIScreen* tools_screen = nullptr;
  UIScreen* ringtone_edit = nullptr;
  UIScreen* bot_screen = nullptr;
  UIScreen* admin_screen = nullptr;
  UIScreen* nearby_screen = nullptr;
  UIScreen* dashboard_config = nullptr;
  UIScreen* auto_advert_screen = nullptr;
  UIScreen* batt_curve_screen = nullptr;
  UIScreen* live_share_screen = nullptr;
  UIScreen* locator_screen = nullptr;
  UIScreen* trail_screen = nullptr;
  UIScreen* compass_screen = nullptr;
  UIScreen* status_screen = nullptr;   // Home › Status (radio, GPS, sky, power, mesh, system)
  UIScreen* repeater_screen = nullptr;
  UIScreen* clock_tools = nullptr;
#if defined(PIN_GPIO1)
  UIScreen* gpio_screen = nullptr;
#endif
  UIScreen* curr = nullptr;

  // Runs the UI Core engines and reacts to their events (alert overlay, buzzer,
  // display wake). Driven from loop() regardless of the current screen.
  void     tickCore();
  void     drainCoreEvents();
  void     onMessageArrived(const UiEvent& ev);   // alert + wake + sound for an incoming message



  void userLedHandler();

  // Button action handlers
  char checkDisplayOn(char c);
  char handleLongPress(char c);
  char handleDoubleClick(char c);
  char handleTripleClick(char c);

  // Key FIFO: a burst of taps captured during a blocking refresh (e-ink) is
  // drained from the buttons into this queue, then all keys are applied before
  // a single redraw — so rapid navigation steps neither get lost nor cost one
  // slow refresh each. Also fixes losing a key when two buttons fire in the
  // same loop iteration.
  static const uint8_t KEY_QUEUE_SIZE = 16;
  char _key_queue[KEY_QUEUE_SIZE];
  uint8_t _kq_head = 0, _kq_tail = 0;
  void enqueueKey(char c);
  bool dequeueKey(char& c);

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
public:
  // JS-callable input entry point for the Phase 2 (Emscripten) sim build --
  // see the sim_enqueue_key() EMSCRIPTEN_KEEPALIVE wrapper defined at the
  // bottom of UITask.cpp, which is what a host HTML page's buttons/keyboard
  // listener actually calls. Routes through checkDisplayOn() (wake-on-any-key,
  // same as every other input source below) then the same enqueueKey()
  // choke point every real board's button poll already uses -- this is the
  // sim's substitute for a real board's GPIO/joystick poll, not a new input
  // path of its own.
  void injectSimKey(char c);
  // Same idea, but for a held-down press: routes through handleLongPress()
  // instead of checkDisplayOn() directly -- the real long-press mechanism
  // (KEY_ENTER -> KEY_CONTEXT_MENU, plus the first-8-seconds CLI-rescue
  // gate) every real board's MomentaryButton(pin, 1000, ...) already
  // reaches via BUTTON_EVENT_LONG_PRESS. Without this, a sim instance could
  // never open the context menu at all -- injectSimKey() alone has no way
  // to signal "this press was held".
  void injectSimKeyLongPress(char c);
#ifdef PIN_BUZZER
  // Lets a host page poll the buzzer's current state every frame to drive a
  // Web Audio oscillator (see sim_buzzer_is_playing()/sim_buzzer_freq_hz()
  // in UITask.cpp, next to sim_enqueue_key() -- they live there, not in
  // main.cpp, because the `g_sim_ui_task_for_js` pointer they dispatch
  // through is file-static to that translation unit) -- `buzzer` itself is a
  // private member, so a free function
  // outside this class needs these to reach it, same reason injectSimKey()
  // above is public.
  // Not const: genericBuzzer::isPlaying() itself isn't const-qualified (its
  // NRF52/non-NRF52 siblings don't need to be, so it wasn't worth widening).
  bool isBuzzerPlaying() { return buzzer.isPlaying(); }
  uint16_t buzzerFreqHz() const { return buzzer.currentFreqHz(); }
  uint8_t buzzerVolume() const { return buzzer.getVolume(); }
#endif
private:
#endif

  // Optional M5Stack CardKB (I2C keyboard, addr 0x5F). See the CARDKB_I2C
  // definition near the top of this file for which bus it's on and why.
#if defined(CARDKB_I2C)
  bool     _has_cardkb = false;
  // CardKB is level-triggered, not edge-triggered -- it keeps returning the
  // same byte for as long as the physical key is held, not just once. Track
  // the last raw byte seen so a held key enqueues exactly one press instead
  // of one per poll tick.
  uint8_t  _cardkb_last_raw = 0;
#endif
  void pollCardKB();

  // Optional magnetic "flip cover" lock: a Hall-effect or reed sensor wired to
  // any free GPIO, closing (pulling active) when a magnet is near. Entirely
  // user-supplied -- no board in this repo ships one, so PIN_HALL_SENSOR must
  // be added as a build_flag by whoever wires the sensor up; no-op everywhere
  // else. HALL_ACTIVE_HIGH overrides the default polarity for modules that
  // pull the pin HIGH (instead of LOW) when the magnet is present.
#if defined(PIN_HALL_SENSOR)
#ifndef HALL_ACTIVE_HIGH
  #define HALL_ACTIVE_HIGH 0
#endif
  bool _hall_magnet_present = false;
  // Contact-bounce guard for a mechanical reed switch (a Hall-effect IC reads
  // clean, but the docs recommend either): a raw reading only replaces
  // _hall_magnet_present once it's held steady for HALL_DEBOUNCE_MS, same
  // threshold and reasoning as MomentaryButton's ISR_DEBOUNCE_MS.
  static const uint32_t HALL_DEBOUNCE_MS = 25;
  bool     _hall_candidate = false;
  uint32_t _hall_candidate_since = 0;
#endif
  void pollHallSensor();

  void setCurrScreen(UIScreen* c);

  // Mirror _locked into HomeScreen's LOCK page so the lock screen renders via
  // the normal home render() path (top-right status bar included) instead of a
  // dedicated lock-screen code path in loop().
  void syncLockToHome();

  // Handles (un)locking and requesting password input when one is set
  void toggleLock();
  void beginUnlockPrompt();
  void cancelUnlockPrompt();
  // Handles shortcuts during lockscreen password input
  void handleUnlockKey(char c);

  // Centred alert overlay (the showAlert() box). Wraps long text to up to
  // three lines inside the box instead of letting it overflow the border.
  // Shared by the normal render path and the lock screen (so a ringing
  // alarm's label is visible while locked).
  void renderAlertOverlay();

public:
  // Lock now, from the Home Power panel (unlocking stays the gesture / PIN prompt).
  void lockScreen() { if (!_locked) toggleLock(); }
  // A new screen PIN (see ui-core/ScreenLock.h); "" clears it. The caller saves the prefs.
  void setNodeLockPassword(const char* plain);
  bool passwordLockEnabled() const;

  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : UITaskBase(board, serial), _display(NULL), _sensors(NULL), _node_prefs(NULL) {
    next_batt_chck = _next_refresh = 0;
    ui_started_at = 0;
    _batt_mv = 0;
    _locked = false;
    _lock_wake_until = 0;
    _lock_seq_count = 0; _lock_seq_ms = 0; _lock_seq_used = false;
    _last_notif_ch_idx = -1;
    _last_notif_dm_valid = false;
    memset(_last_notif_dm_prefix, 0, sizeof(_last_notif_dm_prefix));
    curr = NULL;
  }
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void onBLEDisconnected() override { _next_refresh = 0; }

  NodePrefs* getNodePrefs() const { return _node_prefs; }
  // Global metric/imperial preference for distance/speed display.
  bool useImperial() const { return _node_prefs && _node_prefs->units_imperial; }
  uint16_t getBattMilliVolts() const { return _batt_mv > 0 ? _batt_mv : UITaskBase::getBattMilliVolts(); }
  void gotoHomeScreen() { setCurrScreen(home); }
  void gotoSettingsScreen();
  void gotoMessagesScreen();
  void openContactDM(const ContactInfo& ci);
  void openChannelHistory(uint8_t channel_idx);   // Favourites dial: open a pinned channel
  void openRoomServer(const ContactInfo& ci);     // Favourites dial: open a pinned room (logs in first)
  void shareToMessage(const char* text);   // open Messages pre-loaded to share `text`
  void quickShareMyLocation();             // Home Map Hold-Enter: one-shot position share
  void pickLocShareTarget();               // open Messages to choose the live-share target
  void pickFavouriteTarget(int slot);       // open Messages to fill a Favourites dial slot
  void pickBotChannelTarget();             // open Messages to choose the auto-reply bot's channel
  void pickBotRoomTarget();                // open Messages to choose the auto-reply bot's room
  void gotoToolsScreen();
  void gotoRingtoneEditor(int slot = 0);
  void gotoBotScreen();
  void pickAdminTarget();                  // Admin is remote-only: open Nodes to pick a repeater/room
  void openAdminFor(const ContactInfo& ci, bool from_picker); // canonical Admin entry for a specific target (Nodes' Hold-Enter menu or the picker above)
  void gotoNearbyScreen();
  void gotoDashboardConfig();
  void gotoAutoAdvertScreen();
  void gotoBatteryCurve();   // Settings > System > Battery curve
  void gotoLiveShareScreen();
  // Live share session control (ui-core/LiveShareEngine.h): restart = new
  // session (re-announce + fresh clock); clock-only = changed "Stop after".
  void restartLocShareSession();
  void restartLocShareClock();
  void gotoLocatorScreen();
  // Re-arm the locator state machine so the next evaluation initialises
  // silently (called by the Locator tool after the target/radius changes,
  // so re-entering the zone doesn't fire on a stale inside/outside state).
  void resetLocator();
  // The one "active target" the device tracks — shared by the Locator geofence,
  // the Nav bearing/ETA view and (future) the map focus, so every entry point
  // sets the same thing. kind 0 = waypoint (key ignored), 1 = person (key
  // required, 6-byte prefix). setTarget() only *defines* the target (fields +
  // re-arm); the caller decides when to persist. Two commit policies, by
  // context: a screen with an exit hook (LocatorScreen) batches the save so
  // LEFT/RIGHT cycling doesn't thrash flash, while a per-item popup with no
  // such hook uses setTargetNow() to save + confirm on the spot.
  void setTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name);
  void setTargetNow(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name);
  // Unset the active target (locator_has_target = 0). Distinct from setTarget()
  // because there's no "kind" for nothing — clearing is its own operation.
  void clearTarget();
  // One-shot: if the active target is exactly this waypoint, clear it and
  // persist immediately (setTargetNow()'s save-on-the-spot policy) — called
  // from waypoint deletion so the Locator can't keep pointing at a spot
  // that no longer exists.
  void clearTargetIfWaypoint(int32_t lat_1e6, int32_t lon_1e6);
  // Contact / channel removal cleanup lives in the Core (UiCore::onContactRemoved,
  // chanctl::onRemoved).
  // Resolve a person target (6-byte pubkey prefix) to a current position:
  // prefers an active [LOC] live share, falls back to their last-advertised
  // GPS fix. Returns false when neither is known. Optional live/ts report
  // freshness for the picker's age tag. One precedence, used by both the
  // Locator engine (ui-core/LocatorEngine.h) and the target picker.
  bool resolvePersonPos(const uint8_t* key, int32_t& lat, int32_t& lon,
                        bool* live = nullptr, uint32_t* ts = nullptr) const;
  // Resolved position of the active target — a waypoint's coords, or a person
  // via resolvePersonPos(). Gated only on a target being set, independent of
  // whether the Locator alert is enabled, so a destination you set still shows
  // on the map. Used by the Locator engine and the map renderers.
  bool activeTargetPos(int32_t& lat, int32_t& lon) const;
  void gotoTrailScreen();
  void gotoMapScreen();   // opens the Trail screen directly in its Map view
  void gotoCompassScreen();
  void gotoStatusScreen(uint8_t tab);   // StatusScreen::Tab
  UIScreen* statusScreen() const { return status_screen; }   // its sample histories feed the Home tiles
  void gotoRepeaterScreen();
  void gotoGpioScreen();   // no-op on boards without user GPIO pins (see PIN_GPIO1)
  void gotoClockTools();   // Alarm / Timer / Stopwatch (from the home Clock page)
  // Wake the display for an alarm/timer ring (force an immediate refresh).
  void wakeForAlarm();
  // Clear any active alert overlay early (alarm dismiss).
  void clearAlert() { _alert_expiry = 0; }
  // Clock tools engine API (ui-core/ClockEngine.h) — ClockToolsScreen drives
  // these; the engine runs from tickCore() so it fires regardless of the screen.
  void onAlarmChanged();
  void startTimer(uint32_t duration_ms);
  void stopTimer();
  bool isTimerRunning() const;
  uint32_t timerRemainingMs() const;
  bool isRinging() const;
  void dismissRing();
  TrailStore& trail();
  WaypointStore& waypoints();   // the Core's (ui-core/WaypointModel.h)
  LiveTrackStore& liveTrack();
  // Shared on-screen keyboard — only one screen drives it at a time.
  KeyboardWidget& keyboard() { return _kb; }
  void saveWaypoints();
  // Add a waypoint, persist, and show the standard "Waypoint saved" / "Waypoints
  // full" alert. Returns true on success. The ts-less overload uses current RTC time.
  bool addWaypoint(int32_t lat, int32_t lon, uint32_t ts, const char* label);
  bool addWaypoint(int32_t lat, int32_t lon, const char* label);
  // Position / course over ground (ui-core/CourseEngine.h) — shared by the nav
  // / compass / map screens. Course is independent of trail logging.
  bool currentCourse(int& deg_out) const;
  bool currentLocation(int32_t& lat, int32_t& lon) const;
  void playMelody(const char* melody);
  void stopMelody();
  bool isMelodyPlaying();
  void showAlert(const char* text, int duration_millis);
  MyMesh::Listener* meshListener() override;   // MyMesh talks to the UI Core, not to UITask
  // UiCoreHost
  bool isViewingChannel(uint8_t channel_idx) override;
  bool isViewingDM(const uint8_t* pub_key) override;
  void onViewedHistoryGrew(bool channel) override;
  void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) override;
  void onAdminStateChanged() override;
  int  getDMUnreadTotal() const;
  int  getMsgCount() const;
  int  getChannelUnreadCount() const;
  uint8_t getChannelUnread(uint8_t channel_idx) const;
  bool getChannelUnreadOverflow(uint8_t channel_idx) const;
  bool getAnyChannelUnreadOverflow() const;
  int  getRoomUnreadCount() const;
  void clearRoomUnread();
  // Clamped to the DM ring's actual occupancy for this contact (UiCore).
  uint8_t getDMUnread(const uint8_t* pub_key) const;
  bool getDMUnreadOverflow(const uint8_t* pub_key) const;
  bool getAnyDMUnreadOverflow() const;
  // Aggregate for the clock/lock dashboard's single "Msgs" field -- true if
  // ANY channel or DM contact has permanently lost an unread entry to its
  // ring cap (rooms have no local ring to overflow the same way).
  bool getAnyUnreadOverflow() const;
  void clearDMUnread(const uint8_t* pub_key);
  void clearAllDMUnread();
  UiCore& core() { return *_core; }
  bool hasDisplay() const { return _display != NULL; }
  DisplayDriver* getDisplay() const { return _display; }

  // Ping helpers (engine in ui-core/PingEngine.h)
  bool startPing(const uint8_t* pub_key);
  bool isPingActive() const;
  void getPingResult(int16_t& snr_out_x4, int16_t& snr_back_x4, uint32_t& rtt_ms) const;
  void clearPing();

  // Favourites dial helpers. Slot index 0..FAVOURITES_COUNT-1. A slot holds
  // either a contact/room (pubkey prefix) or a channel (index), per
  // favourite_kinds[] — see NodePrefs.
  uint8_t favouriteSlotKind(int slot) const { return favslots::kind(_node_prefs, slot); }
  int findFavouriteSlot(const uint8_t* pub_key) const { return favslots::findContact(_node_prefs, pub_key); }
  int findFavouriteChannelSlot(uint8_t ch_idx) const { return favslots::findChannel(_node_prefs, ch_idx); }
  bool isFavouriteSlotEmpty(int slot) const { return favslots::isEmpty(_node_prefs, slot); }
  void setFavouriteSlot(int slot, const uint8_t* pub_key) { favslots::setContact(_node_prefs, slot, pub_key); }
  void setFavouriteChannelSlot(int slot, uint8_t ch_idx) { favslots::setChannel(_node_prefs, slot, ch_idx); }
  void clearFavouriteSlot(int slot) { favslots::clear(_node_prefs, slot); }
  bool isButtonPressed() const;

  // Lock-screen support: the HomeScreen LOCK page draws the unlock hint from
  // these (the platform combo is either "Back+3xEnter" or the CardKB nybble).
  int  lockSeqCount() const { return _lock_seq_count; }
  bool hasCardKB() const {
#if defined(CARDKB_I2C)
    return _has_cardkb;
#else
    return false;
#endif
  }

  bool isBuzzerQuiet() {
#ifdef PIN_BUZZER
    return buzzer.isQuiet();
#else
    return true;
#endif
  }

  void toggleBuzzer();
  void cycleBuzzerMode();   // ON → OFF → Auto → ON
  int  getBuzzerMode(); // 0=ON, 1=OFF, 2=Auto
  bool getGPSState();
  bool hasGPS();   // true if this board exposes a toggleable GPS (distinct from GPS being off)
  void toggleGPS(bool announce = true);   // announce: the "GPS: Enabled" toast (off where a switch shows it)
  void applyGpsState(bool on, bool announce = true);   // shared by toggleGPS() and botSetGPS()
  void botSetGPS(bool on) override;
  void botBuzz(int seconds) override;
  // User GPIO (!gpio1..!gpio4 + Tools > GPIO screen). idx is 1-4. Bodies are
  // no-ops / return false on boards without PIN_GPIO1 defined.
  bool botSetGPIO(int idx, bool on) override;
  bool botGetGPIO(int idx, bool& is_output, bool& value) override;
  bool botGetGPIOAnalog(int idx, int& millivolts) override;
  bool gpioSupportsAnalog(int idx) const;    // true only for GPIO1/GPIO2 (AIN0/AIN5)
  void setGpioMode(int idx, uint8_t mode);   // 0=Off 1=In 2=Out-low 3=Out-high 4=Analog; applies + persists
  void applyAllGpioModes();                  // boot-time restore from NodePrefs, called from begin()
  void applyBrightness();
  // Settings changed through the schema (ui-core/SettingsSchema.h).
  void applyDisplayPrefs() override { applyBrightness(); _next_refresh = 0; }
  void applySoundPrefs() override;
  void setBuzzerVolumeLevel(uint8_t level);
  void applyTxPower();
  void applyPowerSave();   // hardware duty-cycle RX on/off from prefs
  void applyApc();         // Adaptive Power Control on/off from prefs
  void applyRadioParams(); // freq/bw/sf/cr from prefs (radio preset change)
  // Save-on-exit helper for the screen `_dirty` pattern: persists NodePrefs once
  // only if `dirty`, then clears the flag. Standardises the screens' exit paths
  // (some used to leave the flag set, relying on onShow() to reset it) and keeps
  // the "did we touch flash?" answer in one place. Returns whether it saved.
  bool savePrefsIfDirty(bool& dirty);
  void applyRotation();
  void applyFullRefreshInterval();
  uint32_t autoOffMillis() const {
    if (!_node_prefs || _node_prefs->auto_off_secs == 0) return 0;
    return (uint32_t)_node_prefs->auto_off_secs * 1000UL;
  }


  // from UITaskBase
  void notify(UIEventType t = UIEventType::none) override;
  void loop() override;
  // Send one [LOC] message to the configured live-share target. Returns false
  // if the target can't be resolved (no such channel / contact).
  bool sendLocationShare(int32_t lat, int32_t lon);

  void shutdown(bool restart = false);
};
