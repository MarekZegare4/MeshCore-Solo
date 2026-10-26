#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// Phase 3: true only once setup() has fully finished (set at the very end
// of setup(), below) -- lets a JS test harness poll "has this instance
// actually booted" instead of guessing a fixed delay after the MODULARIZE
// factory promise resolves, which resolves once the wasm module is
// instantiated, well before sim_idbfs_ready()'s async IDBFS callback ever
// invokes setup() (see variants/sim/sim_main.cpp). Calling any of the other
// sim_test_*() hooks below before this is true would run against a
// the_mesh that exists (global C++ construction already ran) but hasn't
// had begin()/an identity loaded yet.
static bool g_sim_ready = false;
#endif

#ifdef DISPLAY_HAS_BUSY_PUMP
// Run from the e-ink driver's BUSY-pin wait (DisplayDriver::setBusyPumpFn(),
// wired in setup() below) so packets landing during a refresh are pulled off
// the radio as they arrive, and a TX that finishes mid-refresh goes straight
// back to listening -- see RadioLibWrapper::pumpRecvDuringBlockingWait().
static void pumpRadioDuringDisplayBusyWait(void*) {
  radio_driver.pumpRecvDuringBlockingWait();
}
#endif

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

// platform file system
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
    #if defined(EXTRAFS)
      #include <CustomLFS.h>
      CustomLFS ExtraFS(0xD4000, 0x19000, 128);
      DataStore store(InternalFS, ExtraFS, rtc_clock);
    #else
      DataStore store(InternalFS, rtc_clock);
    #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <helpers/esp32/InternalFS.h>
  DataStore store(ESP32_FS, rtc_clock);
#elif defined(SIM_PLATFORM)
  #include <SimFS.h>
  // Real files under ./sim_data/ (relative to the process's cwd) so
  // NodePrefs/contacts/identity genuinely round-trip across process
  // restarts -- see SimFS.h.
  SimFS sim_fs("./sim_data");
  DataStore store(sim_fs, rtc_clock);
#endif

// The companion app's links (upstream's interface manager): Bluetooth, USB,
// WiFi, Ethernet, a UART, whichever the build has, all at once. DUAL_SERIAL
// (Solo's builds) is Bluetooth and USB.
#if defined(DUAL_SERIAL) && !defined(ENABLE_USB_INTERFACE)
  #define ENABLE_USB_INTERFACE
#endif
#if !defined(BLE_PIN_CODE) && !defined(ENABLE_WIFI_INTERFACE) && !defined(ETHERNET_ENABLED) \
    && !defined(SERIAL_RX) && !defined(ENABLE_USB_INTERFACE) && !defined(SIM_PLATFORM)
  #define ENABLE_USB_INTERFACE   // no other link: the app on USB, as before
#endif
#include <helpers/MultiSerialInterface.h>
MultiSerialInterface interface_manager;

// include bluetooth interface
#if defined(BLE_PIN_CODE)
  #ifdef ESP32
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(NRF52_PLATFORM)
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #elif defined(RP2040_PLATFORM)
    #include <helpers/rp2040/SerialBLEInterface.h>
    SerialBLEInterface bluetooth_interface;
  #else
    #error "SerialBLEInterface is not defined for this platform"
  #endif
#endif

// include wifi interface
#ifdef ENABLE_WIFI_INTERFACE
  #ifndef WIFI_SSID
    #define WIFI_SSID ""
  #endif
  #ifndef WIFI_PWD
    #define WIFI_PWD ""
  #endif
  #ifndef TCP_PORT
    #define TCP_PORT 5000
  #endif
  #ifndef WIFI_RETRY_INTERVAL
    #if defined(RP2040_PLATFORM)
      #define WIFI_RETRY_INTERVAL 30000   // each attempt blocks loop(), so retry less often
    #else
      #define WIFI_RETRY_INTERVAL 10000   // millis between reconnect attempts
    #endif
  #endif
  #ifndef WIFI_RETRY_TIMEOUT
    #define WIFI_RETRY_TIMEOUT 5000     // RP2040: cap on how long one join may block loop()
  #endif
  #if defined(ESP32) || defined(RP2040_PLATFORM)
    #include <helpers/wifi/SerialWifiInterface.h>
    SerialWifiInterface wifi_interface;
  #else
    #error "SerialWifiInterface is not defined for this platform"
  #endif
