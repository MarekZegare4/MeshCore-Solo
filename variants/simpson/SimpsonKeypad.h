#pragma once

#include <Arduino.h>
#include <I2CKeyPad.h>

#ifndef SIMPSON_KEYPAD_ADDR
  #define SIMPSON_KEYPAD_ADDR 0x20
#endif

enum SimpsonKeypadMode : uint8_t {
  SIMPSON_KEYPAD_CONTROL,
  SIMPSON_KEYPAD_ABC,
  SIMPSON_KEYPAD_ABC_UPPER,
  SIMPSON_KEYPAD_NUMERIC
};

enum SimpsonKeypadKey : uint8_t {
  SIMPSON_KEY_NONE = 0,
  SIMPSON_KEY_CANCEL = 1,
  SIMPSON_KEY_UP,
  SIMPSON_KEY_BLUETOOTH,
  SIMPSON_KEY_LEFT,
  SIMPSON_KEY_ENTER,
  SIMPSON_KEY_RIGHT,
  SIMPSON_KEY_BRIGHTNESS_UP,
  SIMPSON_KEY_SYMBOLS,
  SIMPSON_KEY_DOWN,
  SIMPSON_KEY_BRIGHTNESS_DOWN,
  SIMPSON_KEY_MESSAGES,
  SIMPSON_KEY_FN,
  SIMPSON_KEY_GPS,
  SIMPSON_KEY_BACKSPACE,
  SIMPSON_KEY_MODE_CHANGED,
  SIMPSON_KEY_ENTER_LONG
};

class SimpsonKeypad {
  I2CKeyPad _keypad;
  SimpsonKeypadMode _mode = SIMPSON_KEYPAD_CONTROL;
  uint8_t _last_key = SIMPSON_KEY_NONE;
  uint32_t _enter_pressed_at = 0;
  bool _enter_long_sent = false;
  bool _ready = false;

  void loadModeMap();

public:
  SimpsonKeypad() : _keypad(SIMPSON_KEYPAD_ADDR, &Wire) {}
  bool begin();
  uint8_t poll();
  SimpsonKeypadMode mode() const { return _mode; }
};
