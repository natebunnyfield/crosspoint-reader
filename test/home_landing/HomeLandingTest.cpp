// Where Home's selector lands on re-entry: the menu row Home was left through,
// else the cover it was left through, else the top cover. Owner report
// 2026-09-02: Back from a book opened off a cover landed on the last MENU row.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "activities/home/HomeLanding.h"

namespace {

struct Cover {
  std::string path;
};

// Menu indices within the menu. The function only adds them to the cover
// count, so any value proves it; these are HomeActivity::menuItemToIndex's
// answers on a CROSSPOINT_NO_DEVICE_FLASH build (Settings is 8 where Update
// Firmware is offered).
constexpr int kSettingsMenuIndex = 7;
constexpr int kFileBrowserMenuIndex = 1;

std::vector<Cover> covers() { return {{"/books/a.epub"}, {"/books/b.epub"}, {"/books/c.epub"}}; }

TEST(HomeLanding, AMenuRowLandsPastTheCovers) {
  EXPECT_EQ(homelanding::selectorIndex(true, kSettingsMenuIndex, "", covers()),
            3 + kSettingsMenuIndex);
  EXPECT_EQ(homelanding::selectorIndex(true, kFileBrowserMenuIndex, "", covers()),
            3 + kFileBrowserMenuIndex);
}

TEST(HomeLanding, NoCoversNoMenuRowIsTheTop) {
  EXPECT_EQ(homelanding::selectorIndex(false, 0, "", std::vector<Cover>{}), 0);
  EXPECT_EQ(homelanding::selectorIndex(false, 0, "", covers()), 0);
}

TEST(HomeLanding, TheCoverLeftThroughIsFoundByPath) {
  EXPECT_EQ(homelanding::selectorIndex(false, 0, "/books/b.epub", covers()), 1);
  EXPECT_EQ(homelanding::selectorIndex(false, 0, "/books/c.epub", covers()), 2);
}

// The reader re-adds the book to the recents on open, so the cover left from
// index 2 comes back at index 0. Landing by index would put the selector on
// a different book. The same search is what lands the Back-on-Home resume
// shortcut on the cover the selector was HOVERING rather than the cover 0 it
// opened.
TEST(HomeLanding, ACoverThatMovedToTheTopIsStillTheOneLandedOn) {
  const std::vector<Cover> reordered = {{"/books/c.epub"}, {"/books/a.epub"}, {"/books/b.epub"}};
  EXPECT_EQ(homelanding::selectorIndex(false, 0, "/books/c.epub", reordered), 0);
}

TEST(HomeLanding, ACoverNoLongerListedFallsToTheTop) {
  EXPECT_EQ(homelanding::selectorIndex(false, 0, "/books/gone.epub", covers()), 0);
}

// A menu row set AFTER a cover wins: the goTo* wrappers never clear the cover
// path, so this precedence is what keeps a stale path harmless.
TEST(HomeLanding, AMenuRowBeatsAStaleCoverPath) {
  EXPECT_EQ(homelanding::selectorIndex(true, kSettingsMenuIndex, "/books/b.epub", covers()),
            3 + kSettingsMenuIndex);
}

// The reported sequence, against the wiring that shipped it. Before this
// change goHome() carried only lastHomeMenuItem, and opening a cover did not
// touch it, so "Settings, Back, cover b, Back" re-entered Home with
// SETTINGS_MENU still in charge (lastRow true here). The naive arm models exactly that and lands on
// Settings; the real wiring (HomeActivity::recordFocus resets the row to NONE
// and records the path) lands on the book.
TEST(HomeLanding, BackFromACoverOpenedAfterAMenuVisitLandsOnTheCover) {
  bool lastRow = false;
  std::string lastCover;

  // Open Settings from Home, Back.
  lastRow = true;  // SETTINGS_MENU
  EXPECT_EQ(homelanding::selectorIndex(lastRow, kSettingsMenuIndex, lastCover, covers()), 3 + kSettingsMenuIndex);

  // Naive: open cover b; the row is left as it was.
  const int naive = homelanding::selectorIndex(lastRow, kSettingsMenuIndex, lastCover, covers());
  EXPECT_EQ(naive, 3 + kSettingsMenuIndex) << "the naive arm must reproduce the reported landing";

  // Real: HomeActivity::recordFocus resets the row and records the path.
  lastRow = false;
  lastCover = "/books/b.epub";
  EXPECT_EQ(homelanding::selectorIndex(lastRow, 0, lastCover, covers()), 1);
  EXPECT_NE(homelanding::selectorIndex(lastRow, 0, lastCover, covers()), naive);
}

}  // namespace
