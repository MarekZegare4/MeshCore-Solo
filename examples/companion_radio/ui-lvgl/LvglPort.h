#pragma once
// LVGL <-> board glue: display flush, pointer input, the storage card (map
// tiles, mounted at /sdcard) and the network used to download map tiles (WiFi
// on the board, the browser in the simulator). One implementation per board;
// the rest of ui-lvgl never touches the hardware directly.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#if defined(SEEED_WIO_TRACKER_L2)
  #include <SD_MMC.h>
  #include <WiFi.h>
  #include <HTTPClient.h>
  #include <WiFiClientSecure.h>
  #include <Preferences.h>
  #include <helpers/esp32/InternalFS.h>
  #include <esp_heap_caps.h>
  #include <esp_system.h>
  #include <esp_core_dump.h>
  #if ESP_ARDUINO_VERSION_MAJOR >= 3
    #include <esp_vfs_fat.h>
    #include <driver/sdmmc_host.h>
    #include <sdmmc_cmd.h>
  #endif
  #if ESP_ARDUINO_VERSION_MAJOR >= 3 && !ARDUINO_USB_MODE && CONFIG_TINYUSB_MSC_ENABLED
    #define LVPORT_USB_DRIVE 1   // the SD card lent to a computer (TinyUSB mass storage)
    #include <USB.h>
    #include <USBMSC.h>
  #endif
  #include <mbedtls/platform.h>
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  #include <emscripten/fetch.h>
  #include <dirent.h>
  #include <sys/stat.h>
#endif

