// The reader's font-size SLOT LABEL, and the one case both copies of it got
// wrong in opposite directions.
//
// Owner report, 2026-08-27, from the device: "XXL is shown as XXS and XXL to
// inknut becomes XL in reading mode."
//
// There are six slot names (XXS XS S M L XL) but a family may have MORE than
// six installed sizes -- a user-built family, or, much more ordinarily, a card
// that has lived through a ramp change. Nothing on a real SD card prunes, so
// when a slot's point size moves (Inknut's S went 10 -> 11 pt on 2026-08-27)
// the file the old ramp vacated stays. Seven files, seven reachable slots, six
// names.
//
// Both steppers already clamp to what is INSTALLED, so slot 6 is reachable and
// is meant to be: an installed size must never become unselectable. The defect
// was purely in the label. Two independent copies of the name table existed and
// disagreed about the out-of-range case -- SettingsList.h clamped to the LAST
// name, FontSelectionActivity.cpp clamped to index 0 -- so the same slot read
// as "XL" on one screen and "XXS" on the other, and the second is the one the
// owner saw.
//
// The direction is the point. A slot past the end is a slot too LARGE. Naming
// it with the smallest name is not an imprecision, it is backwards, and it is
// exactly the shape that makes a user distrust the control.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "ReaderFontSizes.h"

namespace {

const std::vector<uint8_t> kSix{7, 9, 11, 12, 14, 16};        // a shipping ramp
const std::vector<uint8_t> kSeven{7, 9, 10, 11, 12, 14, 16};  // + the orphan
const std::vector<uint8_t> kFour{12, 14, 16, 18};             // NittiTypewriter

TEST(ReaderSlotLabel, SixInstalledSizesGetTheNames) {
  EXPECT_TRUE(readerSlotsAreNamed(kSix.size()));
  EXPECT_EQ(readerSlotLabel(kSix, 0), "XXS (7pt)");
  EXPECT_EQ(readerSlotLabel(kSix, 3), "M (12pt)");
  EXPECT_EQ(readerSlotLabel(kSix, 5), "XL (16pt)");
}

// THE REPORTED BUG. Seven installed sizes: the names no longer describe the
// ramp, so no name is borrowed. Before the fix slot 6 rendered "XXS".
TEST(ReaderSlotLabel, SevenInstalledSizesAreNotNamedAtAll) {
  EXPECT_FALSE(readerSlotsAreNamed(kSeven.size()));
  for (uint8_t s = 0; s < kSeven.size(); s++) {
    const std::string label = readerSlotLabel(kSeven, s);
    EXPECT_EQ(label, std::to_string(kSeven[s]) + "pt");
    EXPECT_EQ(label.find("XX"), std::string::npos) << "slot " << int(s) << " borrowed a name";
  }
}

// A slot past the end clamps to the LARGEST installed size, never the smallest.
TEST(ReaderSlotLabel, OutOfRangeClampsUpwardNotDownward) {
  EXPECT_EQ(readerSlotLabel(kSix, 6), "XL (16pt)");
  EXPECT_EQ(readerSlotLabel(kSix, 99), "XL (16pt)");
  EXPECT_EQ(readerSlotLabel(kFour, 5), "18pt");
  // The specific regression: it must not answer with slot 0's name or size.
  EXPECT_NE(readerSlotLabel(kSix, 6), "XXS (7pt)");
}

// Fewer than six installed sizes was already handled and must stay handled.
TEST(ReaderSlotLabel, FewerThanSixFallsBackToPointSizes) {
  EXPECT_FALSE(readerSlotsAreNamed(kFour.size()));
  EXPECT_EQ(readerSlotLabel(kFour, 0), "12pt");
  EXPECT_EQ(readerSlotLabel(kFour, 3), "18pt");
}

TEST(ReaderSlotLabel, EmptyIsEmptyRatherThanUndefined) {
  EXPECT_EQ(readerSlotLabel({}, 0), "");
  EXPECT_EQ(readerSlotLabel({}, 4), "");
}

// The names themselves, so a reorder or a truncation is a test failure rather
// than a silently different label on two screens.
TEST(ReaderSlotLabel, TheNamesAreTheSixAndInThisOrder) {
  ASSERT_EQ(READER_FONT_SLOT_COUNT, 6);
  const char* want[] = {"XXS", "XS", "S", "M", "L", "XL"};
  for (int i = 0; i < READER_FONT_SLOT_COUNT; i++) EXPECT_STREQ(READER_SLOT_NAMES[i], want[i]);
}

// There must be exactly ONE name table. Both screens used to carry their own.
TEST(ReaderSlotLabel, OnlyOneCopyOfTheNameTableExists) {
  for (const char* path : {CROSSPOINT_SETTINGS_LIST_H, CROSSPOINT_FONT_SELECTION_CPP}) {
    FILE* f = fopen(path, "rb");
    ASSERT_NE(f, nullptr) << path;
    std::string src;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) src.append(buf, n);
    fclose(f);
    EXPECT_EQ(src.find("\"XXS\", \"XS\""), std::string::npos)
        << path << " has grown a second copy of the slot names; use readerSlotLabel()";
  }
}

}  // namespace
