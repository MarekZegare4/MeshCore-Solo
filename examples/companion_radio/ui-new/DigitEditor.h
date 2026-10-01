#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <Arduino.h>

// Digit-by-digit numeric editor: LEFT/RIGHT moves a cursor between decimal
// places (most-significant first), UP/DOWN adds/subtracts that place's value
// (100, 10, 1, 0.1, ...). Every place is directly addressable regardless of
// the starting value's own fractional offset — unlike a fixed step size
// nudging the raw value, which carries whatever odd fraction the starting
// value had forever, making different starting points land on different,
// unpredictable grids. Caller owns persisting the result; this widget only
// edits its own `value` in place.
struct DigitEditor {
  float value = 0.0f;
  float min_val = 0.0f, max_val = 0.0f;
  uint8_t int_digits = 0;   // digits before the decimal point
  uint8_t dec_digits = 0;   // digits after the decimal point
  int8_t  cursor = 0;       // 0 = most-significant digit
  bool    active = false;   // must default false: an embedding screen's input
                             // handler checks this before any begin() call

  enum Result { NONE, DONE, CANCELLED };

  void begin(float v, float vmin, float vmax, uint8_t intd, uint8_t decd) {
    value = v; min_val = vmin; max_val = vmax;
    int_digits = intd; dec_digits = decd;
    cursor = 0; active = true;
  }

  int totalDigits() const { return (int)int_digits + (int)dec_digits; }

  // Place value for the digit at `pos` (0 = most-significant integer digit),
  // e.g. int_digits=3 → pos 0/1/2 = hundreds/tens/units, pos 3.. = tenths/...
  float placeValue(int pos) const {
    int exp = (int)int_digits - 1 - pos;
    float v = 1.0f;
    if (exp >= 0) { for (int i = 0; i < exp; i++) v *= 10.0f; }
    else          { for (int i = 0; i < -exp; i++) v /= 10.0f; }
    return v;
  }

  Result handleInput(char c) {
    if (!active) return CANCELLED;
    if (keyIsPrev(c)) { if (cursor > 0) cursor--; return NONE; }
    if (keyIsNext(c)) { if (cursor < totalDigits() - 1) cursor++; return NONE; }
    if (c == KEY_UP || c == KEY_DOWN) {
      float step = placeValue(cursor);
      float next = value + (c == KEY_UP ? step : -step);
      // Snap to the editor's own decimal precision so repeated +/- can't
      // drift off-grid from float rounding.
      float scale = 1.0f;
      for (int i = 0; i < dec_digits; i++) scale *= 10.0f;
      next = roundf(next * scale) / scale;
      if (next >= min_val && next <= max_val) value = next;
      return NONE;
    }
    if (c == KEY_ENTER)  { active = false; return DONE; }
    if (c == KEY_CANCEL) { active = false; return CANCELLED; }
    return NONE;
  }

  // Draws `value` at (x, y) with the digit under the cursor shown inverted
  // (small DARK box, LIGHT glyph). Assumes the caller is already inside a
  // selected row (ink DARK on a LIGHT selection bar); restores LIGHT after.
  void render(DisplayDriver& d, int x, int y) {
    char buf[16];
    // Zero-pad the integer part to int_digits so the glyph under the cursor is
    // always the place it addresses: the cursor maps place values (100, 10, 1,
    // 0.1…), so a value with fewer integer digits than int_digits would shift
    // every glyph left and misplace the highlight (e.g. "62.500" vs "062.500").
    const int width = (int)int_digits + (dec_digits > 0 ? 1 + (int)dec_digits : 0);
    snprintf(buf, sizeof(buf), "%0*.*f", width, (int)dec_digits, value);
    const int cw = d.getCharWidth();
    const int char_idx = cursor < int_digits ? cursor : cursor + 1;  // skip '.'
    for (int k = 0; buf[k]; k++) {
      int cx = x + k * cw;
      if (k == char_idx) {
        d.setColor(DisplayDriver::DARK);
        d.fillSoftRect(cx, y - 1, cw, d.getLineHeight() + 1);
        d.setColor(DisplayDriver::LIGHT);
      } else {
        d.setColor(DisplayDriver::DARK);
      }
      char one[2] = { buf[k], '\0' };
      d.setCursor(cx, y);
      d.print(one);
    }
    d.setColor(DisplayDriver::LIGHT);
  }
};
