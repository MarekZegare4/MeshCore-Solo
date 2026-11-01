#include <gtest/gtest.h>

#include <cstring>

// Which ! commands a target answers (ui-core/BotCommands.h).
#include "../../examples/companion_radio/ui-core/BotCommands.h"

using namespace botcmd;

TEST(BotCommands, CommandsOnlyAtTheStart) {
  EXPECT_STREQ(start("!batt !time"), "!batt !time");
  EXPECT_STREQ(start("  !batt"), "!batt");
  EXPECT_EQ(start("don't write !loc here"), nullptr);
  EXPECT_EQ(start("cmds: !ping !buzz !advert"), nullptr);   // another bot's !help
  EXPECT_EQ(start("hello"), nullptr);
  EXPECT_EQ(start(""), nullptr);
}

TEST(BotCommands, PastAReplyPrefix) {
  EXPECT_STREQ(start("@[Bob] !batt"), "!batt");
  EXPECT_STREQ(start("@[Bob]!batt"), "!batt");
  EXPECT_EQ(start("@[Bob] thanks !batt"), nullptr);
  EXPECT_EQ(start("@[Bob !batt"), nullptr);
}

TEST(BotCommands, EverythingOnByDefault) {
  for (int i = 0; i < COUNT; i++) EXPECT_TRUE(enabled(i == GPIO ? "gpio1" : CMDS[i].name, 0)) << CMDS[i].name;
}

TEST(BotCommands, FindsTheTableNames) {
  EXPECT_EQ(find("ping"), PING);
  EXPECT_EQ(find("status"), STATUS);
  EXPECT_EQ(find("advert"), ADVERT);
}

TEST(BotCommands, TheFourPinsAreOneSwitch) {
  for (const char* c : { "gpio1", "gpio2", "gpio3", "gpio4" }) EXPECT_EQ(find(c), GPIO);
  EXPECT_EQ(find("gpio5"), -1);
  EXPECT_EQ(find("gpio12"), -1);
  EXPECT_EQ(find("gpio1-4"), -1);   // the row's name, not something to type
  EXPECT_FALSE(enabled("gpio3", flag(GPIO)));
}

TEST(BotCommands, UnknownWordsAreNotBlocked) {
  EXPECT_EQ(find("hello"), -1);
  EXPECT_TRUE(enabled("hello", 0xFFFF));
}

TEST(BotCommands, OneOffLeavesTheOthers) {
  EXPECT_FALSE(enabled("batt", flag(BATT)));
  EXPECT_TRUE(enabled("ping", flag(BATT)));
  EXPECT_TRUE(enabled("temp", flag(BATT)));
}

TEST(BotCommands, LocOffHidesThePositionEverywhere) {
  const uint16_t off = flag(LOC);
  EXPECT_FALSE(position(off));
  EXPECT_FALSE(enabled("loc", off));
  EXPECT_FALSE(enabled("dist", off));
  EXPECT_TRUE(enabled("status", off));   // answers, without the position
  EXPECT_TRUE(enabled("gps", off));      // on / off still work, the fix doesn't
}

TEST(BotCommands, DistOffKeepsLoc) {
  EXPECT_TRUE(position(flag(DIST)));
  EXPECT_TRUE(enabled("loc", flag(DIST)));
  EXPECT_FALSE(enabled("dist", flag(DIST)));
}

TEST(BotCommands, HelpListsAllWithoutItself) {
  char b[128];
  help(b, sizeof(b), 0, true);
  EXPECT_STREQ(b, "cmds: !ping !batt !temp !time !loc !hops !path !snr !dist !status !gps !buzz !advert !gpio1-4");
}

TEST(BotCommands, HelpLeavesOutActionsWhenNotAllowed) {
  char b[128];
  help(b, sizeof(b), 0, false);
  EXPECT_STREQ(b, "cmds: !ping !batt !temp !time !loc !hops !path !snr !dist !status");
}

TEST(BotCommands, HelpLeavesOutWhatIsOff) {
  char b[128];
  help(b, sizeof(b), flag(LOC) | flag(BATT) | flag(GPS), true);
  EXPECT_STREQ(b, "cmds: !ping !temp !time !hops !path !snr !status !buzz !advert !gpio1-4");
}

TEST(BotCommands, HelpStopsAtTheBuffer) {
  char b[12];
  memset(b, 'x', sizeof(b));
  help(b, sizeof(b), 0, true);
  EXPECT_LT(strlen(b), sizeof(b));
}

TEST(BotCommands, RowValue) {
  char b[16];
  shown(0, true, b, sizeof(b));
  EXPECT_STREQ(b, "All");
  shown(flag(LOC) | flag(DIST), true, b, sizeof(b));
  EXPECT_STREQ(b, "13 of 15");
  shown(0xFFFF, true, b, sizeof(b));
  EXPECT_STREQ(b, "0 of 15");
}

TEST(BotCommands, RowValueWithoutActionsCountsWhatIsListed) {
  char b[16];
  EXPECT_EQ(count(false), (int)GPS);
  EXPECT_EQ(count(true), (int)COUNT);
  shown(flag(GPS) | flag(GPIO), false, b, sizeof(b));   // hidden: not counted
  EXPECT_STREQ(b, "All");
  shown(flag(LOC) | flag(GPS), false, b, sizeof(b));
  EXPECT_STREQ(b, "10 of 11");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
