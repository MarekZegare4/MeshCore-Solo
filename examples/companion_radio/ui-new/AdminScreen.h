#pragma once
// Tools > Admin: administer a remote repeater/room you have admin rights on
// (over the mesh -- the on-device equivalent of the app's "repeater admin",
// see docs/cli_commands.md). This device's own settings live in Settings /
// Tools, not here -- Admin is purely for remote nodes.
//
// Entry is a target-then-act flow, reached two ways: Tools > Admin opens
// Tools > Nodes in a pick-mode (NearbyScreen::startPickAdminTarget(), the same
// borrow-another-screen's-list idiom the channel/bot pickers use) and picking
// a node from it enters Admin; or a repeater/room's own Hold-Enter "Admin"
// action in Nodes enters Admin directly while browsing normally. Both call the
// single canonical UITask::openAdminFor(ContactInfo, from_picker) -> startFor(),
// which remembers which door was used: Cancel/login-failure from a command
// screen returns to the picker if that's how the user arrived, or straight
// back to Nodes if they came in directly (see returnToOrigin()); backing out
// of the picker itself returns to Tools.
//
// After login a category tab carousel of admin::Field rows unlocks. The login,
// the CLI round trips, timeouts and the field table are the UI Core's
// AdminSession (ui-core/AdminSession.h, shared with ui-lvgl); this screen
// draws them and edits values. The trailing "Custom command..." row is a
// free-text escape hatch with {}-key CLI command-name completion.

#include "FullscreenMsgView.h"
#include "TabBar.h"
#include "PopupMenu.h"           // "Start OTA" / "Reboot" confirmation
#include "RadioParamsEditor.h"   // DigitEditor + stepSF/stepBW/stepCR -- same widgets Settings/Repeater use locally

class AdminScreen : public UIScreen {
  UITask* _task;

  // True when this visit came through the "Remote node..." picker; false when
  // opened from a node's own "Admin" action in Nodes. Decides where Cancel /
  // failure goes back to (returnToOrigin()).
  bool _from_picker = true;

  AdminSession& S() { return _task->core().admin; }
  const AdminSession& S() const { return _task->core().admin; }

  void returnToOrigin() {
    S().end();
    if (_from_picker) _task->pickAdminTarget(); else _task->gotoNearbyScreen();
  }

  uint8_t _tab = 0;
  int     _row_sel = 0, _row_scroll = 0;

  bool _login_kb = false;       // password keyboard up (session NEED_PASSWORD)
  bool _kb_active = false;      // value / custom command keyboard up
  bool _value_editing = false;  // typed editor live on the selected row
  bool _showing_reply = false;
  DigitEditor _freq_ed;         // K_RADIO_FREQ's digit-cursor editor

  FullscreenMsgView _reply_view;

  // Reboot / Start OTA confirmation (admin::confirmPrompt()).
  PopupMenu _confirm;
  const admin::Field* _pending_confirm_field = nullptr;

  KeyboardWidget& kb() { return _task->keyboard(); }

  void openLoginKb() {
    kb().begin("", 15);       // admin password: same max length as room/repeater login
    kb().prompt = "Password";
    kb().clearPlaceholders(); // {loc}/{time} are for messages, not a password
    _login_kb = true;
  }

  // Free-text keyboard pre-filled with `initial`; `cli_autocomplete` wires the
  // {} picker to CLI command-name completion (Custom-command row only).
  void openValueKb(const char* initial, bool cli_autocomplete) {
    kb().begin(initial, 160);
    kb().prompt = "Command";
    kb().clearPlaceholders();   // a CLI command/value is literal, not a message
    if (cli_autocomplete) kb().setPlaceholderRefresh(refreshCmdPlaceholders, nullptr, "Commands:");
    _kb_active = true;
  }

  // Repopulates the {} list with the admin::CLI_COMMANDS matching the word being
  // typed (text since the last space); an empty word matches everything.
  static void refreshCmdPlaceholders(KeyboardWidget& kbw, void* /*ctx*/) {
    kbw.clearPlaceholders();
    int word_start = kbw.len;
    while (word_start > 0 && kbw.buf[word_start - 1] != ' ') word_start--;
    const char* word = kbw.buf + word_start;
    int word_len = kbw.len - word_start;
    for (int i = 0; i < admin::CLI_COMMAND_COUNT; i++)
      if (word_len == 0 || strncmp(admin::CLI_COMMANDS[i], word, word_len) == 0)
        kbw.addPlaceholder(admin::CLI_COMMANDS[i]);
  }

  void activateField(const admin::Field& f) {
    const char* prompt = admin::confirmPrompt(f);
    if (prompt) {
      _pending_confirm_field = &f;
      _confirm.beginConfirm(prompt, f.label);
      return;
    }
    if (S().run(f) && S().state() == AdminSession::WAITING)
      _task->showAlert(S().fetching() ? "Fetching..." : "Sent, waiting...", 800);
  }

