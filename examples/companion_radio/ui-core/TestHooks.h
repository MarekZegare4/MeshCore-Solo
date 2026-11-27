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
//   guidesave <ms>             the guide saves progress at most this often (2 min; till a restart)
//   trackback                  Track back: the trail walked backwards, as Trail > Track back
//   trail on|off|clear         record the trail, stop, empty it (as Trail's own actions)
//   trail                      "> <recording 0/1>,<points>"
//   usb [clear]                the USB serial's frames since boot (or the last clear):
//                              "> <sent>,<cut at the 300 ms limit>,<over 50 ms>,<longest ms>"
//   mem                        "> <free heap>,<total>,<stack free>,<uptime s>" (0: unknown;
//                              an ESP32's internal RAM, not its PSRAM)
//   lastdm                     the newest direct message, "> <in/out>,<prefix hex>,<ack 0-3>,<text>"
//   lastch                     the newest channel post, "> <channel>,<text>"
//   loops                      since the last ask: "> <loop passes a second>,<busy %>,<seconds>"
//   rxps [0|1|2]               RX power save off / for every node / for v1.16+ only, as the
//                              radio runs it (not saved); alone: "> <set>,<armed>,<asleep %>,
//                              <watchdog soft>,<hard>,<packets received>"
//   txpre <symbols>            send with this preamble (an older node's 16) until the radio is set again
//
// The frontends add their own (ui-new/UITask.cpp: ui, btn down|up, screen on|off;
// ui-lvgl/SettingsPages.h: ui, screen on|off, and on the L2 sdbench -- the SD card's
// read speed, SdBench.h). The keys, the one button's gestures and the L2's tap and
// swipe are in every build (the configurator's live view): UITask::remoteInput().

#ifdef SOLO_TEST_HOOKS
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/DeviceDiag.h>
#include <helpers/ArduinoSerialInterface.h>   // g_serial_tx
#ifdef ESP32
  #include <esp_heap_caps.h>
#endif

extern uint32_t g_loop_passes, g_loop_busy_us;   // main.cpp

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

