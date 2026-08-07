#include <Arduino.h>
#include "target.h"

CardputerADVBoard board;

static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

static void init_lora_cap_ioe() {
  const uint8_t PI4IOE_ADDR = 0x43; // PI4IOE5V6408, ADDR pin -> GND

  // Egyszerű "probe": ha nincs ACK, nincs ott az IO expander (régi cap)
  Wire.beginTransmission(PI4IOE_ADDR);
  if (Wire.endTransmission() != 0) {
    return; // régi Cap LoRa868, nincs teendő
  }

  // Új Cap LoRa-1262: P0 -> output, High-Z letiltva, majd HIGH
  Wire.beginTransmission(PI4IOE_ADDR);
  Wire.write(0x03);   // I/O Direction register
  Wire.write(0x01);   // bit0 = 1 -> P0 kimenet, többi marad bemenet
  Wire.endTransmission();

  Wire.beginTransmission(PI4IOE_ADDR);
  Wire.write(0x07);   // Output High-Impedance register
  Wire.write(0x00);   // bit0 = 0 -> P0 kijön a High-Z állapotból
  Wire.endTransmission();

  Wire.beginTransmission(PI4IOE_ADDR);
  Wire.write(0x05);   // Output Port register
  Wire.write(0x01);   // bit0 = 1 -> P0 HIGH (LoRa "SW" pin engedélyezve)
  Wire.endTransmission();
}

bool radio_init() {
  fallback_clock.begin();

  Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);   // 14pin header & internal
  Wire1.begin(PIN_BOARD_SDA1, PIN_BOARD_SCL1); // grove

  rtc_clock.begin(Wire);   // needs Wire already begun on the right pins to auto-detect an RTC chip

  init_lora_cap_ioe();

  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng); // create new random identity
}
