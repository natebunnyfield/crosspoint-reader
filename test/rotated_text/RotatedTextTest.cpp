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

// --- the wide-table page reads after a CLOCKWISE turn (2026-10-04) ----------
//
// T-021 shipped its page drawn with drawTextRotated90CCW, which is a page the
// reader turns COUNTER-clockwise, against the owner's clockwise ruling. The fix
// places every line for drawTextRotated90CW at the old spot turned 180 degrees
// (RotatedTablePlacement.h), and claims the result is the old page turned
// around EXACTLY. These read the framebuffer back to hold it to that: a sign or
// off-by-one in the placement shows up as a pixel that does not match its
// mirror, and an asymmetric rounding in either draw call would too.

namespace {

std::vector<bool> inkMap(const GfxRenderer& r) {
  const int W = r.getScreenWidth(), H = r.getScreenHeight();
  std::vector<bool> m(static_cast<size_t>(W) * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) m[static_cast<size_t>(y) * W + x] = inkAt(r, x, y);
  return m;
}

// Every inked pixel of `before`, turned 180 degrees inside the screen, is inked
// in `after`, and nothing else is. Returns the number of mismatches.
int mismatchesTurned(const std::vector<bool>& before, const std::vector<bool>& after, const int W, const int H) {
  int bad = 0;
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (before[static_cast<size_t>(y) * W + x] != after[static_cast<size_t>(H - 1 - y) * W + (W - 1 - x)]) bad++;
  return bad;
}

}  // namespace

TEST_F(RotatedText, TheClockwiseTablePageIsTheOldPageTurnedAround) {
  GfxRenderer& r = Gfx::instance().renderer();
  const int W = r.getScreenWidth(), H = r.getScreenHeight();
  constexpr int kMargin = 2;  // ChapterHtmlSlimParser::emitBufferedTableRotated's kRotMargin
  // The viewport is the whole screen here, so the turn is the screen's.
  struct Spot {
    int down, across;
  };
  // The table's first line (both margins), a line deep in the page, and one near
  // the far corner of the reading axis.
  const Spot spots[] = {{kMargin, kMargin}, {120, 260}, {W - 60, H - 200}};
  for (const Spot& s : spots) {
    r.clearScreen(0xFF);
    r.drawTextRotated90CCW(FONT, W - kMargin - s.down, s.across, SAMPLE, true, EpdFontFamily::REGULAR);
    const std::vector<bool> before = inkMap(r);
    ASSERT_GT(std::count(before.begin(), before.end(), true), 0) << "the old placement drew nothing";

    GfxRenderer::resetOutOfRange();
    r.clearScreen(0xFF);
    const rotatedtable::LinePlace at = rotatedtable::line(s.down, s.across, H, kMargin);
    r.drawTextRotated90CW(FONT, at.x, at.y, SAMPLE, true, EpdFontFamily::REGULAR);
    const std::vector<bool> after = inkMap(r);
    EXPECT_EQ(mismatchesTurned(before, after, W, H), 0)
        << "down " << s.down << ", across " << s.across << ": the clockwise line is not the old one turned 180";
    EXPECT_EQ(GfxRenderer::outOfRangeCount(), 0u);
  }
}

TEST_F(RotatedText, TheHeaderRuleIsTheOldRuleTurnedAround) {
  GfxRenderer& r = Gfx::instance().renderer();
  const int W = r.getScreenWidth(), H = r.getScreenHeight();
  constexpr int kMargin = 2, kThick = 2;
  const int ruleDown = 47, length = 610;
  r.clearScreen(0xFF);
  const int oldX = W - kMargin - ruleDown;
  r.drawLine(oldX, kMargin, oldX, kMargin + length - 1, kThick, true);
  const std::vector<bool> before = inkMap(r);
  ASSERT_EQ(std::count(before.begin(), before.end(), true), kThick * length);

  r.clearScreen(0xFF);
  const rotatedtable::RulePlace at = rotatedtable::rule(ruleDown, length, H, kMargin, kThick);
  r.drawLine(at.x, at.y, at.x, at.y + at.length - 1, kThick, true);
  EXPECT_EQ(mismatchesTurned(before, inkMap(r), W, H), 0) << "the rule is not the old rule turned 180";
}

TEST_F(RotatedText, TheTablePageReadsAfterAClockwiseTurn) {
  // Stated the way the reader meets it. Turned clockwise, the page's LEFT edge
  // is the top of what the reader sees and its BOTTOM edge is their left. So the
  // header row must be the leftmost band, and every run must start low on the
  // page and climb.
  GfxRenderer& r = Gfx::instance().renderer();
  const int H = r.getScreenHeight();
  constexpr int kMargin = 2;
  const int lh = r.getLineHeight(FONT);
  r.clearScreen(0xFF);
  const rotatedtable::LinePlace header = rotatedtable::line(kMargin, kMargin, H, kMargin);
  r.drawTextRotated90CW(FONT, header.x, header.y, SAMPLE, true, EpdFontFamily::BOLD);
  const Box h = inkBox(r);
  r.clearScreen(0xFF);
  const rotatedtable::LinePlace row = rotatedtable::line(kMargin + 2 * lh, kMargin, H, kMargin);
  r.drawTextRotated90CW(FONT, row.x, row.y, SAMPLE, true, EpdFontFamily::REGULAR);
  const Box b = inkBox(r);
  ASSERT_FALSE(h.empty());
  ASSERT_FALSE(b.empty());
  EXPECT_LT(h.x1, b.x0) << "the header must be the band nearest the page's LEFT edge";
  EXPECT_LT(h.x0, lh) << "the header must sit at the left margin";
  EXPECT_GT(h.y1, H - 2 * lh) << "the first column must start at the page's BOTTOM, the reader's left";
  EXPECT_LT(h.y0, h.y1 - lh) << "and the run must climb from there";
}
