#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "TableCellLabel.h"

namespace {

using TableCellLabel::forCell;
using TableCellLabel::normalize;

// The header row of the widest table in a real book (Wingspan's action spaces).
const std::vector<std::string> kSpaces = {"Space", "Forest", "Grassland", "Wetland"};

TEST(Normalize, TrimsAndCollapsesWhitespace) {
  EXPECT_EQ(normalize("  Space  "), "Space");
  EXPECT_EQ(normalize("Birds in\n  the row"), "Birds in the row");
  EXPECT_EQ(normalize("\t\n "), "");
  EXPECT_EQ(normalize(""), "");
}

TEST(Normalize, DropsTrailingColonSoLabelsDoNotDouble) {
  EXPECT_EQ(normalize("Space:"), "Space");
  EXPECT_EQ(normalize("Space :"), "Space");
  EXPECT_EQ(normalize("Space::"), "Space");
  // Interior colons are content, not punctuation to strip.
  EXPECT_EQ(normalize("Ratio 2:1"), "Ratio 2:1");
}

TEST(Normalize, ClipsLongLabels) {
  const std::string longHeader(TableCellLabel::kMaxLabelLen + 20, 'x');
  EXPECT_EQ(normalize(longHeader).size(), TableCellLabel::kMaxLabelLen);
}

TEST(Normalize, NeverSplitsAUtf8Sequence) {
  // U+00E9 is two bytes, so a naive clip at an odd offset would halve one.
  std::string accents;
  for (size_t i = 0; i < TableCellLabel::kMaxLabelLen; i++) {
    accents += "\xc3\xa9";
  }
  const std::string out = normalize(accents);
  EXPECT_LE(out.size(), TableCellLabel::kMaxLabelLen);
  EXPECT_EQ(out.size() % 2, 0u) << "clipped mid-sequence";
}

TEST(ForCell, TwoColumnTableIsUnlabelled) {
  // "Forest" / "Gain food from the birdfeeder" already reads as a pair; a label
  // would only repeat the name.
  const std::vector<std::string> head = {"Row", "Its action"};
  EXPECT_EQ(forCell(head, "", 1, 2), "");
  EXPECT_EQ(forCell(head, "Forest", 2, 2), "");
}

TEST(ForCell, FirstColumnIsCompletedByItsOwnText) {
  // The cell's text follows the prefix, so this renders "Space 1".
  EXPECT_EQ(forCell(kSpaces, "", 1, 4), "Space ");
}

TEST(ForCell, LaterColumnsCarryRowAndColumn) {
  EXPECT_EQ(forCell(kSpaces, "1", 3, 4), "1 \xc2\xb7 Grassland: ");
  EXPECT_EQ(forCell(kSpaces, "6 (row full)", 2, 4), "6 (row full) \xc2\xb7 Forest: ");
}

TEST(ForCell, FallsBackWhenAPieceIsMissing) {
  // No <thead>: label by row alone.
  EXPECT_EQ(forCell({}, "1", 2, 4), "1: ");
  // Headers but an empty first cell: label by column alone.
  EXPECT_EQ(forCell(kSpaces, "", 2, 4), "Forest: ");
  // Neither. Better nothing than a coordinate.
  EXPECT_EQ(forCell({}, "", 2, 4), "");
  EXPECT_EQ(forCell({}, "", 1, 4), "");
}

TEST(ForCell, RaggedRowBeyondTheHeaderDoesNotReadOutOfRange) {
  // A <tr> with more cells than the header row is malformed but common.
  EXPECT_EQ(forCell(kSpaces, "1", 9, 4), "1: ");
  EXPECT_EQ(forCell({"A", "B", "C"}, "r", 4, 3), "r: ");
}

TEST(ForCell, NeverEmitsACoordinate) {
  // The whole point: no output may contain the old "Row N, Cell N" shape.
  const std::vector<std::vector<std::string>> heads = {{}, {"A"}, {"A", "B"}, kSpaces};
  for (const auto& head : heads) {
    for (size_t col = 1; col <= 6; col++) {
      for (const std::string& row : {std::string(), std::string("1")}) {
        const std::string out = forCell(head, row, col, head.empty() ? 0 : head.size());
        EXPECT_EQ(out.find("Cell "), std::string::npos) << out;
        EXPECT_EQ(out.find("Tab Row"), std::string::npos) << out;
      }
    }
  }
}

}  // namespace
