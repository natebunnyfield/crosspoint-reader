// Solid block symbols, upstream 7394e40fa ("render redaction blocks without a special font").
//
// A reading face that lacks U+2588 FULL BLOCK or U+25A0 BLACK SQUARE used to draw the replacement
// glyph for a redacted passage (a row of "?" or boxes). Now EpdFont::getGlyphMetrics supplies the
// metrics of a filled rectangle sized to the face's em width and ascender, and every draw path
// paints it: upright, sup/sub (half size), rotated both ways, in dark mode (the ink is the pixel
// state), and not at all in the scan pass or a grayscale pass. This reads the framebuffer back
// rather than asserting a call returned. The hi-res companion path is not exercised here (the host
// build is scale 1); drawText sends a solid block to the base face on purpose.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <MissingGlyphLedger.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

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

}  // namespace

class SolidBlocks : public ::testing::Test {
 protected:
  void SetUp() override {
    GfxRenderer::resetOutOfRange();
    GfxRenderer& r = Gfx::instance().renderer();
    r.setRenderMode(GfxRenderer::BW);
    r.clearScreen(0xFF);
  }
};

TEST_F(SolidBlocks, TheBuiltInFaceReallyLacksTheSymbols) {
  // The premise of every case below.
  const EpdFontFamily& family = Gfx::instance().renderer().getFontMap().at(FONT);
  EXPECT_FALSE(family.hasCodepoint(0x2588));
  EXPECT_FALSE(family.hasCodepoint(0x25A0));
}

TEST_F(SolidBlocks, AFullBlockIsOneFilledRectangleOfTheMeasuredAdvance) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.drawText(FONT, 100, 100, "\xe2\x96\x88");
  const Box b = inkBox(r);
  ASSERT_FALSE(b.empty());
  EXPECT_EQ(b.count, b.width() * b.height()) << "a block is solid, not an outline";
  EXPECT_EQ(b.width(), r.getTextAdvanceX(FONT, "\xe2\x96\x88", EpdFontFamily::REGULAR))
      << "it fills the cell the layout measured";
  EXPECT_EQ(b.height(), r.getFontAscenderSize(FONT));
}

TEST_F(SolidBlocks, ARunOfBlocksAdvancesByTheMeasuredWidth) {
  GfxRenderer& r = Gfx::instance().renderer();
  const char* run = "\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88";
  r.drawText(FONT, 100, 100, run);
  const Box b = inkBox(r);
  EXPECT_EQ(b.width(), r.getTextAdvanceX(FONT, run, EpdFontFamily::REGULAR));
  EXPECT_EQ(b.count, b.width() * b.height()) << "adjacent blocks touch: a redaction bar has no seams";
}

TEST_F(SolidBlocks, ABlackSquareIsSmallerAndCentered) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.drawText(FONT, 100, 100, "\xe2\x96\xa0");
  const Box sq = inkBox(r);
  r.clearScreen(0xFF);
  r.drawText(FONT, 100, 100, "\xe2\x96\x88");
  const Box full = inkBox(r);
  ASSERT_FALSE(sq.empty());
  EXPECT_LT(sq.height(), full.height());
  EXPECT_LE(sq.width(), full.width());
  EXPECT_EQ(sq.count, sq.width() * sq.height());
  EXPECT_EQ(r.getTextAdvanceX(FONT, "\xe2\x96\xa0", EpdFontFamily::REGULAR),
            r.getTextAdvanceX(FONT, "\xe2\x96\x88", EpdFontFamily::REGULAR))
      << "both symbols take the em advance";
}

TEST_F(SolidBlocks, TextBeforeAndAfterKeepsItsPlace) {
  GfxRenderer& r = Gfx::instance().renderer();
  const char* text =
      "ab\xe2\x96\x88\xe2\x96\x88"
      "cd";
  const int measured = r.getTextAdvanceX(FONT, text, EpdFontFamily::REGULAR);
  r.drawText(FONT, 100, 100, text);
  const Box b = inkBox(r);
  EXPECT_LE(b.x1 - 100 + 1, measured) << "the ink stays inside the measured run";
  EXPECT_GE(b.x1 - 100 + 1, measured - 4) << "and the last glyph reaches near its end";
}

