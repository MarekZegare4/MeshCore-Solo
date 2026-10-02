#pragma once
// Tools › Repeater — consolidates the repeater toggle, its flood forwarding
// filters, and an optional dedicated radio profile on one screen. A dedicated
// screen (vs. the old Settings › Radio sub-items) gives full-width rows, so the
// longer labels no longer collide with the value column. Live forwarding stats
// live separately on Tools › Diagnostics.
//
// Network modes:
//  - Current  — repeat on the companion's current frequency. Opt-in only:
//               relaying on whatever network the operator is chatting on
//               isn't the MeshCore community norm.
//  - Custom   — enabling the repeater switches the radio to a dedicated profile
//               (preset or manual), and disabling restores the companion params.
//               Default for a never-configured device, seeded with a frequency
//               in the same band as the companion's own network (so the default
//               can't land outside what's legal for the operator's region).
// Included by UITask.cpp.

#include "InfoKit.h"
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include "icons.h"
#include "RadioParamsEditor.h"
#include "RadioPresetPicker.h"
#include "../RadioPresets.h"
#include "../MyMesh.h"
#include "PopupMenu.h"
#include "../ui-core/RepeaterControl.h"   // toggles, profile, filter ranges / labels, extra scopes

extern MyMesh the_mesh;

class RepeaterScreen : public UIScreen {
  UITask* _task;
  bool    _dirty;
  int     _sel;       // index into the interactive items currently shown
  int     _scroll;    // first visible row (render keeps _sel in view)

  enum Item {
    IT_REPEATER, IT_NETWORK,
    IT_RPRESET, IT_RFREQ, IT_RSF, IT_RBW, IT_RCR,   // dedicated profile (Custom network)
    IT_SKIP, IT_HOPS, IT_YIELD, IT_SNR, IT_SUPPRESS, IT_SCOPE, IT_SCOPE_EXTRA
  };
  uint8_t _items[14];
  bool    _scope_picker_active = false;   // multi-select popup over the extra scopes
  PopupMenu _scope_menu;
  int     _item_count;

  RadioPresetPicker _picker;
  RadioParamsEditor _editor;

  // The dedicated repeater profile's fields, as the shared preset picker's target
  // (Settings › Radio points the same picker at the companion's own params).
  RadioPresetPicker::Target rptTarget(NodePrefs* p) const {
    return { &p->repeater_freq, &p->repeater_bw, &p->repeater_sf, &p->repeater_cr };
  }

  // Shown regardless of the Repeater ON/OFF row itself -- these are plain
  // NodePrefs settings, no different from Bot's per-target rows or Live
  // Share's move/gap/heartbeat, so they stay configurable (and visible) while
  // off rather than forcing an enable/configure/maybe-disable-again dance.
  void buildItems(NodePrefs* p) {
    _item_count = 0;
    _items[_item_count++] = IT_REPEATER;
    if (p) {
      _items[_item_count++] = IT_NETWORK;
      if (p->repeater_use_profile) {
        _items[_item_count++] = IT_RPRESET;
        _items[_item_count++] = IT_RFREQ;
        _items[_item_count++] = IT_RSF;
        _items[_item_count++] = IT_RBW;
        _items[_item_count++] = IT_RCR;
      }
      _items[_item_count++] = IT_SKIP;
      _items[_item_count++] = IT_HOPS;
      _items[_item_count++] = IT_YIELD;
      _items[_item_count++] = IT_SNR;
      _items[_item_count++] = IT_SUPPRESS;
      _items[_item_count++] = IT_SCOPE;
      _items[_item_count++] = IT_SCOPE_EXTRA;
    }
    if (_sel >= _item_count) _sel = _item_count - 1;
    if (_sel < 0) _sel = 0;
  }

  // Freq/SF/BW/CR match Settings > Radio's own terminology exactly -- no "Rpt "
  // prefix needed since this whole screen is already the repeater's own
  // profile (Settings' radio screen is a separate screen for the companion's
  // own params), so the prefix was just noise, not disambiguation.
  static const char* itemLabel(int item) {
    switch (item) {
      case IT_REPEATER: return "Repeater";
      case IT_NETWORK:  return "Network";
      case IT_RPRESET:  return "Preset";
      case IT_RFREQ:    return "Freq";
      case IT_RSF:      return "SF";
      case IT_RBW:      return "BW";
      case IT_RCR:      return "CR";
      case IT_SKIP:     return "Skip advert";
      case IT_HOPS:     return "Max hops";
      case IT_YIELD:    return "Yield";
      case IT_SNR:      return "Min SNR";
      case IT_SUPPRESS:    return "Suppress dup";
      case IT_SCOPE:       return "Scope only";
      case IT_SCOPE_EXTRA: return "Extra scopes";
    }
    return "";
  }

