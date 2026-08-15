// Dark mode's two halves, both of which are invisible until a panel shows them:
//
//   PanelPolarity   — where the framebuffer flip happens and, more importantly,
//                     when it is NOT undone (across an async refresh).
//   GlyphAa::planes — which glyph coverage level the BW base pass paints and
//                     which grayscale plane each antialiased level flags, in
//                     both output polarities.
//
// The invariants asserted here are the ones the panel waveform imposes; see
// lib/GfxRenderer/GlyphAaPlanes.h for the LUT citations behind them.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "GlyphAaPlanes.h"
#include "PanelPolarity.h"

namespace {

constexpr uint8_t L0 = 1u << 0;  // full ink
constexpr uint8_t L1 = 1u << 1;  // high coverage
constexpr uint8_t L2 = 1u << 2;  // low coverage
constexpr uint8_t L3 = 1u << 3;  // no ink

std::vector<uint8_t> pattern() { return {0x00, 0xFF, 0xA5, 0x0F, 0x5A, 0xF0}; }

const GlyphAa::Strength kStrengths[] = {GlyphAa::Standard, GlyphAa::Crisp, GlyphAa::Dark};

}  // namespace

// ---------------------------------------------------------------------------
// PanelPolarity
// ---------------------------------------------------------------------------

TEST(PanelPolarity, LightModeNeverTouchesTheBuffer) {
  PanelPolarity p;
  auto buf = pattern();
  const auto original = buf;

  p.toPanel(buf.data(), buf.size());
  EXPECT_EQ(buf, original);
  EXPECT_FALSE(p.framebufferIsPanelPolarity());

  p.toLogical(buf.data(), buf.size());
  EXPECT_EQ(buf, original);
}

TEST(PanelPolarity, DarkModeFlipsToPanelAndBackAgain) {
  PanelPolarity p;
  ASSERT_TRUE(p.setDarkMode(true));
  auto buf = pattern();
  const auto original = buf;

  p.toPanel(buf.data(), buf.size());
  ASSERT_TRUE(p.framebufferIsPanelPolarity());
  for (size_t i = 0; i < buf.size(); ++i) {
    EXPECT_EQ(buf[i], static_cast<uint8_t>(~original[i])) << "byte " << i;
  }

  p.toLogical(buf.data(), buf.size());
  EXPECT_FALSE(p.framebufferIsPanelPolarity());
  EXPECT_EQ(buf, original);
}

// The reason toPanel/toLogical are idempotent rather than plain flips: every
// SDK entry point calls toPanel, and several of them nest (displayGrayBuffer
// drains a pending async refresh that already flipped the buffer).
TEST(PanelPolarity, RepeatedTransformsAreIdempotent) {
  PanelPolarity p;
  p.setDarkMode(true);
  auto buf = pattern();
  const auto original = buf;

  p.toPanel(buf.data(), buf.size());
  p.toPanel(buf.data(), buf.size());
  p.toPanel(buf.data(), buf.size());
  for (size_t i = 0; i < buf.size(); ++i) EXPECT_EQ(buf[i], static_cast<uint8_t>(~original[i]));

  p.toLogical(buf.data(), buf.size());
  p.toLogical(buf.data(), buf.size());
  EXPECT_EQ(buf, original);
}

// Turning dark mode off while a refresh is in flight must still restore the
// buffer the caller drew, so toLogical is gated on "is it flipped", not on
// "is dark mode on".
TEST(PanelPolarity, LeavingDarkModeMidAsyncStillRestores) {
  PanelPolarity p;
  p.setDarkMode(true);
  auto buf = pattern();
  const auto original = buf;

  p.toPanel(buf.data(), buf.size());  // async refresh starts, buffer left flipped
  ASSERT_TRUE(p.setDarkMode(false));
  p.toLogical(buf.data(), buf.size());  // waitRefreshComplete()

  EXPECT_EQ(buf, original);
  EXPECT_FALSE(p.framebufferIsPanelPolarity());
}

TEST(PanelPolarity, RefreshPromotionIsOneShotAndOnlyOnChange) {
  PanelPolarity p;
  EXPECT_FALSE(p.consumeRefreshPromotion()) << "nothing has changed yet";

  ASSERT_TRUE(p.setDarkMode(true));
  EXPECT_TRUE(p.consumeRefreshPromotion());
  EXPECT_FALSE(p.consumeRefreshPromotion()) << "one refresh, not every refresh";

  EXPECT_FALSE(p.setDarkMode(true)) << "setting the same mode is not a change";
  EXPECT_FALSE(p.consumeRefreshPromotion());
}

// ---------------------------------------------------------------------------
// GlyphAa::planes — invariants the waveform imposes
// ---------------------------------------------------------------------------

// The white->black cell of the OEM gray bank is passive (Uc8253X3Luts.h:117-120),
// and LSB-only is exactly that cell. Emitting it would ask for a drive that does
// not exist.
TEST(GlyphAaPlanes, LsbIsAlwaysASubsetOfMsb) {
  for (const auto s : kStrengths) {
    for (const bool dark : {false, true}) {
      const auto p = GlyphAa::planes(s, dark);
      EXPECT_EQ(p.lsb & static_cast<uint8_t>(~p.msb), 0) << "strength " << s << " dark " << dark;
    }
  }
}

