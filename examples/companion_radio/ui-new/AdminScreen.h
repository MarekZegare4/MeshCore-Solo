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
// After login (session password, self-heals like room logins) a category tab
// carousel of AdminField{label, get_cmd, set_prefix} rows unlocks:
//   set_prefix==null -> action: send get_cmd literally (e.g. "reboot")
//   get_cmd==null    -> set-only: blank keyboard, send "<set_prefix> <value>"
//   both set         -> get, pre-fill keyboard with the reply, then set
// The trailing "Custom command..." row (both null) is a free-text escape hatch
// with {}-key CLI command-name completion (CLI_COMMANDS).

#include "FullscreenMsgView.h"
#include "TabBar.h"
#include "PopupMenu.h"           // "Start OTA" confirmation
#include "RadioParamsEditor.h"   // DigitEditor + stepSF/stepBW/stepCR -- same widgets Settings/Repeater use locally
#include <helpers/ClientACL.h>   // PERM_ACL_ADMIN / PERM_ACL_ROLE_MASK

class AdminScreen : public UIScreen {
  UITask* _task;

  enum Phase : uint8_t { LOGIN, COMMAND, REPLY };
  Phase _phase = LOGIN;

  ContactInfo _target;

  // True when this visit was reached via the "Remote node..." picker
  // (UITask::pickAdminTarget() -> NearbyScreen's pick-mode); false when reached
  // directly from a node's own Hold-Enter "Admin" action while browsing Nodes
  // normally. Determines where Cancel/failure paths send the user back to --
  // see returnToOrigin(). Set fresh by startFor() on every entry.
  bool _from_picker = true;

  // Cancel/failure exit: back to the target picker if that's how we got here,
  // otherwise straight back to Nodes (we were opened directly from its
  // "Admin" context-menu action, so a bare pick-list would be a detour).
  void returnToOrigin() { if (_from_picker) _task->pickAdminTarget(); else _task->gotoNearbyScreen(); }

  // LOGIN
  char _login_pw[16] = "";
  // True while waiting for a login result with no keyboard on screen -- either a
  // silent retry with a remembered password, or right after the typed password
  // was submitted. Distinct from the keyboard being open (kb().render()/
  // handleInput() only run while this is false).
  bool _login_waiting = false;
  // Deadline for _login_waiting, mirroring _cmd_deadline_ms/_waiting in the
  // COMMAND phase below -- without it, a login reply that never arrives (e.g.
  // the remote's password changed since it was saved, and it silently drops
  // instead of nacking) leaves the screen stuck on "Logging in..." forever
  // with only a manual Cancel to escape. See poll().
  uint32_t _login_deadline_ms = 0;

  // One-slot memo: the last contact successfully admin-logged-into this visit,
  // so re-entering COMMAND for the same target right after doesn't require
  // retyping the password. Cleared in onShow().
  uint8_t _admin_ok_prefix[4] = { 0, 0, 0, 0 };
  bool    _admin_ok = false;

  // ── COMMAND: category tabs / rows ───────────────────────────────────────────
  enum AdminTab : uint8_t { ATAB_SYSTEM, ATAB_RADIO, ATAB_ROUTING, ATAB_ACTIONS, ATAB_COUNT };
  static const char* TAB_LABELS[ATAB_COUNT];

  // Most fields are still free text (get reply pre-fills the keyboard, user
  // retypes, "set <prefix> <text>" on submit) -- fine for names/passwords, but
  // painful for values that are really a number or one of a handful of options.
  // `kind` (default FK_TEXT, so every existing 3-field {label,get,set} literal
  // below still compiles unchanged) opts a field into a typed, in-place editor
  // instead: FK_ONOFF toggles on/off; FK_NUMBER steps an int within
  // [min_val,max_val] by `step`; FK_RADIO_* are four views onto the same
  // "get radio"/"set radio f,bw,sf,cr" tuple (freq via the same digit-cursor
  // DigitEditor Settings/Repeater use, bw/sf/cr via RadioParamsEditor's
  // discrete-set steppers) -- editing any one re-sends all four, the other
  // three carried over unchanged from the fetch. See activateField()/
  // beginValueEdit()/the `_value_editing` block in handleInput().
  enum FieldKind : uint8_t { FK_TEXT, FK_ONOFF, FK_NUMBER, FK_RADIO_FREQ, FK_RADIO_BW, FK_RADIO_SF, FK_RADIO_CR };
  struct AdminField {
    const char* label; const char* get_cmd; const char* set_prefix;
    FieldKind kind;
    float min_val, max_val, step;   // FK_NUMBER only
    // Explicit ctor (not default member initializers) so every existing
    // 3-field {label,get,set} literal below still compiles as a converting
    // constructor call -- this toolchain's actual C++ standard (the Adafruit
    // nRF52 core's default; only `env:native` sets -std=c++17) predates
    // C++14's aggregate-with-default-member-initializer rule, so plain
    // aggregate init here fails to convert.
    AdminField(const char* l, const char* g, const char* s, FieldKind k = FK_TEXT,
               float mn = 0, float mx = 0, float st = 1)
      : label(l), get_cmd(g), set_prefix(s), kind(k), min_val(mn), max_val(mx), step(st) {}
  };
  static const AdminField SYSTEM_FIELDS[];
  static const AdminField RADIO_FIELDS[];
  static const AdminField ROUTING_FIELDS[];
  static const AdminField ACTION_FIELDS[];
  static const int ROWS_PER_TAB[ATAB_COUNT];

