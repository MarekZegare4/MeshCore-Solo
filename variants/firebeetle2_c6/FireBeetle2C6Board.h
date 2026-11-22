#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// DFRobot FireBeetle 2 ESP32-C6 (DFR1075) on a breadboard: one SPI bus for
// the SX1262, a Sharp Memory LCD and an SD card, I2C for a 9-DOF IMU.
class FireBeetle2C6Board : public ESP32Board {
public:
  void begin() {
    // Every chip on the shared bus deselected before any of them is talked
    // to: the Sharp's CS is active high (low = idle), the others active low.
    // A CS left floating would take the radio's or the card's start-up
    // clocks for its own.
    pinMode(PIN_DISPLAY_CS, OUTPUT);
    digitalWrite(PIN_DISPLAY_CS, LOW);
    pinMode(P_LORA_NSS, OUTPUT);
    digitalWrite(P_LORA_NSS, HIGH);
#ifdef PIN_SD_CS
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
#endif
    ESP32Board::begin();
  }

  const char* getManufacturerName() const override { return "FireBeetle 2 C6"; }
};
