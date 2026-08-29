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
  LibraryUpdater updater;
  std::string errorMessage;
  size_t currentBook = 0;  // index into the manifest while SYNCING
  unsigned updated = 0;    // ADDED + UPDATED
  unsigned unchanged = 0;
  unsigned errors = 0;
  unsigned int lastRenderedPercent = 101;
  // Same reason as the OTA screen: onEnter cannot block on the network before
  // its first paint, so loop() does the work on its first pass.
  bool checkStarted = false;

  void runSync();
};
