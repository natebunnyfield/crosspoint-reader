// Picker display order, kept separate from the value a setting stores.
//
// THE BUG THIS EXISTS FOR
//
// An ENUM setting persists the row INDEX, so its value list is append-only: a
// new choice goes on the end whatever it means, because moving one would
// re-point every saved settings.json at something else. Three pickers were
// therefore ordered by when each choice was added rather than by anything a
// reader would recognise:
//
//   Typing Redraw Delay   25, 50, 100, 250, 500, 1000, 0 ms   <- zero last
//   Sleep Screen          ... Cover, Cover Custom, None, Quick Resume ...
//   Editor Font           three card-only faces, then the two compiled in
//
// settingorder maps display position -> stored index, so the presentation can
// be fixed while every stored value stays exactly where it is.
//
// What matters, and what these tests pin:
//   1. the stored value survives a round trip through the picker, and
//   2. a bad or stale order table degrades to identity, never hiding a choice.
//
// WHAT THIS FILE DOES NOT COVER, since it has been read as covering it: the
// order of ROWS on a settings SCREEN. Everything here is about the order of
// CHOICES inside one row's picker. Nothing in test/ pins which rows the device
// Settings list shows or what order they come in -- when Line Grid, Line
// Spacing and Justified Text moved to the Typography screen on 2026-08-24, not
// one assertion in this suite changed. That order is verified by rendering the
// screen; see docs/ligature-control.md.
//
// The editor-font case calls the REAL editorfonts::displayOrder() against the
// REAL table, so it cannot drift from what ships. The debounce and sleep-screen
// tables are restated here: both live inside CrossPointSettings, which drags in
// PersistableStore, ArduinoJson and the SD layer. They are asserted as the
// SHAPE the picker must have, not as a copy of a constant.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "FontDisplayNames.h"
#include "ReadingFontList.h"
#include "activities/settings/SettingDisplayOrder.h"
#include "notes/EditorFonts.h"

namespace {

// What the picker does end to end: open on the stored value, pick the row at
// `position`, store what that row maps back to.
uint8_t pickRow(const std::vector<uint8_t>& order, size_t count, uint8_t stored, size_t position) {
  const std::vector<uint8_t> resolved = settingorder::resolve(order, count);
  EXPECT_EQ(resolved[settingorder::positionOf(order, count, stored)], stored)
      << "the picker must open on the row that displays the stored value";
  return resolved[position];
}

// CrossPointSettings::DISPLAY_DEBOUNCE_MS. 0 is LAST because prepending it
// would have shifted every stored index by one.
const std::vector<std::string> kDebounceLabels = {"25 ms", "50 ms", "100 ms", "250 ms", "500 ms", "1000 ms", "0 ms"};
const std::vector<uint8_t> kDebounceOrder = {6, 0, 1, 2, 3, 4, 5};

}  // namespace

TEST(SettingDisplayOrder, DebounceReadsAscendingWithZeroFirst) {
  const std::vector<std::string> want = {"0 ms", "25 ms", "50 ms", "100 ms", "250 ms", "500 ms", "1000 ms"};
  EXPECT_EQ(settingorder::reorder(kDebounceOrder, kDebounceLabels), want);
}

TEST(SettingDisplayOrder, DebounceStoredValuesDoNotMove) {
  // 0 ms is still stored as 6, so a settings.json written before the reorder
  // still means the delay it meant then.
  EXPECT_EQ(pickRow(kDebounceOrder, kDebounceLabels.size(), /*stored=*/3, /*position=*/0), 6);
  EXPECT_EQ(pickRow(kDebounceOrder, kDebounceLabels.size(), /*stored=*/6, /*position=*/6), 5);
}

TEST(SettingDisplayOrder, DebounceRoundTripsEveryValue) {
  const auto shown = settingorder::reorder(kDebounceOrder, kDebounceLabels);
  for (uint8_t stored = 0; stored < kDebounceLabels.size(); stored++) {
    const size_t pos = settingorder::positionOf(kDebounceOrder, kDebounceLabels.size(), stored);
    EXPECT_EQ(pickRow(kDebounceOrder, kDebounceLabels.size(), stored, pos), stored);
    EXPECT_EQ(shown[pos], kDebounceLabels[stored]) << "the row a value opens on must carry that value's label";
  }
}

