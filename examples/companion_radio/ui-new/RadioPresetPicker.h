#pragma once
#include <string.h>
#include "PopupMenu.h"
#include "../RadioPresets.h"
#include "../NodePrefs.h"
#include "../ui-core/RadioControl.h"   // saving / deleting user presets

// Shared "radio preset" picker used by both Settings › Radio and Tools ›
// Repeater. Both edit the same 4 user preset slots in NodePrefs and build the
// same popup layout; they differ only in which freq/bw/sf/cr fields they target
// (the companion's own params vs. the dedicated repeater profile) and in what
// runs when a value is applied. This struct owns the popup + save/delete state
// and the lookup/save logic; the owning screen passes a Target pointing at its
// fields and handles apply()/dirty/keyboard/alert on the Result it returns.
//
// Popup layout (built by open()):
//   [0]                       = "+ Save current..."
//   [1 .. RADIO_PRESET_COUNT] = built-in presets
//   [.. + user_count]         = saved user presets (empty slots skipped)
//   [last, if user_count > 0] = "- Delete preset..."
struct RadioPresetPicker {
  // The four radio fields this picker reads and writes.
  struct Target {
    float*   freq;
    float*   bw;
    uint8_t* sf;
    uint8_t* cr;
  };

  // What the owning screen should do after a SELECTED item is handled.
  enum Result {
    NONE,        // nothing to do (sub-menu opened, or a no-op row)
    START_SAVE,  // open the keyboard to name a new preset, then call save()
    APPLIED,     // target fields changed — apply to the radio + mark dirty
    DELETED      // a saved preset was removed — mark dirty + alert
  };

  PopupMenu menu;
  uint8_t   user_slot[NodePrefs::USER_RADIO_PRESET_MAX];   // popup row → NodePrefs slot
  int       user_count = 0;
  bool      saving   = false;   // keyboard is open to name a new preset
  bool      deleting = false;   // menu is showing the delete sub-list
  int       confirm_slot = -1;  // >=0 = confirming deletion of this NodePrefs slot

  static bool matches(const Target& t, float freq, float bw, uint8_t sf, uint8_t cr) {
    return radioParamsMatchPreset(*t.freq, *t.bw, *t.sf, *t.cr, freq, bw, sf, cr);
  }

  // Display name for the target: a built-in preset, a saved user preset, or
  // "Custom" if the params don't exactly match any of them.
  const char* currentName(NodePrefs* p, const Target& t) const {
    if (!p) return "Custom";
    for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
      const RadioPreset& r = RADIO_PRESETS[i];
      if (matches(t, r.freq, r.bw, r.sf, r.cr)) return r.name;
    }
    for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++) {
      const auto& u = p->user_radio_presets[i];
      if (u.name[0] && matches(t, u.freq, u.bw, u.sf, u.cr)) return u.name;
    }
    return "Custom";
  }

  // Position of the target within the popup list built by open(), or -1
  // ("Custom") if nothing matches.
  int currentListIndex(NodePrefs* p, const Target& t) const {
    if (!p) return -1;
    for (int i = 0; i < RADIO_PRESET_COUNT; i++) {
      const RadioPreset& r = RADIO_PRESETS[i];
      if (matches(t, r.freq, r.bw, r.sf, r.cr)) return i + 1;
    }
    int pos = RADIO_PRESET_COUNT + 1;
    for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++) {
      const auto& u = p->user_radio_presets[i];
      if (!u.name[0]) continue;
      if (matches(t, u.freq, u.bw, u.sf, u.cr)) return pos;
      pos++;
    }
    return -1;
  }

  // Save the target's params as a named user preset (slot choice in
  // radioctl::saveUserPreset). Returns true if anything was written (caller
  // alerts + marks dirty).
  bool save(NodePrefs* p, const char* name, const Target& t) {
    return radioctl::saveUserPreset(p, name, *t.freq, *t.bw, *t.sf, *t.cr);
  }

  void open(NodePrefs* p, const Target& t, const char* title) {
    deleting = false;
    menu.begin(title, 6);
    menu.addItem("+ Save current...");
    for (int i = 0; i < RADIO_PRESET_COUNT; i++) menu.addItem(RADIO_PRESETS[i].name);
    user_count = 0;
    if (p) {
      for (int i = 0; i < NodePrefs::USER_RADIO_PRESET_MAX; i++) {
        if (!p->user_radio_presets[i].name[0]) continue;
        menu.addItem(p->user_radio_presets[i].name);
        user_slot[user_count++] = (uint8_t)i;
      }
    }
    if (user_count > 0) menu.addItem("- Delete preset...");
    int idx = currentListIndex(p, t);
    menu.setSelected(idx >= 0 ? idx : 0);
  }

  // Reuses `menu` for a second-level list of just the saved user presets (their
  // slot mapping in user_slot is still the one open() built, since this is only
  // reached from within that same popup).
  void openDelete(NodePrefs* p) {
    menu.begin("Delete Preset", 6);
    for (int i = 0; i < user_count; i++)
      menu.addItem(p->user_radio_presets[user_slot[i]].name);
    deleting = true;
  }

  // The menu closed with Back: out of the delete list / confirmation.
  void cancel() { deleting = false; confirm_slot = -1; }

  // Third level, reached by picking a name off the delete sub-list: a plain
  // Delete/Cancel confirm, defaulting to Cancel like every other destructive
  // action's popup (see NearbyScreen's contact-delete confirm). Whichever row
  // is picked, onSelected() below is terminal -- the whole picker closes,
  // same as it already does after a built-in/user preset pick.
  void openConfirm(uint8_t slot) {
    menu.beginConfirm("Delete preset?", "Delete");
    confirm_slot = slot;
  }

  // Handle the index the popup reports as SELECTED. Mutates target fields on a
  // built-in/user pick; opens the delete confirm from the delete sub-list, or
  // resolves that confirm if it's the level currently showing. See Result.
  Result onSelected(int idx, NodePrefs* p, const Target& t) {
    if (!p) return NONE;
    if (confirm_slot >= 0) {
      Result r = NONE;
      if (idx == 0) {   // "Delete"
        radioctl::deleteUserPreset(p, confirm_slot);
        r = DELETED;
      }
      confirm_slot = -1;
      return r;
    }
    if (deleting) {
      if (idx >= 0 && idx < user_count) openConfirm(user_slot[idx]);
      deleting = false;
      return NONE;
    }
    const int builtin_base = 1;
    const int user_base    = builtin_base + RADIO_PRESET_COUNT;
    const int delete_idx   = user_base + user_count;
    if (idx == 0) {
      saving = true;
      return START_SAVE;
    }
    if (idx >= builtin_base && idx < user_base) {
      const RadioPreset& r = RADIO_PRESETS[idx - builtin_base];
      *t.freq = r.freq; *t.bw = r.bw; *t.sf = r.sf; *t.cr = r.cr;
      return APPLIED;
    }
    if (idx >= user_base && idx < delete_idx) {
      const NodePrefs::UserRadioPreset& u = p->user_radio_presets[user_slot[idx - user_base]];
      *t.freq = u.freq; *t.bw = u.bw; *t.sf = u.sf; *t.cr = u.cr;
      return APPLIED;
    }
    if (user_count > 0 && idx == delete_idx) {
      openDelete(p);
    }
    return NONE;
  }
};
