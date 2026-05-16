#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>

static const int FS_CHARS_MAX = 32;  // max buffer per line (larger than any font needs)
static const int FS_START_Y   = 12;  // header height (fixed, not font-dependent)

struct FullscreenMsgView {
  int  scroll;
  bool active;

  FullscreenMsgView() : scroll(0), active(false) {}

  enum Result { NONE, PREV, NEXT, CLOSE };

  void begin() { scroll = 0; active = true; }

  static int wrapLines(const char* text, int chars, char out[][FS_CHARS_MAX], int max_lines) {
    int count = 0;
    const char* p = text;
    while (*p && count < max_lines) {
      int len = strlen(p);
      if (len <= chars) {
        strncpy(out[count++], p, FS_CHARS_MAX - 1);
        out[count - 1][len < FS_CHARS_MAX ? len : FS_CHARS_MAX - 1] = '\0';
        break;
      }
      int brk = chars;
      for (int i = chars - 1; i > 0; i--) {
        if (p[i] == ' ') { brk = i; break; }
      }
      int copy = brk < FS_CHARS_MAX - 1 ? brk : FS_CHARS_MAX - 1;
      strncpy(out[count], p, copy);
      out[count++][copy] = '\0';
      p += brk + (p[brk] == ' ' ? 1 : 0);
    }
    return count;
  }

  int render(DisplayDriver& display, const char* sender, const char* text,
             bool has_prev, bool has_next) {
    display.setTextSize(1);
    const int lineH   = display.getLineHeight();
    const int chars   = (display.width() - 6) / display.getCharWidth();
    const int visible = (display.height() - FS_START_Y - lineH) / lineH;

    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), 10);
    display.setColor(DisplayDriver::DARK);
    display.drawTextEllipsized(2, 1, display.width() - 4, sender);
    display.setColor(DisplayDriver::LIGHT);

    char trans_text[512];
    display.translateUTF8ToBlocks(trans_text, text, sizeof(trans_text));
    char lines[12][FS_CHARS_MAX];
    int lcount = wrapLines(trans_text, chars, lines, 12);
    int max_scroll = lcount > visible ? lcount - visible : 0;
    if (scroll > max_scroll) scroll = max_scroll;

    for (int i = 0; i < visible && (scroll + i) < lcount; i++) {
      display.setCursor(0, FS_START_Y + i * lineH);
      display.print(lines[scroll + i]);
    }
    if (scroll > 0) {
      display.setColor(DisplayDriver::DARK);
      display.fillRect(display.width() - 6, FS_START_Y, 6, FS_LINE_H);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - 6, FS_START_Y);
      display.print("^");
    }
    if (scroll < max_scroll) {
      display.setColor(DisplayDriver::DARK);
      display.fillRect(display.width() - 6, FS_START_Y + (visible - 1) * lineH, 6, lineH);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - 6, FS_START_Y + (visible - 1) * lineH);
      display.print("v");
    }
    const int nav_y = display.height() - lineH;
    if (has_next) {
      display.setCursor(0, nav_y);
      display.print("<");
    }
    if (has_prev) {
      display.setCursor(display.width() - 6, nav_y);
      display.print(">");
    }
    return 300;
  }

  Result handleInput(char c) {
    if (c == KEY_UP)     { if (scroll > 0) scroll--; return NONE; }
    if (c == KEY_DOWN)   { scroll++; return NONE; }
    if (c == KEY_LEFT)   return NEXT;
    if (c == KEY_RIGHT)  return PREV;
    if (c == KEY_ENTER || c == KEY_CANCEL) return CLOSE;
    return NONE;
  }
};