namespace lvport {

// Screenshot: while s_shot points at a full-screen RGB565 buffer, the flush
// callbacks also copy what they send to the panel into it.
static uint16_t* s_shot = nullptr;
static void shotCopy(const lv_area_t* a, const uint8_t* px) {
  if (!s_shot) return;
  const int32_t W = lv_display_get_horizontal_resolution(NULL);
  const int32_t w = a->x2 - a->x1 + 1;
  const uint16_t* src = (const uint16_t*)px;
  for (int32_t y = a->y1; y <= a->y2; y++, src += w) memcpy(s_shot + y * W + a->x1, src, w * 2);
}

// Saved WiFi networks (Settings > WiFi), most recently saved first. Kept in RAM
// once read; wifiStoreLoad() / wifiStoreWrite() (per board, below) hold them.
static const int WIFI_SAVED_MAX = 8;
struct WifiNet { char ssid[33]; char pass[65]; };
static WifiNet s_nets[WIFI_SAVED_MAX];
static int s_net_count = -1;   // -1: not read yet
static void wifiStoreLoad();
static void wifiStoreWrite();
static int wifiSavedCount() {
  if (s_net_count < 0) wifiStoreLoad();
  return s_net_count;
}
static const WifiNet* wifiSaved(int i) { return i >= 0 && i < wifiSavedCount() ? &s_nets[i] : nullptr; }
static int wifiFind(const char* ssid) {
  for (int i = 0; i < wifiSavedCount(); i++) if (!strcmp(s_nets[i].ssid, ssid)) return i;
  return -1;
}
// The most recently saved network (false: none) -- what "have WiFi?" asks.
static bool loadWifi(char* ssid, size_t ssid_n, char* pass, size_t pass_n) {
  const WifiNet* w = wifiSaved(0);
  snprintf(ssid, ssid_n, "%s", w ? w->ssid : "");
  snprintf(pass, pass_n, "%s", w ? w->pass : "");
  return w != nullptr;
}
// Adds a network or changes its password; either way it moves to the top.
// A ninth one pushes out the oldest.
static void saveWifi(const char* ssid, const char* pass) {
  int n = wifiSavedCount();
  int at = wifiFind(ssid);
  if (at < 0) at = n < WIFI_SAVED_MAX ? n++ : WIFI_SAVED_MAX - 1;
  memmove(&s_nets[1], &s_nets[0], at * sizeof(WifiNet));
  snprintf(s_nets[0].ssid, sizeof(s_nets[0].ssid), "%s", ssid);
  snprintf(s_nets[0].pass, sizeof(s_nets[0].pass), "%s", pass);
  s_net_count = n;
  wifiStoreWrite();
}
static void forgetWifi(int i) {
  int n = wifiSavedCount();
  if (i < 0 || i >= n) return;
  memmove(&s_nets[i], &s_nets[i + 1], (n - 1 - i) * sizeof(WifiNet));
  s_net_count = n - 1;
  wifiStoreWrite();
}

// Network for map downloads: state of the link, and one HTTP GET at a time.
enum NetState { NET_OFF, NET_CONNECTING, NET_UP, NET_FAILED };
static const int WIFI_SCAN_MAX = 20;

#if defined(SEEED_WIO_TRACKER_L2)
// LovyanGFX device (NV3031B QSPI panel + GT911 touch) from WioTrackerL2Display.

static lgfx::LGFX_Device* s_gfx = nullptr;
static bool s_swallow = false;   // ignore the touch that woke the display until it lifts
static uint32_t s_flush_us = 0;  // UI_PERF_TEST: time spent in flushCb

// The CPU clock down to 80 MHz (LoRa, BLE and WiFi all run at that) with the
// screen off (powerSave()), and with it on while nobody touches it and nothing
// moves (the loop decides); a touch brings the full clock back before the
// frame it causes is drawn.
static uint32_t s_full_mhz = 0;
static bool s_cpu_slow = false;
static void cpuSlow(bool on) {
  if (on == s_cpu_slow) return;
  if (!s_full_mhz) s_full_mhz = getCpuFrequencyMhz();
  s_cpu_slow = on;
  setCpuFrequencyMhz(on ? 80 : s_full_mhz);
}

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
#ifdef UI_PERF_TEST
  uint32_t t = micros();
#endif
  shotCopy(area, px_map);
  int w = area->x2 - area->x1 + 1;
  int h = area->y2 - area->y1 + 1;
  s_gfx->startWrite();
  s_gfx->setAddrWindow(area->x1, area->y1, w, h);
  s_gfx->pushPixels((uint16_t*)px_map, (uint32_t)w * h, true /* LVGL RGB565 is little-endian */);
  s_gfx->endWrite();
#ifdef UI_PERF_TEST
  s_flush_us += micros() - t;
#endif
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  lgfx::touch_point_t tp;
  bool down = s_gfx->getTouch(&tp, 1) > 0;
  if (s_swallow) {
    if (!down) s_swallow = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  if (down) {
    cpuSlow(false);   // the frame this touch causes: at the full clock
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = tp.x;
    data->point.y = tp.y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static bool begin() {
  s_gfx = display.lgfxDevice();
  s_gfx->setColorDepth(16);
  display.releaseFrameBuffer();

  lv_display_t* disp = lv_display_create(s_gfx->width(), s_gfx->height());
  lv_display_set_flush_cb(disp, flushCb);

  // Two half-screen buffers (75 KB each) in PSRAM: a frame renders in two
  // passes instead of six (every pass walks the whole tree and lays text out
  // again) -- measured ~8% faster than 40 lines; internal RAM / DMA / -O2
  // made no difference (UI_PERF_TEST).
  const uint32_t buf_sz = (uint32_t)s_gfx->width() * 120 * 2;
  uint8_t* buf1 = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
  uint8_t* buf2 = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_SPIRAM);
  if (!buf1) { buf1 = (uint8_t*)heap_caps_malloc(buf_sz, MALLOC_CAP_8BIT); buf2 = nullptr; }
  if (!buf1) return false;
  lv_display_set_buffers(disp, buf1, buf2, buf_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  return true;
}

// Raw panel touch, read while LVGL is paused (display asleep).
static bool touched() {
  lgfx::touch_point_t tp;
  return s_gfx->getTouch(&tp, 1) > 0;
}

static void swallowTouch() { s_swallow = true; }

// ── Power ────────────────────────────────────────────────────────────────────
// Screen off: the clock down and the touch panel asleep unless a tap is to
// wake the screen. Screen on: back.
static bool s_touch_asleep = false;
static void powerSave(bool on, bool keep_touch) {
  cpuSlow(on);
  if (on && !keep_touch) { board.touchSleep(); s_touch_asleep = true; }
  else if (!on && s_touch_asleep) { board.touchWake(); s_touch_asleep = false; }
}
// Nothing due for `ms`: the loop task blocks and the core idles, clock-gated,
// instead of spinning -- woken early by a frame from the app (below).
static TaskHandle_t s_loop_task = nullptr;
static void idle(uint32_t ms) {
  if (!ms) return;
  if (!s_loop_task) s_loop_task = xTaskGetCurrentTaskHandle();
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms));
}

// Backlight 1-100 % (the brightness slider), on the LP5814 PWM.
static void setBacklightPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  s_gfx->setBrightness((uint8_t)(8 + (uint16_t)pct * 247 / 100));   // never fully dark
}

// microSD over SDMMC, 1-bit (CLK 2, CMD 3, D0 1); its power rail (expander
// P14) is switched on in WioTrackerL2Board::begin(). Retried until it works --
// at most every 5 s, a failed mount takes a while -- so a card inserted later
// is picked up.
static bool s_sd_mounted = false;
static uint32_t s_sd_last_try = 0;
static bool mountStorage() {
  if (s_sd_mounted) return true;
  if (s_sd_last_try && millis() - s_sd_last_try < 5000) return false;
  s_sd_last_try = millis() | 1;
  SD_MMC.setPins(2, 3, 1);
  s_sd_mounted = SD_MMC.begin("/sdcard", true /* 1-bit */);
  return s_sd_mounted;
}
// Settings > Storage > Format card: one FAT32 volume over the whole card
// (16 KB clusters -- map tiles are small), readable or not. Blocks for a few
// seconds to tens of seconds (the FATs of a big card). The caller restarts
// afterwards: map, history and tile caches all hold state read off the card.
static bool formatCard() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  SD_MMC.end();
  s_sd_mounted = false;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = GPIO_NUM_2;
  slot.cmd = GPIO_NUM_3;
  slot.d0 = GPIO_NUM_1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  esp_vfs_fat_mount_config_t mc = {};
  mc.format_if_mount_failed = true;   // an unreadable card: formatted right here
  mc.max_files = 4;
  mc.allocation_unit_size = 16 * 1024;
  sdmmc_card_t* card = nullptr;
  esp_err_t e = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &mc, &card);
  if (e == ESP_OK) {
    e = esp_vfs_fat_sdcard_format("/sdcard", card);
    esp_vfs_fat_sdcard_unmount("/sdcard", card);
  }
  s_sd_last_try = 0;
  mountStorage();
  return e == ESP_OK;
