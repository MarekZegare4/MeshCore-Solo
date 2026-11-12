#pragma once

#include <helpers/BaseSerialInterface.h>
#include <string.h>

// BaseSerialInterface stub for the native sim build: no companion app can
// connect over BLE/USB in Phase 1 (there's no real transport), so this
// always reports "not connected, nothing to do". Enough to let MyMesh's
// startInterface()/loop() run unmodified.
class SimSerialInterface : public BaseSerialInterface {
public:
  void begin() { }

  void enable() override { }
  void disable() override { }
  bool isEnabled() const override { return true; }    // so MultiSerialInterface polls it (sim_frame_send)
  bool isConnected() const override { return false; }
  bool isWriteBusy() const override { return false; }
  // Sim tests (sim_frame_send / sim_frame_recv): one frame in, the replies
  // out, for the commands a client sends over USB (the site's Tools page). Not
  // "connected", so no app pushes arrive here.
  size_t writeFrame(const uint8_t src[], size_t len) override {
    if (len > MAX_FRAME_SIZE || _out_len + 2 + len > sizeof(_out)) return 0;
    _out[_out_len++] = len & 0xFF;
    _out[_out_len++] = len >> 8;
    memcpy(&_out[_out_len], src, len);
    _out_len += len;
    return len;
  }
  size_t checkRecvFrame(uint8_t dest[]) override {
    if (!_in_len) return 0;
    size_t n = _in_len;
    memcpy(dest, _in, n);
    _in_len = 0;
    return n;
  }

  bool   queueIn(const uint8_t* f, size_t len) {
    if (_in_len || len == 0 || len > MAX_FRAME_SIZE) return false;
    memcpy(_in, f, len); _in_len = len; return true;
  }
  size_t takeOut(uint8_t* dest, size_t cap) {
    size_t n = _out_len < cap ? _out_len : cap;
    memcpy(dest, _out, n);
    _out_len = 0;
    return n;
  }

private:
  uint8_t _in[MAX_FRAME_SIZE + 1];
  size_t  _in_len = 0;
  uint8_t _out[4096];
  size_t  _out_len = 0;
};
