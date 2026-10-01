#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <cstdio>
#include <cstring>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

// DisplayDriver implementation for the native sim build. Shaped like
// src/helpers/ui/NullDisplayDriver.h (same pure-virtual overrides -- start
// from that file, per the Phase-1 plan) but instead of no-ops, maintains an
// in-memory framebuffer and prints it to stdout as ASCII/block-art on
// endFrame(), so the real UITask/menu system's actual draw calls are
// visible in a terminal.
//
// Logical canvas is 128x64 (matches NullDisplayDriver / a typical SSD1306
// OLED, so layout math in the real screens behaves exactly as on that
// hardware). For terminal rendering it's downsampled onto a coarser
// CELL_W x CELL_H-pixel grid:
//   - fillRect()/drawRect()/drawXbm() mark the cells they cover as "filled"
//     (drawXbm -- icons -- has no real bitmap to rasterize in ASCII, so it's
//     approximated as a solid block, same as a fillRect over that area).
//   - print() does NOT rasterize a bitmap font -- it places the real
//     characters of the real string into the grid at the (approximate)
//     cursor cell, which is what actually makes the output legible. Real
//     text always wins over a "filled" block in the same cell.
class SimDisplayDriver : public DisplayDriver {
  static const int CELL_W = 2;   // pixels per terminal column
  static const int CELL_H = 2;   // pixels per terminal row
  static const int COLS = 128 / CELL_W;   // 64
  static const int ROWS = 64  / CELL_H;   // 32

  bool _on = false;
  int _cursor_x = 0, _cursor_y = 0;
  Color _color = LIGHT;
  bool _filled[ROWS][COLS];
  char _text[ROWS][COLS];     // 0 = no character placed
  bool _dirty = false;
  int _frame_no = 0;

  void cellOf(int px, int py, int& cx, int& cy) const {
    cx = px / CELL_W; cy = py / CELL_H;
  }

public:
  SimDisplayDriver() : DisplayDriver(128, 64) { clearBuffers(); }

  bool begin() { _on = true; return true; }

  void clearBuffers() {
    memset(_filled, 0, sizeof(_filled));
    memset(_text, 0, sizeof(_text));
  }

  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override { _on = false; }
  void clear() override { clearBuffers(); }

  void startFrame(Color bkg = DARK) override {
    clearBuffers();
    _color = LIGHT;
  }

  void setTextSize(int sz) override { /* one fixed size in ASCII output */ }

  void setColor(Color c) override { _color = c; }

  void setCursor(int x, int y) override { _cursor_x = x; _cursor_y = y; }

  void print(const char* str) override {
    if (!str) return;
    int cx, cy;
    cellOf(_cursor_x, _cursor_y, cx, cy);
    int col = cx;
    for (const char* p = str; *p; p++) {
      if (*p == '\n') { cy++; col = cx; continue; }
      if (col >= 0 && col < COLS && cy >= 0 && cy < ROWS) {
        _text[cy][col] = (*p >= 32 && *p < 127) ? *p : '?';
      }
      col++;
    }
    // advance cursor horizontally by the printed width, like a real display
    _cursor_x += getTextWidth(str);
  }

  void fillRect(int x, int y, int w, int h) override { markRect(x, y, w, h); }
  void drawRect(int x, int y, int w, int h) override {
    markRect(x, y, w, 1);
    markRect(x, y + h - 1, w, 1);
    markRect(x, y, 1, h);
    markRect(x + w - 1, y, 1, h);
  }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    markRect(x, y, w, h);   // icon placeholder: solid block (see class comment)
  }

  uint16_t getTextWidth(const char* str) override {
    return str ? (uint16_t)(strlen(str) * getCharWidth()) : 0;
  }

  void endFrame() {
    printf("\n===== SimDisplayDriver frame #%d =====\n", _frame_no++);
    printf("+");
    for (int c = 0; c < COLS; c++) printf("-");
    printf("+\n");
    for (int r = 0; r < ROWS; r++) {
      printf("|");
      for (int c = 0; c < COLS; c++) {
        char ch = _text[r][c];
        if (ch) putchar(ch);
        else if (_filled[r][c]) putchar('#');
        else putchar(' ');
      }
      printf("|\n");
    }
    printf("+");
    for (int c = 0; c < COLS; c++) printf("-");
    printf("+\n");
    fflush(stdout);
  }