  void itemValue(int item, NodePrefs* p, char* buf, size_t n) const {
    if (!p) { strncpy(buf, "OFF", n); buf[n-1]=0; return; }
    switch (item) {
      case IT_REPEATER: strncpy(buf, p->client_repeat ? "ON" : "OFF", n); break;
      case IT_NETWORK:  strncpy(buf, p->repeater_use_profile ? "Custom" : "Current", n); break;
      case IT_RPRESET:  strncpy(buf, _picker.currentName(p, rptTarget(p)), n); break;
      case IT_RFREQ:    snprintf(buf, n, "%.3f", p->repeater_freq); break;
      case IT_RSF:      snprintf(buf, n, "%d", (int)p->repeater_sf); break;
      case IT_RBW:      snprintf(buf, n, "%.1f kHz", p->repeater_bw); break;
      case IT_RCR:      snprintf(buf, n, "%d", (int)p->repeater_cr); break;
      case IT_SKIP:     strncpy(buf, p->repeat_skip_adverts ? "ON" : "OFF", n); break;
      case IT_HOPS:     rptctl::fmtHops(buf, n, p->repeat_max_hops); break;    // "Off": a value, not a switch
      case IT_YIELD:    rptctl::fmtYield(buf, n, p->repeat_delay_boost); break;
      case IT_SNR:      rptctl::fmtSnr(buf, n, p->repeat_min_snr); break;
      case IT_SUPPRESS: strncpy(buf, p->repeat_suppress_dup ? "ON" : "OFF", n); break;
      case IT_SCOPE:    strncpy(buf, p->repeat_scope_only ? "ON" : "OFF", n); break;
      case IT_SCOPE_EXTRA: {
        uint8_t total = the_mesh.scopeList().count;
        if (total == 0)   strncpy(buf, "(none)", n);
        else              snprintf(buf, n, "%d/%d", rptctl::extraScopesPicked(p), (int)total);
        break;
      }
      default: strncpy(buf, "", n); break;
    }
    buf[n - 1] = '\0';
  }

public:
  RepeaterScreen(UITask* task) : _task(task), _dirty(false), _sel(0), _scroll(0), _item_count(1) {}

  void onShow() override {
    _dirty = false; _sel = 0; _scroll = 0;
    _picker.menu.active = false; _editor.freq.active = false;
    _picker.saving = false; _picker.deleting = false; _picker.confirm_slot = -1;
    _scope_picker_active = false; _scope_menu.active = false;
  }

  int render(DisplayDriver& display) override {
    if (_picker.saving) return _task->keyboard().render(display);

    NodePrefs* p = _task->getNodePrefs();
    buildItems(p);
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Repeater");

    // Config only — live forwarding stats live on Status › Mesh.
    drawList(display, _item_count, _sel, _scroll, [&](int row, int y, bool sel, int reserve) {
      int item = _items[row];
      drawRowSelection(display, y, sel, reserve);
      if (item == IT_RFREQ && sel && _editor.active()) {   // the digit editor where the value stands
        display.setCursor(2, y);
        display.print(itemLabel(item));
        _editor.render(display, display.width() - reserve - 2 - display.getTextWidth("000.000"), y);
      } else {
        char val[24];
        itemValue(item, p, val, sizeof(val));
        info::listRow(display, y, itemLabel(item), val, sel, reserve);
      }
      display.setColor(DisplayDriver::LIGHT);
    });
    display.setColor(DisplayDriver::LIGHT);
    if (_picker.menu.active) _picker.menu.render(display);
    if (_scope_picker_active) _scope_menu.render(display);
    return (_picker.menu.active || _editor.active() || _scope_picker_active) ? 50 : 500;
  }

  bool handleInput(char c) override {
    NodePrefs* p = _task->getNodePrefs();

    // Keyboard editing mode for naming a new saved preset
    if (_picker.saving) {
      auto res = _task->keyboard().handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (_picker.save(p, _task->keyboard().buf, rptTarget(p))) {
          _dirty = true;
        }
        _picker.saving = false;
      } else if (res == KeyboardWidget::CANCELLED) {
        _picker.saving = false;
      }
      return true;
    }

    // Multi-select popup for the extra (relay-only) scopes -- each row is a
    // checklist item (see PopupMenu::addCheckItem()); Enter toggles it in
    // place via VALUE_NEXT and commits straight into prefs, no separate
    // "apply on close" step needed.
    if (_scope_picker_active) {
      auto res = _scope_menu.handleInput(c);
      if (res == PopupMenu::VALUE_NEXT && p) {
        int i = _scope_menu.selectedIndex();
        bool now_on = !_scope_menu.isChecked(i);
        _scope_menu.setChecked(i, now_on);
        rptctl::setExtraScope(p, (uint8_t)i, now_on);
        _dirty = true;
      } else if (res != PopupMenu::NONE && res != PopupMenu::VALUE_NEXT) {
        _scope_picker_active = false;   // Back closes it -- checklist has no plain-select row
      }
      return true;
    }

