#pragma once
// Included into UITask.cpp only, where it stands (it uses what is defined above it).
// Settings: the schema-driven pages and the hand-built ones (Privacy ...).

// ── Settings (first rows; the declarative schema replaces this later) ────────

static lv_obj_t* s_kb_main_dd = nullptr;
static lv_obj_t* s_kb_alt_dd = nullptr;

static void onKeyboardAlphabet(lv_event_t* e) {
  (void)e;
  s_ui->setKeyboardAlphabets(choiceSelected(s_kb_main_dd), choiceSelected(s_kb_alt_dd));
}

// A preference toggle (0/1 byte in NodePrefs), saved on change.
static void onPrefSwitch(lv_event_t* e) {
  uint8_t* pref = (uint8_t*)lv_event_get_user_data(e);
  *pref = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
  prefsSave();
}

static void onGpsSwitch(lv_event_t* e) {
  s_ui->setGps(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

// pref == nullptr: the caller wires the switch itself.
static lv_obj_t* switchRow(lv_obj_t* parent, const char* text, const char* sub, uint8_t* pref) {
  lv_obj_t* row = settingRow(parent, text, sub, 236);   // the hint clear of the switch
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 46, 24);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  if (pref) {
    if (*pref) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onPrefSwitch, LV_EVENT_VALUE_CHANGED, pref);
  }
  return sw;
}

// ── Schema-driven settings (ui-core/SettingsSchema.h) ─────────────────────────