private:
  void markRect(int x, int y, int w, int h) {
    bool lit = (_color != DARK);
    int cx0, cy0, cx1, cy1;
    cellOf(x, y, cx0, cy0);
    cellOf(x + w - 1, y + h - 1, cx1, cy1);
    for (int r = cy0; r <= cy1; r++) {
      if (r < 0 || r >= ROWS) continue;
      for (int c = cx0; c <= cx1; c++) {
        if (c < 0 || c >= COLS) continue;
        _filled[r][c] = lit;
      }
    }
  }
};

#ifdef __EMSCRIPTEN__
// ---------------------------------------------------------------------
// Phase 2 (Emscripten): DisplayDriver backend that draws to a real
// HTML5 <canvas> instead of dumping ASCII art to stdout. SimDisplayDriver
// above is left completely untouched -- the native build still links that
// class (see variants/sim/platformio.ini's DISPLAY_CLASS=SimDisplayDriver);
// this class is only selected when DISPLAY_CLASS=SimDisplayDriverCanvas is
// set by the Emscripten build (variants/sim/build_wasm.sh).
//
// Design choice: draw straight through the browser's canvas 2D API on every
// draw call (fillRect/strokeRect/fillText), rather than building an offscreen
// RGBA framebuffer in linear memory and blitting it with putImageData(). The
// canvas 2D approach was simpler and more robust for this app's actual draw
// call shape:
//   - print() needs real text rendering (variable glyphs, marquee/ellipsis
//     logic in DisplayDriver.h measures via getTextWidth()) -- letting the
//     browser's own font rasterizer draw it is both less code and crisper
//     than hand-rolling a bitmap font + blit.
//   - Every draw happens synchronously within one call to startFrame()..
//     endFrame() inside a single JS "tick" (called from the Emscripten main
//     loop -- see sim_main.cpp) -- the browser never paints a partial canvas
//     mid-tick, so there's no tearing/flicker risk from not double-buffering
///    in C++ first.
//   - putImageData() would still need *something* to rasterize text and
//     icons into an RGBA buffer first -- it doesn't remove that work, it
///    only relocates it into C++ for no real benefit here.
//
// Each call reaches into the DOM via EM_ASM (synchronous, main-thread JS --
// fine since this build has no pthreads/proxying). The canvas is looked up
// by id once in begin() and cached on a per-instance JS property (see below)
// so every later call is one property read, not a fresh getElementById().
//
// Phase 3 addendum: cached on Module.__simCtx, NOT window.__simCtx as this
// class originally did in Phase 2. Phase 2 only ever ran one instance on a
// page, so a plain `window` global was invisible/harmless as a design smell;
// Phase 3 loads multiple MeshCoreSim()/MeshCoreSimRepeater() instances on
// ONE page, and `window` is the single real browser global shared by every
// one of them (MODULARIZE isolates each instance's own Module/wasm linear
// memory, but NOT the DOM/window) -- two instances' begin() calls would
// stomp the same window.__simCtx in turn, and both would end up drawing
// through whichever one won. `Module` itself, by contrast, IS a distinct
// object per instance (that's the whole point of MODULARIZE) and is already
// reachable from inside EM_ASM here as the current instance's own Module
// (same access pattern SimFS.h's sim_fs_mount_idbfs()/SimInstance.h's
// sim_instance_salt() already rely on for Module['simInstanceTag']), so
// storing it there instead scopes it correctly per instance for free.
//
// The canvas element id is ALSO made per-instance the same way: an untagged
// instance (no Module['simInstanceTag'], e.g. Phase 2's original
// single-instance web/index.html harness) still looks for plain
// "sim-canvas", byte-for-byte the pre-Phase-3 behavior; a tagged instance
// (Module['simInstanceTag'] = 'A', from a Phase 3 multi-instance host page
// like web/mesh.html) looks for "sim-canvas-A" instead, so two instances on
// one page never fight over the same <canvas> element either.
class SimDisplayDriverCanvas : public DisplayDriver {
  bool _on = false;
  int _cursor_x = 0, _cursor_y = 0;
  Color _color = LIGHT;
  int _text_sz = 1;

public:
  SimDisplayDriverCanvas() : DisplayDriver(128, 64) { }

