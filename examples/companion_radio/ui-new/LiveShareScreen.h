#pragma once
// Live location sharing config tool. Tools › Live Share.
// One place for both directions of position sharing:
//   Track loc  — receive [LOC] shares from others (LiveTrackStore).
//   Auto share — periodically broadcast my own [LOC] to a channel/contact
//                while moving (movement-gated engine in UITask).
// Independent of (and able to run alongside) the 0-hop Auto-Advert. The
// one-shot "Share my pos" lives on the Map screen.
// Included by UITask.cpp after AutoAdvertScreen.h.

#include "../NodePrefs.h"
#include "icons.h"   // drawList (shared scrolling-list helper)

class LiveShareScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  bool       _dirty  = false;
  int        _sel    = 0;
  int        _scroll = 0;

  enum Kind : uint8_t { K_TRACK, K_AUTO, K_DURATION, K_TARGET, K_SCOPE, K_MOVE, K_GAP, K_HB };
  struct Row { Kind kind; const char* label; };
  static const int ROW_COUNT = 8;
  static Row rows(int i) {
    static const Row R[ROW_COUNT] = {
      { K_TRACK,  "Track loc" },
      { K_AUTO,   "Auto share" },
      { K_DURATION, "Stop after" },
      { K_TARGET, "To" },
      { K_SCOPE,  "Scope" },
      { K_MOVE,   "Move" },
      { K_GAP,    "Min gap" },
      { K_HB,     "Heartbeat" },
    };
    return R[i];
  }