#endif

// include usb interface
#if defined(ENABLE_USB_INTERFACE)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface usb_serial_interface;
#endif

// include ethernet interface
#if defined(ETHERNET_ENABLED)
  #include <helpers/ethernet/EthernetInterface.h>
  ETHERNET_CLASS ethernet_interface;
#endif

// include hardware serial interface
#if defined(SERIAL_RX)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface hardware_serial_interface;
  HardwareSerial companion_serial(1);
#endif

#if defined(SIM_PLATFORM)
  // No real BLE/USB companion-app transport -- always reports "not
  // connected". See variants/sim/SimSerialInterface.h.
  #include <SimSerialInterface.h>
  SimSerialInterface sim_serial_interface;
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  UITask ui_task(&board, &interface_manager);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
#if defined(MESH_IN_PSRAM) && defined(ESP32)
// ~120 KB of contacts/channels/queues: on PSRAM boards keep it out of internal
// RAM, which WiFi + TLS + BLE need (PSRAM is on the heap before global
// constructors run; internal RAM is the fallback).
#include <new>
#include <esp_heap_caps.h>
static MyMesh* newMesh() {
  void* p = heap_caps_malloc(sizeof(MyMesh), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = malloc(sizeof(MyMesh));
  return new (p) MyMesh(radio_driver, fast_rng, rtc_clock, tables, store);
}
MyMesh& the_mesh = *newMesh();
#else
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);
#endif

/* END GLOBAL OBJECTS */

void halt() {
  while (1) ;
}

// Session BLE PIN, resolved after the_mesh.begin() has loaded prefs and before
// the serial interface starts advertising with it. A saved PIN wins; otherwise
// the default 123456 becomes a random per-session PIN when there's a display to
// show it on (moved here from MyMesh::begin(), as upstream did -- MyMesh has no
// display concept).
static void resolveBLEPin(bool has_display) {
#ifdef BLE_PIN_CODE // 123456 by default
  if (the_mesh.getNodePrefs()->ble_pin == 0) {
    if (has_display && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      the_mesh.setBLEPin(rng.nextInt(100000, 999999)); // random pin each session
    } else {
      the_mesh.setBLEPin(BLE_PIN_CODE); // otherwise static pin
    }
  } else {
    the_mesh.setBLEPin(the_mesh.getNodePrefs()->ble_pin);
  }
#else
  (void)has_display;
  the_mesh.setBLEPin(0);
#endif
}

/* WIFI RECONNECT TRACKERS */
#ifdef ENABLE_WIFI_INTERFACE
  bool wifi_needs_reconnect = false;
  unsigned long last_wifi_reconnect_attempt = 0;
  char wifi_ssid[33] = WIFI_SSID;   // replaced by stored prefs at boot, if set
  char wifi_pwd[64] = WIFI_PWD;
  bool wifi_was_connected = false;
  bool wifi_enabled = false;   // set at boot from prefs; false also when the effective SSID is blank
#endif

void setup() {
  Serial.begin(115200);
  board.begin();

#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.begin();
#endif

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  if (display.begin()) {
    disp = &display;
  #if defined(SEEED_WIO_TRACKER_L2)
    // L2: a plain black frame -- the panel's backlight comes on with it (WioTrackerL2Display
    // ::endFrame()). The default colours here (white background, black dots) came out as a
    // bright bar across the middle of the dark screen. LVGL's splash follows within a second.
    disp->startFrame(0x0000);
    disp->endFrame();
  #else
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    // Three dots mid-screen: no words, so no orientation (the settings aren't read yet).
    const int dot = disp->width() >= 240 ? 6 : 3;
    disp->setColor(UIColor::primary_txt);
    for (int i = -1; i <= 1; i++)
      disp->fillRect(disp->width() / 2 + i * 3 * dot - dot / 2, disp->height() / 2 - dot / 2, dot, dot);
    disp->endFrame();
  #endif
  }
#endif

  if (!radio_init()) { halt(); }

  fast_rng.begin(radio_driver.getRngSeed());

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      // debug output might not be available at this point, might be too early. maybe should fall back to InternalFS here?
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  store.begin();
  the_mesh.begin();
  resolveBLEPin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  store.begin();
  the_mesh.begin();
  resolveBLEPin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#elif defined(ESP32)
  ESP32_FS.begin(true);
  store.begin();
  the_mesh.begin();
  resolveBLEPin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );

