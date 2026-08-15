// Placement of markdown spans, against the REAL renderer and real font tables.
//
// THE BUG THIS EXISTS FOR
//
// drawLine consumes the markers: "**bold**" is eight bytes of source and four
// glyphs on screen. Two callers measured the SOURCE and drew through drawLine,
// so they disagreed with themselves by exactly the markers' width -- four
// characters per bold run, two per italic or code run:
//
//   1. NoteEditorActivity::relayout() wrapped on getTextWidth() of the raw
//      line, so a line carrying two bold runs broke eight characters early and
//      left a ragged gap after the styled words. Reproduced in the simulator on
//      2026-08-15: "aaaa **bbbb** cccc **dddd** eeee ffff" broke after "dddd"
//      at 19 rendered characters, while the unstyled line beside it fitted 26.
//   2. The caret was placed with advanceOf() of the raw source prefix, so after
//      a bold word it sat four characters to the right of the glyph it was
//      supposed to be against.
//
// The per-span placement inside drawLine was NOT the bug and is pinned here so
// a fix does not become one: advanceOf accumulates to the same x as
// getTextAdvanceX across four faces, three styles and every marker shape.

#include <EpdFontFamily.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <cstring>
#include <iterator>
#include <string>

#include "builtinFonts/iawriterduo_12_bold.h"
#include "builtinFonts/iawriterduo_12_bolditalic.h"
#include "builtinFonts/iawriterduo_12_italic.h"
#include "builtinFonts/iawriterduo_12_regular.h"
#include "builtinFonts/iawritermono_12_bold.h"
#include "builtinFonts/iawritermono_12_bolditalic.h"
#include "builtinFonts/iawritermono_12_italic.h"
#include "builtinFonts/iawritermono_12_regular.h"
#include "builtinFonts/iawriterquattro_12_bold.h"
#include "builtinFonts/iawriterquattro_12_bolditalic.h"
#include "builtinFonts/iawriterquattro_12_italic.h"
#include "builtinFonts/iawriterquattro_12_regular.h"
#include "builtinFonts/librefranklin_12_bold.h"
#include "builtinFonts/librefranklin_12_regular.h"
#include "notes/MarkdownRender.h"
#include "notes/MarkdownSpans.h"

HalDisplay display;
FontDecompressor fontDecompressor;

namespace {

// iA Writer Quattro is the editor's DEFAULT face (CrossPointSettings.h,
// editorFont = 0 -> EditorFonts.h FAMILIES[0]) and the one the bug was seen
// under, so it leads. Mono and Duo cover the monospaced editor faces and
// LibreFranklin the proportional UI face, because the two things that can go
// wrong here -- side bearings and style-dependent advances -- show up
// differently on each.
constexpr int kQuattro = 7001;
constexpr int kMono = 7002;
constexpr int kDuo = 7003;
constexpr int kFranklin = 7004;

// The editor passes its line height as indentStep; 20 stands in for it.
constexpr int kIndentStep = 20;

GfxRenderer& renderer() {
  static GfxRenderer r(display);
  static bool begun = [] {
    r.begin();
    static FontCacheManager fcm(r.getFontMap(), r.getSdCardFonts());
    fcm.setFontDecompressor(&fontDecompressor);
    r.setFontCacheManager(&fcm);
    static EpdFont qr(&iawriterquattro_12_regular);
    static EpdFont qb(&iawriterquattro_12_bold);
    static EpdFont qi(&iawriterquattro_12_italic);
    static EpdFont qbi(&iawriterquattro_12_bolditalic);
    static EpdFontFamily qfam(&qr, &qb, &qi, &qbi);
    r.insertFont(kQuattro, qfam);
    static EpdFont mr(&iawritermono_12_regular);
    static EpdFont mb(&iawritermono_12_bold);
    static EpdFont mi(&iawritermono_12_italic);
    static EpdFont mbi(&iawritermono_12_bolditalic);
    static EpdFontFamily mfam(&mr, &mb, &mi, &mbi);
    r.insertFont(kMono, mfam);
    static EpdFont dr(&iawriterduo_12_regular);
    static EpdFont db(&iawriterduo_12_bold);
    static EpdFont di(&iawriterduo_12_italic);
    static EpdFont dbi(&iawriterduo_12_bolditalic);
    static EpdFontFamily dfam(&dr, &db, &di, &dbi);
    r.insertFont(kDuo, dfam);
    // No italic cut is compiled in at LibreFranklin 12 (it is a chrome face);
    // the regular stands in, which is what the UI would draw anyway.
    static EpdFont fr(&librefranklin_12_regular);
    static EpdFont fb(&librefranklin_12_bold);
    static EpdFontFamily ffam(&fr, &fb, &fr, &fb);
    r.insertFont(kFranklin, ffam);
    return true;
  }();
  (void)begun;
  return r;
}

constexpr int kFonts[] = {kQuattro, kMono, kDuo, kFranklin};
constexpr const char* kFontNames[] = {"iAWriterQuattro12", "iAWriterMono12", "iAWriterDuo12", "LibreFranklin12"};

// What the caller MEANT to put on screen: the same line with its markers
// removed by hand, measured as one plain run.
int plainAdvance(int fontId, const char* s) { return renderer().getTextAdvanceX(fontId, s, EpdFontFamily::REGULAR); }

}  // namespace

