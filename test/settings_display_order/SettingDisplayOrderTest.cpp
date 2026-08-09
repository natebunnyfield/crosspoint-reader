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
// The editor-font case calls the REAL editorfonts::displayOrder() against the
// REAL table, so it cannot drift from what ships. The debounce and sleep-screen
// tables are restated here: both live inside CrossPointSettings, which drags in
// PersistableStore, ArduinoJson and the SD layer. They are asserted as the
// SHAPE the picker must have, not as a copy of a constant.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "FontDisplayNames.h"
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
  const std::vector<std::string> labels = {"Dark",         "Light",         "Custom",       "Cover",
                                           "Cover Custom", "None",          "Quick Resume", "Calendar",
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

TEST(SettingDisplayOrder, EditorFontSortsLikeTextSettings) {
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
    EXPECT_EQ(FontDisplayNames::displayName(editorfonts::FAMILIES[i].family), std::string(editorfonts::FAMILIES[i].label))
        << "picker label and colophon display name must not drift: " << editorfonts::FAMILIES[i].family;
  }

  // And the concrete shipped order, so a colophon-data edit that silently
  // reshuffles the picker trips something. Reverse-chronological by earliest
  // year, ties by the drawn label:
  //   Quattro 2018; IBM Plex Mono and Duo both 2017 (tie broken byte-wise, so
  //   'IBM' precedes 'iA'); Space Mono 2016; iA Writer Mono 2009, because it
  //   is Nitti -- iA's own announcement calls it "the classic Nitti, designed
  //   by Bold Monday", long predating the Plex-derived pair.
  const std::vector<uint8_t> want = {0, 4, 1, 3, 2};
  EXPECT_EQ(order, want) << "iA Writer Quattro, IBM Plex Mono, iA Writer Duo, Space Mono, iA Writer Mono";
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
        << editorfonts::FAMILIES[i].family << " must be filtered out of the READING picker";
  }
  // Case-insensitive: the name comes from a directory on a FAT card.
  EXPECT_TRUE(editorfonts::isEditorFamily("iawriterquattro"));
  EXPECT_TRUE(editorfonts::isEditorFamily("IAWRITERMONO"));

  // Reading families and junk must NOT be filtered.
  for (const char* reading : {"Coelacanth", "TeXGyreSchola", "LibreFranklin", "Edgar", "", "iAWriter"}) {
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
