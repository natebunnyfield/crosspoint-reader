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
