#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "target.h"
#include "../../examples/companion_radio/ui-core/MagCal.h"

FireBeetle2C6Board board;

// The one SPI bus (the C6 has a single general-purpose one): begun with all
// three pins by whichever of the radio and the display starts first.
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;
Lsm6Lis3Imu imu;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true, true);   // BOOT, to GND
#endif

static int s_radio_status = 1;   // 1: not tried
static bool s_sd_ok = false;
static uint32_t s_sd_mb = 0;
static uint8_t s_sd_type = CARD_NONE;

#ifdef MESH_DEBUG
// Bring-up: the SX1262's wiring, before RadioLib gives up with a bare -2.
// BUSY goes high while the chip resets and low once it is up; the sync-word
// register reads 0x14 0x24 after a reset. A BUSY that never moves: BUSY or
// RESET wrong (or no power); 0x00 / 0xFF from the register: NSS, MOSI or MISO.
static void radio_probe() {
  pinMode(P_LORA_BUSY, INPUT);
  pinMode(P_LORA_RESET, OUTPUT);
  const int b0 = digitalRead(P_LORA_BUSY);
  digitalWrite(P_LORA_RESET, LOW);
  delay(2);
  const int b_rst = digitalRead(P_LORA_BUSY);
  digitalWrite(P_LORA_RESET, HIGH);
  const uint32_t t0 = micros();
  bool high = false;
  uint32_t low_at = 0;
  while (micros() - t0 < 50000) {
    const int b = digitalRead(P_LORA_BUSY);
    if (b) high = true;
    else if (high && !low_at) low_at = micros() - t0;
  }
  Serial.printf("PROBE: BUSY idle %d, in reset %d, after: %s high, low after %lu us, now %d\n", b0, b_rst,
                high ? "went" : "never", (unsigned long)low_at, digitalRead(P_LORA_BUSY));
  SPI.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  pinMode(P_LORA_NSS, OUTPUT);
  static const uint32_t SPEEDS[] = { 250000, 1000000, 2000000, 8000000 };
  for (uint32_t hz : SPEEDS) {
    int good = 0;
    uint8_t first[6] = {};
    for (int k = 0; k < 50; k++) {
      while (digitalRead(P_LORA_BUSY)) { }
      SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
      digitalWrite(P_LORA_NSS, LOW);
      uint8_t r[6];
      const uint8_t cmd[6] = { 0x1D, 0x07, 0x40, 0x00, 0x00, 0x00 };   // ReadRegister 0x0740
      for (int i = 0; i < 6; i++) r[i] = SPI.transfer(cmd[i]);
      digitalWrite(P_LORA_NSS, HIGH);
      SPI.endTransaction();
      if (k == 0) memcpy(first, r, 6);
      if (r[4] == 0x14 && r[5] == 0x24) good++;
    }
    Serial.printf("PROBE: %lu Hz: %d/50 read 14 24; first %02X %02X %02X %02X | %02X %02X\n", (unsigned long)hz, good,
                  first[0], first[1], first[2], first[3], first[4], first[5]);
  }
  // GetStatus: the chip mode in bits 6:4 (2 = STDBY_RC)
  while (digitalRead(P_LORA_BUSY)) { }
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(P_LORA_NSS, LOW);
  SPI.transfer(0xC0);
  const uint8_t st = SPI.transfer(0x00);
  digitalWrite(P_LORA_NSS, HIGH);
  SPI.endTransaction();
  Serial.printf("PROBE: status %02X (mode %d, cmd %d)\n", st, (st >> 4) & 7, (st >> 1) & 7);
}
#endif

bool radio_init() {
  fallback_clock.begin();
  rtc_clock.begin(Wire);
#ifdef MESH_DEBUG
  delay(3000);   // time for the USB console to attach
  radio_probe();
#endif
  const bool ok = radio.std_init(&SPI);
  s_radio_status = ok ? 0 : -1;
#ifdef PIN_SD_CS
  // Probed once, after the radio has the bus set up; released again (the UI
  // has no use for it yet), only the answer kept for "hw".
  s_sd_ok = SD.begin(PIN_SD_CS, SPI, 4000000);
  if (s_sd_ok) {
    s_sd_type = SD.cardType();
    s_sd_mb = (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL));
    SD.end();
  }
  digitalWrite(PIN_SD_CS, HIGH);
#endif
  imu.begin(Wire);
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

static void board_hw_report(char* reply, int n) {
  int o = snprintf(reply, n, "radio %s", s_radio_status == 0 ? "OK" : "FAIL");
  static const char* const T[] = { "none", "MMC", "SD", "SDHC", "?" };
  if (o < n) o += s_sd_ok ? snprintf(reply + o, n - o, ", SD %s %lu MB", T[s_sd_type <= 3 ? s_sd_type : 4], (unsigned long)s_sd_mb)
                          : snprintf(reply + o, n - o, ", SD none");
  // The addresses this board can have, not all 127: a bus held low makes
  // each one wait out the driver's timeout.
  static const struct { uint8_t a; const char* what; } I2C[] = {
    { 0x6A, "LSM6DS" }, { 0x6B, "LSM6DS" }, { 0x1C, "LIS3MDL" }, { 0x1E, "LIS3MDL" },
    { 0x68, "RTC" }, { 0x51, "RTC" },
  };
  Wire.setTimeOut(20);
  const uint32_t t0 = millis();
  if (o < n) o += snprintf(reply + o, n - o, ", I2C");
  int found = 0;
  for (const auto& d : I2C) {
    if (o >= n) break;
    Wire.beginTransmission(d.a);
    if (Wire.endTransmission() == 0) { o += snprintf(reply + o, n - o, " %s %02X", d.what, d.a); found++; }
  }
  if (!found && o < n) o += snprintf(reply + o, n - o, " none");
  if (o < n) o += snprintf(reply + o, n - o, " (%lu ms)", (unsigned long)(millis() - t0));
  if (o < n) snprintf(reply + o, n - o, ", batt %u mV", board.getBattMilliVolts());
}

// The compass's readings (ui-core/Compass.h) in the device's frame: x to the
// top of the screen, y to its right, z into it. On the breadboard the IMU
// lies face up with its y to the screen's top and its x to the right; its z
// points out of the screen. Both chips the same way, as on Adafruit's 9-DOF
// boards. (A change here wants the compass calibrated again: the fit is in
// this frame.)
bool board_compass_read(magcal::Vec& acc, magcal::Vec& mag) {
  Lsm6Lis3Imu::Raw r;
  if (!imu.read(r)) return false;
  acc = { r.ay, r.ax, -r.az };
  mag = { r.my, r.mx, -r.mz };
  return true;
}

bool board_cli(const char* cmd, char* reply, int n) {
  if (!strcmp(cmd, "hw")) { board_hw_report(reply, n); return true; }
  if (!strcmp(cmd, "imu")) {
    Lsm6Lis3Imu::Raw r;
    if (!imu.read(r)) { snprintf(reply, n, "Error, no IMU (acc %02X, mag %02X)", imu.accAddr(), imu.magAddr()); return true; }
    snprintf(reply, n, "acc %.2f %.2f %.2f  mag %.1f %.1f %.1f", r.ax, r.ay, r.az, r.mx, r.my, r.mz);
    return true;
  }
  return false;
}
