#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include "WioTrackerL2Board.h"
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>
#ifdef DISPLAY_CLASS
  #include "WioTrackerL2Display.h"
  #include <helpers/ui/MomentaryButton.h>
#endif

// The L76K's power and reset lines are on the IO expander, out of the NMEA
// provider's reach: begin() / stop() / reset() drive them here, so GPS off
// (Settings, duty cycling) really powers the receiver down. begin() on a
// running receiver does nothing: a reset there (the app setting "gps" again)
// threw away the acquisition in progress.
class L2GpsProvider : public MicroNMEALocationProvider {
  bool _uart_off = false;
  bool _started = false;       // begin() ran (the boot one does the reset)
  bool _cfg_pending = false;   // constellations to set once the module talks
  uint32_t _cfg_rx = 0;
public:
  using MicroNMEALocationProvider::MicroNMEALocationProvider;
  void begin() override;
  void stop() override;
  void reset() override;
  bool isEnabled() override;
  void loop() override;
protected:
  int readChunk(uint8_t* buf, int max) override;   // the UART's run in one read
};

extern WioTrackerL2Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
extern L2GpsProvider gps;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
