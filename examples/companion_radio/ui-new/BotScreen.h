#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after KeyboardWidget.h is defined.

#include "TabBar.h"
#include "../ui-core/BotConfig.h"

class BotScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  // Categories are a circular tab bar in the header (shared geometry with
  // NearbyScreen's filter tabs and AdminScreen's category tabs — see
  // TabBar.h). LEFT/RIGHT switches tabs; UP/DOWN moves within the active
  // tab's rows; every value is edited via Enter. The rows and their NodePrefs
  // fields are the UI Core's table (ui-core/BotConfig.h, shared with ui-lvgl).
  typedef botcfg::Kind Kind;
  typedef botcfg::Row Row;
  static const Row& tabRow(int tab, int idx) { return botcfg::row(tab, idx); }

  uint8_t _tab;    // persists across visits, like NearbyScreen's _filter
  int  _sel;       // selected row index within the active tab
  int  _scroll;    // index of first visible row (managed by drawList in render)
  bool _dirty;

  // keyboard state (reused for trigger and reply fields)
  int             _kb_row;   // -1=off, else the row index (within the active tab) being edited
  KeyboardWidget* _kb;

  // Quiet-hours stepper: Enter on Quiet from/to enters this sub-mode (LEFT/
  // RIGHT is unavailable for +/- now that it switches tabs), where UP/DOWN
  // steps the hour and Enter/Cancel exits back to normal row browsing.
  int _stepper_row;  // -1=off, else the row index being stepped

  // channel/room counts (refreshed on enter) — just gate whether Enter opens
  // the (possibly empty) picker; the picker itself browses the real list, so
  // no local index cache is needed now that there's no LEFT/RIGHT quick-cycle.
  int _num_channels;
  int _num_rooms;

  void refreshChannels() {
    _num_channels = 0;
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') _num_channels++;
  }

  void refreshRooms() {
    _num_rooms = 0;
    ContactInfo ci;
    int total = the_mesh.getNumContacts();
    // +MAX_ANON_CONTACTS: getContactByIdx() indexes the raw contacts[] table,
    // whose first MAX_ANON_CONTACTS slots are reserved anon-request entries
    // that getNumContacts() already excludes from its count (see
    // NearbyScreen.h's own contact scan, and MessagesScreen.h's
    // buildContactList(), for the same offset) -- without it this underrode
    // real room-server contacts by up to MAX_ANON_CONTACTS.
    for (int i = 0; i < total; i++)
      if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) && ci.type == ADV_TYPE_ROOM) _num_rooms++;
  }

  // Header as a circular tab bar (shared geometry — see TabBar.h). `right_reserve`
  // keeps the reply counter (drawn after this call) clear of the rightmost tab.
  void drawTabBar(DisplayDriver& display, int right_reserve) {
    tabbar::draw(display, botcfg::TAB_LABELS, botcfg::TAB_COUNT, _tab, right_reserve);
  }

