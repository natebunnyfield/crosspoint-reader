// The measure decides the alignment, and every failure mode here is silent.
//
// The 2026-08-22 Text Alignment setting was withdrawn on 2026-08-23 (owner:
// "remove ragged right or justified ios app settings, instead make it automatic
// by letting the character length decide what is optimal"), and the decision
// moved into ParsedText::layoutAndExtractLines, which reads
// autojustify::shouldJustify. Nothing downstream reports the answer: a page
// that justified when it should have ragged just looks slightly worse, a page
// that ragged when it should have justified just looks different, and both
// compile. So the arithmetic is pinned here.
//
// What these tests pin, and why each one exists:
//
//  1. The threshold is Bringhurst's 40-character line, and it is a >= not a >
//     (2.1.2, p. 27: "A reasonable working minimum for justified text in
//     English is the 40-character line").
//  2. The constant is Bringhurst's copyfitting table (pp. 28-29), reproduced
//     against his own worked example -- a 25-pica measure with a 128 pt
//     alphabet is "roughly 65 characters per line". A units slip here is
//     invisible: it just moves every book to the other side of the line.
//  3. The estimate matches what this firmware ACTUALLY renders, to +/-3
//     characters, on the 13 face/size pairs measured through the simulator on
//     2026-08-23. This is the test that fails if the alphabet-length method is
//     ever swapped for a point-size proxy.
//  4. Monotonicity in both arguments, and the flip is a single clean edge --
//     no measure justifies while a wider one rags.
//  5. An unmeasurable alphabet leaves the requested alignment ALONE. The
//     fallback for a missing metric must not be "silently restyle the book".

#include <gtest/gtest.h>

#include <cmath>

#include "AutoJustify.h"

namespace {

// Bringhurst's own worked example, 2.1.2 p. 27: type 10 pt, measure 25 picas,
// lowercase alphabet 128 pt. One pica is 12 points; this file works in whole
// units of a point so the arithmetic is the same integer arithmetic the
// paginator does in pixels (the estimate is scale-free -- it only ever sees a
// ratio).
constexpr int kBringhurstMeasurePt = 25 * 12;  // 300 pt
constexpr int kBringhurstAlphabetPt = 128;

// Measured through the simulator on 2026-08-23: an X3 in portrait, screen
// margin 5, giving a 512 px measure, with a controlled prose EPUB. `alphabet`
// is what GfxRenderer::getTextAdvanceX returned for a-z; `counted` is the mean
// characters per rendered line taken from the firmware's own read-aloud page
// capture (word rects grouped by baseline), body lines only.
struct Rendered {
  const char* face;
  int pointSize;
  int measurePx;
  int alphabetPx;
  double counted;
};

constexpr Rendered kRendered[] = {
    {"LibrisADF", 12, 512, 270, 50.0},      {"LibrisADF", 14, 512, 318, 43.6},
    {"LibrisADF", 16, 512, 364, 37.0},      {"LibrisADF", 18, 512, 412, 35.6},
    {"LibreFranklin", 14, 512, 346, 43.1},  {"LibreFranklin", 18, 512, 456, 31.1},
    {"TeXGyreSchola", 14, 512, 397, 38.4},  {"TeXGyreSchola", 18, 512, 510, 25.7},
    {"Coelacanth", 14, 512, 365, 41.9},     {"Coelacanth", 18, 512, 483, 28.6},
    {"InknutJunicode", 14, 512, 412, 34.4}, {"Edgar", 14, 512, 388, 39.5},
    {"TeXGyreHeros", 14, 512, 323, 44.6},
};

}  // namespace

TEST(AutoJustify, ThresholdIsBringhurstsFortyCharacterLine) {
  EXPECT_EQ(autojustify::THRESHOLD_CHARS, 40)
      << "Bringhurst, The Elements of Typographic Style, 2.1.2 p. 27: 'A reasonable "
         "working minimum for justified text in English is the 40-character line.'";
}

TEST(AutoJustify, ForNineIsTheFirstCountThatJustifies) {
  // Inclusive, not exclusive. He calls 40 the working MINIMUM, so 40 is in.
  // Pick an alphabet where the counts land cleanly on integers.
  const int alphabet = 281;  // 1 px per 0.1 char at this constant
  EXPECT_EQ(autojustify::charsPerLine(400, alphabet), 40);
  EXPECT_TRUE(autojustify::shouldJustify(400, alphabet));
  EXPECT_EQ(autojustify::charsPerLine(390, alphabet), 39);
  EXPECT_FALSE(autojustify::shouldJustify(390, alphabet));
}

