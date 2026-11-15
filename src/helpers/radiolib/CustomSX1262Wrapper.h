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
    idle();   // re-armed by the next recvRaw(): a duty cycle needs its periods for these settings
  }

  // From RadioLib's SX1262::setFrequency(): RADIOLIB_CHECK_RANGE(freq, 150.0f, 960.0f, ...).
  void getFreqBounds(float& min_mhz, float& max_mhz) const override { min_mhz = 150.0f; max_mhz = 960.0f; }

  // Duty-cycling, BUSY high is the chip asleep: asking it anything would wake
  // it deaf, and it can't be hearing a packet then anyway.
  bool isReceivingPacket() override {
    if (_ps_active && isChipBusy()) return false;
    return ((CustomSX1262 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    if (_ps_active && isChipBusy()) return _noise_floor;
    return ((CustomSX1262 *)_radio)->getRSSI(false);
  }
  float readLiveRSSI() const override { return ((CustomSX1262 *)_radio)->getRSSI(); }
  float readLiveSNR() const override { return ((CustomSX1262 *)_radio)->getSNR(); }

  float packetScore(float snr, int packet_len) override {
    int sf = ((CustomSX1262 *)_radio)->spreadingFactor;
    return packetScoreInt(snr, sf, packet_len);
  }
  uint8_t getSpreadingFactor() const override { return ((CustomSX1262 *)_radio)->spreadingFactor; }
  virtual void powerOff() override {
    ((CustomSX1262 *)_radio)->sleep(false);
  }

  // Power-save RX = hardware RX duty-cycle (SX126x SetRxDutyCycle, datasheet
  // 13.1.7): the chip's sequencer cycles RX<->sleep on its own, latches a
  // preamble and then stays in RX for the packet, raising RX_DONE on DIO1 --
  // the normal recvRaw() path. Periods from rxPsGeometry(); the driver takes
  // the TCXO start-up + 1 ms off the sleep. Where no duty cycle fits (SF too
  // low for the preamble), continuous RX.
  int16_t startPowerSaveRecv(uint8_t preamble) override {
    CustomSX1262* r = (CustomSX1262 *)_radio;
    uint32_t rx_us, sleep_us;
    if (!_wd_params_valid ||
        !rxPsGeometry(_wd_sf, _wd_bw, preamble, r->tcxoDelayUs() + 1000, rx_us, sleep_us))
      return RADIOLIB_ERR_INVALID_SLEEP_PERIOD;
    return r->startReceiveDutyCycle(rx_us, sleep_us);
  }
  int rxPsSleepPercent(uint8_t mode) override {
    uint32_t rx_us, sleep_us;
    if (!mode || !_wd_params_valid ||
        !rxPsGeometry(_wd_sf, _wd_bw, rxPsPreamble(mode), ((CustomSX1262 *)_radio)->tcxoDelayUs() + 1000, rx_us, sleep_us))
      return -1;
    return (int)(100 * sleep_us / (rx_us + sleep_us));
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
    setTxPower(getTxPower());   // re-run through the wrapper's own setter so any PA gain curve reapplies
    // Unconditional: std_init() may have just turned boosted gain back ON via
    // the board's SX126X_RX_BOOSTED_GAIN compile default, so the OFF case
    // needs reapplying just as much as ON.
    ((CustomSX1262 *)_radio)->setRxBoostedGainMode(_wd_rx_boosted_gain);
    return true;
  }
  void doResetAGC() override { sx126xResetAGC((SX126x *)_radio, getRxBoostedGainMode()); }
};
