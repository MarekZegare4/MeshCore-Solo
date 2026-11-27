#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

// The message history rings (ui-core/MessageHistory.h): channel and DM entries,
// unread counts, relay echoes and DM ACK/resend state. MessageHistory is a
// single-TU fragment that reaches the mesh and the clock as globals
// (the_mesh, rtc_clock); here they are stand-ins the tests drive.
#define MAX_GROUP_CHANNELS 8
#define HIST_CH_MAX 4   // small rings: eviction is reached in a few adds
#define HIST_DM_MAX 4
#include <helpers/BaseChatMesh.h>   // MAX_TEXT_LEN, MAX_ANON_CONTACTS, ContactInfo

// ContactInfo holds a mesh::Identity; its constructor is in src/Identity.cpp,
// which needs the Ed25519 library the native build hasn't got. The same body.
mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

namespace {

struct FakeClock {
  uint32_t now = 1000000;
  uint32_t getCurrentTime() const { return now; }
};

struct SentMsg {
  std::string text;
  uint32_t ts;
  uint8_t attempt;
};

struct FakeMesh {
  struct { uint8_t pub_key[PUB_KEY_SIZE]; } self_id{};
  std::vector<ContactInfo> contacts;
  std::vector<SentMsg> sent;
  std::vector<std::string> path_resets;
  bool send_ok = true;
  uint32_t next_ack = 0x1000;

  int getNumContacts() const { return (int)contacts.size(); }
  bool getContactByIdx(uint32_t idx, ContactInfo& c) {
    if (idx < MAX_ANON_CONTACTS || idx - MAX_ANON_CONTACTS >= contacts.size()) return false;
    c = contacts[idx - MAX_ANON_CONTACTS];
    return true;
  }
  bool resetContactPath(const uint8_t* pub_key) {
    path_resets.push_back(std::string((const char*)pub_key, 4));
    return true;
  }
  int sendMessage(const ContactInfo&, uint32_t ts, uint8_t attempt, const char* text,
                  uint32_t& expected_ack, uint32_t& est_timeout) {
    if (!send_ok) return 0;
    sent.push_back({text, ts, attempt});
    expected_ack = next_ack++;
    est_timeout = 3000;
    return 1;
  }
};

}  // namespace

static FakeClock rtc_clock;
static FakeMesh the_mesh;

#include "../../examples/companion_radio/ui-core/MessageHistory.h"

