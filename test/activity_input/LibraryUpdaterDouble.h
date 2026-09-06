// Test double for LibraryUpdater -- the network and the card behind Update
// Library -- for LibraryUpdateFirstFrameTest.cpp.
//
// The real class lives in src/network/LibraryUpdater.cpp and reaches
// HttpDownloader (TLS), HalStorage and sha256; none of that is under test
// here. LibraryUpdaterDouble.cpp defines the three member functions the
// activity calls -- fetchManifest, syncBook, flushSyncRecords -- against the
// real header, and records the harness's render-request counters at the
// moment each is entered. That is what turns "the first paint precedes the
// first network call" into an assertion about ORDER rather than about pixels.
#pragma once

#include <cstddef>
#include <vector>

#include "network/LibraryUpdater.h"

namespace libdouble {

// What the next run of the double should answer.
struct Script {
  LibraryUpdater::LibraryError checkResult = LibraryUpdater::OK;
  size_t books = 0;  // manifest entries fetchManifest() returns on OK
};

// What the activity did, in the order it did it.
struct Observed {
  int fetchCalls = 0;
  // host::counters() as fetchManifest() was first entered. -1 = never called.
  int updatesAtFirstFetch = -1;
  int updateAndWaitsAtFirstFetch = -1;
  // host::counters().updates right after each step callback returned, so a
  // test can tell which step the activity chose to repaint.
  int updatesAfterContactingStep = -1;
  int updatesAfterReadingStep = -1;
  std::vector<size_t> syncedBooks;  // syncBook() indices, in call order
  int flushes = 0;
};

Script& script();
const Observed& observed();
void reset();

}  // namespace libdouble
