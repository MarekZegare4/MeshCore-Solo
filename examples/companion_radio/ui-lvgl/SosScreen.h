#pragma once
// SOS on the L2. The engine and the messages are ui-core (SosEngine,
// UiCore::sendSos); here: Home > SOS (the settings), and the view over
// everything, the lock screen and a screen PIN too, on lv_layer_top:
//  - the sheet: holding the top button 5 s (Top button on) or Send SOS brings a
//    slider, like a phone's emergency slider: a button in a pocket mustn't start one;
//  - the countdown: big digits, Cancel; either button cancels too;
//  - while it runs: how it's going, Hide, and a slider to stop it, so a pocket
//    can't end a call for help. Hidden, the status bar and the title bar's
//    clock turn red (refreshStatusBar()); holding the top button brings it back.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after Flashlight.h.

namespace sosview {

enum Mode : uint8_t { NONE, SHEET, LIVE };
static const uint32_t VIEW_MS = 30000;   // the view stays this long untouched (not the countdown)

static Mode s_mode = NONE;
static bool s_counting = false;          // the LIVE view was built for the countdown
static bool s_wifi_was = false;          // onSosRunning() rested it
static lv_obj_t* s_view = nullptr;
static lv_obj_t* s_slider = nullptr;
static lv_obj_t* s_title = nullptr;
static lv_obj_t* s_digits = nullptr;
static lv_obj_t* s_line[4] = { nullptr, nullptr, nullptr, nullptr };
static lv_obj_t* s_ch_lbl = nullptr;     // Channels' value, behind its popup
static SlideDrag s_drag;
static uint32_t s_until = 0, s_next = 0;
static int s_left = 0;                   // the countdown second last beeped
static int s_edit = -1;                  // the row a popup is editing

enum Row : uint8_t { R_CHANNELS, R_DM1, R_DM2, R_DM3, R_ACK, R_NOTE };
enum Pref : uint8_t { P_COUNTDOWN, P_REPEAT, P_BEEP };

static const char* COUNTDOWNS[NodePrefs::SOS_COUNTDOWN_COUNT + 1] = { "5s", "10s", "15s", "" };
static const char* REPEATS[NodePrefs::SOS_REPEAT_COUNT + 1] = { "Off", "1m", "2m", "5m", "10m", "" };
static const char* BEEPS[NodePrefs::SOS_BEEP_COUNT + 1] = { "Off", "30s", "1m", "5m", "" };

}  // namespace sosview