namespace {

const uint8_t KEY_A[PUB_KEY_SIZE] = {0xA1, 0xA2, 0xA3, 0xA4};
const uint8_t KEY_B[PUB_KEY_SIZE] = {0xB1, 0xB2, 0xB3, 0xB4};

class History : public ::testing::Test {
protected:
  MessageHistory h;
  void SetUp() override {
    rtc_clock.now = 1000000;
    g_mock_millis = 0;
    the_mesh = FakeMesh();
  }
  const char* chText(int ch, int j) const { return h.chAtPos(h.histEntryForChannel(ch, j)).text; }
  const char* dmText(const uint8_t* key, int j) const { return h.dmAtPos(h.dmHistEntryForContact(key, j)).text; }
  static ContactInfo contact(const uint8_t* key, uint8_t out_path_len = OUT_PATH_UNKNOWN, uint8_t flags = 0) {
    ContactInfo c{};
    memcpy(c.id.pub_key, key, PUB_KEY_SIZE);
    c.out_path_len = out_path_len;
    c.flags = flags;
    return c;
  }
};

// ── Channel ring ───────────────────────────────────────────────────────────

TEST_F(History, ChannelEntriesComeBackNewestFirstPerChannel) {
  h.addChannelMsg(1, "a1", true);
  h.addChannelMsg(2, "b1", true);
  h.addChannelMsg(1, "a2", true);
  EXPECT_EQ(3, h.chHistCount());
  EXPECT_EQ(2, h.histCountForChannel(1));
  EXPECT_STREQ("a2", chText(1, 0));
  EXPECT_STREQ("a1", chText(1, 1));
  EXPECT_EQ(-1, h.histEntryForChannel(1, 2));
  EXPECT_STREQ("a2", h.chAtPos(h.chHistPosNewest(0)).text);
  EXPECT_STREQ("a1", h.chAtPos(h.chHistPosNewest(2)).text);
}

TEST_F(History, AChannelIndexOutOfRangeIsRejected) {
  EXPECT_EQ(-1, h.addChannelMsg(MAX_GROUP_CHANNELS, "x", false));
  EXPECT_EQ(-1, h.addChannelMsg(255, "x", false));
  EXPECT_EQ(0, h.chHistCount());
}

TEST_F(History, AFullRingDropsTheOldestEntry) {
  for (int i = 0; i < HIST_CH_MAX + 2; i++) {
    char t[8];
    snprintf(t, sizeof(t), "m%d", i);
    h.addChannelMsg(0, t, true);
  }
  EXPECT_EQ(HIST_CH_MAX, h.chHistCount());
  EXPECT_STREQ("m5", chText(0, 0));
  EXPECT_STREQ("m2", chText(0, HIST_CH_MAX - 1));
}

TEST_F(History, ALongTextIsCutOnAWholeCharacter) {
  std::string t(MSG_TEXT_BUF - 2, 'x');
  t += "\xC4\x85";   // "ą" across the last byte that fits
  h.addChannelMsg(0, t.c_str(), true);
  EXPECT_EQ(std::string(MSG_TEXT_BUF - 2, 'x'), chText(0, 0));
}

TEST_F(History, TimestampsInTheFutureOrUnknownBecomeReceiptTime) {
  h.addChannelMsg(0, "old", true, 999000);
  h.addChannelMsg(0, "future", true, rtc_clock.now + 60);
  h.addChannelMsg(0, "unknown", true, 0);
  h.addChannelMsg(0, "mine", true, 999000, nullptr, 0, true);
  EXPECT_EQ(rtc_clock.now, h.chAtPos(h.histEntryForChannel(0, 0)).timestamp);   // own: now
  EXPECT_EQ(rtc_clock.now, h.chAtPos(h.histEntryForChannel(0, 1)).timestamp);
  EXPECT_EQ(rtc_clock.now, h.chAtPos(h.histEntryForChannel(0, 2)).timestamp);
  EXPECT_EQ(999000u, h.chAtPos(h.histEntryForChannel(0, 3)).timestamp);
}

// ── Unread counts ──────────────────────────────────────────────────────────

TEST_F(History, OnlyMessagesNotViewedAndNotOursCountUnread) {
  h.addChannelMsg(3, "seen", true);
  h.addChannelMsg(3, "mine", false, 0, nullptr, 0, true);
  h.addChannelMsg(3, "new1", false);
  h.addChannelMsg(3, "new2", false);
  EXPECT_EQ(2, h.chUnread(3));
  EXPECT_EQ(2, h.getTotalChannelUnread());
  EXPECT_EQ(0, h.chUnread(-1));
  EXPECT_EQ(0, h.chUnread(MAX_GROUP_CHANNELS));
}

TEST_F(History, UnreadNeverClaimsMoreThanTheRingHolds) {
  h.setChUnread(2, 50);   // e.g. restored from before a reboot
  h.addChannelMsg(2, "only", true);
  EXPECT_EQ(1, h.chUnread(2));
  EXPECT_EQ(1, h.getTotalChannelUnread());
}

TEST_F(History, EvictingAnUnreadEntryMarksTheCountIncomplete) {
  for (int i = 0; i < HIST_CH_MAX; i++) h.addChannelMsg(1, "u", false);
  EXPECT_EQ(HIST_CH_MAX, h.chUnread(1));
  EXPECT_FALSE(h.chUnreadOverflow(1));
  h.addChannelMsg(1, "u", false);   // pushes out an unread one
  EXPECT_EQ(HIST_CH_MAX, h.chUnread(1));
  EXPECT_TRUE(h.chUnreadOverflow(1));
  EXPECT_TRUE(h.anyChannelUnreadOverflow());
  h.setChUnread(1, 0);
  EXPECT_FALSE(h.chUnreadOverflow(1));
  EXPECT_FALSE(h.anyChannelUnreadOverflow());
}

TEST_F(History, EvictingAReadEntryKeepsTheCount) {
  h.addChannelMsg(1, "read", true);
  for (int i = 0; i < HIST_CH_MAX - 1; i++) h.addChannelMsg(1, "u", false);
  h.addChannelMsg(1, "u", false);   // pushes out the read one
  EXPECT_EQ(HIST_CH_MAX, h.chUnread(1));
  EXPECT_FALSE(h.chUnreadOverflow(1));
}

TEST_F(History, EvictingAnotherChannelsEntryLeavesThisCountAlone) {
  h.addChannelMsg(0, "other", true);
  for (int i = 0; i < HIST_CH_MAX - 1; i++) h.addChannelMsg(1, "u", false);
  h.addChannelMsg(1, "u", false);
  EXPECT_EQ(HIST_CH_MAX, h.chUnread(1));
  EXPECT_FALSE(h.chUnreadOverflow(0));
}

TEST_F(History, ClearAllChannelUnreadDropsCountsAndFlags) {
  for (int i = 0; i < HIST_CH_MAX + 1; i++) h.addChannelMsg(1, "u", false);
  h.clearAllChannelUnread();
  EXPECT_EQ(0, h.getTotalChannelUnread());
  EXPECT_FALSE(h.chUnreadOverflow(1));
}

TEST_F(History, FirstUnreadChannelSkipsOurOwnPosts) {
  h.addChannelMsg(0, "old", true);
  h.addChannelMsg(0, "x: in1", false);
  h.addChannelMsg(0, "Me: reply", true);
  h.addChannelMsg(0, "x: in2", false);
  EXPECT_EQ(0, h.firstUnreadChannel(0, 1));
  EXPECT_EQ(2, h.firstUnreadChannel(0, 2));   // "in1", past our reply
  EXPECT_EQ(-1, h.firstUnreadChannel(0, 0));
}

// ── Paths and relay echoes ─────────────────────────────────────────────────

TEST_F(History, APathDeeperThanTheBufferKeepsWhatFits) {
  uint8_t path[40];
  for (int i = 0; i < 40; i++) path[i] = (uint8_t)i;
  const uint8_t packed = (3 << 6) | 10;   // 4-byte hashes, 10 hops
  h.addChannelMsg(0, "far", true, 0, path, packed);
  const ChHistEntry& e = h.chAtPos(h.histEntryForChannel(0, 0));
  EXPECT_EQ((3 << 6) | (MAX_HIST_PATH_BYTES / 4), e.path_len);
  EXPECT_EQ(0, memcmp(e.path, path, MAX_HIST_PATH_BYTES));
}

TEST_F(History, RelayEchoesMarkTheSendAndCollectDistinctRepeaters) {
  const int pos = h.addChannelMsg(0, "Me: hi", true, 0, nullptr, 0, true);
  h.armChannelRelay(pos, 77);
  EXPECT_EQ(ACK_PENDING, h.chAtPos(pos).relay_status);
  const uint8_t r1[2] = {1, 2}, r2[2] = {3, 4};
  h.markChannelRelayed(77, r1, 2);
  h.markChannelRelayed(77, r1, 2);   // the same repeater again
  h.markChannelRelayed(77, r2, 2);
  const ChHistEntry& e = h.chAtPos(pos);
  EXPECT_EQ(ACK_OK, e.relay_status);
  EXPECT_EQ((1 << 6) | 2, e.path_len);
  EXPECT_EQ(0, memcmp(e.path, "\x01\x02\x03\x04", 4));
}

TEST_F(History, ARelayHashOfAnotherSizeIsIgnored) {
  const int pos = h.addChannelMsg(0, "Me: hi", true, 0, nullptr, 0, true);
  h.armChannelRelay(pos, 5);
  const uint8_t r1[1] = {9}, r2[2] = {1, 2};
  h.markChannelRelayed(5, r1, 1);
  h.markChannelRelayed(5, r2, 2);
  EXPECT_EQ(1, h.chAtPos(pos).path_len);   // one 1-byte hash
  EXPECT_EQ(9, h.chAtPos(pos).path[0]);
}

TEST_F(History, RelayHashesStopAtTheBuffer) {
  const int pos = h.addChannelMsg(0, "Me: hi", true, 0, nullptr, 0, true);
  h.armChannelRelay(pos, 5);
  for (uint8_t i = 0; i < MAX_HIST_PATH_BYTES + 4; i++) h.markChannelRelayed(5, &i, 1);
  EXPECT_EQ(MAX_HIST_PATH_BYTES, h.chAtPos(pos).path_len);
}

TEST_F(History, AnEchoForAnotherSendOrSeqZeroChangesNothing) {
  const int pos = h.addChannelMsg(0, "Me: hi", true, 0, nullptr, 0, true);
  h.armChannelRelay(pos, 5);
  h.markChannelRelayed(6);
  h.markChannelRelayed(0);
  EXPECT_EQ(ACK_PENDING, h.chAtPos(pos).relay_status);
}

// ── DM ring ────────────────────────────────────────────────────────────────

TEST_F(History, DmsAreKeptPerContactNewestFirst) {
  h.addDMMsg(KEY_A, false, "a1", 100);
  h.addDMMsg(KEY_B, false, "b1", 100);
  h.addDMMsg(KEY_A, true, "a2");
  EXPECT_EQ(3, h.dmHistCount());
  EXPECT_EQ(2, h.dmHistCountForContact(KEY_A));
  EXPECT_STREQ("a2", dmText(KEY_A, 0));
  EXPECT_STREQ("a1", dmText(KEY_A, 1));
  EXPECT_EQ(-1, h.dmHistEntryForContact(KEY_A, 2));
}

TEST_F(History, ARetriedIncomingDmIsStoredOnce) {
  h.addDMMsg(KEY_A, false, "hello", 500);
  h.addDMMsg(KEY_A, false, "hello", 500);   // the sender's retry
  h.addDMMsg(KEY_A, false, "hello", 501);   // another message
  h.addDMMsg(KEY_B, false, "hello", 500);   // another sender
  EXPECT_EQ(2, h.dmHistCountForContact(KEY_A));
  EXPECT_EQ(1, h.dmHistCountForContact(KEY_B));
}

TEST_F(History, AFullDmRingDropsTheOldest) {
  for (int i = 0; i < HIST_DM_MAX + 1; i++) {
    char t[8];
    snprintf(t, sizeof(t), "d%d", i);
    h.addDMMsg(KEY_A, false, t, 100 + i);
  }
  EXPECT_EQ(HIST_DM_MAX, h.dmHistCount());
  EXPECT_STREQ("d1", dmText(KEY_A, HIST_DM_MAX - 1));
}

TEST_F(History, FirstUnreadDmSkipsOurOwnMessages) {
  h.addDMMsg(KEY_A, false, "in1", 100);
  h.addDMMsg(KEY_A, true, "out");
  h.addDMMsg(KEY_A, false, "in2", 101);
  EXPECT_EQ(0, h.firstUnreadDm(KEY_A, 1));
  EXPECT_EQ(2, h.firstUnreadDm(KEY_A, 2));
}

TEST_F(History, AnOutgoingDmWithATagWaitsForItsAck) {
  h.addDMMsg(KEY_A, true, "hi", 0, 0x55, 5000, 2);
  h.addDMMsg(KEY_A, true, "no tag");
  EXPECT_EQ(ACK_NONE, h.dmAtPos(h.dmHistEntryForContact(KEY_A, 0)).ack_status);
  const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(KEY_A, 1));
  EXPECT_EQ(ACK_PENDING, e.ack_status);
  EXPECT_EQ(2, e.resends_left);
  h.markDmDelivered(0x99);
  EXPECT_EQ(ACK_PENDING, e.ack_status);
  h.markDmDelivered(0x55);
  EXPECT_EQ(ACK_OK, e.ack_status);
}

