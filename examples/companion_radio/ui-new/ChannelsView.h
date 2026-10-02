#pragma once
// On-device channel Add/Edit form (Messages > Channels > "+ Add channel" / Edit).
// Owned by MessagesScreen, which delegates render()/handleInput() to it while
// active() — the same relationship WaypointsView has with TrailScreen.
//
// "+ Add channel" starts with a Type picker (Public/Hashtag/Private), matching
// the phone app's channel-type trichotomy (docs/companion_protocol.md's
// "Channel Types") instead of exposing the raw Name+Secret form directly:
//   - Public: one tap re-adds the well-known default channel (same secret
//     MyMesh.cpp pre-configures at first boot) — no fields to fill in.
//   - Hashtag: type just a topic name; the channel name and secret are both
//     derived from it ("#topic", secret = first 16 bytes of sha256("#topic")).
//     A topic-based public group chat, discoverable without knowing the hash
//     convention exists.
//   - Private: today's manual Name + Secret form (see below) — a channel only
//     those you share the secret with can join.
// Editing an existing channel (openEdit()) skips the Type picker and goes
// straight to the Name+Secret form — there's no ambiguity to resolve there.
//
// The Secret field (Private, and internally for Public/Hashtag) toggles
// between two entry modes with LEFT/RIGHT:
//   - Passphrase (default): any text, SHA-256'd down to 16 bytes — the same
//     primitive BaseChatMesh::addChannel()/setChannel() already use to derive
//     a channel's routing hash, just applied here to a typed phrase instead of
//     a raw key. Easiest to agree on verbally, like a room password.
//   - Hex key: the exact 32-hex-char (128-bit) secret, e.g. from a channel QR
//     (see docs/qr_codes.md) — for joining a channel whose precise secret you
//     were given rather than agreeing on a new passphrase.
// Either way only a 16-byte (128-bit) secret is produced, matching the
// existing CMD_GET_CHANNEL comment ("NOTE: only 128-bit supported").

#include "../ui-core/ChannelControl.h"
#include "InfoKit.h"

class ChannelsView {
  UITask* _task;

  enum Mode : uint8_t { OFF, TYPE_PICK, ADD, ADD_HASHTAG, EDIT };
  uint8_t _mode = OFF;
  int     _idx = -1;              // channel slot being added/edited

  int  _type_sel = 0;              // TYPE_PICK cursor: 0=Public 1=Hashtag 2=Private

  char _name[32] = "";
  bool _hex_mode = false;         // false = passphrase, true = 32-hex-char raw key
  char _secret_text[33] = "";     // passphrase or hex string typed so far
  char _topic[31] = "";           // ADD_HASHTAG: topic without the leading '#'

  int  _sel = 0;                  // 0=Name 1=Secret 2=Save (ADD_HASHTAG: 0=Topic 1=Save)
  bool _kb_active = false;
  int  _kb_field = -1;            // 0=Name, 1=Secret, 2=Topic

  KeyboardWidget& kb() { return _task->keyboard(); }

  void openKb(const char* initial, int max, const char* prompt) {
    kb().begin(initial, max);
    kb().prompt = prompt;
    kb().clearPlaceholders();     // literal field — {loc}/{time} make no sense here
    _kb_active = true;
  }

  // Secret parsing, duplicate check and the save itself live in the Core
  // (ui-core/ChannelControl.h), shared with ui-lvgl.
  void report(chanctl::Result r, const char* ok_text) {
    if (r == chanctl::OK) { _task->showAlert(ok_text, 1000); _mode = OFF; }
    else if (r == chanctl::BAD_SECRET) _task->showAlert("Invalid secret", 1400);
    else _task->showAlert(chanctl::resultText(r), 1400);
  }

  // Type picker's Public row: no fields to fill in -- the well-known
  // channel's name and secret are both fixed.
  void commitPublic() {
    chanctl::Result r = chanctl::addPublic();
    if (r == chanctl::EXISTS) { _task->showAlert("Already added", 1200); _mode = OFF; return; }
    report(r, "Channel added");
    _mode = OFF;
  }

  void commit() {
    if (_mode == ADD_HASHTAG) {
      if (_topic[0] == '\0') { _task->showAlert("Topic required", 1200); return; }
      report(chanctl::addHashtag(_topic), "Channel added");
      return;
    }
    report(chanctl::savePrivate(_idx, _name, _secret_text, _hex_mode),
           _mode == ADD ? "Channel added" : "Channel updated");
  }

public:
  explicit ChannelsView(UITask* task) : _task(task) {}

  bool active() const { return _mode != OFF || _kb_active; }
  void reset() { _mode = OFF; _kb_active = false; _kb_field = -1; _type_sel = 0; _topic[0] = '\0'; }

  // idx = the slot to write on Save (first free slot for Add, existing slot
  // for Edit). Starts at the Type picker (Public/Hashtag/Private) -- see
  // handleInput()'s TYPE_PICK branch for what each option does next.
  void openAdd(int idx) {
    _mode = TYPE_PICK; _idx = idx; _type_sel = 0;
  }
  // Private option's form: today's manual Name + Secret entry, unchanged.
  void openPrivate() {
    _mode = ADD;
    _name[0] = '\0'; _secret_text[0] = '\0'; _hex_mode = false; _sel = 0;
  }
  void openEdit(int idx, const char* existing_name) {
    _mode = EDIT; _idx = idx;
    strncpy(_name, existing_name, sizeof(_name) - 1); _name[sizeof(_name) - 1] = '\0';
    _secret_text[0] = '\0'; _hex_mode = false; _sel = 0;
  }

