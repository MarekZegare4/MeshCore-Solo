#pragma once

#include "DisplayDriver.h"

#define KEY_LEFT           0xB4
#define KEY_UP             0xB5
#define KEY_DOWN           0xB6
#define KEY_RIGHT          0xB7
#define KEY_SELECT           10
#define KEY_ENTER            13
#define KEY_CANCEL           27   // Esc
#define KEY_HOME           0xF0
#define KEY_NEXT           0xF1
#define KEY_PREV           0xF2
#define KEY_CONTEXT_MENU   0xF3
// A literal-ASCII input source's (e.g. an I2C CardKB) Enter, translated only
// when the on-screen keyboard's plain grid state is active (see
// UITask::pollCardKB()) -- means "submit the field", not KEY_ENTER's usual
// "commit whatever grid cell (row,col) is currently selected", which would
// otherwise insert a stray character since direct typing never touches the
// grid.
#define KEY_KB_ENTER       0x05

// "Previous"/"Next" navigation keys: the directional key and its rotary-encoder
// twin. Decrement / increment a value, or step a selection. (Cancel and the
// context-menu key stay screen-specific — they mean different things per screen.)
static inline bool keyIsPrev(char c) { return c == KEY_LEFT  || c == KEY_PREV; }
static inline bool keyIsNext(char c) { return c == KEY_RIGHT || c == KEY_NEXT; }

#ifndef JOYSTICK_ROTATION
  #define JOYSTICK_ROTATION 0
#endif

// Rotate directional key by rot steps clockwise (0–3).
// Pass NodePrefs::joystick_rotation at runtime; JOYSTICK_ROTATION macro is the compile-time default.
static inline char rotateJoystickKey(char key, uint8_t rot) {
  switch (rot & 3) {
    case 1:
      if ((uint8_t)key == KEY_UP)    return KEY_RIGHT;
      if ((uint8_t)key == KEY_RIGHT) return KEY_DOWN;
      if ((uint8_t)key == KEY_DOWN)  return KEY_LEFT;
      if ((uint8_t)key == KEY_LEFT)  return KEY_UP;
      break;
    case 2:
      if ((uint8_t)key == KEY_UP)    return KEY_DOWN;
      if ((uint8_t)key == KEY_DOWN)  return KEY_UP;
      if ((uint8_t)key == KEY_LEFT)  return KEY_RIGHT;
      if ((uint8_t)key == KEY_RIGHT) return KEY_LEFT;
      break;
    case 3:
      if ((uint8_t)key == KEY_UP)    return KEY_LEFT;
      if ((uint8_t)key == KEY_LEFT)  return KEY_DOWN;
      if ((uint8_t)key == KEY_DOWN)  return KEY_RIGHT;
      if ((uint8_t)key == KEY_RIGHT) return KEY_UP;
      break;
  }
  return key;
}

class UIScreen {
protected:
  UIScreen() { }
public:
  virtual int render(DisplayDriver& display) =0;   // return value is number of millis until next render
  virtual bool handleInput(char c) { return false; }
  virtual void poll() { }
  // Called by UITask::setCurrScreen() each time this screen becomes current —
  // the place to reset per-visit state (selection, dirty flag, sub-views).
  // Default no-op for screens that keep state across visits. Because it's
  // invoked centrally, a new screen can't "forget" to be reset on show.
  virtual void onShow() { }
  // One-button boards (ui-new/OneButton.h): the key a click stands for here --
  // the next row by default (lists wrap); a screen with tabs or pages says
  // KEY_RIGHT on its last row, so one click walks through everything.
  virtual char stepKey() { return KEY_DOWN; }
  // One-button boards: the selected row is a value changed with left/right, so
  // a long press edits it (click +1, double -1, long done) instead of Enter.
  virtual bool valueRow() { return false; }
  // One-button boards: the screen is in an edit mode of its own --
  //   EDIT_VALUE   left/right change it, Enter keeps it, Cancel drops it:
  //                click +1, double -1, long keeps, very long drops;
  //   EDIT_DIGITS  a DigitEditor: click the digit +1, double -1, long on to
  //                the next digit (KEY_NEXT; past the last, done), very long drops.
  enum EditMode : uint8_t { EDIT_NONE, EDIT_VALUE, EDIT_DIGITS };
  virtual uint8_t editMode() { return EDIT_NONE; }
};

