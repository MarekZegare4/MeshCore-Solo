#pragma once
// WiFi settings (Settings > WiFi, or from the map's download popup): the saved
// networks -- up to lvport::WIFI_SAVED_MAX; map downloads, live tiles and
// updates join the strongest one in range -- and a form to add one or change
// a password. Scan lists nearby networks to pick from; the password field uses
// the same keyboard as compose. Saved through lvport::saveWifi() (NVS on the
// board). WiFi itself stays off except while a download or scan runs;
// Settings > Connections' WiFi switch forbids even that.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after MapScreen.h.

static char s_wifi_names[lvport::WIFI_SCAN_MAX][33];

static void onWifiScan(lv_event_t* e) { (void)e; s_ui->wifiScan(); }
static void onWifiSave(lv_event_t* e) { (void)e; s_ui->wifiSave(); }
static void onWifiPick(lv_event_t* e) { s_ui->wifiPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onWifiSaved(lv_event_t* e) { s_ui->wifiOpenSaved((int)(uintptr_t)lv_event_get_user_data(e)); }
// Forget: the first tap turns the button into a red "Forget?", the second
// forgets (another row's button, or a rebuild, disarms it).
static lv_obj_t* s_wifi_forget_btn = nullptr;
static void wifiForgetLook(lv_obj_t* b, bool armed) {
  lv_obj_set_style_bg_color(b, lv_color_hex(armed ? theme::FAIL : theme::SURFACE_2), 0);
  lv_label_set_text(lv_obj_get_child(b, 0), armed ? "Forget?" : LV_SYMBOL_TRASH);
}
static void onWifiForget(lv_event_t* e) {
  lv_obj_t* b = (lv_obj_t*)lv_event_get_current_target(e);
  if (s_wifi_forget_btn != b) {
    if (s_wifi_forget_btn) wifiForgetLook(s_wifi_forget_btn, false);
    s_wifi_forget_btn = b;
    wifiForgetLook(b, true);
    return;
  }
  s_wifi_forget_btn = nullptr;
  s_ui->wifiForget((int)(uintptr_t)lv_event_get_user_data(e));
}
static void wifiHint(char* sub, size_t n) {
  int saved = lvport::wifiSavedCount();
  if (!lvport::wifiAllowed()) snprintf(sub, n, "Off");
  else if (saved == 1) snprintf(sub, n, "%s", lvport::wifiSaved(0)->ssid);
  else if (saved > 1) snprintf(sub, n, "%d saved networks", saved);
  else snprintf(sub, n, "Tap to pick a network");
}
static void onWifiAllowed(lv_event_t* e) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  s_ui->wifiSetAllowed(lv_obj_has_state(sw, LV_STATE_CHECKED));
  char sub[48];
  wifiHint(sub, sizeof(sub));
  rowHintSet(lv_obj_get_parent(sw), sub);
}
static void onWifiField(lv_event_t* e) { s_ui->wifiEdit((lv_obj_t*)lv_event_get_target(e)); }
static void onWifiKb(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) s_ui->wifiSave();
  else s_ui->wifiKeyboardHide();
}

static lv_obj_t* wifiField(lv_obj_t* parent, const char* placeholder, bool password) {
  lv_obj_t* ta = textField(parent, placeholder);
  lv_textarea_set_password_mode(ta, password);
  lv_obj_add_event_cb(ta, onWifiField, LV_EVENT_CLICKED, NULL);
  return ta;
}

void UITask::showWifi() {
  _screen = SCR_WIFI;
  buildWifi();
}

