# Picco: light rows follow the slide switch, selection fix, three build gates (2026-11-06)

Branch `picco-a`, from 893709214; SDK `picco-a-sdk` 5f34795 -> 1dc07f6.
Follows `docs/picco-input-settings-2026-11-06.md` (the previous build). The
port's running record is `docs/picco-port-2026-11-05.md` in the PRIMARY
checkout (untracked there); it should get a one-line pointer to this file.

## What changed, by ruling

| Item | Commit | Where |
|---|---|---|
| "Hide it then": Restore Light on Wake only while the switch is bound to Dark Mode | 3d9b5815e | `SettingsActivity.cpp` `isHiddenLightRow` (:73), used at :139 |
| "Add the row then": Frontlight on/off row, same rule | 45cf4c10c | `isPiccoDeviceRow` (:58), `isFrontlightToggle` (:81), toggle :423, value text :642 |
| Bug: flipping Slide Switch moved the selection (landed on Wi-Fi Networks) | e836d730b | `rebuildSettingsLists` :96 (remember) and :226 (restore) |
| Gate 1: `picco-gh_release` in `RELEASE_ENVS` | ddd7ba612 | `scripts/check_editor_faces.py:35` |
| Gate 2: board tag exactly once in every device bin | 030ecb5db | `scripts/check_board_tag.py`, `platformio.ini:242`, `test/check_board_tag/` |
| Gate 3: SSD1677 40 MHz default host test | SDK 1dc07f6, gitlink cdd47c3ef | `freeink-sdk/libs/display/FreeInkDisplay/test/host/test_picco.cpp:137` |