#else
  return false;   // IDF 4.4 can't format a mounted card: a computer can
#endif
}
static void restart() { ESP.restart(); }

// ── The card as a USB drive ──────────────────────────────────────────────────
// A computer (not a charger: USB enumerated the device) is offered the card;
// taken, the firmware lets go of it (the filesystem unmounted, the card kept
// initialised) and the computer reads and writes its sectors directly. Given
// back -- ejected on the computer, or the cable out -- the device restarts:
// what it keeps from the card (map index, history, tile caches) is reloaded.
#ifdef LVPORT_USB_DRIVE
static USBMSC s_msc;   // global: its constructor adds the interface before USB starts
static sdmmc_card_t s_msc_card;
static bool s_usb_drive = false;
static volatile bool s_usb_ejected = false;
static int32_t mscRead(uint32_t lba, uint32_t offset, void* buf, uint32_t n) {
  lba += offset / 512;
  return sdmmc_read_sectors(&s_msc_card, buf, lba, n / 512) == ESP_OK ? (int32_t)n : -1;
}
static int32_t mscWrite(uint32_t lba, uint32_t offset, uint8_t* buf, uint32_t n) {
  lba += offset / 512;
  return sdmmc_write_sectors(&s_msc_card, buf, lba, n / 512) == ESP_OK ? (int32_t)n : -1;
}
static bool mscStartStop(uint8_t power, bool start, bool eject) {
  (void)power;
  if (eject && !start) s_usb_ejected = true;
  return true;
}
// A computer on the other end: enumerated and the bus not suspended. On
// battery the chip can't see the cable go (no VBUS sense), but the bus falls
// idle -- a suspend -- within milliseconds; plugged back in, the computer
// resets and enumerates it again. (A computer that sleeps suspends it too.)
static volatile bool s_usb_suspended = false;
static void usbEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (id == ARDUINO_USB_SUSPEND_EVENT) s_usb_suspended = true;
  else if (id == ARDUINO_USB_RESUME_EVENT || id == ARDUINO_USB_STARTED_EVENT) s_usb_suspended = false;
}
static bool usbHost() {
  static bool hooked = false;
  if (!hooked) { hooked = true; USB.onEvent(usbEvent); }
  return (bool)USB && !s_usb_suspended;
}
static bool usbDriveOn() { return s_usb_drive; }
static bool usbDriveStart() {
  if (s_usb_drive) return true;
  SD_MMC.end();
  s_sd_mounted = false;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.clk = GPIO_NUM_2;
  slot.cmd = GPIO_NUM_3;
  slot.d0 = GPIO_NUM_1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  bool ok = sdmmc_host_init() == ESP_OK;
  if (ok && (sdmmc_host_init_slot(host.slot, &slot) != ESP_OK || sdmmc_card_init(&host, &s_msc_card) != ESP_OK)) {
    sdmmc_host_deinit();
    ok = false;
  }
  if (!ok) { s_sd_last_try = 0; mountStorage(); return false; }
  s_msc.vendorID("MeshCore");
  s_msc.productID("Wio L2 SD card");
  s_msc.productRevision("1.0");
  s_msc.onRead(mscRead);
  s_msc.onWrite(mscWrite);
  s_msc.onStartStop(mscStartStop);
  s_msc.isWritable(true);
  s_msc.begin(s_msc_card.csd.capacity, s_msc_card.csd.sector_size);
  s_usb_ejected = false;
  s_usb_drive = true;
  s_msc.mediaPresent(true);
  return true;
}
// Ejected on the computer, or the cable out: time to give the card back.
static bool usbDriveDone() { return s_usb_drive && (s_usb_ejected || !usbHost()); }
static void usbDriveStop() {
  if (!s_usb_drive) return;
  s_msc.mediaPresent(false);   // a transfer under way still finishes
  delay(100);
  sdmmc_host_deinit();
  s_usb_drive = false;
}
#else
static bool usbHost() { return false; }
static bool usbDriveOn() { return false; }
static bool usbDriveStart() { return false; }
static bool usbDriveDone() { return false; }
static void usbDriveStop() {}
#endif

