// The count behind the "some characters have no shape in this font" book note
// (sweep item #38).
//
// Every failure mode here is a wrong NUMBER on a screen nobody is checking
// against anything, which is why it gets a test rather than a code read:
//   - counting occurrences instead of distinct codepoints makes the figure
//     climb every time a page is repainted;
//   - a ledger that is not armed by default counts the UI's own missing glyphs
//     into the book's total;
//   - a full table that keeps counting reports a number it cannot support;
//   - a reset that misses the table leaves the previous BOOK's characters in
//     this book's count.
#include <gtest/gtest.h>

#include "MissingGlyphLedger.h"

namespace {

missingglyphs::Ledger& fresh() {
  auto& ledger = missingglyphs::current();
  ledger.reset();
  ledger.disarm();
  return ledger;
}

}  // namespace

TEST(MissingGlyphLedger, DisarmedByDefaultAndRecordsNothingWhileDisarmed) {
  auto& ledger = fresh();
  EXPECT_FALSE(ledger.isArmed());
  for (uint32_t cp = 0x4E00; cp < 0x4E10; ++cp) ledger.note(cp);
  EXPECT_EQ(ledger.distinct(), 0);
}

TEST(MissingGlyphLedger, CountsDistinctCodepointsNotOccurrences) {
  auto& ledger = fresh();
  ledger.arm();
  for (int repaint = 0; repaint < 50; ++repaint) {
    ledger.note(0x4E00);
    ledger.note(0x3042);
    ledger.note(0x4E00);
  }
  EXPECT_EQ(ledger.distinct(), 2);
}

TEST(MissingGlyphLedger, ZeroIsNotACodepoint) {
  // utf8NextCodepoint returns 0 at end of string, and a walk that hands that
  // straight through would count a phantom character in every chapter.
  auto& ledger = fresh();
  ledger.arm();
  ledger.note(0);
  EXPECT_EQ(ledger.distinct(), 0);
}

TEST(MissingGlyphLedger, DisarmStopsCountingButKeepsWhatWasCounted) {
  auto& ledger = fresh();
  ledger.arm();
  ledger.note(0x2603);
  ledger.disarm();
  ledger.note(0x1F600);
  EXPECT_EQ(ledger.distinct(), 1);  // the emoji arrived from a UI screen, not the book
}

TEST(MissingGlyphLedger, AccumulatesAcrossArmingsBecauseChaptersArePaginatedOneAtATime) {
  auto& ledger = fresh();
  ledger.arm();
  ledger.note(0x2603);
  ledger.disarm();
  ledger.arm();  // the next chapter
  ledger.note(0x4E00);
  ledger.disarm();
  EXPECT_EQ(ledger.distinct(), 2);
}

TEST(MissingGlyphLedger, SaturatesRatherThanReportingANumberItCannotSupport) {
  auto& ledger = fresh();
  ledger.arm();
  // Well past the table's capacity. The figure must stop climbing and SAY it
  // stopped -- the note's wording is "at least %u", which is only honest if
  // this flag is what makes it a floor.
  for (uint32_t cp = 0x4E00; cp < 0x4E00 + 500; ++cp) ledger.note(cp);
  EXPECT_TRUE(ledger.isSaturated());
  const uint16_t capped = ledger.distinct();
  EXPECT_GT(capped, 0);
  EXPECT_LE(capped, 500);
  for (uint32_t cp = 0x5000; cp < 0x5100; ++cp) ledger.note(cp);
  EXPECT_EQ(ledger.distinct(), capped);  // it does not creep past its own limit
}

TEST(MissingGlyphLedger, ResetClearsTheTableAndNotOnlyTheCount) {
  // The bug this pins: clearing distinctCount but leaving the slots populated
  // would make the SECOND book's identical characters invisible, because they
  // would all be found already present. A book would then report 0 missing
  // characters that the previous book had reported.
  auto& ledger = fresh();
  ledger.arm();
  ledger.note(0x4E00);
  ledger.note(0x3042);
  EXPECT_EQ(ledger.distinct(), 2);

  ledger.reset();
  EXPECT_EQ(ledger.distinct(), 0);
  EXPECT_FALSE(ledger.isSaturated());

  ledger.note(0x4E00);
  ledger.note(0x3042);
  EXPECT_EQ(ledger.distinct(), 2) << "reset left the previous book's codepoints in the table";
}

TEST(MissingGlyphLedger, ResetDoesNotDisarm) {
  // booknotes::Notes::setLayoutFingerprint resets mid-book when the reader
  // changes font. If that also disarmed, the chapter being paginated at that
  // moment would silently stop counting.
  auto& ledger = fresh();
  ledger.arm();
  ledger.reset();
  EXPECT_TRUE(ledger.isArmed());
  ledger.note(0x2603);
  EXPECT_EQ(ledger.distinct(), 1);
  ledger.disarm();
}

TEST(MissingGlyphLedger, CodepointsThatCollideInTheHashAreStillCountedSeparately) {
  // Open addressing with a linear probe. Two codepoints an exact multiple of
  // the table size apart are the pair most likely to land in one slot; if the
  // probe were wrong they would count as one character.
  auto& ledger = fresh();
  ledger.arm();
  ledger.note(0x1000);
  ledger.note(0x1000 + 64);
  ledger.note(0x1000 + 128);
  EXPECT_EQ(ledger.distinct(), 3);
  ledger.disarm();
}
