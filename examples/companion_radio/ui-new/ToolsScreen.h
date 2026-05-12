#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp just before HomeScreen.

class ToolsScreen : public UIScreen {
  UITask* _task;
  int _sel;

  static const int ITEM_COUNT = 2;
  static const char* ITEMS[ITEM_COUNT];

public:
  ToolsScreen(UITask* task) : _task(task), _sel(0) {}

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 0, "TOOLS");
    display.fillRect(0, 10, display.width(), 1);

    for (int i = 0; i < ITEM_COUNT; i++) {
      int y = 12 + i * 12;
      bool sel = (i == _sel);
      if (sel) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y - 1, display.width(), 11);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(0, y);
      display.print(sel ? ">" : " ");
      display.setCursor(8, y);
      display.print(ITEMS[i]);
    }
    display.setColor(DisplayDriver::LIGHT);
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_UP   && _sel > 0) { _sel--; return true; }
    if (c == KEY_DOWN && _sel < ITEM_COUNT - 1) { _sel++; return true; }
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) { _task->gotoHomeScreen(); return true; }
    if (c == KEY_ENTER) {
      if (_sel == 0) { _task->gotoRingtoneEditor(); return true; }
      if (_sel == 1) { _task->gotoBotScreen();      return true; }
    }
    return false;
  }
};
const char* ToolsScreen::ITEMS[2] = { "Ringtone Editor", "Auto-Reply Bot" };
