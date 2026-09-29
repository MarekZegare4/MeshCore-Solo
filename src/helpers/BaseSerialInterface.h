#pragma once

#include <Arduino.h>

#define MAX_FRAME_SIZE  176   // +4 for transport codes (region scoping)

class BaseSerialInterface {
protected:
  BaseSerialInterface() { }

public:
  virtual void enable() = 0;
  virtual void disable() = 0;
  virtual bool isEnabled() const = 0;

  virtual bool isConnected() const = 0;
  // Returns true when a BLE companion app is connected. Default delegates to
  // isConnected(); override in dual-interface wrappers that always report
  // isConnected()=true but still need to distinguish BLE from USB state.
  virtual bool isBLEConnected() const { return isConnected(); }
  // True when a companion app is actually connected over *any* transport
  // (BLE bonded or a host holding the USB-CDC port open). Distinct from
  // isConnected(), which a dual interface hardcodes true as a send fallback.
  // Used for app-connected UI (Auto buzzer mute, message-wake). Default =
  // isConnected() so single-transport interfaces are unchanged.
  virtual bool isClientConnected() const { return isConnected(); }

  // For a loop that sleeps between passes: frames waiting to be read or sent.
  // An interface that can't tell says true (poll it often).
  virtual bool hasPendingFrames() const { return true; }

  virtual bool isWriteBusy() const = 0;
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