// ── Settings kept in NVS ─────────────────────────────────────────────────────
// Not in NodePrefs (whose on-flash layout stays fixed) and not on the
// removable card. One value per call: the namespace is opened and closed
// around it; a namespace that won't open reads as the default.
namespace nvs {
struct Ns {
  Preferences p;
  bool ok;
  Ns(const char* ns, bool ro) { ok = p.begin(ns, ro); }
  ~Ns() { if (ok) p.end(); }
};
static bool getBool(const char* ns, const char* key, bool def) { Ns n(ns, true); return n.ok ? n.p.getBool(key, def) : def; }
static void putBool(const char* ns, const char* key, bool v) { Ns n(ns, false); if (n.ok) n.p.putBool(key, v); }
static int  getI8(const char* ns, const char* key, int def) { Ns n(ns, true); return n.ok ? n.p.getChar(key, (int8_t)def) : def; }
static void putI8(const char* ns, const char* key, int v) { Ns n(ns, false); if (n.ok) n.p.putChar(key, (int8_t)v); }
static int  getU8(const char* ns, const char* key, int def) { Ns n(ns, true); return n.ok ? n.p.getUChar(key, (uint8_t)def) : def; }
static void putU8(const char* ns, const char* key, int v) { Ns n(ns, false); if (n.ok) n.p.putUChar(key, (uint8_t)v); }
static void getStr(const char* ns, const char* key, char* out, size_t n_out) {
  out[0] = '\0';
  Ns n(ns, true);
  if (n.ok) n.p.getString(key, out, n_out);
}
static void putStr(const char* ns, const char* key, const char* v) { Ns n(ns, false); if (n.ok) n.p.putString(key, v); }
static void remove(const char* ns, const char* key) { Ns n(ns, false); if (n.ok && n.p.isKey(key)) n.p.remove(key); }
}  // namespace nvs

// ── WiFi (station, only while a map download runs) ───────────────────────────
// The saved networks as "s0".."s7" / "p0".."p7"; the single network older
// builds kept ("ssid" / "pass") becomes the first.
static void wifiStoreLoad() {
  char ks[3] = "s0", kp[3] = "p0";
  s_net_count = 0;
  for (int i = 0; i < WIFI_SAVED_MAX; i++) {
    ks[1] = kp[1] = (char)('0' + i);
    nvs::getStr("mc_wifi", ks, s_nets[i].ssid, sizeof(s_nets[i].ssid));
    if (!s_nets[i].ssid[0]) break;
    nvs::getStr("mc_wifi", kp, s_nets[i].pass, sizeof(s_nets[i].pass));
    s_net_count++;
  }
  char old[33];
  nvs::getStr("mc_wifi", "ssid", old, sizeof(old));
  if (old[0]) {
    char pass[65];
    nvs::getStr("mc_wifi", "pass", pass, sizeof(pass));
    nvs::remove("mc_wifi", "ssid");
    nvs::remove("mc_wifi", "pass");
    if (wifiFind(old) < 0) saveWifi(old, pass);
  }
}
static void wifiStoreWrite() {
  char ks[3] = "s0", kp[3] = "p0";
  for (int i = 0; i < WIFI_SAVED_MAX; i++) {
    ks[1] = kp[1] = (char)('0' + i);
    if (i < s_net_count) {
      nvs::putStr("mc_wifi", ks, s_nets[i].ssid);
      nvs::putStr("mc_wifi", kp, s_nets[i].pass);
    } else {
      nvs::remove("mc_wifi", ks);
      nvs::remove("mc_wifi", kp);
    }
  }
}
// Filesystem size and space in use (Settings > Storage), plus the card's own
// size -- a card whose FAT partition is small (e.g. written by a Raspberry Pi
// imager) shows both. The first free-space count on a big card takes a moment.
static bool sdInfo(uint64_t& total, uint64_t& used, uint64_t& card) {
  if (!mountStorage()) return false;
  card = SD_MMC.cardSize();
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  uint64_t fs_free = 0;
  if (esp_vfs_fat_info("/sdcard", &total, &fs_free) == ESP_OK) {   // asks the mounted volume, not drive 0:
    used = total - fs_free;
    return total > 0;
  }
#endif
  total = SD_MMC.totalBytes();
  used = SD_MMC.usedBytes();
  return total > 0;
}
// Why the device last started (Diagnostics).
static const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:   return "Power on";
    case ESP_RST_SW:        return "Restart";
    case ESP_RST_PANIC:     return "Crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:       return "Watchdog";
    case ESP_RST_BROWNOUT:  return "Low voltage";
    case ESP_RST_DEEPSLEEP: return "Wake from sleep";
    case ESP_RST_EXT:       return "Reset pin";
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    case ESP_RST_USB:       return "USB";
#endif
    default:                return "Other";
  }
}
// The last crash the core dump partition holds: task and address (decode
// the address with the build's firmware.elf, or read the whole dump over USB).
static bool crashSummary(char* out, size_t n) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // Read once: checking the dump in flash takes ~45 ms, and it stays the same
  // until the next restart (Diagnostics asks every second).
  static int8_t s_have = -1;
  static char s_sum[24];
  if (s_have < 0) {
    esp_core_dump_summary_t sum;
    s_have = esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(&sum) == ESP_OK;
    if (s_have) snprintf(s_sum, sizeof(s_sum), "%.10s %08lx", sum.exc_task, (unsigned long)sum.exc_pc);
  }
  if (s_have) snprintf(out, n, "%s", s_sum);
  return s_have;
#else
  (void)out; (void)n;
  return false;
#endif
}
// The internal flash file system (contacts, channels, settings, trail...).
#ifdef ESP32_LITTLEFS
static const char* const FLASH_ROOT = "/littlefs";
#else
static const char* const FLASH_ROOT = "/spiffs";
#endif
static bool flashInfo(uint64_t& total, uint64_t& used) {
  total = ESP32_FS.totalBytes();
  used = ESP32_FS.usedBytes();
  return total > 0;
}

