#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <helpers/BaseChatMesh.h>   // MAX_TEXT_LEN
#include <helpers/TxtDataHelpers.h>  // TXT_TYPE_*
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"
#include "MyMesh.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    advertReceivedFlood,
    advertReceivedZeroHop,
    ack
};

// What every on-device UI (ui-new / ui-orig / ui-tiny) offers main.cpp and
// MyMesh: lifecycle, the companion-link state, board/serial helpers, and the
// object MyMesh should talk to (meshListener()).
class UITaskBase {
protected:
  mesh::MainBoard* _board;
  BaseSerialInterface* _serial;
  bool _connected;

  UITaskBase(mesh::MainBoard* board, BaseSerialInterface* serial) : _board(board), _serial(serial) {
    _connected = false;
  }

  // hasConnection() means "a BLE companion app is connected". Not isConnected()
  // (a dual interface hardcodes that true as a USB send-fallback). Drives the BT
  // status indicator, pairing PIN, and the GPX-export collision warning — all
  // BLE-specific. The Auto buzzer mute / message-wake use isClientConnected()
  // (BLE *or* an open USB port) directly instead. Each UI calls this at the top
  // of its loop() (MyMesh::loop() used to push it at the end of its own).
  void pollConnection() { setHasConnection(_serial->isBLEConnected()); }

public:
  void setHasConnection(bool connected) {
    bool prev = _connected;
    _connected = connected;
    if (prev && !connected) onBLEDisconnected();
  }
  bool hasConnection() const { return _connected; }
  virtual void onBLEDisconnected() {}
  // True only when a BLE central is actually bonded/connected. On a dual
  // (BLE+USB) interface hasConnection() is always true (USB counts), so use
  // this for BLE-specific UI like the pairing-PIN prompt.
  bool isBLEConnected() const { return _serial->isBLEConnected(); }
  // True when a companion app is connected over any transport (BLE bonded or an
  // open USB-CDC port). For app-connected behaviour like Auto buzzer mute.
  bool isClientConnected() const { return _serial->isClientConnected(); }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isSerialEnabled() const { return _serial->isEnabled(); }
  void enableSerial() { _serial->enable(); the_mesh.rememberBle(true); }
  void disableSerial() { _serial->disable(); the_mesh.rememberBle(false); }
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  // Single choke point for every controlled power-down (low-battery auto-off,
  // long-press power-off, and every board.reboot() caller too): flush
  // prefs/RTC/contacts/trail before the board actually goes down or restarts,
  // so no exit path can silently skip a pending write. restart=true reboots,
  // false powers off. Every UI variant (ui-new/ui-tiny/ui-orig) implements
  // this the same way -- see each's UITask::shutdown().
  virtual void shutdown(bool restart = false) = 0;
  virtual void loop() = 0;

  // What main.cpp hands to MyMesh::setListener(): the UI itself (ui-orig /
  // ui-tiny, via AbstractUITask below) or the object it delegates mesh events
  // to (ui-new: the UI Core).
  virtual MyMesh::Listener* meshListener() = 0;
};

// A UI that is MyMesh's Listener itself (ui-orig / ui-tiny). The glue MyMesh
// used to run for the UI -- which message types show on screen, how a room post
// is labelled, which notification plays -- lives here, so each of those UIs
// keeps receiving the same newMsg()/addDMMsg()/notify() calls it always did.
// (ui-new instead hands MyMesh the UI Core, which does the same job.)
class AbstractUITask : public MyMesh::Listener, public UITaskBase {
protected:
  int  _queue_len = 0;   // last onQueueSizeChanged() -- newMsg()'s msgcount

