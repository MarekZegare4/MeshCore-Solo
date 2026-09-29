// Sound: the speaker (genericBuzzer through the L2's codec), notify(),
// Settings > Sound's own rows (the mode, the two melodies), the melody editor
// (SCR_MELODY) and a conversation's melody. The settings and the melody model
// are ui-core/SoundControl.h; which sound a message plays, SoundNotifier.h.
// Included by UITask.cpp (single translation unit).

namespace sndview {

enum : uint8_t { MS_BPM, MS_PITCH, MS_OCT, MS_DUR };
enum : uint8_t { MA_PLAY, MA_ADD, MA_DELETE };

static soundctl::Melody s_mel;          // the melody being edited
static int  s_slot = 0;                 // 0 = melody 1, 1 = melody 2
static int  s_sel = 0;                  // selected note
static bool s_dirty = false;
static lv_obj_t* s_strip = nullptr;     // the notes, side by side
static lv_obj_t* s_cells[soundctl::MAX_NOTES];
static lv_obj_t* s_pitch = nullptr;
static lv_obj_t* s_oct = nullptr;
static lv_obj_t* s_dur = nullptr;
static lv_obj_t* s_bpm = nullptr;
static lv_obj_t* s_title = nullptr;    // "Melodies  3/32"
static lv_obj_t* s_play_lbl = nullptr;
static bool s_playing_shown = false;
static bool s_play_all = false;         // Play (not a note's preview): follow it in the strip
static int  s_hl = -1;                  // the note lit as playing
// The melody playing from here: the sim's and nRF52's players keep a pointer
// into it (only the I2S one copies), so it must outlive the call.
static char s_play_buf[220];

static const char* PITCHES[] = { "Rest", "C", "D", "E", "F", "G", "A", "B", "" };
static const char* OCTAVES[] = { "4", "5", "6", "" };
static const char* DURS[]    = { "1/4", "1/8", "1/16", "1/32", "" };
static const char* SLOTS[]   = { "Melody 1", "Melody 2", "" };

// Selected: dim accent with a border; sounding now (Play): full accent.
static void styleCell(int i, bool sel) {
  if (i < 0 || i >= soundctl::MAX_NOTES || !s_cells[i]) return;
  uint32_t bg = i == s_hl ? theme::ACCENT : sel ? theme::ACCENT_DIM : theme::SURFACE;
  lv_obj_set_style_bg_color(s_cells[i], lv_color_hex(bg), 0);
  lv_obj_set_style_border_width(s_cells[i], sel ? 2 : 0, 0);
}

// Moves the "sounding" light to note `idx` (-1: none).
static void lightNote(int idx) {
  if (idx == s_hl) return;
  int old = s_hl;
  s_hl = idx;
  styleCell(old, old == s_sel);
  styleCell(s_hl, s_hl == s_sel);
  if (s_hl >= 0 && s_hl < s_mel.len && s_cells[s_hl]) lv_obj_scroll_to_view(s_cells[s_hl], LV_ANIM_ON);
}

static void cellText(int i) {
  if (!s_cells[i]) return;
  char name[3];
  soundctl::noteName(s_mel.notes[i], name);
  lv_label_set_text(lv_obj_get_child(s_cells[i], 0), name);
  lv_label_set_text(lv_obj_get_child(s_cells[i], 1), soundctl::durLabel(soundctl::noteDur(s_mel.notes[i])));
}

static void refreshCount() {
  if (s_title) lv_label_set_text_fmt(s_title, "Melodies  %d/%d", s_mel.len, soundctl::MAX_NOTES);
}

static void setMatrix(lv_obj_t* m, int sel, bool enabled) {
  if (!m) return;
  lv_buttonmatrix_clear_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKED);
  if (sel >= 0) lv_buttonmatrix_set_button_ctrl(m, sel, LV_BUTTONMATRIX_CTRL_CHECKED);
  if (enabled) lv_buttonmatrix_clear_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_DISABLED);
  else lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_DISABLED);
}

