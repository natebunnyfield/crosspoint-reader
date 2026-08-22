// SD-font measure/draw kern parity.
//
// Targets the 2026-08-22 punctuation-kerning audit's P0
// (docs/punctuation-kerning-audit-2026-08-22.md §4): layout MEASURED SD
// reading fonts with no kerning (the advance-table fast paths in
// GfxRenderer::getTextAdvanceX / getSpaceAdvance short-circuited past every
// kern lookup) while drawText KERNED, so a word ending "r." in LibreFranklin
// 16 measured 2.75 px wider than it drew and justified gaps absorbed the
// error. Worse, GfxRenderer::getKerning read the per-page mini kern matrix,
// which during layout is either null or stale from a previously RENDERED
// page — non-deterministic values for ParsedText's attached-token calls.
//
// The fix loads full kern-matrix ROWS beside the advance table
// (SdCardFont::loadMeasureKernRows) and routes every layout-time kern lookup
// through them (SdCardFont::getMeasureKern). These tests pin, over the REAL
// LibreFranklin_16.cpfont in fs_/fonts:
//
//   1. The measured advance of "r." equals drawText's cursor arithmetic —
//      toPixel(adv(r) + kern(r,.)) + toPixel(adv(.)) — computed from the
//      glyph data the draw side uses after a full render prewarm.
//   2. The kern is actually IN the measure: measured("r.") is strictly less
//      than the unkerned sum the old fast path returned.
//   3. GfxRenderer::getKerning is deterministic during layout: it returns the
//      font's r→. kern with only the advance table built, no render prewarm.
//   4. The measurement does not change when a full render prewarm later
//      builds the per-page mini matrix — measure and draw agree in both
//      orders.

#include <gtest/gtest.h>

#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>

#include <deque>
#include <string>

HalDisplay display;

namespace {

class SdKernMeasure : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = new GfxRenderer(display);
    renderer_->begin();

    if (!registry_.discover() || registry_.getFamilyCount() == 0) {
      GTEST_SKIP() << "needs SD fonts under fs_; run from the repo root or set CROSSPOINT_TEST_SD";
    }
    const SdCardFontFamilyInfo* libreFranklin = nullptr;
    for (const auto& fam : registry_.getFamilies()) {
      if (fam.name == "LibreFranklin") libreFranklin = &fam;
    }
    if (!libreFranklin) {
      GTEST_SKIP() << "LibreFranklin not installed in the SD tree";
    }
    ASSERT_TRUE(manager_.loadFamily(*libreFranklin, *renderer_, 16));
    fontId_ = manager_.getFontId("LibreFranklin");
    ASSERT_NE(fontId_, 0);
    sdFont_ = renderer_->getSdCardFonts().at(fontId_);
    ASSERT_NE(sdFont_, nullptr);
  }

  void TearDown() override {
    if (renderer_) {
      manager_.unloadAll(*renderer_);
      delete renderer_;
      renderer_ = nullptr;
    }
  }

  // The exact layout-time preparation ParsedText performs for a paragraph.
  void ensureForLayout(const std::deque<std::string>& words) {
    renderer_->ensureSdCardFontReady(fontId_, words, /*includeHyphen=*/true, /*styleMask=*/0x01);
  }

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  GfxRenderer* renderer_ = nullptr;
  SdCardFont* sdFont_ = nullptr;
  int fontId_ = 0;
};

TEST_F(SdKernMeasure, MeasureEqualsDrawArithmeticForTrailingPeriod) {
  ensureForLayout({"r."});
  const int measured = renderer_->getTextAdvanceX(fontId_, "r.", EpdFontFamily::REGULAR);

  // drawText's cursor walk for "r.": toPixel(adv(r) + kern(r,.)) + toPixel(adv(.)).
  // Advances from the same table the fast path reads; the kern from the
  // measure rows, which tests 3/4 pin against the render-side matrix.
  const int32_t advR = sdFont_->getAdvance('r', 0);
  const int32_t advDot = sdFont_->getAdvance('.', 0);
  ASSERT_GT(advR, 0);
  ASSERT_GT(advDot, 0);
  const int8_t kern = sdFont_->getMeasureKern('r', '.', 0);
  EXPECT_LT(kern, 0) << "LibreFranklin 16 kerns r. by -2.75 px (-44 FP); 0 means the rows never loaded";

  const int drawn = fp4::toPixel(advR + kern) + fp4::toPixel(advDot);
  EXPECT_EQ(measured, drawn) << "measured width must equal drawText's cursor arithmetic";

  // And the kern is really in the measure: the old fast path returned the
  // unkerned sum, which must now be strictly wider.
  const int unkerned = fp4::toPixel(advR) + fp4::toPixel(advDot);
  EXPECT_LT(measured, unkerned);
}

TEST_F(SdKernMeasure, RendererKerningIsDeterministicDuringLayout) {
  // Layout-time preparation only — no page has ever been rendered, so the
  // per-page mini kern matrix does not exist. The old code returned 0 here
  // (or a stale value once any page had rendered).
  ensureForLayout({"Try.", "r."});
  const int kernPx = renderer_->getKerning(fontId_, 'r', '.', EpdFontFamily::REGULAR);
  EXPECT_LT(kernPx, 0) << "layout must see the r->. kern without a render prewarm";
  EXPECT_EQ(kernPx, fp4::toPixel(static_cast<int32_t>(sdFont_->getMeasureKern('r', '.', 0))));
}

TEST_F(SdKernMeasure, MeasureUnchangedByRenderPrewarm) {
  ensureForLayout({"Try."});
  const int beforeRender = renderer_->getTextAdvanceX(fontId_, "Try.", EpdFontFamily::REGULAR);

  // Full render prewarm: builds the per-page mini kern matrix drawText uses.
  sdFont_->prewarm("Try.", 0x01, /*metadataOnly=*/false);
  const int afterRender = renderer_->getTextAdvanceX(fontId_, "Try.", EpdFontFamily::REGULAR);
  EXPECT_EQ(beforeRender, afterRender) << "rendering a page must not change what layout measures";

  // The measure rows and the render-side mini matrix must carry the same cell.
  EpdFont* epd = sdFont_->getEpdFont(0);
  ASSERT_NE(epd, nullptr);
  EXPECT_EQ(static_cast<int>(sdFont_->getMeasureKern('r', '.', 0)), static_cast<int>(epd->getKerning('r', '.')));
  EXPECT_EQ(static_cast<int>(sdFont_->getMeasureKern('T', 'r', 0)), static_cast<int>(epd->getKerning('T', 'r')));
}

TEST_F(SdKernMeasure, SpaceAdvanceStaysUnkernedForShippedFonts) {
  // Audit §3a: space is not a kern partner for punctuation in any shipped
  // font, so adding the flanking-kern terms must not change the gap.
  ensureForLayout({"end.", "Next"});
  const int plain = renderer_->getSpaceWidth(fontId_, EpdFontFamily::REGULAR);
  const int flanked = renderer_->getSpaceAdvance(fontId_, '.', 'N', EpdFontFamily::REGULAR);
  EXPECT_EQ(plain, flanked);
}

}  // namespace
