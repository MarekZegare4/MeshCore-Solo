#pragma once
// Waypoint management UI for Tools › Trail: list / navigate / add-by-coords,
// plus mark-here, rename, delete and send. Extracted from TrailScreen as a
// self-contained component that TrailScreen owns and delegates to while active.
// It needs the TrailStore only for the synthetic "Trail start" backtrack row.
//
// Included by TrailScreen.h (which UITask.cpp pulls in after UITask is fully
// defined, so the inline _task->… calls below see the complete type).

#include "../Waypoint.h"
#include "../GeoUtils.h"
#include "../Trail.h"
#include "KeyboardWidget.h"
#include "PopupMenu.h"
#include "NavView.h"
#include "DigitEditor.h"
#include "InfoKit.h"
#include "../ui-core/GpsAverager.h"
#include "../ui-core/TrackBack.h"

class WaypointsView {
  UITask*     _task;
  TrailStore* _store;

  // Sub-modes layered over the trail views. OFF = the component is dormant.
  enum Mode { OFF, LIST, NAV, ADD, AVG, TRACKBACK };
  uint8_t _mode   = OFF;
  int     _sel    = 0;
  int     _scroll = 0;

  // Track-back (Tools › Trail › Track back): retrace the recorded trail in
  // reverse using NavView (ui-core/TrackBack.h picks the breadcrumb).
  TrackBack           _tb;
  navview::EtaTracker _tb_eta;
  navview::EtaTracker _wp_eta;   // same readout for plain waypoint navigation

  // GPS averaging (Tools › Trail › Settings › Mark avg): markHere() averages
  // fixes for gps_avg_idx seconds (ui-core/GpsAverager.h), sampled from poll().
  GpsAverager _avg;

  PopupMenu      _ctx;               // Rename / Delete / Send on a selected waypoint
  bool      _kb_active = false;
  int       _kb_rename_idx = -1;     // -1 = marking new, ≥0 = renaming that index
  int32_t   _mark_lat = 0, _mark_lon = 0;   // pending new-mark position
  uint32_t  _mark_ts  = 0;

  // ADD form — enter a waypoint by coordinates. Lat/Lon are numeric, so they use
  // the digit-by-digit scroll editor (same widget as the radio frequency field)
  // rather than the text keyboard: Enter opens the editor on the magnitude,
  // LEFT/RIGHT on the row toggles the hemisphere (N/S, E/W). Only the Label field
  // uses the keyboard.
  float     _add_lat_mag = 0.0f, _add_lon_mag = 0.0f;   // magnitude (degrees)
  char      _add_label[WAYPOINT_LABEL_LEN] = "";
  bool      _add_lat_neg = false;    // true = S
  bool      _add_lon_neg = false;    // true = W
  int       _add_sel  = 0;           // 0=Lat 1=Lon 2=Label 3=Save
  int       _kb_field = -1;          // ADD label edit (>=0) vs mark/rename (-1)
  DigitEditor _add_editor;           // active while editing a Lat/Lon magnitude

  bool useImperial() const { return _task && _task->useImperial(); }
  bool ownPos(int32_t& lat, int32_t& lon) const { return _task->currentLocation(lat, lon); }

  // The nav list shows a synthetic "Trail start" row (index 0) whenever a trail
  // exists, followed by the saved waypoints. These map the combined row index
  // (_sel) onto that layout.
  bool hasStart() const { return !_store->empty(); }
  int  wpListCount() const { return (hasStart() ? 1 : 0) + _task->waypoints().count(); }
  bool selIsStart() const { return hasStart() && _sel == 0; }
  int  wpIndex() const { return _sel - (hasStart() ? 1 : 0); }  // index into WaypointStore

