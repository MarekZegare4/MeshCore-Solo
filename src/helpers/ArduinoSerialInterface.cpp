#include "ArduinoSerialInterface.h"
#include "StreamUtils.h"

// Worst-case time we'll let writeFrame() block waiting for the host to drain
// the link, before giving up on the rest of the frame. See StreamUtils.h.
#define SERIAL_WRITE_TIMEOUT_MS  300

#define RECV_STATE_IDLE        0
#define RECV_STATE_HDR_FOUND   1
#define RECV_STATE_LEN1_FOUND  2
#define RECV_STATE_LEN2_FOUND  3

void ArduinoSerialInterface::enable() { 
  _isEnabled = true;
  _state = RECV_STATE_IDLE;
}
void ArduinoSerialInterface::disable() {
  _isEnabled = false;
}

bool ArduinoSerialInterface::isConnected() const { 
  return true;   // no way of knowing, so assume yes
}

bool ArduinoSerialInterface::isClientConnected() const {
#if defined(NRF52_PLATFORM) || (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0 && defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT == 1)
  // (bool)Serial: the host has the CDC port open (DTR) -- a serial monitor
  // counts too, charging alone doesn't.
  return _serial == (Stream*)&Serial && (bool)Serial;
#else
  // A UART bridge: (bool)Serial is just "begun", always true.
  return false;
#endif
}

bool ArduinoSerialInterface::isWriteBusy() const {
  return false;
}

size_t ArduinoSerialInterface::writeFrame(const uint8_t src[], size_t len) {
  if (len > MAX_FRAME_SIZE) {
    // frame is too big!
    return 0;
  }

  // A native USB port with no host holding it open: the core drops the bytes
  // (nRF52 TinyUSB, ESP32 USBCDC) or lets its buffer fill, and either way
  // streamWriteUntil() would spin out the whole timeout on every frame -- the
  // loop stalled 300 ms per packet heard while no phone was connected. A UART
  // bridge's Serial always reads true, so those boards are unaffected.
  if (_serial == (Stream*)&Serial && !Serial) return 0;

  uint8_t hdr[3];
  hdr[0] = '>';
  hdr[1] = (len & 0xFF);  // LSB
  hdr[2] = (len >> 8);    // MSB

  // Single deadline shared across header + payload, so a stalled host costs
  // at most SERIAL_WRITE_TIMEOUT_MS total, not that much per write() call.
  uint32_t deadline = millis() + SERIAL_WRITE_TIMEOUT_MS;
#if defined(ESP32) && ARDUINO_USB_MODE && ARDUINO_USB_CDC_ON_BOOT
  if (_serial == (Stream*)&Serial) {
    // The ESP32-S3's USB Serial/JTAG (HWCDC as Serial: the Heltec V4) moves
    // the bytes to its FIFO 64 at a time and never sends a zero-length packet:
    // a frame whose last packet came out full stayed with the host until the
    // next frame (tools/hwtest.py: CLI replies of 67 and 123 bytes never
    // arrived). Pieces of 63 bytes, each sent before the next, are all short
    // packets, which end a transfer by themselves. About 1 ms a piece.
    uint8_t buf[3 + MAX_FRAME_SIZE];
    memcpy(buf, hdr, 3);
    memcpy(buf + 3, src, len);
    size_t sent = 0, total = 3 + len;
    while (sent < total) {
      size_t k = total - sent > 63 ? 63 : total - sent;
      if (streamWriteUntil(*_serial, buf + sent, k, deadline) < k) break;
      Serial.flush();   // until the ISR has it in the FIFO
      sent += k;
    }
    return sent < 3 ? 0 : sent - 3;
  }
#endif
  if (streamWriteUntil(*_serial, hdr, 3, deadline) < 3) return 0;
  return streamWriteUntil(*_serial, src, len, deadline);
}

size_t ArduinoSerialInterface::checkRecvFrame(uint8_t dest[]) {
  while (_serial->available()) {
    int c = _serial->read();
    if (c < 0) break;

    switch (_state) {
      case RECV_STATE_IDLE:
        if (c == '<') {
          _state = RECV_STATE_HDR_FOUND;
        }
        break;
      case RECV_STATE_HDR_FOUND:
        _frame_len = (uint8_t)c;   // LSB
        _state = RECV_STATE_LEN1_FOUND;
        break;
      case RECV_STATE_LEN1_FOUND:
        _frame_len |= ((uint16_t)c) << 8;   // MSB
        rx_len = 0;
        _state = _frame_len > 0 ? RECV_STATE_LEN2_FOUND : RECV_STATE_IDLE;
        break;
      default:
        if (rx_len < MAX_FRAME_SIZE) {
          rx_buf[rx_len] = (uint8_t)c;   // rest of frame will be discarded if > MAX
        }
        rx_len++;
        if (rx_len >= _frame_len) {  // received a complete frame?
          if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;    // truncate
          memcpy(dest, rx_buf, _frame_len);
          _state = RECV_STATE_IDLE;  // reset state, for next frame
          return _frame_len;
        }
    }
  }
  return 0;
}
