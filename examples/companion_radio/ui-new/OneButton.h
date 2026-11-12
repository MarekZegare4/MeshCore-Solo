#pragma once
// One-button gestures: a board with a single button (a bare Heltec V3/V4, the
// Watch13) runs the whole UI from it. Fed the button's raw state each loop, it
// times the presses itself, so the board's MomentaryButton events are not used:
//
//   CLICK      a short press, once the double-click window has passed
//   DOUBLE     two (or more) short presses inside the window
//   LONG       held past LONG_MS, let go before VERY_LONG_MS
//   VERY_LONG  held VERY_LONG_MS: fires while still held; the release is spent
//   HOLD_SOS   still held at HOLD_SOS_MS: SOS (the Back hold of a joystick board)
//
// UITask turns them into keys (see UITask::oneButtonGesture): CLICK the next
// row (the screen's stepKey()), DOUBLE back, LONG OK -- or, on a value row
// (valueRow()), the edit mode, where CLICK is +1, DOUBLE -1 and LONG done --
// VERY_LONG the context menu, or the unlock on the lock screen.
//
// No Arduino calls here: the sim feeds it from the page (sim_button) with the
// same timings.

#include <stdint.h>

class OneButton {
public:
  enum Gesture : uint8_t { NONE, CLICK, DOUBLE, LONG, VERY_LONG, HOLD_SOS };

  static const uint32_t DEBOUNCE_MS  = 25;
  static const uint32_t CLICK_GAP_MS = 300;    // a second press within this: a double
  static const uint32_t LONG_MS      = 600;
  static const uint32_t VERY_LONG_MS = 2000;
  static const uint32_t HOLD_SOS_MS  = 5000;

  // The raw state now; returns the gesture that completed, if any.
  Gesture update(bool pressed, uint32_t now) {
    if (pressed != _down && now - _edge_at >= DEBOUNCE_MS) {
      _down = pressed;
      _edge_at = now;
      if (pressed) {
        _down_at = now;
        _spent = _sos = false;
      } else if (!_spent) {
        if (now - _down_at >= LONG_MS) { _clicks = 0; return LONG; }
        _clicks++;
        _up_at = now;
      }
    }
    if (_down && _spent && !_sos && now - _down_at >= HOLD_SOS_MS) {
      _sos = true;
      return HOLD_SOS;
    }
    if (_down && !_spent && now - _down_at >= VERY_LONG_MS) {
      _spent = true;
      _clicks = 0;
      return VERY_LONG;
    }
    if (!_down && _clicks && now - _up_at >= CLICK_GAP_MS) {
      const uint8_t n = _clicks;
      _clicks = 0;
      return n == 1 ? CLICK : DOUBLE;
    }
    return NONE;
  }

  // Held long enough that letting go now is a LONG (for a cue on screen).
  bool holding(uint32_t now) const { return _down && !_spent && now - _down_at >= LONG_MS; }
  bool busy() const { return _down || _clicks; }

private:
  bool _down = false, _spent = false, _sos = false;
  uint8_t _clicks = 0;
  uint32_t _edge_at = 0, _down_at = 0, _up_at = 0;
};