TEST(SettingDisplayOrder, SleepScreenPutsNoneFirstAndKeepsCalendarsTogether) {
  // Labels by CrossPointSettings::SLEEP_SCREEN_MODE value.
  const std::vector<std::string> labels = {"Dark",          "Light",         "Custom",       "Cover",
                                           "Cover Custom",  "None",          "Quick Resume", "Calendar",
                                           "Calendar Four", "Calendar Five", "Calendar Six", "Westside"};
  const std::vector<uint8_t> order = {5, 0, 1, 2, 3, 4, 6, 7, 8, 9, 10, 11};
  const auto shown = settingorder::reorder(order, labels);

  EXPECT_EQ(shown.front(), "None") << "None is this row's off switch; it belongs first";
  EXPECT_EQ(shown[1], "Dark");
  EXPECT_EQ(shown[2], "Light");
  EXPECT_EQ(shown.back(), "Westside") << "the calendars stay one contiguous block at the end";
  EXPECT_EQ(shown.size(), labels.size()) << "no mode may be dropped";

  // BLANK is 5 on disk and stays 5 after being drawn first.
  EXPECT_EQ(pickRow(order, labels.size(), /*stored=*/0, /*position=*/0), 5);
}

// Named for the reading picker, which was called "Text Settings" until
// 2026-08-24 and is called READER FONT now (owner: "rename Text Settings to
// Reader Font"). Only the visible string moved -- StrId::STR_TEXT_SETTINGS,
// the screen and this sort rule are all unchanged -- but a test named after a
// row nobody can find is a test nobody trusts.
TEST(SettingDisplayOrder, EditorFontSortsLikeReaderFont) {
  // Owner ruling 2026-08-09: the Editor Font list presents and sorts exactly
  // like the reading picker -- reverse chronological by each family's EARLIEST
  // (creation) year from FontDisplayNames, ties broken by the row's own title.
  // The previous compiled-in-faces-first grouping is gone on purpose, and this
  // test replaces the one that pinned it.
  const auto order = editorfonts::displayOrder();
  ASSERT_EQ(order.size(), editorfonts::FAMILY_COUNT) << "every face must appear exactly once";
  EXPECT_TRUE(settingorder::isPermutation(order, editorfonts::FAMILY_COUNT));

  // The property, asserted the same way FontSelectionActivity.cpp:136-141
  // states it.
  for (size_t i = 1; i < order.size(); i++) {
    const auto& prev = editorfonts::FAMILIES[order[i - 1]];
    const auto& cur = editorfonts::FAMILIES[order[i]];
    const uint16_t yPrev = FontDisplayNames::earliestYear(prev.family);
    const uint16_t yCur = FontDisplayNames::earliestYear(cur.family);
    EXPECT_GE(yPrev, yCur) << "newer lineage must come first: " << prev.family << " before " << cur.family;
    if (yPrev == yCur) {
      EXPECT_LT(std::string(prev.label), std::string(cur.label)) << "a year tie must fall back to the row title";
    }
  }

  // Every editor face must be DATED, or it sorts to the bottom as an unknown
  // and the parity is a lie for that row. This is what a new face added to
  // FAMILIES without a FontDisplayNames entry would trip.
  for (size_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    EXPECT_GT(FontDisplayNames::earliestYear(editorfonts::FAMILIES[i].family), 0)
        << editorfonts::FAMILIES[i].family << " has no colophon entry, so it cannot sort by lineage";
  }

  // The tie-break reads FAMILIES[].label because that is the string the row
  // DRAWS. It agrees with FontDisplayNames::displayName today; pin that, since
  // a drift would sort the list by something invisible.
  for (size_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    EXPECT_EQ(FontDisplayNames::displayName(editorfonts::FAMILIES[i].family),
              std::string(editorfonts::FAMILIES[i].label))
        << "picker label and colophon display name must not drift: " << editorfonts::FAMILIES[i].family;
  }

  // And the concrete shipped order, so a colophon-data edit that silently
  // reshuffles the picker trips something. Reverse-chronological by earliest
  // year, ties by the drawn label:
  //   iA Writer Quattro: 2017 (the year IBM Plex Mono was drawn, which Quattro
  //   is built on — 2018 is when iA shipped it, this table dates the WORK).
  //   PragmataPro: 2010. Nitti Typewriter: 2007 (first year in its copyright
  //   string; 2009 was the year of Nitti, not Nitti Typewriter). Three families,
  //   no ties, straight descending. FAMILIES[0,1,2] = Quattro, Pragma, Nitti.
  const std::vector<uint8_t> want = {0, 1, 2};
  EXPECT_EQ(order, want) << "iA Writer Quattro (2017), PragmataPro (2010), Nitti Typewriter (2007)";
}