TEST(AutoJustify, ReproducesBringhurstsWorkedExample) {
  // "a 10 pt text font [with a 128 pt alphabet] set to a 25-pica measure will
  // yield roughly 65 characters per line" (2.1.2, p. 27).
  const int got = autojustify::charsPerLine(kBringhurstMeasurePt, kBringhurstAlphabetPt);
  EXPECT_GE(got, 64);
  EXPECT_LE(got, 66) << "the copyfitting constant has drifted; got " << got << " where the book says roughly 65";
}

TEST(AutoJustify, ReproducesBringhurstsCopyfittingTable) {
  // Selected cells as printed on pp. 28-29. Rows are the lowercase alphabet in
  // points, columns the line length in picas. Tolerance is one character: the
  // table is itself rounded, and its constant drifts from 26.7 at an 80 pt
  // alphabet to 30.0 at 360 pt while this code carries the single value that
  // covers the 120-140 pt band a text roman occupies.
  struct Cell {
    int alphabetPt;
    int picas;
    int chars;
    int tolerance;
  };
  constexpr Cell kCells[] = {
      // The band this constant is fitted to.
      {120, 20, 56, 1},
      {120, 30, 84, 1},
      {125, 24, 65, 1},
      {130, 26, 67, 1},
      {130, 30, 78, 1},
      {140, 30, 73, 1},
      {140, 20, 48, 1},
      // The ends of the table, where the book's own constant differs from ours.
      {80, 20, 80, 4},
      {200, 30, 53, 2},
      {360, 40, 40, 3},
  };
  for (const auto& c : kCells) {
    const int got = autojustify::charsPerLine(c.picas * 12, c.alphabetPt);
    EXPECT_NEAR(got, c.chars, c.tolerance)
        << "alphabet " << c.alphabetPt << " pt at " << c.picas << " picas: table says " << c.chars;
  }
}

TEST(AutoJustify, MatchesWhatTheFirmwareActuallyRenders) {
  // The estimate is only worth anything if it predicts this device's own
  // output. Sweep measured 2026-08-23; see docs/auto-justification.md.
  double worst = 0.0;
  for (const auto& r : kRendered) {
    const int got = autojustify::charsPerLine(r.measurePx, r.alphabetPx);
    const double err = std::fabs(got - r.counted);
    worst = std::max(worst, err);
    EXPECT_LE(err, 3.0) << r.face << " " << r.pointSize << " pt: estimated " << got << " characters per line, rendered "
                        << r.counted;
  }
  EXPECT_LE(worst, 3.0) << "worst residual across the sweep was " << worst;
}

TEST(AutoJustify, ErrsTowardRaggedNearTheBoundary) {
  // Bringhurst's failure zone is "less than 38 or 40 characters", so a late rag
  // costs more than an early one. The counted lines are justified, whose
  // stretched gaps hold slightly fewer characters than the natural setting the
  // estimate models, and the estimate must not systematically overshoot them.
  double sumSigned = 0.0;
  for (const auto& r : kRendered) {
    sumSigned += autojustify::charsPerLine(r.measurePx, r.alphabetPx) - r.counted;
  }
  const double bias = sumSigned / (sizeof(kRendered) / sizeof(kRendered[0]));
  EXPECT_LE(bias, 1.0) << "the estimate is running " << bias << " characters generous; it would justify pages the "
                       << "device renders below Bringhurst's floor";
}

TEST(AutoJustify, APointSizeProxyWouldGetThisWrong) {
  // The reason the alphabet is measured rather than divided out of the point
  // size: at 14 pt this card's faces span a 1.4x range of alphabet length, and
  // that range straddles the threshold at a fixed measure. A measure/fontSize
  // proxy cannot see the difference, so it would put TeXGyre Heros and Inknut
  // Junicode -- both 14 pt, 44.6 and 34.4 rendered characters -- on the same
  // side of the line.
  EXPECT_TRUE(autojustify::shouldJustify(512, 323));   // TeXGyreHeros 14 pt
  EXPECT_FALSE(autojustify::shouldJustify(512, 412));  // InknutJunicode 14 pt
}

