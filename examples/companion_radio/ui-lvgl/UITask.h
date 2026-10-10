#pragma once
// ui-lvgl: the rich (colour + touch) frontend of the UI Core. Wio Tracker L2
// first. All application state lives in the Core (../ui-core); this class owns
// only LVGL screens, display power and input. See docs/solo/developer/ui-core.md.

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <lvgl.h>
#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "../AbstractUITask.h"
#include "../NodePrefs.h"
#include "../ui-core/UiCoreHost.h"
#include "../ui-core/ScreenLock.h"

class UiCore;
class NearbyModel;
struct UiEvent;

namespace mapview { struct TileArea; }

// A popup that asks for one line of text: the field over the keyboard. `done`
// gets the keyboard's READY (OK) and CANCEL; the field is _nav_ta, the keyboard
// _nav_kb, until navClosePopup().
struct TextEntry {
  const char* title;
  lv_event_cb_t done;
  const char* text = nullptr;       // what the field starts with
  const char* hint = nullptr;       // placeholder
  size_t max_bytes = 0;             // UTF-8 bytes the field takes; 0 = no limit
  bool password = false;
  const char* accepted = nullptr;   // only these characters (lv_textarea_set_accepted_chars)
  bool symbols = false;             // open on the digits / symbols page
  bool bare = false;                // no titled panel: the field straight on the dim, for the caller to add rows under it (returns the overlay)
};

class UITask : public UITaskBase, public UiCoreHost {
public:
  UITask(mesh::MainBoard* board, BaseSerialInterface* serial) : UITaskBase(board, serial) {}

  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);
  void loop() override;
  void notify(UIEventType t = UIEventType::none) override;
  void shutdown(bool restart = false) override;
  MyMesh::Listener* meshListener() override;
  // The screen as it is now: RGB565 (native, little-endian), row-major, w x h.
  // For CMD_GET_SCREENSHOT (MyMesh, -D ENABLE_SCREENSHOT, display type 2);
  // nullptr without the memory for it. The caller frees it with free().
  uint16_t* captureFrame(int& w, int& h);
