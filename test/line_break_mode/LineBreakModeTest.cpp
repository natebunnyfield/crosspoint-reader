// Which line breaker runs, given the stored setting byte.
//
// THE BUG THIS EXISTS FOR
//
// `hyphenationEnabled` was frozen at 1 from 2026-08-21 to 2026-08-25, so only
// one of the two breakers ever ran and the mapping between the persisted byte
// and the algorithm was unwritten anywhere. Unfreezing it as a row named
// something other than "Hyphenation" is exactly the change that invites someone
// to "tidy" the enum so the nicer label sorts first -- and that would silently
// swap the line breaks in every book on every device that already has a
// settings.json under this key, with no error and no version bump, because the
// section cache would agree the value moved and rebuild happily to the wrong
// mode.
//
// So the assertions here are about BYTES, not about which mode is nicer:
//   1. 1 is Hyphenated and 0 is Whole Words, forever;
//   2. the shipped default is 1, so an existing install renders identically;
//   3. the two modes are exhaustive and exclusive -- exactly one breaker runs;
//   4. an unrecognized byte falls to the DEFAULT, not to 0.
//
// (4) is the one that is easy to get wrong and impossible to see. A corrupt or
// future settings.json returning WholeWords from a `stored != 1` test would
// re-break every paragraph in the book, which looks like a rendering bug rather
// than like a bad byte.

#include <gtest/gtest.h>

#include <cstdint>

#include "AutoJustify.h"
#include "LineBreakMode.h"