  bool begin() {
    _on = true;
    EM_ASM({
      var tag = (typeof Module !== 'undefined' && Module['simInstanceTag']) ? Module['simInstanceTag'] : '';
      var id = tag ? ('sim-canvas-' + tag) : 'sim-canvas';
      var c = document.getElementById(id);
      if (!c) { console.error('[sim] #' + id + ' not found in the host page'); return; }
      Module.__simCtx = c.getContext('2d');
      Module.__simCtx.imageSmoothingEnabled = false;
    });
    return true;
  }

  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override {
    _on = false;
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = '#000';
      Module.__simCtx.fillRect(0, 0, 128, 64);
    });
  }
  void clear() override { turnOff(); _on = true; }

  void startFrame(Color bkg = DARK) override {
    _color = LIGHT;
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = '#000';
      Module.__simCtx.fillRect(0, 0, 128, 64);
    });
  }

  void setTextSize(int sz) override { _text_sz = sz; }
  void setColor(Color c) override { _color = c; }
  void setCursor(int x, int y) override { _cursor_x = x; _cursor_y = y; }

  // MiscFixed's real metrics (6px advance, 9px row height -- see
  // src/helpers/ui/MiscFixedFont.h), scaled by the current text size, same
  // as a real board's SSD1306Display::getCharWidth()/getLineHeight() do.
  // DisplayDriver.h's own defaults (6/8, unscaled) would make the Clock
  // screen's setTextSize(2)/(4) big-digit layout math (drawBig()'s width
  // centring, line spacing) come out wrong -- half-size digits crowded on
  // top of each other -- even though print() itself renders them at the
  // right size once _text_sz reaches it (see target.cpp).
  int getCharWidth() const override { return 6 * _text_sz; }
  int getLineHeight() const override { return 9 * _text_sz; }

  // Misc-fixed 6x9 is this backend's one and only font, exactly like a real
  // SSD1306Display/SH1106Display built with OLED_MISC_FIXED_FONT=1 (both
  // return true here too). UITask's status-bar indicator height keys off
  // this (`ind_h = display.isSingleFont() ? lh - 2 : lh`, UITask.cpp) -- left
  // at the base class's false, the sim drew that row 2px taller than the
  // real board it's mirroring.
  bool isSingleFont() const override { return true; }

  // White-on-black palette (matches a real monochrome SSD1306/SH1106 OLED)
  // for LIGHT/DARK; the other Color enumerators (RED/GREEN/BLUE/YELLOW/
  // ORANGE) aren't used on the real monochrome OLED boards this sim mirrors
  // either (DisplayDriver.h's own comment: "on b/w screen, colors will be
  // !=0 synonym of light").
  static const char* jsColor(Color c) { return c == DARK ? "#000" : "#fff"; }

  // Real bitmap-font rendering (byte-identical glyphs to a real MeshCore-Solo
  // board with OLED_MISC_FIXED_FONT=1 -- see solo/heltec_v3/platformio.ini),
  // not the browser's own system font -- defined out-of-line in target.cpp,
  // the one Emscripten-only translation unit allowed to pull in
  // MiscFixedRenderer.h (its own header comment: including it from more than
  // one .cpp would duplicate the font's static const tables in each).
  void print(const char* str) override;

  // Real MiscFixedFont covers the full glyph set print() draws above, so
  // unlike DisplayDriver's base assumption ("no extended glyphs -- fall back
  // to transliterateCodepoint()'s ASCII substitution"), this driver never
  // needs that. Matches a real board's SH1106Display/SSD1306Display, whose
  // own translateUTF8ToBlocks() override (gated on _single_font) does the
  // same plain passthrough once OLED_MISC_FIXED_FONT is enabled. Without
  // this override here, anything routed through translateUTF8ToBlocks()
  // (KeyboardWidget's live-typing preview line, screen titles, ...) would
  // inherit the base class's ASCII transliteration and silently strip every
  // accented character down to its plain-Latin base -- e.g. typing "ó" via
  // the accent-picker popup would show "o" in the text-entry preview even
  // though the popup itself (which prints its own variants directly, not
  // through this path) and the underlying buffer both had it right.
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    size_t n = strlen(src);
    if (n >= dest_size) n = dest_size - 1;
    memcpy(dest, src, n);
    dest[n] = '\0';
  }

  void fillRect(int x, int y, int w, int h) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = UTF8ToString($4) === 'L' ? '#fff' : '#000';
      Module.__simCtx.fillRect($0, $1, $2, $3);
    }, x, y, w, h, (_color != DARK) ? "L" : "D");
  }

  void drawRect(int x, int y, int w, int h) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      var ctx = Module.__simCtx;
      ctx.strokeStyle = UTF8ToString($4) === 'L' ? '#fff' : '#000';
      ctx.lineWidth = 1;
      ctx.strokeRect($0 + 0.5, $1 + 0.5, $2 - 1, $3 - 1);
    }, x, y, w, h, (_color != DARK) ? "L" : "D");
  }

  // Real XBM bit-unpacking (row-major, MSB-first, rows padded to whole
  // bytes) -- same convention every real DisplayDriver's drawXbm() already
  // assumes (see e.g. src/helpers/ui/ST7789Display.cpp's own drawXbm(),
  // `0x80 >> (bx & 7)` against `widthInBytes = (w+7)/8`). A canvas can
  // afford to rasterize the real icon pixels cheaply, unlike the native
  // ASCII backend's solid-block placeholder (no ASCII resolution for that).
  // `bits` is a pointer into wasm linear memory; EM_ASM passes it through as
  // a plain integer and the JS side indexes HEAPU8 with it directly.
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      var ctx = Module.__simCtx;
      var x0 = $0;
      var y0 = $1;
      var w = $2;
      var h = $3;
      var bits = $4;
      var lit = UTF8ToString($5) === 'L';
      var widthInBytes = (w + 7) >> 3;
      ctx.fillStyle = lit ? '#fff' : '#000';
      for (var ry = 0; ry < h; ry++) {
        for (var rx = 0; rx < w; rx++) {
          var byteOff = bits + ry * widthInBytes + (rx >> 3);
          var mask = 0x80 >> (rx & 7);
          if (HEAPU8[byteOff] & mask) ctx.fillRect(x0 + rx, y0 + ry, 1, 1);
        }
      }
    }, x, y, w, h, bits, (_color != DARK) ? "L" : "D");
  }

  // Measured off the real MiscFixed glyph table, per CODEPOINT -- not
  // strlen() * 6, which counts UTF-8 BYTES. Since translateUTF8ToBlocks()
  // above stopped transliterating accents away, strings reaching here really
  // do carry multi-byte sequences, and a byte count made every accented
  // character measure double: mis-centred titles, text ellipsized/marquee'd
  // far too early, right-aligned badges pushed off. Same implementation the
  // real single-font OLED drivers use (SH1106Display::getTextWidth() ->
  // miscFixedTextWidth()). Defined out-of-line in target.cpp for the same
  // reason print() is -- only that TU may include MiscFixedRenderer.h.
  uint16_t getTextWidth(const char* str) override;
  // O(1) single-glyph advance, mirroring SSD1306Display::getCodepointWidth()
  // -> glyphXAdvance(). The base class would otherwise re-encode the
  // codepoint and call getTextWidth() on it.
  uint16_t getCodepointWidth(uint32_t cp) override;

  // The canvas is the frame (128x64 native, CSS-scaled), so the slide reads
  // it back: the kept rows now, the new ones at compose time.
  bool slideBegin(int y0) override {
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simSlideY = $0;
      Module.__simSlide = Module.__simCtx.getImageData(0, $0, 128, 64 - $0);
    }, y0);
    return true;
  }
  void slideCompose(int dx) override {
    EM_ASM({
      var c = Module.__simCtx;
      var old = Module.__simSlide;
      if (!c || !old) return;
      var y = Module.__simSlideY;
      var fresh = c.getImageData(0, y, 128, 64 - y);
      c.putImageData(old, -$0, y);
      c.putImageData(fresh, $0 >= 0 ? 128 - $0 : -128 - $0, y);
    }, dx);
  }

  // Every draw call above already lands directly on the visible canvas
  // (see the class comment) -- nothing left to flush.
  void endFrame() override { }
};

