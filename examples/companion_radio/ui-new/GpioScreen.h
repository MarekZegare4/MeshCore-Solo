#pragma once
// Tools › GPIO — manual control for the 4 user-assignable pins (see
// PIN_GPIO1..4 in the board's variant header). Board-specific: this whole
// file only compiles where PIN_GPIO1 is defined (currently Wio Tracker L1).
// Mirrors the bot's !gpio1..!gpio4 commands (MyMeshBot.h) — both paths go
// through UITask::setGpioMode()/botGetGPIO()/botGetGPIOAnalog(), so UI and
// bot state never disagree. Only GPIO1/GPIO2 (the nRF52840's AIN0/AIN5)
// offer the Analog mode step in the cycle -- GPIO3/GPIO4 have no ADC
// channel (see UITask::gpioSupportsAnalog).
// Included by UITask.cpp.
#if defined(PIN_GPIO1)

#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include "icons.h"
#include "../NodePrefs.h"

class GpioScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  int        _sel;
  int        _scroll;

  static const int PIN_COUNT = 4;
  enum Kind : uint8_t { ROW_MODE, ROW_STATE };
  struct Row { uint8_t pin; Kind kind; };   // pin is 0-based here, 1-based for UITask/bot calls
  Row _items[PIN_COUNT * 2];
  int _item_count;

  // Direction (Off/Input/Output) and on/off state are separate concerns, so
  // they're separate rows: a Mode row per pin, always shown, plus a State
  // row right after it that only appears once that pin's Mode is Output —
  // Input has nothing to toggle (its live level is read-only, shown inline
  // on the Mode row instead).
  void buildItems() {
    _item_count = 0;
    for (uint8_t i = 0; i < PIN_COUNT; i++) {
      _items[_item_count++] = { i, ROW_MODE };
      uint8_t m = gpioModeOf(i);
      if (m == 2 || m == 3) _items[_item_count++] = { i, ROW_STATE };   // Output only -- Analog has nothing to toggle
    }
    if (_sel >= _item_count) _sel = _item_count - 1;
    if (_sel < 0) _sel = 0;
  }

  uint8_t gpioModeOf(int idx) const {
    if (!_prefs) return 0;
    switch (idx) {
      case 0: return _prefs->gpio1_mode;
      case 1: return _prefs->gpio2_mode;
      case 2: return _prefs->gpio3_mode;
      default: return _prefs->gpio4_mode;
    }
  }

  void modeRowValue(int pin, char* buf, size_t n) const {
    switch (gpioModeOf(pin)) {
      case 1: {
        bool is_out = false, val = false;
        if (_task->botGetGPIO(pin + 1, is_out, val)) strncpy(buf, val ? "Input (High)" : "Input (Low)", n);
        else strncpy(buf, "Input", n);
        break;
      }
      case 2:
      case 3: strncpy(buf, "Output", n); break;
      case 4: {
        int mv = 0;
        if (_task->botGetGPIOAnalog(pin + 1, mv)) snprintf(buf, n, "%dmV", mv);
        else strncpy(buf, "Analog", n);
        break;
      }
      default: strncpy(buf, "OFF", n); break;
    }
    buf[n - 1] = '\0';
  }

  // OFF -> Input -> Output (starts OFF) -> [Analog, GPIO1/GPIO2 only] -> OFF
  // ... one press per step. Output's ON/OFF split lives on the State row
  // below, not in this cycle; Analog is read-only so it has no State row.
  void cycleMode(int pin) {
    uint8_t mode = gpioModeOf(pin);
    bool analog_ok = _task->gpioSupportsAnalog(pin + 1);
    uint8_t next;
    if      (mode == 0)             next = 1;
    else if (mode == 1)             next = 2;
    else if (mode == 2 || mode == 3) next = analog_ok ? 4 : 0;
    else                             next = 0;   // was Analog -> back to OFF
    _task->setGpioMode(pin + 1, next);
    const char* name = (next == 0) ? "OFF" : (next == 1) ? "Input" : (next == 2) ? "Output" : "Analog";
    char msg[24];
    snprintf(msg, sizeof(msg), "GPIO%d: %s", pin + 1, name);
    _task->showAlert(msg, 800);
    buildItems();
  }

  void toggleState(int pin) {
    bool on = (gpioModeOf(pin) == 3);
    _task->setGpioMode(pin + 1, on ? 2 : 3);
    char msg[24];
    snprintf(msg, sizeof(msg), "GPIO%d: %s", pin + 1, on ? "OFF" : "ON");
    _task->showAlert(msg, 800);
  }

public:
  GpioScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs), _sel(0), _scroll(0), _item_count(1) {}

  void onShow() override { _sel = 0; _scroll = 0; buildItems(); }

  int render(DisplayDriver& display) override {
    buildItems();
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawCenteredHeader("GPIO");

    bool any_live = false;
    drawList(display, _item_count, _sel, _scroll, [&](int row, int y, bool sel, int reserve) {
      const Row& it = _items[row];
      drawRowSelection(display, y, sel, reserve);
      display.setCursor(2, y);
      char val[16];
      if (it.kind == ROW_MODE) {
        char lbl[8];
        snprintf(lbl, sizeof(lbl), "GPIO%d", it.pin + 1);
        display.print(lbl);
        modeRowValue(it.pin, val, sizeof(val));
        uint8_t m = gpioModeOf(it.pin);
        if (m == 1 || m == 4) any_live = true;   // Input / Analog need a live-refreshed reading
      } else {
        display.print("  State");   // indented: reads as GPIOn's sub-row
        strncpy(val, gpioModeOf(it.pin) == 3 ? "ON" : "OFF", sizeof(val));
      }
      display.drawTextRightAlign(display.width() - reserve - 2, y, val);
      display.setColor(DisplayDriver::LIGHT);
    });
    return any_live ? 500 : 2000;   // faster refresh only while a row needs a live reading
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) { _task->gotoToolsScreen(); return true; }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : _item_count - 1; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < _item_count - 1) ? _sel + 1 : 0; return true; }
    if (!_prefs) return false;
    if (c == KEY_ENTER || keyIsNext(c) || keyIsPrev(c)) {
      const Row& it = _items[_sel];
      if (it.kind == ROW_MODE) cycleMode(it.pin);
      else                     toggleState(it.pin);
      return true;
    }
    return false;
  }
};

#endif // PIN_GPIO1
