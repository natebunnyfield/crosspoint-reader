// CSS length units: the conversion table (lib/Epub/Epub/css/CssUnits.h) and
// what the declaration path does with a unit it cannot convert
// (lib/Epub/Epub/css/CssParser.cpp).
//
// Every failure mode here is a WRONG NUMBER on a page that renders cleanly.
// Before this landed, `margin: 1cm` was one pixel and nothing logged, nothing
// crashed, and no test failed -- the book simply had no margin. That is why
// this suite asserts against the conversion arithmetic directly (a table any
// reader can check against a ruler) and then, separately, against the parser,
// which is where the "drop it rather than guess" rule actually has to hold.
//
// The parser half compiles the real CssParser.cpp against local stubs. Nothing
// else in test/ did, which is exactly how the unit fall-through survived.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "Epub/BookNotes.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssUnits.h"

namespace {

// The X3's reading default, so the numbers below are the ones a reader sees:
// 18 pt at the converters' 150 dpi is a 37.5 px em, and the font reports an
// advanceY of 45 px, which is what BlockStyle::fromCssStyle is handed as
// `emSize`.
constexpr float kEmSize = 45.0f;
constexpr float kViewportWidth = 450.0f;

CssStyle parse(const std::string& decls) { return CssParser::parseInlineStyle(decls); }

booknotes::Notes& freshNotes() {
  auto& n = booknotes::current();
  n.resetForTest();
  return n;
}

}  // namespace

// --- The basis --------------------------------------------------------------

TEST(CssUnits, TheBasisIsTheResolutionTheTypeIsRasterizedAt) {
  // 150, not 96 and not the panel's ~257 ppi. If this ever moves, every book
  // already indexed measures differently and SECTION_FILE_VERSION owes a bump.
  // The number is lib/EpdFont/scripts/fontconvert.py's set_char_size(.., 150,
  // 150); see CssUnits.h for why the panel's own ppi is the wrong anchor.
  EXPECT_FLOAT_EQ(cssunits::kPixelsPerInch, 150.0f);
}

TEST(CssUnits, APointOfMarginIsAPointOfType) {
  // The whole argument for 150 dpi in one assertion: a 12 pt margin and 12 pt
  // type have to be the same physical size, or a book that sets both in points
  // is laid out to two different scales.
  const float ppemAt12pt = 12.0f * 150.0f / 72.0f;  // the converters' own formula
  const auto pt = cssunits::classify("pt");
  ASSERT_EQ(pt.kind, cssunits::Kind::Absolute);
  EXPECT_FLOAT_EQ(12.0f * pt.pixelsPerUnit, ppemAt12pt);
}

// --- The conversion table ---------------------------------------------------

TEST(CssUnits, AbsoluteUnitsConvertThroughTheInch) {
  struct Case {
    const char* unit;
    float perInch;  // how many of this unit make an inch
  };
  // CSS Values 4 section 6.2. Every one of these was read as PIXELS before
  // 2026-08-23: `1cm` was 1 px rather than 59.
  constexpr Case kCases[] = {
      {"in", 1.0f}, {"cm", 2.54f}, {"mm", 25.4f}, {"q", 101.6f}, {"pt", 72.0f}, {"pc", 6.0f},
  };
  for (const auto& c : kCases) {
    const auto got = cssunits::classify(c.unit);
    EXPECT_EQ(got.kind, cssunits::Kind::Absolute) << c.unit;
    EXPECT_NEAR(got.pixelsPerUnit, cssunits::kPixelsPerInch / c.perInch, 0.001f) << c.unit;
  }
}

TEST(CssUnits, TheHeadlineNumbers) {
  // The figures quoted in the writeup, so a change to the basis fails HERE
  // rather than being discovered on a page.
  EXPECT_NEAR(1.0f * cssunits::classify("cm").pixelsPerUnit, 59.055f, 0.01f);
  EXPECT_NEAR(1.0f * cssunits::classify("mm").pixelsPerUnit, 5.9055f, 0.001f);
  EXPECT_NEAR(1.0f * cssunits::classify("in").pixelsPerUnit, 150.0f, 0.01f);
  EXPECT_NEAR(1.0f * cssunits::classify("pt").pixelsPerUnit, 2.0833f, 0.001f);
  EXPECT_NEAR(1.0f * cssunits::classify("pc").pixelsPerUnit, 25.0f, 0.01f);
}

