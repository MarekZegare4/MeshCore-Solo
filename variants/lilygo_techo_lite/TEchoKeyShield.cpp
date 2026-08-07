#ifdef LILYGO_TECHO_LITE_KEYSHIELD

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ui/UIScreen.h>   // KEY_PREV / KEY_NEXT / KEY_ENTER / KEY_CANCEL

// ---- TCA8418 (KeyShield keypad) --------------------------------------------
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

#define MULTI_TAP_THRESHOLD   300 // multitap threshold csúszó ablak ideje
#define LONG_PRESS_THRESHOLD  600 // longpress threshold idő

// ---- Multi-tap / long-press állapotgép -------------------------------------

// true  : folyamatos mód – minden koppintáskor azonnal küld backspace-t + új karaktert
//         (gyors, sima kijelzőknek jó, "élőben" látszik a T9 ciklus)
// false : halasztott mód – csak akkor küldi a végleges karaktert, ha lejárt a
//         MULTI_TAP_THRESHOLD (e-ink-hez ajánlott)
static bool sendBackSpace = false;

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

// Num chars per key (T9 ciklus hossz)
static uint8_t KeyTapMod[5*4] = {
  1, 5, 1, 5,
  1, 9, 7, 9,
  1, 7, 7, 7,
  1, 1, 7, 7,
  1, 1, 1, 1
};

static const unsigned char keymap[5*4][9] = {
  {' '},
  {'.', ',', '?', '!', '*'},
  {'0'},
  {':', ';', '-', '+', '#'},
  {'\b'},
  {'p', 'q', 'r', 's', 'P', 'Q', 'R', 'S', '7'},
  {'t', 'u', 'v', 'T', 'U', 'V', '8'},
  {'w', 'x', 'y', 'z', 'W', 'X', 'Y', 'Z', '9'},
  {KEY_DOWN},
  {'g', 'h', 'i', 'G', 'H', 'I', '4'},
  {'j', 'k', 'l', 'J', 'K', 'L', '5'},
  {'m', 'n', 'o', 'M', 'N', 'O', '6'},
  {KEY_ENTER},
  {'1'},
  {'a', 'b', 'c', 'A', 'B', 'C', '2'},
  {'d', 'e', 'f', 'D', 'E', 'F', '3'},
  {KEY_UP},
  {KEY_CANCEL},
  {KEY_HOME},
  {KEY_CONTEXT_MENU}
};

// long press
static const unsigned char keymapLongPress[5*4] = {
  0, 0, 0, 0,
  0, 0, 0, 0,
  KEY_LEFT, 0, 0, 0,
  KEY_KB_ENTER, 0, 0, 0,
  KEY_RIGHT, KEY_CANCEL, KEY_HOME, KEY_CONTEXT_MENU
};

void tca8418_keypad_set_backspace_mode(bool continuous) {
  sendBackSpace = continuous;
}

// kimeneti kör-FIFO, mert egy koppintás akár 2 karaktert is eredményezhet
// (backspace + új kar) folyamatos módban
#define KEY_OUT_QUEUE_LEN 4
static char    s_outQueue[KEY_OUT_QUEUE_LEN];
static uint8_t s_outHead = 0, s_outTail = 0;

static void queue_push(char c) {
  uint8_t next = (s_outTail + 1) % KEY_OUT_QUEUE_LEN;
  if (next != s_outHead) { s_outQueue[s_outTail] = c; s_outTail = next; }
}

static bool queue_pop(char &c) {
  if (s_outHead == s_outTail) return false;
  c = s_outQueue[s_outHead];
  s_outHead = (s_outHead + 1) % KEY_OUT_QUEUE_LEN;
  return true;
}

// aktuálisan lenyomva tartott (fizikai) billentyű, a long-press méréshez
static uint8_t     s_downKeyIndex = 0xFF;
static unsigned long s_downTime   = 0;

// függőben lévő (még nem lezárt) multi-tap ciklus
static uint8_t       s_pendingIndex   = 0xFF; // 0xFF = nincs függő ciklus
static uint8_t       s_pendingTap     = 0;    // aktuális tap-index (0-alapú)
static unsigned long s_pendingTapTime = 0;