#define UI_SCREENSHOT_RGB565 1

  // UiCoreHost
  bool isViewingChannel(uint8_t channel_idx) override;
  bool isViewingDM(const uint8_t* pub_key) override;
  void onViewedHistoryGrew(bool channel) override { (void)channel; _thread_dirty = true; }
  void botSetGPS(bool on) override;
  void applySoundPrefs() override;
  bool cliCommand(const char* command, char* reply, int n) override;

  // ── Navigation (called from LVGL event callbacks) ────────────────────────
  void showHome();
  void setHomePage(int page);
  void homeEdit(bool on);   // Home's apps: drag to move, tap to hide (hold an app, or Settings > Home apps)
  void homeAppDrop(int from, int to);
  void homeAppToggle(int pos);
  void goHome();                   // the side button: Home's clock page
  // The quick panel, pulled down from the status bar (QuickPanel.h)
  void quickPanelBuild();
  bool quickPanelBegin();
  void quickPanelClose(bool animate);
  bool quickPanelOpen() const;
  void quickPanelRefresh();
  void quickPanelTile(int t, bool hold);
  void homeFieldSet(int slot, int field);
  void showChats();
  void showContacts();
  void showSettings();
  void showSchemaSettings(int page);
  // Home > Admin: the repeaters / room servers to log in to (AdminScreen.h)
  void showAdminPick();
  // Settings > System > Firmware update (OtaScreen.h)
  void showOta();
  void otaAction();
  void adminPick(int row);
  void pruneContacts();
  // Settings > Privacy: the position adverts carry with the GPS off, the
  // Bluetooth PIN; Settings: factory reset, asked twice (stage 1, 2; 3 erases).
  void ownPositionPopup();
  void ownPositionPick(int code);
  void ownPositionDone(bool ok);
  void blePinPopup();
  void blePinDone(bool ok);
  void factoryResetStep(int stage);
  void setBrightnessPct(uint8_t pct, bool save);
  void applyDisplayPrefs() override;
  void setSchemaValue(int idx, int v);
  void setMsgBanner(bool on);   // Settings > Display > Message banner
  void showNearby();
  void showMap();                  // in its current mode
  void openMap(bool nav);          // the one map; false: from Nodes, with the nodes on it and Back to Nodes
  void mapLongPress(int x, int y);
  void mapZoom(int delta);
  void mapCenterOnMe();
  void mapPan(int dx, int dy);
  void mapOpenMarker(int idx);
  void mapDownloadPopup();
  // Map areas (MapAreas.h)
  void areaBuildLayer(lv_obj_t* body);
  void areaSelectBegin();
  void areaSelectEnd();
  bool areaSelecting() const;
  void areaSelectDownload();
  void areaSelectZmax(int d);
  void areaHandleDrag(int corner, int dx, int dy);
  void areaLayout();
  bool areaStartDownload(const mapview::TileArea& box, bool force, bool trails);
  void mapAreasPopup();
  void mapAreaPopup(int idx);
  void mapAreaAction(uint8_t act);
  void mapRegionsPopup();          // Map tools > Vector regions (MapRegions.h)
  void mapRegionPopup(int idx);
  void mapRegionAction(uint8_t act);
  void mapAreaRenameDone(bool ok);
  void areaPreviewEnd();   // back to the view from before a map area's preview
  int  mapZoomLevel() const;
  void setTapWake(bool on);
  void mapDownloadClose();
  void mapDownloadStart();
  void mapDownloadStop();
  void mapDownloadResume();
  void mapDownloadDiscard();
  void setLiveTiles(bool on);
  void setTrails(bool on);
  void setVectorMap(bool on);
  void simMapAt(const char* spec);      // Map tools > Live tiles
  void simCompose(const char* text);    // the open thread's field: this text, keyboard up
  void simDm();                         // the first chat contact's thread
  void simUnread();                     // read and new messages to open on
  void simSleep(bool on) { if (on) sleep(); else wake(); }
  void simLock(bool on) { if (on) lockScreen(); else unlockScreen(); }
  void simSend() { sendFromCompose(); } // the field, sent
  // Navigation map (NavMap.h)
  void navTargetsPopup();
  void navClosePopup();
  void navPick(int code);
  void navWaypointMenu(int idx);
  void navWaypointAction(uint8_t act);
  void navRenameDone(bool ok);
  void navMarkHere();
  void navAveragingCancel();
  void navToNode(const uint8_t* key, int32_t lat, int32_t lon, const char* name);
  void navToolsPopup();
  void navToolAction(uint8_t act);
  void navSetShareTarget(int sel);
  void navWaypointsPopup();
  void navCoordsPopup();
  void navCoordsDone(bool ok);
  void shareToMessage(const char* text);
  void messageLocationAction(int idx, bool save);
  void showWifi();
  void wifiSetAllowed(bool on);
  void wifiScan();
  void wifiPick(int idx);
  void wifiOpenSaved(int idx);   // a saved network into the fields, to change its password
  void wifiTestShow(const char* msg, const char* ssid, const char* pass);   // a password check's outcome
  void wifiForget(int idx);      // (its button asks first: WifiScreen.h)
  void wifiSave();
  void wifiEdit(lv_obj_t* ta);
  void wifiKeyboardHide();
  void setNearbyFilter(uint8_t f);
  void toggleNearbySort();
  void startNearbyScan();
  void closeScanPopup();
  void openScanNode(int row);
  void openNode(int row);
  void nodeAction(uint8_t action);
  void setKeyboardAlphabets(int main_idx, int alt_sel);
  void openChannel(uint8_t channel_idx);
  void openDM(const uint8_t* pub_key);
  void bannerOpen();   // the message banner tapped: its conversation
  void back();
  void sendFromCompose();
  int composeLimit() const;                 // bytes the open thread's message may take
  int composeLen(const char* typed) const;  // what `typed` takes once sent
  void setKeyboardVisible(bool show);
  void showToast(const char* text, uint32_t ms = 2500);
  // Clock tools (ClockScreen.h)
  void showClock();
  void clockTab(int tab);
  void clockAction(uint8_t act);
  void setAlarm(int which, int v);
  void dismissRing();
  // Sound (SoundScreen.h): the speaker, Settings > Sound, the melody editor
  void playMelody(const char* melody);   // through mute (alarms, previews)
  void stopMelody();
  bool melodyPlaying();
  int melodyNote();                      // the note sounding now, -1 silent
  void setSoundMode(int mode);
  void setSoundVolume(int level);
  void showMelodies(int slot);
  void melodySlot(int slot);
  void melodyPick(int idx);
  void melodySet(uint8_t which, int v);
  void melodyAction(uint8_t act);
  void conversationMelody(int v);
  void hearMelody(int slot);