  // Add a waypoint by coordinates (no GPS fix needed). Opens the ADD form.
  void openAddForm() {
    if (_task->waypoints().full()) { _task->showAlert("Waypoints full", 1000); return; }
    _add_lat_mag = _add_lon_mag = 0.0f;
    _add_label[0] = '\0';
    _add_lat_neg = _add_lon_neg = false;
    _add_sel = 0;
    _add_editor.active = false;
    _mode = ADD;
  }

  // Store the just-typed label (the only ADD field that still uses the keyboard).
  void applyAddField(const char* buf) {
    strncpy(_add_label, buf, sizeof(_add_label) - 1);
    _add_label[sizeof(_add_label) - 1] = '\0';
  }

  // Validate the ADD form and save the waypoint.
  void commitAddForm() {
    double la = _add_lat_mag, lo = _add_lon_mag;
    if (_add_lat_neg) la = -la;
    if (_add_lon_neg) lo = -lo;
    if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0) {
      _task->showAlert("Out of range", 1200); return;
    }
    if (_task->waypoints().full()) { _task->showAlert("Waypoints full", 1000); return; }
    int32_t lat = (int32_t)(la * 1e6 + (la < 0 ? -0.5 : 0.5));
    int32_t lon = (int32_t)(lo * 1e6 + (lo < 0 ? -0.5 : 0.5));
    char label[WAYPOINT_LABEL_LEN];
    if (_add_label[0]) { strncpy(label, _add_label, sizeof(label) - 1); label[sizeof(label) - 1] = '\0'; }
    else               snprintf(label, sizeof(label), "WP%d", _task->waypoints().count() + 1);
    if (_task->addWaypoint(lat, lon, label)) { _mode = OFF; }
  }

  // Open the shared keyboard for a literal field. Waypoint labels and the
  // lat/lon magnitudes are never placeholder-expanded, so {loc}/{time} are
  // cleared (same intent as the bot's literal trigger field).
  void openKb(const char* initial, int max) {
    KeyboardWidget& kb = _task->keyboard();
    kb.begin(initial, max);
    kb.clearPlaceholders();
    _kb_active = true;
  }

  // Capture a mark position and open the keyboard for its label. Shared by the
  // instant "Mark here" and the end of GPS averaging. _mode goes OFF: the
  // keyboard takes over the screen, and there's no sub-view to return to once
  // the label is committed (active() then tracks _kb_active alone).
  void beginLabel(int32_t lat, int32_t lon) {
    _mode = OFF;
    _mark_lat = lat; _mark_lon = lon; _mark_ts = (uint32_t)rtc_clock.getCurrentTime();
    _kb_rename_idx = -1;
    openKb("", WAYPOINT_LABEL_LEN - 1);
  }

  // Commit a mark-here / rename label from the keyboard.
  void commitLabel(const char* buf) {
    if (_kb_rename_idx >= 0) {
      _task->waypoints().rename(_kb_rename_idx, buf);
      _task->saveWaypoints();
      _kb_rename_idx = -1;
      return;
    }
    char auto_lbl[WAYPOINT_LABEL_LEN];
    if (!buf || !buf[0]) {
      snprintf(auto_lbl, sizeof(auto_lbl), "WP%d", _task->waypoints().count() + 1);
      buf = auto_lbl;
    }
    _task->addWaypoint(_mark_lat, _mark_lon, _mark_ts, buf);
  }

  void renderAddForm(DisplayDriver& display) {
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Add waypoint");
    const int top  = display.listStart();
    const int step = display.lineStep();
    for (int i = 0; i < 4; i++) {
      int y = top + i * step;
      bool sel = (i == _add_sel);
      if (i == 3) {   // the form's button
        drawButton(display, (display.width() - buttonWidth(display, "Save")) / 2, y, "Save", sel);
        continue;
      }
      display.drawSelectionRow(0, y - 1, display.width(), step - 1, sel);
      const char* label = i == 0 ? "Lat" : i == 1 ? "Lon" : "Label";
      const char hemi = i == 0 ? (_add_lat_neg ? 'S' : 'N') : (_add_lon_neg ? 'W' : 'E');
      // While editing a magnitude, draw the hemisphere + the digit editor so
      // the place under the cursor is highlighted; otherwise the plain value.
      if ((i == 0 || i == 1) && sel && _add_editor.active) {
        const int right = display.width() - 2;
        const int ex = right - display.getTextWidth(i == 0 ? "00.00000" : "000.00000");
        display.setCursor(2, y); display.print(label);
        char h[3] = { hemi, ' ', 0 };
        display.setCursor(ex - display.getTextWidth(h), y); display.print(h);
        _add_editor.render(display, ex, y);
      } else {
        char val[24];
        if (i < 2) snprintf(val, sizeof(val), "%c %.5f", hemi, i == 0 ? _add_lat_mag : _add_lon_mag);
        else       snprintf(val, sizeof(val), "%s", _add_label[0] ? _add_label : "(auto)");
        info::valueRow(display, y, label, val, sel, 0);
      }
      display.setColor(DisplayDriver::LIGHT);
    }
  }

  // GPS-averaging progress screen. Sampling itself happens in poll(); this only
  // reports remaining time and the running sample count.
  void renderAvg(DisplayDriver& display) {
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Averaging GPS");
    const int top  = display.listStart();
    const int step = display.lineStep();
    char line[28];
    snprintf(line, sizeof(line), "%ds left (of %us)", _avg.remainingSecs(), (unsigned)_avg.totalSecs());
    display.setCursor(2, top);            display.print(line);
    snprintf(line, sizeof(line), "Samples: %u", (unsigned)_avg.samples());
    display.setCursor(2, top + step);     display.print(line);
    display.setCursor(2, top + 2 * step); display.print("Cancel to abort");
  }

  // Returns 0, or the ms until the selected row's name should next redraw to
  // keep a marquee animation going (see DisplayDriver::drawTextEllipsized).
  int renderWpList(DisplayDriver& display) {
    int mq_delay = 0;
    display.setColor(DisplayDriver::LIGHT);
    char title[24];
    snprintf(title, sizeof(title), "Waypoints %d/%d",
             _task->waypoints().count(), WaypointStore::CAPACITY);
    drawScreenHeader(display, title);

    int n     = wpListCount();
    int total = n + 1;                    // final row = "+ Add by coords"
    int32_t mylat, mylon; bool have = ownPos(mylat, mylon);

    drawList(display, total, _sel, _scroll, [&](int row, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);

      if (row == n) {                     // the synthetic "Add" row
        display.setCursor(2, y); display.print("+ Add by coords");
        display.setColor(DisplayDriver::LIGHT);
        return;
      }

      int32_t tlat, tlon; const char* label;
      if (!rowTarget(row, tlat, tlon, label)) return;
      char dist[12] = ""; int bw = 0;
      if (have) {
        geo::fmtDist(dist, sizeof(dist), geo::haversineKm(mylat, mylon, tlat, tlon), useImperial());
        bw = display.getTextWidth(dist) + 2;
      }
      char nm[24];
      display.translateUTF8ToBlocks(nm, label, sizeof(nm));
      int mqr = display.drawTextEllipsized(2, y, display.width() - 2 - bw - reserve, nm, sel);
      if (sel && mqr > 0) mq_delay = mqr;
      if (dist[0]) { display.setCursor(display.width() - bw + 1 - reserve, y); display.print(dist); }
      display.setColor(DisplayDriver::LIGHT);
    });
    return mq_delay;
  }

  void renderWpNav(DisplayDriver& display) {
    int32_t tlat, tlon; const char* label;
    if (!rowTarget(_sel, tlat, tlon, label)) { _mode = LIST; return; }
    int32_t mylat, mylon; bool have = ownPos(mylat, mylon);
    int cog; bool cogv = _task->currentCourse(cog);
    navview::draw(display, have, mylat, mylon, tlat, tlon, label, cogv, cog, useImperial(), &_wp_eta);
  }

  // Navigate to the current track-back breadcrumb. The header doubles as the
  // progress readout: "Trail start" on the last leg, else points still to go.
  void renderTrackBack(DisplayDriver& display) {
    if (!_tb.active() || _tb.index() >= _store->count()) { _mode = OFF; return; }
    const TrailPoint& t = _store->at(_tb.index());
    int32_t mylat, mylon; bool have = ownPos(mylat, mylon);
    int cog; bool cogv = _task->currentCourse(cog);
    char label[20];
    _tb.label(label, sizeof(label));
    navview::draw(display, have, mylat, mylon, t.lat_1e6, t.lon_1e6,
                  label, cogv, cog, useImperial(), &_tb_eta);
  }

  // Resolve a combined-list row to a nav target. Row 0 is the trail start when
  // a trail exists; the rest are saved waypoints.
  bool rowTarget(int row, int32_t& lat, int32_t& lon, const char*& label) {
    if (hasStart() && row == 0) {
      const TrailPoint& s = _store->first();
      lat = s.lat_1e6; lon = s.lon_1e6; label = "Trail start";
      return true;
    }
    int wi = row - (hasStart() ? 1 : 0);
    WaypointStore& wp = _task->waypoints();
    if (wi >= 0 && wi < wp.count()) {
      const Waypoint& w = wp.at(wi);
      lat = w.lat_1e6; lon = w.lon_1e6;
      label = w.label[0] ? w.label : "(unnamed)";
      return true;
    }
    return false;
  }

