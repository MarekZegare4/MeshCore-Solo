#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp just before HomeScreen.
//
// The flat 10-item list grew crowded, so tools are grouped into collapsible
// sections (Location / Comms / System) via the shared AccordionList helper —
// the same fold-in-place model as Settings. Rows are text only; section
// headers carry a fold mark.

#include "AccordionList.h"

class ToolsScreen : public UIScreen {
public:
  enum Action {
    ACT_NEARBY, ACT_LIVESHARE, ACT_TRAIL, ACT_LOCATOR, ACT_COMPASS,
    ACT_BOT, ACT_AUTOADVERT, ACT_REPEATER, ACT_ADMIN,
    ACT_CLOCK, ACT_RINGTONE
#if defined(PIN_GPIO1)
    , ACT_GPIO
#endif
  };
  struct Tool { const char* label; Action action; };
  struct Section { const char* name; const Tool* tools; uint8_t count; };

  // The two tools used last, newest first, for the Home Tools panel. Kept in
  // RAM only: a reboot starts again from Nodes and Live Share.
  static const int RECENT_MAX = 2;
  static Action s_recent[RECENT_MAX];

  static const char* labelOf(Action a) {
    for (int i = 0; i < SECTION_COUNT; i++)
      for (int j = 0; j < SECTIONS[i].count; j++)
        if (SECTIONS[i].tools[j].action == a) return SECTIONS[i].tools[j].label;
    return "";
  }

  // Opens a tool and moves it to the front of the recent list.
  static void run(UITask* task, Action a) {
    if (s_recent[0] != a) { s_recent[1] = s_recent[0]; s_recent[0] = a; }
    switch (a) {
      case ACT_NEARBY:      task->gotoNearbyScreen();      break;
      case ACT_LIVESHARE:   task->gotoLiveShareScreen();   break;
      case ACT_TRAIL:       task->gotoTrailScreen();       break;
      case ACT_LOCATOR:     task->gotoLocatorScreen();     break;
      case ACT_COMPASS:     task->gotoCompassScreen();     break;
      case ACT_BOT:         task->gotoBotScreen();         break;
      case ACT_AUTOADVERT:  task->gotoAutoAdvertScreen();  break;
      case ACT_REPEATER:    task->gotoRepeaterScreen();    break;
      case ACT_ADMIN:       task->pickAdminTarget();       break;  // Admin is remote-only: pick a node first
      case ACT_CLOCK:       task->gotoClockTools();        break;
      case ACT_RINGTONE:    task->gotoRingtoneEditor();    break;
#if defined(PIN_GPIO1)
      case ACT_GPIO:        task->gotoGpioScreen();        break;
#endif
    }
  }

private:
  UITask* _task;

  static const Tool LOCATION_TOOLS[];
  static const Tool COMMS_TOOLS[];
  static const Tool SYSTEM_TOOLS[];
  static const Section SECTIONS[];
  static const int SECTION_COUNT = 3;

  AccordionList _acc;

public:
  ToolsScreen(UITask* task) : _task(task) {}

  // Open folded at the section list each time Tools is entered from Home.
  void onShow() override {
    static uint8_t sizes[SECTION_COUNT];
    for (int i = 0; i < SECTION_COUNT; i++) sizes[i] = SECTIONS[i].count;
    _acc.begin(sizes, SECTION_COUNT);
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    drawScreenHeader(display, "Tools");

    const int cw = display.getCharWidth();

    _acc.render(display,
      // Section header: disclosure mark + name
      [&](int sec, int y, bool sel, int reserve, bool collapsed) {
        drawRowSelection(display, y, sel, reserve);
        drawDisclosure(display, 2, y, !collapsed);
        display.setCursor(2 + 2 * cw, y);   // after the disclosure mark, as in Settings
        display.print(SECTIONS[sec].name);
      },
      // Item: its label, under its section's name
      [&](int sec, int item, int y, bool sel, int reserve) {
        drawRowSelection(display, y, sel, reserve);
        display.setCursor(2 + 2 * cw, y);
        display.print(SECTIONS[sec].tools[item].label);
      });
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
    switch (_acc.handleInput(c)) {
      case AccordionList::ACTIVATED: {
        const AccordionList::Row& r = _acc.selected();
        run(_task, SECTIONS[r.sec].tools[r.item].action);
        return true;
      }
      case AccordionList::HANDLED:  return true;
      case AccordionList::IGNORED:  return false;
    }
    return false;
  }
};

ToolsScreen::Action ToolsScreen::s_recent[ToolsScreen::RECENT_MAX] = { ACT_NEARBY, ACT_LIVESHARE };

const ToolsScreen::Tool ToolsScreen::LOCATION_TOOLS[] = {
  { "Nodes",           ACT_NEARBY },
  { "Live Share",      ACT_LIVESHARE },
  { "Trail",           ACT_TRAIL },
  { "Locator",         ACT_LOCATOR },
  { "Compass",         ACT_COMPASS },
};
const ToolsScreen::Tool ToolsScreen::COMMS_TOOLS[] = {
  { "Remote Bot",      ACT_BOT },
  { "Auto-Advert",     ACT_AUTOADVERT },
  { "Repeater",        ACT_REPEATER },
  { "Admin",           ACT_ADMIN },
};
const ToolsScreen::Tool ToolsScreen::SYSTEM_TOOLS[] = {
  { "Clock Tools",     ACT_CLOCK },
  { "Ringtone Editor", ACT_RINGTONE },
#if defined(PIN_GPIO1)
  { "GPIO",            ACT_GPIO },
#endif
};
const ToolsScreen::Section ToolsScreen::SECTIONS[] = {
  { "Location", LOCATION_TOOLS, (uint8_t)(sizeof(LOCATION_TOOLS)/sizeof(LOCATION_TOOLS[0])) },
  { "Comms",    COMMS_TOOLS,    4 },
  { "System",   SYSTEM_TOOLS,   (uint8_t)(sizeof(SYSTEM_TOOLS)/sizeof(SYSTEM_TOOLS[0])) },
};