// Joins the strongest saved network in range: with more than one saved, a
// scan first (a few seconds, inside the callers' connect timeouts). The given
// network is joined when none of them shows up -- a hidden one, say -- and
// straight away when it's the only one.
static bool s_net_scanning = false;
static char s_fb_ssid[33], s_fb_pass[65];
static void netBegin(const char* ssid, const char* pass) {
  WiFi.mode(WIFI_STA);
  snprintf(s_fb_ssid, sizeof(s_fb_ssid), "%s", ssid);
  snprintf(s_fb_pass, sizeof(s_fb_pass), "%s", pass);
  if (wifiSavedCount() <= 1) { WiFi.begin(ssid, pass); return; }
  s_net_scanning = WiFi.scanNetworks(true /* async */) == WIFI_SCAN_RUNNING;
  if (!s_net_scanning) WiFi.begin(ssid, pass);
}
// Just this network, no scan (Settings > WiFi checking a password it saved).
static void netJoin(const char* ssid, const char* pass) {
  s_net_scanning = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
}
// After NET_FAILED / a timeout: true when the network wasn't there at all
// (else it answered and turned us away: the password, most likely).
static bool netNotFound() { return WiFi.status() == WL_NO_SSID_AVAIL; }
static int netState() {
  if (s_net_scanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return NET_CONNECTING;
    s_net_scanning = false;
    const WifiNet* best = nullptr;
    int best_rssi = -1000;
    for (int i = 0; i < n; i++) {
      int k = wifiFind(WiFi.SSID(i).c_str());
      if (k >= 0 && WiFi.RSSI(i) > best_rssi) { best = &s_nets[k]; best_rssi = WiFi.RSSI(i); }
    }
    WiFi.scanDelete();
    if (best) WiFi.begin(best->ssid, best->pass);
    else WiFi.begin(s_fb_ssid, s_fb_pass);
    return NET_CONNECTING;
  }
  switch (WiFi.status()) {
    case WL_CONNECTED:      return NET_UP;
    case WL_CONNECT_FAILED:
    case WL_NO_SSID_AVAIL:  return NET_FAILED;
    case WL_IDLE_STATUS:
    case WL_DISCONNECTED:   return NET_CONNECTING;
    default:                return NET_CONNECTING;
  }
}
static void fetchFreeBuffer();
static void netEnd() {
  if (s_net_scanning) { s_net_scanning = false; WiFi.scanDelete(); }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  fetchFreeBuffer();
}
// The network joined (Settings > WiFi marks it), "" when not connected.
static void netSsid(char* out, size_t n) {
  snprintf(out, n, "%s", WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "");
}
// For the status bar: NET_UP once connected, else NET_OFF (radio off, a scan,
// still connecting).
static int netRadio() { return WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED ? NET_UP : NET_OFF; }
// Async scan: scanStart(), then scanResults() returns -1 while running, else
// the count, filling `names` (strongest first, as the driver reports them).
static void scanStart() {
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true /* async */);
}
static int scanResults(char names[][33], int max) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return -1;
  if (n < 0) return 0;
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) if (strcmp(names[j], s.c_str()) == 0) { dup = true; break; }
    if (dup) continue;
    snprintf(names[k++], 33, "%s", s.c_str());
  }
  WiFi.scanDelete();
  return k;
}

// ── One HTTP GET at a time, on a worker task (core 0) ────────────────────────
// TLS handshakes and slow servers would stall the mesh loop, so the request
// runs on its own task; the UI loop polls. The HTTP connection is reused
// between tiles (keep-alive). Tiles are public pictures, so the TLS peer
// isn't verified (no CA bundle in flash for this). The task runs at the idle
// priority: HTTPClient's read loop only yields with delay(0), which never lets
// a lower-priority task in, so at priority 1 a slow transfer starved IDLE0
// until the task watchdog reset the device.
enum { F_IDLE, F_REQUESTED, F_BUSY, F_DONE, F_FAILED };
static volatile int s_fstate = F_IDLE;
static volatile int s_fcode = 0;
static char     s_furl[200];
static uint8_t* s_fbuf = nullptr;
static size_t   s_fcap = 0;
static volatile size_t s_flen = 0;
static volatile bool s_fabandoned = false;
static TaskHandle_t s_ftask = nullptr;

