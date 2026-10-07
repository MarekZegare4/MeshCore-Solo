#pragma once

#include <MeshCore.h>
#include <cstdio>
#include <cstdlib>

// mesh::MainBoard implementation for the native sim build -- no real
// hardware, so battery/manufacturer/reboot are all just plausible fakes.
class SimMainBoard : public mesh::MainBoard {
  // Real telemetry code (examples/companion_radio/MyMesh.cpp,
  // examples/simple_room_server/MyMesh.cpp) reads getBattMilliVolts()
  // directly rather than going through SensorManager -- so a JS-settable
  // battery level lives here, not on SimSensorManager. Static (not a plain
  // member) so the EMSCRIPTEN_KEEPALIVE free function below can reach it
  // without needing a reference to the one `board` global (target.cpp)
  // plumbed through, same reasoning as SimLocationProvider.h's singleton.
  static uint16_t& battMilliVoltsRef() { static uint16_t mv = 4000; return mv; }

  static bool& externalPowerRef() { static bool on = false; return on; }

public:
  void begin() { }

  bool isExternalPowered() override { return externalPowerRef(); }
  static void setExternalPower(bool on) { externalPowerRef() = on; }

  uint16_t getBattMilliVolts() override { return battMilliVoltsRef(); }
  static void setBattMilliVolts(uint16_t mv) { battMilliVoltsRef() = mv; }
  const char* getManufacturerName() const override { return "MeshCore Sim (native)"; }

  void reboot() override {
    printf("\n[sim] reboot() requested -- exiting process (rerun the binary to simulate a reboot)\n");
    fflush(stdout);
    exit(0);
  }

  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }

  void onBootComplete() override { }
  void sleep(uint32_t secs) override { }   // no-op: native process never actually sleeps the CPU
};

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// C linkage so JS's ccall('sim_battery_set_mv', ...) can find it by its
// exact, unmangled name -- same pattern as SimLocationProvider.h's
// sim_location_set().
extern "C" inline EMSCRIPTEN_KEEPALIVE void sim_battery_set_mv(int mv) {
  SimMainBoard::setBattMilliVolts((uint16_t)mv);
}
extern "C" inline EMSCRIPTEN_KEEPALIVE void sim_external_power_set(int on) {
  SimMainBoard::setExternalPower(on != 0);
}
#endif
