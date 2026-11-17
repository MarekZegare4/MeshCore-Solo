#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

ESP32Board board;
SoloESPNOWRadio radio_driver;
ESP32RTCClock rtc_clock;
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  // Four buttons to GND with pull-ups on the board (34/35 have no internal
  // ones): up, down, OK, Back. No left/right: pin -1 reads never pressed.
  // multiclick only on Back, for the triple-click buzzer toggle.
  MomentaryButton user_btn      (PIN_USER_BTN,   1000, true, false, false);
  MomentaryButton joystick_up   (JOYSTICK_UP,    1000, true, false, false);
  MomentaryButton joystick_down (JOYSTICK_DOWN,  1000, true, false, false);
  MomentaryButton joystick_left (JOYSTICK_LEFT,  1000, true, false, false);
  MomentaryButton joystick_right(JOYSTICK_RIGHT, 1000, true, false, false);
  MomentaryButton back_btn      (PIN_BACK_BTN,   1000, true, false, true);
#endif

bool radio_init() {
  rtc_clock.begin();
  radio_driver.init();
  return true;
}

// ESP-NOW runs on the WiFi radio, so the IDF has the hardware RNG on.
class ESP_RNG : public mesh::RNG {
public:
  void random(uint8_t* dest, size_t sz) override { esp_fill_random(dest, sz); }
};

mesh::LocalIdentity radio_new_identity() {
  ESP_RNG rng;
  return mesh::LocalIdentity(&rng);
}
