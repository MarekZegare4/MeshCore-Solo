#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

// The message log on flash (ui-core/HistoryLog.h), the L1s' Keep messages:
// what is written when, what comes back after a restart, the segments, the
// pages of older messages, Delete after. Over an in-memory LittleFS
// (MemLittleFS.h) in the nRF52's shape; the mesh and the clock are stand-ins.
#define MAX_GROUP_CHANNELS 8
#define HIST_FLASH   // the rings at their flash boards' size (48 / 32)
#include "MemLittleFS.h"
#include <helpers/BaseChatMesh.h>   // MAX_TEXT_LEN, ContactInfo, ChannelDetails
#include "../../examples/companion_radio/NodePrefs.h"

// ContactInfo holds a mesh::Identity; its constructor is in src/Identity.cpp,
// which needs the Ed25519 library the native build hasn't got. The same body.
mesh::Identity::Identity() { memset(pub_key, 0, sizeof(pub_key)); }

namespace {

struct FakeClock {
  uint32_t now = 1800000000;   // a set clock (2027)
  uint32_t getCurrentTime() const { return now; }
};

struct DataStore {
  Adafruit_LittleFS* fs = nullptr;
  Adafruit_LittleFS* getSecondaryFS() { return fs; }
};

struct FakeMesh {
  struct { uint8_t pub_key[PUB_KEY_SIZE]; } self_id{};
  ChannelDetails channels[MAX_GROUP_CHANNELS]{};
  DataStore store;

  DataStore* getDataStore() { return &store; }
  bool getChannel(int idx, ChannelDetails& out) {
    if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;
    out = channels[idx];
    return true;
  }
  bool setChannelLocal(uint8_t idx, const ChannelDetails& ch) {
    if (idx >= MAX_GROUP_CHANNELS) return false;
    channels[idx] = ch;
    return true;
  }
  // MessageHistory's DM resends: not exercised here.
  int getNumContacts() const { return 0; }
  bool getContactByIdx(uint32_t, ContactInfo&) { return false; }
  bool resetContactPath(const uint8_t*) { return false; }
  int sendMessage(const ContactInfo&, uint32_t, uint8_t, const char*, uint32_t&, uint32_t&) { return 0; }
};

}  // namespace

static FakeClock rtc_clock;
static FakeMesh the_mesh;

#include "../../examples/companion_radio/ui-core/MessageHistory.h"
#include "../../examples/companion_radio/ui-core/ChannelControl.h"
#include "../../examples/companion_radio/ui-core/HistoryLog.h"

namespace {

const uint8_t KEY_A[PUB_KEY_SIZE] = {0xA1, 0xA2, 0xA3, 0xA4};
const uint8_t KEY_B[PUB_KEY_SIZE] = {0xB1, 0xB2, 0xB3, 0xB4};

void setChannel(int idx, const char* name, uint8_t secret_byte) {
  ChannelDetails ch{};
  strcpy(ch.name, name);
  memset(ch.channel.secret, secret_byte, sizeof(ch.channel.secret));
  the_mesh.channels[idx] = ch;
}

class Log : public ::testing::Test {
protected:
  Adafruit_LittleFS fs;
  NodePrefs prefs;
  std::unique_ptr<MessageHistory> hist;
  std::unique_ptr<HistoryLog> log;

