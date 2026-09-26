# Update Fonts / Update Library: "appearing frozen" (2026-09-26)

Owner report, taken as stated: *"not appearing frozen when i select Update
Library and Update Fonts"* -- selecting either makes the reader look frozen.

Surveyed at `12f1c852d` (firmware) against `crosspoint-simulator` `main` of the
same day. Line numbers below are from `12f1c852d` unless marked "now".
Instruments: `tools/update_progress_repro/` (README there).

## 1. The trace, selection to glass

| Step | Where (at 12f1c852d) | What it does |
|---|---|---|
| Menu selection | `SettingsActivity.cpp:353` (fonts, pushed) / `HomeActivity.cpp:568` -> `ActivityManager.cpp:318` (library, replaced) | stages the activity; `onEnter()` runs inside the transition tick |
| First frame | `FontUpdateActivity::onEnter` / `LibraryUpdateActivity::onEnter` | `requestUpdateAndWait()` -- the CHECKING frame IS drawn before any network call (a 2026-09 fix, still correct) |
| Manifest check | `FontUpdateActivity.cpp:123` -> `runCheck()`; `LibraryUpdateActivity.cpp:63` | ONE `loop()` call: token, TLS + release JSON, manifest download + streaming parse |
| Per item | `FontUpdateActivity.cpp:137` -> `syncNextFamily()` -> `:292 updater.syncFamily()`; `LibraryUpdateActivity.cpp:75` -> `:198 updater.syncBook()` | ONE `loop()` call per family / book: recovery, stamps, **SHA-256 of every installed file** (`FontUpdater.cpp:869` -> `:675`; `LibraryUpdater.cpp:454`), then every download |
| Progress callbacks | `FontUpdater.cpp:752`, `LibraryUpdater.cpp:514` -> activity `requestUpdate(true)` (`FontUpdateActivity.cpp:281`) | fire once per percent of the FILE being DOWNLOADED; **none during the hash** |
| Render gate | `FontUpdateActivity.cpp:396`, `LibraryUpdateActivity.cpp:296` | `if (pct == lastRenderedPercent) return;` -- repaints only when the WHOLE RUN's percentage moves |
| Present (host) | `crosspoint-simulator/src/simulator_main.cpp:373` `loop();` then `:419` `display.presentIfNeeded();` | the ONLY place a host puts pixels on the glass, and it runs after `loop()` returns |

### The death points

**Host builds (iOS app, Mac apps) -- the glass stops.** `simulator_main.cpp:373`
calls `loop()` and `:419` presents after it returns, on the same main thread. The
render task does draw and convert every frame the callbacks request, but none is
presented until the tick ends, and one tick was one whole family
(`FontUpdateActivity.cpp:137`/`:292`) or one whole book
(`LibraryUpdateActivity.cpp:75`/`:198`). The same thread pumps SDL events
(`HalGPIO::update`), so for the same span no touch, key or Back is read -- on a
phone, an app that has stopped. The manifest check (`:123`) is one tick too, so
its "Reading the font list" step (requested from inside it) never reached the
glass either.

A second host effect makes it worse: the host transport buffers a whole body
before handing it over (`SimHttpFetch.h:411 fetch()` fills `Response.body`), so
even inside the tick the per-chunk callbacks fire in one burst at the end of each
file.

**Every build, device included -- nothing to say.**
1. The hash of installed files had **no progress callback at all**
   (`FontUpdater::countMatchingFiles` -> `computeCardSha256`, `FontUpdater.cpp:675`;
   `LibraryUpdater.cpp:454`). A card with no ledger -- every card the iOS app
   seeds, and any card whose `font_sync.json` is lost -- hashes ~80 MB of fonts
   with the screen parked on "Font k of N".
2. The render gate (`:396`, `:296`) repainted only when the whole-run percentage
   moved. The hash never moves it, and on a 13-family run 1% of the run is most of
   a megabyte of download.
