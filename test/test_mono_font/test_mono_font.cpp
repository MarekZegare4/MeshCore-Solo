#include <gtest/gtest.h>

#include <string>

// The misc-fixed fonts in MonoFont form (tools/bdf2mono.py): a bit per
// codepoint for whether the font draws it, a rank table to find its cell.
#include "helpers/ui/MiscFixedFont.h"
#include "helpers/ui/MiscFixed8x13Font.h"
#include "helpers/ui/MiscFixed9x15Font.h"

// A glyph's cell as rows of '#' and '.', the way MiscFixedRenderer draws it
// (row r at y + r); "" for a blank one.
static std::string rows(const MonoFont& f, uint32_t cp) {
  const uint8_t* bm = f.glyph(cp);
  if (!bm) return "";
  std::string s;
  int bit = 0;
  for (int r = 0; r < f.h; r++) {
    if (r) s += '/';
    for (int c = 0; c < f.w; c++, bit++) s += (bm[bit >> 3] >> (7 - (bit & 7))) & 1 ? '#' : '.';
  }
  return s;
}

static int drawn(const MonoFont& f) {
  const int words = (f.last - f.first + 1 + 31) / 32;
  return f.rank[words - 1] + __builtin_popcount(f.defined[words - 1]);
}

// The rank table matches the bits, and the bitmap holds exactly one cell per
// drawn codepoint -- a glyph never reads past the end or into its neighbour.
static void checkTables(const MonoFont& f, size_t bitmap_bytes) {
  const int words = (f.last - f.first + 1 + 31) / 32;
  int n = 0;
  for (int i = 0; i < words; i++) {
    EXPECT_EQ(f.rank[i], n) << "word " << i;
    n += __builtin_popcount(f.defined[i]);
  }
  EXPECT_EQ(bitmap_bytes, (size_t)drawn(f) * f.cellBytes());
  // The bits past `last` in the last word are clear.
  const int tail = (f.last - f.first + 1) % 32;
  if (tail) EXPECT_EQ(f.defined[words - 1] >> tail, 0u);
  // Codepoints come in order: each drawn one's cell is the next after the last.
  const uint8_t* prev = nullptr;
  for (uint32_t cp = f.first; cp <= f.last; cp++) {
    const uint8_t* g = f.glyph(cp);
    if (!g) continue;
    EXPECT_EQ(g, prev ? prev + f.cellBytes() : f.bitmap) << std::hex << cp;
    prev = g;
  }
}

TEST(MonoFont, Tables6x9) { checkTables(MiscFixed, sizeof(MiscFixedBitmaps)); }
TEST(MonoFont, Tables8x13) { checkTables(MiscFixed8x13, sizeof(MiscFixed8x13Bitmaps)); }
TEST(MonoFont, Tables9x15) { checkTables(MiscFixed9x15, sizeof(MiscFixed9x15Bitmaps)); }

TEST(MonoFont, Metrics) {
  EXPECT_EQ(MiscFixed.w, 6);      EXPECT_EQ(MiscFixed.h, 9);      EXPECT_EQ(MiscFixed.ascent, 7);
  EXPECT_EQ(MiscFixed8x13.w, 8);  EXPECT_EQ(MiscFixed8x13.h, 13); EXPECT_EQ(MiscFixed8x13.ascent, 11);
  EXPECT_EQ(MiscFixed9x15.w, 9);  EXPECT_EQ(MiscFixed9x15.h, 15); EXPECT_EQ(MiscFixed9x15.ascent, 12);
  EXPECT_EQ(drawn(MiscFixed), 618);
  EXPECT_EQ(drawn(MiscFixed9x15), 1112);
}

TEST(MonoFont, KnownGlyphs) {
  EXPECT_EQ(rows(MiscFixed, 'A'), "....../..#.../.#.#../#...#./#####./#...#./#...#./....../......");
  // A space is a drawn, empty cell.
  EXPECT_EQ(rows(MiscFixed, ' '), "....../....../....../....../....../....../....../....../......");
  EXPECT_EQ(rows(MiscFixed, 0x0416), "....../#.#.#./#.#.#./.###../#.#.#./#.#.#./#.#.#./....../......");  // Ж
  // The 9x15's last glyph is U+04F9 (ӹ), in the last word of the bits; U+04FF is blank.
  EXPECT_NE(rows(MiscFixed9x15, 0x04F9), "");
  EXPECT_EQ(MiscFixed9x15.glyph(0x04F9) + MiscFixed9x15.cellBytes(),
            MiscFixed9x15Bitmaps + sizeof(MiscFixed9x15Bitmaps));
  EXPECT_EQ(MiscFixed9x15.glyph(0x04FF), nullptr);
}

TEST(MonoFont, DescenderInsideTheCell) {
  // The 9x15's 'g' reaches the cell's last row: drawn from the top of the
  // cell, it stays in its own text row.
  const std::string g = rows(MiscFixed9x15, 'g');
  ASSERT_EQ(g.size(), 15u * 10 - 1);
  EXPECT_NE(g.substr(14 * 10, 9).find('#'), std::string::npos);
}

TEST(MonoFont, BlankAndOutside) {
  // In the range but not drawn: no glyph (a blank cell), the same as before.
  EXPECT_TRUE(MiscFixed.covers(0x7F));
  EXPECT_EQ(MiscFixed.glyph(0x7F), nullptr);
  // Outside the range: no glyph, and covers() says so (the substitution box).
  EXPECT_FALSE(MiscFixed.covers(0x1F));
  EXPECT_FALSE(MiscFixed.covers(0x500));
  EXPECT_EQ(MiscFixed.glyph(0x1F), nullptr);
  EXPECT_EQ(MiscFixed.glyph(0x500), nullptr);
  EXPECT_EQ(MiscFixed.glyph(0xE03B), nullptr);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
