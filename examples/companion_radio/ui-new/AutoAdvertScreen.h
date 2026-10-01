#pragma once
// Configures periodic automatic 0-hop advert with GPS position.
// Included by UITask.cpp after DashboardConfigScreen.h.

class AutoAdvertScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  bool       _dirty;

  static const int OPT_COUNT = 8;
  static const uint32_t OPTS[OPT_COUNT];
  static const char*    OPT_LABELS[OPT_COUNT];

  int currentIdx() const {
    for (int i = 0; i < OPT_COUNT; i++)
      if (OPTS[i] == _prefs->advert_auto_interval_sec) return i;
    return 0;
  }

public:
  AutoAdvertScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void onShow() override { _dirty = false; }

  // One value row, drawn like every other settings list (LEFT/RIGHT or Enter
  // cycles, Back saves) instead of a screen of its own with typed hints.
  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawCenteredHeader("AUTO-ADVERT");
    const int valx = display.width() / 2 + 6;
    int sel = 0, scroll = 0;
    drawList(display, 1, sel, scroll, [&](int, int y, bool is_sel, int reserve) {
      drawRowSelection(display, y, is_sel, reserve);
      display.setCursor(4, y);
      display.print("Interval");
      display.setCursor(valx, y);
      display.print(OPT_LABELS[currentIdx()]);
    });
    return 5000;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);
      _task->gotoToolsScreen();
      return true;
    }
    bool right = keyIsNext(c) || c == KEY_ENTER;
    bool left  = keyIsPrev(c);
    if (right || left) {
      int idx = currentIdx();
      idx = right ? (idx + 1) % OPT_COUNT : (idx + OPT_COUNT - 1) % OPT_COUNT;
      _prefs->advert_auto_interval_sec = OPTS[idx];
      _dirty = true;
      return true;
    }
    return false;
  }
};

const uint32_t AutoAdvertScreen::OPTS[AutoAdvertScreen::OPT_COUNT]       = { 0, 30, 60, 120, 300, 600, 1800, 3600 };
const char*    AutoAdvertScreen::OPT_LABELS[AutoAdvertScreen::OPT_COUNT] = { "OFF", "30s", "1min", "2min", "5min", "10min", "30min", "1h" };