public:
  BotScreen(UITask* task, NodePrefs* prefs, KeyboardWidget* kb)
    : _task(task), _prefs(prefs), _tab(botcfg::TAB_CHANNEL), _kb(kb) {}

  void onShow() override {
    // _tab persists across visits (like NearbyScreen's _filter) — only reset
    // the in-tab cursor and any open sub-mode.
    _sel        = 0;
    _scroll     = 0;
    _kb_row     = -1;
    _stepper_row = -1;
    _dirty      = false;
    refreshChannels();
    refreshRooms();
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_kb_row >= 0) {
      return _kb->render(display);
    }

    int val_x = display.valCol();

    // Reply counter, right-aligned in the header — reserve its width from the
    // tab bar before drawing so a right-side tab can't run under it.
    uint16_t sent = the_mesh.botReplyCount();
    char cbuf[12];
    int  cw = 0;
    if (sent > 0) {
      snprintf(cbuf, sizeof(cbuf), "%u", (unsigned)sent);
      cw = display.getTextWidth(cbuf);
    }
    drawTabBar(display, sent > 0 ? cw + 2 : 0);
    if (sent > 0) {
      display.setCursor(display.width() - cw - 1, 0);
      display.print(cbuf);
      display.setColor(DisplayDriver::LIGHT);
    }

    int n = botcfg::rowCount(_tab);
    int mq_delay = 0;
    drawList(display, n, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      Row r = tabRow(_tab, i);
      drawRowSelection(display, y, sel, reserve);
      display.setCursor(2, y);
      display.print(r.label);
      display.setCursor(val_x, y);

      int cap;
      const char* txt = botcfg::text(_prefs, r.kind, cap);
      const uint8_t* fl = botcfg::flag(_prefs, r.kind);
      char name[32];
      if (fl) {
        display.print(r.kind == botcfg::DM_SCOPE ? (*fl ? "Fav" : "All") : (*fl ? "ON" : "OFF"));
      } else if (txt) {
        int mqr = display.drawTextEllipsized(val_x, y, display.width() - val_x - 1 - reserve,
                                             botcfg::shownText(txt, botcfg::isTrigger(r.kind)), sel);
        if (sel && mqr > 0) mq_delay = mqr;
      } else if (r.kind == botcfg::CHANNEL || r.kind == botcfg::ROOM) {
        bool any = r.kind == botcfg::CHANNEL ? _num_channels > 0 : _num_rooms > 0;
        bool ok = r.kind == botcfg::CHANNEL ? botcfg::channelName(_prefs, name, sizeof(name))
                                            : botcfg::roomName(_prefs, name, sizeof(name));
        if (!any) display.print("(none)");
        else if (!ok) display.print("?");
        else {
          int mqr = display.drawTextEllipsized(val_x, y, display.width() - val_x - 1 - reserve, name, sel);
          if (sel && mqr > 0) mq_delay = mqr;
        }
      } else if (botcfg::isHour(r.kind)) {
        char hb[10];
        if (botcfg::quietOff(_prefs)) strcpy(hb, "OFF");
        else snprintf(hb, sizeof(hb), "%02d:00", botcfg::hour(_prefs, r.kind));
        // Bracket the value while the stepper sub-mode is open on this row,
        // as a visual cue that UP/DOWN now steps it instead of moving rows.
        if (_stepper_row == i) {
          char bracketed[14];
          snprintf(bracketed, sizeof(bracketed), "[%s]", hb);
          display.print(bracketed);
        } else {
          display.print(hb);
        }
      }
      display.setColor(DisplayDriver::LIGHT);
    });
    return 2000;
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool enter = (c == KEY_ENTER);
    bool cancel = (c == KEY_CANCEL);

    if (_kb_row >= 0) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        Kind  k   = tabRow(_tab, _kb_row).kind;
        int   cap = 0;
        char* dst = botcfg::text(_prefs, k, cap);
        if (dst) { strncpy(dst, _kb->buf, cap - 1); dst[cap - 1] = '\0'; }
        _dirty  = true;
        _kb_row = -1;
      } else if (res == KeyboardWidget::CANCELLED) {
        _kb_row = -1;
      }
      return true;
    }

    if (_stepper_row >= 0) {
      uint8_t& h = botcfg::hour(_prefs, tabRow(_tab, _stepper_row).kind);
      if (up)   { h = (h + 1) % 24;  _dirty = true; return true; }
      if (down) { h = (h + 23) % 24; _dirty = true; return true; }
      if (enter || cancel) { _stepper_row = -1; return true; }
      return true;  // swallow LEFT/RIGHT etc. while stepping
    }

    if (cancel) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoToolsScreen();
      return true;
    }
    if (keyIsPrev(c)) { _tab = (_tab + botcfg::TAB_COUNT - 1) % botcfg::TAB_COUNT; _sel = _scroll = 0; return true; }
    if (keyIsNext(c)) { _tab = (_tab + 1) % botcfg::TAB_COUNT;             _sel = _scroll = 0; return true; }

    int n = botcfg::rowCount(_tab);
    // drawList() reclamps _scroll from _sel every render.
    if (up)   { _sel = (_sel > 0) ? _sel - 1 : n - 1; return true; }
    if (down) { _sel = (_sel < n - 1) ? _sel + 1 : 0; return true; }

    if (!enter) return false;
    Kind k = tabRow(_tab, _sel).kind;

    if (uint8_t* fl = botcfg::flag(_prefs, k)) { *fl ^= 1; _dirty = true; return true; }
    int cap = 0;
    if (char* txt = botcfg::text(_prefs, k, cap)) {
      _kb_row = _sel;
      _kb->begin(txt, cap - 1);
      _kb->prompt = botcfg::isTrigger(k) ? "Trigger" : "Reply";
      if (botcfg::isTrigger(k)) {
        _kb->clearPlaceholders();  // trigger is literal — placeholders never match
      } else {
        kbAddSensorPlaceholders(*_kb, &sensors);
        // Only meaningful in a bot reply (there's an actual triggering
        // sender/hop-count to fill them with) — not offered on the general
        // compose keyboard's own placeholder list.
        _kb->addPlaceholder("{name}");
        _kb->addPlaceholder("{hops}");
      }
      return true;
    }
    switch (k) {
      case botcfg::CHANNEL:
        // Full browsable channel picker (same experience as Live Share's "To"
        // row); which channel is *active* is the separate Enable row.
        if (_num_channels == 0) return false;
        _task->savePrefsIfDirty(_dirty);
        _task->pickBotChannelTarget();
        return true;
      case botcfg::ROOM:
        if (_num_rooms == 0) return false;
        _task->savePrefsIfDirty(_dirty);
        _task->pickBotRoomTarget();
        return true;
      case botcfg::QUIET_FROM:
      case botcfg::QUIET_TO:
        _stepper_row = _sel;
        return true;
      default: return false;
    }
  }
};
