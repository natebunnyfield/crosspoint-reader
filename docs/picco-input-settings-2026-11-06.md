# Picco: iOS gesture timing, stay awake on USB, light rows (2026-11-06)

Branch `picco-a`, from 1d623d68c; SDK `picco-a-sdk` 5f34795 (unchanged).
The port's running record is `docs/picco-port-2026-11-05.md` in the PRIMARY
checkout (untracked there, absent from this branch); this file is the record
for this build and should get a one-line pointer from it.

## What changed, by ruling

| Item | Commit | Where |
|---|---|---|
| "Match iOS exactly": tap <= 400 ms and <= 28 px, hold = 750 ms, nothing in between | e0f571473 | `src/components/PiccoInput.h` constants, `classifyRelease` |
| Review minor: a lost TMA525C lift-off cannot become a Confirm hold | d639d3ae9 | `GestureDecoder::feed`, `HalGPIO::readTouchContact` (report time) |
| Review minor: a gesture does not release a rocker end that is held | bd241ebe8 | `piccoinput::canSynthesizeTap`, `MappedInputManager::synthesizeGesture` |
| Idle sampling: 100 Hz for 1 s after any contact report | 426f41174 | `piccoinput::pollFast`, `main.cpp touchContactDown`, `HalGPIO::lastTouchReportMs` |
| Stay awake on USB | 13869607b | `main.cpp loop`, before the idle-timeout sleep |
| "Two Settings rows": Brightness, Warmth | c3ab67511 | `SettingsActivity.cpp` (`isRockerStepped`, `loopLightAdjust`, `stepLight`) |
| "Add both rows": Restore Light on Wake, Slide Switch | 1ffda21f9 | `SettingsActivity.cpp isPiccoDeviceRow` |

All of it is under `FREEINK_DEVICE_PICCO`.

## Design decisions made here (not ruled)

- **Stepping is a mode on the row.** The rocker is the Picco's only list
  navigation (Right/Left = next/previous row), so it cannot also step a value
  on a row the selection merely passes. Confirm on Brightness or Warmth starts
  stepping (value column reads `< 60 >`, hints `Done / Down / Up`); the
  rocker's upper end raises, the lower lowers, 5 per press, repeating while
  held, clamped at 0 and 100 (never wraps); every step goes to `HalFrontlight`
  at once; Confirm or Back ends it and saves once if the value moved. The
  alternative, the fork's existing VALUE-row behavior (Confirm adds 5 and wraps
  100 -> 0, `SettingRowUi.h:156-162`), does not use the rocker at all.
- **Hold freshness limit 200 ms** (`HOLD_REPORT_GAP_MS`): a hold needs a
  contact report at most 200 ms old when it fires and no gap over 200 ms
  between reports since touch-down. A guess, not a measured report rate.
- **Fast-poll window 1 s** (`CONTACT_FAST_POLL_MS`), the same length as the
  existing post-activity 100 Hz window (`HalPowerManager::IDLE_LIGHT_SLEEP_MS`).
- **On USB the idle timer restarts** instead of sleeping, so after unplugging
  the device sleeps within one timeout. VBUS is the test (`isUsbConnectedCached`:
  SGM41562 external power or USB SOF, `HalGPIO.cpp:194`), so a wall charger
  keeps it awake too.

## What the user sees on USB

The screen they left stays up, unchanged, with the frontlight as it was. No
sleep screen and no charge strip appear (the strip belongs to sleep,
`main.cpp holdSleepWhileUsbPowered`). Pressing Power still sleeps it, and a
sleeping Picco on USB is still unreachable over USB-Serial/JTAG. On battery
nothing changed.

## Battery cost of the 1 s fast-poll window

Reasoned, not measured. Per contact that fires nothing, at most 1 s of the
100 Hz delay-poll instead of 50 ms light-sleep slices. A recognized gesture
costs nothing extra: its synthesized press is activity, which already buys the
same 1 s of 100 Hz polling. Upstream's X3 figures for the two idle states,
9.68 mA and 2.78 mA (`docs/device-verification-checklist.md:108`), put it at
about 6.9 mA for up to 1 s, about 0.002 mAh per such contact. The Picco (ESP32-S3)
has never been measured.

Limit: the first tap after a long idle is still sampled at the slice rate,
because nothing has reported yet. The touch INT (GPIO11, active low,
`BoardConfig.h:1465-1468` in the SDK) is not a light-sleep wake source;
arming it would fix that case at zero idle cost and was not done (not asked).

## Evidence

- `PiccoInputTest` 25/25 (`cmake --build build/test --target PiccoInputTest`).
  New: 399/400/401 ms, 28/29 px on both axes, 749/750 ms, 59 px drift is
  nothing, lost lift-off, reports resuming after a gap, reports every 200 ms
  hold and every 201 ms do not, held-button drop, poll window edges,
  `stepClamped`. Mutation checks, each made one test fail: TAP_MAX_MS 399 and
  401, LONG_PRESS_MS 749, TAP_SLOP_PX 29, the gap latch removed, the freshness
  test removed, the tap time limit removed, the tap slop test removed,
  `canSynthesizeTap` always true, the poll window 1001, the clamp removed.
- Default (X4) env: RAM 55,700 -> 55,700, Flash 4,969,701 -> 4,969,701, and
  `nm -S --size-sort -C firmware.elf` byte-identical (17,449 lines) before and
  after; no new symbol names.
- Picco env (`pio run -e picco -t upload_plan`, after moving the generated
  `sdkconfig.defaults` aside; "Reinstall Arduino framework" printed): SUCCESS,
  RAM 67,840, Flash 4,776,074. `firmware.bin` 4,776,576 B (1d623d68c's was
  4,775,264), sha256 `373e314b…a150f`. Image header parsed in Python (not
  esptool): magic E9, chip id 9, 7 segments, checksum and appended SHA-256
  valid. Board tag `CROSSPOINT-BOARD-V1:picco` occurs once. upload_plan: one
  image at 0xe0000, `esp_psram_init` and the octal driver placed, "OK".
- Not run: KeyboardTouchTest (no keyboard code touched), any device step.

## Checked and found clean

- No other Picco idle-sleep path: `enterDeepSleep` is called only from
  `main.cpp` (idle timeout, low-battery guard, Power hold); the low-battery
  guard already passes `isUsbConnected()`.
- The main-loop light sleep and the e-ink busy slice already stand down on USB
  (`HalPowerManager.cpp:202`, `:267`).
- `synthesizeGesture` reads the physical button level: `HalGPIO::update()`
  clears the previous frame's synthesized level before it runs.
- No new strings were needed (STR_BRIGHTNESS, STR_WARMTH,
  STR_RESTORE_LIGHT_ON_WAKE, STR_SLIDE_SWITCH, STR_DONE, STR_DIR_UP/DOWN all
  exist with Spanish), so no yaml edit and no `gen_i18n` run.

## UNCONFIRMED on device

Everything above. To observe: a quick tap = next, a 0.5 s press = nothing, a
still 0.75 s press = Confirm; holds still fire on a resting finger (if not,
the TMA525C pauses its reports and `HOLD_REPORT_GAP_MS` must grow); left
idle on USB it stays on the screen it was on and esptool reaches it; Settings
shows the four rows after Clock UTC Offset and Brightness/Warmth move the
light as the rocker steps them.