TEST_F(History, AnAckForAnEarlierAttemptStillCounts) {
  // A resent DM's ACK CRC is the hash of (timestamp, attempt & 3, text,
  // our key): a late ACK for attempt 0 arrives after attempt 1 went out.
  the_mesh.contacts.push_back(contact(KEY_A));
  memset(the_mesh.self_id.pub_key, 0x42, PUB_KEY_SIZE);
  h.addDMMsg(KEY_A, true, "hi", 1234, 0x55, 0, 1);
  h.tickDmResends();   // deadline 0 has passed: attempt 1
  const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(KEY_A, 0));
  ASSERT_EQ(1, e.attempt);

  uint8_t buf[5 + 2];
  const uint32_t ts = 1234;
  memcpy(buf, &ts, 4);
  buf[4] = 0;
  memcpy(&buf[5], "hi", 2);
  uint32_t crc0;
  mesh::Utils::sha256((uint8_t*)&crc0, 4, buf, sizeof(buf), the_mesh.self_id.pub_key, PUB_KEY_SIZE);
  h.markDmDelivered(crc0);
  EXPECT_EQ(ACK_OK, e.ack_status);
}

TEST_F(History, AnUnansweredDmFailsOnlyOnceItsResendsAreUsed) {
  h.addDMMsg(KEY_A, true, "hi", 0, 0x55, 5000, 0);
  const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(KEY_A, 0));
  g_mock_millis = 4999;
  EXPECT_EQ(ACK_PENDING, h.dmEffectiveStatus(e));
  g_mock_millis = 5000;
  EXPECT_EQ(ACK_FAIL, h.dmEffectiveStatus(e));
  EXPECT_EQ(ACK_PENDING, e.ack_status);   // shown failed, stored state unchanged until a tick
  h.tickDmResends();
  EXPECT_EQ(ACK_FAIL, e.ack_status);
}