#ifdef PIN_BUZZER
  // For the sim page's Web Audio speaker (sim_buzzer_* in UITask.cpp).
  bool isBuzzerPlaying() { return _buzzer.isPlaying(); }
  uint16_t buzzerFreqHz() const;
  uint8_t buzzerVolume() const { return _buzzer.getVolume(); }
#endif
  // Quick messages, placeholders, advert, Bluetooth (QuickScreen.h)
  void quickPopup();                 // the compose bar's "+"
  void quickSend(int slot);
  void quickInsert(int ph);
  void showQuickMsgs();
  void quickEdit(int slot);
  void quickEditDone(bool ok);
  void quickEditInsert(int ph);
  void advertPopup();
  void advertSend(bool flood);
  void setBluetooth(bool on);
  void markAllRead();
  // Settings > Radio (RadioScreen.h)
  void showRadio();
  void radioSet(int which, int v);
  void radioFreqPopup(bool repeater);   // the companion's frequency, or the repeater profile's
  void radioFreqDone(bool ok);
  // Settings > Radio: my presets, scopes (RadioExtras.h)
  void presetMenu(int slot);
  void presetAction(uint8_t act);
  void radioNamePopup(uint8_t what);
  void radioNameDone(bool ok);
  void showScopes();
  void scopeMenu(int idx);
  void scopeAction(uint8_t act);
  // Home > Repeater (RepeaterScreen.h)
  void showRepeater();
  void repeaterSet(int which, int v);
  void repeaterScopesPopup();
  void repeaterScopeSet(uint8_t i, bool on);
  // Channels (ChannelScreen.h)
  void channelMenu(int idx);
  void channelSet(uint8_t which, int v);
  void channelAction(uint8_t act);
  void showChannelEdit(int idx);
  void channelEditType(int type);
  void channelEditHex(bool hex);
  void channelEditField(lv_obj_t* ta);
  void channelEditKbHide();
  void channelEditSave();
  // Remote admin (AdminScreen.h)
  void openAdmin(const uint8_t* pub_key);
  void adminTab(int tab);
  void adminRow(int row);
  void adminValue(uint8_t act);
  void adminChoice(int idx);
  void adminLogin(bool ok);
  void adminTextDone(bool ok);
  void adminCancelWait();
  // Bot settings (BotScreen.h)
  void showBot();
  void botTab(int tab);
  void botRow(int row);
  void botToggle(int row, bool on);
  void botPick(int idx);
  void botHour(uint8_t act);
  void botTextDone(bool ok);
  // ConversationScreen.h
  void openRoom(const uint8_t* pub_key);
  void roomLoginPopup(const uint8_t* pub_key);
  void roomLoginDone(bool ok);
  void conversationMenu(const uint8_t* pub_key);   // nullptr: the open thread's
  void conversationAction(uint8_t act);
  void conversationNotif(int v);
  void messageMenu(int idx);
  void messagePopupTick();
  void messageAction(uint8_t act);
  void chatFold(int which);
  void toggleChatFilter(uint8_t which);
  // DeviceScreen.h
  void nodeNamePopup();
  void nodeNameDone(bool ok);
  void powerPopup(bool restart);
  void unlockScreen();
  void threadPage(int dir);           // +1 older, -1 newer
  // Saved trails on the SD card (NavMap.h)
  bool saveTrailToCard(char* name_out, size_t n);
  void savedTrailsPopup();
  void savedTrailPopup(int idx);      // -1 = the device's own slot
  void savedTrailAction(uint8_t act);
  void navFrameTrail();
  // Settings > Storage (StorageScreen.h)
  void showStorage();
  void showGps(bool from_settings);
  void buildGps();
  void refreshGps();
  void buildStorage();
  void pollStorage();
  void storageKeep(int idx);
  void storageClearHistory();
  void storageLiveCap(int idx);
  void storageClearLive();
  void storageFormatAsk();    // Storage > Format card: the popup (first confirmation)
  void storageFormatTap(bool go);   // its buttons: Format twice (the second confirmation), or Cancel
  void usbPoll();              // a computer plugged in: offer the SD card as a USB drive
  void usbTap(bool drive);     // that popup's buttons
  bool locked() const;
  void pinKey(const char* key);        // the lock screen's keypad
  void pinSetupPopup();                // Settings > Display > Screen PIN
  void pinSetupKey(const char* key);
  void pinRemove();
  void pinRowRefresh();
  void accentRow(lv_obj_t* body);   // Settings > Display > Accent colour
  void setAccent(int idx);
  void showDiag();
  void diagTab(int tab);
  void diagResetPopup();
  void diagReset();
  void diagNoiseRun();
  void showCompass();
  void showBattCurve();            // Settings > Power > Battery curve (BatteryScreen.h)
  void refreshBattCurve();
  int  battMv() const { return _batt_mv; }   // smoothed cell voltage, 0 before the first read
  NodePrefs* prefsMut() const { return _prefs; }   // for the free helpers that change a setting
  void favTap(int slot);
  void favHold(int slot);
  void favAction(uint8_t act);
  void favPick(int code);
  void pinPopup(bool channel, uint8_t ch_idx, const uint8_t* pub_key);
  void pinTo(int slot);
  void setGps(bool on);
  bool ensureGps();   // true with a fix; else turns GPS on / says it's waiting

  // Kept for main.cpp's sim hooks (ui-new API).
  void openContactDM(const ContactInfo& ci) { openDM(ci.id.pub_key); }
  void openAdminFor(const ContactInfo& ci, bool from_picker) { (void)from_picker; openAdmin(ci.id.pub_key); }

