#pragma once

// The other half of a safe OTA: confirming a new image once it has proved it
// boots.
//
// WHY THIS FILE EXISTS. The write side was already careful -- OtaUpdater streams
// into the INACTIVE slot, so the running firmware is never touched, and
// esp_ota_end() verifies the image before the boot pointer moves. What was
// missing was the part that makes a bad-but-valid image survivable: an image can
// pass every static check and still fail to run on the device.
//
// The ESP-IDF answer is a two-phase commit, and this project already had two of
// its three pieces:
//
//   1. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y      (sdkconfig.defaults) -- set
//   2. boot entry written as ESP_OTA_IMG_NEW        (OtaBootSwitch.cpp)  -- set
//   3. the new image calling esp_ota_mark_app_valid_cancel_rollback()    -- MISSING
//
// With 1 and 2 but not 3, the bootloader flips NEW to PENDING_VERIFY and boots
// the new image; on the boot AFTER that it sees PENDING_VERIFY still standing,
// marks the image ABORTED and rolls back to the previous slot. So before this
// file, an OTA update could never stick -- it would install, boot once, and
// revert. Fail-safe, but useless.
//
// WHERE THE CALL GOES IS THE WHOLE DESIGN. Confirming too early confirms an
// image that has not proved anything; confirming too late risks never
// confirming a healthy one. It is called at the END of setup(), which means the
// image has already:
//
//   * booted far enough to run C++ static init and reach setup()
//   * brought up the display and the panel driver
//   * mounted the SD card (setup() returns early on failure, before this)
//   * loaded settings, app state and recent books off that card
//   * routed to an activity and had its first frame requested
//
// Those are the failure modes that actually brick a reader. What this cannot
// catch is a crash that happens later -- a fault in a specific book, or on a
// screen nobody opened during boot. That is inherent: any scheme with a finite
// confirmation point has a window after it. Said plainly here rather than
// implied, because the alternative -- confirming after N minutes of uptime --
// buys a little more coverage for a much worse failure mode, an image that
// silently reverts hours later while the owner is reading.
//
// A power cut mid-update is safe at every point: the new image is only ever
// written to the slot that is NOT running, and the boot pointer moves in one
// atomic otadata write after the image verifies.
namespace ota_commit {

// Confirm the running image if the bootloader is waiting on it. No-op when the
// image is already valid (every normal boot) or when rollback is not compiled
// in. Safe to call unconditionally; logs what it found either way.
void confirmBootIfPending();

}  // namespace ota_commit
