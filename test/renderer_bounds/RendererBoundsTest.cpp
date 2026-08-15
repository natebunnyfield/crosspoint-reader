// Renderer bounds — the class of bug that produced, on an X3 device:
//
//   [24143] [ERR] [GFX] !! Outside range (528, 302) -> (302, -1)
//
// The (302, -1) is a PHYSICAL coordinate. Portrait maps logical (x, y) with
// (GfxRenderer.cpp, rotateCoordinates): phyX = y, phyY = panelHeight - 1 - x.
// With the X3's panelHeight of 528, phyY == -1 means x == 528 — one column
// past the last valid logical column (527). The y of 302 was never out of
// range; it became phyX and is fine. So the fault was an X overrun of exactly
// one pixel, not the negative-Y ink-metrics problem it looked like.
//
// drawPixel() CLIPS such a write — it logs and returns without touching the
// framebuffer — so nothing crashes and no test can see it. GFX_BOUNDS_COUNTER
// makes it observable.
//
// The invariant under test:
//
//   A draw request whose declared geometry lies inside the screen must not
//   emit a single out-of-range pixel.
//
// Deliberately NOT asserted: text placed partly off-screen on purpose. That is
// legitimate clipping, and drawPixel dropping those pixels is correct.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <Utf8.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <string>

#include "fontIds.h"

HalDisplay display;

namespace {

// One renderer for the whole suite: begin() allocates the framebuffer chunks
// and the builtin fonts are compressed, so decompressor wiring must happen
// once before any drawText (mirrors src/main.cpp and the calendar harness).
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
    renderer_.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, lfReader12_);
    renderer_.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14_);
    renderer_.insertFont(LIBREFRANKLIN_READER_18_FONT_ID, lfReader18_);
    renderer_.insertFont(UI_10_FONT_ID, ui10_);
    renderer_.insertFont(UI_12_FONT_ID, ui12_);
    renderer_.insertFont(SMALL_FONT_ID, small_);
    // The editor/Claude face, and the one that carries no U+FFFD (B-009).
    renderer_.insertFont(IBMPLEXMONO_12_FONT_ID, ibmplexmono12_);
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;

  EpdFont lfr12R_{&librefranklin_reader_12_regular}, lfr12B_{&librefranklin_reader_12_bold},
      lfr12I_{&librefranklin_reader_12_italic}, lfr12BI_{&librefranklin_reader_12_bolditalic};
  EpdFontFamily lfReader12_{&lfr12R_, &lfr12B_, &lfr12I_, &lfr12BI_};
  EpdFont lfr14R_{&librefranklin_reader_14_regular}, lfr14B_{&librefranklin_reader_14_bold},
      lfr14I_{&librefranklin_reader_14_italic}, lfr14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lfReader14_{&lfr14R_, &lfr14B_, &lfr14I_, &lfr14BI_};
  EpdFont lfr18R_{&librefranklin_reader_18_regular}, lfr18B_{&librefranklin_reader_18_bold},
      lfr18I_{&librefranklin_reader_18_italic}, lfr18BI_{&librefranklin_reader_18_bolditalic};
  EpdFontFamily lfReader18_{&lfr18R_, &lfr18B_, &lfr18I_, &lfr18BI_};
  EpdFont ui10R_{&librefranklin_10_regular}, ui10B_{&librefranklin_10_bold};
  EpdFontFamily ui10_{&ui10R_, &ui10B_};
  EpdFont ui12R_{&librefranklin_12_regular}, ui12B_{&librefranklin_12_bold};
  EpdFontFamily ui12_{&ui12R_, &ui12B_};
  EpdFont small8_{&librefranklin_8_regular};
  EpdFontFamily small_{&small8_};
  EpdFont sm12R_{&ibmplexmono_12_regular}, sm12B_{&ibmplexmono_12_bold};
  EpdFontFamily ibmplexmono12_{&sm12R_, &sm12B_};
};

