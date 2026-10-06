#pragma once
// Settings > System > Firmware update: one-button OTA from this fork's GitHub
// Releases. The latest release's app image for this board (the asset whose
// name ends in OTA_ASSET_SUFFIX, published by build-solo-firmwares.yml) is
// streamed over verified TLS (the framework's Mozilla CA bundle) straight into
// the idle OTA slot; the device then restarts into it. Settings, contacts and
// messages live outside the app slots and are kept.
//
// Needs Arduino-ESP32 3.x for the CA bundle: the 2.0.17 build and the sim say
// so instead of offering the update.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

#ifndef OTA_REPO
  #define OTA_REPO "MarekZegare4/MeshCore-Solo"
#endif

#if defined(ESP32) && defined(OTA_ASSET_SUFFIX) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #define OTA_SUPPORTED 1
  #include <Update.h>
  #include <NetworkClientSecure.h>
  #include <HTTPClient.h>
  extern const uint8_t ota_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
  extern const uint8_t ota_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");
#else
  #define OTA_SUPPORTED 0
#endif

namespace ota {

enum State : uint8_t { IDLE, CONNECTING, CHECKING, CHECKED, INSTALLING, DONE, FAILED };
static volatile uint8_t s_state = IDLE;
static char s_msg[96] = "";          // why it failed / what was found
static char s_latest[24] = "";       // the release tag
static char s_url[256] = "";         // the asset's download URL
static volatile uint32_t s_size = 0, s_written = 0;
static uint32_t s_since = 0;         // WiFi connect start / restart countdown
static bool s_newer = false;         // the release is newer than this build
static uint8_t s_job = 0;            // what the task does once the WiFi is up: 1 check, 2 install

// "v1.28" / "1.28.3" / "v2.0-rc1" -> comparable number; 0 when it isn't a
// version (a dev build). A release candidate comes before its release
// (v2.0-rc1 < v2.0-rc2 < v2.0).
static uint32_t versionNum(const char* v) {
  if (*v == 'v' || *v == 'V') v++;
  unsigned a = 0, b = 0, c = 0, rc = 0;
  int n = sscanf(v, "%u.%u.%u", &a, &b, &c);
  if (n < 2) return 0;
  const char* r = strstr(v, "-rc");
  if (r) rc = (unsigned)atoi(r + 3);
  if (rc > 62) rc = 62;
  return (a << 22) | ((b & 1023) << 12) | ((c & 63) << 6) | (r ? rc : 63);
}

#if OTA_SUPPORTED
static TaskHandle_t s_task = nullptr;

static void fail(const char* m) { snprintf(s_msg, sizeof(s_msg), "%s", m); s_state = FAILED; }

// "key":"value" at or after `from`.
static bool jsonStr(const char* from, const char* key, char* out, size_t n) {
  char pat[40];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);
  const char* p = strstr(from, pat);
  if (!p) return false;
  p += strlen(pat);
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < n) out[i++] = *p++;
  out[i] = '\0';
  return *p == '"';
}

static void check(HTTPClient& http, NetworkClientSecure& tls) {
  if (!http.begin(tls, "https://api.github.com/repos/" OTA_REPO "/releases/latest")) { fail("Bad release URL"); return; }
  http.addHeader("Accept", "application/vnd.github+json");
  int code = http.GET();
  if (code != 200) {
    if (code == 404) fail("No published release yet");
    else if (code < 0) fail(HTTPClient::errorToString(code).c_str());
    else snprintf(s_msg, sizeof(s_msg), "GitHub answered %d", code), s_state = FAILED;
    http.end();
    return;
  }
  String body = http.getString();   // tens of KB: lands in PSRAM (large mallocs do)
  http.end();
  const char* b = body.c_str();
  if (!jsonStr(b, "tag_name", s_latest, sizeof(s_latest))) { fail("Unexpected answer from GitHub"); return; }
  const char* a = strstr(b, OTA_ASSET_SUFFIX "\"");   // the asset's "name" comes before its size / URL
  if (!a) { snprintf(s_msg, sizeof(s_msg), "%s has no image for this device", s_latest); s_state = FAILED; return; }
  const char* sz = strstr(a, "\"size\":");
  s_size = sz ? (uint32_t)strtoul(sz + 7, nullptr, 10) : 0;
  if (!jsonStr(a, "browser_download_url", s_url, sizeof(s_url)) || !s_size) { fail("Unexpected answer from GitHub"); return; }
  uint32_t have = versionNum(FIRMWARE_VERSION), got = versionNum(s_latest);
  s_newer = !have || got > have;
  s_state = CHECKED;
}