TEST(AutoJustify, WiderMeasureNeverRagsWhenANarrowerOneJustifies) {
  const int alphabet = 400;
  bool seenJustified = false;
  for (int measure = 1; measure <= 2000; ++measure) {
    const bool justify = autojustify::shouldJustify(measure, alphabet);
    if (justify) {
      seenJustified = true;
    } else {
      EXPECT_FALSE(seenJustified) << "measure " << measure << " rags after a narrower one justified";
    }
    // And the count itself never goes backwards.
    if (measure > 1) {
      EXPECT_GE(autojustify::charsPerLine(measure, alphabet), autojustify::charsPerLine(measure - 1, alphabet));
    }
  }
  EXPECT_TRUE(seenJustified);
}

TEST(AutoJustify, ABiggerFaceNeverJustifiesWhenASmallerOneRagged) {
  const int measure = 512;
  bool seenRagged = false;
  for (int alphabet = 1; alphabet <= 2000; ++alphabet) {
    const bool justify = autojustify::shouldJustify(measure, alphabet);
    if (!justify) {
      seenRagged = true;
    } else {
      EXPECT_FALSE(seenRagged) << "alphabet " << alphabet << " justifies after a smaller one ragged";
    }
  }
  EXPECT_TRUE(seenRagged);
}

TEST(AutoJustify, TheFlipIsASingleCleanEdge) {
  // narrowestJustifiedMeasurePx is what the doc's worked examples quote, so it
  // has to agree with the predicate exactly -- one px narrower must rag.
  for (int alphabet = 100; alphabet <= 800; alphabet += 7) {
    const int flip = autojustify::narrowestJustifiedMeasurePx(alphabet);
    EXPECT_TRUE(autojustify::shouldJustify(flip, alphabet)) << "alphabet " << alphabet << ", flip " << flip;
    EXPECT_FALSE(autojustify::shouldJustify(flip - 1, alphabet)) << "alphabet " << alphabet << ", flip " << flip;
  }
}

TEST(AutoJustify, AnUnmeasurableAlphabetLeavesTheRequestAlone) {
  // A face with no lowercase (a symbol font), or a metrics read that came back
  // empty. The wrong answer here is "rag the whole book because a number was
  // missing" -- silent, global, and attributable to nothing.
  EXPECT_TRUE(autojustify::shouldJustify(512, 0));
  EXPECT_TRUE(autojustify::shouldJustify(512, -1));
  EXPECT_EQ(autojustify::charsPerLine(512, 0), 0);
  EXPECT_EQ(autojustify::charsPerLine(0, 400), 0);
  EXPECT_EQ(autojustify::charsPerLine(-5, 400), 0);
  EXPECT_EQ(autojustify::narrowestJustifiedMeasurePx(0), 0);
}

TEST(AutoJustify, TheArithmeticDoesNotOverflowAtAnyPlausibleMeasure) {
  // measurePx * 281 must stay in an int. The widest panel this firmware drives
  // is 800 px, and a 3x render scale would make it 2400; a hostile EPUB cannot
  // reach the measure, which is derived from the viewport. Sweep well past it,
  // and past any alphabet a 4-byte advance sum could hold.
  int previous = -1;
  for (int measure = 1; measure <= 200000; measure += 37) {
    const int got = autojustify::charsPerLine(measure, 300);
    EXPECT_GE(got, 0) << "measure " << measure;
    EXPECT_GE(got, previous) << "wrapped at measure " << measure;
    previous = got;
  }
  EXPECT_GT(previous, 0);
  // A one-pixel measure holds no characters, and that is not the same signal as
  // "the alphabet could not be measured" -- shouldJustify only reads the
  // sentinel off the alphabet, so a hairline measure rags rather than justifies.
  EXPECT_EQ(autojustify::charsPerLine(1, 300), 0);
  EXPECT_FALSE(autojustify::shouldJustify(1, 300));
}

// ---------------------------------------------------------------------------
// THE THRESHOLD IS A SETTING (owner ruling 2026-08-24, "make justified or
// ragged right character count an ios app setting" -> the firmware's own
// Settings screen, as Justified Text).
//
// The DECISION is unchanged: the measure still decides, per block. What moves
// is the count it decides against. Everything below exists because each failure
// mode is silent -- a threshold that does not reach layout looks exactly like a
// book whose measure happens to sit on the same side of both values, and a
// ladder rung that duplicates its neighbour looks exactly like a settings row
// that works.
// ---------------------------------------------------------------------------

