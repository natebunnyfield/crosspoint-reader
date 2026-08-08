#pragma once

#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> push FileBrowserActivity in PickFirmware mode (only .bin files visible).
 *  2) On result: validate the .bin (header magic, size fits OTA partition).
 *  3) Show an in-place OptionPopup ("Update firmware?").
 *  4) On confirm: stream the file into the OTA partition via the Arduino Update API,
 *     drawing a progress bar; on success ESP.restart().
 *
 * Used both from Settings -> System -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (left side button + power on X3).
 */
class SdFirmwareUpdateActivity : public Activity {
 public:
  enum class State {
    PICKING,
    VALIDATING,
    CONFIRMING,
    UPDATING,
    SUCCESS,
    FAILED,
  };

  // `preselectedPath` skips the picker and goes straight to validate +
  // confirm. Used by the file-transfer screen when a firmware image arrives
  // over WebDAV: the file has already been chosen, by uploading it.
  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false,
                                    std::string preselectedPath = "")
      : Activity("SdFirmwareUpdate", renderer, mappedInput),
        recoveryMode(recoveryMode),
        preselectedPath(std::move(preselectedPath)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::UPDATING || state == State::VALIDATING; }
  bool skipLoopDelay() override { return state == State::UPDATING; }

 private:
  State state = State::PICKING;
  bool recoveryMode = false;

  std::string preselectedPath;
  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;
  OptionPopup confirmPopup;

  void launchPicker();
  void beginValidateAndConfirm();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
