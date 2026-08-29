#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>
#include "icons.h"

static const int FS_CHARS_MAX = 80;  // max bytes per wrapped line

// Shared, non-reentrant wrap scratch. The UI renders on a single thread, and the
// fullscreen message view and the history list are never laid out in the same
// frame (opening the fullscreen view early-returns before the list loop runs),
// so one static buffer serves both. This keeps ~1.5 KB of line buffers off the
// render-call stack at the cost of a fixed RAM allocation. Only used inside
// render()/wrap helpers — never across a yield, so the single instance is safe.
static char s_wrap_trans[512];
static char s_wrap_lines[12][FS_CHARS_MAX];

// Parse a leading "@[nick] " reply prefix. Returns the message body that
// follows it (and any leading whitespace); when nick/nick_n are supplied,
// fills nick with the addressee, or "" when there's no prefix. One parser for
// both the history list (body only — see MessagesScreen::skipReplyPrefix) and
// the fullscreen view (which also shows the "To:" nick).
static inline const char* msgReplyBody(const char* text, char* nick = nullptr, int nick_n = 0) {
  if (nick && nick_n > 0) nick[0] = '\0';
  const char* body = text;
  if (text[0] == '@' && text[1] == '[') {
    const char* close = strchr(text + 2, ']');
    if (close && close[1] == ' ' && close[2]) {
      if (nick && nick_n > 0) {
        int len = (int)(close - text) - 2;
        if (len > nick_n - 1) len = nick_n - 1;
        memcpy(nick, text + 2, len);
        nick[len] = '\0';
      }
      body = close + 2;
    }
  }
  while (*body == '\n' || *body == '\r' || *body == ' ') body++;
  return body;
}

struct FullscreenMsgView {
  int  scroll;
  bool active;
  int  _max_scroll;   // last value computed in render(); bounds KEY_DOWN

  FullscreenMsgView() : scroll(0), active(false), _max_scroll(0) {}

  enum Result { NONE, PREV, NEXT, CLOSE, REPLY };

  void begin() { scroll = 0; active = true; }

  // Pixel-accurate word-wrap: breaks at last space that fits within max_px,
  // hard-breaks long words, and honours newlines the sender put in the text.
  // Walks the string codepoint-by-codepoint, summing per-codepoint widths via
  // DisplayDriver::getCodepointWidth — O(n) instead of O(n²) substring
  // re-measurement. Works for both fixed- and variable-width fonts.
  static int wrapLines(DisplayDriver& display, const char* text, int max_px,
                       char out[][FS_CHARS_MAX], int max_lines) {
    int count = 0;
    const uint8_t* seg = (const uint8_t*)text;
    while (*seg && count < max_lines) {
      const uint8_t* p        = seg;
      const uint8_t* last_sp  = nullptr;       // pointer to space byte (cut here, drop the space)
      const uint8_t* last_fit = seg;           // farthest point that still fits
      const uint8_t* brk      = nullptr;       // explicit newline that ends this line
      int width = 0;

      while (*p && (p - seg) < FS_CHARS_MAX - 1) {
        const uint8_t* cp_start = p;
        uint32_t cp = DisplayDriver::decodeCodepoint(p);
        if ((p - seg) >= FS_CHARS_MAX) { p = cp_start; break; }
        // A newline in the message ends the line right here, and is consumed
        // rather than copied out. Letting one through was drawing two words on
        // top of each other: both display drivers' print() acts on '\n' by
        // resetting the cursor to x=0 and stepping down one row, so the rest of
        // this wrapped line landed on the following one. Measuring it is just
        // as wrong — it has no ink, but glyphXAdvance() reports a full 6px cell
        // for it (it's below the font's first codepoint), so it also ate width.
        if (cp == '\n' || cp == '\r') { brk = cp_start; break; }
        uint16_t cw = display.getCodepointWidth(cp);
        if (width + cw > max_px && cp_start > seg) { p = cp_start; break; }
        if (cp == ' ') last_sp = cp_start;
        width   += cw;
        last_fit = p;
      }

      const uint8_t* end;
      const uint8_t* next_seg;
      if (brk) {
        end = brk;
        next_seg = brk + 1;
        if (*brk == '\r' && *next_seg == '\n') next_seg++;   // CRLF is one break, not two
      } else if (!*p) {
        end = p; next_seg = p;
      } else if (last_sp && last_sp > seg) {
        end = last_sp; next_seg = last_sp + 1;
      } else {
        end = last_fit; next_seg = last_fit;
      }

      int len = (int)(end - seg);
      // An empty line is only emitted for a real newline (a blank line the
      // sender typed); an empty wrap segment would loop forever, so skip it.
      if (len <= 0 && !brk) { seg = *seg ? seg + 1 : seg; continue; }
      if (len < 0) len = 0;
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
    const int lineH  = display.getLineHeight();
    const int max_px = display.width() - 6;

    // "@[nick] " reply prefix → "To:" header + body (shared parser).
    char to_nick[32];
    const char* body = msgReplyBody(text, to_nick, sizeof(to_nick));

    const int cw       = display.getCharWidth();
    const int header_h = to_nick[0] ? (lineH * 2 + 4) : (lineH + 2);
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
      display.drawTextEllipsized(2, lineH + 3, display.width() - 4, to_label);
    }
    display.setColor(DisplayDriver::LIGHT);

    display.translateUTF8ToBlocks(s_wrap_trans, body, sizeof(s_wrap_trans));
    int lcount = wrapLines(display, s_wrap_trans, max_px, s_wrap_lines, 12);
    // Reserve the right-edge column for the scroll indicator and re-wrap so text
    // can't run under it. Reserve appears only once the list overflows, and a
    // narrower second pass only ever yields more lines — so it stays overflowing.
    int reserve = scrollIndicatorReserve(display, lcount, visible);
    if (reserve > 0)
      lcount = wrapLines(display, s_wrap_trans, max_px - reserve, s_wrap_lines, 12);
    int max_scroll = lcount > visible ? lcount - visible : 0;
    _max_scroll = max_scroll;
    if (scroll > max_scroll) scroll = max_scroll;

    for (int i = 0; i < visible && (scroll + i) < lcount; i++) {
      display.setCursor(0, startY + i * lineH);
      display.print(s_wrap_lines[scroll + i]);
    }
    drawScrollIndicator(display, startY, visible * lineH, lcount, visible, scroll);
    // Page markers read like a book: the older message is back to the left,
    // the newer one forward to the right (see handleInput).
    const int nav_y = display.height() - lineH;
    if (has_prev) {
      display.setCursor(0, nav_y);
      display.print("<");
    }
    if (has_next) {
      display.setCursor(display.width() - cw, nav_y);
      display.print(">");
    }
    return 2000;
  }

  Result handleInput(char c) {
    if (c == KEY_UP)          { if (scroll > 0) scroll--; return NONE; }
    if (c == KEY_DOWN)        { if (scroll < _max_scroll) scroll++; return NONE; }
    // Page between messages (encoder too), in reading order: left goes back to
    // the older message, right forward to the newer one. PREV/NEXT are named in
    // message order, and MessagesScreen's _hist_sel counts newest-first, so PREV
    // (older) is what a left press means.
    if (keyIsPrev(c))         return PREV;
    if (keyIsNext(c))         return NEXT;
    if (c == KEY_CONTEXT_MENU) return REPLY;
    if (c == KEY_ENTER || c == KEY_CANCEL) return CLOSE;
    return NONE;
  }
};