// 320x240 colour touch LCD for the ui-lvgl frontend (Wio Tracker L2 shape).
// LVGL renders everything itself; this driver only owns the canvas and power
// state. blit() takes an RGB565 area straight from LVGL's flush callback
// (ui-lvgl/LvglPort.h) and putImageData()s it. The DisplayDriver text/shape
// API is a no-op -- only main.cpp's "Loading..." splash calls it.
//
// Touch comes from the host page: sim_lcd_touch(x, y, down) on mouse/pointer
// events, read back by LvglPort.h through touchState().
class SimLcdDisplay : public DisplayDriver {
  bool _on = false;
public:
  static const int W = 320, H = 240;
  SimLcdDisplay() : DisplayDriver(W, H) { }

  struct Touch { int x, y; bool down; };
  static Touch& touchState() { static Touch t = {0, 0, false}; return t; }

  bool begin() {
    _on = true;
    EM_ASM({
      var tag = (typeof Module !== 'undefined' && Module['simInstanceTag']) ? Module['simInstanceTag'] : '';
      var id = tag ? ('sim-canvas-' + tag) : 'sim-canvas';
      var c = document.getElementById(id);
      if (!c) { console.error('[sim] #' + id + ' not found in the host page'); return; }
      Module.__simCtx = c.getContext('2d');
      Module.__simCtx.fillStyle = '#000';
      Module.__simCtx.fillRect(0, 0, $0, $1);
    }, W, H);
    return true;
  }

