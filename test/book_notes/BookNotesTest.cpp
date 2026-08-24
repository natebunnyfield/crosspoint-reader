// The book-notes model (lib/Epub/Epub/BookNotes.h).
//
// Every failure here is silent in the same way the notices themselves are: a
// scope that never clears tells the reader his text is ragged at a size where
// it is not, a raise that is not idempotent turns a per-word loop into a write
// storm, and a note that is never cleared between books puts one book's
// complaints on another book's screen. None of that shows up as a crash.
//
// The persistence half (openBook / flush) is deliberately NOT covered: it is the
// only part that touches HalStorage, and it is exercised end to end by the
// headless render in docs/book-notes-and-sparse-ruby-2026-08-23.md.

#include <gtest/gtest.h>

#include <string>

#include "Epub/BookNotes.h"

namespace {

booknotes::Notes& fresh() {
  auto& n = booknotes::current();
  n.resetForTest();
  return n;
}

}  // namespace

TEST(BookNotes, StartsEmptyAndShowsNothing) {
  auto& n = fresh();
  EXPECT_FALSE(n.any());
  EXPECT_EQ(n.count(), 0);
}

TEST(BookNotes, RaiseIsIdempotent) {
  auto& n = fresh();
  for (int i = 0; i < 100; i++) n.raise(booknotes::Note::AlignmentOverridden);
  EXPECT_TRUE(n.has(booknotes::Note::AlignmentOverridden));
  EXPECT_EQ(n.count(), 1);
}

TEST(BookNotes, EveryNoteIsIndependentlyAddressable) {
  // A shared bit would make two different notes the same note, which the screen
  // would show as one, and no other test would notice.
  for (uint8_t i = 0; i < static_cast<uint8_t>(booknotes::Note::_COUNT); i++) {
    auto& n = fresh();
    const auto note = static_cast<booknotes::Note>(i);
    n.raise(note);
    EXPECT_EQ(n.count(), 1) << "note " << static_cast<int>(i);
    for (uint8_t j = 0; j < static_cast<uint8_t>(booknotes::Note::_COUNT); j++) {
      EXPECT_EQ(n.has(static_cast<booknotes::Note>(j)), i == j)
          << "note " << static_cast<int>(i) << " vs " << static_cast<int>(j);
    }
  }
}

TEST(BookNotes, CountsBothScopes) {
  auto& n = fresh();
  n.raise(booknotes::Note::Drm);                  // book scope
  n.raise(booknotes::Note::AlignmentOverridden);  // layout scope
  EXPECT_EQ(n.count(), 2);
}

TEST(BookNotes, ALayoutChangeDropsLayoutNotesAndKeepsBookNotes) {
  auto& n = fresh();
  n.setLayoutFingerprint(0x1111);
  n.raise(booknotes::Note::EmbeddedFontsIgnored);
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 30);
  n.countDroppedImage();
  ASSERT_EQ(n.count(), 3);

  n.setLayoutFingerprint(0x2222);
  EXPECT_TRUE(n.has(booknotes::Note::EmbeddedFontsIgnored));
  EXPECT_FALSE(n.has(booknotes::Note::JustificationDemoted));
  EXPECT_FALSE(n.has(booknotes::Note::ImagesDropped));
  // The figures those notes quoted go with them: a stale character count is a
  // lie with a number in it, which is worse than no note.
  EXPECT_EQ(n.details().narrowestCharsPerLine, 0);
  EXPECT_EQ(n.details().imagesDropped, 0);
}

TEST(BookNotes, TheSameFingerprintChangesNothing) {
  auto& n = fresh();
  n.setLayoutFingerprint(0x1111);
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 31);
  n.setLayoutFingerprint(0x1111);
  EXPECT_TRUE(n.has(booknotes::Note::JustificationDemoted));
  EXPECT_EQ(n.details().narrowestCharsPerLine, 31);
}

TEST(BookNotes, TheNarrowestMeasureIsTheOneKept) {
  auto& n = fresh();
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 38);
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 22);
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 35);
  EXPECT_EQ(n.details().narrowestCharsPerLine, 22);
}

TEST(BookNotes, AnUnmeasurableLineDoesNotOverwriteAKnownOne) {
  // charsPerLine 0 means "could not be measured" upstream; taking it would
  // print "about 0 characters per line".
  auto& n = fresh();
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 30);
  n.raiseWithCharsPerLine(booknotes::Note::JustificationDemoted, 0);
  EXPECT_EQ(n.details().narrowestCharsPerLine, 30);
}

TEST(BookNotes, DroppedImagesAccumulate) {
  auto& n = fresh();
  for (int i = 0; i < 5; i++) n.countDroppedImage();
  EXPECT_TRUE(n.has(booknotes::Note::ImagesDropped));
  EXPECT_EQ(n.details().imagesDropped, 5);
  EXPECT_EQ(n.count(), 1);
}

