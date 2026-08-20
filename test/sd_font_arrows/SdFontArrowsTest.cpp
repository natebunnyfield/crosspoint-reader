// Unicode arrows in the SD-card reading faces.
//
// THE BUG. Reported twice, most recently 2026-08-20: "only the TeXGyreSchola
// reading font renders Unicode arrows. Other installed families show nothing
// (or a fallback box) where an arrow should be."
//
// It was true, and it was in the FILES, not in the firmware. The Arrows block
// (U+2190-21FF) is requested by all six installed families, but no face in the
// old fallback chain carried it: NotoSans-Regular has 0 of the 128 codepoints
// and TeX Gyre Schola has 4. So the build asked for arrows, found them in
// neither the family's own cmap nor its fallback, and pruned them. Only TeX
// Gyre Schola shipped any, because its OWN face has those four.
//
// WHY A RENDERER FALLBACK CANNOT FIX IT, since that is the reflex. The
// renderer's fallback (GfxRenderer::resolveTextFontId) is registered only for
// the three chrome font ids and is gated on utf8IsCjkCodepoint, and the
// coverage face behind it is Noto Sans -- measured at 0 of 128 arrows. There is
// no face in the binary at reading sizes that can draw an arrow, so the glyph
// has to be present in the .cpfont. That makes this test a check on the
// SHIPPED FONT FILES, which is exactly the layer that regressed.
//
// WHY IT ASSERTS ON PIXELS AND NOT ON COVERAGE METADATA. A missing glyph is not
// blank: EpdFont::getGlyph falls back to U+FFFD and then to '?'
// (EpdFont.cpp:200-204), which is the "fallback box" in the report. So "the
// draw produced ink" would pass on a font with no arrows at all. The assertion
// instead renders the arrow and a codepoint known to be absent, and requires
// the two to DIFFER: identical framebuffers mean the arrow resolved to the same
// replacement glyph, which is precisely the reported symptom.
//
// Drives the REAL registry, manager and renderer over the real .cpfont tree
// under fs_/ (see stubs/HalStorage.h), so it tests the fonts that ship rather
// than a mock.

#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "notes/EditorFonts.h"

HalDisplay display;

namespace {

// Size slots, mirroring CrossPointSettings::FONT_SIZE (ordinal, not absolute).
constexpr uint8_t kSizeSlots[] = {0, 1, 2, 3};

// The arrows a reader actually meets in a book: the four cardinals, the two
// bidirectionals, the hook (footnote returns) and the double-struck right
// (used for implication in anything mathematical). NotoSansMath answers for
// all of them; this is the subset worth failing the build over, not all 128.
struct Arrow {
  const char* utf8;
  const char* name;
};
constexpr Arrow kReaderArrows[] = {
    {"←", "U+2190 LEFTWARDS ARROW"},           {"↑", "U+2191 UPWARDS ARROW"},
    {"→", "U+2192 RIGHTWARDS ARROW"},          {"↓", "U+2193 DOWNWARDS ARROW"},
    {"↔", "U+2194 LEFT RIGHT ARROW"},          {"↕", "U+2195 UP DOWN ARROW"},
    {"↩", "U+21A9 LEFTWARDS ARROW WITH HOOK"}, {"⇒", "U+21D2 RIGHTWARDS DOUBLE ARROW"},
};

// Codepoints no curated Latin reading family builds, used as the "this is what
// a missing glyph looks like" reference. Two of them, so the fixture can prove
// they agree with each other before any arrow is compared against them -- if
// they ever disagreed, the replacement path would have changed and every
// comparison below would be meaningless rather than merely failing.
constexpr const char* kAbsentProbeA = "ਕ";  // GURMUKHI LETTER KA
constexpr const char* kAbsentProbeB = "க";  // TAMIL LETTER KA

// WRITING faces are out of scope, and deliberately so.
//
// The registry discovers every family on the card, which includes the EDITOR
// group (src/notes/EditorFonts.h) -- writing faces chosen by SETTINGS.editorFont
// that "never join the reading tier" per the owner ruling of 2026-08-05. An
// editor-only face is a monospace typewriter texture for composing notes, it is
// not built by scripts/install-sim-fonts.py, and on most clones its source is a
// gitignored commercial TTF that cannot be rebuilt at all. Requiring arrows of
// it would fail everywhere for a reason unrelated to the reported bug.
//
// The predicate is editorfonts::isWritingOnlyFamily -- the SAME call the
// reading picker makes to decide whether to list a family at all. Reusing it
// rather than re-deriving the rule here means a face promoted to the reading
// tier starts being checked without anyone editing this file, and it inherits
// the two details a local reimplementation would have missed: the comparison is
// case-insensitive (the name comes from a directory on a FAT card), and retired
// writing faces still sitting on old cards are covered by FORMER_WRITING_FAMILIES.

class SdFontArrows : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = new GfxRenderer(display);
    renderer_->begin();

