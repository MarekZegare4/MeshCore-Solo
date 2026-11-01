#include <gtest/gtest.h>

#include <cstring>
#include <string>

// The timing and matching of an SOS (ui-core/SosEngine.h): no radio, the test
// plays the caller's part and says when a round went out.
#include "../../examples/companion_radio/NodePrefs.h"
#include "../../examples/companion_radio/ui-core/SosEngine.h"
#include "../../examples/companion_radio/GeoUtils.h"
#include "../../examples/companion_radio/LiveTrack.h"

namespace {

class Sos : public ::testing::Test {
protected:
  NodePrefs prefs;
  SosEngine sos;
  uint32_t  now = 1000;

  void SetUp() override {
    prefs.sos_countdown_idx = 1;   // 10 s
    prefs.sos_repeat_idx = 3;      // 5 min
    strcpy(prefs.sos_ack, "ACK");
    sos.begin(&prefs);
  }
  // Past the countdown, with the first round out.
  void activate(bool fix = true, bool ok = true) {
    sos.start(now);
    now += 10000;
    ASSERT_TRUE(sos.loop(now, fix));
    sos.markSent(now, fix, ok);
  }
};

TEST_F(Sos, CountsDownWholeSecondsThenSends) {
  sos.start(now);
  EXPECT_TRUE(sos.counting());
  EXPECT_EQ(sos.secsLeft(now), 10);
  EXPECT_EQ(sos.secsLeft(now + 1), 10);
  EXPECT_EQ(sos.secsLeft(now + 9001), 1);
  EXPECT_FALSE(sos.loop(now + 9999, true));
  EXPECT_TRUE(sos.loop(now + 10000, true));
  EXPECT_FALSE(sos.counting());
}

TEST_F(Sos, CancelledCountdownSendsNothing) {
  sos.start(now);
  sos.stop();
  EXPECT_TRUE(sos.idle());
  EXPECT_FALSE(sos.loop(now + 20000, true));
}

TEST_F(Sos, RepeatsAtTheIntervalUpToTheLimit) {
  activate();
  EXPECT_FALSE(sos.loop(now + 299999, true));
  EXPECT_EQ(sos.nextSecs(now), 300);
  int rounds = 1;
  for (int i = 0; i < 40; i++) {
    now += 300000;
    if (sos.loop(now, true)) { sos.markSent(now, true, true); rounds++; }
  }
  EXPECT_EQ(rounds, (int)SosEngine::MAX_SENDS);
  EXPECT_EQ(sos.nextSecs(now), -1);
}

TEST_F(Sos, RepeatOffSendsOnce) {
  prefs.sos_repeat_idx = 0;
  activate();
  EXPECT_FALSE(sos.loop(now + 3600000UL, true));
  EXPECT_EQ(sos.nextSecs(now), -1);
}

TEST_F(Sos, FirstFixSendsOnceMoreEvenWithRepeatOff) {
  prefs.sos_repeat_idx = 0;
  activate(false);
  EXPECT_FALSE(sos.loop(now + 60000, false));
  now += 60000;
  EXPECT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_FALSE(sos.loop(now + 1, true));
}

TEST_F(Sos, NothingWentOutRetriesSoonWithoutSpinning) {
  activate(true, false);
  EXPECT_EQ(sos.sent(), 0);
  EXPECT_FALSE(sos.loop(now + 1, true));
  EXPECT_TRUE(sos.loop(now + (uint32_t)SosEngine::RETRY_MS, true));
}

TEST_F(Sos, FailedRoundAfterAGoodOneDoesNotSpinOnTheFix) {
  activate(false);
  now += 300000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, false);   // had a fix, but nothing went out
  EXPECT_FALSE(sos.loop(now + 1, true));
  EXPECT_TRUE(sos.loop(now + (uint32_t)SosEngine::RETRY_MS, true));
}

TEST_F(Sos, AckStopsRepeatsAndNamesWhoAnswered) {
  activate();
  EXPECT_FALSE(sos.onReply("Ann", "on my way"));
  EXPECT_TRUE(sos.onReply("Bob", "ack, on my way"));
  EXPECT_TRUE(sos.onReply("Cid", "ACK"));   // a second one counts too, the latest first
  EXPECT_STREQ(sos.responder(0).name, "Cid");
  EXPECT_STREQ(sos.responder(1).name, "Bob");
  EXPECT_FALSE(sos.loop(now + 600000, true));
  EXPECT_EQ(sos.nextSecs(now), -1);
}

