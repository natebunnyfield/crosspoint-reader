// The per-glyph fast path (GfxRenderer::drawGlyphBitmap) against the per-pixel
// loops it replaces, through the REAL renderer: same text, same fonts, every
// render mode, antialiasing strength, polarity, rotation, strip band and a set
// of positions that includes glyphs half off the panel. The frame (or strip
// scratch) must be byte-identical, the out-of-range count identical, and the
// dirty rect must contain the per-pixel one. It is not always EQUAL: the fast
// path marks the glyph's whole cell, the loops marked only the pixels they
// painted, and some Reader 14 bitmaps carry a blank last row or column, so the
// rect can be one pixel wider at the text's extreme edge (measured 2026-11-08:
// equal in 7011 of 7812 light-mode BW cases). Over-reporting is the safe side
// (GfxRenderer.h, dirty rect).
//
// One binary renders both ways: GFX_GLYPH_FASTPATH_TOGGLE (compiled only here)
// gives GfxRenderer a switch that sends every glyph down the old loops.
//
// Built twice (CMakeLists.txt): at RENDER_SCALE 1, the device's case, and at 2
// with hi-res companion faces, where the companions take the deviceSpace fast
// path and the 1x faces must be declined (drawPixel paints 2x2 blocks there).
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/librefranklin_10_bold.h>
#include <builtinFonts/librefranklin_10_regular.h>
#include <builtinFonts/librefranklin_reader_14_bold.h>
#include <builtinFonts/librefranklin_reader_14_bolditalic.h>
#include <builtinFonts/librefranklin_reader_14_italic.h>
#include <builtinFonts/librefranklin_reader_14_regular.h>
#if CROSSPOINT_RENDER_SCALE > 1
#include <builtinFonts/librefranklin_10_bold_2x.h>
#include <builtinFonts/librefranklin_10_regular_2x.h>
#include <builtinFonts/librefranklin_reader_14_bold_2x.h>
#include <builtinFonts/librefranklin_reader_14_regular_2x.h>
#endif
#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

HalDisplay display;

namespace {

constexpr int READER_14 = 9301;  // compressed 2-bit, the reader's own face
constexpr int CHROME_10 = 9302;  // uncompressed 2-bit, the UI chrome face
constexpr int ONE_BIT = 9303;    // synthetic 1-bit face (no built-in is 1-bit any more)

// A 1-bit face: four glyphs of awkward widths filled with a fixed pseudo-random
// bit pattern, so the bitstream crosses byte boundaries mid-row.
struct OneBitFace {
  static constexpr int N = 4;
  uint8_t bits[512] = {};
  EpdGlyph glyphs[N] = {};
  EpdUnicodeInterval interval{'A', 'A' + N - 1, 0};
  EpdFontData data{};
  OneBitFace() {
    for (size_t i = 0; i < sizeof bits; ++i) bits[i] = static_cast<uint8_t>(i * 151 + 0x3d);
    const uint8_t widths[N] = {1, 5, 9, 13};
    const uint8_t heights[N] = {11, 7, 14, 9};
    uint32_t offset = 0;
    for (int i = 0; i < N; ++i) {
      const uint16_t length = static_cast<uint16_t>((widths[i] * heights[i] + 7) / 8);
      glyphs[i] = {widths[i], heights[i], static_cast<uint16_t>((widths[i] + 2) << 4), 1, 10, length, offset};
      offset += length;
    }
    data.bitmap = bits;
    data.glyph = glyphs;
    data.intervals = &interval;
    data.intervalCount = 1;
    data.advanceY = 16;
    data.ascender = 12;
    data.descender = -4;
    data.is2Bit = false;
  }
};

class Gfx {
 public:
  static Gfx& instance() {
    static Gfx g;
    return g;
  }
  GfxRenderer& renderer() { return renderer_; }