// The overlay can only lift a BLACK pixel. A level painted by the base pass is
// not black on the panel in the polarity that matters, so flagging it as well
// would be a request the panel cannot serve.
TEST(GlyphAaPlanes, DarkModeNeverPaintsAndFlagsTheSameLevel) {
  for (const auto s : kStrengths) {
    const auto p = GlyphAa::planes(s, /*darkModeAa=*/true);
    EXPECT_EQ(p.baseInk & p.msb, 0) << "strength " << s;
    EXPECT_EQ(p.baseInk & p.lsb, 0) << "strength " << s;
  }
}

TEST(GlyphAaPlanes, NoLevelIsEverLeftUnaccountedFor) {
  for (const auto s : kStrengths) {
    for (const bool dark : {false, true}) {
      const auto p = GlyphAa::planes(s, dark);
      const uint8_t handled = p.baseInk | p.msb | p.lsb;
      EXPECT_EQ(handled & L0, L0) << "solid ink must always be drawn";
      EXPECT_EQ(handled & L3, 0) << "the page level must never be drawn or flagged";
    }
  }
}

// Light mode is the shipped behaviour and must be bit-for-bit unchanged: the BW
// base pass paints every non-white level, exactly as `bmpVal < 3` did.
TEST(GlyphAaPlanes, LightModeMatchesTheShippedMasks) {
  const auto standard = GlyphAa::planes(GlyphAa::Standard, false);
  EXPECT_EQ(standard.baseInk, L0 | L1 | L2);
  EXPECT_EQ(standard.msb, L1 | L2);
  EXPECT_EQ(standard.lsb, L1);

  const auto crisp = GlyphAa::planes(GlyphAa::Crisp, false);
  EXPECT_EQ(crisp.baseInk, L0 | L1 | L2);
  EXPECT_EQ(crisp.msb, L2);
  EXPECT_EQ(crisp.lsb, 0);

  const auto dark = GlyphAa::planes(GlyphAa::Dark, false);
  EXPECT_EQ(dark.baseInk, L0 | L1 | L2);
  EXPECT_EQ(dark.msb, L2);
  EXPECT_EQ(dark.lsb, L2);
}

// Dark mode with no overlay coming falls back to the light-mode base pass, or
// glyphs would render with their antialiased levels missing and nothing to fill
// them in. This is why the renderer flag is "dark mode AND AA", not isInverted().
TEST(GlyphAaPlanes, DarkModeWithoutAaPaintsSolidGlyphs) {
  for (const auto s : kStrengths) {
    EXPECT_EQ(GlyphAa::planes(s, /*darkModeAa=*/false).baseInk, L0 | L1 | L2);
  }
}

// Standard is the interesting one: both antialiased levels stay off the base
// pass so the background's black can be lifted, and the two gray targets swap
// (MSB-only is the lighter lift, so it now goes to the level nearest the ink).
TEST(GlyphAaPlanes, DarkModeStandardSwapsTheTwoGrayTargets) {
  const auto light = GlyphAa::planes(GlyphAa::Standard, false);
  const auto dark = GlyphAa::planes(GlyphAa::Standard, true);

  EXPECT_EQ(dark.baseInk, L0);
  EXPECT_EQ(dark.msb, light.msb) << "both levels are still lifted";
  EXPECT_EQ(light.lsb, L1) << "light mode: the ink-side level takes the darker gray";
  EXPECT_EQ(dark.lsb, L2) << "dark mode: the page-side level takes the darker gray";
}

TEST(GlyphAaPlanes, DarkModeCrispAndDarkHardenTheInkSideLevel) {
  const auto crisp = GlyphAa::planes(GlyphAa::Crisp, true);
  EXPECT_EQ(crisp.baseInk, L0 | L1);
  EXPECT_EQ(crisp.msb, L2);
  EXPECT_EQ(crisp.lsb, L2) << "crisp gives the page-side level the lighter ink weight";

  const auto dark = GlyphAa::planes(GlyphAa::Dark, true);
  EXPECT_EQ(dark.baseInk, L0 | L1);
  EXPECT_EQ(dark.msb, L2);
  EXPECT_EQ(dark.lsb, 0) << "dark gives the page-side level the heavier ink weight";
}

// Crisp and Dark differ in dark mode exactly as they do in light mode: same
// levels involved, opposite gray target. A regression that collapsed the two
// strengths onto one mapping would pass every other test here.
TEST(GlyphAaPlanes, CrispAndDarkStayDistinctInBothPolarities) {
  for (const bool dark : {false, true}) {
    const auto c = GlyphAa::planes(GlyphAa::Crisp, dark);
    const auto d = GlyphAa::planes(GlyphAa::Dark, dark);
    EXPECT_EQ(c.baseInk, d.baseInk);
    EXPECT_EQ(c.msb, d.msb);
    EXPECT_NE(c.lsb, d.lsb) << "dark " << dark;
  }
}
