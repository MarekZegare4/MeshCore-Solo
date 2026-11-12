#pragma once
// Tools › SOS: what happens on a call for help, and the status while one runs.
// The status (renderStatus) isn't this screen's list: UITask draws it over
// whatever is showing, on a locked device too. Engine: ui-core/SosEngine.h.
// Included by UITask.cpp after LiveShareScreen.h.

#include "InfoKit.h"
#include "../NodePrefs.h"
#include "icons.h"   // drawList (shared scrolling-list helper)

class SosScreen : public UIScreen {
  UITask*         _task;
  NodePrefs*      _prefs;
  KeyboardWidget* _kb;
  bool _dirty  = false;
  bool _picking = false;    // off to the contact picker: the changes stay unsaved until we leave
  int  _sel    = 0;
  int  _scroll = 0;
  int  _kb_row = -1;        // >=0: the keyboard is open on that row
  int  _blink_left = -1;    // e-ink: the countdown second last drawn...
  bool _blink_lit = false;  // ...and whether it was the negative

  bool    _ch_list = false; // the Channels list instead of the rows
  int     _ch_sel = 0, _ch_scroll = 0, _ch_count = 0;
  uint8_t _ch_idx[MAX_GROUP_CHANNELS];

  enum Kind : uint8_t { K_HOLD, K_COUNTDOWN, K_REPEAT, K_ADVERT, K_BEEP, K_CHANNELS, K_DM1, K_DM2, K_DM3, K_ACK, K_MSG, K_START };
  struct Row { Kind kind; const char* label; };
  static const int ROW_COUNT = 12;
  // The L2's order: how it starts, who it goes to and what it says, then
  // what happens while it runs.
  static Row rows(int i) {
    static const Row R[ROW_COUNT] = {
      { K_HOLD,      "Hold Back" },
      { K_COUNTDOWN, "Countdown" },
      { K_CHANNELS,  "Channels" },
      { K_DM1,       "Contact 1" },
      { K_DM2,       "Contact 2" },
      { K_DM3,       "Contact 3" },
      { K_MSG,       "Message" },
      { K_REPEAT,    "Repeat" },
      { K_ADVERT,    "Advert" },
      { K_BEEP,      "Beep" },
      { K_ACK,       "ACK word" },
      { K_START,     "Send SOS" },
    };
    return R[i];
  }

  static void step(uint8_t& idx, int count, int dir) {
    idx = (uint8_t)((idx + (dir >= 0 ? 1 : count - 1)) % count);
  }

  void valueLabel(Kind k, char* buf, int n) {
    switch (k) {
      case K_HOLD:      snprintf(buf, n, "%s", _prefs->sos_enabled ? "ON" : "OFF"); break;
      case K_COUNTDOWN: snprintf(buf, n, "%u s", (unsigned)NodePrefs::sosCountdownSecs(_prefs->sos_countdown_idx)); break;
      case K_REPEAT: {
        unsigned m = NodePrefs::sosRepeatMins(_prefs->sos_repeat_idx);
        if (m) snprintf(buf, n, "%u min", m);
        else   snprintf(buf, n, "Off");
        break;
      }
      case K_ADVERT: snprintf(buf, n, "%s", NodePrefs::sosAdvertLabel(_prefs->sos_advert)); break;
      case K_BEEP: {
        unsigned s = NodePrefs::sosBeepSecs(_prefs->sos_beep_idx);
        if (!s)        snprintf(buf, n, "Off");
        else if (s < 60) snprintf(buf, n, "%u s", s);
        else           snprintf(buf, n, "%u min", s / 60);
        break;
      }
      case K_CHANNELS: snprintf(buf, n, "%d on", SosEngine::channels(_prefs)); break;
      case K_DM1: case K_DM2: case K_DM3: {
        int slot = k - K_DM1;
        ContactInfo* c = SosEngine::hasDm(_prefs, slot)
          ? the_mesh.lookupContactByPubKey(_prefs->sos_dm[slot], NodePrefs::FAVOURITE_PREFIX_LEN) : nullptr;
        snprintf(buf, n, "%s", c ? c->name : "-");
        break;
      }
      case K_ACK:  snprintf(buf, n, "%s", _prefs->sos_ack[0] ? _prefs->sos_ack : "-"); break;
      case K_MSG: snprintf(buf, n, "%s", _prefs->sos_msg[0] ? _prefs->sos_msg : "-"); break;
      case K_START: snprintf(buf, n, "%s", _task->core().sos.idle() ? "" : "Running"); break;
    }
  }

  void openChannels() {
    _ch_count = 0;
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      if (the_mesh.getChannel(i, ch) && ch.name[0]) _ch_idx[_ch_count++] = (uint8_t)i;
    _ch_sel = _ch_scroll = 0;
    _ch_list = true;
  }

