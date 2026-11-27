#pragma once
// Settings > Coverage map: MatterMap recording (ui-core/CoverageRecorder.h),
// voluntary and off by default, and the upload of finished files to
// mattermeshos.com over the saved WiFi. The upload is MatterMap's resumable
// API (https://mattermeshos.com/coverage/developers/): begin -> PUT chunks of
// whole rows (12 KB at most) at the offset the server last acknowledged ->
// end; a file the server has (done) or refuses (4xx) goes to sent/ or
// failed/. A network error leaves it for the next try.
//
// On its own (Upload automatically, the default once recording is on): when
// finished files wait and WiFi is allowed with a network saved, at most every
// COV_AUTO_RETRY_MS -- joining for it, as the firmware update does, and
// letting the WiFi go afterwards. "Upload now" tries at once.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define COVUP_SUPPORTED 1
  #include <NetworkClientSecure.h>
  #include <HTTPClient.h>
  #include <esp_mac.h>
  extern const uint8_t ota_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t ota_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
#else
  #define COVUP_SUPPORTED 0
#endif

namespace covup {

static const char* const BASE = "https://mattermeshos.com";
#ifdef SOLO_TEST_HOOKS
// A test build fakes GPS fixes (ui-core/TestHooks.h): its files go elsewhere,
// where a release build never looks, and it uploads nothing at all -- so no
// made-up data reaches the map, even from a test cut short.
static const char* const DIR = "/sdcard/meshcore/coverage-test";
#else
static const char* const DIR = "/sdcard/meshcore/coverage";
#endif
static const uint32_t CHUNK = 12 * 1024;
static const uint32_t COV_AUTO_RETRY_MS = 30UL * 60 * 1000;   // after a failed try, on its own
static const int MAX_FILES = 8;                                // one batch

enum State : uint8_t { IDLE, CONNECTING, UPLOADING, DONE, FAILED };
static volatile uint8_t s_state = IDLE;
static char s_msg[96] = "";              // the last batch's outcome, for the page
static char s_files[MAX_FILES][CoverageRecorder::NAME_LEN + 4];
static int s_nfiles = 0;
static volatile int s_file_i = 0;        // the file going now
static volatile uint32_t s_sent_b = 0, s_size_b = 0;
static uint32_t s_since = 0;             // WiFi connect start
static uint32_t s_next_auto = 0;         // no try on its own before this (millis)
static bool s_we_joined = false;         // the WiFi was brought up for this: let go after

static bool sdReady() { return lvport::mountStorage(); }

#if COVUP_SUPPORTED
static TaskHandle_t s_task = nullptr;

static long jsonNum(const char* b, const char* key, long dflt) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":", key);
  const char* p = strstr(b, pat);
  return p ? strtol(p + strlen(pat), nullptr, 10) : dflt;
}
static bool jsonTrue(const char* b, const char* key) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":true", key);
  return strstr(b, pat) != nullptr;
}
static bool jsonStr(const char* b, const char* key, char* out, size_t n) {
  char pat[24];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(b, pat);
  if (!p) return false;
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = '\0';
  return *p == '"';
}

