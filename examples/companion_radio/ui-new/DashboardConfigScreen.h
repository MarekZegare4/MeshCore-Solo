#pragma once
// Configures which data fields appear on the clock home page.
// Included by UITask.cpp after BotScreen.h.

// The fields and their names: ui-core/Telemetry.h.

class DashboardConfigScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  static const int FIELD_SLOTS = 3;

  int  _sel;
  bool _dirty;

  void cycle(int slot, int dir) {
    uint8_t& f = _prefs->dashboard_fields[slot];
    f = (uint8_t)((f + telemetry::COUNT + dir) % telemetry::COUNT);
    _dirty = true;
  }

public:
  DashboardConfigScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _sel = 0; _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    int item_h  = display.lineStep();
    int start_y = display.listStart();
    int val_x   = display.valCol();

    drawScreenHeader(display, "Clock fields");

    static const char* labels[] = { "Field 1", "Field 2", "Field 3" };
    for (int i = 0; i < FIELD_SLOTS; i++) {
      int y = start_y + i * item_h;
      bool sel = (i == _sel);
      display.drawSelectionRow(0, y - 1, display.width(), item_h, sel);
      display.setCursor(2, y);
      display.print(labels[i]);
      display.setCursor(val_x, y);
      uint8_t f = _prefs->dashboard_fields[i];
      display.print(telemetry::COMPACT[f < telemetry::COUNT ? f : telemetry::NONE]);
      display.setColor(DisplayDriver::LIGHT);
    }
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoHomeScreen();
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : FIELD_SLOTS - 1; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < FIELD_SLOTS - 1) ? _sel + 1 : 0; return true; }
    if (keyIsPrev(c))  { cycle(_sel, -1); return true; }
    if (keyIsNext(c))  { cycle(_sel,  1); return true; }
    if (c == KEY_ENTER)                   { cycle(_sel,  1); return true; }
    return false;
  }
};

