// The spaced-sign rule (lib/EpdFont/SpacedSignWhite.h): a space beside a math
// sign is set so the visible white on both sides of the sign is one target.

#include <SpacedSignWhite.h>
#include <gtest/gtest.h>

using spacedsign::GlyphBox;

namespace {

// White drawn between two glyphs across a space: rsb(left) + space + lsb(right).
int whiteAcross(const GlyphBox& left, const int spacePx, const GlyphBox& right) {
  return left.rightBearingPx() + spacePx + right.leftPx;
}

}  // namespace

TEST(SpacedSignWhite, SignSetIsTheMathSignsAndNotTheProseMarks) {
  for (const uint32_t cp : {0x2Bu, 0x3Cu, 0x3Du, 0x3Eu, 0xB1u, 0xD7u, 0xF7u, 0x2212u, 0x2213u, 0x2243u, 0x2245u,
                            0x2248u, 0x2260u, 0x2261u, 0x2264u, 0x2265u}) {
    EXPECT_TRUE(spacedsign::isSign(cp)) << std::hex << cp;
  }
  // Spaced, these are dashes, separators or emphasis in prose.
  for (const uint32_t cp : {static_cast<uint32_t>('-'), 0x2013u, 0x2014u, static_cast<uint32_t>('/'),
                            static_cast<uint32_t>('*'), static_cast<uint32_t>('~'), 0xB7u, static_cast<uint32_t>(' '),
                            static_cast<uint32_t>('0'), static_cast<uint32_t>('n')}) {
    EXPECT_FALSE(spacedsign::isSign(cp)) << std::hex << cp;
  }
}

TEST(SpacedSignWhite, AppliesOnlyBesideASign) {
  EXPECT_TRUE(spacedsign::applies('1', '+'));
  EXPECT_TRUE(spacedsign::applies('+', '1'));
  EXPECT_TRUE(spacedsign::applies('=', 0x2212));
  EXPECT_FALSE(spacedsign::applies('1', '1'));
  EXPECT_FALSE(spacedsign::applies('n', 'n'));  // the layout's reference space
  EXPECT_FALSE(spacedsign::applies('a', '-'));
}

TEST(SpacedSignWhite, TargetIsTheFiguresWhiteAcrossASpace) {
  const GlyphBox zero{7, 1, 5};  // rsb 1
  EXPECT_EQ(spacedsign::targetWhitePx(zero, 4), 1 + 4 + 1);
}

// The property the owner asked for: equal white left and right of the sign,
// whatever the neighbors' bearings, and equal to the target.
TEST(SpacedSignWhite, BothSidesOfTheSignGetTheSameWhite) {
  const int target = 8;
  const GlyphBox one{7, 2, 2};   // a "1": wide bearings
  const GlyphBox plus{7, 0, 7};  // a sign drawn edge to edge
  const GlyphBox four{7, 0, 7};  // a "4": no bearings
  const GlyphBox x{6, 0, 6};
  for (const GlyphBox& before : {one, four, x}) {
    for (const GlyphBox& after : {one, four, x}) {
      const int leftSpace = spacedsign::spaceForWhitePx(before, plus, target);
      const int rightSpace = spacedsign::spaceForWhitePx(plus, after, target);
      EXPECT_EQ(whiteAcross(before, leftSpace, plus), target);
      EXPECT_EQ(whiteAcross(plus, rightSpace, after), target);
    }
  }
}

TEST(SpacedSignWhite, BearingsWiderThanTheTargetFloorTheSpaceAtZero) {
  const GlyphBox wide{12, 5, 2};  // rsb 5, lsb 5
  EXPECT_EQ(spacedsign::spaceForWhitePx(wide, wide, 6), 0);
}

TEST(SpacedSignWhite, NegativeBearingsWidenTheSpace) {
  const GlyphBox overhang{6, -1, 8};  // ink past both edges: rsb -1, lsb -1
  EXPECT_EQ(spacedsign::spaceForWhitePx(overhang, overhang, 6), 8);
}

// The converter keeps FreeType's box but quantizes faint coverage to level 0,
// so the drawn columns can be narrower than the box. 2 bits per pixel, high
// bits first, continuous across rows (drawText's packing).
TEST(SpacedSignWhite, DrawnColumnsSkipLevelZeroEdgeColumns2Bit) {
  // 5 x 2 glyph. Row 0: 0 0 3 1 0. Row 1: 0 2 0 0 0. Drawn columns 1..3.
  // Pixels in order: 0 0 3 1 | 0 0 2 0 | 0 0 -> bytes 0b00001101, 0b00001000, 0b00000000
  const uint8_t bitmap[] = {0b00001101, 0b00001000, 0b00000000};
  int first = -1, last = -1;
  ASSERT_TRUE(spacedsign::drawnColumns(bitmap, 5, 2, true, first, last));
  EXPECT_EQ(first, 1);
  EXPECT_EQ(last, 3);
}

TEST(SpacedSignWhite, DrawnColumns1Bit) {
  // 4 x 2, MSB first. Row 0: 0 0 1 0. Row 1: 0 0 0 1. Drawn columns 2..3.
  const uint8_t bitmap[] = {0b00100001};
  int first = -1, last = -1;
  ASSERT_TRUE(spacedsign::drawnColumns(bitmap, 4, 2, false, first, last));
  EXPECT_EQ(first, 2);
  EXPECT_EQ(last, 3);
}

TEST(SpacedSignWhite, DrawnColumnsOfABlankAreNone) {
  const uint8_t blank[] = {0, 0};
  int first = 0, last = 0;
  EXPECT_FALSE(spacedsign::drawnColumns(blank, 4, 2, true, first, last));
  EXPECT_FALSE(spacedsign::drawnColumns(nullptr, 4, 2, true, first, last));
}
