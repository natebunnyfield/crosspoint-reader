// The spaced-sign rule through the REAL renderer and SD font (review P2f):
// GfxRenderer::getSpacedSignGap over a real .cpfont, checked against the white
// the DRAW side would put on the page -- each glyph's drawn columns decoded
// here, independently, from the bitmap drawText blits after a render prewarm
// (getDrawnBox reads the file; this reads the per-page render data).
//
// Two properties:
//   1. For spaced sums in one style, the white left of the sign equals the
//      white right of it -- and the plain space (all main ever did) leaves at
//      least one of the same sums off-center, so the fixture discriminates.
//   2. Mixed styles (review P1): each neighbor measured in its own word's
//      style, one target per sign from the sign's style, still equal.
//
// Over LibreFranklin 12 in the SD tree; skips without it.

#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <SpacedSignWhite.h>
#include <gtest/gtest.h>

#include <deque>
#include <string>

extern HalDisplay display;  // SdKernMeasureTest.cpp

namespace {

using Style = EpdFontFamily::Style;

std::string utf8(const uint32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s += static_cast<char>(cp);
  } else if (cp < 0x800) {
    s += static_cast<char>(0xC0 | (cp >> 6));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
  }
  return s;
}

class SpacedSignGap : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = new GfxRenderer(display);
    renderer_->begin();
    if (!registry_.discover() || registry_.getFamilyCount() == 0) {
      GTEST_SKIP() << "needs SD fonts under fs_; run from the repo root or set CROSSPOINT_TEST_SD";
    }
    const SdCardFontFamilyInfo* family = nullptr;
    for (const auto& fam : registry_.getFamilies()) {
      if (fam.name == "LibreFranklin") family = &fam;
    }
    if (!family) GTEST_SKIP() << "LibreFranklin not installed in the SD tree";
    ASSERT_TRUE(manager_.loadFamily(*family, *renderer_, 12));
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

  // Right side-bearing and left side-bearing of one glyph as drawText paints
  // it: advance snapped to the pixel, ink = columns with a nonzero level.
  struct Drawn {
    int rsb = 0;
    int lsb = 0;
  };
  Drawn drawn(const uint32_t cp, const Style style) {
    const uint8_t idx = static_cast<uint8_t>(style & 0x03);
    const std::string text = utf8(cp);
    sdFont_->prewarm(text.c_str(), static_cast<uint8_t>(1u << idx), /*metadataOnly=*/false);
    EpdFont* font = sdFont_->getEpdFont(idx);
    EXPECT_NE(font, nullptr);
    const EpdGlyph* g = font ? font->getGlyph(cp) : nullptr;
    EXPECT_NE(g, nullptr) << "U+" << std::hex << cp;
    if (!g) return {};
    const uint8_t* bm = renderer_->getGlyphBitmap(font->data, g);
    EXPECT_NE(bm, nullptr);
    int first = g->width, last = -1, pos = 0;
    for (int y = 0; y < g->height; y++) {
      for (int x = 0; x < g->width; x++, pos++) {
        const int v =
            font->data->is2Bit ? (bm[pos / 4] >> (6 - 2 * (pos % 4))) & 3 : (bm[pos / 8] >> (7 - pos % 8)) & 1;
        if (v) {
          first = std::min(first, x);
          last = std::max(last, x);
        }
      }
    }
    EXPECT_GE(last, 0);
    return {fp4::toPixel(g->advanceX) - (g->left + last + 1), g->left + first};
  }

  void ensure(const std::deque<std::string>& words, const uint8_t styleMask) {
    renderer_->ensureSdCardFontReady(fontId_, words, /*includeHyphen=*/true, styleMask);
  }

  int white(const uint32_t l, const Style ls, const int gap, const uint32_t r, const Style rs) {
    return drawn(l, ls).rsb + gap + drawn(r, rs).lsb;
  }

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  GfxRenderer* renderer_ = nullptr;
  SdCardFont* sdFont_ = nullptr;
  int fontId_ = 0;
};

struct Sum {
  uint32_t a, sign, b;
  Style as = Style::REGULAR, ss = Style::REGULAR, bs = Style::REGULAR;
};

}  // namespace

