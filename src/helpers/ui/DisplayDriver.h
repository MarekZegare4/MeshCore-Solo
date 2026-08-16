#pragma once

#include <stdint.h>
#include <string.h>

class DisplayDriver {
  int _w, _h;
protected:
  bool _vw_dirty = true;
  bool _vw_result = false;
  DisplayDriver(int w, int h) { _w = w; _h = h; }
  void setDimensions(int w, int h) { _w = w; _h = h; }
public:
  enum Color { DARK=0, LIGHT, RED, GREEN, BLUE, YELLOW, ORANGE }; // on b/w screen, colors will be !=0 synonym of light

  int width() const { return _w; }
  int height() const { return _h; }

  virtual bool isOn() = 0;
  virtual bool isEink() { return false; } // default to non-eink, override in eink drivers
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  virtual void clear() = 0;
  virtual void startFrame(Color bkg = DARK) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(Color c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void printWordWrap(const char* str, int max_width) { print(str); }   // fallback to basic print() if no override
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual int getCharWidth() const { return 6; }   // typical character advance width (px)
  virtual int getLineHeight() const { return 8; }  // pixel rows per text line
  virtual void setSingleFont(bool) { }              // no-op; both concrete drivers are permanently pinned to their one font
  virtual bool isSingleFont() const { return false; }
  // Layout helpers — derived from font metrics and screen size.
  // Use these instead of hardcoded pixel values so layouts adapt to any display.
  int lineStep()             const { return getLineHeight() + 2; }         // row pitch: text + gap
  int headerH()              const { return getLineHeight() + 3; }         // title bar height
  // y where list items begin: a 2px breathing gap below the header separator so
  // the first row doesn't touch the line (matches the hand-rolled hdr+2 used by
  // the graphical screens).
  #ifndef LIST_START_EXTRA_PAD
     #define LIST_START_EXTRA_PAD 0
   #endif
     int listStart()            const { return headerH() + 2 + LIST_START_EXTRA_PAD; }
  int listVisible(int itemH) const { return (height() - listStart()) / itemH; }
  int listVisible()          const { return listVisible(lineStep()); }
  // x where a right-side value column starts (leaves ~8 chars for the value)
  int valCol()               const { return width() - getCharWidth() * 8; }
  // true only on landscape e-ink; use instead of comparing pixel counts or getLineHeight()
#ifdef EINK_DISPLAY_MODEL
  bool isLandscape()         const { return width() >= height(); }
#else
  bool isLandscape()         const { return false; }
#endif
  // separator line thickness: 2px on landscape e-ink, 1px everywhere else
  int sepH()                 const { return isLandscape() ? 2 : 1; }
  virtual void drawTextCentered(int mid_x, int y, const char* str) {
    char tmp[256]; translateUTF8ToBlocks(tmp, str, sizeof(tmp));
    int w = getTextWidth(tmp);
    setCursor(mid_x - w/2, y);
    print(tmp);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    char tmp[256]; translateUTF8ToBlocks(tmp, str, sizeof(tmp));
    int w = getTextWidth(tmp);
    setCursor(x_anch - w, y);
    print(tmp);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    char tmp[256]; translateUTF8ToBlocks(tmp, str, sizeof(tmp));
    setCursor(x_anch, y);
    print(tmp);
  }
  
  // Sorted-codepoint → ASCII transliteration table.
  // Binary search beats a ~110-case switch on flash (parallel arrays = 3 bytes/entry,
  // no padding) and gives O(log n) lookup. Covers Latin Extended A/B + common
  // Latin-1 accented diacritics needed by EU languages and Turkish.
  static char transliterateCodepoint(uint32_t cp) {
    // Codepoints must stay sorted ascending — binary search assumes it.
    static const uint16_t CPS[] = {
      0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
      0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
      0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D8,
      0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
      0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
      0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
      0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F8,
      0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE,
      0x0100, 0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x0106, 0x0107,
      0x010C, 0x010D, 0x010E, 0x010F, 0x0110, 0x0111, 0x0112, 0x0113,
      0x0116, 0x0117, 0x0118, 0x0119, 0x011A, 0x011B, 0x011E, 0x011F,
      0x0122, 0x0123, 0x012A, 0x012B, 0x012E, 0x012F, 0x0131,
      0x0136, 0x0137, 0x0139, 0x013A, 0x013B, 0x013C, 0x013D, 0x013E,
      0x0141, 0x0142, 0x0143, 0x0144, 0x0145, 0x0146, 0x0147, 0x0148,
      0x0150, 0x0151, 0x0154, 0x0155, 0x0156, 0x0157, 0x0158, 0x0159,
      0x015A, 0x015B, 0x015E, 0x015F, 0x0160, 0x0161, 0x0162, 0x0163,
      0x0164, 0x0165, 0x016A, 0x016B, 0x016E, 0x016F, 0x0170, 0x0171,
      0x0172, 0x0173, 0x0179, 0x017A, 0x017B, 0x017C, 0x017D, 0x017E,
      0x0218, 0x0219, 0x021A, 0x021B
    };
    static const char ASCII[] = {
      'A','A','A','A','A','A','A','C',
      'E','E','E','E','I','I','I','I',
      'D','N','O','O','O','O','O','O',
      'U','U','U','U','Y','T','s',
      'a','a','a','a','a','a','a','c',
      'e','e','e','e','i','i','i','i',
      'd','n','o','o','o','o','o','o',
      'u','u','u','u','y','t',
      'A','a','A','a','A','a','C','c',
      'C','c','D','d','D','d','E','e',
      'E','e','E','e','E','e','G','g',
      'G','g','I','i','I','i','i',
      'K','k','L','l','L','l','L','l',
      'L','l','N','n','N','n','N','n',
      'O','o','R','r','R','r','R','r',
      'S','s','S','s','S','s','T','t',
      'T','t','U','u','U','u','U','u',
      'U','u','Z','z','Z','z','Z','z',
      'S','s','T','t'
    };
    static_assert(sizeof(CPS) / sizeof(CPS[0]) == sizeof(ASCII), "transliteration tables out of sync");
    int lo = 0, hi = (int)(sizeof(ASCII)) - 1;
    while (lo <= hi) {
      int mid = (lo + hi) >> 1;
      uint16_t v = CPS[mid];
      if (v == cp) return ASCII[mid];
      if (v < cp) lo = mid + 1; else hi = mid - 1;
    }
    return '\xDB';
  }

  // convert UTF-8 to ASCII, transliterating accented/diacritic characters (callable without a display instance)
  static void translateUTF8Static(char* dest, const char* src, size_t dest_size) {
    size_t j = 0;
    const uint8_t* p = (const uint8_t*)src;
    while (*p && j < dest_size - 1) {
      if (*p < 0x80) {
        uint8_t c = *p++;
        if (c >= 32) dest[j++] = c;
      } else {
        uint32_t cp = decodeCodepoint(p);
        // transliterateCodepoint already returns '\xDB' for unmapped values (incl. 0xFFFD).
        dest[j++] = transliterateCodepoint(cp);
      }
    }
    dest[j] = 0;
  }

  virtual void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
    translateUTF8Static(dest, src, dest_size);
  }