#elif defined(SIM_PLATFORM)
  // sim_fs already exists/mkdir'd itself in its constructor above.
  store.begin();
  the_mesh.begin();
  resolveBLEPin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
#else
  #error "need to define filesystem"
#endif

// add bluetooth interface
#if defined(BLE_PIN_CODE)
  bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
#endif

// add wifi interface
#ifdef ENABLE_WIFI_INTERFACE
  // use wifi ssid and password from prefs if ssid is not empty, otherwise use the build flag defaults
  if (the_mesh.getNodePrefs()->wifi_ssid[0]) {
    strcpy(wifi_ssid, the_mesh.getNodePrefs()->wifi_ssid);
    strcpy(wifi_pwd, the_mesh.getNodePrefs()->wifi_pwd);
  }
  // only start wifi if enabled and ssid is not empty
  wifi_enabled = the_mesh.getNodePrefs()->wifi_enabled;
  if (wifi_enabled && wifi_ssid[0]) {
#if defined(ESP32)
    board.setInhibitSleep(true);   // prevent sleep when WiFi is active
    WiFi.setAutoReconnect(true);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
        if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            WIFI_DEBUG_PRINTLN("WiFi disconnected (reason=%s). Flagging for reconnect...",
              WiFi.disconnectReasonName((wifi_err_reason_t)info.wifi_sta_disconnected.reason));
            wifi_needs_reconnect = true;
        } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            WIFI_DEBUG_PRINTLN("WiFi connected successfully!");
            wifi_needs_reconnect = false;
        }
    });
#endif

    WIFI_DEBUG_PRINTLN("connecting to %s", wifi_ssid);

#if defined(RP2040_PLATFORM)
    // the join itself blocks inside the core (CYW43::begin busy-waits for the
    // association), so every attempt stalls the mesh loop. beginNoBlock() only skips the
    // extra DHCP wait. Give the first connect a full window, then bound the retries below.
    WiFi.beginNoBlock(wifi_ssid, wifi_pwd);
    last_wifi_reconnect_attempt = millis();   // let DHCP finish before the poll can retry
#else
    WiFi.begin(wifi_ssid, wifi_pwd);
#endif
    wifi_interface.begin(TCP_PORT);
    interface_manager.addInterface(InterfaceType::WiFi, &wifi_interface);
  } else {
    WIFI_DEBUG_PRINTLN("wifi disabled");
  }
#endif

// add usb interface
#if defined(ENABLE_USB_INTERFACE)
  usb_serial_interface.begin(Serial);
  interface_manager.addInterface(InterfaceType::USB, &usb_serial_interface);
#endif

// add ethernet interface
#if defined(ETHERNET_ENABLED)
  ethernet_interface.begin();
  interface_manager.addInterface(InterfaceType::Ethernet, &ethernet_interface);
#endif

// add hardware serial interface
#if defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  hardware_serial_interface.begin(companion_serial);
  interface_manager.addInterface(InterfaceType::HardwareSerial, &hardware_serial_interface);
#endif

#if defined(SIM_PLATFORM)
  sim_serial_interface.begin();
  interface_manager.addInterface(InterfaceType::USB, &sim_serial_interface);
#endif

  the_mesh.startInterface(interface_manager);
  // Bluetooth as the user left it (Settings, the quick panel): off stays off
  // after a restart; the other links stay on.
  if (the_mesh.getNodePrefs()->ble_off) interface_manager.disableBluetooth();

  store.restoreRTCTime();
  sensors.begin();

#if ENV_INCLUDE_GPS == 1
  the_mesh.applyGpsPrefs();
#endif

#ifdef DISPLAY_CLASS
  // Apply saved brightness as soon as prefs are available so the tail of
  // the loading screen is not stuck at full brightness.
  if (disp && the_mesh.getNodePrefs())
    disp->setBrightness(the_mesh.getNodePrefs()->display_brightness);
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());  // still want to pass this in as dependency, as prefs might be moved
  the_mesh.setListener(ui_task.meshListener());