TEST(SettingDisplayOrder, EditorFontStoredIndicesDoNotMove) {
  const auto order = editorfonts::displayOrder();
  for (uint8_t stored = 0; stored < editorfonts::FAMILY_COUNT; stored++) {
    const size_t pos = settingorder::positionOf(order, editorfonts::FAMILY_COUNT, stored);
    EXPECT_EQ(pickRow(order, editorfonts::FAMILY_COUNT, stored, pos), stored);
  }
}

TEST(SettingDisplayOrder, EditorFamiliesAreRecognisedByName) {
  for (size_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
    EXPECT_TRUE(editorfonts::isEditorFamily(editorfonts::FAMILIES[i].family))
        << editorfonts::FAMILIES[i].family << " must be recognised as an editor face";
    // What the READING picker actually asks. Every row is writing-only since
    // 2026-08-21, when the owner withdrew Quattro's 2026-08-09 alsoReading
    // exception -- so a card carrying any of them does not grow phantom reading
    // families.
    const bool writingOnly = editorfonts::isWritingOnlyFamily(editorfonts::FAMILIES[i].family);
    EXPECT_EQ(writingOnly, !editorfonts::FAMILIES[i].alsoReading)
        << editorfonts::FAMILIES[i].family << ": reading-picker visibility must follow alsoReading";
  }
  // Case-insensitive: the name comes from a directory on a FAT card.
  EXPECT_TRUE(editorfonts::isEditorFamily("iawriterquattro"));
  EXPECT_TRUE(editorfonts::isWritingOnlyFamily("iawriterquattro"))
      << "Quattro left the reading list on 2026-08-21 (owner: 'remove ia quattro from reading fonts')";
  EXPECT_TRUE(editorfonts::isWritingOnlyFamily("iAWriterDuo")) << "Duo stays writing-only";
  EXPECT_FALSE(editorfonts::isEditorFamily("IAWRITERMONO"))
      << "Mono was removed from FAMILIES; isEditorFamily returns false for former faces";

  // Reading families and junk must NOT be filtered.
  for (const char* reading :
       {"Coelacanth", "TeXGyreSchola", "LibreFranklin", "TeXGyreHeros", "Edgar", "", "iAWriter"}) {
    EXPECT_FALSE(editorfonts::isEditorFamily(reading)) << reading << " is not an editor face";
  }
  EXPECT_FALSE(editorfonts::isEditorFamily(nullptr));
}

TEST(SettingDisplayOrder, ABadTableNeverCostsAChoice) {
  const size_t count = kDebounceLabels.size();
  const std::vector<std::pair<const char*, std::vector<uint8_t>>> bad = {
      {"never filled in", {}},
      {"stale: a choice was appended, the table was not", {6, 0, 1, 2, 3, 4}},
      {"too long", {6, 0, 1, 2, 3, 4, 5, 5}},
      {"out of range", {6, 0, 1, 2, 3, 4, 9}},
      {"duplicate, which would hide index 5", {6, 0, 1, 2, 3, 4, 6}},
  };
  for (const auto& [why, order] : bad) {
    SCOPED_TRACE(why);
    // Identity fallback: every label still present, in the stored order.
    EXPECT_EQ(settingorder::reorder(order, kDebounceLabels), kDebounceLabels);
    for (uint8_t stored = 0; stored < count; stored++) {
      const size_t pos = settingorder::positionOf(order, count, stored);
      EXPECT_EQ(pickRow(order, count, stored, pos), stored);
    }
  }
}

TEST(SettingDisplayOrder, PermutationCheck) {
  EXPECT_TRUE(settingorder::isPermutation({0, 1, 2}, 3));
  EXPECT_TRUE(settingorder::isPermutation({2, 0, 1}, 3));
  EXPECT_TRUE(settingorder::isPermutation({}, 0)) << "an empty row is trivially ordered";
  EXPECT_FALSE(settingorder::isPermutation({0, 1}, 3)) << "short";
  EXPECT_FALSE(settingorder::isPermutation({0, 1, 1}, 3)) << "duplicate";
  EXPECT_FALSE(settingorder::isPermutation({0, 1, 3}, 3)) << "out of range";
}