void UITask::buildWifi() {
  lv_obj_t* body = newScreen("WiFi", true);
  s_wifi_forget_btn = nullptr;
  lv_obj_set_style_pad_row(body, 4, 0);
  label(body, lvport::wifiAllowed() ? "For map downloads, live map tiles and updates." : "WiFi is off (Settings).",
        THEME_FONT_SMALL, theme::TEXT_MUTED);

  // Saved networks: a tap loads one into the form below; Forget takes two taps.
  int n = lvport::wifiSavedCount();
  char joined[33];
  lvport::netSsid(joined, sizeof(joined));
  lv_obj_t* card = group(body, "Saved networks");
  for (int i = 0; i < n; i++) {
    const lvport::WifiNet* w = lvport::wifiSaved(i);
    lv_obj_t* row = groupLine(card, true);
    groupText(row, w->ssid, !strcmp(w->ssid, joined) ? "Connected" : w->pass[0] ? nullptr : "Open network");
    lv_obj_add_event_cb(row, onWifiSaved, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_t* fb = lv_button_create(row);
    lv_obj_set_size(fb, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_pad_hor(fb, 10, 0);
    lv_obj_set_style_shadow_width(fb, 0, 0);
    lv_obj_set_style_radius(fb, theme::RADIUS, 0);
    lv_obj_add_event_cb(fb, onWifiForget, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_center(label(fb, "", THEME_FONT_SMALL, theme::TEXT));
    wifiForgetLook(fb, false);
  }
  if (!n) groupText(groupLine(card, false), "None yet", "Add one below.", theme::TEXT_MUTED);
  if (n > 1) groupNote(body, "The strongest one in range is used.");

  sectionTitle(body, n >= lvport::WIFI_SAVED_MAX ? "Add a network (replaces the oldest)" : "Add a network");
  lv_obj_t* row = lv_obj_create(body);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, theme::GAP, 0);
  _wifi_ssid = wifiField(row, "Network name", false);
  lv_obj_set_width(_wifi_ssid, 0);
  lv_obj_set_flex_grow(_wifi_ssid, 1);
  lv_obj_t* scan = lv_button_create(row);
  lv_obj_set_size(scan, 64, 34);
  lv_obj_set_style_shadow_width(scan, 0, 0);
  lv_obj_set_style_radius(scan, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(scan, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(scan, onWifiScan, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(scan, "Scan", THEME_FONT_SMALL, theme::TEXT));

  _wifi_list = lv_obj_create(body);   // scan results, shown after a scan
  styleSurface(_wifi_list, theme::BG);
  lv_obj_remove_flag(_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(_wifi_list, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(_wifi_list, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(_wifi_list, 4, 0);
  lv_obj_set_style_pad_row(_wifi_list, 4, 0);
  lv_obj_add_flag(_wifi_list, LV_OBJ_FLAG_HIDDEN);

  _wifi_pass = wifiField(body, "Password", true);

  lv_obj_t* save = lv_button_create(body);
  lv_obj_set_size(save, LV_PCT(100), 38);
  lv_obj_set_style_shadow_width(save, 0, 0);
  lv_obj_set_style_radius(save, theme::RADIUS, 0);
  lv_obj_add_event_cb(save, onWifiSave, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(save, LV_SYMBOL_OK " Save", THEME_FONT_BODY, theme::TEXT));
  stylePrimary(save);
  _wifi_status = label(body, "", THEME_FONT_SMALL, theme::TEXT_MUTED);

  // Keyboard over the bottom of the screen; the body shrinks above it while
  // it is up, so the field being edited stays visible.
  _wifi_kb = kb::create(screen(), _prefs);
  lv_obj_set_size(_wifi_kb, LV_PCT(100), 124);
  lv_obj_align(_wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_event_cb(_wifi_kb, onWifiKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_wifi_kb, onWifiKb, LV_EVENT_CANCEL, NULL);
  lv_obj_add_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN);
}

// Rebuilt in place (a network saved or forgotten), at the same scroll.
static void wifiRebuild(UITask* ui, lv_obj_t*& body) {
  int32_t y = body ? lv_obj_get_scroll_y(body) : 0;
  ui->showWifi();
  if (body) { layoutNow(body); lv_obj_scroll_to_y(body, y, LV_ANIM_OFF); }
}

void UITask::wifiEdit(lv_obj_t* ta) {
  if (!_wifi_kb) return;
  fieldFocus(_wifi_ssid, false);
  fieldFocus(_wifi_pass, false);
  lv_keyboard_set_textarea(_wifi_kb, ta);
  fieldFocus(ta, true);
  if (lv_obj_has_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_remove_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN);
    if (_body) lv_obj_set_height(_body, lv_obj_get_height(_body) - lv_obj_get_height(_wifi_kb));
  }
  layoutNow(screen());
  lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
}

void UITask::wifiKeyboardHide() {
  if (!_wifi_kb || lv_obj_has_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN)) return;
  lv_obj_add_flag(_wifi_kb, LV_OBJ_FLAG_HIDDEN);
  fieldFocus(_wifi_ssid, false);
  fieldFocus(_wifi_pass, false);
  if (_body) lv_obj_set_height(_body, lv_obj_get_height(_body) + lv_obj_get_height(_wifi_kb));
}

// Settings > Connections > WiFi, as the Bluetooth row: the switch turns it
// on / off, the row opens the network settings.
static void wifiRow(lv_obj_t* body) {
  char sub[48];
  wifiHint(sub, sizeof(sub));
  lv_obj_t* sw = switchRow(body, LV_SYMBOL_WIFI "  WiFi", sub, nullptr);
  if (lvport::wifiAllowed()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, onWifiAllowed, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_t* row = lv_obj_get_parent(sw);
  rowPressable(row);
  lv_obj_add_event_cb(row, [](lv_event_t* e) {
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) s_ui->showWifi();   // not the switch
  }, LV_EVENT_CLICKED, NULL);
}

void UITask::wifiSetAllowed(bool on) {
  lvport::setWifiAllowed(on);
  if (!on) {
    if (mapview::s_dl.active()) mapDownloadStop();
    if (_wifi_scanning) { _wifi_scanning = false; lvport::netEnd(); }
  }
}

void UITask::wifiScan() {
  if (_wifi_scanning) return;
  if (!lvport::wifiAllowed()) { lv_label_set_text(_wifi_status, "Turn WiFi on in Settings first."); return; }
  if (wifiInUse()) { lv_label_set_text(_wifi_status, "WiFi busy (a download or an update) - try later."); return; }
  lvport::scanStart();
  _wifi_scanning = true;
  lv_label_set_text(_wifi_status, LV_SYMBOL_REFRESH "  Scanning...");
}

// A just-saved network, joined once to check the password: connected, turned
// away (the password, most likely: it's forgotten again, the form keeps it to
// fix) or not found (kept: maybe it's just out of range now).
static char s_wifi_test_ssid[33], s_wifi_test_pass[65];
static const uint32_t WIFI_TEST_MS = 15000;

static void wifiTestPoll(UITask* ui, bool in_use) {
  if (!s_wifi_test_ms) return;
  int ns = lvport::netState();
  bool timed_out = millis() - s_wifi_test_ms > WIFI_TEST_MS;
  if (ns != lvport::NET_UP && ns != lvport::NET_FAILED && !timed_out) return;
  s_wifi_test_ms = 0;
  char msg[80];
  if (ns == lvport::NET_UP) {
    snprintf(msg, sizeof(msg), LV_SYMBOL_OK "  Connected to %s - password OK.", s_wifi_test_ssid);
  } else if (lvport::netNotFound()) {
    snprintf(msg, sizeof(msg), "Saved. %s isn't in range now - the password is checked when it is.", s_wifi_test_ssid);
  } else {
    int k = lvport::wifiFind(s_wifi_test_ssid);
    if (k >= 0) lvport::forgetWifi(k);
    snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING "  %s turned the password down - not saved.", s_wifi_test_ssid);
  }
  if (!in_use) lvport::netEnd();
  bool failed = ns != lvport::NET_UP && !lvport::netNotFound();
  ui->showWifi();   // the saved list may have changed
  ui->wifiTestShow(msg, failed ? s_wifi_test_ssid : nullptr, s_wifi_test_pass);
}

void UITask::wifiTestShow(const char* msg, const char* ssid, const char* pass) {
  if (!_wifi_status) return;
  lv_label_set_text(_wifi_status, msg);
  lv_label_set_long_mode(_wifi_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(_wifi_status, LV_PCT(100));
  if (ssid) {   // back in the form, to fix
    lv_textarea_set_text(_wifi_ssid, ssid);
    lv_textarea_set_text(_wifi_pass, pass);
  }
  layoutNow(_body);
  lv_obj_scroll_to_view(_wifi_status, LV_ANIM_ON);
}

void UITask::pollWifiScan() {
  wifiTestPoll(this, wifiInUse());
  if (!_wifi_scanning || !_wifi_list) return;
  int n = lvport::scanResults(s_wifi_names, lvport::WIFI_SCAN_MAX);
  if (n < 0) return;
  _wifi_scanning = false;
  lv_obj_clean(_wifi_list);
  for (int i = 0; i < n; i++) {
    lv_obj_t* b = lv_button_create(_wifi_list);
    lv_obj_set_height(b, 30);
    lv_obj_set_style_pad_hor(b, 10, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(b, onWifiPick, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    char text[40];
    bool known = lvport::wifiFind(s_wifi_names[i]) >= 0;
    snprintf(text, sizeof(text), known ? LV_SYMBOL_OK " %s" : "%s", s_wifi_names[i]);
    lv_obj_center(label(b, text, THEME_FONT_SMALL, known ? theme::ACCENT : theme::TEXT));
  }
  if (n > 0) lv_obj_remove_flag(_wifi_list, LV_OBJ_FLAG_HIDDEN);
  setText(_wifi_status, n > 0 ? "Tap a network, then enter its password." : "No networks found.");
  if (!wifiInUse()) lvport::netEnd();   // the scan switched the radio on
}

void UITask::wifiPick(int idx) {
  if (idx < 0 || idx >= lvport::WIFI_SCAN_MAX) return;
  lv_textarea_set_text(_wifi_ssid, s_wifi_names[idx]);
  const lvport::WifiNet* w = lvport::wifiSaved(lvport::wifiFind(s_wifi_names[idx]));
  lv_textarea_set_text(_wifi_pass, w ? w->pass : "");
  lv_obj_add_flag(_wifi_list, LV_OBJ_FLAG_HIDDEN);
  wifiEdit(_wifi_pass);
}

void UITask::wifiOpenSaved(int idx) {
  const lvport::WifiNet* w = lvport::wifiSaved(idx);
  if (!w) return;
  lv_textarea_set_text(_wifi_ssid, w->ssid);
  lv_textarea_set_text(_wifi_pass, w->pass);
  lv_label_set_text(_wifi_status, "Change the password, then Save.");
  wifiEdit(_wifi_pass);
}

void UITask::wifiForget(int idx) {
  const lvport::WifiNet* w = lvport::wifiSaved(idx);
  char msg[48];
  snprintf(msg, sizeof(msg), "Forgot %s", w ? w->ssid : "");
  lvport::forgetWifi(idx);
  wifiRebuild(this, _body);
  showToast(msg);
}

void UITask::wifiSave() {
  const char* ssid = lv_textarea_get_text(_wifi_ssid);
  const char* pass = lv_textarea_get_text(_wifi_pass);
  if (!ssid[0]) { lv_label_set_text(_wifi_status, "Enter a network name."); return; }
  snprintf(s_wifi_test_ssid, sizeof(s_wifi_test_ssid), "%s", ssid);
  snprintf(s_wifi_test_pass, sizeof(s_wifi_test_pass), "%s", pass);
  lvport::saveWifi(s_wifi_test_ssid, s_wifi_test_pass);
  wifiKeyboardHide();
  wifiRebuild(this, _body);
  // Then joined once, to check the password -- unless WiFi is off or busy.
  char msg[80];
  if (!lvport::wifiAllowed() || wifiInUse()) {
    snprintf(msg, sizeof(msg), "Saved %s", s_wifi_test_ssid);
    showToast(msg);
    return;
  }
  lvport::netEnd();   // a scan might have left the radio on
  lvport::netJoin(s_wifi_test_ssid, s_wifi_test_pass);
  s_wifi_test_ms = millis() | 1;
  snprintf(msg, sizeof(msg), LV_SYMBOL_REFRESH "  Connecting to %s...", s_wifi_test_ssid);
  wifiTestShow(msg, nullptr, nullptr);
}