// ---------------------------------------------------------------------------
// measureLine: the primitive both fixes are built on.
// ---------------------------------------------------------------------------

// The bug, stated as an equation. Measuring the source over-counts by the
// markers; measureLine must not.
TEST(MeasureLine, MarkersCostNothing) {
  const struct {
    const char* src;
    const char* stripped;
  } cases[] = {
      {"Plain **bold** and more", "Plain bold and more"},
      {"aaaa **bbbb** cccc **dddd** eeee ffff", "aaaa bbbb cccc dddd eeee ffff"},
      {"a *b* c *d* e", "a b c d e"},
      {"tick `code` tock", "tick code tock"},
      {"no styling at all here", "no styling at all here"},
  };
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    for (const auto& c : cases) {
      const int measured = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, c.src, strlen(c.src));
      const int source = renderer().getTextWidth(kFonts[f], c.src, EpdFontFamily::REGULAR);
      // Close to the marker-free string. Not an identity: the styled runs draw
      // in a different face, so their advances differ by a pixel or two.
      EXPECT_NEAR(measured, plainAdvance(kFonts[f], c.stripped), 4)
          << kFontNames[f] << " '" << c.src << "' measured " << measured;
      if (strcmp(c.src, c.stripped) != 0) {
        EXPECT_LT(measured, source) << kFontNames[f] << " '" << c.src
                                    << "': measuring the source is what wrapped the line early";
      }
    }
  }
}

// measureLine and drawLine must be the same walk. drawLine returns the pen x it
// ended at, so with originX 0 the two are the same number by construction --
// this pins that they stay so.
TEST(MeasureLine, AgreesWithWhatDrawLineReturns) {
  const char* cases[] = {
      "Rain again; the **fix** was to *stop*.",  "# Heading one here",
      "- bullet with **bold** then tail words",  "> quote with *italic* then tail words",
      "1. numbered, `code_span --flag` tail",    "**bold** first",
      "trailing space and a bold run **here** ",
  };
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    for (const char* src : cases) {
      const size_t len = strlen(src);
      const int drawn = mdrender::drawLine(renderer(), kFonts[f], kIndentStep, 0, 0, src, len);
      const int measured = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, src, len);
      EXPECT_EQ(measured, drawn) << kFontNames[f] << " '" << src << "'";
    }
  }
}

TEST(MeasureLine, HangingIndentIsCounted) {
  const char* bullet = "- text";
  const char* plain = "text";
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    const int withMarker = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, bullet, strlen(bullet));
    const int without = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, plain, strlen(plain));
    EXPECT_EQ(withMarker, without + kIndentStep) << kFontNames[f];
  }
}

// ---------------------------------------------------------------------------
// caretX: the second half of the same bug.
// ---------------------------------------------------------------------------

// A caret at the end of the line must land on the end of the drawn text, not
// four characters past it.
TEST(CaretX, EndOfLineSitsAtEndOfDrawnText) {
  const char* cases[] = {
      "Plain **bold**", "Plain **bold** and", "a *b*", "`code`", "# Heading", "- bullet **b**",
  };
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    for (const char* src : cases) {
      const size_t len = strlen(src);
      const int caret = mdrender::caretX(renderer(), kFonts[f], kIndentStep, src, len, len);
      const int end = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, src, len);
      EXPECT_EQ(caret, end) << kFontNames[f] << " '" << src << "'";
    }
  }
}