TEST(BookNotes, DroppedCssRulesAccumulateAndSaturate) {
  auto& n = fresh();
  n.countDroppedCssRules(0);
  EXPECT_FALSE(n.any());  // nothing dropped is not a note
  n.countDroppedCssRules(7);
  n.countDroppedCssRules(3);
  EXPECT_EQ(n.details().cssRulesDropped, 10);
  for (int i = 0; i < 1000; i++) n.countDroppedCssRules(1000);
  EXPECT_EQ(n.details().cssRulesDropped, UINT16_MAX);  // wraps to a small number otherwise
}

// A second CSS parse of the SAME stylesheet must report the same figure, not
// twice it. Reproduced on 2026-08-24 against the real firmware in the
// simulator: wingspan-the-whole-bird.epub read 7 dropped rules on its first
// parse and 14 on the next CSS cache rebuild, with the count persisted to
// notes.bin, so the drift was permanent and compounding. openBook() loads the
// previous pass's total and countDroppedCssRules ACCUMULATES, so the second
// pass was counting on top of the first. The trigger is a CSS cache rebuild --
// which a CSS_CACHE_VERSION bump performs for every book on every card at once.
TEST(BookNotes, ASecondCssParseReplacesTheDroppedRuleCountRatherThanAddingToIt) {
  auto& n = fresh();

  // Pass one, as a cold open would run it.
  n.countDroppedCssRules(5);
  n.countDroppedCssRules(2);
  EXPECT_EQ(n.details().cssRulesDropped, 7);
  EXPECT_TRUE(n.has(booknotes::Note::StylesheetPartlyUnderstood));

  // Pass two, on the same stylesheet -- the state here is what openBook() would
  // have loaded off the card.
  n.beginCssParse();
  EXPECT_EQ(n.details().cssRulesDropped, 0) << "the figure the last pass produced has to go before this one derives it";
  EXPECT_FALSE(n.has(booknotes::Note::StylesheetPartlyUnderstood))
      << "and so does the note it raised, or the screen claims a parse that has been superseded";

  n.countDroppedCssRules(5);
  n.countDroppedCssRules(2);
  EXPECT_EQ(n.details().cssRulesDropped, 7) << "measured 14 before the fix";
  EXPECT_TRUE(n.has(booknotes::Note::StylesheetPartlyUnderstood));
}

TEST(BookNotes, BeginningACssParseLeavesEveryNonCssFigureAlone) {
  // The reset is scoped to what a CSS pass re-derives. A pass that also cleared
  // the book's other notes would silently drop them: nothing re-raises a DRM
  // flag or an image count during a stylesheet parse, and both are persisted,
  // so they would be gone from the card as well as from the screen.
  auto& n = fresh();
  n.raise(booknotes::Note::Drm);
  n.raise(booknotes::Note::NoTableOfContents);
  n.raise(booknotes::Note::AlignmentOverridden);
  for (int i = 0; i < 3; i++) n.countDroppedImage();
  n.raiseUnsupportedCssUnit("vh");
  n.countDroppedCssRules(4);

  n.beginCssParse();

  EXPECT_TRUE(n.has(booknotes::Note::Drm));
  EXPECT_TRUE(n.has(booknotes::Note::NoTableOfContents));
  EXPECT_TRUE(n.has(booknotes::Note::AlignmentOverridden));
  EXPECT_EQ(n.details().imagesDropped, 3);
  // The unconvertible UNIT is raised from the same parse, but it is a first-one-
  // wins NAME rather than a tally, so re-deriving it is a no-op and clearing it
  // would only make the reader lose the name on a rebuild.
  EXPECT_TRUE(n.has(booknotes::Note::CssUnitsUnsupported));
  EXPECT_STREQ(n.details().unsupportedCssUnit, "vh");
  // Only the two CSS-pass notes and the tally moved.
  EXPECT_EQ(n.details().cssRulesDropped, 0);
  EXPECT_FALSE(n.has(booknotes::Note::StylesheetPartlyUnderstood));
}

TEST(BookNotes, BeginningACssParseClearsAStylesheetThatWasSkippedEntirely) {
  // StylesheetSkipped is raised by the heap guard and the rule cap, both inside
  // the same pass, so it goes with the rest. A device that skipped a stylesheet
  // once and had the heap for it on the next parse would otherwise keep telling
  // the reader his book is unstyled.
  auto& n = fresh();
  n.raise(booknotes::Note::StylesheetSkipped);
  n.beginCssParse();
  EXPECT_FALSE(n.has(booknotes::Note::StylesheetSkipped));
}

