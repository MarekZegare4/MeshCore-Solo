#pragma once
#include <string.h>

#include <helpers/SensorManager.h>

// SensorManager subclass wiring the sim's already-existing
// SimLocationProvider (SimLocationProvider.h) into the real
// getLocationProvider() hook -- the base SensorManager (previously used
// as-is in target.cpp) always returns NULL there (see
// SimLocationProvider.h's own comment on this exact gap), so on-device
// screens that check "do I have a GPS fix" (CompassScreen.h,
// NearbyScreen.h) never saw a fix even after sim_location_set() was
// called, even though the *advertised* position (sensors.node_lat/lon)
// already worked. Also adds one representative JS-settable environment
// telemetry channel (temperature) to prove the querySensors() mechanism --
// same channel-numbering convention as the real
// src/helpers/sensors/EnvironmentSensorManager.cpp: GPS on
// TELEM_CHANNEL_SELF, each other active sensor on the next channel.
//
// getLocationProvider()/querySensors() are declared here but DEFINED in
// target.cpp, not inline -- they need SimLocationProvider.h's complete
// type (for the LocationProvider* upcast, and to call
// sim_location_provider()), and SimLocationProvider.h in turn needs THIS
// class's complete type for its own `extern SimSensorManager sensors;`
// declaration (see that file's comment) -- pulling SimLocationProvider.h
// in here too would make the two headers mutually dependent on each
// other's complete type with no valid include order. target.cpp already
// includes target.h, which includes both in the one order that works
// (this file first, then SimLocationProvider.h), so it's the natural
// place for the bodies that need both.
//
// Deliberately doesn't fake any of the ~15 real I2C sensor chip drivers
// (BME280/INA219/etc, EnvironmentSensorManager.cpp) -- those are
// hardware-specific and out of scope; one JS-settable channel is enough to
// prove the mechanism and is trivially extended later if a specific sensor
// type turns out to matter for a demo.
class SimSensorManager : public SensorManager {
  static float& envTemperatureRef() { static float t = 21.0f; return t; }
  bool _gps = false;   // the "gps" setting, as a real board's GPS switch: the UI's toggle needs it

public:
  LocationProvider* getLocationProvider() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;

  int getNumSettings() const override { return 1; }
  const char* getSettingName(int i) const override { return i == 0 ? "gps" : NULL; }
  const char* getSettingValue(int i) const override { return i == 0 ? (_gps ? "1" : "0") : NULL; }
  bool setSettingValue(const char* name, const char* value) override {
    if (strcmp(name, "gps") != 0) return false;
    _gps = strcmp(value, "0") != 0;
    return true;
  }

  static void setEnvTemperature(float celsius) { envTemperatureRef() = celsius; }
};

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// C linkage so JS's ccall('sim_env_temperature_set', ...) can find it by
// its exact, unmangled name -- same pattern as SimLocationProvider.h's
// sim_location_set() / SimMainBoard.h's sim_battery_set_mv(). Doesn't touch
// SimLocationProvider.h, so it's safe to define inline here.
extern "C" inline EMSCRIPTEN_KEEPALIVE void sim_env_temperature_set(float celsius) {
  SimSensorManager::setEnvTemperature(celsius);
}
#endif
