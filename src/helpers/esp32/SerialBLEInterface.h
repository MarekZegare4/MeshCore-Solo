#pragma once

#include "../BaseSerialInterface.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// A frame from the app was queued (the BLE stack's task). Weak, empty by
// default: a firmware whose loop blocks between passes defines it to wake it.
void serialFrameArrived();

class SerialBLEInterface : public BaseSerialInterface, BLESecurityCallbacks, BLEServerCallbacks, BLECharacteristicCallbacks {
  BLEServer *pServer;
  BLEService *pService;
  BLECharacteristic * pTxCharacteristic;
  bool deviceConnected;
  bool oldDeviceConnected;
  bool _isEnabled;
  uint16_t last_conn_id;
  uint32_t _pin_code;
  unsigned long _last_write;
  unsigned long adv_restart_time;
  unsigned long adv_slow_time;   // when fast advertising drops to the slow interval (0 = not pending)
  unsigned long adv_check_time;  // next look at whether it's still findable
  unsigned long conn_since;      // a link up but not yet paired: since when (0 = none)

  struct Frame {
    uint8_t len;
    uint8_t buf[MAX_FRAME_SIZE];
  };

  #define FRAME_QUEUE_SIZE  4
  StaticQueue_t recv_queue_state;
  uint8_t recv_queue_storage[FRAME_QUEUE_SIZE * sizeof(Frame)];
  QueueHandle_t recv_queue;
  int send_queue_len;
  Frame send_queue[FRAME_QUEUE_SIZE];

  void clearBuffers();
  void startAdvertising(bool fast);

protected:
  // BLESecurityCallbacks methods
  uint32_t onPassKeyRequest() override;
  void onPassKeyNotify(uint32_t pass_key) override;
  bool onConfirmPIN(uint32_t pass_key) override;
  bool onSecurityRequest() override;
#if defined(CONFIG_NIMBLE_ENABLED)   // Arduino-ESP32 3.x on NimBLE (ESP32-S3 default)
  void onAuthenticationComplete(ble_gap_conn_desc* desc, int status) override;
#else
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override;
#endif

  // BLEServerCallbacks methods
  void onConnect(BLEServer* pServer) override;
#if defined(CONFIG_NIMBLE_ENABLED)
  void onConnect(BLEServer* pServer, ble_gap_conn_desc* desc) override;
  void onMtuChanged(BLEServer* pServer, ble_gap_conn_desc* desc, uint16_t mtu) override;
#else
  void onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) override;
  void onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) override;
#endif
  void onDisconnect(BLEServer* pServer) override;

  // BLECharacteristicCallbacks methods
#if defined(CONFIG_NIMBLE_ENABLED)
  void onWrite(BLECharacteristic* pCharacteristic, ble_gap_conn_desc* desc) override;
#else
  void onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) override;
#endif
  void authDone(bool ok);

public:
  SerialBLEInterface() {
    pServer = NULL;
    pService = NULL;
    deviceConnected = false;
    oldDeviceConnected = false;
    adv_restart_time = 0;
    adv_slow_time = 0;
    adv_check_time = conn_since = 0;
    _isEnabled = false;
    _last_write = 0;
    last_conn_id = 0;
    recv_queue = xQueueCreateStatic(
      FRAME_QUEUE_SIZE, sizeof(Frame), recv_queue_storage, &recv_queue_state
    );
    send_queue_len = 0;
  }

  /**
   * init the BLE interface.
   * @param prefix   a prefix for the device name
   * @param name  IN/OUT - a name for the device (combined with prefix). If "@@MAC", is modified and returned
   * @param pin_code   the BLE security pin
   */
  void begin(const char* prefix, char* name, uint32_t pin_code);

  // BaseSerialInterface methods
  void enable() override;
  void disable() override;
  bool isEnabled() const override { return _isEnabled; }

  bool isConnected() const override;

  bool hasPendingFrames() const override { return send_queue_len > 0 || uxQueueMessagesWaiting(recv_queue) > 0; }
  bool isWriteBusy() const override;
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;
};

#if BLE_DEBUG_LOGGING && ARDUINO
  #include <Arduino.h>
  #define BLE_DEBUG_PRINT(F, ...) Serial.printf("BLE: " F, ##__VA_ARGS__)
  #define BLE_DEBUG_PRINTLN(F, ...) Serial.printf("BLE: " F "\n", ##__VA_ARGS__)
#else
  #define BLE_DEBUG_PRINT(...) {}
  #define BLE_DEBUG_PRINTLN(...) {}
#endif