static void keypad_init() {
  tca_write(TCA8418_REG_CFG, 0x00);
  for (int i = 0; i < 16; i++) {
    if ((tca_read(TCA8418_REG_KEY_LCK_EC) & 0x0F) == 0) break;
    tca_read(TCA8418_REG_KEY_EVENT_A);
  }
  tca_write(TCA8418_REG_INT_STAT, 0x1F);
  tca_write(TCA8418_REG_GPI_EM1, 0x00);
  tca_write(TCA8418_REG_GPI_EM2, 0x00);
  tca_write(TCA8418_REG_GPI_EM3, 0x00);
  tca_write(TCA8418_REG_KP_GPIO1, 0x1F);  // ROW0-ROW4 in keypad matrix 0b00011111
  tca_write(TCA8418_REG_KP_GPIO2, 0x0F);  // COL0-COL3 in keypad matrix 0b00001111
  tca_write(TCA8418_REG_KP_GPIO3, 0x00);  // COL8, COL9 
  tca_write(TCA8418_REG_DEBOUNCE, 0x03);
  tca_write(TCA8418_REG_INT_STAT, 0x1F);
  tca_write(TCA8418_REG_CFG, 0x11);
  delay(5);
  for (int i = 0; i < 16; i++) {
    if ((tca_read(TCA8418_REG_KEY_LCK_EC) & 0x0F) == 0) break;
    tca_read(TCA8418_REG_KEY_EVENT_A);
  }
  tca_write(TCA8418_REG_INT_STAT, 0x1F);
}

// lezár egy függő multi-tap ciklust: halasztott módban ilyenkor kell
// kiküldeni a "végleges" kart, mert eddig semmi nem ment ki érte
static void commit_pending_if_needed() {
  if (s_pendingIndex == 0xFF) return;
  if (!sendBackSpace) {
    uint8_t idx = s_pendingTap % KeyTapMod[s_pendingIndex];
    queue_push(keymap[s_pendingIndex][idx]);
  }
  s_pendingIndex = 0xFF;
}

char tca8418_keypad_read() {
  static bool inited = false;
  if (!inited) { keypad_init(); inited = true; }

  unsigned long now = millis();

  // 1) lejárt-e a függő multi-tap ciklus? (nem kap I2C eseményt, ezért
  //    minden híváskor ellenőrizni kell)
  if (s_pendingIndex != 0xFF && (now - s_pendingTapTime) >= MULTI_TAP_THRESHOLD) {
    commit_pending_if_needed();
  }

  // 2) van-e még kiküldendő karakter a pufferben?
  char qc;
  if (queue_pop(qc)) return qc;

  // 3) hardver FIFO
  if ((tca_read(TCA8418_REG_KEY_LCK_EC) & 0x0F) == 0) return 0;

  uint8_t ev = tca_read(TCA8418_REG_KEY_EVENT_A);
  tca_write(TCA8418_REG_INT_STAT, 0x1F);

  bool    pressed = (ev & 0x80) != 0;
  uint8_t code    = ev & 0x7F;
  uint8_t row     = (code - 1) / 10;
  uint8_t col     = (code - 1) % 10;
  uint8_t key_index = row * 4 + col;
  if (key_index >= 5*4) return 0;

  if (pressed) {
    // csak eltároljuk, a döntés (rövid/hosszú) a release-nél történik
    s_downKeyIndex = key_index;
    s_downTime     = now;
    return 0;
  }

  // --- release ---
  if (s_downKeyIndex != key_index) return 0;  // eltévedt/pár nélküli release
  unsigned long duration = now - s_downTime;
  s_downKeyIndex = 0xFF;

  bool longEligible = keymapLongPress[key_index] != 0;
  bool isLong = longEligible && duration >= LONG_PRESS_THRESHOLD;

  if (isLong) {
    if (sendBackSpace && s_pendingIndex == key_index) {
      queue_push('\b');           // az előzőleg megjelenített preview karakter törlése
    } else {
      commit_pending_if_needed(); // más gombra vonatkozó függő ciklus lezárása
    }
    s_pendingIndex = 0xFF;
    queue_push(keymapLongPress[key_index]);
  }
  else if (KeyTapMod[key_index] <= 1) {
    // nem ciklikus gomb -> mindig azonnali, önálló leütés
    commit_pending_if_needed();
    s_pendingIndex = 0xFF;
    queue_push(keymap[key_index][0]);
  }
  else {
    // Serial.printf("gap: %lu ms\n", now - s_pendingTapTime);  // gap idő debug
    bool isContinuation = (s_pendingIndex == key_index) &&
                           ((now - s_pendingTapTime) < MULTI_TAP_THRESHOLD);

    if (!isContinuation) {
      commit_pending_if_needed();   // esetleges más gombos függő ciklus lezárása
      s_pendingIndex = key_index;
      s_pendingTap   = 0;
    } else {
      s_pendingTap = (s_pendingTap + 1) % KeyTapMod[key_index];
    }
    s_pendingTapTime = now;

    if (sendBackSpace) {
      if (isContinuation) queue_push('\b');
      queue_push(keymap[key_index][s_pendingTap]);
    }
    // halasztott módban itt nem küldünk semmit, a threshold lejártakor
    // (1-es lépés) vagy a következő eltérő gombnál (commit_pending_if_needed)
    // fog kimenni a végleges karakter
  }

  if (queue_pop(qc)) return qc;
  return 0;
}

#endif