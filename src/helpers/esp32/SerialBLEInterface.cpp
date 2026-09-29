#include "SerialBLEInterface.h"
#include "esp_mac.h"

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" // UART service UUID
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ADVERT_RESTART_DELAY  1000   // millis

__attribute__((weak)) void serialFrameArrived() {}

// As the nRF52 interface: 20-30 ms for the first 30 s (the app finds it at
// once), then 152.5 ms. Left alone, the library advertises every 30-60 ms
// for as long as nothing is connected. Units of 0.625 ms.
#define ADV_FAST_MIN          32
#define ADV_FAST_MAX          48
#define ADV_SLOW              244
#define ADV_FAST_MS           30000

// While nothing is connected, every few seconds: is it still advertising? A
// NimBLE host reset (controller error, timeout) ends advertising with no
// disconnect to restart it, and it stayed unfindable until a reboot. A link
// that never pairs (the phone's OS connecting on its own, a PIN left
// unanswered) is dropped after a while: it stops advertising too.
#define ADV_CHECK_MS          5000
#define AUTH_TIMEOUT_MS       60000

void SerialBLEInterface::begin(const char* prefix, char* name, uint32_t pin_code) {
  _pin_code = pin_code;

  if (strcmp(name, "@@MAC") == 0) {
    uint8_t addr[8];
    memset(addr, 0, sizeof(addr));
    esp_efuse_mac_get_default(addr);
    sprintf(name, "%02X%02X%02X%02X%02X%02X",    // modify (IN-OUT param)
          addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
  }
  char dev_name[32+16];
  sprintf(dev_name, "%s%s", prefix, name);

  // Create the BLE Device
  BLEDevice::init(dev_name);
  BLEDevice::setSecurityCallbacks(this);
  BLEDevice::setMTU(MAX_FRAME_SIZE);

#if defined(CONFIG_NIMBLE_ENABLED)
  BLESecurity::setPassKey(true, pin_code);   // static PIN, shown by this device
  BLESecurity::setCapability(ESP_IO_CAP_OUT);
  BLESecurity::setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
#else
  BLESecurity  sec;
  sec.setStaticPIN(pin_code);
  sec.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
#endif

  //BLEDevice::setPower(ESP_PWR_LVL_N8);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(this);

  // Create the BLE Service
  pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE takes the encryption / MITM requirement as properties (its
  // setAccessPermissions() is a no-op) and adds the 2902 descriptor itself.
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY |
      BLECharacteristic::PROPERTY_READ_ENC | BLECharacteristic::PROPERTY_READ_AUTHEN);
  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC | BLECharacteristic::PROPERTY_WRITE_AUTHEN);
#else
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->setAccessPermissions(ESP_GATT_PERM_READ_ENC_MITM);
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic * pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setAccessPermissions(ESP_GATT_PERM_WRITE_ENC_MITM);
#endif
  pRxCharacteristic->setCallbacks(this);

  pServer->getAdvertising()->addServiceUUID(SERVICE_UUID);
}

// -------- BLESecurityCallbacks methods

uint32_t SerialBLEInterface::onPassKeyRequest() {
  BLE_DEBUG_PRINTLN("onPassKeyRequest()");
  return _pin_code;
}

void SerialBLEInterface::onPassKeyNotify(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onPassKeyNotify(%u)", pass_key);
}

bool SerialBLEInterface::onConfirmPIN(uint32_t pass_key) {
  BLE_DEBUG_PRINTLN("onConfirmPIN(%u)", pass_key);
  return true;
}

bool SerialBLEInterface::onSecurityRequest() {
  BLE_DEBUG_PRINTLN("onSecurityRequest()");
  return true;  // allow
}

#if defined(CONFIG_NIMBLE_ENABLED)
void SerialBLEInterface::onAuthenticationComplete(ble_gap_conn_desc* desc, int status) {
  (void)desc;
  authDone(status == 0);
}
#else
void SerialBLEInterface::onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
  authDone(cmpl.success);
}
#endif

