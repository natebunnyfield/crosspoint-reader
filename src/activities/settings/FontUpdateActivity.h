#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "UpdateWorker.h"
#include "activities/Activity.h"
#include "network/FontUpdater.h"
#include "network/UpdateProgress.h"

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
 * Families on the card that the manifest does not
// mention ARE REMOVED -- this sync is a mirror, by an owner ruling recorded in
// docs/sd-card-fonts.md with its rejected alternatives, and there is
// deliberately no exemption for a sideloaded family. This line used to say the
// opposite, which is exactly the sort of comment that gets the mirror "fixed"
// away by someone who trusted it.
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
 *
 * THE STEP RUNS OFF THE PRESENTING THREAD ON A HOST (owner bug 2026-09-26,
 * "not appearing frozen when i select ... Update Fonts"). "One family per tick"
 * above gave a host ONE present per family: the render task drew every progress
 * frame, but simulator_main presents only after loop() returns, so a 7 MB family
 * on a 1 MB/s link was 18.7 s of glass that never changed and a button pad that
 * read nothing. Each blocking step -- the check, one family, the removals -- now
 * goes through UpdateWorker: a std::thread on a host, so loop() keeps ticking
 * (presents, Back, the heartbeat), and inline on the device, where the render
 * task already paints concurrently and a second stack is not worth its heap.
 * The step's RESULT is still consumed here on the loop thread, in
 * completeStep(), so every piece of state below has one writer.
 *
 * WHAT THE SCREEN SAYS WHILE IT WORKS, AND HOW OFTEN IT REPAINTS. Before
 * 2026-09-26 it repainted when the whole run's percentage moved, which a hash
 * never moves and a slow download moves every several seconds. Now a detail
 * line names the phase, the file and the family's bytes ("Downloading Edgar_12
 * · 1.2 of 6.8 MB"), a small line carries the elapsed time, and
 * updprogress::shouldRepaint decides: a phase, family or file change within a
 * quarter second, bytes and the clock at most once a second, FAST refresh
 * always. Back is acknowledged at once ("Stopping after this font") and still
 * acts only between families -- the ruling above is unchanged.
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
  // A home gesture while a step runs on a host worker would tear the activity
  // down under it. Treat it as Back: stop after this family.
  bool handleHomeGesture() override;
  void onExit() override;

 private:
  // Where a failed run leaves its log ring, so the cause can be read off the
  // card instead of a USB cable. See the definition.
  static constexpr const char* FAILURE_LOG_PATH = "/fontsync.log";
  void writeFailureLog(unsigned updated, unsigned unchanged, unsigned removed, unsigned errors);

  State state = State::CHECKING;
  // Atomic, and written WITHOUT the render lock: on a host the check runs on a
  // worker thread, and a worker that waits on RenderLock deadlocks against a
  // sleep, whose transition holds RenderLock while onExit() joins the worker
  // (ActivityManager.cpp:153; adversarial review 2026-09-26).
  std::atomic<FontUpdater::CheckStep> checkStep{FontUpdater::CheckStep::CONTACTING};
  FontUpdater updater;
  std::string errorMessage;
  size_t currentFamily = 0;  // index into the manifest while SYNCING (the family on screen)
  size_t nextFamily = 0;     // first manifest index not yet synced; == families.size() means done
  unsigned updated = 0;      // ADDED + UPDATED
  unsigned unchanged = 0;
  unsigned errors = 0;
  // Families the owner deleted from this card and the sync therefore left alone
  // (FontUpdater::FamilyResult::SKIPPED_DELETED). Logged, with the names; the
  // on-screen summary is unchanged.
  unsigned skippedDeleted = 0;
  std::string skippedNames;
  // Families the HOST bundled whose release copy differs, and which the sync
  // therefore did not download (FontUpdater::FamilyResult::SKIPPED_BUNDLED):
  // the iOS seed pass would put the app's copy back on the next launch. Logged
  // with the names, same as the deleted ones; the on-screen summary is
  // unchanged.
  unsigned keptBundled = 0;
  std::string keptBundledNames;
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
  bool checkStarted = false;
  // finishRun() has been called for this run. onExit calls it for a run torn
  // down part-way (a sleep, a home gesture between families) so the families
  // that DID install are discoverable and their ledger entries are written.
  bool runFinished = false;
  unsigned long startMs = 0;

  // One blocking step at a time. See UpdateWorker.h and the header above.
  enum class Step : uint8_t { NONE, CHECK, FAMILY, FINISH };
  Step step = Step::NONE;
  UpdateWorker worker;
  FontUpdater::FontError checkResult = FontUpdater::OK;
  FontUpdater::FamilyResult familyResult = FontUpdater::FamilyResult::FAILED;
  size_t familyIndex = 0;
  // Back was pressed: the run ends after the family in hand. Read by the
  // render task and a host worker's callbacks, hence atomic.
  std::atomic<bool> stopRequested{false};
  // Set only by onExit while a host worker still runs; FontUpdater abandons the
  // family in hand (staging discarded, installed copy untouched).
  std::atomic<bool> abandon{false};

  // The repaint throttle's memory. Guarded because on a host both the worker's
  // progress callbacks and the loop's heartbeat reach maybeRepaint().
  std::mutex paintMutex;
  updprogress::Snapshot shown;
  bool hasShown = false;
  unsigned long lastPaintMs = 0;

  void startStep(Step next);
  static void runStep(void* ctx);  // the blocking half, on the worker
  void completeStep();             // the bookkeeping half, on the loop thread
  void afterCheck();               // the manifest check's result
  void beginNextFamily();          // one family per step, then FINISH
  void afterFamily();              // tally the family's result
  void afterFinish();              // removals done: ledger, summary, DONE
  void cancelSync();               // Back between families: finish the run early, honestly
  void requestStop();
  updprogress::Snapshot snapshot(unsigned long now) const;
  // force: a new family/book is always requested at once, as the code before
  // 2026-09-26 did -- items are seconds apart and the render task coalesces.
  void maybeRepaint(bool force = false);
  static void onProgress(void* ctx);
};
