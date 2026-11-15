#pragma once

#include <Dispatcher.h>
#include <MeshCore.h>   // MAX_TRANS_UNIT
#include <ctime>
#include <cstdlib>
#include <cstring>
#include "SimInstance.h"

// mesh::Radio implementation for the native sim build. Mirrors the
// FakeRadio in test/test_kiss_modem/test_tx_backpressure.cpp in spirit
// (always-succeed send, no real RF) but is written directly against the
// REAL mesh::Radio interface in src/Dispatcher.h -- that test mock is for a
// different, out-of-date mocked Mesh.h (see the Phase-1 plan) and must not
// be copied.
//
// Phase 1/2 had exactly one logical device, so there was nothing to
// actually exchange packets with: recvRaw() always reported "nothing
// received", startSendRaw()/isSendComplete() always reported success
// instantly. Phase 3 adds a real in-memory "ether": a bounded FIFO of whole
// raw packets in each direction, drained/filled by the JS-facing functions
// at the bottom of this file. Dispatcher::checkRecv()/checkSend() only ever
// deal in whole packets (recvRaw() returns 0-or-a-whole-packet in one call;
// startSendRaw() is handed one whole packet to send) -- see
// src/Dispatcher.cpp -- so queueing whole packets (not a byte stream)
// matches that contract exactly, no framing/reassembly needed on either side.
class SimRadio : public mesh::Radio {
  uint32_t n_recv = 0, n_sent = 0, n_recv_errors = 0;
  uint8_t _power_save = 0;
  bool _rx_boosted_gain = false;
  int8_t _tx_dbm = 0;

  // A "clean, high-quality" fake link by default -- packetScore() below is
  // already a flat 100.0, these back getLastRSSI()/getLastSNR() (read by
  // Dispatcher for scoring/logging and by MyMesh for the advert path's SNR
  // display) with plausible non-zero numbers instead of the base class's
  // default 0/0.
  float _last_snr = 40.0f;   // Packet::_snr stores this * 4 as an int8_t (see Dispatcher.cpp)
  float _last_rssi = -60.0f;

  struct QueuedPacket {
    uint8_t data[MAX_TRANS_UNIT];
    int len = 0;
  };
  static const int QUEUE_CAP = 16;
  QueuedPacket _tx_queue[QUEUE_CAP];
  int _tx_head = 0, _tx_count = 0;
  QueuedPacket _rx_queue[QUEUE_CAP];
  int _rx_head = 0, _rx_count = 0;

public:
  void begin() override { }

  int recvRaw(uint8_t* bytes, int sz) override {
    if (_rx_count == 0) return 0;
    QueuedPacket& p = _rx_queue[_rx_head];
    int n = p.len < sz ? p.len : sz;
    memcpy(bytes, p.data, n);
    _rx_head = (_rx_head + 1) % QUEUE_CAP;
    _rx_count--;
    n_recv++;
    return n;
  }

  uint32_t getEstAirtimeFor(int len_bytes) override {
    // Rough LoRa-ish estimate so anything that logs/uses airtime for
    // scheduling doesn't see nonsense; not calibrated to any real profile.
    return (uint32_t)(len_bytes * 3 + 50);
  }

  float packetScore(float snr, int packet_len) override {
    return 100.0f;  // pretend every packet we'd send is a clean, high-quality one
  }

  bool startSendRaw(const uint8_t* bytes, int len) override {
    n_sent++;
    if (len > 0) {
      int n = len > MAX_TRANS_UNIT ? MAX_TRANS_UNIT : len;
      if (_tx_count == QUEUE_CAP) {
        // Nobody (no JS ether tick) is draining the outbox -- true for the
        // Phase 1/2 single-instance builds, since nothing there ever polls
        // sim_radio_poll_tx(). Drop the oldest queued TX rather than growing
        // unboundedly; a long-running single-instance sim just silently
        // "transmits into the void" exactly as it always did pre-Phase-3.
        _tx_head = (_tx_head + 1) % QUEUE_CAP;
        _tx_count--;
      }
      int idx = (_tx_head + _tx_count) % QUEUE_CAP;
      memcpy(_tx_queue[idx].data, bytes, n);
      _tx_queue[idx].len = n;
      _tx_count++;
    }
    return true;    // instantly "succeeds" -- matches every real RadioLib wrapper's fire-and-forget startSendRaw()
  }

  bool isSendComplete() override { return true; }
  void onSendFinished() override { }

  bool isInRecvMode() const override { return true; }

  float getLastRSSI() const override { return _last_rssi; }
  float getLastSNR() const override { return _last_snr; }

  // --- Extra methods below (not part of mesh::Radio) -------------------
  // MyMesh.cpp/DataStore.cpp/the Settings/Diagnostics UI screens call these
  // directly on the concrete radio_driver object on every real board, the
  // same way they'd call them on a RadioLibWrapper subclass (see
  // src/helpers/radiolib/RadioLibWrappers.h, which every one of these
  // mirrors). No real chip underneath, so these just report plausible
  // static/no-op values.

