"""
Companion-protocol frames over USB serial, shared by the host tools here
(console.py, route.py). The same frames the phone app and the Solo site's USB
tools send: '<' len_lo len_hi payload to the device, '>' len_lo len_hi payload
back. Anything else on the line (log output, a GPX export) is skipped.
"""

import struct
import sys
import time

import serial
import serial.tools.list_ports

FRAME_OUT = 0x3C   # '<'
FRAME_IN = 0x3E    # '>'
RESP_OK = 0
RESP_ERR = 1

ERRORS = {
    1: "not supported by this firmware",
    2: "not found",
    3: "full",
    4: "bad state",
    5: "file error on the device",
    6: "bad argument",
}


class DeviceError(Exception):
    pass


def find_port():
    """The first port that looks like a Solo device (Seeed / Espressif / M5 USB), else any ACM/usbmodem."""
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = f"{p.description} {p.manufacturer or ''}".lower()
        if any(k in desc for k in ("wio", "seeed", "espressif", "m5stack", "heltec", "tracker")):
            return p.device
    for p in ports:
        if "ACM" in p.device or "usbmodem" in p.device or p.device.startswith("COM"):
            return p.device
    return None


class Companion:
    def __init__(self, port=None, baud=115200, timeout=4.0):
        port = port or find_port()
        if not port:
            sys.exit("No device found. Plug it in, or name the port with --port.")
        self.port = port
        self.timeout = timeout
        self.ser = serial.Serial(port, baud, timeout=0.2)
        self.ser.dtr = True
        self.ser.rts = True
        time.sleep(0.2)
        self.ser.reset_input_buffer()
        self._rx = bytearray()

    def close(self):
        self.ser.close()

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    def send(self, payload: bytes):
        self.ser.write(bytes([FRAME_OUT]) + struct.pack("<H", len(payload)) + payload)
        self.ser.flush()

    def read_frame(self, timeout=None):
        """The next frame's payload, or None on timeout."""
        end = time.time() + (timeout if timeout is not None else self.timeout)
        while time.time() < end:
            i = self._rx.find(bytes([FRAME_IN]))
            if i < 0:
                self._rx.clear()
            else:
                del self._rx[:i]
                if len(self._rx) >= 3:
                    n = self._rx[1] | (self._rx[2] << 8)
                    if n == 0 or n > 300:
                        del self._rx[:1]
                        continue
                    if len(self._rx) >= 3 + n:
                        f = bytes(self._rx[3:3 + n])
                        del self._rx[:3 + n]
                        return f
            chunk = self.ser.read(max(1, self.ser.in_waiting))   # what's there, not a full timeout for 256
            if chunk:
                self._rx.extend(chunk)
        return None

    def ask(self, payload: bytes, code: int, timeout=None) -> bytes:
        """Send a request, wait for the frame starting with `code`; an error frame raises."""
        self.send(payload)
        end = time.time() + (timeout if timeout is not None else self.timeout)
        while time.time() < end:
            f = self.read_frame(end - time.time())
            if f is None:
                break
            if f[0] == code:
                return f
            if f[0] == RESP_ERR and len(f) <= 2:
                err = f[1] if len(f) > 1 else 0
                raise DeviceError(ERRORS.get(err, f"error {err}"))
            # anything else (a push) is not ours: keep waiting
        raise DeviceError("no answer from the device")