TEST(AutoJustifyThreshold, TheDefaultIsStillBringhurstsFortyCharacterLine) {
  // The row moved; the book's number did not. A call that names no threshold
  // must land exactly where every call landed before the setting existed --
  // this is what keeps the test harnesses, and any future caller that does not
  // care, on the documented value rather than on whatever the ladder's first
  // rung happens to be.
  EXPECT_EQ(autojustify::THRESHOLD_CHARS, 40);
  EXPECT_EQ(autojustify::clampThreshold(autojustify::THRESHOLD_CHARS), 40);
  for (int alphabet = 200; alphabet <= 600; alphabet += 7) {
    for (int measure = 200; measure <= 900; measure += 11) {
      EXPECT_EQ(autojustify::shouldJustify(measure, alphabet),
                autojustify::shouldJustify(measure, alphabet, autojustify::THRESHOLD_CHARS))
          << "measure " << measure << " alphabet " << alphabet;
    }
    EXPECT_EQ(autojustify::narrowestJustifiedMeasurePx(alphabet),
              autojustify::narrowestJustifiedMeasurePx(alphabet, autojustify::THRESHOLD_CHARS));
  }
}

TEST(AutoJustifyThreshold, TheLadderIsAscendingAndEveryRungIsDistinct) {
  // A duplicated or out-of-order rung is a settings row that lies: the picker
  // shows five choices and two of them do the same thing, which no compiler and
  // no render can see.
  ASSERT_EQ(autojustify::THRESHOLD_CHOICE_COUNT, 5);
  for (int i = 1; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    EXPECT_LT(autojustify::THRESHOLD_CHOICES[i - 1], autojustify::THRESHOLD_CHOICES[i])
        << "rung " << i << " does not ascend";
  }
  // The shipped ladder, pinned. Not a restatement of the array: these are the
  // numbers the settings labels print, and a rung that moves without its label
  // moving ships a row reading "Balanced (40)" that sets something else.
  EXPECT_EQ(autojustify::THRESHOLD_CHOICES[0], 32);
  EXPECT_EQ(autojustify::THRESHOLD_CHOICES[1], 36);
  EXPECT_EQ(autojustify::THRESHOLD_CHOICES[2], 40);
  EXPECT_EQ(autojustify::THRESHOLD_CHOICES[3], 45);
  EXPECT_EQ(autojustify::THRESHOLD_CHOICES[4], 50);
  // Bringhurst's number is on the ladder, and it is the default.
  bool defaultIsOffered = false;
  for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    if (autojustify::THRESHOLD_CHOICES[i] == autojustify::THRESHOLD_CHARS) defaultIsOffered = true;
  }
  EXPECT_TRUE(defaultIsOffered) << "the default must be selectable, or the row cannot return to it";
}

TEST(AutoJustifyThreshold, BothEndpointsLeaveTheOtherRegimeReachable) {
  // THE reason the rungs are not round numbers, and the one thing a future
  // reader cannot reconstruct from the array. Each endpoint is the last rung
  // that still leaves the OTHER regime reachable on this device; one step
  // further and the row becomes decoration -- a setting that reads as a measure
  // and behaves as an on/off switch.
  //
  // Instrument: the widest and narrowest settings in the 2026-08-23 calibration
  // sweep, at the X3's 512 px portrait measure.
  constexpr int kMeasurePx = 512;
  constexpr int kWidestAlphabetPx = 270;     // LibrisADF 12 pt -> est 53 ch/line
  constexpr int kNarrowestAlphabetPx = 510;  // TeXGyre Schola 18 pt -> est 28

  const int widest = autojustify::charsPerLine(kMeasurePx, kWidestAlphabetPx);
  const int narrowest = autojustify::charsPerLine(kMeasurePx, kNarrowestAlphabetPx);
  ASSERT_EQ(widest, 53);
  ASSERT_EQ(narrowest, 28);

  const int top = autojustify::THRESHOLD_CHOICES[autojustify::THRESHOLD_CHOICE_COUNT - 1];
  const int bottom = autojustify::THRESHOLD_CHOICES[0];

  // TOP RUNG: the widest setting on the card still justifies, so a justified
  // regime remains reachable. 55 would justify nothing at all.
  EXPECT_LE(top, widest) << "top rung justifies nothing on this device -- a dead row";
  EXPECT_TRUE(autojustify::shouldJustify(kMeasurePx, kWidestAlphabetPx, top));

  // BOTTOM RUNG: the narrowest setting on the card still rags, so a ragged
  // regime remains reachable. 28 would justify everything.
  EXPECT_GT(bottom, narrowest) << "bottom rung justifies everything -- equally dead, from the other end";
  EXPECT_FALSE(autojustify::shouldJustify(kMeasurePx, kNarrowestAlphabetPx, bottom));

  // And the bottom rung is genuinely a THRESHOLD there, not a floor: Libre
  // Franklin 18 pt (alphabet 456 px, est 32) justifies at 32 while Coelacanth
  // 18 pt (483 px, est 30) does not. Both are in the same sweep.
  EXPECT_TRUE(autojustify::shouldJustify(kMeasurePx, 456, bottom));
  EXPECT_FALSE(autojustify::shouldJustify(kMeasurePx, 483, bottom));
}

