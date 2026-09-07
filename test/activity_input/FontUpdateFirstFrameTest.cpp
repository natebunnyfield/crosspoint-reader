// Update Fonts: the first frame is displayed before the first network call,
// and each family syncs on its own tick.
//
// WHY THE ORDER IS WORTH A TEST, in full, is in
// LibraryUpdateFirstFrameTest.cpp: a deferred requestUpdate() in onEnter()
// compiles, reads naturally, and shipped a screen that stayed on Home for the
// whole sync on a host build. Update Fonts is the same shape and would repeat
// it exactly, so it gets the same assertions.
//
// IT MATTERS MORE HERE. A book is a few hundred KB; a font FAMILY is six
// .cpfont cuts and several megabytes, so a run that does not repaint between
// families is a screen frozen for minutes -- and a run that does not RETURN
// between families never reads the Back button either.
//
// The harness's ActivityManager double counts both request flavours, and the
// FontUpdater double (FontUpdaterDouble.cpp) records those counts at the moment
// fetchManifest() is entered -- so the assertions are about ORDER, not about
// pixels having been drawn. No render task runs here.
//
// What is real: FontUpdateActivity, unmodified; Activity; MappedInputManager;
// UITheme. What is doubled: FontUpdater (the network, the card and the font
// registry), WiFi (stubs/WiFi.h) and ActivityManager (HostHarness.cpp).

#include <HalGPIO.h>
#include <WiFi.h>
#include <gtest/gtest.h>

#include <memory>

#include "FontUpdaterDouble.h"
#include "HostHarness.h"
#include "activities/settings/FontUpdateActivity.h"

namespace {

std::unique_ptr<FontUpdateActivity> makeActivity() {
  return std::make_unique<FontUpdateActivity>(host::renderer(), host::input());
}

class FontUpdateFirstFrame : public ::testing::Test {
 protected:
  void SetUp() override {
    host::reset();
    fontdouble::reset();
    WiFi.simSetConnected(true);
  }
};

// onEnter() paints synchronously -- one waited-for request, not a deferred one
// -- and touches no network.
TEST_F(FontUpdateFirstFrame, OnEnterWaitsForThePaintAndFetchesNothing) {
  host::setRootActivity(makeActivity());

  EXPECT_EQ(host::counters().updateAndWaits, 1);
  EXPECT_EQ(host::counters().updateAndWaitsHoldingRenderLock, 0);
  EXPECT_EQ(fontdouble::observed().fetchCalls, 0);
}

// The check runs on the first loop() tick, and by then the paint has been
// waited for. This is the ordering the screen exists to keep.
TEST_F(FontUpdateFirstFrame, TheFirstNetworkCallComesAfterTheWaitedPaint) {
  host::setRootActivity(makeActivity());
  host::frame();

  ASSERT_EQ(fontdouble::observed().fetchCalls, 1);
  EXPECT_GE(fontdouble::observed().updateAndWaitsAtFirstFetch, 1);
}

// The frame onEnter waited for already says "Contacting GitHub", so that step
// must not repaint; the READING step must. Repainting CONTACTING put a second,
// identical refresh on top of the TLS handshake.
TEST_F(FontUpdateFirstFrame, OnlyTheReadingStepRepaints) {
  host::setRootActivity(makeActivity());
  host::frame();

  const auto& o = fontdouble::observed();
  ASSERT_EQ(o.fetchCalls, 1);
  EXPECT_EQ(o.updatesAfterContactingStep, o.updatesAtFirstFetch);
  EXPECT_EQ(o.updatesAfterReadingStep, o.updatesAtFirstFetch + 1);
}

// One FAMILY per tick, with the manifest check's tick and the summary's tick on
// either side. A host presents only between ticks, which is what makes
// "Font N of M" and the bar visible there at all. (It does NOT make the run
// cancellable -- loop() returns before the input block while SYNCING; see the
// activity header.)
TEST_F(FontUpdateFirstFrame, EachFamilySyncsOnItsOwnTick) {
  fontdouble::script().families = 3;
  host::setRootActivity(makeActivity());

  // The check's tick: the manifest, the waited-for "Font 1 of 3" frame, and no
  // download yet.
  host::frame();
  const auto& o = fontdouble::observed();
  ASSERT_EQ(o.fetchCalls, 1);
  EXPECT_TRUE(o.syncedFamilies.empty());
  EXPECT_EQ(host::counters().updateAndWaits, 2);  // CHECKING, then SYNCING
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_TRUE(host::currentActivity()->preventAutoSleep());

  // One family per tick, each with its own repaint request before the download.
  for (size_t family = 0; family < 3; ++family) {
    const int updatesBefore = host::counters().updates;
    host::frame();
    ASSERT_EQ(o.syncedFamilies.size(), family + 1);
    EXPECT_EQ(o.syncedFamilies.back(), family);
    EXPECT_GT(host::counters().updates, updatesBefore);
    EXPECT_EQ(o.finishes, 0);
    EXPECT_TRUE(host::currentActivity()->preventAutoSleep());
  }

  // The tick after the last family: one finishRun(), then the summary.
  host::frame();
  EXPECT_EQ(o.syncedFamilies.size(), 3u);
  EXPECT_EQ(o.finishes, 1);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());

