#pragma once
// Locator config tool. Tools › Locator.
// A single geofence whose target is either a saved waypoint (a place) or a
// person — a contact or a live [LOC] sender, keyed by pubkey prefix.
// When armed the device beeps / alerts as it crosses into (arrive/near) or out
// of (leave/away) the radius. A waypoint target is snapshotted (coord + label);
// a person target follows their latest shared position. The crossing engine
// lives in the UI Core (ui-core/LocatorEngine.h). The Target row's Enter opens a picker
// ("None" first — the only way to unset a target once chosen — then your
// favourites, offered even with no known position yet so you can arm ahead
// of time, then any other contact with a currently-known position:
// live-sharing or just last-advertised, e.g. a repeater; then waypoints).
// LEFT/RIGHT quick-cycles the same set. The same active target can also be set
// directly from Nearby Nodes' or Waypoints' own "Set as target" menu item
// (UITask::setTargetNow()), bypassing this picker entirely.
// Included by UITask.cpp after LiveShareScreen.h.

#include "InfoKit.h"
#include "../NodePrefs.h"
#include "../Waypoint.h"
#include "../LiveTrack.h"
#include "../GeoUtils.h"
#include "icons.h"   // drawList (shared scrolling-list helper)

class LocatorScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  bool       _dirty  = false;
  int        _sel    = 0;
  int        _scroll = 0;

  // Target picker (Enter on the Target row): a flat selectable list of people
  // and places, rebuilt from favourites / known-position contacts / waypoints
  // on open. `live` + `ts` are picker-display-only (freshness), never written
  // to prefs — a person target is re-resolved by key at evaluation time.
  bool       _picking = false;
  int        _pick_sel = 0, _pick_scroll = 0;
  struct Target {
    uint8_t  kind;            // 0=waypoint 1=person 2=none (clear target)
    int32_t  lat, lon;
    uint8_t  key[6];
    char     name[20];
    uint32_t ts;              // last position update (0 = unknown), for the age tag
    bool     live;            // true = an active [LOC] share right now
    bool     fav;             // person is a favourite — ★ on the row, as in every other list
  };
  static const int TARGET_MAX = 40;
  Target _targets[TARGET_MAX];
  int    _target_n = 0;

  enum Kind : uint8_t { K_ENABLE, K_TARGET, K_RADIUS, K_MODE, K_BEEPER };
  struct Row { Kind kind; const char* label; };
  static const int ROW_COUNT = 5;
  static Row rows(int i) {
    static const Row R[ROW_COUNT] = {
      { K_ENABLE, "Alert" },
      { K_TARGET, "Target" },
      { K_RADIUS, "Radius" },
      { K_MODE,   "Mode" },
      { K_BEEPER, "Beeper" },
    };
    return R[i];
  }

  // The homing Beeper only makes sense when "arrive" is part of the alert mode —
  // it guides you toward the target while inside the radius. In pure "leave"
  // mode (1) it's contradictory (silent outside, slows as you near the exit), so
  // hide its row. Beeper is the last row, so this is just a shorter visible count;
  // the beeper engine (UITask::locatorProximityBeeper) is gated on the mode too.
  int visibleRows() const {
    return (_prefs && _prefs->locator_mode == 1) ? ROW_COUNT - 1 : ROW_COUNT;
  }

