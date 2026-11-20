#include <gtest/gtest.h>

#include <string>

#include <mecp.h>

using mecp::Beacon;
using mecp::Code;
using mecp::Message;
using mecp::Severity;

static bool decode(const char* s, Message& m) { return Message::decode(s, m); }

// ---------- Code (compile time) ----------

static_assert(Code() == Code('\0', 0), "default is no code");
static_assert(Code('B', 1) != Code('B', 2), "");
static_assert(Code("B01") == Code('B', 1), "");
static_assert(Code("A00") == Code('A', 0), "");
static_assert(Code("Z99 tail") == Code('Z', 99), "a space ends a code");
static_assert(Code("B01x") == Code(), "anything else after a code is not one");
static_assert(Code("b01") == Code(), "category must be uppercase");
static_assert(Code("BB1") == Code(), "number must be digits");
static_assert(Code("B1") == Code(), "too short");
static_assert(Code("B") == Code(), "too short");
static_assert(Code("") == Code(), "empty");

TEST(MecpDecode, SeverityAndCodes) {
  Message m;
  ASSERT_TRUE(decode("MECP/1/B01 M01 hello world", m));
  EXPECT_EQ(Severity::Urgent, m.severity);
  ASSERT_EQ(2u, m.code_count);
  EXPECT_EQ(Code('B', 1), m.codes[0]);
  EXPECT_EQ(Code('M', 1), m.codes[1]);
}

TEST(MecpDecode, BeaconKinds) {
  Message m;
  ASSERT_TRUE(decode("MECP/0/B01 M01 P05 48.65,20.13 @1430", m));
  EXPECT_EQ(Beacon::Active, m.beacon());
  EXPECT_EQ(Severity::Mayday, m.severity);

  ASSERT_TRUE(decode("MECP/2/B02 R01", m));
  EXPECT_EQ(Beacon::Ack, m.beacon());

  ASSERT_TRUE(decode("MECP/2/B03", m));
  EXPECT_EQ(Beacon::Cancel, m.beacon());

  ASSERT_TRUE(decode("MECP/0/M01 M07 P05 2pax", m));
  EXPECT_EQ(Beacon::None, m.beacon());

  EXPECT_TRUE(m.hasCode({ 'M', 7 }));
  EXPECT_FALSE(m.hasCode({ 'B', 1 }));
}

TEST(MecpDecode, CodesEndAtFirstTokenThatIsNotACode) {
  Message m;
  ASSERT_TRUE(decode("MECP/0/B01 B02x", m));  // "B02x" is freetext
  EXPECT_EQ(1u, m.code_count);
  ASSERT_TRUE(decode("MECP/0/B01 hello B02", m));
  EXPECT_EQ(1u, m.code_count);
  ASSERT_TRUE(decode("MECP/0/B01 ", m));
  EXPECT_EQ(1u, m.code_count);
  ASSERT_TRUE(decode("MECP/0/B01 M0", m));
  EXPECT_EQ(1u, m.code_count);
}

TEST(MecpDecode, Rejects) {
  Message m;
  EXPECT_FALSE(decode(nullptr, m));
  EXPECT_FALSE(decode("", m));
  EXPECT_FALSE(decode("MECP/", m));
  EXPECT_FALSE(decode("hello B01", m));
  EXPECT_FALSE(decode("[LOC]48.65,20.13", m));
  EXPECT_FALSE(decode("MECP/9/B01", m));      // bad severity
  EXPECT_FALSE(decode("MECP/0/", m));         // no codes
  EXPECT_FALSE(decode("MECP/0/ B01", m));     // no codes
  EXPECT_FALSE(decode("MECP/0/B01M01", m));   // missing delimiter
  EXPECT_FALSE(decode("MECP/0/b01", m));      // malformed code
  EXPECT_FALSE(decode("Alice: MECP/0/B01", m));  // channel prefix not stripped
}

TEST(MecpDecode, ResetsCodesFromAPreviousDecode) {
  Message m;
  ASSERT_TRUE(decode("MECP/0/B01 M01 P05", m));
  ASSERT_TRUE(decode("MECP/3/L04", m));
  EXPECT_EQ(Severity::Routine, m.severity);
  EXPECT_EQ(1u, m.code_count);
  EXPECT_FALSE(m.hasCode({ 'M', 1 }));
}

TEST(MecpDecode, AtMostMaxCodes) {
  std::string s = "MECP/2/B01";
  for (size_t i = 1; i < mecp::kMaxCodes; i++) s += " M01";
  ASSERT_EQ(158u, s.size());  // fits MeshCore's 160-byte MAX_TEXT_LEN
  Message m;
  ASSERT_TRUE(decode(s.c_str(), m));
  EXPECT_EQ(mecp::kMaxCodes, m.code_count);
  EXPECT_TRUE(decode((s + " freetext").c_str(), m));
  ASSERT_TRUE(decode((s + " B03").c_str(), m));  // codes past kMaxCodes are dropped
  EXPECT_EQ(mecp::kMaxCodes, m.code_count);
  EXPECT_FALSE(m.hasCode({ 'B', 3 }));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