  // RGB565 (little-endian, as LVGL renders it) -> RGBA -> canvas.
  void blit(int x, int y, int w, int h, const uint16_t* px) {
    if (!_on) return;
    EM_ASM({
      if (!Module.__simCtx) return;
      // (no bare commas: EM_ASM is a macro)
      var src = $4 >> 1;
      var n = $2 * $3;
      var img = Module.__simCtx.createImageData($2, $3);
      var d = img.data;
      for (var i = 0; i < n; i++) {
        var c = HEAPU16[src + i];
        var r = (c >> 11) & 0x1F;
        var g = (c >> 5) & 0x3F;
        var b = c & 0x1F;
        var o = i * 4;
        d[o] = (r << 3) | (r >> 2);
        d[o + 1] = (g << 2) | (g >> 4);
        d[o + 2] = (b << 3) | (b >> 2);
        d[o + 3] = 255;
      }
      Module.__simCtx.putImageData(img, $0, $1);
    }, x, y, w, h, px);
  }

  bool isOn() override { return _on; }
  void turnOn() override { _on = true; }
  void turnOff() override {
    _on = false;
    EM_ASM({
      if (!Module.__simCtx) return;
      Module.__simCtx.fillStyle = '#000';
      Module.__simCtx.fillRect(0, 0, $0, $1);
    }, W, H);
  }
  void clear() override { }
  void startFrame(Color bkg = DARK) override { (void)bkg; }
  void setTextSize(int sz) override { (void)sz; }
  void setColor(Color c) override { (void)c; }
  void setCursor(int x, int y) override { (void)x; (void)y; }
  void print(const char* str) override { (void)str; }
  void fillRect(int x, int y, int w, int h) override { (void)x; (void)y; (void)w; (void)h; }
  void drawRect(int x, int y, int w, int h) override { (void)x; (void)y; (void)w; (void)h; }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { (void)x; (void)y; (void)bits; (void)w; (void)h; }
  uint16_t getTextWidth(const char* str) override { return str ? strlen(str) * 6 : 0; }
  void endFrame() override { }
};

extern "C" EMSCRIPTEN_KEEPALIVE inline void sim_lcd_touch(int x, int y, int down) {
  SimLcdDisplay::Touch& t = SimLcdDisplay::touchState();
  t.x = x; t.y = y; t.down = down != 0;
}
#endif // __EMSCRIPTEN__