// The pitch / octave / length controls show the selected note.
static void syncControls() {
  bool any = s_sel < s_mel.len;
  uint8_t b = any ? s_mel.notes[s_sel] : 0;
  setMatrix(s_pitch, any ? soundctl::notePitch(b) : -1, any);
  setMatrix(s_oct, any && soundctl::notePitch(b) ? soundctl::noteOctave(b) - soundctl::OCT_MIN : -1,
            any && soundctl::notePitch(b));   // a rest has no octave
  setMatrix(s_dur, any ? soundctl::noteDur(b) : -1, any);
  refreshCount();
}

static void onMelNote(lv_event_t* e);

static void rebuildStrip() {
  if (!s_strip) return;
  lv_obj_clean(s_strip);
  memset(s_cells, 0, sizeof(s_cells));
  s_hl = -1;
  if (!s_mel.len) {
    lv_obj_t* t = label(s_strip, "Empty: tap + to add a note", THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_obj_set_style_pad_top(t, 12, 0);
  }
  for (int i = 0; i < s_mel.len; i++) {
    lv_obj_t* c = lv_button_create(s_strip);
    lv_obj_set_size(c, 38, 44);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_radius(c, theme::RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(theme::ACCENT), 0);
    lv_obj_add_event_cb(c, onMelNote, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_align(label(c, "", THEME_FONT_BODY, theme::TEXT), LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_align(label(c, "", THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_BOTTOM_MID, 0, -4);
    s_cells[i] = c;
    cellText(i);
    styleCell(i, i == s_sel);
  }
  lv_obj_update_layout(s_strip);
  if (s_sel < s_mel.len) lv_obj_scroll_to_view(s_cells[s_sel], LV_ANIM_OFF);
}

// A short blip of the note, as it's picked or changed.
static void preview(UITask* ui, uint8_t b) {
  s_play_all = false;
  lightNote(-1);
  ui->stopMelody();   // before the buffer changes under the player
  soundctl::notePreview(b, s_play_buf, sizeof(s_play_buf));
  if (s_play_buf[0]) ui->playMelody(s_play_buf);
}

static void onSoundMode(lv_event_t* e) {
  s_ui->setSoundMode((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onMelodyRow(lv_event_t* e) { s_ui->showMelodies((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onMelSlot(lv_event_t* e) {
  s_ui->melodySlot((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onMelNote(lv_event_t* e) { s_ui->melodyPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onMelMatrix(lv_event_t* e) {
  s_ui->melodySet((uint8_t)(uintptr_t)lv_event_get_user_data(e),
                  (int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onMelBpm(lv_event_t* e) {
  s_ui->melodySet(MS_BPM, choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onMelAction(lv_event_t* e) { s_ui->melodyAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onMelPlay(lv_event_t* e) { (void)e; s_ui->melodyAction(MA_PLAY); }

static lv_obj_t* smallButton(lv_obj_t* parent, const char* text, uint8_t act) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, 40, 32);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS_SM, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onMelAction, LV_EVENT_CLICKED, (void*)(uintptr_t)act);
  lv_obj_center(label(b, text, THEME_FONT_BODY, theme::TEXT));
  return b;
}

static lv_obj_t* flexRow(lv_obj_t* parent) {
  lv_obj_t* r = lv_obj_create(parent);
  styleSurface(r, theme::BG);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, theme::GAP, 0);
  return r;
}

}  // namespace sndview

// ── The speaker ───────────────────────────────────────────────────────────────

void UITask::playMelody(const char* melody) {
#ifdef PIN_BUZZER
  _buzzer.playForced(melody);
#else
  (void)melody;
#endif
}

void UITask::stopMelody() {
#ifdef PIN_BUZZER
  _buzzer.stop();
#endif
}

int UITask::melodyNote() {
#ifdef PIN_BUZZER
  return _buzzer.noteIndex();
#else
  return -1;
#endif
}

bool UITask::melodyPlaying() {
#ifdef PIN_BUZZER
  return _buzzer.isPlaying();
#else
  return false;
#endif
}

#ifdef PIN_BUZZER
uint16_t UITask::buzzerFreqHz() const {
#ifdef SIM_PLATFORM
  return _buzzer.currentFreqHz();
#else
  return 0;
#endif
}
#endif

// An incoming message, advert or delivery tick: the sound its settings pick.
void UITask::notify(UIEventType t) {
#ifdef PIN_BUZZER
  SoundNotifier sn(_buzzer, _prefs, _notif_mel_buf, sizeof(_notif_mel_buf));
  switch (t) {
    case UIEventType::contactMessage:
      sn.playDM(_notif_dm_valid, _notif_dm_prefix);
      _notif_dm_valid = false;
      break;
    case UIEventType::channelMessage:
      sn.playCH(_notif_ch_idx);
      _notif_ch_idx = -1;
      break;
    case UIEventType::roomMessage:   // many authors, no per-room melody: the DM default
      sn.playDM(false, nullptr);
      break;
    case UIEventType::advertReceivedFlood:
    case UIEventType::advertReceivedZeroHop:
      sn.playAD(t == UIEventType::advertReceivedFlood);
      break;
    case UIEventType::ack:
      _buzzer.play("ack:d=32,o=8,b=120:c");
      break;
    default:
      break;
  }
#else
  (void)t;
#endif
}

// ── Settings > Sound ──────────────────────────────────────────────────────────

void UITask::applySoundPrefs() {
#ifdef PIN_BUZZER
  if (_prefs) _buzzer.setVolume(_prefs->buzzer_volume);
#endif
}

void UITask::setSoundMode(int mode) {
  if (!_prefs || mode < 0 || mode >= soundctl::MODE_COUNT) return;
#ifdef PIN_BUZZER
  soundctl::setMode(_prefs, _buzzer, (uint8_t)mode, isClientConnected());
  if (mode == soundctl::MODE_ON) notify(UIEventType::ack);   // hear that it's on
#endif
  prefsSave();
  refreshStatusBar();
  if (mode == soundctl::MODE_AUTO) showToast(isClientConnected() ? "Silent while the app is connected" : "Sound on until the app connects");
}

void UITask::setSoundVolume(int level) {
  if (!_prefs) return;
#ifdef PIN_BUZZER
  soundctl::setVolume(_prefs, _buzzer, (uint8_t)level);
#endif
  prefsSave();
}

// The Sound page's own rows: the mode at the top, the melodies at the bottom.
void UITask::buildSoundRows(lv_obj_t* body, bool top) {
  using namespace sndview;
  if (top) {
    static const char* MODES[] = { "On", "Off", "Auto", "" };
    lv_obj_t* row = settingRow(body, "Sound", "Auto: off with the app");
    radioview::rowSegmented(row, MODES, soundctl::mode(_prefs), 150, onSoundMode, 0);
    return;
  }
  lv_obj_t* g = group(body, "MELODIES");
  for (int slot = 0; slot < 2; slot++) {
    soundctl::Melody m;
    soundctl::load(_prefs, slot, m);
    char sub[40];
    if (m.len) snprintf(sub, sizeof(sub), "%d note%s, %u BPM", m.len, m.len == 1 ? "" : "s", soundctl::bpm(m.bpm_idx));
    else snprintf(sub, sizeof(sub), "Empty  -  tap to compose");
    listRow(g, slot ? "Melody 2" : "Melody 1", sub, onMelodyRow, (void*)(uintptr_t)slot);
  }
  groupNote(body, "Per chat: hold it in Chats for its options.");
}

// ── Melody editor ─────────────────────────────────────────────────────────────

void UITask::showMelodies(int slot) {
  using namespace sndview;
  stopMelody();
  s_slot = slot == 1 ? 1 : 0;
  soundctl::load(_prefs, s_slot, s_mel);
  s_sel = 0;
  s_dirty = false;
  s_play_all = false;
  _screen = SCR_MELODY;
  buildMelodies();
}

void UITask::buildMelodies() {
  using namespace sndview;
  lv_obj_t* body = newScreen("Melodies", true);
  s_title = lv_obj_get_child(_header, 1);   // after the back button
  lv_obj_set_style_pad_row(body, 6, 0);
  headerButton(_header, LV_SYMBOL_PLAY "  Play", onMelPlay, 0, &s_play_lbl);
  s_playing_shown = false;

  lv_obj_t* top = flexRow(body);
  lv_obj_t* slots = segmented(top, SLOTS, s_slot, 150, 32);
  lv_obj_add_event_cb(slots, onMelSlot, LV_EVENT_VALUE_CHANGED, NULL);
  s_bpm = choiceCreate(top, "60 BPM\n90 BPM\n120 BPM\n150 BPM\n180 BPM", s_mel.bpm_idx, "Tempo");
  lv_obj_set_size(s_bpm, 102, 32);
  lv_obj_add_event_cb(s_bpm, onMelBpm, LV_EVENT_VALUE_CHANGED, NULL);
  smallButton(top, LV_SYMBOL_PLUS, MA_ADD);   // (fits: 150 + 102 + 40 + gaps)

  s_strip = lv_obj_create(body);
  styleSurface(s_strip, theme::BG);
  lv_obj_set_size(s_strip, LV_PCT(100), 50);
  lv_obj_set_flex_flow(s_strip, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(s_strip, 4, 0);
  lv_obj_set_style_pad_ver(s_strip, 3, 0);
  lv_obj_set_scroll_dir(s_strip, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(s_strip, LV_SCROLLBAR_MODE_OFF);

  s_pitch = segmented(body, PITCHES, -1, LV_PCT(100), 34);
  lv_obj_add_event_cb(s_pitch, onMelMatrix, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)MS_PITCH);

  lv_obj_t* bottom = flexRow(body);
  s_oct = segmented(bottom, OCTAVES, -1, 84, 32);
  lv_obj_add_event_cb(s_oct, onMelMatrix, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)MS_OCT);
  s_dur = segmented(bottom, DURS, -1, 158, 32);
  lv_obj_add_event_cb(s_dur, onMelMatrix, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)MS_DUR);
  smallButton(bottom, LV_SYMBOL_TRASH, MA_DELETE);
  for (lv_obj_t* m : { s_pitch, s_oct, s_dur }) {   // greyed out with no note to edit
    lv_obj_set_style_text_opa(m, LV_OPA_30, LV_PART_ITEMS | LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(m, lv_color_hex(theme::SURFACE), LV_PART_ITEMS | LV_STATE_DISABLED);
  }

  rebuildStrip();
  syncControls();
}

// Every 30 ms on SCR_MELODY: the playing note lights up, the header's
// Play / Stop follows the player.
void UITask::refreshMelody() {
  using namespace sndview;
  bool playing = melodyPlaying();
  if (!playing) s_play_all = false;
  lightNote(s_play_all ? melodyNote() : -1);
  if (!s_play_lbl || playing == s_playing_shown) return;
  s_playing_shown = playing;
  setText(s_play_lbl, playing ? LV_SYMBOL_STOP "  Stop" : LV_SYMBOL_PLAY "  Play");
}

void UITask::melodySave() {
  using namespace sndview;
  if (!s_dirty || !_prefs) return;
  soundctl::store(_prefs, s_slot, s_mel);
  prefsSave();
  s_dirty = false;
  showToast(s_slot ? "Melody 2 saved" : "Melody 1 saved", 1200);
}

void UITask::melodySlot(int slot) {
  using namespace sndview;
  if (slot == s_slot) return;
  melodySave();
  stopMelody();
  s_play_all = false;
  s_slot = slot == 1 ? 1 : 0;
  soundctl::load(_prefs, s_slot, s_mel);
  s_sel = 0;
  if (s_bpm) choiceSetSelected(s_bpm, s_mel.bpm_idx);
  rebuildStrip();
  syncControls();
  refreshMelody();
}

void UITask::melodyPick(int idx) {
  using namespace sndview;
  if (idx < 0 || idx >= s_mel.len) return;
  styleCell(s_sel, false);
  s_sel = idx;
  styleCell(s_sel, true);
  syncControls();
  preview(this, s_mel.notes[s_sel]);
}

void UITask::melodySet(uint8_t which, int v) {
  using namespace sndview;
  if (which == MS_BPM) {
    if (v < 0 || v >= soundctl::BPM_COUNT) return;
    s_mel.bpm_idx = (uint8_t)v;
    s_dirty = true;
    return;
  }
  if (s_sel >= s_mel.len || v < 0) return;
  uint8_t b = s_mel.notes[s_sel];
  uint8_t p = soundctl::notePitch(b), o = soundctl::noteOctave(b), d = soundctl::noteDur(b);
  if (which == MS_PITCH && v < soundctl::PITCH_COUNT) p = (uint8_t)v;
  else if (which == MS_OCT && v <= soundctl::OCT_MAX - soundctl::OCT_MIN) o = (uint8_t)(soundctl::OCT_MIN + v);
  else if (which == MS_DUR && v < soundctl::DUR_COUNT) d = (uint8_t)v;
  else return;
  s_mel.notes[s_sel] = soundctl::packNote(p, o, d);
  s_dirty = true;
  cellText(s_sel);
  syncControls();
  if (which != MS_DUR) preview(this, s_mel.notes[s_sel]);
}

void UITask::melodyAction(uint8_t act) {
  using namespace sndview;
  switch (act) {
    case MA_PLAY:
      if (melodyPlaying()) { stopMelody(); s_play_all = false; break; }
      if (!s_mel.len) { showToast("Add a note first", 1200); break; }
      soundctl::toRtttl(s_mel, s_play_buf, sizeof(s_play_buf));
      playMelody(s_play_buf);
      s_play_all = true;
      break;
    case MA_ADD: {   // a copy of the selected note after it (or C5 1/8 to start)
      int at = s_mel.len ? s_sel + 1 : 0;
      uint8_t note = s_mel.len ? s_mel.notes[s_sel] : soundctl::NEW_NOTE;
      if (!soundctl::insertNote(s_mel, at, note)) { showToast("32 notes at most", 1200); break; }
      s_sel = at;
      s_dirty = true;
      rebuildStrip();
      syncControls();
      preview(this, note);
      break;
    }
    case MA_DELETE:
      if (!soundctl::removeNote(s_mel, s_sel)) break;
      if (s_sel >= s_mel.len && s_sel > 0) s_sel--;
      s_dirty = true;
      rebuildStrip();
      syncControls();
      break;
  }
  refreshMelody();
}

// ── A conversation's melody (its options popup) ───────────────────────────────

void UITask::conversationMelody(int v) {
  if (!_prefs || v < 0 || v >= soundctl::OVERRIDE_COUNT) return;
  contactctl::setMelody(_prefs, convview::s_key, (uint8_t)v);
  prefsSave();
  hearMelody(v);
}

// A melody picked for a chat: play it (slot 1 / 2; 0 = the default, silent).
void UITask::hearMelody(int slot) {
  if (slot < 1 || slot > 2) return;
  soundctl::Melody m;
  soundctl::load(_prefs, slot - 1, m);
  if (!m.len) { showToast("That melody is empty - compose it in Settings > Sound", 2500); return; }
  stopMelody();
  soundctl::toRtttl(m, sndview::s_play_buf, sizeof(sndview::s_play_buf));
  playMelody(sndview::s_play_buf);
}
