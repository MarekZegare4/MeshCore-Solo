#include "SimpsonKeypad.h"

static const uint32_t SIMPSON_LONG_PRESS_MS = 1200;

#include <Wire.h>

namespace {
char control_keymap[19] = {
  SIMPSON_KEY_CANCEL, SIMPSON_KEY_UP, 0, SIMPSON_KEY_BLUETOOTH,
  SIMPSON_KEY_LEFT, SIMPSON_KEY_ENTER, SIMPSON_KEY_RIGHT, SIMPSON_KEY_BRIGHTNESS_UP,
  SIMPSON_KEY_SYMBOLS, SIMPSON_KEY_DOWN, 0, SIMPSON_KEY_BRIGHTNESS_DOWN,
  SIMPSON_KEY_MESSAGES, 0, SIMPSON_KEY_FN, SIMPSON_KEY_GPS, 0, 0, 0
};

char alphabet_keymap[19] = {
  ',', 'A', 'D', SIMPSON_KEY_BLUETOOTH,
  'G', 'J', 'M', SIMPSON_KEY_BRIGHTNESS_UP,
  'P', 'T', 'W', SIMPSON_KEY_BRIGHTNESS_DOWN,
  SIMPSON_KEY_BACKSPACE, ' ', SIMPSON_KEY_FN, SIMPSON_KEY_GPS, 0, 0, 0
};

char numeric_keymap[19] = {
  '1', '2', '3', SIMPSON_KEY_BLUETOOTH,
  '4', '5', '6', SIMPSON_KEY_BRIGHTNESS_UP,
  '7', '8', '9', SIMPSON_KEY_BRIGHTNESS_DOWN,
  SIMPSON_KEY_BACKSPACE, '0', SIMPSON_KEY_FN, SIMPSON_KEY_GPS, 0, 0, 0
};
}

bool SimpsonKeypad::begin() {
  if (!_keypad.begin()) return false;
  _keypad.setKeyPadMode(I2C_KEYPAD_4x4);
  _keypad.setDebounceThreshold(50);
  loadModeMap();
  _ready = true;
  return true;
}

void SimpsonKeypad::loadModeMap() {
  switch (_mode) {
    case SIMPSON_KEYPAD_CONTROL:
      _keypad.loadKeyMap(control_keymap);
      break;
    case SIMPSON_KEYPAD_ABC:
    case SIMPSON_KEYPAD_ABC_UPPER:
      _keypad.loadKeyMap(alphabet_keymap);
      break;
    case SIMPSON_KEYPAD_NUMERIC:
      _keypad.loadKeyMap(numeric_keymap);
      break;
  }
}

uint8_t SimpsonKeypad::poll() {
  if (!_ready) return SIMPSON_KEY_NONE;

  uint8_t key = _keypad.getChar();
  if (key == I2C_KEYPAD_THRESHOLD) return SIMPSON_KEY_NONE;
  if (key == SIMPSON_KEY_NONE) {
    if (_last_key == SIMPSON_KEY_ENTER && !_enter_long_sent) {
      _last_key = SIMPSON_KEY_NONE;
      _enter_pressed_at = 0;
      return SIMPSON_KEY_ENTER;
    }
    _last_key = SIMPSON_KEY_NONE;
    _enter_pressed_at = 0;
    _enter_long_sent = false;
    return SIMPSON_KEY_NONE;
  }
  if (key == SIMPSON_KEY_ENTER && _last_key == SIMPSON_KEY_ENTER) {
    if (!_enter_long_sent && millis() - _enter_pressed_at >= SIMPSON_LONG_PRESS_MS) {
      _enter_long_sent = true;
      return SIMPSON_KEY_ENTER_LONG;
    }
    return SIMPSON_KEY_NONE;
  }
  if (key == _last_key) return SIMPSON_KEY_NONE;
  _last_key = key;

  if (key == SIMPSON_KEY_ENTER) {
    _enter_pressed_at = millis();
    _enter_long_sent = false;
    return SIMPSON_KEY_NONE;
  }

  if (key == SIMPSON_KEY_FN) {
    _mode = static_cast<SimpsonKeypadMode>((_mode + 1) % 4);
    loadModeMap();
    return SIMPSON_KEY_MODE_CHANGED;
  }
  return key;
}
