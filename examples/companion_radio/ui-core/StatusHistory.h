#pragma once
// The Status history, shared by both UIs: the noise floor, the battery and
// the mesh traffic, one sample every SAMPLE_MS into a ring of N, and the SNR
// of each packet heard (the last 32). ui-new draws it on its Status tabs,
// ui-lvgl in Diagnostics > History. The UI loop calls tick() on every pass
// with the readings; it keeps its own pace.

#include <stdint.h>

namespace uicore {

// A ring of the last N samples, oldest first.
template <int N>
struct History {
  int16_t v[N];
  uint8_t n = 0, head = 0;
  void push(int16_t x) { v[head] = x; head = (head + 1) % N; if (n < N) n++; }
  int16_t at(int i) const { return v[(head + N - n + i) % N]; }   // 0 = oldest
  static constexpr int size() { return N; }
};

template <int N, unsigned long SAMPLE_MS>
struct StatusHistory {
  static constexpr int SAMPLES = N;
  static constexpr unsigned long INTERVAL_MS = SAMPLE_MS;

  History<N> noise;     // dBm
  History<N> batt;      // mV
  History<N> traffic;   // packets received in each interval
  History<32> snr;      // dB x4, a sample a packet
  uint32_t version = 0; // counts every change, for a view to see one

  // `noise_dbm` 0 = no reading (the sample is skipped); `total_rx` counts
  // every packet received since boot; `last_snr` is the last packet's.
  void tick(unsigned long now, int noise_dbm, int batt_mv, uint32_t total_rx, float last_snr) {
    if (total_rx != _snr_rx) { snr.push((int16_t)(last_snr * 4)); _snr_rx = total_rx; version++; }
    if (_next && (long)(now - _next) < 0) return;
    _next = now + SAMPLE_MS;
    if (noise_dbm) noise.push((int16_t)noise_dbm);
    batt.push((int16_t)batt_mv);
    if (_last_rx || total_rx) traffic.push((int16_t)(total_rx - _last_rx > 30000 ? 30000 : total_rx - _last_rx));
    _last_rx = total_rx;
    version++;
  }
  // After the packet counters are reset: the next interval counts from 0.
  void countersReset() { _last_rx = 0; _snr_rx = 0; }

private:
  unsigned long _next = 0;
  uint32_t _last_rx = 0, _snr_rx = 0;
};

}  // namespace uicore
