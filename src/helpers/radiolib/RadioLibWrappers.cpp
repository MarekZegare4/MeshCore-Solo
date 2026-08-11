
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

#define RXPS_WATCHDOG_THRESHOLD_MS  60000UL   // no BUSY transition for this long => stuck
#define NF_CALIB_INTERVAL_MS         60000UL  // recalibrate at least once a minute
#define NF_CALIB_TIMEOUT_MS           5000UL  // give up on the window (busy channel)

static volatile uint8_t state = STATE_IDLE;

// this function is called when a complete packet
// is transmitted by the module
static 
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt
  _preamble_sf = getSpreadingFactor();
  _radio->setPreambleLength(preambleLengthForSF(_preamble_sf)); // longer preamble for lower SF improves reliability
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;
  _cad_enabled = false;

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
  _tx_dbm = dbm;
  _radio->setOutputPower(dbm);
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // ignore trigger if currently sampling
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::doResetAGC() {
  _radio->sleep();  // warm sleep to reset analog frontend
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  doResetAGC();
  state = STATE_IDLE;   // trigger a startReceive()

  // Reset noise floor sampling so it reconverges from scratch.
  // Without this, a stuck _noise_floor of -120 makes the sampling threshold
  // too low (-106) to accept normal samples (~-105), self-reinforcing the
  // stuck value even after the receiver has recovered.
  _noise_floor = 0;
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

// Detects a stuck RX duty-cycle sequencer: a healthy chip shows the hardware
// BUSY pin toggling as it moves through its own RX<->sleep cycle. If that
// stops for too long, first try a cheap soft re-arm; if it's still stuck next
// time, escalate to a full chip reset (radioHardReset()).
void RadioLibWrapper::rxPsWatchdogCheck() {
  // Don't interfere mid-transmit, or with a completed-but-unread packet — a
  // pending DIO1 event is itself proof the radio is alive, and recvRaw()
  // will consume it and re-arm (re-basing the watchdog) on its own.
  if ((state & STATE_INT_READY) != 0 || (state & ~STATE_INT_READY) == STATE_TX_WAIT) return;

  uint32_t now = millis();
  bool busy = isChipBusy();
  if (busy != _wd_last_busy) {
    _wd_last_busy = busy;
    _wd_last_transition_ms = now;
    _wd_stage = 0;   // proof of life -- any prior stall is over
    return;
  }

  if (now - _wd_last_transition_ms <= RXPS_WATCHDOG_THRESHOLD_MS) return;

  _wd_last_transition_ms = now;   // grace period before the next escalation

  if (_wd_stage == 0) {
    _wd_stage = 1;
    _wd_soft_count++;
    MESH_DEBUG_PRINTLN("RadioLibWrapper: RX duty-cycle watchdog: stuck, soft re-arm");
    state = STATE_IDLE;   // next recvRaw()/loop() re-arms via armRecv()
  } else {
    _wd_hard_count++;
    MESH_DEBUG_PRINTLN("RadioLibWrapper: RX duty-cycle watchdog: still stuck, hard reset");
    radioHardReset();
    _wd_stage = 0;   // chip is freshly (re)initialized either way -- observe again from scratch
    state = STATE_IDLE;
  }
}

void RadioLibWrapper::reattachRecvAction() {
  _radio->setPacketReceivedAction(setFlag);
}

// Checks whether a periodic noise-floor recalibration window is due while RX
// duty-cycle power-save is active (see the field comments in the header).
// Only starts one when it's safe to interrupt: not mid-transmit, no unread
// packet waiting, and no reception currently in progress.
void RadioLibWrapper::noiseFloorCalibCheck() {
  if ((state & STATE_INT_READY) != 0 || (state & ~STATE_INT_READY) == STATE_TX_WAIT) return;
  if (isReceivingPacket()) return;

  uint32_t now = millis();
  if (_nf_last_calib_ms != 0 && now - _nf_last_calib_ms < NF_CALIB_INTERVAL_MS) return;

  _nf_calib_active = true;
  _nf_calib_deadline_ms = now + NF_CALIB_TIMEOUT_MS;
  _num_floor_samples = 0;   // start a fresh batch for this window
  _floor_sample_sum = 0;
  state = STATE_IDLE;       // next loop() re-arms into continuous RX (see armRecv())
}

void RadioLibWrapper::loop() {
  // Power-save vs the currently-armed RX mode (toggled by the user, or by a
  // noise-floor recalibration window borrowing continuous RX for a moment):
  // re-arm into the wanted mode once the radio is idle (don't interrupt an
  // in-flight TX or an unread RX-done).
  bool want_duty_cycle = _power_save && !_nf_calib_active;
  if (want_duty_cycle != _ps_active) {
    if (state != STATE_TX_WAIT && !(state & STATE_INT_READY) && !isReceivingPacket()) armRecv();
    return;
  }

  if (want_duty_cycle) {
    // Steady-state duty-cycle: the chip cycles RX<->sleep on its own, nothing
    // to poll here. Just watch for a stuck sequencer and check whether a
    // recalibration window is due (noiseFloorCalibCheck() flips
    // _nf_calib_active, which the branch above then re-arms out of).
    if (supportsRxPsWatchdog()) rxPsWatchdogCheck();
    noiseFloorCalibCheck();
    return;
  }

  if (_nf_calib_active && !isReceivingPacket() && (int32_t)(millis() - _nf_calib_deadline_ms) >= 0) {
    // Batch couldn't complete in time (busy channel) -- give up, keep the
    // previous floor, and let the branch above re-arm the duty-cycle.
    _nf_calib_active = false;
    _nf_last_calib_ms = millis();
    state = STATE_IDLE;
    return;
  }

  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {  // only consider samples below current floor + sampling THRESHOLD
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) {
      _noise_floor = -120;    // clamp to lower bound of -120dBi
    }
    _floor_sample_sum = 0;

    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);

    if (_nf_calib_active) {
      _nf_calib_active = false;   // fresh floor published -- back to duty-cycle
      _nf_last_calib_ms = millis();
    }
  }
}

