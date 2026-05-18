#pragma once
// Test screen that displays sample characters from various alphabets/scripts
// to verify native font coverage (no transliteration — Lemon renders natively).

class CharTestScreen : public UIScreen {
  UITask* _task;
  int _scroll;
  int _visible;  // updated each render, used by handleInput

  struct TestLine { const char* label; const char* sample; };
  static const int LINE_COUNT = 11;
  static const TestLine LINES[LINE_COUNT];

public:
  CharTestScreen(UITask* task) : _task(task), _scroll(0), _visible(6) {}

  void enter() { _scroll = 0; }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 0, "CHAR TEST");
    display.fillRect(0, 10, display.width(), 1);

    const int lineH  = display.getLineHeight();
    const int startY = 12;
    _visible = (display.height() - startY) / lineH;

    for (int i = 0; i < _visible && (_scroll + i) < LINE_COUNT; i++) {
      char buf[80];
      snprintf(buf, sizeof(buf), "%s:%s", LINES[_scroll + i].label, LINES[_scroll + i].sample);
      display.setCursor(2, startY + i * lineH);
      display.print(buf);
    }

    if (_scroll > 0) {
      display.setCursor(display.width() - 6, startY);
      display.print("^");
    }
    if (_scroll + _visible < LINE_COUNT) {
      display.setCursor(display.width() - 6, startY + (_visible - 1) * lineH);
      display.print("v");
    }
    return 2000;
  }

  bool handleInput(char c) override {
    if (c == KEY_UP   && _scroll > 0)                       { _scroll--; return true; }
    if (c == KEY_DOWN && _scroll + _visible < LINE_COUNT)   { _scroll++; return true; }
    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) { _task->gotoToolsScreen(); return true; }
    return false;
  }
};

// Lemon font covers U+0020–U+04FF: Latin, Greek and Cyrillic all render natively.
const CharTestScreen::TestLine CharTestScreen::LINES[CharTestScreen::LINE_COUNT] = {
  { "PL", "ąćęłńóśźżĄĆĘŁŃ"  },  // Polish
  { "DE", "äöüÄÖÜß"           },  // German
  { "CS", "čďěňřšťžůýČŘŠŤŽ"  },  // Czech/Slovak
  { "FR", "àâçèêëîïôùûÿ"      },  // French
  { "NO", "åæøÅÆØðþÐÞ"        },  // Nordic/Icelandic
  { "HU", "áéíőúűÁÉÍŐÚŰ"      },  // Hungarian
  { "HR", "čćđšžČĆĐŠŽ"        },  // Croatian
  { "TR", "çğışöüİŞĞ"          },  // Turkish
  { "LT", "āēīūģķļņŗėįų"      },  // Baltic (Latvian/Lithuanian)
  { "RU", "АБВГДЕЖЗабвгдеж"   },  // Cyrillic
  { "GR", "αβγδεζηθικλμ"         },  // Greek
};