  // Common selection-row pattern: when sel, fills (x,y,w,h) with ink and sets
  // colour to DARK so the next text render appears inverted; when not sel,
  // just sets ink colour to LIGHT. Replaces the 5-line if/else copy-paste
  // present in every list-style screen.
  void drawSelectionRow(int x, int y, int w, int h, bool sel) {
    setColor(LIGHT);
    if (sel) {
      fillRect(x, y, w, h);
      setColor(DARK);
    }
  }

  // Format a small unread count into buf: "1".."99", then "99+". count >= 1.
  // No stdio — DisplayDriver.h only pulls stdint/string.
  static void fmtBadgeCount(char* buf, int count) {
    if (count > 99)       { buf[0]='9'; buf[1]='9'; buf[2]='+'; buf[3]=0; }
    else if (count >= 10) { buf[0]=(char)('0'+count/10); buf[1]=(char)('0'+count%10); buf[2]=0; }
    else                  { buf[0]=(char)('0'+count); buf[1]=0; }
  }
  // Trailing blank column baked into a backend's own advance-based
  // getTextWidth() (0 if it's already ink-tight). Adafruit_GFX's classic
  // built-in font measures every character as a fixed 6px advance cell (5px
  // glyph + 1px inter-character gap) regardless of the glyph actually drawn —
  // so getTextWidth() always reports 1px more than the real ink, all of it
  // trailing the last character. Centring on the raw width then leaves 1px
  // more slack on the right than the left. Overridden by backends that use
  // that font.
  virtual int textWidthTrailingGap() const { return 0; }
  // Pixel width the pill from drawUnreadBadge(count) occupies — for reserving
  // the name column before it. Mirrors the pill's horizontal padding.
  int unreadBadgeWidth(int count) {
    char buf[5]; fmtBadgeCount(buf, count);
    int pad = sepH() + 1;
    return getTextWidth(buf) - textWidthTrailingGap() + pad * 2;
  }
  // Right-aligned unread-count "pill": a filled capsule ending at right_x,
  // aligned to a text row of height getLineHeight() at y, with the count
  // knocked out. On a selected/inverted row pass sel=true so the pill inverts
  // too (paper capsule + ink digits) and stays visible. The four corners are
  // knocked back to the surrounding colour for a rounded-capsule look.
  // Restores ink to LIGHT. Returns the pill width.
  int drawUnreadBadge(int right_x, int y, int count, bool sel) {
    char buf[5]; fmtBadgeCount(buf, count);
    int pad = sepH() + 1;
    int pw = getTextWidth(buf) - textWidthTrailingGap() + pad * 2;
    int ph = getLineHeight();
    int px = right_x - pw;
    Color body = sel ? DARK : LIGHT;
    Color ink  = sel ? LIGHT : DARK;
    setColor(body);
    fillRect(px, y, pw, ph);
    setColor(ink);
    fillRect(px, y, 1, 1);
    fillRect(px + pw - 1, y, 1, 1);
    fillRect(px, y + ph - 1, 1, 1);
    fillRect(px + pw - 1, y + ph - 1, 1, 1);
    setCursor(px + pad, y);
    print(buf);
    setColor(LIGHT);
    return pw;
  }