  // Right-column text for the row in _value_editing (freq draws its own editor).
  void formatEditValue(const admin::Field& f, char* buf, size_t n) const {
    switch (f.kind) {
      case admin::K_ONOFF:    snprintf(buf, n, "%s", S().value != 0 ? "ON" : "OFF"); break;
      case admin::K_RADIO_BW: snprintf(buf, n, "%.1f", S().radio_bw); break;
      case admin::K_RADIO_SF: snprintf(buf, n, "%d", (int)S().radio_sf); break;
      case admin::K_RADIO_CR: snprintf(buf, n, "%d", (int)S().radio_cr); break;
      default:                snprintf(buf, n, "%d", (int)S().value); break;   // K_NUMBER
    }
  }

  void sent(bool ok) { if (ok) _task->showAlert("Sent, waiting...", 800); }

public:
  explicit AdminScreen(UITask* task) : _task(task) {}

  // Per-visit reset (see UIScreen::onShow). startFor() runs right after.
  void onShow() override {
    _tab = 0;
    _row_sel = _row_scroll = 0;
    _login_kb = _kb_active = _value_editing = _showing_reply = false;
    _confirm.active = false;
    _pending_confirm_field = nullptr;
  }

  // Canonical entry for a target (UITask::openAdminFor()). The session skips
  // the login for the node last logged into, and tries a saved password first.
  void startFor(const ContactInfo& ci, bool from_picker) {
    _from_picker = from_picker;
    S().start(ci);
    if (S().state() == AdminSession::NEED_PASSWORD) openLoginKb();
    else if (S().state() == AdminSession::LOGGING_IN) _task->showAlert("Logging in...", 1000);
  }

  void poll() override {
    const admin::Field* f = S().currentField();
    switch (S().take()) {
      case AdminSession::LOGGED_IN:
        _tab = admin::TAB_SYSTEM;
        _row_sel = _row_scroll = 0;
        _task->showAlert("Logged in (admin)", 1000);
        break;
      case AdminSession::NOT_ADMIN:     _task->showAlert("Not admin on this node", 1600); returnToOrigin(); break;
      case AdminSession::LOGIN_FAILED:  _task->showAlert("Login failed", 1400); returnToOrigin(); break;
      case AdminSession::LOGIN_TIMEOUT: _task->showAlert("Login failed (timeout)", 1400); returnToOrigin(); break;
      case AdminSession::SEND_FAILED:   _task->showAlert("Send failed", 1200); break;
      case AdminSession::FETCH_FAILED:  _task->showAlert("Fetch failed - try again", 1400); break;
      case AdminSession::TIMEOUT:       _task->showAlert("No response (timeout)", 1600); break;
      case AdminSession::REPLY:
        _reply_view.begin();
        _showing_reply = true;
        break;
      case AdminSession::VALUE_READY:
        if (f && f->kind == admin::K_RADIO_FREQ) _freq_ed.begin(S().radio_freq, f->min_val, f->max_val, 4, 3);
        _value_editing = true;
        break;
      case AdminSession::TEXT_READY:
        if (f && f->get_cmd && !S().text()[0]) _task->showAlert("Fetch failed - enter value", 1400);
        openValueKb(S().text(), f && f->isCustom());
        break;
      default: break;
    }
  }

  // A waiting line centred under the header, with the loading dots below it.
  int waiting(DisplayDriver& display, const char* what) {
    int mid = (display.listStart() + display.height()) / 2;
    display.drawTextCentered(display.width() / 2, mid - display.lineStep(), what);
    return drawLoadingDots(display, display.width() / 2, mid + display.lineStep());
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_login_kb) return kb().render(display);
    AdminSession::State st = S().state();
    if (st == AdminSession::LOGGING_IN || st == AdminSession::IDLE || st == AdminSession::NEED_PASSWORD) {
      display.drawCenteredHeader("ADMIN LOGIN");
      if (st != AdminSession::LOGGING_IN) return 500;
      return waiting(display, "Logging in");
    }
    if (_showing_reply) return _reply_view.render(display, S().target().name, S().reply(), false, false);
    if (_kb_active) return kb().render(display);
    if (st == AdminSession::WAITING) {
      char title[24];
      snprintf(title, sizeof(title), "%.23s", S().target().name);
      display.drawCenteredHeader(title);
      return waiting(display, S().fetching() ? "Fetching" : "Waiting for reply");
    }

