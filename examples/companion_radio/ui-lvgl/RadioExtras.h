#pragma once
// Settings > Radio, the parts beyond the radio parameters -- ui-new's
// Settings > Radio preset picker ("+ Save current..." / "- Delete preset...")
// and Settings > Radio > Scope list:
//  - My presets: the user's saved presets (NodePrefs::user_radio_presets, 4
//    slots; saving is ui-core/RadioControl.h). Tap one to use or delete it;
//    "Save current settings" names the current parameters as a new one. They
//    also appear in the Preset dropdown above.
//  - Scopes: the shared named-scope list (ScopeList.h, edited through
//    MyMesh). "*" is no scope; the default one goes on direct messages and
//    relaying, channels pick their own in their options.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

namespace radiox {

enum : uint8_t { N_PRESET, N_SCOPE_ADD, N_SCOPE_RENAME };                 // what the name popup names
enum : uint8_t { P_USE, P_DELETE, P_DELETE_GO };                          // preset popup actions
enum : uint8_t { S_DEFAULT, S_RENAME, S_DELETE, S_DELETE_GO };            // scope popup actions

static uint8_t s_naming = N_PRESET;
static int s_slot = -1;    // user preset slot the popup is about
static int s_scope = -1;   // scope list index the popup is about

}  // namespace radiox