All firmware C++ is under `FREEINK_DEVICE_PICCO`. Editing `platformio.ini`
changed `project.checksum`, so the first `pio run` after 030ecb5db wiped every
env build dir (expected, CLAUDE.md "Editing platformio.ini wipes every build
directory").

## Design decisions made here (not ruled)

- **Upstream's whole rule, not half of it.** The ruling says
  `!slideSwitchOwnsLight()`; upstream hides the row on
  `!Frontlight.present() || slideSwitchOwnsLight()` (upstream/develop
  `src/SettingsList.h:281-282`), so the presence half came too. The Picco
  always has the light, so it changes nothing there.
- **The Frontlight row toggles the light as it is, and reads it as it is.**
  Upstream's `toggleFrontlight()` (upstream/develop `src/main.cpp:211-218`)
  and reader-menu toggle (`EpubReaderMenuActivity.cpp:177-183`) do
  `lightOn = !Frontlight.isOn()` and save that. The generic toggle flips the
  stored byte instead; after a wake with Restore Light on Wake off,
  `frontlightOn` reads 1 while the light is off, so it would take two presses
  to light it and the row would say On over a dark screen. Hidden on the same
  rule as Restore Light on Wake (upstream's reader menu shows its light toggle
  only on `Frontlight.present() && !slideSwitchOwnsLight()`, :83).
- **The selection fix is Picco-only.** On the X4 the list never changes shape
  on a rebuild, so remembering the row there would be dead code that moves
  the X4's bytes. Row identity is `(valuePtr, nameId)`.
- **Board-tag gate: the expected tag is read from `FirmwareBoardTag.cpp`, not
  from the env name.** The env names do not match the tags (`default`,
  `gh_release`, `slim` -> `x4`; `picco-gh_release` -> `picco`). The script
  parses the `#if/#elif FREEINK_DEVICE_*` chain and evaluates it on the env's
  CPPDEFINES, first match wins, a 0 value is false. Only `A || B` conditions
  and literal names are accepted; anything else fails the build. It also fails
  on any OTHER board tag in the image, because the device's
  `board_tag::Scanner` rejects an image carrying a tag that differs. On
  failure it deletes `firmware.bin`, because SCons would otherwise find the
  bad bin up to date on the next run and the gate would never run again.
- **Gate 3 lives in the existing Picco harness** (`run_picco.py`), run once per
  panel, rather than a new harness: that is the only host build of
  `Ssd1677Driver.cpp` in the SDK.

## Evidence

- `CheckBoardTagTest` passes (`ctest --test-dir build/test -R CheckBoardTagTest`).
  Six mutations of the script, each killed: duplicate allowed, foreign tag
  ignored, a 0 flag counted, last match wins, an unreadable condition
  guessed, "any exact tag passes".
  **Trap found:** the first mutation pass reported wrong results. Python
  reused `scripts/__pycache__/check_board_tag.cpython-314.pyc` because two
  mutations had the same file size and the same mtime second. Rerun with
  `PYTHONDONTWRITEBYTECODE=1` and the cache deleted; the results above are
  from that rerun.
- `run_picco.py`: all five panels pass. Mutation: `Ssd1677Driver::spiHz()`
  default set to 20000000 makes the yrd run abort (SIGABRT). Restored.
- `check_editor_faces.py` run with a stub env on an empty font dir:
  `picco-gh_release` exits 1, `picco` 0, `gh_release` 1. This worktree has no
  licensed editor faces, so the default and picco builds below carry the
  warning and no PragmataPro/Nitti, the same as the previous build.
- **Default (X4/X3)** `pio run -e default`: SUCCESS, RAM 55,700 -> 55,700,
  Flash 4,969,709 -> 4,969,701. The -8 B is all `.flash.rodata`: the two
  `__TIME__` strings (OnlineFirmwareUpdateActivity, SdFirmwareUpdateActivity)
  were 20:18:42 and 20:18:43 in the old bin and are one string now. That is
  the +8 B the previous build recorded, reversed, so against 1d623d68c's
  4,969,701 it is +0. Every other loaded section is the same size
  (`riscv32-esp-elf-size -A`; only `.debug_*` moved). `nm -S --size-sort -C`
  sizes and names identical (17,449 lines). Gate printed
  `check_board_tag: .pio/build/default/firmware.bin carries CROSSPOINT-BOARD-V1:x4; exactly once`.
  The non-Picco source is unchanged: `unifdef -DFREEINK_DEVICE_PICCO=0` on
  `SettingsActivity.cpp` and `SettingsList.h` gives identical output at
  893709214 and HEAD.
- **The gate fires inside a real build.** `scripts/check_board_tag.py`
  temporarily forced to expect `picco`, `firmware.bin` deleted,
  `pio run -e default`: `ERROR: scripts/check_board_tag.py:
  .pio/build/default/firmware.bin (env default): 'CROSSPOINT-BOARD-V1:picco;'
  occurs 0 times, not once; other board tags present:
  'CROSSPOINT-BOARD-V1:x4;'. Deleted it so the next build relinks.`,
  `[FAILED]`, and the bin was gone. Script restored (`git status` clean),
  rebuilt: SUCCESS and the gate passed. The script's functions on copies of the
  real default, sticky and picco bins with the magic overwritten report
  "no ... in the image" for each.
- **Sticky** `pio run -e sticky`: SUCCESS, RAM 65,752, Flash 4,740,207, gate
  passed on `CROSSPOINT-BOARD-V1:sticky;`. No sticky baseline existed in this
  tree, so "unchanged" there rests on the unifdef check above (its source is
  the PICCO=0 side) and the SDK change being a test file only.
- **Picco** (generated `sdkconfig.defaults` moved aside, "Reinstall Arduino
  framework" at log line 87, `pio run -e picco -t upload_plan`): SUCCESS, RAM
  67,840 (same), Flash 4,776,166 -> 4,776,618 (+452 B, the new Picco code),
  zero compiler warnings in the log. Gate passed on
  `CROSSPOINT-BOARD-V1:picco;`. `firmware.bin` 4,777,120 B, sha256
  `2dd576bb753cc16840cdb1078cea539a30df682eff129a1195dec2123e614707`. Python
  header parse: magic E9, chip id 9, 7 segments, checksum FB valid, appended
  SHA-256 valid, image end = file length. upload_plan "OK, one image:
  .pio/build/picco/firmware.bin at 0xe0000", `esp_psram_init` and the octal
  driver placed.
- Not run: any device step, PiccoInputTest and KeyboardTouchTest (no input
  code touched).

## Checked and found clean

- Nothing applies the web page's `frontlightOn` edit to the light live
  (`grep -rn frontlightOn src`: only main.cpp's switch handler and wake rule,
  and now the Settings row). Unchanged; the device row applies its own.
- The simulator envs do not extend `[base]` and keep their own
  `extra_scripts` list (`platformio.ini` `[env:simulator]`), so the new
  post-script never loads there.
- `stamp_app_desc.py` and the new gate are both `AddPostAction`s on the same
  bin, run in `extra_scripts` order (the build log shows stamp, then
  `_check`); stamp edits only the descriptor, never the tag.
- No new strings: STR_FRONTLIGHT exists in english.yaml:70 and spanish.yaml:66,
  STR_STATE_ON/OFF are what `settingrow::valueText` already uses.

## UNCONFIRMED on device

With the switch bound to Frontlight: Settings shows Dark Mode, and after
Clock UTC Offset only Brightness, Warmth and Slide Switch. Flip Slide Switch to Dark
Mode: the selection stays on Slide Switch, Dark Mode's row goes, Frontlight
and Restore Light on Wake appear after Warmth; Confirm on Frontlight turns
the light on and off and the row reads the light. Flip back: those two go,
the selection still on Slide Switch.
