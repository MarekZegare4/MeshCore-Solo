#include <gtest/gtest.h>

// The battery since boot (ui-core/BattLog.h): the log keeps the whole time
// in N slots by merging pairs, and the estimate only reads the time on
// battery since the last unplug.
#include "../../examples/companion_radio/ui-core/BattLog.h"

namespace {

using Log = uicore::BattLog<8, 60000UL>;

TEST(BattLog, FillsThenMergesPairsAndDoublesTheInterval) {
  Log log;
  unsigned long now = 1000;
  for (int i = 0; i < 8; i++) { log.tick(now, 4000 - i * 10, false); now += 60000; }
  EXPECT_EQ(log.mv.n, 8);
  EXPECT_EQ(log.interval_ms, 60000UL);
  EXPECT_EQ(log.spanMinutes(), 7u);
  log.tick(now, 3900, false);
  EXPECT_EQ(log.mv.n, 5);                  // 4 merged + the new one
  EXPECT_EQ(log.interval_ms, 120000UL);
  EXPECT_EQ(log.mv.at(0), (4000 + 3990) / 2);
  EXPECT_EQ(log.mv.at(4), 3900);
}

TEST(BattLog, WaitsForTheInterval) {
  Log log;
  log.tick(1000, 4000, false);
  log.tick(2000, 3990, false);   // too soon
  EXPECT_EQ(log.mv.n, 1);
  log.tick(61000, 3990, false);
  EXPECT_EQ(log.mv.n, 2);
}

TEST(BattLog, MergedPairOnPowerIfEitherWas) {
  Log log;
  unsigned long now = 0;
  for (int i = 0; i < 8; i++) { log.tick(now, 4000, i == 1); now += 60000; }
  log.tick(now, 4000, false);
  EXPECT_TRUE(log.ext[0]);
  EXPECT_FALSE(log.ext[1]);
}

TEST(BattLog, NoEstimateOnPowerOrTooSoon) {
  uicore::BattLog<64> log;
  unsigned long now = 0;
  for (int i = 0; i < 10; i++) { log.tick(now, 4000, true); now += log.interval_ms; }
  EXPECT_TRUE(log.estimate(0).charging);
  for (int i = 0; i < 10; i++) { log.tick(now, 3990 - i, false); now += log.interval_ms; }
  auto e = log.estimate(0);
  EXPECT_FALSE(e.charging);
  EXPECT_FALSE(e.ok);   // 9 minutes on battery
}

TEST(BattLog, EstimateFromTheDrainSinceUnplugged) {
  uicore::BattLog<64> log;
  unsigned long now = 0;
  for (int m = 0; m < 20; m++) { log.tick(now, 4150, true); now += 60000; }   // charging: left out
  // Two hours on battery, 50 % -> 40 % on the built-in curve, a reading a
  // minute (the log takes what its interval asks for).
  for (int m = 0; m <= 120; m++) {
    const int pct = 50 - 10 * m / 120;
    int mv = 3000;
    while (battery::percent(mv, 0) < pct) mv++;
    log.tick(now, mv, false);
    now += 60000;
  }
  auto e = log.estimate(0);
  ASSERT_TRUE(e.ok);
  EXPECT_NEAR(e.pct_per_h_x10 / 10.0, 5.0, 1.0);    // ~5 %/h
  EXPECT_NEAR(e.minutes_left / 60.0, 8.0, 1.5);     // ~40 % left at 5 %/h
}

TEST(BattLog, SpanText) {
  char b[16];
  uicore::spanText(45, b, sizeof(b)); EXPECT_STREQ(b, "45 min");
  uicore::spanText(13 * 60 + 10, b, sizeof(b)); EXPECT_STREQ(b, "13 h");
  uicore::spanText(2 * 1440 + 4 * 60, b, sizeof(b)); EXPECT_STREQ(b, "2 d 4 h");
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