// Stream sink for HTTPClient::writeToStream() (which also undoes chunking).
class FetchSink : public Stream {
public:
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* d, size_t n) override {
    if (s_flen + n > s_fcap) {
      size_t cap = s_fcap ? s_fcap : 32 * 1024;
      while (cap < s_flen + n) cap *= 2;
      if (cap > 512 * 1024) return 0;
      uint8_t* nb = (uint8_t*)heap_caps_realloc(s_fbuf, cap, MALLOC_CAP_SPIRAM);
      if (!nb) return 0;
      s_fbuf = nb; s_fcap = cap;
    }
    memcpy(s_fbuf + s_flen, d, n);
    s_flen += n;
    return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

static char s_ferr[64] = "";   // why the last request failed (HTTP / TLS), for the popup

// The framework builds mbedTLS with internal-RAM-only buffers (~40 KB per
// session), which Bluedroid + WiFi + LVGL leave too little of: put them in
// PSRAM, falling back to internal RAM.
static void* tlsCalloc(size_t n, size_t size) {
  return heap_caps_calloc_prefer(n, size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
}

static void fetchTask(void*) {
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
  WiFiClientSecure tls;
  tls.setInsecure();
  tls.setHandshakeTimeout(20);   // seconds (default 120)
  WiFiClient plain;
  HTTPClient http;
  http.setReuse(true);
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setUserAgent("MeshCore-Solo/1.0 (LoRa handheld; offline map download)");
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_fstate = F_BUSY;
    s_flen = 0;
    bool https = strncmp(s_furl, "https:", 6) == 0;
    int code = -1;
    s_ferr[0] = '\0';
    if (https ? http.begin(tls, s_furl) : http.begin(plain, s_furl)) {
      code = http.GET();
      if (code == 200) {
        FetchSink sink;
        if (http.writeToStream(&sink) < 0) { code = -1; snprintf(s_ferr, sizeof(s_ferr), "read failed / out of memory"); }
      } else if (code < 0) {
        char tls_err[40] = "";
        if (https && tls.lastError(tls_err, sizeof(tls_err)) != 0 && tls_err[0])
          snprintf(s_ferr, sizeof(s_ferr), "TLS: %s", tls_err);
        else
          snprintf(s_ferr, sizeof(s_ferr), "%s", HTTPClient::errorToString(code).c_str());
      } else {
        snprintf(s_ferr, sizeof(s_ferr), "HTTP %d", code);
      }
      http.end();
    } else {
      snprintf(s_ferr, sizeof(s_ferr), "bad URL");
    }
    s_fcode = code;
    if (s_fabandoned) { s_fabandoned = false; s_fstate = F_IDLE; }   // nobody wants it any more
    else s_fstate = code == 200 ? F_DONE : F_FAILED;
  }
}

// 1 = started, 0 = busy (an abandoned request is still finishing), -1 = no
// memory for the worker task.
static int fetchStart(const char* url) {
  if (s_fstate != F_IDLE) return 0;
  if (!s_ftask && xTaskCreatePinnedToCore(fetchTask, "tilefetch", 10240, nullptr, tskIDLE_PRIORITY, &s_ftask, 0) != pdPASS) {
    s_ftask = nullptr;
    return -1;
  }
  snprintf(s_furl, sizeof(s_furl), "%s", url);
  s_fstate = F_REQUESTED;
  xTaskNotifyGive(s_ftask);
  return 1;
}
// 0 = in flight, 1 = done (fetchData valid until fetchRelease), <0 = failed (-HTTP status)
static int fetchPoll() {
  int st = s_fstate;
  if (st == F_DONE) return 1;
  if (st == F_FAILED) return s_fcode > 0 ? -s_fcode : -1;
  return 0;
}
static const uint8_t* fetchData(size_t& len) { len = s_flen; return s_fbuf; }
static void fetchRelease() { if (s_fstate == F_DONE || s_fstate == F_FAILED) s_fstate = F_IDLE; }
// Stop caring about the request in flight; it completes (or fails) on its own.
static void fetchAbandon() {
  if (s_fstate == F_REQUESTED || s_fstate == F_BUSY) s_fabandoned = true;
  else fetchRelease();
}
static const char* fetchError() { return s_ferr; }
// WiFi off: the response buffer (grown to the biggest tile or update chunk,
// up to 512 KB of PSRAM) given back, unless a request is still finishing.
static void fetchFreeBuffer() {
  if (s_fstate != F_IDLE) return;
  heap_caps_free(s_fbuf);
  s_fbuf = nullptr;
  s_fcap = s_flen = 0;
}

#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// Browser simulator (variants/sim/build_wasm_lvgl.sh): SimLcdDisplay blits to
// a <canvas>, the host page feeds the mouse in as touch.

// The device's NVS (Preferences) as a table for the session: the UI settings
// below read and write it by the same names.
namespace nvs {
struct Kv { char ns[10]; char key[10]; char val[40]; };
static Kv s_kv[24];
static int s_kv_n = 0;
static Kv* find(const char* ns, const char* key, bool add) {
  for (int i = 0; i < s_kv_n; i++) if (!strcmp(s_kv[i].ns, ns) && !strcmp(s_kv[i].key, key)) return &s_kv[i];
  if (!add || s_kv_n >= (int)(sizeof(s_kv) / sizeof(s_kv[0]))) return nullptr;
  Kv* k = &s_kv[s_kv_n++];
  snprintf(k->ns, sizeof(k->ns), "%s", ns);
  snprintf(k->key, sizeof(k->key), "%s", key);
  return k;
}
static int getInt(const char* ns, const char* key, int def) { Kv* k = find(ns, key, false); return k ? atoi(k->val) : def; }
static void putInt(const char* ns, const char* key, int v) { if (Kv* k = find(ns, key, true)) snprintf(k->val, sizeof(k->val), "%d", v); }
static bool getBool(const char* ns, const char* key, bool def) { return getInt(ns, key, def) != 0; }
static void putBool(const char* ns, const char* key, bool v) { putInt(ns, key, v); }
static int  getI8(const char* ns, const char* key, int def) { return getInt(ns, key, def); }
static void putI8(const char* ns, const char* key, int v) { putInt(ns, key, v); }
static int  getU8(const char* ns, const char* key, int def) { return getInt(ns, key, def); }
static void putU8(const char* ns, const char* key, int v) { putInt(ns, key, v); }
static void getStr(const char* ns, const char* key, char* out, size_t n) { Kv* k = find(ns, key, false); snprintf(out, n, "%s", k ? k->val : ""); }
static void putStr(const char* ns, const char* key, const char* v) { if (Kv* k = find(ns, key, true)) snprintf(k->val, sizeof(k->val), "%s", v); }
}  // namespace nvs

static bool s_swallow = false;

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
  shotCopy(area, px_map);
  display.blit(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1,
               (const uint16_t*)px_map);
  lv_display_flush_ready(disp);
}