// The whole point: a caret placed from the raw source drifts by the markers.
// This is the assertion that failed before the fix.
TEST(CaretX, DoesNotDriftByTheMarkerWidth) {
  const char* src = "Plain **bold** and more";
  const size_t len = strlen(src);
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    const int caret = mdrender::caretX(renderer(), kFonts[f], kIndentStep, src, len, len);
    const int rawWay = mdrender::advanceOf(renderer(), kFonts[f], src, EpdFontFamily::REGULAR);
    EXPECT_LT(caret, rawWay) << kFontNames[f] << ": the old placement counted the four asterisks";
    EXPECT_GT(rawWay - caret, 8) << kFontNames[f] << ": four characters is a visible gap, not a rounding error";
  }
}

TEST(CaretX, AdvancesMonotonicallyAcrossTheLine) {
  const char* src = "- one **two** three *four* five";
  const size_t len = strlen(src);
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    int previous = -1;
    for (size_t col = 0; col <= len; ++col) {
      const int x = mdrender::caretX(renderer(), kFonts[f], kIndentStep, src, len, col);
      EXPECT_GE(x, previous) << kFontNames[f] << " went backwards at column " << col;
      previous = x;
    }
  }
}

// A column inside a consumed marker has no pixel of its own. It must clamp to
// the run the marker opens (or closes), never to some interpolated position --
// otherwise the caret sits in whitespace that is not on screen.
TEST(CaretX, ColumnsInsideMarkersClampToTheRun) {
  const char* src = "ab**cd**ef";  // markers at [2,4) and [6,8)
  const size_t len = strlen(src);
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    const int id = kFonts[f];
    const int atAB = mdrender::caretX(renderer(), id, kIndentStep, src, len, 2);
    EXPECT_EQ(mdrender::caretX(renderer(), id, kIndentStep, src, len, 3), atAB) << kFontNames[f];
    EXPECT_EQ(mdrender::caretX(renderer(), id, kIndentStep, src, len, 4), atAB) << kFontNames[f];
    const int atCD = mdrender::caretX(renderer(), id, kIndentStep, src, len, 6);
    EXPECT_EQ(mdrender::caretX(renderer(), id, kIndentStep, src, len, 7), atCD) << kFontNames[f];
    EXPECT_EQ(mdrender::caretX(renderer(), id, kIndentStep, src, len, 8), atCD) << kFontNames[f];
    EXPECT_GT(atCD, atAB) << kFontNames[f];
  }
}

// Every column of the block prefix sits against the body, including column 0:
// "## " is not drawn at all, so there is nothing before the body to point at.
TEST(CaretX, BlockPrefixColumnsSitAtTheBody) {
  const char* src = "## Heading";
  const size_t len = strlen(src);
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    const int body = mdrender::caretX(renderer(), kFonts[f], kIndentStep, src, len, 3);
    for (size_t col = 0; col <= 3; ++col) {
      EXPECT_EQ(mdrender::caretX(renderer(), kFonts[f], kIndentStep, src, len, col), body) << kFontNames[f];
    }
  }
}

// ---------------------------------------------------------------------------
// wrapLine
// ---------------------------------------------------------------------------

TEST(WrapLine, EveryFragmentFitsAndNothingIsLost) {
  const char* cases[] = {
      "aaaa **bbbb** cccc **dddd** eeee ffff gggg hhhh iiii jjjj",
      "Rain again; the **fix** was to *stop*, and then the rain came back again.",
      "- a bulleted line with **bold** inside it that is long enough to need wrapping twice",
      "plain words with no styling at all but quite a lot of them to force several breaks",
  };
  const int widths[] = {120, 200, 320};
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    for (const char* src : cases) {
      for (const int w : widths) {
        const size_t len = strlen(src);
        mdrender::Fragment frags[32];
        const size_t n = mdrender::wrapLine(renderer(), kFonts[f], kIndentStep, src, len, w, frags, std::size(frags));
        ASSERT_GT(n, 0u) << kFontNames[f] << " '" << src << "' @" << w;
        EXPECT_EQ(frags[0].start, 0u);
        EXPECT_EQ(frags[n - 1].end, len) << kFontNames[f] << " '" << src << "' @" << w << " dropped the tail";
        for (size_t i = 0; i < n; ++i) {
          EXPECT_LE(frags[i].start, frags[i].end);
          if (i > 0) EXPECT_GE(frags[i].start, frags[i - 1].end);
          const size_t flen = frags[i].end - frags[i].start;
          // A single character that still overflows cannot be cut further;
          // every other fragment must fit what it was measured against.
          if (flen > 1) {
            const int drawn = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, src + frags[i].start, flen);
            EXPECT_LE(drawn, w) << kFontNames[f] << " fragment " << i << " of '" << src << "' @" << w;
          }
        }
      }
    }
  }
}