  AbstractUITask(mesh::MainBoard* board, BaseSerialInterface* serial) : UITaskBase(board, serial) { }

public:
  // An end-to-end ACK (CRC) arrived for one of our sent messages — drives the
  // DM delivery-status marker. Default no-op for UIs that don't track it.
  virtual void onMsgAck(uint32_t ack_crc) { (void)ack_crc; }
  virtual void msgRead(int msgcount) = 0;
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t contact_type = 0, const uint8_t* pub_key = nullptr) = 0;
  MyMesh::Listener* meshListener() override { return this; }

  // ---- MyMesh::Listener ----
  void onQueueSizeChanged(int msgcount) override {
    _queue_len = msgcount;
    msgRead(msgcount);   // only acts on 0 (queue drained by the app) in every UI
  }
  void onACKRecv(uint32_t ack_crc) override { onMsgAck(ack_crc); }
  void onAdvertHeard(bool was_flood) override {
    notify(was_flood ? UIEventType::advertReceivedFlood : UIEventType::advertReceivedZeroHop);
  }
  bool requestShutdown(bool restart) override { shutdown(restart); return true; }

  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override {
    onMessageRecvEx(pkt, from, txt_type, sender_timestamp, nullptr, 0, text);
  }
  void onMessageRecvEx(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp,
                       const uint8_t* extra, int extra_len, const char* text) override {
    // we only want to show text messages on display, not cli data
    if (!(txt_type == TXT_TYPE_PLAIN || txt_type == TXT_TYPE_SIGNED_PLAIN)) return;
    uint8_t path_len = pkt->isRouteFlood() ? pkt->path_len : 0xFF;
    newMsg(path_len, from.name, text, _queue_len, from.type, from.id.pub_key);
    notify(from.type == ADV_TYPE_ROOM ? UIEventType::roomMessage : UIEventType::contactMessage);
    // Add to the on-device conversation history. Room servers (ADV_TYPE_ROOM) are
    // viewed through the same history list as chat contacts (keyed by the server's
    // pubkey), so their posts must be stored too — otherwise an incoming room
    // message fires the notification and reaches the app via the offline queue but
    // never shows when the room is opened directly on the device.
    if (from.type == ADV_TYPE_CHAT) {
      addDMMsg(from.id.pub_key, false, text, sender_timestamp, 0, 0, 0, pkt->path, (uint8_t)pkt->path_len);
    } else if (from.type == ADV_TYPE_ROOM) {
      // A room carries many guests, so prefix the post with its author so the UI
      // can attribute each line. The signed message's `extra` holds the sender's
      // pubkey prefix; resolve it to a contact name, falling back to a short hex.
      char labeled[MAX_TEXT_LEN + 40];  // room text + "Sender: " (history store truncates)
      if (extra && extra_len >= 4) {
        ContactInfo* sc = the_mesh.lookupContactByPubKey(extra, extra_len);
        if (sc && sc->name[0])
          snprintf(labeled, sizeof(labeled), "%s: %s", sc->name, text);
        else
          snprintf(labeled, sizeof(labeled), "%02X%02X: %s", extra[0], extra[1], text);
      } else {
        snprintf(labeled, sizeof(labeled), "%s", text);
      }
      addDMMsg(from.id.pub_key, false, labeled, sender_timestamp, 0, 0, 0, pkt->path, (uint8_t)pkt->path_len);
    }
  }

  // Upstream-shaped entry point without the channel slot: MyMesh itself always
  // calls the Ex variant below, so this only serves a caller that doesn't know
  // the slot -- show and notify, but there's no history ring to file it under.
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override {
    notify(UIEventType::channelMessage);
    newMsg(pkt->isRouteFlood() ? pkt->path_len : 0xFF, channel_details.name, text, _queue_len, 0);
  }
  void onChannelMessageRecvEx(mesh::Packet *pkt, uint8_t channel_idx, ChannelDetails& channel_details,
                              uint32_t timestamp, const char* text) override {
    addChannelMsg(channel_idx, text, timestamp, pkt->path, (uint8_t)pkt->path_len);
    notify(UIEventType::channelMessage);
    newMsg(pkt->isRouteFlood() ? pkt->path_len : 0xFF, channel_details.name, text, _queue_len, 0);
  }
};
