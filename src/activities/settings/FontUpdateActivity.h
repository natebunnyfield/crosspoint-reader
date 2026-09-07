#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "network/FontUpdater.h"

/**
 * One-button font sync, from the claude-tools fonts-latest release.
 *
 * Home -> "Update Fonts" -> it checks, syncs, summarizes. The same shape as
 * LibraryUpdateActivity, which is the model for "fetch a private GitHub release
 * and act on it", down to the state names. Read that screen's header for the
 * two ordering rules repeated below; they were both real, shipped defects.
 *
 *   CHECKING    fetch release JSON + manifest.json (FontUpdater)
 *   NO_WIFI     no connection — say so and point at Settings
 *   NO_TOKEN    no GitHub token — say where it goes, do nothing
 *   SYNCING     per-family compare/install with a progress bar
 *   DONE        summary: N updated, M unchanged, K errors
 *   FAILED      a reason, and Back
 *
 * WHAT IS DIFFERENT FROM UPDATE LIBRARY, and it is the reason this feature was
 * deleted once: the unit is a FAMILY, not a file. A family is six .cpfont cuts
 * and installs whole or not at all — see FontUpdater.h and FontSyncPlan.h. So
 * the row under the bar names a family, the bar's denominator is families, and
 * a family that fails leaves the previously installed one exactly as it was
 * rather than a hole in the ramp.
 *
 * Families on the card that the manifest does not mention are never touched.
 *
 * THE FIRST FRAME IS ON THE PANEL BEFORE ANY NETWORK CALL, and onEnter() waits
 * for it rather than merely requesting it. A deferred requestUpdate() there is
 * only a flag until the transition tick ends (ActivityManager.cpp:87-92), and
 * the very next loop() tick is the one that blocks on the manifest check — on a
 * host build, which presents pixels only from its main thread after loop()
 * returns, the reader keeps looking at Home for the whole sync.
 *
 * THE FAMILIES SYNC ONE PER loop() TICK, for the same reason the books do: a
 * host presents pixels only between ticks, so a sync that ran to completion
 * inside one call would show "Font 1 of N" and then the summary.
 *
 * BACK STOPS THE RUN, BETWEEN FAMILIES ONLY (owner ruling 2026-09-07). A
 * 12-family sync is minutes rather than the seconds Update Library takes, so
 * "wait or pull the power" was not an acceptable pair of options.
 *
 * WHERE THE CHECK IS, AND WHY IT IS REACHABLE. An earlier draft of this header
 * claimed Back was already read between families; it was not, and the reason is
 * worth writing down because it is the bug class this feature would repeat. The
 * edge is SAMPLED for us -- src/main.cpp:1097 calls mappedInputManager.update()
 * at the top of every main-loop iteration, before ActivityManager.cpp:82 calls
 * this activity's loop() -- so wasPressed() inside loop() reports this tick's
 * input whether or not the activity pumps anything itself. What made it
 * unreachable was purely loop()'s own early `return` out of the SYNCING branch,
 * ahead of the Back/Confirm block at the bottom. So the check is read at the
 * TOP of loop(), before that branch, and LibraryUpdateActivity's shape
 * (LibraryUpdateActivity.cpp:74-77) is deliberately no longer copied here.
 *
 * BETWEEN families, never between files. A cancel therefore cannot leave a
 * family half-installed: the commit is already atomic, so the worst case is
 * "stopped after Doves, the rest not yet installed", which the next run
 * resumes. Checking between FILES would discard a staged family and throw away
 * its download, and would put an input read in the tight loop.
 */
class FontUpdateActivity : public Activity {
 public:
  enum class State {
    CHECKING,
    NO_WIFI,
    NO_TOKEN,
    SYNCING,
    CANCELED,  // Back pressed between families: some installed, the rest untouched
    DONE,
    FAILED,
  };

  FontUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FontUpdate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Sleeping mid-install would leave a staging directory. Harmless — the next
  // run sweeps it (FontUpdater::recoverStaleStaging) — but pointless.
  bool preventAutoSleep() override { return state == State::SYNCING || state == State::CHECKING; }
  bool skipLoopDelay() override { return state == State::SYNCING; }

 private:
  State state = State::CHECKING;
  FontUpdater::CheckStep checkStep = FontUpdater::CheckStep::CONTACTING;
  FontUpdater updater;
  std::string errorMessage;
  size_t currentFamily = 0;  // index into the manifest while SYNCING (the family on screen)
  size_t nextFamily = 0;     // first manifest index not yet synced; == families.size() means done
  unsigned updated = 0;      // ADDED + UPDATED
  unsigned unchanged = 0;
  unsigned errors = 0;
  // Families deleted because the manifest no longer lists them, counted apart
  // from the three above: this is the only destructive thing the screen does,
  // and folding it into "updated" would hide it. The names are kept so the
  // summary can say WHICH -- a count alone is the "22 errors" mistake again.
  unsigned removed = 0;
  std::string removedNames;
  std::vector<std::string> removedFamilies;
  // errors, by kind, so the summary can name the one that dominates.
  unsigned storageErrors = 0;
  unsigned networkErrors = 0;
  unsigned verifyErrors = 0;
  unsigned int lastRenderedPercent = 101;
  bool checkStarted = false;

  void runCheck();        // the manifest check, on the first loop() tick
  void syncNextFamily();  // one family per SYNCING tick, then DONE
  void cancelSync();      // Back between families: finish the run early, honestly
};