private:
  enum Screen : uint8_t { SCR_HOME, SCR_CHATS, SCR_CONTACTS, SCR_THREAD, SCR_SETTINGS, SCR_NEARBY, SCR_NODE, SCR_MAP, SCR_WIFI, SCR_SETTINGS_NAV, SCR_CLOCK, SCR_RADIO, SCR_CHANNEL_EDIT, SCR_ADMIN, SCR_BOT, SCR_DIAG, SCR_COMPASS, SCR_SCOPES, SCR_REPEATER, SCR_MELODY, SCR_QUICK, SCR_ADMIN_PICK, SCR_OTA, SCR_STORAGE, SCR_GPS, SCR_BATT };

  void buildStatusBar();
  void refreshStatusBar();
  lv_obj_t* newScreen(const char* title, bool with_back);
  void showSplash();   // Splash.h
  // The screen being shown: newScreen()'s, already while it slides in (popups
  // and keyboards go on it, not on the one sliding out).
  lv_obj_t* screen() const { return _scr ? _scr : lv_screen_active(); }
  void buildHome();
  void buildHomeClock(lv_obj_t* box);
  void buildHomeMap(lv_obj_t* box);
  void homeFieldText(uint8_t field, char* buf, int n);
  bool homeMapFit();
#ifdef UI_PERF_TEST
  void perfMap(bool reset = false);
  void perfWalk();   // PerfTest.h: loop()'s self-driving walk through the screens
  void perfNearbyAgain() { _nearby_sig = 0; refreshNearbyList(); }
