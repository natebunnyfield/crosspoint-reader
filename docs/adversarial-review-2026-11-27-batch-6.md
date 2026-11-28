# Adversarial review, batch 6 (2026-11-27)

A read-only agent that did not write the code reviewed `origin/main..campaign-b6`
(base `f42b03cae`): the six clean picks and the twelve hand-ports of batch 6
(`docs/fork-sync.md`, "Batch 6" and "Taken EARLY, 2026-11-27"). Its brief was to
refute, to try to disprove each finding before reporting it, and to list what it
checked and found clean. Model: Sonnet 5.5, as were the ports.

How sure: everything below is **by reading** unless marked. One probe was
written (the sleep-image shuffle). Nothing ran on a device. The builds, the
latency gate and the host suite had already passed and were not re-run by the
reviewer.

## Findings

**F1. Sleeping before the first page renders woke into a different book.
FIXED before the push.** Port of upstream `a70ade72e` (`b81c31972`).
`ReaderActivity::onEnter` cleared `APP_STATE.openEpubPath`, and
`rememberBookOnceRendered()` wrote it back only after the first page drew. This
fork boots an empty path into the first EPUB under `/books` (`main.cpp`, the
"device IS the current book" branch), so opening a book with a long first index
and sleeping during it woke into an unrelated book and lost the sleep cover.
Confirmed by reading both sites. Fix: the clear is removed. The remembered book
stays the last one that drew a page until the new one draws, so a book that
never renders is still never remembered (the point of the upstream fix), and a
sleep mid-index wakes into the previous book. Not found: a boot loop (the
`readerActivityLoadCount` guard still sends a repeat failure Home).

**F2. A short Back tap during an upload can be lost. Cosmetic, left as is.**
Port of upstream `457f64c69` (`5a7809d3e`). The upload callback's mid-frame
`mappedInput.update()` (`CrossPointWebServerActivity.cpp`, the
`setUploadCancelCheck` lambda) clears the frame's edges, and it samples at most
once per received chunk. A tap that starts and ends between two samples is
never seen; a held Back is. Other buttons' edges in that frame are lost too,
which the comment at the call documents. WebDAV PUT and WebSocket uploads never
poll the callback, so Back cannot interrupt those (not claimed by the port).

**F3. The framebuffer loan may add a refresh on image-heavy books. Device-only,
UNCONFIRMED, left as is.** Port of upstream `56c677f68` (`e3d4b9343`). A redraw
is requested whenever the loan counter moved during a background build tick.
The probe retry lends the buffer for any image whose first 1 KB shows no header,
which happens when the 32 KB inflate window does not fit the heap. Each such
image met during a background build redraws the page on screen and spends a
`pagesUntilFullRefresh` slot. The latency gate covers text pages only. Worth one
look on a device with an image-heavy book.

Disproved while hunting: Back mid-upload leaving a half-written file (the
vendored WebServer always raises `UPLOAD_FILE_ABORTED`, and both of the fork's
handlers close and remove the file); re-entrant teardown from the callback (it
sets a flag and stops the client; `onGoHome()` runs after `handleClient()`
returns); a double-fired Back.

## Checked and found clean

- **Upload Back (`5a7809d3e`):** the callback, the WRITE/END/ABORTED handlers
  for files and fonts, the vendored parse loop, every reader of input edges
  after `activityManager.loop()`. `handleClient` has one call site. The rate
  limit starts from 0 safely.
- **Wake (`b81c31972`), apart from F1:** the Epub, Txt and Xtc loops all reach
  `rememberBookOnceRendered()`; `markPageRendered` sits at the Epub render end,
  `renderEndOfBook`, Txt and both Xtc paths; the writes run under the Storage
  mutex; the Picco uses the same `main.cpp` paths. Two cases are remembered a
  page late: an "Empty chapter" first page, and a Txt file with zero pages.
- **Framebuffer loan (`e3d4b9343`, `859588b27`):** the counter moves only on a
  real lend (a nested loan is inert); no return sits between the before and
  after reads; idle prewarm draws nothing and the next-page prerender draws to
  its own strip; the deferred gray pass is guarded by the frame hash. Not
  examined: `skipLoopDelay()`'s unlocked read of `section`, unchanged by this
  batch.
- **Korean justification (`930c2921b`):** with no Hangul splits
  `wordBreakCandidates` returns the hyphenator's list unchanged, so Latin,
  soft-hyphen, explicit-hyphen and zero-candidate words break as before, in
  both breakers. UTF-8 decoding validates continuation bytes. The one change
  outside Hangul: a Hangul-Han pair no longer breaks implicitly. Section 80 is
  the next number and reads need an exact match.
- **SD-font kerning (`ede1608c5`):** scratch stride 256 against a maximum index
  of 255; `matrixBytes` is 32-bit; an out-of-range class id is still refused
  before the mini matrix or the top-up can run; failed builds leave counts at
  zero. No layout width changes, so no section bump.
- **Zip central directory (`860011b28`):** a failed or short header read ends
  the scan; the stat is written only on a match; zero-length names are
  harmless and names of 256 bytes or more are skipped; the cache is cleared in
  `close()`. By reading plus the existing host tests; no fuzz run.
- **XTC bit depth (`63a999222`):** the probe offset is bounded by the file
  size; out-of-range, truncated and unknown-magic cases keep the container's
  depth; every consumer reads `getBitDepth()`.
- **Sleep images (`9b4eb9dff`, `26c927c9d`), by probe:** the selection logic
  re-implemented and run under ASan and UBSan for n = 1 to 300 over 200 cycles
  each, plus 1,000, 5,000, 40,000 and 65,535: no duplicate within a cycle, none
  missing, no index out of range, no repeat across a cycle boundary for n > 1.
  A state file from before the change starts a new cycle. Not simulated: a
  file count that changes mid-cycle can repeat or skip an image, as upstream.
- **Idle time (`468af6bde`):** `lastActivityTime` cannot pass `millis()`; the
  Picco USB branch is unchanged; time inside an activity's `loop()` now counts
  as activity by design.
- **X3 USB detection (`0a83f5ec0`):** `usbSofActive`, `isUsbConnected` and
  `isUsbSleepGuardActive` are untouched; `BatteryMonitor::isCharging()` applies
  the same positive-current rule as the read it replaces; the SOF heartbeat
  still covers a full battery. One read with no retry now. Device-only.
- **RTL tap zones (`0b579d1b8`):** the LTR geometry is identical; the language
  test needs exactly two letters or a `-`/`_` at index 2, so `heb`, `ara` and
  `hex` cannot match. Txt and Xtc keep LTR zones.
- **`initializer_list` and captures (`125762246`, `47f047541`):** every
  `Buttons` use is a `const&` consumed synchronously; the static getters point
  at static storage; callbacks capture only `this` and run synchronously.
- **The rest:** `f8cf5939b` frees only rebuildable caches; `5b5bb4d05`,
  `ad0c97bdb`, `81f2a24a2`, `da91db289` are as their messages say;
  `docs/file-formats.md` rows 79 and 80 match the code.

## Not done

- No device run of anything.
- No fuzzing of the zip or `.cpfont` parsers.
- How often the F3 loan fires on real books was not measured.
- The sleep and boot paths were not driven in the simulator, before or after
  the F1 fix. After the fix: X4, Picco and simulator build, and
  `tools/latency/gate.sh` is OK (14 turns, 2 Home entries, 2 Settings moves).
- Eleven of the triage's twelve "already here" classifications were not
  re-checked by a second reader (`ce9f5c28e` was, and held).