static void onPresetRow(lv_event_t* e)    { s_ui->presetMenu((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onPresetAction(lv_event_t* e) { s_ui->presetAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onPresetSave(lv_event_t* e)   { (void)e; s_ui->radioNamePopup(radiox::N_PRESET); }
static void onRadioNameKb(lv_event_t* e)  { s_ui->radioNameDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onOpenScopes(lv_event_t* e)   { (void)e; s_ui->showScopes(); }
static void onScopeRow(lv_event_t* e)     { s_ui->scopeMenu((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onScopeAdd(lv_event_t* e)     { (void)e; s_ui->radioNamePopup(radiox::N_SCOPE_ADD); }
static void onScopeAction(lv_event_t* e)  { s_ui->scopeAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

// The Radio screen's lower sections, under the parameters.
void UITask::buildRadioExtras(lv_obj_t* body) {
  NodePrefs* p = _prefs;
  lv_obj_t* g = group(body, "MY PRESETS");
  for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++) {
    const NodePrefs::UserRadioPreset& u = p->user_radio_presets[i];
    if (!u.name[0]) continue;
    char sub[48];
    snprintf(sub, sizeof(sub), "%.3f MHz  SF%u  BW%g  4/%u", u.freq, (unsigned)u.sf, (double)u.bw, (unsigned)u.cr);
    lv_obj_t* row = listRow(g, u.name, sub, onPresetRow, (void*)(uintptr_t)i);
    if (radioParamsMatchPreset(p->freq, p->bw, p->sf, p->cr, u.freq, u.bw, u.sf, u.cr)) rowCheck(row);
  }
  actionRow(g, LV_SYMBOL_PLUS "  Save current settings", onPresetSave, NULL, theme::ACCENT);
  if (radioctl::userPresetsFull(p)) groupNote(body, "All 4 used - a new name replaces the oldest.");

  g = group(body, "SCOPE");
  const ScopeList& sl = the_mesh.scopeList();
  char sub[48];
  if (sl.count == 0) snprintf(sub, sizeof(sub), "None set up - messages go everywhere");
  else snprintf(sub, sizeof(sub), "Default: %s  (%u set up)", sl.default_idx ? sl.name(sl.default_idx) : "no scope",
                (unsigned)sl.count);
  listRow(g, "Scopes", sub, onOpenScopes, NULL);
}

// ── My presets ────────────────────────────────────────────────────────────────

void UITask::presetMenu(int slot) {
  if (!_prefs || slot < 0 || slot >= NodePrefs::USER_RADIO_PRESET_MAX) return;
  const NodePrefs::UserRadioPreset& u = _prefs->user_radio_presets[slot];
  if (!u.name[0]) return;
  radiox::s_slot = slot;
  lv_obj_t* panel = navPopupPanel(u.name, false);
  char info[64];
  snprintf(info, sizeof(info), "%.3f MHz, SF%u, BW %g kHz, CR 4/%u", u.freq, (unsigned)u.sf, (double)u.bw, (unsigned)u.cr);
  label(panel, info, THEME_FONT_BODY, theme::TEXT);
  lv_obj_t* acts = buttonBar(panel);
  barButton(acts, LV_SYMBOL_OK " Use", onPresetAction, radiox::P_USE, true);
  barButton(acts, LV_SYMBOL_TRASH " Delete", onPresetAction, radiox::P_DELETE, false);
}

void UITask::presetAction(uint8_t act) {
  using namespace radiox;
  NodePrefs* p = _prefs;
  if (!p || s_slot < 0 || !p->user_radio_presets[s_slot].name[0]) { navClosePopup(); return; }
  NodePrefs::UserRadioPreset& u = p->user_radio_presets[s_slot];
  switch (act) {
    case P_USE:
      p->freq = u.freq; p->bw = u.bw; p->sf = u.sf; p->cr = u.cr;
      radioctl::applyParams();
      prefsSave();
      navClosePopup();
      rebuildRadio();
      showToast("Preset in use");
      break;
    case P_DELETE: {
      char t[40];
      snprintf(t, sizeof(t), "Delete %s?", u.name);
      confirmBody(navPopupPanel(t, false), "Removes the saved preset. The radio settings stay as they are.",
                  LV_SYMBOL_TRASH "  Delete", onPresetAction, P_DELETE_GO);
      break;
    }
    case P_DELETE_GO:
      radioctl::deleteUserPreset(p, s_slot);
      prefsSave();
      navClosePopup();
      rebuildRadio();
      break;
  }
}

// ── Name entry: a new preset, a new scope, a scope's new name ────────────────

void UITask::radioNamePopup(uint8_t what) {
  using namespace radiox;
  if (!_prefs) return;
  s_naming = what;
  const char* title = what == N_PRESET ? "Preset name" : what == N_SCOPE_ADD ? "New scope" : "Scope name";
  const char* hint = what == N_PRESET ? "e.g. Hiking" : "Region name, e.g. pl-maz";
  size_t cap = what == N_PRESET ? sizeof(_prefs->user_radio_presets[0].name) - 1 : sizeof(ScopeEntry::name) - 1;
  TextEntry t = { title, onRadioNameKb };
  t.hint = hint;
  t.max_bytes = cap;
  if (what == N_SCOPE_RENAME) t.text = the_mesh.scopeList().name((uint8_t)s_scope);
  navTextEntry(t);
}

void UITask::radioNameDone(bool ok) {
  using namespace radiox;
  if (!ok || !_nav_ta || !_prefs) { navClosePopup(); return; }
  char name[32];
  snprintf(name, sizeof(name), "%s", lv_textarea_get_text(_nav_ta));
  if (!name[0]) { showToast("Name can't be empty"); return; }
  const char* toast = nullptr;
  if (s_naming == N_PRESET) {
    NodePrefs* p = _prefs;
    radioctl::saveUserPreset(p, name, p->freq, p->bw, p->sf, p->cr);
    prefsSave();
    toast = "Preset saved";
  } else if (s_naming == N_SCOPE_ADD) {
    toast = the_mesh.addScope(name) ? "Scope added" : "The list is full";
  } else {
    the_mesh.renameScope((uint8_t)s_scope, name);
    toast = "Scope renamed";
  }
  navClosePopup();
  if (_screen == SCR_RADIO) rebuildRadio();
  else if (_screen == SCR_SCOPES) buildScopes();
  showToast(toast);
}

// ── Scopes ────────────────────────────────────────────────────────────────────

void UITask::showScopes() {
  _screen = SCR_SCOPES;
  buildScopes();
}

void UITask::buildScopes() {
  lv_obj_t* body = newScreen("Scopes", true);
  const ScopeList& sl = the_mesh.scopeList();
  if (_header && sl.count < ScopeList::MAX_SCOPE_ENTRIES) headerButton(_header, LV_SYMBOL_PLUS " Add", onScopeAdd, 4, NULL);
  lv_obj_t* g = group(body, nullptr);
  for (uint8_t i = 0; i <= sl.count; i++) {
    bool def = i == sl.default_idx;
    const char* sub = def ? "Default" : i == 0 ? "Reaches every repeater" : nullptr;
    lv_obj_t* row = listRow(g, i == 0 ? "*  (no scope)" : sl.name(i), sub, onScopeRow, (void*)(uintptr_t)i);
    if (def) rowCheck(row);
  }
  groupNote(body, "A scope keeps messages within a region's repeaters. The default is used for direct messages "
                  "and relaying; each channel picks its own in its options.");
}

void UITask::scopeMenu(int idx) {
  const ScopeList& sl = the_mesh.scopeList();
  if (idx < 0 || idx > sl.count) return;
  radiox::s_scope = idx;
  bool def = idx == sl.default_idx;
  lv_obj_t* panel = navPopupPanel(idx == 0 ? "* (no scope)" : sl.name((uint8_t)idx), false);
  if (def) noteLabel(panel, "This is the default: direct messages and relaying use it.");
  if (idx == 0 && def) return;   // nothing else to do with "*"
  lv_obj_t* acts = buttonBar(panel);
  if (!def) barButton(acts, LV_SYMBOL_OK " Default", onScopeAction, radiox::S_DEFAULT, true);
  if (idx > 0) {
    barButton(acts, LV_SYMBOL_EDIT " Rename", onScopeAction, radiox::S_RENAME, false);
    barButton(acts, LV_SYMBOL_TRASH " Delete", onScopeAction, radiox::S_DELETE, false);
  }
}

void UITask::scopeAction(uint8_t act) {
  using namespace radiox;
  const ScopeList& sl = the_mesh.scopeList();
  if (s_scope < 0 || s_scope > sl.count) { navClosePopup(); return; }
  switch (act) {
    case S_DEFAULT:
      the_mesh.setDefaultScope((uint8_t)s_scope);
      navClosePopup();
      buildScopes();
      showToast("Default for direct messages and relaying");
      break;
    case S_RENAME:
      if (s_scope > 0) radioNamePopup(N_SCOPE_RENAME);
      break;
    case S_DELETE: {
      if (s_scope == 0) break;
      char t[40];
      snprintf(t, sizeof(t), "Delete %s?", sl.name((uint8_t)s_scope));
      confirmBody(navPopupPanel(t, false),
                  "Channels using it go back to no scope. If it is the default, * becomes the default.",
                  LV_SYMBOL_TRASH "  Delete", onScopeAction, S_DELETE_GO);
      break;
    }
    case S_DELETE_GO:
      if (s_scope > 0) the_mesh.removeScope((uint8_t)s_scope);
      navClosePopup();
      buildScopes();
      break;
  }
}