namespace {

// ---------------------------------------------------------------------------
// 1. The persisted bytes
// ---------------------------------------------------------------------------

TEST(LineBreakMode, StoredValuesAreTheOnesSettingsJsonAlreadyCarries) {
  // These two are append-only in the strictest sense: settings.json files
  // written before the 2026-08-21 freeze already hold them under the
  // "hyphenationEnabled" key with this meaning. Changing either re-points a
  // saved choice at the other algorithm.
  EXPECT_EQ(linebreak::STORED_WHOLE_WORDS, 0u);
  EXPECT_EQ(linebreak::STORED_HYPHENATED, 1u);
}

TEST(LineBreakMode, TheEnumeratorsMatchTheStoredBytes) {
  // The enum is stored directly as this byte in ReaderRenderSpec and in the
  // section file, so the enumerator values are not free to be anything.
  EXPECT_EQ(static_cast<uint8_t>(linebreak::Mode::WholeWords), linebreak::STORED_WHOLE_WORDS);
  EXPECT_EQ(static_cast<uint8_t>(linebreak::Mode::Hyphenated), linebreak::STORED_HYPHENATED);
}

// ---------------------------------------------------------------------------
// 2. The default is today's shipped behavior
// ---------------------------------------------------------------------------

TEST(LineBreakMode, DefaultIsWhatEveryShippedBuildHasDrawn) {
  // The freeze pinned this at 1 and the row must not change it. A device that
  // has never opened Typography Settings has to lay out identically before and
  // after the unfreeze.
  EXPECT_EQ(linebreak::STORED_DEFAULT, linebreak::STORED_HYPHENATED);
  EXPECT_EQ(linebreak::modeFor(linebreak::STORED_DEFAULT), linebreak::Mode::Hyphenated);
  EXPECT_TRUE(linebreak::splitsWordsAtLineEnds(linebreak::modeFor(linebreak::STORED_DEFAULT)));
  EXPECT_FALSE(linebreak::usesTotalFit(linebreak::modeFor(linebreak::STORED_DEFAULT)));
}

// ---------------------------------------------------------------------------
// 3. Which breaker runs
// ---------------------------------------------------------------------------

TEST(LineBreakMode, HyphenatedRunsTheGreedyBreakerThatSplitsWords) {
  const auto mode = linebreak::modeFor(linebreak::STORED_HYPHENATED);
  EXPECT_EQ(mode, linebreak::Mode::Hyphenated);
  EXPECT_TRUE(linebreak::splitsWordsAtLineEnds(mode));
  EXPECT_FALSE(linebreak::usesTotalFit(mode));
}

TEST(LineBreakMode, WholeWordsRunsTheTotalFitDynamicProgram) {
  const auto mode = linebreak::modeFor(linebreak::STORED_WHOLE_WORDS);
  EXPECT_EQ(mode, linebreak::Mode::WholeWords);
  EXPECT_TRUE(linebreak::usesTotalFit(mode));
  EXPECT_FALSE(linebreak::splitsWordsAtLineEnds(mode));
}

TEST(LineBreakMode, ExactlyOneBreakerRunsForEveryPossibleByte) {
  // Exhaustive over the whole domain of the stored field: no byte may route to
  // neither breaker (a paragraph with no line breaks at all) or to both.
  //
  // Through resolvedMode, because that is now the only total function. The
  // third mode this test was written in anticipation of arrived on 2026-09-11,
  // and Automatic deliberately answers NEITHER predicate on its own -- it is a
  // policy, and asking it "do you split words?" without telling it which block
  // has no answer. Every block shape is swept here so the guarantee still holds
  // where it matters: after resolution.
  for (int v = 0; v <= 255; ++v) {
    for (const bool justified : {false, true}) {
      for (const int chars : {0, 20, 39, 40, 49, 50, 66, 120}) {
        const auto mode = linebreak::resolvedMode(static_cast<uint8_t>(v), justified, chars);
        const bool greedy = linebreak::splitsWordsAtLineEnds(mode);
        const bool totalFit = linebreak::usesTotalFit(mode);
        EXPECT_NE(greedy, totalFit) << "byte " << v << " at " << (justified ? "justified" : "ragged") << " " << chars
                                    << " chars routes to " << (greedy ? "both" : "neither") << " breaker";
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Automatic: hyphens only where NOT hyphenating is the worse harm
// ---------------------------------------------------------------------------
//
// Owner ruling 2026-09-11, and the bar is the clause: "only turns on
// hyphenation automatically when it is helpful for even a dyslexic reader".
// Both costs land on that reader -- a split word has to be rejoined before it
// is recognised, and the white channels justification opens down a short
// paragraph pull the eye off the line -- so the only defensible question is
// which is worse HERE. These cases pin the answer at every boundary.

TEST(AutomaticLineBreaks, ARaggedBlockIsNeverHyphenated) {
  // The whole point, and the sharpest difference from "Allow hyphens", which
  // still hyphenates a ragged block as a rescue against a short line. Ragged
  // spacing is already even: a hyphen buys nothing and costs a stumble.
  for (const int chars : {10, 30, 39, 40, 45, 49, 50, 66, 200}) {
    EXPECT_EQ(linebreak::resolveAutomatic(/*blockIsJustified=*/false, chars), linebreak::Mode::WholeWords)
        << "ragged at " << chars << " chars/line";
  }
}

TEST(AutomaticLineBreaks, AJustifiedLineTooShortForItsGapsIsHyphenated) {
  // The band is [justification threshold, HELPFUL_MAX_CHARS). Its lower edge is
  // where auto-justification stops handing blocks over at all -- below 40 the
  // block is ragged and the case above has it.
  EXPECT_EQ(linebreak::resolveAutomatic(true, 40), linebreak::Mode::Hyphenated);
  EXPECT_EQ(linebreak::resolveAutomatic(true, 45), linebreak::Mode::Hyphenated);
  EXPECT_EQ(linebreak::resolveAutomatic(true, linebreak::HELPFUL_MAX_CHARS - 1), linebreak::Mode::Hyphenated);
}

TEST(AutomaticLineBreaks, AJustifiedLineWithRoomToComposeIsNot) {
  // At the bound the line is inside Butterick's comfortable 45-90, so the gaps
  // absorb their slack invisibly and the hyphen is paying for nothing.
  EXPECT_EQ(linebreak::resolveAutomatic(true, linebreak::HELPFUL_MAX_CHARS), linebreak::Mode::WholeWords);
  EXPECT_EQ(linebreak::resolveAutomatic(true, 66), linebreak::Mode::WholeWords)
      << "Gregory & Poulton measured no disadvantage to justification by here";
  EXPECT_EQ(linebreak::resolveAutomatic(true, 120), linebreak::Mode::WholeWords);
}

TEST(AutomaticLineBreaks, AnUnmeasurableLineDoesNotHyphenate) {
  // The opposite of autojustify's fallback, deliberately. There the fallback
  // preserves what the BOOK asked for; here there is no request to preserve,
  // only a claim that hyphens would help -- and a claim that cannot be checked
  // has not been made.
  EXPECT_EQ(linebreak::resolveAutomatic(true, 0), linebreak::Mode::WholeWords);
  EXPECT_EQ(linebreak::resolveAutomatic(true, -1), linebreak::Mode::WholeWords);
}

TEST(AutomaticLineBreaks, TheBoundSitsInsideTheBandEverySourceAgreesOn) {
  // Above Bringhurst's 38-40 failure zone (and this reader's own default
  // justification threshold, so the band is not empty), and at or below
  // Butterick's 45 comfortable floor plus a margin -- not out at Gregory &
  // Poulton's 66, where justification is merely imperfect rather than harmful.
  EXPECT_GT(linebreak::HELPFUL_MAX_CHARS, autojustify::THRESHOLD_CHARS)
      << "at or below the justification threshold the band is empty and Automatic never hyphenates";
  EXPECT_LE(linebreak::HELPFUL_MAX_CHARS, 55) << "imperfect is not harmful, and does not justify a split word";
}

TEST(AutomaticLineBreaks, TheFixedModesAreUntouchedByAnyBlock) {
  // Automatic must not have leaked into the other two: whatever the block, the
  // stored byte a reader chose is what runs.
  for (const bool justified : {false, true}) {
    for (const int chars : {0, 30, 45, 80}) {
      EXPECT_EQ(linebreak::resolvedMode(linebreak::STORED_HYPHENATED, justified, chars), linebreak::Mode::Hyphenated);
      EXPECT_EQ(linebreak::resolvedMode(linebreak::STORED_WHOLE_WORDS, justified, chars), linebreak::Mode::WholeWords);
    }
  }
}

TEST(AutomaticLineBreaks, TheDefaultIsStillHyphenatedSoNoInstallMoves) {
  // A third choice is an offer, not a migration. Every existing card and every
  // fresh install renders exactly as before until the row is touched.
  EXPECT_EQ(linebreak::STORED_DEFAULT, linebreak::STORED_HYPHENATED);
  EXPECT_NE(linebreak::STORED_DEFAULT, linebreak::STORED_AUTOMATIC);
  EXPECT_EQ(linebreak::STORED_AUTOMATIC, 2) << "0 and 1 are in settings.json files in the wild and may never move";
}

// ---------------------------------------------------------------------------
// 4. An unrecognized byte falls to the DEFAULT, not to 0
// ---------------------------------------------------------------------------

TEST(LineBreakMode, AnUnknownByteFallsToTheShippedDefault) {
  // A `stored != STORED_HYPHENATED` test would send every one of these to the
  // total-fit breaker, re-breaking every paragraph in the book off a byte
  // nobody chose. The failure would present as a rendering bug.
  //
  // 2 IS NO LONGER IN THIS LIST. It became STORED_AUTOMATIC on 2026-09-11, and
  // this case failing on it is exactly what it was written to do -- the domain
  // of the field grew, so the set of bytes that mean nothing shrank. Every byte
  // above the modes is still unknown and still falls to the default.
  for (const uint8_t v : {uint8_t{3}, uint8_t{17}, uint8_t{200}, uint8_t{255}}) {
    EXPECT_EQ(linebreak::modeFor(v), linebreak::modeFor(linebreak::STORED_DEFAULT))
        << "byte " << static_cast<int>(v) << " did not fall to the default";
  }
}

// ---------------------------------------------------------------------------
// 5. The picker's label order is presentation only
// ---------------------------------------------------------------------------

// The Line Breaks row builds its labels INDEXED BY STORED VALUE and then asks
// for a display order that shows the default first.
//
// WHAT THIS DOES NOT DO, said plainly because it reads as if it does: the order
// below is a LOCAL LITERAL, not the row's. This suite is pure -- pulling the
// real SettingInfo would drag CrossPointSettings, PersistableStore, ArduinoJson
// and the SD layer into it -- so editing withDisplayOrder() in SettingsList.h
// does not fail here. What it does catch is the failure that actually loses
// data: a re-pointing of the STORED_* constants, which the tests above pin in
// bytes. Presentation order is recoverable by editing one line; a re-pointed
// stored value silently restyles every book on a device that already has a
// save. The screen itself is verified by rendering it.
TEST(LineBreakMode, ShowingTheDefaultFirstDoesNotChangeWhatEitherChoiceStores) {
  // Mirrors SettingsList.h. See the caveat above: this is a model of that row,
  // not a read of it.
  const uint8_t displayOrder[2] = {linebreak::STORED_HYPHENATED, linebreak::STORED_WHOLE_WORDS};

  // Position 0 is what the reader sees first, and it must be the mode the
  // device already renders.
  EXPECT_EQ(displayOrder[0], linebreak::STORED_DEFAULT);
  EXPECT_EQ(linebreak::modeFor(displayOrder[0]), linebreak::Mode::Hyphenated);
  EXPECT_EQ(linebreak::modeFor(displayOrder[1]), linebreak::Mode::WholeWords);

  // Every stored value is reachable from some display position: an order that
  // dropped one would make a mode unreachable while leaving it storable, which
  // is how a setting becomes a one-way door.
  bool seen[2] = {false, false};
  for (const uint8_t stored : displayOrder) {
    ASSERT_LT(stored, 2u);
    seen[stored] = true;
  }
  EXPECT_TRUE(seen[linebreak::STORED_WHOLE_WORDS]);
  EXPECT_TRUE(seen[linebreak::STORED_HYPHENATED]);
}

}  // namespace
