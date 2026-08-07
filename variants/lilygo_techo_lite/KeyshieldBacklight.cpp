// TechoKeyBacklight.cpp
// Self-contained AW21009(QNR) keyboard-backlight driver for the T-Echo Lite
// KeyShield. Provides a single free function consumed by the UITask Home-key hook:
//
//     void techo_keyshield_backlight_toggle();   // on <-> off
//
// Raw-Wire register driver. The register sequence mirrors LilyGo's
// cpp_bus_driver Aw21009 implementation (reset / global-control / global-current
// / per-channel scaling / latch, then 12-bit per-channel brightness), so no
// dependency on that library is pulled in.

#ifdef LILYGO_TECHO_LITE_KEYSHIELD

#include <Arduino.h>
#include <Wire.h>

// ---- AW21009 (KeyShield backlight) -----------------------------------------
// I2C 0x20 on the shared Wire bus. Wire.begin() (TechoBoard::begin) and the
// shield 3V3 rail are already up before the UI loop runs, so this lazy-inits on
// first toggle.
#define AW21009_ADDR          0x20
#define AW21009_REG_GCR       0x20   // global control (auto-save / PWM / chip enable)
#define AW21009_REG_BRIGHT    0x21   // brightness start; per ch: LSB=0x21+ch*2, MSB+1
#define AW21009_REG_UPDATE    0x45   // write-to-latch register
#define AW21009_REG_SCALE     0x46   // per-channel current scaling start
#define AW21009_REG_GCCR      0x58   // global current control
#define AW21009_REG_RESET     0x70   // software reset

#define AW21009_CHANNELS      6
#define AW21009_ON_LEVEL      4095    // 0..4095; lower this to dim the backlight

static void aw_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(AW21009_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void aw_init() {
  aw_write(AW21009_REG_RESET, 0x00);                 // software reset
  delay(10);
  aw_write(AW21009_REG_GCR, 0x87);                   // auto power save + 12-bit PWM + chip enable
  aw_write(AW21009_REG_GCCR, 0xFF);                  // global current = max
  for (uint8_t i = 0; i < AW21009_CHANNELS; i++)
    aw_write(AW21009_REG_SCALE + i, 0xFF);           // per-channel current = max
  aw_write(AW21009_REG_UPDATE, 0x00);                // latch
}

static void aw_set_brightness(uint16_t value) {
  if (value > 4095) value = 4095;
  for (uint8_t i = 0; i < AW21009_CHANNELS; i++) {
    aw_write(AW21009_REG_BRIGHT + i * 2,     (uint8_t)(value & 0xFF));         // LSB
    aw_write(AW21009_REG_BRIGHT + i * 2 + 1, (uint8_t)((value >> 8) & 0x0F));  // MSB (12-bit)
  }
  aw_write(AW21009_REG_UPDATE, 0x00);                // latch
}

void techo_keyshield_backlight_toggle() {
  static bool inited = false;
  static bool on = false;
  if (!inited) { aw_init(); inited = true; }
  on = !on;
  aw_set_brightness(on ? AW21009_ON_LEVEL : 0);
}

#endif