static void touchCb(lv_indev_t* indev, lv_indev_data_t* data) {
  (void)indev;
  const SimLcdDisplay::Touch& t = SimLcdDisplay::touchState();
  if (s_swallow) {
    if (!t.down) s_swallow = false;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  data->state = t.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->point.x = t.x;
  data->point.y = t.y;
}

static bool begin() {
  lv_display_t* disp = lv_display_create(SimLcdDisplay::W, SimLcdDisplay::H);
  lv_display_set_flush_cb(disp, flushCb);
  static uint8_t buf[SimLcdDisplay::W * 40 * 2];   // 40 lines (the board: two half screens)
  lv_display_set_buffers(disp, buf, nullptr, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchCb);
  return true;
}

static bool touched() { return SimLcdDisplay::touchState().down; }
static void swallowTouch() { s_swallow = true; }
static void powerSave(bool, bool) {}   // the browser's page loop runs the sim
static void cpuSlow(bool) {}
static void idle(uint32_t) {}
static void setBacklightPct(uint8_t pct) { (void)pct; }   // the browser canvas has no backlight

// The host page preloads map tiles into the in-memory FS under /sdcard/maps.
static bool mountStorage() { return true; }
static void rmTree(const char* path) {
  if (DIR* d = opendir(path)) {
    while (struct dirent* de = readdir(d)) {
      if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
      char p[256];
      snprintf(p, sizeof(p), "%s/%s", path, de->d_name);
      struct stat st;
      if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) { rmTree(p); rmdir(p); }
      else remove(p);
    }
    closedir(d);
  }
}
static bool formatCard() { rmTree("/sdcard"); return true; }   // the MEMFS card: emptied
static bool usbHost() { return false; }   // no USB in a browser
static bool usbDriveOn() { return false; }
static bool usbDriveStart() { return false; }
static bool usbDriveDone() { return false; }
static void usbDriveStop() {}
static void restart() {}   // the page's Reset does that

// The browser is always online; saved WiFi networks only live for the session.
static void wifiStoreLoad() { s_net_count = 0; }
static void wifiStoreWrite() {}
// The browser has no card: a nominal 32 GB, used = what the files add up to
// (the storage screen counts them anyway; it passes that in).
static bool sdInfo(uint64_t& total, uint64_t& used, uint64_t& card) { total = card = 32ULL << 30; used = 0; return true; }
static const char* const FLASH_ROOT = "/sim_data";
static const char* resetReason() { return "Power on"; }
static bool crashSummary(char*, size_t) { return false; }
static bool flashInfo(uint64_t& total, uint64_t& used) { total = 1536ULL << 10; used = 0; return true; }
static bool s_net_on = false;
static void netBegin(const char*, const char*) { s_net_on = true; }
static void netJoin(const char*, const char*) { s_net_on = true; }
static bool netNotFound() { return false; }
static int  netState() { return NET_UP; }
static void netEnd() { s_net_on = false; }
static int  netRadio() { return s_net_on ? NET_UP : NET_OFF; }
static void netSsid(char* out, size_t n) { const WifiNet* w = wifiSaved(0); snprintf(out, n, "%s", s_net_on && w ? w->ssid : ""); }
static uint32_t s_scan_at = 0;
static void scanStart() { s_scan_at = millis(); }
static int scanResults(char names[][33], int max) {   // a pretend scan, for the UI
  if (millis() - s_scan_at < 800) return -1;
  static const char* const FAKE[] = { "Sim-Home", "Sim-Office", "Cafe Guest" };
  int n = 0;
  for (; n < 3 && n < max; n++) snprintf(names[n], 33, "%s", FAKE[n]);
  return n;
}

// fetch() through emscripten's Fetch API (the tile server must allow CORS,
// which OpenTopoMap does).
enum { F_IDLE, F_BUSY, F_DONE, F_FAILED };
static int      s_fstate = F_IDLE;
static int      s_fcode = 0;
static uint8_t* s_fbuf = nullptr;
static size_t   s_flen = 0;
static bool     s_fabandoned = false;

