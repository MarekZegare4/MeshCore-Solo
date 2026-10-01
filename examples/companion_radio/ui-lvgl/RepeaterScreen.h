#pragma once
// Home > Repeater -- ui-new's Tools > Repeater: relay other people's
// messages from this device. The switch, the network it relays on (the
// companion's own frequency, or a dedicated profile with its preset / freq /
// SF / BW / CR), and the flood filters (adverts, hop limit, yield, min SNR,
// duplicate suppression, scope only, extra scopes). Every change applies at
// once and is saved; the logic is ui-core/RepeaterControl.h, the relaying MyMesh.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

namespace rptview {

enum : uint8_t { RP_ON, RP_NETWORK, RP_PRESET, RP_SF, RP_BW, RP_CR, RP_HOPS, RP_YIELD, RP_SNR };
static const int OPTS_LEN = 768;
static char* s_opts = psramBuf<char>(OPTS_LEN);   // dropdown options (LVGL copies them)
static lv_obj_t* s_scopes_sub = nullptr;   // "Extra scopes" row's count, updated from the popup

}  // namespace rptview

static void onOpenRepeater(lv_event_t* e) { (void)e; s_ui->showRepeater(); }
static void onRptDropdown(lv_event_t* e) {
  s_ui->repeaterSet((int)(uintptr_t)lv_event_get_user_data(e), choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onRptSwitch(lv_event_t* e) {
  s_ui->repeaterSet((int)(uintptr_t)lv_event_get_user_data(e),
                    lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onRptNetwork(lv_event_t* e) {
  s_ui->repeaterSet(rptview::RP_NETWORK, (int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onRptFreq(lv_event_t* e)   { (void)e; s_ui->radioFreqPopup(true); }
static void onRptScopes(lv_event_t* e) { (void)e; s_ui->repeaterScopesPopup(); }
static void onRptScopeSwitch(lv_event_t* e) {
  s_ui->repeaterScopeSet((uint8_t)(uintptr_t)lv_event_get_user_data(e),
                         lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

static void extraScopesSummary(const NodePrefs* p, char* b, int n) {
  const ScopeList& sl = the_mesh.scopeList();
  if (sl.count == 0) snprintf(b, n, "No scopes set up (Settings > Radio)");
  else snprintf(b, n, "%d of %u relayed besides the default", rptctl::extraScopesPicked(p), (unsigned)sl.count);
}

void UITask::showRepeater() {
  _screen = SCR_REPEATER;
  buildRepeater();
}

void UITask::buildRepeater() {
  using namespace rptview;
  lv_obj_t* body = newScreen("Repeater", true);
  s_scopes_sub = nullptr;   // made with its group (repeaterRelays)
  NodePrefs* p = _prefs;

  lv_obj_t* g = group(body, nullptr);
  lv_obj_t* sw = switchRow(g, "Repeater", p->client_repeat ? "Relaying for others" : "Relay others' messages", nullptr);
  if (p->client_repeat) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, onRptSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)RP_ON);
  groupNote(body, "Uses more battery; auto power pauses.");
  fill(&UITask::repeaterGroup, 1, 3);   // the rest as the loop goes
}

void UITask::repeaterGroup(int i) {
  using namespace rptview;
  if (i == 2) { repeaterRelays(); return; }
  NodePrefs* p = _prefs;
  lv_obj_t* g = group(_body, "NETWORK");
  lv_obj_t* row = settingRow(g, "Relay on", p->repeater_use_profile ? "Its own frequency" : "Your chat frequency");
  static const char* NET[] = { "Current", "Custom", "" };
  radioview::rowSegmented(row, NET, p->repeater_use_profile ? 1 : 0, 150, onRptNetwork, 0);
  if (p->repeater_use_profile) {
    static const uint8_t IDS[4] = { RP_PRESET, RP_SF, RP_BW, RP_CR };
    radioview::paramRows(g, p, rptctl::currentPreset(p), p->repeater_freq, p->repeater_sf, p->repeater_bw,
                         p->repeater_cr, onRptDropdown, IDS, onRptFreq, nullptr);
  }
}

void UITask::repeaterRelays() {
  using namespace rptview;
  using radioview::rowDropdown;
  NodePrefs* p = _prefs;
  lv_obj_t* g = group(_body, "WHAT IT RELAYS");
  switchRow(g, "Skip adverts", "Relay messages, not adverts", &p->repeat_skip_adverts);
  char v[12];
  int o = 0;
  for (int i = 0; i <= rptctl::MAX_HOPS; i++) {
    rptctl::fmtHops(v, sizeof(v), (uint8_t)i);
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%s", i ? "\n" : "", v);
  }
  rowDropdown(settingRow(g, "Max hops", "Hop limit for floods"), s_opts, p->repeat_max_hops, 0, onRptDropdown, RP_HOPS);
  o = 0;
  for (int i = 0; i <= rptctl::MAX_YIELD; i++) {
    rptctl::fmtYield(v, sizeof(v), (uint8_t)i);
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%s", i ? "\n" : "", v);
  }
  rowDropdown(settingRow(g, "Yield", "Let fixed repeaters go first"), s_opts, p->repeat_delay_boost, 0,
              onRptDropdown, RP_YIELD);
  o = 0;
  for (int i = 0; i < rptctl::snrChoiceCount(); i++) {
    rptctl::fmtSnr(v, sizeof(v), rptctl::snrFromChoice(i));
    o += snprintf(s_opts + o, OPTS_LEN - o, "%s%s", i ? "\n" : "", v);
  }
  rowDropdown(settingRow(g, "Min SNR", "Ignore weaker packets"), s_opts, rptctl::snrToChoice(p->repeat_min_snr), 0,
              onRptDropdown, RP_SNR);
  switchRow(g, "Skip duplicates", "Skip if already relayed", &p->repeat_suppress_dup);
  switchRow(g, "Scope only", "Only floods in your scopes", &p->repeat_scope_only);
  char sub[48];
  extraScopesSummary(p, sub, sizeof(sub));
  s_scopes_sub = rowSub(listRow(g, "Extra scopes", sub, onRptScopes, NULL));
}

// Rebuilds the screen where it was scrolled to.
void UITask::rebuildRepeater() {
  int32_t y = _body ? lv_obj_get_scroll_y(_body) : 0;
  buildRepeater();
  fillFlush();
  if (_body) { layoutNow(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
}

void UITask::repeaterSet(int which, int v) {
  using namespace rptview;
  NodePrefs* p = _prefs;
  if (!p) return;
  switch (which) {
    case RP_ON:
      rptctl::setEnabled(p, v != 0);
      break;
    case RP_NETWORK: rptctl::setUseProfile(p, v != 0); break;
    case RP_PRESET:  if (v == 0) return; rptctl::choosePreset(p, v - 1); break;
    case RP_SF: p->repeater_sf = (uint8_t)(5 + v); rptctl::applyProfile(); break;
    case RP_BW: if (v >= 0 && v < LORA_BW_OPT_COUNT) { p->repeater_bw = LORA_BW_OPTS[v]; rptctl::applyProfile(); } break;
    case RP_CR: p->repeater_cr = (uint8_t)(5 + v); rptctl::applyProfile(); break;
    case RP_HOPS:  p->repeat_max_hops = (uint8_t)v; break;
    case RP_YIELD: p->repeat_delay_boost = (uint8_t)v; break;
    case RP_SNR:   p->repeat_min_snr = rptctl::snrFromChoice(v); break;
  }
  prefsSave();
  if (which <= RP_CR) rebuildRepeater();   // hints, the profile rows and the preset name follow
  refreshStatusBar();
}

// A switch per named scope: relayed besides the default one.
void UITask::repeaterScopesPopup() {
  const ScopeList& sl = the_mesh.scopeList();
  if (sl.count == 0) { showToast("Set up scopes in Settings > Radio first"); return; }
  lv_obj_t* panel = navPopupPanel("Extra scopes", sl.count > 2);
  if (sl.count > 2) lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);   // up to 8 rows
  noteLabel(panel, "Relayed besides the default scope.");
  for (uint8_t i = 0; i < sl.count; i++) {
    lv_obj_t* sw = switchRow(panel, sl.name((uint8_t)(i + 1)), (i + 1) == sl.default_idx ? "Default - always relayed" : nullptr,
                             nullptr);
    if (rptctl::extraScope(_prefs, i)) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onRptScopeSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
  }
}

void UITask::repeaterScopeSet(uint8_t i, bool on) {
  rptctl::setExtraScope(_prefs, i, on);
  prefsSave();
  char sub[48];
  extraScopesSummary(_prefs, sub, sizeof(sub));
  if (rptview::s_scopes_sub) lv_label_set_text(rptview::s_scopes_sub, sub);
}
