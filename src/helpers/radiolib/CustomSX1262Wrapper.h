#pragma once

#include "CustomSX1262.h"
#include "RadioLibWrappers.h"
#include "SX126xReset.h"

#ifndef USE_SX1262
#define USE_SX1262
#endif

class CustomSX1262Wrapper : public RadioLibWrapper {
  // Cached runtime radio params, only used to recover from a hard reset (see
  // radioHardReset()): std_init() re-applies the compiled LORA_FREQ/BW/SF/CR
  // firmware defaults, not whatever the user has actually configured, so
  // these are needed to restore real state afterwards.
  float   _wd_freq = 0, _wd_bw = 0;
  uint8_t _wd_sf = 0, _wd_cr = 0;
  bool    _wd_params_valid = false;
  bool    _wd_rx_boosted_gain = false;

public:
  CustomSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) override {
    _wd_freq = freq; _wd_bw = bw; _wd_sf = sf; _wd_cr = cr; _wd_params_valid = true;
    ((CustomSX1262 *)_radio)->setFrequency(freq);
    ((CustomSX1262 *)_radio)->setSpreadingFactor(sf);
    ((CustomSX1262 *)_radio)->setBandwidth(bw);
    ((CustomSX1262 *)_radio)->setCodingRate(cr);
    updatePreamble(sf);
    PacketMillis pm = calcMaxPacketMillis(sf, bw, cr, preambleLengthForSF(sf));
    ((CustomSX1262 *)_radio)->setPreambleMillis(pm.preambleMillis);
    ((CustomSX1262 *)_radio)->setMaxPayloadMillis(pm.payloadMillis);
  }

  // From RadioLib's SX1262::setFrequency(): RADIOLIB_CHECK_RANGE(freq, 150.0f, 960.0f, ...).
  void getFreqBounds(float& min_mhz, float& max_mhz) const override { min_mhz = 150.0f; max_mhz = 960.0f; }

  bool isReceivingPacket() override { 
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1262 *)_radio)->getRSSI(false);
  }
  float getLastRSSI() const override { return ((CustomSX1262 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1262 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSX1262 *)_radio)->spreadingFactor; }
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }

  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio); }

  // Power-save RX = hardware RX duty-cycle (SX126x SetRxDutyCycle, datasheet
  // 13.1.7). The chip's sequencer cycles RX↔sleep on its own, latches a preamble
  // of the configured length and then stays in RX to receive the packet, raising
  // RX_DONE on DIO1 — handled by the normal recvRaw() path, no MCU polling.
  // minSymbols=8 is the reliable preamble-latch count for SF7-12. If the
  // configured preamble is too short for a real duty-cycle (senderPreamble <
  // 2*minSymbols+1), RadioLib transparently falls back to a continuous receive.
  int16_t startPowerSaveRecv() override {
    return ((SX126x *)_radio)->startReceiveDutyCycleAuto(preambleLengthForSF(_preamble_sf), 8);
  }

  bool setRxBoostedGainMode(bool en) override {
    _wd_rx_boosted_gain = en;
    return ((CustomSX1262 *)_radio)->setRxBoostedGainMode(en) == RADIOLIB_ERR_NONE;
  }
  bool getRxBoostedGainMode() const override {
    return ((CustomSX1262 *)_radio)->getRxBoostedGainMode();
  }

  bool supportsRxPsWatchdog() const override { return true; }

  // BUSY is high whenever the chip can't service SPI -- including the sleep
  // window of an armed RX duty-cycle. Same access pattern already used by
  // sx126xResetAGC() in SX126xReset.h.
  bool isChipBusy() override {
    SX126x* radio = (SX126x *)_radio;
    return radio->mod->hal->digitalRead(radio->mod->getGpio());
  }

  // Full chip reset + re-init after a stuck RX duty-cycle that a soft re-arm
  // didn't clear. std_init() re-applies compiled firmware defaults, not the
  // user's runtime settings, so reapply the cached params and re-attach the
  // packet-received action once it returns.
  bool radioHardReset() override {
    if (!((CustomSX1262 *)_radio)->std_init(&SPI)) return false;
    reattachRecvAction();
    if (_wd_params_valid) {
      ((CustomSX1262 *)_radio)->setFrequency(_wd_freq);
      ((CustomSX1262 *)_radio)->setSpreadingFactor(_wd_sf);
      ((CustomSX1262 *)_radio)->setBandwidth(_wd_bw);
      ((CustomSX1262 *)_radio)->setCodingRate(_wd_cr);
      updatePreamble(_wd_sf);
    }
    _radio->setOutputPower(getTxPower());
    // Unconditional: std_init() may have just turned boosted gain back ON via
    // the board's SX126X_RX_BOOSTED_GAIN compile default, so the OFF case
    // needs reapplying just as much as ON.
    ((CustomSX1262 *)_radio)->setRxBoostedGainMode(_wd_rx_boosted_gain);
    return true;
  }
};
