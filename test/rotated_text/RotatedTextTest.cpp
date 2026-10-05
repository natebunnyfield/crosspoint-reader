// drawTextRotated90CCW: the mirror of drawTextRotated90CW, added for T-021 so a
// wide table can become a clockwise-rotated page.
//
// It is four coordinate branches written by hand inside a template. Any one of
// them can be wrong in a way that compiles, draws something, and shows up only
// as a page of sideways text nobody can read — which is why this reads the
// framebuffer back rather than asserting that a call returned.
//
// The invariants, each of which a plausible sign error breaks:
//
//   * CW ink climbs the page (toward -y); CCW ink descends it.
//   * Their ink is the same SHAPE, mirrored — same pixel count, same bounding
//     box dimensions, transposed the same way against upright text.
//   * A CCW run stays inside its own band, one line-height wide.
//   * Neither writes a pixel outside the screen (GFX_BOUNDS_COUNTER).
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "RotatedTablePlacement.h"
#include "fontIds.h"

HalDisplay display;

namespace {

class Gfx {
 public:
  static Gfx& instance() {
    static Gfx g;
    return g;
  }
  GfxRenderer& renderer() { return renderer_; }

 private:
  Gfx() : renderer_(display), cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()) {
    renderer_.begin();
    if (!decompressor_.init()) {
      ADD_FAILURE() << "font decompressor init failed";
    }
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14_);
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont lfr14R_{&librefranklin_reader_14_regular}, lfr14B_{&librefranklin_reader_14_bold},
      lfr14I_{&librefranklin_reader_14_italic}, lfr14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lfReader14_{&lfr14R_, &lfr14B_, &lfr14I_, &lfr14BI_};
};

constexpr int FONT = LIBREFRANKLIN_READER_14_FONT_ID;
constexpr const char* SAMPLE = "Departed";

// Portrait maps logical (x, y) to physical with phyX = y,
// phyY = panelHeight - 1 - x, writing 1bpp MSB-first where a SET bit is white.
bool inkAt(const GfxRenderer& r, const int x, const int y) {
  if (x < 0 || y < 0 || x >= r.getScreenWidth() || y >= r.getScreenHeight()) return false;
  const uint8_t* fb = r.getFrameBuffer();
  const int phyX = y;
  const int phyY = (r.getDisplayHeight() - 1) - x;
  const bool white = (fb[phyY * r.getDisplayWidthBytes() + (phyX >> 3)] >> (7 - (phyX & 7))) & 0x1;
  return !white;
}

struct Box {
  int x0 = INT32_MAX, y0 = INT32_MAX, x1 = -1, y1 = -1;
  int count = 0;
  bool empty() const { return count == 0; }
  int width() const { return x1 - x0 + 1; }
  int height() const { return y1 - y0 + 1; }
};

Box inkBox(const GfxRenderer& r) {
  Box b;
  for (int y = 0; y < r.getScreenHeight(); y++) {
    for (int x = 0; x < r.getScreenWidth(); x++) {
      if (!inkAt(r, x, y)) continue;
      b.count++;
      b.x0 = std::min(b.x0, x);
      b.y0 = std::min(b.y0, y);
      b.x1 = std::max(b.x1, x);
      b.y1 = std::max(b.y1, y);
    }
  }
  return b;
}

Box drawAndMeasure(void (GfxRenderer::*fn)(int, int, int, const char*, bool, EpdFontFamily::Style) const, int x,
                   int y) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.clearScreen(0xFF);
  (r.*fn)(FONT, x, y, SAMPLE, true, EpdFontFamily::REGULAR);
  return inkBox(r);
}

}  // namespace

class RotatedText : public ::testing::Test {
 protected:
  void SetUp() override { GfxRenderer::resetOutOfRange(); }
};

TEST_F(RotatedText, CwClimbsThePageAndCcwDescendsIt) {
  const int anchorY = 400;
  const Box cw = drawAndMeasure(&GfxRenderer::drawTextRotated90CW, 100, anchorY);
  ASSERT_FALSE(cw.empty());
  const Box ccw = drawAndMeasure(&GfxRenderer::drawTextRotated90CCW, 100, anchorY);
  ASSERT_FALSE(ccw.empty());

  EXPECT_LT(cw.y0, anchorY) << "the CW run should occupy the page ABOVE its anchor";
  EXPECT_LE(cw.y1, anchorY + 2);
  EXPECT_GT(ccw.y1, anchorY) << "the CCW run should occupy the page BELOW its anchor";
  EXPECT_GE(ccw.y0, anchorY - 2);
}

TEST_F(RotatedText, TheTwoRotationsDrawTheSameShape) {
  const Box cw = drawAndMeasure(&GfxRenderer::drawTextRotated90CW, 100, 400);
  const Box ccw = drawAndMeasure(&GfxRenderer::drawTextRotated90CCW, 100, 400);

  // Same glyphs, same face, same string: a mirror changes where the ink is, not
  // how much of it there is. A wrong branch shows up here as a pixel-count or
  // extent mismatch even when the render still "looks like text".
  EXPECT_EQ(cw.count, ccw.count) << "mirrored text lost or gained ink";
  EXPECT_EQ(cw.width(), ccw.width());
  EXPECT_EQ(cw.height(), ccw.height());
}

TEST_F(RotatedText, RotatedTextIsTallerThanItIsWide) {
  const Box upright = [] {
    GfxRenderer& r = Gfx::instance().renderer();
    r.clearScreen(0xFF);
    r.drawText(FONT, 100, 100, SAMPLE);
    return inkBox(r);
  }();
  const Box ccw = drawAndMeasure(&GfxRenderer::drawTextRotated90CCW, 100, 300);

  // The transpose, stated as the thing a reader would notice: a rotated run is
  // long down the page and narrow across it, exactly swapping upright's extents.
  EXPECT_GT(upright.width(), upright.height());
  EXPECT_GT(ccw.height(), ccw.width());
  EXPECT_NEAR(ccw.height(), upright.width(), 2);
  EXPECT_NEAR(ccw.width(), upright.height(), 2);
}