    if (!registry_.discover() || registry_.getFamilyCount() < 1) {
      GTEST_SKIP() << "needs >=1 SD font family under fs_/ (found " << registry_.getFamilyCount()
                   << "); run from the repo root or set CROSSPOINT_TEST_SD";
    }
  }

  void TearDown() override {
    if (renderer_) {
      manager_.unloadAll(*renderer_);
      delete renderer_;
      renderer_ = nullptr;
    }
  }

  static uint8_t ptForSlot(const SdCardFontFamilyInfo& fam, uint8_t slot) {
    const auto* f = fam.findClosestReaderSize(slot);
    return f ? f->pointSize : 0;
  }

  // Draw one string on a cleared screen and return the whole framebuffer.
  //
  // The explicit prewarm is not optional: drawText only calls
  // ensureSdGlyphsResident when it has REDIRECTED to a fallback font
  // (GfxRenderer.cpp:704), and nothing is redirected here, so an SD glyph that
  // was never prewarmed would render blank and every comparison would pass for
  // the wrong reason.
  std::vector<uint8_t> render(int fontId, SdCardFont* sd, const char* utf8) {
    sd->prewarm(utf8, 0x0F, /*metadataOnly=*/false);
    renderer_->clearScreen();
    renderer_->drawText(fontId, 40, 200, utf8);
    const uint8_t* fb = renderer_->getFrameBuffer();
    return std::vector<uint8_t>(fb, fb + renderer_->getBufferSize());
  }

  std::vector<uint8_t> blankScreen() {
    renderer_->clearScreen();
    const uint8_t* fb = renderer_->getFrameBuffer();
    return std::vector<uint8_t>(fb, fb + renderer_->getBufferSize());
  }

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  GfxRenderer* renderer_ = nullptr;
};

// Every installed family must draw a REAL glyph for each reader arrow, at every
// size slot and not merely at one. This is the reported bug.
TEST_F(SdFontArrows, EveryInstalledFamilyDrawsRealArrowGlyphs) {
  const auto& fams = registry_.getFamilies();
  for (const auto& fam : fams) {
    if (editorfonts::isWritingOnlyFamily(fam.name.c_str())) continue;
    for (const uint8_t slot : kSizeSlots) {
      const uint8_t pt = ptForSlot(fam, slot);
      ASSERT_NE(pt, 0) << fam.name << " has no file for slot " << static_cast<int>(slot);
      ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, pt)) << "failed to load " << fam.name;

      const int fontId = manager_.getFontId(fam.name);
      ASSERT_NE(fontId, 0) << fam.name << " loaded but has no font id";
      const auto& sdFonts = renderer_->getSdCardFonts();
      const auto it = sdFonts.find(fontId);
      ASSERT_NE(it, sdFonts.end()) << fam.name << " is not registered as an SD font";
      SdCardFont* sd = it->second;

      const std::vector<uint8_t> blank = blankScreen();
      const std::vector<uint8_t> missingA = render(fontId, sd, kAbsentProbeA);
      const std::vector<uint8_t> missingB = render(fontId, sd, kAbsentProbeB);

      // The comparison below is only meaningful if the replacement path is
      // stable and visible. Both must draw something, and the same something.
      ASSERT_NE(missingA, blank) << fam.name << " @" << static_cast<int>(pt)
                                 << "pt: a missing codepoint drew nothing at all, so 'differs from the "
                                    "replacement glyph' cannot distinguish anything";
      ASSERT_EQ(missingA, missingB) << fam.name << " @" << static_cast<int>(pt)
                                    << "pt: the two absent probes drew DIFFERENT glyphs, so at least one of "
                                       "them is present in this font and is not a valid missing-glyph reference";

      for (const auto& arrow : kReaderArrows) {
        const std::vector<uint8_t> drawn = render(fontId, sd, arrow.utf8);
        EXPECT_NE(drawn, blank) << fam.name << " @" << static_cast<int>(pt) << "pt drew NOTHING for " << arrow.name;
        EXPECT_NE(drawn, missingA)
            << fam.name << " @" << static_cast<int>(pt) << "pt drew the replacement glyph for " << arrow.name
            << " -- the font has no such glyph. Arrows come from the NotoSansMath tail of the fallback "
               "chain (build-sd-fonts.py fallback_chain_for), so this family's .cpfont files are stale: "
               "rebuild with python3 scripts/install-sim-fonts.py";
      }
    }
  }
}

// The hi-res tiers are separate FILES and regressed independently: the
// 2026-08-17 fix reached 1x and left 2x/3x on the old glyphless cut, so arrows
// worked on the device and stayed broken on every scaled host build. At
// CROSSPOINT_RENDER_SCALE 1 there is no companion to check and this is a no-op.
#if defined(CROSSPOINT_RENDER_SCALE) && CROSSPOINT_RENDER_SCALE > 1
TEST_F(SdFontArrows, HiResCompanionsCarryTheArrowsToo) {
  const auto& fams = registry_.getFamilies();
  for (const auto& fam : fams) {
    if (editorfonts::isWritingOnlyFamily(fam.name.c_str())) continue;
    const uint8_t pt = ptForSlot(fam, 2);
    ASSERT_NE(pt, 0) << fam.name << " has no file for slot 2";
    ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, pt)) << "failed to load " << fam.name;
    const int fontId = manager_.getFontId(fam.name);
    ASSERT_NE(fontId, 0);
    const auto& sdFonts = renderer_->getSdCardFonts();
    const auto it = sdFonts.find(fontId);
    ASSERT_NE(it, sdFonts.end());
    SdCardFont* sd = it->second;

    const std::vector<uint8_t> missing = render(fontId, sd, kAbsentProbeA);
    for (const auto& arrow : kReaderArrows) {
      EXPECT_NE(render(fontId, sd, arrow.utf8), missing)
          << fam.name << " at render scale " << CROSSPOINT_RENDER_SCALE << " drew the replacement glyph for "
          << arrow.name << " -- the hi-res companion is stale relative to the 1x set";
    }
  }
}
#endif

}  // namespace