void SerialBLEInterface::authDone(bool ok) {
  if (ok) {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Success");
    deviceConnected = true;
    conn_since = 0;
  } else {
    BLE_DEBUG_PRINTLN(" - SecurityCallback - Authentication Failure*");

    //pServer->removePeerDevice(pServer->getConnId(), true);
    pServer->disconnect(pServer->getConnId());
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- BLEServerCallbacks methods

void SerialBLEInterface::onConnect(BLEServer* pServer) {
}

#if defined(CONFIG_NIMBLE_ENABLED)
void SerialBLEInterface::onConnect(BLEServer* pServer, ble_gap_conn_desc* desc) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_handle=%d", desc->conn_handle);
  last_conn_id = desc->conn_handle;
  conn_since = millis() | 1;
}

void SerialBLEInterface::onMtuChanged(BLEServer* pServer, ble_gap_conn_desc* desc, uint16_t mtu) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", mtu);
}
#else
void SerialBLEInterface::onConnect(BLEServer* pServer, esp_ble_gatts_cb_param_t *param) {
  BLE_DEBUG_PRINTLN("onConnect(), conn_id=%d, mtu=%d", param->connect.conn_id, pServer->getPeerMTU(param->connect.conn_id));
  last_conn_id = param->connect.conn_id;
  conn_since = millis() | 1;
}

void SerialBLEInterface::onMtuChanged(BLEServer* pServer, esp_ble_gatts_cb_param_t* param) {
  BLE_DEBUG_PRINTLN("onMtuChanged(), mtu=%d", pServer->getPeerMTU(param->mtu.conn_id));
}
#endif

void SerialBLEInterface::onDisconnect(BLEServer* pServer) {
  BLE_DEBUG_PRINTLN("onDisconnect()");
  deviceConnected = false;
  conn_since = 0;
  if (_isEnabled) {
    adv_restart_time = millis() + ADVERT_RESTART_DELAY;
  }
}

// -------- BLECharacteristicCallbacks methods

#if defined(CONFIG_NIMBLE_ENABLED)
void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, ble_gap_conn_desc* desc) {
#else
void SerialBLEInterface::onWrite(BLECharacteristic* pCharacteristic, esp_ble_gatts_cb_param_t* param) {
#endif
  uint8_t* rxValue = pCharacteristic->getData();
  int len = pCharacteristic->getLength();

  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("ERROR: onWrite(), frame too big, len=%d", len);
  } else {
    Frame frame = {};
    frame.len = len;
    memcpy(frame.buf, rxValue, len);

    if (xQueueSend(recv_queue, &frame, 0) != pdTRUE) {
      BLE_DEBUG_PRINTLN("ERROR: onWrite(), recv_queue is full!");
    }
    serialFrameArrived();
  }
}

// ---------- public methods

void SerialBLEInterface::clearBuffers() {
  xQueueReset(recv_queue);
  send_queue_len = 0;
}

void SerialBLEInterface::enable() { 
  if (_isEnabled) return;

  _isEnabled = true;
  clearBuffers();

  // Start the service
  pService->start();

  startAdvertising(true);
  adv_restart_time = 0;
}

void SerialBLEInterface::startAdvertising(bool fast) {
  BLEAdvertising* adv = pServer->getAdvertising();
  adv->stop();   // the interval only takes on a (re)start
  adv->setMinInterval(fast ? ADV_FAST_MIN : ADV_SLOW);
  adv->setMaxInterval(fast ? ADV_FAST_MAX : ADV_SLOW);
  adv->start();
  adv_slow_time = fast ? (millis() + ADV_FAST_MS) | 1 : 0;
}

void SerialBLEInterface::disable() {
  _isEnabled = false;

  BLE_DEBUG_PRINTLN("SerialBLEInterface::disable");

  pServer->getAdvertising()->stop();
  pServer->disconnect(last_conn_id);
  pService->stop();
  oldDeviceConnected = deviceConnected = false;
  adv_restart_time = adv_slow_time = conn_since = 0;
}