// One file. 1 sent (or already there), 0 a network error (try later), -1 refused.
static int sendFile(HTTPClient& http, NetworkClientSecure& tls, const CoverageRecorder& rec, const char* fname, char* buf) {
  char path[96];
  snprintf(path, sizeof(path), "%s/%s", DIR, fname);
  FILE* f = fopen(path, "r");
  if (!f) { snprintf(s_msg, sizeof(s_msg), "Can't read %s", fname); return -1; }
  // Its size and rows (lines not starting with '#'), read in the upload's
  // own CHUNK-sized blocks: one quick pass, each line counted once however long.
  uint32_t size = 0, rows = 0;
  bool bol = true;   // at the start of a line
  for (size_t n; (n = fread(buf, 1, CHUNK, f)) > 0; ) {
    size += n;
    for (size_t k = 0; k < n; k++) {
      if (bol && buf[k] != '#') rows++;
      bol = buf[k] == '\n';
    }
  }
  s_size_b = size;
  s_sent_b = 0;
  char name[CoverageRecorder::NAME_LEN];
  snprintf(name, sizeof(name), "%.*s", CoverageRecorder::NAME_LEN - 1, fname);

  // Begin: the same dev + name again says how much the server has.
  char url[96], body[192], id[32] = "";
  snprintf(url, sizeof(url), "%s/api/coverage/begin", BASE);
  snprintf(body, sizeof(body), "{\"dev\":\"%s\",\"board\":\"%s\",\"fw\":\"%s\",\"name\":\"%s\",\"rows\":%lu,\"bytes\":%lu}",
           rec.deviceIdHex(), rec.board(), rec.fw(), name, (unsigned long)rows, (unsigned long)size);
  int code = -1;
  String ans;
  if (http.begin(tls, url)) {
    http.addHeader("Content-Type", "application/json");
    code = http.POST((uint8_t*)body, strlen(body));
    ans = code > 0 ? http.getString() : String();
    http.end();
  }
  if (code < 0) { fclose(f); snprintf(s_msg, sizeof(s_msg), "Network: %s", HTTPClient::errorToString(code).c_str()); return 0; }
  if (code != 200) { fclose(f); snprintf(s_msg, sizeof(s_msg), "%s refused (%d): %.40s", name, code, ans.c_str()); return code >= 500 ? 0 : -1; }
  if (!jsonStr(ans.c_str(), "id", id, sizeof(id))) { fclose(f); snprintf(s_msg, sizeof(s_msg), "Unexpected answer from the map"); return 0; }
  uint32_t got = (uint32_t)jsonNum(ans.c_str(), "received", 0);
  if (jsonTrue(ans.c_str(), "done")) { fclose(f); s_sent_b = size; return 1; }

  // Chunks: whole rows, from what the server acknowledged. Each answer must
  // move it on; a server that keeps saying the same offset is given up on.
  int resyncs = 0;
  while (got < size) {
    if (fseek(f, got, SEEK_SET) != 0) { fclose(f); snprintf(s_msg, sizeof(s_msg), "Can't read %s", fname); return -1; }
    size_t n = fread(buf, 1, CHUNK, f);
    if (got + n < size) {   // cut after the last whole row
      while (n > 0 && buf[n - 1] != '\n') n--;
    }
    if (n == 0) { fclose(f); snprintf(s_msg, sizeof(s_msg), "%s: a row too long", name); return -1; }
    snprintf(url, sizeof(url), "%s/api/coverage/%s?offset=%lu", BASE, id, (unsigned long)got);
    code = -1;
    if (http.begin(tls, url)) {
      http.addHeader("Content-Type", "text/csv");
      code = http.PUT((uint8_t*)buf, n);
      ans = code > 0 ? http.getString() : String();
      http.end();
    }
    if (code < 0) { fclose(f); snprintf(s_msg, sizeof(s_msg), "Network: %s", HTTPClient::errorToString(code).c_str()); return 0; }
    long r = jsonNum(ans.c_str(), "received", -1);
    if (code == 409 && r >= 0) {   // the server says where it is
      if ((uint32_t)r != got && ++resyncs <= 3) { got = (uint32_t)r; continue; }
      fclose(f); snprintf(s_msg, sizeof(s_msg), "%s: out of step with the map, trying later", name); return 0;
    }
    if (code == 200 && r >= 0 && (uint32_t)r <= got) {   // took nothing: later, not again and again
      fclose(f); snprintf(s_msg, sizeof(s_msg), "%s: the map took nothing, trying later", name); return 0;
    }
    if (code != 200 || r < 0) { fclose(f); snprintf(s_msg, sizeof(s_msg), "%s refused (%d): %.40s", name, code, ans.c_str()); return code >= 500 ? 0 : -1; }
    got = (uint32_t)r;
    s_sent_b = got;
  }
  fclose(f);

  // End: the server cleans the rows and adds them to the map.
  snprintf(url, sizeof(url), "%s/api/coverage/%s/end", BASE, id);
  code = -1;
  if (http.begin(tls, url)) {
    code = http.POST((uint8_t*)"", 0);
    ans = code > 0 ? http.getString() : String();
    http.end();
  }
  if (code < 0) { snprintf(s_msg, sizeof(s_msg), "Network: %s", HTTPClient::errorToString(code).c_str()); return 0; }
  if (code != 200) { snprintf(s_msg, sizeof(s_msg), "%s refused (%d): %.40s", name, code, ans.c_str()); return code >= 500 ? 0 : -1; }
  snprintf(s_msg, sizeof(s_msg), "Sent %s: %ld rows kept, %ld areas", name, jsonNum(ans.c_str(), "rows", 0), jsonNum(ans.c_str(), "cells", 0));
  return 1;
}

