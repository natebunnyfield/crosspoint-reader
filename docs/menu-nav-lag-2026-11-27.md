# Menu navigation lag: "fix the jumpy delay while navigating menus" (2026-11-27)

Owner report, verbatim: *"fix the jumpy delay while navigating menus"*. Device:
the BOOX Picco, which runs this firmware on the hardware and inside the iOS app
"CrossPoint Picco" (the simulator's second app). Taken at face value: moving the
selection through Home, Settings and the file lists lags, and the highlight jumps
instead of stepping.

Surveyed at firmware `cf1bba014` (branch `picco-layout`) and simulator
`d9c66408`. Simulator commits that came out of it: `8ede3c54` (trace),
`734d887a` (beam fix), `95c2e568` (letterpress field). **No firmware source
changed.** Everything below is measured in the desktop simulator and in the iOS
Simulator (Mac-hosted, Release build of the Picco app on an iPhone 17
simulator). **Nothing was measured on the owner's iPhone or on the Picco
itself: device feel is UNCONFIRMED on both.**

## The answer in one paragraph

The iOS app and the desktop simulator had two defects in the present path, both
in the simulator, both fixed. (1) **The beam swept new frames over a stale
picture.** The 30 ms coalescing hold blocks every present, including the one
that would finish a sweep and re-read the glass, so when presses came under
~165 ms apart every new frame was swept down over the glass from *before the
burst*: the selection went back to where the burst began and then wiped forward
again, on every press: 9 of the 11 sweeps in the 150 and 100 ms bursts.
(2) **Every menu step rebuilt the whole letterpress field** (1600x960 at the
phone's 2x, ~54 ms) on the main thread, which is also the firmware's loop, so for
those 54 ms no press was read either. After both fixes the iOS Simulator shows the
first flip of a menu step at a median **55 ms** after the press (was 103) and the
complete frame at **115 ms** (was 197), with no stale sweep and no lost frame.
The **shared firmware** behavior that reads as "jumpy" on both the device and the
app is a ruled convention, not a bug, so it is an owner decision below: **holding
the rocker on a list jumps a whole screen every 500 ms and wraps** (Home steps
rows). The **device's** own delay is one whole-panel FAST waveform with a panel
power-on and power-off per step; it cannot be measured here.

## Which path each platform runs

| Stage | Picco (hardware) | iOS app / desktop |
|---|---|---|
| Rocker | GPIO21/GPIO9, logical Right/Left (`src/components/PiccoInput.h:48-73`); armed as a light-sleep wake (`lib/hal/HalPowerManager.cpp:261-265`) | on-screen rocker capsule (`injectButtonDown/Up`, a real hold) or the volume buttons (`queueButtonTap`, 60 ms taps), `ios/CrossPointIOSShim.cpp:1043-1058`, `ios/CrossPointVolumeButtons.mm:291-300` |
| Idle poll | light sleep, woken by the rocker edge | `HalPowerManager::lightSleep` is `delay(50)` (`crosspoint-simulator src/HalPowerManager.h:43-46`), no early wake |
| Step | `ButtonNavigator` press = one row; hold > 500 ms repeats every 500 ms (`src/util/ButtonNavigator.cpp:92-99`) | same firmware |
| Hold on a list | page jump, wraps last page -> first (`src/activities/UiListActivity.cpp:115-118`, `ButtonNavigator.cpp:115-131`) | same firmware |
| Render | render task, coalesces queued requests (`src/activities/ActivityManager.cpp:52`) | same, on a host thread |
| Panel | windowed refresh offered (`lib/GfxRenderer/GfxRenderer.cpp:2535-2556`) but the SSD2677 has none: whole-panel FAST, PON + refresh + POF every frame (`freeink-sdk .../PanelDriver.h:60-63`, `Ssd2677Driver.cpp:89-105`, `:133-145`) | instant; then the present: 30 ms hold (`crosspoint-simulator src/HalDisplay.cpp:899`, armed at `:1111`), field builds, 55 ms beam, glass readback -- all on the main thread that also runs `loop()` |

## Measured (iOS Simulator, Picco app, Settings, light)

Script: Home -> Settings, then 4 presses 900 ms apart, then bursts of 6 at 250,
200, 150 and 100 ms. 28 presses. "glass" = first flip carrying the press's
frame; "done" = the frame wholly on the glass (its sweep finished, or it landed
whole). One press, broken down from the `[timing]` and `[lat]` lines:

| Step | ms after the press |
|---|---|
| firmware renders, 1-bit frame written | +6 |
| coalescing hold (no compose follows on a menu) | +30 |
| letterpress panel field, rebuilt whole (1600x960) | +54 |
| texture upload, flip -> **glass** | **~+100** |
| beam sweep | +55 |
| glass readback, BEFORE the final flip | +35 |
| -> **done** | **~+195** |

| | before | after |
|---|---|---|
| press -> glass, median (max) | 102.7 (137.3) ms | **55.3 (89.4) ms** |
| press -> done, median (max) | 196.7 (231.9) ms | **114.7 (149.6) ms** |
| sweeps over a STALE picture | **9 of 27** (all in the 150 and 100 ms bursts) | **0** |
| frames written that never reached the glass | 1 | 0 |
| presses read late in the 100 ms burst (main thread busy) | 39-68 ms | 3-11 ms (40 on the first, see O3) |
| letterpress field per step | 54 ms, whole | 5.5 ms, a 194x866 box (9.7 ms with the inkness pass) |

Desktop (`simulator_picco`, 1x, software renderer, same walk, bursts at 200/150/100 ms, 22 presses):

| | before | after |
|---|---|---|
| light: press -> glass median | 92.8 ms | 53.6 ms |
| light: stale sweeps / lost frames | 9 / 1 | 0 / 0 |
| dark: press -> glass median | 72.5 ms | 72.3 ms (the dark page has no letterpress; its scanline field, ~24 ms, is untouched) |
| dark: stale sweeps | 3 | 0 |

The stale sweep is the "jumpy" half on the app: with the selection moving down
the list, the beam restarts at the top over the pre-burst glass, so below the
beam the OLD highlight is back on screen until the sweep passes it. It needed
presses under ~165 ms apart (a quick double click, or a held volume button),
which is why slow testing never showed it.

**On the glass** ([data/menu-nav-lag-2026-11-27/burst-150ms-before-after.png](data/menu-nav-lag-2026-11-27/burst-150ms-before-after.png),
composed output frames from a desktop light run, scaled 1/2 nearest-neighbor;
times on SDL's clock). Six presses 150 ms apart starting on Clock UTC Offset.
Before: at 15699 ms the highlight has reached Warmth, at 15795 and 15829 ms it
is back on Clock UTC Offset, and at 15962 and 15980 ms the screen is split, the
new page above the beam and the OLD page, still highlighting Clock UTC Offset,
below it. After: Clock UTC Offset, Brightness, Warmth, Frontlight, Restore
Light on Wake, never back; the page below the beam at 15953 ms is the frame
just before it (Frontlight). That run read the glass back on every present
(`CROSSPOINT_SIM_LOG_SCREEN=1`, `CROSSPOINT_SIM_SCREEN_DUMP`), which slows the
main thread, so its timings are not the measurement runs'.

## The fixes (simulator only)

1. **`8ede3c54` trace.** `CROSSPOINT_SIM_LOG_LATENCY` now follows each frame to
   the glass: `written`, `glass`, `swept`, `sweep-over`. `tools/latency/glass.py`
   summarizes it.
2. **`734d887a` beam.** `glasscapture::shouldArmBeam` also asks that the capture
   hold the frame the glass was showing; a frame over a stale capture lands
   whole. And the present that completes a frame no longer reads the glass back
   before its flip (rule 3 in `src/GlassCapture.h`); the next present does, from
   the same cached composition. Host test `glass_capture`.
3. **`95c2e568` letterpress.** The field is rebuilt only inside the bounding box
   of the changed inkness, grown by one pixel for the 3x3 window
   (`src/LetterpressDirty.h`). Bit-identical: host test `letterpress_dirty`
   (300 chained random edits; the ungrown box fails), and eight desktop Settings
   captures, as shipped, light, have the same md5 with and without it.

The X3 app and the packaged Mac apps run the same present path, so both fixes
reach them too; they were not measured separately.

## Decisions for the owner (nothing built)

| # | Option | Measured | Cost / risk |
|---|---|---|---|
| O1 | **RULED 2026-11-27: "Pages, no wrap"; built** (`ButtonNavigator::kHoldPagingWraps`, false on the Picco; `ListPaging.HoldPaging*`; an 8 s hold on the Picco simulator's Settings rests on the last row). **Hold-to-page on lists, on the Picco.** A held rocker jumps a whole screen every 500 ms and wraps from the last page to the first. The Picco has no other paging control (`PiccoInput.h:58-66`: PageNext/PagePrevious come from nothing). Home steps rows instead. Choices: keep; step rows while held (like Home); step rows faster (e.g. 150-250 ms, bounded by the waveform on the device); keep pages but clamp at the end instead of wrapping. | 3 s hold on Settings: 5 whole-screen jumps, alternating page 2 <-> page 1 (two frame hashes alternating) | Ruled convention, `docs/ui-conventions.md:102-111` (2026-11-07, written for the X4/X3, where the side pair also pages). Row-stepping a long file list is slower to cross than paging. |
| O2 | **RULED 2026-11-27: "Hold only before gray"; built.** The firmware calls `display.renderPassComplete()` after every `render()` (`ActivityManager.cpp`, a no-op in `lib/hal/HalDisplay.h`); the simulator releases a still-armed hold there (`presentheld::releaseAtPassEnd`), since every gray pass runs inside `render()`. Desktop, Settings, same script and card: press->glass median 54.5 -> 31.0 ms, press->done 123.2 -> 93.1 ms, 0 frames lost, 0 stale sweeps. The Colophon (chrome gray pass inside `render()`) presented 5 composed frames and no 1-bit one. The reader's 1-bit page still shows for ~600 ms before its DEFERRED gray pass (`EpubReaderActivity.cpp:369`), as it did before: that deferral was always far past the 30 ms hold. **Present hold on frames no compose follows (iOS/desktop).** The 30 ms hold exists so an anti-aliased page's 1-bit pass never flashes; a menu has no second pass, so it is 30 ms of the remaining 55 ms to first glass. | -30 ms per menu step | The simulator cannot know a compose is coming (`HalDisplay.cpp:874-885`); gating it on the reader page would flash any other screen that does paint gray (the colophon, the font picker specimen). |
| O3 | **RULED 2026-11-27: "20 ms"; built** (simulator `HalPowerManager::kHostIdleSliceMs`). iOS Simulator, 16 rested presses at 1.5 s with seeded 0-49 ms jitter, same build otherwise: read-late spread 0-48 ms (mean 23.2) at 50 ms, 0-22 ms (mean 10.2) at 20 ms; detection->glass median 34.0 / 32.7 ms. **Measurement trap:** `glass.py` times a press from its `[lat] btn-down` line, which is stamped when the firmware NOTICES the press, so it cannot see this wait at all; read-late has to come from the script's scheduled times against those stamps (spread across presses, since the two clocks have different origins). Presses at multiples of the slice also phase-lock and hide it; jitter them. **Idle slice on the host (iOS/desktop).** After 1 s idle the loop sleeps `delay(50)` and reads no input; the Picco hardware wakes on the rocker edge. First press after a rest was read 16-51 ms late (mean 31 and 39 ms over two runs of 5). A 10 ms slice would make it at most 10. | -25 to -40 ms on nearly every deliberate step | 5x the idle main-loop wakes on the phone; battery cost not measured. Pumping SDL events inside the sleep instead is NOT safe: the pad injects from an event watch, and an edge injected outside `update()` is wiped by the next `beginFrame()`. |
| O4 | **Beam paint, 55 ms** (frozen 2026-08-22, `src/SimulatorDials.h:169-175`). | done = glass + ~60 ms | The owner's look. |
| O5 | **Picco panel per step (hardware).** Every step is a whole-panel FAST: 96 KB over 20 MHz SPI (~38 ms on the wire), the LUT, power-on, the waveform, power-off. Choices: keep the panel powered between frames while input is active; a windowed refresh on the SSD2677 (its window command is 0x83 per `Ssd2677Driver.h:4-6`). The 20 MHz is the board's own (`BoardConfig.h:1449`). | not measurable here | Both depart from the stock firmware's PON/refresh/POF and need the device and the owner's eyes (ghosting, panel health). |
| O6 | **Dark page (iOS).** A dark menu step rebuilds the scanline field (desktop ~24 ms at 1x); its bloom reads the composed frame back, so the letterpress trick does not carry over directly. | not measured on iOS: the Picco app's fresh card stayed light under a dark appearance | Unknown until measured with `darkMode` set on the card. |

## On the device

Presses during a waveform are coalesced: the render task takes every queued
request in one `ulTaskNotifyTake(pdTRUE, ...)` (`ActivityManager.cpp:52`) and
draws the latest selection, so a fast double click on the hardware shows one
jump of two rows after one waveform. That is the right behavior for e-ink (a
queue would trail the finger by a waveform per press); the per-step cost is what
O5 would reduce. The FAST waveform's duration on the SE0400NQW47 is not known
(only "full refresh 426 ms", `docs/picco-port-2026-11-05.md:104`).

## Checked and found clean

- **The firmware's own work per step is small:** frame written +5-6 ms after the
  press on the iOS Simulator, +3-4 ms on the desktop.
- **No input loop takes the render lock** (`grep RenderLock` in `UiListActivity.cpp`,
  `HomeActivity.cpp`, `UiAppHost.cpp`, `MappedInputManager.cpp`, `SettingsActivity.cpp`,
  `UiTabListActivity.cpp`, `Activity.cpp`: only `render()` signatures), so on the
  device the main loop keeps polling during a waveform. On battery the render task
  light-sleeps in 20 ms slices and yields to the loop between them
  (`lib/hal/HalPowerManager.h:73`, `.cpp:318-391`), so a press during a refresh
  is sampled every ~20 ms (code read, not measured).
- **No repaint of an identical frame** on a Settings step: every written frame
  hash differs from the one before.
- **At 200 and 250 ms between presses nothing was wrong** before the fix: every
  sweep finished and was captured; the stale sweep needs < ~165 ms.
- **T-035 (keyboard entry delay) is a different measurement.** The beam fix
  applies to keyboard-grid steps too (same present path), but the letterpress
  field is already held while a text field is open, so fix 3 does not touch it.
  T-035 stays open, unmeasured.

## Adversarial review (read-only agent, 2026-11-27)

- **Fixed, latent:** `glass_capture_test` printed FAIL and still exited 0 (a
  mid-file "all checks passed" and a bare `return 0`, there before this work),
  so the new rules gated nothing. It exits 1 on any failed check now; with
  `captureIsCurrent` or rule 3 removed from a copy of `GlassCapture.h` it fails.
- **Accepted, latent:** the WIRING is untested -- `captureIsCurrent` read
  before `presentedSeq` moves, `landedWhole`, the plain present asked for after
  the flip (`HalDisplay.cpp`), and the `partial` predicate (`SurfaceSheet.cpp`).
  The two host tests cover the pure headers; the wiring is covered only by the
  trace runs above and the capture md5 gate.
- **Accepted, cosmetic, by design:** a frame can land whole where it used to
  sweep, when the next 1-bit write arms the hold before the deferred readback
  runs. About one present wide and self-correcting; the tables count it
  ("landed whole"). The ~35 ms readback still costs the main thread, one present
  later.
- **Checked clean** (per the reviewer): the glass cannot stay stale forever (every
  early return keeps the present owed or resets the glass; a present without a
  picture or with a stale size always captures first); no endless present loop;
  S-035's generation recapture is not lost; `glassSeq` is only ever set to
  `presentedSeq`; a reader page turn and the deferred gray pass sweep as before;
  no double or missing phosphor deposit; the partial field's failure paths, the
  text-entry hold, the raking light, the cache keys, the reboot, `fillField`
  against the loop it replaced, and SDL3's sub-rect upload; the trace lines do
  not change `tools/latency/gate_check.py`.

## Unconfirmed

- **Device feel, both platforms.** The iOS numbers come from the iOS Simulator
  on a Mac, not the owner's iPhone; field builds there may be slower or faster.
  The Picco was not touched (no flash, no build sent).
- **What the owner pressed.** The report did not say device or app, rocker,
  volume buttons or taps; the app findings cover the rocker capsule and the
  scripted presses that stand for it, not the volume-button path's own timing.

## Method (re-runnable)

Desktop, from the firmware worktree, a clean run directory per arm with a fresh
copy of the card and a `{}` `settings.json` (`docs/headless-qa.md` in the
simulator). The card: one book (Alice), `readerActivityLoadCount` 1, so the run
boots to Home; `darkMode` 0 in its `settings.json` for the light arm.

```bash
pio run -e simulator_picco
d=$RUN/arm; rm -rf $d; mkdir -p $d; cp -R $CARD $d/fs_; echo '{}' > $d/settings.json; cd $d
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CROSSPOINT_SIM_AS_SHIPPED=1 CROSSPOINT_SIM_GRAIN_SEED=7 \
CROSSPOINT_SIM_LOG_LATENCY=1 CROSSPOINT_SIM_LOG_PRESENTS=1 CROSSPOINT_SIM_LOG_TIMING=1 \
CROSSPOINT_SIM_INPUT_SCRIPT="$SCRIPT" timeout 120 $FW/.pio/build/simulator_picco/program > run.log 2>&1
python3 $FW/tools/latency/glass.py run.log 7600
```

`$SCRIPT`: ten `UP:60` 300 ms apart from 2500 (Home, clamped on Settings),
`CONFIRM:80` 600 ms later, then from 7600 four `UP:80` 900 ms apart and bursts of
six `UP:60` at 200, 150 and 100 ms, 1500 ms between bursts. `UP` is the rocker's
upper end (logical Right, next row).

iOS Simulator: build `crosspoint-simulator/build/ios-sim-picco` with
`xcodebuild -project crosspoint_simulator.xcodeproj -scheme CrossPointX3
-configuration Release -sdk iphonesimulator -destination "generic/platform=iOS
Simulator" build`, then for each arm `xcrun simctl ui <udid> appearance light`,
uninstall and install the app (a fresh card), and

```bash
SIMCTL_CHILD_CROSSPOINT_SIM_INPUT_SCRIPT="$SCRIPT" SIMCTL_CHILD_CROSSPOINT_SIM_LOG_LATENCY=1 \
SIMCTL_CHILD_CROSSPOINT_SIM_LOG_PRESENTS=1 SIMCTL_CHILD_CROSSPOINT_SIM_LOG_TIMING=1 \
  xcrun simctl launch --console-pty <udid> com.natebunnyfield.crosspoint.picco > console.log
python3 tools/latency/glass.py console.log 11000
```

with `$SCRIPT` = `200:QTAP:BACK:2500`, nine `UP:60` 700 ms apart from 3500,
`CONFIRM:80`, then from 11800 four `UP:80` 900 ms apart and bursts of six `UP:60`
at 250, 200, 150 and 100 ms with 1500 ms between them. The app is the shipped
page (its dials are native); `CROSSPOINT_SIM_AS_SHIPPED` is desktop only.

"Before" is `8ede3c54` (the trace alone); "after" is `95c2e568`. The
bit-identity gate built `95c2e568` twice, once with the partial path forced off,
and compared eight captures, 800 ms after each step.

Limits: one card, one screen (Settings) for the bursts, light page on iOS only,
one run per arm (the iOS medians are over 28 presses each; spread is in the
tables as max). The script's own clock is `millis()`, so a press the main thread
was too busy to read shows up as a late `btn-down`, not as a longer latency:
"read late" in the tables is that lateness.