#ifdef DISPLAY_HAS_BUSY_PUMP
  if (disp) disp->setBusyPumpFn(pumpRadioDuringDisplayBusyWait, nullptr);
#endif
#endif

#ifdef NRF52_PLATFORM
  NRF_WDT->CONFIG      = 0x01;        // run during sleep; pause during debug halt
  NRF_WDT->CRV         = 32768*30-1;  // 30 second timeout
  NRF_WDT->RREN        = 0x01;        // enable reload register 0
  NRF_WDT->TASKS_START = 1;
#endif
  board.onBootComplete();
#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  g_sim_ready = true;
#endif
}

void loop() {
#ifdef NRF52_PLATFORM
  NRF_WDT->RR[0] = 0x6E524635UL;  // pet watchdog
#endif
  the_mesh.loop();
  interface_manager.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
  board.loop();
#ifdef HAS_EXTERNAL_WATCHDOG
  external_watchdog.loop();
#endif

  // CPU sleeps until next interrupt (radio, timer, BLE) — but only when the
  // mesh has no pending work, so queued TX/processing isn't delayed.
  if (!the_mesh.hasPendingWork()) {
#if defined(NRF52_PLATFORM)
    board.sleep(0); // nrf ignores seconds param, sleeps whenever possible
#elif defined(ESP32) && !defined(UI_LOOP_IDLES)
    // Arduino's loop task never yields: without this it spins the CPU flat
    // out. One tick lets the idle task halt the core (WAITI) until the next
    // interrupt or tick -- radio, buttons and serial still get a pass a ms.
    vTaskDelay(1);
#endif
  }

#ifdef ENABLE_WIFI_INTERFACE
  if (wifi_enabled) {
    // RP2040 has no WiFi event callbacks, so poll the link state instead
  #if defined(RP2040_PLATFORM)
      wifi_needs_reconnect = (WiFi.status() != WL_CONNECTED);
      if (wifi_was_connected == wifi_needs_reconnect) {   // link state changed
        wifi_was_connected = !wifi_needs_reconnect;
        if (wifi_was_connected) {
          WIFI_DEBUG_PRINTLN("connected, listening on %s:%d", WiFi.localIP().toString().c_str(), TCP_PORT);
        } else {
          WIFI_DEBUG_PRINTLN("link lost");
        }
      }
  #endif

    // Safely attempt to reconnect if flagged. On RP2040 each attempt blocks the mesh loop
    // for up to WIFI_RETRY_TIMEOUT, so retry less often and cap how long a join may stall.
    if (wifi_needs_reconnect && (millis() - last_wifi_reconnect_attempt > WIFI_RETRY_INTERVAL)) {
      WIFI_DEBUG_PRINTLN("Attempting manual WiFi reconnect to %s (status %d)...", wifi_ssid, WiFi.status());
    #if defined(RP2040_PLATFORM)
        WiFi.setTimeout(WIFI_RETRY_TIMEOUT);
        WiFi.beginNoBlock(wifi_ssid, wifi_pwd);   // no reconnect() on this platform
    #else
        WiFi.disconnect();
        WiFi.reconnect();
    #endif
      last_wifi_reconnect_attempt = millis();
    }
  }
#endif
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// Phase 3 sim test hooks -- a browser test harness (no real phone app, no
// on-device keyboard-driven compose flow in this sim yet) needs SOME way
// to trigger "send a flood advert" / "send a DM" from JS. Every one of
// these calls straight into the exact same real BaseChatMesh/MyMesh
// functions the real phone-app serial protocol (CMD_SEND_SELF_ADVERT,
// CMD_SEND_TXT_MSG in this same file) or the on-device UI compose flow
// (MessagesScreen::afterSend) already use -- real crypto, real routing,
// real contact table, nothing about the mesh/message logic is faked here,
// only "what UI gesture triggers it" is short-circuited. See the Phase 3
// report for why: scripting the on-device virtual keyboard widget
// key-by-key to compose free text was judged not worth the fragility for
// an automated test, versus this ~20-line, obviously-inert-on-real-hardware
// addition.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_is_ready() {
  return g_sim_ready ? 1 : 0;
}