// ============================================================================
// Subset orders (2026-08-21): a row may deliberately WITHDRAW choices.
// The permutation gate stays the default so an accidentally short table still
// degrades to identity; only subset=true honors a shorter list.
// ============================================================================

TEST(SettingDisplayOrder, SubsetIsHonoredOnlyWhenAskedFor) {
  const std::vector<uint8_t> nine = {5, 0, 1, 2, 3, 4, 6, 7, 11};
  // Default (permutation) mode: 9 of 12 is a stale table -> identity.
  EXPECT_EQ(settingorder::resolve(nine, 12).size(), 12u);
  // Subset mode: honored as-is.
  const auto r = settingorder::resolve(nine, 12, /*subset=*/true);
  EXPECT_EQ(r, nine);
}

TEST(SettingDisplayOrder, SubsetStillRejectsDuplicatesAndRange) {
  EXPECT_EQ(settingorder::resolve({1, 1}, 12, true).size(), 12u);  // dup -> identity
  EXPECT_EQ(settingorder::resolve({12}, 12, true).size(), 12u);    // range -> identity
  EXPECT_EQ(settingorder::resolve({}, 12, true).size(), 12u);      // empty -> identity
}

// EARLIEST ORIGIN, added 2026-09-06 with FontDisplayNames::Entry::origin.
//
// The reader picker draws `origin` as the FIRST line of every colophon, so a
// row that gets it wrong is wrong in the most visible place on the screen, and
// two of the three ways it can go wrong are silent:
//
//   * a new family added with no origin draws a blank first line and a stray
//     blank after it -- nothing errors, the colophon just starts two lines
//     down;
//   * an origin later than the face's own first lineage stage is a lineage
//     running BACKWARDS, which no reader would parse as an error, only as a
//     confusing date;
//   * `earliestYear` quietly following `origin` instead of `lineage` would
//     reorder both this picker and the in-book font cycle
//     (readingfonts::sortsBefore), which is the consequence the field's own
//     comment says was deliberately NOT taken.
//
// Comments were doing this job and comments are not a gate.
TEST(SettingDisplayOrder, EveryFamilyCarriesAnEarliestOrigin) {
  // First run of digits in a "YEAR PLACE" stage. "c. 1450 Mainz" -> 1450.
  const auto firstYear = [](const char* s) {
    for (const char* p = s; p != nullptr && *p != '\0'; ++p) {
      if (*p >= '0' && *p <= '9') return std::atoi(p);
    }
    return 0;
  };

  // The rows whose origin is NOT their own first lineage stage, and every one
  // of them is the same fact: a 20th-century revival of a 15th-century
  // Venetian roman. Coelacanth and Venetian 301 are both Bruce Rogers' Centaur
  // (docs/font-dates.md, Excluded dates: "the 1914 design is the model year for
  // both"); Doves Type "was cut after Jenson" in the same table's own words.
  // Listed EXHAUSTIVELY so a fourth divergence has to be added here on purpose
  // -- an origin that silently disagrees with its lineage is exactly the kind
  // of attribution this table exists to prevent.
  //
  // DanteMT joined them 2026-09-07: its origin became Griffo's 1501 Venice
  // (owner ruling), so it too reaches back past its own 1954 first stage. Note
  // it is NOT the same fact as the other three -- they are Jenson, this is
  // Griffo, and the sources call Dante "influenced by (but not directly
  // indebted to)" his types. Both the header row and docs/font-dates.md record
  // it as a ruling rather than a citation.
  const std::vector<std::string> kOriginDiffersFromFirstStage = {"Coelacanth", "DanteMT", "Doves",
                                                                 "Venetian301"};

  std::vector<std::string> diverged;
  for (const auto& e : FontDisplayNames::kEntries) {
    ASSERT_NE(e.origin, nullptr) << e.directory << " has no earliest origin";
    EXPECT_NE(std::string(e.origin), "") << e.directory << " has an empty earliest origin";

    const int originYear = firstYear(e.origin);
    const int lineageYear = firstYear(e.lineage);
    EXPECT_GT(originYear, 0) << e.directory << ": origin carries no year -- \"" << e.origin << "\"";
    EXPECT_LE(originYear, lineageYear) << e.directory << ": an origin cannot post-date the face's own first stage ("
                                       << e.origin << " vs " << e.lineage << ")";

    // `earliestYear` STILL FOLLOWS `lineage`, and that is now the SECONDARY
    // sort key rather than the only one. The 2026-09-06 ruling that kept the
    // order on stage 1 was superseded the same day, after the origin line
    // shipped and the list visibly disagreed with itself -- Coelacanth showing
    // "c. 1470 Venice" four rows above Inknut's "1469 Venice". The order is
    // now origin, then stage 1, then stage 2 (readingfonts::sortsBefore), and
    // this field is the middle key. It must still mirror the lineage, or the
    // second key would sort by something no one can see.
    EXPECT_EQ(static_cast<int>(e.earliestYear), lineageYear)
        << e.directory << ": earliestYear must track the LINEAGE's first stage, not the origin";

    if (originYear != lineageYear) diverged.push_back(e.directory);
  }
  EXPECT_EQ(diverged, kOriginDiffersFromFirstStage);
}

