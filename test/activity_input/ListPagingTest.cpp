// Side-button list paging.
//
// THE ASK (owner, re-reported 2026-08-15): "make side buttons be page up and
// page down, when they are identical functionality to front buttons, like on
// the home screen." Before this change the two pairs were literally one action:
// MappedInputManager resolved NavNext to "side Down OR front Right", and every
// list screen navigates through ButtonNavigator::onNext/onPrevious, so pressing
// side-Down and pressing front-Right moved the selector by the same single row
// on every list in the firmware.
//
// WHAT IS UNDER TEST HERE
//
//   1. The mapping seam. NavNext/NavPrevious must no longer answer to the side
//      buttons, and the new PageNext/PagePrevious must answer to them (honoring
//      the user's side-button swap). Everything else in this file rides on that,
//      and it is the one place a regression would be invisible: the lists would
//      still navigate, just with both pairs doing the same thing again.
//
//   2. The paging arithmetic, which is the part with edges: clamping at both
//      ends, a list shorter than one page, and a list that is an exact multiple
//      of the page size. Rulings behind each assertion are in
//      docs/ui-conventions.md ("Side buttons should page, not repeat the front
//      buttons") and cited per case below.
//
//   3. That a held side button does not eat the next front-button step —
//      ButtonNavigator::onRelease() swallows a release whenever a continuous
//      fired, so the page pair had to be given its own repeat clock.
//
// The per-screen wiring (11 activities) is NOT asserted here: only two of them
// link host-side and both are the deliberately exempt font pickers. That half is
// covered in the simulator instead.

#include <HalGPIO.h>
#include <gtest/gtest.h>

#include "CrossPointSettings.h"
#include "HostHarness.h"
#include "MappedInputManager.h"
#include "util/ButtonNavigator.h"

namespace {

using Button = MappedInputManager::Button;

class ListPaging : public ::testing::Test {
 protected:
  void SetUp() override { host::reset(); }
  void TearDown() override { host::reset(); }

