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

  void enter() { _dirty = false; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 0, "AUTO-ADVERT");
    display.fillRect(0, 10, display.width(), 1);

    display.setCursor(2, 14);
    display.print("Interval:");

    int idx = currentIdx();
    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 24, display.width(), 12);
    display.setColor(DisplayDriver::DARK);
    display.drawTextCentered(display.width() / 2, 25, OPT_LABELS[idx]);
    display.setColor(DisplayDriver::LIGHT);

    display.setCursor(2, 40);
    display.print("<  > to change");
    display.setCursor(2, 51);
    display.print("[Esc] to save");
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) {
      if (_dirty) the_mesh.savePrefs();
      _task->gotoToolsScreen();
      return true;
    }
    bool right = (c == KEY_RIGHT || c == KEY_NEXT || c == KEY_ENTER);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
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
const char*    AutoAdvertScreen::OPT_LABELS[AutoAdvertScreen::OPT_COUNT] = { "off", "30s", "1min", "2min", "5min", "10min", "30min", "1h" };