public:
  LiveShareScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _dirty = false; _sel = 0; _scroll = 0; }

  // Resolve the configured target's display name (channel name or contact name).
  void currentTargetName(char* buf, int n) {
    if (!_prefs) { snprintf(buf, n, "none"); return; }
    if (_prefs->loc_share_target_type == 0) {
      ChannelDetails ch;
      if (the_mesh.getChannel(_prefs->loc_share_channel_idx, ch) && ch.name[0])
        snprintf(buf, n, "%s", ch.name);
      else
        snprintf(buf, n, "Ch %d", (int)_prefs->loc_share_channel_idx);
    } else {
      ContactInfo* c = the_mesh.lookupContactByPubKey(_prefs->loc_share_dm_prefix, NodePrefs::FAVOURITE_PREFIX_LEN);
      if (c) snprintf(buf, n, "%s", c->name);
      else   snprintf(buf, n, "(contact?)");
    }
  }

  void valueLabel(Kind k, char* buf, int n) {
    switch (k) {
      case K_TRACK:
        snprintf(buf, n, "%s", (_prefs && _prefs->track_shared_loc) ? "ON" : "OFF");
        break;
      case K_AUTO:
        snprintf(buf, n, "%s", (_prefs && _prefs->loc_share_enabled) ? "ON" : "OFF");
        break;
      case K_DURATION:
        snprintf(buf, n, "%uh", (unsigned)(NodePrefs::locShareDurationMins(_prefs ? _prefs->loc_share_duration_idx : 0) / 60));
        break;
      case K_TARGET:
        currentTargetName(buf, n);
        break;
      case K_SCOPE: {
        // 0 = follow the target; n>0 = scope-list index n-1 ("*" first).
        uint8_t v = _prefs ? _prefs->loc_share_scope : 0;
        const ScopeList& sl = the_mesh.scopeList();
        if (v == 0 || v > sl.count + 1) snprintf(buf, n, "Target");
        else                            snprintf(buf, n, "%s", sl.name(v - 1));
        break;
      }
      case K_MOVE:
        snprintf(buf, n, "%um", (unsigned)NodePrefs::locShareMoveMeters(_prefs ? _prefs->loc_share_move_idx : 1));
        break;
      case K_GAP: {
        uint16_t s = NodePrefs::locShareIntervalSecs(_prefs ? _prefs->loc_share_interval_idx : 1);
        if (s < 60) snprintf(buf, n, "%us", (unsigned)s);
        else        snprintf(buf, n, "%um", (unsigned)(s / 60));
        break;
      }
      case K_HB: {
        uint16_t s = NodePrefs::locShareHeartbeatSecs(_prefs ? _prefs->loc_share_heartbeat_idx : 0);
        if (s == 0) snprintf(buf, n, "OFF");
        else        snprintf(buf, n, "%um", (unsigned)(s / 60));
        break;
      }
      default: buf[0] = '\0';
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawCenteredHeader("LIVE SHARE");

    const int valx = display.width() / 2 + 6;
    int mq_delay = 0;
    drawList(display, ROW_COUNT, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      Row r = rows(i);
      drawRowSelection(display, y, sel, reserve);
      display.setCursor(4, y);
      display.print(r.label);
      char val[24];
      valueLabel(r.kind, val, sizeof(val));
      if (val[0]) {
        int mqr = display.drawTextEllipsized(valx, y, display.width() - valx - reserve, val, sel);
        if (sel && mqr > 0) mq_delay = mqr;
      }
    });
    return (mq_delay > 0 && mq_delay < 500) ? mq_delay : 500;
  }

  void moveSel(int dir) { _sel = (_sel + dir + ROW_COUNT) % ROW_COUNT; }

  // Cycle/act on the selected row. `enter` opens the target picker (vs LEFT/RIGHT
  // value cycling).
  void activate(int dir, bool enter) {
    if (!_prefs) return;
    switch (rows(_sel).kind) {
      case K_TRACK: _prefs->track_shared_loc ^= 1; _dirty = true; break;
      case K_AUTO:
        _prefs->loc_share_enabled ^= 1;
        if (_prefs->loc_share_enabled) _task->restartLocShareSession();
        _dirty = true; break;
      case K_DURATION:
        _prefs->loc_share_duration_idx = (uint8_t)((_prefs->loc_share_duration_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_DURATION_COUNT - 1)) % NodePrefs::LOC_SHARE_DURATION_COUNT);
        _task->restartLocShareSession();   // a new length starts the session over
        _dirty = true; break;
      case K_TARGET:
        if (enter) { _task->pickLocShareTarget(); return; }  // full chooser
        cycleTarget(dir); _dirty = true;                     // L/R quick cycle
        break;
      case K_SCOPE: {
        // Target, then every list entry ("*" included): count + 2 stops, wrapping.
        int total = the_mesh.scopeList().count + 2;
        int v = _prefs->loc_share_scope;
        if (v >= total) v = 0;
        v = (v + (dir >= 0 ? 1 : total - 1)) % total;
        _prefs->loc_share_scope = (uint8_t)v;
        _dirty = true; break;
      }
      case K_MOVE:
        _prefs->loc_share_move_idx = (uint8_t)((_prefs->loc_share_move_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_MOVE_COUNT - 1)) % NodePrefs::LOC_SHARE_MOVE_COUNT);
        _dirty = true; break;
      case K_GAP:
        _prefs->loc_share_interval_idx = (uint8_t)((_prefs->loc_share_interval_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_INTERVAL_COUNT - 1)) % NodePrefs::LOC_SHARE_INTERVAL_COUNT);
        _dirty = true; break;
      case K_HB:
        _prefs->loc_share_heartbeat_idx = (uint8_t)((_prefs->loc_share_heartbeat_idx + (dir >= 0 ? 1 : NodePrefs::LOC_SHARE_HEARTBEAT_COUNT - 1)) % NodePrefs::LOC_SHARE_HEARTBEAT_COUNT);
        _dirty = true; break;
    }
  }

  void cycleTarget(int dir) {
    struct T { uint8_t type, ch; uint8_t prefix[NodePrefs::FAVOURITE_PREFIX_LEN]; } list[24];
    int n = 0;
    ChannelDetails ch;
    for (int i = 0; i < 64 && n < 24; i++) {
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
      list[n].type = 0; list[n].ch = (uint8_t)i; n++;
    }
    for (int i = 0; i < NodePrefs::FAVOURITES_COUNT && n < 24; i++) {
      const uint8_t* pre = _prefs->favourite_contacts[i];
      bool empty = true;
      for (int b = 0; b < NodePrefs::FAVOURITE_PREFIX_LEN; b++) if (pre[b]) { empty = false; break; }
      ContactInfo* fc = the_mesh.lookupContactByPubKey(pre, NodePrefs::FAVOURITE_PREFIX_LEN);
      // Skip room servers: a [LOC] DM to a room server isn't reposted to the
      // room's members, so it can't serve as a live-share broadcast target.
      if (empty || !fc || fc->type == ADV_TYPE_ROOM) continue;
      list[n].type = 1; list[n].ch = 0;
      memcpy(list[n].prefix, pre, NodePrefs::FAVOURITE_PREFIX_LEN); n++;
    }
    if (n == 0) { _task->showAlert("No channels/favs", 1200); return; }
    int cur = -1;
    for (int i = 0; i < n; i++) {
      if (list[i].type != _prefs->loc_share_target_type) continue;
      if (list[i].type == 0) { if (list[i].ch == _prefs->loc_share_channel_idx) { cur = i; break; } }
      else if (memcmp(list[i].prefix, _prefs->loc_share_dm_prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) { cur = i; break; }
    }
    int nx = (cur < 0) ? 0 : ((cur + (dir >= 0 ? 1 : n - 1)) % n);
    _prefs->loc_share_target_type = list[nx].type;
    if (list[nx].type == 0) _prefs->loc_share_channel_idx = list[nx].ch;
    else memcpy(_prefs->loc_share_dm_prefix, list[nx].prefix, NodePrefs::FAVOURITE_PREFIX_LEN);
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { moveSel(-1); return true; }
    if (c == KEY_DOWN) { moveSel(+1); return true; }
    if (keyIsPrev(c))  { activate(-1, false); return true; }
    if (keyIsNext(c))  { activate(+1, false); return true; }
    if (c == KEY_ENTER) { activate(+1, true); return true; }
    return false;
  }
};