size_t SerialBLEInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    BLE_DEBUG_PRINTLN("writeFrame(), frame too big, len=%d", len);
    return 0;
  }

  if (deviceConnected && len > 0) {
    if (send_queue_len >= FRAME_QUEUE_SIZE) {
      BLE_DEBUG_PRINTLN("writeFrame(), send_queue is full!");
      return 0;
    }

    send_queue[send_queue_len].len = len;  // add to send queue
    memcpy(send_queue[send_queue_len].buf, src, len);
    send_queue_len++;

    return len;
  }
  return 0;
}

#define  BLE_WRITE_MIN_INTERVAL   60

bool SerialBLEInterface::isWriteBusy() const {
  return millis() < _last_write + BLE_WRITE_MIN_INTERVAL;   // still too soon to start another write?
}

size_t SerialBLEInterface::checkRecvFrame(uint8_t dest[]) {
  if (send_queue_len > 0   // first, check send queue
    && millis() >= _last_write + BLE_WRITE_MIN_INTERVAL    // space the writes apart
  ) {
    _last_write = millis();
    pTxCharacteristic->setValue(send_queue[0].buf, send_queue[0].len);
    pTxCharacteristic->notify();

    BLE_DEBUG_PRINTLN("writeBytes: sz=%d, hdr=%d", (uint32_t)send_queue[0].len, (uint32_t) send_queue[0].buf[0]);

    send_queue_len--;
    for (int i = 0; i < send_queue_len; i++) {   // delete top item from queue
      send_queue[i] = send_queue[i + 1];
    }
  }

  Frame frame;
  if (xQueueReceive(recv_queue, &frame, 0) == pdTRUE) {
    memcpy(dest, frame.buf, frame.len);
    BLE_DEBUG_PRINTLN("readBytes: sz=%d, hdr=%d", (uint32_t) frame.len, (uint32_t) dest[0]);
    return frame.len;
  }

  if (deviceConnected != oldDeviceConnected) {
    if (!deviceConnected) {    // disconnecting
      clearBuffers();

      BLE_DEBUG_PRINTLN("SerialBLEInterface -> disconnecting...");
      adv_restart_time = millis() + ADVERT_RESTART_DELAY;
    } else {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> stopping advertising");
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> connecting...");
      // connecting
      // do stuff here on connecting
      pServer->getAdvertising()->stop();
      adv_restart_time = adv_slow_time = 0;
    }
    oldDeviceConnected = deviceConnected;
  }

  if (adv_restart_time && millis() >= adv_restart_time) {
    if (pServer->getConnectedCount() == 0) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> re-starting advertising");
      startAdvertising(true);  // re-Start advertising
    }
    adv_restart_time = 0;
  }

  if (adv_slow_time && (long)(millis() - adv_slow_time) >= 0) {
    adv_slow_time = 0;
    if (_isEnabled && pServer->getConnectedCount() == 0) startAdvertising(false);
  }

  if (_isEnabled && !deviceConnected && (long)(millis() - adv_check_time) >= 0) {
    adv_check_time = millis() + ADV_CHECK_MS;
    unsigned long since = conn_since;
    if (pServer->getConnectedCount() > 0) {
      if (since && millis() - since > AUTH_TIMEOUT_MS) {
        BLE_DEBUG_PRINTLN("SerialBLEInterface -> dropping a link that never paired");
        conn_since = 0;
        pServer->disconnect(last_conn_id);   // onDisconnect() restarts advertising
      }
    }
#if defined(CONFIG_NIMBLE_ENABLED)
    else if (!adv_restart_time && !pServer->getAdvertising()->isAdvertising()) {
      BLE_DEBUG_PRINTLN("SerialBLEInterface -> advertising had stopped, restarting");
      startAdvertising(true);
    }
#endif
  }
  return 0;
}

bool SerialBLEInterface::isConnected() const {
  return deviceConnected;  //pServer != NULL && pServer->getConnectedCount() > 0;
}
