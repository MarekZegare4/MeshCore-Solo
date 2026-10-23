#pragma once
// Populates a KeyboardWidget's placeholder list based on live sensor data.
// Only adds placeholders for types that actually returned a value right now.
// Always adds {loc} and {time} via begin(); this helper appends the rest.

#include "KeyboardWidget.h"
#include <helpers/SensorManager.h>
#include <helpers/sensors/LPPDataHelpers.h>

inline void kbAddSensorPlaceholders(KeyboardWidget& kb, SensorManager* sm) {   // list: ui-core/MessageText.h
  msgtext::sensorPlaceholders(sm, board.getMCUTemperature(), [](const char* ph, void* k) { ((KeyboardWidget*)k)->addPlaceholder(ph); }, &kb);
}
