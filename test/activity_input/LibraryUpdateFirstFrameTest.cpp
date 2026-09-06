// Update Library: the first frame is displayed before the first network call.
//
// WHY THE ORDER IS WORTH A TEST. LibraryUpdateActivity::onEnter() ends with
// requestUpdateAndWait(), and the manifest check runs on the loop() tick
// after. A deferred requestUpdate() there compiles, reads naturally, and
// shipped a screen that stayed on Home for the whole sync on a host build:
// the deferred flag becomes one notify to the render task as the transition
// tick ends (ActivityManager.cpp:87-92), and the next tick is the one that
// blocks on the network. Nothing in the firmware waits for the paint unless
// the activity does.
//
// The harness's ActivityManager double counts both request flavours, and the
// LibraryUpdater double (LibraryUpdaterDouble.cpp) records those counts at
// the moment fetchManifest() is entered -- so the assertions are about ORDER,
// not about pixels having been drawn. No render task runs here.
//
// What is real: LibraryUpdateActivity, unmodified; Activity;
// MappedInputManager; UITheme. What is doubled: LibraryUpdater (the network
// and the card), WiFi (stubs/WiFi.h) and ActivityManager (HostHarness.cpp).

#include <WiFi.h>
#include <gtest/gtest.h>

#include <memory>

#include "HostHarness.h"
#include "LibraryUpdaterDouble.h"
#include "activities/settings/LibraryUpdateActivity.h"

namespace {

std::unique_ptr<LibraryUpdateActivity> makeActivity() {
  return std::make_unique<LibraryUpdateActivity>(host::renderer(), host::input());
}

class LibraryUpdateFirstFrame : public ::testing::Test {
 protected:
  void SetUp() override {
    host::reset();
    libdouble::reset();
    WiFi.simSetConnected(true);
  }
};

// onEnter() paints synchronously -- one waited-for request, not a deferred
// one -- and touches no network.
TEST_F(LibraryUpdateFirstFrame, OnEnterWaitsForThePaintAndFetchesNothing) {
  host::setRootActivity(makeActivity());

  EXPECT_EQ(host::counters().updateAndWaits, 1);
  EXPECT_EQ(host::counters().updateAndWaitsHoldingRenderLock, 0);
  EXPECT_EQ(libdouble::observed().fetchCalls, 0);
}

// The check runs on the first loop() tick, and by then the paint has been
// waited for. This is the ordering the screen exists to keep.
TEST_F(LibraryUpdateFirstFrame, TheFirstNetworkCallComesAfterTheWaitedPaint) {
  host::setRootActivity(makeActivity());
  host::frame();

  ASSERT_EQ(libdouble::observed().fetchCalls, 1);
  EXPECT_GE(libdouble::observed().updateAndWaitsAtFirstFetch, 1);
}

// The frame onEnter waited for already says "Contacting GitHub", so that step
// must not repaint; the READING step must. Repainting CONTACTING put a second,
// identical refresh on top of the TLS handshake.
TEST_F(LibraryUpdateFirstFrame, OnlyTheReadingStepRepaints) {
  host::setRootActivity(makeActivity());
  host::frame();

  const auto& o = libdouble::observed();
  ASSERT_EQ(o.fetchCalls, 1);
  EXPECT_EQ(o.updatesAfterContactingStep, o.updatesAtFirstFetch);
  EXPECT_EQ(o.updatesAfterReadingStep, o.updatesAtFirstFetch + 1);
}

// The books sync ONE PER TICK, with the manifest check's tick and the summary's
// tick on either side. A host presents only between ticks, so this is what
// makes "Book N of M" and the bar visible there at all; before, the whole sync
// ran inside the check's tick and the host went from "Book 1 of 3" straight to
// the summary.
TEST_F(LibraryUpdateFirstFrame, EachBookSyncsOnItsOwnTick) {
  libdouble::script().books = 3;
  host::setRootActivity(makeActivity());

  // The check's tick: the manifest, the waited-for "Book 1 of 3" frame, and
  // no download yet.
  host::frame();
  const auto& o = libdouble::observed();
  ASSERT_EQ(o.fetchCalls, 1);
  EXPECT_TRUE(o.syncedBooks.empty());
  EXPECT_EQ(host::counters().updateAndWaits, 2);  // CHECKING, then SYNCING
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_TRUE(host::currentActivity()->preventAutoSleep());

  // One book per tick, each with its own repaint request before the download.
  for (size_t book = 0; book < 3; ++book) {
    const int updatesBefore = host::counters().updates;
    host::frame();
    ASSERT_EQ(o.syncedBooks.size(), book + 1);
    EXPECT_EQ(o.syncedBooks.back(), book);
    EXPECT_GT(host::counters().updates, updatesBefore);
    EXPECT_EQ(o.flushes, 0);
    EXPECT_TRUE(host::currentActivity()->preventAutoSleep());
  }

  // The tick after the last book: one flush, then the summary.
  host::frame();
  EXPECT_EQ(o.syncedBooks.size(), 3u);
  EXPECT_EQ(o.flushes, 1);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());

  // Nothing further happens on later ticks.
  host::frames(2);
  EXPECT_EQ(o.syncedBooks.size(), 3u);
  EXPECT_EQ(o.flushes, 1);
}

// No link: say so with a deferred paint -- nothing blocks after it, so there
// is nothing to wait for -- and never reach the network.
TEST_F(LibraryUpdateFirstFrame, NoWifiPaintsDeferredAndNeverFetches) {
  WiFi.simSetConnected(false);
  host::setRootActivity(makeActivity());

  EXPECT_EQ(host::counters().updateAndWaits, 0);
  EXPECT_EQ(host::counters().updates, 1);

  host::frames(3);
  EXPECT_EQ(libdouble::observed().fetchCalls, 0);
  ASSERT_NE(host::currentActivity(), nullptr);
  EXPECT_FALSE(host::currentActivity()->preventAutoSleep());
}

}  // namespace
