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

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include "icons.h"
#include "RadioParamsEditor.h"
#include "RadioPresetPicker.h"
#include "../RadioPresets.h"
#include "../MyMesh.h"

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
  bool    _editing_scope;   // keyboard is entering/editing the extra scopes
  int     _item_count;

  RadioPresetPicker _picker;
  RadioParamsEditor _editor;

  // The dedicated repeater profile's fields, as the shared preset picker's target
  // (Settings › Radio points the same picker at the companion's own params).
  RadioPresetPicker::Target rptTarget(NodePrefs* p) const {
    return { &p->repeater_freq, &p->repeater_bw, &p->repeater_sf, &p->repeater_cr };
  }

  void buildItems(NodePrefs* p) {
    _item_count = 0;
    _items[_item_count++] = IT_REPEATER;
    if (p && p->client_repeat) {
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

  static const char* itemLabel(int item) {
    switch (item) {
      case IT_REPEATER: return "Repeater";
      case IT_NETWORK:  return "Network";
      case IT_RPRESET:  return "Rpt preset";
      case IT_RFREQ:    return "Rpt freq";
      case IT_RSF:      return "Rpt SF";
      case IT_RBW:      return "Rpt BW";
      case IT_RCR:      return "Rpt CR";
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
      case IT_RBW:      snprintf(buf, n, "%.1f", p->repeater_bw); break;
      case IT_RCR:      snprintf(buf, n, "%d", (int)p->repeater_cr); break;
      case IT_SKIP:     strncpy(buf, p->repeat_skip_adverts ? "ON" : "OFF", n); break;
      case IT_HOPS:
        if (p->repeat_max_hops > 0) snprintf(buf, n, "%d", (int)p->repeat_max_hops);
        else strncpy(buf, "OFF", n);
        break;
      case IT_YIELD:
        if (p->repeat_delay_boost > 0) snprintf(buf, n, "x%d", (int)p->repeat_delay_boost + 1);
        else strncpy(buf, "OFF", n);
        break;
      case IT_SNR:
        if (p->repeat_min_snr != NodePrefs::REPEAT_SNR_DISABLED) snprintf(buf, n, "%ddB", (int)p->repeat_min_snr);
        else strncpy(buf, "OFF", n);
        break;
      case IT_SUPPRESS: strncpy(buf, p->repeat_suppress_dup ? "ON" : "OFF", n); break;
      case IT_SCOPE:    strncpy(buf, p->repeat_scope_only ? "ON" : "OFF", n); break;
      case IT_SCOPE_EXTRA:
        strncpy(buf, p->repeat_extra_scopes[0] ? p->repeat_extra_scopes : "(none)", n);
        break;
      default: strncpy(buf, "", n); break;
    }
    buf[n - 1] = '\0';
  }

  // Seed the profile from the companion's current params (used when first
  // switching to Custom, so the starting point is a valid, familiar config).
  void seedProfileFromCompanion(NodePrefs* p) {
    p->repeater_freq = p->freq; p->repeater_bw = p->bw;
    p->repeater_sf = p->sf;     p->repeater_cr = p->cr;
  }

public:
  RepeaterScreen(UITask* task) : _task(task), _dirty(false), _sel(0), _scroll(0), _item_count(1), _editing_scope(false) {}

  void onShow() override {
    _dirty = false; _sel = 0; _scroll = 0;
    _picker.menu.active = false; _editor.freq.active = false;
    _picker.saving = false; _picker.deleting = false;
    _editing_scope = false;
  }

  int render(DisplayDriver& display) override {
    if (_picker.saving || _editing_scope) return _task->keyboard().render(display);

    NodePrefs* p = _task->getNodePrefs();
    buildItems(p);
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawCenteredHeader("REPEATER");

    // Config only — live forwarding stats live on Tools › Diagnostics.
    drawList(display, _item_count, _sel, _scroll, [&](int row, int y, bool sel, int reserve) {
      int item = _items[row];
      drawRowSelection(display, y, sel, reserve);
      display.setCursor(2, y);
      display.print(itemLabel(item));
      if (item == IT_RFREQ && sel && _editor.active()) {
        _editor.render(display, display.valCol(), y);
      } else {
        char val[16];
        itemValue(item, p, val, sizeof(val));
        display.drawTextRightAlign(display.width() - reserve - 2, y, val);
      }
      display.setColor(DisplayDriver::LIGHT);
    });
    display.setColor(DisplayDriver::LIGHT);
    if (_picker.menu.active) _picker.menu.render(display);
    return (_picker.menu.active || _editor.active()) ? 50 : 500;
  }

  bool handleInput(char c) override {
    NodePrefs* p = _task->getNodePrefs();

    // Keyboard editing mode for naming a new saved preset
    if (_picker.saving) {
      auto res = _task->keyboard().handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (_picker.save(p, _task->keyboard().buf, rptTarget(p))) {
          _dirty = true;
          _task->showAlert("Preset saved", 800);
        }
        _picker.saving = false;
      } else if (res == KeyboardWidget::CANCELLED) {
        _picker.saving = false;
      }
      return true;
    }

    // Keyboard editing mode for the extra (relay-only) scopes
    if (_editing_scope) {
      auto res = _task->keyboard().handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (p) {
          strncpy(p->repeat_extra_scopes, _task->keyboard().buf, sizeof(p->repeat_extra_scopes) - 1);
          p->repeat_extra_scopes[sizeof(p->repeat_extra_scopes) - 1] = '\0';
          the_mesh.rebuildRepeatScopes();
          _dirty = true;
        }
        _editing_scope = false;
      } else if (res == KeyboardWidget::CANCELLED) {
        _editing_scope = false;
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
            the_mesh.applyRepeaterRadio();   // live if currently relaying on the profile
            _dirty = true;
            break;
          case RadioPresetPicker::DELETED:
            _dirty = true;
            _task->showAlert("Preset deleted", 800);
            break;
          case RadioPresetPicker::NONE:
            break;
        }
      } else if (res == PopupMenu::CANCELLED) {
        _picker.deleting = false;
      }
      return true;
    }
    if (_editor.active()) {
      if (_editor.handleFreqInput(c) && p) { the_mesh.applyRepeaterRadio(); _dirty = true; }
      return true;
    }

    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) {
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
      p->client_repeat ^= 1;
      the_mesh.applyRepeaterRadio();   // switch to profile on enable / restore companion on disable
      _task->applyPowerSave();         // duty-cycle RX is forced off while repeating
      _task->applyApc();               // pin TX power to the ceiling while repeating (APC suppressed)
      buildItems(p);
      _dirty = true;
      return true;
    }
    if (item == IT_NETWORK && (left || right || enter)) {
      p->repeater_use_profile ^= 1;
      if (p->repeater_use_profile && !the_mesh.repeaterProfileValid())
        seedProfileFromCompanion(p);   // start Custom from a valid, familiar config
      the_mesh.applyRepeaterRadio();
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
    if (item == IT_RSF && dir && RadioParamsEditor::stepSF(p->repeater_sf, dir)) { the_mesh.applyRepeaterRadio(); _dirty = true; return true; }
    if (item == IT_RBW && dir && RadioParamsEditor::stepBW(p->repeater_bw, dir)) { the_mesh.applyRepeaterRadio(); _dirty = true; return true; }
    if (item == IT_RCR && dir && RadioParamsEditor::stepCR(p->repeater_cr, dir)) { the_mesh.applyRepeaterRadio(); _dirty = true; return true; }
    if (item == IT_SKIP && (left || right || enter)) {
      p->repeat_skip_adverts ^= 1; _dirty = true; return true;
    }
    if (item == IT_HOPS) {
      if (right && p->repeat_max_hops < 8) { p->repeat_max_hops++; _dirty = true; return true; }
      if (left  && p->repeat_max_hops > 0) { p->repeat_max_hops--; _dirty = true; return true; }
    }
    if (item == IT_YIELD) {
      if (right && p->repeat_delay_boost < 8) { p->repeat_delay_boost++; _dirty = true; return true; }
      if (left  && p->repeat_delay_boost > 0) { p->repeat_delay_boost--; _dirty = true; return true; }
    }
    if (item == IT_SNR) {
      int8_t v = p->repeat_min_snr;
      if (right) {
        v = (v == NodePrefs::REPEAT_SNR_DISABLED) ? -20 : (v < 10 ? v + 1 : 10);
        p->repeat_min_snr = v; _dirty = true; return true;
      }
      if (left) {
        if (v != NodePrefs::REPEAT_SNR_DISABLED)
          p->repeat_min_snr = (v <= -20) ? NodePrefs::REPEAT_SNR_DISABLED : (int8_t)(v - 1);
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
      _task->keyboard().begin(p->repeat_extra_scopes, (int)sizeof(p->repeat_extra_scopes) - 1);
      _editing_scope = true;
      return true;
    }
    return false;
  }
};
