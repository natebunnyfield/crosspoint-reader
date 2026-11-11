// The spaced-sign rule (lib/EpdFont/SpacedSignWhite.h): a space beside a LONE
// math sign is set so the visible white on both sides of the sign is one
// target; a sign is ruled on both sides or on neither.

#include <SpacedSignWhite.h>
#include <gtest/gtest.h>

using spacedsign::FarSide;
using spacedsign::GlyphBox;
using spacedsign::Sign;

namespace {

// White drawn between two glyphs across a space: rsb(left) + space + lsb(right).
int whiteAcross(const GlyphBox& left, const int spacePx, const GlyphBox& right) {
  return left.rightBearingPx() + spacePx + right.leftPx;
}

const FarSide kEdge{};                     // paragraph edge
const FarSide kSpaced{true, true, false};  // an ordinary space to a full-size word
const FarSide kGlued{true, false, false};  // a no-break space or an attached token
const FarSide kSupSub{true, true, true};   // a superscript/subscript word

Sign decide(const char* l, const char* r, const FarSide& before = kSpaced, const FarSide& after = kSpaced) {
  return spacedsign::decide(l, false, r, false, before, after);
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

TEST(SpacedSignWhite, ALoneSignIsAWordThatIsExactlyOneSign) {
  for (const char* w : {"+", "=", "<", ">", "\xC3\x97" /* × */, "\xE2\x88\x92" /* − */, "\xE2\x89\xA5" /* ≥ */}) {
    EXPECT_TRUE(spacedsign::isLoneSign(w)) << w;
  }
  for (const char* w : {"", "C++", "A+", "18+", "<div>", "->", "!=", "-=", "==", "+1", "=7.", "-", "x",
                        "+\xCC\x84" /* + with a combining macron */}) {
    EXPECT_FALSE(spacedsign::isLoneSign(w)) << w;
  }
  EXPECT_FALSE(spacedsign::isLoneSign(nullptr));
}

TEST(SpacedSignWhite, SpacedSumsAreRuledOnTheSignSide) {
  EXPECT_EQ(decide("1", "+"), Sign::Right);
  EXPECT_EQ(decide("+", "1"), Sign::Left);
  EXPECT_EQ(decide("x", "\xE2\x88\x92"), Sign::Right);       // x −
  EXPECT_EQ(decide("=", "\xE2\x88\x92"), Sign::Right);       // = −: the right sign owns the gap
  EXPECT_EQ(decide("1", "+", kSpaced, kEdge), Sign::Right);  // a sign ending the paragraph
}

// Prose and code that merely CONTAIN a sign keep the ordinary space, on both
// sides (the review's list).
TEST(SpacedSignWhite, ProseAndCodeAreUntouched) {
  const char* pairs[][2] = {{"C++", "is"}, {"A+", "grade"}, {"18+", "years"}, {"<div>", "element"}, {"a", "->"},
                            {"->", "b"},   {"a", "!="},     {"!=", "b"},      {"x", "-="},          {"-=", "1"},
                            {"a", "-"},    {"-", "b"},      {"and", "/"},     {"the", "quiet"}};
  for (const auto& p : pairs) {
    EXPECT_EQ(decide(p[0], p[1]), Sign::None) << p[0] << " | " << p[1];
  }
}

// A sign is ruled on both sides or neither: if its far side cannot be ruled,
// this side is left alone too, so a sum is never made one-sided.
TEST(SpacedSignWhite, ASignWhoseFarSideIsGluedOrHalfSizeIsLeftAlone) {
  // "x&nbsp;= y": the gap (=, y) sees "=" glued to its left.
  EXPECT_EQ(decide("=", "y", kGlued, kSpaced), Sign::None);
  // "x =&nbsp;y": the gap (x, =) sees "=" glued to its right.
  EXPECT_EQ(decide("x", "=", kSpaced, kGlued), Sign::None);
  // "x<sup>2</sup> = y": the far side of "=" is a superscript.
  EXPECT_EQ(decide("=", "y", kSupSub, kSpaced), Sign::None);
  // A superscript neighbor in this gap.
  EXPECT_EQ(spacedsign::decide("2", true, "=", false, kSpaced, kSpaced), Sign::None);
  EXPECT_EQ(spacedsign::decide("=", false, "y", true, kSpaced, kSpaced), Sign::None);
  // The far side of the OTHER word does not matter when that word is no sign.
  EXPECT_EQ(decide("1", "+", kGlued, kSpaced), Sign::Right);
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
      const int leftSpace = spacedsign::spaceForWhite(before, plus, target);
      const int rightSpace = spacedsign::spaceForWhite(plus, after, target);
      EXPECT_EQ(whiteAcross(before, leftSpace, plus), target);
      EXPECT_EQ(whiteAcross(plus, rightSpace, after), target);
    }
  }
}

// At a render scale of 2 the boxes are device pixels and the space is logical:
// each side lands within one device pixel of the target.
TEST(SpacedSignWhite, AtScaleTwoEachSideIsWithinOneDevicePixel) {
  const int scale = 2;
  const int target = 17;  // odd, so exact equality is impossible for some bearings
  for (int rsb = -2; rsb <= 5; rsb++) {
    for (int lsb = -2; lsb <= 5; lsb++) {
      const GlyphBox left{20, 2, 18 - rsb};  // advance 20, ink from 2, rsb as given
      const GlyphBox right{20, lsb, 10};
      const int space = spacedsign::spaceForWhite(left, right, target, scale);
      const int white = left.rightBearingPx() + space * scale + right.leftPx;
      if (target - rsb - lsb > 0) {
        EXPECT_LE(std::abs(white - target), 1) << rsb << " " << lsb;
      } else {
        EXPECT_EQ(space, 0);
      }
    }
  }
}

TEST(SpacedSignWhite, BearingsWiderThanTheTargetFloorTheSpaceAtZero) {
  const GlyphBox wide{12, 5, 2};  // rsb 5, lsb 5
  EXPECT_EQ(spacedsign::spaceForWhite(wide, wide, 6), 0);
}

TEST(SpacedSignWhite, NegativeBearingsWidenTheSpace) {
  const GlyphBox overhang{6, -1, 8};  // ink past both edges: rsb -1, lsb -1
  EXPECT_EQ(spacedsign::spaceForWhite(overhang, overhang, 6), 8);
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
