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

// Every 2-bit builtin is compressed, so a glyph only reaches the framebuffer
// through the decompressor + cache manager. Mirrors src/main.cpp:43-48 and
// tools/calendar_preview/render_harness.cpp:58-66.
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
// That is not a detail: every UI chrome font in this firmware is 1-bit.
// lib/EpdFont/builtinFonts holds exactly twelve 1-bit faces, and they are the
// twelve Libre Franklin 8/10/12 cuts that main.cpp:224-231 binds to
// SMALL_FONT_ID, UI_10_FONT_ID and UI_12_FONT_ID. So the headers, list rows,
// button hints, popups, keyboard, colophon and file viewer CANNOT be
// antialiased no matter what overlay is plumbed to them — running the pass
// over them buys a second panel refresh and zero pixels. Only the 2-bit faces
// (the SD .cpfont reading cuts, the librefranklin_reader_* fallbacks and the
// built-in editor monospaces) can take it.
//
// These two cases pin that, so the next person to wire up a screen finds out
// here rather than on a panel.
// ---------------------------------------------------------------------------

TEST_F(TextAntiAliasing, OneBitFontContributesNothingToAGrayscalePlane) {
  ASSERT_FALSE(librefranklin_10_regular.is2Bit) << "fixture no longer a 1-bit face";

  static EpdFont font(&librefranklin_10_regular);
  static EpdFontFamily family(&font, &font, &font, &font);
  renderer().insertFont(9101, family);

  // Sanity first: the face DOES draw, so the empty-plane assertion below is
  // about the 1-bit branch and not about a font that failed to load.
  renderer().setRenderMode(GfxRenderer::BW);
  renderer().clearScreen(0xFF);
  renderer().drawText(9101, 20, 60, "Handgloves", true);
  bool anyInk = false;
  for (uint32_t i = 0; i < frameBytes() && !anyInk; ++i) anyInk = frame()[i] != 0xFF;
  ASSERT_TRUE(anyInk) << "fixture face drew nothing at all in BW";

  renderer().setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  renderer().clearScreen(0x00);
  renderer().drawText(9101, 20, 60, "Handgloves", true);
  renderer().setRenderMode(GfxRenderer::BW);

  const uint8_t* fb = frame();
  for (uint32_t i = 0; i < frameBytes(); ++i) {
    ASSERT_EQ(0x00, fb[i]) << "a 1-bit face flagged plane byte " << i
                           << "; the whole 'chrome cannot antialias' ruling rests on it not doing that";
  }
}

TEST_F(TextAntiAliasing, TwoBitFontDoesFlagAGrayscalePlane) {
  ASSERT_TRUE(librefranklin_reader_12_regular.is2Bit) << "fixture no longer a 2-bit face";

  static EpdFont font(&librefranklin_reader_12_regular);
  static EpdFontFamily family(&font, &font, &font, &font);
  renderer().insertFont(9102, family);

  // Every 2-bit builtin is compressed: its glyph groups only become drawable
  // after a prewarm pass, the same one CalendarSleepScreen::render does.
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
