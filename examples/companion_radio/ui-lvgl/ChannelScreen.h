#pragma once
// Channel management -- ui-new's Messages > Channels options and Add / Edit
// form. Options popup (hold a channel row, or the ⚙ in an open channel):
// notifications, scope, favourite, edit, delete. Add channel: Public (the
// well-known channel), Hashtag ("#topic", secret derived from the name) or
// Private (name + passphrase or 32-hex key). Edit: new name, secret kept unless
// a new one is typed. The logic is ui-core/ChannelControl.h, shared with ui-new.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after RadioScreen.h.

namespace chanview {

enum : uint8_t { A_FAV, A_EDIT, A_DELETE, A_READ, A_PIN };
enum : uint8_t { T_PUBLIC, T_HASHTAG, T_PRIVATE };
enum : uint8_t { C_NOTIF, C_SCOPE, C_MELODY };

static int s_idx = -1;           // channel the popup / form is about (-1: adding)
static uint8_t s_type = T_HASHTAG;
static bool s_hex = false;
static lv_obj_t* s_del_lbl = nullptr;
static lv_obj_t* s_fav_btn = nullptr;
// Add / Edit form
static lv_obj_t* s_types = nullptr;
static lv_obj_t* s_name = nullptr;
static lv_obj_t* s_secret = nullptr;
static lv_obj_t* s_hex_sw = nullptr;
static lv_obj_t* s_hint = nullptr;
static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_kb = nullptr;
static char s_scope_opts[256];

static void clearForm() {
  s_types = s_name = s_secret = s_hex_sw = s_hint = s_status = s_kb = nullptr;
}

}  // namespace chanview