// Fixture: a zeroed counter per test. The renderer is portrait-only now —
// GfxRenderer::orientation is a compile-time constant, so there is nothing to
// set and no other orientation to escape into.
class RendererBounds : public ::testing::Test {
 protected:
  void SetUp() override {
    r = &Gfx::instance().renderer();
    r->clearScreen();
    GfxRenderer::resetOutOfRange();
  }

  // Failure message carries the offending coordinates, so a red test reads
  // like the device log rather than "expected 0, got 7".
  ::testing::AssertionResult noEscapes() const {
    const auto& o = GfxRenderer::outOfRange;
    if (o.count == 0) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << o.count << " out-of-range pixel(s); last (" << o.lastX << ", " << o.lastY
                                         << ") -> (" << o.lastPhyX << ", " << o.lastPhyY << ")";
  }

  GfxRenderer* r = nullptr;
};

// Sanity: the geometry the analysis above depends on. If the stub panel ever
// stops being the X3, every coordinate in this file needs revisiting.
TEST_F(RendererBounds, PortraitGeometryMatchesX3) {
  EXPECT_EQ(r->getDisplayWidth(), 792);
  EXPECT_EQ(r->getDisplayHeight(), 528);
  EXPECT_EQ(r->getScreenWidth(), 528);   // logical portrait width
  EXPECT_EQ(r->getScreenHeight(), 792);  // logical portrait height
}

// ---------------------------------------------------------------------------
// The exact device signature.
// ---------------------------------------------------------------------------

// Control: proves the counter actually observes the fault. A full-width rule
// drawn with the screen WIDTH used as an inclusive endpoint escapes by exactly
// one pixel, and reproduces the crash report's coordinates verbatim.
//
// drawLine's horizontal fast path is `for (x = x1; x <= x2; x++)` — inclusive —
// so the last valid endpoint is width - 1. This is the trap the font picker
// fell into (FontSelectionActivity::render drew its list separator to
// pageWidth). drawRect avoids it correctly with `x + width - 1`.
TEST_F(RendererBounds, WidthAsInclusiveEndpointEscapesByExactlyOnePixel) {
  r->drawLine(0, 302, r->getScreenWidth(), 302);

  const auto& o = GfxRenderer::outOfRange;
  ASSERT_EQ(o.count, 1u) << "expected exactly one escaped pixel";
  EXPECT_EQ(o.lastX, 528);
  EXPECT_EQ(o.lastY, 302);
  EXPECT_EQ(o.lastPhyX, 302);
  EXPECT_EQ(o.lastPhyY, -1);  // the device's "-> (302, -1)"
}

// The corrected idiom. This is the assertion that goes red if the
// FontSelectionActivity separator fix is reverted in spirit.
TEST_F(RendererBounds, FullWidthRuleAtLastValidColumnIsClean) {
  r->drawLine(0, 302, r->getScreenWidth() - 1, 302);
  EXPECT_TRUE(noEscapes());
}

// Every row and column of the screen edge. This used to sweep two
// orientations; the renderer is portrait-only now, so the loop would have run
// the same case twice. Catches an off-by-one that only shows up on one edge.
TEST_F(RendererBounds, ScreenEdgeRulesAreClean) {
  {
    GfxRenderer::resetOutOfRange();
    const int w = r->getScreenWidth();
    const int h = r->getScreenHeight();
    r->drawLine(0, 0, w - 1, 0);          // top
    r->drawLine(0, h - 1, w - 1, h - 1);  // bottom
    r->drawLine(0, 0, 0, h - 1);          // left
    r->drawLine(w - 1, 0, w - 1, h - 1);  // right
    r->drawRect(0, 0, w, h);              // full-screen border
    EXPECT_TRUE(noEscapes());
  }
}

// B-011: the lineWidth overload of drawRect treated width/height as the last
// coordinate rather than an extent, so its border landed one pixel right of and
// below the rect it was handed -- while the 5-argument overload beside it, and
// its own "Border is inside the rectangle" comment, said otherwise. Neither
// shipped caller sat at a screen edge, so the symptom was a border 1px too
// large rather than lost pixels; drawn flush right or bottom it escapes.
TEST_F(RendererBounds, FullScreenRectWithLineWidthIsClean) {
  r->drawRect(0, 0, r->getScreenWidth(), r->getScreenHeight(), 2, true);
  EXPECT_TRUE(noEscapes());
}

