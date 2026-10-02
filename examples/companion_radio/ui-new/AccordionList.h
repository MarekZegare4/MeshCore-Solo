#pragma once
// Shared collapsible section list (accordion) — a Settings-style list whose
// rows are grouped under headers that expand/collapse in place. Owns the
// collapse bitmask, the flattened visible-row list, selection and scroll; the
// host screen only supplies the section sizes and two row painters.
//
// Reusable UI element (cf. drawList / DigitEditor / NavView): any screen that
// wants grouped, foldable rows drives it the same way —
//   _acc.begin(sizes, n);                       // once, on enter()
//   _acc.render(display, headerFn, itemFn);     // in render()
//   switch (_acc.handleInput(c)) { ... }        // in handleInput()
//
// Relies on drawList() + the KEY_* codes already pulled in by icons.h /
// UIScreen.h before this header (same include-order contract as the screens).
#include "icons.h"

class AccordionList {
public:
  // A visible row is either a section header (item < 0) or the item-th entry
  // within section `sec`.
  struct Row { int8_t sec; int8_t item; };

  enum Result { IGNORED, HANDLED, ACTIVATED };

  static const int MAX_SECTIONS = 8;   // collapse bitmask is 8 bits
  static const int MAX_ROWS     = 64;

  // (Re)initialise from per-section item counts. `collapsed` sets the initial
  // fold state for every section (Settings opens folded, so default true).
  void begin(const uint8_t* section_sizes, int section_count, bool collapsed = true) {
    _count = section_count > MAX_SECTIONS ? MAX_SECTIONS : section_count;
    for (int i = 0; i < _count; i++) _sizes[i] = section_sizes[i];
    _collapsed = collapsed ? 0xFF : 0x00;
    _sel = 0; _scroll = 0;
    rebuild();
  }

  bool collapsed(int sec) const        { return (_collapsed >> sec) & 1; }
  const Row& selected() const          { return _rows[_sel]; }

  // Paint via the shared drawList skeleton. `header(sec, y, sel, reserve,
  // collapsed)` and `item(sec, item, y, sel, reserve)` each draw one row
  // (including its own selection bar, as drawList's callers do).
  template <class HeaderFn, class ItemFn>
  void render(DisplayDriver& d, HeaderFn header, ItemFn item) {
    drawList(d, _vis_count, _sel, _scroll, [&](int idx, int y, bool sel, int reserve) {
      const Row& r = _rows[idx];
      if (r.item < 0) header(r.sec, y, sel, reserve, collapsed(r.sec));
      else            item(r.sec, r.item, y, sel, reserve);
    });
  }

  // Standard navigation. Returns ACTIVATED when Enter lands on an item (the
  // host then acts on selected()); HANDLED for nav / fold toggles; IGNORED
  // otherwise so the host can treat the key itself (e.g. Cancel).
  Result handleInput(char c) {
    if (c == KEY_UP)   { if (_vis_count) _sel = (_sel > 0) ? _sel - 1 : _vis_count - 1; return HANDLED; }
    if (c == KEY_DOWN) { if (_vis_count) _sel = (_sel + 1 < _vis_count) ? _sel + 1 : 0; return HANDLED; }
    if (c == KEY_ENTER) {
      if (!_vis_count) return HANDLED;
      if (_rows[_sel].item < 0) { toggle(_rows[_sel].sec); return HANDLED; }
      return ACTIVATED;
    }
    return IGNORED;
  }

  // Fold/unfold a section, keeping that section's header selected afterwards so
  // the cursor never jumps to an unrelated row when items appear/disappear.
  void toggle(int sec) {
    if (sec < 0 || sec >= _count) return;
    _collapsed ^= (uint8_t)(1u << sec);
    rebuild();
    for (int i = 0; i < _vis_count; i++)
      if (_rows[i].sec == sec && _rows[i].item < 0) { _sel = i; break; }
  }

private:
  uint8_t _sizes[MAX_SECTIONS];
  int     _count = 0;
  uint8_t _collapsed = 0xFF;
  Row     _rows[MAX_ROWS];
  int     _vis_count = 0;
  int     _sel = 0, _scroll = 0;

  void rebuild() {
    _vis_count = 0;
    for (int s = 0; s < _count; s++) {
      if (_vis_count < MAX_ROWS) _rows[_vis_count++] = { (int8_t)s, (int8_t)-1 };
      if (!collapsed(s))
        for (int it = 0; it < _sizes[s] && _vis_count < MAX_ROWS; it++)
          _rows[_vis_count++] = { (int8_t)s, (int8_t)it };
    }
    if (_sel >= _vis_count) _sel = _vis_count ? _vis_count - 1 : 0;
  }
};