  static const AdminField& fieldAt(int tab, int row) {
    switch (tab) {
      case ATAB_SYSTEM:  return SYSTEM_FIELDS[row];
      case ATAB_RADIO:   return RADIO_FIELDS[row];
      case ATAB_ROUTING: return ROUTING_FIELDS[row];
      default:           return ACTION_FIELDS[row];
    }
  }

  uint8_t _tab = 0;
  int     _row_sel = 0, _row_scroll = 0;

  bool        _kb_active = false;
  char        _cmd_text[161] = "";           // the command being built/sent
  bool        _waiting = false;              // awaiting sendAdminCommand()'s reply
  uint32_t    _cmd_deadline_ms = 0;
  bool        _fetch_for_edit = false;       // true while waiting on a "get" whose reply opens an editor
  const char* _pending_set_prefix = nullptr; // non-null => edited keyboard text gets this prefix on submit

  // ── Typed value editors (FK_ONOFF/FK_NUMBER/FK_RADIO_*) ────────────────────
  bool              _fetch_for_value = false;   // true while waiting on a "get" whose reply feeds a typed editor
  bool              _value_editing = false;     // true while the selected row's typed editor is live (Enter to send, Cancel to drop)
  const AdminField* _edit_field = nullptr;      // field currently open in _value_editing
  float             _edit_val = 0;              // FK_ONOFF (0/1) / FK_NUMBER current value
  float             _radio_freq = 0, _radio_bw = 0;   // last-fetched radio tuple -- kept for whichever of the
  uint8_t           _radio_sf = 0, _radio_cr = 0;     // 4 radio sub-fields isn't the one actually being edited
  DigitEditor       _freq_ed;                   // FK_RADIO_FREQ's digit-cursor editor (same widget as Settings/Repeater)

  // REPLY
  FullscreenMsgView _reply_view;
  char _reply_text[200] = "";

  // "Start OTA" confirmation -- pulls the remote out of the mesh into BLE DFU
  // mode for the duration of the update, far more disruptive than the other
  // one-shot actions on this tab, so unlike Reboot it doesn't fire on a bare
  // Enter. Only this one action needs a confirm today, so it's special-cased
  // in activateField() by command string rather than adding a generic
  // needs_confirm flag to every AdminField literal below.
  PopupMenu _confirm;
  const AdminField* _pending_confirm_field = nullptr;

  KeyboardWidget& kb() { return _task->keyboard(); }

  bool isAdminOk(const uint8_t* pub_key) const {
    return _admin_ok && memcmp(_admin_ok_prefix, pub_key, 4) == 0;
  }

  void startLogin() {
    _login_pw[0] = '\0';
    _login_waiting = false;
    kb().begin("", 15);       // admin password: same max length as room/repeater login
    kb().clearPlaceholders(); // {loc}/{time} are for messages, not a password
    _phase = LOGIN;
  }

  // Silent retry with a remembered password (see startFor()) -- same idea as
  // MessagesScreen's room-login flow: no keyboard shown, just a "Logging
  // in..." wait for the async result.
  void startLoginWithSaved(const char* password) {
    strncpy(_login_pw, password, sizeof(_login_pw) - 1);
    _login_pw[sizeof(_login_pw) - 1] = '\0';
    uint32_t est_timeout = 0;
    bool sent = the_mesh.sendRoomLogin(_target, _login_pw, est_timeout);
    _task->showAlert(sent ? "Logging in..." : "Login failed", sent ? 1000 : 1500);
    if (sent) { _login_waiting = true; _login_deadline_ms = millis() + est_timeout + 4000; _phase = LOGIN; }
  }

  void sendCommand() {
    uint32_t est_timeout = 0;
    if (!the_mesh.sendAdminCommand(_target, _cmd_text, est_timeout)) {
      _task->showAlert("Send failed", 1200);
      _fetch_for_edit = false;
      _pending_set_prefix = nullptr;
      return;
    }
    _waiting = true;
    // Generous margin over the base estimate, same reasoning as the DM ACK
    // deadline in MessagesScreen::sendText() -- a slow multi-hop reply isn't
    // prematurely shown as a timeout.
    _cmd_deadline_ms = millis() + est_timeout + 4000;
    _task->showAlert(_fetch_for_edit ? "Fetching..." : "Sent, waiting...", 800);
  }