TEST(BookNotes, ExactlyTheLayoutScopedNotesAreDroppedByAFingerprintChange) {
  // isLayoutScope is an explicit list as of 2026-08-23, not a >= against the
  // first layout note -- the ordering version made a BOOK-scope note appended
  // to the end silently layout-scope, which is a note that deletes itself the
  // first time the reader changes font. This walks the whole enum and checks
  // each note against the predicate rather than against a boundary value, so a
  // new note is covered the day it is added.
  auto& n = fresh();
  n.setLayoutFingerprint(1);
  uint8_t expectedSurvivors = 0;
  for (uint8_t i = 0; i < static_cast<uint8_t>(booknotes::Note::_COUNT); i++) {
    const auto note = static_cast<booknotes::Note>(i);
    n.raise(note);
    if (!booknotes::isLayoutScope(note)) expectedSurvivors++;
  }
  EXPECT_EQ(n.count(), static_cast<uint8_t>(booknotes::Note::_COUNT));
  EXPECT_GT(expectedSurvivors, 0);  // a run where the predicate said "everything is layout" would pass vacuously

  n.setLayoutFingerprint(2);
  EXPECT_EQ(n.count(), expectedSurvivors);
  for (uint8_t i = 0; i < static_cast<uint8_t>(booknotes::Note::_COUNT); i++) {
    const auto note = static_cast<booknotes::Note>(i);
    EXPECT_EQ(n.has(note), !booknotes::isLayoutScope(note)) << "note " << static_cast<int>(i);
  }
}

TEST(BookNotes, TheUnsupportedEncodingKeepsTheFirstNameAndSurvivesAFontChange) {
  auto& n = fresh();
  n.raiseUnsupportedEncoding("shift_jis");
  n.raiseUnsupportedEncoding("euc-jp");  // a later chapter; the reader stopped at the first
  EXPECT_TRUE(n.has(booknotes::Note::TextEncodingUnsupported));
  EXPECT_STREQ(n.details().unsupportedEncoding, "shift_jis");

  // Book scope: which encoding a file is written in has nothing to do with the
  // measure, so changing the reading font must not retire it.
  n.setLayoutFingerprint(7);
  EXPECT_TRUE(n.has(booknotes::Note::TextEncodingUnsupported));
  EXPECT_STREQ(n.details().unsupportedEncoding, "shift_jis");
}

TEST(BookNotes, AnEncodingNameLongerThanTheFieldIsTruncatedAndTerminated) {
  auto& n = fresh();
  n.raiseUnsupportedEncoding("x-mac-central-european-roman-and-then-some-more");
  const auto& name = n.details().unsupportedEncoding;
  EXPECT_EQ(name[sizeof(n.details().unsupportedEncoding) - 1], '\0');
  EXPECT_EQ(std::string(name), std::string("x-mac-central-european-roman-and-then-some-more").substr(0, 23));
}

TEST(BookNotes, AnEmptyEncodingNameStillRaisesTheNote) {
  // Expat can hand the handler an empty or null name for a malformed
  // declaration. The book still cannot be decoded, so the note must appear --
  // it simply cannot say which encoding.
  auto& n = fresh();
  n.raiseUnsupportedEncoding("");
  EXPECT_TRUE(n.has(booknotes::Note::TextEncodingUnsupported));
  n.raiseUnsupportedEncoding(nullptr);
  EXPECT_TRUE(n.has(booknotes::Note::TextEncodingUnsupported));
}

TEST(BookNotes, MissingCodepointsAreLayoutScopedAndZeroIsNotANote) {
  auto& n = fresh();
  n.setMissingCodepoints(0);
  EXPECT_FALSE(n.any());  // a font that covers the book has nothing to report

  n.setMissingCodepoints(4);
  EXPECT_TRUE(n.has(booknotes::Note::MissingGlyphs));
  EXPECT_EQ(n.details().missingCodepoints, 4);
  n.setMissingCodepoints(11);  // the ledger's running total, not an increment
  EXPECT_EQ(n.details().missingCodepoints, 11);

  // A different font has different holes: the count and the note both go.
  n.setLayoutFingerprint(3);
  EXPECT_FALSE(n.has(booknotes::Note::MissingGlyphs));
  EXPECT_EQ(n.details().missingCodepoints, 0);
}

TEST(BookNotes, ClearingTheNotesAlsoClearsTheGlyphLedgerTheyReportFrom) {
  // Opening a second book must not inherit the first book's missing characters.
  // The ledger lives in lib/EpdFont and its lifetime is owned here, so this is
  // the only assertion that connects the two.
  auto& n = fresh();
  auto& ledger = missingglyphs::current();
  ledger.arm();
  ledger.note(0x4E00);
  ledger.note(0x3042);
  EXPECT_EQ(ledger.distinct(), 2);

  n.resetForTest();  // the inline clear(); openBook() is the storage-touching half
  EXPECT_EQ(ledger.distinct(), 0);
  ledger.disarm();
}