    tabbar::draw(display, admin::TAB_LABELS, admin::TAB_COUNT, _tab);
    int n = admin::rowCount(_tab);
    int mq_delay = 0;
    drawList(display, n, _row_sel, _row_scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(display, y, sel, reserve);
      const admin::Field& f = admin::field(_tab, i);
      bool show_val = _value_editing && sel;   // the row whose typed editor is open
      // Room for the value column only on the row showing one, so a long label
      // can't run under the value being edited.
      int label_max = show_val ? display.valCol() - 4 : display.width() - 4 - reserve;
      int r = display.drawTextEllipsized(2, y, label_max, f.label, sel);
      if (sel && r > 0) mq_delay = r;
      if (show_val) {
        if (f.kind == admin::K_RADIO_FREQ) {
          // valCol() reserves exactly this editor's 8 chars; keep clear of a
          // visible scrollbar's reserve column (same as the plain value below).
          _freq_ed.render(display, display.valCol() - reserve, y);
        } else {
          char val[16];
          formatEditValue(f, val, sizeof(val));
          display.drawTextRightAlign(display.width() - reserve - 2, y, val);
        }
      }
    });
    if (_confirm.active) { _confirm.render(display); return 50; }
    if (_value_editing) return 50;
    return (mq_delay > 0 && mq_delay < 2000) ? mq_delay : 2000;
  }

  bool handleInput(char c) override {
    if (_login_kb) {
      auto r = kb().handleInput(c);
      if (r == KeyboardWidget::CANCELLED) {
        _login_kb = false;
        returnToOrigin();
      } else if (r == KeyboardWidget::DONE) {
        _login_kb = false;
        if (S().login(kb().buf)) _task->showAlert("Logging in...", 1000);
        // else: LOGIN_FAILED arrives through poll()
      }
      return true;
    }
    AdminSession::State st = S().state();
    if (st == AdminSession::LOGGING_IN) {
      if (c == KEY_CANCEL) returnToOrigin();
      return true;
    }
    if (st == AdminSession::IDLE || st == AdminSession::NEED_PASSWORD) return true;   // poll() leaves shortly

    if (_showing_reply) {
      if (_reply_view.handleInput(c) != FullscreenMsgView::NONE) _showing_reply = false;   // any close returns here
      return true;
    }
    if (_confirm.active) {
      auto res = _confirm.handleInput(c);
      if (res == PopupMenu::SELECTED && _confirm.selectedIndex() == 0 && _pending_confirm_field)
        sent(S().run(*_pending_confirm_field));
      if (res == PopupMenu::SELECTED || res == PopupMenu::CANCELLED) _pending_confirm_field = nullptr;
      return true;
    }
    if (_kb_active) {
      auto r = kb().handleInput(c);
      if (r == KeyboardWidget::DONE) {
        _kb_active = false;
        sent(S().sendSet(kb().buf));
      } else if (r == KeyboardWidget::CANCELLED) {
        _kb_active = false;
      }
      return true;
    }
    if (_value_editing) {
      const admin::Field* f = S().currentField();
      if (!f) { _value_editing = false; return true; }
      bool commit = false;
      if (f->kind == admin::K_RADIO_FREQ) {
        auto r = _freq_ed.handleInput(c);
        if (r == DigitEditor::DONE)           { S().radio_freq = _freq_ed.value; commit = true; }
        else if (r == DigitEditor::CANCELLED) { _value_editing = false; }
      } else if (keyIsPrev(c) || keyIsNext(c)) {
        int dir = keyIsNext(c) ? 1 : -1;
        switch (f->kind) {
          case admin::K_RADIO_BW: RadioParamsEditor::stepBW(S().radio_bw, dir); break;
          case admin::K_RADIO_SF: RadioParamsEditor::stepSF(S().radio_sf, dir); break;
          case admin::K_RADIO_CR: RadioParamsEditor::stepCR(S().radio_cr, dir); break;
          default:                S().stepValue(dir); break;
        }
      } else if (c == KEY_ENTER) {
        commit = true;
      } else if (c == KEY_CANCEL) {
        _value_editing = false;
      }
      if (commit) {
        _value_editing = false;
        sent(S().sendValue());
      }
      return true;
    }
    if (st == AdminSession::WAITING) {
      if (c == KEY_CANCEL) S().cancelWait();   // a text fetch still opens a blank editor (poll())
      return true;
    }
    if (c == KEY_CANCEL) { returnToOrigin(); return true; }
    if (keyIsPrev(c)) { _tab = (_tab + admin::TAB_COUNT - 1) % admin::TAB_COUNT; _row_sel = _row_scroll = 0; return true; }
    if (keyIsNext(c)) { _tab = (_tab + 1) % admin::TAB_COUNT;                    _row_sel = _row_scroll = 0; return true; }
    int n = admin::rowCount(_tab);
    if (c == KEY_UP)   { _row_sel = (_row_sel > 0) ? _row_sel - 1 : n - 1; return true; }
    if (c == KEY_DOWN) { _row_sel = (_row_sel < n - 1) ? _row_sel + 1 : 0; return true; }
    if (c == KEY_ENTER) { activateField(admin::field(_tab, _row_sel)); return true; }
    return true;
  }
};
