#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "network/OtaUpdater.h"

/**
 * One-button firmware update, from this fork's own GitHub releases.
 *
 * Home -> "Update Firmware" -> it checks, asks once, installs, reboots. That is
 * the whole flow; there is no picker and no settings sub-tree, because the ask
 * was one button.
 *
 *   CHECKING    fetch the latest release JSON (OtaUpdater::checkForUpdate)
 *   NO_WIFI     no connection -- say so and point at Settings
 *   UP_TO_DATE  nothing newer published
 *   CONFIRMING  "Update firmware?" with the version, Cancel / Confirm
 *   UPDATING    streaming into the inactive slot, progress bar
 *   SUCCESS     restart into the new image
 *   FAILED      a reason, and Back
 *
 * WHY THIS CANNOT BRICK THE DEVICE. Nothing here writes the running firmware.
 * The chain, all of it pre-existing and none of it invented for this screen:
 *
 *   1. The image streams into the OTA slot that is NOT running
 *      (esp_ota_get_next_update_partition), so the working firmware survives a
 *      power cut, a dropped connection or a corrupt download untouched.
 *   2. The chip_id in the image header is checked against the running slot's
 *      before the first byte is written, so a wrong-MCU build is refused.
 *   3. esp_ota_end() verifies the written image. A failure there leaves the boot
 *      pointer where it was.
 *   4. Only then does the boot pointer move, in one atomic otadata write.
 *   5. The bootloader boots the new image as PENDING_VERIFY. If it does not
 *      reach the end of setup() -- panel up, SD mounted, settings loaded -- it
 *      never calls esp_ota_mark_app_valid_cancel_rollback(), and the bootloader
 *      reverts to the previous image on the next boot. See network/OtaCommit.h.
 *
 * So the worst case of a bad release is one wasted reboot, not a dead reader.
 */
class OnlineFirmwareUpdateActivity : public Activity {
 public:
  enum class State {
    CHECKING,
    NO_WIFI,
    UP_TO_DATE,
    CONFIRMING,
    UPDATING,
    SUCCESS,
    FAILED,
  };

  OnlineFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OnlineFirmwareUpdate", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  // A reboot mid-download would leave a half-written slot. Harmless (the
  // running image is untouched) but pointless, so hold sleep off while it runs.
  bool preventAutoSleep() override { return state == State::UPDATING || state == State::CHECKING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }

 private:
  State state = State::CHECKING;
  OtaUpdater updater;
  std::string errorMessage;
  std::string latestVersion;
  size_t processed = 0;
  size_t total = 0;
  unsigned int lastRenderedPercent = 101;
  OptionPopup confirmPopup;
  // onEnter cannot block on the network before its first paint, or the screen
  // arrives already finished. loop() does the check on its first pass instead.
  bool checkStarted = false;

  void runCheck();
  void promptConfirmation();
  void performUpdate();
  const char* messageForError(OtaUpdater::OtaUpdaterError err) const;
};