// A host page's "Reset" control (meshcore-solo-site's RESET button,
// mesh.html's own Reset buttons) re-invokes the MODULARIZE factory
// function for the same simInstanceTag to simulate a real device restart
// -- board.reboot() is inert here (exit()s the whole wasm process, which
// under -sEXIT_RUNTIME=0 just freezes the tab). That leaves the OLD
// Module instance's own emscripten_set_main_loop() callback (see
// sim_main.cpp's sim_idbfs_ready()) still registered and still ticking
// forever afterwards -- nothing ever tore it down. Two real, user-visible
// consequences: it keeps re-drawing onto the same simInstanceTag-keyed
// <canvas> element the NEW instance is also drawing onto (visible as
// flicker/reversion once more than one reset has piled up orphaned
// instances), and if the old instance had been sitting on the Shutdown
// screen, HomeScreen::poll() keeps re-firing shutdown() -> turnOff() on
// every tick, permanently blacking that canvas out from under the new
// instance. A host page should call this on the OLD Module reference
// right before discarding it (i.e. right before re-invoking the
// MODULARIZE factory for that same tag) to actually stop it.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_stop_main_loop() {
  emscripten_cancel_main_loop();
}

extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_advert_flood() {
  if (!g_sim_ready) return 0;
  return the_mesh.advertFlood() ? 1 : 0;
}

// getContactByIdx(idx, ...) indexes DIRECTLY into the raw contacts[] array
// (src/helpers/BaseChatMesh.cpp) with NO offset applied -- getNumContacts()
// only SUBTRACTS MAX_ANON_CONTACTS from the count to hide the reserved
// anon-request slots at the front of that array, it doesn't shift where
// index 0 points. The real UI/CLI code never hits this because it always
// walks contacts via startContactsIterator() (BaseChatMesh.cpp), which
// already begins at MAX_ANON_CONTACTS -- this small helper mirrors that
// same offset for these test-only hooks instead of duplicating an iterator.
static bool findFirstChatContact(ContactInfo& out) {
  int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) && ci.type == ADV_TYPE_CHAT) {
      out = ci;
      return true;
    }
  }
  return false;
}

// Finds the first known contact of type ADV_TYPE_CHAT (i.e. another
// companion_radio instance, not a repeater/room) and sends it a real DM via
// BaseChatMesh::sendMessage() -- the exact function CMD_SEND_TXT_MSG calls.
// Returns MSG_SEND_SENT_FLOOD/MSG_SEND_SENT_DIRECT/MSG_SEND_FAILED (see
// src/helpers/BaseChatMesh.h), or -1 if no chat contact is known yet.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_send_msg_to_first_contact(const char* text) {
  if (!g_sim_ready) return -1;
  ContactInfo ci;
  if (!findFirstChatContact(ci)) return -1;
  uint32_t expected_ack, est_timeout;
  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  return the_mesh.sendMessage(ci, ts, 0, text, expected_ack, est_timeout);
}

// Sends `text` on group channel `channel_idx` (0 = Public) via
// BaseChatMesh::sendGroupMessage() -- what CMD_SEND_CHANNEL_TXT_MSG calls --
// so a peer instance can post into a channel another instance is viewing
// (web/lvgl.html). Returns 1 if sent, 0 if not (no such channel / not ready).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_send_channel_msg(int channel_idx, const char* text) {
  if (!g_sim_ready || !text) return 0;
  ChannelDetails ch;
  if (!the_mesh.getChannel(channel_idx, ch)) return 0;
  return the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                   the_mesh.getNodeName(), text, strlen(text)) ? 1 : 0;
}

// How many contacts this instance has discovered so far (any type) -- lets
// the JS ether-tick loop poll "has advert propagation finished yet" without
// guessing a fixed timeout.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_get_num_contacts() {
  // Same g_sim_ready gate as every other hook here: before setup() runs,
  // BaseChatMesh::num_contacts hasn't been seeded to MAX_ANON_CONTACTS yet,
  // so getNumContacts() would report a negative count.
  if (!g_sim_ready) return 0;
  return the_mesh.getNumContacts();
}

