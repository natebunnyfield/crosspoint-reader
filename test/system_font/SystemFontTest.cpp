// System font: the built-in Libre Franklin cuts, and the hi-res registration
// that has to survive reader font churn.
//
// Two things are under test, and they are separate failures.
//
// 1. THE CUTS. Libre Franklin shipped only as an SD .cpfont, which the chrome
//    cannot draw with -- the UI faces are compiled into the binary. Making it a
//    system font meant generating a 3x2 matrix (8/10/12 pt, regular and bold)
//    plus a 2x companion for each. A cut that silently lost its Latin Extended
//    coverage, or a companion generated at the wrong ppem, would render as
//    "looks a bit off" rather than as an error, so assert the shape directly.
//
// 2. THE REGISTRATION. GfxRenderer::clearSdCardFonts() used to clear the hi-res
//    map outright. Every reader font or size change routes through
//    SdCardFontManager::unloadAll() -> clearSdCardFonts(), so the moment the UI
//    faces gained 2x companions, the first font-size change in Text Settings
//    wiped them and the chrome fell back to replicated 1x pixels until reboot.
//    The reader itself looked fine throughout, because loadFile() re-registers
//    the reader font on the way back in -- which is exactly why the bug read as
//    "2x rendering is lost" rather than as anything font-manager shaped.
//
// Built at CROSSPOINT_RENDER_SCALE=2: at scale 1 the whole hi-res path compiles
// out and test 2 would assert nothing.

#include <GfxRenderer.h>
#include <SdCardFontManager.h>
#include <SdCardFontRegistry.h>
#include <builtinFonts/librefranklin_10_bold.h>
#include <builtinFonts/librefranklin_10_bold_2x.h>
#include <builtinFonts/librefranklin_10_regular.h>
#include <builtinFonts/librefranklin_10_regular_2x.h>
#include <builtinFonts/librefranklin_12_bold.h>
#include <builtinFonts/librefranklin_12_bold_2x.h>
#include <builtinFonts/librefranklin_12_regular.h>
#include <builtinFonts/librefranklin_12_regular_2x.h>
#include <builtinFonts/librefranklin_8_bold.h>
#include <builtinFonts/librefranklin_8_bold_2x.h>
#include <builtinFonts/librefranklin_8_regular.h>
#include <builtinFonts/librefranklin_8_regular_2x.h>
#include <fontIds.h>
#include <gtest/gtest.h>

#include <cstdlib>
#include <string>
#include <vector>

HalDisplay display;

namespace {

struct Cut {
  const char* name;
  const EpdFontData* oneX;
  const EpdFontData* twoX;
};

// The full matrix, in the order applySystemFont() consumes it.
const Cut kCuts[] = {
    {"librefranklin_8_regular", &librefranklin_8_regular, &librefranklin_8_regular_2x},
    {"librefranklin_8_bold", &librefranklin_8_bold, &librefranklin_8_bold_2x},
    {"librefranklin_10_regular", &librefranklin_10_regular, &librefranklin_10_regular_2x},
    {"librefranklin_10_bold", &librefranklin_10_bold, &librefranklin_10_bold_2x},
    {"librefranklin_12_regular", &librefranklin_12_regular, &librefranklin_12_regular_2x},
    {"librefranklin_12_bold", &librefranklin_12_bold, &librefranklin_12_bold_2x},
};

// Codepoints the chrome actually needs. The Latin-1 and Latin Extended-A
// samples are the ones that go missing first when an interval list is trimmed:
// a cut that covers only ASCII renders every accented book title as tofu.
constexpr uint32_t kRequiredCodepoints[] = {
    'A',    'z', '0', '9', ' ', '.', '%',
    0x00E9,  // e-acute   -- Latin-1
    0x00FC,  // u-umlaut  -- Latin-1
    0x00D1,  // N-tilde   -- Latin-1, and the Spanish translation needs it
    0x0141,  // L-stroke  -- Latin Extended-A
    0x2019,  // right single quote -- used in the UI strings themselves
};

}  // namespace

// --- 1. The cuts ------------------------------------------------------------

