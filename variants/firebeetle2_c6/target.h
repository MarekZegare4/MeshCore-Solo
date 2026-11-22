#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <FireBeetle2C6Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SharpMemDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern FireBeetle2C6Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

// The CLI's "hw": what answered at start -- the radio, the SD card, the I2C
// addresses (the IMU: 0x6A/0x6B LSM6DS, 0x1C/0x1E LIS3MDL). For bringing a
// breadboard up; the Sharp is write-only, so it isn't in it.
#define BOARD_HW_REPORT 1
void board_hw_report(char* reply, int n);