TEST(AutoJustifyThreshold, EveryRungActuallyMovesTheVerdictForSomeRealSetting) {
  // A rung that never changes any decision on any face this device ships is a
  // row the owner can select and see nothing happen. Swept against the 13
  // face/size pairs of the calibration table, at their measured measure.
  struct Pair {
    int measurePx;
    int alphabetPx;
  };
  constexpr Pair kSweep[] = {
      {512, 270}, {512, 318}, {512, 364}, {512, 412}, {512, 346}, {512, 456}, {512, 397},
      {512, 510}, {512, 365}, {512, 483}, {512, 412}, {512, 388}, {512, 323},
  };
  for (int i = 1; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    const int lower = autojustify::THRESHOLD_CHOICES[i - 1];
    const int higher = autojustify::THRESHOLD_CHOICES[i];
    bool someFaceChanges = false;
    for (const auto& p : kSweep) {
      if (autojustify::shouldJustify(p.measurePx, p.alphabetPx, lower) !=
          autojustify::shouldJustify(p.measurePx, p.alphabetPx, higher)) {
        someFaceChanges = true;
        break;
      }
    }
    EXPECT_TRUE(someFaceChanges) << "no shipped face crosses between rungs " << lower << " and " << higher;
  }
}

TEST(AutoJustifyThreshold, RaisingItOnlyEverRemovesJustification) {
  // Monotone in the threshold, which is what makes the row legible: dragging it
  // up can only ever turn justified blocks ragged, never the reverse. An
  // inversion here would be invisible per-page and maddening in use.
  for (int alphabet = 200; alphabet <= 600; alphabet += 3) {
    for (int measure = 200; measure <= 900; measure += 17) {
      bool wasJustified = true;
      for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
        const bool now = autojustify::shouldJustify(measure, alphabet, autojustify::THRESHOLD_CHOICES[i]);
        if (!wasJustified) {
          EXPECT_FALSE(now) << "justification came BACK as the threshold rose: measure " << measure << " alphabet "
                            << alphabet << " rung " << autojustify::THRESHOLD_CHOICES[i];
        }
        wasJustified = now;
      }
    }
  }
}

TEST(AutoJustifyThreshold, AByteOffTheLadderFallsBackToTheDefaultRatherThanSnapping) {
  // A hand-edited settings.json, a byte posted by the web settings API, or a
  // file written when the ladder differed. Snapping to the nearest rung would
  // silently restyle a book to a value nobody chose; the documented default is
  // at least defensible and is what the row will then display.
  EXPECT_EQ(autojustify::clampThreshold(0), autojustify::THRESHOLD_CHARS);
  EXPECT_EQ(autojustify::clampThreshold(1), autojustify::THRESHOLD_CHARS);
  EXPECT_EQ(autojustify::clampThreshold(33), autojustify::THRESHOLD_CHARS);
  EXPECT_EQ(autojustify::clampThreshold(44), autojustify::THRESHOLD_CHARS);
  EXPECT_EQ(autojustify::clampThreshold(255), autojustify::THRESHOLD_CHARS);
  EXPECT_EQ(autojustify::clampThreshold(-7), autojustify::THRESHOLD_CHARS);
  // ...and every offered rung survives the round trip unchanged, which is the
  // half that would break if clampThreshold were ever written as a range check.
  for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    EXPECT_EQ(autojustify::clampThreshold(autojustify::THRESHOLD_CHOICES[i]), autojustify::THRESHOLD_CHOICES[i]);
  }
}