static void task(void* arg) {
  CoverageRecorder* rec = (CoverageRecorder*)arg;
  mbedtls_platform_set_calloc_free(lvport::tlsCalloc, heap_caps_free);   // TLS buffers in PSRAM
  char* buf = (char*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool net_err = false;
  int sent = 0, refused = 0;
  if (!buf) { snprintf(s_msg, sizeof(s_msg), "Out of memory"); net_err = true; }
  else {
    NetworkClientSecure tls;
    tls.setCACertBundle(ota_crt_bundle_start, ota_crt_bundle_end - ota_crt_bundle_start);
    tls.setHandshakeTimeout(20);
    HTTPClient http;
    http.setReuse(true);
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    http.setUserAgent("MeshCore-Solo-MatterMap");
    for (int i = 0; i < s_nfiles; i++) {
      s_file_i = i;
      // The home zone may have been set since the file was written: its rows
      // there go first (the first try only -- a resumed one sends the same bytes).
      if (rec->scrubForUpload(s_files[i]) < 0) {   // never sent with its home rows
        snprintf(s_msg, sizeof(s_msg), "%s: couldn't take the home zone out - is the card full?", s_files[i]);
        refused++;
        continue;
      }
      char path[96];
      struct stat st;
      snprintf(path, sizeof(path), "%s/%s", DIR, s_files[i]);
      if (stat(path, &st) != 0) { rec->moveDone(s_files[i], true); continue; }   // home rows only: nothing left to send
      int r = sendFile(http, tls, *rec, s_files[i], buf);
      if (r == 0) { net_err = true; break; }
      rec->moveDone(s_files[i], r > 0);
      if (r > 0) sent++; else refused++;
    }
  }
  free(buf);
  if (!net_err && s_nfiles > 1 && !refused) snprintf(s_msg, sizeof(s_msg), "Sent %d files", sent);
  s_state = net_err || refused ? FAILED : DONE;
  s_task = nullptr;
  vTaskDelete(nullptr);
}
#endif

}  // namespace covup

// Something holds the WiFi for the upload.
bool UITask::coverageBusy() const { return covup::s_state == covup::CONNECTING || covup::s_state == covup::UPLOADING; }

void UITask::coverageBegin() {
  uint8_t mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };   // the sim
#if COVUP_SUPPORTED
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
#endif
  char fw[32];
  snprintf(fw, sizeof(fw), "solo-%s", FIRMWARE_VERSION);
  _core->coverage.begin(_prefs, covup::DIR, covup::sdReady, "wio-l2", fw, mac);
  covup::s_next_auto = millis() + 60000;   // not in the first minute: the start has enough to do
}

// "Upload now", or on its own (auto). False when it can't start (said why, when asked).
bool UITask::coverageUpload(bool asked) {
  using namespace covup;
#ifdef SOLO_TEST_HOOKS
  if (asked) showToast("A test build uploads nothing");
  return false;
#endif
#if COVUP_SUPPORTED
  if (coverageBusy() || s_task) return false;
  if (asked) _core->coverage.finishNow();
  s_nfiles = _core->coverage.pending(s_files, MAX_FILES);
  if (s_nfiles == 0) { if (asked) showToast("Nothing to upload"); return false; }
  char ssid[33], pass[65];
  if (!lvport::wifiAllowed()) { if (asked) showToast("WiFi is off - Settings > WiFi"); return false; }
  if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) { if (asked) showToast("Pick a WiFi network first - Settings > WiFi", 3000); return false; }
  if (mapview::s_dl.active() || otaBusy()) { if (asked) showToast("WiFi busy (a download or an update) - try later"); return false; }
  s_msg[0] = '\0';
  s_file_i = 0;
  s_sent_b = 0;
  s_size_b = 0;
  crashlog::note("cov upload %d", s_nfiles);
  if (lvport::netState() == lvport::NET_UP) { s_we_joined = false; coverageStartTask(); }
  else { s_we_joined = true; lvport::netBegin(ssid, pass); s_state = CONNECTING; s_since = millis(); }
  return true;
