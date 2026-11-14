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
  virtual void loop() {};
  // Returns true when a BLE companion app is connected. Default delegates to
  // isConnected(); MultiSerialInterface asks its Bluetooth interfaces only.
  virtual bool isBLEConnected() const { return isConnected(); }
  // True when a companion app is actually connected over *any* transport
  // (BLE bonded or a host holding the USB-CDC port open). Distinct from
  // isConnected(), which a USB serial port reports true even with nothing on
  // the other end (it can't tell). Used for app-connected UI (Auto buzzer
  // mute, message-wake). Default = isConnected().
  virtual bool isClientConnected() const { return isConnected(); }

  // For a loop that sleeps between passes: frames waiting to be read or sent.
  // An interface that can't tell says true (poll it often).
  virtual bool hasPendingFrames() const { return true; }

  virtual bool isWriteBusy() const = 0;
  // Several links at once (MultiSerialInterface): the one a reply goes back to,
  // the link the last command came in on. A single link has nothing to choose.
  virtual int replyTarget() const { return -1; }
  virtual void setReplyTarget(int target) { }
  virtual size_t writeFrame(const uint8_t src[], size_t len) = 0;
  virtual size_t checkRecvFrame(uint8_t dest[]) = 0;
};
