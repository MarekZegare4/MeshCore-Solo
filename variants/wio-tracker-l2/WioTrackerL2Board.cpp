#include "WioTrackerL2Board.h"

// TCA9535 register map
static const uint8_t TCA_REG_OUTPUT0 = 0x02;
static const uint8_t TCA_REG_OUTPUT1 = 0x03;
static const uint8_t TCA_REG_CONFIG0 = 0x06;  // 1 = input, 0 = output
static const uint8_t TCA_REG_CONFIG1 = 0x07;

// ADS1115 registers / config bits
static const uint8_t ADS_REG_CONVERSION = 0x00;
static const uint8_t ADS_REG_CONFIG     = 0x01;
// OS=1 (start single shot) | MUX=100 (AIN0 vs GND) | PGA=001 (+/-4.096V)
// | MODE=1 (single shot) | DR=100 (128 SPS) | COMP_QUE=11 (disabled)
static const uint16_t ADS_CFG_BATT = 0x8000 | 0x4000 | 0x0200 | 0x0100 | 0x0080 | 0x0003;
// +/-4.096V FSR -> 125uV/LSB; battery is behind a x2 divider -> 0.25 mV/LSB
static const float ADS_MV_PER_LSB = 0.125f * 2.0f;
static const uint32_t BATT_SETTLE_MS = 1;       // divider on -> input settled (measured: at once)
static const uint32_t BATT_REUSE_MS  = 2000;    // the UI, diagnostics and the app all ask

// GT911: its command register; 0x05 = sleep
static const uint8_t GT911_ADDR = 0x5D;

bool WioTrackerL2Board::expWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA9535_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

void WioTrackerL2Board::expSetOutput(uint8_t pin, bool initial_level) {
  uint8_t port = pin >> 3, bit = pin & 7;
  // set desired level first so the pin doesn't glitch when direction flips
  if (initial_level) out_shadow[port] |= (1 << bit);
  else out_shadow[port] &= ~(1 << bit);
  expWriteReg(port ? TCA_REG_OUTPUT1 : TCA_REG_OUTPUT0, out_shadow[port]);

  cfg_shadow[port] &= ~(1 << bit);  // 0 = output
  expWriteReg(port ? TCA_REG_CONFIG1 : TCA_REG_CONFIG0, cfg_shadow[port]);
}

void WioTrackerL2Board::expSetInput(uint8_t pin) {
  uint8_t port = pin >> 3, bit = pin & 7;
  cfg_shadow[port] |= (1 << bit);
  expWriteReg(port ? TCA_REG_CONFIG1 : TCA_REG_CONFIG0, cfg_shadow[port]);
}

void WioTrackerL2Board::expWritePin(uint8_t pin, bool level) {
  uint8_t port = pin >> 3, bit = pin & 7;
  if (level) out_shadow[port] |= (1 << bit);
  else out_shadow[port] &= ~(1 << bit);
  expWriteReg(port ? TCA_REG_OUTPUT1 : TCA_REG_OUTPUT0, out_shadow[port]);
}

// Power-up sequence mirrors Seeed's reference firmware for this board
// (order and delays matter, especially the LCD reset settle time).
bool WioTrackerL2Board::initExpander() {
  Wire.beginTransmission(TCA9535_ADDR);
  if (Wire.endTransmission() != 0) {
    return false;  // expander not responding
  }

  expSetInput(EXP_PIN_WAKE_BTN);
  expSetInput(EXP_PIN_I2C_IRQ);
  expSetInput(EXP_PIN_SD_DETECT);

  expSetOutput(EXP_PIN_OTG_EN, LOW);
  delay(10);
  expSetOutput(EXP_PIN_PA_EN, LOW);   // amp only powered while a tone plays
  delay(10);
  expSetOutput(EXP_PIN_TF_EN, HIGH);
  delay(10);
  expSetOutput(EXP_PIN_BAT_ADC_EN, LOW);   // the divider only draws while a reading is taken
  delay(10);
  expSetOutput(EXP_PIN_GNSS_EN, HIGH);
  delay(10);
  // GNSS reset is active HIGH; hold 10ms then release LOW so the L76K runs
  expSetOutput(EXP_PIN_GNSS_RST, HIGH);
  delay(10);
  expWritePin(EXP_PIN_GNSS_RST, LOW);
  // user LED: lit while the pin is HIGH (it read as active-low before, and
  // stayed lit except while transmitting); idle LOW = off
  expSetOutput(EXP_PIN_USER_LED, LOW);
  delay(10);
  expSetOutput(EXP_PIN_GROVE_EN, HIGH);
  delay(10);

  expSetOutput(EXP_PIN_LCD_EN, HIGH);
  delay(50);
  expSetOutput(EXP_PIN_LCD_RST, HIGH);
  delay(5);
  expWritePin(EXP_PIN_LCD_RST, LOW);
  delay(10);
  expWritePin(EXP_PIN_LCD_RST, HIGH);
  delay(500);  // NV3031B needs a long settle after reset before init commands
  expSetOutput(EXP_PIN_LCD_CS, HIGH);
  delay(10);

  // GT911 touch reset sequence: INT low during reset selects I2C addr 0x5D
  expSetOutput(EXP_PIN_TP_RST, LOW);
  expSetOutput(EXP_PIN_TP_INT, LOW);
  delay(10);
  expWritePin(EXP_PIN_TP_RST, HIGH);
  delay(60);

  // capture WAKE button idle level (polarity unknown on alpha hardware)
  int inputs = expReadInputs();
  wake_btn_baseline = inputs >= 0 ? (uint8_t)(inputs & 1) : 0;

  return true;
}

