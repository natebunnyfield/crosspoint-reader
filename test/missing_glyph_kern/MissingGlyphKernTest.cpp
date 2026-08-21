// B-036: measurement and rendering must agree about kerning across a MISSING
// glyph.
//
// The trigger is real, not hypothetical: SD-font conversion prunes a codepoint
// whose outline no face in the fallback chain carries, but the kern class
// tables are built from the source face's GPOS and can still list it. A font
// can therefore hold a kern pair for a codepoint it has no glyph for. Before
// the fix, EpdFont::getTextBounds reset prevCp on the hole (no kern taken)
// while GfxRenderer::drawText and getTextAdvanceX kept it (kern taken) — so a
// line measured as exactly fitting could draw a kerned pixel wider and clip.
//
// The synthetic font below constructs exactly that: 'h' (0x68) is a member of
// kern classes with strong pairs on both sides, but is absent from the glyph
// intervals. The assertions pin all three walks to one width.
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include "EpdFontData.h"

HalDisplay display;

namespace {

// clang-format off
const EpdGlyph kGlyphs[] = {
  // width height advanceX left top dataLength dataOffset
  /* 0 'A' */ { 8, 12, 160, 0, 12, 0, 0 },
  /* 1 'V' */ { 8, 12, 160, 0, 12, 0, 0 },
};

// NO 'h': the codepoint is deliberately outside every interval, the way an
// SD-font build prunes an outline the fallback chain cannot supply.
const EpdUnicodeInterval kIntervals[] = {
  { 0x41, 0x41, 0 },  // 'A'
  { 0x56, 0x56, 1 },  // 'V'
};

// 'h' sits in BOTH kern classes with the largest magnitude pairs in the
// matrix, so if either the draw or the measure side kerns across the hole the
// widths split loudly (by 2px, not a rounding whisker).
const EpdKernClassEntry kKernLeft[] = {
  { 0x41, 1 },  // 'A'
  { 0x68, 2 },  // 'h' -- glyphless kern partner
};
const EpdKernClassEntry kKernRight[] = {
  { 0x56, 1 },  // 'V'
  { 0x68, 2 },  // 'h'
};
// [L1,R1]=A->V  [L1,R2]=A->h  [L2,R1]=h->V  [L2,R2]=h->h   (4.4 FP)
// A->V is -16 (-1px exactly) so the pair survives fp4 rounding; the pairs
// through the hole are -32 (-2px) so a walk that takes either one splits
// loudly from a walk that does not.
const int8_t kKernMatrix[] = { -16, -32, -32, 0 };

const EpdFontData kFontData = {
  .bitmap            = nullptr,
  .glyph             = kGlyphs,
  .intervals         = kIntervals,
  .intervalCount     = 2,
  .advanceY          = 16,
  .ascender          = 12,
  .descender         = 0,
  .is2Bit            = false,
  .groups            = nullptr,
  .groupCount        = 0,
  .glyphToGroup      = nullptr,
  .kernLeftClasses   = kKernLeft,
  .kernRightClasses  = kKernRight,
  .kernMatrix        = kKernMatrix,
  .kernLeftEntryCount  = 2,
  .kernRightEntryCount = 2,
  .kernLeftClassCount  = 2,
  .kernRightClassCount = 2,
  .ligaturePairs     = nullptr,
  .ligaturePairCount = 0,
  .glyphMissHandler  = nullptr,
  .glyphMissCtx      = nullptr,
};
// clang-format on

constexpr int FONT_ID = 900;

class Gfx {
 public:
  static Gfx& instance() {
    static Gfx g;
    return g;
  }
  GfxRenderer& renderer() { return renderer_; }
  EpdFont& epdFont() { return holeR_; }

 private:
  Gfx() : renderer_(display), cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()) {
    renderer_.begin();
    if (!decompressor_.init()) {
      ADD_FAILURE() << "font decompressor init failed";
    }
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(FONT_ID, holeFamily_);
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont holeR_{&kFontData};
  EpdFontFamily holeFamily_{&holeR_};
};

int boundsWidth(const char* s) {
  int w = 0, h = 0;
  Gfx::instance().epdFont().getTextDimensions(s, &w, &h);
  return w;
}

int advanceWidth(const char* s) {
  return Gfx::instance().renderer().getTextAdvanceX(FONT_ID, s, EpdFontFamily::REGULAR);
}

}  // namespace

// The two measurers report DIFFERENT metrics by design: getTextBounds is ink
// extent (last glyph contributes left+width), getTextAdvanceX is advance sum
// (last glyph contributes advanceX). For this fixture's glyphs that gap is a
// constant 2px (advance 10, ink 8) -- so the invariant that catches B-036 is
// not equality but that the GAP stays the sidebearing constant. Any kern taken
// by one walk and not the other moves it.
constexpr int kSidebearingGap = 2;

// Sanity: intact pair. Both walks take the A->V kern (-1px), so the gap is
// exactly the sidebearing constant. Guards the fixture itself.
TEST(MissingGlyphKern, IntactPairAgrees) {
  EXPECT_EQ(boundsWidth("AV"), 17);   // 9 (kerned advance) + 8 (V ink)
  EXPECT_EQ(advanceWidth("AV"), 19);  // 9 + 10
  EXPECT_EQ(advanceWidth("AV") - boundsWidth("AV"), kSidebearingGap);
}

// The bug, both directions. 'h' has kern pairs but no glyph, the shape
// SD-font pruning produces. The hole must sever BOTH pairs in BOTH walks:
// before the fix the draw-side walks kerned A->h (into the hole) and h->V
// (out of it) where getTextBounds took neither, and a line measured as
// fitting could draw 2px wider per pair and clip.
TEST(MissingGlyphKern, HoleSeversBothPairsInBothWalks) {
  EXPECT_EQ(boundsWidth("AhV"), 18);   // 10 (bare A advance) + 8 (V ink)
  EXPECT_EQ(advanceWidth("AhV"), 20);  // 10 + 10, no kern anywhere
  EXPECT_EQ(advanceWidth("AhV") - boundsWidth("AhV"), kSidebearingGap);
}

// Leading and trailing holes: prevCp starts severed and ends severed, and a
// trailing hole adds no width in either walk.
TEST(MissingGlyphKern, EdgeHoles) {
  EXPECT_EQ(boundsWidth("hAV"), boundsWidth("AV"));
  EXPECT_EQ(advanceWidth("hAV"), advanceWidth("AV"));
  // A trailing hole adds ADVANCE but no INK: the advance walk flushes V's
  // bare 10px (19 total), while the ink walk's extent stays where "AV"
  // left it (17). The sidebearing gap holds, which is the B-036 invariant.
  EXPECT_EQ(boundsWidth("AVh"), 17);
  EXPECT_EQ(advanceWidth("AVh"), 19);
  EXPECT_EQ(advanceWidth("AVh") - boundsWidth("AVh"), kSidebearingGap);
}
