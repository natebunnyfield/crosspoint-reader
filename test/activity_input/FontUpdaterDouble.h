// Test double for FontUpdater -- the network and the card behind Update Fonts
// -- for FontUpdateFirstFrameTest.cpp.
//
// Same construction as LibraryUpdaterDouble: the real class lives in
// src/network/FontUpdater.cpp and reaches HttpDownloader (TLS), HalStorage,
// SdCardFontRegistry and sha256, none of which is under test here.
// FontUpdaterDouble.cpp defines the three member functions the activity calls
// -- fetchManifest, syncFamily, finishRun -- against the real header, and
// records the harness's render-request counters at the moment each is entered.
// That is what turns "the first paint precedes the first network call" into an
// assertion about ORDER rather than about pixels.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "network/FontUpdater.h"

namespace fontdouble {

// What the next run of the double should answer.
struct Script {
  FontUpdater::FontError checkResult = FontUpdater::OK;
  size_t families = 0;        // manifest entries fetchManifest() returns on OK
  size_t filesPerFamily = 6;  // every installed family ships six cuts since 2026-08-26
  // The first `failedFamilies` syncFamily() calls answer FAILED with this kind;
  // the rest answer UNCHANGED.
  size_t failedFamilies = 0;
  fontsync::FailureKind failureKind = fontsync::FailureKind::STORAGE;
  // Names removeUnlistedFamilies() should report as removed, if it is called
  // at all -- which is itself the thing under test on the cancel path.
  std::vector<std::string> removes;
};

// What the activity did, in the order it did it.
struct Observed {
  int fetchCalls = 0;
  // host::counters() as fetchManifest() was first entered. -1 = never called.
  int updatesAtFirstFetch = -1;
  int updateAndWaitsAtFirstFetch = -1;
  // host::counters().updates right after each step callback returned, so a test
  // can tell which step the activity chose to repaint.
  int updatesAfterContactingStep = -1;
  int updatesAfterReadingStep = -1;
  std::vector<size_t> syncedFamilies;  // syncFamily() indices, in call order
  // Times removeUnlistedFamilies() was entered. Zero after a cancel is a
  // REQUIREMENT, not an accident: a reader who stopped the run did not ask for
  // a mirror (owner ruling 2026-09-07).
  int removeCalls = 0;
  int finishes = 0;
};

Script& script();
const Observed& observed();
void reset();

}  // namespace fontdouble
