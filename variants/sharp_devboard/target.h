#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/esp32/ESPNOWRadio.h>
#include <helpers/SensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SharpMemDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

// What the Solo companion asks of its radio beyond mesh::Radio (it is written
// for RadioLibWrapper): nothing to tune over ESP-NOW, so neutral answers.
class SoloESPNOWRadio : public ESPNOWRadio {
public:
  enum { RX_PS_OFF = 0, RX_PS_ALL = 1, RX_PS_RECENT = 2 };
  void setPowerSaving(uint8_t) { }
  uint8_t getPowerSaving() const { return RX_PS_OFF; }
  uint8_t getPowerSavingActive() const { return RX_PS_OFF; }
  int rxPsSleepPercent(uint8_t) { return -1; }
  void getFreqBounds(float& min_mhz, float& max_mhz) const { min_mhz = 150.0f; max_mhz = 2500.0f; }
  int8_t getTxPower() const { return 0; }
  static float snrFloorForSF(uint8_t sf) {
    if (sf < 7) sf = 7; else if (sf > 12) sf = 12;
    return -7.5f - 2.5f * (float)(sf - 7);
  }
  uint32_t getRxPsWatchdogSoftCount() const { return 0; }
  uint32_t getRxPsWatchdogHardCount() const { return 0; }
};

extern ESP32Board board;
extern SoloESPNOWRadio radio_driver;
extern ESP32RTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
  extern MomentaryButton joystick_left;
  extern MomentaryButton joystick_right;
  extern MomentaryButton joystick_up;
  extern MomentaryButton joystick_down;
  extern MomentaryButton back_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