TEST_F(History, ResendsGoOutAfterTheDeadlineAndTheLastOneForgetsThePath) {
  the_mesh.contacts.push_back(contact(KEY_A, 2));   // a known direct path
  h.addDMMsg(KEY_A, true, "hi", 1234, 0x55, 5000, 2);
  const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(KEY_A, 0));

  g_mock_millis = 4000;
  h.tickDmResends();
  EXPECT_TRUE(the_mesh.sent.empty());

  g_mock_millis = 5000;
  h.tickDmResends();
  ASSERT_EQ(1u, the_mesh.sent.size());
  EXPECT_EQ(1, the_mesh.sent[0].attempt);
  EXPECT_EQ(1234u, the_mesh.sent[0].ts);
  EXPECT_EQ(1, e.resends_left);
  EXPECT_EQ(5000u + 3000 + 4000, e.ack_deadline_ms);
  EXPECT_TRUE(the_mesh.path_resets.empty());

  g_mock_millis = e.ack_deadline_ms;
  h.tickDmResends();   // the last resend: as a flood
  ASSERT_EQ(2u, the_mesh.sent.size());
  EXPECT_EQ(2, the_mesh.sent[1].attempt);
  EXPECT_EQ(1u, the_mesh.path_resets.size());
  EXPECT_EQ(0, e.resends_left);
  EXPECT_EQ(ACK_PENDING, e.ack_status);
}

