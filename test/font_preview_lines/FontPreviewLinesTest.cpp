// The Reader Font preview's line spacing (src/activities/settings/FontPreviewLines.h).
//
// THE BUG THIS EXISTS FOR (2026-11-13): accented capitals collided with the line
// above in the font preview. The specimen stepped its lines by the ascender plus
// 2 px, which leaves no room for one line's descenders and the next line's
// accents; a face whose accents rise well above its ascender showed the tilde of
// Ñ and the acutes of Á and É running into the descenders above. The reader has
// always advanced by the font's line height, and the preview now does too.

#include <gtest/gtest.h>

#include "activities/settings/FontPreviewLines.h"

namespace {

// A face at a reading size, in pixels, shaped like the one in the report: the
// accented capitals rise 5 px above the reported ascender and the descenders
// sink 1 px below the reported descender, as real faces' ink routinely does.
constexpr int kAscender = 24;
constexpr int kDescender = 9;
constexpr int kAdvanceY = 41;
constexpr int kAccentOvershoot = 5;
constexpr int kDescenderInk = 10;

}  // namespace

TEST(FontPreviewLines, StepIsTheFontsOwnLineHeightLikeTheReader) {
  const auto m = fontpreview::lineMetrics(kAscender, kDescender, kAdvanceY);
  EXPECT_EQ(m.step, kAdvanceY);
  EXPECT_EQ(m.inkHeight, kAscender + kDescender);
}

// The reported collision, as geometry: the accent on the next line's capital
// must clear the foot of this line's descenders. The old ascender + 2 step
// (26 px here) put the accent 13 px INTO the descenders.
TEST(FontPreviewLines, AccentedCapitalsClearTheDescendersAbove) {
  const auto m = fontpreview::lineMetrics(kAscender, kDescender, kAdvanceY);
  const int descenderFoot = kAscender + kDescenderInk;                            // line 0, from its top
  const int nextAccentTop = m.step + kAscender - (kAscender + kAccentOvershoot);  // line 1
  EXPECT_GT(nextAccentTop, descenderFoot);
}

TEST(FontPreviewLines, NoAdvanceFallsBackToTheInkHeightSoLinesNeverOverlap) {
  const auto m = fontpreview::lineMetrics(kAscender, kDescender, 0);
  EXPECT_EQ(m.step, kAscender + kDescender);
}

TEST(FontPreviewLines, AStoredNegativeDescenderIsNotSubtracted) {
  const auto m = fontpreview::lineMetrics(kAscender, -kDescender, kAdvanceY);
  EXPECT_EQ(m.inkHeight, kAscender);
}

// maxLines and blockHeight must agree, or the centered block is laid out for a
// line count the pane cannot hold and the fit check silently drops the last one
// (for the style grid, the Bold Italic).
TEST(FontPreviewLines, MaxLinesIsExactlyWhatFits) {
  const auto m = fontpreview::lineMetrics(kAscender, kDescender, kAdvanceY);
  for (int inner = 0; inner <= 400; ++inner) {
    const int n = fontpreview::maxLines(inner, m);
    ASSERT_GE(n, 1) << inner;
    if (inner >= m.inkHeight) {
      EXPECT_LE(fontpreview::blockHeight(n * m.step, n, m), inner) << inner;
    }
    EXPECT_GT(fontpreview::blockHeight((n + 1) * m.step, n + 1, m), inner) << inner;
  }
}

// The prose specimen's half-line gap before its closing italic is taken off the
// height BEFORE the count, so a counted block, gap included, always fits and the
// fit check never drops the italic's last line.
TEST(FontPreviewLines, CountingWithTheProseGapKeepsTheWholeBlockInside) {
  const auto m = fontpreview::lineMetrics(kAscender, kDescender, kAdvanceY);
  const int gap = m.step / 2;
  for (int inner = m.inkHeight + gap; inner <= 400; ++inner) {
    const int n = fontpreview::maxLines(inner - gap, m);
    EXPECT_LE(fontpreview::blockHeight(n * m.step + gap, n, m), inner) << inner;
  }
}

TEST(FontPreviewLines, BlockHeightCountsTheLastLineAsInkNotAStep) {
  const auto m = fontpreview::lineMetrics(kAscender, kDescender, kAdvanceY);
  EXPECT_EQ(fontpreview::blockHeight(0, 0, m), 0);
  EXPECT_EQ(fontpreview::blockHeight(m.step, 1, m), m.inkHeight);
  EXPECT_EQ(fontpreview::blockHeight(4 * m.step, 4, m), 3 * m.step + m.inkHeight);
  // A gap before a line (the prose specimen's italic) is carried through.
  EXPECT_EQ(fontpreview::blockHeight(4 * m.step + 20, 4, m), 3 * m.step + 20 + m.inkHeight);
}
