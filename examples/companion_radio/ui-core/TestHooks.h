#pragma once
// Test hooks for tools/hwtest.py: CLI commands that drive a device at a desk
// the way its user would outdoors. Only in a build with -D SOLO_TEST_HOOKS
// (hwtest.py --flash makes one, in a build directory of its own); a release
// has none of this, so nothing here is reachable over Bluetooth on a device
// people carry. `ver` says "+test" on such a build.
//
//   gps <lat>,<lon>[,<alt m>]  a fix there, as the GPS would give it: every
//                              screen, the trail and the route guide see it
//   gps off                    the real receiver again
//   guide                      the route guide, "> state,slot,left m,off m,label"
//                              (state 0 off, 1 on route, 2 off route, 3 finished)
//   follow <slot> [reversed]   follow a planned route, as Routes > Follow does
//   follow off                 stop following
//   trackback                  Track back: the trail walked backwards, as Trail > Track back
//   trail on|off|clear         record the trail, stop, empty it (as Trail's own actions)
//   trail                      "> <recording 0/1>,<points>"
//   mem                        "> <free heap>,<total>,<stack free>,<uptime s>" (0: unknown;
//                              an ESP32's internal RAM, not its PSRAM)
//   lastdm                     the newest direct message, "> <in/out>,<prefix hex>,<ack 0-3>,<text>"
//   lastch                     the newest channel post, "> <channel>,<text>"
//
// The frontends add their own (ui-new/UITask.cpp: key, ui).

#ifdef SOLO_TEST_HOOKS
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/DeviceDiag.h>
#ifdef ESP32
  #include <esp_heap_caps.h>
#endif

namespace testhooks {

// Stands in front of the board's provider: with a fake fix on, the position
// is the fake one; everything else (power, time, the parser) goes through.
class FakeLocation : public LocationProvider {
public:
  LocationProvider* real = nullptr;
  bool on = false;
  long lat = 0, lon = 0, alt_mm = 0;

