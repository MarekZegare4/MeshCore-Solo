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
  uint32_t _gps_search_until = 0;        // searchGps(): listening for a module not heard at the start (0: not)
  bool     _gps_on_when_found = false;   // saved as on: switched on when the search hears it
  uint32_t _gps_wake_at = 0;             // searchGps(): held off until then, then woken (0: woken)
  static const uint32_t GPS_WAKE_LOW_MS = 1000;
  // "gps diag" (#72): when (millis()) the start's listening began, after how
  // long the first byte came (0: none in its 5 s), and the same for the last
  // search (start 0: none yet).
  uint32_t _gps_boot_at = 0, _gps_boot_heard = 0, _gps_search_at = 0, _gps_search_heard = 0;
  static const uint32_t GPS_LATE_SEARCH_MS = 30000;   // after the start's 5 s, for a GPS saved as on
  // The last fix taken from the receiver, kept apart from node_lat/lon (which
  // the app, `set lat/lon` and the device can overwrite with a typed position):
  // lets telemetry give the position out while the GPS naps between
  // duty-cycle fixes -- see gpsFixFresh(). Call after node_* hold the fix.
  double   _gps_fix_lat = 0, _gps_fix_lon = 0, _gps_fix_alt = 0;
  uint32_t _gps_last_fix_ms = 0;
  bool     _gps_fix_seen = false;
  void     noteGpsFix() {
    _gps_fix_lat = node_lat; _gps_fix_lon = node_lon; _gps_fix_alt = node_altitude;
    _gps_last_fix_ms = millis(); _gps_fix_seen = true;
  }
  bool     gpsFixFresh() const;
  uint32_t gpsFixWindowMs() const;
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
  bool isGPSDetected() const override { return gps_detected; }
  bool searchGps(uint32_t ms) override;
  bool gpsSearching() const override { return _gps_search_until != 0; }
  void formatGpsDiag(char* out, int n) override;
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