TEST_F(Sos, NoAckBeforeAnythingWentOut) {
  sos.start(now);
  EXPECT_FALSE(sos.onReply("Ann", "ACK"));
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, false);
  EXPECT_FALSE(sos.onReply("Ann", "ACK"));
}

TEST_F(Sos, StopClearsTheNextOne) {
  activate();
  ASSERT_TRUE(sos.onReply("Ann", "ACK"));
  sos.stop();
  EXPECT_FALSE(sos.acked());
  EXPECT_EQ(sos.responders(), 0);
  EXPECT_EQ(sos.sent(), 0);
}

TEST(SosText, WordOnItsOwn) {
  EXPECT_TRUE(SosEngine::hasWord("ACK", "ACK"));
  EXPECT_TRUE(SosEngine::hasWord("ok, Ack!", "ACK"));
  EXPECT_FALSE(SosEngine::hasWord("back", "ACK"));
  EXPECT_FALSE(SosEngine::hasWord("acknowledged", "ACK"));
  EXPECT_FALSE(SosEngine::hasWord("ack\xC4\x85", "ACK"));
  EXPECT_FALSE(SosEngine::hasWord("ACK", ""));
  EXPECT_TRUE(SosEngine::hasWord("copied now", "copied"));
  EXPECT_TRUE(SosEngine::hasWord("aack ack", "ACK"));
}

TEST(SosText, Compose) {
  char t[128];
  int16_t alt = 212;
  SosEngine::compose(t, sizeof(t), SosEngine::FIX, 50061430, 19936580, &alt, -1, "14:32", "");
  EXPECT_STREQ(t, "SOS 50.06143,19.93658 ALT 212m sent 14:32");
  SosEngine::compose(t, sizeof(t), SosEngine::FIX, -33868800, 151209300, nullptr, -1, "", "broken leg");
  EXPECT_STREQ(t, "SOS -33.86880,151.20930 broken leg");
  SosEngine::compose(t, sizeof(t), SosEngine::LAST_KNOWN, 50061430, 19936580, &alt, 12, "14:32", "");
  EXPECT_STREQ(t, "SOS last 50.06143,19.93658 12min ago sent 14:32");
  SosEngine::compose(t, sizeof(t), SosEngine::LAST_KNOWN, 50061430, 19936580, nullptr, -1, "14:32", "");
  EXPECT_STREQ(t, "SOS last 50.06143,19.93658 sent 14:32");
  SosEngine::compose(t, sizeof(t), SosEngine::NOWHERE, 0, 0, nullptr, -1, "14:32", "");
  EXPECT_STREQ(t, "SOS no GPS sent 14:32");
}

// What a receiver's Navigate reads out of it, on a channel with the sender in front.
TEST(SosText, PositionCanBeReadBack) {
  char t[128], msg[160];
  int16_t alt = 212;
  SosEngine::compose(t, sizeof(t), SosEngine::FIX, 50061430, -19936580, &alt, -1, "14:32", "");
  snprintf(msg, sizeof(msg), "Hiker: %s", t);
  int32_t lat = 0, lon = 0;
  ASSERT_TRUE(geo::parseLatLon(msg, lat, lon));
  EXPECT_EQ(lat, 50061430);
  EXPECT_EQ(lon, -19936580);
}

TEST(SosText, OnlyAnSosWithAPositionIsTakenForOne) {
  int32_t lat = 0, lon = 0;
  EXPECT_TRUE(geo::parseSos("SOS 50.06143,19.93658 ALT 212m", lat, lon));
  EXPECT_EQ(lat, 50061430);
  EXPECT_TRUE(geo::parseSos("SOS last -33.86880,151.20930 12min ago", lat, lon));
  EXPECT_EQ(lat, -33868800);
  EXPECT_FALSE(geo::parseSos("SOS no GPS sent 14:32", lat, lon));
  EXPECT_FALSE(geo::parseSos("I got your SOS 50.06143,19.93658", lat, lon));
  EXPECT_FALSE(geo::parseSos("SOS meet at 50.06143,19.93658", lat, lon));
  EXPECT_FALSE(geo::parseSos("SOSNA 50.06143,19.93658", lat, lon));
}

TEST(SosText, NoGpsIsRecognisedOnItsOwn) {
  char t[64];
  SosEngine::compose(t, sizeof(t), SosEngine::NOWHERE, 0, 0, nullptr, -1, "14:32", "");
  EXPECT_TRUE(geo::isSosNoGps(t));
  EXPECT_TRUE(geo::isSosNoGps("SOS no GPS"));
  EXPECT_FALSE(geo::isSosNoGps("SOS no GPSfoo"));
  EXPECT_FALSE(geo::isSosNoGps("no SOS no GPS"));
}

