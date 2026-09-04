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
