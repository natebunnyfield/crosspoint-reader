// The grayscale text anti-aliasing pass — TextAa::overlay.
//
// What is worth testing here is not the pixels (a plane flag's optical result
// happens in a waveform no host can run) but the CONTRACT the pass has with
// its caller, because every part of it fails silently on a panel:
//
//   1. The BW framebuffer the caller just displayed must survive the pass.
//      The tiled path's whole reason to exist is that it never overwrites it,
//      and the intact frame is what the next differential update diffs
//      against. A pass that corrupted it would show up as ghosting on the
//      NEXT screen, not this one.
//   2. The tiled path must be preferred when the controller supports strips,
//      because the alternative costs 48 KB of heap on a device with ~380 KB.
//   3. It must cover the panel exactly once per plane — no missed band (a
//      stripe of un-antialiased text) and no double-written one.
//   4. The whole-frame path must still work, since it is the fallback for a
//      controller without strip support, and it must restore what it saved.
//   5. The callback must run for both planes in both paths. A callback that
//      ran once would flag one plane and produce the wrong gray level.
//
// Links the REAL GfxRenderer against the stub HAL in tools/calendar_preview,
// the same arrangement image_polarity and renderer_bounds use.

#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <TextAntiAliasingPass.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "builtinFonts/librefranklin_10_regular.h"
#include "builtinFonts/librefranklin_reader_12_regular.h"

HalDisplay display;

// Every 2-bit builtin EXCEPT the chrome cuts is compressed, so those glyphs
// only reach the framebuffer through the decompressor + cache manager. Mirrors
// src/main.cpp:43-48 and tools/calendar_preview/render_harness.cpp:58-66. (The
// chrome cuts are 2-bit uncompressed and need none of this -- see
// docs/two-bit-chrome.md; the fixture still wires it for the reader face below.)
FontDecompressor fontDecompressor;

namespace {

// Records every band the pass asked the renderer to draw into, so a test can
// assert coverage without any panel.
struct Recorder {
  GfxRenderer* renderer = nullptr;
  int calls = 0;
  std::vector<int> lsbRowsSeen;
  std::vector<int> msbRowsSeen;
  // A pixel the callback paints on every invocation, used to prove the writes
  // went to the scratch band and not to the framebuffer.
  int paintX = 8;
  int paintY = 8;

  static void trampoline(void* ctx) {
    auto* self = static_cast<Recorder*>(ctx);
    self->calls++;
    self->renderer->drawPixel(self->paintX, self->paintY, true);
  }
};

class TextAntiAliasing : public ::testing::Test {
 protected:
  static GfxRenderer& renderer() {
    static GfxRenderer r(display);
    static bool begun = [] {
      r.begin();
      static FontCacheManager fcm(r.getFontMap(), r.getSdCardFonts()
#if CROSSPOINT_RENDER_SCALE > 1
                                                      ,
                                  r.getHiResSdCardFonts()
#endif
      );
      fcm.setFontDecompressor(&fontDecompressor);
      r.setFontCacheManager(&fcm);
      return true;
    }();
    (void)begun;
    return r;
  }

  void SetUp() override {
    display.resetGrayscaleCounters();
    display.stripGrayscale = false;
    renderer().setRenderMode(GfxRenderer::BW);
    renderer().clearScreen(0xFF);
  }

  static uint8_t* frame() { return display.getFrameBuffer(); }
  static uint32_t frameBytes() { return display.getBufferSize(); }