TEST(LiveTrack, SosOutlivesAShareAndIsEvictedLast) {
  LiveTrackStore t;
  t.update(nullptr, "SOS Ann", 1, 2, 1000, false, true);
  uint32_t later = 1000 + LiveTrackStore::EXPIRY_SECS + 60;
  EXPECT_TRUE(t.isActive(0, later));
  EXPECT_FALSE(t.isActive(0, 1000 + LiveTrackStore::SOS_EXPIRY_SECS + 1));
  t.update(nullptr, "SOS Ann", 1, 2, 2000, false);   // a later update doesn't undo it
  EXPECT_TRUE(t.slotAt(0).sos);
  for (int i = 0; i < LiveTrackStore::CAPACITY + 3; i++) {
    char n[8];
    snprintf(n, sizeof(n), "n%d", i);
    t.update(nullptr, n, 1, 2, 3000 + i, false);
  }
  EXPECT_TRUE(t.slotAt(0).used && t.slotAt(0).sos);
}

TEST(SosText, ComposeNeverOverruns) {
  char t[12];
  SosEngine::compose(t, sizeof(t), SosEngine::NOWHERE, 0, 0, nullptr, -1, "14:32", "a long note");
  EXPECT_LT(strlen(t), sizeof(t));
}

TEST(SosTargets, ChannelsAndContacts) {
  NodePrefs p;
  EXPECT_FALSE(SosEngine::hasTargets(&p));
  p.sos_ch_mask = 1ULL << 39;
  EXPECT_TRUE(SosEngine::hasTargets(&p));
  p.sos_ch_mask = 0;
  p.sos_dm[2][5] = 1;
  EXPECT_TRUE(SosEngine::hasTargets(&p));
  EXPECT_FALSE(SosEngine::hasDm(&p, 0));
}

TEST_F(Sos, BeepOffByDefault) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, false));
  now += 60000;
  EXPECT_FALSE(sos.beepDue(now));
}

TEST_F(Sos, BeepKeepsGoingPastAnAck) {
  prefs.sos_beep_idx = 1;   // 30 s
  sos.start(now);
  EXPECT_FALSE(sos.beepDue(now));   // not in the countdown
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_FALSE(sos.beepDue(now));
  now += SosEngine::BEEP_FIRST_MS;
  EXPECT_TRUE(sos.beepDue(now));
  EXPECT_FALSE(sos.beepDue(now + 1000));
  sos.onReply("Bob", "ack");
  EXPECT_TRUE(sos.acked());
  EXPECT_TRUE(sos.beepDue(now + 30000));
  sos.stop();
  EXPECT_FALSE(sos.beepDue(now + 90000));
}

TEST_F(Sos, FirstFixPastTheLastSendStillGoesOut) {
  prefs.sos_repeat_idx = 1;   // 1 min
  sos.start(now);
  now += 10000;
  for (int i = 0; i < SosEngine::MAX_SENDS; i++) {
    ASSERT_TRUE(sos.loop(now, false)) << i;
    sos.markSent(now, false, true);
    now += 60000;
  }
  EXPECT_FALSE(sos.loop(now, false));
  EXPECT_FALSE(sos.finished());     // the GPS is still wanted
  EXPECT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_TRUE(sos.finished());
  EXPECT_FALSE(sos.loop(now + 60000, true));
}

TEST_F(Sos, AnswerKeepsWhoAndWhat) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_FALSE(sos.onReply("Bob", "hello there", now));   // no word, not yet someone who answered
  EXPECT_EQ(sos.responders(), 0);
  EXPECT_TRUE(sos.onReply("Bob", "ack, 20 min away", now));
  EXPECT_STREQ(sos.responder(0).name, "Bob");
  EXPECT_STREQ(sos.responder(0).text, "ack, 20 min away");
  EXPECT_EQ(sos.responder(0).at, now);
}

TEST_F(Sos, TheWordAloneHasNoText) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_TRUE(sos.onReply("Bob", "ack", now));
  EXPECT_STREQ(sos.responder(0).text, "");
}

TEST_F(Sos, TheBareWordKeepsWhatTheySaidBefore) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_TRUE(sos.onReply("Bob", "ack, 20 min away", now));
  EXPECT_TRUE(sos.onReply("Bob", "ACK", now + 1000));
  EXPECT_STREQ(sos.responder(0).text, "ack, 20 min away");
  EXPECT_EQ(sos.responder(0).at, now + 1000);
}

