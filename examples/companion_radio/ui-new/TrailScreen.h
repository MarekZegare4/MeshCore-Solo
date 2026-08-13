#pragma once
// GPS trail viewer. Tools › Trail.
// Three live views — Summary, Map, List — cyclable with LEFT/RIGHT.
// All settings and actions live in the Hold-Enter popup so a short Enter never
// accidentally stops tracking.
// Included by UITask.cpp after Trail store + ToolsScreen.

#include "../Trail.h"
#include "../GeoUtils.h"
#include "GfxUtils.h"
#include "icons.h"     // scalable mini-icons (map markers, north arrow, grid dots)
#include "NavView.h"
#include "WaypointsView.h"
#include <helpers/StreamUtils.h>
#include <math.h>

// Bounds every Serial write the GPX export makes. A full trail dump is tens
// of KB across hundreds of individual write() calls (one or more per point) —
// without this, a serial monitor that's open but not actively reading (DTR
// asserted, nothing draining the USB-CDC FIFO) would block each one, and the
// main loop with it, indefinitely (see StreamUtils.h). Duck-typed to the
// handful of Print methods Trail.h's GPX writers actually call, so it slots
// into their `S& out` template parameter without any change there.
// __FlashStringHelper content is plain flash on nRF52/ARM (no
// Harvard-architecture PROGMEM split), so it's safe to read like any other
// char*.
//
// The first stalled write gives up for the rest of the export (_gave_up),
// rather than re-arming a fresh timeout on every one of those hundreds of
// calls — a host that's truly stuck would otherwise cost calls × timeout_ms
// in total instead of one bounded stall.
struct BoundedSerialPrint {
  uint32_t timeout_ms;
  bool _gave_up;
  BoundedSerialPrint(uint32_t t) : timeout_ms(t), _gave_up(false) {}
  size_t write(const uint8_t* buf, size_t len) {
    if (_gave_up) return 0;
    size_t sent = streamWriteUntil(Serial, buf, len, millis() + timeout_ms);
    if (sent < len) _gave_up = true;
    return sent;
  }
  size_t print(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const __FlashStringHelper* s) {
    const char* p = reinterpret_cast<const char*>(s);
    return write((const uint8_t*)p, strlen(p));
  }
};

class TrailScreen : public UIScreen {
  UITask*     _task;
  TrailStore* _store;

  enum View { V_SUMMARY = 0, V_MAP = 1, V_LIST = 2, V_COUNT };
  uint8_t _view           = V_SUMMARY;
  int     _summary_scroll = 0;
  int     _list_scroll    = 0;
  // Top-of-scroll bound for each view, recomputed every render() from the
  // live display height (mirrors how _scroll itself is reclamped) — lets
  // handleInput() wrap top<->bottom without duplicating the layout math.
  int     _summary_max_scroll = 0;
  int     _list_max_scroll    = 0;
  bool    _cfg_dirty      = false;
  bool    _map_grid       = true;   // show scale grid in map view (Enter to toggle)
  bool    _return_home    = false;  // entered via Home "Map" page → KEY_CANCEL returns to Home

  // Waypoint management UI (list / nav / add / mark / rename / delete / send)
  // lives in its own component; TrailScreen delegates to it while it's active.
  WaypointsView _wp;

  // Action popup (Hold Enter / KEY_CONTEXT_MENU). Carries both settings rows
  // (Min dist, Units — cycled with LEFT/RIGHT while focused) and actions
  // (Start/Stop tracking, Reset). PopupMenu stores label pointers verbatim, so
  // the labels live in member buffers that get refreshed in openActionMenu()
  // and after every LEFT/RIGHT cycle.
  enum ActionId { ACT_MIN_DIST, ACT_UNITS, ACT_GRID, ACT_AUTOPAUSE, ACT_MARK_AVG, ACT_AUTOSAVE, ACT_TOGGLE, ACT_SAVE, ACT_LOAD, ACT_RESET, ACT_EXPORT, ACT_EXPORT_SAVED, ACT_MARK, ACT_WAYPOINTS, ACT_FILE, ACT_SETTINGS,
                 ACT_SHARE_NOW, ACT_TRACKBACK };
  // The action popup is multi-level: a short main menu, plus "Trail file…" and
  // "Settings…" submenus. _menu_level tracks which is open so input is routed
  // correctly (settings rows cycle with LEFT/RIGHT; everything else is Enter).
  // Live-share *config* lives in its own tool (Tools › Live Share); the map only
  // keeps the one-shot "Share my pos" action.
  enum MenuLevel { ML_MAIN, ML_FILE, ML_SETTINGS, ML_CONFIRM_GPS };
  PopupMenu _action_menu;
  uint8_t   _menu_level = ML_MAIN;
  uint8_t   _act_map[16];  // max rows on any one level; pushAction guards the cap
  uint8_t   _act_count = 0;
  char      _act_min_dist_label[24];
  char      _act_units_label[24];
  char      _act_grid_label[16];
  char      _act_autopause_label[24];
  char      _act_mark_avg_label[24];
  char      _act_autosave_label[24];
  char      _act_toggle_label[20];

  static const int SUMMARY_ITEM_COUNT = 5;

public:
  TrailScreen(UITask* task, TrailStore* store)
    : _task(task), _store(store), _wp(task, store) {}

  void onShow() override {
    _view = V_SUMMARY;
    _summary_scroll = 0;
    _list_scroll    = 0;
    _cfg_dirty      = false;
    _action_menu.active = false;
    _menu_level     = ML_MAIN;
    _return_home    = false;
    _wp.reset();
  }

  // Switch straight into the Map view (used by the Home "Map" page). Layered on
  // top of onShow()'s reset by gotoMapScreen(); remembers we came from Home so
  // KEY_CANCEL returns there, not to Tools.
  void showMapView() { _view = V_MAP; _return_home = true; }

  // Forwarded to UITask's GPS duty-cycle hold — see WaypointsView::needsLiveGps().
  bool wpNeedsLiveGps() const { return _wp.needsLiveGps(); }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    // Waypoint management UI owns the screen while active.
    if (_wp.active()) return _wp.render(display);