TEST(CssUnits, UnitsAreCaseInsensitiveButThePercentSignIsNot) {
  // CSS units are ASCII case-insensitive; `PT` and `Pt` are `pt`. The percent
  // sign has no case and must not be reached by the letter comparison.
  EXPECT_EQ(cssunits::classify("PT").kind, cssunits::Kind::Absolute);
  EXPECT_EQ(cssunits::classify("Cm").kind, cssunits::Kind::Absolute);
  EXPECT_EQ(cssunits::classify("EM").kind, cssunits::Kind::Em);
  EXPECT_EQ(cssunits::classify("%").kind, cssunits::Kind::Percent);
}

TEST(CssUnits, NoUnitAndPxAreBothPixels) {
  EXPECT_EQ(cssunits::classify("").kind, cssunits::Kind::Pixels);
  EXPECT_EQ(cssunits::classify("px").kind, cssunits::Kind::Pixels);
}

TEST(CssUnits, WhatCannotBeConvertedSaysSo) {
  // The point of the whole change: these must NOT come back as Pixels. `ex` and
  // `ch` need font metrics this layer is not given; the viewport units need a
  // height CssLength::toPixels never receives.
  for (const char* unit : {"ex", "ch", "vw", "vh", "vmin", "vmax", "fr", "deg", "s", "cap", "ic", "lh", "rlh"}) {
    EXPECT_EQ(cssunits::classify(unit).kind, cssunits::Kind::Unconvertible) << unit;
  }
}

TEST(CssUnits, APrefixOfAKnownUnitIsNotThatUnit) {
  // The matcher walks the literal, so a length check at the end is the only
  // thing stopping "pts" or "inch" from being taken for "pt" and "in".
  for (const char* unit : {"p", "i", "c", "e", "pts", "inch", "cms", "emu", "remx"}) {
    EXPECT_EQ(cssunits::classify(unit).kind, cssunits::Kind::Unconvertible) << unit;
  }
}

// --- The parser: absolute units reach the page ------------------------------

TEST(CssLengths, CentimetersBecomeRealPixels) {
  freshNotes();
  const CssStyle s = parse("margin-top: 1cm");
  ASSERT_TRUE(s.defined.marginTop);
  EXPECT_NEAR(s.marginTop.toPixels(kEmSize, kViewportWidth), 59.055f, 0.01f);
  // ...and the old answer, one pixel, is emphatically not it.
  EXPECT_GT(s.marginTop.toPixels(kEmSize, kViewportWidth), 50.0f);
}

TEST(CssLengths, EveryAbsoluteUnitSurvivesTheDeclarationPath) {
  struct Case {
    const char* decl;
    float expectPx;
  };
  constexpr Case kCases[] = {
      {"margin-left: 1in", 150.0f},   {"margin-left: 10mm", 59.055f}, {"margin-left: 12pt", 25.0f},
      {"margin-left: 1pc", 25.0f},    {"margin-left: 4Q", 5.9055f},   {"margin-left: 0.5in", 75.0f},
      {"margin-left: -2mm", -11.811f},
  };
  for (const auto& c : kCases) {
    freshNotes();
    const CssStyle s = parse(c.decl);
    ASSERT_TRUE(s.defined.marginLeft) << c.decl;
    EXPECT_NEAR(s.marginLeft.toPixels(kEmSize, kViewportWidth), c.expectPx, 0.01f) << c.decl;
    EXPECT_FALSE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported)) << c.decl;
  }
}