TEST_F(RotatedText, TheRunStaysInsideOneBand) {
  GfxRenderer& r = Gfx::instance().renderer();
  const Box ccw = drawAndMeasure(&GfxRenderer::drawTextRotated90CCW, 100, 300);
  EXPECT_LE(ccw.width(), r.getLineHeight(FONT) + 2) << "a rotated run must not spill outside its own band";
}

TEST_F(RotatedText, NeitherRotationWritesOutsideTheScreen) {
  drawAndMeasure(&GfxRenderer::drawTextRotated90CW, 100, 400);
  EXPECT_EQ(GfxRenderer::outOfRangeCount(), 0u);
  GfxRenderer::resetOutOfRange();
  drawAndMeasure(&GfxRenderer::drawTextRotated90CCW, 100, 400);
  EXPECT_EQ(GfxRenderer::outOfRangeCount(), 0u);
}

TEST_F(RotatedText, TheAnchorsAreMirroredToo) {
  // Not just the glyphs: CW hangs its band to the RIGHT of x and CCW to the
  // LEFT, because the whole mapping is rotated 180 degrees. A caller that
  // assumes both take a left edge draws the CCW band off the page — which is
  // how the first rotated table page rendered 11,197 out-of-range pixels.
  const int bandX = 200;
  const Box cw = drawAndMeasure(&GfxRenderer::drawTextRotated90CW, bandX, 400);
  const Box ccw = drawAndMeasure(&GfxRenderer::drawTextRotated90CCW, bandX, 300);
  EXPECT_GE(cw.x0, bandX - 2) << "CW ink should sit at or right of its anchor";
  EXPECT_LE(ccw.x1, bandX + 2) << "CCW ink should sit at or left of its anchor";
}

// --- the wide-table page: the TABLE turned clockwise, read after a ---------
// --- COUNTER-clockwise device turn (owner, corrected 2026-10-04) ------------
//
// One TestFlight build (304) drew this page the other way round, after the
// owner's "rotation is clockwise" was read as the READER's turn; he corrected
// it the same day: "the iphone would need to be turned ccw not clockwise, you've
// mixed things up". These pin the direction through the real renderer and the
// real placement (RotatedTablePlacement.h), so a second flip fails here rather
// than on his phone. Turned counter-clockwise, the page's RIGHT edge is the top
// of what the reader sees and its TOP edge is their left.

TEST_F(RotatedText, TheTablePageReadsAfterACounterClockwiseTurn) {
  GfxRenderer& r = Gfx::instance().renderer();
  const int W = r.getScreenWidth();
  constexpr int kMargin = 2;  // ChapterHtmlSlimParser::emitBufferedTableRotated's kRotMargin
  const int lh = r.getLineHeight(FONT);
  GfxRenderer::resetOutOfRange();
  r.clearScreen(0xFF);
  const rotatedtable::LinePlace header = rotatedtable::line(kMargin, kMargin, W, kMargin);
  r.drawTextRotated90CCW(FONT, header.x, header.y, SAMPLE, true, EpdFontFamily::BOLD);
  const Box h = inkBox(r);
  r.clearScreen(0xFF);
  const rotatedtable::LinePlace row = rotatedtable::line(kMargin + 2 * lh, kMargin, W, kMargin);
  r.drawTextRotated90CCW(FONT, row.x, row.y, SAMPLE, true, EpdFontFamily::REGULAR);
  const Box b = inkBox(r);
  ASSERT_FALSE(h.empty());
  ASSERT_FALSE(b.empty());
  EXPECT_GT(h.x0, b.x1) << "the header must be the band nearest the page's RIGHT edge";
  EXPECT_GT(h.x1, W - 1 - lh) << "the header must sit at the right margin";
  EXPECT_LT(h.y0, 2 * lh) << "the first column must start at the page's TOP, the reader's left";
  EXPECT_GT(h.y1, h.y0 + lh) << "and the run must descend from there";
  EXPECT_EQ(GfxRenderer::outOfRangeCount(), 0u);
}

TEST_F(RotatedText, TheHeaderRuleSitsBetweenTheHeaderAndTheRows) {
  // Under the header in the reader's view is, on the upright page, a vertical
  // line just LEFT of the header band, starting at the top margin.
  GfxRenderer& r = Gfx::instance().renderer();
  const int W = r.getScreenWidth();
  constexpr int kMargin = 2, kThick = 2;
  const int lh = r.getLineHeight(FONT);
  const int ruleDown = kMargin + lh + 4, length = 400;
  const rotatedtable::RulePlace at = rotatedtable::rule(ruleDown, length, W, kMargin);
  r.clearScreen(0xFF);
  r.drawLine(at.x, at.y, at.x, at.y + at.length - 1, kThick, true);
  const Box rule = inkBox(r);
  r.clearScreen(0xFF);
  const rotatedtable::LinePlace header = rotatedtable::line(kMargin, kMargin, W, kMargin);
  r.drawTextRotated90CCW(FONT, header.x, header.y, SAMPLE, true, EpdFontFamily::BOLD);
  const Box h = inkBox(r);
  EXPECT_EQ(rule.count, kThick * length);
  EXPECT_EQ(rule.y0, kMargin) << "the rule starts at the top margin, with the header";
  EXPECT_LT(rule.x1, h.x0) << "the rule lies between the header and the rows";
}
