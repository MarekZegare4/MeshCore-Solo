#pragma once

#include "BaseSerialInterface.h"

#ifndef MAX_INTERFACES
  // ble, usb, wifi, ethernet
  #define MAX_INTERFACES 4
#endif

enum class InterfaceType : uint8_t {
  NONE,
  Bluetooth,
  USB,
  WiFi,
  Ethernet,
  HardwareSerial
};

class MultiSerialInterface : public BaseSerialInterface {
private:

  struct RegisteredInterface {
    InterfaceType type = InterfaceType::NONE;
    BaseSerialInterface* instance = nullptr;
  };

  bool _enabled = false;
  RegisteredInterface _interfaces[MAX_INTERFACES] = {};
  int _reply_to = -1;   // the interface the last command came in on

  // A frame to all of them or to one: true when it went out whole.
  bool writeTo(int i, const uint8_t src[], size_t len) {
    return _interfaces[i].instance->writeFrame(src, len) == len;
  }
  bool usable(int i) const {
    return i >= 0 && i < MAX_INTERFACES && _interfaces[i].instance && _interfaces[i].instance->isEnabled();
  }

public:
  bool addInterface(InterfaceType type, BaseSerialInterface* iface) {
    // make sure an interface was provided
    if(iface == nullptr){
      return false;
    }

    // put it in the first free slot
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance == nullptr){
        _interfaces[i].instance = iface;
        _interfaces[i].type = type;
        return true;
      }
    }

    // no free slots available
    return false;
  }

  bool removeInterface(BaseSerialInterface* iface) {
    // make sure an interface was provided
    if(iface == nullptr){
      return false;
    }

    // find and remove interface
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance == iface){
        _interfaces[i] = {};
        if(_reply_to == i) _reply_to = -1;
        return true;
      }
    }

    // interface not found
    return false;
  }

  void enableBluetooth() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        iface.instance->enable();
      }
    }
  }

  void disableBluetooth() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        iface.instance->disable();
      }
    }
  }

  bool isBluetoothEnabled() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        return iface.instance->isEnabled();
      }
    }
    return false; 
  }

  // enable all interfaces
  void enable() override {
    _enabled = true;
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->enable();
      }
    }
  }

  // disable all interfaces
  void disable() override {
    _enabled = false;
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->disable();
      }
    }
  }

  bool isEnabled() const override { 
    return _enabled; 
  }

  bool isConnected() const override {
    // not connected when disabled
    if(!_enabled){
      return false;
    }
    
    // check if any interface is connected
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isConnected()) {
        return true;
      }
    }

    // nothing connected
    return false;
  }

  // A companion app on Bluetooth (the pairing PIN, the BT indicator).
  bool isBLEConnected() const override {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth && iface.instance->isEnabled() && iface.instance->isConnected()){
        return true;
      }
    }
    return false;
  }

  // A companion app on any of them (each says how it knows).
  bool isClientConnected() const override {
    if(!_enabled){
      return false;
    }
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isEnabled() && iface.instance->isClientConnected()){
        return true;
      }
    }
    return false;
  }

  bool hasPendingFrames() const override {
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isEnabled() && iface.instance->hasPendingFrames()){
        return true;
      }
    }
    return false;
  }

  // loop all interfaces
  void loop() override {
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->loop();
      }
    }
  }

  bool isWriteBusy() const override {
    // not busy when disabled
    if(!_enabled){
      return false;
    }

    // check if any interface is busy
    for(auto iface : _interfaces){
      if(iface.instance && iface.instance->isEnabled() && iface.instance->isWriteBusy()){
        return true;
      }
    }

    // nothing busy
    return false;
  }

  int replyTarget() const override { return _reply_to; }
  void setReplyTarget(int target) override { _reply_to = target; }

  // A reply (a code below 0x80) goes back only to the link whose command it
  // answers, so an app on Bluetooth and a tool on USB don't read each other's
  // replies; a push (0x80 and up: a new message, an advert, an ACK) goes to
  // all of them. With no command seen yet, or that link turned off, to all.
  size_t writeFrame(const uint8_t src[], size_t len) override {
    // don't write when disabled or nothing provided
    if(!_enabled || len == 0){
      return 0;
    }

    if(src[0] < 0x80 && usable(_reply_to)){
      return writeTo(_reply_to, src, len) ? len : 0;
    }

    // write frame to all enabled interfaces
    bool allSuccessful = true;
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(usable(i) && !writeTo(i, src, len)){
        allSuccessful = false;
      }
    }

    // report success if all writes completed successfully
    return allSuccessful ? len : 0; 
  }

  size_t checkRecvFrame(uint8_t dest[]) override {
    // don't read when disabled
    if(!_enabled){
      return 0;
    }

    // try to read a frame from any enabled interface
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(usable(i)){
        size_t frameSize = _interfaces[i].instance->checkRecvFrame(dest);
        if(frameSize > 0){
          _reply_to = i;
          return frameSize; 
        }
      }
    }

    // no frame received
    return 0;
  }

};