  int renderChannels(DisplayDriver& display) {
    drawScreenHeader(display, "SOS channels");
    int mq_delay = 0;
    drawList(display, _ch_count, _ch_sel, _ch_scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      ChannelDetails ch;
      char name[32] = "";
      if (the_mesh.getChannel(_ch_idx[i], ch)) display.translateUTF8ToBlocks(name, ch.name, sizeof(name));
      bool on = _prefs->sos_ch_mask & (1ULL << _ch_idx[i]);
      int mqr = info::listRow(display, y, name, on ? "ON" : "OFF", sel, reserve);
      if (mqr > 0) mq_delay = mqr;
    });
    return (mq_delay > 0 && mq_delay < 500) ? mq_delay : 500;
  }

  bool channelsInput(char c) {
    if (c == KEY_CANCEL) { _ch_list = false; return true; }
    if (_ch_count == 0) return true;
    if (c == KEY_UP)   { _ch_sel = (_ch_sel + _ch_count - 1) % _ch_count; return true; }
    if (c == KEY_DOWN) { _ch_sel = (_ch_sel + 1) % _ch_count; return true; }
    if (c == KEY_ENTER || keyIsPrev(c) || keyIsNext(c)) {
      _prefs->sos_ch_mask ^= 1ULL << _ch_idx[_ch_sel];
      _dirty = true;
      return true;
    }
    return false;
  }

  // Enter: LEFT/RIGHT only step the values, or empty a contact.
  void activate(int dir, bool enter) {
    Kind k = rows(_sel).kind;
    switch (k) {
      case K_HOLD:
        _prefs->sos_enabled ^= 1;
        _dirty = true;
        if (_prefs->sos_enabled && !SosEngine::hasTargets(_prefs)) _task->showAlert("Choose who it goes to", 1500);
        break;
      case K_COUNTDOWN:
        step(_prefs->sos_countdown_idx, NodePrefs::SOS_COUNTDOWN_COUNT, dir);
        _dirty = true; break;
      case K_REPEAT:
        step(_prefs->sos_repeat_idx, NodePrefs::SOS_REPEAT_COUNT, dir);
        _dirty = true; break;
      case K_ADVERT:
        step(_prefs->sos_advert, NodePrefs::SOS_ADVERT_COUNT, dir);
        _dirty = true; break;
      case K_BEEP:
        step(_prefs->sos_beep_idx, NodePrefs::SOS_BEEP_COUNT, dir);
        _dirty = true; break;
      case K_CHANNELS:
        if (enter) openChannels();
        break;
      case K_DM1: case K_DM2: case K_DM3:
        // No save on the way: a prefs write stalls the L1 for a moment, and
        // leaving this screen saves the pick with the rest.
        if (enter) { _picking = true; _task->pickSosContact(k - K_DM1); }
        else { memset(_prefs->sos_dm[k - K_DM1], 0, NodePrefs::FAVOURITE_PREFIX_LEN); _dirty = true; }
        break;
      case K_ACK: case K_MSG:
        if (enter) {
          bool ack = k == K_ACK;
          _kb_row = _sel;
          _kb->begin(ack ? _prefs->sos_ack : _prefs->sos_msg, (ack ? sizeof(_prefs->sos_ack) : sizeof(_prefs->sos_msg)) - 1);
          _kb->prompt = ack ? "ACK word" : "Message";
          _kb->clearPlaceholders();   // the ACK word is matched as it is
          if (!ack) for (int i = 0; i < SosEngine::PLACEHOLDER_COUNT; i++) _kb->addPlaceholder(SosEngine::placeholder(i));
        }
        break;
      case K_START:
        if (enter) { _task->savePrefsIfDirty(_dirty); _task->startSos(); }
        break;
    }
  }

