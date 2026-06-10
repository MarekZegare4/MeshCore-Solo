
#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

// Power-save CAD-windowing sub-states (tracked in _ps_phase; `state` stays
// STATE_RX throughout so isInRecvMode() reports "receiving" and the dispatcher
// never trips its 8 s not-in-RX watchdog while we sit in standby between scans).
#define PS_OFF       0
#define PS_SCANNING  1   // CAD armed, waiting for the CAD-done interrupt
#define PS_SLEEPING  2   // standby between scans, waiting for _ps_timer
#define PS_RXING     3   // CAD hit → full receive armed, waiting for the packet

// Time between CAD scans. Must be shorter than half the on-air preamble so at
// least two scans land inside it. 32-symbol preamble ≈ 131 ms at SF8/BW62.5
// → 65 ms gives 2 guaranteed windows with margin. Revisit if SF/BW change.
#define PS_CAD_INTERVAL_MS  65
// If CAD detects activity but no packet actually lands, fall back to scanning
// instead of sitting in continuous RX forever. Must exceed the longest possible
// packet air-time: 256 B at SF8/BW62.5/CR5 ≈ 1925 ms from preamble start, so
// 3000 ms gives ~1 s margin for a false-detect case.
#define PS_RX_TIMEOUT_MS    3000
// After a successful receive, skip inter-scan sleep for this long so back-to-back
// messages are caught without waiting out the 65 ms window each time.
#define PS_BURST_WINDOW_MS  500

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
  _radio->setChannelScanAction(setFlag);      // CAD-done shares the same DIO1 flag
  _ps_phase = PS_OFF;
  _preamble_sf = getSpreadingFactor();
  _radio->setPreambleLength(preambleLengthForSF(_preamble_sf)); // longer preamble for lower SF improves reliability
  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

uint32_t RadioLibWrapper::getRngSeed() {
  return _radio->random(0x7FFFFFFF);
}

void RadioLibWrapper::setTxPower(int8_t dbm) {
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

void RadioLibWrapper::loop() {
  if (_power_save) {
    // Just enabled while in continuous RX: drop into the CAD window once idle
    // (don't interrupt an in-flight packet or an unread RX-done).
    if (_ps_phase == PS_OFF) {
      if (!(state & STATE_INT_READY) && !isReceivingPacket()) armRecv();
      return;
    }
    powerSaveLoop();   // CAD windowing replaces continuous-RX noise sampling
    return;
  }
  // Just disabled while in a CAD phase: return to continuous RX immediately
  // (otherwise the radio would stay parked in standby between scans).
  if (_ps_phase != PS_OFF) { armRecv(); return; }

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
  }
}

void RadioLibWrapper::startRecv() {
  armRecv();
}

// Arm the receiver. In power-save mode this starts a CAD scan (and the CAD
// state machine in powerSaveLoop() takes over); otherwise a continuous RX.
// If CAD isn't supported by the module, permanently fall back to continuous RX.
void RadioLibWrapper::armRecv() {
  if (_power_save) {
    cadWake();                              // bring the radio out of warm sleep first
    int16_t e = _radio->startChannelScan();
    if (e == RADIOLIB_ERR_NONE) { _ps_phase = PS_SCANNING; state = STATE_RX; return; }
    MESH_DEBUG_PRINTLN("RadioLibWrapper: CAD unsupported (%d) — power-save off", (int)e);
    _power_save = false;
  }
  _ps_phase = PS_OFF;
  int err = _radio->startReceive();
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

void RadioLibWrapper::enterCadSleep() {
  if ((int32_t)(millis() - _ps_burst_until) < 0) {
    armRecv();   // burst window: skip sleep, scan again immediately
    return;
  }
  cadSleep();                     // warm sleep (SX126x) or standby fallback between scans
  _ps_phase = PS_SLEEPING;
  _ps_timer = millis() + PS_CAD_INTERVAL_MS;
  state = STATE_RX;               // keep isInRecvMode() true so the dispatcher doesn't flag a stuck radio
}

// CAD-windowed receive state machine. Runs each loop() (before recvRaw):
//  SLEEPING : wait out the inter-scan window, then re-arm a CAD scan.
//  SCANNING : on CAD-done, either drop into full RX (activity) or sleep (free).
//  RXING    : a real RX-done is consumed by recvRaw; here we only guard against
//             a false CAD that never yields a packet.
void RadioLibWrapper::powerSaveLoop() {
  if (state == STATE_TX_WAIT) return;   // never touch the radio mid-transmit

  switch (_ps_phase) {
    case PS_SLEEPING:
      if ((int32_t)(millis() - _ps_timer) >= 0) armRecv();   // → PS_SCANNING
      break;

    case PS_SCANNING:
      if (state & STATE_INT_READY) {
        state &= ~STATE_INT_READY;                            // consume CAD-done
        int16_t r = _radio->getChannelScanResult();
        if (r == RADIOLIB_LORA_DETECTED || r == RADIOLIB_PREAMBLE_DETECTED) {
          if (_radio->startReceive() == RADIOLIB_ERR_NONE) {
            _ps_phase = PS_RXING; state = STATE_RX;
            _ps_timer = millis() + PS_RX_TIMEOUT_MS;
          } else {
            enterCadSleep();
          }
        } else {
          enterCadSleep();                                    // channel free → sleep window
        }
      }
      break;

    case PS_RXING:
      // The packet (RX-done → STATE_INT_READY) is read by recvRaw, which then
      // re-arms via armRecv(). If nothing arrives, the CAD was a false hit:
      // return to scanning rather than burning current in continuous RX.
      if (!(state & STATE_INT_READY) && (int32_t)(millis() - _ps_timer) >= 0) {
        enterCadSleep();
      }
      break;

    default:
      break;
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
        _ps_burst_until = millis() + PS_BURST_WINDOW_MS;
      }
    }
    state = STATE_IDLE;   // need another startReceive()
  }

  if (state != STATE_RX) {
    armRecv();   // continuous RX, or re-arm the CAD window in power-save mode
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

bool RadioLibWrapper::isChannelActive() {
  return _threshold == 0 
          ? false    // interference check is disabled
          : getCurrentRSSI() > _noise_floor + _threshold;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};
  
float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;
  
  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}