TEST(LibreFranklinBuiltinCuts, EveryCutCarriesGlyphsAndMetrics) {
  for (const auto& cut : kCuts) {
    for (const EpdFontData* data : {cut.oneX, cut.twoX}) {
      const bool hiRes = (data == cut.twoX);
      const std::string label = std::string(cut.name) + (hiRes ? " (2x)" : " (1x)");

      ASSERT_NE(data->bitmap, nullptr) << label << " has no bitmap data";
      ASSERT_NE(data->glyph, nullptr) << label << " has no glyph table";
      ASSERT_NE(data->intervals, nullptr) << label << " has no interval table";
      EXPECT_GT(data->intervalCount, 0u) << label;
      EXPECT_GT(data->advanceY, 0) << label << " has no line height";
      EXPECT_GT(data->ascender, 0) << label;
      EXPECT_LT(data->descender, 0) << label << " descender must be below the baseline";

      // Built-in cuts keep every glyph in RAM, so the interval table is the
      // whole truth and no miss handler should be wired up.
      EXPECT_EQ(data->glyphMissHandler, nullptr) << label << " is not an SD font";
      EXPECT_EQ(data->coverageHandler, nullptr) << label << " is not an SD font";

      const EpdFont font(data);
      for (const uint32_t cp : kRequiredCodepoints) {
        EXPECT_TRUE(font.hasCodepoint(cp)) << label << " is missing U+" << std::hex << cp;
      }
    }
  }
}

// A companion generated at the wrong ppem is the failure this catches: the
// glyphs would blit at the wrong size while layout kept using the 1x advances,
// so text would overrun or under-fill its measured box. Vertical metrics are
// the cheapest proxy for ppem that does not depend on hinting luck.
TEST(LibreFranklinBuiltinCuts, CompanionsAreRasterisedAtDoubleThePpem) {
  for (const auto& cut : kCuts) {
    // Hinting at 16/20/24 px does not land on exactly twice the 8/10/12 px
    // rounding, so allow a pixel of slop either way rather than demanding 2.0x.
    EXPECT_NEAR(cut.twoX->ascender, cut.oneX->ascender * 2, 2) << cut.name << " ascender";
    EXPECT_NEAR(cut.twoX->descender, cut.oneX->descender * 2, 2) << cut.name << " descender";

    // Same family, same coverage -- only the raster differs.
    EXPECT_EQ(cut.twoX->intervalCount, cut.oneX->intervalCount) << cut.name << " coverage differs between scales";
  }
}

// The bold cut has to be a genuinely different raster. Pointing both styles at
// the same EpdFontData compiles, links and renders -- it just renders bold text
// in regular, which is invisible in a diff and easy to do with a copy-paste in
// the family macro.
TEST(LibreFranklinBuiltinCuts, BoldIsNotTheRegularCut) {
  EXPECT_NE(librefranklin_8_bold.bitmap, librefranklin_8_regular.bitmap);
  EXPECT_NE(librefranklin_10_bold.bitmap, librefranklin_10_regular.bitmap);
  EXPECT_NE(librefranklin_12_bold.bitmap, librefranklin_12_regular.bitmap);
  EXPECT_NE(librefranklin_8_bold_2x.bitmap, librefranklin_8_regular_2x.bitmap);
  EXPECT_NE(librefranklin_10_bold_2x.bitmap, librefranklin_10_regular_2x.bitmap);
  EXPECT_NE(librefranklin_12_bold_2x.bitmap, librefranklin_12_regular_2x.bitmap);
}

// --- 2. The registration ----------------------------------------------------

namespace {

class ChromeHiRes : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = new GfxRenderer(display);
    renderer_->begin();