// Settings > Display: ui-new's Message alert as a switch -- the banner, or
// none (Normal / Off; the L2 has no corner for Compact's envelope).
static void onMsgBannerSwitch(lv_event_t* e) {
  s_ui->setMsgBanner(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
void UITask::msgBannerRow(lv_obj_t* card) {
  lv_obj_t* sw = switchRow(card, "Message banner", "A new message drops in from the top", nullptr);
  if (!_prefs || _prefs->msg_alert != NodePrefs::MSG_ALERT_OFF) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, onMsgBannerSwitch, LV_EVENT_VALUE_CHANGED, NULL);
}
void UITask::setMsgBanner(bool on) {
  if (!_prefs) return;
  _prefs->msg_alert = on ? NodePrefs::MSG_ALERT_NORMAL : NodePrefs::MSG_ALERT_OFF;
  prefsSave();
}

static void onSchemaSwitch(lv_event_t* e) {
  s_ui->setSchemaValue((int)(uintptr_t)lv_event_get_user_data(e),
                       lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onSchemaDropdown(lv_event_t* e) {
  s_ui->setSchemaValue((int)(uintptr_t)lv_event_get_user_data(e),
                       choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onOpenSchemaPage(lv_event_t* e) {
  s_nav_section = -1;   // from Settings: all of a page
  s_ui->showSchemaSettings((int)(uintptr_t)lv_event_get_user_data(e));
}
static void onHomeApps(lv_event_t* e) { (void)e; s_ui->homeEdit(true); }
static void onPruneContacts(lv_event_t* e) { (void)e; s_ui->pruneContacts(); }
static void onOpenQuickMsgs(lv_event_t* e);
static void onPinSetup(lv_event_t* e);   // DeviceScreen.h
static void onBattCurve(lv_event_t* e) { (void)e; s_ui->showBattCurve(); }
static void onOpenOta(lv_event_t* e);    // OtaScreen.h
static void bluetoothRow(lv_obj_t* body);
static void onVolumeSlider(lv_event_t* e) {
  s_ui->setSoundVolume((int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)));
}


// Map tools > Trail / Share / Alert options: that section of the Map options.
void UITask::showMapOptions(uint8_t section) {
  s_nav_section = section;
  showSchemaSettings(settings::PG_NAV);
}

void UITask::showSchemaSettings(int page) {
  if (page == settings::PG_NAV && !_nav_back) s_opts_from_map = _screen == SCR_MAP;
  _screen = SCR_SETTINGS_NAV;
  _settings_page = (uint8_t)(page < PG_ALL ? page : 0);
  buildSchemaSettings();
}

// Contacts > prune now: first tap shows how many, the second (within 3 s) removes them.
void UITask::pruneContacts() {
  int n = the_mesh.countStaleContacts();
  if (n == 0) {
    showToast(_prefs && _prefs->contact_expiry_idx == 0 ? "Contact expiry is off" : "No inactive contacts");
    return;
  }
  char ask[40];
  snprintf(ask, sizeof(ask), LV_SYMBOL_TRASH "  Remove %d contact%s?", n, n == 1 ? "" : "s");
  if (!tapConfirmed(_prune_lbl, ask)) return;
  int removed = the_mesh.pruneStaleContacts();
  char t[40];
  snprintf(t, sizeof(t), "Removed %d contact%s", removed, removed == 1 ? "" : "s");
  showToast(t);
}

void UITask::applyDisplayPrefs() {
  if (!_prefs) return;
  if (_prefs->display_brightness_pct) lvport::setBacklightPct(_prefs->display_brightness_pct);
  else if (_display) _display->setBrightness(_prefs->display_brightness);
}

// Brightness slider: live while dragging, saved on release.
static void onBrightnessSlider(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  s_ui->setBrightnessPct((uint8_t)lv_slider_get_value(sl), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

void UITask::setBrightnessPct(uint8_t pct, bool save) {
  if (!_prefs) return;
  _prefs->display_brightness_pct = pct;
  _prefs->display_brightness = (uint8_t)((pct + 12) / 25 > 4 ? 4 : (pct + 12) / 25);   // nearest level, for anything reading it
  applyDisplayPrefs();
  if (save) prefsSave();
}

// Every screen is built in the theme's colours: a new one starts afresh.
static void onThemeChoice(lv_event_t* e) {
  const bool light = choiceSelected((lv_obj_t*)lv_event_get_target(e)) == 1;
  if (light == theme::s_light) return;
  lvport::saveLight(light);
  restartScreen(light ? "Starting in the light theme." : "Starting in the dark theme.");
}

static void onTapWake(lv_event_t* e) {
  s_ui->setTapWake(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

void UITask::setTapWake(bool on) {
  _tap_wake = on;
  lvport::saveTapWake(on);
}

void UITask::buildSchemaSettings() {
  static const char* const OWN[] = { "Keyboard", "About", "Display", "Power", "Time" };
  const char* title = _settings_page >= PG_KEYBOARD ? OWN[_settings_page - PG_KEYBOARD] : settings::pageTitle(_settings_page);
  if (_settings_page == settings::PG_NAV && s_nav_section >= 0)
    title = s_nav_section == settings::SEC_TRAIL ? "Trail options"
          : s_nav_section == settings::SEC_LIVE_SHARE ? "Share options" : "Alert options";
  lv_obj_t* body = newScreen(title, true);
  _prune_lbl = nullptr;
  switch (_settings_page) {
    case PG_KEYBOARD: buildKeyboardPage(body); return;
    case PG_ABOUT:    buildAboutPage(body); return;
    case PG_DISPLAY: {
      lv_obj_t* g = group(body, "SCREEN");
      schemaRow(g, SETTING(display_brightness));
      schemaRow(g, SETTING(auto_off_secs));
      schemaRow(g, SETTING(msg_wake));
      lv_obj_t* tw = switchRow(g, "Tap to wake", "Off: only the top button wakes it", nullptr);   // in NVS
      if (_tap_wake) lv_obj_add_state(tw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(tw, onTapWake, LV_EVENT_VALUE_CHANGED, NULL);
      g = group(body, "LOCK");
      schemaRow(g, SETTING(auto_lock));
      listRow(g, "Screen PIN", pinSet() ? "On  -  asked when the screen wakes" : "Off", onPinSetup, NULL);
      g = group(body, "LOOK");
      choiceRow(g, "Theme", "Changing it restarts the device", "Dark\nLight", theme::s_light ? 1 : 0, onThemeChoice);
      accentRow(g);   // DeviceScreen.h
      return;
    }
    case PG_POWER: {
      lv_obj_t* g = group(body, "BATTERY");
      schemaRow(g, SETTING(batt_display_mode));
      schemaRow(g, SETTING(low_batt_mv));
      listRow(g, "Battery curve", _prefs && battery::validCurve(_prefs->batt_curve_mv) ? "Your own" : "LiPo (built in)",
              onBattCurve, NULL);
      g = group(body, "GPS");
      schemaRow(g, SETTING(gps_interval));
      return;
    }
    case PG_TIME: {
      lv_obj_t* g = group(body, nullptr);
      for (int i = 0; i < settings::COUNT; i++) if (settings::ALL[i].section == settings::SEC_TIME) schemaRow(g, i);
      return;
    }
  }
  schemaRows(body, _settings_page);
  if (_settings_page == settings::PG_SOUND) then(&UITask::schemaExtras, settings::PG_SOUND);   // after its groups
  if (_settings_page == settings::PG_MESSAGES) then(&UITask::schemaExtras, settings::PG_MESSAGES);
  if (_settings_page == settings::PG_PRIVACY) then(&UITask::schemaExtras, settings::PG_PRIVACY);
}

// ── Settings > Privacy: position, Bluetooth PIN; factory reset ─────────────────

static void onOwnPos(lv_event_t* e)     { (void)e; s_ui->ownPositionPopup(); }
static void onOwnPosPick(lv_event_t* e) { s_ui->ownPositionPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onOwnPosKb(lv_event_t* e)   { s_ui->ownPositionDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onBlePin(lv_event_t* e)     { (void)e; s_ui->blePinPopup(); }
static void onBlePinKb(lv_event_t* e)   { s_ui->blePinDone(lv_event_get_code(e) == LV_EVENT_READY); }

enum : int { OWNPOS_TYPE, OWNPOS_WAYPOINTS, OWNPOS_CLEAR, OWNPOS_WP0 = 100 };

void UITask::ownPositionPopup() {
  if (_core->gpsEnabled()) { showToast("The GPS sets your position while it's on", 2500); return; }
  int32_t lat, lon;
  bool have = _core->fixedPosition(lat, lon);
  lv_obj_t* g = group(navPopupPanel("Fixed position", false), nullptr);
  listRow(g, "Type coordinates", "Latitude, longitude in degrees", onOwnPosPick, (void*)(uintptr_t)OWNPOS_TYPE);
  if (_core->waypoints.count() > 0) {
    char sub[24];
    snprintf(sub, sizeof(sub), "%d saved", _core->waypoints.count());
    listRow(g, "From a waypoint", sub, onOwnPosPick, (void*)(uintptr_t)OWNPOS_WAYPOINTS);
  }
  if (have) actionRow(g, "Clear", onOwnPosPick, (void*)(uintptr_t)OWNPOS_CLEAR);
}

void UITask::ownPositionPick(int code) {
  const WaypointModel& wp = _core->waypoints;
  if (code == OWNPOS_TYPE) {
    int32_t lat, lon;
    char cur[32] = "";
    if (_core->fixedPosition(lat, lon)) snprintf(cur, sizeof(cur), "%.5f, %.5f", lat / 1e6, lon / 1e6);
    TextEntry t = { "Fixed position", onOwnPosKb };
    t.text = cur;
    t.hint = "50.06143, 19.93658";
    t.max_bytes = 31;
    t.accepted = "0123456789.,- ";
    t.symbols = true;
    navTextEntry(t);
    return;
  }
  if (code == OWNPOS_WAYPOINTS) {
    lv_obj_t* list = scrollList(navPopupPanel("From a waypoint", true));
    for (int i = 0; i < wp.count(); i++) {
      const Waypoint& w = wp.at(i);
      char title[WAYPOINT_LABEL_LEN + 8], sub[40];
      snprintf(title, sizeof(title), UI_SYMBOL_FLAG "  %s", w.label[0] ? w.label : "(unnamed)");
      snprintf(sub, sizeof(sub), "%.5f, %.5f", w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);
      listRow(list, title, sub, onOwnPosPick, (void*)(uintptr_t)(OWNPOS_WP0 + i));
    }
    return;
  }
  if (code == OWNPOS_CLEAR) _core->setFixedPosition(0, 0);
  else if (code >= OWNPOS_WP0 && code - OWNPOS_WP0 < wp.count())
    _core->setFixedPosition(wp.at(code - OWNPOS_WP0).lat_1e6, wp.at(code - OWNPOS_WP0).lon_1e6);
  navClosePopup();
  if (_screen == SCR_SETTINGS_NAV) rebuildSchemaSettings();
}

void UITask::ownPositionDone(bool ok) {
  if (ok && _nav_ta) {
    int32_t lat, lon;
    if (!geo::parseLatLon(lv_textarea_get_text(_nav_ta), lat, lon) || (!lat && !lon)) {
      showToast("Type it as latitude, longitude");
      return;   // the field stays, to fix it
    }
    _core->setFixedPosition(lat, lon);
  }
  navClosePopup();
  if (ok && _screen == SCR_SETTINGS_NAV) rebuildSchemaSettings();
}

void UITask::blePinPopup() {
  if (!_prefs) return;
  char cur[8] = "";
  if (_prefs->ble_pin) snprintf(cur, sizeof(cur), "%06lu", (unsigned long)_prefs->ble_pin);
  TextEntry t = { "Bluetooth PIN", onBlePinKb };
  t.text = cur;
  t.hint = "6 digits, empty for a random one";
  t.max_bytes = 6;
  t.accepted = "0123456789";
  t.symbols = true;
  navTextEntry(t);
}

void UITask::blePinDone(bool ok) {
  if (ok && _nav_ta) {
    const char* t = lv_textarea_get_text(_nav_ta);
    if ((t[0] && strlen(t) != 6) || !_core->setBlePin((uint32_t)strtoul(t, nullptr, 10))) {
      showToast("Six digits, not starting with 0");
      return;
    }
    showToast("Used from the next start", 2000);
  }
  navClosePopup();
  if (ok && _screen == SCR_SETTINGS_NAV) rebuildSchemaSettings();
}

void UITask::factoryResetStep(int stage) {
  if (stage == 1)
    confirmBody(navPopupPanel("Factory reset?", false),
                "Erases contacts, channels, messages, settings and this node's keys. The SD card is left as it is.",
                "Continue", onFactoryReset, 2);
  else if (stage == 2)
    confirmBody(navPopupPanel("Erase everything?", false),
                "This can't be undone. The device restarts as a new node.",
                LV_SYMBOL_TRASH "  Erase", onFactoryReset, 3);
  else {
    navClosePopup();
    _core->factoryReset();   // restarts; back here only if the erase failed
    showToast("Erase failed");
  }
}

// The companion CLI's settings keys (ui-core/SettingsCli.h); a Settings page
// open on the screen is built again with the new value.
bool UITask::cliCommand(const char* command, char* reply, int n) {
  if (!settingscli::handle(*_core, command, reply, n)) return false;
  if (_screen == SCR_SETTINGS_NAV) rebuildSchemaSettings();
  return true;
}

// What a page adds to its schema groups, once they are there.
void UITask::schemaExtras(int page) {
  if (page == settings::PG_PRIVACY) {
    if (lv_obj_t* c = s_sec_card[settings::SEC_PRIVACY]) {   // with "Position in adverts"
      char sub[40];
      int32_t lat, lon;
      if (_core->gpsEnabled()) snprintf(sub, sizeof(sub), "From the GPS while it's on");
      else if (_core->fixedPosition(lat, lon)) snprintf(sub, sizeof(sub), "%.5f, %.5f", lat / 1e6, lon / 1e6);
      else snprintf(sub, sizeof(sub), "Not set");
      listRow(c, "Fixed position", sub, onOwnPos, NULL);
    }
#ifdef BLE_PIN_CODE
    char pin[40];
    if (_prefs && _prefs->ble_pin) snprintf(pin, sizeof(pin), "%06lu", (unsigned long)_prefs->ble_pin);
    else snprintf(pin, sizeof(pin), "Random each start, shown on screen");
    listRow(group(_body, "BLUETOOTH"), "Pairing PIN", pin, onBlePin, NULL);
#endif
  }
  if (page == settings::PG_SOUND) buildSoundRows(_body, false);   // the melodies
  if (page == settings::PG_MESSAGES) {
    lv_obj_t* c = s_sec_card[settings::SEC_CONTACTS];   // the action that goes with "Contact expiry"
    if (c) _prune_lbl = actionRow(c, LV_SYMBOL_TRASH "  Remove inactive contacts now", onPruneContacts, NULL, theme::FAIL);
    char sub[48];
    snprintf(sub, sizeof(sub), "%d of %d set  -  sent with one tap", msgtext::quickUsed(_prefs), msgtext::QUICK_COUNT);
    if (lv_obj_t* m = s_sec_card[settings::SEC_MESSAGES]) listRow(m, "Quick messages", sub, onOpenQuickMsgs, NULL);
  }
}

// Settings > Keyboard: the two scripts the keyboard switches between.
void UITask::buildKeyboardPage(lv_obj_t* body) {
  uint8_t main_a = _prefs ? _prefs->keyboard_main_alphabet : 0;
  uint8_t alt_a  = _prefs ? _prefs->keyboard_alt_alphabet : 0;
  if (main_a >= NodePrefs::KB_ALPHABET_COUNT) main_a = 0;
  if (alt_a >= NodePrefs::KB_ALPHABET_COUNT) alt_a = main_a;
  lv_obj_t* g = group(body, "SCRIPTS");
  // Order matches NodePrefs::KB_ALPHABET_* (Latin, Cyrillic, Greek).
  s_kb_main_dd = choiceRow(g, "Main", nullptr, "Latin\nCyrillic\nGreek", main_a, onKeyboardAlphabet);
  // "None" = no second script (stored as alt == main, as ui-new does).
  s_kb_alt_dd  = choiceRow(g, "Additional", "The globe key switches to it", "None\nLatin\nCyrillic\nGreek",
                           alt_a == main_a ? 0 : alt_a + 1, onKeyboardAlphabet);
  groupNote(body, "Hold a letter for accents and other variants.");
}

// A QR code as a 1-bit image into dsc / data (the palette, then the rows):
// black on white with its quiet zone, s px a module. The image cache forgets
// an older picture at the same place.
template <class Dark>
static void qrImage(lv_image_dsc_t& dsc, uint8_t* data, size_t size, int n, int s, Dark dark) {
  const int side = n + 2 * siteqr::QUIET, px = side * s, stride = (px + 7) / 8;
  memset(data, 0, size);
  const lv_color32_t pal[2] = { lv_color32_make(0xFF, 0xFF, 0xFF, 0xFF), lv_color32_make(0, 0, 0, 0xFF) };
  memcpy(data, pal, sizeof(pal));
  uint8_t* rows = data + 8;
  for (int y = 0; y < px; y++)
    for (int x = 0; x < px; x++) {
      int mx = x / s - siteqr::QUIET, my = y / s - siteqr::QUIET;
      if (mx >= 0 && my >= 0 && mx < n && my < n && dark(mx, my)) rows[y * stride + x / 8] |= 0x80 >> (x % 8);
    }
  lv_image_cache_drop(&dsc);
  dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  dsc.header.cf = LV_COLOR_FORMAT_I1;
  dsc.header.w = px;
  dsc.header.h = px;
  dsc.header.stride = stride;
  dsc.data_size = 8 + stride * px;
  dsc.data = data;
}

// The Solo site's QR code (ui-core/SiteQr.h), built once at 5 px a module.
static lv_obj_t* siteQr(lv_obj_t* parent) {
  static const int S = 5, PX = (siteqr::N + 2 * siteqr::QUIET) * S;
  static PSRAM_BSS uint8_t data[8 + (PX + 7) / 8 * PX];
  static lv_image_dsc_t dsc;
  if (!dsc.data) qrImage(dsc, data, sizeof(data), siteqr::N, S, siteqr::dark);
  lv_obj_t* img = lv_image_create(parent);
  lv_image_set_src(img, &dsc);
  return img;
}

// Your own contact's QR code (ui-core/ContactQr.h), made each time it is
// shown (the name can change), 3 px a module; null when it doesn't fit.
static lv_obj_t* contactQr(lv_obj_t* parent) {
  static const int S = 3, PX = (contactqr::MAX_VERSION * 4 + 17 + 2 * siteqr::QUIET) * S;
  static PSRAM_BSS uint8_t data[8 + (PX + 7) / 8 * PX];
  static lv_image_dsc_t dsc;
  static PSRAM_BSS contactqr::Code code;
  if (!contactqr::make(code)) return nullptr;
  qrImage(dsc, data, sizeof(data), code.n, S, [](int x, int y) { return code.dark(x, y); });
  lv_obj_t* img = lv_image_create(parent);
  lv_image_set_src(img, &dsc);
  return img;
}

// Settings > About: this node, the firmware, the manual's QR code, credits.
void UITask::buildAboutPage(lv_obj_t* body) {
  lv_obj_t* about = infoCard(body);
  infoRow(about, "Node", the_mesh.getNodeName());
  infoRow(about, "Firmware", FIRMWARE_VERSION);
  if (!strstr(FIRMWARE_VERSION, FIRMWARE_BUILD_DATE)) infoRow(about, "Built", FIRMWARE_BUILD_DATE);
  sectionTitle(body, "MY CONTACT");   // the phone app scans it to add you
  lv_obj_t* me = infoCard(body);
  lv_obj_set_flex_align(me, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(me, theme::PAD, 0);
  lv_obj_set_style_pad_row(me, 6, 0);
  contactQr(me);
  label(me, "Scan with the MeshCore app to add this node", THEME_FONT_SMALL, theme::TEXT_MUTED);
  sectionTitle(body, "MANUAL");
  lv_obj_t* man = infoCard(body);
  lv_obj_set_flex_align(man, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(man, theme::PAD, 0);
  lv_obj_set_style_pad_row(man, 6, 0);
  siteQr(man);
  label(man, siteqr::URL, THEME_FONT_SMALL, theme::TEXT_MUTED);
  sectionTitle(body, "CREDITS");
  lv_obj_t* cr = infoCard(body);
  infoNote(cr, "Map data", creditText((lvport::mountStorage() && mapview::s_provider->available())
                                          ? mapview::s_provider->attribution() : "\xC2\xA9 OpenStreetMap contributors"));
  infoNote(cr, "Emoji", creditText("Twemoji \xC2\xA9 Twitter, Inc. and contributors (CC-BY\xC2\xA0" "4.0)"));
}

// A page's schema rows, one group per section (Settings pages and the tools'
// options). Each section's card is kept in s_sec_card for rows added after.

// On a screen's body the first group shows at once and the others follow
// (fill); a popup's list gets them all now.
static lv_obj_t* s_schema_body = nullptr;
static bool s_schema_one = false;   // one section: its own page, no heading
static bool s_schema_bare = false;  // no section headings

void UITask::schemaRows(lv_obj_t* body, uint8_t page) {
  for (lv_obj_t*& c : s_sec_card) c = nullptr;
  s_schema_body = body;
  s_schema_one = page == settings::PG_NAV && s_nav_section >= 0;
  s_schema_bare = s_schema_one || page == settings::PG_RADIO;   // one section: the page title says it
  uint8_t sec = 0xFF;
  bool first = true;
  for (int i = 0; i < settings::COUNT; i++) {
    const settings::Setting& st = settings::ALL[i];
    if (settings::sectionPage(st.section) != page || st.section == sec) continue;
    if (s_schema_one && st.section != s_nav_section) continue;
    sec = st.section;
    if (first || body != _body) schemaSection(i);
    else fill(&UITask::schemaSection, i, i + 1);
    first = false;
  }
}

// The group of the section setting i starts, with its rows.
void UITask::schemaSection(int i) {
  uint8_t sec = settings::ALL[i].section;
  lv_obj_t* card = s_sec_card[sec] = group(s_schema_body, s_schema_bare ? nullptr : settings::sectionTitle(sec));
  if (sec == settings::SEC_SOUND) buildSoundRows(card, true);   // On / Off / Auto
  for (int k = i; k < settings::COUNT && settings::ALL[k].section == sec; k++) {
    if (settings::ALL[k].offset == offsetof(NodePrefs, lock_compact)) continue;   // ui-new's lock look; the slide card has one
    if (settings::ALL[k].offset == offsetof(NodePrefs, msg_alert)) { msgBannerRow(card); continue; }   // no corner envelope here
    schemaRow(card, k);
  }
}

// One schema setting as a row of `card`: a switch, a choice, or (brightness,
// volume) a slider.
void UITask::schemaRow(lv_obj_t* card, int i) {
  if (i < 0 || i >= settings::COUNT) return;
  const settings::Setting& st = settings::ALL[i];
  uint8_t v = settings::get(*_prefs, st);
  if (st.offset == offsetof(NodePrefs, buzzer_volume)) {   // a five-step slider, heard on release
    lv_obj_t* sl = rowSlider(settingRow(card, st.label, NULL), 0, 4, v);
    lv_obj_add_event_cb(sl, onVolumeSlider, LV_EVENT_RELEASED, NULL);
    return;
  }
  if (st.offset == offsetof(NodePrefs, display_brightness)) {   // a slider here instead of five steps
    uint8_t pct = _prefs->display_brightness_pct ? _prefs->display_brightness_pct
                                                 : (uint8_t)(_prefs->display_brightness * 25 > 5 ? _prefs->display_brightness * 25 : 5);
    lv_obj_t* sl = rowSlider(settingRow(card, st.label, NULL), 5, 100, pct);
    lv_obj_add_event_cb(sl, onBrightnessSlider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, onBrightnessSlider, LV_EVENT_RELEASED, NULL);
    return;
  }
  if (!st.option) {
    lv_obj_t* sw = switchRow(card, st.label, st.hint, nullptr);
    if (v) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onSchemaSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
    return;
  }
  // The longest list, the time zone, is 27 options and 167 bytes: at 160
  // it lost UTC+8 .. +14, and a zone set from the app showed as UTC-12.
  char opts[320];
  int o = 0;
  for (uint8_t k = 0; k < st.count && o < (int)sizeof(opts) - 16; k++) {
    if (k) opts[o++] = '\n';
    st.option(k, opts + o, sizeof(opts) - o, *_prefs);
    o += strlen(opts + o);
  }
  opts[o] = '\0';
  choiceRow(card, st.label, st.hint, opts, v, onSchemaDropdown, (void*)(uintptr_t)i);
}

void UITask::setSchemaValue(int idx, int v) {
  if (!_prefs || idx < 0 || idx >= settings::COUNT) return;
  const settings::Setting& st = settings::ALL[idx];
  settings::set(*_prefs, st, (uint8_t)v);
  if (st.changed) st.changed(*_core);
  prefsSave();
  if (st.offset == offsetof(NodePrefs, units_imperial) && _screen == SCR_SETTINGS_NAV)   // other labels depend on it
    rebuildSoon(&UITask::rebuildSchemaSettings);
}

void UITask::rebuildSchemaSettings() {
  int32_t y = _body ? lv_obj_get_scroll_y(_body) : 0;
  buildSchemaSettings();
  fillFlush();
  if (_body) { layoutNow(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
}

// A screen rebuilt for what a switch or segmented row changed (hints, rows
// that come and go), once its knob has moved: rebuilt at once, the 80-130 ms
// cut the animation short and the knob jumped. Not if the screen's gone by then.
static lv_timer_t* s_rebuild_t = nullptr;
void UITask::rebuildSoon(void (UITask::*fn)()) {
  static void (UITask::*s_fn)() = nullptr;
  static uint8_t s_scr = 0;
  s_fn = fn;
  s_scr = (uint8_t)_screen;
  if (s_rebuild_t) lv_timer_delete(s_rebuild_t);
  s_rebuild_t = lv_timer_create([](lv_timer_t*) {
    s_rebuild_t = nullptr;
    if ((uint8_t)s_ui->_screen == s_scr && !lv_display_get_screen_prev(NULL)) (s_ui->*s_fn)();
  }, 150, nullptr);
  lv_timer_set_repeat_count(s_rebuild_t, 1);
}

void UITask::showSettings() {
  _screen = SCR_SETTINGS;
  buildSettings();
}

static void onOpenRadio(lv_event_t* e);   // RadioScreen.h
static void onOpenStorage(lv_event_t* e); // StorageScreen.h

// Settings: groups of rows, each opening a page (chevron) or switching
// something on the spot. What the app is for first, the system last.
void UITask::buildSettings() {
  bool restore = _nav_back;   // back from one of its pages: where it was
  lv_obj_t* body = newScreen("Settings", true);
  // A group at a time (fillStart): the first shows at once. Coming back to
  // where it was scrolled needs them all first.
  fillStart(SETTINGS_GROUPS, 1, &UITask::settingsGroup);
  if (restore) { fillFlush(); layoutNow(body); lv_obj_scroll_to_y(body, s_settings_y, LV_ANIM_OFF); }
}

void UITask::settingsGroup(int i) {
  lv_obj_t* body = _body;
  char sub[48];
  lv_obj_t* g;
  char v1[24], v2[24];
  switch (i) {
  case 0:
    if (!_prefs) break;
    g = group(body, "DEVICE");
    listRow(g, LV_SYMBOL_EYE_OPEN "  Display", "Brightness, screen off, lock, colour",
            onOpenSchemaPage, (void*)(uintptr_t)PG_DISPLAY);
    snprintf(sub, sizeof(sub), "Order, hide  -  %d of %d shown", home::shownCount(), home::APP_COUNT);
    listRow(g, LV_SYMBOL_HOME "  Home apps", sub, onHomeApps, NULL);
    snprintf(sub, sizeof(sub), "Shutdown %s  -  GPS: %s", settingText(*_prefs, SETTING(low_batt_mv), v1, sizeof(v1)),
             settingText(*_prefs, SETTING(gps_interval), v2, sizeof(v2)));
    listRow(g, LV_SYMBOL_BATTERY_FULL "  Power", sub, onOpenSchemaPage, (void*)(uintptr_t)PG_POWER);
    {
      char vol[12];
      settings::optVolume(_prefs->buzzer_volume, vol, sizeof(vol), *_prefs);
      snprintf(sub, sizeof(sub), "%s, %s", soundctl::modeLabel(soundctl::mode(_prefs)), vol);
    }
    listRow(g, LV_SYMBOL_VOLUME_MAX "  Sound", sub, onOpenSchemaPage, (void*)(uintptr_t)settings::PG_SOUND);
    {
      static const char* const SCRIPT[] = { "Latin", "Cyrillic", "Greek" };
      uint8_t ma = _prefs->keyboard_main_alphabet % NodePrefs::KB_ALPHABET_COUNT;
      uint8_t aa = _prefs->keyboard_alt_alphabet % NodePrefs::KB_ALPHABET_COUNT;
      snprintf(sub, sizeof(sub), aa == ma ? "%s" : "%s + %s", SCRIPT[ma], SCRIPT[aa]);
    }
    listRow(g, LV_SYMBOL_KEYBOARD "  Keyboard", sub, onOpenSchemaPage, (void*)(uintptr_t)PG_KEYBOARD);
    listRow(g, LV_SYMBOL_ENVELOPE "  Messages & contacts", "Resend, adding, expiry, quick messages",
            onOpenSchemaPage, (void*)(uintptr_t)settings::PG_MESSAGES);
    break;

  case 1:
    g = group(body, "CONNECTIONS");
    if (_prefs) {
      int pi = radioctl::currentPreset(_prefs);
      const char* pn = "Custom"; float f, b; uint8_t sf, cr;
      if (pi >= 0) radioctl::presetAt(_prefs, pi, pn, f, b, sf, cr);
      snprintf(sub, sizeof(sub), "%s  -  %.3f MHz, %d dBm", pn, _prefs->freq, _prefs->tx_power_dbm);
      listRow(g, UI_SYMBOL_RADIO "  Radio", sub, onOpenRadio, NULL);
      listRow(g, LV_SYMBOL_EYE_CLOSE "  Privacy", "Position, who can ask for telemetry, Bluetooth PIN",
              onOpenSchemaPage, (void*)(uintptr_t)settings::PG_PRIVACY);
    }
    bluetoothRow(g);
    wifiRow(g);
    if (_core->gpsAvailable()) {   // as WiFi: the switch turns it on / off, the row opens its details
      lv_obj_t* sw = switchRow(g, LV_SYMBOL_GPS "  GPS", "Tap for satellites and signal", nullptr);
      if (_core->gpsEnabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, onGpsSwitch, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_t* row = lv_obj_get_parent(sw);
      rowPressable(row);
      lv_obj_add_event_cb(row, [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) onOpenGpsFromSettings(e);   // not the switch
      }, LV_EVENT_CLICKED, NULL);
    }
    break;

  case 2:
    g = group(body, "MAP & DATA");
    if (_prefs) listRow(g, UI_SYMBOL_MAP "  Map", "Trail, live sharing, arrival alert",
                        onOpenSchemaPage, (void*)(uintptr_t)settings::PG_NAV);
    listRow(g, LV_SYMBOL_SD_CARD "  Storage", "SD card, message history", onOpenStorage, NULL);
    break;

  case 3:
    g = group(body, "SYSTEM");
    listRow(g, LV_SYMBOL_EDIT "  Name", the_mesh.getNodeName(), onNodeName, NULL);
    if (_prefs) {
      snprintf(sub, sizeof(sub), "%s%s, %s", settingText(*_prefs, SETTING(tz_offset_hours), v1, sizeof(v1)),
               inSummerTime(*_prefs, rtc_clock.getCurrentTime()) ? " summer" : "", _prefs->clock_12h ? "12 h" : "24 h");
      listRow(g, UI_SYMBOL_CLOCK "  Time", sub, onOpenSchemaPage, (void*)(uintptr_t)PG_TIME);
      schemaRow(g, SETTING(units_imperial));
    }
    listRow(g, LV_SYMBOL_DOWNLOAD "  Firmware update", FIRMWARE_VERSION, onOpenOta, NULL);
    listRow(g, LV_SYMBOL_LIST "  About", "Node, your contact QR, manual, credits", onOpenSchemaPage, (void*)(uintptr_t)PG_ABOUT);
    break;

  case 4:
    g = group(body, nullptr);
    actionRow(g, LV_SYMBOL_REFRESH "  Reboot", onPowerRow, (void*)(uintptr_t)1);
    actionRow(g, LV_SYMBOL_POWER "  Power off", onPowerRow, (void*)(uintptr_t)0, theme::FAIL);
    actionRow(g, LV_SYMBOL_TRASH "  Factory reset", onFactoryReset, (void*)(uintptr_t)1, theme::FAIL);
    break;
  }
}

void UITask::setKeyboardAlphabets(int main_idx, int alt_sel) {
  if (!_prefs) return;
  _prefs->keyboard_main_alphabet = (uint8_t)main_idx;
  _prefs->keyboard_alt_alphabet  = (uint8_t)(alt_sel == 0 ? main_idx : alt_sel - 1);
  prefsSave();
}
