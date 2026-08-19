// Column planning for T-012 tables. Everything here is the half that can be
// wrong without being visible: a column one pixel too wide pushes the last
// column off the page, and a ragged table silently files a row's cells under
// the wrong heading.
#include <gtest/gtest.h>

#include "TableColumnLayout.h"

using namespace tablecolumns;

namespace {

// Monospace stand-in: 10px per character, 12px per character when bold. Real
// enough for geometry, and it makes every expectation arithmetic rather than a
// number copied out of a font.
int fakeMeasure(void*, const char* text, const bool bold) {
  int n = 0;
  for (const char* p = text; *p; ++p) n++;
  return n * (bold ? 12 : 10);
}

Cell cell(const char* s) { return Cell{Run{s, false, false}}; }

std::vector<Row> table(const std::vector<std::vector<const char*>>& src) {
  std::vector<Row> rows;
  rows.reserve(src.size());
  for (const auto& r : src) {
    Row row;
    row.reserve(r.size());
    for (const char* c : r) row.push_back(cell(c));
    rows.push_back(std::move(row));
  }
  return rows;
}

constexpr int kSpace = 4;

}  // namespace

TEST(TableColumns, PlansNaturalWidthsWhenEverythingFits) {
  const auto rows = table({{"Ship", "Year"}, {"Beagle", "1831"}, {"Erebus", "1839"}});
  const Plan p = planColumns(rows, 800, kSpace, fakeMeasure, nullptr);
  ASSERT_TRUE(p.usable);
  EXPECT_EQ(p.columnCount, 2u);
  EXPECT_EQ(p.w[0], 6 * 10 + kColumnSlack);  // "Beagle" beats bold "Ship" (4*12=48)
  EXPECT_EQ(p.w[1], 4 * 12 + kColumnSlack);  // bold header "Year" is the widest thing in it
  EXPECT_EQ(p.x[0], 0);
  EXPECT_EQ(p.x[1], p.w[0] + p.gutter);
}

TEST(TableColumns, TheLastColumnEndsInsideTheViewport) {
  const auto rows = table({{"Voyage of the Beagle", "Departed", "Days"},
                           {"Beagle, second survey with FitzRoy", "1831", "1741"},
                           {"Challenger", "1872", "1251"}});
  const int viewport = 472;
  const Plan p = planColumns(rows, viewport, kSpace, fakeMeasure, nullptr);
  ASSERT_TRUE(p.usable);
  const int right = p.x[p.columnCount - 1] + p.w[p.columnCount - 1];
  EXPECT_LE(right, viewport) << "the table overflows the page";
}

TEST(TableColumns, SqueezesTheWidestColumnAndLeavesTheShortOnesAlone) {
  const auto rows = table({{"Voyage", "Departed", "Days"},
                           {"Beagle, second survey with FitzRoy", "1831", "1741"},
                           {"Challenger", "1872", "1251"}});
  const Plan p = planColumns(rows, 472, kSpace, fakeMeasure, nullptr);
  ASSERT_TRUE(p.usable);
  EXPECT_EQ(p.w[1], 8 * 12 + kColumnSlack) << "the year column kept its natural width";
  EXPECT_EQ(p.w[2], 4 * 12 + kColumnSlack) << "the figures column kept its natural width";
  EXPECT_LT(p.w[0], 33 * 10) << "the prose column is the one that gave ground";
}

TEST(TableColumns, RefusesWhenSqueezingWouldGoBelowTheFloor) {
  const auto rows = table({{"A very long heading indeed here", "Another long heading here"},
                           {"and a long cell to go with it now", "and another long cell here"}});
  // 100px viewport: one 12px gutter leaves 88 for two columns whose floor is
  // 48 each. 96 > 88, so there is no arrangement that clears the floor.
  const Plan p = planColumns(rows, 100, kSpace, fakeMeasure, nullptr);
  EXPECT_FALSE(p.usable) << "too narrow for columns; the caller must flatten";

  // And the boundary the other way: 108 available is exactly two floors plus
  // the gutter, so it squeezes rather than refusing.
  EXPECT_TRUE(planColumns(rows, 120, kSpace, fakeMeasure, nullptr).usable);
}

TEST(TableColumns, RefusesARaggedTable) {
  auto rows = table({{"Ship", "Year"}, {"Beagle", "1831"}});
  rows.push_back(Row{cell("Erebus")});  // colspan, or a layout table
  const Plan p = planColumns(rows, 800, kSpace, fakeMeasure, nullptr);
  EXPECT_FALSE(p.usable);
}

TEST(TableColumns, RefusesOneColumnAndTooManyColumns) {
  EXPECT_FALSE(planColumns(table({{"Ship"}, {"Beagle"}}), 800, kSpace, fakeMeasure, nullptr).usable);
  EXPECT_FALSE(planColumns(table({{"a", "b", "c", "d", "e", "f"}, {"1", "2", "3", "4", "5", "6"}}), 800, kSpace,
                           fakeMeasure, nullptr)
                   .usable);
}

TEST(TableColumns, RefusesAHeaderWithNoBody) {
  EXPECT_FALSE(planColumns(table({{"Ship", "Year"}}), 800, kSpace, fakeMeasure, nullptr).usable);
}

TEST(TableColumns, FiguresColumnsAreFlushRightAndProseIsNot) {
  const auto rows = table({{"Ship", "Year", "Days", "Notes"},
                           {"Beagle", "1831", "1,741", "second survey"},
                           {"Erebus", "1839", "1,428", "with Terror"}});
  const Plan p = planColumns(rows, 800, kSpace, fakeMeasure, nullptr);
  ASSERT_TRUE(p.usable);
  EXPECT_FALSE(p.rightAlign[0]);
  EXPECT_TRUE(p.rightAlign[1]);
  EXPECT_TRUE(p.rightAlign[2]) << "thousands separators are still figures";
  EXPECT_FALSE(p.rightAlign[3]);
}

TEST(TableColumns, TheHeaderCellDoesNotDecideNumericness) {
  // "Days" is a word in a column of figures. Judging the header would make
  // every numeric column left-aligned, which is the bug this guards.
  const auto rows = table({{"Days"}, {"1741"}, {"1251"}});
  EXPECT_TRUE(columnIsNumeric(rows, 0));
}

TEST(TableColumns, RunsAreJoinedInOrder) {
  // Qualified: inside a TEST body, `Run` would resolve to testing::Test::Run.
  const Cell c{tablecolumns::Run{"HMS ", false, false}, tablecolumns::Run{"Beagle", false, true}};
  EXPECT_EQ(cellText(c), "HMS Beagle");
}