    // Title carries the view counter so the bottom hint row can be reclaimed
    // for content.
    const char* base = (_view == V_MAP)  ? (_map_grid ? "TRAIL MAP+" : "TRAIL MAP")
                     : (_view == V_LIST) ? "TRAIL LIST"
                     :                      "TRAIL";
    char title[20];
    snprintf(title, sizeof(title), "%s %d/%d", base, (int)_view + 1, (int)V_COUNT);
    display.drawCenteredHeader(title);

    if      (_view == V_MAP)  renderMap(display);
    else if (_view == V_LIST) renderList(display);
    else                       renderSummary(display);

    if (_action_menu.active) _action_menu.render(display);
    return _store->isActive() ? 1000 : 5000;
  }

  // Forward the loop tick to the waypoint UI so GPS averaging (Mark avg) can
  // sample independently of the render cadence (slow on e-ink).
  void poll() override { _wp.poll(); }

  bool handleInput(char c) override {
    // Waypoint management UI consumes all input while active.
    if (_wp.active()) return _wp.handleInput(c);

    // Action popup overrides everything else.
    if (_action_menu.active) {
      // LEFT/RIGHT cycles the focused value — only meaningful in the Settings
      // submenu; the popup stays open so the user can keep tapping.
      if (keyIsPrev(c) || keyIsNext(c)) {
        int dir = keyIsNext(c) ? 1 : -1;
        int idx = _action_menu.selectedIndex();
        if (idx >= 0 && idx < _act_count && _menu_level == ML_SETTINGS)
          cycleSetting((ActionId)_act_map[idx], dir);
        return true;  // swallow elsewhere
      }
      auto res = _action_menu.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        // GPS-off confirmation popup: rows aren't ActionIds, route by level.
        if (_menu_level == ML_CONFIRM_GPS) {
          if (_action_menu.selectedIndex() == 0) {   // "Enable GPS & start"
            _task->toggleGPS();                       // off → on (persists, shows GPS alert)
            _store->setActive(true);
            _task->showAlert("GPS on, tracking started", 1200);
          }
          _menu_level = ML_MAIN;   // popup already closed by handleInput()
          return true;
        }
        int sel = _action_menu.selectedIndex();
        ActionId act = (sel >= 0 && sel < _act_count) ? (ActionId)_act_map[sel] : ACT_TOGGLE;
        switch (act) {
          case ACT_FILE:      buildFileMenu();     return true;   // descend into submenu
          case ACT_SETTINGS:  buildSettingsMenu(); return true;
          // Settings rows: Enter advances/toggles the value and keeps focus.
          case ACT_MIN_DIST:
          case ACT_UNITS:
          case ACT_GRID:
          case ACT_MARK_AVG:
          case ACT_AUTOSAVE:
          case ACT_AUTOPAUSE: cycleSetting(act, 1); reopenSettingsAt(sel); return true;
          case ACT_SHARE_NOW:     shareMyLocationNow(); break;
          case ACT_TOGGLE:
            // Starting a trail with GPS switched off logs nothing and just
            // sits on "Waiting for GPS fix" forever -- prompt to enable it
            // first (only on boards that actually have a toggleable GPS).
            if (!_store->isActive() && _task->hasGPS() && !_task->getGPSState()) {
              buildGpsConfirmMenu();
              return true;
            }
            handleToggle();
            break;
          case ACT_MARK:          _wp.markHere();      break;
          case ACT_WAYPOINTS:     _wp.openList();      break;
          case ACT_TRACKBACK:     _wp.startTrackBack(); break;
          case ACT_SAVE:          handleSave();        break;
          case ACT_LOAD:          handleLoad();        break;
          case ACT_RESET:         handleReset();       break;
          case ACT_EXPORT:        handleExport();      break;
          case ACT_EXPORT_SAVED:  handleExportSaved(); break;
        }
        _task->savePrefsIfDirty(_cfg_dirty);
      } else if (res == PopupMenu::CANCELLED) {
        _task->savePrefsIfDirty(_cfg_dirty);
        if (_menu_level != ML_MAIN) buildMainMenu();   // Cancel backs out of a submenu
      }
      return true;
    }

    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_cfg_dirty);
      if (_return_home) _task->gotoHomeScreen();
      else              _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_CONTEXT_MENU) { openActionMenu(); return true; }

    if (keyIsPrev(c)) { _view = (uint8_t)((_view + V_COUNT - 1) % V_COUNT); return true; }
    if (keyIsNext(c)) { _view = (uint8_t)((_view + 1) % V_COUNT);          return true; }
    if (_view == V_SUMMARY && c == KEY_UP) {
      _summary_scroll = (_summary_scroll > 0) ? _summary_scroll - 1 : _summary_max_scroll;
      return true;
    }
    if (_view == V_SUMMARY && c == KEY_DOWN) {
      _summary_scroll = (_summary_scroll < _summary_max_scroll) ? _summary_scroll + 1 : 0;
      return true;
    }
    if (_view == V_LIST && c == KEY_UP) {
      _list_scroll = (_list_scroll > 0) ? _list_scroll - 1 : _list_max_scroll;
      return true;
    }
    if (_view == V_LIST && c == KEY_DOWN) {
      _list_scroll = (_list_scroll < _list_max_scroll) ? _list_scroll + 1 : 0;
      return true;
    }
    if (c == KEY_ENTER) {
      // Short Enter intentionally does nothing destructive — both start and
      // stop go through the Hold-Enter popup so a stray tap can't change the
      // tracking state.
      _task->showAlert("Hold Enter for menu", 1000);
      return true;
    }
    return false;
  }

  // True when there's a usable GPS fix (same source as ownPos).
  bool gpsHasFix() const { int32_t lat, lon; return ownPos(lat, lon); }