  int render(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    if (_kb_active) return kb().render(display);

    const int top  = display.listStart();
    const int step = display.lineStep();

    if (_mode == TYPE_PICK) {
      drawScreenHeader(display, "Add channel");
      static const char* TYPE_LABELS[3] = { "Public", "Hashtag", "Private" };
      for (int i = 0; i < 3; i++) {
        int y = top + i * step;
        display.drawSelectionRow(0, y - 1, display.width(), step - 1, i == _type_sel);
        display.drawTextEllipsized(2, y, display.width() - 4, TYPE_LABELS[i]);
        display.setColor(DisplayDriver::LIGHT);
      }
      return 1000;
    }

    if (_mode == ADD_HASHTAG) {
      drawScreenHeader(display, "Add channel");
      int mq_delay = 0;
      for (int i = 0; i < 2; i++) {
        int y = top + i * step;
        bool sel = (i == _sel);
        if (i == 1) { drawButton(display, (display.width() - buttonWidth(display, "Save")) / 2, y, "Save", sel); continue; }
        display.drawSelectionRow(0, y - 1, display.width(), step - 1, sel);
        int r = info::valueRow(display, y, "Topic", _topic[0] ? _topic : "(none)", sel, 0);
        if (r > 0) mq_delay = r;
        display.setColor(DisplayDriver::LIGHT);
      }
      return (mq_delay > 0 && mq_delay < 1000) ? mq_delay : 1000;
    }

    drawScreenHeader(display, _mode == ADD ? "Add channel" : "Edit channel");
    int mq_delay = 0;
    for (int i = 0; i < 3; i++) {
      int y = top + i * step;
      bool sel = (i == _sel);
      if (i == 2) { drawButton(display, (display.width() - buttonWidth(display, "Save")) / 2, y, "Save", sel); continue; }
      display.drawSelectionRow(0, y - 1, display.width(), step - 1, sel);
      // valueRow cuts a long name/secret (scrolling it while selected) rather
      // than letting print() wrap it onto the next row's line.
      int r = i == 0 ? info::valueRow(display, y, "Name", _name[0] ? _name : "(none)", sel, 0)
                     : info::valueRow(display, y, _hex_mode ? "Secret (hex)" : "Secret (phrase)",
                                      _secret_text[0] ? _secret_text : "(none)", sel, 0);
      if (r > 0) mq_delay = r;
      display.setColor(DisplayDriver::LIGHT);
    }
    return (mq_delay > 0 && mq_delay < 1000) ? mq_delay : 1000;
  }

  bool handleInput(char c) {
    if (_kb_active) {
      auto r = kb().handleInput(c);
      if (r == KeyboardWidget::DONE) {
        if      (_kb_field == 0) { strncpy(_name, kb().buf, sizeof(_name) - 1); _name[sizeof(_name) - 1] = '\0'; }
        else if (_kb_field == 1) { strncpy(_secret_text, kb().buf, sizeof(_secret_text) - 1); _secret_text[sizeof(_secret_text) - 1] = '\0'; }
        else                     { strncpy(_topic, kb().buf, sizeof(_topic) - 1); _topic[sizeof(_topic) - 1] = '\0'; }
        _kb_active = false; _kb_field = -1;
      } else if (r == KeyboardWidget::CANCELLED) {
        _kb_active = false; _kb_field = -1;
      }
      return true;
    }

    if (_mode == TYPE_PICK) {
      if (c == KEY_CANCEL) { _mode = OFF; return true; }
      if (c == KEY_UP)   { _type_sel = (_type_sel > 0) ? _type_sel - 1 : 2; return true; }
      if (c == KEY_DOWN) { _type_sel = (_type_sel < 2) ? _type_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) {
        if      (_type_sel == 0) commitPublic();
        else if (_type_sel == 1) { _mode = ADD_HASHTAG; _sel = 0; _topic[0] = '\0'; }
        else                     openPrivate();
      }
      return true;
    }

    if (_mode == ADD_HASHTAG) {
      if (c == KEY_CANCEL) { _mode = OFF; return true; }
      if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 1; return true; }
      if (c == KEY_DOWN) { _sel = (_sel < 1) ? _sel + 1 : 0; return true; }
      if (c == KEY_ENTER) {
        if (_sel == 0) { _kb_field = 2; openKb(_topic, sizeof(_topic) - 1, "Hashtag"); }
        else            commit();
      }
      return true;
    }

    if (c == KEY_CANCEL) { _mode = OFF; return true; }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 2; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < 2) ? _sel + 1 : 0; return true; }
    if ((keyIsPrev(c) || keyIsNext(c)) && _sel == 1) {
      _hex_mode = !_hex_mode;
      _secret_text[0] = '\0';   // switching mode invalidates whatever was typed
      return true;
    }
    if (c == KEY_ENTER) {
      if      (_sel == 0) { _kb_field = 0; openKb(_name, sizeof(_name) - 1, "Name"); }
      else if (_sel == 1) { _kb_field = 1; openKb(_secret_text, _hex_mode ? 32 : (int)sizeof(_secret_text) - 1, "Secret"); }
      else                 commit();
      return true;
    }
    return true;
  }
};