static void fetchDone(emscripten_fetch_t* f) {
  if (!s_fabandoned) {
    s_fcode = f->status;
    free(s_fbuf);
    s_fbuf = nullptr;
    s_flen = 0;
    if (f->status == 200 && f->numBytes > 0 && (s_fbuf = (uint8_t*)malloc(f->numBytes))) {
      memcpy(s_fbuf, f->data, f->numBytes);
      s_flen = f->numBytes;
      s_fstate = F_DONE;
    } else {
      s_fstate = F_FAILED;
    }
  } else {
    s_fstate = F_IDLE;
  }
  s_fabandoned = false;
  emscripten_fetch_close(f);
}

static int fetchStart(const char* url) {   // 1 started, 0 busy
  if (s_fstate != F_IDLE) return 0;
  emscripten_fetch_attr_t a;
  emscripten_fetch_attr_init(&a);
  strcpy(a.requestMethod, "GET");
  a.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
  a.onsuccess = fetchDone;
  a.onerror = fetchDone;
  s_fstate = F_BUSY;
  emscripten_fetch(&a, url);
  return 1;
}
static int fetchPoll() {
  if (s_fstate == F_DONE) return 1;
  if (s_fstate == F_FAILED) return s_fcode > 0 ? -s_fcode : -1;
  return 0;
}
static const uint8_t* fetchData(size_t& len) { len = s_flen; return s_fbuf; }
static void fetchRelease() { if (s_fstate == F_DONE || s_fstate == F_FAILED) s_fstate = F_IDLE; }
static void fetchAbandon() { if (s_fstate == F_BUSY) s_fabandoned = true; else fetchRelease(); }
static const char* fetchError() { return s_fcode > 0 && s_fcode != 200 ? "HTTP error" : s_fcode == 0 ? "network / CORS" : ""; }

#else
  #error "ui-lvgl: no LVGL port for this board (see LvglPort.h)"
#endif

// ── UI settings kept outside NodePrefs (nvs:: above: NVS, or the session) ──
// Settings > WiFi's switch: off keeps the radio off for everything (scan, map
// download). Kept with the credentials.
static int8_t s_wifi_allowed = -1;   // read once (the status bar asks every second)
static bool wifiAllowed() {
  if (s_wifi_allowed < 0) s_wifi_allowed = nvs::getBool("mc_wifi", "on", true);
  return s_wifi_allowed;
}
static void setWifiAllowed(bool on) { s_wifi_allowed = on; nvs::putBool("mc_wifi", "on", on); }
// Map tools > Live tiles: missing tiles fetched over WiFi while the map is open.
static bool liveTiles() { return nvs::getBool("mc_wifi", "live", true); }
static void setLiveTiles(bool on) { nvs::putBool("mc_wifi", "live", on); }
// Map tools > Hiking trails: the Waymarked Trails overlay over the map.
static bool trailsOn() { return nvs::getBool("mc_ui", "trails", false); }
static void setTrailsOn(bool on) { nvs::putBool("mc_ui", "trails", on); }
// Map tools > Vector map (test). Unfinished: on only in a -D MAP_VECTOR build,
// off (and its switch gone) otherwise, whatever was saved.
#ifdef MAP_VECTOR
static bool vectorOn() { return nvs::getBool("mc_ui", "vector", false); }
#else
static bool vectorOn() { return false; }
#endif
static void setVectorOn(bool on) { nvs::putBool("mc_ui", "vector", on); }

// Screen-lock PIN (Settings > Display > Screen PIN): digits, "" = none.

// Accent colour (Settings > Display): an index into theme::ACCENTS.
static int loadAccent() { return nvs::getU8("mc_ui", "accent", 0); }
static void saveAccent(int idx) { nvs::putU8("mc_ui", "accent", idx); }
// Settings > Storage > Kept per conversation (an index into histstore::KEEP).
static int loadHistKeep() { return nvs::getI8("mc_ui", "hkeep", -1); }
static void saveHistKeep(int idx) { nvs::putI8("mc_ui", "hkeep", idx); }
// Settings > Storage > Live map tiles: index into mapview::LIVE_CAP_MB, -1 = default.
static int loadLiveCap() { return nvs::getI8("mc_ui", "ltcap", -1); }
static void saveLiveCap(int idx) { nvs::putI8("mc_ui", "ltcap", idx); }
// Settings > Display > Tap to wake: a touch turns the dark screen on
// (off: only the top button does).
static bool loadTapWake() { return nvs::getBool("mc_ui", "tapwake", true); }
static void saveTapWake(bool on) { nvs::putBool("mc_ui", "tapwake", on); }
// Messages: which sections are folded (bit per section).
static int loadChatFold() { return nvs::getU8("mc_ui", "chfold", 0); }
static void saveChatFold(int bits) { nvs::putU8("mc_ui", "chfold", bits); }
// Home's apps: their letters in the user's order, a hidden one in lower case.
static void loadHomeApps(char* out, size_t n) { nvs::getStr("mc_ui", "apps", out, n); }
static void saveHomeApps(const char* v) { nvs::putStr("mc_ui", "apps", v); }

}  // namespace lvport

#if defined(SEEED_WIO_TRACKER_L2)
// A frame from the app (SerialBLEInterface, the BLE stack's task): the loop,
// blocked in lvport::idle(), reads it now rather than at the next timeout.
void serialFrameArrived() {
  if (TaskHandle_t t = lvport::s_loop_task) xTaskNotifyGive(t);
}
#endif