TEST_F(Sos, ThoseWhoAnsweredAreFollowedWithoutTheWord) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_TRUE(sos.onReply("Bob", "ack", now));
  EXPECT_TRUE(sos.onReply("Ann", "ACK coming too", now + 1000));
  EXPECT_EQ(sos.responders(), 2);
  EXPECT_STREQ(sos.responder(0).name, "Ann");
  EXPECT_TRUE(sos.onReply("Bob", "5 minutes", now + 2000));
  EXPECT_EQ(sos.responders(), 2);
  EXPECT_STREQ(sos.responder(0).name, "Bob");
  EXPECT_STREQ(sos.responder(0).text, "5 minutes");
  EXPECT_FALSE(sos.onReply("Eve", "hi", now + 3000));
  EXPECT_EQ(sos.responders(), 2);
}

TEST_F(Sos, TheQuietestResponderMakesRoom) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  for (const char* n : { "A", "B", "C", "D" }) EXPECT_TRUE(sos.onReply(n, "ack", now));
  EXPECT_EQ(sos.responders(), (int)SosEngine::RESPONDERS_MAX);
  EXPECT_STREQ(sos.responder(0).name, "D");
  EXPECT_STREQ(sos.responder(2).name, "B");
}

TEST_F(Sos, ALongAnswerIsCutOnACharacter) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  std::string msg = "ack ";
  while (msg.size() < 80) msg += "\xC5\x82";   // l-stroke: 2 bytes
  ASSERT_TRUE(sos.onReply("Bob", msg.c_str(), now));
  const char* t = sos.responder(0).text;
  EXPECT_EQ((strlen(t) - 4) % 2, 0u);
}

TEST_F(Sos, FinishedOnAnAnswer) {
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, false));
  sos.markSent(now, false, true);
  EXPECT_FALSE(sos.finished());
  sos.onReply("Bob", "ACK");
  EXPECT_TRUE(sos.finished());
}

TEST_F(Sos, FinishedAfterTheOnlySendWithAPosition) {
  prefs.sos_repeat_idx = 0;   // once
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_TRUE(sos.finished());
}

TEST_F(Sos, TheStatusLines) {
  char b[32];
  sos.start(now);
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.sentLine(b, sizeof(b));
  EXPECT_STREQ(b, "Not sent");
  sos.markSent(now, true, true);
  sos.sentLine(b, sizeof(b));
  EXPECT_STREQ(b, "Sent 1 x");
  sos.nextLine(b, sizeof(b), now);
  EXPECT_STREQ(b, "Next in 5 min");
  sos.nextLine(b, sizeof(b), now + 5UL * 60000UL - 30000UL);
  EXPECT_STREQ(b, "Next in 30 s");
  prefs.sos_repeat_idx = 0;
  sos.markSent(now, true, true);
  sos.nextLine(b, sizeof(b), now);
  EXPECT_STREQ(b, "No more repeats");
}

TEST_F(Sos, TheGpsIsWantedUntilNothingMoreGoesOut) {
  EXPECT_FALSE(sos.gpsWanted());
  sos.start(now);
  EXPECT_TRUE(sos.gpsWanted());      // the countdown already: a fix for the first send
  now += 10000;
  ASSERT_TRUE(sos.loop(now, true));
  sos.markSent(now, true, true);
  EXPECT_TRUE(sos.gpsWanted());
  EXPECT_TRUE(sos.onReply("Ann", "ACK", now));
  EXPECT_FALSE(sos.gpsWanted());     // answered: no more sends, the beep may go on
  sos.stop();
  EXPECT_FALSE(sos.gpsWanted());
}

TEST_F(Sos, TheScreenGoesOffSoonerOnlyWhileOneRuns) {
  EXPECT_EQ(sos.screenMs(0), 0u);
  EXPECT_EQ(sos.screenMs(120000), 120000u);
  sos.start(now);
  EXPECT_EQ(sos.screenMs(0), (uint32_t)SosEngine::SCREEN_MS);
  EXPECT_EQ(sos.screenMs(120000), (uint32_t)SosEngine::SCREEN_MS);
  EXPECT_EQ(sos.screenMs(5000), 5000u);
}

TEST(SosTargets, Counts) {
  NodePrefs p;
  EXPECT_FALSE(SosEngine::hasTargets(&p));
  p.sos_ch_mask = (1ULL << 0) | (1ULL << 7) | (1ULL << 63);
  EXPECT_EQ(SosEngine::channels(&p), 3);
  EXPECT_TRUE(SosEngine::hasTargets(&p));
  p.sos_ch_mask = 0;
  p.sos_dm[1][3] = 0x42;
  EXPECT_EQ(SosEngine::contacts(&p), 1);
  EXPECT_TRUE(SosEngine::hasTargets(&p));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