TEST_F(SpacedSignGap, EqualWhiteBothSidesAndThePlainSpaceIsNot) {
  const Sum sums[] = {{'3', '+', '4'},    {'2', 0xD7, '3'},   {'a', '<', 'b'},
                      {'x', 0x2212, 'y'}, {'a', 0x2265, 'b'}, {'1', '=', '7'}};
  int plainOffCenter = 0;
  for (const Sum& s : sums) {
    ensure({utf8(s.a), utf8(s.sign), utf8(s.b), "0"}, 0x01);
    const int gl = renderer_->getSpacedSignGap(fontId_, s.a, Style::REGULAR, s.sign, Style::REGULAR, Style::REGULAR);
    const int gr = renderer_->getSpacedSignGap(fontId_, s.sign, Style::REGULAR, s.b, Style::REGULAR, Style::REGULAR);
    ASSERT_GE(gl, 0) << "U+" << std::hex << s.sign;
    ASSERT_GE(gr, 0) << "U+" << std::hex << s.sign;
    const int wl = white(s.a, Style::REGULAR, gl, s.sign, Style::REGULAR);
    const int wr = white(s.sign, Style::REGULAR, gr, s.b, Style::REGULAR);
    EXPECT_EQ(wl, wr) << "U+" << std::hex << s.a << " U+" << s.sign << " U+" << s.b;

    const int pl = renderer_->getSpaceAdvance(fontId_, s.a, s.sign, Style::REGULAR);
    const int pr = renderer_->getSpaceAdvance(fontId_, s.sign, s.b, Style::REGULAR);
    if (white(s.a, Style::REGULAR, pl, s.sign, Style::REGULAR) !=
        white(s.sign, Style::REGULAR, pr, s.b, Style::REGULAR))
      plainOffCenter++;
  }
  EXPECT_GT(plainOffCenter, 0) << "fixture does not discriminate: the plain space already centers every sum";
}

TEST_F(SpacedSignGap, EachNeighborInItsOwnStyleOneTargetPerSign) {
  if (!sdFont_->hasStyle(1) || !sdFont_->hasStyle(2)) GTEST_SKIP() << "needs bold and italic";
  const Sum sums[] = {
      {'1', '=', '2', Style::REGULAR, Style::REGULAR, Style::BOLD},    // 1 = <b>2</b>
      {'x', '=', 'y', Style::ITALIC, Style::REGULAR, Style::ITALIC},   // <i>x</i> = <i>y</i>
      {'3', '+', '4', Style::BOLD, Style::BOLD, Style::REGULAR},       // <b>3 +</b> 4
      {'a', 0x2265, 'b', Style::REGULAR, Style::ITALIC, Style::BOLD},  // a <i>≥</i> <b>b</b>
  };
  int leftStyleOffCenter = 0;
  for (const Sum& s : sums) {
    ensure({utf8(s.a), utf8(s.sign), utf8(s.b), "0"}, 0x07);
    const int gl = renderer_->getSpacedSignGap(fontId_, s.a, s.as, s.sign, s.ss, s.ss);
    const int gr = renderer_->getSpacedSignGap(fontId_, s.sign, s.ss, s.b, s.bs, s.ss);
    ASSERT_GE(gl, 0);
    ASSERT_GE(gr, 0);
    EXPECT_EQ(white(s.a, s.as, gl, s.sign, s.ss), white(s.sign, s.ss, gr, s.b, s.bs))
        << "U+" << std::hex << s.a << " U+" << s.sign << " U+" << s.b;

    // The first build's shortcut: every glyph of a gap, and its target, in the
    // LEFT word's style. Drawn in their real styles, those gaps lean.
    const int ol = renderer_->getSpacedSignGap(fontId_, s.a, s.as, s.sign, s.as, s.as);
    const int orr = renderer_->getSpacedSignGap(fontId_, s.sign, s.ss, s.b, s.ss, s.ss);
    if (ol >= 0 && orr >= 0 && white(s.a, s.as, ol, s.sign, s.ss) != white(s.sign, s.ss, orr, s.b, s.bs))
      leftStyleOffCenter++;
  }
  EXPECT_GT(leftStyleOffCenter, 0) << "fixture does not discriminate: the left-style shortcut already centers these";
}
