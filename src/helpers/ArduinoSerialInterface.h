#pragma once

#include "BaseSerialInterface.h"
#include <Arduino.h>

#ifdef SOLO_TEST_HOOKS
struct SerialTxStats { uint32_t frames, cut, slow, max_ms; };   // slow: over 50 ms
extern SerialTxStats g_serial_tx;
#endif

class ArduinoSerialInterface : public BaseSerialInterface {
  bool _isEnabled;
  uint8_t _state;
  uint16_t _frame_len;
  uint16_t rx_len;
  Stream* _serial;
  uint8_t rx_buf[MAX_FRAME_SIZE];

public:
  ArduinoSerialInterface() { _isEnabled = false; _state = 0; }

  void begin(Stream& serial) { 
    _serial = &serial; 
  #ifdef RAK_4631
    pinMode(WB_IO2, OUTPUT);
  #endif  
  }

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;
  // A host holding the port open: native USB tells (DTR), a UART bridge
  // can't, so it never counts as an app being there.
  bool isClientConnected() const override;

  // Bytes from the host waiting (a frame is written out whole, nothing is
  // queued to send). The base's "can't tell" (true) kept a sleeping loop
  // (UI_LOOP_IDLES) at 2 ms naps whenever an app was connected, on any link.
  bool hasPendingFrames() const override { return _isEnabled && _serial && _serial->available() > 0; }

  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};