  // A recognisable BW frame standing in for "the page the caller just drew and
  // displayed".
  static std::vector<uint8_t> paintBaseFrame() {
    uint8_t* fb = frame();
    for (uint32_t i = 0; i < frameBytes(); ++i) {
      fb[i] = static_cast<uint8_t>(0xA5 ^ (i & 0xFF));
    }
    return std::vector<uint8_t>(fb, fb + frameBytes());
  }
};

TEST_F(TextAntiAliasing, TiledPathLeavesTheBwFrameByteIdentical) {
  display.stripGrayscale = true;
  const auto before = paintBaseFrame();

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

  EXPECT_EQ(0, std::memcmp(before.data(), frame(), before.size()))
      << "the tiled path must never write the framebuffer; the intact BW frame "
         "is the differential baseline for the next update";
}

TEST_F(TextAntiAliasing, TiledPathIsPreferredWhenTheControllerSupportsStrips) {
  display.stripGrayscale = true;
  paintBaseFrame();

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

  EXPECT_GT(display.stripWrites, 0) << "strips available but the pass did not use them";
  EXPECT_EQ(0, display.lsbPlaneCopies) << "whole-frame plane copy used despite strip support";
  EXPECT_EQ(0, display.msbPlaneCopies);
}

TEST_F(TextAntiAliasing, TiledPathCoversEveryPanelRowExactlyOncePerPlane) {
  display.stripGrayscale = true;
  paintBaseFrame();

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

  const int panelRows = static_cast<int>(display.getDisplayHeight());
  EXPECT_EQ(panelRows, display.lsbStripRows) << "LSB plane did not cover the panel exactly once";
  EXPECT_EQ(panelRows, display.msbStripRows) << "MSB plane did not cover the panel exactly once";
}

TEST_F(TextAntiAliasing, TiledPathPushesOneOverlayAndResyncsTheBaseline) {
  display.stripGrayscale = true;
  paintBaseFrame();

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

  EXPECT_EQ(1, display.grayDisplays) << "exactly one grayscale refresh per overlay";
  EXPECT_EQ(1, display.grayCleanups) << "controller RAM must be re-synced from the intact frame";
}

TEST_F(TextAntiAliasing, TiledPathRunsTheCallbackForBothPlanes) {
  display.stripGrayscale = true;
  paintBaseFrame();

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

  // Once per band per plane. Bands are 80 rows.
  const int panelRows = static_cast<int>(display.getDisplayHeight());
  const int bands = (panelRows + 79) / 80;
  EXPECT_EQ(bands * 2, rec.calls);
}

TEST_F(TextAntiAliasing, WholeFramePathIsUsedWithoutStripSupportAndRestoresTheFrame) {
  display.stripGrayscale = false;
  const auto before = paintBaseFrame();

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

  EXPECT_EQ(0, display.stripWrites) << "no strip support, yet the strip path ran";
  EXPECT_EQ(1, display.lsbPlaneCopies);
  EXPECT_EQ(1, display.msbPlaneCopies);
  EXPECT_EQ(1, display.grayDisplays);
  EXPECT_EQ(2, rec.calls) << "one render per plane";
  EXPECT_EQ(0, std::memcmp(before.data(), frame(), before.size()))
      << "restoreBwBuffer must put back exactly what storeBwBuffer saved";
}

TEST_F(TextAntiAliasing, LeavesTheRendererBackInBwMode) {
  for (const bool strips : {false, true}) {
    display.resetGrayscaleCounters();
    display.stripGrayscale = strips;
    paintBaseFrame();
    renderer().setRenderMode(GfxRenderer::BW);

    Recorder rec;
    rec.renderer = &renderer();
    TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, &Recorder::trampoline, &rec);

    // A pass that left GRAYSCALE_MSB set would send the NEXT screen's whole
    // render into a plane buffer and display nothing.
    EXPECT_EQ(GfxRenderer::BW, renderer().getRenderMode()) << "strips=" << strips;
  }
}

TEST_F(TextAntiAliasing, StrengthIsAppliedToTheRendererBeforeThePlanePasses) {
  display.stripGrayscale = true;
  paintBaseFrame();
  renderer().setGrayscaleAaStrength(GfxRenderer::AA_STANDARD);

  Recorder rec;
  rec.renderer = &renderer();
  TextAa::overlay(renderer(), GfxRenderer::AA_DARK, &Recorder::trampoline, &rec);

  EXPECT_EQ(GfxRenderer::AA_DARK, renderer().getGrayscaleAaStrength());
}

