#include <Arduino.h>
#include "target.h"

HeltecV3Board board;

#if defined(P_LORA_SCLK)
  static SPIClass spi;
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  MicroNMEALocationProvider nmea = MicroNMEALocationProvider(Serial1, &rtc_clock);
  EnvironmentSensorManager sensors = EnvironmentSensorManager(nmea);
#else
  EnvironmentSensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
  #if UI_HAS_JOYSTICK
    // Optional wired joystick — see the Heltec_v3_companion_solo_dual env for the
    // pin defines this needs. Unlike the Wio Tracker L1 (external pull-ups on
    // board) these pass pulldownup = true, so each contact only has to short its
    // pin to GND; the internal pull-up does the rest. Back gets multiclick = true
    // because the UI's triple-click buzzer toggle lives on it.
    MomentaryButton joystick_left (JOYSTICK_LEFT,  1000, true, true, false);
    MomentaryButton joystick_right(JOYSTICK_RIGHT, 1000, true, true, false);
    MomentaryButton back_btn      (PIN_BACK_BTN,   1000, true, true, true);
    #if UI_HAS_JOYSTICK_UPDOWN
      MomentaryButton joystick_up  (JOYSTICK_UP,   1000, true, true, false);
      MomentaryButton joystick_down(JOYSTICK_DOWN, 1000, true, true, false);
    #endif
  #endif
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
  
#if defined(P_LORA_SCLK)
  return radio.std_init(&spi);
#else
  return radio.std_init();
#endif
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}