// Both overloads must agree on what (x, width) means -- that is the actual
// defect, and a clean full-screen draw alone would also pass if someone
// "fixed" it by clamping instead. Drawn flush to the right and bottom edges,
// the 5-argument form is known clean (ScreenEdgeRulesAreClean above), so the
// lineWidth form must be clean at the identical geometry.
TEST_F(RendererBounds, LineWidthOverloadMatchesPlainOverloadAtTheEdge) {
  const int w = r->getScreenWidth();
  const int h = r->getScreenHeight();

  r->drawRect(w - 10, h - 10, 10, 10, true);  // plain: the reference
  ASSERT_TRUE(noEscapes()) << "reference overload escaped; the test is wrong, not the code";

  GfxRenderer::resetOutOfRange();
  r->drawRect(w - 10, h - 10, 10, 10, 1, true);  // lineWidth: same rect
  EXPECT_TRUE(noEscapes()) << "lineWidth overload treats width/height as the last coordinate";
}

// ---------------------------------------------------------------------------
// Text drawing at hostile geometry.
// ---------------------------------------------------------------------------

// The preview label the crash frame was building, verbatim from the stack dump.
constexpr const char* kCrashLabel = "Preview \"Almendra\" — Medium (14pt)";

// A pane shorter than one line of the font it is asked to render. The picker
// computes labelY = top + height - padding - labelH, which goes negative for a
// short enough pane; the label must still not escape sideways or upward when
// the caller keeps it on-screen.
TEST_F(RendererBounds, ShortPaneLabelStaysInBounds) {
  const int labelH = r->getTextHeight(UI_10_FONT_ID);
  ASSERT_GT(labelH, 0);

  for (int paneH = 0; paneH <= labelH + 4; ++paneH) {
    GfxRenderer::resetOutOfRange();
    const int top = 100;
    const int labelY = top + paneH - 12 - labelH;
    if (labelY < 0) continue;  // caller's job to skip; not a renderer fault
    r->drawText(UI_10_FONT_ID, 12, labelY, kCrashLabel);
    EXPECT_TRUE(noEscapes()) << "paneH=" << paneH << " labelY=" << labelY;
  }
}

// A font large enough that the label origin computes negative. Drawing it at a
// clamped, on-screen origin must be clean at every size the picker can select.
// Note drawText takes no width and never clips: a long enough string always
// runs off the right edge. So the label is fitted to the pane first, exactly
// as renderPreviewPane does — the invariant is about FITTED text, not about
// drawText refusing to walk off-screen on its own.
TEST_F(RendererBounds, LargeFontLabelAtClampedOriginIsClean) {
  const int padding = 12;
  const int width = r->getScreenWidth() - padding * 2;
  for (const int fontId : {UI_10_FONT_ID, UI_12_FONT_ID, LIBREFRANKLIN_READER_12_FONT_ID,
                           LIBREFRANKLIN_READER_14_FONT_ID, LIBREFRANKLIN_READER_18_FONT_ID}) {
    const int h = r->getTextHeight(fontId);
    ASSERT_GT(h, 0) << "fontId " << fontId;
    (void)h;
    const std::string fitted = r->truncatedText(fontId, kCrashLabel, width);
    GfxRenderer::resetOutOfRange();
    r->drawText(fontId, padding, 0, fitted.c_str());  // flush to top edge
    EXPECT_TRUE(noEscapes()) << "fontId " << fontId;
  }
}

