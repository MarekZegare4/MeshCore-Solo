#pragma once
// Telemetry as text: the fields a frontend's clock page shows
// (NodePrefs::dashboard_fields -- Home on L2, the clock page and lock screen
// on L1) and any sensor reading (L1's sensors page). One set of numbers,
// names and formats; each frontend passes its own Style (the degree glyph in
// its font, how much room it has).

#include <stdint.h>
#include <stdio.h>
#include <helpers/sensors/LPPDataHelpers.h>
#include <helpers/sensors/LocationProvider.h>
#include "Battery.h"

namespace telemetry {

// Stored in NodePrefs::dashboard_fields: append only.
enum Field : uint8_t { NONE, BATT_V, TEMP, HUM, PRES, GPS, ALT, LUX, CO2, NODES, MSGS,
                       BATT_PCT, SATS, ALT_GPS, COUNT };
static const char* const NAME[COUNT] = {
  "None", "Battery (V)", "Temperature", "Humidity", "Pressure", "Position", "Altitude (sensor)",
  "Light", "CO2", "Contacts", "Unread", "Battery (%)", "Satellites", "Altitude (GPS)",
};
// The same, for a narrow menu column (L1).
static const char* const COMPACT[COUNT] = {
  "None", "Batt V", "Temp", "Humidity", "Pressure", "GPS", "Altitude (Baro)", "Lux", "CO2", "Contacts",
  "Messages", "Batt %", "Sats", "Altitude (GPS)",
};
// A label beside the value, where room is short.
static const char* const LABEL[COUNT] = {
  "", "Batt", "Temp", "Hum", "Pres", "GPS", "Alt", "Lux", "CO2", "Nodes", "Msgs", "Batt", "Sats", "AltG",
};

struct Style {
  const char* deg;   // the degree sign in the display's font
  bool spaced;       // "12 m" rather than "12m"
  bool nouns;        // counts say what they count ("3 nodes"): a value without its label
  uint8_t gps_dp;    // decimals of a position
};

// What the values are read from; the frontend fills in what it has.
struct Inputs {
  uint16_t batt_mv = 0, low_batt_mv = 0;
  int nodes = 0;
  int unread = 0;
  bool unread_more = false;          // some unread already gone from the rings: "3+"
  bool imperial = false;
  LocationProvider* loc = nullptr;   // nullptr: no GPS
  bool gps_on = true;
  const uint8_t* lpp = nullptr;      // sensors on the bus, as one querySensors() left them
  uint8_t lpp_len = 0;
};

static uint8_t lppType(uint8_t f) {
  switch (f) {
    case TEMP: return LPP_TEMPERATURE;
    case HUM:  return LPP_RELATIVE_HUMIDITY;
    case PRES: return LPP_BAROMETRIC_PRESSURE;
    case ALT:  return LPP_ALTITUDE;
    case LUX:  return LPP_LUMINOSITY;
    case CO2:  return LPP_CONCENTRATION;
  }
  return 0;
}
// Read from the sensors, not the radio or GPS.
static bool isSensor(uint8_t f) { return lppType(f) != 0; }

// Altitude always in the small unit (never km / mi).
static void altText(float m, bool imperial, const Style& st, char* v, int n) {
  snprintf(v, n, "%.0f%s%s", imperial ? m * 3.28084f : m, st.spaced ? " " : "", imperial ? "ft" : "m");
}

// The reading `r` is at (its header read) as text; false, and the data
// skipped, for a type that isn't shown.
static bool lppText(LPPReader& r, uint8_t type, bool imperial, const Style& st, char* v, int n) {
  const char* sp = st.spaced ? " " : "";
  float x, y, z;
  switch (type) {
    case LPP_TEMPERATURE:
      r.readTemperature(x);
      snprintf(v, n, "%.1f%s%s%s", imperial ? x * 9 / 5 + 32 : x, sp, st.deg, imperial ? "F" : "C");
      return true;
    case LPP_RELATIVE_HUMIDITY:   r.readRelativeHumidity(x); snprintf(v, n, "%.0f%%", x); return true;
    case LPP_BAROMETRIC_PRESSURE: r.readPressure(x); snprintf(v, n, "%.0f%shPa", x, sp); return true;
    case LPP_ALTITUDE:            r.readAltitude(x); altText(x, imperial, st, v, n); return true;
    case LPP_LUMINOSITY:          r.readLuminosity(x); snprintf(v, n, "%.0f%slx", x, sp); return true;
    case LPP_CONCENTRATION:       r.readConcentration(x); snprintf(v, n, "%.0f%sppm", x, sp); return true;
    case LPP_VOLTAGE:             r.readVoltage(x); snprintf(v, n, "%.2f%sV", x, sp); return true;
    case LPP_CURRENT:             r.readCurrent(x); snprintf(v, n, "%.3f%sA", x, sp); return true;
    case LPP_POWER:               r.readPower(x); snprintf(v, n, "%.1f%sW", x, sp); return true;
    case LPP_PERCENTAGE:          r.readPercentage(x); snprintf(v, n, "%.0f%%", x); return true;
    case LPP_DISTANCE:            r.readDistance(x); snprintf(v, n, "%.2f%sm", x, sp); return true;
    case LPP_GPS:
      r.readGPS(x, y, z);
      if (x != 0 || y != 0) snprintf(v, n, "%.4f %.4f", x, y);
      else snprintf(v, n, "--");
      return true;
  }
  r.skipData(type);
  return false;
}

// Field `f` as text; "--" when there's nothing to show.
static void text(uint8_t f, const Inputs& in, const Style& st, char* v, int n) {
  const char* sp = st.spaced ? " " : "";
  snprintf(v, n, "--");
  bool fix = in.loc && in.loc->isValid();
  switch (f) {
    case BATT_V:
      if (in.batt_mv) snprintf(v, n, "%u.%02u%sV", in.batt_mv / 1000, (in.batt_mv % 1000) / 10, sp);
      return;
    case BATT_PCT:
      if (in.batt_mv) snprintf(v, n, "%d%%", battery::percent(in.batt_mv, in.low_batt_mv));
      return;
    case NODES: snprintf(v, n, "%d%s", in.nodes, st.nouns ? " nodes" : ""); return;
    case MSGS:  snprintf(v, n, "%d%s%s", in.unread, in.unread_more ? "+" : "", st.nouns ? " msgs" : ""); return;
    case SATS:
      if (in.loc && in.gps_on) snprintf(v, n, "%ld%s", (long)in.loc->satellitesCount(), st.nouns ? " sats" : "");
      return;
    case GPS:
      if (fix) snprintf(v, n, "%.*f %.*f", st.gps_dp, in.loc->getLatitude() / 1e6, st.gps_dp, in.loc->getLongitude() / 1e6);
      else if (in.loc) snprintf(v, n, "no fix");
      return;
    case ALT_GPS:
      if (fix) altText(in.loc->getAltitude() / 1000.0f, in.imperial, st, v, n);
      else if (in.loc) snprintf(v, n, "no fix");
      return;
  }
  uint8_t want = lppType(f);
  if (!want || !in.lpp) return;
  LPPReader r(in.lpp, in.lpp_len);
  uint8_t ch, type;
  while (r.readHeader(ch, type)) {
    if (type == want) { lppText(r, type, in.imperial, st, v, n); return; }
    r.skipData(type);
  }
}

}  // namespace telemetry