  // Nothing further happens on later ticks.
  host::frames(2);
  EXPECT_EQ(o.syncedFamilies.size(), 3u);
  EXPECT_EQ(o.removeCalls, 1);
  EXPECT_EQ(o.finishes, 1);
}

// finishRun() IS THE ONLY PLACE the registry is marked dirty and stale layout
// caches are dropped, so it must be reached even when every family failed --
// and exactly once. A run that skipped it after failures would leave a card on
// which a later successful run's bookkeeping never happened.
TEST_F(FontUpdateFirstFrame, EveryFamilyFailingStillReachesTheSummaryAndFinishesOnce) {
  fontdouble::script().families = 3;
  fontdouble::script().failedFamilies = 3;
  fontdouble::script().failureKind = fontsync::FailureKind::STORAGE;
  host::setRootActivity(makeActivity());

  host::frame();    // the check
  host::frames(3);  // three families, each FAILED
  const auto& o = fontdouble::observed();
  EXPECT_EQ(o.syncedFamilies.size(), 3u);
  EXPECT_EQ(o.finishes, 0);

  const int updatesBefore = host::counters().updates;
  host::frame();  // the tick after the last family
  EXPECT_EQ(o.finishes, 1);
  EXPECT_GT(host::counters().updates, updatesBefore);
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());

  host::frames(2);
  EXPECT_EQ(o.finishes, 1);
}

// A release that carries a manifest naming no usable family (every entry
// dropped by the all-or-nothing parse, or a half-published release) must land
// on the summary, NOT on a SYNCING frame -- that frame indexes
// getFamilies()[currentFamily], which on an empty vector is a LoadProhibited
// panic on device.
TEST_F(FontUpdateFirstFrame, AManifestWithNoUsableFamilyGoesStraightToTheSummary) {
  fontdouble::script().families = 0;
  host::setRootActivity(makeActivity());

  host::frame();
  const auto& o = fontdouble::observed();
  ASSERT_EQ(o.fetchCalls, 1);
  EXPECT_TRUE(o.syncedFamilies.empty());
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());

  host::frames(3);
  EXPECT_TRUE(o.syncedFamilies.empty());
}

// No link: say so with a deferred paint -- nothing blocks after it, so there is
// nothing to wait for -- and never reach the network.
TEST_F(FontUpdateFirstFrame, NoWifiPaintsDeferredAndNeverFetches) {
  WiFi.simSetConnected(false);
  host::setRootActivity(makeActivity());

  EXPECT_EQ(host::counters().updateAndWaits, 0);
  EXPECT_EQ(host::counters().updates, 1);

  host::frames(3);
  EXPECT_EQ(fontdouble::observed().fetchCalls, 0);
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());
}

// No token: the screen says where the token goes and stops. There is only ONE
// GitHub token on this device -- the fonts release is in the same private repo
// as the library -- so this path must not imply a second one exists.
TEST_F(FontUpdateFirstFrame, NoTokenStopsBeforeAnySync) {
  fontdouble::script().checkResult = FontUpdater::NO_TOKEN;
  fontdouble::script().families = 3;
  host::setRootActivity(makeActivity());

  host::frames(4);
  const auto& o = fontdouble::observed();
  EXPECT_EQ(o.fetchCalls, 1);
  EXPECT_TRUE(o.syncedFamilies.empty());
  EXPECT_EQ(o.finishes, 0);
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());
}