public:
  LocatorScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _dirty = false; _sel = 0; _scroll = 0; _picking = false; }

  void valueLabel(Kind k, char* buf, int n) {
    switch (k) {
      case K_ENABLE:
        snprintf(buf, n, "%s", (_prefs && _prefs->locator_enabled) ? "ON" : "OFF");
        break;
      case K_TARGET:
        if (_prefs && _prefs->locator_has_target) {
          const char* nm = _prefs->locator_label[0] ? _prefs->locator_label : "(unnamed)";
          // '@' prefix marks a live contact target (a moving person) vs a waypoint.
          if (_prefs->locator_target_kind != 0) snprintf(buf, n, "@%s", nm);
          else                                    snprintf(buf, n, "%s", nm);
        } else {
          snprintf(buf, n, "none");
        }
        break;
      case K_RADIUS: {
        uint16_t r = NodePrefs::locatorRadiusMeters(_prefs ? _prefs->locator_radius_idx : 1);
        geo::fmtDist(buf, n, r / 1000.0f, _task && _task->useImperial(), true);
        break;
      }
      case K_MODE:
        snprintf(buf, n, "%s", NodePrefs::locatorModeLabel(_prefs ? _prefs->locator_mode : 0));
        break;
      case K_BEEPER:
        snprintf(buf, n, "%s", (_prefs && _prefs->locator_beeper) ? "ON" : "OFF");
        break;
      default: buf[0] = '\0';
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    if (_picking) { int mq = renderPicker(display); return (mq > 0 && mq < 400) ? mq : 400; }
    display.drawCenteredHeader("LOCATOR");

    const int rc = visibleRows();
    if (_sel >= rc) _sel = rc - 1;   // beeper row may have just been hidden
    int mq_delay = 0;
    drawList(display, rc, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      Row r = rows(i);
      drawRowSelection(display, y, sel, reserve);
      char val[24];
      valueLabel(r.kind, val, sizeof(val));
      int mqr = info::listRow(display, y, r.label, val, sel, reserve);
      if (mqr > 0) mq_delay = mqr;
    });
    return (mq_delay > 0 && mq_delay < 500) ? mq_delay : 500;
  }

  void moveSel(int dir) { int rc = visibleRows(); _sel = (_sel + dir + rc) % rc; }

  void activate(int dir) {
    if (!_prefs) return;
    switch (rows(_sel).kind) {
      case K_ENABLE:
        _prefs->locator_enabled ^= 1;
        if (_prefs->locator_enabled && !_prefs->locator_has_target)
          _task->showAlert("Pick a target", 1200);
        _dirty = true;
        break;
      case K_TARGET:
        cycleTarget(dir);
        break;
      case K_RADIUS:
        _prefs->locator_radius_idx = (uint8_t)((_prefs->locator_radius_idx
            + (dir >= 0 ? 1 : NodePrefs::LOCATOR_RADIUS_COUNT - 1)) % NodePrefs::LOCATOR_RADIUS_COUNT);
        _dirty = true;
        break;
      case K_MODE:
        _prefs->locator_mode = (uint8_t)((_prefs->locator_mode
            + (dir >= 0 ? 1 : NodePrefs::LOCATOR_MODE_COUNT - 1)) % NodePrefs::LOCATOR_MODE_COUNT);
        _dirty = true;
        break;
      case K_BEEPER:
        _prefs->locator_beeper ^= 1;
        _dirty = true;
        break;
    }
    // Re-seed the crossing engine after any change so editing the target/radius
    // while armed can't fire a stale arrive/leave before the screen is closed.
    _task->resetLocator();
  }

  // Add a person candidate to _targets, deduped by pubkey prefix. Freshness is
  // resolved here for display only: an active [LOC] share wins (live=true),
  // else the contact's last-advertised position if it has one — the same
  // precedence LocatorEngine uses at evaluation time. When
  // `require_position` is false (favourites), a contact with neither is still
  // added with no position — "arm ahead of time", per the existing feature.
  bool addPersonTarget(const uint8_t* key, const char* name, bool require_position, bool fav) {
    if (_target_n >= TARGET_MAX) return false;
    for (int j = 0; j < _target_n; j++)
      if (_targets[j].kind == 1 && memcmp(_targets[j].key, key, 6) == 0) return false;  // already added

    int32_t lat = 0, lon = 0; uint32_t ts = 0; bool live = false;
    bool has_pos = _task->resolvePersonPos(key, lat, lon, &live, &ts);  // same precedence as the engine
    if (require_position && !has_pos) return false;   // nothing to navigate to yet

    Target& t = _targets[_target_n++];
    t.kind = 1; t.lat = lat; t.lon = lon; t.ts = ts; t.live = live; t.fav = fav;
    memcpy(t.key, key, 6);
    snprintf(t.name, sizeof(t.name), "%s", name);
    return true;
  }

  // Build the selectable target set into _targets: "None" first (clears the
  // target — the only way to unset it once chosen), then your favourites (the
  // quick path, offered even with no known position yet), then
  // any other contact with a currently-known position — live-sharing or just
  // last-advertised (a repeater, a room, or someone who shared a fix once) —
  // then saved waypoints. A person is keyed by pubkey prefix so the engine
  // re-resolves their position each evaluation rather than trusting a snapshot.
  void buildTargets() {
    _target_n = 0;
    Target& none = _targets[_target_n++];
    none.kind = 2; none.lat = 0; none.lon = 0; none.ts = 0; none.live = false; none.fav = false;
    memset(none.key, 0, 6);
    snprintf(none.name, sizeof(none.name), "(none)");
    for (int idx = 0; _target_n < TARGET_MAX; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      if (!contactctl::favourite(c)) continue;
      addPersonTarget(c.id.pub_key, c.name, /*require_position=*/false, true);
    }
    for (int idx = 0; _target_n < TARGET_MAX; idx++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(idx, c)) break;
      addPersonTarget(c.id.pub_key, c.name, /*require_position=*/true, contactctl::favourite(c));
    }
    WaypointStore& wp = _task->waypoints();
    for (int i = 0; i < wp.count() && _target_n < TARGET_MAX; i++) {
      const Waypoint& w = wp.at(i);
      Target& t = _targets[_target_n++];
      t.kind = 0; t.lat = w.lat_1e6; t.lon = w.lon_1e6; t.fav = false;
      memset(t.key, 0, 6);
      snprintf(t.name, sizeof(t.name), "%s", w.label);
    }
  }

  // Index of the currently-configured target within _targets, or -1.
  // No target set at all maps to the synthetic "None" entry (always index 0).
  int currentTargetIndex() const {
    if (!_prefs->locator_has_target) return 0;
    for (int i = 0; i < _target_n; i++) {
      if (_targets[i].kind == 1 && _prefs->locator_target_kind == 1) {
        if (memcmp(_targets[i].key, _prefs->locator_key, 6) == 0) return i;
      } else if (_targets[i].kind == 0 && _prefs->locator_target_kind == 0) {
        if (_targets[i].lat == _prefs->locator_lat_1e6 &&
            _targets[i].lon == _prefs->locator_lon_1e6) return i;
      }
    }
    return -1;
  }

  void applyTarget(const Target& t) {
    if (t.kind == 2) { _task->clearTarget(); _dirty = true; return; }
    // Same target definition as every other entry point (UITask::setTarget),
    // but the save is deferred to screen exit (_dirty) so LEFT/RIGHT cycling
    // through candidates doesn't write flash on each step.
    _task->setTarget(t.kind, t.kind == 1 ? t.key : nullptr, t.lat, t.lon, t.name);
    _dirty = true;
  }

  // LEFT/RIGHT quick-cycle over the same set the picker shows (includes "None").
  void cycleTarget(int dir) {
    buildTargets();
    int cur = currentTargetIndex();
    int nx = (cur < 0) ? (dir >= 0 ? 0 : _target_n - 1)
                       : ((cur + (dir >= 0 ? 1 : _target_n - 1)) % _target_n);
    applyTarget(_targets[nx]);
  }

  void openPicker() {
    if (!_prefs) return;
    buildTargets();
    int cur = currentTargetIndex();
    _pick_sel = (cur >= 0) ? cur : 0;
    _pick_scroll = 0;
    _picking = true;
  }

  // Returns 0, or the ms until the selected row should next redraw to keep a
  // marquee animation going (see DisplayDriver::drawTextEllipsized).
  int renderPicker(DisplayDriver& display) {
    display.drawCenteredHeader("PICK TARGET");
    uint32_t now = rtc_clock.getCurrentTime();
    int mq_delay = 0;
    drawList(display, _target_n, _pick_sel, _pick_scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      const Target& t = _targets[i];
      char row[36];
      if (t.kind != 1) {
        snprintf(row, sizeof(row), "%s", t.name);          // a plain name is a waypoint
      } else if (t.live) {
        snprintf(row, sizeof(row), "@%s", t.name);          // '@' marks a person; live needs no age tag
      } else if (t.ts) {
        char age[8];
        geo::fmtAgeShort(age, sizeof(age), now, t.ts);
        snprintf(row, sizeof(row), "@%s (%s)", t.name, age);   // last-advertised, not actively sharing
      } else {
        snprintf(row, sizeof(row), "@%s", t.name);          // favourite, no position known yet
      }
      int sw = t.fav ? favStarWidth(display) : 0;
      int mqr = display.drawTextEllipsized(2, y, display.width() - 2 - sw - reserve, row, sel);
      if (sel && mqr > 0) mq_delay = mqr;
      if (sw) drawFavStar(display, display.width() - reserve - sw + 1, y);
    });
    return mq_delay;
  }

  bool handleInput(char c) override {
    // Target picker takes all input while open.
    if (_picking) {
      if (c == KEY_UP)   { _pick_sel = (_pick_sel > 0) ? _pick_sel - 1 : _target_n - 1; return true; }
      if (c == KEY_DOWN) { _pick_sel = (_pick_sel < _target_n - 1) ? _pick_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) { applyTarget(_targets[_pick_sel]); _picking = false; return true; }
      if (c == KEY_CANCEL) { _picking = false; return true; }
      return true;
    }
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);   // engine re-seeded per edit
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { moveSel(-1); return true; }
    if (c == KEY_DOWN) { moveSel(+1); return true; }
    if (keyIsPrev(c))  { activate(-1); return true; }
    if (keyIsNext(c))  { activate(+1); return true; }
    if (c == KEY_ENTER) {
      if (rows(_sel).kind == K_TARGET) { openPicker(); return true; }  // full list picker
      activate(+1); return true;
    }
    return false;
  }
};