#endif
  void homeMapLayout();
  void homeMapLoop();
  void refreshHome();
  void homeSwipePoll();
  void buildChats();
  void refreshChats();
  void fillChats();
  void buildContacts();
  void fill(void (UITask::*fn)(int), int from, int to);
  void then(void (UITask::*fn)(int), int arg);
  void fillStart(int n, int first, void (UITask::*row)(int));
  void fillStep();
  void fillTick();
  void fillFlush();
  int fillPending() const;
  void buildSettings();
  static const int SETTINGS_GROUPS = 5;
  void settingsGroup(int i);
  void buildSchemaSettings();
  void schemaRows(lv_obj_t* body, uint8_t page);
  void schemaSection(int i);
  void schemaExtras(int page);
  void showMapOptions(uint8_t section);
  void schemaRow(lv_obj_t* card, int idx);
  void msgBannerRow(lv_obj_t* card);
  void buildKeyboardPage(lv_obj_t* body);
  void buildAboutPage(lv_obj_t* body);
  void buildAdminPick();
  void buildOta();
  void refreshOta();
  void otaTick();
  void otaLeave();
  bool otaBusy() const;
  bool wifiInUse() const;   // a map download or an update holds the WiFi
  void buildNearby();
  void refreshNearbyList();
  uint32_t nearbySignature() const;
  void showScanPopup();
  void refreshScanPopup();
  void buildMap();
  void layoutMap();
  void mapLoop();
  void rebuildMapMarkers();
  void rebuildNodeMarkers();
  void addMapMark(uint8_t kind, int idx, int32_t lat_e6, int32_t lon_e6, uint32_t col, const char* text);
  void mapPointToLatLon(int x, int y, int32_t& lat_e6, int32_t& lon_e6) const;
  double mapCenterBias() const;
  void buildNavLayers();
  void buildNavControls(lv_obj_t* body);
  void rebuildNavMarkers();
  bool navTrailSync();
  bool trailJournalTick(bool now = false);   // NavMap.h: the live trail's copy on the card
  void trailJournalRestore();
  void layoutNav();
  void refreshNavBar();
  void navFrameTarget();
  void navSetTarget(uint8_t kind, const uint8_t* key, int32_t lat, int32_t lon, const char* name);
  lv_obj_t* navPopupPanel(const char* title, bool full, bool bottom = false);
  lv_obj_t* navTextEntry(const TextEntry& t);   // popup + one-line field + keyboard (NavMap.h)
  void navRenamePopup(int idx);
  void navAddWaypoint(int32_t lat, int32_t lon);
  void navDropAt(int x, int y);
  void navSpotPopup(int32_t lat, int32_t lon);
  void placeCard(lv_obj_t* parent, int32_t lat, int32_t lon);
  void refreshNavTools();
  void navPollAveraging();
  void navPollTrackBack();
  bool navCurrentTarget(int32_t& lat, int32_t& lon, char* name, int n, bool& person, bool& set);
  bool exportTrailGpx(char* name_out, size_t n);
  void mapDownloadTick();
  void mapLiveBegin();
  void refreshDownloadPopup();
  void buildWifi();
  void pollWifiScan();
  void buildNode();
  void refreshNode();
  void buildThread();
  void threadOpenAt(int first);
  void reopenThreadIfUnread();
  bool threadInView() const;   // a thread on a lit, unlocked screen
  void refreshThread();
  void refreshThreadAges();
  uint32_t threadSignature() const;
  void buildClock();
  void buildRadio();
  void rebuildRadio();
  void rebuildSchemaSettings();
  void rebuildSoon(void (UITask::*fn)());   // after the switch that asked for it has moved
  void radioGroup(int i);
  void buildRadioExtras(lv_obj_t* body);
  void buildScopes();
  void buildRepeater();
  void rebuildRepeater();
  void repeaterGroup(int i);
  void repeaterRelays();
  void buildSoundRows(lv_obj_t* body, bool top);
  void buildMelodies();
  void buildQuickMsgs();
  bool sendThreadText(const char* text);
  void refreshMelody();
  void melodySave();
  void radioCloseFreq();
  void buildChannelEdit();
  void buildAdmin();
  void buildBot();
  void refreshAdminStatus();
  void adminLeave();
  void adminPoll();
  void roomPoll();
  void lockScreen();
  void refreshLock();
  void lockPoll();
  void favGrid(lv_obj_t* grid);
  void favRefresh();
  int unreadTotal();   // DMs, channels and rooms
  void buildDiag();
  void diagSection(int sec);
  void refreshDiag();
  void sampleHistory();            // DiagScreen.h: History's readings, from loop()
  void fillHistory();
  void buildCompass();
  void refreshCompass();
  void favPickPopup(int slot);
  void adminValuePopup();
  void adminTextPopup(const char* text, bool digits);
  void adminReplyPopup(const char* text);
  bool radioPopupOpen() const;
  void refreshClock();
  void showRing(const char* text);
  void hideRing();
  void drainCoreEvents();
  void onMessageArrived(const UiEvent& ev);
  void wake();
  void sleep();
  void toggleMute();
  void takeScreenshot();
  uint32_t _next_wake_poll_ms = 0;
  uint32_t _next_touch_poll_ms = 0;   // tap to wake, while the screen is off
  bool _wake_down = false;
  uint32_t autoOffMillis() const;
  uint32_t idleMillis(uint32_t lv_next);
  bool cpuNeeded();
  void checkLowBattery();

#ifdef PIN_BUZZER
  genericBuzzer _buzzer;
  char _notif_mel_buf[220];    // a user melody's RTTTL while it plays (the player copies it)