TEST(CssLengths, TheRelativeUnitsAreUnchanged) {
  freshNotes();
  const CssStyle em = parse("text-indent: 2em");
  ASSERT_TRUE(em.defined.textIndent);
  EXPECT_FLOAT_EQ(em.textIndent.toPixels(kEmSize, kViewportWidth), 2.0f * kEmSize);

  const CssStyle pc = parse("text-indent: 10%");
  ASSERT_TRUE(pc.defined.textIndent);
  EXPECT_FLOAT_EQ(pc.textIndent.toPixels(kEmSize, kViewportWidth), 45.0f);

  const CssStyle px = parse("margin-bottom: 24px");
  ASSERT_TRUE(px.defined.marginBottom);
  EXPECT_FLOAT_EQ(px.marginBottom.toPixels(kEmSize, kViewportWidth), 24.0f);
}

// --- The parser: a unit with no honest conversion ---------------------------

TEST(CssLengths, AnUnconvertibleUnitDropsTheDeclarationAndSaysSo) {
  freshNotes();
  const CssStyle s = parse("margin-left: 5ex");
  // NOT defined: the cascade keeps whatever it had, which is what a browser
  // does with an invalid declaration. Silently writing 5 px was the bug.
  EXPECT_FALSE(s.defined.marginLeft);
  EXPECT_TRUE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported));
  EXPECT_STREQ(booknotes::current().details().unsupportedCssUnit, "ex");
}

TEST(CssLengths, ADroppedDeclarationLeavesAnEarlierOneStanding) {
  // The failure this guards is worse than losing the value: writing `out` on
  // the way to rejecting it would replace a good margin with a bad one.
  freshNotes();
  const CssStyle s = parse("margin-left: 2em; margin-left: 5vw");
  ASSERT_TRUE(s.defined.marginLeft);
  EXPECT_FLOAT_EQ(s.marginLeft.toPixels(kEmSize, kViewportWidth), 2.0f * kEmSize);
}

TEST(CssLengths, TheNoteNamesTheUnit) {
  freshNotes();
  parse("margin-top: 100vh");
  EXPECT_STREQ(booknotes::current().details().unsupportedCssUnit, "vh");
}

TEST(CssLengths, TheFirstUnitWinsAndTheNoteDoesNotChurn) {
  freshNotes();
  parse("margin-top: 10ex; margin-bottom: 20vw; text-indent: 3ch");
  EXPECT_STREQ(booknotes::current().details().unsupportedCssUnit, "ex");
  EXPECT_EQ(booknotes::current().count(), 1);
}

TEST(CssLengths, WidthAndHeightTakeTheSameRule) {
  freshNotes();
  const CssStyle s = parse("width: 50vw; height: 3cm");
  EXPECT_FALSE(s.defined.imageWidth);
  ASSERT_TRUE(s.defined.imageHeight);
  EXPECT_NEAR(s.imageHeight.toPixels(kEmSize, kViewportWidth), 177.165f, 0.01f);
  EXPECT_TRUE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported));
}

TEST(CssLengths, AKeywordIsNotAnUnconvertibleUnit) {
  // `auto` has resolved to zero here since the parser was written, and turning
  // it into an inherited margin is a different change. What it must NOT do is
  // raise the note: `margin: 0 auto` is on a large share of books, and a notice
  // that fires on nearly every one of them says nothing about any of them.
  for (const char* decl : {"margin-left: auto", "margin-top: inherit", "text-indent: initial"}) {
    freshNotes();
    parse(decl);
    EXPECT_FALSE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported)) << decl;
  }
}

TEST(CssLengths, AValueThatIsNotOneLengthIsNotAnUnconvertibleUnit) {
  // `margin-top: 10px 20px` is invalid CSS, and the digit scan hands the whole
  // remainder over as the "unit". Calling that unconvertible would print
  // `px 20px` into a notice a person reads.
  for (const char* decl : {"margin-top: 10px 20px", "margin-top: 1cm/2", "margin-top: 12px;;"}) {
    freshNotes();
    parse(decl);
    EXPECT_FALSE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported)) << decl;
  }
}

// --- The edge shorthand -----------------------------------------------------

TEST(CssShorthand, OneBadComponentDropsTheWholeDeclaration) {
  // CSS's own rule for an invalid value inside a shorthand, and the only one
  // that cannot leave a book with three sides of a margin.
  freshNotes();
  const CssStyle s = parse("margin: 1cm 3ex 1cm 1cm");
  EXPECT_FALSE(s.defined.marginTop);
  EXPECT_FALSE(s.defined.marginRight);
  EXPECT_FALSE(s.defined.marginBottom);
  EXPECT_FALSE(s.defined.marginLeft);
  EXPECT_TRUE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported));
}

