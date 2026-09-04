# Adversarial review — firmware, 2026-09-04

Read-only refuting pass over the code commits since 2026-08-30 (`be43e4846`
… `dc43c4950`), run per the `adversarial-review` skill: one agent that did
not write the code, told to disprove each candidate before reporting, and to
say what it found CLEAN. Three findings survived; all three are fixed in the
commit that adds this file. The CLEAN list is the half that compounds — do
not re-read those areas for this bug class.

## Findings, ranked — all FIXED 2026-09-04

### 1. Library sync progress bar ran BACKWARDS and reached 100 early — would-ship

`LibraryUpdateActivity::loop` moves `currentBook` on and forces a repaint
BEFORE `syncBook(i)` runs; the only reset of `processedSize`/`totalSize` sat
AFTER the compare stage (records, stamp, a full SHA over the card file —
seconds on a large epub). So during book i's compare the bar computed
`overallPercent(i, total, <previous book's final 100>)`: a two-book sync read
50 → 100 → 50 → 100, and on a mostly-current library the bar sat one book
ahead of the truth, showing 100% while the last book was still checked. The
commit's own tests pinned `overallPercent` and missed the wiring.
**Fix:** the two counters reset at the top of `LibraryUpdater::syncBook`.

### 2. A stale OpenActionMenu request survived a PUSHED child — latent

`FileManagerActivity::onEnter` drains the gesture channel, but a pop back
from the text viewer or the rename keyboard does not re-enter the manager
(`ActivityManager.cpp` pop path: swallow + result handler only). A gesture
bound to `OpenActionMenu` fired inside the child sat latched and popped the
menu on the first frame back, on whatever row was focused. Latent: nothing
ships bound to that action, and on hardware the channel is a constant false.
**Fix:** both result handlers drain the channel first. (The simulator review
found the same thing from the other side, its finding 5.)

### 3. `ButtonHoldTimer::started[]` was never cleared — latent

The guard against a press edge consumed outside the pump (boot-time
`waitForPowerRelease`, which on iOS survives the longjmp reboot together with
the timer) covered only a button's FIRST unseen press: after any press/release
pair, an edge-less level read now minus the OLD stamp — the uptime-class
phantom hold the guard exists to prevent. Not reachable today (no boot
destination reads a Confirm hold before `swallowUntilIdle`), would be the day
one does. **Fix:** the release edge spends the press; `heldMs` reads the
release frame's finished length first, so the release-driven sites are
unchanged. Pinned by `ALevelWithNoPressEdgeAfterAnEarlierPressReadsZero`.

## CLEAN — checked and found nothing

- `ButtonHoldTimer` + `getHeldTime(Button)` core: release-frame answers once,
  0 thereafter; swallow gate mirrors `isPressed()` incl. `staleReleaseMask_`;
  every logical button maps to one physical index. No converted site reads
  the frame AFTER release.
- Held across `swallowUntilIdle()`: gated to 0, release eaten, pinned by
  `ActivityInputTest` 664/693/720.
- Touch long-tap override (X4 Pro / Sticky): neither board synthesizes a
  Confirm from touch, so the physical-edge branch never shadows it.
- Sticky's same-frame press+release Confirm answers 0 where the global timer
  answered the click length — harmless, every hold threshold exceeds
  `CONFIRM_POWER_HOLD_MS`.
- FileManager `confirmHoldSpent` on every path out of the mid-hold;
  FileBrowser delete release-only, popup re-derives the latch, `findEntry`
  miss → 0.
- `6a9731b6d` hold kills: only comments and the tombstone reference the
  removed names; `backShortToFileBrowser` read by nothing; all four
  `handleBackNavigation` callers consistent; `CHAPTER_SKIP` slot pinned.
- `5dcd2ba11`: `focusEntry` consumed once after `loadFiles()`, miss → 0;
  `NoteEditorActivity::path` never reassigned; both `startActivityForResult`
  sites clear the latch before any early return; Wi-Fi side pair inside
  `!networks.empty()`; held DOWN steps because `onPageNext` = press +
  continuous.
- `682514416` Back-to-cover: `homelanding::selectorIndex` recomputes against
  the reloaded list; every Home exit passes `recordFocus()`.
- `157791688` cover reset: dest path, cache hash and thumb template match the
  reader's own formulas; `RECENT_BOOKS` loaded at boot before any save. Noted,
  out of scope: a deleted-then-re-added book keeps a stale "" cover sentinel.
- `be146343a` kickoff: `fetchManifest` still blocks `loop()` by design; the
  step callback uses the proven RenderLock + `requestUpdate(true)` shape.
- `225c8e5c6` cppcheck sweep, every hunk: the removed NoteEditor OOM block was
  a byte-identical duplicate; `dnsOk` scope move sound; Colophon `span > 0`
  guaranteed; the `const&`, algorithm rewrites, typed references and the
  Wi-Fi timeout unification preserve semantics.

## Sanitizers, same day — CLEAN