static void onChanAction(lv_event_t* e) { s_ui->channelAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onChanDropdown(lv_event_t* e) {
  s_ui->channelSet((uint8_t)(uintptr_t)lv_event_get_user_data(e),
                   choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onChanRowHold(lv_event_t* e) {
  intptr_t i = vlist::arg(e);
  if (i < 0) return;
  lv_indev_wait_release(lv_indev_active());   // the hold isn't also a tap that opens the channel
  s_ui->channelMenu((int)i);
}
static void onChanThreadMenu(lv_event_t* e) { (void)e; s_ui->channelMenu(-1); }
static void onChanAdd(lv_event_t* e)      { (void)e; s_ui->showChannelEdit(-1); }
static void onChanType(lv_event_t* e) {
  s_ui->channelEditType((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onChanHex(lv_event_t* e) {
  s_ui->channelEditHex(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
static void onChanField(lv_event_t* e) { s_ui->channelEditField((lv_obj_t*)lv_event_get_target(e)); }
static void onChanSave(lv_event_t* e)  { (void)e; s_ui->channelEditSave(); }
static void onChanKb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) s_ui->channelEditSave();
  else s_ui->channelEditKbHide();
}

// ── Options popup ─────────────────────────────────────────────────────────────

// idx -1: the channel open in the thread.
void UITask::channelMenu(int idx) {
  using namespace chanview;
  if (idx < 0) idx = _thread_channel;
  ChannelDetails ch;
  if (!chanctl::exists(idx) || !the_mesh.getChannel(idx, ch)) return;
  s_idx = idx;
  s_del_lbl = s_fav_btn = nullptr;
  char title[40];
  snprintf(title, sizeof(title), "%s", ch.name);   // as named: "#" marks a hashtag channel
  lv_obj_t* panel = navPopupPanel(title, false);

  lv_obj_t* g = group(panel, nullptr);
  choiceRow(g, "Alerts", nullptr, "Default\nMuted\nAlways", chanctl::notif(_prefs, idx), onChanDropdown,
            (void*)(uintptr_t)C_NOTIF);
  choiceRow(g, "Sound", nullptr, "Default\nMelody 1\nMelody 2", chanctl::melody(_prefs, idx), onChanDropdown,
            (void*)(uintptr_t)C_MELODY);
  const ScopeList& sl = the_mesh.scopeList();
  if (sl.count > 0) {   // only once regions are set up (Settings in the app)
    int o = 0;
    for (uint8_t i = 0; i <= sl.count && o < (int)sizeof(s_scope_opts) - 24; i++)
      o += snprintf(s_scope_opts + o, sizeof(s_scope_opts) - o, "%s%s", i ? "\n" : "", sl.name(i));
    choiceRow(g, "Scope", "Region it's sent in", s_scope_opts, chanctl::scope(_prefs, idx), onChanDropdown,
              (void*)(uintptr_t)C_SCOPE);
  }

  lv_obj_t* acts = buttonBar(panel);
  bool unread = _core->history.chUnread(idx) > 0;
  struct { const char* text; uint8_t act; } btns[] = {
    { UI_SYMBOL_STAR " Fav", A_FAV },
    { LV_SYMBOL_OK " Read", A_READ },
    { UI_SYMBOL_TACK, A_PIN },
    { LV_SYMBOL_EDIT " Edit", A_EDIT },
    { LV_SYMBOL_TRASH, A_DELETE },
  };
  for (auto& b : btns) {
    if (b.act == A_READ && !unread) continue;
    lv_obj_t* bt = barButton(acts, b.text, onChanAction, b.act, b.act == A_FAV && chanctl::favourite(_prefs, idx));
    if (b.act == A_DELETE) s_del_lbl = lv_obj_get_child(bt, 0);
    if (b.act == A_FAV) s_fav_btn = bt;
  }
}

void UITask::channelSet(uint8_t which, int v) {
  using namespace chanview;
  if (!chanctl::exists(s_idx) || !_prefs) return;
  if (which == C_NOTIF) chanctl::setNotif(_prefs, s_idx, (uint8_t)v);
  else if (which == C_SCOPE) the_mesh.setChannelScope(s_idx, (uint8_t)v);
  else if (which == C_MELODY) { chanctl::setMelody(_prefs, s_idx, (uint8_t)v); hearMelody(v); }
  prefsSave();
}

void UITask::channelAction(uint8_t act) {
  using namespace chanview;
  int idx = s_idx;
  if (!chanctl::exists(idx)) { navClosePopup(); return; }
  switch (act) {
    case A_FAV: {
      bool on = !chanctl::favourite(_prefs, idx);
      chanctl::setFavourite(_prefs, idx, on);
      prefsSave();
      if (s_fav_btn) barButtonOn(s_fav_btn, on);
      showToast(on ? "Added to favourites" : "Removed from favourites", 1200);
      if (_screen == SCR_CHATS) { buildChats(); channelMenu(idx); }   // list order / star
      break;
    }
    case A_READ:
      _core->history.setChUnread(idx, 0);
      navClosePopup();
      if (_screen == SCR_CHATS) buildChats();
      break;
    case A_EDIT:
      navClosePopup();
      showChannelEdit(idx);
      break;
    case A_PIN:
      pinPopup(true, (uint8_t)idx, nullptr);   // DeviceScreen.h
      break;
    case A_DELETE:
      if (!tapConfirmed(s_del_lbl, "Delete?")) break;
      chanctl::remove(idx);
      navClosePopup();
      showChats();
      break;
  }
}

// ── Add / Edit ────────────────────────────────────────────────────────────────

static lv_obj_t* chanField(lv_obj_t* parent, const char* placeholder) {
  lv_obj_t* ta = wifiField(parent, placeholder, false);
  lv_obj_remove_event_cb(ta, onWifiField);
  lv_obj_add_event_cb(ta, onChanField, LV_EVENT_CLICKED, NULL);
  return ta;
}

void UITask::showChannelEdit(int idx) {
  _screen = SCR_CHANNEL_EDIT;
  chanview::s_idx = chanctl::exists(idx) ? idx : -1;
  chanview::s_hex = false;
  buildChannelEdit();
}

void UITask::buildChannelEdit() {
  using namespace chanview;
  bool edit = s_idx >= 0;
  lv_obj_t* body = newScreen(edit ? "Edit channel" : "Add channel", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  clearForm();

  if (!edit) {
    static const char* TYPES[] = { "Public", "Hashtag", "Private", "" };
    s_types = segmented(body, TYPES, s_type, LV_PCT(100), 38, theme::SURFACE);
    lv_obj_set_style_text_font(s_types, THEME_FONT_BODY, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_types, onChanType, LV_EVENT_VALUE_CHANGED, NULL);
  }

  s_hint = noteLabel(body, "");
  s_name = chanField(body, "Name");
  lv_textarea_set_max_length(s_name, sizeof(((ChannelDetails*)0)->name) - 1);
  s_secret = chanField(body, "Passphrase");
  lv_textarea_set_max_length(s_secret, 32);

  lv_obj_t* row = settingRow(body, "Hex key", "32 characters");
  s_hex_sw = lv_switch_create(row);
  lv_obj_set_size(s_hex_sw, 46, 24);
  lv_obj_align(s_hex_sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  lv_obj_add_event_cb(s_hex_sw, onChanHex, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t* save = lv_button_create(body);
  lv_obj_set_size(save, LV_PCT(100), 38);
  lv_obj_set_style_shadow_width(save, 0, 0);
  lv_obj_set_style_radius(save, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(save, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_add_event_cb(save, onChanSave, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(save, LV_SYMBOL_OK " Save", THEME_FONT_BODY, theme::TEXT));
  stylePrimary(save);
  s_status = noteLabel(body, "", THEME_FONT_SMALL, theme::FAIL);

  if (edit) {
    ChannelDetails ch;
    char hex[33] = "";
    the_mesh.getChannel(s_idx, ch);
    chanctl::secretHex(s_idx, hex);
    lv_textarea_set_text(s_name, ch.name);
    lv_textarea_set_placeholder_text(s_secret, "New passphrase (empty: keep)");
    lv_label_set_text_fmt(s_hint, "Key: %s", hex);
  }

  s_kb = kb::create(screen(), _prefs);
  lv_obj_set_size(s_kb, LV_PCT(100), 124);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(s_kb, onChanKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(s_kb, onChanKb, LV_EVENT_CANCEL, NULL);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  channelEditType(edit ? T_PRIVATE : s_type);
}

// Show the fields the channel type needs.
void UITask::channelEditType(int type) {
  using namespace chanview;
  if (!s_name || type < T_PUBLIC || type > T_PRIVATE) return;
  bool edit = s_idx >= 0;
  if (!edit) s_type = (uint8_t)type;
  channelEditKbHide();
  auto show = [](lv_obj_t* o, bool on) {
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };
  show(s_name, type != T_PUBLIC);
  show(s_secret, type == T_PRIVATE);
  show(lv_obj_get_parent(s_hex_sw), type == T_PRIVATE);
  lv_label_set_text(s_status, "");
  if (edit) return;   // the hint shows the key
  switch (type) {
    case T_PUBLIC:
      lv_label_set_text(s_hint, "The open channel every MeshCore device starts with. Adds it back if you removed it.");
      break;
    case T_HASHTAG:
      lv_textarea_set_placeholder_text(s_name, "Topic, e.g. hiking");
      lv_label_set_text(s_hint, "A public group anyone can join by typing the same topic. The key comes from the name.");
      break;
    default:
      lv_textarea_set_placeholder_text(s_name, "Name");
      lv_label_set_text(s_hint, "Readable only by people you give the key to.");
      break;
  }
}

void UITask::channelEditHex(bool hex) {
  using namespace chanview;
  s_hex = hex;
  if (!s_secret) return;
  lv_textarea_set_text(s_secret, "");   // a passphrase isn't a key and vice versa
  lv_textarea_set_placeholder_text(s_secret, hex ? "32 hex characters" : (s_idx >= 0 ? "New passphrase (empty: keep)" : "Passphrase"));
  lv_textarea_set_accepted_chars(s_secret, hex ? "0123456789abcdefABCDEF" : NULL);
}

void UITask::channelEditField(lv_obj_t* ta) {
  using namespace chanview;
  if (!s_kb) return;
  fieldFocus(s_name, false);
  fieldFocus(s_secret, false);
  lv_keyboard_set_textarea(s_kb, ta);
  fieldFocus(ta, true);
  if (lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    if (_body) lv_obj_set_height(_body, lv_obj_get_height(_body) - lv_obj_get_height(s_kb));
  }
  layoutNow(screen());
  lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
}

void UITask::channelEditKbHide() {
  using namespace chanview;
  if (!s_kb || lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) return;
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  fieldFocus(s_name, false);
  fieldFocus(s_secret, false);
  if (_body) lv_obj_set_height(_body, lv_obj_get_height(_body) + lv_obj_get_height(s_kb));
}

void UITask::channelEditSave() {
  using namespace chanview;
  if (!s_name) return;
  const char* name = lv_textarea_get_text(s_name);
  const char* secret = lv_textarea_get_text(s_secret);
  chanctl::Result r;
  if (s_idx >= 0)
    r = secret[0] ? chanctl::savePrivate(s_idx, name, secret, s_hex) : chanctl::rename(s_idx, name);
  else if (s_type == T_PUBLIC)  r = chanctl::addPublic();
  else if (s_type == T_HASHTAG) r = chanctl::addHashtag(name);
  else                          r = chanctl::savePrivate(-1, name, secret, s_hex);
  if (r != chanctl::OK) {   // say so over the keyboard, and put the cursor where the fix goes
    const char* msg = (r == chanctl::NO_NAME && s_type == T_HASHTAG && s_idx < 0) ? "Topic required" : chanctl::resultText(r);
    lv_label_set_text(s_status, msg);
    showToast(msg, 2000);
    if (r == chanctl::NO_NAME) channelEditField(s_name);
    else if (r == chanctl::NO_SECRET || r == chanctl::BAD_SECRET) channelEditField(s_secret);
    return;
  }
  channelEditKbHide();
  showToast(s_idx >= 0 ? "Channel updated" : "Channel added", 1500);
  showChats();
}
