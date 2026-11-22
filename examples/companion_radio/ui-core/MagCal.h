#pragma once
// A magnetometer calibrated on the device, and the heading from it.
//
// Calibration: the device turned every way while the readings go in (add());
// they lie on an ellipsoid -- shifted by the board's own magnetism (hard iron:
// b) and squashed by metal near it (soft iron: A). fit() finds that ellipsoid
// (a least-squares quadric, the readings centred and scaled first so the
// normal equations stay well conditioned) and the correction that turns it
// back into a sphere:  corrected = A * (raw - b),  |corrected| = the field.
// The same model as an offline ellipsoid fit (teslabs.com/articles/
// magnetometer-calibration); here a 9 x 9 linear solve and a 3 x 3 symmetric
// square root, no libraries.
//
// While collecting, where the readings point (from a rough centre, the middle
// of their min / max) fills a cube map of 6 x 3 x 3 cells -- the coverage the
// screen shows, and what fit() asks for: readings from one plane only (turned
// flat on a table) can't tell the ellipsoid's third axis.
//
// heading(): tilt compensated, from the corrected field and the accelerometer
// (which way is down), for a forward axis in the same frame.
//
// Standalone header: no MeshCore or display dependencies.

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <algorithm>

namespace magcal {

struct Vec { float x, y, z; };

// The correction: corrected = A * (raw - b). Identity / zero when unset.
struct Cal {
  float A[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
  float b[3] = { 0, 0, 0 };
  float field = 0;   // |corrected| the fit aimed at (uT): the local field

  Vec apply(Vec r) const {
    const float d[3] = { r.x - b[0], r.y - b[1], r.z - b[2] };
    return { A[0][0] * d[0] + A[0][1] * d[1] + A[0][2] * d[2],
             A[1][0] * d[0] + A[1][1] * d[1] + A[1][2] * d[2],
             A[2][0] * d[0] + A[2][1] * d[1] + A[2][2] * d[2] };
  }
};

// Heading in degrees 0..360 (0 = magnetic north, 90 = east) of `fwd` (the
// device's forward axis), from the field `m` and the accelerometer `a` (at rest
// it reads up, +1 g against gravity), all in one frame. -1 when undefined (no
// gravity, or the field along it).
inline float heading(Vec m, Vec a, Vec fwd) {
  const float an = sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
  if (an < 1e-3f) return -1;
  const Vec d = { -a.x / an, -a.y / an, -a.z / an };                      // down
  Vec e = { d.y * m.z - d.z * m.y, d.z * m.x - d.x * m.z, d.x * m.y - d.y * m.x };   // east = down x field
  const float en = sqrtf(e.x * e.x + e.y * e.y + e.z * e.z);
  if (en < 1e-6f) return -1;
  e = { e.x / en, e.y / en, e.z / en };
  const Vec n = { e.y * d.z - e.z * d.y, e.z * d.x - e.x * d.z, e.x * d.y - e.y * d.x };   // north = east x down
  const float fe = fwd.x * e.x + fwd.y * e.y + fwd.z * e.z;
  const float fn = fwd.x * n.x + fwd.y * n.y + fwd.z * n.z;
  if (fabsf(fe) < 1e-6f && fabsf(fn) < 1e-6f) return -1;   // pointing straight down / up
  float h = atan2f(fe, fn) * 57.2957795f;
  if (h < 0) h += 360;
  return h;
}

class Collector {
public:
  static constexpr int N = 400;          // readings kept: 4.8 KB
  static constexpr int CELLS = 54;       // 6 faces x 3 x 3
  static constexpr int MIN_POINTS = 60;
  static constexpr int MIN_CELLS = 36;   // two thirds of the sphere
  // A reading this close to the last kept one is skipped (in its units: 3 uT
  // is ~4 degrees of a 50 uT field, ten times the LIS3MDL's noise; 0 keeps all).
  float min_step = 3;

  void reset() { _n = 0; }

  // A reading (uT). False when it wasn't kept (too close to the last, or full).
  bool add(Vec r) {
    if (_n >= N) return false;
    if (_n > 0 && min_step > 0) {
      const Vec& l = _p[_n - 1];
      const float dx = r.x - l.x, dy = r.y - l.y, dz = r.z - l.z;
      if (dx * dx + dy * dy + dz * dz < min_step * min_step) return false;
    }
    _p[_n++] = r;
    if (_n == 1) { _lo = _hi = r; }
    else {
      _lo = { fminf(_lo.x, r.x), fminf(_lo.y, r.y), fminf(_lo.z, r.z) };
      _hi = { fmaxf(_hi.x, r.x), fmaxf(_hi.y, r.y), fmaxf(_hi.z, r.z) };
    }
    return true;
  }

