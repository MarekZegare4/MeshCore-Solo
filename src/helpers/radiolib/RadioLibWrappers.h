#pragma once

#include <Mesh.h>
#include <RadioLib.h>

class RadioLibWrapper : public mesh::Radio {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  uint32_t n_recv, n_sent, n_recv_errors;
  int16_t _noise_floor, _threshold;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;
  uint8_t _preamble_sf;

  // Periodic noise-floor recalibration while RX duty-cycle power-save is
  // active: the frontend is off for most of a duty cycle, so samples taken
  // there aren't meaningful and loop() skips them entirely (see loop()) —
  // meaning _noise_floor would otherwise freeze at whatever it was when
  // power-save turned on, silently breaking int.thresh interference
  // detection. Instead, drop to plain continuous RX for one window every
  // NF_CALIB_INTERVAL_MS, run the normal sampling loop, then re-arm the
  // duty-cycle once a fresh average is published.
  bool     _nf_calib_active = false;
  uint32_t _nf_last_calib_ms = 0;
  uint32_t _nf_calib_deadline_ms = 0;   // abort the window if it can't complete (busy channel)
  void noiseFloorCalibCheck();

  void idle();
  void startRecv();
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;
  virtual void doResetAGC();

  // Power-save RX: hardware SX126x RX duty-cycle (SetRxDutyCycle). Instead of a
  // continuous receive the chip itself cycles RX↔sleep, latches a preamble, then
  // stays in RX to receive the packet (RX_DONE on DIO1) — no MCU state machine,
  // average RX current cut several-fold. Driven from armRecv()/loop(); falls back
  // to continuous RX if the modem doesn't support it.
  bool _power_save = false;
  bool _ps_active = false;       // is the radio currently armed in duty-cycle mode
  int8_t _tx_dbm = 0;            // last TX power applied (tracks APC's live value)
  void armRecv();                // arm RX: duty-cycle in power-save, else continuous
  // Arm the hardware RX duty-cycle. Base returns UNSUPPORTED → armRecv() falls
  // back to continuous RX; SX126x overrides with startReceiveDutyCycleAuto().
  virtual int16_t startPowerSaveRecv() { return RADIOLIB_ERR_UNSUPPORTED; }

  // RX duty-cycle watchdog: the chip's own sequencer cycles RX<->sleep with no
  // MCU polling, so if it desyncs (a known SX126x failure mode) nothing else
  // would notice. Healthy operation shows up as the hardware BUSY pin
  // toggling as the chip moves through its cycle; if that stops for too long,
  // first try a cheap soft re-arm, then a full chip reset.
  bool     _wd_last_busy = false;
  uint32_t _wd_last_transition_ms = 0;
  uint8_t  _wd_stage = 0;         // 0 = healthy / not yet tried, 1 = soft re-arm already attempted this stall
  uint32_t _wd_soft_count = 0, _wd_hard_count = 0;
  void rxPsWatchdogCheck();
  // Re-attach the packet-received/duty-cycle-done interrupt action. Exposed so
  // radioHardReset() overrides (a different translation unit) can redo this
  // binding after a fresh begin(), without duplicating the static ISR here.
  void reattachRecvAction();

  // Overridden by radios that support the watchdog (SX126x only today, since
  // it's the only one with a working startPowerSaveRecv()). Default false so
  // the watchdog never runs where isChipBusy()/radioHardReset() aren't real.
  virtual bool supportsRxPsWatchdog() const { return false; }
  // True while the chip can't service SPI (duty-cycle sleep window, or
  // briefly mid-command) — radios expose this via the hardware BUSY pin.
  virtual bool isChipBusy() { return false; }
  // Full chip reset + re-init after a stuck duty-cycle a soft re-arm didn't
  // clear. Returns false if unsupported (base default: no-op). Implementations
  // must reapply any runtime radio state a fresh init would reset to compiled
  // firmware defaults (frequency/bandwidth/SF/CR/TX power/preamble/gain).
  virtual bool radioHardReset() { return false; }

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : _radio(&radio), _board(&board), _preamble_sf(0) { n_recv = n_sent = 0; }

  void begin() override;
  // Enable/disable hardware duty-cycle RX. Takes effect on the next RX re-arm
  // (loop() re-arms once the live mode differs from this request).
  void setPowerSaving(bool en) { _power_save = en; }
  bool getPowerSaving() const { return _power_save; }
  virtual void powerOff() { _radio->sleep(); }
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  bool isChannelActive();

  bool isReceiving() override {
    if (isReceivingPacket()) return true;

    return isChannelActive();
  }

  virtual void setParams(float freq, float bw, uint8_t sf, uint8_t cr) = 0;
  // RadioLib's own setFrequency() silently rejects values outside the chip's
  // validated range and leaves the radio retuned to its previous frequency —
  // setParams() above doesn't check that return code, so the UI clamps to this
  // instead of letting NodePrefs drift out of sync with the actual radio.
  // Default is the generic sanity bound the app's CMD_SET_RADIO_PARAMS already
  // uses; chips with a narrower RadioLib-validated range override it.
  virtual void getFreqBounds(float& min_mhz, float& max_mhz) const { min_mhz = 150.0f; max_mhz = 2500.0f; }
  uint32_t getRngSeed();
  void setTxPower(int8_t dbm);
  int8_t getTxPower() const { return _tx_dbm; }   // actual current power (reflects APC)

  virtual float getCurrentRSSI() =0;
  virtual uint8_t getSpreadingFactor() const { return LORA_SF; }
  static uint16_t preambleLengthForSF(uint8_t sf) { return sf <= 8 ? 32 : 16; }
  // Approx SNR demod floor per SF (Semtech): SF7 -7.5 dB … SF12 -20 dB, -2.5 dB/SF.
  // Single source for both packetScore() and the APC link-margin target.
  static float snrFloorForSF(uint8_t sf) {
    if (sf < 7) sf = 7; else if (sf > 12) sf = 12;
    return -7.5f - 2.5f * (float)(sf - 7);
  }
  void updatePreamble(uint8_t sf) { _preamble_sf = sf; _radio->setPreambleLength(preambleLengthForSF(sf)); }

  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void resetAGC() override;

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  uint32_t getRxPsWatchdogSoftCount() const { return _wd_soft_count; }
  uint32_t getRxPsWatchdogHardCount() const { return _wd_hard_count; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; _wd_soft_count = _wd_hard_count = 0; }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10

  virtual void setRxBoostedGainMode(bool) { }
  virtual bool getRxBoostedGainMode() const { return false; }
};

/**
 * \brief  an RNG impl using the noise from the LoRa radio as entropy.
 *         NOTE: this is VERY SLOW!  Use only for things like creating new LocalIdentity
*/
class RadioNoiseListener : public mesh::RNG {
  PhysicalLayer* _radio;
public:
  RadioNoiseListener(PhysicalLayer& radio): _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
  }
};