// The live radio channel config -- lets a host page (meshcore-solo-site's
// WebSocket relay bridge) tag this instance's outgoing packets with what
// it's currently tuned to, so a server-side relay can fan bytes out only to
// other instances tuned to the same freq/bw/sf/cr, mirroring real LoRa
// channel isolation. Reads NodePrefs directly (not SimRadio, which never
// stores the values SimRadio::setParams() is called with) since NodePrefs
// is the actual live source of truth -- the same fields the on-device
// Settings > Radio screen edits via UITask::applyRadioParams().
extern "C" EMSCRIPTEN_KEEPALIVE void sim_radio_get_params(float* out_freq, float* out_bw, int* out_sf, int* out_cr) {
  NodePrefs* p = g_sim_ready ? the_mesh.getNodePrefs() : nullptr;
  *out_freq = p ? p->freq : 0;
  *out_bw   = p ? p->bw   : 0;
  *out_sf   = p ? p->sf   : 0;
  *out_cr   = p ? p->cr   : 0;
}

// A brand-new device's home_pages_mask defaults to 0 = all pages visible
// (see NodePrefs.h and MyMesh.cpp) -- this is now a no-op on a freshly
// booted sim instance, but is kept for a saved-prefs instance whose mask
// was narrowed by an actual Settings > Home Pages visit (a visitor who
// toggled some pages off before this hook runs on a later boot).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_show_all_home_pages() {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  prefs->home_pages_mask = 0;
  return 1;
}

// tz_offset_hours (NodePrefs.h) defaults to 0 (UTC) -- real hardware has
// no other way to know the visitor's timezone, so a real user sets it
// manually in Settings > System > Timezone. RTCClock itself (SimRTCClock.h)
// is already correct live UTC (time(NULL)), so a demo instance's Clock
// screen otherwise displays correct-but-UTC time, which reads as "wrong"
// to a visitor who never opened Settings -- exactly the site's own
// "would be nice if the time was synced with the computer" ask. Called
// once after boot with the browser's own timezone offset (whole hours
// only -- tz_offset_hours is an int8_t, same real-hardware constraint a
// half-hour-offset timezone would hit too).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_set_timezone_hours(int hours) {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  if (hours < -12) hours = -12;
  if (hours > 14) hours = 14;
  prefs->tz_offset_hours = (int8_t)hours;
  return 1;
}

// Renames this instance (NodePrefs::node_name, what Settings > Name sets) so
// a host page's peers have readable names in screenshots, instead of the
// default hex of their public key. In memory only; the next advert carries it.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_set_node_name(const char* name) {
  if (!g_sim_ready || !name || !*name) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  snprintf(prefs->node_name, sizeof(prefs->node_name), "%s", name);
  return 1;
}

// The first contact of `type`'s one-byte hash as hex ("e9"), "" if none --
// how a hop to it reads in a route ({path}).
extern "C" EMSCRIPTEN_KEEPALIVE const char* sim_test_contact_hash(int type) {
  static char hex[3];
  hex[0] = 0;
  if (!g_sim_ready) return hex;
  for (int i = 0; i < the_mesh.getNumContacts(); i++) {
    ContactInfo ci;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) && ci.type == type) {
      snprintf(hex, sizeof(hex), "%02x", ci.id.pub_key[0]);
      break;
    }
  }
  return hex;
}

// Turns on the DM bot's commands (Tools > Bot > Direct > Commands), with its
// actions and who may run them (Other > Actions by: 0 all, 1 favourites), so
// a host page can test the bot from a peer. In memory only.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_bot(int actions, int actions_scope) {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  prefs->bot_commands_enabled = 1;
  prefs->bot_actions_dm = actions ? 1 : 0;
  prefs->bot_actions_scope = actions_scope ? 1 : 0;
  return 1;
}

// Whether adverts carry this node's position (ADVERT_LOC_NONE / _SHARE /
// _PREFS, src/helpers/CommonCLI.h) -- off by default, so peers in a test
// mesh have no distance or direction to one another until it's on.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_set_advert_loc(int policy) {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  prefs->advert_loc_policy = (uint8_t)policy;
  return 1;
}