 private:
  Gfx()
      : renderer_(display),
        cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()
#if CROSSPOINT_RENDER_SCALE > 1
                                           ,
               renderer_.getHiResSdCardFonts()
#endif
        ) {
    renderer_.begin();
    if (!decompressor_.init()) ADD_FAILURE() << "font decompressor init failed";
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(READER_14, reader14_);
    renderer_.insertFont(CHROME_10, chrome10_);
    renderer_.insertFont(ONE_BIT, oneBit_);
#if CROSSPOINT_RENDER_SCALE > 1
    renderer_.registerHiResBuiltinFont(READER_14, reader14HiRes_);
    renderer_.registerHiResBuiltinFont(CHROME_10, chrome10HiRes_);
#endif
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont r14R_{&librefranklin_reader_14_regular}, r14B_{&librefranklin_reader_14_bold},
      r14I_{&librefranklin_reader_14_italic}, r14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily reader14_{&r14R_, &r14B_, &r14I_, &r14BI_};
  EpdFont c10R_{&librefranklin_10_regular}, c10B_{&librefranklin_10_bold};
  EpdFontFamily chrome10_{&c10R_, &c10B_, &c10R_, &c10B_};
  OneBitFace oneBitFace_;
  EpdFont oneBitFont_{&oneBitFace_.data};
  EpdFontFamily oneBit_{&oneBitFont_, &oneBitFont_, &oneBitFont_, &oneBitFont_};
#if CROSSPOINT_RENDER_SCALE > 1
  EpdFont r14R2_{&librefranklin_reader_14_regular_2x}, r14B2_{&librefranklin_reader_14_bold_2x};
  EpdFontFamily reader14HiRes_{&r14R2_, &r14B2_, &r14R2_, &r14B2_};
  EpdFont c10R2_{&librefranklin_10_regular_2x}, c10B2_{&librefranklin_10_bold_2x};
  EpdFontFamily chrome10HiRes_{&c10R2_, &c10B2_, &c10R2_, &c10B2_};
#endif
};

GfxRenderer& R() { return Gfx::instance().renderer(); }

struct Snapshot {
  std::vector<uint8_t> frame;
  std::vector<uint8_t> strip;
  GfxRenderer::DirtyRectForTest dirty{};
  uint32_t outOfRange = 0;
};

// Renders `draw` onto a fixed noise background (so a wrongly SET bit and a
// wrongly CLEARED bit are both visible), optionally into a strip band.
// `s` is reused across cases so its buffers are allocated once, not twice per
// case (the page faults of a fresh 52-209 KB vector dominated the run time).
void render(Snapshot& s, const bool fast, const int stripY0, const int stripRows, const std::function<void()>& draw) {
  GfxRenderer& r = R();
  GfxRenderer::glyphFastPathEnabled = fast;
  uint8_t* fb = r.getFrameBuffer();
  const size_t fbBytes = static_cast<size_t>(r.getDisplayWidthBytes()) * r.getDisplayHeight();
  for (size_t i = 0; i < fbBytes; ++i) fb[i] = static_cast<uint8_t>(i * 197 + 0x5a);
  std::vector<uint8_t>& scratch = s.strip;
  scratch.clear();
  if (stripRows > 0) {
    scratch.resize(static_cast<size_t>(r.getDisplayWidthBytes()) * stripRows);
    for (size_t i = 0; i < scratch.size(); ++i) scratch[i] = static_cast<uint8_t>(i * 89 + 0xc3);
    r.beginStripTarget(scratch.data(), stripY0, stripRows);
  }
  r.resetDirtyForTest();
  GfxRenderer::resetOutOfRange();
  draw();
  if (stripRows > 0) r.endStripTarget();
  s.frame.assign(fb, fb + fbBytes);
  s.dirty = r.dirtyRectForTest();
  s.outOfRange = GfxRenderer::outOfRangeCount();
  GfxRenderer::glyphFastPathEnabled = true;
}

bool contains(const GfxRenderer::DirtyRectForTest& outer, const GfxRenderer::DirtyRectForTest& inner) {
  if (outer.all) return true;
  if (inner.all) return false;
  if (inner.left > inner.right || inner.top > inner.bottom) return true;  // inner is empty
  return outer.left <= inner.left && outer.right >= inner.right && outer.top <= inner.top &&
         outer.bottom >= inner.bottom;
}

bool sameRect(const GfxRenderer::DirtyRectForTest& a, const GfxRenderer::DirtyRectForTest& b) {
  return a.all == b.all && a.left == b.left && a.right == b.right && a.top == b.top && a.bottom == b.bottom;
}

enum class Draw { Upright, CW, CCW };

// Text: ligature/kern pairs, descenders, a SUP run (renderCharScaled, which
// stays per-pixel), and U+2603, which no built-in face carries -- the notdef
// box path, also per-pixel.
constexpr const char* TEXT = "Departed quickly fi ffl WAVE 0123 jgyQ \xE2\x98\x83!";
constexpr const char* ONE_BIT_TEXT = "ABCDDCBA";

// drawPixel logs every off-panel pixel at ERR, and the half-off positions below
// make that millions of lines. Both paths log the same pixels (the fast path
// declines an off-panel glyph); the out-of-range COUNT is what is compared.
class QuietStderr {
 public:
  QuietStderr() : saved_(dup(STDERR_FILENO)) {
    std::fflush(stderr);
    const int null = open("/dev/null", O_WRONLY);
    if (null >= 0) {
      dup2(null, STDERR_FILENO);
      close(null);
    }
    // stderr is unbuffered: three write(2)s per logged pixel otherwise.
    std::setvbuf(stderr, buffer_, _IOFBF, sizeof buffer_);
  }
  ~QuietStderr() {
    std::fflush(stderr);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (saved_ >= 0) {
      dup2(saved_, STDERR_FILENO);
      close(saved_);
    }
  }
  QuietStderr(const QuietStderr&) = delete;
  QuietStderr& operator=(const QuietStderr&) = delete;

