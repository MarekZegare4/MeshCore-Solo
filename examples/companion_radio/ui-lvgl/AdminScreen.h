#pragma once
// Remote admin of a repeater / room server (node detail > Admin, or the Home
// tile's list) -- ui-new's Tools > Admin. The session (login with the saved
// password, CLI round trips, timeouts, field table) is the UI Core's AdminSession; this draws it:
// password entry, tabs of fields, and a popup per field -- a switch, a
// -/+ stepper, a row of choices (SF / bandwidth / coding rate), or a text
// field with the keyboard (text, frequency, custom command); replies in a popup.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after ChannelScreen.h.

namespace adminview {

static uint8_t s_tab = 0;
static lv_obj_t* s_status = nullptr;    // "Logging in..." / "Waiting for reply..."
static lv_obj_t* s_list = nullptr;      // the tab's rows
static lv_obj_t* s_tabs = nullptr;
static lv_obj_t* s_pw = nullptr;        // password field (NEED_PASSWORD)
static lv_obj_t* s_kb = nullptr;
static lv_obj_t* s_val_lbl = nullptr;   // stepper value in the popup
static lv_obj_t* s_choice = nullptr;    // SF / BW / CR choices in the popup
static const admin::Field* s_confirm = nullptr;   // Reboot / OTA waiting for "yes"
static char s_btn_map[24][8];
static const char* s_map[28];

enum : uint8_t { V_MINUS, V_PLUS, V_SAVE, V_TOGGLE, V_CONFIRM, V_CLOSE };

static void clear() { s_status = s_list = s_tabs = s_pw = s_kb = s_val_lbl = s_choice = nullptr; }

// Choice buttons for a radio field: labels into s_btn_map, map into s_map
// (a "\n" after every `per_row`), selected index out.
static int radioChoices(const admin::Field& f, const AdminSession& S, int& per_row) {
  int n = 0, sel = 0, m = 0;
  if (f.kind == admin::K_RADIO_SF) {
    per_row = 8;
    for (int v = 5; v <= 12; v++, n++) { snprintf(s_btn_map[n], 8, "%d", v); if (v == S.radio_sf) sel = n; }
  } else if (f.kind == admin::K_RADIO_CR) {
    per_row = 4;
    for (int v = 5; v <= 8; v++, n++) { snprintf(s_btn_map[n], 8, "4/%d", v); if (v == S.radio_cr) sel = n; }
  } else {
    per_row = 5;
    sel = nearestBwIndex(S.radio_bw);
    for (; n < LORA_BW_OPT_COUNT; n++) snprintf(s_btn_map[n], 8, "%g", (double)LORA_BW_OPTS[n]);
  }
  for (int i = 0; i < n; i++) {
    if (i && i % per_row == 0) s_map[m++] = "\n";
    s_map[m++] = s_btn_map[i];
  }
  s_map[m] = "";
  return sel;
}

}  // namespace adminview