  // Inverted title bar: light background, dark ellipsized label, then the
  // standard separator line. The label is UTF-8 translated by
  // drawTextEllipsized. Leaves ink colour LIGHT for following content.
  // Pixel width the ≡ context-menu hint reserves at the header's right edge.
  int menuHintWidth() const { return getCharWidth() + 3; }

  // Small ≡ glyph at the header's top-right, signalling the screen has a
  // Hold-Enter context menu. Three stacked bars, drawn in colour c (LIGHT on a
  // plain header, DARK on an inverted bar). When active (the menu is open) the
  // corner cell is highlighted and the bars knocked out, tying the glyph to the
  // popup it spawned. Call after the header body.
  void drawContextMenuHint(Color c = LIGHT, bool active = false) {
    int gw = getCharWidth() + 1;
    int gx = width() - gw - 1;
    int th = sepH();
    int gy = (getLineHeight() - (th * 3 + 4)) / 2;
    if (gy < 0) gy = 0;
    Color bars = c;
    if (active) {
      setColor(c);
      fillRect(gx - 1, 0, gw + 2, headerH() - sepH());
      bars = (c == LIGHT) ? DARK : LIGHT;
    }
    setColor(bars);
    for (int i = 0; i < 3; i++) fillRect(gx, gy + i * (th + 2), gw, th);
    setColor(LIGHT);
  }

  void drawInvertedHeader(const char* label, bool menu_hint = false, bool menu_open = false) {
    int hdr = headerH();
    setColor(LIGHT);
    fillRect(0, 0, width(), hdr - 1);
    int reserve = menu_hint ? menuHintWidth() : 0;
    setColor(DARK);
    drawTextEllipsized(2, 1, width() - 4 - reserve, (label && label[0]) ? label : "");
    if (menu_hint) drawContextMenuHint(DARK, menu_open);
    setColor(LIGHT);
    fillRect(0, hdr - 1, width(), sepH());
  }

  // Centred screen title + bottom separator line — the standard top bar for
  // full-screen list/detail views. Leaves ink LIGHT; callers can draw extra
  // header content (counters, etc.) afterwards. menu_hint adds the ≡ glyph;
  // menu_open highlights it while the context menu is on screen.
  void drawCenteredHeader(const char* title, bool menu_hint = false, bool menu_open = false) {
    setColor(LIGHT);
    int reserve = menu_hint ? menuHintWidth() : 0;
    char buf[96];
    translateUTF8ToBlocks(buf, (title && title[0]) ? title : "", sizeof(buf));
    int avail = width() - reserve - 4;
    if (avail < 0) avail = 0;
    if (getTextWidth(buf) <= avail) {
      int w = getTextWidth(buf);
      setCursor((width() - reserve) / 2 - w / 2, 0);   // centred, clear of the ≡ hint
      print(buf);
    } else {
      // Too wide to centre without print() wrapping onto a second line → left-align
      // and ellipsize, like a list row. (Long DM / room-server / channel names.)
      drawTextEllipsized(2, 0, avail, buf);
    }
    fillRect(0, headerH() - sepH(), width(), sepH());
    if (menu_hint) drawContextMenuHint(LIGHT, menu_open);
  }