    if (!registry_.discover() || registry_.getFamilyCount() < 1) {
      GTEST_SKIP() << "needs at least one SD font family under fs_/ (found " << registry_.getFamilyCount()
                   << "); run from the repo root";
    }
  }

  void TearDown() override {
    if (renderer_) {
      manager_.unloadAll(*renderer_);
      delete renderer_;
      renderer_ = nullptr;
    }
  }

  // What applySystemFont() does at boot, minus the settings lookup: point the
  // three chrome slots at Libre Franklin and register the 2x companions.
  void applyLibreFranklinChrome() {
    renderer_->insertFont(SMALL_FONT_ID, ui8_);
    renderer_->insertFont(UI_10_FONT_ID, ui10_);
    renderer_->insertFont(UI_12_FONT_ID, ui12_);
    renderer_->registerHiResBuiltinFont(SMALL_FONT_ID, ui8HiRes_);
    renderer_->registerHiResBuiltinFont(UI_10_FONT_ID, ui10HiRes_);
    renderer_->registerHiResBuiltinFont(UI_12_FONT_ID, ui12HiRes_);
  }

  void expectChromeHasCompanions(const char* when) {
    for (const int id : {SMALL_FONT_ID, UI_10_FONT_ID, UI_12_FONT_ID}) {
      EXPECT_NE(renderer_->getHiResFamily(id), nullptr) << "chrome font id " << id << " lost its 2x companion " << when;
    }
  }

  static uint8_t ptForSlot(const SdCardFontFamilyInfo& fam, uint8_t slot) {
    const auto* f = fam.findClosestReaderSize(slot);
    return f ? f->pointSize : 0;
  }

  // Two slots that resolve to genuinely different point sizes for this family,
  // so loadFamily() really does tear down and rebuild rather than no-opping.
  static bool pickDistinctSlots(const SdCardFontFamilyInfo& fam, uint8_t& lo, uint8_t& hi) {
    for (uint8_t a = 0; a < 4; ++a) {
      for (uint8_t b = static_cast<uint8_t>(a + 1); b < 4; ++b) {
        if (ptForSlot(fam, a) != 0 && ptForSlot(fam, a) != ptForSlot(fam, b)) {
          lo = a;
          hi = b;
          return true;
        }
      }
    }
    return false;
  }

  EpdFont r8_{&librefranklin_8_regular}, b8_{&librefranklin_8_bold};
  EpdFont r10_{&librefranklin_10_regular}, b10_{&librefranklin_10_bold};
  EpdFont r12_{&librefranklin_12_regular}, b12_{&librefranklin_12_bold};
  EpdFontFamily ui8_{&r8_, &b8_}, ui10_{&r10_, &b10_}, ui12_{&r12_, &b12_};

  EpdFont hr8_{&librefranklin_8_regular_2x}, hb8_{&librefranklin_8_bold_2x};
  EpdFont hr10_{&librefranklin_10_regular_2x}, hb10_{&librefranklin_10_bold_2x};
  EpdFont hr12_{&librefranklin_12_regular_2x}, hb12_{&librefranklin_12_bold_2x};
  EpdFontFamily ui8HiRes_{&hr8_, &hb8_}, ui10HiRes_{&hr10_, &hb10_}, ui12HiRes_{&hr12_, &hb12_};

  SdCardFontRegistry registry_;
  SdCardFontManager manager_;
  GfxRenderer* renderer_ = nullptr;
};

}  // namespace

// Changing the setting a second time has to actually rebind the face.
//
// GfxRenderer::insertFont() refuses to overwrite an id it already holds -- it
// logs "already registered, ignoring duplicate" and returns -- which is the
// guard that stops two SD families colliding on a hashed id. applySystemFont()
// runs at boot AND on every change of the setting, so going through insertFont
// made the second call a silent no-op: the settings row read "Ubuntu" while
// every pixel stayed Libre Franklin until a reboot.
//
// The 2x half made it worse rather than better, which is why this asserts both
// halves move together. registerHiResBuiltinFont() uses insert_or_assign, so it
// DID take -- leaving the chrome blitting the newly chosen family's glyphs
// against the outgoing family's advances.
TEST_F(ChromeHiRes, ChangingTheSettingASecondTimeRebindsBothHalves) {
  applyLibreFranklinChrome();
  ASSERT_EQ(renderer_->getFontMap().at(UI_12_FONT_ID).getData(), &librefranklin_12_regular);
  ASSERT_EQ(renderer_->getHiResFamily(UI_12_FONT_ID)->getData(), &librefranklin_12_regular_2x);

  // "User picked a different system font." Stand in for another family with the
  // 10 pt cut: a different EpdFontData is all the assertion needs.
  EpdFont other{&librefranklin_10_regular}, otherHiRes{&librefranklin_10_regular_2x};
  const EpdFontFamily otherFamily{&other}, otherHiResFamily{&otherHiRes};
  renderer_->replaceFont(UI_12_FONT_ID, otherFamily);
  renderer_->registerHiResBuiltinFont(UI_12_FONT_ID, otherHiResFamily);

  EXPECT_EQ(renderer_->getFontMap().at(UI_12_FONT_ID).getData(), &librefranklin_10_regular)
      << "the 1x face did not change -- the UI would keep drawing the old font";
  EXPECT_EQ(renderer_->getHiResFamily(UI_12_FONT_ID)->getData(), &librefranklin_10_regular_2x)
      << "the 2x companion did not follow the 1x face";
}

// The regression, stated the way the bug was reported: change the reader font
// size in Text Settings and the chrome stops rendering at 2x.
TEST_F(ChromeHiRes, SurvivesAReaderFontSizeChange) {
  const auto& fam = registry_.getFamilies()[0];
  uint8_t lo = 0, hi = 0;
  if (!pickDistinctSlots(fam, lo, hi)) GTEST_SKIP() << fam.name << " has only one distinct size";

  applyLibreFranklinChrome();
  expectChromeHasCompanions("at boot");

  ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, ptForSlot(fam, lo)));
  expectChromeHasCompanions("after the reader font loaded");

  // The reported action. Internally: loadFamily -> unloadAll ->
  // clearSdCardFonts -> (previously) hiResFontMap_.clear().
  ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, ptForSlot(fam, hi)));
  expectChromeHasCompanions("after a reader font-size change");
}