// auto_off_secs (NodePrefs.h) defaults to 15 -- a real device's screen
// (and, if auto_lock is also set, the UI itself) turns off/locks 15s
// after the last input, real power-saving behaviour a battery-powered
// board actually needs. UITask::autoOffMillis() (ui-new/UITask.h) treats
// 0 as "never" and skips the whole turnOff()/auto_lock branch in
// UITask::loop() entirely -- there's no separate flag to touch. A demo
// running on a visitor's screen has no battery to save and no reason to
// go dark or lock itself while they're reading it.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_disable_screen_timeout() {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  prefs->auto_off_secs = 0;
  return 1;
}

// Sets NodePrefs::msg_wake directly (the same field Settings >
// Display > "Msg wake" toggles -- SettingsScreen.h's MSG_WAKE item), so a test
// harness can verify UITask::newMsg()'s wake-gating without scripting the
// on-device Settings accordion navigation key-by-key.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_set_msg_wake_disabled(int disabled) {
  if (!g_sim_ready) return 0;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return 0;
  prefs->msg_wake = disabled ? 0 : 1;
  return 1;
}

// Raw home_pages_mask readback -- verifies a fresh instance really does
// default to 0 (= all pages visible, MyMesh.cpp) without having to count
// carousel frames on-canvas (unreliable: several home pages, e.g. Clock,
// redraw with live-changing content every tick, so a page revisited later
// in the cycle rarely hashes identically to its first visit).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_get_home_pages_mask() {
  if (!g_sim_ready) return -1;
  NodePrefs* prefs = the_mesh.getNodePrefs();
  if (!prefs) return -1;
  return (int)prefs->home_pages_mask;
}

// Re-anchors this instance's RTC to the host's real wall clock. SimRTCClock
// (variants/sim/SimRTCClock.h) already starts out reading time(NULL), so a
// freshly booted instance needs no help -- but the clock is a shared, live
// thing the real mesh code legitimately writes to at runtime: a received
// packet or a contact-list bootstrap carrying a timestamp ahead of ours
// pushes it forward (BaseChatMesh::bootstrapRTCfromContacts(),
// MyMesh's own timestamp handling), which is correct behaviour on a real
// node with no better time source, and means one peer with a badly skewed
// clock can drag every node that hears it. Calling this on every (re)boot
// makes "reset the device" mean "the demo's clock matches the visitor's own
// computer again", the same guarantee the timezone hook above gives.
// Seconds since the Unix epoch, as a double because a JS Date.now()/1000
// value has no exact int32 representation to pass through ccall.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_sync_time(double epoch_secs) {
  if (epoch_secs < 1000000000.0) return 0;   // obvious nonsense (pre-2001) -- leave the clock alone
  rtc_clock.setCurrentTime((uint32_t)epoch_secs);
  return 1;
}

#ifdef DISPLAY_CLASS
// Jumps the on-device UI straight to the DM thread with the first known
// ADV_TYPE_CHAT contact (UITask::openContactDM() -- the exact same real
// function NearbyScreen's contact-list "select" action calls) so a test
// harness can screenshot the canvas and see the actual received message
// text, rendered by the real MessagesScreen/DisplayDriver code, without
// having to script the on-device contact-list navigation key-by-key.
// Returns 1 if a chat contact was found and the screen switched, 0 if not
// (e.g. advert propagation hasn't reached this instance yet).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_dm_with_first_contact() {
  if (!g_sim_ready) return 0;
  ContactInfo ci;
  if (!findFirstChatContact(ci)) return 0;
  ui_task.openContactDM(ci);
  return 1;
}
#endif

// Same idea as findFirstChatContact() above, but for the first known
// admin-loginable contact (a repeater or room server) instead of another
// chat instance.
static bool findFirstAdminContact(ContactInfo& out) {
  int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) &&
        (ci.type == ADV_TYPE_REPEATER || ci.type == ADV_TYPE_ROOM)) {
      out = ci;
      return true;
    }
  }
  return false;
}