  long getLatitude() override  { return on ? lat : real->getLatitude(); }
  long getLongitude() override { return on ? lon : real->getLongitude(); }
  long getAltitude() override  { return on ? alt_mm : real->getAltitude(); }
  long satellitesCount() override { return on ? 9 : real->satellitesCount(); }
  long getHDOP() override      { return on ? 9 : real->getHDOP(); }
  bool isValid() override      { return on || real->isValid(); }
  long getTimestamp() override { return real->getTimestamp(); }
  void syncTime() override     { real->syncTime(); }
  bool waitingTimeSync() override { return real->waitingTimeSync(); }
  void sendSentence(const char* s) override { real->sendSentence(s); }
  bool waitFor(const char* p, uint32_t ms) override { return real->waitFor(p, ms); }
  void drain() override  { real->drain(); }
  void reset() override  { real->reset(); }
  void begin() override  { real->begin(); }
  void stop() override   { real->stop(); }
  void loop() override   { real->loop(); }
  bool isEnabled() override { return real->isEnabled(); }
  void formatDiagnostics(char* out, size_t n) override {
    if (on) snprintf(out, n, "fake fix (test build)");
    else real->formatDiagnostics(out, n);
  }
};

inline FakeLocation& fake() { static FakeLocation f; return f; }

// The sensor manager's provider pointer is protected: reached through a
// member pointer named in a derived class, which C++ allows.
struct SensorsPeek : EnvironmentSensorManager {
  static LocationProvider* EnvironmentSensorManager::* loc() { return &SensorsPeek::_location; }
};

// Puts the fake in front of the board's provider (once); false with no GPS.
inline bool install(EnvironmentSensorManager& s) {
  LocationProvider*& p = s.*SensorsPeek::loc();
  if (p == &fake()) return true;
  if (!p) return false;
  fake().real = p;
  p = &fake();
  return true;
}

// The board's own provider, for code that needs its concrete type (the sky view).
inline LocationProvider* real(LocationProvider* p) { return p == &fake() ? fake().real : p; }

template <typename Core>
inline bool handle(Core& core, const char* cmd, char* reply, int n) {
  (void)core;
  if (!strncmp(cmd, "gps ", 4)) {
    const char* a = cmd + 4;
    if (!install(sensors)) { snprintf(reply, n, "Error, no GPS on this board"); return true; }
    if (!strcmp(a, "off")) { fake().on = false; snprintf(reply, n, "OK"); return true; }
    char* e;
    const double la = strtod(a, &e);
    if (e == a || *e != ',') { snprintf(reply, n, "Error, gps <lat>,<lon>[,<alt m>] or gps off"); return true; }
    const char* b = e + 1;
    const double lo = strtod(b, &e);
    if (e == b || (*e && *e != ',')) { snprintf(reply, n, "Error, gps <lat>,<lon>[,<alt m>] or gps off"); return true; }
    const double al = *e ? strtod(e + 1, nullptr) : 0;
    if (la < -90 || la > 90 || lo < -180 || lo > 180) { snprintf(reply, n, "Error, out of range"); return true; }
    fake().lat = lround(la * 1e6);
    fake().lon = lround(lo * 1e6);
    fake().alt_mm = lround(al * 1000);
    fake().on = true;
    snprintf(reply, n, "OK");
    return true;
  }
  if (!strncmp(cmd, "follow ", 7)) {
#if FEAT_ROUTES
    const char* a = cmd + 7;
    if (!strcmp(a, "off")) { core.guide.stop(); snprintf(reply, n, "OK"); return true; }
    char* e;
    const long slot = strtol(a, &e, 10);
    if (e == a || (*e && strcmp(e, " reversed"))) { snprintf(reply, n, "Error, follow <slot> [reversed] or follow off"); return true; }
    snprintf(reply, n, core.guide.start((int)slot, *e != 0) ? "OK" : "Error, no route there");
#else
    snprintf(reply, n, "Error, no routes on this board");
#endif
    return true;
  }
  if (!strcmp(cmd, "trackback")) {
#if FEAT_ROUTES
    snprintf(reply, n, core.startTrackBack() ? "OK" : "Error, no trail to walk back");
#else
    snprintf(reply, n, "Error, no routes on this board");
#endif
    return true;
  }
  if (!strcmp(cmd, "trail")) {
    snprintf(reply, n, "> %d,%d", core.trail.isRecording() ? 1 : 0, core.trail.store().count());
    return true;
  }
  if (!strncmp(cmd, "trail ", 6)) {
    const char* a = cmd + 6;
    if (!strcmp(a, "on")) core.trail.setActive(true);
    else if (!strcmp(a, "off")) core.trail.setActive(false);
    else if (!strcmp(a, "clear")) core.trail.reset();
    else { snprintf(reply, n, "Error, trail on/off/clear"); return true; }
    snprintf(reply, n, "OK");
    return true;
  }
  if (!strcmp(cmd, "mem")) {
    uint32_t fr = 0, total = 0;
#ifdef ESP32
    fr = (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);     // not the PSRAM: what runs short
    total = (uint32_t)heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
#else
    DeviceDiag::getHeapStats(fr, total);
#endif
    snprintf(reply, n, "> %lu,%lu,%lu,%lu", (unsigned long)fr, (unsigned long)total,
             (unsigned long)DeviceDiag::getStackFreeBytes(), (unsigned long)(millis() / 1000));
    return true;
  }
  if (!strcmp(cmd, "lastdm")) {
    const MessageHistory& h = core.history;
    if (h.dmHistCount() == 0) { snprintf(reply, n, "> none"); return true; }
    const DmHistEntry& e = h.dmAtPos(h.dmHistPosNewest(0));
    snprintf(reply, n, "> %s,%02x%02x%02x%02x,%d,%s", e.outgoing ? "out" : "in", e.prefix[0], e.prefix[1],
             e.prefix[2], e.prefix[3], (int)e.ack_status, e.text);
    return true;
  }
  if (!strcmp(cmd, "lastch")) {
    const MessageHistory& h = core.history;
    if (h.chHistCount() == 0) { snprintf(reply, n, "> none"); return true; }
    const ChHistEntry& e = h.chAtPos(h.chHistPosNewest(0));
    snprintf(reply, n, "> %d,%s", (int)e.ch_idx, e.text);
    return true;
  }
  if (!strcmp(cmd, "guide")) {
#if FEAT_ROUTES
    const RouteGuide& g = core.guide;
    char label[40];
    g.label(label, sizeof(label), false);
    snprintf(reply, n, "> %d,%d,%lu,%lu,%s", (int)g.state(), g.slot(), (unsigned long)g.remainingM(),
             (unsigned long)g.offM(), label);
#else
    snprintf(reply, n, "Error, no routes on this board");
#endif
    return true;
  }
  return false;
}

}  // namespace testhooks
#endif