// ---------------------------------------------------------------------------
// CANCELLATION (owner ruling 2026-09-07): Back stops the run BETWEEN FAMILIES.
//
// The bug class these two tests exist for is the one this screen already
// committed once in prose: a check that EXISTS but is never REACHED. An earlier
// header here claimed Back was read between families; it was not, because
// loop() returned out of the SYNCING branch before the input block. So it is
// not enough to assert "a cancel stops the sync" -- the tests have to
// distinguish a reachable check from a decorative one.
// ---------------------------------------------------------------------------

// THE REACHABILITY TEST. Frames keep coming and the sync keeps going; the only
// thing that changes on the cancel frame is that Back is down. If the check
// were unreachable -- the pre-ruling shape -- syncedFamilies would keep growing
// through it, which is exactly what this asserts it does NOT do.
TEST_F(FontUpdateFirstFrame, TheBackCheckIsActuallyReachedDuringASync) {
  fontdouble::script().families = 6;
  host::setRootActivity(makeActivity());
  host::frame();  // the check

  const auto& o = fontdouble::observed();
  host::frame();  // family 0
  host::frame();  // family 1
  ASSERT_EQ(o.syncedFamilies.size(), 2u) << "the sync must be genuinely under way before Back is pressed";

  // The SAME kind of frame as the two above, with Back held. Nothing else differs.
  host::pressFrame(HalGPIO::BTN_BACK);
  EXPECT_EQ(o.syncedFamilies.size(), 2u) << "the cancel frame must not sync another family";

  // ...and it stays stopped. A check that fired once and then let the loop
  // carry on would show up here.
  host::frames(6);
  EXPECT_EQ(o.syncedFamilies.size(), 2u);
}

// A cancel leaves every family that was reached COMMITTED and every family that
// was not ABSENT -- there is no third state, because the cancel point is
// between families and the commit itself is atomic (test/font_commit proves the
// atomicity on a real filesystem; this proves where the cancel lands).
TEST_F(FontUpdateFirstFrame, ACancelBetweenFamiliesStopsCleanlyAndFinishesTheRun) {
  fontdouble::script().families = 5;
  host::setRootActivity(makeActivity());
  host::frame();  // the check
  host::frame();  // family 0
  host::frame();  // family 1

  const auto& o = fontdouble::observed();
  ASSERT_EQ(o.syncedFamilies.size(), 2u);
  EXPECT_EQ(o.finishes, 0);

  host::pressFrame(HalGPIO::BTN_BACK);

  // Exactly the families that were reached, in order, and no partial sixth.
  ASSERT_EQ(o.syncedFamilies.size(), 2u);
  EXPECT_EQ(o.syncedFamilies[0], 0u);
  EXPECT_EQ(o.syncedFamilies[1], 1u);

  // finishRun() STILL RAN. It is the only place the ledger is written and the
  // registry marked dirty, so skipping it on a cancel would leave the families
  // that DID install missing from the picker until reboot.
  EXPECT_EQ(o.finishes, 1);

  // ...and NOTHING WAS REMOVED. The mirror is not applied to a run the reader
  // stopped: the families never reached are not evidence that they should go
  // (owner ruling 2026-09-07).
  EXPECT_EQ(o.removeCalls, 0);

  // The run is over: no auto-sleep hold, no fast-loop request, and no further
  // syncing however many ticks pass.
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());
  EXPECT_FALSE(host::currentActivity()->skipLoopDelay());
  host::frames(4);
  EXPECT_EQ(o.syncedFamilies.size(), 2u);
  EXPECT_EQ(o.finishes, 1);
}

// The stopped screen is dismissed like any other terminal state.
TEST_F(FontUpdateFirstFrame, TheStoppedScreenIsDismissedWithBack) {
  fontdouble::script().families = 4;
  host::setRootActivity(makeActivity());
  host::frame();
  host::frame();
  host::pressFrame(HalGPIO::BTN_BACK);
  host::releaseFrame(HalGPIO::BTN_BACK);
  ASSERT_NE(host::currentActivity(), nullptr);

  host::tap(HalGPIO::BTN_BACK);
  host::frames(2);
  EXPECT_EQ(fontdouble::observed().finishes, 1);
}

}  // namespace