// ---------------------------------------------------------------------------
// The ceiling on where anti-aliasing can go at all.
//
// A grayscale plane is flagged only from the 2-BIT branch of
// GfxRenderer::renderCharImpl — the branch that reads a glyph's four coverage
// levels. A 1-bit glyph has no partial coverage to flag, and its one write in
// a plane pass CLEARS a bit in a plane that starts cleared, so it contributes
// nothing at all.
//
// That USED to rule out every chrome surface. lib/EpdFont/builtinFonts held
// exactly twelve 1-bit faces — the Libre Franklin 8/10/12 cuts main.cpp binds
// to SMALL_FONT_ID, UI_10_FONT_ID and UI_12_FONT_ID — so headers, list rows,
// button hints, popups, the keyboard, the colophon and the file viewer could
// not be antialiased no matter what overlay was plumbed to them, and Colophon
// and TextViewer were wired up and reverted after an A/B showed the frames
// byte-identical.
//
// Those twelve were rebuilt 2-bit on 2026-08-14 (uncompressed: compressing
// them would have been cheaper on flash and 6.1x slower on the colophon --
// docs/two-bit-chrome.md), and the colophon and file viewer are wired again.
// So nothing in builtinFonts is 1-bit today and this case can no longer borrow
// a real face for its fixture; it builds a synthetic 1-bit glyph instead.
//
// The RULE is what is under test, not the font, and it still binds: an SD
// .cpfont written 1-bit, or a chrome cut regenerated without --2bit, silently
// turns every one of those overlays back into a second panel refresh for zero
// pixels. test/system_font's EveryChromeCutIsTwoBit guards the built-ins;
// this guards the mechanism they rely on.
// ---------------------------------------------------------------------------

