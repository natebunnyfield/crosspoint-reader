#pragma once

#include <string>

#include "activities/Activity.h"
#include "network/LibraryUpdater.h"

/**
 * One-button library sync, from the claude-tools library-latest release.
 *
 * Home -> "Update Library" -> it checks, syncs, summarizes. Deliberately the
 * same shape as OnlineFirmwareUpdateActivity (that screen is the model for
 * "fetch a GitHub release and act on it"), minus the confirmation popup: a
 * library sync is idempotent and touches nothing but /books/, so there is no
 * one-way step worth interrupting for.
 *
 *   CHECKING    fetch release JSON + manifest.json (LibraryUpdater)
 *   NO_WIFI     no connection — say so and point at Settings
 *   NO_TOKEN    SETTINGS.githubToken is empty — say where it goes, do nothing
 *   SYNCING     per-book compare/download with a progress bar
 *   DONE        summary: N updated, M unchanged, K errors
 *   FAILED      a reason, and Back
 *
 * Books on the card that the manifest does not mention are never touched.
 *
 * THE FIRST FRAME IS ON THE PANEL BEFORE ANY NETWORK CALL, and onEnter() waits
 * for it rather than merely requesting it. A deferred requestUpdate() there
 * is only a flag until the transition tick ends, when it becomes one notify to
 * the render task (ActivityManager.cpp:87-92) -- and the very next loop() tick
 * is the one that blocks on the manifest check. On device the paint raced the
 * TLS handshake; on a host build, which presents pixels only from its main
 * thread after loop() returns, the reader kept looking at Home until the whole
 * sync had finished. requestUpdateAndWait() in onEnter() is the same move
 * SleepActivity makes and main.cpp makes at boot: the frame is displayed
 * before onEnter returns, and the check starts on the tick after.
 *
 * THE BOOKS SYNC ONE PER loop() TICK, not all inside one call. A host build
 * presents only between loop() calls, so a sync that ran to completion inside
 * one showed "Book 1 of N" and then the summary, with every frame in between
 * converted and never presented. Each tick paints the book about to be synced,
 * syncs it, and returns; the tick after the last book flushes the sync records
 * and shows the summary. skipLoopDelay() keeps the ticks back to back on the
 * device, where the render task was already painting concurrently and nothing
 * changes but the shape of the code.
 */
class LibraryUpdateActivity : public Activity {
 public:
  enum class State {
    CHECKING,
    NO_WIFI,
    NO_TOKEN,
    SYNCING,
    DONE,
    FAILED,
  };

  LibraryUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LibraryUpdate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // A sleep mid-sync would leave a .part file. Harmless but pointless.
  bool preventAutoSleep() override { return state == State::SYNCING || state == State::CHECKING; }
  bool skipLoopDelay() override { return state == State::SYNCING; }

 private:
  State state = State::CHECKING;
  // Which network step the manifest check is on. The whole check runs inside
  // one loop() call, so without this the screen sits on one static line and
  // takes no input until it returns -- the "hangs on kickoff" report.
  LibraryUpdater::CheckStep checkStep = LibraryUpdater::CheckStep::CONTACTING;
  LibraryUpdater updater;
  std::string errorMessage;
  size_t currentBook = 0;  // index into the manifest while SYNCING (the book on screen)
  size_t nextBook = 0;     // first manifest index not yet synced; == books.size() means done
  unsigned updated = 0;    // ADDED + UPDATED
  unsigned unchanged = 0;
  unsigned errors = 0;
  // errors, by kind, so the summary can name the one that dominates.
  unsigned storageErrors = 0;
  unsigned networkErrors = 0;
  unsigned verifyErrors = 0;
  unsigned int lastRenderedPercent = 101;
  // onEnter() paints and waits; the network work is loop()'s, on its first
  // pass, so that nothing blocks before the frame is displayed (see above).
  bool checkStarted = false;

  void runCheck();      // the manifest check, on the first loop() tick
  void syncNextBook();  // one book per SYNCING tick, then DONE
};
