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
| Review fix: a main-loop stall no longer cancels a hold | ec7363182 | `GestureDecoder::feed` (silence judged by a poll) |
| Review fix: a second tap on a lost lift-off is not a swipe | 5112f2424 | `GestureDecoder::feed`, `NEW_CONTACT_GAP_MS` |
| Review fix: the touch INT wakes a light-sleep slice | f09e426e2 | `HalPowerManager::lightSleep` |

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
- **Hold freshness limit 200 ms** (`HOLD_REPORT_GAP_MS`): once a poll finds
  the latest contact report more than 200 ms old, the contact cannot hold
  (since ec7363182; before, the gap was measured between reports, so a
  main-loop stall looked like silence). A guess, not a measured report rate.
- **Second-finger gap 50 ms** (`NEW_CONTACT_GAP_MS`): a report that ends a
  polled silence of at least 50 ms and lands more than 28 px from the last
  reported point ends the contact with nothing fired. Also a guess.
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

The first tap after a long idle got nothing from that window, because
nothing had reported yet. Since f09e426e2 the touch INT (GPIO11, active low,
`BoardConfig.h:1468` in the SDK) is also a light-sleep wake source, so that
tap wakes its slice at once. That costs nothing while INT idles high, and
one early wake per touch. It is armed only when INT reads high at entry, so a
stuck-low line falls back to the old 50 ms cadence instead of spinning.
Unknown: whether INT pulses with no finger on the glass, which would add
wakes. Not done: the e-ink busy slice (`onEinkBusyWaitSlice`) still samples
touch once per slice while a refresh runs.

## Review findings on this build (2026-11-06) and what was done

1. **The 200 ms hold-freshness rule may stop real holds firing** (PLAUSIBLE).
   The SDK's commit f8274f1 and its host test (`test_picco_touch.cpp:49-62`)
   say the TMA525C may go quiet on a resting finger and model a long press
   as one report followed by silence. If it does, report timing cannot tell a
   resting finger from a lost lift-off, hold = Confirm never fires, and only
   swipe-down gives Confirm. **Not resolved: it needs a measurement.**
   Building with `TOUCH_PROBE_DEBUG` logs every report
   (`InputManager.cpp:1232-1236`). The owner then rules which failure to
   accept, or names another signal. The finding's second path was fixed in
   ec7363182: a main-loop stall over 200 ms during a hold no longer cancels
   it, because silence now counts only when a poll sees it.
2. **A lost lift-off plus a second tap could still be Confirm** (CONFIRMED
   on the host with the review's sequence). Fixed in 5112f2424 by the
   second-finger rule above. Still possible: a second tap that lands within
   50 ms, or within 28 px of the stale point, joins the first. The pair is
   then a tap, or a swipe if the first tap had drifted. The review's other
   suggestion, letting any 200 ms gap cancel the release too, was not taken.
   Its only gain is that narrow drifted case, and it would drop real taps
   longer than 200 ms if the controller does go quiet on a still finger.
3. **Item 5 missed the first tap after idle** (CONFIRMED by reading
   `HalPowerManager.cpp`). Fixed in f09e426e2 (the INT wake above).
   UNCONFIRMED on device.

Test-build trap found while doing this: after the header was swapped back
and forth with `cp`, `cmake --build build/test` twice reported a stale
PiccoInputTest (a test failed on the correct header, then passed after
`rm` of `PiccoInputTest.cpp.o`). The mutation checks below were rerun with
the object deleted before each build. The earlier mutation list was not
rerun.

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

After the review fixes (head f09e426e2):
- `PiccoInputTest` 31/31. New: a 400 ms poll-less stall still holds at
  1750 ms; a lost lift-off across a stall does not hold; a poll that finds
  the report 200 ms old holds and one that finds it 201 ms old does not; the
  review's second-tap sequence in four directions fires nothing; the 49/50 ms
  and 28/29 px second-finger edges; a 25 Hz swipe and a pause then a fast
  swipe are still swipes. Mutations, each killed by at least one test:
  `NEW_CONTACT_GAP_MS` 49 and 51, the jump test `>=` and `+1`, the consume
  removed, the silence reset removed, the silence latch removed,
  `HOLD_REPORT_GAP_MS` 199 and 201, `!reportGap_` dropped from the hold.
  The review's repro program now prints `fired=0`.
- Default (X4): RAM 55,700 -> 55,700. Flash 4,969,701 -> 4,969,709. The +8 B
  is all `.flash.rodata`, from the two `__TIME__` strings in
  OnlineFirmwareUpdateActivity and SdFirmwareUpdateActivity. They were
  compiled one second apart (20:18:42, 20:18:43), so they no longer merge
  into one string. Every other section is the same size (`riscv32-esp-elf-size
  -A`). `nm -S --size-sort -C` names and sizes are identical. Addresses after
  that string shift by 8.
- Picco (sdkconfig.defaults moved aside; "Reinstall Arduino framework" at log
  line 87; `-t upload_plan`): SUCCESS, RAM 67,840, Flash 4,776,166, no
  compiler warnings at all in the log. `firmware.bin` 4,776,672 B, sha256
  `9bde8d0c…e24141`. Python header parse: magic E9, chip id 9, 7 segments,
  checksum and appended SHA-256 valid, image end = file length. Board tag
  once. upload_plan "OK, one image ... at 0xe0000".

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
the TMA525C pauses its reports: see review finding 1 above, which is a
ruling question, not a bigger number); a tap after several seconds idle
registers as reliably as one mid-run; left
idle on USB it stays on the screen it was on and esptool reaches it; Settings
shows the four rows after Clock UTC Offset and Brightness/Warmth move the
light as the rocker steps them.