// THE THREE-KEY ORDER, owner ruling 2026-09-06: "sort by origin year,
// secondary sort by next year, tertiary by next year."
//
// Pinned because the comparator is shared with the IN-BOOK font cycle, so a
// change here silently changes what the next-family gesture does in a book --
// a place no picker screenshot would ever show. The Coelacanth/Doves pair is
// the case that proves the secondary key is wired: both originate c. 1470, and
// only stage 1 (1914 against 1900) separates them.
TEST(SettingDisplayOrder, PickerSortsByOriginThenStages) {
  // The installed set, 2026-09-07: DTLRomulus and GoldenCockerel were cut
  // ("Drop DTL Romulus and Golden Cockerel entirely") and are on no surface;
  // VandenKeere was added the same day and is the thirteenth.
  std::vector<std::string> fams = {"Edgar",       "Coelacanth",     "TeXGyreSchola", "LibreFranklin",
                                   "LibrisADF",   "InknutJunicode", "TeXGyreHeros",  "Almendra",
                                   "DanteMT",     "LutetiaNova",    "Doves",         "WarblerText",
                                   "VandenKeere"};
  std::stable_sort(fams.begin(), fams.end(), [](const std::string& a, const std::string& b) {
    return readingfonts::sortsBefore(a.c_str(), b.c_str());
  });

  // Dante sits SEVENTH, not second: its origin became Griffo's 1501 Venice on
  // 2026-09-07, so it sorts with the Venetians rather than with its own 1954.
  // VandenKeere slots between Edgar (1722) and Dante (1501): its origin is
  // van den Keere's 1575 Parangon roman, its own first lineage stage.
  const std::vector<std::string> want = {"TeXGyreHeros",  "LibrisADF",   "LutetiaNova",  "TeXGyreSchola",
                                         "LibreFranklin", "WarblerText", "Edgar",        "VandenKeere",
                                         "DanteMT",       "Coelacanth",  "Doves",        "InknutJunicode",
                                         "Almendra"};
  EXPECT_EQ(fams, want);

  // The two Venetians tie on origin and are separated by stage 1 alone.
  EXPECT_TRUE(readingfonts::sortsBefore("Coelacanth", "Doves"));
  EXPECT_FALSE(readingfonts::sortsBefore("Doves", "Coelacanth"));

  // And the ordering the ruling was made to fix: Coelacanth now sits ABOVE
  // Inknut, because c. 1470 is later than 1469 -- it read backwards before.
  EXPECT_TRUE(readingfonts::sortsBefore("Coelacanth", "InknutJunicode"));

  // An unlisted face has no date and sorts last, at every level.
  EXPECT_TRUE(readingfonts::sortsBefore("Almendra", "NoSuchFamily"));
}

TEST(SettingDisplayOrder, WithdrawnValueStillDecodesToPositionZero) {
  // A save holding CALENDAR_FIVE (9) with the trimmed sleep-screen subset:
  // not listed, so the picker highlights position 0 rather than crashing or
  // hiding the popup. The stored byte itself is remapped on load elsewhere.
  const std::vector<uint8_t> nine = {5, 0, 1, 2, 3, 4, 6, 7, 11};
  EXPECT_EQ(settingorder::positionOf(nine, 12, 9, true), 0u);
  EXPECT_EQ(settingorder::positionOf(nine, 12, 11, true), 8u);  // WESTSIDE at tail
}

TEST(SettingDisplayOrder, ReorderWithSubsetReturnsOnlyTheListed) {
  const std::vector<int> vals = {10, 11, 12, 13};
  const auto out = settingorder::reorder({3, 0}, vals, true);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], 13);
  EXPECT_EQ(out[1], 10);
}