#else
  (void)asked;
  if (asked) showToast("Uploads need the device (not the simulator)");
  return false;
#endif
}

void UITask::coverageStartTask() {
#if COVUP_SUPPORTED
  using namespace covup;
  s_state = UPLOADING;
  // Idle priority, as the firmware update (OtaScreen.h) and the tile fetch.
  if (xTaskCreatePinnedToCore(task, "covup", 12288, &_core->coverage, tskIDLE_PRIORITY, &s_task, 0) != pdPASS) {
    s_task = nullptr;
    snprintf(s_msg, sizeof(s_msg), "Out of memory (upload task)");
    s_state = FAILED;
  }
#endif
}

// Every loop pass: the WiFi join, the end of a batch, the tries on its own.
void UITask::coverageTick() {
  using namespace covup;
  if (!_prefs || !_core->coverage.available()) return;
  static uint32_t next = 0;
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 500;
  if (s_state == CONNECTING) {
    int ns = lvport::netState();
    if (ns == lvport::NET_UP) coverageStartTask();
    else if (ns == lvport::NET_FAILED || millis() - s_since > 20000) {
      lvport::netEnd();
      snprintf(s_msg, sizeof(s_msg), "WiFi: can't connect");
      s_state = FAILED;
    }
  } else if (s_state == DONE || s_state == FAILED) {
    crashlog::note("cov %s", s_state == DONE ? "sent" : "failed");
    if (s_we_joined && !wifiInUse() && !mapview::s_dl.liveNet() && !_wifi_scanning) lvport::netEnd();
    s_we_joined = false;
    s_next_auto = millis() + (s_state == FAILED ? COV_AUTO_RETRY_MS : 60000);
    s_state = IDLE;
    if (_screen == SCR_SETTINGS_NAV && _settings_page == PG_COVERAGE) buildSchemaSettings();
  } else if (s_state == IDLE && _prefs->cov_auto && (int32_t)(millis() - s_next_auto) >= 0) {
    s_next_auto = millis() + 60000;   // looked again in a minute (a file finished meanwhile)
    if (_core->coverage.pending(nullptr, 0) > 0 && !wifiInUse() && !_wifi_scanning) coverageUpload(false);
  }
  // The page's live line while it's open.
  if (_screen == SCR_SETTINGS_NAV && _settings_page == PG_COVERAGE && _cov_status) {
    char st[128];
    coverageStatusText(st, sizeof(st));
    lv_label_set_text(_cov_status, st);
  }
}

void UITask::coverageStatusText(char* st, size_t n) {
  using namespace covup;
  CoverageRecorder::Status s = _core->coverage.status();
  int pend = s.storage ? _core->coverage.pending(nullptr, 0) : 0;
  int o = 0;
  if (!s.storage) o += snprintf(st + o, n - o, "No SD card: nothing is recorded.");
  else if (!_prefs->cov_on) o += snprintf(st + o, n - o, "Not recording.");
  else if (s.home) o += snprintf(st + o, n - o, "In the home zone: not recording here.");
  else if (!s.fix_ok) o += snprintf(st + o, n - o, "Waiting for a GPS fix.");
  else o += snprintf(st + o, n - o, "Recording: %lu points, %lu KB.", (unsigned long)s.rows, (unsigned long)(s.bytes / 1024));
  if (s_state == CONNECTING) o += snprintf(st + o, n - o, "\nConnecting to WiFi...");
  else if (s_state == UPLOADING)
    o += snprintf(st + o, n - o, "\nUploading %d of %d: %lu of %lu KB", s_file_i + 1, s_nfiles,
                  (unsigned long)(s_sent_b / 1024), (unsigned long)(s_size_b / 1024));
  else if (pend) o += snprintf(st + o, n - o, "\n%d file%s waiting to upload.", pend, pend == 1 ? "" : "s");
  if (s_msg[0] && s_state != UPLOADING) snprintf(st + o, n - o, "\n%s", s_msg);
}