public:
  SosScreen(UITask* task, NodePrefs* prefs, KeyboardWidget* kb) : _task(task), _prefs(prefs), _kb(kb) {}

  void onShow() override {   // the row stays: the contact list comes back here
    _dirty = _picking;   // back from the picker: what was changed, and the pick, still to save
    _picking = false;
    _kb_row = -1;
    _ch_list = false;
  }

  int render(DisplayDriver& display) override {
    if (_kb_row >= 0) return _kb->render(display);
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);
    if (_ch_list) return renderChannels(display);
    drawScreenHeader(display, "SOS");
    int mq_delay = 0;
    drawList(display, ROW_COUNT, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      Row r = rows(i);
      drawRowSelection(display, y, sel, reserve);
      char val[64], shown[64];
      valueLabel(r.kind, val, sizeof(val));
      display.translateUTF8ToBlocks(shown, val, sizeof(shown));
      const char* label = r.kind == K_HOLD && _task->oneButtonUi() ? "Hold 5 s" : r.label;   // the button held, not Back
      int mqr = info::listRow(display, y, label, shown, sel, reserve);
      if (mqr > 0) mq_delay = mqr;
    });
    return (mq_delay > 0 && mq_delay < 500) ? mq_delay : 500;
  }

  bool handleInput(char c) override {
    if (_kb_row >= 0) {
      auto res = _kb->handleInput(c);
      if (res == KeyboardWidget::DONE) {
        bool ack = rows(_kb_row).kind == K_ACK;
        char* dst = ack ? _prefs->sos_ack : _prefs->sos_msg;
        snprintf(dst, ack ? sizeof(_prefs->sos_ack) : sizeof(_prefs->sos_msg), "%s", _kb->buf);
        _dirty = true;
        _kb_row = -1;
      } else if (res == KeyboardWidget::CANCELLED) {
        _kb_row = -1;
      }
      return true;
    }
    if (_ch_list) return channelsInput(c);
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _sel = _scroll = 0;
      _task->leaveTool();
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel + ROW_COUNT - 1) % ROW_COUNT; return true; }
    if (c == KEY_DOWN) { _sel = (_sel + 1) % ROW_COUNT; return true; }
    if (keyIsPrev(c))  { activate(-1, false); return true; }
    if (keyIsNext(c))  { activate(+1, false); return true; }
    if (c == KEY_ENTER) { activate(+1, true); return true; }
    return false;
  }

  // The countdown, then what has been sent and who answered. Returns the delay
  // to the next frame. On e-ink only the countdown ticks: a send or an answer
  // redraws the view anyway (UITask::openSosView).
  int renderStatus(DisplayDriver& d) {
    SosEngine& s = _task->core().sos;
    const uint32_t now = millis();
    const bool eink = d.isEink();
    const int status_ms = eink ? 15000 : 1000;
    const int cx = d.width() / 2;
    const int foot = d.height() - d.getLineHeight() - 1;
    char buf[40];
    d.setTextSize(1);
    d.setColor(UIColor::primary_txt);

    if (s.counting()) {
      // The screen blinks, to be seen as well as heard (one started in a
      // pocket), faster near the end. E-ink turns over with each new second
      // drawn, not by the clock: a large panel takes longer than a second to
      // refresh, and timed, the frame after came on the same side now and then.
      const int left = s.secsLeft(now);
      const uint32_t half = eink ? 1000 : left > 10 ? 500 : left > 5 ? 250 : 125;
      if (eink && left != _blink_left) { _blink_left = left; _blink_lit = !_blink_lit; }
      const bool lit = eink ? _blink_lit : (now / half) & 1;
      // An OLED inverts in its controller: the same frame each time, nothing
      // sent but the command (a drawn negative is the whole frame, and tore).
      if (lit && !d.invertFrame()) {
        d.fillRect(0, 0, d.width(), d.height());
        d.setColor(UIColor::window_bkg);
        d.drawTextCentered(cx, 0, "SOS in");
      } else {
        drawScreenHeader(d, "SOS in");
      }
      snprintf(buf, sizeof(buf), "%d", left);
      d.setTextSize(2);
      d.drawTextCentered(cx, d.listStart() + 4, buf);
      d.setTextSize(1);
      d.drawTextCentered(cx, foot, "Any key: cancel");
      d.setColor(UIColor::primary_txt);
      return (int)(half - now % half);
    }
    drawScreenHeader(d, s.acked() ? "Answered" : "SOS");

    int y = d.listStart();
    if (s.acked()) {
      const SosEngine::Responder& r = s.responder(0);
      char line[64];
      const uint32_t mins = (now - r.at) / 60000UL;
      if (s.responders() > 1) snprintf(line, sizeof(line), "%s +%d", r.name, s.responders() - 1);
      else                    snprintf(line, sizeof(line), "%s", r.name);
      d.translateUTF8ToBlocks(buf, line, sizeof(buf));
      d.drawTextEllipsized(2, y, d.width() - 4, buf, false);
      y += d.lineStep();
      if (r.text[0]) {
        d.translateUTF8ToBlocks(buf, r.text, sizeof(buf));
        d.drawTextEllipsized(2, y, d.width() - 4, buf, false);
        y += d.lineStep();
      }
      if (mins) { snprintf(buf, sizeof(buf), "%u min ago", (unsigned)mins); d.drawTextCentered(cx, y, buf); }
      d.drawTextCentered(cx, foot, _task->oneButtonUi() ? "Hold 2 s: end" : "Hold Enter: end");
      return status_ms;
    }
    s.sentLine(buf, sizeof(buf));
    d.drawTextCentered(cx, y, buf);
    y += d.lineStep();
    s.nextLine(buf, sizeof(buf), now, eink);
    d.drawTextCentered(cx, y, buf);
    int32_t lat, lon;
    if (!_task->currentLocation(lat, lon)) d.drawTextCentered(cx, y + d.lineStep(), "No GPS fix");
    d.drawTextCentered(cx, foot, _task->oneButtonUi() ? "Hold 2 s: stop" : "Hold Enter: stop");
    return status_ms;
  }
};
