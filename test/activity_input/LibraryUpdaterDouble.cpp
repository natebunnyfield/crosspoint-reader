// See LibraryUpdaterDouble.h. Member functions of the REAL LibraryUpdater
// class, defined here instead of by src/network/LibraryUpdater.cpp, which is
// not linked. The private helpers the header declares (computeCardSha256,
// loadSyncRecords, ...) are never referenced from these bodies, so they need
// no definition.

#include "LibraryUpdaterDouble.h"

#include <string>

#include "HostHarness.h"

namespace {

struct State {
  libdouble::Script script;
  libdouble::Observed observed;
};

State& state() {
  static State s;
  return s;
}

}  // namespace

namespace libdouble {

Script& script() { return state().script; }
const Observed& observed() { return state().observed; }

void reset() { state() = State{}; }

}  // namespace libdouble

LibraryUpdater::LibraryError LibraryUpdater::fetchManifest(StepCallback onStep, void* ctx) {
  auto& o = state().observed;
  if (o.fetchCalls++ == 0) {
    o.updatesAtFirstFetch = host::counters().updates;
    o.updateAndWaitsAtFirstFetch = host::counters().updateAndWaits;
  }
  const libdouble::Script& s = state().script;

  // The real one answers NO_TOKEN before either network step.
  if (s.checkResult == NO_TOKEN) return NO_TOKEN;

  // Both steps fire, in the real order, even when the check is scripted to
  // fail afterwards: a failure comes back from the network, after the steps.
  if (onStep) {
    onStep(ctx, CheckStep::CONTACTING);
    o.updatesAfterContactingStep = host::counters().updates;
    onStep(ctx, CheckStep::READING);
    o.updatesAfterReadingStep = host::counters().updates;
  }

  books.clear();
  if (s.checkResult != OK) return s.checkResult;

  books.reserve(s.books);
  for (size_t i = 0; i < s.books; ++i) {
    Book book;
    book.file = "book-" + std::to_string(i + 1) + ".epub";
    book.bytes = 1000;
    books.push_back(std::move(book));
  }
  return OK;
}

LibraryUpdater::BookResult LibraryUpdater::syncBook(size_t index, ProgressCallback onProgress, void* ctx) {
  state().observed.syncedBooks.push_back(index);
  // One whole-book progress step, the way a real download's last callback
  // lands: counters at 100% of this book, then the activity's repaint.
  processedSize = 1000;
  totalSize = 1000;
  if (onProgress) onProgress(ctx);
  return BookResult::UNCHANGED;
}

void LibraryUpdater::flushSyncRecords() { state().observed.flushes++; }
