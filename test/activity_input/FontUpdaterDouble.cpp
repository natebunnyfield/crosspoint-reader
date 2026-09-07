// See FontUpdaterDouble.h. Member functions of the REAL FontUpdater class,
// defined here instead of by src/network/FontUpdater.cpp, which is not linked.
// The private helpers the header declares (computeCardSha256, stageFamily, ...)
// are never referenced from these bodies, so they need no definition.

#include "FontUpdaterDouble.h"

#include <string>

#include "HostHarness.h"

namespace {

struct State {
  fontdouble::Script script;
  fontdouble::Observed observed;
};

State& state() {
  static State s;
  return s;
}

}  // namespace

namespace fontdouble {

Script& script() { return state().script; }
const Observed& observed() { return state().observed; }

void reset() { state() = State{}; }

}  // namespace fontdouble

FontUpdater::FontError FontUpdater::fetchManifest(StepCallback onStep, void* ctx) {
  auto& o = state().observed;
  if (o.fetchCalls++ == 0) {
    o.updatesAtFirstFetch = host::counters().updates;
    o.updateAndWaitsAtFirstFetch = host::counters().updateAndWaits;
  }
  const fontdouble::Script& s = state().script;

  // The real one answers NO_TOKEN before either network step.
  if (s.checkResult == NO_TOKEN) return NO_TOKEN;

  // Both steps fire, in the real order, even when the check is scripted to fail
  // afterwards: a failure comes back from the network, after the steps.
  if (onStep) {
    onStep(ctx, CheckStep::CONTACTING);
    o.updatesAfterContactingStep = host::counters().updates;
    onStep(ctx, CheckStep::READING);
    o.updatesAfterReadingStep = host::counters().updates;
  }

  families.clear();
  if (s.checkResult != OK) return s.checkResult;

  families.reserve(s.families);
  for (size_t i = 0; i < s.families; ++i) {
    Family family;
    family.name = "Family" + std::to_string(i + 1);
    family.files.reserve(s.filesPerFamily);
    for (size_t f = 0; f < s.filesPerFamily; ++f) {
      FontFile file;
      file.file = family.name + "_" + std::to_string(8 + 2 * f) + ".cpfont";
      file.bytes = 1000;
      family.files.push_back(std::move(file));
    }
    families.push_back(std::move(family));
  }
  return OK;
}

FontUpdater::FamilyResult FontUpdater::syncFamily(size_t index, ProgressCallback onProgress, void* ctx) {
  state().observed.syncedFamilies.push_back(index);
  const fontdouble::Script& s = state().script;
  fileCount = s.filesPerFamily;
  // One whole-file progress step, the way a real download's last callback
  // lands: counters at 100% of the last file, then the activity's repaint.
  currentFile = s.filesPerFamily;
  processedSize = 1000;
  totalSize = 1000;
  if (onProgress) onProgress(ctx);
  if (index < s.failedFamilies) {
    lastFailure_ = s.failureKind;
    return FamilyResult::FAILED;
  }
  lastFailure_ = fontsync::FailureKind::NONE;
  return FamilyResult::UNCHANGED;
}

size_t FontUpdater::removeUnlistedFamilies(std::vector<std::string>& removed) {
  state().observed.removeCalls++;
  for (const auto& name : state().script.removes) removed.push_back(name);
  return state().script.removes.size();
}

void FontUpdater::finishRun() { state().observed.finishes++; }
