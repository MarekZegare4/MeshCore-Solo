#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>
#include "PopupMenu.h"
#include "icons.h"   // mini-icons for the special-key row (⇧ ⌫ ⎵ ✓)
#include "../NodePrefs.h"

// Layout constants shared by all keyboard users.
// Two pages: letters (page 0) and symbols (page 1), toggled by the "#@"/"abc"
// special key. Space lives only on the ⎵ special key now, so the freed grid
// slot on page 0 holds the comma; punctuation is grouped as . , ! ?
static const int KB_PAGES      = 2;
static const char KB_CHARS[KB_PAGES][4][10] = {
  { // page 0 — letters + digits
    {'a','b','c','d','e','f','g','h','i','j'},
    {'k','l','m','n','o','p','q','r','s','t'},
    {'u','v','w','x','y','z','.',',','!','?'},
    {'1','2','3','4','5','6','7','8','9','0'},
  },
  { // page 1 — symbols + digits (ASCII only — one byte per key)
    {'@','#','&','*','(',')','-','_','+','='},
    {'/','\\',':',';','\'','"','<','>','[',']'},
    {'{','}','|','~','^','$','%','`',',','.'},
    {'1','2','3','4','5','6','7','8','9','0'},
  },
};
static const int KB_ROWS_CHAR  = 4;
static const int KB_COLS_CHAR  = 10;
static const int KB_SPECIAL    = 6;   // ⇧ ⎵ ⌫ {} #@/abc ✓