3. Back is sampled by polling at the top of each loop iteration (`main.cpp:1098`
   -> `HalGPIO::update` -> `inputMgr.update()`; no `attachInterrupt` in `lib/hal` or
   `src`, grep 2026-09-26 -- the freeink-sdk input layer was not audited), so a press made and released while a
   family blocks the loop task is never seen. Fonts' cancel was reachable only at
   the instant between two families; Library had no cancel at all.

## 2. Measured (desktop `simulator_x3`, mock release, 1 MB/s link)

Fixture: Edgar installed with no ledger (hashed, unchanged), Doves and Coelacanth
to download (7.1 and 8.3 MB), library of three books incl. a 6 MB one. Numbers are
the longest interval with no **new picture** presented, from activity entry to the
run's summary (`gaps.py`; trail-decay re-presents of the same picture excluded).

| Run | Before (`12f1c852d`) | After |
|---|---|---|
| Update Fonts, 3 families | **18,708 ms**; 4 new pictures in 37.7 s | **1,045 ms**; 47 new pictures |
| Update Library, 3 books | **8,591 ms**; 4 new pictures in 11.9 s | **1,009 ms**; 16 new pictures |
| Update Fonts + Back at 41.0 s | Back unreadable until the family ended | Back logged at 41,001 ms, "Stopping after this font" painted, stopped after Doves; max gap 1,002 ms |

The ~1 s ceiling after the fix is the heartbeat interval by design (the clock
line is the only thing that moves while the host transport holds a body).
Before, every one-second screenshot from 33.5 s to 47.5 s was byte-identical
(md5), and they were written late -- the capture itself runs on the blocked
thread, so a host screenshot cannot be used to time a freeze; the present log can.

**Not reproduced headlessly:** the hash-phase frame. On the desktop Edgar's
7 MB hashes in 17 ms, so no screenshot lands in it. Its behavior is proven by
`FontCommit.AnUnchangedFamilyWithNoLedgerReportsItsHashAsItGoes` (fails against
`12f1c852d`: 0 callbacks, 0 bytes). Device feel -- refresh cadence on the X3's
panel during a real 80 MB run -- is **SHIPPED, UNCONFIRMED on device**; what to
observe: the clock line ticking about once a second, "Checking Edgar_12 · x of
y MB" during the first run after a ledger loss, no full-panel flash.

## 3. The fix