// Off -- Power off, or the battery ran down -- the chip sleeps, waking every
// OFF_POLL_SECS to look for USB power plugged in, and starts when it is: as
// an nRF board starts on its own once charged (a deep sleep with no wake
// source left it off until the reset button). Plugged in when switched off,
// it stays off until the cable is pulled and plugged in again. A look costs
// a boot as far as begin(), a fraction of a second.
static const uint32_t OFF_MAGIC = 0x4F464621;   // "OFF!"
static const uint32_t OFF_POLL_SECS = 30;
RTC_NOINIT_ATTR static uint32_t s_off_magic;
RTC_NOINIT_ATTR static uint32_t s_off_vbus;     // USB power at the last look

void WioTrackerL2Board::powerOff() {
  s_off_vbus = isExternalPowered();
  s_off_magic = OFF_MAGIC;
  enterDeepSleep(OFF_POLL_SECS);
}

// Off and woken by the timer (or a brown-out of the flat battery): back to
// sleep unless USB power has come since the last look.
static void stayOffUnlessPlugged(WioTrackerL2Board& b) {
  const esp_reset_reason_t r = esp_reset_reason();
  if (s_off_magic != OFF_MAGIC || (r != ESP_RST_DEEPSLEEP && r != ESP_RST_BROWNOUT)) {
    s_off_magic = 0;
    return;
  }
  Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
  const bool vbus = b.isExternalPowered();
  if (vbus && !s_off_vbus) { s_off_magic = 0; return; }   // plugged in: start
  s_off_vbus = vbus;
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(OFF_POLL_SECS * 1000000ULL);
  esp_deep_sleep_start();
}

void WioTrackerL2Board::begin() {
  stayOffUnlessPlugged(*this);   // first: before the 3 s wait for a serial monitor below

  // GNSS UART: NMEA arrives at ~500 B/s and the main loop can stall for
  // hundreds of ms on SD tile decodes; the 256-byte default overflowed
  Serial1.setRxBufferSize(1024);

  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) { delay(50); }
  MESH_DEBUG_PRINTLN("WioTrackerL2Board: ESP32Board init");

  ESP32Board::begin();  // starts Wire on PIN_BOARD_SDA/PIN_BOARD_SCL (47/48)

  pinMode(PIN_USER_BTN, INPUT_PULLUP);
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  MESH_DEBUG_PRINTLN("WioTrackerL2Board: TCA9535 expander init");
  expander_ok = initExpander();
  if (!expander_ok) {
    Serial.println("ERROR: TCA9535 IO expander not found - peripherals unpowered!");
  }

  Wire.beginTransmission(0x22);
  aw_ok = Wire.endTransmission() == 0;
  if (aw_ok) {
    // AW35615 = FUSB302-style register map. VBUSOK (STATUS0 bit 7) only
    // works with the measure block powered: POWER (0x0B) resets to 0x01
    // (bandgap only), which reads "no USB" forever. Add the receiver /
    // current references and the measure block (PWR[1..2]).
    // Verified on hardware: device ID 0x91; STATUS0 0x01 -> 0x80 on USB.
    int pw = awRead(0x0B);
    if (pw >= 0) {
      Wire.beginTransmission(0x22);
      Wire.write((uint8_t)0x0B);
      Wire.write((uint8_t)(pw | 0x07));
      Wire.endTransmission();
    }
  }
  MESH_DEBUG_PRINTLN("WioTrackerL2Board: init done");

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;  // LoRa packet woke us from deep sleep
    }
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

