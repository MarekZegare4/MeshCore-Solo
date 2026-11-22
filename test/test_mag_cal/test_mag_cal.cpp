#include <gtest/gtest.h>
#include <math.h>

// The magnetometer calibration and heading (ui-core/MagCal.h): a synthetic
// cell of known hard / soft iron turned every way, a flat turn that can't be
// fitted, the tracker's real readings against its MATLAB fit, the heading
// with the device tilted.
#include "../../examples/companion_radio/ui-core/MagCal.h"
#include "tracker_acc.h"

using magcal::Vec;

namespace {

struct Rng {
  uint32_t s = 11;
  float u() { s = s * 1103515245u + 12345u; return ((s >> 8) & 0xffff) / 65535.0f; }
  float n() { return (u() + u() + u() + u() - 2) * 1.2f; }   // roughly normal, sd ~1
};

// A field of 50 uT turned every way, seen through soft iron W (symmetric) and
// hard iron h, with noise of `noise` uT.
const float W[3][3] = { { 1.20f, 0.08f, -0.05f }, { 0.08f, 0.85f, 0.04f }, { -0.05f, 0.04f, 1.05f } };
const float H[3] = { -47.0f, 36.5f, -38.9f };   // the tracker's b_mag

Vec distorted(Vec f, Rng& r, float noise) {
  return { W[0][0] * f.x + W[0][1] * f.y + W[0][2] * f.z + H[0] + r.n() * noise,
           W[1][0] * f.x + W[1][1] * f.y + W[1][2] * f.z + H[1] + r.n() * noise,
           W[2][0] * f.x + W[2][1] * f.y + W[2][2] * f.z + H[2] + r.n() * noise };
}

Vec randomDir(Rng& r) {
  while (true) {
    const Vec v = { r.u() * 2 - 1, r.u() * 2 - 1, r.u() * 2 - 1 };
    const float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 0.1f && l <= 1) return { v.x / l, v.y / l, v.z / l };
  }
}

float len(Vec v) { return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); }

TEST(MagCal, TurnedEveryWayGivesASphere) {
  magcal::Collector c;
  Rng r;
  for (int i = 0; i < 3000 && c.count() < magcal::Collector::N; i++) {
    const Vec d = randomDir(r);
    c.add(distorted({ d.x * 50, d.y * 50, d.z * 50 }, r, 0.3f));
  }
  ASSERT_TRUE(c.ready());
  EXPECT_EQ(magcal::Collector::CELLS, c.coverage());
  magcal::Cal cal;
  float rms = 1;
  ASSERT_TRUE(c.fit(cal, &rms));
  EXPECT_LT(rms, 0.02f);
  for (int i = 0; i < 3; i++) EXPECT_NEAR(H[i], cal.b[i], 0.5f) << "b[" << i << "]";
  // A undoes W up to its scale: A W = k I.
  const float k = 50 / cal.field;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      float aw = 0;
      for (int l = 0; l < 3; l++) aw += cal.A[i][l] * W[l][j];
      EXPECT_NEAR(i == j ? 1.0f : 0.0f, aw * k, 0.02f) << "AW[" << i << "][" << j << "]";
    }
  // Corrected readings all the field's length.
  for (int i = 0; i < 20; i++) {
    const Vec d = randomDir(r);
    EXPECT_NEAR(cal.field, len(cal.apply(distorted({ d.x * 50, d.y * 50, d.z * 50 }, r, 0))), cal.field * 0.01f);
  }
}

TEST(MagCal, TurnedFlatIsntEnough) {
  magcal::Collector c;
  Rng r;
  for (int i = 0; i < 360; i += 2) {
    const float a = i * 0.0174533f;
    c.add(distorted({ 20 * cosf(a), 20 * sinf(a), 45 }, r, 0.3f));   // on a table, the field dipping
  }
  EXPECT_GT(c.count(), 30);   // plenty, but all round one circle
  EXPECT_LT(c.coverage(), magcal::Collector::MIN_CELLS);
  EXPECT_FALSE(c.ready());
}

