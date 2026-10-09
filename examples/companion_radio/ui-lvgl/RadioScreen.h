#pragma once
// Settings > Radio: preset, frequency, SF / bandwidth / coding rate, TX power
// and Adaptive Power Control -- ui-new's Settings > Radio; the saved presets
// and scopes below them are RadioExtras.h. Every change is
// applied to the radio at once and saved (ui-core/RadioControl.h does the
// applying for both frontends).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after ClockScreen.h.

namespace radioview {

enum : uint8_t { R_PRESET, R_SF, R_BW, R_CR, R_TX, R_APC };
static const int TX_MIN = 2;
#ifdef LORA_TX_POWER
static const int TX_MAX = LORA_TX_POWER;
#else
static const int TX_MAX = 22;
#endif

static lv_obj_t* s_overlay = nullptr;   // frequency entry
static lv_obj_t* s_ta = nullptr;
static bool s_freq_rpt = false;         // the entry is for the repeater profile (RepeaterScreen.h)
static const int OPTS_LEN = 1024;
static char* s_opts = psramBuf<char>(OPTS_LEN);   // dropdown options (LVGL copies them)

// A choice at the right of a settings row (its label titles the picker).
static lv_obj_t* rowDropdown(lv_obj_t* row, const char* opts, int sel, int width, lv_event_cb_t cb, uintptr_t which) {
  (void)width;
  lv_obj_t* t = rowTitle(row);
  lv_obj_t* c = choiceCreate(row, opts, sel, t ? lv_label_get_text(t) : "");
  lv_obj_align(c, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(c, cb, LV_EVENT_VALUE_CHANGED, (void*)which);
  return c;
}

// The LoRa parameters, as Radio and the repeater's own profile both set them:
// preset (0 = "Custom"), frequency (typed in a popup), SF, bandwidth, coding
// rate. `ids` are the radioSet / repeaterSet codes for preset, SF, BW, CR.
static void paramRows(lv_obj_t* g, const NodePrefs* p, int preset, float freq, uint8_t sf_v, float bw_v, uint8_t cr_v,
                      lv_event_cb_t cb, const uint8_t ids[4], lv_event_cb_t freq_cb, const char* sf_hint) {
  int o = snprintf(s_opts, OPTS_LEN, "Custom");
  const char* name; float f, b; uint8_t sf, cr;
  for (int i = 0; radioctl::presetAt(p, i, name, f, b, sf, cr) && o < OPTS_LEN - 24; i++)
    o += snprintf(s_opts + o, OPTS_LEN - o, "\n%s", name);
  rowDropdown(settingRow(g, "Preset", nullptr), s_opts, preset + 1, 0, cb, ids[0]);
  char fs[16];
  snprintf(fs, sizeof(fs), "%.3f MHz", freq);
  rowValue(settingRow(g, "Frequency", nullptr), fs, freq_cb);
  o = 0;
  for (int v = 5; v <= 12; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > 5 ? "\n%d" : "%d", v);
  rowDropdown(settingRow(g, "Spreading factor", sf_hint), s_opts, sf_v >= 5 && sf_v <= 12 ? sf_v - 5 : 0, 0, cb, ids[1]);
  o = 0;
  for (int i = 0; i < LORA_BW_OPT_COUNT; i++)
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%g kHz", i ? "\n" : "", (double)LORA_BW_OPTS[i]);
  rowDropdown(settingRow(g, "Bandwidth", nullptr), s_opts, nearestBwIndex(bw_v), 0, cb, ids[2]);
  o = 0;
  for (int v = 5; v <= 8; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > 5 ? "\n4/%d" : "4/%d", v);
  rowDropdown(settingRow(g, "Coding rate", nullptr), s_opts, cr_v >= 5 && cr_v <= 8 ? cr_v - 5 : 0, 0, cb, ids[3]);
}

// A few choices side by side at the right of a row (a dropdown's list can run
// off the bottom of a popup). `map` ends with "".
static lv_obj_t* rowSegmented(lv_obj_t* row, const char** map, int sel, int width, lv_event_cb_t cb, uintptr_t which) {
  lv_obj_t* seg = segmented(row, map, sel, width, 36);
  lv_obj_align(seg, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(seg, cb, LV_EVENT_VALUE_CHANGED, (void*)which);
  return seg;
}

}  // namespace radioview

static void onRadioDropdown(lv_event_t* e) {
  s_ui->radioSet((int)(uintptr_t)lv_event_get_user_data(e), choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onRadioSwitch(lv_event_t* e) {
  s_ui->radioSet((int)(uintptr_t)lv_event_get_user_data(e),
                 lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onRadioFreq(lv_event_t* e)   { (void)e; s_ui->radioFreqPopup(false); }
static void onRadioFreqKb(lv_event_t* e) { s_ui->radioFreqDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onOpenRadio(lv_event_t* e)   { (void)e; s_ui->showRadio(); }

void UITask::showRadio() {
  _screen = SCR_RADIO;
  buildRadio();
}

void UITask::buildRadio() {
  using namespace radioview;
  lv_obj_t* body = newScreen("Radio", true);
  s_overlay = s_ta = nullptr;
  NodePrefs* p = _prefs;

  lv_obj_t* g = group(body, "LORA");
  static const uint8_t IDS[4] = { R_PRESET, R_SF, R_BW, R_CR };
  paramRows(g, p, radioctl::currentPreset(p), p->freq, p->sf, p->bw, p->cr, onRadioDropdown, IDS, onRadioFreq,
            "Higher = longer range");
  groupNote(body, "Everyone you talk to needs the same settings.");
  fill(&UITask::radioGroup, 1, 4);   // the rest as the loop goes
}

void UITask::radioGroup(int i) {
  using namespace radioview;
  NodePrefs* p = _prefs;
  if (i == 2) { buildRadioExtras(_body); return; }   // my presets, scopes (RadioExtras.h)
  if (i == 3) {   // the schema's advanced options, a page of their own
    lv_obj_t* g = group(_body, nullptr);
    listRow(g, LV_SYMBOL_SETTINGS "  Advanced", "Path hash size, ACKs, listen before talk, delays",
            onOpenSchemaPage, (void*)(uintptr_t)settings::PG_RADIO);
    return;
  }
  lv_obj_t* g = group(_body, "TRANSMIT");
  int o = 0;
  for (int v = TX_MIN; v <= TX_MAX; v++) o += snprintf(s_opts + o, OPTS_LEN - o, v > TX_MIN ? "\n%d dBm" : "%d dBm", v);
  int tx = p->tx_power_dbm < TX_MIN ? TX_MIN : p->tx_power_dbm > TX_MAX ? TX_MAX : p->tx_power_dbm;
  rowDropdown(settingRow(g, "TX power", p->tx_apc ? "Ceiling for auto power" : nullptr), s_opts, tx - TX_MIN, 0,
              onRadioDropdown, R_TX);
  lv_obj_t* sw = switchRow(g, "Auto power", p->client_repeat ? "Off while repeating" : "Lowers power on good links", nullptr);
  if (p->tx_apc) lv_obj_add_state(sw, LV_STATE_CHECKED);
  if (p->client_repeat) lv_obj_add_state(sw, LV_STATE_DISABLED);
  lv_obj_add_event_cb(sw, onRadioSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)R_APC);
}

void UITask::radioSet(int which, int v) {
  using namespace radioview;
  NodePrefs* p = _prefs;
  if (!p) return;
  switch (which) {
    case R_PRESET:
      if (v == 0) return;   // "Custom": nothing to take
      radioctl::choosePreset(p, v - 1);
      break;
    case R_SF: p->sf = (uint8_t)(5 + v); radioctl::applyParams(); break;
    case R_BW: if (v >= 0 && v < LORA_BW_OPT_COUNT) { p->bw = LORA_BW_OPTS[v]; radioctl::applyParams(); } break;
    case R_CR: p->cr = (uint8_t)(5 + v); radioctl::applyParams(); break;
    case R_TX: p->tx_power_dbm = (int8_t)(TX_MIN + v); radioctl::applyTxPower(p); break;
    case R_APC: p->tx_apc = (uint8_t)v; radioctl::applyApc(); break;
  }
  prefsSave();
  if (which == R_APC) rebuildSoon(&UITask::rebuildRadio);   // the TX power hint follows
  else rebuildRadio();   // preset name / hints follow
}

// Rebuilds the screen where it was scrolled to.
void UITask::rebuildRadio() {
  int32_t y = _body ? lv_obj_get_scroll_y(_body) : 0;
  buildRadio();
  fillFlush();
  if (_body) { layoutNow(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
}

void UITask::radioFreqPopup(bool repeater) {
  using namespace radioview;
  if (s_overlay) return;
  s_freq_rpt = repeater;
  lv_obj_t* panel = popupOpen(screen(), POP_TOP, s_overlay);
  lv_obj_set_style_pad_row(panel, 6, 0);
  float lo, hi;
  radio_driver.getFreqBounds(lo, hi);
  char t[48];
  snprintf(t, sizeof(t), "Frequency, MHz (%.0f - %.0f)", lo, hi);
  label(panel, t, THEME_FONT_BODY, theme::TEXT);
  s_ta = textField(panel);
  lv_textarea_set_accepted_chars(s_ta, "0123456789.");
  lv_textarea_set_max_length(s_ta, 10);
  char fs[16];
  snprintf(fs, sizeof(fs), "%.3f", repeater ? _prefs->repeater_freq : _prefs->freq);
  lv_textarea_set_text(s_ta, fs);
  lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
  lv_obj_t* kbd = kb::create(s_overlay, _prefs);
  lv_obj_set_size(kbd, LV_PCT(100), 124);
  lv_obj_align(kbd, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(kbd, s_ta);
  kb::apply(kbd, kb::L_SYM);
  lv_obj_add_event_cb(kbd, onRadioFreqKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(kbd, onRadioFreqKb, LV_EVENT_CANCEL, NULL);
}

void UITask::radioFreqDone(bool ok) {
  using namespace radioview;
  if (ok && s_ta) {
    float lo, hi;
    radio_driver.getFreqBounds(lo, hi);
    float f = strtof(lv_textarea_get_text(s_ta), nullptr);
    if (f < lo || f > hi) { showToast("Out of the radio's range"); return; }
    if (s_freq_rpt) { _prefs->repeater_freq = f; rptctl::applyProfile(); }
    else            { _prefs->freq = f; radioctl::applyParams(); }
    prefsSave();
  }
  radioCloseFreq();
  if (ok) { if (s_freq_rpt) rebuildRepeater(); else rebuildRadio(); }
}

void UITask::radioCloseFreq() {
  if (radioview::s_overlay) lv_obj_delete_async(radioview::s_overlay);
  radioview::s_overlay = radioview::s_ta = nullptr;
}

bool UITask::radioPopupOpen() const { return radioview::s_overlay != nullptr; }