static void onOpenSos(lv_event_t* e) { (void)e; s_ui->showSos(); }
static void onSosButton(lv_event_t* e) { (void)e; s_ui->sosButton(); }
static void onSosSend(lv_event_t* e) { (void)e; s_ui->sosSheet(); }
static void onSosRow(lv_event_t* e) { s_ui->sosRow((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onSosSeg(lv_event_t* e) {
  s_ui->sosSet((int)(uintptr_t)lv_event_get_user_data(e),
               (int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onSosChannel(lv_event_t* e) {
  s_ui->sosChannel((int)(uintptr_t)lv_event_get_user_data(e), lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
static void onSosContact(lv_event_t* e) { s_ui->sosContact((int)(intptr_t)lv_event_get_user_data(e)); }
static void onSosTextKb(lv_event_t* e) { s_ui->sosTextDone(lv_event_get_code(e) == LV_EVENT_READY); }

// ── The view ──────────────────────────────────────────────────────────────────

bool UITask::sosViewOpen() const { return sosview::s_mode != sosview::NONE; }

void UITask::sosViewClose() {
  using namespace sosview;
  if (s_view) lv_obj_delete_async(s_view);   // may be closing from its own button
  s_view = s_slider = s_title = s_digits = nullptr;
  s_line[0] = s_line[1] = s_line[2] = s_line[3] = nullptr;
  s_drag = SlideDrag();
  s_mode = NONE;
}

static lv_obj_t* sosPanel() {
  lv_obj_t* v = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(v);
  lv_obj_set_size(v, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(v, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(v, LV_OPA_COVER, 0);
  lv_obj_add_flag(v, LV_OBJ_FLAG_CLICKABLE);   // swallows every touch but its own
  lv_obj_remove_flag(v, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(v, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(v, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(v, 14, 0);
  lv_obj_set_style_pad_bottom(v, 12, 0);
  lv_obj_set_style_pad_hor(v, theme::PAD, 0);
  lv_obj_set_style_pad_row(v, 6, 0);
  return v;
}
static lv_obj_t* sosText(lv_obj_t* v, const char* text, const lv_font_t* font, uint32_t col) {
  lv_obj_t* l = label(v, text, font, col);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(l, LV_PCT(100));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}
static void sosWideButton(lv_obj_t* parent, const char* text, int32_t w, int32_t h, uint32_t bg, uint32_t col, lv_event_cb_t cb) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, text, THEME_FONT_BODY, col));
}
static void sosFootButton(lv_obj_t* v, const char* text) {
  sosWideButton(v, text, SLIDE_W, 40, theme::SURFACE_2, theme::TEXT, onSosButton);
}

// The sheet: what will happen, and the slide that starts it.
void UITask::sosSheet() {
  using namespace sosview;
  if (!_prefs || !_core->sos.idle()) { sosOpen(); return; }
  if (!SosEngine::hasTargets(_prefs)) { showToast("SOS: no targets"); return; }
  flashlightClose();
  quickPanelClose(false);
  sosViewClose();
  s_view = sosPanel();
  s_mode = SHEET;
  s_until = millis() + VIEW_MS;
  sosText(s_view, "Emergency SOS", THEME_FONT_LARGE, theme::FAIL);
  const int ch = SosEngine::channels(_prefs), dm = SosEngine::contacts(_prefs);
  char to[64];
  snprintf(to, sizeof(to), "Sends your position to %d channel%s and %d contact%s", ch, ch == 1 ? "" : "s", dm, dm == 1 ? "" : "s");
  sosText(s_view, to, THEME_FONT_BODY, theme::TEXT_MUTED);
  flexFill(s_view);
  s_slider = slideBar(s_view, "slide for SOS", theme::FAIL);
  sosFootButton(s_view, "Cancel");
}

// The countdown, or how it's going.
void UITask::sosLive() {
  using namespace sosview;
  sosViewClose();
  s_view = sosPanel();
  s_mode = LIVE;
  s_counting = _core->sos.counting();
  s_until = millis() + VIEW_MS;
  s_next = 0;
  s_title = sosText(s_view, "", THEME_FONT_LARGE, theme::FAIL);
  if (s_counting) s_digits = sosText(s_view, "", THEME_FONT_CLOCK, theme::TEXT);
  else for (int i = 0; i < 4; i++) {
    // Name and when, then what they said, for the latest two who answered.
    s_line[i] = sosText(s_view, "", THEME_FONT_BODY, i % 2 ? theme::TEXT_MUTED : theme::TEXT);
    lv_label_set_long_mode(s_line[i], LV_LABEL_LONG_DOT);
    lv_obj_set_height(s_line[i], 18);
  }
  flexFill(s_view);
  lv_obj_set_style_pad_row(s_view, 2, 0);   // room for the lines
  if (!s_counting) s_slider = slideBar(s_view, _core->sos.acked() ? "slide to end" : "slide to stop", theme::FAIL);
  sosFootButton(s_view, s_counting ? "Cancel" : "Hide");
  sosRefresh();
}

void UITask::sosRefresh() {
  using namespace sosview;
  SosEngine& s = _core->sos;
  const uint32_t now = millis();
  char buf[48];
  if (s_counting) {
    setText(s_title, "SOS in");
    snprintf(buf, sizeof(buf), "%d", s.secsLeft(now));
    setText(s_digits, buf);
    return;
  }
  setText(s_title, s.acked() ? "Answered" : "SOS");
  setTextColor(s_title, s.acked() ? theme::OK : theme::FAIL);
  if (s.acked()) {
    for (int i = 0; i < 2; i++) {
      const bool on = i < s.responders();
      buf[0] = '\0';
      if (on) {
        const SosEngine::Responder& r = s.responder(i);
        const uint32_t mins = (now - r.at) / 60000UL;
        if (mins) snprintf(buf, sizeof(buf), "%s, %u min ago", r.name, (unsigned)mins);
        else      snprintf(buf, sizeof(buf), "%s, now", r.name);
      }
      setText(s_line[i * 2], buf);
      setText(s_line[i * 2 + 1], on ? s.responder(i).text : "");
    }
    return;
  }
  setText(s_line[3], "");
  s.sentLine(buf, sizeof(buf));
  setText(s_line[0], buf);
  s.nextLine(buf, sizeof(buf), now);
  setText(s_line[1], buf);
  int32_t lat, lon;
  setText(s_line[2], _core->course.currentLocation(lat, lon) ? "" : "No GPS fix");
}

// A screen that was dark comes on for it.
void UITask::sosOpen() {
  if (_core->sos.idle()) return;
  if (_asleep) wake();
  lv_display_trigger_activity(NULL);
  if (!sosViewOpen() || sosview::s_mode == sosview::LIVE) sosLive();
}

// The top button held.
void UITask::sosButtonHeld() {
  if (!_core->sos.idle()) { sosOpen(); return; }
  if (!_prefs || !_prefs->sos_enabled || sosViewOpen()) return;
  if (_asleep) wake();
  lv_display_trigger_activity(NULL);
  playMelody(soundctl::MEL_TICK);   // felt in a pocket: the hold got through
  sosSheet();
}

// Cancel / Hide, or either physical button: anything ends the sheet and the
// countdown; after that it only hides the view.
void UITask::sosButton() {
  if (_core->sos.counting()) {
    _core->stopSos();
    showToast("SOS cancelled");
  }
  sosViewClose();
}

// The sliders. Driven from the loop like the lock screen's.
void UITask::sosPoll() {
  using namespace sosview;
  if (!s_slider || !slidePoll(s_slider, s_drag)) return;
  if (s_mode == SHEET) {
    _core->startSos();
    s_left = 0;
    sosLive();
  } else {
    _core->stopSos();
    sosViewClose();
    showToast("SOS stopped");
  }
}

void UITask::sosTick() {
  using namespace sosview;
  SosEngine& s = _core->sos;
  const uint32_t now = millis();
  if (_sensors) _sensors->setGpsKeepAwake(s.gpsWanted());   // past a GPS duty cycle
  if (s.beepDue(now)) playMelody(soundctl::MEL_SOS);
  if (s.counting()) {
    const int left = s.secsLeft(now);
    if (left != s_left) {   // a beep a second, past the buzzer's mute
      s_left = left;
      playMelody(soundctl::MEL_TICK);
    }
  }
  if (s_mode == LIVE && s_counting != s.counting()) sosLive();   // the countdown ended
  if (s_mode == LIVE && (int32_t)(now - s_next) >= 0) {
    s_next = now + 200;
    sosRefresh();
  }
  if (s_mode != NONE && !s.counting()) {
    if (lv_display_get_inactive_time(NULL) < 1000) s_until = now + VIEW_MS;
    if ((int32_t)(now - s_until) >= 0) sosViewClose();
  }
}

// The battery goes to the SOS: WiFi rests until it ends. Its switch is saved, so
// a restart mid-SOS leaves it off.
void UITask::onSosRunning(bool running) {
  using namespace sosview;
  if (running) {
    s_wifi_was = lvport::wifiAllowed();
    if (s_wifi_was) wifiSetAllowed(false);
  } else if (s_wifi_was) {
    wifiSetAllowed(true);
  }
#ifdef PIN_BUZZER
  if (_prefs) _buzzer.setVolume(running && _prefs->sos_beep_idx ? 4 : _prefs->buzzer_volume);   // to be heard, not to be polite
#endif
  refreshStatusBar();
}

// ── Home > SOS ────────────────────────────────────────────────────────────────

void UITask::showSos() {
  _screen = SCR_SOS;
  buildSos();
}

static lv_obj_t* sosValueRow(lv_obj_t* g, const char* name, const char* hint, const char* val, int row) {
  return rowPick(settingRow(g, name, hint), val, 150, onSosRow, (void*)(uintptr_t)row);
}

void UITask::buildSos() {
  using namespace sosview;
  lv_obj_t* body = newScreen("SOS", true);
  NodePrefs* p = _prefs;
  if (!p) return;
  lv_obj_t* list = scrollList(body);
  lv_obj_t* g = group(list, "START");   // how it begins
  switchRow(g, "Top button", "Hold 5 s for the SOS slider", &p->sos_enabled);
  lv_obj_t* seg = segmented(settingRow(g, "Countdown", nullptr), COUNTDOWNS, p->sos_countdown_idx, 130, 32);
  lv_obj_add_event_cb(seg, onSosSeg, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)P_COUNTDOWN);

  g = group(list, "SENT TO");   // where it goes, and what it says
  char val[40];
  snprintf(val, sizeof(val), "%d on", SosEngine::channels(p));
  s_ch_lbl = sosValueRow(g, "Channels", nullptr, val, R_CHANNELS);
  for (int i = 0; i < NodePrefs::SOS_DM_MAX; i++) {
    ContactInfo* c = SosEngine::hasDm(p, i)
      ? the_mesh.lookupContactByPubKey(p->sos_dm[i], NodePrefs::FAVOURITE_PREFIX_LEN) : nullptr;
    char name[12];
    snprintf(name, sizeof(name), "Contact %d", i + 1);
    sosValueRow(g, name, nullptr, c ? c->name : "-", R_DM1 + i);
  }
  sosValueRow(g, "Note", "Added to the message", p->sos_note[0] ? p->sos_note : "-", R_NOTE);

  g = group(list, "WHILE IT RUNS");   // until someone answers, and after
  seg = segmented(settingRow(g, "Repeat", nullptr), REPEATS, p->sos_repeat_idx, 200, 32);
  lv_obj_add_event_cb(seg, onSosSeg, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)P_REPEAT);
  seg = segmented(settingRow(g, "Beep", "To be found by"), BEEPS, p->sos_beep_idx, 170, 32);
  lv_obj_add_event_cb(seg, onSosSeg, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)P_BEEP);
  sosValueRow(g, "ACK word", "The reply that means received", p->sos_ack[0] ? p->sos_ack : "-", R_ACK);

  sosWideButton(list, "Send SOS", LV_PCT(100), 44, theme::FAIL, 0xFFFFFF, onSosSend);
}

void UITask::sosSet(int what, int val) {
  using namespace sosview;
  if (!_prefs) return;
  if (what == P_COUNTDOWN && val >= 0 && val < NodePrefs::SOS_COUNTDOWN_COUNT) _prefs->sos_countdown_idx = (uint8_t)val;
  else if (what == P_REPEAT && val >= 0 && val < NodePrefs::SOS_REPEAT_COUNT) _prefs->sos_repeat_idx = (uint8_t)val;
  else if (what == P_BEEP && val >= 0 && val < NodePrefs::SOS_BEEP_COUNT) _prefs->sos_beep_idx = (uint8_t)val;
  prefsSave();
}

void UITask::sosChannel(int idx, bool on) {
  using namespace sosview;
  if (!_prefs || idx < 0 || idx >= MAX_GROUP_CHANNELS) return;
  if (on) _prefs->sos_ch_mask |= 1ULL << idx;
  else    _prefs->sos_ch_mask &= ~(1ULL << idx);
  prefsSave();
  char val[16];
  snprintf(val, sizeof(val), "%d on", SosEngine::channels(_prefs));
  if (s_ch_lbl) pickSet(s_ch_lbl, val);
}

void UITask::sosRow(int row) {
  using namespace sosview;
  NodePrefs* p = _prefs;
  if (!p) return;
  s_edit = row;
  if (row == R_ACK || row == R_NOTE) {
    TextEntry te = { row == R_ACK ? "ACK word" : "Note", onSosTextKb };
    te.text = row == R_ACK ? p->sos_ack : p->sos_note;
    te.max_bytes = (row == R_ACK ? sizeof(p->sos_ack) : sizeof(p->sos_note)) - 1;
    te.hint = row == R_ACK ? "e.g. ACK" : "e.g. blood group";
    navTextEntry(te);
    return;
  }
  lv_obj_t* list = scrollList(navPopupPanel(row == R_CHANNELS ? "SOS channels" : "SOS contact", true));
  if (row == R_CHANNELS) {
    lv_obj_t* g = group(list, nullptr);
    int n = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
      switchRow(g, ch.name, nullptr, (p->sos_ch_mask >> i) & 1, onSosChannel, (void*)(uintptr_t)i);
      n++;
    }
    if (n == 0) label(list, "No channels.", THEME_FONT_SMALL, theme::TEXT_MUTED);
    return;
  }
  listRow(list, "(none)", NULL, onSosContact, (void*)(intptr_t)-1);
  pickContacts(list, false, onSosContact, 0, p->sos_dm[row - R_DM1]);
}

void UITask::sosContact(int idx) {
  using namespace sosview;
  if (!_prefs || s_edit < R_DM1 || s_edit > R_DM3 || idx >= devview::PICK_MAX) return;
  uint8_t* slot = _prefs->sos_dm[s_edit - R_DM1];
  if (idx < 0) memset(slot, 0, NodePrefs::FAVOURITE_PREFIX_LEN);
  else         memcpy(slot, devview::s_pick_keys[idx], NodePrefs::FAVOURITE_PREFIX_LEN);
  prefsSave();
  navClosePopup();
  buildSos();
}

void UITask::sosTextDone(bool ok) {
  using namespace sosview;
  if (ok && _nav_ta && _prefs && (s_edit == R_ACK || s_edit == R_NOTE)) {
    if (s_edit == R_ACK) snprintf(_prefs->sos_ack, sizeof(_prefs->sos_ack), "%s", lv_textarea_get_text(_nav_ta));
    else                 snprintf(_prefs->sos_note, sizeof(_prefs->sos_note), "%s", lv_textarea_get_text(_nav_ta));
    prefsSave();
  }
  navClosePopup();
  if (ok) buildSos();
}