TEST(CssShorthand, TheOneToFourExpansionIsUnchanged) {
  freshNotes();
  const CssStyle one = parse("padding: 2mm");
  EXPECT_NEAR(one.paddingTop.toPixels(kEmSize), 11.811f, 0.01f);
  EXPECT_NEAR(one.paddingRight.toPixels(kEmSize), 11.811f, 0.01f);
  EXPECT_NEAR(one.paddingBottom.toPixels(kEmSize), 11.811f, 0.01f);
  EXPECT_NEAR(one.paddingLeft.toPixels(kEmSize), 11.811f, 0.01f);

  const CssStyle two = parse("margin: 1cm 2cm");
  EXPECT_NEAR(two.marginTop.toPixels(kEmSize), 59.055f, 0.01f);
  EXPECT_NEAR(two.marginRight.toPixels(kEmSize), 118.11f, 0.01f);
  EXPECT_NEAR(two.marginBottom.toPixels(kEmSize), 59.055f, 0.01f);
  EXPECT_NEAR(two.marginLeft.toPixels(kEmSize), 118.11f, 0.01f);

  const CssStyle three = parse("margin: 1pc 2pc 3pc");
  EXPECT_NEAR(three.marginTop.toPixels(kEmSize), 25.0f, 0.01f);
  EXPECT_NEAR(three.marginRight.toPixels(kEmSize), 50.0f, 0.01f);
  EXPECT_NEAR(three.marginBottom.toPixels(kEmSize), 75.0f, 0.01f);
  EXPECT_NEAR(three.marginLeft.toPixels(kEmSize), 50.0f, 0.01f);
}

// --- !important -------------------------------------------------------------

TEST(CssImportant, ItComesOffBeforeTheUnitIsRead) {
  // It used to reach the unit scan glued to the unit ("cm !important"), which
  // matched nothing and read as pixels. With an unknown unit now dropping the
  // declaration, leaving it on would fire the note on most styled books.
  freshNotes();
  const CssStyle s = parse("margin-top: 1cm !important");
  ASSERT_TRUE(s.defined.marginTop);
  EXPECT_NEAR(s.marginTop.toPixels(kEmSize), 59.055f, 0.01f);
  EXPECT_FALSE(booknotes::current().has(booknotes::Note::CssUnitsUnsupported));
}

TEST(CssImportant, AShorthandKeepsAllFourSides) {
  // `margin: 1em !important` used to tokenize as TWO values -- 1em and the
  // literal "!important" -- so the right and left sides came out zero.
  freshNotes();
  const CssStyle s = parse("margin: 1em !important");
  ASSERT_TRUE(s.defined.marginRight);
  EXPECT_FLOAT_EQ(s.marginTop.toPixels(kEmSize), kEmSize);
  EXPECT_FLOAT_EQ(s.marginRight.toPixels(kEmSize), kEmSize);
  EXPECT_FLOAT_EQ(s.marginBottom.toPixels(kEmSize), kEmSize);
  EXPECT_FLOAT_EQ(s.marginLeft.toPixels(kEmSize), kEmSize);
}

TEST(CssImportant, AnUnconvertibleUnitStillReportsTheUnitAlone) {
  freshNotes();
  parse("margin-top: 3ch !important");
  EXPECT_STREQ(booknotes::current().details().unsupportedCssUnit, "ch");
}