  // Advance a UTF-8 pointer by one codepoint, returning the decoded value.
  // Invalid sequences return 0xFFFD and consume trailing continuation bytes.
  static uint32_t decodeCodepoint(const uint8_t*& p) {
    uint8_t c = *p++;
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0) {
      uint32_t cp = c & 0x1F;
      if (*p) cp = (cp << 6) | (*p++ & 0x3F);
      return cp;
    }
    if ((c & 0xF0) == 0xE0) {
      uint32_t cp = c & 0x0F;
      if (*p) cp = (cp << 6) | (*p++ & 0x3F);
      if (*p) cp = (cp << 6) | (*p++ & 0x3F);
      return cp;
    }
    if ((c & 0xF8) == 0xF0) {
      uint32_t cp = c & 0x07;
      if (*p) cp = (cp << 6) | (*p++ & 0x3F);
      if (*p) cp = (cp << 6) | (*p++ & 0x3F);
      if (*p) cp = (cp << 6) | (*p++ & 0x3F);
      return cp;
    }
    while (*p && (*p & 0xC0) == 0x80) p++;
    return 0xFFFD;
  }

  // Width of a single codepoint in pixels. Default: fall back to getTextWidth
  // on a one-codepoint UTF-8 string. Drivers can override for O(1) lookup.
  virtual uint16_t getCodepointWidth(uint32_t cp) {
    char buf[5];
    int n = 0;
    if (cp < 0x80) { buf[n++] = (char)cp; }
    else if (cp < 0x800) { buf[n++] = 0xC0 | (cp >> 6); buf[n++] = 0x80 | (cp & 0x3F); }
    else if (cp < 0x10000) { buf[n++] = 0xE0 | (cp >> 12); buf[n++] = 0x80 | ((cp >> 6) & 0x3F); buf[n++] = 0x80 | (cp & 0x3F); }
    else { buf[n++] = 0xF0 | (cp >> 18); buf[n++] = 0x80 | ((cp >> 12) & 0x3F); buf[n++] = 0x80 | ((cp >> 6) & 0x3F); buf[n++] = 0x80 | (cp & 0x3F); }
    buf[n] = '\0';
    return getTextWidth(buf);
  }


  // draw text with ellipsis if it exceeds max_width
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char temp_str[256];  // reasonable buffer size
    translateUTF8ToBlocks(temp_str, str, sizeof(temp_str));

    // Fold newlines into spaces: this draws ONE line clipped to max_width, but
    // print() acts on '\n' by returning to x=0 one row down, which would spill
    // the tail onto whatever is drawn below. Message bodies (the compact
    // one-line previews in the history list) are the texts that carry them;
    // for labels and names this is a no-op. A space keeps the words apart and
    // measures the same, so the width/ellipsis maths below is unaffected.
    for (char* q = temp_str; *q; q++) if (*q == '\n' || *q == '\r') *q = ' ';

    if (getTextWidth(temp_str) <= max_width) {
      setCursor(x, y);
      print(temp_str);
      return;
    }
    
    // for variable-width fonts (GxEPD), add space after ellipsis
    // for fixed-width fonts (OLED), keep tight spacing to save precious characters
    const char* ellipsis;
    // use a simple heuristic: if 'i' and 'l' have different widths, it's variable-width
    if (_vw_dirty) {
      _vw_result = (getTextWidth("i") != getTextWidth("l"));
      _vw_dirty = false;
    }
    if (_vw_result) {
      ellipsis = "... ";  // variable-width fonts: add space
    } else {
      ellipsis = "...";   // fixed-width fonts: no space
    }
    
    int ellipsis_width = getTextWidth(ellipsis);
    int str_len = strlen(temp_str);
    
    while (str_len > 0 && getTextWidth(temp_str) > max_width - ellipsis_width) {
      temp_str[--str_len] = 0;
    }
    // Strip orphaned UTF-8 leading byte left by byte-at-a-time trimming above.
    while (str_len > 0 && ((uint8_t)temp_str[str_len - 1] & 0xC0) == 0xC0) {
      temp_str[--str_len] = 0;
    }
    strcat(temp_str, ellipsis);
    
    setCursor(x, y);
    print(temp_str);
  }
  
  virtual void setBrightness(uint8_t level) { }  // level 0-4 (min to max), no-op default
  virtual void setDisplayRotation(uint8_t rot) { }  // 0-3, no-op for fixed-orientation displays
  virtual void setFullRefreshInterval(uint8_t n) { }  // e-ink: do full refresh every n partial refreshes (0=never)
  virtual void endFrame() = 0;

#ifdef ENABLE_SCREENSHOT
  // Screenshot support — return raw framebuffer and its size in bytes.
  // 0=OLED (page-based, column-major), 1=e-ink (row-major, MSB-first, 1=white/0=black).
  virtual const uint8_t* getBuffer() { return nullptr; }
  virtual uint16_t getBufferSize() { return 0; }
  virtual uint8_t getDisplayType() { return 0; }
  // Visible pixel dimensions to embed in the screenshot header.
  // Override in e-ink drivers to return the GxEPD2-reported dimensions (which use
  // WIDTH_VISIBLE instead of the full physical WIDTH used by DisplayDriver).
  // OLED drivers: width()/height() already reflect the visible canvas, so no override needed.
  virtual int screenshotWidth()    { return width(); }
  virtual int screenshotHeight()   { return height(); }
  virtual uint8_t screenshotRotation() { return 0; }   // 0-3, GxEPD2/GFX rotation value
#endif
};