- **`src/activities/settings/UpdateWorker.h`** -- each blocking step (the check,
  one family, one book, the end-of-run removals) runs on a `std::thread` under
  `SIMULATOR`, so `loop()` returns every tick: frames present, SDL events pump,
  Back is read, the heartbeat repaints. On the device the step runs inline,
  exactly as before, and its result is collected in the same tick (the
  `activity_input` suite's one-item-per-tick tests still pass unchanged). The
  step's result is consumed on the loop thread (`completeStep()`), so every piece
  of activity state keeps one writer.
- **`src/network/UpdateProgress.h`** (pure, host-tested) -- the phase enum, the
  repaint rule (`shouldRepaint`: a phase/file change after 250 ms, bytes or the
  clock after 1,000 ms, a new family/book at once, FAST refresh only; a held-back
  change is painted by the next caller rather than lost), the between-items gate
  (`nextStep`: a stop at any point, including during the last item, never runs
  the font mirror's removals), byte totals and the "1.2 of 6.8 MB" / "0:07"
  formatting.
- **Updaters** -- `processedSize`/`totalSize`/`currentFile`/`fileCount` are
  `std::atomic` (they were plain `size_t` read by the render task while the loop
  task wrote them); a `phase()` (PREPARING, CHECKING, DOWNLOADING, INSTALLING,
  FINISHING); the hash reports per 1 KB chunk; downloads report per chunk (the
  activity throttles, not the updater); an **abandon** flag checked per hash
  chunk and per download chunk, set only by `onExit` while a host worker still
  runs -- the family fails, its `.part` staging directory is removed, the
  installed copy is untouched (`FontCommit.AnAbandoned*` tests).
- **Screens** -- a detail line ("Downloading Doves_16 · 3.0 of 7.1 MB",
  "Checking ...", "Installing", "Finishing up"), an elapsed clock under the bar,
  "Stopping after this font/book" the moment Back lands, and the bar counts an
  item only while it downloads (the hash walks the same file indices and would
  otherwise run the bar forward and back). The `pct == lastRenderedPercent`
  gate is gone; the throttle is at request time.
- **Back** -- fonts keep the 2026-09-07 ruling (between families only; a
  canceled run removes nothing; `finishRun()` still runs). Library gains the same
  stop between books with a Stopped screen ("Stopped after N of M books"); the
  ledger is flushed. A host home gesture while a step runs is treated as Back.
  `onExit` with a run part-way (sleep, home gesture between items) now calls
  `finishRun()` / `flushSyncRecords()` so what installed is recorded.
- **Strings** -- 12 new keys in `english.yaml` and `spanish.yaml`.

Screenshots of the fixed screens mid-run (PNG, native pixels) were delivered with
the session; regenerate them with the repro README.

## 3a. Adversarial review (read-only agent, same day) -- what it found

| # | Finding | Verdict | Action |
|---|---|---|---|
| 1 | HOST DEADLOCK: power-hold during the manifest check. The sleep transition holds `RenderLock` (`ActivityManager.cpp:153`) while `onExit()` joins the worker; the check's step callback took `RenderLock` on the worker. Permanent hang. | confirmed by reading | `checkStep` is now an atomic written with no lock; `UpdateWorkerTest.NoStepTakesTheRenderLock` is a source gate on every `runStep`. Re-run headless: power held at 30.15 s during the check -> `Exiting activity: FontUpdate` / `Entering activity: Sleep` at 31.54 s, no hang |
| 2 | Library: an abandon mid-hash fell through to a full download before the per-chunk check could fire (host transport fetches the whole body first) | confirmed | `aborted()` checked before the download in `LibraryUpdater::syncBook`. Not covered by a test: `LibraryUpdater` has no filesystem harness like `test/font_commit` |
| 3 | Host: bytes do not move within one file; abandon waits out the current file | confirmed, simulator transport | documented (section 4); the clock is the heartbeat |
| 4 | Bar could flash to ~100% of a family at the CHECKING -> DOWNLOADING flip | confirmed (one-`open` window) | counters zeroed before the phase flips |
| 5 | `checkStep` read unsynchronized by the heartbeat | confirmed | same fix as 1 |
| 6 | No test exercised the threaded path | confirmed | `test/update_progress/UpdateWorkerTest.cpp`, compiled with `-DSIMULATOR` |

Reported CLEAN by the reviewer: device threading (inline, same-tick
collection), `std::mutex` on ESP32 (already used by `WifiCredentialStore`),
per-chunk cost, repaint rate vs. downloads, lock ordering other than #1, member
destruction order (onExit always joins first), render reads vs. worker writes,
font abandon semantics, cancel semantics, render bounds, translation format
specifiers, the `·` glyph (Noto Sans fallback covers U+00B7).

## 4. Options not taken (owner decision if wanted)

- **A worker task on the device too.** Would make Back responsive mid-family on
  the X3 (today a short press during a download is still unseen, finding 3
  above) and remove the one-tick blocking design everywhere. Cost: a second task
  stack of 8-16 KB of DRAM for the run, on a heap measured refusing 22 KB
  contiguous with Wi-Fi and wolfSSL up (B-053). Not done without a ruling.
- **Streaming bodies on the host transport.** `SimHttpFetch` buffers each body,
  so on a phone the byte figures jump per file and only the clock moves within
  one. A simulator-repo change; recorded, not made.

## 5. Checked and found CLEAN

- The CHECKING frame before any network call (`onEnter` -> `requestUpdateAndWait`)
  -- correct on both platforms; the freeze starts after it.
- Staging/commit atomicity of a family (`FontUpdater::syncFamily` two-rename
  commit, `recoverStaleStaging`) and `.part` + rename for books -- unchanged, and
  the abandon path reuses the existing DISCARD branch.
- The owner-deleted list, the removal fence (`manifestOk_`), the seeded-family
  spare -- untouched; the full `FontCommit` suite (37 tests) passes.
- `requestUpdate(true)` from a non-loop thread -- `xTaskNotify` in the host shim
  takes the task's mutex; safe from the worker.

## 6. Tests

- `test/update_progress/UpdateProgressTest.cpp` -- 15 tests on the pure header.
- `test/update_progress/UpdateWorkerTest.cpp` -- the host `std::thread` path, and
  the no-RenderLock-on-the-worker gate.
- `test/font_commit/FontCommitTest.cpp` -- hash reports progress (fail-first
  against `12f1c852d`), phase order, abandon mid-download keeps the old install
  and leaves no staging dir, abandon on a first install leaves nothing, abandon
  during the hash does not reinstall.
- `test/activity_input/LibraryUpdateFirstFrameTest.cpp` -- Back stops between
  books and flushes the ledger (fail-first against `12f1c852d`), the Stopped
  screen dismisses. All 68 activity tests pass.

## 7. REPEATED REPORT, same day: "in ios app update library and fonts both freezing screen"

Reported after TestFlight builds 226/227. Build 227's archive was checked to
contain the fix above (its binary carries `%s elapsed` and `Stopping after this
font`), so the phone ran it. Taken as stated: on the phone, the screen stops.
Sections 1-6 were proven on the DESKTOP only, and that is the gap this section
closes as far as it can be closed without the phone.

### 7a. The iOS path, traced line by line (at `a29b432f6` + simulator `1648143`)

| Step | Where | Verified on iOS |
|---|---|---|
| Worker compiled in | `UpdateWorker.h` `#ifdef SIMULATOR` | `SIMULATOR` is in the generated iOS define set (`crosspoint-simulator/cmake/CrossPointSources.cmake:236`), applied to `crosspoint_core`; the step runs on a `std::thread` on iOS exactly as on the desktop |
| Main loop | `crosspoint-simulator/src/simulator_main.cpp` `while (!display.shouldQuit())` | the SAME loop on iOS: `loop()`, `CrossPointHarness_perFrame()`, `pumpHostTextInput()`, `presentIfNeeded()`, `SDL_Delay(1)` -- no iOS-only present loop |
| Transport | `ios/CrossPointHttp.mm` `hostFetch` | NSURLSession `sharedSession` + `dispatch_semaphore_wait`; the completion runs on the session's own background queue, never the main queue. Called on the WORKER now, so the main thread never waits on it |
| Wi-Fi | `SimWiFiHost.h` / `ios/CrossPointWiFi.mm` | cached, mutex only, no wait on the main thread |
| Main-queue syncs | `grep dispatch_sync\|waitUntilDone:YES` over `ios/` | none; every main-queue hop is `dispatch_async` |
| Render path | `HalDisplay.cpp` render-task side | no wait on the main thread anywhere (`grep wait\(\|condition_variable`), so `requestUpdateAndWait()` on the main thread cannot deadlock against a present |
| Present gates | `HalDisplay::presentIfNeeded` early returns | `g_backgrounded` (S-041), the sleep veto (dark + power-off collapse), the 30 ms / 2 s coalescing hold, no texture -- none tied to these screens |

### 7b. Measured on the iOS Simulator (iPhone Air, iOS 26.5) -- the phone's code, not reproduced

Every run below goes through the app's own UIKit/SDL3/Metal present path and
NSURLSession. "Gap" is the longest interval between two presents from entering
the screen to the run's summary.

| Run | Result |
|---|---|
| Fonts, Debug, light, mock release at 1 MB/s (3 families, 15 MB) | 1,066 ms max gap; 48 new pictures |
| Same, DARK (CRT page, trail) | 553 ms all presents / 1,291 ms new pictures |
| Fonts, Debug, the REAL `fonts-latest` release (79 assets, 13 families) via NSURLSession | 1,006 ms |
| Same, **Release** (TestFlight's configuration) | 1,053 ms |
| Library, Release, real `library-latest` (58 books downloaded) | 1,045 ms |
| Library, Release, second run on the synced card (58 unchanged) | 1,070 ms |
| Fonts on a card seeded exactly as `CrossPointFsPrep` seeds a phone (13 families CPZ1-compressed, 2x companions, no ledger), real release | 1,033 ms |
| Fonts with E-Ink Mode, Raking Light, Read Aloud, Power-Off Collapse all ON | 1,009 ms; a new screenshot every second |
| Fonts with NO in-app instrument (no screenshot schedule, no present log, no diagnostics), glass sampled from OUTSIDE by `simctl io screenshot` every ~0.5 s | frame changes every ~1 s for the whole run; the only still span is after the summary |
| Fonts, another app brought to the front mid-run for ~11 s, then back | presents declined as `backgrounded` while away (S-041, by design), resumed on return, run completed |
| After the instrument below (Release): fonts / library / fonts + Back at 32 s | 1,013 / 1,016 / 1,017 ms; Back stopped after 2 of 3 families |

So: **no death point was found on any path the iOS Simulator runs.** I do not
say the phone does not freeze. The difference between those runs and the
owner's phone is the physical device itself (its Metal, CPU, thermal state,
network) and the owner's own card and settings, none of which this Mac can
reach -- no iPhone is paired (`xcrun devicectl list devices`: none) and screen
control of the Mac is not available. Why the desktop proof missed it is
therefore unknown in its specifics, but the general reason is now measured:
the desktop and the iOS Simulator share every line of this path, so neither
can see a cause that lives in the device.

### 7c. What ships instead: the update flight recorder (project rule: second device failure -> instrument)

`crosspoint-simulator/src/SimUpdateTrace.h` (header-only), hooked into the
firmware's two update activities (`UPD_TRACE_*` in `UpdateWorker.h`), the host
main loop, `presentIfNeeded`, and every host fetch. While either update screen
is up it writes, to **`diagnostics/update-trace.log`** on the card (Files app:
On My iPhone > CrossPoint X3 > diagnostics; the previous run survives as
`update-trace.log.1`) and interleaved into `firmware.log`:

- `BEGIN`/`END`, every step boundary (`step CHECK/FAMILY/BOOK/FINISH start`,
  `worker: step returned`, `loop: collecting the step`), both
  `requestUpdateAndWait` waits (before and after), with firmware-clock ms;
- every host fetch: URL (query dropped), duration, status, bytes;
- every present with the gap since the last;
- every owed present that `presentIfNeeded` DECLINED, and which gate did it;
- from a watchdog THREAD (a stalled main thread cannot report itself), one
  `STALL` line per stall: the main loop silent for 1.5 s (with the stage it is
  parked in: `loop()`, `harness perFrame`, `pumpHostTextInput`,
  `pumpPendingOpen`, `presentIfNeeded`, `SDL_Delay`), a drawn frame or a
  working run not reaching the glass for 1.5 s (with the last decline), or a
  working run whose firmware stopped asking for frames.

**It needs Settings.app > CrossPoint X3 > Diagnostics Log ON.** With the next
report, the file answers which of four things happened: the main thread is
parked (and where), the host is declining presents (and why), the firmware
stopped drawing (and after which step), or a fetch is hanging (and which).

Device firmware is unaffected: the macros are `((void)0)` without `SIMULATOR`.
Host tests: `crosspoint-simulator/tests/update_trace_test.cpp` (in
`run_all.sh`), compiled with the iOS HTTP define so the phone's fetch branch is
the one traced; its "finished run is not quiet" check fails against the first
cut of the watchdog, which read a run that had just finished as a stall
(measured on the iOS Simulator, 1.3 s after `font sync done`).

### 7d. Checked and found CLEAN on the iOS path (so the next pass need not)

`SIMULATOR` reaching the iOS core; the shared main loop; NSURLSession
completion queue; main-queue syncs in `ios/`; render-side waits on the main
thread; `requestUpdateAndWait` from the main thread (handle resolution in the
FreeRTOS shim: any unregistered thread, the worker included, resolves to the
one `main` handle -- harmless today because only the loop thread ever waits on
it, and worth knowing); Release-only differences (asserts compiled out) on
these screens; the phone-shaped seeded CPZ1 card; the experimental toggles;
backgrounding mid-run.

### 7e. Candidates the owner's phone could still hold (unverified, recorded so they are not re-derived)

- **Auto-lock mid-run.** `allowSleepOnBattery` ships ON and the firmware's
  `preventAutoSleep()` during a sync is not consulted by the host's idle-timer
  logic (`applyKeepScreenAwake` reads `SETTINGS.keepScreenAwake` only), so a
  long run with no touches can reach iOS auto-lock. The trace will show it as
  `present declined: backgrounded`. Not changed: it is a behavior change nobody
  asked for; it is an owner decision.
- **Per-file byte counter frozen during a download.** The host transport still
  buffers a whole body (section 4), so on a slow link only the small clock line
  moves within a file. If that is what reads as frozen, the fix is the
  streaming host transport already listed in section 4.

### 7f. Adversarial review of the instrument (read-only agent, same day)

| # | Finding | Verdict | Action |
|---|---|---|---|
| 1 | The glass watchdog keyed "frame owed" on render START vs ANY present; on a dark page the trail and beam re-present the OLD picture every display frame, so a new frame that never reached the glass would never be reported -- plausibly the very symptom | confirmed by reading | now keyed on the framebuffer GENERATION: the host reports each written `pixelBufSeq` (`frameWritten`) and the generation the presented texture holds (`uploadedSeq`); a same-frame present is counted as a repeat, not as the glass moving. Test: "a new frame hidden behind same-frame re-presents is reported" |
| 2 | Log volume: a line per present (display rate on a dark page), and `SDL_Log` ran even with Diagnostics Log off; `update-trace.log` unbounded | confirmed | lines only while the diagnostics log is armed; one line per NEW frame; `update-trace.log` also rotates at 1 MB. Measured: 7.8 KB for a 38 s fonts run |
| 3 | Decline dedupe never reset: after the first "coalescing hold" no later decline was ever logged, even in later runs | confirmed | a new-frame present and `begin()` end a run of declines; counted per run, not per main-loop pass. Test added |
| 4 | Backgrounding reads as a main-loop stall with no hint why | confirmed (millis() runs while suspended) | the host marks `app backgrounded` / `app foregrounded`, which the STALL line names as the last step |
| 5 | A reboot with an update screen up would leave the trace armed | theoretical (no update path reboots) | reset registered in `SimulatorRebootResets` |
| 6 | `update-trace.log` deleted from Files mid-run keeps writing to the unlinked file | confirmed | reopened when the path is gone, as `firmware.log` does |

Reported CLEAN: no lock-order hazard with RenderLock / the sleep transition's
join (every lock the sink takes is a leaf; `rendered()` does not log);
`SDL_Log` off the main thread; header-only inline atomics across the static
`crosspoint_core` and the app (one program, C++20); the once-guard across the
iOS longjmp; `__has_include` reaches the header in BOTH the iOS and desktop
builds (the same TUs already include `<SimHostSettings.h>`) and falls back only
in `test/update_progress`; no secret in any line (query dropped, headers never
formatted); only literals stored as breadcrumbs; the device build (macros
`((void)0)`); the two sleep-only `SDL_RenderPresent` calls in `SurfacePower.cpp`
(sleep ENDs the trace first).

Re-measured after the fixes, iOS Simulator, Release, DARK page: Update Fonts
(mock, 15 MB at 1 MB/s) 49 new frames, max gap 1,040 ms, no STALL; Update
Library 16 new frames, max gap 1,007 ms, no STALL.

## 8. Build 229: streaming downloads and keep-awake (owner ruling 2026-09-26, "Fix both")

Both candidates from 7e, fixed. The recorder from section 7 stays.

### 8a. Streaming host downloads

**The cause, measured.** Every host transport buffered a WHOLE response before
the firmware saw a byte: `crosspoint-simulator/src/esp_http_client.h`
`esp_http_client_open` called `sim_http_fetch::fetch`, which fills
`Response.body` (NSURLSession completion handler on iOS, `curl_easy_perform`
into a string on the Mac). `HttpDownloader.cpp` `runGet` -- the path BOTH the
desktop simulator and iOS compile, since neither defines `FREEINK_NET_WOLFSSL`
-- then read that buffer in 1 KB pieces within microseconds, so the updaters'
per-chunk progress (`FontUpdater.cpp` ~776, `LibraryUpdater.cpp` ~529) fired in
one burst at the end of each file. On the iOS Simulator at 1 MB/s before the fix
the screen read "Downloading Coelacanth_11 · 0.0 of 8.2 MB" 20 s into the family
(section 7 screenshots).

**The fix.** `crosspoint-simulator/src/SimHttpStream.h`: a `Stream` shared by
the transport (producer) and the firmware thread in `esp_http_client_read`
(consumer). `esp_http_client_open` now returns once the final response's headers
are in, and each read hands over bytes as they arrive.
- iOS: `ios/CrossPointHttp.mm` `hostOpenStream`, an NSURLSession data-task
  delegate on its own serial queue. A body with a `Content-Encoding` declares
  no length, because NSURLSession decodes gzip and the header counts encoded
  bytes.
- Mac: libcurl on a detached thread (same options as the buffered path, plus a
  progress callback so an abort lands on a stalled link).
- Fixtures (`CROSSPOINT_SIM_HTTP_MOCK_ROOT`, `file://`) and the Linux curl
  SUBPROCESS stay buffered, filled into a stream in one piece.
- **No whole-file buffering**: the producer blocks at 1 MB unread
  (`kStreamCapBytes`; on iOS that is the only flow control a data task has).
- **Abort**: the firmware closing early (an abandoned family, a sleep mid-file)
  cancels the transfer at once. Before, the whole file downloaded first.
- `.part` staging, the SHA-256 check at the end, and the two-rename commit are
  untouched; the updaters see the same chunks, sooner.
- `perform()` and `HTTPClient` stay buffered (small JSON bodies).

### 8b. Keep awake during a run, and only then

`crosspoint-simulator/src/SimKeepAwake.h`. The activity says whether its run is
WORKING (`UPD_KEEP_AWAKE`, `UpdateWorker.h`), in `onEnter`, on EVERY `loop()`
tick ahead of any return, and `false` in `onExit`. The host's main loop (and the
deep-sleep loop) holds `UIApplication.idleTimerDisabled` while it is, and
restores the value it found when it is not. So done / failed / stopped release
on the next tick, and Back / sleep / home / destroyed release in `onExit`. A
reboot mid-run clears the request (reboot reset); backgrounding releases, and a
return mid-run takes it again. Every SET / REASSERT / RESTORE is a line in
`update-trace.log`.

**The device's own auto-sleep cannot fire mid-run, and never could**:
`main.cpp:1145` resets `lastActivityTime` from `activityManager.preventAutoSleep()`
before the timeout check at `:1151`, and both activities answer true in
CHECKING and SYNCING (`FontUpdateActivity.h:117`, `LibraryUpdateActivity.h:83`;
pinned by `FontUpdateFirstFrameTest` / `LibraryUpdateFirstFrameTest`). The same
code runs on the desktop and the phone. On the device a single family runs
inline, so no loop tick happens during it, but the reset comes first on the
next tick. No change there.

### 8c. Proof, iOS Simulator (iPhone Air, Release), mock release at 200 KB/s

| Run | Result |
|---|---|
| Update Fonts | the byte figure moves WITHIN one file: "Downloading Doves_18 · 4.6 → 4.9 → 5.5 → 6.1 of 7.1 MB" across 9 s of the same 1.9 MB file; 89 new frames, max gap 1,055 ms; every stream `complete=1` |
| Update Library | within the 6 MB book: 0.9 → 2.5 → 4.0 → 5.6 of 6.0 MB; max gap 1,030 ms; `keep-awake SET` at 10,602 ms, `RESTORED` at 47,539 ms after "library sync done" |
| Real GitHub `fonts-latest` / `library-latest` on the streaming build | 5 updated + 8 unchanged / 2 updated + 56 unchanged, 0 errors; every stream complete (the library release JSON arrived with no declared length and read clean) |
| Back mid-run | stopped after 2 of 3 families; RESTORED at the stop, before the screen was left |
| Power held mid-file | stream closed at 720,896 of 1,263,585 bytes (aborted, not waited out); staging removed; `Entering activity: Sleep`; RESTORED 42 ms later **from the sleep loop** (see 8d, finding 0) |
| Another app brought forward mid-run | RESTORED on `app backgrounded`, SET again on return, RESTORED at done |

### 8d. Adversarial review (read-only agent) and what the runs found

| # | Finding | Verdict | Action |
|---|---|---|---|
| 0 | (found by the power-mid-file run, before the review) the lease was applied only from the main loop, which does not run in the deep-sleep loop, so a power-off mid-run left the phone held awake while "asleep" | reproduced on the iOS Simulator | applied from `HalGPIO::startDeepSleep`'s loop too; re-run shows RESTORED 42 ms after sleep |
| 1 | The lease and the host's charging-dependent screen-awake preference (`applyKeepScreenAwake`) both write the idle timer. Unplugging mid-run let the phone lock. Plugging in mid-run and then restoring the stale snapshot lost the owner's "awake while charging" | confirmed by reading | the lease REASSERTS while held if the timer was turned back on; every restore makes the preference re-apply itself as it stands now. Tests: "unplugged mid-run", "every restore asks the preference to re-apply" |
| 2 | A blocked producer could outlive a consumer that never cleans up | not reachable (`runGet` cleans up on every return) | recorded |
| 3 | One NSURLSession per request: no connection reuse | performance only | recorded |

Reported CLEAN: the live path is `runGet` on both hosts (the desktop env does
not extend `base`, and iOS has no `FREEINK_NET_WOLFSSL`); no other firmware
caller of `esp_http_client_open/read`; gzip (measured with Foundation on the
Mac: decoded length or -1, never the encoded length); `answered()` against the
old accept rule; abort/cancel ordering, session invalidation, and ARC holding
the task; redirects; fixtures over the cap; no token in any line; main-thread
use of the idle timer from the sleep loop; exit paths (no sub-activity can hold
the request); the request while backgrounded; WebServer's async writes cannot
overlap a run.

### 8e. Tests

- `crosspoint-simulator/tests/http_stream_test.cpp`. Its socket case checks
  that the first chunk is read while the server still holds the rest back. It
  **fails against the old buffered shim**; the fixture-over-the-cap case is
  there too.
- `crosspoint-simulator/tests/keep_awake_test.cpp`: every exit path × both
  starting values of the idle timer, background round trip, back-to-back runs,
  unplugged mid-run. Mutants (restore to "enabled"; ignore foreground) fail it.
- `test/update_progress/UpdateWorkerTest.cpp` `EveryExitReleasesTheKeepAwake`:
  source gate that `loop()` states the request before any return and `onExit()`
  releases. It fails with the Font activity's change reverted.
