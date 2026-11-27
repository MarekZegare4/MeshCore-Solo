#include <gtest/gtest.h>

#include <cstring>

// Text cut to fit on a whole UTF-8 character (ui-core/TextFit.h).
#include "../../examples/companion_radio/ui-core/TextFit.h"

TEST(TextFit, FitsWhole) {
  char b[16];
  EXPECT_EQ(textfit::fmt(b, sizeof(b), "#%s", "Łódź"), 8);
  EXPECT_STREQ(b, "#Łódź");
}

TEST(TextFit, AsciiCutAtTheByte) {
  char b[4];
  EXPECT_EQ(textfit::copy(b, sizeof(b), "abcdef"), 3);
  EXPECT_STREQ(b, "abc");
}

TEST(TextFit, TwoByteLetterNotSplit) {
  char b[3];   // room for "a" and half of "ł"
  EXPECT_EQ(textfit::copy(b, sizeof(b), "ałb"), 1);
  EXPECT_STREQ(b, "a");
  char c[4];   // "a" and the whole "ł"
  EXPECT_EQ(textfit::copy(c, sizeof(c), "ałb"), 3);
  EXPECT_STREQ(c, "ał");
}

TEST(TextFit, ThreeAndFourByteCharacters) {
  const char* euro = "x\xE2\x82\xAC";           // x€
  char b[4];                                    // "x" + 2 of the 3 bytes
  EXPECT_EQ(textfit::copy(b, sizeof(b), euro), 1);
  EXPECT_STREQ(b, "x");
  const char* emoji = "\xF0\x9F\x98\x80" "a";   // 😀a
  char e[5];                                    // the 4 bytes, no room for "a"
  EXPECT_EQ(textfit::copy(e, sizeof(e), emoji), 4);
  EXPECT_STREQ(e, "\xF0\x9F\x98\x80");
  char f[4];                                    // 3 of the 4
  EXPECT_EQ(textfit::copy(f, sizeof(f), emoji), 0);
  EXPECT_STREQ(f, "");
}

TEST(TextFit, NotUtf8IsKept) {
  char b[3];   // Latin-1 "é": the last 0xE9 reads as a 3-byte lead missing its tail, the rest stays
  EXPECT_EQ(textfit::copy(b, sizeof(b), "\xE9\xE9\xE9"), 1);
  char c[3];
  EXPECT_EQ(textfit::copy(c, sizeof(c), "\x80\x80\x80"), 2);   // lone continuation bytes: kept
}

TEST(TextFit, EdgeCases) {
  char b[1];
  EXPECT_EQ(textfit::copy(b, sizeof(b), "abc"), 0);
  EXPECT_STREQ(b, "");
  EXPECT_EQ(textfit::copy(b, 0, "abc"), 0);
  char c[8];
  EXPECT_EQ(textfit::copy(c, sizeof(c), nullptr), 0);
  EXPECT_STREQ(c, "");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
