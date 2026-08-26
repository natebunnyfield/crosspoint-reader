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
  EXPECT_FALSE(p.usable) << "too narrow for columns; the caller rotates instead";

  // 120 was ACCEPTED before the widest-word floor existed, because 48+48+12
  // fits. It is refused now, and it should be: "heading" alone is 72px in this
  // metric, so a 48px column would not wrap it, it would overflow into the
  // neighbouring column. That shipped once and printed five columns on top of
  // each other.
  EXPECT_FALSE(planColumns(rows, 120, kSpace, fakeMeasure, nullptr).usable);
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

TEST(TableColumns, NoColumnIsEverNarrowerThanItsLongestWord) {
  // The overlap bug, stated as an invariant. A word cannot be split by the line
  // breaker, so a column narrower than its longest word does not wrap -- it
  // draws past its own edge and over the next column's text.
  const auto rows = table({{"Expedition", "Commander", "Departed", "Returned", "Crew"},
                           {"Beagle, second survey", "Robert FitzRoy", "December 1831", "October 1836", "74"},
                           {"Challenger", "George Nares", "December 1872", "May 1876", "243"}});

  for (int viewport = 200; viewport <= 900; viewport += 20) {
    const Plan p = planColumns(rows, viewport, kSpace, fakeMeasure, nullptr);
    if (!p.usable) continue;
    for (size_t c = 0; c < p.columnCount; c++) {
      int longestWord = 0;
      for (const auto& row : rows) {
        const std::string text = cellText(row[c]);
        size_t start = 0;
        while (start <= text.size()) {
          const size_t space = text.find(' ', start);
          const std::string word = text.substr(start, space == std::string::npos ? std::string::npos : space - start);
          if (!word.empty()) longestWord = std::max(longestWord, fakeMeasure(nullptr, word.c_str(), false));
          if (space == std::string::npos) break;
          start = space + 1;
        }
      }
      EXPECT_GE(p.w[c], longestWord) << "column " << c << " at viewport " << viewport << " would overflow";
    }
  }
}

// --- columnStartY: the vertical half of the same invariant -------------------
//
// planColumns above makes horizontal overlap impossible. These make VERTICAL
// overlap impossible, which is what shipped: reported 2026-08-23 against a
// Catalan phrasebook whose table <caption>, "English" and "Say it" printed on
// one line with "Catalan" pushed to the next. Every failure mode here is text
// drawn on top of text, which no other test in this tree can see.

TEST(TableColumnStartY, ColumnsShareTheRowTopWhenBothPreconditionsHold) {
  // The ordinary case, and the whole point of the function: column 2 starts
  // where column 0 did, not where column 0 ENDED.
  EXPECT_EQ(columnStartY(120, 168, true, true), 120);
  EXPECT_EQ(columnStartY(120, 120, true, true), 120);
}

TEST(TableColumnStartY, UnclearedRowTopNeverRewinds) {
  // A <caption> is unlaid at the row top: laying it out costs lines the row top
  // was measured without, so rewinding to it prints the columns over it. Carry
  // on below instead.
  EXPECT_EQ(columnStartY(120, 168, false, true), 168);
}

TEST(TableColumnStartY, ARowThatChangedPageNeverRewinds) {
  // Once a cell has overflowed and completed a page, rowTop names a y on a page
  // that is finished. On the NEW page it is just a number, and usually a large
  // one -- rewinding to it would print this column over the spill.
  EXPECT_EQ(columnStartY(400, 20, true, false), 20);
  EXPECT_EQ(columnStartY(400, 20, false, false), 20);
}

TEST(TableColumnStartY, TheAnswerIsNeverAboveTheCursorUnlessTheRowTopIsClear) {
  // The property, swept rather than sampled: the only way this function may
  // return a y ABOVE where the page cursor already stands -- the only way it can
  // put ink over ink -- is with both preconditions satisfied, which is the one
  // case where nothing has been drawn between rowTop and the cursor.
  for (int rowTop = 0; rowTop <= 400; rowTop += 40) {
    for (int cursor = 0; cursor <= 400; cursor += 40) {
      for (const bool clear : {false, true}) {
        for (const bool samePage : {false, true}) {
          const int y = columnStartY(rowTop, cursor, clear, samePage);
          if (y < cursor) {
            EXPECT_TRUE(clear && samePage)
                << "rewound to " << y << " from " << cursor << " with clear=" << clear
                << " samePage=" << samePage;
            EXPECT_EQ(y, rowTop);
          }
        }
      }
    }
  }
}

// --- breakBeforeHeaderKeep: does the header travel with its rows? ----------
//
// Owner 2026-08-26: "don't split up table header or caption from rest (when
// possible). intact is best". The whole rule is here; the parser only measures.
// See test/table_keep_together for the same rule exercised through the real
// paginator on a real document.