// KNOWN DEFECT, pinned deliberately.
//
// The idiomatic way to sit text on the bottom edge is
// `y = screenHeight - getTextHeight(fontId)`. That still paints BELOW the
// panel: getTextHeight() under-reports the real ink extent, so descenders
// (and the parentheses in "(14pt)") overshoot by a few pixels. Measured here
// at up to y = 796 against a 792-high logical screen.
//
// Same family as the crash this suite was written for — a draw escaping the
// framebuffer because a metric under-reports — but on the bottom edge and via
// text metrics rather than an inclusive-endpoint off-by-one. drawPixel clips
// it, so on device it is silent except for "!! Outside range" spam.
//
// Asserted as >0 so the defect is recorded rather than forgotten. When the
// metrics are fixed, this test goes red: flip it to noEscapes() and delete
// this comment.
TEST_F(RendererBounds, BottomFlushTextEscapesBelowPanel_KnownDefect) {
  const int padding = 12;
  const int width = r->getScreenWidth() - padding * 2;
  const int fontId = LIBREFRANKLIN_READER_18_FONT_ID;
  const int h = r->getTextHeight(fontId);
  const std::string fitted = r->truncatedText(fontId, kCrashLabel, width);

  GfxRenderer::resetOutOfRange();
  r->drawText(fontId, padding, r->getScreenHeight() - h, fitted.c_str());

  const auto& o = GfxRenderer::outOfRange;
  EXPECT_GT(o.count, 0u) << "bottom-flush overshoot appears to be fixed — flip this assertion";
  EXPECT_GE(o.lastY, r->getScreenHeight()) << "overshoot should be past the BOTTOM edge";
}

