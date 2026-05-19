#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>

static const int FS_CHARS_MAX = 80;  // max bytes per line; sized for worst-case narrow glyphs (122px / 2px = 61 chars)
static const int FS_START_Y   = 12;  // header height (fixed, not font-dependent)

struct FullscreenMsgView {
  int  scroll;
  bool active;

  FullscreenMsgView() : scroll(0), active(false) {}

  enum Result { NONE, PREV, NEXT, CLOSE, REPLY };

  void begin() { scroll = 0; active = true; }

  // Pixel-accurate word-wrap using display.getTextWidth().
  // Breaks at the last space that fits within max_px; hard-breaks long words.
  static int wrapLines(DisplayDriver& display, const char* text, int max_px,
                       char out[][FS_CHARS_MAX], int max_lines) {
    int count = 0;
    const char* seg = text;
    while (*seg && count < max_lines) {
      const char* p        = seg;
      const char* last_sp  = nullptr;  // pointer to last space that fits
      const char* last_fit = seg;      // pointer past last char that fits

      while (*p && (p - seg) < FS_CHARS_MAX - 1) {
        // Byte length of this UTF-8 codepoint
        uint8_t b0 = (uint8_t)*p;
        int cb = (b0 < 0x80) ? 1 : (b0 < 0xE0) ? 2 : (b0 < 0xF0) ? 3 : 4;
        if ((p - seg) + cb >= FS_CHARS_MAX) break;

        // Measure [seg .. p+cb)
        char buf[FS_CHARS_MAX];
        int n = (int)(p - seg) + cb;
        memcpy(buf, seg, n); buf[n] = '\0';

        // Stop if this character would overflow — but always accept the first one
        // so we never get stuck on a single very-wide character.
        if (display.getTextWidth(buf) > max_px && p > seg) break;

        if (*p == ' ') last_sp = p;
        p       += cb;
        last_fit = p;
      }

      // Determine break point: prefer last space, fall back to last fitting char
      const char* end;
      const char* next_seg;
      if (!*p) {
        end = p; next_seg = p;                         // reached end of text
      } else if (last_sp && last_sp > seg) {
        end = last_sp; next_seg = last_sp + 1;         // break at space
      } else {
        end = last_fit; next_seg = last_fit;           // hard break (no space found)
      }

      int len = (int)(end - seg);
      if (len <= 0) { seg = *seg ? seg + 1 : seg; continue; }  // safety
      if (len > FS_CHARS_MAX - 1) len = FS_CHARS_MAX - 1;
      memcpy(out[count], seg, len);
      out[count][len] = '\0';
      count++;
      seg = next_seg;
    }
    return count;
  }

  int render(DisplayDriver& display, const char* sender, const char* text,
             bool has_prev, bool has_next) {
    display.setTextSize(1);
    const int lineH   = display.getLineHeight();
    const int max_px  = display.width() - 6;

    // detect @recipient at start of message
    char to_nick[32] = "";
    const char* body = text;
    if (text[0] == '@' && text[1] == '[') {
      const char* close = strchr(text + 2, ']');
      if (close && close[1] == ' ' && close[2]) {
        int len = (int)(close - text) - 2;
        if (len >= (int)sizeof(to_nick)) len = sizeof(to_nick) - 1;
        memcpy(to_nick, text + 2, len);
        to_nick[len] = '\0';
        body = close + 2;
      }
    }

    const int header_h = to_nick[0] ? 20 : 10;
    const int startY   = header_h + 2;
    const int visible  = (display.height() - startY - lineH) / lineH;

    display.setColor(DisplayDriver::LIGHT);
    display.fillRect(0, 0, display.width(), header_h);
    display.setColor(DisplayDriver::DARK);
    display.drawTextEllipsized(2, 1, display.width() - 4, sender);
    if (to_nick[0]) {
      char trans_nick[32], to_label[36];
      display.translateUTF8ToBlocks(trans_nick, to_nick, sizeof(trans_nick));
      snprintf(to_label, sizeof(to_label), "To: %s", trans_nick);
      display.drawTextEllipsized(2, 11, display.width() - 4, to_label);
    }
    display.setColor(DisplayDriver::LIGHT);

    char trans_text[512];
    display.translateUTF8ToBlocks(trans_text, body, sizeof(trans_text));
    char lines[12][FS_CHARS_MAX];
    int lcount = wrapLines(display, trans_text, max_px, lines, 12);
    int max_scroll = lcount > visible ? lcount - visible : 0;
    if (scroll > max_scroll) scroll = max_scroll;

    for (int i = 0; i < visible && (scroll + i) < lcount; i++) {
      display.setCursor(0, startY + i * lineH);
      display.print(lines[scroll + i]);
    }
    if (scroll > 0) {
      display.setColor(DisplayDriver::DARK);
      display.fillRect(display.width() - 6, startY, 6, lineH);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - 6, startY);
      display.print("^");
    }
    if (scroll < max_scroll) {
      display.setColor(DisplayDriver::DARK);
      display.fillRect(display.width() - 6, startY + (visible - 1) * lineH, 6, lineH);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(display.width() - 6, startY + (visible - 1) * lineH);
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
    return 2000;
  }

  Result handleInput(char c) {
    if (c == KEY_UP)          { if (scroll > 0) scroll--; return NONE; }
    if (c == KEY_DOWN)        { scroll++; return NONE; }
    if (c == KEY_LEFT)        return NEXT;
    if (c == KEY_RIGHT)       return PREV;
    if (c == KEY_CONTEXT_MENU) return REPLY;
    if (c == KEY_ENTER || c == KEY_CANCEL) return CLOSE;
    return NONE;
  }
};