  // Open the free-text keyboard, pre-filled with `initial`. Shared by the
  // Custom-command row and remote set-only/get-then-edit fields. `cli_autocomplete`
  // wires the {} picker to CLI command-name completion (Custom-command row only).
  void openValueKb(const char* initial, bool cli_autocomplete = false) {
    kb().begin(initial, (int)sizeof(_cmd_text) - 1);   // begin() resets any previous placeholder hook
    kb().clearPlaceholders();   // a CLI command/value is literal, not a message
    if (cli_autocomplete) kb().setPlaceholderRefresh(refreshCmdPlaceholders, nullptr, "Commands:");
    _kb_active = true;
  }

  // Candidate completions for the Custom-command row's {} picker: every remotely-
  // usable command from docs/cli_commands.md (Serial-Only ones and the region
  // sub-grammar's many variants excluded). Entries needing a value end in a
  // trailing space, ready to type it.
  static const char* const CLI_COMMANDS[];
  static const int CLI_COMMAND_COUNT;

  // KeyboardWidget::PlaceholderRefreshFn -- repopulates the {} list with only the
  // CLI_COMMANDS entries matching the word currently being typed (text since the
  // last space), so it narrows as you type. An empty word matches everything.
  static void refreshCmdPlaceholders(KeyboardWidget& kbw, void* /*ctx*/) {
    kbw.clearPlaceholders();
    int word_start = kbw.len;
    while (word_start > 0 && kbw.buf[word_start - 1] != ' ') word_start--;
    const char* word = kbw.buf + word_start;
    int word_len = kbw.len - word_start;
    for (int i = 0; i < CLI_COMMAND_COUNT; i++)
      if (word_len == 0 || strncmp(CLI_COMMANDS[i], word, word_len) == 0)
        kbw.addPlaceholder(CLI_COMMANDS[i]);
  }

  // Dispatch Enter on a COMMAND row per the three field shapes (see file header).
  // The trailing "Custom command..." row (both null) is handled first.
  void activateField(const AdminField& f) {
    _fetch_for_edit = false;
    _pending_set_prefix = nullptr;
    if (f.kind != FK_TEXT) {                     // typed editor: on/off, number, or a radio sub-field
      _edit_field = &f;
      _fetch_for_value = true;
      strncpy(_cmd_text, f.get_cmd, sizeof(_cmd_text) - 1);
      _cmd_text[sizeof(_cmd_text) - 1] = '\0';
      sendCommand();
      return;
    }
    if (f.get_cmd == nullptr && f.set_prefix == nullptr) {          // Custom command...
      openValueKb(_cmd_text, true);
    } else if (f.set_prefix == nullptr) {                            // Action
      if (!strcmp(f.get_cmd, "start ota")) {                         // see _confirm's comment
        _pending_confirm_field = &f;
        _confirm.begin("Start OTA update?", 2);
        _confirm.addItem("Start");
        _confirm.addItem("Cancel");
        _confirm.setSelected(1);   // default highlight = Cancel
        return;
      }
      strncpy(_cmd_text, f.get_cmd, sizeof(_cmd_text) - 1);
      _cmd_text[sizeof(_cmd_text) - 1] = '\0';
      sendCommand();
    } else if (f.get_cmd == nullptr) {                               // Set-only
      _pending_set_prefix = f.set_prefix;
      openValueKb("");
    } else {                                                         // Get-then-edit-then-set
      _pending_set_prefix = f.set_prefix;
      _fetch_for_edit = true;
      strncpy(_cmd_text, f.get_cmd, sizeof(_cmd_text) - 1);
      _cmd_text[sizeof(_cmd_text) - 1] = '\0';
      sendCommand();
    }
  }

  // A fetch-for-edit didn't get a reply (timeout or the user cancelled the
  // wait) -- fall back to a blank editor rather than dead-ending, so the field
  // can still be set blind.
  void fallBackToBlankEdit() {
    _fetch_for_edit = false;
    openValueKb("");
  }