// Same invariant across the other churn paths, since they share unloadAll().
TEST_F(ChromeHiRes, SurvivesFamilySwitchesAndGoingBuiltin) {
  const auto& fams = registry_.getFamilies();
  applyLibreFranklinChrome();

  for (size_t i = 0; i < fams.size(); ++i) {
    ASSERT_TRUE(manager_.loadFamily(fams[i], *renderer_, ptForSlot(fams[i], 1))) << fams[i].name;
    expectChromeHasCompanions("after switching reader family");
  }

  // "User picked a built-in reader font": unloadAll with nothing reloaded.
  manager_.unloadAll(*renderer_);
  expectChromeHasCompanions("after going built-in");
}

// --- 3. The rotated path ----------------------------------------------------

// drawTextRotated90CW is how the side-button hint labels are drawn -- the only
// rotated text in the chrome. It walks the same 1x metrics as drawText() but
// used to blit only the 1x glyph bitmaps, so at RENDER_SCALE=2 the hint
// chevrons rendered pixel-doubled on an otherwise hi-res screen (reported as
// mixed-resolution rendering on the keyboard entry screen, 2026-08-04).
// Registering a companion must change the rotated raster too; the cursor walk
// stays on the 1x metrics either way, so layout is unaffected.
TEST(RotatedHiRes, RotatedTextBlitsTheCompanionFace) {
  GfxRenderer renderer(display);
  renderer.begin();

  EpdFont regular{&librefranklin_8_regular}, bold{&librefranklin_8_bold};
  const EpdFontFamily fam{&regular, &bold};
  EpdFont regular2x{&librefranklin_8_regular_2x}, bold2x{&librefranklin_8_bold_2x};
  const EpdFontFamily fam2x{&regular2x, &bold2x};
  renderer.insertFont(SMALL_FONT_ID, fam);

  const auto snapshot = [&renderer]() {
    const uint8_t* fb = renderer.getFrameBuffer();
    return std::vector<uint8_t>(fb, fb + renderer.getBufferSize());
  };

  // The real chrome strings: the side hints draw single chevrons.
  renderer.clearScreen();
  renderer.drawTextRotated90CW(SMALL_FONT_ID, 40, 200, "<a");
  const std::vector<uint8_t> withoutCompanion = snapshot();
  ASSERT_NE(withoutCompanion, std::vector<uint8_t>(withoutCompanion.size(), withoutCompanion[0]))
      << "rotated draw produced a blank framebuffer -- the comparison below would be vacuous";

  renderer.registerHiResBuiltinFont(SMALL_FONT_ID, fam2x);
  renderer.clearScreen();
  renderer.drawTextRotated90CW(SMALL_FONT_ID, 40, 200, "<a");
  const std::vector<uint8_t> withCompanion = snapshot();

  EXPECT_NE(withoutCompanion, withCompanion)
      << "drawTextRotated90CW ignored the registered 2x companion -- rotated chrome text (the side-button "
         "hints) renders pixel-doubled at RENDER_SCALE > 1";
}

// The other half of the fix: sparing the built-ins must not spare the SD ones.
// If clearSdCardHiResFonts() stopped erasing them, every reader font ever
// loaded would leave a dangling companion behind pointing at freed glyph data.
TEST_F(ChromeHiRes, SdCompanionsAreStillDroppedOnUnload) {
  const auto& fam = registry_.getFamilies()[0];
  applyLibreFranklinChrome();

  ASSERT_TRUE(manager_.loadFamily(fam, *renderer_, ptForSlot(fam, 1)));
  const int sdId = manager_.getFontId(fam.name);
  ASSERT_NE(sdId, 0);

  // Not every family on the card is guaranteed a 2x/ directory; only assert the
  // teardown for one that actually registered a companion.
  const bool hadCompanion = renderer_->getHiResFamily(sdId) != nullptr;

  manager_.unloadAll(*renderer_);

  EXPECT_EQ(renderer_->getHiResFamily(sdId), nullptr)
      << "SD companion for " << fam.name << " outlived the font it belongs to";
  EXPECT_TRUE(renderer_->getHiResSdCardFonts().empty()) << "SD hi-res registry not emptied by unloadAll";
  if (hadCompanion) expectChromeHasCompanions("after the SD companion was dropped");
}