TEST(AutoJustifyThreshold, TheReportedFlipMeasureTracksTheThresholdExactly) {
  // narrowestJustifiedMeasurePx is what the doc's worked examples quote. It
  // rounds the same way the predicate does, and that agreement has to hold at
  // every rung -- an off-by-a-rounding here would put a wrong number in the
  // documentation forever, which is exactly what the naive inverse did.
  for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    const int threshold = autojustify::THRESHOLD_CHOICES[i];
    for (int alphabet = 200; alphabet <= 600; alphabet += 1) {
      const int flip = autojustify::narrowestJustifiedMeasurePx(alphabet, threshold);
      ASSERT_GT(flip, 0);
      EXPECT_TRUE(autojustify::shouldJustify(flip, alphabet, threshold))
          << "the reported flip measure does not justify: alphabet " << alphabet << " threshold " << threshold;
      EXPECT_FALSE(autojustify::shouldJustify(flip - 1, alphabet, threshold))
          << "one pixel narrower still justifies: alphabet " << alphabet << " threshold " << threshold;
    }
  }
}

// ---------------------------------------------------------------------------
// THE THRESHOLD REACHES THE THINGS THAT DECIDE WHETHER A PAGE IS REBUILT.
//
// This is the half that made the setting a firmware setting rather than an iOS
// Settings.app row. Moving the threshold moves line BREAKS -- a demoted block
// also stops hyphenating lines already past 70% of the measure -- so a stored
// page laid out at one threshold is wrong at another. Two mechanisms notice:
// the section file's own header comparison (Section.cpp, which reads the byte
// back and rejects a mismatch) and the layout fingerprint a book note is scoped
// by. If the field ever stops reaching either, the setting still writes its
// value, the row still moves, and NOTHING on the page changes -- which is
// precisely the failure the route decision was made to avoid.
// ---------------------------------------------------------------------------

#include "ReaderRenderSpec.h"

TEST(AutoJustifyThreshold, TheSpecDefaultsToTheDocumentedThreshold) {
  const ReaderRenderSpec spec;
  EXPECT_EQ(spec.justifyThresholdChars, autojustify::THRESHOLD_CHARS);
}

TEST(AutoJustifyThreshold, EveryRungGivesADistinctLayoutFingerprint) {
  // Not merely "different from the default" -- ALL FIVE must be mutually
  // distinct, or two settings share a cache and one of them serves the other's
  // pagination. A collision is invisible: the page renders, it is just the
  // wrong page.
  ReaderRenderSpec spec;
  spec.fontId = 7;
  spec.viewportWidth = 512;
  spec.viewportHeight = 700;
  spec.hyphenationEnabled = true;

  uint32_t seen[autojustify::THRESHOLD_CHOICE_COUNT];
  for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    spec.justifyThresholdChars = static_cast<uint8_t>(autojustify::THRESHOLD_CHOICES[i]);
    seen[i] = spec.layoutFingerprint();
  }
  for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
    for (int j = i + 1; j < autojustify::THRESHOLD_CHOICE_COUNT; j++) {
      EXPECT_NE(seen[i], seen[j]) << "rungs " << autojustify::THRESHOLD_CHOICES[i] << " and "
                                  << autojustify::THRESHOLD_CHOICES[j] << " share a layout fingerprint";
    }
  }
}

TEST(AutoJustifyThreshold, TheThresholdDoesNotCollideWithTheOtherPackedFlags) {
  // The fingerprint packs several small fields into one mixed word, and this
  // byte is mixed SEPARATELY for that reason. The trap it avoids: a threshold
  // change that cancels against an imageRendering or paragraphAlignment change
  // and leaves the hash where it started. Sweep the neighbours it could collide
  // with and require the threshold to move the hash in every combination.
  for (uint8_t align = 0; align <= 4; align++) {
    for (uint8_t imageRendering = 0; imageRendering <= 3; imageRendering++) {
      ReaderRenderSpec a;
      a.paragraphAlignment = align;
      a.imageRendering = imageRendering;
      a.justifyThresholdChars = 40;
      ReaderRenderSpec b = a;
      b.justifyThresholdChars = 45;
      EXPECT_NE(a.layoutFingerprint(), b.layoutFingerprint())
          << "threshold cancelled against align " << static_cast<int>(align) << " imageRendering "
          << static_cast<int>(imageRendering);
    }
  }
}