TEST_F(History, APinnedRouteIsKeptOnTheLastResend) {
  the_mesh.contacts.push_back(contact(KEY_A, 2, CONTACT_FLAG_ROUTE_PINNED));
  h.addDMMsg(KEY_A, true, "hi", 1234, 0x55, 0, 1);
  h.tickDmResends();
  EXPECT_EQ(1u, the_mesh.sent.size());
  EXPECT_TRUE(the_mesh.path_resets.empty());
}

TEST_F(History, AResendToAContactNoLongerKnownOrUnsentFails) {
  h.addDMMsg(KEY_A, true, "gone", 1, 0x55, 0, 2);
  h.tickDmResends();
  EXPECT_EQ(ACK_FAIL, h.dmAtPos(h.dmHistEntryForContact(KEY_A, 0)).ack_status);

  the_mesh.contacts.push_back(contact(KEY_B));
  the_mesh.send_ok = false;
  h.addDMMsg(KEY_B, true, "unsent", 1, 0x56, 0, 2);
  h.tickDmResends();
  EXPECT_EQ(ACK_FAIL, h.dmAtPos(h.dmHistEntryForContact(KEY_B, 0)).ack_status);
}

TEST_F(History, ClearAllEmptiesBothRings) {
  h.addChannelMsg(0, "c", false);
  h.addDMMsg(KEY_A, false, "d", 1);
  h.clearAll();
  EXPECT_EQ(0, h.chHistCount());
  EXPECT_EQ(0, h.dmHistCount());
  EXPECT_EQ(0, h.getTotalChannelUnread());
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
