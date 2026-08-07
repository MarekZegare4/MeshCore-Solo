#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;

  bool     gps_detected = false;
  bool     gps_active = false;           // physically powered on right now
  uint32_t gps_update_interval_sec = 1;

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  bool     gps_master_enabled = false;   // user's on/off intent (Settings/bot/CLI) -- distinct
                                          // from gps_active, which duty-cycling now flips on its own
  // Duty-cycling: while enabled, GPS sleeps for gps_duty_sleep_sec between
  // acquisitions instead of running continuously. 0 = disabled (today's
  // always-on behaviour). Skipped entirely while _gps_keep_awake is held by
  // a live consumer (background trail/live-share/locator, or a screen that
  // needs a fresh reading right now) -- see setGpsKeepAwake().
  uint32_t gps_duty_sleep_sec = 0;
  bool     _gps_keep_awake = false;
  uint32_t _gps_duty_phase_until = 0;
  bool     _gps_just_woke = false;       // one-shot; consumed by UITask to reset locator state
  void gpsDutyCycleLoop();
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  void setGpsKeepAwake(bool on) override { _gps_keep_awake = on; }
  bool consumeGpsWakeEvent() override { bool w = _gps_just_woke; _gps_just_woke = false; return w; }
  bool isGpsDutySleeping() const override {
    return gps_master_enabled && gps_duty_sleep_sec > 0 && !_gps_keep_awake && !gps_active;
  }
  #else
  EnvironmentSensorManager(){};
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  #if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  void loop() override;
  #endif
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