// ── The page ─────────────────────────────────────────────────────────────────
static void onCovSwitch(lv_event_t* e) {
  s_ui->coverageSet(0, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onCovAuto(lv_event_t* e) {
  s_ui->coverageSet(1, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onCovRadius(lv_event_t* e) { s_ui->coverageSet(2, choiceSelected((lv_obj_t*)lv_event_get_target(e))); }
static void onCovHome(lv_event_t* e)   { (void)e; s_ui->coverageHomePopup(); }
static void onCovHomePick(lv_event_t* e) { s_ui->coverageHomePick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onCovUpload(lv_event_t* e) { (void)e; s_ui->coverageUpload(true); }
static void onCovClear(lv_event_t* e)  { (void)e; s_ui->coverageClear(); }
static lv_obj_t* s_cov_clear_lbl = nullptr;

void UITask::coverageSet(int what, int v) {
  if (!_prefs) return;
  if (what == 0) { _prefs->cov_on = (uint8_t)v; _core->coverage.apply(); }
  else if (what == 1) _prefs->cov_auto = (uint8_t)v;
  else _prefs->cov_home_r = (uint8_t)v;
  prefsSave();
  if (what == 2) coverageScrubHome();     // a larger zone takes in rows already recorded
  if (what == 0) buildSchemaSettings();   // the status line and what's shown follow it
}

// The home zone set, moved or grown: the rows already on the card inside it
// go now. During an upload the task does it file by file before sending.
void UITask::coverageScrubHome() {
  if (coverageBusy()) return;
  const int n = _core->coverage.scrubHome();
  if (n <= 0) return;
  char t[48];
  snprintf(t, sizeof(t), "Removed %d point%s in the home zone", n, n == 1 ? "" : "s");
  showToast(t, 2500);
}

// Delete everything recorded, after a second tap. What the map has stays.
void UITask::coverageClear() {
  if (coverageBusy()) { showToast("Uploading - try again when it's done"); return; }
  if (!tapConfirmed(s_cov_clear_lbl, "Tap again to delete")) return;
  if (!_core->coverage.clearAll()) { showToast("No SD card"); return; }
  covup::s_msg[0] = '\0';
  showToast("Recorded points deleted");
  buildSchemaSettings();
}

enum : int { COVHOME_HERE, COVHOME_CLEAR, COVHOME_WP0 = 100 };

void UITask::coverageHomePopup() {
  lv_obj_t* list = scrollList(navPopupPanel("Home", true));
  int32_t lat, lon;
  uint32_t age;
  // Always there, saying what the GPS needs when it can't give a place yet.
  const char* here = "Where you are now";
  if (!(_core->course.lastFix(lat, lon, age) && age < 30))
    here = !_core->gpsAvailable() ? "No GPS on this device" : !_core->gpsEnabled() ? "The GPS is off: tap to turn it on" : "Waiting for a GPS fix";
  listRow(list, LV_SYMBOL_GPS "  Here", here, onCovHomePick, (void*)(uintptr_t)COVHOME_HERE);
  const WaypointModel& wp = _core->waypoints;
  for (int i = 0; i < wp.count(); i++) {
    const Waypoint& w = wp.at(i);
    listRow(list, w.label[0] ? w.label : "(unnamed)", nullptr, onCovHomePick, (void*)(uintptr_t)(COVHOME_WP0 + i));
  }
  if (wp.count() == 0) noteLabel(list, "No waypoints yet: the map's pin button (Places) marks one.");
  if (_prefs && (_prefs->cov_home_lat || _prefs->cov_home_lon)) actionRow(list, "Clear", onCovHomePick, (void*)(uintptr_t)COVHOME_CLEAR);
}

void UITask::coverageHomePick(int code) {
  if (!_prefs) return;
  int32_t lat = 0, lon = 0;
  uint32_t age;
  if (code == COVHOME_HERE) {
    if (!_core->course.lastFix(lat, lon, age) || age >= 30) {
      if (_core->gpsAvailable() && !_core->gpsEnabled()) { setGps(true); coverageHomePopup(); showToast("GPS on - tap Here once it has a fix", 2500); }
      else showToast(_core->gpsAvailable() ? "No GPS fix yet - try again outdoors" : "No GPS on this device", 2500);
      return;
    }
  }
  else if (code >= COVHOME_WP0 && code - COVHOME_WP0 < _core->waypoints.count()) {
    const Waypoint& w = _core->waypoints.at(code - COVHOME_WP0);
    lat = w.lat_1e6; lon = w.lon_1e6;
  }
  _prefs->cov_home_lat = lat;
  _prefs->cov_home_lon = lon;
  prefsSave();
  navClosePopup();
  coverageScrubHome();
  buildSchemaSettings();
}

void UITask::buildCoveragePage(lv_obj_t* body) {
  _cov_status = nullptr;
  if (!_prefs) return;
  lv_obj_t* g = group(body, nullptr);
  switchRow(g, "Record coverage", "Off: nothing is recorded or sent", _prefs->cov_on, onCovSwitch);
  groupNote(body, "Help draw MatterMap, a MeshCore coverage map by MatterMesh OS. Voluntary: off unless you turn it on.");

  g = group(body, "HOW IT WORKS");
  infoNote(g, "Recorded", "Each packet your radio hears: the time, your position, SNR, RSSI and the repeater that "
                          "passed it on; every 30 s with nothing heard, a \"nothing heard\" point.");
  infoNote(g, "Not recorded", "Without a GPS fix, with the clock not set, or in the home zone.");
  infoNote(g, "On the mesh", "Nothing: the device only listens.");
  infoNote(g, "Kept", "On the SD card (meshcore/coverage) until it's uploaded.");
  infoNote(g, "Sent", "To mattermeshos.com over your saved WiFi, with the board, the firmware version and a "
                      "device number made from a hash (not your node's key or address).");
  infoNote(g, "On the map", "Only areas of about 200 m: never who measured them, never a route. Raw files are "
                            "deleted after 7 days.");

  g = group(body, "HOME ZONE");
  char opts[48];
  int o = 0;
  for (uint8_t i = 0; i < NodePrefs::COV_HOME_COUNT; i++) {
    uint32_t r = NodePrefs::covHomeRadiusM(i);
    o += r ? snprintf(opts + o, sizeof(opts) - o, "%s%lu m", i ? "\n" : "", (unsigned long)r)
           : snprintf(opts + o, sizeof(opts) - o, "%sOff", i ? "\n" : "");
  }
  choiceRow(g, "Radius", "Nothing recorded this close to home", opts, _prefs->cov_home_r % NodePrefs::COV_HOME_COUNT, onCovRadius);
  char where[32];
  if (_prefs->cov_home_lat || _prefs->cov_home_lon) geo::fmtLatLon(where, sizeof(where), _prefs->cov_home_lat, _prefs->cov_home_lon, true);
  else snprintf(where, sizeof(where), "Not set");
  listRow(g, "Home", where, onCovHome, NULL);
  groupNote(body, "Here, or a waypoint. Until one is picked there is no home zone. Points already recorded in it "
                  "are taken out too, before they're uploaded.");

  g = group(body, "UPLOAD");
  switchRow(g, "Upload automatically", "Over the saved WiFi, joined for it", _prefs->cov_auto, onCovAuto);
  actionRow(g, LV_SYMBOL_UPLOAD "  Upload now", onCovUpload, NULL);
  char st[128];
  coverageStatusText(st, sizeof(st));
  _cov_status = infoNote(g, "Status", st);
  s_cov_clear_lbl = actionRow(g, LV_SYMBOL_TRASH "  Delete recorded points", onCovClear, NULL, theme::FAIL);
  groupNote(body, "Everything on the SD card: waiting, being recorded and already sent. What the map has stays there.");
}