 private:
  int saved_;
  char buffer_[1 << 16];
};

struct Totals {
  int cases = 0;
  int exactDirty = 0;
  // Light-mode BW, the pass whose dirty rect a windowed present can use without
  // a clearScreen before it (UI chrome); there the cell is the painted box.
  int lightBwCases = 0;
  int lightBwExactDirty = 0;
};

void runCase(Totals& t, const int font, const Draw how, const int x, const int y, const bool black,
             const EpdFontFamily::Style style, const int stripY0, const int stripRows) {
  const char* text = font == ONE_BIT ? ONE_BIT_TEXT : TEXT;
  const auto draw = [&] {
    switch (how) {
      case Draw::Upright:
        R().drawText(font, x, y, text, black, style);
        break;
      case Draw::CW:
        R().drawTextRotated90CW(font, x, y, text, black, style);
        break;
      case Draw::CCW:
        R().drawTextRotated90CCW(font, x, y, text, black, style);
        break;
    }
  };
  static Snapshot slow, fast;
  render(slow, false, stripY0, stripRows, draw);
  render(fast, true, stripY0, stripRows, draw);
  SCOPED_TRACE(::testing::Message() << "font=" << font << " draw=" << static_cast<int>(how) << " at=" << x << ',' << y
                                    << " black=" << black << " style=" << static_cast<int>(style)
                                    << " mode=" << R().getRenderMode() << " aa=" << R().getGrayscaleAaStrength()
                                    << " darkAa=" << R().getDarkModeAntiAliasing() << " strip=" << stripY0 << '+'
                                    << stripRows);
  ASSERT_TRUE(slow.frame == fast.frame) << "framebuffer differs";
  ASSERT_TRUE(slow.strip == fast.strip) << "strip scratch differs";
  ASSERT_EQ(slow.outOfRange, fast.outOfRange) << "out-of-range count differs";
  ASSERT_TRUE(contains(fast.dirty, slow.dirty))
      << "fast dirty " << fast.dirty.left << ',' << fast.dirty.top << '-' << fast.dirty.right << ','
      << fast.dirty.bottom << " misses slow " << slow.dirty.left << ',' << slow.dirty.top << '-' << slow.dirty.right
      << ',' << slow.dirty.bottom;
  t.cases++;
  const bool exact = sameRect(fast.dirty, slow.dirty);
  if (exact) t.exactDirty++;
  if (R().getRenderMode() == GfxRenderer::BW && !R().getDarkModeAntiAliasing()) {
    t.lightBwCases++;
    if (exact) t.lightBwExactDirty++;
  }
}

}  // namespace