#if ENV_INCLUDE_GPS
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
#else
inline bool install(SensorManager&) { return false; }   // a board with no GPS (the C6, the Sharp)
#endif

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
  if (!strncmp(cmd, "guidesave ", 10)) {
#if FEAT_ROUTES
    core.guide.saveEvery((uint32_t)strtoul(cmd + 10, nullptr, 10));
    snprintf(reply, n, "OK");
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
  if (!strcmp(cmd, "loops")) {
    static uint32_t at_ms = 0, at_passes = 0, at_busy = 0;
    const uint32_t now = millis(), dt = now - at_ms, p = g_loop_passes - at_passes, b = g_loop_busy_us - at_busy;
    const unsigned long permille = dt ? (unsigned long)((uint64_t)b / dt) : 0;   // us busy per ms
    snprintf(reply, n, "> %lu,%lu.%lu,%lu", (unsigned long)(dt ? (uint64_t)p * 1000 / dt : 0),
             permille / 10, permille % 10, (unsigned long)(dt / 1000));
    at_ms = now; at_passes = g_loop_passes; at_busy = g_loop_busy_us;
    return true;
  }
#if defined(USE_SX1262) && !defined(SIM_BUILD)
  if (!strcmp(cmd, "rxps")) {
    const uint16_t pm = radio_driver.sampleAsleepPermille(1000);
    snprintf(reply, n, "> %u,%u,%u.%u,%lu,%lu,%lu", radio_driver.getPowerSaving(), radio_driver.getPowerSavingActive(),
             pm / 10, pm % 10, (unsigned long)radio_driver.getRxPsWatchdogSoftCount(),
             (unsigned long)radio_driver.getRxPsWatchdogHardCount(), (unsigned long)radio_driver.getRecvCount());
    return true;
  }
  if (!strncmp(cmd, "rxps ", 5)) {
    radio_driver.setPowerSaving((uint8_t)atoi(cmd + 5));
    snprintf(reply, n, "OK");
    return true;
  }
  if (!strncmp(cmd, "txpre ", 6)) {
    radio_driver.forceTxPreamble((uint16_t)atoi(cmd + 6));
    snprintf(reply, n, "OK");
    return true;
  }
#endif
  if (!strcmp(cmd, "usb") || !strcmp(cmd, "usb clear")) {   // the USB serial frames: "> <sent>,<cut>,<over 50 ms>,<longest ms>"
    snprintf(reply, n, "> %lu,%lu,%lu,%lu", (unsigned long)g_serial_tx.frames, (unsigned long)g_serial_tx.cut,
             (unsigned long)g_serial_tx.slow, (unsigned long)g_serial_tx.max_ms);
    if (cmd[3]) memset(&g_serial_tx, 0, sizeof(g_serial_tx));
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
  // MatterMap recording: "cov" -> "> on,auto,rows,pending,fix ok,in home,newest pending";
  // "cov on|off", "cov auto on|off" (RAM only, as a test wants), "cov rm <name>".
  if (!strcmp(cmd, "cov") || !strncmp(cmd, "cov ", 4)) {
    CoverageRecorder& c = core.coverage;
    NodePrefs* p = core.prefs();
    if (!c.available() || !p) { snprintf(reply, n, "Error, no coverage recording on this board"); return true; }
    const char* a = cmd + 3;
    while (*a == ' ') a++;
    if (!strcmp(a, "on") || !strcmp(a, "off")) { p->cov_on = a[1] == 'n'; c.apply(); snprintf(reply, n, "OK"); return true; }
    if (!strcmp(a, "auto on") || !strcmp(a, "auto off")) { p->cov_auto = a[6] == 'n'; snprintf(reply, n, "OK"); return true; }
    if (!strcmp(a, "last")) { snprintf(reply, n, "> %s", c.lastRow()); return true; }
    // "cov home" -> "> lat,lon,r"; "cov home <lat>,<lon> <r>" sets it (RAM) and
    // scrubs, -> "> rows taken out".
    if (!strcmp(a, "home")) {
      snprintf(reply, n, "> %ld,%ld,%u", (long)p->cov_home_lat, (long)p->cov_home_lon, (unsigned)p->cov_home_r);
      return true;
    }
    if (!strncmp(a, "home ", 5)) {
      long la = 0, lo = 0;
      unsigned r = 0;
      if (sscanf(a + 5, "%ld,%ld %u", &la, &lo, &r) != 3) { snprintf(reply, n, "Error, cov home <lat_1e6>,<lon_1e6> <r>"); return true; }
      p->cov_home_lat = (int32_t)la; p->cov_home_lon = (int32_t)lo; p->cov_home_r = (uint8_t)r;
      snprintf(reply, n, "> %d", c.scrubHome());
      return true;
    }
    // "cov count <name>" -> "> rows,rows in the home zone" (-1 when it isn't there).
    if (!strncmp(a, "count ", 6)) {
      char path[112];
      snprintf(path, sizeof(path), "%s/%s", c.dir(), a + 6);
      FILE* f = strstr(a, "/") || strstr(a, "..") ? nullptr : fopen(path, "r");
      if (!f) { snprintf(reply, n, "> -1,-1"); return true; }
      char line[160];
      int rows = 0, home = 0;
      while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#') continue;
        rows++;
        const char* la = strchr(line, ',');
        const char* lo = la ? strchr(la + 1, ',') : nullptr;
        if (la && lo && NodePrefs::covHomeRadiusM(p->cov_home_r) && (p->cov_home_lat || p->cov_home_lon)
            && geo::haversineKm((int32_t)lround(strtod(la + 1, nullptr) * 1e6), (int32_t)lround(strtod(lo + 1, nullptr) * 1e6),
                                p->cov_home_lat, p->cov_home_lon) * 1000.0f < NodePrefs::covHomeRadiusM(p->cov_home_r))
          home++;
      }
      fclose(f);
      snprintf(reply, n, "> %d,%d", rows, home);
      return true;
    }
    if (!strcmp(a, "file")) { snprintf(reply, n, "> %s", c.status().name); return true; }   // the one being written
    if (!strcmp(a, "clear")) { snprintf(reply, n, c.clearAll() ? "OK" : "Error, no storage"); return true; }
    if (!strncmp(a, "rm ", 3)) {
      char path[112];
      snprintf(path, sizeof(path), "%s/%s", c.dir(), a + 3);
      snprintf(reply, n, strstr(a, "/") || strstr(a, "..") || remove(path) != 0 ? "Error, not removed" : "OK");
      return true;
    }
    char names[8][CoverageRecorder::NAME_LEN + 4] = {};
    int pend = c.pending(names, 8);
    const char* newest = pend ? names[(pend < 8 ? pend : 8) - 1] : "";
    CoverageRecorder::Status s = c.status();
    snprintf(reply, n, "> %d,%d,%lu,%d,%d,%d,%s", (int)p->cov_on, (int)p->cov_auto, (unsigned long)s.rows, pend,
             (int)s.fix_ok, (int)s.home, newest);
    return true;
  }
  return false;
}

}  // namespace testhooks
#endif
