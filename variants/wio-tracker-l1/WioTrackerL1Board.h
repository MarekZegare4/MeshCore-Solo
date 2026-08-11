#pragma once

#include <MeshCore.h>
#include <Arduino.h>
#include <helpers/NRF52Board.h>

class WioTrackerL1Board : public NRF52BoardDCDC {
protected:
  uint8_t btn_prev_state;

public:
  WioTrackerL1Board() : NRF52Board("WioTrackerL1 OTA") {}
  void begin();

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#endif

  uint16_t getBattMilliVolts() override {
    // VBAT_ENABLE is held HIGH continuously (see begin()/initVariant()) so the
    // divider node is already settled — don't gate it per-read, that reads high
    // and jittery because 10ms wasn't enough for the divider to stabilize.
    analogReadResolution(12);
    analogReference(AR_INTERNAL);
    int adcvalue = analogRead(PIN_VBAT_READ);
    return (adcvalue * ADC_MULTIPLIER * AREF_VOLTAGE) / 4.096;
  }

  const char* getManufacturerName() const override {
    return "Seeed Wio Tracker L1";
  }

  void powerOff() override {
    NRF52Board::powerOff();
  }
};