TEST(MagCal, CloseReadingsAreSkipped) {
  magcal::Collector c;
  Rng r;
  for (int i = 0; i < 100; i++) c.add(distorted({ 50, 0, 0 }, r, 0.05f));   // held still
  EXPECT_LE(c.count(), 2);
}

TEST(MagCal, CellsCoverEachFace) {
  EXPECT_EQ(0 * 9 + 4, magcal::Collector::cellOf({ 1, 0, 0 }));   // +x, the middle
  EXPECT_EQ(1 * 9 + 4, magcal::Collector::cellOf({ -1, 0, 0 }));
  EXPECT_EQ(5 * 9 + 4, magcal::Collector::cellOf({ 0, 0, -1 }));
  EXPECT_EQ(0 * 9 + 8, magcal::Collector::cellOf({ 0.8f, 0.6f, 0.6f }));   // a corner of +x
}

// The tracker's accelerometer readings, against its MATLAB script (the
// constrained Li-Griffiths fit, r = 9.81) run on the same fifth of them. On
// all 1000 it gave the firmware's b_acc (-0.0588, 0.0512, 0.3196), diag A_acc
// (0.9898, 0.9945, 0.9961); this fit on all 1000: (-0.0571, 0.0513, 0.3168),
// (0.9887, 0.9940, 0.9953) -- the difference is the subset, not the method.
TEST(MagCal, TheTrackersReadingsMatchItsMatlabFit) {
  magcal::Collector c;
  c.min_step = 0;
  for (const auto& p : TRACKER_ACC) c.add({ p[0], p[1], p[2] });
  magcal::Cal cal;
  float rms = 1;
  ASSERT_TRUE(c.fit(cal, &rms));
  const float b_acc[3] = { -0.0173f, 0.0734f, 0.3066f };
  const float A_acc[3][3] = { { 0.9920f, 0.0036f, 0.0066f }, { 0.0036f, 0.9954f, 0.0017f }, { 0.0066f, 0.0017f, 0.9983f } };
  for (int i = 0; i < 3; i++) EXPECT_NEAR(b_acc[i], cal.b[i], 0.005f) << "b[" << i << "]";
  const float k = 9.81f / cal.field;   // its r, ours the mean radius
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) EXPECT_NEAR(A_acc[i][j], cal.A[i][j] * k, 0.003f) << "A[" << i << "][" << j << "]";
  EXPECT_LT(rms, 0.02f);
}

// North-east-down: the field north and dipping 65 degrees (Poland), gravity down.
TEST(MagHeading, LevelAndTilted) {
  const float dip = 65 * 0.0174533f;
  const Vec m = { 50 * cosf(dip), 0, 50 * sinf(dip) };
  const Vec up = { 0, 0, -9.81f };   // the accelerometer reads up
  EXPECT_NEAR(0, magcal::heading(m, up, { 1, 0, 0 }), 0.1f);
  EXPECT_NEAR(90, magcal::heading(m, up, { 0, 1, 0 }), 0.1f);
  EXPECT_NEAR(270, magcal::heading(m, up, { 0, -1, 0 }), 0.1f);
  EXPECT_NEAR(45, magcal::heading(m, up, { 0.7071f, 0.7071f, 0 }), 0.1f);
  // The device pitched 30 degrees nose up, still facing north: the same heading,
  // where a level-only formula would be thrown by the dip.
  const float t = 30 * 0.0174533f;
  const Vec fwd = { cosf(t), 0, -sinf(t) };
  EXPECT_NEAR(0, magcal::heading(m, up, fwd), 0.1f);
  // Rolled 40 degrees, facing east.
  const float q = 40 * 0.0174533f;
  EXPECT_NEAR(90, magcal::heading(m, up, { 0, cosf(q), sinf(q) }), 0.1f);
  EXPECT_LT(magcal::heading(m, { 0, 0, 0 }, { 1, 0, 0 }), 0);   // no gravity: undefined
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