namespace {
// A page 700 tall; a header that costs 70 with its rule, body rows 60 each,
// and a one-line caption at 52. The figures are the ones measured at 14 pt in
// TableKeepTogetherTest, so the arithmetic below is the arithmetic that ships.
constexpr int kPage = 700;
constexpr int kHeader = 70;
constexpr int kCaption = 52;
const int kRows[2] = {60, 60};
}  // namespace

TEST(TableHeaderKeep, LeavesAGroupThatAlreadyFitsAlone) {
  // 400 + 52 + 70 + 60 + 60 = 642, under the page. Nothing to do.
  EXPECT_FALSE(breakBeforeHeaderKeep(400, 0, kPage, true, kCaption, kHeader, kRows, 2));
}

TEST(TableHeaderKeep, BreaksWhenTheHeaderWouldBeTheLastThingOnThePage) {
  // THE REPORTED CASE. 556 + 70 + 60 = 686 fits, so the header and one row land
  // and the rest of the table opens the next page; 556 + 70 + 60 + 60 = 746
  // does not, and 0 + 190 does. So break, and all three travel.
  EXPECT_TRUE(breakBeforeHeaderKeep(556, 0, kPage, true, 0, kHeader, kRows, 2));
}

TEST(TableHeaderKeep, CarriesTheCaptionWithIt) {
  // 600 + 70 + 60 + 60 = 790 already fails without the caption. With it the
  // group is 242 and still fits an empty page, so the caption travels too.
  EXPECT_TRUE(breakBeforeHeaderKeep(600, 0, kPage, true, kCaption, kHeader, kRows, 2));
}

TEST(TableHeaderKeep, DegradesToOneRowBeforeGivingUp) {
  // A page with room for the header and one row but not two, on a table whose
  // two-row group would not fit an empty page either (rows 400 tall here). The
  // ladder drops to one row, finds that DOES need a break, and takes it.
  const int tallRows[2] = {400, 400};
  EXPECT_TRUE(breakBeforeHeaderKeep(300, 0, kPage, true, 0, kHeader, tallRows, 2));
  // ...and from a cursor where header + one row still fits, it does not.
  EXPECT_FALSE(breakBeforeHeaderKeep(200, 0, kPage, true, 0, kHeader, tallRows, 2));
}

TEST(TableHeaderKeep, YieldsRatherThanBlankingAPage) {
  // "(when possible)". A header plus one row taller than a whole empty page has
  // nowhere to be kept: breaking would publish a blank page and strand the
  // header again on the next one. The constraint gives way.
  const int hugeRows[2] = {900, 900};
  EXPECT_FALSE(breakBeforeHeaderKeep(300, 0, kPage, true, 0, kHeader, hugeRows, 2));
  EXPECT_FALSE(breakBeforeHeaderKeep(699, 0, kPage, true, kCaption, kHeader, hugeRows, 2));
}

TEST(TableHeaderKeep, NeverBreaksAnEmptyPage) {
  // Nothing above the header to strand it against. This is also what makes the
  // parser's second call a no-op after its first one has already broken.
  EXPECT_FALSE(breakBeforeHeaderKeep(0, 0, kPage, false, kCaption, kHeader, kRows, 2));
  EXPECT_FALSE(breakBeforeHeaderKeep(690, 0, kPage, false, kCaption, kHeader, kRows, 2));
}

TEST(TableHeaderKeep, AHeaderOnlyTableStillKeepsItsCaption) {
  // No body rows at all: the ladder's bottom rung is the caption and the header
  // together, because there is nothing that could have followed them.
  EXPECT_TRUE(breakBeforeHeaderKeep(600, 0, kPage, true, kCaption, kHeader, nullptr, 0));
  EXPECT_FALSE(breakBeforeHeaderKeep(400, 0, kPage, true, kCaption, kHeader, nullptr, 0));
}

TEST(TableHeaderKeep, ABreakIsOnlyEverAskedForWhenItHelps) {
  // The property that makes this terminate, swept rather than sampled: it may
  // return true ONLY when the group fails to fit where it stands AND fits an
  // empty page. Otherwise a break would produce the same stranding one page
  // later, having blanked a page on the way.
  for (int cursor = 0; cursor <= kPage; cursor += 25) {
    for (const int rowH : {20, 60, 200, 400, 900}) {
      const int rows[2] = {rowH, rowH};
      for (int count = 0; count <= 2; count++) {
        for (const int lead : {0, kCaption}) {
          if (!breakBeforeHeaderKeep(cursor, 0, kPage, true, lead, kHeader, rows, count)) continue;
          // Some rung of the ladder must be both too big for here and small
          // enough for an empty page.
          bool justified = false;
          for (int k = count; k >= (count > 0 ? 1 : 0); k--) {
            int group = lead + kHeader + k * rowH;
            if (cursor + group > kPage && group <= kPage) justified = true;
          }
          EXPECT_TRUE(justified) << "asked for a pointless break at cursor " << cursor << " rowH " << rowH
                                 << " count " << count << " lead " << lead;
        }
      }
    }
  }
}
