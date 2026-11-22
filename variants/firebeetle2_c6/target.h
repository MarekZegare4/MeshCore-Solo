#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <FireBeetle2C6Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/Lsm6Lis3Imu.h>
#ifdef DISPLAY_CLASS
  #include <helpers/ui/SharpMemDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

extern FireBeetle2C6Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;
extern Lsm6Lis3Imu imu;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

// The board's own console commands (MyMeshCli), for bringing a breadboard up:
//   hw   what answered at start -- the radio, the SD card, the I2C chips
//        (the IMU: 0x6A/0x6B LSM6DS, 0x1C/0x1E LIS3MDL), the battery; the
//        Sharp is write-only, so it isn't in it
//   imu  one reading, in the chips' own axes: acceleration (m/s2), field (uT)
#define BOARD_CLI 1
bool board_cli(const char* cmd, char* reply, int n);
