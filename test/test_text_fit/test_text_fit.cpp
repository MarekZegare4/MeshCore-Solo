#include <gtest/gtest.h>

#include <cstring>

// Text cut to fit on a whole UTF-8 character (TextFit.h).
#include "../../examples/companion_radio/TextFit.h"

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
  char b[3];   // Latin-1 "ééé": the byte after the cut is no continuation, so both stay
  EXPECT_EQ(textfit::copy(b, sizeof(b), "\xE9\xE9\xE9"), 2);
  char d[3];   // fmt sees only what fit: the last 0xE9 reads as a lead missing its tail
  EXPECT_EQ(textfit::fmt(d, sizeof(d), "%s", "\xE9\xE9\xE9"), 1);
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

TEST(TextFit, CopyNPartOfASource) {
  const char* t = "Łukasz: cześć";      // a name before its separator
  char b[16];
  EXPECT_EQ(textfit::copyN(b, sizeof(b), t, 7), 7);
  EXPECT_STREQ(b, "Łukasz");
  char c[3];                             // "Ł" whole, "u" doesn't fit
  EXPECT_EQ(textfit::copyN(c, sizeof(c), t, 7), 2);
  EXPECT_STREQ(c, "Ł");
  char d[2];                             // half of "Ł": nothing
  EXPECT_EQ(textfit::copyN(d, sizeof(d), t, 7), 0);
  EXPECT_STREQ(d, "");
  char e[8];                             // a field not NUL-terminated
  const char f[4] = {'a', 'b', 'c', 'd'};
  EXPECT_EQ(textfit::copyN(e, sizeof(e), f, 3), 3);
  EXPECT_STREQ(e, "abc");
}

TEST(TextFit, CutAt) {
  EXPECT_EQ(textfit::cutAt("ałb", 4, 9), 4u);   // fits
  EXPECT_EQ(textfit::cutAt("ałb", 4, 2), 1u);   // inside "ł"
  EXPECT_EQ(textfit::cutAt("ałb", 4, 3), 3u);   // after it
  EXPECT_EQ(textfit::cutAt("\x80\x80\x80\x80\x80", 5, 4), 4u);   // no lead near: cut where it is
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