TEST_F(TextAntiAliasing, OneBitFontContributesNothingToAGrayscalePlane) {
  // A synthetic 1-bit face: one glyph, a solid 8x8 block of ink. Built here
  // rather than borrowed from builtinFonts because no built-in is 1-bit any
  // more -- see the block above. 1-bit packing is a continuous MSB-first
  // bitstream where a SET bit is ink, so 0xFF x 8 is a filled square.
  static const uint8_t kInk[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  // Field order is width, height, advanceX (12.4 fixed-point), left, top,
  // dataLength, dataOffset -- EpdFontData.h:131-139.
  static const EpdGlyph kGlyph = {8, 8, 10 << 4, 0, 8, 8, 0};
  static const EpdUnicodeInterval kInterval = {'A', 'A', 0};
  static EpdFontData kOneBit = {};
  kOneBit.bitmap = kInk;
  kOneBit.glyph = &kGlyph;
  kOneBit.intervals = &kInterval;
  kOneBit.intervalCount = 1;
  kOneBit.advanceY = 12;
  kOneBit.ascender = 10;
  kOneBit.descender = -2;
  kOneBit.is2Bit = false;

  ASSERT_FALSE(kOneBit.is2Bit) << "fixture no longer a 1-bit face";

  static EpdFont font(&kOneBit);
  static EpdFontFamily family(&font, &font, &font, &font);
  renderer().insertFont(9101, family);

  // Sanity first: the face DOES draw, so the empty-plane assertion below is
  // about the 1-bit branch and not about a font that failed to load.
  renderer().setRenderMode(GfxRenderer::BW);
  renderer().clearScreen(0xFF);
  renderer().drawText(9101, 20, 60, "AAAA", true);
  bool anyInk = false;
  for (uint32_t i = 0; i < frameBytes() && !anyInk; ++i) anyInk = frame()[i] != 0xFF;
  ASSERT_TRUE(anyInk) << "fixture face drew nothing at all in BW";

  renderer().setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer().clearScreen(0x00);
  renderer().drawText(9101, 20, 60, "AAAA", true);
  renderer().setRenderMode(GfxRenderer::BW);

  const uint8_t* fb = frame();
  for (uint32_t i = 0; i < frameBytes(); ++i) {
    ASSERT_EQ(0x00, fb[i]) << "a 1-bit face flagged plane byte " << i
                           << "; every 'can this screen be antialiased' answer rests on it not doing that";
  }
}

// The chrome faces specifically, since they are what the four new overlay call
// sites draw with. A depth regression here is invisible on a panel: the screen
// still renders, the overlay still runs, and the only symptom is a second
// refresh that changes nothing.
TEST_F(TextAntiAliasing, TheChromeFacesAreTwoBitAndFlagAPlane) {
  ASSERT_TRUE(librefranklin_10_regular.is2Bit)
      << "the UI chrome cut is 1-bit again -- Colophon, TextViewer, Settings and Home are paying a panel refresh "
         "for nothing (docs/two-bit-chrome.md)";

  static EpdFont font(&librefranklin_10_regular);
  static EpdFontFamily family(&font, &font, &font, &font);
  // No prewarm: the chrome cuts are 2-bit UNCOMPRESSED, so their glyphs come
  // straight out of flash with no decompressor involved (test/system_font's
  // NoChromeCutIsCompressed pins that).
  renderer().insertFont(9103, family);

  renderer().setGrayscaleAaStrength(GfxRenderer::AA_STANDARD);
  renderer().setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer().clearScreen(0x00);
  renderer().drawText(9103, 20, 60, "Handgloves", true);
  renderer().setRenderMode(GfxRenderer::BW);

  const uint8_t* fb = frame();
  bool anyFlagged = false;
  for (uint32_t i = 0; i < frameBytes() && !anyFlagged; ++i) anyFlagged = fb[i] != 0x00;
  EXPECT_TRUE(anyFlagged) << "the chrome face flagged no plane pixels";
}

TEST_F(TextAntiAliasing, TwoBitFontDoesFlagAGrayscalePlane) {
  ASSERT_TRUE(librefranklin_reader_12_regular.is2Bit) << "fixture no longer a 2-bit face";

  static EpdFont font(&librefranklin_reader_12_regular);
  static EpdFontFamily family(&font, &font, &font, &font);
  renderer().insertFont(9102, family);

  // The READER cuts are compressed (unlike the chrome cuts), so their glyph
  // groups only become drawable after a prewarm pass -- the same one
  // CalendarSleepScreen::render does.
  ASSERT_NE(nullptr, renderer().getFontCacheManager());
  renderer().getFontCacheManager()->prewarmCache(9102, "Handgloves", 0x01);

  // Sanity: the face must actually render, or the plane assertion below would
  // pass vacuously for the wrong reason.
  // 0xFF is white paper (a set bit is white); black ink clears bits.
  renderer().setRenderMode(GfxRenderer::BW);
  renderer().clearScreen(0xFF);
  renderer().drawText(9102, 20, 60, "Handgloves", true);
  bool anyInk = false;
  for (uint32_t i = 0; i < frameBytes() && !anyInk; ++i) anyInk = frame()[i] != 0xFF;
  ASSERT_TRUE(anyInk) << "fixture face drew nothing at all in BW";

  renderer().setGrayscaleAaStrength(GfxRenderer::AA_STANDARD);
  renderer().setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer().clearScreen(0x00);
  renderer().drawText(9102, 20, 60, "Handgloves", true);
  renderer().setRenderMode(GfxRenderer::BW);

  const uint8_t* fb = frame();
  bool anyFlagged = false;
  for (uint32_t i = 0; i < frameBytes() && !anyFlagged; ++i) {
    anyFlagged = fb[i] != 0x00;
  }
  EXPECT_TRUE(anyFlagged) << "a 2-bit face flagged no plane pixels — the overlay would have nothing to lift";
}

TEST_F(TextAntiAliasing, NullCallbackIsRefusedRatherThanCrashing) {
  display.stripGrayscale = true;
  const auto before = paintBaseFrame();

  TextAa::overlay(renderer(), GfxRenderer::AA_STANDARD, nullptr, nullptr);

  EXPECT_EQ(0, display.grayDisplays);
  EXPECT_EQ(0, std::memcmp(before.data(), frame(), before.size()));
}

}  // namespace