  int count() const { return _n; }
  const Vec& point(int i) const { return _p[i]; }
  Vec roughCentre() const {
    return { (_lo.x + _hi.x) / 2, (_lo.y + _hi.y) / 2, (_lo.z + _hi.z) / 2 };
  }

  // Which cube-map cells hold a reading (seen from the rough centre); returns
  // how many. `cells` (CELLS bytes, optional): 1 for each one filled.
  int coverage(uint8_t* cells = nullptr) const {
    uint8_t c[CELLS] = {};
    const Vec o = roughCentre();
    for (int i = 0; i < _n; i++) c[cellOf(dir(_p[i], o))] = 1;
    int k = 0;
    for (int i = 0; i < CELLS; i++) k += c[i];
    if (cells) memcpy(cells, c, CELLS);
    return k;
  }
  // The cell a direction (unit vector) falls in: face 0..5 (+x -x +y -y +z -z), 3 x 3 on it.
  static int cellOf(Vec d) {
    const float ax = fabsf(d.x), ay = fabsf(d.y), az = fabsf(d.z);
    int face;
    float u, v, m;
    if (ax >= ay && ax >= az) { face = d.x > 0 ? 0 : 1; u = d.y; v = d.z; m = ax; }
    else if (ay >= az)        { face = d.y > 0 ? 2 : 3; u = d.x; v = d.z; m = ay; }
    else                      { face = d.z > 0 ? 4 : 5; u = d.x; v = d.y; m = az; }
    auto third = [](float t) { return t < -1.0f / 3 ? 0 : t > 1.0f / 3 ? 2 : 1; };
    return face * 9 + third(u / m) * 3 + third(v / m);
  }

  // Enough to try a fit.
  bool ready() const { return _n >= MIN_POINTS && coverage() >= MIN_CELLS; }

  // The fit, into `out`; `rms` (optional): how far the corrected readings stray
  // from the sphere, as a share of its radius (0.02: 2 %). Fitted twice: the
  // tenth straying most from the first fit (a phone or a watch passing by,
  // a cable swinging) is left out of the second, and of its rms. False when
  // it isn't an ellipsoid (too few or too flat readings). `once`: the first
  // fit only, over every reading (as an offline fit would be).
  bool fit(Cal& out, float* rms = nullptr, bool once = false) const {
    if (!fitSome(out, nullptr)) return false;
    uint8_t keep[N];
    memset(keep, 1, sizeof(keep));
    if (!once) trimmedRefit(out, keep);
    if (rms) {
      double sum = 0;
      int k = 0;
      for (int i = 0; i < _n; i++) {
        if (!keep[i]) continue;
        const double d = stray(out, _p[i]);
        sum += d * d;
        k++;
      }
      *rms = k ? (float)sqrt(sum / k) : 1;
    }
    return true;
  }

private:
  Vec _p[N];
  int _n = 0;
  Vec _lo = { 0, 0, 0 }, _hi = { 0, 0, 0 };

  // The tenth straying most from `out` left out (`keep`), and `out` fitted
  // again without them.
  void trimmedRefit(Cal& out, uint8_t* keep) const {
    float e[N], sorted[N];
    for (int i = 0; i < _n; i++) sorted[i] = e[i] = fabsf((float)stray(out, _p[i]));
    std::nth_element(sorted, sorted + (_n * 9) / 10, sorted + _n);
    const float cut = sorted[(_n * 9) / 10];
    for (int i = 0; i < _n; i++) keep[i] = e[i] <= cut;
    Cal second;
    if (fitSome(second, keep)) out = second;
    else memset(keep, 1, N);
  }

  // A reading's distance from the fitted sphere, as a share of its radius.
  static double stray(const Cal& c, Vec p) {
    const Vec q = c.apply(p);
    return sqrt((double)q.x * q.x + (double)q.y * q.y + (double)q.z * q.z) / c.field - 1;
  }

