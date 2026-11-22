#pragma once

#include <Arduino.h>
#include <Wire.h>

// A 9-DOF pair on I2C: ST's LSM6DS3TR-C (accelerometer + gyro) and LIS3MDL
// (magnetometer), as on Adafruit's 9-DOF boards. Register-level, no
// libraries: the accelerometer at 104 Hz +-2 g, the magnetometer at 80 Hz
// +-4 gauss in its ultra-high-performance mode; the gyro left off. Each
// read() is one burst per chip, in the chip's own axes (their orientation
// on the board is the variant's business: IMU_AXES below).
//
// begin() probes both addresses of each (SA0/SDO high or low) and checks
// WHO_AM_I, so a board without one runs on with ok() false.
class Lsm6Lis3Imu {
public:
  struct Raw { float ax, ay, az;    // m/s2; at rest it reads up (+9.8 on the axis pointing up)
               float mx, my, mz; }; // uT

  bool begin(TwoWire& w = Wire) {
    _w = &w;
    _acc = probe(0x6A, 0x6B, 0x0F, 0x6A);   // LSM6DS3TR-C
    _mag = probe(0x1C, 0x1E, 0x0F, 0x3D);   // LIS3MDL
    if (_acc) {
      write(_acc, 0x12, 0x44);   // CTRL3_C: BDU, register auto-increment
      write(_acc, 0x10, 0x40);   // CTRL1_XL: 104 Hz, +-2 g
      write(_acc, 0x11, 0x00);   // CTRL2_G: gyro off
    }
    if (_mag) {
      write(_mag, 0x20, 0x7C);   // CTRL_REG1: X/Y ultra-high performance, 80 Hz
      write(_mag, 0x21, 0x00);   // CTRL_REG2: +-4 gauss
      write(_mag, 0x23, 0x0C);   // CTRL_REG4: Z ultra-high performance
      write(_mag, 0x22, 0x00);   // CTRL_REG3: continuous
    }
    return ok();
  }
  bool ok() const { return _acc && _mag; }
  uint8_t accAddr() const { return _acc; }
  uint8_t magAddr() const { return _mag; }

  bool read(Raw& r) {
    if (!ok()) return false;
    int16_t a[3], m[3];
    if (!burst(_acc, 0x28, a)) return false;           // OUTX_L_XL..
    if (!burst(_mag, 0x28 | 0x80, m)) return false;    // OUT_X_L.., MSB: auto-increment
    const float ga = 0.061e-3f * 9.80665f;             // m/s2 per LSB at +-2 g
    const float gm = 100.0f / 6842.0f;                 // uT per LSB at +-4 gauss
    r.ax = a[0] * ga; r.ay = a[1] * ga; r.az = a[2] * ga;
    r.mx = m[0] * gm; r.my = m[1] * gm; r.mz = m[2] * gm;
    return true;
  }

private:
  TwoWire* _w = nullptr;
  uint8_t _acc = 0, _mag = 0;

  uint8_t probe(uint8_t a1, uint8_t a2, uint8_t reg, uint8_t who) {
    for (uint8_t a : { a1, a2 }) {
      uint8_t v = 0;
      if (readReg(a, reg, v) && v == who) return a;
    }
    return 0;
  }
  bool readReg(uint8_t a, uint8_t reg, uint8_t& v) {
    _w->beginTransmission(a);
    _w->write(reg);
    if (_w->endTransmission(false) != 0) return false;
    if (_w->requestFrom(a, (uint8_t)1) != 1) return false;
    v = _w->read();
    return true;
  }
  void write(uint8_t a, uint8_t reg, uint8_t v) {
    _w->beginTransmission(a);
    _w->write(reg);
    _w->write(v);
    _w->endTransmission();
  }
  bool burst(uint8_t a, uint8_t reg, int16_t* out) {
    _w->beginTransmission(a);
    _w->write(reg);
    if (_w->endTransmission(false) != 0) return false;
    if (_w->requestFrom(a, (uint8_t)6) != 6) return false;
    for (int i = 0; i < 3; i++) {
      const uint8_t lo = _w->read(), hi = _w->read();
      out[i] = (int16_t)(lo | (hi << 8));
    }
    return true;
  }
};