private:
  void handleToggle() {
    bool was = _store->isActive();
    _store->setActive(!was);
    _task->showAlert(was ? "Tracking stopped"
                         : (gpsHasFix() ? "Tracking started" : "Waiting for GPS fix"),
                      was ? 800 : 1000);
  }
  void handleReset() {
    if (_store->isActive()) _store->setActive(false);
    _store->clear();
    _task->showAlert("Trail reset", 800);
  }
  void handleSave() {
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) { _task->showAlert("FS unavailable", 800); return; }
    File f = ds->openWrite(TRAIL_FILE);
    if (!f) { _task->showAlert("Save failed", 800); return; }
    bool ok = _store->writeTo(f);
    f.close();
    _task->showAlert(ok ? "Trail saved" : "Save failed", 800);
  }
  void handleLoad() {
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) { _task->showAlert("FS unavailable", 800); return; }
    File f = ds->openRead(TRAIL_FILE);
    if (!f) { _task->showAlert("No saved trail", 800); return; }
    bool ok = _store->readFrom(f);
    f.close();
    _task->showAlert(ok ? "Trail loaded" : "Load failed", 800);
  }
  void handleExport() {
    if (!Serial) { _task->showAlert("Connect USB first", 1200); return; }
    BoundedSerialPrint out{300};
    size_t n = _store->exportGpx(out, _task->waypoints());
    showExportAlert(n);
  }

  void handleExportSaved() {
    if (!Serial) { _task->showAlert("Connect USB first", 1200); return; }
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) { _task->showAlert("FS unavailable", 800); return; }
    File f = ds->openRead(TRAIL_FILE);
    if (!f) { _task->showAlert("No saved trail", 800); return; }
    BoundedSerialPrint out{300};
    size_t n = TrailStore::exportGpxFromFile(f, out, _task->waypoints());
    f.close();
    if (n == 0) { _task->showAlert("Bad saved file", 1000); return; }
    showExportAlert(n);
  }

  void showExportAlert(size_t n) {
    // The companion app multiplexes BLE + USB on the same frame protocol.
    // When BLE is connected, USB-receive is ignored and the dump can't
    // collide. Without a BLE link the app might be on USB itself, so warn
    // the user — they may need to reconnect their app afterwards.
    char alert[28];
    if (_task->hasConnection()) snprintf(alert, sizeof(alert), "GPX %u B (USB)",       (unsigned)n);
    else                         snprintf(alert, sizeof(alert), "GPX %u B - disc. app", (unsigned)n);
    _task->showAlert(alert, 1500);
  }

  static constexpr const char* TRAIL_FILE = "/trail";

  // Refresh the three settings/action labels in-place. Pointer identity is
  // preserved, so PopupMenu picks up the new text on the next render.
  void refreshActionLabels() {
    NodePrefs* p = _task->getNodePrefs();
    snprintf(_act_min_dist_label, sizeof(_act_min_dist_label),
              "Min dist: %s", TrailStore::minDeltaLabel(p ? p->trail_min_delta_idx : 0,
                                                        p && p->units_imperial));
    // Unit system (km/mi) is the global Settings choice; here we only toggle
    // what the Summary readout shows — speed or pace.
    snprintf(_act_units_label, sizeof(_act_units_label),
              "Readout: %s",  (p && p->trail_show_pace) ? "Pace" : "Speed");
    snprintf(_act_grid_label,   sizeof(_act_grid_label),
              "Grid: %s",      _map_grid ? "ON" : "OFF");
    snprintf(_act_autopause_label, sizeof(_act_autopause_label),
              "Auto-pause: %s", NodePrefs::trailAutoPauseLabel(p ? p->trail_autopause_idx : 0));
    snprintf(_act_mark_avg_label, sizeof(_act_mark_avg_label),
              "Mark avg: %s", NodePrefs::gpsAvgLabel(p ? p->gps_avg_idx : 0));
    snprintf(_act_autosave_label, sizeof(_act_autosave_label),
              "Auto-save: %s", (p && p->trail_autosave_lowbatt) ? "ON" : "OFF");
    snprintf(_act_toggle_label, sizeof(_act_toggle_label),
              "%s tracking",  _store->isActive() ? "Stop" : "Start");
  }

  // Append an action row, guarding both the id map and the popup capacity so
  // adding a new action can never silently overrun _act_map / PopupMenu.
  void pushAction(ActionId id, const char* label) {
    if (_act_count >= (int)sizeof(_act_map)) return;
    _act_map[_act_count++] = (uint8_t)id;
    _action_menu.addItem(label);
  }

  bool fileMenuHasItems() const { return !_store->empty() || savedTrailExists(); }

  // Hold-Enter entry point — always opens the short main menu.
  void openActionMenu() { buildMainMenu(); }

  void buildMainMenu() {
    refreshActionLabels();
    _menu_level = ML_MAIN;
    _act_count  = 0;
    _action_menu.begin("Trail", 4);
    pushAction(ACT_TOGGLE,    _act_toggle_label);
    pushAction(ACT_MARK,      "Mark here");
    // "Waypoints" opens the nav list — always available; it hosts the list,
    // backtrack (Trail-start row) and the "+ Add by coords" entry.
    pushAction(ACT_WAYPOINTS, "Waypoints...");
    // Track back retraces the recorded route to its start; needs ≥2 points.
    if (_store->count() >= 2) pushAction(ACT_TRACKBACK, "Track back");
    pushAction(ACT_SHARE_NOW,  "Share my pos");
    if (fileMenuHasItems()) pushAction(ACT_FILE, "Trail file...");
    pushAction(ACT_SETTINGS,  "Settings...");
  }

  // Confirmation shown when "Start tracking" is chosen with GPS off. Rows are
  // plain (not ActionIds); handled by _menu_level in the SELECTED branch.
  void buildGpsConfirmMenu() {
    _menu_level = ML_CONFIRM_GPS;
    _act_count  = 0;
    _action_menu.begin("GPS is off", 2);
    _action_menu.addItem("Enable GPS & start");
    _action_menu.addItem("Cancel");
  }

  // Trail-file submenu — only the operations that make sense right now.
  void buildFileMenu() {
    _menu_level = ML_FILE;
    _act_count  = 0;
    _action_menu.begin("Trail file", 4);
    bool saved = savedTrailExists();
    if (!_store->empty()) pushAction(ACT_SAVE,         "Save trail");
    if (saved)            pushAction(ACT_LOAD,         "Load trail");
    if (!_store->empty()) pushAction(ACT_EXPORT,       "Export (live)");
    if (saved)            pushAction(ACT_EXPORT_SAVED, "Export (saved)");
    if (!_store->empty()) pushAction(ACT_RESET,        "Reset trail");
  }

  // Settings submenu — values cycled with LEFT/RIGHT (or Enter). View-aware:
  // Grid only matters on the Map, Readout only on the Summary.
  void buildSettingsMenu() {
    refreshActionLabels();
    _menu_level = ML_SETTINGS;
    _act_count  = 0;
    _action_menu.begin("Settings", 4);
    pushAction(ACT_MIN_DIST,  _act_min_dist_label);
    pushAction(ACT_AUTOPAUSE, _act_autopause_label);
    pushAction(ACT_MARK_AVG,  _act_mark_avg_label);
    pushAction(ACT_AUTOSAVE,  _act_autosave_label);
    if (_view == V_SUMMARY) pushAction(ACT_UNITS, _act_units_label);
    if (_view == V_MAP)     pushAction(ACT_GRID,  _act_grid_label);
  }

  // Cycle a settings value. Returns true if `act` was a settings row.
  bool cycleSetting(ActionId act, int dir) {
    NodePrefs* p = _task->getNodePrefs();
    if (act == ACT_MIN_DIST && p) { cycleMinDelta(p, dir); _cfg_dirty = true; refreshActionLabels(); return true; }
    if (act == ACT_UNITS    && p) { cycleUnits(p, dir);    _cfg_dirty = true; refreshActionLabels(); return true; }
    if (act == ACT_GRID)          { _map_grid = !_map_grid;                    refreshActionLabels(); return true; }
    if (act == ACT_AUTOPAUSE && p){ cycleAutoPause(p, dir); _cfg_dirty = true; refreshActionLabels(); return true; }
    if (act == ACT_MARK_AVG  && p){ cycleMarkAvg(p, dir);   _cfg_dirty = true; refreshActionLabels(); return true; }
    if (act == ACT_AUTOSAVE  && p){ p->trail_autosave_lowbatt = !p->trail_autosave_lowbatt; _cfg_dirty = true; refreshActionLabels(); return true; }
    return false;
  }

  static bool savedTrailExists() {
    DataStore* ds = the_mesh.getDataStore();
    if (!ds) return false;
    File f = ds->openRead(TRAIL_FILE);
    bool ok = (bool)f;
    if (f) f.close();
    return ok;
  }

  // GPS / unit helpers shared by the trail views and the map. (Waypoint list /
  // navigation / add / mark / rename / delete / send moved to WaypointsView.)
  bool ownPos(int32_t& lat, int32_t& lon) const { return _task->currentLocation(lat, lon); }
  bool useImperial() const { return _task && _task->useImperial(); }

  // After Enter on a settings row, the popup auto-closes per PopupMenu's
  // semantics. Re-open the Settings submenu with focus restored to that row so
  // the user can continue cycling.
  void reopenSettingsAt(int sel) {
    buildSettingsMenu();
    _action_menu.setSelected(sel);
  }

  void cycleMinDelta(NodePrefs* p, int dir) {
    uint8_t idx = p->trail_min_delta_idx;
    if (idx >= TrailStore::MIN_DELTA_COUNT) idx = 0;
    idx = (dir > 0) ? (idx + 1) % TrailStore::MIN_DELTA_COUNT
                    : (idx + TrailStore::MIN_DELTA_COUNT - 1) % TrailStore::MIN_DELTA_COUNT;
    p->trail_min_delta_idx = idx;
  }
  // The trail "Readout" row toggles speed vs pace; the metric/imperial unit
  // comes from the global Settings preference, so this is a plain on/off flip.
  void cycleUnits(NodePrefs* p, int /*dir*/) { p->trail_show_pace ^= 1; }
  void cycleAutoPause(NodePrefs* p, int dir) {
    uint8_t idx = p->trail_autopause_idx;
    if (idx >= NodePrefs::TRAIL_AUTOPAUSE_COUNT) idx = 0;
    idx = (dir > 0) ? (idx + 1) % NodePrefs::TRAIL_AUTOPAUSE_COUNT
                    : (idx + NodePrefs::TRAIL_AUTOPAUSE_COUNT - 1) % NodePrefs::TRAIL_AUTOPAUSE_COUNT;
    p->trail_autopause_idx = idx;
  }
  void cycleMarkAvg(NodePrefs* p, int dir) {
    uint8_t idx = p->gps_avg_idx;
    if (idx >= NodePrefs::GPS_AVG_COUNT) idx = 0;
    idx = (dir > 0) ? (idx + 1) % NodePrefs::GPS_AVG_COUNT
                    : (idx + NodePrefs::GPS_AVG_COUNT - 1) % NodePrefs::GPS_AVG_COUNT;
    p->gps_avg_idx = idx;
  }

  // One-shot manual share: build "[LOC]lat,lon" and hand it to the Messages
  // screen, where the user picks a DM or channel recipient. (Auto live-share
  // config lives in Tools › Live Share.)
  void shareMyLocationNow() {
    int32_t lat, lon;
    if (!_task->currentLocation(lat, lon)) { _task->showAlert("No GPS fix", 1000); return; }
    char text[40];
    snprintf(text, sizeof(text), LOCATION_MSG_TAG "%.5f,%.5f", lat / 1e6, lon / 1e6);
    _task->shareToMessage(text);
  }

  // Render the average speed (or pace) in the globally-selected unit system.
  void formatAvgPaceOrSpeed(char* buf, size_t n, NodePrefs* p) const {
    bool imperial = p && p->units_imperial;
    bool pace     = p && p->trail_show_pace;
    uint16_t kmh  = _store->avgSpeedKmh();
    if (!pace) {
      if (imperial) snprintf(buf, n, "Avg: %u mph", (unsigned)((float)kmh * 0.621371f + 0.5f));
      else          snprintf(buf, n, "Avg: %u km/h", (unsigned)kmh);
      return;
    }
    uint32_t d_m  = _store->totalDistanceMeters();
    uint32_t es_s = _store->elapsedSeconds();
    const char* unit = imperial ? "/mi" : "/km";
    float dist_unit  = imperial ? (d_m / 1609.344f) : (d_m / 1000.0f);
    if (d_m == 0 || es_s == 0 || dist_unit <= 0) {
      snprintf(buf, n, "Pace: --:-- %s", unit);
      return;
    }
    float pace_min = (es_s / 60.0f) / dist_unit;
    unsigned pm = (unsigned)pace_min;
    unsigned ps = (unsigned)((pace_min - pm) * 60.0f);
    snprintf(buf, n, "Pace: %u:%02u %s", pm, ps, unit);
  }

  void summaryItem(int i, char* buf, size_t n) const {
    switch (i) {
      case 0: {
        const char* st;
        if (!_store->isActive())     st = "stopped";
        else if (_store->isPaused()) st = "paused";
        else if (_store->empty())    st = "waiting fix";
        else                          st = "tracking";
        snprintf(buf, n, "Status: %s", st);
        break;
      }
      case 1:
        snprintf(buf, n, "Points: %d / %d", _store->count(), TrailStore::CAPACITY);
        break;
      case 2: {
        uint32_t d = _store->totalDistanceMeters();
        char ds[12];
        geo::fmtDist(ds, sizeof(ds), d / 1000.0f, useImperial());
        snprintf(buf, n, "Dist: %s", ds);
        break;
      }
      case 3: {
        uint32_t es = _store->elapsedSeconds();
        // Unit-suffixed so "1h 05m" can't be misread as "1m 05s".
        if (es < 3600) snprintf(buf, n, "Time: %lum %02lus",
                                  (unsigned long)(es / 60), (unsigned long)(es % 60));
        else           snprintf(buf, n, "Time: %luh %02lum",
                                  (unsigned long)(es / 3600), (unsigned long)((es % 3600) / 60));
        break;
      }
      case 4:
        formatAvgPaceOrSpeed(buf, n, _task->getNodePrefs());
        break;
      default:
        buf[0] = '\0';
    }
  }

  void renderSummary(DisplayDriver& display) {
    const int y0     = display.listStart();
    const int step   = display.lineStep();
    const int avail  = display.height() - y0 - 2;
    int visible      = avail / step;
    if (visible < 1) visible = 1;
    if (visible > SUMMARY_ITEM_COUNT) visible = SUMMARY_ITEM_COUNT;

    int max_scroll = SUMMARY_ITEM_COUNT - visible;
    if (_summary_scroll > max_scroll) _summary_scroll = max_scroll;
    if (_summary_scroll < 0)          _summary_scroll = 0;
    _summary_max_scroll = max_scroll;

    for (int i = 0; i < visible; i++) {
      int idx = _summary_scroll + i;
      if (idx >= SUMMARY_ITEM_COUNT) break;
      char buf[28];
      summaryItem(idx, buf, sizeof(buf));
      display.setCursor(2, y0 + i * step);
      display.print(buf);
    }

    drawScrollIndicator(display, y0, visible * step, SUMMARY_ITEM_COUNT, visible, _summary_scroll);
  }

  void renderList(DisplayDriver& display) {
    const int top    = display.listStart();
    const int step   = display.lineStep();
    const int avail  = display.height() - top - 2;

    if (_store->empty()) {
      display.drawTextCentered(display.width() / 2, top + avail / 2, "No trail yet");
      _list_max_scroll = 0;
      return;
    }

    int visible = avail / step;
    if (visible < 1) visible = 1;
    const int total = _store->count();
    if (visible > total) visible = total;

    int max_scroll = total - visible;
    if (_list_scroll > max_scroll) _list_scroll = max_scroll;
    if (_list_scroll < 0)          _list_scroll = 0;
    _list_max_scroll = max_scroll;

    NodePrefs* p = _task->getNodePrefs();
    int32_t tz_off = (int32_t)(p ? p->tz_offset_hours : 0) * 3600;

    for (int i = 0; i < visible; i++) {
      int idx = total - 1 - (_list_scroll + i);
      if (idx < 0) break;
      const TrailPoint& pt = _store->at(idx);

      time_t lt = (time_t)((int32_t)pt.ts + tz_off);
      struct tm* ti = gmtime(&lt);

      char dist[12];
      if (idx == 0 || (pt.flags & TRAIL_FLAG_SEG_START)) {
        snprintf(dist, sizeof(dist), "start");
      } else {
        const TrailPoint& prev = _store->at(idx - 1);
        float d = TrailStore::haversineMeters(prev.lat_1e6, prev.lon_1e6,
                                                pt.lat_1e6,    pt.lon_1e6);
        char ds[10];
        geo::fmtDist(ds, sizeof(ds), d / 1000.0f, useImperial());
        snprintf(dist, sizeof(dist), "+%s", ds);
      }

      char row[28];
      snprintf(row, sizeof(row), "%02d:%02d  %s",
                ti->tm_hour, ti->tm_min, dist);
      display.setCursor(2, top + i * step);
      display.print(row);
    }

    drawScrollIndicator(display, top, visible * step, total, visible, _list_scroll);
  }

  // Shared map projection: geographic (1e-6 deg) → screen pixels. The scale is
  // isotropic (cos(lat) folded into lon), so a metre maps to the same pixel
  // count on both axes. Built once in renderMap and handed to renderGrid and
  // the marker drawing so the projection equation lives in exactly one place.
  struct MapProjection {
    int32_t min_lat, max_lat, min_lon;
    float   lon_scale_geo, scale;
    int     off_x, off_y, area_x, area_y, area_w, area_h;
    void project(int32_t lat, int32_t lon, int& px, int& py) const {
      px = off_x + (int)((float)(lon - min_lon) * lon_scale_geo * scale);
      py = off_y + (int)((float)(max_lat - lat) * scale);
    }
  };

  // Fold the trail / waypoints / live position into a bounding box. With a trail,
  // only the route + live position define the box (far waypoints clamp to the
  // edge when drawn); without one, the waypoints do. Returns false if empty.
  bool computeBounds(bool have_trail, bool have_gps, int32_t my_lat, int32_t my_lon,
                     int32_t& min_lat, int32_t& min_lon,
                     int32_t& max_lat, int32_t& max_lon) const {
    bool init = false;
    auto fold = [&](int32_t lat, int32_t lon) {
      if (!init) { min_lat = max_lat = lat; min_lon = max_lon = lon; init = true; }
      else {
        if (lat < min_lat) min_lat = lat;  if (lat > max_lat) max_lat = lat;
        if (lon < min_lon) min_lon = lon;  if (lon > max_lon) max_lon = lon;
      }
    };
    if (have_trail) {
      int32_t a, b, c, d; _store->boundingBox(a, b, c, d);
      fold(a, b); fold(c, d);
    } else {
      WaypointStore& wp = _task->waypoints();
      for (int i = 0; i < wp.count(); i++) fold(wp.at(i).lat_1e6, wp.at(i).lon_1e6);
    }
    if (have_gps) fold(my_lat, my_lon);
    // Fold in live-tracked contacts ([LOC] shares) so they stay in frame.
    {
      LiveTrackStore& lt = _task->liveTrack();
      uint32_t now = rtc_clock.getCurrentTime();
      for (int i = 0; i < LiveTrackStore::CAPACITY; i++)
        if (lt.isActive(i, now)) fold(lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6);
    }
    // Fold in the active target too, so a destination you set never sits off-frame.
    { int32_t tla, tlo; if (_task->activeTargetPos(tla, tlo)) fold(tla, tlo); }
    return init;
  }

  // Draw the saved waypoints (label's first two chars beside each, clamped to the
  // frame) and the current-position marker (live GPS, else last trail point).
  void drawMarkers(DisplayDriver& display, const MapProjection& proj,
                   bool have_gps, int32_t my_lat, int32_t my_lon, bool have_trail) {
    const int area_x = proj.area_x, area_y = proj.area_y;
    const int area_w = proj.area_w, area_h = proj.area_h;
    WaypointStore& wp = _task->waypoints();
    const int wp_x_max = area_x + area_w - 1, wp_y_max = area_y + area_h - 1;
    // Reserved rectangles of labels already placed this frame, so a dense
    // cluster of points doesn't smear their labels on top of one another.
    // Live-tracked contacts are labelled FIRST so a moving person's name wins a
    // slot over a nearby static waypoint (on a live map the person matters most).
    LblRect occ[40]; int nocc = 0;
    {
      LiveTrackStore& lt = _task->liveTrack();
      uint32_t now = rtc_clock.getCurrentTime();
      for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
        if (!lt.isActive(i, now)) continue;
        const LiveTrackStore::Entry& s = lt.slotAt(i);
        int cx, cy; proj.project(s.lat_1e6, s.lon_1e6, cx, cy);
        if (cx < area_x) cx = area_x;  else if (cx > wp_x_max) cx = wp_x_max;
        if (cy < area_y) cy = area_y;  else if (cy > wp_y_max) cy = wp_y_max;
        drawContactMarker(display, cx, cy);
        char s2[3] = { s.name[0], s.name[0] ? s.name[1] : (char)0, 0 };
        drawLabelAvoiding(display, s2, cx, cy, area_x, area_y, area_w, area_h, occ, nocc, 40);
      }
    }
    for (int i = 0; i < wp.count(); i++) {
      const Waypoint& w = wp.at(i);
      int wx, wy; proj.project(w.lat_1e6, w.lon_1e6, wx, wy);
      if (wx < area_x) wx = area_x;  else if (wx > wp_x_max) wx = wp_x_max;
      if (wy < area_y) wy = area_y;  else if (wy > wp_y_max) wy = wp_y_max;
      drawWaypointMarker(display, wx, wy);
      char s[3] = { w.label[0], w.label[0] ? w.label[1] : (char)0, 0 };
      drawLabelAvoiding(display, s, wx, wy, area_x, area_y, area_w, area_h, occ, nocc, 40);
    }

    if (have_gps) {
      int mx, my; proj.project(my_lat, my_lon, mx, my);
      drawCurrentMarker(display, mx, my);
    } else if (have_trail) {
      int ex, ey; proj.project(_store->last().lat_1e6, _store->last().lon_1e6, ex, ey);
      drawCurrentMarker(display, ex, ey);
    }

    // Active target flag, last so it stays legible atop any waypoint/contact it
    // coincides with. Clamped to the frame like the other off-edge markers.
    int32_t tla, tlo;
    if (_task->activeTargetPos(tla, tlo)) {
      int tx, ty; proj.project(tla, tlo, tx, ty);
      if (tx < area_x) tx = area_x;  else if (tx > wp_x_max) tx = wp_x_max;
      if (ty < area_y) ty = area_y;  else if (ty > wp_y_max) ty = wp_y_max;
      drawTargetMarker(display, tx, ty);
    }
  }

  void renderMap(DisplayDriver& display) {
    const int top    = display.listStart();
    const int bottom = display.height() - 2;

    WaypointStore& wp = _task->waypoints();
    const int nwp     = wp.count();
    const bool have_trail = !_store->empty();

    // Live GPS position — lets the map double as a "you + waypoints" view even
    // when no trail is being recorded.
    int32_t my_lat, my_lon;
    const bool have_gps = ownPos(my_lat, my_lon);
    const bool have_live = _task->liveTrack().active(rtc_clock.getCurrentTime()) > 0;
    int32_t tgt_lat, tgt_lon;
    const bool have_tgt = _task->activeTargetPos(tgt_lat, tgt_lon);

    if (!have_trail && nwp == 0 && !have_gps && !have_live && !have_tgt) {
      display.drawTextCentered(display.width() / 2, (top + bottom) / 2, "No GPS / no trail");
      return;
    }

    const int area_x = 2;
    const int area_y = top + 1;
    const int area_w = display.width() - 4;
    const int area_h = bottom - top - 2;
    if (area_w < 6 || area_h < 6) return;

    // Bounding box spans the trail, every waypoint, and the live position, so
    // everything of interest stays in frame.
    int32_t min_lat = 0, min_lon = 0, max_lat = 0, max_lon = 0;
    computeBounds(have_trail, have_gps, my_lat, my_lon, min_lat, min_lon, max_lat, max_lon);

    // Degenerate: everything at one coordinate — just centre the markers.
    if (min_lat == max_lat && min_lon == max_lon) {
      int ccx = area_x + area_w / 2, ccy = area_y + area_h / 2;
      // Only pin a waypoint to the centre in the genuine "lone waypoint, no
      // trail" case. With a trail the centre is the trail's spot, and the
      // waypoints (not folded into this box) belong elsewhere — don't draw
      // them here, or they'd appear at the trail's location.
      if (nwp > 0 && !have_trail) {
        drawWaypointMarker(display, ccx, ccy);
        const char* lbl = wp.at(0).label;        // single coincident spot — label it
        char s[3] = { lbl[0], lbl[0] ? lbl[1] : (char)0, 0 };
        drawWaypointLabel(display, s, ccx, ccy, area_x, area_y, area_w, area_h);
      }
      if (have_gps || have_trail) drawCurrentMarker(display, ccx, ccy);
      { int32_t tla, tlo; if (_task->activeTargetPos(tla, tlo)) drawTargetMarker(display, ccx, ccy); }
      return;
    }

    float avg_lat_rad = ((min_lat + max_lat) / 2.0e6f) * (float)M_PI / 180.0f;
    float lon_scale_geo = cosf(avg_lat_rad);
    if (lon_scale_geo < 0.05f) lon_scale_geo = 0.05f;

    float lat_span = (float)(max_lat - min_lat);
    float lon_span = (float)(max_lon - min_lon) * lon_scale_geo;

    float scale_lat = (float)area_h / (lat_span > 0 ? lat_span : 1.0f);
    float scale_lon = (float)area_w / (lon_span > 0 ? lon_span : 1.0f);
    float scale     = (scale_lat < scale_lon) ? scale_lat : scale_lon;

    int used_w = (int)(lon_span * scale);
    int used_h = (int)(lat_span * scale);

    MapProjection proj;
    proj.min_lat = min_lat;  proj.max_lat = max_lat;  proj.min_lon = min_lon;
    proj.lon_scale_geo = lon_scale_geo;  proj.scale = scale;
    proj.off_x  = area_x + (area_w - used_w) / 2;
    proj.off_y  = area_y + (area_h - used_h) / 2;
    proj.area_x = area_x;  proj.area_y = area_y;
    proj.area_w = area_w;  proj.area_h = area_h;

    if (_map_grid) renderGrid(display, proj);

    if (have_trail) {
      int x0, y0;
      proj.project(_store->at(0).lat_1e6, _store->at(0).lon_1e6, x0, y0);
      miniIconDrawCentered(display, x0, y0, ICON_MAP_DOT);
      gfx::drawTrail(display, *_store,
        [&](int32_t la, int32_t lo, int& x, int& y) { proj.project(la, lo, x, y); },
        [&](int xa, int ya, int xb, int yb) { drawFilledDot(display, xa, ya); drawOpenDot(display, xb, yb); });
      int sx, sy;
      proj.project(_store->first().lat_1e6, _store->first().lon_1e6, sx, sy);
      drawStartMarker(display, sx, sy);
    }

    drawMarkers(display, proj, have_gps, my_lat, my_lon, have_trail);

    int nx, ny;
    northIconPos(display, area_x, area_w, area_y, nx, ny);
    miniIconDrawTop(display, nx, ny, ICON_MAP_NORTH);
  }

  // Top-left placement of the north marker — flush to the top-right corner of
  // the map area with a small buffer. Shared with renderGrid so its exclusion
  // box always matches exactly where the icon is actually drawn.
  static void northIconPos(DisplayDriver& d, int area_x, int area_w, int area_y,
                           int& nx, int& ny) {
    const int s = miniIconScale(d);
    nx = area_x + area_w - ICON_MAP_NORTH.w * s - 1;
    ny = area_y + 1;
  }

  void renderGrid(DisplayDriver& display, const MapProjection& proj) {
    const int area_x = proj.area_x, area_y = proj.area_y;
    const int area_w = proj.area_w, area_h = proj.area_h;
    static constexpr float M_PER_1E6 = 0.11132f;  // metres per 1e-6 deg lat
    float ppm = proj.scale / M_PER_1E6;            // pixels per metre (isotropic)
    if (ppm <= 0.0f) return;

    // Round scale steps with matching labels. Imperial steps are stored as their
    // metre equivalents but labelled with their exact round ft/mi value. Both
    // tables are parallel (step ↔ label).
    static const float METRIC_STEPS[] = {
      1, 2, 5, 10, 20, 50, 100, 200, 500,
      1000, 2000, 5000, 10000, 20000, 50000, 100000
    };
    static const char* METRIC_LABELS[] = {
      "1m", "2m", "5m", "10m", "20m", "50m", "100m", "200m", "500m",
      "1km", "2km", "5km", "10km", "20km", "50km", "100km"
    };
    static const float IMPERIAL_STEPS[] = {  // 10..2000 ft, then 1..100 mi (metres)
      3.048f, 7.62f, 15.24f, 30.48f, 76.2f, 152.4f, 304.8f, 609.6f,
      1609.344f, 3218.69f, 8046.72f, 16093.44f, 32186.88f, 80467.2f, 160934.4f
    };
    static const char* IMPERIAL_LABELS[] = {
      "10ft", "25ft", "50ft", "100ft", "250ft", "500ft", "1000ft", "2000ft",
      "1mi", "2mi", "5mi", "10mi", "20mi", "50mi", "100mi"
    };
    const bool imp = useImperial();
    const float* STEPS        = imp ? IMPERIAL_STEPS  : METRIC_STEPS;
    const char* const* LABELS = imp ? IMPERIAL_LABELS : METRIC_LABELS;
    const int N_STEPS = imp ? (int)(sizeof(IMPERIAL_STEPS)/sizeof(IMPERIAL_STEPS[0]))
                            : (int)(sizeof(METRIC_STEPS)/sizeof(METRIC_STEPS[0]));

    // Pick the round step nearest ~1/3 of the shorter side, then keep it sparse
    // enough to read on an OLED (≥ MIN_GRID_PX between intersections).
    static const float MIN_GRID_PX = 22.0f;
    int shorter_px = area_w < area_h ? area_w : area_h;
    float target_m = (shorter_px / ppm) / 3.0f;
    int sel = 0;
    for (int si = N_STEPS - 1; si >= 0; si--) { if (STEPS[si] <= target_m) { sel = si; break; } }
    while (sel < N_STEPS - 1 && STEPS[sel] * ppm < MIN_GRID_PX) sel++;

    // Square cells, snapped to the shorter side: divide the shorter dimension
    // into a whole number of equal cells (so the grid touches that pair of
    // borders), use that exact size as the square cell, and centre the whole
    // number of cells that fit along the longer dimension. Result: square grid,
    // reaches the frame top↔bottom (landscape), symmetric left↔right — no
    // one-sided drift, no rectangular cells. Cap so spacing stays ≥ MIN_GRID_PX.
    float step_px   = STEPS[sel] * ppm;
    bool  h_shorter = (area_h <= area_w);
    int   short_px  = h_shorter ? area_h : area_w;
    int   long_px   = h_shorter ? area_w : area_h;
    int   max_short = (int)(short_px / MIN_GRID_PX); if (max_short < 1) max_short = 1;
    int   n_short   = (int)((float)short_px / step_px + 0.5f);
    if (n_short < 1) n_short = 1;  if (n_short > max_short) n_short = max_short;
    float cell      = (float)short_px / (float)n_short;          // exact square size
    int   n_long    = (int)((float)long_px / cell);              // whole cells that fit
    if (n_long < 1) n_long = 1;
    int   off_long  = (int)(((float)long_px - (float)n_long * cell) / 2.0f + 0.5f);

    // Map the short/long axes back to screen x/y.
    int x0 = h_shorter ? area_x + off_long : area_x;
    int y0 = h_shorter ? area_y            : area_y + off_long;
    int cx = h_shorter ? n_long : n_short;   // cell count along x
    int cy = h_shorter ? n_short : n_long;   // cell count along y

    const char* lbl = LABELS[sel];
    int lh = display.getLineHeight();
    int cw = display.getCharWidth();
    int x_max = area_x + area_w - 1, y_max = area_y + area_h - 1;

    // Exclusion boxes so grid dots don't smear the scale label (bottom-left) or
    // the north marker (top-right) — northIconPos() is the single source of
    // truth for where that icon actually lands, so this box can't drift out of
    // sync with it as the mini-icon scale changes.
    const int s = miniIconScale(display);
    int lbl_x2 = area_x + (int)strlen(lbl) * cw + 1;
    int lbl_y1 = area_y + area_h - lh - 1;
    int nx, ny;
    northIconPos(display, area_x, area_w, area_y, nx, ny);
    int arr_x1 = nx - 1;
    int arr_y2 = ny + ICON_MAP_NORTH.h * s + 1;

    // Grid dots are drawn at mini-icon scale too, so they stay visible instead
    // of shrinking to a single pixel on large-font (e-ink) layouts.
    auto fillSafe = [&](int x, int y) {
      if (x < area_x || x > x_max || y < area_y || y > y_max) return;
      if (x <= lbl_x2 && y >= lbl_y1) return;   // under the scale label
      if (x >= arr_x1 && y <= arr_y2) return;   // under the north arrow
      display.fillRect(x - s / 2, y - s / 2, s, s);
    };

    // A single dot per intersection — cleaner and more legible on an OLED than
    // a 5-pixel cross (whose arms also clipped at the frame edges).
    for (int i = 0; i <= cx; i++) {
      int px = x0 + (int)((float)i * cell + 0.5f);
      if (px > x_max) px = x_max;
      for (int j = 0; j <= cy; j++) {
        int py = y0 + (int)((float)j * cell + 0.5f);
        if (py > y_max) py = y_max;
        fillSafe(px, py);
      }
    }

    display.setCursor(area_x, area_y + area_h - lh);
    display.print(lbl);
  }

  // Place a 1–2 char waypoint label beside its marker, kept inside the map
  // rect: prefer upper-right, flip to the left if it would clip the right
  // edge, and clamp vertically so edge/corner waypoints stay on the map.
  static void drawWaypointLabel(DisplayDriver& d, const char* s, int wx, int wy,
                                int ax, int ay, int aw, int ah) {
    if (!s[0]) return;
    int tw = (int)d.getTextWidth(s);
    int ch = d.getLineHeight();
    int lx = wx + 4;
    if (lx + tw > ax + aw) lx = wx - 4 - tw;     // would clip right → flip left
    if (lx < ax)           lx = ax;               // clamp to left edge
    if (lx + tw > ax + aw) lx = ax + aw - tw;     // clamp to right edge
    int ly = wy - 3;
    if (ly < ay)           ly = ay;               // clamp to top edge
    if (ly + ch > ay + ah) ly = ay + ah - ch;     // clamp to bottom edge
    d.setCursor(lx, ly);
    d.print(s);
  }

  // Map label collision avoidance. Each placed label reserves a rectangle; a new
  // label tries up to four offsets around its marker (upper-right, upper-left,
  // lower-right, lower-left) and is skipped — the marker still shows — if none
  // fit on-frame without overlapping an already-placed one. On a dense map this
  // drops a few labels instead of smearing them into an unreadable blob.
  struct LblRect { int x, y, w, h; };
  static bool rectsOverlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
  }
  static void drawLabelAvoiding(DisplayDriver& d, const char* s, int wx, int wy,
                                int ax, int ay, int aw, int ah,
                                LblRect* occ, int& nocc, int occ_max) {
    if (!s[0]) return;
    int tw = (int)d.getTextWidth(s);
    int ch = d.getLineHeight();
    const int cand[4][2] = { { wx + 4, wy - 3 }, { wx - 4 - tw, wy - 3 },
                             { wx + 4, wy + 3 }, { wx - 4 - tw, wy + 3 } };
    for (int c = 0; c < 4; c++) {
      int lx = cand[c][0], ly = cand[c][1];
      if (lx < ax || lx + tw > ax + aw || ly < ay || ly + ch > ay + ah) continue;
      bool clash = false;
      for (int k = 0; k < nocc; k++)
        if (rectsOverlap(lx, ly, tw, ch, occ[k].x, occ[k].y, occ[k].w, occ[k].h)) { clash = true; break; }
      if (clash) continue;
      d.setCursor(lx, ly);
      d.print(s);
      if (nocc < occ_max) occ[nocc++] = { lx, ly, tw, ch };
      return;
    }
    // No free slot — skip the label; the marker alone still conveys the point.
  }

  static void drawFilledDot(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_DOT);
  }
  // Hollow diamond — distinct from the trail's dots (○●), start (+) and
  // current (✕) markers.
  static void drawWaypointMarker(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_WAYPOINT);
  }
  static void drawOpenDot(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_RING);
  }
  static void drawStartMarker(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_START);
  }
  static void drawCurrentMarker(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_CURRENT);
  }
  // Filled diamond — a contact whose position arrived via a [LOC] share.
  static void drawContactMarker(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_CONTACT);
  }
  // Flag — the active Locator/Nav target. Drawn last (on top) so it stays
  // legible even when it lands on the same spot as a waypoint or contact.
  static void drawTargetMarker(DisplayDriver& d, int cx, int cy) {
    miniIconDrawCentered(d, cx, cy, ICON_MAP_TARGET);
  }
};