static void onAdminTab(lv_event_t* e) {
  s_ui->adminTab((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onAdminRow(lv_event_t* e)   { s_ui->adminRow((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onAdminValue(lv_event_t* e) { s_ui->adminValue((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onAdminChoice(lv_event_t* e) {
  s_ui->adminChoice((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onAdminPwKb(lv_event_t* e) { s_ui->adminLogin(lv_event_get_code(e) == LV_EVENT_READY); }
static void onAdminTextKb(lv_event_t* e) { s_ui->adminTextDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onAdminCancelWait(lv_event_t* e) { (void)e; s_ui->adminCancelWait(); }
static void onOpenAdminPick(lv_event_t* e) { (void)e; s_ui->showAdminPick(); }
static void onAdminPickRow(lv_event_t* e)  { s_ui->adminPick((int)(uintptr_t)lv_event_get_user_data(e)); }

namespace adminview {
static const int PICK_MAX = 128;
static uint8_t (*s_pick)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(PICK_MAX);
static uint16_t* s_pick_raw = psramBuf<uint16_t>(PICK_MAX);   // their raw table index, for the row
}

// Home > Admin: every repeater and room server, favourites first (ui-new's
// Tools > Admin picker); the session is the same as from node detail.
void UITask::showAdminPick() {
  _screen = SCR_ADMIN_PICK;
  buildAdminPick();
}

void UITask::buildAdminPick() {
  lv_obj_t* body = newScreen("Admin", true);
  int rows = 0;
  for (int pass = 0; pass < 2; pass++) {   // favourites first
    for (int i = 0; i < the_mesh.getNumContacts() && rows < adminview::PICK_MAX; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
      if (c.type != ADV_TYPE_REPEATER && c.type != ADV_TYPE_ROOM) continue;
      if (contactctl::favourite(c) != (pass == 0)) continue;
      memcpy(adminview::s_pick[rows], c.id.pub_key, PUB_KEY_SIZE);
      adminview::s_pick_raw[rows] = (uint16_t)(MAX_ANON_CONTACTS + i);
      rows++;
    }
  }
  if (rows) sectionTitle(body, "LOG IN TO");
  fillStart(rows, 8, &UITask::adminPickRow);   // the rest as the loop goes (fill)
  if (!rows) {
    noteLabel(body, "No repeaters or room servers yet. They show up here once their advert is heard.",
              THEME_FONT_BODY, theme::TEXT_MUTED);
  }
}

void UITask::adminPickRow(int i) {
  ContactInfo c;
  if (!the_mesh.getContactByIdx(adminview::s_pick_raw[i], c) || memcmp(c.id.pub_key, adminview::s_pick[i], PUB_KEY_SIZE) != 0) {
    listRow(_body, "?", NULL, onAdminPickRow, (void*)(uintptr_t)i);   // deleted meanwhile
    return;
  }
  char name[48];
  snprintf(name, sizeof(name), "%s%s", contactctl::favourite(c) ? UI_SYMBOL_STAR "  " : "", c.name);
  listRow(_body, name, c.type == ADV_TYPE_ROOM ? "Room server" : "Repeater", onAdminPickRow, (void*)(uintptr_t)i);
}

void UITask::adminPick(int row) {
  if (row < 0 || row >= adminview::PICK_MAX) return;
  openAdmin(adminview::s_pick[row]);
}

// Node detail > Admin.
void UITask::openAdmin(const uint8_t* pub_key) {
  ContactInfo ci;
  if (!MessageHistory::contactByPrefix(pub_key, ci)) return;
  _core->admin.start(ci);
  _admin_from_pick = _screen == SCR_ADMIN_PICK;
  adminview::s_tab = admin::TAB_SYSTEM;
  _screen = SCR_ADMIN;
  buildAdmin();
}

void UITask::adminLeave() {
  _core->admin.end();
  navClosePopup();
  if (_admin_from_pick) { showAdminPick(); return; }
  _screen = SCR_NODE;
  buildNode();
}

void UITask::buildAdmin() {
  using namespace adminview;
  AdminSession& S = _core->admin;
  lv_obj_t* body = newScreen(S.target().name, true);
  lv_obj_set_style_pad_row(body, 4, 0);
  clear();
  AdminSession::State st = S.state();

  if (st == AdminSession::NEED_PASSWORD) {
    label(body, "Admin password for this node", THEME_FONT_SMALL, theme::TEXT_MUTED);
    s_pw = wifiField(body, "Password", true);
    lv_obj_remove_event_cb(s_pw, onWifiField);
    lv_textarea_set_max_length(s_pw, 15);
    lv_obj_add_state(s_pw, LV_STATE_FOCUSED);
    s_kb = kb::create(screen(), _prefs);
    lv_obj_set_size(s_kb, LV_PCT(100), 124);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(s_kb, s_pw);
    lv_obj_add_event_cb(s_kb, onAdminPwKb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_kb, onAdminPwKb, LV_EVENT_CANCEL, NULL);
    return;
  }
  if (st == AdminSession::LOGGING_IN || st == AdminSession::IDLE) {
    s_status = label(body, st == AdminSession::LOGGING_IN ? LV_SYMBOL_REFRESH "  Logging in..." : "", THEME_FONT_BODY, theme::TEXT);
    return;
  }

  static const char* TABS[admin::TAB_COUNT + 1];
  for (int i = 0; i < admin::TAB_COUNT; i++) TABS[i] = admin::TAB_LABELS[i];
  TABS[admin::TAB_COUNT] = "";
  s_tabs = segmented(body, TABS, s_tab, LV_PCT(100), 34, theme::SURFACE);
  lv_obj_add_event_cb(s_tabs, onAdminTab, LV_EVENT_VALUE_CHANGED, NULL);

  s_status = label(body, "", THEME_FONT_SMALL, theme::ACCENT);
  lv_obj_add_flag(s_status, LV_OBJ_FLAG_CLICKABLE);   // tap: stop waiting
  lv_obj_add_event_cb(s_status, onAdminCancelWait, LV_EVENT_CLICKED, NULL);
  lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);

  s_list = scrollList(body);
  for (int i = 0; i < admin::rowCount(s_tab); i++) {
    const admin::Field& f = admin::field(s_tab, i);
    const char* hint = f.isCustom() ? "Any CLI command" : f.isAction() ? nullptr
                     : f.isSetOnly() ? "Set a new one" : "Tap to read and change";
    lv_obj_t* row = listRow(s_list, f.label, hint, onAdminRow, (void*)(uintptr_t)i);
    if (f.isAction() && admin::confirmPrompt(f))
      lv_obj_align(label(row, LV_SYMBOL_WARNING, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  }
  refreshAdminStatus();
}

void UITask::refreshAdminStatus() {
  using namespace adminview;
  if (!s_status || !s_list) return;
  AdminSession& S = _core->admin;
  bool waiting = S.state() == AdminSession::WAITING;
  if (waiting) {
    lv_label_set_text(s_status, S.fetching() ? LV_SYMBOL_REFRESH "  Reading...  (tap to stop)"
                                             : LV_SYMBOL_REFRESH "  Waiting for reply...  (tap to stop)");
    lv_obj_remove_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
  }
  if (waiting) lv_obj_add_state(s_list, LV_STATE_DISABLED); else lv_obj_remove_state(s_list, LV_STATE_DISABLED);
}

void UITask::adminTab(int tab) {
  if (tab < 0 || tab >= admin::TAB_COUNT) return;
  adminview::s_tab = (uint8_t)tab;
  buildAdmin();
}

void UITask::adminLogin(bool ok) {
  using namespace adminview;
  if (!ok) { adminLeave(); return; }
  if (!s_pw) return;
  _core->admin.login(lv_textarea_get_text(s_pw));   // a send failure comes back as LOGIN_FAILED
  buildAdmin();
}

void UITask::adminCancelWait() {
  _core->admin.cancelWait();
  refreshAdminStatus();
}

void UITask::adminRow(int row) {
  using namespace adminview;
  AdminSession& S = _core->admin;
  if (S.state() != AdminSession::READY || row < 0 || row >= admin::rowCount(s_tab)) return;
  const admin::Field& f = admin::field(s_tab, row);
  const char* prompt = admin::confirmPrompt(f);
  if (prompt) {   // takes the node out of service for a while: ask first
    s_confirm = &f;
    confirmBody(navPopupPanel(prompt, false), "It's offline until it comes back.", f.label, onAdminValue, V_CONFIRM);
    return;
  }
  S.run(f);
  refreshAdminStatus();
}

// Popup buttons: stepper, save, confirm.
void UITask::adminValue(uint8_t act) {
  using namespace adminview;
  AdminSession& S = _core->admin;
  const admin::Field* f = S.currentField();
  switch (act) {
    case V_MINUS: case V_PLUS:
      S.stepValue(act == V_PLUS ? 1 : -1);
      if (s_val_lbl) lv_label_set_text_fmt(s_val_lbl, "%d", (int)S.value);
      break;
    case V_TOGGLE:
      S.stepValue(1);
      break;
    case V_SAVE:
      navClosePopup();
      if (f) S.sendValue();
      refreshAdminStatus();
      break;
    case V_CONFIRM:
      navClosePopup();
      if (s_confirm) S.run(*s_confirm);
      s_confirm = nullptr;
      refreshAdminStatus();
      break;
    default:
      navClosePopup();
      break;
  }
}

void UITask::adminChoice(int idx) {
  AdminSession& S = _core->admin;
  const admin::Field* f = S.currentField();
  if (!f || idx < 0) return;
  if (f->kind == admin::K_RADIO_SF) S.radio_sf = (uint8_t)(5 + idx);
  else if (f->kind == admin::K_RADIO_CR) S.radio_cr = (uint8_t)(5 + idx);
  else if (f->kind == admin::K_RADIO_BW && idx < LORA_BW_OPT_COUNT) S.radio_bw = LORA_BW_OPTS[idx];
}

static lv_obj_t* adminPopupButton(lv_obj_t* parent, const char* text, uint8_t act, bool accent) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, 40);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_add_event_cb(b, onAdminValue, LV_EVENT_CLICKED, (void*)(uintptr_t)act);
  lv_obj_center(label(b, text, act == adminview::V_MINUS || act == adminview::V_PLUS ? THEME_FONT_LARGE : THEME_FONT_BODY, theme::TEXT));
  return b;
}

static lv_obj_t* adminRowBox(lv_obj_t* parent) {
  lv_obj_t* r = lv_obj_create(parent);
  styleSurface(r, theme::BG);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, theme::GAP, 0);
  return r;
}

// A typed value arrived: its editor.
void UITask::adminValuePopup() {
  using namespace adminview;
  AdminSession& S = _core->admin;
  const admin::Field* f = S.currentField();
  if (!f) return;
  if (f->kind == admin::K_RADIO_FREQ) {   // typed, like text
    char fs[16];
    snprintf(fs, sizeof(fs), "%.3f", S.radio_freq);
    adminTextPopup(fs, true);
    return;
  }
  lv_obj_t* panel = navPopupPanel(f->label, false);
  s_val_lbl = s_choice = nullptr;
  if (f->kind == admin::K_ONOFF) {
    lv_obj_t* r = adminRowBox(panel);
    label(r, "Off", THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_obj_t* sw = lv_switch_create(r);
    lv_obj_set_size(sw, 56, 28);
    if (S.value != 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onAdminValue, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)V_TOGGLE);
    label(r, "On", THEME_FONT_BODY, theme::TEXT_MUTED);
  } else if (f->kind == admin::K_NUMBER) {
    lv_obj_t* r = adminRowBox(panel);
    adminPopupButton(r, LV_SYMBOL_MINUS, V_MINUS, false);
    s_val_lbl = label(r, "", THEME_FONT_LARGE, theme::TEXT);
    lv_obj_set_width(s_val_lbl, 80);
    lv_obj_set_style_text_align(s_val_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(s_val_lbl, "%d", (int)S.value);
    adminPopupButton(r, LV_SYMBOL_PLUS, V_PLUS, false);
  } else {   // SF / bandwidth / coding rate
    int per_row = 1;
    int sel = radioChoices(*f, S, per_row);
    int rows = f->kind == admin::K_RADIO_BW ? 2 : 1;
    s_choice = segmented(panel, s_map, sel, LV_PCT(100), rows * 34 + (rows - 1) * 4, theme::SURFACE);
    lv_obj_add_event_cb(s_choice, onAdminChoice, LV_EVENT_VALUE_CHANGED, NULL);
  }
  if (f->isRadio()) {
    noteLabel(panel, "A mismatch cuts the node off.");
  }
  lv_obj_t* r = adminRowBox(panel);
  adminPopupButton(r, LV_SYMBOL_OK " Save", V_SAVE, true);
}

// Text to edit (a fetched value, a new password, a custom command, the frequency).
void UITask::adminTextPopup(const char* text, bool digits) {
  AdminSession& S = _core->admin;
  const admin::Field* f = S.currentField();
  lv_obj_t* panel = navPopupPanel(f ? f->label : "Command", false);
  lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // above the keyboard
  _nav_ta = textField(panel);
  lv_textarea_set_max_length(_nav_ta, 150);
  if (digits) lv_textarea_set_accepted_chars(_nav_ta, "0123456789.");
  if (f && f->isCustom()) lv_textarea_set_placeholder_text(_nav_ta, "e.g. get advert.interval");
  lv_textarea_set_text(_nav_ta, text);
  lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);
  _nav_kb = kb::create(_nav_overlay, _prefs);
  lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
  lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(_nav_kb, _nav_ta);
  if (digits) kb::apply(_nav_kb, kb::L_SYM);
  lv_obj_add_event_cb(_nav_kb, onAdminTextKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_nav_kb, onAdminTextKb, LV_EVENT_CANCEL, NULL);
}

void UITask::adminTextDone(bool ok) {
  AdminSession& S = _core->admin;
  const admin::Field* f = S.currentField();
  if (ok && _nav_ta && f) {
    const char* t = lv_textarea_get_text(_nav_ta);
    if (f->kind == admin::K_RADIO_FREQ) {
      float v = strtof(t, nullptr);
      if (v < f->min_val || v > f->max_val) { showToast("Frequency out of range"); return; }
      S.radio_freq = v;
      S.sendValue();
    } else {
      S.sendSet(t);
    }
  }
  navClosePopup();
  refreshAdminStatus();
}

void UITask::adminReplyPopup(const char* text) {
  bool long_text = strlen(text) > 90;   // e.g. "neighbors": full height, scrolls
  lv_obj_t* panel = navPopupPanel("Reply", long_text);
  lv_obj_t* box = panel;
  if (long_text) {
    box = lv_obj_create(panel);
    styleSurface(box, theme::BG);
    lv_obj_set_width(box, LV_PCT(100));
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_ACTIVE);
  }
  noteLabel(box, text[0] ? text : "(empty)", THEME_FONT_BODY, theme::TEXT);
  lv_obj_t* r = adminRowBox(panel);
  adminPopupButton(r, "OK", adminview::V_CLOSE, false);
}

// From loop() on SCR_ADMIN: the session's results.
void UITask::adminPoll() {
  AdminSession& S = _core->admin;
  const admin::Field* f = S.currentField();
  switch (S.take()) {
    case AdminSession::NONE: return;
    case AdminSession::LOGGED_IN:     showToast("Logged in as admin", 1500); buildAdmin(); break;
    case AdminSession::NOT_ADMIN:     showToast("Not an admin on this node"); adminLeave(); break;
    case AdminSession::LOGIN_FAILED:  showToast("Login failed - wrong password?", 3000); adminLeave(); break;
    case AdminSession::LOGIN_TIMEOUT: showToast("No answer to the login - out of range?"); adminLeave(); break;
    case AdminSession::SEND_FAILED:   showToast("Send failed"); break;
    case AdminSession::FETCH_FAILED:  showToast("Couldn't read the value; try again"); break;
    case AdminSession::TIMEOUT:       showToast("No reply (timeout)"); break;
    case AdminSession::REPLY:         adminReplyPopup(S.reply()); break;
    case AdminSession::VALUE_READY:   adminValuePopup(); break;
    case AdminSession::TEXT_READY:
      if (f && f->get_cmd && !S.text()[0]) showToast("Couldn't read the value; enter a new one");
      adminTextPopup(S.text(), false);
      break;
  }
  refreshAdminStatus();
}