  // A press edge on one hardware button, committed for exactly this frame.
  static void press(const uint8_t hw) {
    host::buttons().simSetDown(hw, true);
    host::buttons().update();
  }
};

// ---------------------------------------------------------------------------
// 1. The mapping seam
// ---------------------------------------------------------------------------

TEST_F(ListPaging, SideButtonsNoLongerStepTheList) {
  // The regression this whole change is about: side-Down used to satisfy
  // NavNext, which is what every list screen's step handler listens on.
  press(HalGPIO::BTN_DOWN);
  EXPECT_FALSE(host::input().wasPressed(Button::NavNext))
      << "side Down must not step the list any more — that is what made the two pairs identical";
  EXPECT_TRUE(host::input().wasPressed(Button::PageNext)) << "side Down must page instead";

  host::reset();

  press(HalGPIO::BTN_UP);
  EXPECT_FALSE(host::input().wasPressed(Button::NavPrevious));
  EXPECT_TRUE(host::input().wasPressed(Button::PagePrevious));
}

TEST_F(ListPaging, FrontButtonsStillStepAndDoNotPage) {
  press(HalGPIO::BTN_RIGHT);
  EXPECT_TRUE(host::input().wasPressed(Button::NavNext));
  EXPECT_FALSE(host::input().wasPressed(Button::PageNext)) << "the front pair must not page; it steps one row";

  host::reset();

  press(HalGPIO::BTN_LEFT);
  EXPECT_TRUE(host::input().wasPressed(Button::NavPrevious));
  EXPECT_FALSE(host::input().wasPressed(Button::PagePrevious));
}

TEST_F(ListPaging, PagingFollowsTheUsersSideButtonSwap) {
  // Same setting the reader's page turns use, so paging a list runs the same
  // way round as paging the book.
  SETTINGS.sideButtonLayout = CrossPointSettings::NEXT_PREV;

  press(HalGPIO::BTN_UP);
  EXPECT_TRUE(host::input().wasPressed(Button::PageNext)) << "under NEXT_PREV the TOP side button pages forward";
  EXPECT_FALSE(host::input().wasPressed(Button::PagePrevious));
}

TEST_F(ListPaging, PagingStillWorksWhenSideButtonsAreDisabled) {
  // SIDE_BUTTONS_DISABLED exists to stop the side buttons turning BOOK pages.
  // Honoring it here would leave every list with no page control at all — the
  // same trap ReaderUtils.h records for the reader's side font controls, where
  // routing through the paging aliases made the feature dead on exactly the
  // devices whose owner had opted out.
  SETTINGS.sideButtonLayout = CrossPointSettings::SIDE_BUTTONS_DISABLED;

  press(HalGPIO::BTN_DOWN);
  EXPECT_TRUE(host::input().wasPressed(Button::PageNext));
  EXPECT_FALSE(host::input().wasPressed(Button::PageBack))
      << "book paging stays inert, which is what the setting means";
}

// ---------------------------------------------------------------------------
// 2. The paging arithmetic
// ---------------------------------------------------------------------------

TEST_F(ListPaging, PageMovesExactlyOneScreenful) {
  // 20 rows, 8 per screen. From the top row of page 0 to the top row of page 1:
  // nothing skipped, nothing shown twice.
  int index = 0;
  EXPECT_TRUE(ButtonNavigator::pageDown(index, 20, 8));
  EXPECT_EQ(index, 8);
  EXPECT_TRUE(ButtonNavigator::pageDown(index, 20, 8));
  EXPECT_EQ(index, 16);
}

TEST_F(ListPaging, PageKeepsTheHighlightsPositionWithinThePage) {
  // Moving by exactly itemsPerPage means (i + p) / p == i / p + 1 for any i, so
  // the drawn page (BaseTheme::drawList derives it as selectedIndex / pageItems)
  // advances by exactly one whether or not the selection started page-aligned,
  // and the highlight stays on the same row of the screen rather than jumping to
  // the top. That is the "keep the selection sensible" rule for this change.
  int index = 3;  // row 3 of page 0
  ASSERT_TRUE(ButtonNavigator::pageDown(index, 20, 8));
  EXPECT_EQ(index, 11) << "row 3 of page 1";
  EXPECT_EQ(index / 8, 1) << "exactly one page on";
  EXPECT_EQ(index % 8, 3) << "same row of the screen";
}

TEST_F(ListPaging, PageDownClampsAtTheEndAndLandsOnTheLastRow) {
  // RULED 2026-08-14: clamp, do not wrap. The press still moves — onto the last
  // row — so the end of the list is perceptible rather than a dead press.
  int index = 16;  // page 2 of a 20-row list
  EXPECT_TRUE(ButtonNavigator::pageDown(index, 20, 8));
  EXPECT_EQ(index, 19);

  // And once there, nothing moves and the caller must not redraw.
  EXPECT_FALSE(ButtonNavigator::pageDown(index, 20, 8));
  EXPECT_EQ(index, 19) << "no wrap back to the top";
}

TEST_F(ListPaging, PageUpClampsAtTheStartAndLandsOnTheFirstRow) {
  int index = 5;
  EXPECT_TRUE(ButtonNavigator::pageUp(index, 20, 8));
  EXPECT_EQ(index, 0);

  EXPECT_FALSE(ButtonNavigator::pageUp(index, 20, 8));
  EXPECT_EQ(index, 0) << "no wrap round to the last page";
}

TEST_F(ListPaging, ExactMultipleOfThePageSizeReachesTheLastRowAndStops) {
  // 24 rows / 8 per screen = 3 whole pages, no ragged tail. The off-by-one risk
  // is a page that ends at index 24 (past the end) or that refuses the last hop.
  int index = 0;
  ASSERT_TRUE(ButtonNavigator::pageDown(index, 24, 8));
  EXPECT_EQ(index, 8);
  ASSERT_TRUE(ButtonNavigator::pageDown(index, 24, 8));
  EXPECT_EQ(index, 16);
  ASSERT_TRUE(ButtonNavigator::pageDown(index, 24, 8));
  EXPECT_EQ(index, 23) << "clamped onto the last row of the last page, not 24";
  EXPECT_FALSE(ButtonNavigator::pageDown(index, 24, 8));

  // ...and all the way back.
  ASSERT_TRUE(ButtonNavigator::pageUp(index, 24, 8));
  EXPECT_EQ(index, 15);
  ASSERT_TRUE(ButtonNavigator::pageUp(index, 24, 8));
  EXPECT_EQ(index, 7);
  ASSERT_TRUE(ButtonNavigator::pageUp(index, 24, 8));
  EXPECT_EQ(index, 0);
  EXPECT_FALSE(ButtonNavigator::pageUp(index, 24, 8));
}

TEST_F(ListPaging, AListShorterThanOnePageIsDead) {
  // RULED 2026-08-14: a list that fits on one screen has no page to turn, and
  // the side buttons do NOT fall back to stepping one row. Returning false is
  // load-bearing rather than cosmetic — with hold-repeat enabled, a held side
  // button would otherwise burn a redraw several times a second against a list
  // that cannot move.
  int index = 2;
  EXPECT_FALSE(ButtonNavigator::pageDown(index, 5, 8));
  EXPECT_EQ(index, 2);
  EXPECT_FALSE(ButtonNavigator::pageUp(index, 5, 8));
  EXPECT_EQ(index, 2);

  // Exactly one full page is still one page.
  index = 3;
  EXPECT_FALSE(ButtonNavigator::pageDown(index, 8, 8));
  EXPECT_EQ(index, 3);
}

TEST_F(ListPaging, DegenerateInputsMoveNothing) {
  int index = 0;
  EXPECT_FALSE(ButtonNavigator::pageDown(index, 0, 8)) << "empty list";
  EXPECT_FALSE(ButtonNavigator::pageDown(index, 20, 0)) << "no rows fit — a theme cannot really produce this, but the "
                                                           "division would be the crash if it did";
  EXPECT_FALSE(ButtonNavigator::pageUp(index, 20, -1));
  EXPECT_EQ(index, 0);
}

// ---------------------------------------------------------------------------
// 3. The two pairs must not interfere
// ---------------------------------------------------------------------------

TEST_F(ListPaging, AHeldSideButtonDoesNotSwallowTheNextFrontStep) {
  // ButtonNavigator::onRelease() deliberately drops the release when a
  // continuous already fired for that hold (that is how a hold does not also
  // fire a step on the way up). It reads ONE timestamp, so if the page pair
  // shared it, a held side button would leave the flag set and the very next
  // front-button step would be silently eaten. The page pair keeps its own
  // clock; this is the test that says so.
  ButtonNavigator nav(/*continuousIntervalMs=*/50, /*continuousStartMs=*/50);

  int pages = 0;
  int steps = 0;

  // Hold side Down past the repeat threshold: the page callback repeats.
  host::buttons().simSetDown(HalGPIO::BTN_DOWN, true);
  host::buttons().simSetHeldTime(500);
  host::buttons().update();
  nav.onPageNext([&pages] { pages++; });
  ASSERT_GE(pages, 1) << "a held side button must keep paging";

  // Let go.
  host::buttons().simSetDown(HalGPIO::BTN_DOWN, false);
  host::buttons().simSetHeldTime(0);
  host::buttons().update();
  nav.onPageNext([&pages] { pages++; });

  // Now a clean front-button tap. The step runs on the RELEASE.
  host::buttons().simSetDown(HalGPIO::BTN_RIGHT, true);
  host::buttons().update();
  nav.onNextRelease([&steps] { steps++; });
  host::buttons().simSetDown(HalGPIO::BTN_RIGHT, false);
  host::buttons().update();
  nav.onNextRelease([&steps] { steps++; });

  EXPECT_EQ(steps, 1) << "the front step after a side hold must not be swallowed";
}

}  // namespace
