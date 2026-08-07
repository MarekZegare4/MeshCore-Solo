#ifdef CARDPUTER_ADV

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ui/UIScreen.h>   // KEY_PREV / KEY_NEXT / KEY_ENTER / KEY_CANCEL

// --- TCA8418 registers ---
#define TCA8418_ADDR            0x34
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LCK_EC  0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KP_GPIO1    0x1D
#define TCA8418_REG_KP_GPIO2    0x1E
#define TCA8418_REG_KP_GPIO3    0x1F
#define TCA8418_REG_GPI_EM1     0x20
#define TCA8418_REG_GPI_EM2     0x21
#define TCA8418_REG_GPI_EM3     0x22
#define TCA8418_REG_DEBOUNCE    0x29

// --- modifier key event-kódok (nyers TCA8418 "code", key_index előtti érték) ---
#define TCA_CODE_TAB    2   // egyelőre nem használt
#define TCA_CODE_FN     3
#define TCA_CODE_CTRL   4   // egyelőre nincs funkció
#define TCA_CODE_SHIFT  7
#define TCA_CODE_OPT    8   // egyelőre nincs funkció
#define TCA_CODE_ALT    14  // egyelőre nincs funkció

// --- modifier állapot flag-ek ---
static bool mod_fn    = false;
static bool mod_ctrl  = false;
static bool mod_shift = false;
static bool mod_opt   = false;
static bool mod_alt   = false;

static void tca_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA8418_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t tca_read(uint8_t reg) {
  Wire.beginTransmission(TCA8418_ADDR);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom((uint8_t)TCA8418_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0;
}

// Map a TCA8418 key-event number to a UI value.
// normal
static const unsigned char keymap[7*8] = {
  '\'', KEY_CONTEXT_MENU, 0, 0,
  '1', 'q', 0, 0,
  '2', 'w', 'a', 0,
  '3', 'e', 's', 'z',
  '4', 'r', 'd', 'x',
  '5', 't', 'f', 'c',
  '6', 'y', 'g', 'v',
  '7', 'u', 'h', 'b',
  '8', 'i', 'j', 'n',
  '9', 'o', 'k', 'm',
  '0', 'p', 'l', ',',
  '_', '[', ';', '.',
  '=', ']', '\'', '/',
  '\b', '\\', KEY_ENTER, ' '
};
// Fn pressed
static const unsigned char keymapFN[7*8] = {
  KEY_CANCEL, KEY_CONTEXT_MENU, 0, 0,
  '1', 'q', 0, 0,
  '2', 'w', 'a', 0,
  '3', 'e', 's', 'z',
  '4', 'r', 'd', 'x',
  '5', 't', 'f', 'c',
  '6', 'y', 'g', 'v',
  '7', 'u', 'h', 'b',
  '8', 'i', 'j', 'n',
  '9', 'o', 'k', 'm',
  '0', 'p', 'l', KEY_LEFT,
  '_', '[', KEY_UP, KEY_DOWN,
  '=', ']', '\'', KEY_RIGHT,
  '\b', '\\', KEY_KB_ENTER, ' '
};
// Shift pressed
static const unsigned char keymapSH[7*8] = {
  '~', 0, 0, 0,
  '!', 'Q', 0, 0,
  '@', 'W', 'A', 0,
  '#', 'E', 'S', 'Z',
  '$', 'R', 'D', 'X',
  '%', 'T', 'F', 'C',
  '^', 'Y', 'G', 'V',
  '&', 'U', 'H', 'B',
  '*', 'I', 'J', 'N',
  '{', 'O', 'K', 'M',
  '}', 'P', 'L', '<',
  '-', '(', ':', '>',
  '+', ')', '"', '?',
  '\b', '|', KEY_SELECT, ' '
};


static void keypad_init() {
  tca_write(TCA8418_REG_CFG, 0x00);                 // scanner off
  for (int i = 0; i < 16; i++) {
    if ((tca_read(TCA8418_REG_KEY_LCK_EC) & 0x0F) == 0) break;
    tca_read(TCA8418_REG_KEY_EVENT_A);
  }
  tca_write(TCA8418_REG_INT_STAT, 0x1F);            // clear all int flags
  tca_write(TCA8418_REG_GPI_EM1, 0x00);
  tca_write(TCA8418_REG_GPI_EM2, 0x00);
  tca_write(TCA8418_REG_GPI_EM3, 0x00);
  tca_write(TCA8418_REG_KP_GPIO1, 0x7F);            // ROW0-ROW6 in keypad matrix 0b01111111
  tca_write(TCA8418_REG_KP_GPIO2, 0xFF);            // COL0-COL7 in keypad matrix 0b11111111
  tca_write(TCA8418_REG_KP_GPIO3, 0x00);            // COL8, COL9 
  tca_write(TCA8418_REG_DEBOUNCE, 0x03);            //
  tca_write(TCA8418_REG_INT_STAT, 0x1F);
  tca_write(TCA8418_REG_CFG, 0x11);                 // KE_IEN + INT_CFG, scanner on
  delay(5);
  for (int i = 0; i < 16; i++) {
    if ((tca_read(TCA8418_REG_KEY_LCK_EC) & 0x0F) == 0) break;
    tca_read(TCA8418_REG_KEY_EVENT_A);
  }
  tca_write(TCA8418_REG_INT_STAT, 0x1F);
}

char tca8418_keypad_read() {
  static bool inited = false;
  if (!inited) { keypad_init(); inited = true; }

  if ((tca_read(TCA8418_REG_KEY_LCK_EC) & 0x0F) == 0) return 0;  // FIFO empty

  uint8_t ev = tca_read(TCA8418_REG_KEY_EVENT_A);
  tca_write(TCA8418_REG_INT_STAT, 0x1F);            // clear int

  bool    pressed = (ev & 0x80) != 0;
  uint8_t code    = ev & 0x7F;

  // --- modifier kezelés: press ÉS release is számít, sosem küld karaktert ---
  switch (code) {
    case TCA_CODE_FN:    mod_fn    = pressed; return 0;
    case TCA_CODE_CTRL:  mod_ctrl  = pressed; return 0;
    case TCA_CODE_SHIFT: mod_shift = pressed; return 0;
    case TCA_CODE_OPT:   mod_opt   = pressed; return 0;
    case TCA_CODE_ALT:   mod_alt   = pressed; return 0;
    default: break;
  }

  if (!pressed) return 0;   // sima gomb release -> ignore

  uint8_t row = (code - 1) / 10;
  uint8_t col = (code - 1) % 10;
  uint8_t key_index = row * 8 + col;
  if (key_index >= 7*8) return 0;   // védelem, ha valamiért kilógna

  // --- prioritás: Fn > Shift > normal (ctrl/opt/alt egyelőre nincs saját map) ---
  const unsigned char* active_map = keymap;
  if (mod_fn)         active_map = keymapFN;
  else if (mod_shift)  active_map = keymapSH;

  return active_map[key_index];
}

#endif