TEST(GlyphFastPath, RendersExactlyWhatThePerPixelLoopsRender) {
  GfxRenderer& r = R();
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  const int xs[] = {-9, 0, 37, W / 2, W - 60, W - 4};
  const int ys[] = {-11, 0, 41, H / 2, H - 30, H - 5};
  const int panelRows = r.getDisplayHeight();
  const std::pair<int, int> strips[] = {{0, 0}, {0, 16}, {101, 37}, {panelRows - 8, 8}};

  Totals totals;
  GfxRenderer::glyphFastPathGlyphs = 0;
  const QuietStderr quiet;
  for (const auto mode : {GfxRenderer::BW, GfxRenderer::GRAYSCALE_LSB, GfxRenderer::GRAYSCALE_MSB}) {
    r.setRenderMode(mode);
    for (const auto aa : {GfxRenderer::AA_STANDARD, GfxRenderer::AA_CRISP, GfxRenderer::AA_DARK}) {
      r.setGrayscaleAaStrength(aa);
      for (const bool darkAa : {false, true}) {
        r.setDarkModeAntiAliasing(darkAa);
        for (const int font : {READER_14, CHROME_10, ONE_BIT}) {
          for (const Draw how : {Draw::Upright, Draw::CW, Draw::CCW}) {
            for (const bool black : {true, false}) {
              for (const auto [stripY0, stripRows] : strips) {
                for (const int x : xs) {
                  for (const int y : ys) {
                    runCase(totals, font, how, x, y, black, EpdFontFamily::REGULAR, stripY0, stripRows);
                    if (HasFatalFailure()) return;
                  }
                }
              }
            }
          }
          // Styles once per font and mode, upright and unbanded.
          for (const auto style : {EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC,
                                   static_cast<EpdFontFamily::Style>(EpdFontFamily::REGULAR | EpdFontFamily::SUP)}) {
            runCase(totals, font, Draw::Upright, 37, 41, true, style, 0, 0);
            if (HasFatalFailure()) return;
          }
        }
      }
    }
  }
  r.setRenderMode(GfxRenderer::BW);
  r.setGrayscaleAaStrength(GfxRenderer::AA_STANDARD);
  r.setDarkModeAntiAliasing(false);

  std::printf(
      "[glyph-fastpath] scale %d: %d cases byte-identical, dirty rect identical in %d (light BW: %d of %d), fast "
      "path drew %u glyphs\n",
      GfxRenderer::renderScale(), totals.cases, totals.exactDirty, totals.lightBwExactDirty, totals.lightBwCases,
      GfxRenderer::glyphFastPathGlyphs);
  // Not vacuous: the fast path must actually have run.
  EXPECT_GT(GfxRenderer::glyphFastPathGlyphs, 1000u);
}

#if CROSSPOINT_RENDER_SCALE == 1
// Wall time for the same page both ways. Host CPU, -O2: a measurement of the
// per-pixel overhead removed, NOT a device number (the C3 is a 160 MHz RISC-V
// at -Os). Printed, never asserted, so a loaded CI box cannot fail it.
TEST(GlyphFastPath, HostTimingOfAFullPageOfText) {
  GfxRenderer& r = R();
  r.setRenderMode(GfxRenderer::BW);
  const int lineHeight = r.getLineHeight(READER_14);
  const char* line = "The quick brown fox jumps over";
  ASSERT_LT(r.getTextWidth(READER_14, line) + 12, r.getScreenWidth()) << "every glyph must be on the panel";
  const auto page = [&] {
    for (int y = 0; y + lineHeight < r.getScreenHeight(); y += lineHeight) {
      r.drawText(READER_14, 12, y, line, true);
    }
  };
  GfxRenderer::resetOutOfRange();
  page();  // warm the glyph cache so neither side pays decompression first
  constexpr int N = 200;
  double ms[2] = {0, 0};
  for (int pass = 0; pass < 2; ++pass) {
    GfxRenderer::glyphFastPathEnabled = pass == 1;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < N; ++i) page();
    ms[pass] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
  GfxRenderer::glyphFastPathEnabled = true;
  EXPECT_EQ(0u, GfxRenderer::outOfRangeCount()) << "an off-panel glyph would time the fallback, not the fast path";
  std::printf("[glyph-fastpath] host, %d full pages of Reader 14: per-pixel %.1f ms, fast path %.1f ms (%.2fx)\n", N,
              ms[0], ms[1], ms[0] / ms[1]);
  SUCCEED();
}
#endif