static void install(HTTPClient& http, NetworkClientSecure& tls) {
  s_written = 0;
  if (!http.begin(tls, s_url)) { fail("Bad download URL"); return; }
  int code = http.GET();   // github.com answers with a redirect to its download host
  if (code != 200) {
    if (code < 0) fail(HTTPClient::errorToString(code).c_str());
    else snprintf(s_msg, sizeof(s_msg), "Download answered %d", code), s_state = FAILED;
    http.end();
    return;
  }
  int len = http.getSize();
  if (len <= 0 || (uint32_t)len != s_size) { fail("Download size doesn't match the release"); http.end(); return; }
  if (!Update.begin(len)) { snprintf(s_msg, sizeof(s_msg), "No room for the update: %s", Update.errorString()); s_state = FAILED; http.end(); return; }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)malloc(4096);
  NetworkClient* in = http.getStreamPtr();
  uint32_t last_data = millis();
  bool ok = buf != nullptr;
  if (!ok) snprintf(s_msg, sizeof(s_msg), "Out of memory");
  while (ok && s_written < (uint32_t)len) {
    size_t avail = in->available();
    if (!avail || (s_written == 0 && avail < 16)) {   // the header check below wants its 16 bytes in one read
      if (!in->connected() || millis() - last_data > 20000) { snprintf(s_msg, sizeof(s_msg), "Download stalled"); ok = false; break; }
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }
    int n = in->readBytes(buf, avail > 4096 ? 4096 : avail);
    if (n <= 0) continue;
    last_data = millis();
    // The image header: ESP magic, and the chip (offset 12, little endian) must be this one.
    if (s_written == 0 && (n < 16 || buf[0] != 0xE9 || (buf[12] | (buf[13] << 8)) != CONFIG_IDF_FIRMWARE_CHIP_ID)) {
      snprintf(s_msg, sizeof(s_msg), "Not a firmware image for this chip");
      ok = false;
      break;
    }
    if (Update.write(buf, n) != (size_t)n) { snprintf(s_msg, sizeof(s_msg), "Flash write failed: %s", Update.errorString()); ok = false; break; }
    s_written += n;
  }
  free(buf);
  http.end();
  if (!ok) { Update.abort(); s_state = FAILED; return; }
  if (!Update.end(true)) { snprintf(s_msg, sizeof(s_msg), "Image rejected: %s", Update.errorString()); s_state = FAILED; return; }
  s_state = DONE;
}

static void task(void*) {
  mbedtls_platform_set_calloc_free(lvport::tlsCalloc, heap_caps_free);   // TLS buffers in PSRAM
  {
    NetworkClientSecure tls;
    tls.setCACertBundle(ota_crt_bundle_start, ota_crt_bundle_end - ota_crt_bundle_start);
    tls.setHandshakeTimeout(20);
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setConnectTimeout(10000);
    http.setTimeout(20000);
    http.setUserAgent("MeshCore-Solo-OTA");
    if (s_job == 1) check(http, tls); else install(http, tls);
  }
  s_task = nullptr;
  vTaskDelete(nullptr);
}

static bool startTask() {
  s_state = s_job == 1 ? CHECKING : INSTALLING;
  // Idle priority, as the tile fetch (LvglPort.h): HTTPClient only yields
  // with delay(0), which at priority 1 starves IDLE0 into a watchdog reset.
  if (xTaskCreatePinnedToCore(task, "ota", 12288, nullptr, tskIDLE_PRIORITY, &s_task, 0) != pdPASS) {
    s_task = nullptr;
    fail("Out of memory (update task)");
    return false;
  }
  return true;
}
#endif

}  // namespace ota

static lv_obj_t* s_ota_latest = nullptr;   // the release found, once checked

static void onOpenOta(lv_event_t* e) { (void)e; s_ui->showOta(); }
static void onOtaButton(lv_event_t* e) { (void)e; s_ui->otaAction(); }

void UITask::showOta() {
  _screen = SCR_OTA;
  buildOta();
}

// Something else holds the WiFi: a scan leaves it up.
bool UITask::wifiInUse() const { return mapview::s_dl.active() || otaBusy() || ota::s_state == ota::CONNECTING; }

bool UITask::otaBusy() const { return ota::s_state == ota::CHECKING || ota::s_state == ota::INSTALLING || ota::s_state == ota::DONE; }