// The regression, in the shape it was seen: a styled line must not need more
// visual lines than the same line with its markers removed.
TEST(WrapLine, StyledLineBreaksNoEarlierThanThePlainOne) {
  const char* styled = "aaaa **bbbb** cccc **dddd** eeee ffff";
  const char* plain = "aaaa bbbb cccc dddd eeee ffff";
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    mdrender::Fragment a[8];
    mdrender::Fragment b[8];
    const size_t na =
        mdrender::wrapLine(renderer(), kFonts[f], kIndentStep, styled, strlen(styled), 300, a, std::size(a));
    const size_t nb =
        mdrender::wrapLine(renderer(), kFonts[f], kIndentStep, plain, strlen(plain), 300, b, std::size(b));
    ASSERT_GT(na, 0u);
    ASSERT_GT(nb, 0u);
    EXPECT_EQ(na, nb) << kFontNames[f] << ": the markers bought extra visual lines";
  }
}

TEST(WrapLine, BlankLineKeepsItsRow) {
  mdrender::Fragment frags[4];
  const size_t n = mdrender::wrapLine(renderer(), kQuattro, kIndentStep, "", 0, 300, frags, std::size(frags));
  EXPECT_EQ(n, 1u);
  EXPECT_EQ(frags[0].start, 0u);
  EXPECT_EQ(frags[0].end, 0u);
}

TEST(WrapLine, OneUnbreakableWordIsCutRatherThanDropped) {
  const char* src = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  const size_t len = strlen(src);
  mdrender::Fragment frags[32];
  const size_t n = mdrender::wrapLine(renderer(), kQuattro, kIndentStep, src, len, 100, frags, std::size(frags));
  ASSERT_GT(n, 1u);
  EXPECT_EQ(frags[0].start, 0u);
  EXPECT_EQ(frags[n - 1].end, len);
}

TEST(WrapLine, RespectsTheFragmentCap) {
  const char* src = "one two three four five six seven eight nine ten eleven twelve";
  mdrender::Fragment frags[2];
  const size_t n = mdrender::wrapLine(renderer(), kQuattro, kIndentStep, src, strlen(src), 60, frags, std::size(frags));
  EXPECT_LE(n, std::size(frags));
}

// ---------------------------------------------------------------------------
// The placement that was NOT the bug, pinned so it does not become one.
// ---------------------------------------------------------------------------

TEST(SpanPlacement, AdvanceOfAgreesWithGetTextAdvanceX) {
  const char* pieces[] = {"Plain ", "bold", " and", "fix",       "stop", "was to ", "The ",   "Wave",
                          "yes. ",  "a",    "  ",   "code_span", "AV",   "To be",   "l vs 1", "half four"};
  const EpdFontFamily::Style styles[] = {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC};
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    for (const auto st : styles) {
      for (const char* p : pieces) {
        // Exactly, not nearly. advanceOf IS getTextAdvanceX now; the sentinel
        // form it replaced was off by up to 4 px on an italic run, which is
        // what put a gap after every styled word.
        EXPECT_EQ(mdrender::advanceOf(renderer(), kFonts[f], p, st), renderer().getTextAdvanceX(kFonts[f], p, st))
            << kFontNames[f] << " '" << p << "' style " << static_cast<int>(st);
      }
    }
  }
}

// Span-by-span placement must land on the same total as drawing the marker-free
// string in one call -- i.e. the gaps between the runs are ordinary word gaps,
// not the wide ones the report described. This one passed BEFORE the fix and is
// here so the fix is not credited with something it did not do.
TEST(SpanPlacement, StyledRunsLandWhereThePlainStringWouldPutThem) {
  const struct {
    const char* src;
    const char* stripped;
  } cases[] = {
      {"Rain again; the **fix** was to *stop*.", "Rain again; the fix was to stop."},
      {"Plain **bold** and more", "Plain bold and more"},
      {"aaaa **bbbb** cccc **dddd** eeee ffff", "aaaa bbbb cccc dddd eeee ffff"},
  };
  for (size_t f = 0; f < std::size(kFonts); ++f) {
    for (const auto& c : cases) {
      const int measured = mdrender::measureLine(renderer(), kFonts[f], kIndentStep, c.src, strlen(c.src));
      EXPECT_NEAR(measured, plainAdvance(kFonts[f], c.stripped), 4) << kFontNames[f] << " '" << c.src << "'";
    }
  }
}