  uint32_t getRngSeed() {
    // sim_instance_salt(): see SimInstance.h -- without it, two module
    // instances of the same compiled binary started in the same browser
    // tick could plausibly compute the exact same seed here (same
    // time(NULL) second, same `rand()` process state, often the same
    // `this` address across independent-but-identically-laid-out linear
    // memories) and end up with correlated "random" behaviour.
    // sim_instance_entropy(): same reasoning, but for two genuinely
    // different browser tabs/machines both booting a same-tagged instance
    // (e.g. two different visitors' 'hero') -- salt alone is a pure
    // function of the tag string, identical for both; this mixes in real
    // crypto.getRandomValues()-sourced entropy from the host page. See
    // SimInstance.h and SimRNG.h for the full story (found while testing
    // meshcore-solo-site's cross-visitor relay bridge).
    return (uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)this ^ (uint32_t)rand() ^ sim_instance_salt() ^ sim_instance_entropy();
  }

  void getFreqBounds(float& min_mhz, float& max_mhz) const {
    min_mhz = 150.0f;
    max_mhz = 2500.0f;
  }

  void setParams(float freq, float bw, uint8_t sf, uint8_t cr) { }
  void powerOff() { }

  void setPowerSaving(uint8_t mode) { _power_save = mode; }
  uint8_t getPowerSaving() const { return _power_save; }
  int rxPsSleepPercent(uint8_t mode) { return mode == 1 ? 33 : mode == 2 ? 64 : -1; }   // a device's at SF8 / 62.5 kHz

  void setTxPower(int8_t dbm) { _tx_dbm = dbm; }
  int8_t getTxPower() const { return _tx_dbm; }

  bool setRxBoostedGainMode(bool en) { _rx_boosted_gain = en; return true; }
  bool getRxBoostedGainMode() const { return _rx_boosted_gain; }

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsRecvErrors() const { return n_recv_errors; }
  uint32_t getPacketsSent() const { return n_sent; }
  uint32_t getRxPsWatchdogSoftCount() const { return 0; }
  uint32_t getRxPsWatchdogHardCount() const { return 0; }
  void resetStats() { n_recv = n_sent = n_recv_errors = 0; }

  static float snrFloorForSF(uint8_t sf) {
    if (sf < 7) sf = 7; else if (sf > 12) sf = 12;
    return -7.5f - 2.5f * (float)(sf - 7);
  }

  // --- Ether hooks (Phase 3) ---------------------------------------------
  // Called from the JS-facing extern "C" wrappers below (and reusable from
  // a native test harness, since neither depends on Emscripten). These are
  // the ONLY way bytes cross between two SimRadio instances -- there is no
  // shared C++ state between module instances, on purpose (see the plan's
  // "never run two logical devices in one process" decision).

  // Pop one queued outbound packet (FIFO) into `out`, truncated to
  // `max_len`. Returns bytes written, or 0 if nothing is queued. A JS ether
  // tick calls this once per instance per tick to drain whatever this
  // device tried to transmit since the last tick.
  int pollTx(uint8_t* out, int max_len) {
    if (_tx_count == 0) return 0;
    QueuedPacket& p = _tx_queue[_tx_head];
    int n = p.len < max_len ? p.len : max_len;
    memcpy(out, p.data, n);
    _tx_head = (_tx_head + 1) % QUEUE_CAP;
    _tx_count--;
    return n;
  }

  // Push one raw packet into this device's inbox for recvRaw() to pick up
  // on Dispatcher's next checkRecv() poll. Returns false (no-op) if `len`
  // is out of range or the inbox is already full (oldest entry dropped to
  // make room rather than blocking -- a real radio would just drop an
  // over-the-air packet it couldn't buffer either).
  bool injectRx(const uint8_t* data, int len) {
    if (len <= 0 || len > MAX_TRANS_UNIT) return false;
    if (_rx_count == QUEUE_CAP) {
      _rx_head = (_rx_head + 1) % QUEUE_CAP;
      _rx_count--;
      n_recv_errors++;
    }
    int idx = (_rx_head + _rx_count) % QUEUE_CAP;
    memcpy(_rx_queue[idx].data, data, len);
    _rx_queue[idx].len = len;
    _rx_count++;
    return true;
  }
};

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// JS-facing ether bridge. `radio_driver` is a file-scope global defined in
// variants/sim/target.cpp (one instance per compiled module -- see
// target.h's `extern SimRadio radio_driver;`), so these two functions
// always operate on THIS module instance's own radio, never any other's.
// Because -sMODULARIZE=1 -sEXPORT_NAME=MeshCoreSim gives every
// MeshCoreSim() call its own independent Module/globals/linear memory
// (verified empirically for this phase, not just assumed from the build
// flags -- see the Phase 3 report), calling instanceA.ccall('sim_radio_poll_tx', ...)
// and instanceB.ccall('sim_radio_poll_tx', ...) really do reach two
// separate SimRadio objects with no way to cross-talk except through
// whatever the host page's ether loop explicitly wires together by
// shuttling bytes from one instance's poll_tx into another's inject_rx.
//
// `inline` (not just EMSCRIPTEN_KEEPALIVE'd) because this header is
// included from several .cpp translation units (via target.h) -- without
// it, each would emit its own non-inline definition and the link would
// fail with duplicate symbols, same reasoning as sim_fs_mount_idbfs() in
// SimFS.h.
extern SimRadio radio_driver;

extern "C" inline EMSCRIPTEN_KEEPALIVE int sim_radio_poll_tx(uint8_t* out_buf, int max_len) {
  return radio_driver.pollTx(out_buf, max_len);
}
extern "C" inline EMSCRIPTEN_KEEPALIVE void sim_radio_inject_rx(const uint8_t* data, int len) {
  radio_driver.injectRx(data, len);
}
#endif
