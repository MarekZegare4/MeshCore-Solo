#pragma once
// Device-level pieces from ui-new's Settings > System and Home:
//  - node name (Settings > System > Name), reboot / power off (Settings > System);
//  - lock screen (Settings > Display > Lock screen, NodePrefs::auto_lock):
//    once the screen turns off, waking shows a clock card and nothing reacts
//    until "slide to unlock" -- against touches in a pocket; with a screen PIN
//    (NodePrefs, ui-core/ScreenLock.h) the card always comes up, also after a reboot, and
//    asks for the PIN on a keypad instead of the slider;
//  - favourites dial (Home > Favourites, NodePrefs::favourite_contacts): six
//    slots holding a contact, room or channel; tap opens it, hold changes /
//    removes it; "Pin" in a conversation's / channel's options puts it on one.
// Slot storage is ui-core/Favourites.h, names contactctl::favName, shared
// with ui-new's dial.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

namespace devview {

static lv_obj_t* s_lock = nullptr;        // lock overlay (lv_layer_top)
static lv_obj_t* s_lock_clock = nullptr;
static lv_obj_t* s_lock_date = nullptr;
static lv_obj_t* s_lock_unread = nullptr;
static lv_obj_t* s_lock_slider = nullptr;
static lv_obj_t* s_pin_dots = nullptr;    // lock card / setup popup: one dot per digit typed
static lv_obj_t* s_pin_msg = nullptr;     // "Wrong PIN" / "Try again in 30 s" / setup step
static const uint8_t PIN_MIN = 4, PIN_MAX = 8;
static const char* const PIN_MAP[] = { "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                       LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, "" };

static int s_fav_slot = -1;               // slot the hold / pick popup is about
static const int PICK_MAX = 128;
static uint8_t (*s_pick_keys)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(PICK_MAX);
enum : int { PICK_CONTACT = 100 };        // pick codes: channel index, or PICK_CONTACT + row

// "Pin" from an options popup: what gets pinned.
static bool s_pin_channel = false;
static uint8_t s_pin_ch = 0;
static uint8_t s_pin_key[PUB_KEY_SIZE];

enum : uint8_t { F_CHANGE, F_REMOVE };

}  // namespace devview

static void onNodeName(lv_event_t* e)    { (void)e; s_ui->nodeNamePopup(); }
static void onNodeNameKb(lv_event_t* e)  { s_ui->nodeNameDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onPowerRow(lv_event_t* e)    { s_ui->powerPopup((uintptr_t)lv_event_get_user_data(e) != 0); }
static void onPowerGo(lv_event_t* e)     { s_ui->shutdown((uintptr_t)lv_event_get_user_data(e) != 0); }
static void onFavTap(lv_event_t* e) { s_ui->favTap((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onFavHold(lv_event_t* e) {
  lv_indev_wait_release(lv_indev_active());   // the hold isn't also a tap that opens it
  s_ui->favHold((int)(uintptr_t)lv_event_get_user_data(e));
}
static void onFavAction(lv_event_t* e) { s_ui->favAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onFavPick(lv_event_t* e)   { s_ui->favPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onPinSlot(lv_event_t* e)   { s_ui->pinTo((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onPinPad(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_target(e);
  const char* k = lv_buttonmatrix_get_button_text(m, lv_buttonmatrix_get_selected_button(m));
  if (!k) return;
  if (lv_event_get_user_data(e)) s_ui->pinSetupKey(k); else s_ui->pinKey(k);
}
static void onPinSetup(lv_event_t* e)  { (void)e; s_ui->pinSetupPopup(); }
static void onPinRemove(lv_event_t* e) { (void)e; s_ui->pinRemove(); }

namespace devview {
// The digit keypad (1-9, backspace, 0, OK) under a row of dots.
static lv_obj_t* pinPad(lv_obj_t* parent, bool setup, int w, int h) {
  s_pin_dots = label(parent, "", THEME_FONT_LARGE, theme::TEXT);
  lv_obj_set_style_text_letter_space(s_pin_dots, 6, 0);
  lv_obj_set_height(s_pin_dots, 22);
  s_pin_msg = label(parent, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_t* m = lv_buttonmatrix_create(parent);
  lv_buttonmatrix_set_map(m, PIN_MAP);
  lv_obj_set_size(m, w, h);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_pad_all(m, 0, 0);
  lv_obj_set_style_pad_gap(m, 6, 0);
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::SURFACE), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::SURFACE_2), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_font(m, THEME_FONT_LARGE, LV_PART_ITEMS);
  lv_obj_set_style_text_color(m, lv_color_hex(theme::TEXT), LV_PART_ITEMS);
  lv_obj_set_style_radius(m, theme::RADIUS, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  lv_obj_add_event_cb(m, onPinPad, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)(setup ? 1 : 0));
  return m;
}

static void showDots(const char* entry) {
  if (!s_pin_dots) return;
  char d[PIN_MAX * 3 + 1] = "";
  for (size_t i = 0; entry[i] && i < PIN_MAX; i++) strcat(d, "\xE2\x80\xA2");   // U+2022 bullet
  lv_label_set_text(s_pin_dots, d);
}

// Digit / backspace into `entry`; true when OK was pressed.
static bool pinEdit(char* entry, const char* key) {
  size_t n = strlen(entry);
  if (!strcmp(key, LV_SYMBOL_BACKSPACE)) { if (n) entry[n - 1] = '\0'; }
  else if (!strcmp(key, LV_SYMBOL_OK)) return true;
  else if (key[0] >= '0' && key[0] <= '9' && !key[1] && n < PIN_MAX) { entry[n] = key[0]; entry[n + 1] = '\0'; }
  showDots(entry);
  return false;
}
}  // namespace devview

// ── Node name, reboot, power off ──────────────────────────────────────────────

void UITask::nodeNamePopup() {
  if (!_prefs) return;
  TextEntry t = { "Node name", onNodeNameKb };
  t.hint = "Shown to others in adverts";
  t.text = the_mesh.getNodeName();
  t.max_bytes = sizeof(_prefs->node_name) - 1;
  navTextEntry(t);
}

void UITask::nodeNameDone(bool ok) {
  if (ok && _nav_ta && _prefs) {
    const char* t = lv_textarea_get_text(_nav_ta);
    if (!t[0]) { showToast("Name can't be empty"); return; }
    snprintf(_prefs->node_name, sizeof(_prefs->node_name), "%s", t);
    prefsSave();   // getNodeName() and the self-advert read node_name live
    showToast("Name saved - others see it with your next advert", 3000);
  }
  navClosePopup();
  if (ok && _screen == SCR_SETTINGS) buildSettings();
}

void UITask::powerPopup(bool restart) {
  confirmBody(navPopupPanel(restart ? "Reboot?" : "Power off?", false),
              restart ? "Restarts the device. Messages and settings are kept."
                      : "Turns the device off. Messages and settings are kept; no messages arrive while it is off.",
              restart ? LV_SYMBOL_REFRESH "  Reboot" : LV_SYMBOL_POWER "  Power off", onPowerGo, restart ? 1 : 0);
}

// ── Lock screen ───────────────────────────────────────────────────────────────

// From sleep() when the lock is on. Built while the screen is dark, so waking
// shows it straight away.
void UITask::lockScreen() {
  using namespace devview;
  if (s_lock) return;
  navClosePopup();
  setKeyboardVisible(false);
  s_lock = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_lock);
  lv_obj_set_size(s_lock, LV_PCT(100), lv_display_get_vertical_resolution(NULL) - theme::STATUS_H);
  lv_obj_set_pos(s_lock, 0, theme::STATUS_H);   // the status bar stays visible
  lv_obj_set_style_bg_color(s_lock, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(s_lock, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_lock, LV_OBJ_FLAG_CLICKABLE);   // swallows every touch but the slider's
  lv_obj_remove_flag(s_lock, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(s_lock, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_lock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(s_lock, 18, 0);
  lv_obj_set_style_pad_row(s_lock, 4, 0);
  if (pinSet()) {   // compact card: the time and unread on one line, the keypad below
    lv_obj_set_style_pad_top(s_lock, 4, 0);
    lv_obj_set_style_pad_row(s_lock, 2, 0);
    lv_obj_t* top = lv_obj_create(s_lock);
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, 14, 0);
    s_lock_clock = clockFace(top, THEME_FONT_LARGE, THEME_FONT_SMALL);
    s_lock_unread = label(top, "", THEME_FONT_BODY, theme::ACCENT);
    s_lock_date = nullptr;
    _pin_entry[0] = '\0';
    pinPad(s_lock, false, 228, 124);
    showDots(_pin_entry);
    if (_toast) lv_obj_move_foreground(_toast);
    refreshLock();
    return;
  }
  s_lock_clock = clockFace(s_lock, nullptr, THEME_FONT_TITLE);
  s_lock_date = label(s_lock, "", THEME_FONT_BODY, theme::TEXT_MUTED);
  s_lock_unread = label(s_lock, "", THEME_FONT_BODY, theme::ACCENT);
  lv_obj_set_style_pad_top(s_lock_unread, 8, 0);

  lv_obj_t* track = lv_obj_create(s_lock);   // grows to push the slider to the bottom
  lv_obj_remove_style_all(track);
  lv_obj_set_width(track, 1);
  lv_obj_set_flex_grow(track, 1);

  s_lock_slider = lv_slider_create(s_lock);
  lv_obj_set_size(s_lock_slider, 250, 44);
  lv_obj_set_style_margin_bottom(s_lock_slider, 44, 0);   // clear of the toast below
  lv_slider_set_range(s_lock_slider, 0, 100);
  lv_obj_set_style_anim_duration(s_lock_slider, 0, 0);   // no lag behind the finger
  lv_obj_set_style_bg_color(s_lock_slider, lv_color_hex(theme::SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_lock_slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_lock_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_lock_slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_lock_slider, lv_color_hex(theme::ACCENT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(s_lock_slider, 2, LV_PART_KNOB);
  lv_obj_set_style_shadow_width(s_lock_slider, 0, LV_PART_KNOB);
  lv_obj_remove_flag(s_lock_slider, LV_OBJ_FLAG_CLICKABLE);   // lockPoll() drives it
  lv_obj_t* hint = label(s_lock_slider, "slide to unlock  " LV_SYMBOL_RIGHT, THEME_FONT_BODY, theme::TEXT_MUTED);
  lv_obj_align(hint, LV_ALIGN_CENTER, 14, 0);

  if (_toast) lv_obj_move_foreground(_toast);   // new-message toasts still show
  refreshLock();
}

void UITask::unlockScreen() {
  using namespace devview;
  if (!s_lock) return;
  lv_indev_wait_release(lv_indev_active());   // the slide isn't also a tap underneath
  // The lock's content goes at once and its background fades away as the
  // screen underneath drifts up into place -- the same cheap cover fade as a
  // screen change (Anim.h), which deletes the overlay at the end.
  lv_obj_t* fading = s_lock;
  for (uint32_t i = 0; i < lv_obj_get_child_count(fading); i++)
    lv_obj_add_flag(lv_obj_get_child(fading, i), LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(fading, LV_OBJ_FLAG_CLICKABLE);
  if (_asleep) lv_obj_delete_async(fading);
  else {
    anim::run(fading, anim::setBgOpa, LV_OPA_COVER, LV_OPA_TRANSP, anim::UNLOCK_MS, anim::coverDone);
    if (_body) anim::run(_body, anim::setTy, 12, 0, anim::UNLOCK_MS);
  }
  s_lock = s_lock_clock = s_lock_date = s_lock_unread = s_lock_slider = s_pin_dots = s_pin_msg = nullptr;
  _pin_entry[0] = '\0';
  if (_screen == SCR_HOME) refreshHome();
  else if (_screen == SCR_CHATS) refreshChats();   // counts moved on while locked
  else if (_screen == SCR_THREAD) reopenThreadIfUnread();   // on to what came in meanwhile
}

bool UITask::locked() const { return devview::s_lock != nullptr; }

// From loop() while locked: the knob follows the finger and unlocks when let
// go at the end (a knob let go earlier springs back). Tracked from the touch
// itself, like the Home page swipe -- LVGL stops feeding a pressed widget
// once the finger has moved past its scroll threshold.
void UITask::lockPoll() {
  using namespace devview;
  static bool s_dragging = false, s_was_down = false;
  lv_indev_t* in = lv_indev_get_next(NULL);
  if (!s_lock_slider || !in) { s_dragging = s_was_down = false; return; }
  bool down = lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  lv_area_t a;
  lv_obj_get_coords(s_lock_slider, &a);
  int32_t knob = lv_area_get_height(&a);   // round knob, as tall as the track
  if (down && !s_was_down) {               // a drag starts on the knob only
    s_dragging = p.y >= a.y1 - 10 && p.y <= a.y2 + 10 && p.x <= a.x1 + knob + 16;
    if (s_dragging) lv_anim_delete(s_lock_slider, anim::setSlider);
  }
  s_was_down = down;
  if (!down) {
    if (!s_dragging) return;
    s_dragging = false;
    int32_t v = lv_slider_get_value(s_lock_slider);
    if (v >= 100) unlockScreen();
    else if (v > 0) anim::run(s_lock_slider, anim::setSlider, v, 0, anim::SPRING_MS);
    return;
  }
  if (!s_dragging) return;
  int32_t span = lv_area_get_width(&a) - knob;
  int32_t v = span > 0 ? (p.x - a.x1 - knob / 2) * 100 / span : 0;
  v = v < 0 ? 0 : v >= 92 ? 100 : v;       // snaps to the end: let go to unlock
  lv_slider_set_value(s_lock_slider, v, LV_ANIM_OFF);
}

void UITask::refreshLock() {
  using namespace devview;
  if (!s_lock) return;
  struct tm ti;
  if (localTime(_prefs, ti)) {
    clockFaceSet(s_lock_clock, &ti, _prefs);
    char date[48];
    fmtDate(date, sizeof(date), ti);
    if (s_lock_date) setText(s_lock_date, date);
  }
  int unread = unreadTotal();
  if (!s_lock_date) {   // PIN card: the count next to the clock, the keypad's line
    if (unread > 0) setTextFmt(s_lock_unread, LV_SYMBOL_ENVELOPE " %d", unread);
    else setText(s_lock_unread, "");
    if (!s_pin_msg) return;
    bool was = _pin_tries.block_until != 0;
    if (_pin_tries.blocked()) setTextFmt(s_pin_msg, "Too many tries - wait %lu s", (unsigned long)_pin_tries.secondsLeft());
    else if (was) setText(s_pin_msg, "Enter PIN");
    else if (!lv_label_get_text(s_pin_msg)[0]) setText(s_pin_msg, "Enter PIN");
    return;
  }
  if (unread > 0) setTextFmt(s_lock_unread, LV_SYMBOL_ENVELOPE "  %d new message%s", unread, unread == 1 ? "" : "s");
  else setText(s_lock_unread, "");
}

// ── Screen PIN ────────────────────────────────────────────────────────────────

// Lock card keypad: the PIN unlocks as soon as it matches (from 4 digits on;
// only its hash is kept, so not its length); OK or a full 8 digits that
// don't match count a miss, and five misses pause entry for 30 s.
void UITask::pinKey(const char* key) {
  using namespace devview;
  if (!s_lock || !pinSet()) return;
  if (_pin_tries.blocked()) return;
  bool ok = pinEdit(_pin_entry, key);
  size_t n = strlen(_pin_entry);
  if (n >= PIN_MIN && screenlock::check(*_prefs, _pin_entry)) { _pin_tries.ok(); unlockScreen(); return; }
  if (!ok && n < PIN_MAX) {
    if (n && s_pin_msg) lv_label_set_text(s_pin_msg, "Enter PIN");
    return;
  }
  if (!n) return;
  _pin_entry[0] = '\0';
  showDots(_pin_entry);
  if (_pin_tries.miss()) {
    refreshLock();
  } else if (s_pin_msg) {
    int left = _pin_tries.left();
    lv_label_set_text_fmt(s_pin_msg, "Wrong PIN - %d tr%s left", left, left == 1 ? "y" : "ies");
  }
}

// Settings > Display > Screen PIN: a new PIN typed twice; with one
// set, "Remove" in the header too.
void UITask::pinSetupPopup() {
  using namespace devview;
  _pin_entry[0] = _pin_new[0] = '\0';
  lv_obj_t* panel = navPopupPanel(pinSet() ? "Change PIN" : "Set screen PIN", true);
  lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(panel, 2, 0);
  if (pinSet()) {   // next to the close button
    lv_obj_t* l;
    headerButton(lv_obj_get_child(panel, 0), LV_SYMBOL_TRASH " Remove", onPinRemove, 44, &l);
    lv_obj_set_style_text_color(l, lv_color_hex(theme::FAIL), 0);
  }
  pinPad(panel, true, 228, 116);
  lv_label_set_text(s_pin_msg, "New PIN, 4 to 8 digits, then " LV_SYMBOL_OK);
}

void UITask::pinSetupKey(const char* key) {
  using namespace devview;
  if (!s_pin_msg || !pinEdit(_pin_entry, key)) return;
  size_t n = strlen(_pin_entry);
  if (n < PIN_MIN) { lv_label_set_text(s_pin_msg, "At least 4 digits"); return; }
  if (!_pin_new[0]) {   // first entry: ask again
    strcpy(_pin_new, _pin_entry);
    _pin_entry[0] = '\0';
    showDots(_pin_entry);
    lv_label_set_text(s_pin_msg, "Once more to confirm, then " LV_SYMBOL_OK);
    return;
  }
  if (strcmp(_pin_new, _pin_entry) != 0) {
    _pin_new[0] = _pin_entry[0] = '\0';
    showDots(_pin_entry);
    lv_label_set_text(s_pin_msg, "Didn't match - new PIN again");
    return;
  }
  if (_prefs) {
    screenlock::set(*_prefs, _pin_new, the_mesh.getRNG());
    the_mesh.savePrefs();
  }
  _pin_new[0] = _pin_entry[0] = '\0';
  navClosePopup();
  showToast("PIN set - asked each time the screen wakes", 3000);
  pinRowRefresh();
}

void UITask::pinRemove() {
  if (_prefs) {
    screenlock::clear(*_prefs);
    the_mesh.savePrefs();
  }
  navClosePopup();
  showToast("PIN removed");
  pinRowRefresh();
}

// Settings > Display rebuilt for the Screen PIN row, still at its bottom.
void UITask::pinRowRefresh() {
  if (_screen != SCR_SETTINGS_NAV) return;
  buildSchemaSettings();
  fillFlush();
  if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
}

// ── Accent colour ─────────────────────────────────────────────────────────────

static void onAccent(lv_event_t* e) { s_ui->setAccent((int)(uintptr_t)lv_event_get_user_data(e)); }

// A row of colour swatches; the current one ringed.
void UITask::accentRow(lv_obj_t* body) {   // a row of a group: the label over the swatches
  lv_obj_t* card = groupLine(body, false);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(card, 8, 0);
  lv_obj_set_style_pad_bottom(card, 10, 0);
  label(card, "Accent colour", THEME_FONT_BODY, theme::TEXT);
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, LV_PCT(100), 36);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < theme::ACCENT_COUNT; i++) {
    lv_obj_t* sw = lv_button_create(row);
    lv_obj_set_size(sw, 32, 32);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sw, lv_color_hex(theme::ACCENTS[i].col), 0);
    bool cur = i == theme::s_accent;
    lv_obj_set_style_border_width(sw, cur ? 3 : 0, 0);
    lv_obj_set_style_border_color(sw, lv_color_hex(theme::TEXT), 0);
    lv_obj_add_event_cb(sw, onAccent, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    if (cur) lv_obj_center(label(sw, LV_SYMBOL_OK, THEME_FONT_SMALL, theme::BG));
  }
}

// Recolours everything: the theme, the status bar and toast, then this page.
void UITask::setAccent(int idx) {
  if (idx == theme::s_accent) return;
  theme::setAccent(idx);
  lvport::saveAccent(theme::s_accent);
  theme::install(lv_display_get_default());
  if (_toast) lv_obj_set_style_border_color(_toast, lv_color_hex(theme::ACCENT), 0);
  _status_sig[0] = '\0';   // icons in the accent rebuilt
  refreshStatusBar();
  pinRowRefresh();   // the page, kept at its bottom where this row is
}

// ── Favourites card (Home, left of the main page) ─────────────────────────────

// The six slots, three to a row, into `grid` (a wrapping row).
void UITask::favGrid(lv_obj_t* grid) {
  int w = (lv_display_get_horizontal_resolution(NULL) - 2 * theme::PAD - 2 * theme::GAP) / 3;
  for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) {
    char name[33];
    ContactInfo c;
    bool used = contactctl::favName(_prefs, s, name, sizeof(name), &c);
    bool chan = favslots::kind(_prefs, s) == NodePrefs::FAV_KIND_CHANNEL;
    lv_obj_t* b = lv_button_create(grid);
    lv_obj_set_size(b, w, 76);
    lv_obj_set_style_pad_all(b, 4, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(used ? theme::SURFACE : theme::BG), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
    if (!used) {
      lv_obj_set_style_border_color(b, lv_color_hex(theme::SURFACE_2), 0);
      lv_obj_set_style_border_width(b, 1, 0);
    }
    lv_obj_add_event_cb(b, onFavTap, LV_EVENT_CLICKED, (void*)(uintptr_t)s);
    lv_obj_add_event_cb(b, onFavHold, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)s);
    const char* icon = !used ? LV_SYMBOL_PLUS : chan ? "#" : c.type == ADV_TYPE_ROOM ? LV_SYMBOL_HOME : UI_SYMBOL_USERS;
    lv_obj_align(label(b, icon, THEME_FONT_LARGE, used ? theme::ACCENT : theme::TEXT_MUTED), LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t* n = label(b, used ? name : "Add", THEME_FONT_SMALL, used ? theme::TEXT : theme::TEXT_MUTED);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(n, w - 10);
    lv_obj_align(n, LV_ALIGN_BOTTOM_MID, 0, -8);
    if (used) {
      int unread = chan ? _core->history.chUnread(_prefs->favourite_contacts[s][0])
                        : (c.type == ADV_TYPE_CHAT ? _core->dmUnread(c.id.pub_key) : 0);
      tileBadge(b, unread);
    }
  }
  lv_obj_t* hint = label(grid, "Tap to open. Hold to change or remove.", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_width(hint, LV_PCT(100));
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
}

// A slot changed: the card redrawn if it's up.
void UITask::favRefresh() {
  if (_screen == SCR_HOME && home::s_page == home::FAVS) setHomePage(home::FAVS);
}

void UITask::favTap(int slot) {
  char name[33];
  ContactInfo c;
  if (!contactctl::favName(_prefs, slot, name, sizeof(name), &c)) { favPickPopup(slot); return; }
  if (favslots::kind(_prefs, slot) == NodePrefs::FAV_KIND_CHANNEL) openChannel(_prefs->favourite_contacts[slot][0]);
  else if (c.type == ADV_TYPE_ROOM) openRoom(c.id.pub_key);
  else openDM(c.id.pub_key);
}

void UITask::favHold(int slot) {
  char name[33];
  if (!contactctl::favName(_prefs, slot, name, sizeof(name))) { favPickPopup(slot); return; }
  devview::s_fav_slot = slot;
  lv_obj_t* panel = navPopupPanel(name, false);
  lv_obj_t* acts = buttonBar(panel);
  barButton(acts, LV_SYMBOL_EDIT " Change", onFavAction, devview::F_CHANGE, false);
  barButton(acts, LV_SYMBOL_TRASH " Remove", onFavAction, devview::F_REMOVE, false);
}

void UITask::favAction(uint8_t act) {
  int slot = devview::s_fav_slot;
  if (act == devview::F_CHANGE) { favPickPopup(slot); return; }
  favslots::clear(_prefs, slot);
  prefsSave();
  navClosePopup();
  favRefresh();
}

// What to put in a slot: channels, then contacts and rooms (favourites first).
void UITask::favPickPopup(int slot) {
  using namespace devview;
  s_fav_slot = slot;
  char title[24];
  snprintf(title, sizeof(title), "Slot %d", slot + 1);
  lv_obj_t* panel = navPopupPanel(title, true);
  lv_obj_t* list = lv_obj_create(panel);
  styleSurface(list, theme::BG);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, theme::GAP, 0);
  sectionTitle(list, "CHANNELS");
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
    listRow(list, ch.name, NULL, onFavPick, (void*)(uintptr_t)i);
  }
  sectionTitle(list, "CONTACTS AND ROOMS");
  int rows = 0, total = the_mesh.getNumContacts();
  const int MAX_ROWS = PICK_MAX;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < total && rows < MAX_ROWS; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
      if (c.type != ADV_TYPE_CHAT && c.type != ADV_TYPE_ROOM) continue;
      if (contactctl::favourite(c) != (pass == 0)) continue;
      memcpy(s_pick_keys[rows], c.id.pub_key, PUB_KEY_SIZE);
      char t[48];
      snprintf(t, sizeof(t), "%s%s%s", contactctl::favourite(c) ? UI_SYMBOL_STAR " " : "", c.name,
               c.type == ADV_TYPE_ROOM ? "  (room)" : "");
      listRow(list, t, NULL, onFavPick, (void*)(uintptr_t)(PICK_CONTACT + rows));
      rows++;
    }
  }
}

void UITask::favPick(int code) {
  using namespace devview;
  if (code >= PICK_CONTACT) favslots::pinContact(_prefs, s_fav_slot, s_pick_keys[code - PICK_CONTACT]);
  else favslots::pinChannel(_prefs, s_fav_slot, (uint8_t)code);
  prefsSave();
  navClosePopup();
  favRefresh();
}

// "Pin" in an options popup: six slot buttons with who is in them now.
void UITask::pinPopup(bool channel, uint8_t ch_idx, const uint8_t* pub_key) {
  using namespace devview;
  s_pin_channel = channel;
  s_pin_ch = ch_idx;
  if (pub_key) memcpy(s_pin_key, pub_key, PUB_KEY_SIZE);
  lv_obj_t* panel = navPopupPanel("Pin to favourites", false);
  lv_obj_t* grid = lv_obj_create(panel);
  styleSurface(grid, theme::BG);
  lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, theme::GAP, 0);
  lv_obj_set_style_pad_row(grid, theme::GAP, 0);
  int here = channel ? favslots::findChannel(_prefs, ch_idx) : favslots::findContact(_prefs, pub_key);
  for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) {
    char name[33], t[48];
    bool used = contactctl::favName(_prefs, s, name, sizeof(name));
    snprintf(t, sizeof(t), "%d  %s", s + 1, used ? name : "empty");
    lv_obj_t* b = lv_button_create(grid);
    lv_obj_set_size(b, 90, 36);
    lv_obj_set_style_pad_hor(b, 4, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(b, onPinSlot, LV_EVENT_CLICKED, (void*)(uintptr_t)s);
    lv_obj_t* l = label(b, t, THEME_FONT_SMALL, used ? theme::TEXT : theme::TEXT_MUTED);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, 82);
    lv_obj_center(l);
    if (s == here) styleSelected(b, true);
  }
  label(panel, here >= 0 ? "Tap its slot again to unpin." : "A taken slot is replaced.", THEME_FONT_SMALL, theme::TEXT_MUTED);
}

void UITask::pinTo(int slot) {
  using namespace devview;
  int here = s_pin_channel ? favslots::findChannel(_prefs, s_pin_ch) : favslots::findContact(_prefs, s_pin_key);
  char msg[32];
  if (here == slot) {
    favslots::clear(_prefs, slot);
    snprintf(msg, sizeof(msg), "Unpinned");
  } else {
    if (s_pin_channel) favslots::pinChannel(_prefs, slot, s_pin_ch);
    else favslots::pinContact(_prefs, slot, s_pin_key);
    snprintf(msg, sizeof(msg), "Pinned to slot %d", slot + 1);
  }
  prefsSave();
  navClosePopup();
  showToast(msg, 1500);
}
