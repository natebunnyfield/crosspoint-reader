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
  EXPECT_TRUE(linebreak::splitsWordsAtLineEnds(linebreak::STORED_DEFAULT));
  EXPECT_FALSE(linebreak::usesTotalFit(linebreak::STORED_DEFAULT));
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
  // Exhaustive over the whole domain of the stored field, so a third mode
  // appended later cannot leave a byte routing to neither breaker (a paragraph
  // with no line breaks at all) or to both.
  for (int v = 0; v <= 255; ++v) {
    const auto mode = linebreak::modeFor(static_cast<uint8_t>(v));
    const bool greedy = linebreak::splitsWordsAtLineEnds(mode);
    const bool totalFit = linebreak::usesTotalFit(mode);
    EXPECT_NE(greedy, totalFit) << "byte " << v << " routes to " << (greedy ? "both" : "neither") << " breaker";
  }
}

// ---------------------------------------------------------------------------
// 4. An unrecognized byte falls to the DEFAULT, not to 0
// ---------------------------------------------------------------------------

TEST(LineBreakMode, AnUnknownByteFallsToTheShippedDefault) {
  // A `stored != STORED_HYPHENATED` test would send every one of these to the
  // total-fit breaker, re-breaking every paragraph in the book off a byte
  // nobody chose. The failure would present as a rendering bug.
  for (const uint8_t v : {uint8_t{2}, uint8_t{3}, uint8_t{17}, uint8_t{200}, uint8_t{255}}) {
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