void RadioLibWrapper::startRecv() {
  armRecv();
}

// Arm the receiver. In power-save mode (and outside a noise-floor
// recalibration window, which needs a moment of plain continuous RX -- see
// noiseFloorCalibCheck()) this starts the SX126x hardware RX duty-cycle (the
// chip cycles RX↔sleep and latches a preamble on its own); otherwise a
// continuous RX. Falls back to continuous RX if the modem doesn't support
// duty-cycle (base startPowerSaveRecv() returns UNSUPPORTED).
void RadioLibWrapper::armRecv() {
  if (_power_save && !_nf_calib_active) {
    int16_t e = startPowerSaveRecv();
    if (e == RADIOLIB_ERR_NONE) { state = STATE_RX; _ps_active = true; return; }
    MESH_DEBUG_PRINTLN("RadioLibWrapper: RX duty-cycle unsupported (%d) — power-save off", (int)e);
    _power_save = false;
  }
  _ps_active = false;
  #if defined(USE_LR2021)
  _radio->standby(); // without this LR2021 can throw -706 when calling startReceive after hardware CAD when side detectors are enabled
  #endif
  int err = _radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
        n_recv_errors++;
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
      }
    }
    #if defined(USE_LR2021)
    state = STATE_RX;     // LR2021 stays in Rx after readData, calling startReceive while still in Rx throws -706 errors
    #else
    state = STATE_IDLE;   // need another startReceive()
    #endif
  }

  if (state != STATE_RX) {
    armRecv();   // continuous RX, or re-arm the duty-cycle in power-save mode
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  _board->onAfterTransmit();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
}

int16_t RadioLibWrapper::performChannelScan() {
  return _radio->scanChannel();
}

bool RadioLibWrapper::isChannelActive() {
  // int.thresh: RSSI-based interference detection (relative to noise floor)
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;

  // cad: hardware channel activity detection
  if (_cad_enabled) {
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) return true;
  }

  return false;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;

  float floor = snrFloorForSF(sf);                 // min SNR for a chance of success
  if (snr < floor) return 0.0f;                    // below the demod floor → no chance

  auto success_rate_based_on_snr = (snr - floor) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;
  
  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF)
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000 - preamble_us; // fallback to 4 secs at worst case
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (payload_us * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}