The whole host suite (`test/`, 623 cases after the day's additions) built
and run under `-fsanitize=address,undefined` (Debug, `detect_leaks=0`):
100% passed, no AddressSanitizer report, no UBSan `runtime error` line. A
negative result, recorded so the next pass does not pay for it again; the
2026-08-23 review's memory-safety find came from exactly this kind of run.
Note the build needs `-j4` on this Mac — an unbounded `-j` across the ~65
test targets exhausts the process table (`posix_spawn failed`).

## Second pass, same day — over the fixes above

A second read-only reviewer over `df3598872`, `222e3f4f1`, `8678d7c78`,
`e484edb75`. Three survived; all fixed in the commit that adds this section.

1. **`ButtonHoldTimer`: a release and a re-press of one button in ONE frame
   left the new press dead** (latent, host-only — a device's edges are level
   diffs and cannot produce the shape; a KEY_UP/KEY_DOWN inside one pump or two
   queued QTAPs can). `frame()` processed the press first, then the release
   cleared `started`. Order swapped: release spends the old press, then the
   press arms the new one. Two new cases pin it, including the Sticky
   synthesized click still reading 0.
2. **The sync-bar counter reset was not ordered before the repaint it feeds**
   (latent, ~0.1% per book): the activity notified the render task and THEN
   `syncBook` zeroed the counters a few stores later. `resetBookProgress()` is
   now called under the activity's render lock before the notify, and
   `syncBook` still calls it first.
3. **Pre-existing: `cursorPos` could exceed the text** in a password field —
   a Confirm hold on DEL is not gated on cursor mode, so the restore on Left's
   release could put a saved position past a cleared string. Clamped.

CLEAN: every asked walk of the timer (synthesized click, boot-time release
with no press, release-driven sites, touch override untouched); the cursor
repeat's toggle-position, password, early-return, ButtonNavigator and
boundary questions; `PageNext` under `SIDE_BUTTONS_DISABLED`; the compare
stage's `i/N` reading; the result lambda runs before the manager's next
`loop()`; `__LINUX__` reaches nothing but JPEGDEC; `ChrInfo` has one
aggregate init and only named reads.

## Third pass, same day — over the seven-fix batch (`fc077e634`)

Two survived, both fixed in the commit that adds this section:

1. **`BmpViewerActivity::imageValid` was never cleared**, so the "Invalid
   BMP" gate held only for the file the viewer opened on: step to a bad
   sibling and Confirm still copied it over `/sleep.bmp` — the exact failure
   the batch claimed closed. Reset at the top of `onEnter()`, which
   `openSibling()` re-enters.
2. **Book Notes paged back one line short of a headline** that topped the
   page (modeled: 8.7% of pages): the backward walk charged every gap where
   `render()` skips the first line's. `pageStartBefore` now asks
   `linesFrom()` itself for the furthest-back start that still reaches the
   line, so the two cannot disagree.

CLEAN: `linesFrom` matches `render()` line for line; no line unreachable
from `maxScroll` (asserted over 20,000 random documents); empty `lines`
safe; the ±3 nudge cannot strand; ButtonNavigator invokes its callbacks
synchronously; the DONE frame's counters initialize to 0 and
`requestUpdate()` under the render lock is the pattern its neighbors use;
`wasReleased(PageNext)` maps through `sideButtonLayout`; no side hold in the
viewer; Back during CONNECTING leaves the link exactly as the pre-existing
dismiss path did; the braced `labels` block; `-1` on the clock list is exact
on both panels (631/40 vs 575/40, 639/40 vs 583/40); `cp::renderScale()`
visible and used only in runtime arithmetic. Noted, pre-existing: a headline
advances by the UI_10 line height while drawing in UI_12; the clock list's
touch hit-test still uses the un-reserved height (touch boards).

## Crafted-input hunt, follow-up (EPUB / CSS / JSON / text / images), 2026-09-04

A second read-only ASan/UBSan pass over the four parser families the first
crafted-input hunt skipped — every one reachable from a file a Wi-Fi peer can
PUT over WebDAV. No memory corruption of B-045's class was found; the zip
reader, the expat-fed OPF/NCX/XHTML, the CSS parser, the streaming JSON
parser (fuzzed with deep nesting, 10^6-digit numbers, token storms) and the
text/markdown/UTF-8 paths are guarded (details in the CLEAN list of the
agent's report; the load-bounding is B-023/B-024's and holds). Two latent
survivors, both FIXED the same day:

1. **`ImageToFramebufferDecoder::validateImageDimensions` computed `width *
   height` as signed `int`** — a crafted cover (65535x65535, or a PNG IHDR
   with `height = 0x7FFFFFFF`, which PNGdec's open path does not bound)
   overflowed the product negative and PASSED the `> MAX_SOURCE_PIXELS` guard
   that exists to reject it. UB under UBSan. Blunted downstream (the row
   buffers size off width alone and JPEGDEC streams MCUs, so it aborts the
   decode rather than over-reading), but the guard did not guard and the
   multiply was UB. Each dimension is now bounded first and the product taken
   in `int64_t`, the shape `Bitmap.cpp` already uses.
2. **`LibraryReleaseParser` accumulated an unbounded `std::vector<Asset>`**
   from the GitHub release JSON, with no total cap — a hostile or MITM'd
   response could stream assets until the ~380 KB device heap is out. Not the
   card-PUT model (the URL is the fixed api.github.com endpoint), so low;
   capped at 512 assets, which no real release approaches. The card-side
   `library_sync.json` was already capped (`MAX_LEDGER_BYTES`).

Not covered by either pass, for a future session: the progressive-JPEG and
PNG decoders' internal MCU/scan loops below the dimension guard (the vendored
JPEGDEC and PNGdec), and the EPUB image `src` -> path resolution.