  // The fit of the readings `keep` marks (nullptr: all).
  bool fitSome(Cal& out, const uint8_t* keep) const {
    if (_n < 10) return false;
    const Vec c0 = roughCentre();
    double s = fmax(fmax(_hi.x - _lo.x, _hi.y - _lo.y), _hi.z - _lo.z) / 2;
    if (s <= 0) return false;
    // a x2 + b y2 + c z2 + 2d yz + 2e xz + 2f xy + 2g x + 2h y + 2i z = 1
    double NM[9][10] = {};
    for (int k = 0; k < _n; k++) {
      if (keep && !keep[k]) continue;
      const double x = (_p[k].x - c0.x) / s, y = (_p[k].y - c0.y) / s, z = (_p[k].z - c0.z) / s;
      const double r[9] = { x * x, y * y, z * z, 2 * y * z, 2 * x * z, 2 * x * y, 2 * x, 2 * y, 2 * z };
      for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 9; j++) NM[i][j] += r[i] * r[j];
        NM[i][9] += r[i];
      }
    }
    double p[9];
    if (!solve9(NM, p)) return false;
    double M[3][3] = { { p[0], p[5], p[4] }, { p[5], p[1], p[3] }, { p[4], p[3], p[2] } };
    double Mi[3][3];
    if (!inv3(M, Mi)) return false;
    const double n[3] = { p[6], p[7], p[8] };
    double bu[3];
    for (int i = 0; i < 3; i++) bu[i] = -(Mi[i][0] * n[0] + Mi[i][1] * n[1] + Mi[i][2] * n[2]);
    double k = 1;
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) k += bu[i] * M[i][j] * bu[j];
    if (k <= 0) return false;
    // (raw - b)^T Q (raw - b) = 1, Q = M / (k s^2)
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) M[i][j] /= k * s * s;
    double V[3][3], w[3];
    eigSym3(M, V, w);
    if (w[0] <= 0 || w[1] <= 0 || w[2] <= 0) return false;   // not an ellipsoid
    const double r = pow(w[0] * w[1] * w[2], -1.0 / 6);      // the mean radius: kept as the field
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) {
        double a = 0;
        for (int l = 0; l < 3; l++) a += V[i][l] * sqrt(w[l]) * V[j][l];
        out.A[i][j] = (float)(r * a);
      }
    out.b[0] = (float)(c0.x + s * bu[0]);
    out.b[1] = (float)(c0.y + s * bu[1]);
    out.b[2] = (float)(c0.z + s * bu[2]);
    out.field = (float)r;
    return true;
  }

  static Vec dir(Vec p, Vec c) {
    const float x = p.x - c.x, y = p.y - c.y, z = p.z - c.z;
    const float l = sqrtf(x * x + y * y + z * z);
    if (l < 1e-6f) return { 1, 0, 0 };
    return { x / l, y / l, z / l };
  }

  // Gaussian elimination with partial pivoting on [A | b].
  static bool solve9(double (&a)[9][10], double* x) {
    for (int c = 0; c < 9; c++) {
      int piv = c;
      for (int r = c + 1; r < 9; r++) if (fabs(a[r][c]) > fabs(a[piv][c])) piv = r;
      if (fabs(a[piv][c]) < 1e-12) return false;
      if (piv != c) for (int j = 0; j < 10; j++) { const double t = a[c][j]; a[c][j] = a[piv][j]; a[piv][j] = t; }
      for (int r = c + 1; r < 9; r++) {
        const double f = a[r][c] / a[c][c];
        for (int j = c; j < 10; j++) a[r][j] -= f * a[c][j];
      }
    }
    for (int r = 8; r >= 0; r--) {
      double s = a[r][9];
      for (int j = r + 1; j < 9; j++) s -= a[r][j] * x[j];
      x[r] = s / a[r][r];
    }
    return true;
  }

  static bool inv3(const double (&m)[3][3], double (&o)[3][3]) {
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
                     - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                     + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-12) return false;
    o[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    o[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    o[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    o[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    o[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    o[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    o[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    o[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    o[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return true;
  }

  // Jacobi rotations: m = V diag(w) V^T, eigenvectors in V's columns.
  static void eigSym3(const double (&m)[3][3], double (&V)[3][3], double (&w)[3]) {
    double a[3][3];
    memcpy(a, m, sizeof(a));
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) V[i][j] = i == j;
    for (int sweep = 0; sweep < 50; sweep++) {
      const double off = fabs(a[0][1]) + fabs(a[0][2]) + fabs(a[1][2]);
      if (off < 1e-15) break;
      for (int p = 0; p < 2; p++)
        for (int q = p + 1; q < 3; q++) {
          if (fabs(a[p][q]) < 1e-18) continue;
          const double th = (a[q][q] - a[p][p]) / (2 * a[p][q]);
          const double t = (th >= 0 ? 1 : -1) / (fabs(th) + sqrt(th * th + 1));
          const double c = 1 / sqrt(t * t + 1), s = t * c;
          for (int k = 0; k < 3; k++) {   // a = J^T a J
            const double akp = a[k][p], akq = a[k][q];
            a[k][p] = c * akp - s * akq;
            a[k][q] = s * akp + c * akq;
          }
          for (int k = 0; k < 3; k++) {
            const double apk = a[p][k], aqk = a[q][k];
            a[p][k] = c * apk - s * aqk;
            a[q][k] = s * apk + c * aqk;
          }
          for (int k = 0; k < 3; k++) {
            const double vkp = V[k][p], vkq = V[k][q];
            V[k][p] = c * vkp - s * vkq;
            V[k][q] = s * vkp + c * vkq;
          }
        }
    }
    for (int i = 0; i < 3; i++) w[i] = a[i][i];
  }
};

}  // namespace magcal
