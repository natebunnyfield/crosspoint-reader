# One-button firmware update

Home → **Update Firmware** → it checks this fork's GitHub releases, asks once,
installs, reboots. Added 2026-08-16.

There is no picker and no settings sub-tree, because the ask was one button.
The SD-card path (Settings → SD Card Firmware Update) is untouched and remains
the way to install a `.bin` you already have.

## What was already there, and what was missing

Almost all of the machinery existed and had **no caller** — `OtaUpdater` was
referenced only by its own header and `.cpp`, verified by grep across `src/`,
`lib/` and `freeink-sdk/` on 2026-08-16. What this change added is mostly wiring,
plus the one genuinely missing safety piece.

| Piece | Where | Before |
|---|---|---|
| Release check (GitHub JSON, streamed) | `OtaUpdater::checkForUpdate` | existed, no caller |
| Streaming install to the inactive slot | `OtaUpdater::installUpdate` | existed, no caller |
| Dual OTA slots | `partitions.csv` (`app0`/`app1`, 0x640000 each) | existed |
| Bootloader rollback | `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` | existed |
| Boot entry written as `ESP_OTA_IMG_NEW` | `OtaBootSwitch.cpp` | existed |
| **Confirming a healthy boot** | `network/OtaCommit.cpp` | **missing** |
| Release URL pointing at this fork | `OtaUpdater.cpp` | pointed at UPSTREAM |
| A way to reach any of it | Home row + `OnlineFirmwareUpdateActivity` | missing |

## Why it cannot brick the device

Five things, in order. Only the last one was new.

1. **The image is written to the slot that is not running.**
   `esp_ota_get_next_update_partition()` picks the other of `app0`/`app1`, so a
   power cut, a dropped connection or a corrupt download cannot touch the
   firmware currently working.
2. **The chip id is checked before the first byte is written.** The first 14
   bytes of the stream are buffered and `chip_id` (offset 12 of
   `esp_image_header_t`) is compared against the running slot's. A build for a
   different MCU is refused with `WRONG_DEVICE_ERROR` and nothing is written.
3. **`esp_ota_end()` verifies the written image.** If it fails, the boot pointer
   never moves.
4. **The boot pointer moves last, in one atomic otadata write.** Up to this
   instant the device still boots the old firmware.
5. **The new image must prove it boots.** The bootloader starts it as
   `ESP_OTA_IMG_PENDING_VERIFY`. If it does not reach the end of `setup()`, it
   never calls `esp_ota_mark_app_valid_cancel_rollback()`, and **the bootloader
   reverts to the previous image on the next boot.**

Step 5 is the part that did not work before. Rollback was enabled in
`sdkconfig.defaults` and `OtaBootSwitch` already wrote `ESP_OTA_IMG_NEW`, but
nothing in the firmware ever called `esp_ota_mark_app_valid_cancel_rollback()` —
so an update would have installed, booted once, and reverted. Fail-safe, and
useless.

### Where the confirmation happens, and why there

`ota_commit::confirmBootIfPending()` is the **last statement in `setup()`**.
Reaching it means the image has:

* run static init and reached `setup()`
* brought up the display and panel driver
* mounted the SD card (`setup()` returns early above if it did not)
* loaded settings, app state and recent books off that card
* routed to an activity

Those are the failure modes that actually brick a reader.

**What it does not cover, stated plainly:** a crash that happens later — in a
particular book, or on a screen nobody opened during boot. Any scheme with a
finite confirmation point has a window after it. Confirming after N minutes of
uptime would buy slightly more coverage at the cost of a much worse failure
mode: an image that silently reverts hours later while someone is reading.

## The release it looks for

`https://api.github.com/repos/natebunnyfield/crosspoint-reader/releases/latest`,
and the parser wants a **`firmware.bin`** asset attached to that release. No
asset means `NO_UPDATE`, which the screen reports as "Already up to date" rather
than as a failure.

Version comparison is `isUpdateNewer()`: semver on three segments, and an `-rc`
build takes an equal-numbered release. The old `-BNY` carve-out was removed with
this change — it existed only because the URL pointed at upstream, where an
equal-numbered release would have been *someone else's* firmware.

## Testing it without a device

The simulator provides `OtaUpdater` from `src/simulator_ota.cpp`, driven by env
vars, so every state is reachable headlessly:

```bash
CROSSPOINT_SIM_OTA=available \
CROSSPOINT_SIM_OTA_VERSION=9.9.9 \
CROSSPOINT_SIM_OTA_INSTALL=ok \
  .pio/build/simulator/program
```

`CROSSPOINT_SIM_OTA` takes `none` (default) / `available` / `error`;
`CROSSPOINT_SIM_OTA_INSTALL` takes `error` (default) / `ok` / `cancel`. The
screen needs a Wi-Fi connection first — join the fake network through
Settings → Wi-Fi Networks, which connects instantly.

Verified this way on 2026-08-16: the Home row opens the screen, "No Wi-Fi
connection" appears without a connection, "Already up to date" with no release,
the "Update firmware? / 9.9.9" prompt with one, and Confirm runs
`installing 9.9.9` → `install complete, restarting into the new image`.

**Not verified on hardware.** The simulator cannot flash a partition, so what is
proven here is the flow, the states and the wiring — not the write, the boot
switch, or the rollback. Those need a device and a real release.

## iOS

Excluded. `CROSSPOINT_NO_DEVICE_FLASH` gates the Home row, the route, both index
maps and the menu count, and `cmake/CrossPointIOSExclusions.cmake` drops
`OnlineFirmwareUpdateActivity.cpp` from the source set. A phone can do the
download half and not the write half, so the whole screen goes rather than
offering something that cannot finish.

## The Home menu has five sources of truth

Adding this row meant touching all of them, and the comment in
`HomeActivity.h` said four:

1. `menuItemToIndex()` — `HomeActivity.h`
2. `indexToMenuItem()` — `HomeActivity.h`
3. `menuItems` vector — `HomeActivity.cpp`
4. `menuIcons` vector — `HomeActivity.cpp`
5. **`getMenuItemCount()`'s literal** — `HomeActivity.cpp`

A mismatch in 1–4 opens the wrong screen; a stale 5 renders a row the selector
can never reach. The comment now says five.