// The real-world case: Almendra is missing "¿ ¡ ñ « » ¾ £ …" and the device
// logged "8 glyph(s) not found" immediately before the escape. A glyph the
// font does not have must not yield degenerate metrics that walk the pen off
// the screen. The builtin fonts stand in for Almendra here — every string
// below contains codepoints they do not carry (CJK, emoji, and the Spanish
// punctuation from the specimen).
TEST_F(RendererBounds, MissingGlyphsDoNotWalkThePenOffScreen) {
  const char* const specimens[] = {
      "¿Qué tal? ¡Vaya! niño «cita» ¾ £100 …",  // the Almendra specimen set
      "\xE4\xB8\x80\xE4\xB8\x81\xE4\xB8\x82",   // CJK: certainly absent
      "\xF0\x9F\x98\x80\xF0\x9F\x8E\x89",       // emoji: certainly absent
      "a\xE4\xB8\x80z",                         // absent glyph between present ones
      "\xE4\xB8\x80",                           // absent glyph alone
  };

  const int padding = 12;
  const int width = r->getScreenWidth() - padding * 2;

  for (const char* s : specimens) {
    for (const int fontId : {UI_10_FONT_ID, LIBREFRANKLIN_READER_14_FONT_ID, LIBREFRANKLIN_READER_18_FONT_ID}) {
      for (const auto style :
           {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
        // Fit in the SAME style it is drawn in — see the style-mismatch test.
        const std::string fitted = r->truncatedText(fontId, s, width, style);
        GfxRenderer::resetOutOfRange();
        r->drawText(fontId, padding, 300, fitted.c_str(), /*black=*/true, style);
        EXPECT_TRUE(noEscapes()) << "font " << fontId << " style " << static_cast<int>(style) << " text " << s;
      }
    }
  }
}

// Text fitted to a pane must stay inside it. truncatedText/wrappedText measure
// by ADVANCE, while a glyph's ink can overhang its advance (notably italic), so
// a string reported as fitting can still paint past the right edge.
TEST_F(RendererBounds, FittedTextStaysInsideItsPane) {
  const int padding = 12;
  const int left = padding;
  const int width = r->getScreenWidth() - padding * 2;
  ASSERT_GT(width, 0);

  const char* const texts[] = {
      kCrashLabel,
      "¿Qué tal? ¡Vaya! niño «cita» ¾ £100 …",
      "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM",
      "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii",
  };

  for (const char* t : texts) {
    for (const int fontId : {UI_10_FONT_ID, LIBREFRANKLIN_READER_14_FONT_ID, LIBREFRANKLIN_READER_18_FONT_ID}) {
      for (const auto style :
           {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
        GfxRenderer::resetOutOfRange();
        const std::string fitted = r->truncatedText(fontId, t, width, style);
        r->drawText(fontId, left, 300, fitted.c_str(), /*black=*/true, style);
        EXPECT_TRUE(noEscapes()) << "truncated font " << fontId << " style " << static_cast<int>(style);

        GfxRenderer::resetOutOfRange();
        int y = 100;
        // wrappedText's style argument defaults to REGULAR; it must be passed
        // whenever the lines are drawn in another style.
        for (const auto& line : r->wrappedText(fontId, t, width, 4, style)) {
          r->drawText(fontId, left, y, line.c_str(), /*black=*/true, style);
          y += r->getTextHeight(fontId) + 2;
        }
        EXPECT_TRUE(noEscapes()) << "wrapped font " << fontId << " style " << static_cast<int>(style);
      }
    }
  }
}

// Degenerate panes. The picker early-returns on width/height <= 0, but the
// measuring helpers are called with whatever the theme metrics produce, and
// must not divide by zero, loop forever, or paint anything.
TEST_F(RendererBounds, DegeneratePanesDrawNothingAndDoNotCrash) {
  for (const int width : {0, -1, -1000}) {
    GfxRenderer::resetOutOfRange();
    const std::string fitted = r->truncatedText(LIBREFRANKLIN_READER_14_FONT_ID, kCrashLabel, width);
    r->drawText(LIBREFRANKLIN_READER_14_FONT_ID, 12, 300, fitted.c_str());
    EXPECT_TRUE(noEscapes()) << "width " << width;

    GfxRenderer::resetOutOfRange();
    const auto lines = r->wrappedText(LIBREFRANKLIN_READER_14_FONT_ID, kCrashLabel, width, 4);
    int y = 300;
    for (const auto& line : lines) {
      r->drawText(LIBREFRANKLIN_READER_14_FONT_ID, 12, y, line.c_str());
      y += r->getTextHeight(LIBREFRANKLIN_READER_14_FONT_ID) + 2;
    }
    EXPECT_TRUE(noEscapes()) << "wrapped width " << width;
  }

  // Zero/negative line counts and empty strings.
  GfxRenderer::resetOutOfRange();
  EXPECT_TRUE(r->wrappedText(LIBREFRANKLIN_READER_14_FONT_ID, kCrashLabel, 200, 0).empty());
  r->drawText(LIBREFRANKLIN_READER_14_FONT_ID, 12, 300, "");
  r->drawText(LIBREFRANKLIN_READER_14_FONT_ID, 12, 300, nullptr);
  EXPECT_TRUE(noEscapes());
}

// B-009. A codepoint no face can draw must still occupy space.
//
// Space Mono carries neither U+1F60A nor U+FFFD, and it is what the Claude
// answer and the note editor are painted in — SETTINGS.editorFont defaults to
// the card-only iA Writer row, so resolveEditorFont falls through to the
// built-in mono. Before the fallback chain, getGlyph returned nullptr,
// renderCharImpl drew nothing, and drawText advanced the cursor by ZERO
// (`prevAdvanceFP = glyph ? glyph->advanceX : 0`). The character did not just
// vanish: the rest of the line slid left into its place, so a width measured
// with the emoji present no longer matched what was drawn.
TEST(MissingGlyph, ResolvesToAVisibleFallbackRatherThanNothing) {
  EpdFont mono{&ibmplexmono_12_regular};
  // hasCodepoint is the raw coverage question — no substitution — so it still
  // reports the truth about this face after the fallback chain exists.
  ASSERT_FALSE(mono.hasCodepoint(REPLACEMENT_GLYPH))
      << "this face is supposed to lack U+FFFD; if it gained one, this test no longer exercises the fallback";
  ASSERT_FALSE(mono.hasCodepoint(0x1F60A));

  const EpdGlyph* emoji = mono.getGlyph(0x1F60A);  // the smiley the API sent
  ASSERT_NE(emoji, nullptr) << "a codepoint with neither a glyph nor U+FFFD must still resolve to something";
  EXPECT_EQ(emoji, mono.getGlyph('?')) << "the documented last resort is '?'";
  EXPECT_GT(emoji->advanceX, 0) << "a fallback with no advance is the metrics half of the bug";
}

// The same property one layer up: a string must not measure NARROWER because a
// character in it is unrepresentable. That is what silently corrupted wrapping.
TEST_F(RendererBounds, TextWidthCountsUnrepresentableCharacters) {
  const int without = r->getTextWidth(IBMPLEXMONO_12_FONT_ID, "ab");
  const int with = r->getTextWidth(IBMPLEXMONO_12_FONT_ID,
                                   "a\xF0\x9F\x98\x8A"
                                   "b");  // a + U+1F60A + b
  EXPECT_GT(with, without) << "the unrepresentable character contributed zero width";
}

// A zero-height and a negative-height pane, driven through the same geometry
// the picker uses, must not place the label off the top of the screen.
TEST_F(RendererBounds, ZeroAndNegativeHeightPanesProduceNoDraw) {
  const int labelH = r->getTextHeight(UI_10_FONT_ID);
  for (const int height : {0, -1, -500}) {
    GfxRenderer::resetOutOfRange();
    const int top = 100;
    const int labelY = top + height - 12 - labelH;
    if (labelY >= 0) {
      r->drawText(UI_10_FONT_ID, 12, labelY, kCrashLabel);
    }
    EXPECT_TRUE(noEscapes()) << "height " << height;
  }
}

}  // namespace

// ── B-025: a trailing space is under-counted ────────────────────────────────
//
// NoteEditorActivity measures the caret from the source text before the cursor.
// The instant the typist presses space that text ends in one -- and the raw
// measurement gives a trailing space almost none of its advance, so the caret
// crawled a pixel instead of a space-width and read as stuck.
//
// Measured with LibreFranklin 12: "ab" = 28, "ab " = 29, "ab  " = 34. An
// interior space is 5px; a trailing one counts 1. advanceOf()'s sentinel
// recovers the full advance, which is why the caret must go through it.

namespace {
int sentinelWidth(GfxRenderer& r, int fontId, const char* s) {
  char probe[200];
  snprintf(probe, sizeof(probe), "%s|", s);
  return r.getTextWidth(fontId, probe) - r.getTextWidth(fontId, "|");
}
}  // namespace

TEST_F(RendererBounds, RawMeasurementUnderCountsATrailingSpace) {
  const int fontId = LIBREFRANKLIN_READER_12_FONT_ID;
  ASSERT_GT(r->getTextWidth(fontId, "ab"), 0) << "font not loaded; this would pass on zeros";
  const int rawDelta = r->getTextWidth(fontId, "ab ") - r->getTextWidth(fontId, "ab");
  const int trueDelta = sentinelWidth(*r, fontId, "ab ") - sentinelWidth(*r, fontId, "ab");
  EXPECT_GT(trueDelta, 0) << "a space must have some advance";
  EXPECT_LT(rawDelta, trueDelta) << "if these now agree, getTextWidth counts trailing spaces "
                                    "fully and NoteEditorActivity::advanceOf can be retired";
}

TEST_F(RendererBounds, SentinelMeasurementRecoversTheFullSpaceAdvance) {
  const int fontId = LIBREFRANKLIN_READER_12_FONT_ID;
  ASSERT_GT(sentinelWidth(*r, fontId, "ab"), 0) << "font not loaded; this would pass on zeros";
  // An interior space and a trailing space must advance by the same amount --
  // that equality is what makes the caret land where the glyph will go.
  const int interior = r->getTextWidth(fontId, "a b") - r->getTextWidth(fontId, "ab");
  const int trailing = sentinelWidth(*r, fontId, "ab ") - sentinelWidth(*r, fontId, "ab");
  EXPECT_EQ(trailing, interior) << "the caret must advance by a full space when one is typed";
}
