#include <gtest/gtest.h>
#include <math.h>

// Learning the battery curve (ui-core/BattLearn.h) from a synthetic cell: a
// known open-circuit curve, a day/night load with the screen on now and then
// (its load pulls the voltage down), ADC noise, the frontends' 8 s smoothed
// reading.
#define BATT_LEARN_TEST
#include "../../examples/companion_radio/ui-core/BattLearn.h"

namespace {

const float QT[]  = { 0, .1f, .2f, .3f, .4f, .5f, .6f, .7f, .8f, .9f, 1 };
const float OCV[] = { 3380, 3680, 3715, 3745, 3778, 3815, 3855, 3915, 3985, 4055, 4180 };
float ocv(float q) {
  if (q <= 0) return OCV[0];
  for (int i = 1; i < 11; i++)
    if (q <= QT[i]) return OCV[i - 1] + (OCV[i] - OCV[i - 1]) * (q - QT[i - 1]) / (QT[i] - QT[i - 1]);
  return OCV[10];
}

struct Cell {
  uint32_t seed = 7;
  float rnd() { seed = seed * 1103515245u + 12345u; return ((seed >> 8) & 0xffff) / 65535.0f; }
};

const int LOW = 3400;

// A run from charge q0 down to the shutdown; `usb_at_h` > 0 plugs it in that
// many hours in, for a minute. Returns finish()'s answer.
bool run(battlearn::Learner& L, float q0, const uint16_t* old, uint8_t runs, uint16_t* out,
         float usb_at_h = 0, float stop_h = 0, uint32_t seed = 7) {
  Cell c; c.seed = seed;
  uint32_t t = 0;
  for (int i = 0; i < 10; i++) { L.tick(t, 4200, true, true); t += 8000; }
  float q = q0;
  uint16_t ema = 0;
  bool scr = false;
  uint32_t scr_until = 0;
  while (true) {
    const float h = t / 3.6e6f, hod = fmodf(h + 8, 24);
    const bool day = hod > 8 && hod < 23;
    if (t >= scr_until) { scr = day && c.rnd() < 0.2f; scr_until = t + 60000; }
    const float I = scr ? 70 : (day ? 22 : 15);   // mA
    q -= I * 8 / 3600.0f / 1500;                  // a 1500 mAh cell
    const float v = ocv(q) - I * 0.6f + (c.rnd() - 0.5f) * 30;
    ema = ema ? (uint16_t)((ema * 4 + (uint16_t)v) / 5) : (uint16_t)v;
    const bool usb = usb_at_h > 0 && h >= usb_at_h && h < usb_at_h + 0.02f;
    L.tick(t, ema, usb, scr);
    if (stop_h > 0 && h >= stop_h) return L.finish(t, LOW, old, runs, out);
    if (ema < LOW) return L.finish(t, LOW, old, runs, out);
    t += 8000;
  }
}

// The cell's own curve at the device: 0 % where the shutdown is.
void truth(uint16_t* c) {
  float qc = 0;
  while (ocv(qc) - 11 < LOW) qc += 0.0005f;
  c[0] = LOW;
  for (int k = 1; k < 11; k++) c[k] = (uint16_t)(ocv(qc + (1 - qc) * k / 10.0f) - 11);
}

const uint16_t LIPO[11] = { 3200, 3440, 3560, 3640, 3680, 3720, 3760, 3790, 3910, 4050, 4170 };

TEST(BattLearn, AFullRunGivesTheCellsCurve) {
  battlearn::Learner L;
  battery::useCurve(LIPO);
  uint16_t out[11], want[11];
  ASSERT_TRUE(run(L, 1.0f, LIPO, 0, out));
  truth(want);
  EXPECT_EQ(out[0], LOW);
  // Within ~60 mV: the load isn't even (screen in the day, none at night) but
  // the run is read as if it were, which moves the steep upper points most.
  for (int k = 1; k < 10; k++) EXPECT_NEAR(out[k], want[k], 60) << "point " << k * 10 << " %";
  EXPECT_TRUE(battery::validCurve(out));
  EXPECT_TRUE(L.lastFull());
  EXPECT_EQ(L.state(), battlearn::WAITING);   // ready for the next one
}

TEST(BattLearn, APartialRunKeepsThePointsAboveItsStart) {
  battlearn::Learner L;
  battery::useCurve(LIPO);
  uint16_t out[11];
  ASSERT_TRUE(run(L, 0.85f, LIPO, 0, out));
  EXPECT_FALSE(L.lastFull());
  EXPECT_EQ(out[10], LIPO[10]);
  EXPECT_TRUE(battery::validCurve(out));
}

TEST(BattLearn, TooLowAStartDoesntCount) {
  battlearn::Learner L;
  battery::useCurve(LIPO);
  uint16_t out[11] = {};
  EXPECT_FALSE(run(L, 0.55f, LIPO, 0, out));
  EXPECT_EQ(out[5], 0);
}

TEST(BattLearn, PluggingInOnTheWayBreaksTheRun) {
  battlearn::Learner L;
  battery::useCurve(LIPO);
  uint16_t out[11] = {};
  // Plugged in for a minute 40 h in: what follows is a run of its own,
  // starting far too low.
  EXPECT_FALSE(run(L, 1.0f, LIPO, 0, out, 40));
}

TEST(BattLearn, TooShortARunDoesntCount) {
  battlearn::Learner L;
  battery::useCurve(LIPO);
  uint16_t out[11] = {};
  EXPECT_FALSE(run(L, 1.0f, LIPO, 0, out, 0, 5));   // "shut down" 5 h in
}

TEST(BattLearn, LaterRunsAreAveragedIn) {
  battlearn::Learner L;
  battery::useCurve(LIPO);
  uint16_t first[11], second[11];
  ASSERT_TRUE(run(L, 1.0f, LIPO, 0, first));
  // A second run against an odd old curve: halfway between the two.
  uint16_t odd[11];
  for (int k = 0; k < 11; k++) odd[k] = first[k] + 100;
  odd[0] = LOW;
  battery::useCurve(odd);
  ASSERT_TRUE(run(L, 1.0f, odd, 1, second, 0, 0, 9));
  for (int k = 2; k < 10; k++) EXPECT_NEAR(second[k], first[k] + 50, 30) << "point " << k * 10 << " %";
}

TEST(BattLearn, SourceAndMarks) {
  struct P { uint16_t batt_curve_mv[11]; uint8_t batt_curve_src, batt_learn_runs; uint32_t batt_learn_date; } p = {};
  EXPECT_EQ(battlearn::source(p.batt_curve_mv, p.batt_curve_src), battlearn::BUILTIN);
  memcpy(p.batt_curve_mv, LIPO, sizeof(LIPO));
  EXPECT_EQ(battlearn::source(p.batt_curve_mv, p.batt_curve_src), battlearn::MANUAL);   // set before 2.4
  p.batt_curve_src = battlearn::LEARNED; p.batt_learn_runs = 3;
  EXPECT_EQ(battlearn::source(p.batt_curve_mv, p.batt_curve_src), battlearn::LEARNED);
  battlearn::markManual(p);
  EXPECT_EQ(battlearn::source(p.batt_curve_mv, p.batt_curve_src), battlearn::MANUAL);
  battlearn::markBuiltin(p);
  EXPECT_EQ(p.batt_learn_runs, 0);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