// T9 multi-tap layout (Settings › Keyboard). A classic phone keypad: 9 cells (keys
// 1-9) laid out 3x3, each holding a handful of letters/symbols. Repeated Enter
// presses on the same cell within KB_T9_TIMEOUT_MS cycle through the group, ending
// on the cell's own digit (computed as '1'+cell, not stored here) before wrapping.
// Keys 0/*/# aren't part of the grid — space/backspace/etc. already live on the
// special row below, shared with the ABC layout.
static const int KB_T9_ROWS = 3;
static const int KB_T9_COLS = 3;
static const uint32_t KB_T9_TIMEOUT_MS = 800;
static const char* const KB_T9_GROUPS[KB_PAGES][9] = {
  { ".,!?'-", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" },   // page 0 — letters
  { "@#&", "*()", "-_+", "=/\\", ":;'\"", "<>[]", "{}|~", "^$%`", ",." },  // page 1 — symbols
};
// Buffer cap for typed text, in bytes. Matches MeshCore's MAX_TEXT_LEN
// (10*CIPHER_BLOCK_SIZE = 160) so a full-length message can be composed; each
// field passes its own smaller max to begin() where its store is smaller.
static const int KB_MAX_LEN    = 160;

// Longest preview line we render per row. Caps the per-line stack buffers so a
// very wide display (small font → many chars per line) can't overrun them.
static const int KB_PREVIEW_CAP = 46;

static const int KB_PH_MAX     = 12;  // max placeholders in list
static const int KB_PH_LEN     = 9;   // max placeholder string length incl. null
static const int KB_PH_VISIBLE = 3;   // items shown at once in overlay

struct KeyboardWidget {
  char buf[KB_MAX_LEN + 1];
  int  len;
  int  max_len;
  int  row, col;
  int  page;        // 0 = letters, 1 = symbols
  bool caps;
  char _ph_buf[KB_PH_MAX][KB_PH_LEN];
  int  _ph_count;
  PopupMenu _ph_menu;

  // Live setting lookup — set once by UITask::begin(). NULL only in tests/tools
  // that construct a KeyboardWidget standalone, in which case isT9() defaults
  // to ABC.
  NodePrefs* prefs = nullptr;
  bool isT9() const { return prefs && prefs->keyboard_type == 1; }

  // T9 multi-tap state: which grid cell is mid-cycle (-1 = none), its cycle
  // position, and when the last Enter landed on it (for the timeout).
  int      t9_cell = -1;
  int      t9_cycle = 0;
  uint32_t t9_last_ms = 0;

  int gridRows() const { return isT9() ? KB_T9_ROWS : KB_ROWS_CHAR; }
  int gridCols() const { return isT9() ? KB_T9_COLS : KB_COLS_CHAR; }

  enum Result { NONE, DONE, CANCELLED };

  void begin(const char* initial = "", int max = KB_MAX_LEN) {
    max_len = (max > KB_MAX_LEN) ? KB_MAX_LEN : max;
    strncpy(buf, initial, max_len);
    buf[max_len] = '\0';
    len = strlen(buf);
    row = col = 0;
    page = 0;
    caps = false;
    t9_cell = -1;
    t9_cycle = 0;
    _ph_menu.active = false;
    // default placeholders — always available
    _ph_count = 0;
    addPlaceholder("{loc}");
    addPlaceholder("{time}");
  }

  void clearPlaceholders() { _ph_count = 0; }

  void addPlaceholder(const char* ph) {
    if (_ph_count < KB_PH_MAX) {
      strncpy(_ph_buf[_ph_count], ph, KB_PH_LEN - 1);
      _ph_buf[_ph_count][KB_PH_LEN - 1] = '\0';
      _ph_count++;
    }
  }

  int render(DisplayDriver& display) {
    // A stale mid-cycle T9 press (no further input since) finalizes on its own —
    // the character is already committed to buf, this just stops a later Enter
    // on the same cell from being treated as a continued cycle.
    if (t9_cell >= 0 && millis() - t9_last_ms > KB_T9_TIMEOUT_MS) t9_cell = -1;

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    const int rows = gridRows();
    const int cols = gridCols();
    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int cell_w  = display.width() / cols;
    // compact: don't stretch cells beyond lh; freed vertical space goes to preview lines
    const int kb_h      = (rows + 1) * lh;
    const int preview_h = display.height() - kb_h - display.sepH();
    const int prev_lines = (preview_h / lh) > 1 ? (preview_h / lh) : 1;
    const int sep_y   = prev_lines * lh;
    const int chars_y = sep_y + display.sepH();
    const int cell_h  = (display.height() - chars_y) / (rows + 1);
    const int spec_y  = chars_y + rows * cell_h;
    const int spec_w  = display.width() / KB_SPECIAL;

    // multi-line text preview: cursor always on last preview line
    int cpl = display.width() / cw;  // chars per preview line
    if (cpl < 1) cpl = 1;
    if (cpl > KB_PREVIEW_CAP) cpl = KB_PREVIEW_CAP;  // never overrun linebuf below
    int cursor_line = len / cpl;
    int first_line  = (cursor_line >= prev_lines) ? (cursor_line - prev_lines + 1) : 0;
    int start = first_line * cpl;
    for (int pl = 0; pl < prev_lines; pl++) {
      int ps = start + pl * cpl;
      int pe = ps + cpl;
      bool cursor_here = (ps <= len && (len < pe || pl == prev_lines - 1));
      char linebuf[KB_PREVIEW_CAP + 2];   // cpl chars + cursor '_' + NUL
      if (cursor_here) {
        int nc = len - ps; if (nc < 0) nc = 0; if (nc > cpl - 1) nc = cpl - 1;
        snprintf(linebuf, sizeof(linebuf), "%.*s_", nc, buf + ps);
      } else if (len > ps) {
        int nc = (len < pe) ? (len - ps) : cpl;
        snprintf(linebuf, sizeof(linebuf), "%.*s", nc, buf + ps);
      } else {
        linebuf[0] = '\0';
      }
      char linebuf_t[KB_PREVIEW_CAP + 2];
      display.translateUTF8ToBlocks(linebuf_t, linebuf, sizeof(linebuf_t));
      display.setCursor(0, pl * lh);
      display.print(linebuf_t);
    }
    display.fillRect(0, sep_y, display.width(), display.sepH());

    // character grid
    if (isT9()) {
      for (int r = 0; r < rows; r++) {
        int y = chars_y + r * cell_h;
        for (int c = 0; c < cols; c++) {
          bool sel = (row == r && col == c);
          int cell = r * cols + c;
          char group[10];
          strncpy(group, KB_T9_GROUPS[page][cell], sizeof(group) - 1);
          group[sizeof(group) - 1] = '\0';
          if (caps) for (char* p = group; *p; p++) if (*p >= 'a' && *p <= 'z') *p = *p - 'a' + 'A';
          int cx = c * cell_w;
          display.drawSelectionRow(cx, y - 1, cell_w - 1, cell_h, sel);
          int tw = display.getTextWidth(group);
          display.setCursor(cx + (cell_w - tw) / 2, y);
          display.print(group);
        }
      }
    } else {
      for (int r = 0; r < rows; r++) {
        int y = chars_y + r * cell_h;
        for (int c = 0; c < cols; c++) {
          bool sel = (row == r && col == c);
          char ch = KB_CHARS[page][r][c];
          if (caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
          char ch_buf[2] = { ch == ' ' ? '_' : ch, '\0' };
          int cx = c * cell_w;
          display.drawSelectionRow(cx, y - 1, cell_w - 1, cell_h, sel);
          display.setCursor(cx + (cell_w - cw) / 2, y);
          display.print(ch_buf);
        }
      }
    }

    // special row: caps ⇧ · space ⎵ · delete ⌫ · placeholders {} (text) · OK ✓
    const int s   = miniIconScale(display);
    const int icy = spec_y + (cell_h - lh) / 2;   // centre icons within the cell
    for (int i = 0; i < KB_SPECIAL; i++) {
      bool sel    = (row == rows && col == i);
      bool active = (i == 0 && caps);
      int sx = i * spec_w;
      display.drawSelectionRow(sx, spec_y - 1, spec_w - 1, cell_h, sel || active);
      if (i == 3 || i == 4) {               // text keys: {} picker, page toggle
        const char* lbl = (i == 3) ? "{}" : (page == 0 ? "#@" : "abc");
        int tw = display.getTextWidth(lbl);
        display.setCursor(sx + (spec_w - tw) / 2, spec_y);
        display.print(lbl);
      } else if (i == 1) {                  // space ⎵ — two halves side by side
        int icw = (ICON_SPACE_L.w + ICON_SPACE_R.w) * s;
        int ix  = sx + (spec_w - icw) / 2;
        miniIconDraw(display, ix, icy, ICON_SPACE_L);
        miniIconDraw(display, ix + ICON_SPACE_L.w * s, icy, ICON_SPACE_R);
      } else {
        const MiniIcon& ic = (i == 0) ? ICON_SHIFT
                           : (i == 2) ? ICON_BACKSPACE
                                      : ICON_CHECK;   // i == 5 → OK
        int ix = sx + (spec_w - ic.w * s) / 2;
        miniIconDraw(display, ix, icy, ic);
      }
      display.setColor(DisplayDriver::LIGHT);
    }

    // placeholder picker overlay (drawn on top of keyboard)
    if (_ph_menu.active) _ph_menu.render(display);

    return 50;
  }

  Result handleInput(char c) {
    // placeholder overlay consumes all input
    if (_ph_menu.active) {
      auto res = _ph_menu.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        int idx = _ph_menu.selectedIndex();
        const char* ph = _ph_buf[idx];
        int ph_len = strlen(ph);
        if (len + ph_len <= max_len) {
          memcpy(buf + len, ph, ph_len);
          len += ph_len;
          buf[len] = '\0';
        }
      }
      return NONE;
    }

    if (c == KEY_CANCEL || c == KEY_CONTEXT_MENU) return CANCELLED;

    const int rows = gridRows();
    const int cols = gridCols();

    if (c == KEY_UP) {
      if (row > 0) {
        row--;
        if (row == rows - 1)  // leaving special row upward
          col = col * cols / KB_SPECIAL;
      } else {
        row = rows;             // wrap up onto the special row
        col = col * KB_SPECIAL / cols;
      }
      t9_cell = -1;   // navigating away finalizes any pending multi-tap cycle
      return NONE;
    }
    if (c == KEY_DOWN) {
      if (row < rows) {
        row++;
        if (row == rows)  // entering special row
          col = col * KB_SPECIAL / cols;
      } else {
        row = 0;                        // wrap down onto the first char row
        col = col * cols / KB_SPECIAL;
      }
      t9_cell = -1;
      return NONE;
    }
    if (c == KEY_LEFT) {
      int max_col = (row == rows) ? KB_SPECIAL - 1 : cols - 1;
      col = (col > 0) ? col - 1 : max_col;
      t9_cell = -1;
      return NONE;
    }
    if (c == KEY_RIGHT) {
      int max_col = (row == rows) ? KB_SPECIAL - 1 : cols - 1;
      col = (col < max_col) ? col + 1 : 0;
      t9_cell = -1;
      return NONE;
    }
    if (c == KEY_ENTER) {
      if (row < rows && isT9()) {
        int cell = row * cols + col;
        const char* group = KB_T9_GROUPS[page][cell];
        int glen = (int)strlen(group);
        int total = glen + 1;   // + the cell's own digit, at the end of the cycle
        bool cycling = (t9_cell == cell) && (millis() - t9_last_ms < KB_T9_TIMEOUT_MS);
        if (cycling) {
          t9_cycle = (t9_cycle + 1) % total;
          if (len > 0) {
            char ch = (t9_cycle < glen) ? group[t9_cycle] : (char)('1' + cell);
            if (caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            buf[len - 1] = ch;
          }
        } else if (len < max_len) {
          char ch = group[0];
          if (caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
          buf[len++] = ch;
          buf[len] = '\0';
          t9_cell = cell;
          t9_cycle = 0;
        }
        t9_last_ms = millis();
      } else if (row < rows) {
        if (len < max_len) {
          char ch = KB_CHARS[page][row][col];
          if (caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
          buf[len++] = ch;
          buf[len] = '\0';
        }
      } else {
        t9_cell = -1;   // any special-row action finalizes a pending multi-tap cycle
        switch (col) {
          case 0: caps = !caps; break;
          case 1:
            if (len < max_len) { buf[len++] = ' '; buf[len] = '\0'; }
            break;
          case 2:
            if (len > 0) buf[--len] = '\0';
            break;
          case 3:
            _ph_menu.begin("Placeholder:", KB_PH_VISIBLE);
            for (int i = 0; i < _ph_count; i++) _ph_menu.addItem(_ph_buf[i]);
            break;
          case 4:
            page ^= 1;   // toggle letters ⇄ symbols
            break;
          case 5:
            return DONE;
        }
      }
    }
    return NONE;
  }
};