void WioTrackerL2Board::setLed(bool on) {
  if (expander_ok) {
    expWritePin(EXP_PIN_USER_LED, on);
  }
}

int WioTrackerL2Board::expReadInputs() {
  Wire.beginTransmission(TCA9535_ADDR);
  Wire.write((uint8_t)0x00);   // input port 0 register
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom((int)TCA9535_ADDR, 2) != 2) return -1;
  int lo = Wire.read();
  int hi = Wire.read();
  return lo | (hi << 8);
}

int WioTrackerL2Board::awRead(uint8_t reg) {
  if (!aw_ok) return -1;
  Wire.beginTransmission(0x22);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom(0x22, 1) != 1) return -1;
  return Wire.read();
}

bool WioTrackerL2Board::isExternalPowered() {
  int st0 = awRead(0x40);   // STATUS0
  return st0 >= 0 && (st0 & 0x80) != 0;   // VBUSOK
}

bool WioTrackerL2Board::readWakeButton() {
  if (!expander_ok) return false;
  int v = expReadInputs();
  if (v < 0) return false;
  return ((uint8_t)(v & 1)) != wake_btn_baseline;
}

int16_t WioTrackerL2Board::adsReadRaw() {
  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONFIG);
  Wire.write((uint8_t)(ADS_CFG_BATT >> 8));
  Wire.write((uint8_t)(ADS_CFG_BATT & 0xFF));
  if (Wire.endTransmission() != 0) return -1;

  delay(10);  // 128 SPS -> ~8ms conversion time

  Wire.beginTransmission(ADS1115_ADDR);
  Wire.write(ADS_REG_CONVERSION);
  if (Wire.endTransmission() != 0) return -1;
  if (Wire.requestFrom((int)ADS1115_ADDR, 2) != 2) return -1;

  // sequence the two reads explicitly: operand evaluation order of <<|
  // is unspecified and each read dequeues a byte
  uint8_t hi = Wire.read();
  uint8_t lo = Wire.read();
  int16_t raw = ((int16_t)hi << 8) | lo;
  return raw < 0 ? 0 : raw;
}

// A failed or zero ADS1115 read (the I2C bus busy or the ADC not answering)
// keeps the last good value instead of reporting 0 mV -- that read as 0 % in
// the status bar and could trip the low-battery shutdown. Retried once first.
// The divider is powered only around the reading; one taken in the last 2 s
// is reused.
uint16_t WioTrackerL2Board::getBattMilliVolts() {
  if (!expander_ok) return 0;  // BAT_ADC_EN rail never came up
  if (batt_mv_last && millis() - batt_read_ms < BATT_REUSE_MS) return batt_mv_last;

  expWritePin(EXP_PIN_BAT_ADC_EN, HIGH);
  delay(BATT_SETTLE_MS);
  for (int attempt = 0; attempt < 2; attempt++) {
    int16_t raw = adsReadRaw();
    if (raw > 0) {
      batt_mv_last = (uint16_t)(raw * ADS_MV_PER_LSB);
      break;
    }
  }
  expWritePin(EXP_PIN_BAT_ADC_EN, LOW);
  batt_read_ms = millis();
  return batt_mv_last;
}

void WioTrackerL2Board::touchSleep() {
  if (!expander_ok) return;
  expWritePin(EXP_PIN_TP_INT, LOW);   // held low while it sleeps (it is, from the reset sequence on)
  Wire.beginTransmission(GT911_ADDR);
  Wire.write((uint8_t)0x80);
  Wire.write((uint8_t)0x40);
  Wire.write((uint8_t)0x05);
  Wire.endTransmission();
}

void WioTrackerL2Board::touchWake() {
  if (!expander_ok) return;
  expWritePin(EXP_PIN_TP_INT, HIGH);   // 2-5 ms high wakes it
  delay(5);
  expWritePin(EXP_PIN_TP_INT, LOW);
}