TEST(CssImportant, TheKEYWORDPathsStripItToo) {
  // Found by adversarial review, 2026-08-23. `interpretAlignment`'s default is
  // NOT neutral: an unmatched value returns Left and the caller then sets
  // `defined.textAlign`, so `text-align: center !important` did not fall back,
  // it FORCED LEFT. Headings are the one block whose CSS alignment is honored,
  // so it was visible on the page. Pre-existing, and fixed here because the
  // length paths' rationale applies to these identically.
  CssStyle s = parse("text-align: center !important");
  ASSERT_TRUE(s.defined.textAlign);
  EXPECT_EQ(s.textAlign, CssTextAlign::Center);

  s = parse("text-align: right !important");
  EXPECT_EQ(s.textAlign, CssTextAlign::Right);

  s = parse("font-style: italic !important");
  ASSERT_TRUE(s.defined.fontStyle);
  EXPECT_EQ(s.fontStyle, CssFontStyle::Italic);

  s = parse("font-weight: bold !important");
  ASSERT_TRUE(s.defined.fontWeight);
  EXPECT_EQ(s.fontWeight, CssFontWeight::Bold);

  s = parse("font-weight: 700 !important");
  EXPECT_EQ(s.fontWeight, CssFontWeight::Bold);

  s = parse("text-decoration: underline !important");
  ASSERT_TRUE(s.defined.textDecoration);
  EXPECT_EQ(s.textDecoration, CssTextDecoration::Underline);

  s = parse("vertical-align: super !important");
  ASSERT_TRUE(s.defined.verticalAlign);
  EXPECT_EQ(s.verticalAlign, CssVerticalAlign::Super);
}

// --- The int16 boundary -----------------------------------------------------

TEST(CssLengths, AHugeAbsoluteLengthCannotOverflowTheStoredPixel) {
  // Found by adversarial review, 2026-08-23. `static_cast<int16_t>` from a float
  // outside the type's range is undefined behavior, and absolute units made it
  // reachable from a stylesheet: `500in` is 75,000 px where every unknown unit
  // used to collapse to single digits. The vertical margins have no layout
  // clamp above this, so nothing else stands in the way.
  freshNotes();
  const CssStyle big = parse("margin-top: 500in");
  ASSERT_TRUE(big.defined.marginTop);
  EXPECT_EQ(big.marginTop.toPixelsInt16(kEmSize, kViewportWidth), INT16_MAX);

  const CssStyle negative = parse("margin-top: -900cm");
  EXPECT_EQ(negative.marginTop.toPixelsInt16(kEmSize, kViewportWidth), INT16_MIN);

  // Just inside the boundary still converts exactly, so the clamp is a fence
  // and not a ceiling anything ordinary meets.
  const CssLength inRange{32000.0f, CssUnit::Pixels};
  EXPECT_EQ(inRange.toPixelsInt16(kEmSize, kViewportWidth), 32000);
}

TEST(CssLengths, ANonFiniteLengthIsZeroRatherThanUndefined) {
  // `strtof` accepts the spelling "nan", and the digit scan stops at 'n' -- so
  // the guard has to survive a value that is neither large nor small.
  const CssLength nan{std::numeric_limits<float>::quiet_NaN(), CssUnit::Pixels};
  EXPECT_EQ(nan.toPixelsInt16(kEmSize, kViewportWidth), 0);
  const CssLength inf{std::numeric_limits<float>::infinity(), CssUnit::Pixels};
  EXPECT_EQ(inf.toPixelsInt16(kEmSize, kViewportWidth), INT16_MAX);
}

// --- The note itself --------------------------------------------------------

TEST(CssUnitNote, ItIsBookScopeAndSurvivesAFontChange) {
  // The stylesheet is parsed once, at book load. A layout-scope note would be
  // dropped by the next render pass's fingerprint and the reader would never
  // see it again.
  EXPECT_FALSE(booknotes::isLayoutScope(booknotes::Note::CssUnitsUnsupported));
  auto& n = freshNotes();
  n.raiseUnsupportedCssUnit("vh");
  n.setLayoutFingerprint(0x12345678);
  EXPECT_TRUE(n.has(booknotes::Note::CssUnitsUnsupported));
  EXPECT_STREQ(n.details().unsupportedCssUnit, "vh");
}

TEST(CssUnitNote, ALongUnitIsTruncatedRatherThanOverrunning) {
  auto& n = freshNotes();
  n.raiseUnsupportedCssUnit("abcdefghijklmnop");
  EXPECT_EQ(std::string(n.details().unsupportedCssUnit).size(), 7u);
}
