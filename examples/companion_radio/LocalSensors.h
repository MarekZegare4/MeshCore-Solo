#pragma once
// This device's own readings, as the screens, the bot and message placeholders
// read them.

#include <math.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LPPDataHelpers.h>

// The sensors' readings, plus the chip's own temperature (NaN where a board has
// none) when no sensor reads one: the telemetry reply sends it too, so a board
// without a temperature sensor shows on screen and in {temp} what the app shows.
inline void queryLocalSensors(SensorManager& sm, float mcu_temp, CayenneLPP& lpp) {
  sm.querySensors(0xFF, lpp);
  if (isnan(mcu_temp)) return;
  LPPReader r(lpp.getBuffer(), lpp.getSize());
  uint8_t ch, type;
  while (r.readHeader(ch, type)) {
    if (type == LPP_TEMPERATURE) return;
    r.skipData(type);
  }
  lpp.addTemperature(TELEM_CHANNEL_SELF, mcu_temp);
}