    // Modal overlays first.
    if (_picker.menu.active) {
      auto res = _picker.menu.handleInput(c);
      if (res == PopupMenu::SELECTED && p) {
        switch (_picker.onSelected(_picker.menu.selectedIndex(), p, rptTarget(p))) {
          case RadioPresetPicker::START_SAVE:
            _task->keyboard().begin("", (int)sizeof(p->user_radio_presets[0].name) - 1);
            _task->keyboard().clearPlaceholders();   // {loc}/{time} are for messages, not preset names
            break;
          case RadioPresetPicker::APPLIED:
            rptctl::applyProfile();   // live if currently relaying on the profile
            _dirty = true;
            break;
          case RadioPresetPicker::DELETED:
            _dirty = true;
            break;
          case RadioPresetPicker::NONE:
            break;
        }
      } else if (res == PopupMenu::CANCELLED) {
        _picker.deleting = false;
        _picker.confirm_slot = -1;
      }
      return true;
    }
    if (_editor.active()) {
      if (_editor.handleFreqInput(c) && p) { rptctl::applyProfile(); _dirty = true; }
      return true;
    }

    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : _item_count - 1; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < _item_count - 1) ? _sel + 1 : 0; return true; }
    if (!p) return false;

    bool right = keyIsNext(c);
    bool left  = keyIsPrev(c);
    bool enter = (c == KEY_ENTER);
    int item = _items[_sel];

    if (item == IT_REPEATER && (left || right || enter)) {
      rptctl::setEnabled(p, !p->client_repeat);   // radio profile, RX power save, APC follow
      buildItems(p);
      _dirty = true;
      return true;
    }
    if (item == IT_NETWORK && (left || right || enter)) {
      rptctl::setUseProfile(p, !p->repeater_use_profile);   // seeds a fresh profile from the companion's params
      buildItems(p);
      _dirty = true;
      return true;
    }
    if (item == IT_RPRESET && enter) { _picker.open(p, rptTarget(p), "Repeater Preset"); return true; }
    if (item == IT_RFREQ && enter) {
      float lo, hi; radio_driver.getFreqBounds(lo, hi);
      _editor.beginFreq(p->repeater_freq, lo, hi);
      return true;
    }
    int dir = right ? 1 : (left ? -1 : 0);
    if (item == IT_RSF && dir && RadioParamsEditor::stepSF(p->repeater_sf, dir)) { rptctl::applyProfile(); _dirty = true; return true; }
    if (item == IT_RBW && dir && RadioParamsEditor::stepBW(p->repeater_bw, dir)) { rptctl::applyProfile(); _dirty = true; return true; }
    if (item == IT_RCR && dir && RadioParamsEditor::stepCR(p->repeater_cr, dir)) { rptctl::applyProfile(); _dirty = true; return true; }
    if (item == IT_SKIP && (left || right || enter)) {
      p->repeat_skip_adverts ^= 1; _dirty = true; return true;
    }
    if (item == IT_HOPS) {
      if (right && p->repeat_max_hops < rptctl::MAX_HOPS) { p->repeat_max_hops++; _dirty = true; return true; }
      if (left  && p->repeat_max_hops > 0) { p->repeat_max_hops--; _dirty = true; return true; }
    }
    if (item == IT_YIELD) {
      if (right && p->repeat_delay_boost < rptctl::MAX_YIELD) { p->repeat_delay_boost++; _dirty = true; return true; }
      if (left  && p->repeat_delay_boost > 0) { p->repeat_delay_boost--; _dirty = true; return true; }
    }
    if (item == IT_SNR) {
      int8_t v = p->repeat_min_snr;
      if (right) {
        v = (v == NodePrefs::REPEAT_SNR_DISABLED) ? rptctl::SNR_MIN : (v < rptctl::SNR_MAX ? v + 1 : rptctl::SNR_MAX);
        p->repeat_min_snr = v; _dirty = true; return true;
      }
      if (left) {
        if (v != NodePrefs::REPEAT_SNR_DISABLED)
          p->repeat_min_snr = (v <= rptctl::SNR_MIN) ? NodePrefs::REPEAT_SNR_DISABLED : (int8_t)(v - 1);
        _dirty = true; return true;
      }
    }
    if (item == IT_SUPPRESS && (left || right || enter)) {
      p->repeat_suppress_dup ^= 1; _dirty = true; return true;
    }
    if (item == IT_SCOPE && (left || right || enter)) {
      p->repeat_scope_only ^= 1; _dirty = true; return true;
    }
    if (item == IT_SCOPE_EXTRA && enter) {
      const ScopeList& sl = the_mesh.scopeList();
      if (sl.count == 0) {
        _task->showAlert("No scopes defined", 1200);
        return true;
      }
      _scope_menu.begin("Extra scopes", sl.count < 4 ? sl.count : 4);
      for (uint8_t i = 0; i < sl.count; i++)
        _scope_menu.addCheckItem(sl.name((uint8_t)(i + 1)), (p->repeat_extra_scope_mask & (1u << i)) != 0);
      _scope_picker_active = true;
      return true;
    }
    return false;
  }
};