TEST_F(SolidBlocks, SupSubDrawsAHalfSizeBlock) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.drawText(FONT, 100, 100, "\xe2\x96\x88");
  const Box full = inkBox(r);
  r.clearScreen(0xFF);
  r.drawText(FONT, 100, 100, "\xe2\x96\x88", true, EpdFontFamily::SUP);
  const Box half = inkBox(r);
  ASSERT_FALSE(half.empty());
  EXPECT_NEAR(half.width(), (full.width() + 1) / 2, 1);
  EXPECT_NEAR(half.height(), (full.height() + 1) / 2, 1);
  EXPECT_EQ(half.count, half.width() * half.height());
}

TEST_F(SolidBlocks, DarkModeInkIsThePixelState) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.clearScreen(0x00);  // an all-black page: ink is WHITE here, drawText's `black` = false
  r.drawText(FONT, 100, 100, "\xe2\x96\x88", false);
  int white = 0;
  for (int y = 0; y < r.getScreenHeight(); y++) {
    for (int x = 0; x < r.getScreenWidth(); x++) white += !inkAt(r, x, y);
  }
  const int adv = r.getTextAdvanceX(FONT, "\xe2\x96\x88", EpdFontFamily::REGULAR);
  EXPECT_EQ(white, adv * r.getFontAscenderSize(FONT));
}

TEST_F(SolidBlocks, RotatedBlocksAreTheTransposedRectangle) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.drawText(FONT, 100, 100, "\xe2\x96\x88\xe2\x96\x88");
  const Box up = inkBox(r);
  for (auto fn : {&GfxRenderer::drawTextRotated90CW, &GfxRenderer::drawTextRotated90CCW}) {
    r.clearScreen(0xFF);
    (r.*fn)(FONT, 200, 400, "\xe2\x96\x88\xe2\x96\x88", true, EpdFontFamily::REGULAR);
    const Box rot = inkBox(r);
    ASSERT_FALSE(rot.empty());
    EXPECT_EQ(rot.count, up.count);
    EXPECT_EQ(rot.count, rot.width() * rot.height());
    EXPECT_EQ(rot.width(), up.height());
    EXPECT_EQ(rot.height(), up.width());
  }
  EXPECT_EQ(GfxRenderer::outOfRangeCount(), 0u);
}

TEST_F(SolidBlocks, TheScanPassAndTheGrayscalePassesDrawNothing) {
  GfxRenderer& r = Gfx::instance().renderer();
  r.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  r.drawText(FONT, 100, 100, "\xe2\x96\x88");
  EXPECT_TRUE(inkBox(r).empty()) << "a full-ink glyph is never flagged into a gray plane";
  r.setRenderMode(GfxRenderer::BW);
  r.drawText(FONT, 100, 100, "\xe2\x96\x88");
  EXPECT_FALSE(inkBox(r).empty());
}

TEST_F(SolidBlocks, ABlockIsNotAMissingGlyph) {
  auto& ledger = missingglyphs::current();
  ledger.reset();
  ledger.arm();
  GfxRenderer& r = Gfx::instance().renderer();
  r.drawText(FONT, 100, 100, "\xe2\x96\x88\xe2\x96\xa0");
  r.getTextAdvanceX(FONT, "\xe2\x96\x88\xe2\x96\xa0", EpdFontFamily::REGULAR);
  EXPECT_EQ(ledger.distinct(), 0u) << "drawn blocks must not raise the missing-characters note";
  r.drawText(FONT, 100, 200, "\xe2\x96\x93");  // U+2593 is still missing
  EXPECT_EQ(ledger.distinct(), 1u);
  ledger.disarm();
  ledger.reset();
}