#ifdef DISPLAY_CLASS
// Jumps the on-device UI straight to AdminScreen for the first known
// repeater/room contact -- UITask::openAdminFor(ci, false), the exact same
// function NearbyScreen's "Nodes" Hold-Enter admin action calls (see
// NearbyScreen.h:709), so a test harness can screenshot the real Admin
// screen instead of scripting contact-list navigation key-by-key. Returns
// 1 if a repeater/room contact was found and the screen switched, 0 if not.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_admin_with_first_repeater() {
  if (!g_sim_ready) return 0;
  ContactInfo ci;
  if (!findFirstAdminContact(ci)) return 0;
  ui_task.openAdminFor(ci, false);
  return 1;
}
#endif

#ifdef DISPLAY_CLASS
// Opens Path / trace (ui-new) or Route (ui-lvgl) for the first contact of
// `type` (ADV_TYPE_*), as the node menu does. 1 if found, 0 if not yet.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_route(int type) {
  if (!g_sim_ready) return 0;
  int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) && ci.type == type) {
      ui_task.openRouteFor(ci.id.pub_key);
      return 1;
    }
  }
  return 0;
}
#endif

// Puts the first contact of `type` (ADV_TYPE_*) at a position, as if its
// advert had carried one (the map's packet flow needs nodes on the map).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_place_contact(int type, double lat, double lon) {
  if (!g_sim_ready) return 0;
  int n = the_mesh.getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, ci) || ci.type != type) continue;
    ContactInfo* c = the_mesh.lookupContactByPubKey(ci.id.pub_key, PUB_KEY_SIZE);
    if (!c) return 0;
    c->gps_lat = (int32_t)lround(lat * 1e6);
    c->gps_lon = (int32_t)lround(lon * 1e6);
    return 1;
  }
  return 0;
}

// A hashtag channel into the first free slot ("#name": the name is the
// passphrase, as chanctl::addHashtag does). The slot, or -1.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_add_channel(const char* name) {
  if (!g_sim_ready || !name || !name[0]) return -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (the_mesh.getChannel(i, ch) && ch.name[0]) continue;
    memset(&ch, 0, sizeof(ch));
    strncpy(ch.name, name, sizeof(ch.name) - 1);
    uint8_t digest[32];
    mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t*)name, (int)strlen(name));
    memcpy(ch.channel.secret, digest, 16);
    return the_mesh.setChannelLocal((uint8_t)i, ch) ? i : -1;
  }
  return -1;
}

#ifdef DISPLAY_CLASS
// The nodes map (ui-new: Nodes > Map; ui-lvgl: the map from Nodes).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_nodes_map() {
  return g_sim_ready && ui_task.openNodesMap() ? 1 : 0;
}

// The messages list (ui-lvgl: Messages; ui-new: the Messages screen).
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_open_messages() {
  if (!g_sim_ready) return 0;
  ui_task.openMessagesList();
  return 1;
}
#endif

// Submits a login against the first known repeater/room contact via
// MyMesh::sendRoomLogin() -- the exact same function AdminScreen's own
// submit button calls (examples/companion_radio/ui-new/AdminScreen.h) --
// so a test harness can verify the admin/password flow without scripting
// the on-device virtual keyboard. Only reports whether the login *request*
// was sent (matching sim_test_send_msg_to_first_contact()'s same
// synchronous-only contract) -- the actual accept/reject arrives async via
// AbstractUITask::onRoomLoginResult() and is visible on AdminScreen once
// sim_test_open_admin_with_first_repeater() has switched to it. Returns 1
// if sent, 0 if send failed, -1 if no repeater/room contact known yet.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_test_login_first_repeater(const char* password) {
  if (!g_sim_ready) return -1;
  ContactInfo ci;
  if (!findFirstAdminContact(ci)) return -1;
  uint32_t est_timeout;
  return the_mesh.sendRoomLogin(ci, password, est_timeout) ? 1 : 0;
}

// The companion CLI (CMD_RUN_CLI_COMMAND) without a transport: the text in,
// the reply out, as the app's console gets it.
extern "C" EMSCRIPTEN_KEEPALIVE const char* sim_cli(const char* command) {
  static char reply[MAX_FRAME_SIZE];
  reply[0] = 0;
  if (!g_sim_ready) return reply;
  if (!the_mesh.handleCliCommand(command, reply, sizeof(reply))) {
    size_t at = strlen(reply);
    snprintf(reply + at, sizeof(reply) - at, "Unknown command");
  }
  return reply;
}
#endif