  void SetUp() override {
    rtc_clock.now = 1800000000;
    g_mock_millis = 0;
    the_mesh = FakeMesh();
    the_mesh.store.fs = &fs;
    setChannel(0, "Public", 0x11);
    setChannel(1, "#hike", 0x22);
    prefs.msg_keep = 1;     // 500 in all: segments of 41 channel / 20 DM records
    prefs.msg_expire = 0;
    prefs.ch_no_save = 0;
    start();
  }
  void TearDown() override {
    if (log) log->pageEnd();
  }
  // A start of the device: new rings, the log read back.
  void start() {
    if (log) log->pageEnd();
    hist.reset(new MessageHistory());
    log.reset(new HistoryLog());
    log->begin(&prefs, hist.get());
  }
  void restart() {
    log->onShutdown();
    start();
  }
  void tick(uint32_t ms) {
    g_mock_millis += ms;
    log->loop();
  }
  // A message in, then a pass of the device's loop (no time passing).
  void ch(int slot, const char* text) {
    rtc_clock.now++;
    hist->addChannelMsg(slot, text, true, rtc_clock.now);
    log->loop();
  }
  void dmIn(const uint8_t* key, const char* text) {
    rtc_clock.now++;
    hist->addDMMsg(key, false, text, rtc_clock.now);
    log->loop();
  }
  static constexpr int CH_RING = MessageHistory::CH_HIST_MAX, DM_RING = MessageHistory::DM_HIST_MAX;
  static constexpr int PAGE = HistoryLog::PAGE, SEGMENTS = HistoryLog::SEGMENTS;
  std::vector<std::string> chTexts(int slot) {
    std::vector<std::string> out;
    for (int j = hist->histCountForChannel(slot) - 1; j >= 0; j--)
      out.push_back(hist->chAtPos(hist->histEntryForChannel(slot, j)).text);
    return out;
  }
  std::vector<std::string> dmTexts(const uint8_t* key) {
    std::vector<std::string> out;
    for (int j = hist->dmHistCountForContact(key) - 1; j >= 0; j--)
      out.push_back(hist->dmAtPos(hist->dmHistEntryForContact(key, j)).text);
    return out;
  }
  // The page now in MessageHistory::older, oldest first.
  std::vector<std::string> pageDm() {
    std::vector<std::string> out;
    for (int i = log->pageCount() - 1; i >= 0; i--) out.push_back(hist->older[i].dm.text);
    return out;
  }
  std::vector<std::string> pageCh() {
    std::vector<std::string> out;
    for (int i = log->pageCount() - 1; i >= 0; i--) out.push_back(hist->older[i].ch.text);
    return out;
  }
  static std::string n(const char* p, int i) { return p + std::to_string(i); }
  static std::vector<std::string> range(const char* p, int from, int to) {
    std::vector<std::string> v;
    for (int i = from; i <= to; i++) v.push_back(n(p, i));
    return v;
  }
};

// ── When it writes ─────────────────────────────────────────────────────────

TEST_F(Log, KeepMessagesOffWritesNothing) {
  prefs.msg_keep = 0;
  start();
  ch(0, "x");
  dmIn(KEY_A, "y");
  tick(HistoryLog::SAVE_DELAY_MS);
  log->onShutdown();
  EXPECT_FALSE(fs.exists("/h"));
}

TEST_F(Log, ABatchIsWrittenADelayAfterTheFirstChange) {
  ch(0, "first");
  tick(HistoryLog::SAVE_DELAY_MS / 2);
  ch(0, "second");   // doesn't push the save back
  tick(HistoryLog::SAVE_DELAY_MS / 2 - 1);
  EXPECT_FALSE(fs.exists("/h/c1/l"));
  tick(1);
  ASSERT_TRUE(fs.exists("/h/c1/l"));
  start();   // no shutdown: only what was saved
  EXPECT_EQ((std::vector<std::string>{"first", "second"}), chTexts(0));
}

TEST_F(Log, ABurstOfMessagesIsWrittenAtOnce) {
  for (int i = 0; i < HistoryLog::SAVE_BURST - 1; i++) dmIn(KEY_A, "m");
  tick(1);
  EXPECT_FALSE(fs.exists("/h/d1/l"));
  dmIn(KEY_A, "m");
  tick(1);
  EXPECT_TRUE(fs.exists("/h/d1/l"));
}

TEST_F(Log, ShutdownWritesWhatIsPending) {
  ch(1, "late");
  restart();
  EXPECT_EQ((std::vector<std::string>{"late"}), chTexts(1));
}

TEST_F(Log, ADmAwaitingItsAckHoldsBackItAndWhatFollows) {
  dmIn(KEY_A, "before");
  rtc_clock.now++;
  hist->addDMMsg(KEY_A, true, "sent", rtc_clock.now, 0x55, 999999, 0);
  dmIn(KEY_B, "after");
  tick(HistoryLog::SAVE_DELAY_MS);
  start();
  EXPECT_EQ((std::vector<std::string>{"before"}), dmTexts(KEY_A));
  EXPECT_TRUE(dmTexts(KEY_B).empty());
}

TEST_F(Log, TheDeliveredStateIsWhatIsKept) {
  rtc_clock.now++;
  hist->addDMMsg(KEY_A, true, "sent", rtc_clock.now, 0x55, 999999, 0);
  tick(HistoryLog::SAVE_DELAY_MS);
  hist->markDmDelivered(0x55);
  tick(HistoryLog::SAVE_DELAY_MS);
  start();
  ASSERT_EQ(1, hist->dmHistCount());
  const DmHistEntry& e = hist->dmAtPos(hist->dmHistPosNewest(0));
  EXPECT_EQ(ACK_OK, e.ack_status);
  EXPECT_EQ(1, e.outgoing);
  EXPECT_STREQ("sent", e.text);
}

TEST_F(Log, AnUnechoedOwnPostWaitsToSettle) {
  const int pos = hist->addChannelMsg(0, "Me: hi", true, 0, nullptr, 0, true);
  hist->armChannelRelay(pos, 7);
  tick(HistoryLog::SAVE_DELAY_MS);
  EXPECT_FALSE(fs.exists("/h/c1/l"));
  rtc_clock.now += HistoryLog::SETTLE_SECS;
  tick(HistoryLog::SAVE_DELAY_MS);
  EXPECT_TRUE(fs.exists("/h/c1/l"));
}

TEST_F(Log, AChannelWithSaveOffStaysOut) {
  chanctl::setSaved(&prefs, 1, false);
  ch(1, "busy");
  ch(0, "kept");
  restart();
  EXPECT_TRUE(chTexts(1).empty());
  EXPECT_EQ((std::vector<std::string>{"kept"}), chTexts(0));
}

// ── What comes back ────────────────────────────────────────────────────────

TEST_F(Log, ARestartBringsBackTextPathAndOrder) {
  const uint8_t path[3] = {7, 8, 9};
  rtc_clock.now++;
  hist->addChannelMsg(0, "with path", true, rtc_clock.now, path, 3);
  ch(1, "other");
  dmIn(KEY_A, "dm \xC4\x85");
  restart();
  EXPECT_EQ((std::vector<std::string>{"with path"}), chTexts(0));
  EXPECT_EQ((std::vector<std::string>{"other"}), chTexts(1));
  EXPECT_EQ((std::vector<std::string>{"dm \xC4\x85"}), dmTexts(KEY_A));
  const ChHistEntry& e = hist->chAtPos(hist->histEntryForChannel(0, 0));
  EXPECT_EQ(3, e.path_len);
  EXPECT_EQ(0, memcmp(e.path, path, 3));
  EXPECT_EQ(0, hist->getTotalChannelUnread());   // restored: nothing new
}

TEST_F(Log, TheRingsGetTheNewestOfALongerLog) {
  const int total = DM_RING + 10;
  for (int i = 0; i < total; i++) dmIn(KEY_A, n("d", i).c_str());
  restart();
  EXPECT_EQ(range("d", total - DM_RING, total - 1), dmTexts(KEY_A));
}

TEST_F(Log, MessagesFollowTheirChannelToAnotherSlot) {
  ch(1, "hike talk");
  restart();
  the_mesh.channels[1] = ChannelDetails{};
  setChannel(5, "#hike", 0x22);   // the same secret, re-added elsewhere
  start();
  EXPECT_TRUE(chTexts(1).empty());
  EXPECT_EQ((std::vector<std::string>{"hike talk"}), chTexts(5));
}

TEST_F(Log, ADeletedChannelsMessagesDontComeBack) {
  ch(1, "gone");
  ch(0, "stays");
  restart();
  the_mesh.channels[1] = ChannelDetails{};
  start();
  EXPECT_EQ(1, hist->chHistCount());
  EXPECT_EQ((std::vector<std::string>{"stays"}), chTexts(0));
}

TEST_F(Log, ATruncatedTailCostsOnlyTheBrokenRecord) {
  for (int i = 0; i < 3; i++) dmIn(KEY_A, n("d", i).c_str());
  restart();
  std::vector<uint8_t>* f = fs.fileMut("/h/d1/l");
  ASSERT_NE(nullptr, f);
  f->resize(f->size() - 3);   // the last record cut short
  start();
  EXPECT_EQ(range("d", 0, 1), dmTexts(KEY_A));
}

// ── Segments ───────────────────────────────────────────────────────────────

TEST_F(Log, OldSegmentsAreDroppedWholeKeepingTheNewest) {
  // DM segments hold 20 here: 8 segments' worth and 3 more.
  const int per = 500 / 3 / SEGMENTS;
  const int total = per * SEGMENTS + 3;
  for (int i = 0; i < total; i++) dmIn(KEY_A, n("d", i).c_str());
  restart();
  EXPECT_FALSE(fs.isDir("/h/d1"));
  EXPECT_TRUE(fs.isDir("/h/d2"));
  EXPECT_TRUE(fs.isDir("/h/d9"));
  int dm_dirs = 0;
  for (int s = 1; s <= 9; s++) dm_dirs += fs.isDir("/h/d" + std::to_string(s));
  EXPECT_EQ(SEGMENTS, dm_dirs);
  EXPECT_EQ(range("d", total - DM_RING, total - 1), dmTexts(KEY_A));
}

// ── Older messages, a page at a time ───────────────────────────────────────

TEST_F(Log, OlderDmsComePageByPageAndBack) {
  // Another contact's messages between: a page is found among them.
  const int in_ring = DM_RING / 2, total = in_ring + 2 * PAGE + 5;
  for (int i = 0; i < total; i++) {
    dmIn(KEY_A, n("d", i).c_str());
    dmIn(KEY_B, "noise");
  }
  restart();
  ASSERT_EQ(range("d", total - in_ring, total - 1), dmTexts(KEY_A));
  const int top = total - in_ring - 1;   // the newest past the ring

  ASSERT_TRUE(log->pageBeginDm(KEY_A));
  const std::vector<std::string> p1 = pageDm();
  EXPECT_EQ(range("d", top - PAGE + 1, top), p1);
  ASSERT_TRUE(log->pageOlder());
  const std::vector<std::string> p2 = pageDm();
  EXPECT_EQ(range("d", top - 2 * PAGE + 1, top - PAGE), p2);
  ASSERT_TRUE(log->pageOlder());
  const std::vector<std::string> p3 = pageDm();
  EXPECT_EQ(range("d", 0, top - 2 * PAGE), p3);
  EXPECT_FALSE(log->pageOlder());   // the start of the log: the page stays
  EXPECT_EQ(p3, pageDm());

  ASSERT_TRUE(log->pageNewer());
  EXPECT_EQ(p2, pageDm());
  ASSERT_TRUE(log->pageNewer());
  EXPECT_EQ(p1, pageDm());
  EXPECT_FALSE(log->pageNewer());   // the ring takes over
}

TEST_F(Log, AChannelsOlderPageHasOnlyThatChannel) {
  const int each = CH_RING / 2 + 5;
  for (int i = 0; i < each; i++) {
    ch(0, n("p", i).c_str());
    ch(1, n("h", i).c_str());
  }
  restart();
  EXPECT_EQ(range("h", 5, each - 1), chTexts(1));
  ASSERT_TRUE(log->pageBeginCh(1));
  EXPECT_EQ(range("h", 0, 4), pageCh());
  for (int i = 0; i < log->pageCount(); i++) EXPECT_EQ(1, hist->older[i].ch.ch_idx);
}

TEST_F(Log, NoOlderPageWhenTheRingHoldsItAll) {
  dmIn(KEY_A, "only");
  restart();
  EXPECT_FALSE(log->pageBeginDm(KEY_A));
  EXPECT_EQ(nullptr, hist->older);
}

// ── Delete after, Delete history ───────────────────────────────────────────

TEST_F(Log, DeleteAfterHidesOlderMessagesAtOnce) {
  dmIn(KEY_A, "old");
  rtc_clock.now += 8 * 86400;
  dmIn(KEY_A, "new");
  restart();
  prefs.msg_expire = 1;   // 7 days
  start();
  EXPECT_EQ((std::vector<std::string>{"new"}), dmTexts(KEY_A));
}

TEST_F(Log, PruneFreesOldSegmentsButNeverTheNewest) {
  const int per = 500 / 3 / SEGMENTS;
  for (int i = 0; i < per; i++) dmIn(KEY_A, "old");   // segment 1, full
  tick(HistoryLog::SAVE_DELAY_MS);
  rtc_clock.now += 30 * 86400;
  dmIn(KEY_A, "new");                                // segment 2
  tick(HistoryLog::SAVE_DELAY_MS);
  ASSERT_TRUE(fs.isDir("/h/d1"));
  ASSERT_TRUE(fs.isDir("/h/d2"));
  prefs.msg_expire = 1;
  tick(HistoryLog::PRUNE_EVERY_MS);
  EXPECT_FALSE(fs.isDir("/h/d1"));
  EXPECT_TRUE(fs.isDir("/h/d2"));
  rtc_clock.now += 30 * 86400;
  tick(HistoryLog::PRUNE_EVERY_MS);
  EXPECT_TRUE(fs.isDir("/h/d2"));   // the newest stays, however old
}

TEST_F(Log, AClockNotSetNeverDeletes) {
  dmIn(KEY_A, "kept");
  restart();
  prefs.msg_expire = 1;
  rtc_clock.now = 1000;   // not set (after a power loss, no GPS yet)
  start();
  EXPECT_EQ((std::vector<std::string>{"kept"}), dmTexts(KEY_A));
}

TEST_F(Log, DeleteHistoryClearsTheLogAndTheRings) {
  ch(0, "x");
  dmIn(KEY_A, "y");
  restart();
  log->erase();
  EXPECT_FALSE(fs.exists("/h"));
  EXPECT_EQ(0, hist->chHistCount());
  EXPECT_EQ(0, hist->dmHistCount());
  start();
  EXPECT_EQ(0, hist->dmHistCount());
}

TEST_F(Log, KeepOffAtAStartDeletesTheLog) {
  dmIn(KEY_A, "y");
  restart();
  prefs.msg_keep = 0;
  start();
  EXPECT_FALSE(fs.exists("/h"));
}

// ── The flash failing ──────────────────────────────────────────────────────

TEST_F(Log, AFailedWriteIsTriedAgain) {
  dmIn(KEY_A, "d0");
  tick(HistoryLog::SAVE_DELAY_MS);
  fs.failWritesAfter(0);   // full
  dmIn(KEY_A, "d1");
  tick(HistoryLog::SAVE_DELAY_MS);
  fs.failWritesAfter(-1);   // room again: the next flush, with nothing new
  tick(HistoryLog::SAVE_DELAY_MS);
  start();
  EXPECT_EQ(range("d", 0, 1), dmTexts(KEY_A));
}

TEST_F(Log, AWriteFailingMidBatchLosesNoneOfIt) {
  // LittleFS keeps none of a batch whose write failed: the records before
  // the failing one aren't on the flash either, and are written again.
  dmIn(KEY_A, "d0");
  tick(HistoryLog::SAVE_DELAY_MS);
  dmIn(KEY_A, "d1");
  dmIn(KEY_A, "d2");
  dmIn(KEY_A, "d3");
  fs.failWritesAfter(2 * (15 + 2 + 1) + 5);   // d1, d2 and a part of d3
  tick(HistoryLog::SAVE_DELAY_MS);
  EXPECT_EQ(1u * (15 + 2 + 1), fs.file("/h/d1/l")->size());   // only d0
  fs.failWritesAfter(-1);
  tick(HistoryLog::SAVE_DELAY_MS);
  start();
  EXPECT_EQ(range("d", 0, 3), dmTexts(KEY_A));
}

TEST_F(Log, ARecordCutShortCostsNothingBeforeOrAfterIt) {
  fs.keepCutWrites(true);   // SPIFFS: what went in stays
  dmIn(KEY_A, "d0");
  tick(HistoryLog::SAVE_DELAY_MS);
  fs.failWritesAfter(5);   // a part of the next record
  dmIn(KEY_A, "d1");
  tick(HistoryLog::SAVE_DELAY_MS);
  fs.failWritesAfter(-1);
  dmIn(KEY_A, "d2");
  restart();
  EXPECT_TRUE(fs.isDir("/h/d2"));   // after the cut, another segment
  EXPECT_EQ(range("d", 0, 2), dmTexts(KEY_A));
  start();   // and again: the cut segment takes no more
  dmIn(KEY_A, "d3");
  restart();
  EXPECT_EQ(range("d", 0, 3), dmTexts(KEY_A));
}

TEST_F(Log, OlderPagesReadPastACutRecord) {
  fs.keepCutWrites(true);
  const int total = DM_RING + 5;
  for (int i = 0; i < total; i++) dmIn(KEY_A, n("d", i).c_str());
  tick(HistoryLog::SAVE_DELAY_MS);
  fs.failWritesAfter(5);
  dmIn(KEY_A, "late");
  tick(HistoryLog::SAVE_DELAY_MS);
  fs.failWritesAfter(-1);
  restart();
  ASSERT_TRUE(log->pageBeginDm(KEY_A));
  EXPECT_EQ(range("d", 0, 5), pageDm());
}

TEST_F(Log, APowerCutBeforeASaveLeavesTheLogAsItWas) {
  dmIn(KEY_A, "d0");
  tick(HistoryLog::SAVE_DELAY_MS);
  dmIn(KEY_A, "d1");
  fs.crash();
  start();   // no shutdown
  EXPECT_EQ((std::vector<std::string>{"d0"}), dmTexts(KEY_A));
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