public:
  WaypointsView(UITask* task, TrailStore* store) : _task(task), _store(store) {}

  void reset() { _mode = OFF; _ctx.active = false; _kb_active = false; _kb_field = -1; }

  // True while the component owns the screen (a sub-mode or the keyboard is up).
  bool active() const { return _mode != OFF || _kb_active; }

  // True in the sub-modes that read a live, continuous GPS position (bearing
  // to target, running average, walking a track back) -- as opposed to LIST
  // (just browsing) or ADD (typed coordinates, no fix involved). Used by
  // UITask's GPS duty-cycle hold so these don't stall waiting on a nap.
  bool needsLiveGps() const { return _mode == NAV || _mode == AVG || _mode == TRACKBACK; }

  // Entry points called from TrailScreen's action menu.
  void openList() { _mode = LIST; _sel = 0; _scroll = 0; }
  void markHere() {
    int32_t lat, lon;
    if (!ownPos(lat, lon))         { _task->showAlert("No GPS fix", 1000); return; }
    if (_task->waypoints().full()) { _task->showAlert("Waypoints full", 1000); return; }
    NodePrefs* p = _task->getNodePrefs();
    uint8_t avg_idx = p ? p->gps_avg_idx : 0;
    if (avg_idx == 0) { beginLabel(lat, lon); return; }   // instant mark (default)
    // Averaging: seed with the current fix, then sample for N more seconds.
    _avg.start(NodePrefs::gpsAvgSecs(avg_idx), lat, lon);
    _mode = AVG;
  }

  // Retrace the recorded trail back to its start. Snaps onto the route at the
  // nearest recorded point, then NavView guides to each earlier breadcrumb in
  // turn (poll() advances the target) until the start is reached.
  void startTrackBack() {
    int32_t lat = 0, lon = 0;
    bool have = ownPos(lat, lon);
    if (!_tb.start(*_store, have, lat, lon)) { _task->showAlert("No trail", 1000); return; }
    _tb_eta.reset();
    _mode = TRACKBACK;
  }

  // Driven from TrailScreen::poll() so the position-tracking sub-modes tick on
  // the main loop, not on the (slow on e-ink) render cadence.
  void poll() {
    if (_mode == AVG)       pollAvg();
    else if (_mode == TRACKBACK) pollTrackBack();
  }

  // Accumulate GPS fixes while averaging; mark the mean when the window closes.
  void pollAvg() {
    int32_t lat = 0, lon = 0, mlat, mlon;
    bool fix = ownPos(lat, lon);
    GpsAverager::Step st = _avg.poll(fix, lat, lon, mlat, mlon);
    if (st == GpsAverager::DONE) beginLabel(mlat, mlon);   // window closed: opens the label keyboard
    else if (st == GpsAverager::NO_FIX) { _task->showAlert("No GPS fix", 1000); _mode = OFF; }
  }

  // Advance the track-back target when the current breadcrumb is reached; once
  // at the start (index 0) and within range, announce arrival and exit.
  void pollTrackBack() {
    int32_t lat = 0, lon = 0;
    bool have = ownPos(lat, lon);
    TrackBack::Step st = _tb.poll(*_store, have, lat, lon);
    if (st == TrackBack::ADVANCED) _tb_eta.reset();
    else if (st == TrackBack::ARRIVED) { _task->showAlert("Back at start", 1500); _mode = OFF; }
    else if (!_tb.active()) _mode = OFF;   // trail went away
  }

  // Only called while active().
  int render(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    if (_kb_active) return _task->keyboard().render(display);  // keyboard owns the screen
    if (_mode == ADD) { renderAddForm(display); return 1000; }
    if (_mode == AVG) { renderAvg(display);     return display.isEink() ? 1000 : 300; }
    if (_mode == TRACKBACK) { renderTrackBack(display); return 1000; }
    if (_mode == NAV) { renderWpNav(display);   return 1000; }
    int mq_delay = renderWpList(display);            // LIST
    if (_ctx.active) _ctx.render(display);
    return (mq_delay > 0 && mq_delay < 1000) ? mq_delay : 1000;
  }

  // Returns true if the input was consumed (always, while active()).
  bool handleInput(char c) {
    // Label keyboard (mark new / rename / ADD field) takes all input first.
    if (_kb_active) {
      KeyboardWidget& kb = _task->keyboard();
      auto r = kb.handleInput(c);
      if (r == KeyboardWidget::DONE) {
        if (_kb_field >= 0) applyAddField(kb.buf);    // ADD field edit
        else                commitLabel(kb.buf);      // mark / rename label
        _kb_active = false; _kb_field = -1;
      } else if (r == KeyboardWidget::CANCELLED) {
        _kb_active = false; _kb_field = -1;
      }
      return true;
    }

    // Rename/Delete/Send popup — operates on the saved-waypoint index (the
    // synthetic Trail-start row, if any, is never editable).
    if (_ctx.active) {
      auto res = _ctx.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        int sel = _ctx.selectedIndex();
        int wi  = wpIndex();
        if (sel == 0) {                                  // Rename
          if (wi >= 0 && wi < _task->waypoints().count()) {
            _kb_rename_idx = wi;
            openKb(_task->waypoints().at(wi).label, WAYPOINT_LABEL_LEN - 1);
          }
        } else if (sel == 1) {                            // Delete
          if (wi >= 0 && wi < _task->waypoints().count()) {
            const Waypoint& w = _task->waypoints().at(wi);
            _task->clearTargetIfWaypoint(w.lat_1e6, w.lon_1e6);   // don't leave the Locator pointed at a deleted spot
            _task->waypoints().remove(wi);
            _task->saveWaypoints();
            if (_sel >= wpListCount()) _sel = wpListCount() - 1;
            if (_sel < 0) _sel = 0;
          }
        } else if (sel == 2) {                            // Send (share in a message)
          if (wi >= 0 && wi < _task->waypoints().count()) {
            const Waypoint& w = _task->waypoints().at(wi);
            double lat = w.lat_1e6 / 1000000.0, lon = w.lon_1e6 / 1000000.0;
            char text[80];
            if (w.label[0]) snprintf(text, sizeof(text), WAYPOINT_MSG_TAG "%.5f,%.5f %s", lat, lon, w.label);
            else            snprintf(text, sizeof(text), WAYPOINT_MSG_TAG "%.5f,%.5f", lat, lon);
            _task->shareToMessage(text);   // hands off to the Messages screen
          }
        } else {                                          // Set as target
          if (wi >= 0 && wi < _task->waypoints().count()) {
            const Waypoint& w = _task->waypoints().at(wi);
            _task->setTargetNow(0, nullptr, w.lat_1e6, w.lon_1e6, w.label);
          }
        }
      }
      return true;
    }

    // Averaging window — any cancel aborts the mark; otherwise just wait it out.
    if (_mode == AVG) {
      if (c == KEY_CANCEL) { _avg.cancel(); _mode = OFF; }
      return true;
    }

    // ADD form: scroll-edit lat/lon (magnitude) + hemisphere toggle + label.
    if (_mode == ADD) {
      // The digit editor, while open, consumes all input (UP/DOWN change the
      // digit, LEFT/RIGHT move the cursor, Enter commits, Cancel aborts).
      if (_add_editor.active) {
        if (_add_editor.handleInput(c) == DigitEditor::DONE) {
          if (_add_sel == 0) _add_lat_mag = _add_editor.value;
          else               _add_lon_mag = _add_editor.value;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _mode = OFF; return true; }
      if (c == KEY_UP)   { _add_sel = (_add_sel > 0) ? _add_sel - 1 : 3; return true; }
      if (c == KEY_DOWN) { _add_sel = (_add_sel < 3) ? _add_sel + 1 : 0; return true; }
      if (keyIsPrev(c) || keyIsNext(c)) {
        if      (_add_sel == 0) _add_lat_neg = !_add_lat_neg;   // N <-> S
        else if (_add_sel == 1) _add_lon_neg = !_add_lon_neg;   // E <-> W
        return true;
      }
      if (c == KEY_ENTER) {
        if      (_add_sel == 0) _add_editor.begin(_add_lat_mag, 0.0f,  90.0f, 2, 5);
        else if (_add_sel == 1) _add_editor.begin(_add_lon_mag, 0.0f, 180.0f, 3, 5);
        else if (_add_sel == 2) { _kb_field = 2; openKb(_add_label, WAYPOINT_LABEL_LEN - 1); }
        else                    { commitAddForm(); }
        return true;
      }
      return true;
    }

    // Navigation view — Back returns to the list, as in every navigate view.
    if (_mode == NAV) {
      if (c == KEY_CANCEL) { _mode = LIST; }
      return true;
    }

    // Track-back — launched from the action menu, so Cancel returns to the
    // trail views (not the waypoint list).
    if (_mode == TRACKBACK) {
      if (c == KEY_CANCEL) { _tb.stop(); _mode = OFF; }
      return true;
    }

    // List (row 0 is the synthetic "Trail start" when a trail exists; the final
    // row is "+ Add by coords"). Cancel returns control to the trail views.
    int n = wpListCount();
    int total = n + 1;                       // + the "Add by coords" row
    if (c == KEY_CANCEL) { _mode = OFF; return true; }
    // drawList() reclamps _scroll from _sel every render.
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : total - 1; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < total - 1) ? _sel + 1 : 0; return true; }
    if (c == KEY_ENTER) {
      if (_sel == n) openAddForm();          // last row → open the add form
      else           { _mode = NAV; _wp_eta.reset(); }   // a waypoint / Trail-start row
      return true;
    }
    // Rename/Delete/Send/Locator apply to saved waypoints only — not Trail-start or Add.
    if (c == KEY_CONTEXT_MENU && !selIsStart() && _sel != n &&
        wpIndex() >= 0 && wpIndex() < _task->waypoints().count()) {
      _ctx.begin("Waypoint", 4);
      _ctx.addItem("Rename");
      _ctx.addItem("Delete");
      _ctx.addItem("Send");
      _ctx.addItem("Set as target");
      return true;
    }
    return true;
  }
};