#endif
  // Who the message being notified came from (SoundNotifier's overrides).
  bool    _notif_dm_valid = false;
  uint8_t _notif_dm_prefix[4] = {0};
  int     _notif_ch_idx = -1;

  DisplayDriver* _display = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs*     _prefs = nullptr;
  bool _gps_from_settings = false;   // GPS screen: Back returns there (else Home)
  UiCore*        _core = nullptr;

  Screen   _screen = SCR_HOME;
  // What newScreen() last loaded, for the slide direction (Anim.h)
  Screen    _shown_screen = SCR_HOME;
  char      _shown_title[32] = "";
  bool      _nav_back = false;       // set by back() around the screen it shows
  bool      _fade_next = false;      // newScreen() cross-fades the next screen in (the side button)
  lv_obj_t* _scr = nullptr;          // the screen newScreen() built (active once its slide starts)
  static int screenDepth(Screen s);
  bool     _asleep = false;
  uint32_t _next_status_ms = 0;
  uint32_t _next_thread_check_ms = 0;
  uint32_t _next_thread_ages_ms = 0;
  uint32_t _next_trackback_ms = 0;
  uint32_t _next_clock_ms = 0;
  uint8_t  _settings_page = 0;   // settings::Page shown in SCR_SETTINGS_NAV
  lv_obj_t* _ota_status = nullptr;
  lv_obj_t* _ota_bar = nullptr;
  lv_obj_t* _ota_btn = nullptr;
  lv_obj_t* _ota_btn_lbl = nullptr;
  uint32_t _map_left_ms = 0;          // when the map was left, for dropping live tiles' WiFi
  bool     _admin_from_pick = false;   // Admin opened from its Home tile (back returns there)
  uint16_t _batt_mv = 0;         // smoothed, read every 8 s: the status bar and the low-battery shutdown
  uint32_t _next_batt_ms = 0;
  lv_obj_t* _prune_lbl = nullptr;
  // Screen PIN (DeviceScreen.h): a salted hash in NodePrefs (ui-core/ScreenLock.h)
  bool pinSet() const { return _prefs && screenlock::isSet(*_prefs); }
  bool      _tap_wake = true;      // a touch wakes the dark screen (NVS)
  char      _pin_entry[9] = "";    // digits typed so far
  char      _pin_new[9] = "";      // setup: the first entry, waiting for its confirmation
  screenlock::Attempts _pin_tries;

  // Open conversation (SCR_THREAD)
  bool     _thread_is_channel = false;
  int      _thread_skip = 0;          // newest messages above the page shown (Older / Newer)
  bool     _thread_scroll_top = false;
  int      _thread_new = -1;          // on opening: newest-first index of the first unread (-1: none)
  uint32_t _thread_new_key = 0;       // and that message, marked with "New" while the thread is open
  bool     _thread_new_on = false;
  int      loadThreadPage(int& total);
  uint8_t  _thread_channel = 0;
  uint8_t  _thread_key[PUB_KEY_SIZE] = {0};
  bool     _thread_dirty = false;
  uint32_t _thread_sig = 0;

  // Widgets
  lv_obj_t* _status_time = nullptr;
  lv_obj_t* _status_icons = nullptr;   // a row of icon labels, rebuilt with the state
  lv_obj_t* _status_batt = nullptr;
  lv_obj_t* _status_chg = nullptr;    // the charging bolt, green, left of the battery
  char      _status_sig[128] = "";      // what the icon row shows, to skip unchanged rebuilds
  lv_obj_t* _toast = nullptr;
  lv_timer_t* _toast_timer = nullptr;
  lv_obj_t* _home_clock = nullptr;
  lv_obj_t* _home_date = nullptr;
  lv_obj_t* _home_unread = nullptr;
  lv_obj_t* _header = nullptr;    // current screen's title bar (nullptr on Home)
  lv_obj_t* _body = nullptr;
  lv_obj_t* _thread_list = nullptr;
  lv_obj_t* _compose_ta = nullptr;
  lv_obj_t* _keyboard = nullptr;

  // Nearby (SCR_NEARBY) / node detail (SCR_NODE); list data in NearbyModel
  NearbyModel* _nearby = nullptr;
  NearbyModel* _scan = nullptr;    // NODE_DISCOVER results: nodes in range now (popup over the list)
  bool      _scanning = false;
  bool      _node_from_scan = false;
  uint32_t  _scan_sig = 0;
  lv_obj_t* _scan_overlay = nullptr;
  lv_obj_t* _scan_list = nullptr;
  lv_obj_t* _scan_status = nullptr;
  uint32_t  _scan_until_ms = 0;
  uint32_t  _nearby_sig = 0;
  uint32_t  _next_nearby_ms = 0;
  bool      _pinging = false;
  uint32_t  _ping_started_ms = 0;
  uint32_t  _ui_started_ms = 0;   // begin(): the side button held soon after is the CLI rescue
  lv_obj_t* _nearby_list = nullptr;
  lv_obj_t* _nearby_status = nullptr;
  lv_obj_t* _nearby_sort_lbl = nullptr;
  lv_obj_t* _nearby_chips = nullptr;
  lv_obj_t* _node_info = nullptr;
  lv_obj_t* _node_ping = nullptr;
  lv_obj_t* _node_delete_lbl = nullptr;
  lv_obj_t* _node_path_lbl = nullptr;
  bool      _node_from_map = false;

  // Map (SCR_MAP): view centre in fractional tile coords at zoom _map_z
  double    _map_cx = 0, _map_cy = 0;
  int       _map_z = 0;             // 0 = not initialised yet
  bool      _map_follow = true;     // keep centred on own position until panned
  bool      _map_pending = false;   // visible tiles still to decode
  uint32_t  _next_map_marks_ms = 0;
  lv_obj_t* _map_area = nullptr;
  lv_obj_t* _map_tiles[6] = {nullptr};
  lv_obj_t* _map_marks = nullptr;
  lv_obj_t* _map_me = nullptr;
  lv_obj_t* _map_zoom_lbl = nullptr;
  lv_obj_t* _map_center_btn = nullptr;   // accent while the map follows you
  uint32_t  _next_follow_ms = 0;
  lv_obj_t* _map_hint = nullptr;
  lv_obj_t* _map_dl_pill = nullptr;     // download progress over the map
  lv_obj_t* _dl_overlay = nullptr;      // download popup
  lv_obj_t* _dl_info = nullptr;
  lv_obj_t* _dl_bar = nullptr;          // progress while downloading
  lv_obj_t* _dl_err = nullptr;          // failures, one line
  lv_obj_t* _dl_sub = nullptr;
  lv_obj_t* _dl_start_lbl = nullptr;
  lv_obj_t* _dl_job_row = nullptr;      // "Unfinished ... Resume" in the popup
  lv_obj_t* _dl_job_lbl = nullptr;
  uint8_t   _dl_last_state = 0;

  // Navigation map (SCR_MAP with _map_nav)
  bool      _map_nav = false;
  bool      _map_nodes = false;       // opened from Nodes: their markers too, Back returns there
  uint32_t  _next_nav_bar_ms = 0;
  lv_obj_t* _nav_bar = nullptr;       // target bar along the bottom
  lv_obj_t* _nav_title = nullptr;
  lv_obj_t* _nav_info = nullptr;
  lv_obj_t* _nav_clear = nullptr;
  lv_obj_t* _nav_overlay = nullptr;   // target list / waypoint menu / rename
  lv_obj_t* _nav_ta = nullptr;
  lv_obj_t* _nav_kb = nullptr;
  lv_obj_t* _nav_del_lbl = nullptr;
  int       _nav_wp = -1;             // waypoint the menu / rename is about
  lv_obj_t* _nav_rec = nullptr;       // "REC 1.2 km  LIVE 58m" pill
  lv_obj_t* _nav_avg_pill = nullptr;  // GPS averaging progress (tap: cancel)
  lv_obj_t* _nav_trail_lbl = nullptr; // tools panel
  lv_obj_t* _nav_trail_btn = nullptr;
  lv_obj_t* _nav_reset_lbl = nullptr;
  lv_obj_t* _nav_share_lbl = nullptr;
  lv_obj_t* _nav_share_btn = nullptr;
  lv_obj_t* _nav_tb_btn = nullptr;
  int32_t   _nav_spot_lat = 0, _nav_spot_lon = 0;   // the spot a long-press picked
  char      _share_text[96] = "";     // waiting for a conversation to be picked (shareToMessage)

  // WiFi settings (SCR_WIFI)
  bool      _wifi_scanning = false;
  lv_obj_t* _wifi_ssid = nullptr;
  lv_obj_t* _wifi_pass = nullptr;
  lv_obj_t* _wifi_kb = nullptr;
  lv_obj_t* _wifi_list = nullptr;
  lv_obj_t* _wifi_status = nullptr;
};
