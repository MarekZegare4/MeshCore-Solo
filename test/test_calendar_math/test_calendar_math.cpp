#include <gtest/gtest.h>

#include <cstring>

// Calendar and clock text shared by both UIs (ui-core/CalendarMath.h).
#include "../../examples/companion_radio/ui-core/CalendarMath.h"

TEST(CalendarMath, Hour12) {
  EXPECT_EQ(calmath::hour12(0), 12);    // midnight
  EXPECT_EQ(calmath::hour12(1), 1);
  EXPECT_EQ(calmath::hour12(11), 11);
  EXPECT_EQ(calmath::hour12(12), 12);   // noon
  EXPECT_EQ(calmath::hour12(13), 1);
  EXPECT_EQ(calmath::hour12(23), 11);
}

TEST(CalendarMath, FmtHM) {
  char b[8];
  calmath::fmtHM(b, sizeof(b), 9, 5, false);  EXPECT_STREQ(b, "09:05");
  calmath::fmtHM(b, sizeof(b), 14, 5, false); EXPECT_STREQ(b, "14:05");
  calmath::fmtHM(b, sizeof(b), 14, 5, true);  EXPECT_STREQ(b, "2:05");
  calmath::fmtHM(b, sizeof(b), 0, 30, true);  EXPECT_STREQ(b, "12:30");
}

TEST(CalendarMath, Names) {
  EXPECT_STREQ(calmath::monthAbbr(0), "Jan");
  EXPECT_STREQ(calmath::monthAbbr(11), "Dec");
  EXPECT_STREQ(calmath::wdayAbbr(0), "Sun");   // tm_wday
  EXPECT_STREQ(calmath::wdayAbbr(6), "Sat");
  EXPECT_STREQ(calmath::colHead(0), "Mo");     // a calendar starts on Monday
  EXPECT_STREQ(calmath::colHead(6), "Su");
}

TEST(CalendarMath, DaysIn) {
  EXPECT_EQ(calmath::daysIn(2024 - 1900, 1), 29);   // leap
  EXPECT_EQ(calmath::daysIn(2026 - 1900, 1), 28);
  EXPECT_EQ(calmath::daysIn(2000 - 1900, 1), 29);   // by 400
  EXPECT_EQ(calmath::daysIn(2100 - 1900, 1), 28);   // by 100
  EXPECT_EQ(calmath::daysIn(2026 - 1900, 3), 30);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