  // Splits a "get radio" reply's value ("freq,bw,sf,cr") into its 4 parts.
  // Self-contained (no mesh::Utils dependency) since it only ever needs to
  // parse this one, fixed 4-field shape.
  static bool parseRadioReply(const char* val, float& freq, float& bw, uint8_t& sf, uint8_t& cr) {
    char tmp[48];
    strncpy(tmp, val, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char* p = tmp;
    char* parts[4];
    for (int i = 0; i < 4; i++) {
      parts[i] = p;
      if (i < 3) {
        char* c = strchr(p, ',');
        if (!c) return false;
        *c = '\0';
        p = c + 1;
      }
    }
    freq = strtof(parts[0], nullptr);
    bw   = strtof(parts[1], nullptr);
    sf   = (uint8_t)atoi(parts[2]);
    cr   = (uint8_t)atoi(parts[3]);
    return true;
  }

  // Seeds the typed editor from a (already "> "-stripped) get-reply, per
  // _edit_field->kind. Returns false on a malformed reply (caller falls back
  // to an alert instead of entering an editor with garbage in it).
  bool beginValueEdit(const char* val) {
    switch (_edit_field->kind) {
      case FK_ONOFF:
        _edit_val = (memcmp(val, "on", 2) == 0) ? 1.0f : 0.0f;
        return true;
      case FK_NUMBER:
        // Clamp in case the node's real value sits outside this row's declared
        // range (e.g. set via a different firmware/build or the Custom command) --
        // otherwise every LEFT/RIGHT step lands out of range and gets silently
        // rejected in both directions, stranding the field on an unreachable value.
        _edit_val = constrain((float)atoi(val), _edit_field->min_val, _edit_field->max_val);
        return true;
      case FK_RADIO_FREQ:
      case FK_RADIO_BW:
      case FK_RADIO_SF:
      case FK_RADIO_CR:
        if (!parseRadioReply(val, _radio_freq, _radio_bw, _radio_sf, _radio_cr)) return false;
        if (_edit_field->kind == FK_RADIO_FREQ)
          _freq_ed.begin(_radio_freq, _edit_field->min_val, _edit_field->max_val, 4, 3);
        return true;
      default:
        return false;
    }
  }

  // Right-column display text for the row currently in _value_editing (freq
  // has its own DigitEditor::render call instead -- see render()). Matches
  // RepeaterScreen's itemValue() formatting for the same quantities (plain
  // %d for SF/CR, %.1f for BW) so a value reads the same wherever it's shown.
  void formatEditValue(char* buf, size_t n) const {
    switch (_edit_field->kind) {
      case FK_ONOFF:    snprintf(buf, n, "%s", _edit_val != 0 ? "ON" : "OFF"); break;
      case FK_RADIO_BW: snprintf(buf, n, "%.1f", _radio_bw); break;
      case FK_RADIO_SF: snprintf(buf, n, "%d", (int)_radio_sf); break;
      case FK_RADIO_CR: snprintf(buf, n, "%d", (int)_radio_cr); break;
      default:          snprintf(buf, n, "%d", (int)_edit_val); break;   // FK_NUMBER
    }
  }

public:
  explicit AdminScreen(UITask* task) : _task(task) {}

  // Per-visit reset (see UIScreen::onShow). startFor() runs immediately after
  // and sets the real phase/target; these are just safe defaults.
  void onShow() override {
    _phase = LOGIN;
    _tab = 0;
    _row_sel = _row_scroll = 0;
    _kb_active = false;
    _waiting = false;
    _fetch_for_edit = false;
    _pending_set_prefix = nullptr;
    _fetch_for_value = false;
    _value_editing = false;
    _login_waiting = false;
    _admin_ok = false;
    _confirm.active = false;
    _pending_confirm_field = nullptr;
  }

  // Canonical entry for a specific target -- called by UITask::openAdminFor(),
  // itself reached from NearbyScreen's "Admin" context-menu action or its
  // Admin-target pick-mode. Skips straight past login if this contact is already
  // admin-ok this visit.
  void startFor(const ContactInfo& ci, bool from_picker) {
    _target = ci;
    _from_picker = from_picker;
    if (isAdminOk(ci.id.pub_key)) {
      _tab = ATAB_SYSTEM; _row_sel = _row_scroll = 0;
      _phase = COMMAND;
    } else {
      char saved_pw[sizeof(_login_pw)];
      if (the_mesh.getRoomPassword(ci.id.pub_key, saved_pw, sizeof(saved_pw)))
        startLoginWithSaved(saved_pw);
      else
        startLogin();
    }
  }

  // Async result of the on-device login, routed here from UITask::onRoomLoginResult()
  // only while this screen is current (see that routing for why).
  void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {
    // Being in LOGIN phase isn't enough on its own -- this only confirms *some*
    // login is in flight for *this screen*, not that this particular reply is
    // for _target. UITask::onRoomLoginResult() dispatches by whichever screen
    // is current, not by who sent the request, so e.g. a slow MessagesScreen
    // login reply arriving while the user has since opened Admin on a
    // *different*, password-less node (still sat at the blank LOGIN keyboard,
    // so _phase == LOGIN here too) would otherwise be accepted as if it were
    // this node's own login -- flipping _admin_ok/_phase to COMMAND for
    // _target without ever actually authenticating with it.
    if (_phase != LOGIN || memcmp(_target.id.pub_key, pub_key, 4) != 0) return;   // stale/unrelated result
    _login_waiting = false;
    if (success && (permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_ADMIN) {
      memcpy(_admin_ok_prefix, pub_key, 4);
      _admin_ok = true;
      the_mesh.saveRoomPassword(pub_key, _login_pw);   // remember it -- same logic as room login
      _tab = ATAB_SYSTEM;
      _row_sel = _row_scroll = 0;
      _phase = COMMAND;
      _task->showAlert("Logged in (admin)", 1000);
    } else if (success) {
      // Correct password, just insufficient permission -- leave any saved
      // password alone, retyping the same one won't change the outcome.
      _task->showAlert("Not admin on this node", 1600);
      returnToOrigin();
    } else {
      // Wrong/stale password -- forget it so the next attempt prompts fresh,
      // same self-healing behaviour as a room login.
      the_mesh.forgetRoomPassword(pub_key);
      _task->showAlert("Login failed", 1400);
      returnToOrigin();
    }
  }

  // Async CLI reply, routed here from UITask::onAdminReply().
  void onAdminReply(const uint8_t* pub_key, const char* text) {
    if (!_waiting || memcmp(_target.id.pub_key, pub_key, 4) != 0) return;
    _waiting = false;
    // Every "get ..." reply comes back as "> value" (see CommonCLI::handleGetCmd) --
    // strip that CLI-decoration prefix before parsing/pre-filling from it. The
    // final free-form REPLY view still shows `text` raw: action confirmations
    // like "OK" never have the prefix, and a user-typed Custom "get ..." is
    // meant to echo the raw wire reply verbatim, prefix included.
    const char* val = (text[0] == '>' && text[1] == ' ') ? text + 2 : text;
    if (_fetch_for_value) {
      _fetch_for_value = false;
      if (beginValueEdit(val)) {
        _value_editing = true;
      } else {
        _task->showAlert("Fetch failed", 1400);
      }
      return;
    }
    if (_fetch_for_edit) {
      _fetch_for_edit = false;
      char trimmed[161];
      strncpy(trimmed, val, sizeof(trimmed) - 1);
      trimmed[sizeof(trimmed) - 1] = '\0';
      size_t n = strlen(trimmed);
      while (n > 0 && (trimmed[n-1] == '\n' || trimmed[n-1] == '\r' || trimmed[n-1] == ' ')) trimmed[--n] = '\0';
      openValueKb(trimmed);   // _pending_set_prefix is already set from activateField()
      return;
    }
    // The "Admin password" row (SYSTEM_FIELDS, set-only) just changed the
    // remote's own admin credential -- CommonCLI::handleCommand() always
    // echoes it back as "password now: <value>" (see src/helpers/CommonCLI.cpp),
    // truncation and all, so parsing that confirms the exact value now
    // required to log back in. Without this, our saved password goes stale
    // the instant this command succeeds, guaranteeing the next login attempt
    // fails (see the LOGIN-phase timeout fix above for what that used to look
    // like: a permanent "Logging in..." hang with a stale saved password).
    static const char PW_ECHO_PREFIX[] = "password now: ";
    if (!strncmp(text, PW_ECHO_PREFIX, sizeof(PW_ECHO_PREFIX) - 1)) {
      char newpw[32];
      strncpy(newpw, text + sizeof(PW_ECHO_PREFIX) - 1, sizeof(newpw) - 1);
      newpw[sizeof(newpw) - 1] = '\0';
      size_t n = strlen(newpw);
      while (n > 0 && (newpw[n-1] == '\n' || newpw[n-1] == '\r' || newpw[n-1] == ' ')) newpw[--n] = '\0';
      the_mesh.saveRoomPassword(_target.id.pub_key, newpw);
    }
    strncpy(_reply_text, text, sizeof(_reply_text) - 1);
    _reply_text[sizeof(_reply_text) - 1] = '\0';
    _reply_view.begin();
    _phase = REPLY;
  }

  void poll() override {
    if (_phase == LOGIN && _login_waiting && (int32_t)(millis() - _login_deadline_ms) >= 0) {
      _login_waiting = false;
      // Stop tracking this request on the MyMesh side too -- otherwise a reply
      // that still arrives after we've given up gets misrouted to whatever
      // screen the user is on by then (see cancelUiPendingLogin()'s comment),
      // corrupting that screen's unrelated login state with our answer.
      the_mesh.cancelUiPendingLogin(_target.id.pub_key);
      // No response at all is ambiguous (could be a wrong/stale password, could
      // just be out of range) -- but a saved password that's gone stale (the
      // remote's password changed) is exactly this: silence, not a nack. Treat
      // it the same as onRoomLoginResult()'s explicit-failure branch: forget it
      // so the next attempt prompts fresh instead of retrying the same dead
      // password forever.
      the_mesh.forgetRoomPassword(_target.id.pub_key);
      _task->showAlert("Login failed (timeout)", 1400);
      returnToOrigin();
    }
    if (_phase == COMMAND && _waiting && (int32_t)(millis() - _cmd_deadline_ms) >= 0) {
      _waiting = false;
      if (_fetch_for_edit)       { fallBackToBlankEdit(); _task->showAlert("Fetch failed - enter value", 1400); }
      else if (_fetch_for_value) { _fetch_for_value = false; _task->showAlert("Fetch failed - try again", 1400); }
      else                       { _task->showAlert("No response (timeout)", 1600); }
    }
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_phase == LOGIN) {
      if (_login_waiting) {
        display.drawCenteredHeader("ADMIN LOGIN");
        display.setCursor(2, display.listStart());
        display.print("Logging in...");
        return 500;
      }
      return kb().render(display);
    }

    if (_phase == COMMAND) {
      if (_kb_active) return kb().render(display);
      if (_waiting) {
        char title[24];
        snprintf(title, sizeof(title), "%.23s", _target.name);
        display.drawCenteredHeader(title);
        display.setCursor(2, display.listStart());
        display.print((_fetch_for_edit || _fetch_for_value) ? "Fetching..." : "Waiting for reply...");
        return 500;
      }
      tabbar::draw(display, TAB_LABELS, ATAB_COUNT, _tab);
      int n = ROWS_PER_TAB[_tab];
      drawList(display, n, _row_sel, _row_scroll, [&](int i, int y, bool sel, int reserve) {
        drawRowSelection(display, y, sel, reserve);
        const AdminField& f = fieldAt(_tab, i);
        bool show_val = _value_editing && sel;   // the row whose typed editor is currently open
        // Leave room for the value column on the row currently showing one, so a
        // long label (e.g. "Flood advert interval (h)") can't run under/through
        // the value being edited -- only rows without an inline value get the
        // full row width.
        int label_max = show_val ? display.valCol() - 4 : display.width() - 4 - reserve;
        display.drawTextEllipsized(2, y, label_max, f.label);
        if (show_val) {
          if (f.kind == FK_RADIO_FREQ) {
            // valCol() reserves exactly the 8-char width this editor draws (4
            // int + '.' + 3 dec), so with no margin to spare, a visible
            // scrollbar's reserve column would otherwise swallow the last
            // (thousandths) digit -- shift left to stay clear of it, same as
            // the plain-value branch below.
            _freq_ed.render(display, display.valCol() - reserve, y);
          } else {
            char val[16];
            formatEditValue(val, sizeof(val));
            display.drawTextRightAlign(display.width() - reserve - 2, y, val);
          }
        }
      });
      if (_confirm.active) { _confirm.render(display); return 50; }
      return _value_editing ? 50 : 2000;
    }

    // REPLY
    return _reply_view.render(display, _target.name, _reply_text, false, false);
  }

  bool handleInput(char c) override {
    if (_phase == LOGIN) {
      if (_login_waiting) {
        if (c == KEY_CANCEL) {
          _login_waiting = false;
          the_mesh.cancelUiPendingLogin(_target.id.pub_key);   // see cancelUiPendingLogin()'s comment
          returnToOrigin();
        }
        return true;
      }
      auto r = kb().handleInput(c);
      if (r == KeyboardWidget::CANCELLED) {
        returnToOrigin();
      } else if (r == KeyboardWidget::DONE) {
        strncpy(_login_pw, kb().buf, sizeof(_login_pw) - 1);
        _login_pw[sizeof(_login_pw) - 1] = '\0';
        uint32_t est_timeout = 0;
        bool sent = the_mesh.sendRoomLogin(_target, _login_pw, est_timeout);
        _task->showAlert(sent ? "Logging in..." : "Login failed", sent ? 1000 : 1500);
        if (sent) { _login_waiting = true; _login_deadline_ms = millis() + est_timeout + 4000; } else returnToOrigin();
        // else: stay in LOGIN until onRoomLoginResult() fires above.
      }
      return true;
    }

    if (_phase == COMMAND) {
      if (_confirm.active) {
        auto res = _confirm.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          if (_confirm.selectedIndex() == 0 && _pending_confirm_field) {
            strncpy(_cmd_text, _pending_confirm_field->get_cmd, sizeof(_cmd_text) - 1);
            _cmd_text[sizeof(_cmd_text) - 1] = '\0';
            sendCommand();
          }
          _pending_confirm_field = nullptr;
        } else if (res == PopupMenu::CANCELLED) {
          _pending_confirm_field = nullptr;
        }
        return true;
      }
      if (_kb_active) {
        auto r = kb().handleInput(c);
        if (r == KeyboardWidget::DONE) {
          _kb_active = false;
          char edited[sizeof(_cmd_text)];
          strncpy(edited, kb().buf, sizeof(edited) - 1);
          edited[sizeof(edited) - 1] = '\0';
          if (_pending_set_prefix) {
            snprintf(_cmd_text, sizeof(_cmd_text), "%s %s", _pending_set_prefix, edited);
            _pending_set_prefix = nullptr;
            sendCommand();
          } else {
            strncpy(_cmd_text, edited, sizeof(_cmd_text) - 1);
            _cmd_text[sizeof(_cmd_text) - 1] = '\0';
            if (_cmd_text[0]) sendCommand();
          }
        } else if (r == KeyboardWidget::CANCELLED) {
          _kb_active = false;
          _pending_set_prefix = nullptr;
        }
        return true;
      }
      if (_value_editing) {
        bool commit = false;
        if (_edit_field->kind == FK_RADIO_FREQ) {
          auto r = _freq_ed.handleInput(c);
          if (r == DigitEditor::DONE)            { _radio_freq = _freq_ed.value; commit = true; }
          else if (r == DigitEditor::CANCELLED)  { _value_editing = false; }
        } else if (keyIsPrev(c) || keyIsNext(c)) {
          int dir = keyIsNext(c) ? 1 : -1;
          switch (_edit_field->kind) {
            case FK_ONOFF: _edit_val = (_edit_val != 0) ? 0.0f : 1.0f; break;
            case FK_NUMBER: {
              float nv = _edit_val + dir * _edit_field->step;
              if (nv >= _edit_field->min_val && nv <= _edit_field->max_val) _edit_val = nv;
              break;
            }
            case FK_RADIO_BW: RadioParamsEditor::stepBW(_radio_bw, dir); break;
            case FK_RADIO_SF: RadioParamsEditor::stepSF(_radio_sf, dir); break;
            case FK_RADIO_CR: RadioParamsEditor::stepCR(_radio_cr, dir); break;
            default: break;
          }
        } else if (c == KEY_ENTER) {
          commit = true;
        } else if (c == KEY_CANCEL) {
          _value_editing = false;
        }
        if (commit) {
          _value_editing = false;
          switch (_edit_field->kind) {
            case FK_RADIO_FREQ: case FK_RADIO_BW: case FK_RADIO_SF: case FK_RADIO_CR:
              snprintf(_cmd_text, sizeof(_cmd_text), "%s %.3f,%.3f,%d,%d",
                       _edit_field->set_prefix, _radio_freq, _radio_bw, (int)_radio_sf, (int)_radio_cr);
              break;
            case FK_ONOFF:
              snprintf(_cmd_text, sizeof(_cmd_text), "%s %s", _edit_field->set_prefix, _edit_val != 0 ? "on" : "off");
              break;
            default:   // FK_NUMBER
              snprintf(_cmd_text, sizeof(_cmd_text), "%s %d", _edit_field->set_prefix, (int)_edit_val);
              break;
          }
          sendCommand();
        }
        return true;
      }
      if (_waiting) {
        if (c == KEY_CANCEL) {
          _waiting = false;
          if (_fetch_for_edit)       fallBackToBlankEdit();
          else if (_fetch_for_value) _fetch_for_value = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) { returnToOrigin(); return true; }
      if (keyIsPrev(c)) { _tab = (_tab + ATAB_COUNT - 1) % ATAB_COUNT; _row_sel = _row_scroll = 0; return true; }
      if (keyIsNext(c)) { _tab = (_tab + 1) % ATAB_COUNT;             _row_sel = _row_scroll = 0; return true; }
      int n = ROWS_PER_TAB[_tab];
      if (c == KEY_UP)   { _row_sel = (_row_sel > 0) ? _row_sel - 1 : n - 1; return true; }
      if (c == KEY_DOWN) { _row_sel = (_row_sel < n - 1) ? _row_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) { activateField(fieldAt(_tab, _row_sel)); return true; }
      return true;
    }

    // REPLY
    auto res = _reply_view.handleInput(c);
    if (res != FullscreenMsgView::NONE) _phase = COMMAND;   // CLOSE / PREV / NEXT / REPLY all just return here
    return true;
  }
};

const char* AdminScreen::TAB_LABELS[AdminScreen::ATAB_COUNT] = { "System", "Radio", "Routing", "Actions" };

const AdminScreen::AdminField AdminScreen::SYSTEM_FIELDS[] = {
  { "Name",           "get name",       "set name" },
  { "Owner info",     "get owner.info", "set owner.info" },
  { "Admin password", nullptr,          "password" },
};
// The 4 radio rows all share get_cmd/set_prefix ("get radio" / "set radio") --
// each fetches and re-sends the whole f,bw,sf,cr tuple, only its own value
// actually editable (see beginValueEdit()/the commit switch in handleInput()).
const AdminScreen::AdminField AdminScreen::RADIO_FIELDS[] = {
  { "Frequency (MHz)",  "get radio", "set radio", AdminScreen::FK_RADIO_FREQ, 150.0f, 2500.0f },
  { "Bandwidth (kHz)",  "get radio", "set radio", AdminScreen::FK_RADIO_BW },
  { "Spreading factor", "get radio", "set radio", AdminScreen::FK_RADIO_SF },
  { "Coding rate",      "get radio", "set radio", AdminScreen::FK_RADIO_CR },
  { "TX power (dBm)",   "get tx",    "set tx",    AdminScreen::FK_NUMBER, -9, 30, 1 },
};
const AdminScreen::AdminField AdminScreen::ROUTING_FIELDS[] = {
  { "Repeat",                    "get repeat",                "set repeat",                AdminScreen::FK_ONOFF },
  { "Advert interval (min)",     "get advert.interval",       "set advert.interval",       AdminScreen::FK_NUMBER, 0, 240, 2 },
  { "Flood advert interval (h)", "get flood.advert.interval", "set flood.advert.interval", AdminScreen::FK_NUMBER, 0, 168, 1 },
  { "Max hops",                  "get flood.max",             "set flood.max",             AdminScreen::FK_NUMBER, 0, 64, 1 },
};
const AdminScreen::AdminField AdminScreen::ACTION_FIELDS[] = {
  { "Send advert",          "advert",         nullptr },
  { "Send zero-hop advert", "advert.zerohop", nullptr },
  { "Sync clock",           "clock sync",     nullptr },
  { "Reboot",               "reboot",         nullptr },  // disruptive + no confirm: keep off the default row
  { "Start OTA",            "start ota",      nullptr },  // confirmed first -- see _confirm's comment
  { "Custom command...",    nullptr,          nullptr },
};

const int AdminScreen::ROWS_PER_TAB[AdminScreen::ATAB_COUNT] = {
  sizeof(AdminScreen::SYSTEM_FIELDS)  / sizeof(AdminScreen::AdminField),
  sizeof(AdminScreen::RADIO_FIELDS)   / sizeof(AdminScreen::AdminField),
  sizeof(AdminScreen::ROUTING_FIELDS) / sizeof(AdminScreen::AdminField),
  sizeof(AdminScreen::ACTION_FIELDS)  / sizeof(AdminScreen::AdminField),
};

const char* const AdminScreen::CLI_COMMANDS[] = {
  "reboot", "poweroff", "shutdown", "clkreboot", "clock", "clock sync",
  "advert", "advert.zerohop", "start ota", "erase",
  "neighbors", "neighbor.remove ", "discover.neighbors",
  "ver", "board",
  "get radio", "set radio ", "get tx", "set tx ", "tempradio ",
  "get freq", "set freq ", "get radio.rxgain", "set radio.rxgain ",
  "get name", "set name ", "get lat", "set lat ", "get lon", "set lon ",
  "get owner.info", "set owner.info ", "get adc.multiplier", "set adc.multiplier ",
  "get public.key", "get role",
  "powersaving", "powersaving on", "powersaving off",
  "get repeat", "set repeat ",
  "get path.hash.mode", "set path.hash.mode ",
  "get loop.detect", "set loop.detect ",
  "get txdelay", "set txdelay ", "get direct.txdelay", "set direct.txdelay ",
  "get rxdelay", "set rxdelay ", "get dutycycle", "set dutycycle ",
  "get af", "set af ", "get int.thresh", "set int.thresh ",
  "get agc.reset.interval", "set agc.reset.interval ",
  "get multi.acks", "set multi.acks ",
  "get flood.advert.interval", "set flood.advert.interval ",
  "get advert.interval", "set advert.interval ",
  "get flood.max", "set flood.max ",
  "get flood.max.unscoped", "set flood.max.unscoped ",
  "get flood.max.advert", "set flood.max.advert ",
  "setperm ", "get acl", "get allow.read.only", "set allow.read.only ",
  "region", "region load", "region save", "region list ",
  "gps", "gps sync", "gps setloc", "gps advert ",
  "sensor list", "sensor get ", "sensor set ",
  "get bridge.type", "get bridge.enabled", "set bridge.enabled ",
  "get bridge.delay", "set bridge.delay ", "get bridge.source", "set bridge.source ",
  "get bridge.baud", "set bridge.baud ", "get bridge.channel", "set bridge.channel ",
  "get bridge.secret", "set bridge.secret ",
  "get pwrmgt.support", "get pwrmgt.source", "get pwrmgt.bootreason", "get pwrmgt.bootmv",
  "password ", "get guest.password", "set guest.password ",
};
const int AdminScreen::CLI_COMMAND_COUNT = sizeof(AdminScreen::CLI_COMMANDS) / sizeof(AdminScreen::CLI_COMMANDS[0]);