void UITask::buildOta() {
  lv_obj_t* body = newScreen("Firmware update", true);
  lv_obj_t* card = infoCard(body);
  infoRow(card, "Installed", FIRMWARE_VERSION);
  s_ota_latest = infoRow(card, "Latest release", "");
  _ota_status = noteLabel(body, "", THEME_FONT_BODY, theme::TEXT);
  _ota_bar = lv_bar_create(body);
  lv_obj_set_size(_ota_bar, LV_PCT(100), 10);
  lv_bar_set_range(_ota_bar, 0, 1000);
  lv_obj_set_style_bg_color(_ota_bar, lv_color_hex(theme::SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(_ota_bar, lv_color_hex(theme::ACCENT), LV_PART_INDICATOR);
  lv_obj_t* b = lv_button_create(body);
  lv_obj_set_size(b, LV_PCT(100), 40);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_add_event_cb(b, onOtaButton, LV_EVENT_CLICKED, NULL);
  _ota_btn = b;
  _ota_btn_lbl = label(b, "", THEME_FONT_BODY, theme::TEXT);
  lv_obj_center(_ota_btn_lbl);
  stylePrimary(b);
  groupNote(body, "From github.com/" OTA_REPO " releases, over WiFi. About a minute; keep the device "
                  "charged. Settings, contacts and messages are kept.");
  refreshOta();
}

void UITask::refreshOta() {
  if (_screen != SCR_OTA || !_ota_status) return;
  using namespace ota;
  const char* btn = "Check for update";
  bool bar = false, btn_on = true;
  char st[128];
  st[0] = '\0';
  switch (s_state) {
    case IDLE:
#if OTA_SUPPORTED
      snprintf(st, sizeof(st), "Looks for a newer release on GitHub.");
#elif defined(SIM_PLATFORM)
      snprintf(st, sizeof(st), "Not available in the simulator.");
      btn_on = false;
#else
      snprintf(st, sizeof(st), "Not available in this build (needs the Arduino-ESP32 3.x firmware line).");
      btn_on = false;
#endif
      break;
    case CONNECTING: snprintf(st, sizeof(st), "Connecting to WiFi..."); btn_on = false; break;
    case CHECKING:   snprintf(st, sizeof(st), "Asking GitHub for the latest release..."); btn_on = false; break;
    case CHECKED:
      if (s_newer) {
        snprintf(st, sizeof(st), "%s is available.", s_latest);
        static char b[48];
        snprintf(b, sizeof(b), LV_SYMBOL_DOWNLOAD "  Install %s (%.1f MB)", s_latest, s_size / 1048576.0);
        btn = b;
      } else {
        snprintf(st, sizeof(st), "Up to date - the latest release is %s.", s_latest);
        btn = "Check again";
      }
      break;
    case INSTALLING:
      snprintf(st, sizeof(st), "Installing %s: %lu of %lu KB. Don't turn the device off.", s_latest,
               (unsigned long)(s_written / 1024), (unsigned long)(s_size / 1024));
      bar = true; btn_on = false;
      if (s_size) lv_bar_set_value(_ota_bar, (int32_t)((uint64_t)s_written * 1000 / s_size), LV_ANIM_OFF);
      break;
    case DONE:
      snprintf(st, sizeof(st), "Installed %s - restarting...", s_latest);
      bar = true; btn_on = false;
      lv_bar_set_value(_ota_bar, 1000, LV_ANIM_OFF);
      break;
    case FAILED:
      snprintf(st, sizeof(st), "%s", s_msg);
      btn = "Try again";
      break;
  }
  lv_label_set_text(_ota_status, st);
  if (s_ota_latest) infoSet(s_ota_latest, s_latest);
  lv_label_set_text(_ota_btn_lbl, btn);
  if (bar) lv_obj_remove_flag(_ota_bar, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_ota_bar, LV_OBJ_FLAG_HIDDEN);
  if (btn_on) lv_obj_remove_flag(_ota_btn, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(_ota_btn, LV_OBJ_FLAG_HIDDEN);
}

// The button: check (after bringing the WiFi up), then install what was found.
void UITask::otaAction() {
#if OTA_SUPPORTED
  using namespace ota;
  if (otaBusy() || s_state == CONNECTING) return;
  bool install = s_state == CHECKED && s_newer;
  char ssid[33], pass[65];
  if (!lvport::wifiAllowed()) { showToast("WiFi is off - Settings > WiFi"); return; }
  if (!lvport::loadWifi(ssid, sizeof(ssid), pass, sizeof(pass))) { showToast("Pick a WiFi network first - Settings > WiFi", 3000); return; }
  if (mapview::s_dl.active()) { showToast("A map download is running - stop it first"); return; }
  mapview::s_dl.liveEnd();   // the update owns the WiFi from here
  s_job = install ? 2 : 1;
  if (lvport::netState() == lvport::NET_UP) startTask();
  else { lvport::netBegin(ssid, pass); s_state = CONNECTING; s_since = millis(); }
  refreshOta();
#endif
}

// Every loop pass (the install runs on with the screen off).
void UITask::otaTick() {
  using namespace ota;
  static uint32_t next = 0;
  if (s_state == IDLE || (int32_t)(millis() - next) < 0) return;
  next = millis() + 250;
#if OTA_SUPPORTED
  if (s_state == CONNECTING) {
    int ns = lvport::netState();
    if (ns == lvport::NET_UP) startTask();
    else if (ns == lvport::NET_FAILED || millis() - s_since > 20000) { lvport::netEnd(); fail("WiFi: can't connect"); }
  } else if (s_state == DONE) {
    if (!s_since || s_job) { s_since = millis(); s_job = 0; }
    else if (millis() - s_since > 2000) { the_mesh.savePrefs(); ESP.restart(); }
  } else if (s_state == FAILED && _screen != SCR_OTA) {
    lvport::netEnd();
    s_state = IDLE;
  }
#endif
  refreshOta();
}

// Leaving the screen: the WiFi goes unless the update is running.
void UITask::otaLeave() {
  if (otaBusy()) { showToast("Updating - wait for it to finish"); return; }
  if (ota::s_state != ota::IDLE) { lvport::netEnd(); ota::s_state = ota::IDLE; }
  showSettings();
}
