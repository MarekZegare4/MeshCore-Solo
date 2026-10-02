#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp just before HomeScreen.
//
// The flat 10-item list grew crowded, so tools are grouped into collapsible
// sections (Location / Comms / System) via the shared AccordionList helper —
// the same fold-in-place model as Settings. Each row carries a mini-icon;
// section headers show their cog/marker plus a fold indicator.

#include "AccordionList.h"

class ToolsScreen : public UIScreen {
  UITask* _task;

  enum Action {
    ACT_NEARBY, ACT_LIVESHARE, ACT_TRAIL, ACT_LOCATOR, ACT_COMPASS,
    ACT_BOT, ACT_AUTOADVERT, ACT_REPEATER, ACT_ADMIN,
    ACT_CLOCK, ACT_RINGTONE
#if defined(PIN_GPIO1)
    , ACT_GPIO
#endif
  };
  struct Tool { const char* label; const MiniIcon* icon; Action action; };
  struct Section { const char* name; const Tool* tools; uint8_t count; };

  static const Tool LOCATION_TOOLS[];
  static const Tool COMMS_TOOLS[];
  static const Tool SYSTEM_TOOLS[];
  static const Section SECTIONS[];
  static const int SECTION_COUNT = 3;

  AccordionList _acc;

  // Fixed icon gutter so labels line up whether or not a row has a glyph. Sized
  // for the widest icon (the 7px cog) at the current font scale.
  static int gutter(DisplayDriver& d) { return 7 * miniIconScale(d) + 2; }

  // Was hand-rolling its own vertical centring against lineStep() (line height
  // + the 2px inter-row gap) instead of just the line height itself -- 2px too
  // much slack pushed a nearly-full-height icon (e.g. the 7px cog) low enough
  // to clip against the row below, which is what "don't fit visually" meant.
  // miniIconDraw() (icons.h) already centres correctly against getLineHeight()
  // alone -- the same call drawAckGlyph uses for the message-list checkmark --
  // so just delegate to it instead of keeping a second, subtly-wrong copy.
  static void drawIcon(DisplayDriver& d, int x, int y, const MiniIcon* ic) {
    if (!ic) return;
    miniIconDraw(d, x, y, *ic);
  }

  void dispatch(Action a) {
    switch (a) {
      case ACT_NEARBY:      _task->gotoNearbyScreen();      break;
      case ACT_LIVESHARE:   _task->gotoLiveShareScreen();   break;
      case ACT_TRAIL:       _task->gotoTrailScreen();       break;
      case ACT_LOCATOR:     _task->gotoLocatorScreen();     break;
      case ACT_COMPASS:     _task->gotoCompassScreen();     break;
      case ACT_BOT:         _task->gotoBotScreen();         break;
      case ACT_AUTOADVERT:  _task->gotoAutoAdvertScreen();  break;
      case ACT_REPEATER:    _task->gotoRepeaterScreen();    break;
      case ACT_ADMIN:       _task->pickAdminTarget();       break;  // Admin is remote-only: pick a node first
      case ACT_CLOCK:       _task->gotoClockTools();        break;
      case ACT_RINGTONE:    _task->gotoRingtoneEditor();    break;
#if defined(PIN_GPIO1)
      case ACT_GPIO:        _task->gotoGpioScreen();        break;
#endif
    }
  }

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
    const int g  = gutter(display);

    _acc.render(display,
      // Section header: disclosure mark + name
      [&](int sec, int y, bool sel, int reserve, bool collapsed) {
        drawRowSelection(display, y, sel, reserve);
        drawDisclosure(display, 2, y, !collapsed);
        display.setCursor(2 + 2 * cw, y);   // the mark stands in for an icon, as in Settings
        display.print(SECTIONS[sec].name);
      },
      // Item: indented "<icon> Label"
      [&](int sec, int item, int y, bool sel, int reserve) {
        drawRowSelection(display, y, sel, reserve);
        const int icon_x = 2 + cw + 2;   // align item icons under the header icon
        drawIcon(display, icon_x, y, SECTIONS[sec].tools[item].icon);
        display.setCursor(icon_x + g, y);
        display.print(SECTIONS[sec].tools[item].label);
      });
    return 500;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
    switch (_acc.handleInput(c)) {
      case AccordionList::ACTIVATED: {
        const AccordionList::Row& r = _acc.selected();
        dispatch(SECTIONS[r.sec].tools[r.item].action);
        return true;
      }
      case AccordionList::HANDLED:  return true;
      case AccordionList::IGNORED:  return false;
    }
    return false;
  }
};

const ToolsScreen::Tool ToolsScreen::LOCATION_TOOLS[] = {
  { "Nodes", &ICON_MAP_CONTACT,  ACT_NEARBY },
  { "Live Share",   &ICON_GPS,          ACT_LIVESHARE },
  { "Trail",        &ICON_TRAIL,        ACT_TRAIL },
  { "Locator",    &ICON_MAP_WAYPOINT, ACT_LOCATOR },
  { "Compass",      &ICON_MAP_NORTH,    ACT_COMPASS },
};
const ToolsScreen::Tool ToolsScreen::COMMS_TOOLS[] = {
  { "Remote Bot",     &ICON_BOT,      ACT_BOT },
  { "Auto-Advert",    &ICON_ADVERT,   ACT_AUTOADVERT },
  { "Repeater",       &ICON_REPEATER, ACT_REPEATER },
  { "Admin",          &ICON_KEY,      ACT_ADMIN },
};
const ToolsScreen::Tool ToolsScreen::SYSTEM_TOOLS[] = {
  { "Clock Tools",     &ICON_ALARM, ACT_CLOCK },
  { "Ringtone Editor", &ICON_NOTE,  ACT_RINGTONE },
#if defined(PIN_GPIO1)
  { "GPIO",            &ICON_PINS,  ACT_GPIO },
#endif
};
const ToolsScreen::Section ToolsScreen::SECTIONS[] = {
  { "Location", LOCATION_TOOLS, (uint8_t)(sizeof(LOCATION_TOOLS)/sizeof(LOCATION_TOOLS[0])) },
  { "Comms",    COMMS_TOOLS,    4 },
  { "System",   SYSTEM_TOOLS,   (uint8_t)(sizeof(SYSTEM_TOOLS)/sizeof(SYSTEM_TOOLS[0])) },
};
