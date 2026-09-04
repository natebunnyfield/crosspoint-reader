# UI/UX navigation audit — firmware (2026-09-02)

Owner asked for a finding pass ("take a pass at finding ui/ux bugs,
especially around navigation"). The findings below are the pass as written;
**status per finding, 2026-09-02 (same day), after the owner's ruling
"Chord timer + iOS P1s":**

| Finding | Status |
|---|---|
| F4, F9, F10 | **FIXED** — `src/ButtonHoldTimer.h` + `MappedInputManager::getHeldTime(Button)`, all six raw sites converted (KeyboardEntry x4, FontSelection, RecentBooks, FileBrowser, FileManager x2). Pinned by `test/button_hold_timer/` (pure chord truth table) and three `ActivityInput` cases (`PerButtonHeldTime…`, `FontListHoldDoesNotFireOnATapInsideAChord`, `FontListHoldStillFiresOnConfirmsOwnLongPress`); the chord case was run against the old site first and FAILED there. |
| F5 | **FIXED** 2026-09-02, after the owner re-reported it in his own words (*"on back button, navigates go to last home menu item, but should go back to last home menu item or hovered book, whatever was actually last focused"*). NOT the fix proposed below: `NONE` in `goToReader` would land every return on cover 0, not on the cover left from, and `goToReader` is also the file browser's and Recents' route. Instead `HomeActivity::recordFocus` — called on every leave from Home, the Back-opens-most-recent shortcut included — records what the SELECTOR was on: a cover by path into `ActivityManager::lastHomeBookPath` (resetting `lastHomeMenuItem`), a menu row into `lastHomeMenuItem`; `goHome()` hands both to Home, and `src/activities/home/HomeLanding.h` (pure) picks the row: menu row if one is in charge, else the cover found BY PATH (the reader re-adds the book to the recents on open, so it comes back at cover 0 whichever cover it left from), else 0. Pinned by `test/home_landing/` (7 cases, one modeling the shipped wiring and reproducing the reported landing). Proven headless on the X3 canary with the new `[HOME] Landing on row` line: Recents row → Back → row 1; resume shortcut pressed with the selector on the Recents row → Back → row 1 (the shortcut is not a focus — adversarial review, same day, which also found the path search mostly redundant with cover 0 being both the re-added book and the fallback: kept, since it is what keeps a HOVERED cover under the resume shortcut); cover → Back → **row 0** (the shipped code printed row 1 there). Review checked CLEAN: every Home→book path goes through Home's own `onSelectBook`/`activateSelection`; all nine menu rows set the row via their `goTo*`; FileBrowser pushes its reader and Recents uses the base `onSelectBook`, so neither changes; bounds vs `getMenuItemCount()`; `homeContinueReadingInMenu` index space; string lifetime across `replaceActivity`; the activity harness needs no model of the new member. |
| F8 | **KILLED** — owner ruling 2026-09-02 (`docs/hold-gestures.md`): the reader's held-Back destination is gone, not re-timed. Back in a reader is one short press → pop. `GO_HOME_MS` / `GO_BACK_OR_HOME_MS` deleted from `ReaderUtils.h`. |
| F3, F6, F7 | **FIXED** 2026-09-02, owner ruling "All three". **F3 — NOT the fix proposed below.** The proposal was to move Retry/Forget off the front pair so Left/Right step the list; that invents a new home for two actions the hints already name, which is an architectural choice nobody asked for. Instead the SIDE pair steps the networks (`ButtonNavigator::nextIndex`/`previousIndex`, wrapping) and the front pair keeps Retry and Forget — upstream's shape, and the screen joins the paging ruling's exemption list in `docs/ui-conventions.md` (a screen whose front pair already means something else is exactly what that list is for). If Left/Right stepping is what was wanted, say so and it is one more edit. **F6** — `lockNextConfirmRelease = false` in both the View and the Rename result handlers (`FileManagerActivity.cpp`). **F7** — `goToFileManager(startPath, focusEntry)` carries the edited note's basename out of `NoteEditorActivity::exitEditor()`, and `FileManagerActivity::onEnter` `findEntry()`s it once, logging `[FileManager] Re-focused '<name>' at row N`. No pure unit in any of the three, so the pin is the real firmware under a script on a scratch card: `crosspoint-simulator/tests/test_manage_files_and_wifi_nav.sh` (in `run_all.sh`). F6's arm was measured to FAIL on the pre-fix tree (TextViewer entered once, not twice); F7's arm reads the `Re-focused 'note1.md' at row 2` line; F3's arm (four networks, one Down, Confirm → `Connecting to Bravo`) discriminates by construction — pre-fix `pageDown` returns false on a one-screen list (`ButtonNavigator.cpp:156`), so the old tree connects to Alpha — but was NOT run against a pre-fix binary. F7's arm was strengthened after review to also ACT on the row (menu → Move → `armMove entry='note1.md'`), since the log line alone would pass a tree that computed the row and then lost it. **Adversarial review of the diff, same day** (read-only, tried to disprove each): nothing would-ship. Two survived and are recorded, not fixed: (a) F3 gives up screenful paging on the Wi-Fi list, and on X3/X4 (no touch) a 30-SSID scan is a held DOWN rather than four presses — a deliberate trade under the ruling, now stated in the code comment and `ui-conventions.md`; (b) the front pair is not the clean Retry/Forget split the first draft of the comment claimed — Left steps UP on an unsaved network, unlabeled, pre-existing — comment and doc corrected. CLEAN: `onEnter` runs only on a fresh activity (`ActivityManager.cpp:180,212`; the pop path never calls it), so a View/Rename return cannot reset the row; no separate scroll offset (`drawList` derives the page from `selectorIndex`, `BaseTheme.cpp:297`, `LyraTheme.cpp:200`); the basename matches the listing form byte for byte and the editor cannot rename; only two `startActivityForResult` sites exist in FileManager, both cleared; clearing the latch in the handler cannot eat a needed release because `swallowUntilIdle()` runs first (`ActivityManager.cpp:130`) and the stale-release mask covers a still-held Confirm; `nextIndex`/`previousIndex` guard an empty list and every path back to `NETWORK_LIST` resets the index; `WifiSelectionActivity` is the ONLY `onPageNext` caller that touches `Button::Right`/`Left`, so F3 has no sibling. |
| F11, F15 | **FIXED** 2026-09-04. `ClaudeChatActivity.cpp` answer view reads the side pair as `PageNext`/`PagePrevious` (so `sideButtonLayout` is honored like every other paging surface) and calls `requestUpdate()` only when `answerTop` actually moved. Note `sideButtonLayout` is a `static constexpr` today, so F11 was latent rather than visible; the fix is the convention, not a symptom. |
| F14 | **FIXED** 2026-09-04. Cursor mode steps the caret on PRESS and auto-repeats while held — `KeyboardEntryActivity::repeatCursor`, `repeatCaret`'s 600/300 ms cadence, one deadline per button. Right stays release-driven in a PASSWORD field, where a held Right is the reveal-position gesture. Pinned by `test/activity_input/KeyboardCursorRepeatTest.cpp` (4 cases; the first EXPECT — one update on the press frame — reads 0 on the release-driven tree by construction). `KeyboardEntryActivity.cpp` is linked into `ActivityInputTest` for it. |
| F13 | **WRITTEN DOWN** 2026-09-04 as a convention in `docs/ui-conventions.md` (Navigation contracts): release-edge Confirm is required exactly where Confirm also has a hold meaning; press-edge elsewhere; no screen-by-screen migration. |
| F12, F16 | open, as written. F12 (no hints during a synchronous exchange) is a design proposal; F16 (`IntervalSelectionActivity` unreachable) is upstream's code and nothing on the fork constructs it — deleting it widens the fork for no user-visible gain, so it stays. |
| F1, F2 | open, TOUCH BOARDS ONLY (X4 Pro, Sticky) — unreachable on every device the owner has; shippable to upstream. |
| **Second pass, 2026-09-04** — over the `## Not read` list below | Eight survivors, seven FIXED the same day, one recorded. See `## Second pass, 2026-09-04` at the foot. |

Deliberately NOT converted, because nothing asked for it and each is a
proposal rather than a fix: `ButtonNavigator.cpp` hold-to-repeat stays on
the global timer (a chord there only starts auto-repeat early, which is
harmless); `EpubReaderActivity.cpp`'s side font hold keeps its own stamp and
its "next wins if both down" comment; `DaisyEntryActivity` keeps its own
stamp; `SETTINGS.backShortToFileBrowser` (`CrossPointSettings.h`) is now read
by nothing and is left tombstoned rather than deleted.

**Adversarial review of the fix, same day** (read-only refuting agent over the
diff; 21/21 host tests, `-Wall -Wextra -pedantic` clean). Three findings, all
taken; nothing confirmed as a regression:

- **FileManager hold-to-paste double-fired into the action menu.** The mid-hold
  branch relied on its action leaving a popup up to eat the release, but
  `performMoveHere()` opens none on its success and same-directory paths, and
  the per-button timer answers the finished press length on the release frame
  — so paste, then `openActionMenu()`. Pre-existing on DEVICE (the SDK's global
  timer also reports the full length on the last-button-up frame); previously
  invisible on the simulator, which read 0 there. `confirmHoldSpent` latch now,
  the shape the other five sites already had. Not host-tested: no
  `FileManagerActivity` harness exists.
- **A missed press edge read as uptime.** `pressStartMs` initialised to 0, so a
  button already down when the first frame arrived — the only path is
  `setup()`'s absorb loop, which the boot swallow masks today — would have read
  `millis()`: past every threshold. `started[]` flag; reads 0 until an edge is
  seen. Pinned by `ADownButtonWhosePressWasNeverSeenReadsZero`. Plus a
  `static_assert` tying `kButtons` to `HalGPIO::BTN_POWER + 1`.
- **Test quality.** One tautology (`EXPECT_GE(unsigned, 0)`) replaced with
  `< 900` and a first-pressed-is-older check; the release-frame count in the
  chord test is annotated as non-discriminating (it reads 1 with or without
  the defect — the press-frame zero is the line that fails).

Checked and found CLEAN by that review, so the next pass need not: `mapButton`
fan-out (every `Button` invokes at most one index); touch long-tap-to-delete
(both call sites take the override branch, same frame); override retirement
(bounded by the 250 ms window, no converted site reaches it); the SDK's
synthesized-Confirm clicks (650/400/650 ms emit thresholds all below every
converted hold threshold, so no gesture changes); swallow gating (fed before
the early return, read-gated by a superset of `isPressed()`'s masks); HAL reads
are non-consuming; single per-frame pump; reader-kill completeness (no
`GO_HOME_MS`/`GO_BACK_OR_HOME_MS` reference in `src/`, `test/`, `lib/`; all four
`handleBackNavigation` callers compile; Txt and Bmp had no guard to remove).

Surveyed at `822483b3d` (fork `main`). One read-only hunting agent over
`src/activities/**`, `src/MappedInputManager.*`, `src/util/ButtonNavigator.*`,
`src/CrossPointSettings.h` and the SDK's `InputManager.cpp`; every finding
marked VERIFIED below was then re-read independently at the cited lines
before it was written here. The iOS-harness half of the same pass lives in
the simulator repo (`crosspoint-simulator/docs/ux-navigation-audit-2026-09-02.md`).

## The one fact six findings rest on

`InputManager::applyStateChange` stamps `buttonPressStart` **only when no
button was already down** (`freeink-sdk/libs/hardware/InputManager/src/InputManager.cpp:252-254`),
and `getHeldTime()` returns `millis() - buttonPressStart` while anything is
down (`:460-467`). So `getHeldTime()` is ONE GLOBAL CHORD TIMER, not a
per-button hold. Press Right, hold it two seconds, then press Confirm: on
Confirm's first frame `getHeldTime()` already reads ~2000. Two activities
know this and defend against it with their own `millis() - pressedAt`
(`EpubReaderActivity.cpp:429-438`, `DaisyEntryActivity.cpp:213-218`). Six
other hold sites read `getHeldTime()` raw — F4, F8, F9, F10 below. The fix
shape is the same for all of them: one per-button stamp on the press edge
(a shared helper, since `hold-gestures.md` already counts the thresholds
that would want it).

Holding a front button is not an exotic input here: column movement on the
keyboard, list paging on the font picker, the file browser and Manage Files
are all `onPressAndContinuous` / `onContinuous` — holding IS how you cross a
long list — so "held Right, then tapped Confirm" is a gesture readers make.

## Findings, ranked

Severity is for the owner's own devices (X3 hardware and the iOS X3 app,
neither of which has firmware touch). Touch-only findings are marked.

### F4 — P1 — Keyboard: a Confirm tap while Left/Right is held wipes the field. VERIFIED — FIXED 2026-09-02

`src/activities/util/KeyboardEntryActivity.cpp:769-782`; thresholds
`KeyboardEntryActivity.h:135-136` (`LONG_PRESS_MS = 500`,
`DEL_LONG_PRESS_MS = 1500`). Column movement is `onPressAndContinuous`
(`:704`, `:731`). Hold Right ~2 s to slide onto DEL, press Confirm without
lifting Right: next frame `getHeldTime() > 1500` → `text.clear()`
(`:378-380`), `confirmLongHandled = true`, the release does nothing. Whole
entry gone, no undo. Same at `:776-782` with `> 500`: a Confirm tap yields
the UPPERCASE letter and swallows the release, so the lowercase one is never
typed. Fix: stamp `confirmPressedAt` at `:762`, compare against that.

### F3 — P1 — Wi-Fi list cannot be stepped forward. VERIFIED — FIXED 2026-09-02 (side pair steps; NOT the fix below — see the status table)

`src/activities/network/WifiSelectionActivity.cpp:685-695` vs `:750-760`.
`Right` returns at `:685` (rescan) before `buttonNavigator.onNext` at `:750`
can run, and `NavNext` IS front Right (`MappedInputManager.cpp:98`). `Left`
steps the highlight up only when the highlighted network has no saved
password; on a saved one it opens Forget (`:691-696`). The side pair pages
(`:734-746`) and `ButtonNavigator::pageDown` clamps to nothing on a
one-screen list (`ButtonNavigator.cpp:141`), which is the common case. So
with four networks found there is no button that moves the highlight down.
The hints say the intent was never list nav (`mapLabels(BACK, CONNECT,
forgetLabel, RETRY)` `:877`). Fix: move Retry/Forget off the front pair.

### F5 — P1 — Back out of a book lands Home on the last MENU row. VERIFIED — FIXED 2026-09-02 (see the status table; the fix below was NOT the one taken)

`src/activities/ActivityManager.cpp:248-250`: `goToReader` is the one
`goTo*` wrapper that does not set `lastHomeMenuItem`. `goHome()` reuses it
(`:282`), `HomeActivity::onEnter` sets `selectorIndex = base +
menuItemToIndex(initialMenuItem)` (`HomeActivity.cpp:131`). Home → Settings
→ Back → side Up to the covers → open a cover → read → Back: Home opens on
the Settings row on the MENU page, the book a page away. Fix:
`lastHomeMenuItem = HomeMenuItem::NONE` (or `RECENTS`) in `goToReader`.
The fresh-boot case (`NONE` → index 0 = a cover) is deliberate — it is the
Resume-on-Back feature at `HomeActivity.cpp:323` — and NOT filed.

### F8 — P1 — Reader: tap Back while a side button is held → file browser. VERIFIED — KILLED 2026-09-02

`src/activities/reader/ReaderUtils.h:262`, from `EpubReaderActivity.cpp:479`
and `XtcReaderActivity.cpp`. `isPressed(Back) && getHeldTime() >=
GO_BACK_OR_HOME_MS` (1000, `ReaderUtils.h:17-18`). Hold a side button ≥1 s
(the font-family hold is 700 ms, so readers do this), tap Back without
lifting: the LONG-press destination fires on Back's press edge. Note the
2026-09-01 ruling killed held-Back in FileBrowser and FileManager
(`hold-gestures.md`); the reader still overloads a Back hold with a second
destination. Same button, two screens, two meanings — that half is an
owner call, the chord false-trigger is a bug either way.

### F9 — P1 — Font picker: Confirm while paging DEACTIVATES the font. VERIFIED — FIXED 2026-09-02

`src/activities/settings/FontSelectionActivity.cpp:218-224`, `SKIP_HOLD_MS
= 700`. Paging is `onContinuous(kNextButtons)` (`:288`). Hold Right ~1 s to
page, press Confirm on the family you landed on: `getHeldTime() >= 700` →
`toggleSelectedFontActive()` writes `SETTINGS.fontsOff` and saves
(`:365`, `:408`); if it was the family being read, the reader is moved off
it (`:392-404`). Release marked spent (`:226-228`), so the apply never
happens either.

### F6 — P1 — Manage Files eats the first Confirm after View or Rename. VERIFIED — FIXED 2026-09-02

`src/activities/home/FileManagerActivity.cpp:602-607` sets
`lockNextConfirmRelease` when the action popup closes on a Confirm press;
`:656-659` is the only clear, inside `activateSelected`. Move / Duplicate /
Summarize stay in-activity so the next release clears it. View (`:323`)
and Rename (`:332`) PUSH a child via `startActivityForResult`; the release
lands in the child and is eaten by `swallowUntilIdle()`. FileManager is
pushed, not re-entered, so `onEnter` (`:129-141`) never re-derives the
latch. Back from the viewer → first Confirm on any file does nothing.
Fix: clear the latch in both result handlers.

### F7 — P1 — Edit a note from Manage Files → folder selection lost. VERIFIED — FIXED 2026-09-02

`FileManagerActivity.cpp:292` → `goToNoteEditor` → `replaceActivity`
(`ActivityManager.cpp:265`, deliberate: heap for NimBLE, comment at
`:255-263`). `NoteEditorActivity::exitEditor()` (`:183-189`) then
`goToFileManager(returnDir)` — a NEW FileManager, `selectorIndex = 0`
(`:138`). 20th file → Edit → Back → top of the folder. Fix: carry the entry
name into `goToFileManager` and `findEntry()` it on enter (the helper
already exists for rename/move).

### F10 — P2 — Same chord false-trigger, three more sites. VERIFIED — FIXED 2026-09-02

Each ends in a Cancel-default popup, so lower blast radius:
`FileBrowserActivity.cpp:148` (hold Right to page, tap Confirm → Delete?
instead of open), `RecentBooksActivity.cpp:69-74` (→ Remove from recents?),
`FileManagerActivity.cpp:637-638`, `:672` (→ action menu instead of open).

### F11 — P2 — Claude answer pages backwards under swapped side buttons. VERIFIED — FIXED 2026-09-04

`ClaudeChatActivity.cpp:394-405` reads raw `Button::Up/Down`; every other
paging surface reads `PageNext/PagePrevious`, which honor
`sideButtonLayout` (`MappedInputManager.cpp:111-122`). (Front Left/Right
paging here is T-022, already landed at the same lines.)

### F1 / F2 — P2, TOUCH BOARDS ONLY — Home page-2 hit-test and cover grid. VERIFIED, not reachable on X3/X4/iOS

`HomeActivity.cpp:330-353` vs render `:457-460`. LyraSix
(`themes/lyra/LyraSixTheme.h:24-26,38`: 3×2 covers, 312 px tile, menu on
its own page) is the only theme. On the menu page render puts the menu at
56+16 = 72 (`:458`, `splitPages` branch); `loop()` hit-tests it at
56+312+16 = 384 (`:346`, one-page formula, never updated). Neither the cover
touchdown (`:330`) nor cover tap (`:339`) checks which page is showing, and
both hardcode `selectorIndex = 0`. Consequences: tapping rows 0-3 on the
menu page opens the most recent book; tapping Create Note opens Recents;
any top-row cover opens book #1; bottom-row covers hit the misplaced menu.
Needs `wasScreenTouchDown`/`wasTapInRect`, i.e. `BoardConfig::hasTouch()`
— X4 Pro and Sticky only. Real, shippable-to-upstream, and invisible on
every device the owner has.

### F12 — P2 — Claude: no input for the whole exchange, nothing on screen says so. PLAUSIBLE

`ClaudeChatActivity.cpp:220-232`: `send()` runs `claudechat::runExchange`
synchronously inside `loop()`; each `setPhase` blocks in
`requestUpdateAndWait()` (`:152`). `View::Working` draws no button hints.
Worst-case duration bounded only by the HTTP timeout, which was not read.

### F13 — P2 — Confirm fires on PRESS on some lists and on RELEASE on others. VERIFIED (inventory) — CONVENTION WRITTEN 2026-09-04

Press: `SettingsActivity.cpp:154,164`, `WifiSelectionActivity.cpp:670-683`,
`TypographySettingsActivity.cpp:189`, `ColophonActivity.cpp:171`,
`EditorFontSelectionActivity.cpp:194`. Release: `FileBrowserActivity.cpp:209`,
`FileManagerActivity.cpp:701`, `RecentBooksActivity.cpp:76`,
`EpubReaderChapterSelectionActivity.cpp:109`. A press-edge screen cannot
grow a hold without first moving its tap (the migration
`FontSelectionActivity.cpp:204-217` documents). A convention to write down,
not a bug to fix screen by screen.

### F14 — P2 — Keyboard cursor mode has no auto-repeat. VERIFIED — FIXED 2026-09-04

`KeyboardEntryActivity.cpp:704-708`, `:731-735`: `if (cursorMode) return;`
in the continuous callbacks, move on `wasReleased`. 60 presses to cross a
60-character field; the same button repeats outside cursor mode.
`NoteEditorActivity.cpp:368-386` already has the `repeatCaret` timer to copy.

### F15 — P2 — Paging past the end of a Claude answer still refreshes. VERIFIED — FIXED 2026-09-04

`ClaudeChatActivity.cpp:396-398`, `:402-404`: `requestUpdate()` outside the
bounds check. One-line fix.

### F16 — P3 — `IntervalSelectionActivity` is unreachable. VERIFIED

`src/activities/util/IntervalSelectionActivity.{h,cpp}` (174 lines),
included at `SettingsActivity.cpp:29`, constructed nowhere in `src/`
(`grep -rln IntervalSelectionActivity src` → the header, the .cpp,
SettingsActivity.cpp). Its `ignoreConfirmRelease` (`.h:45`) is dead against
its own `wasPressed(Confirm)` handler (`.cpp:93`).

## Checked and CLEAN — do not re-read for this bug class

- `src/MappedInputManager.cpp`, end to end: swallow state machine
  (`update()` `:352-375`, `swallowUntilIdle()` `:377-397`, the gated
  readers `:399-450`) coherent — arming frame reads idle, per-button mask,
  stale release eaten for the whole frame, level and `getHeldTime()` gated.
  No `lockLongPressBack` residue anywhere in `src/` (grep: zero hits). Its
  one weakness is the global `getHeldTime()`, documented at `:452-459`.
- `src/util/ButtonNavigator.{h,cpp}`: separate nav/page repeat clocks
  correct; `onRelease` swallows the release that ended a repeat;
  `pageDown/pageUp` clamp and return false as documented.
- `src/activities/ActivityManager.cpp`: push/pop/replace/
  `ReplaceCurrentOnly`, `swallowUntilIdle()` at all four transition points,
  `goToSleep`'s `processPendingTransitions()`. Only gap was F5, fixed the
  same day.
- `src/components/OptionPopup.h`: `drainingClosePress` requires level-low
  AND no latched edge; touch-close skips the drain; layout cache
  invalidated on `show()`/`setInfoLines`. FileManager gates on `isActive()`
  rather than `handleInput()`'s return and compensates with its own two
  latches — sound apart from F6.
- `DaisyEntryActivity.cpp`: per-button `pickPressedAt` is the correct
  pattern; `activePick` exclusion, ring-swap petal preservation, UTF-8
  backspace check out. No touch handling — by design, button-first.
- `NoteEditorActivity.cpp`: caret-mode gating above Back, `pickSlot`/
  `pickFired` reset on space-hold entering caret mode (`:458-461`),
  `repeatCol`/`repeatCaret` per-button, `loadRefused` guarding `save()`,
  drain above `panelHidden`.
- `EpubReaderActivity.cpp` loop (`:237-640`): Confirm-modifier chord,
  `sideChordConsumed`, `suppressNextSideRelease`, `ignoreNextConfirmRelease`,
  twin-latch retirement `:521-531`. Could not be broken. Chapter-skip
  removal left no unreachable branch.
- `EpubReaderChapterSelectionActivity.cpp`: selection recomputed from
  `currentSpineIndex` per entry; notes-row offset consistent across
  `loop()`, `render()`, `getTotalItems()`; Book Notes pushed so the
  highlight survives.
- `TextViewerActivity.cpp` `loop()` (`:359-390`): both pairs paging the
  same unit is deliberate and commented.
- `SettingsActivity.cpp`: `rebuildSettingsLists()` clamps the index
  (`:127-130`); every mutation reaches `requestUpdate()`; dark mode reaches
  the panel driver before the repaint (`:296`).
- `FileBrowserActivity.cpp` / `FileManagerActivity.cpp` delete/rename
  focus: `findEntry()` after rename/move/duplicate, clamp-to-last after
  delete.
- Hold-kill residue (2026-09-01): `lockLongPressBack` gone from both
  headers and both .cpps; `DELETE_HOLD_MS` / `CONFIRM_HOLD_MS` renames
  accurate; `CHAPTER_SKIP` tombstoned without renumbering
  (`CrossPointSettings.h:154-170`), no live comparison remains.

## Not read

`boot_sleep/**`, `CrossPointWebServerActivity`, `OnlineFirmwareUpdateActivity`,
`SdFirmwareUpdateActivity`, `LibraryUpdateActivity`, `ColophonActivity`,
`ClockOffsetActivity`, `ClearCacheActivity`, `BmpViewerActivity`,
`PrettyView`, `XtcReader*` beyond its `handleBackNavigation` call,
`BookNotesActivity`, `EpubReaderFootnotesActivity`, `notes/KeyboardPanel.cpp`,
`lib/GfxRenderer/**`. A second pass starts there.

## Second pass, 2026-09-04 — the `## Not read` list

A read-only refuting pass over exactly the files the first pass skipped, told
to disprove each candidate first and to list what it found CLEAN. Verified by
reading at `7cabffea0`; nothing in `build/test` covered these paths.

| # | Where | What | Status |
|---|---|---|---|
| 1 | `BookNotesActivity.cpp` | Paging by a flat `contentHeight / lineHeight` while `render()` inserts a half-line gap before every headline: ~2 lines skipped per page, and the last `ceil(G/2)` lines of the final note unreachable. | **FIXED** — `linesFrom()` / `pageStartBefore()` lay the page out the way `render()` does; the side pair, swipes and hold-repeat page by what fits; `maxScroll` is the start of the last page. Not pinned by a test (the harness has no BookNotes fixture); verified by reading the two paths side by side. |
| 2 | `LibraryUpdateActivity.cpp` | `fetchManifest` returns OK with zero books (every entry can `continue` on a missing asset); the SYNCING frame indexed `getBooks()[currentBook]` on an empty vector — a LoadProhibited panic. | **FIXED** — zero books is DONE. |
| 3 | `BmpViewerActivity.cpp` | Raw `Button::Up/Down` step siblings, ignoring `sideButtonLayout` (F11's class). | **FIXED** — `PagePrevious`/`PageNext`. |
| 4 | `OnlineFirmwareUpdateActivity.cpp` | CONNECTING is a live loop that never read Back: 20 s connect + 8 s DNS with no hint bar when the saved network is out of range. | **FIXED** — Back finishes during CONNECTING, and the frame draws the Back hint. |
| 5 | `BmpViewerActivity::onExit` | The only `onExit` that paints (clear + present); a Quick Resume sleep from the viewer saves a blank frame with the moon on it. Only when the sleep MODE is Quick Resume. | **RECORDED, not fixed** — the clear exists to take the image's ghost off the panel before the browser repaints; dropping it trades a blank Quick Resume frame for ghosting on every exit. An owner call. |
| 6 | `BmpViewerActivity.cpp` | Confirm on the "Invalid BMP" screen still copied the bad file over `/sleep.bmp` and switched the sleep screen to CUSTOM. | **FIXED** — `imageValid` gates it. |
| 7 | `ClockOffsetActivity.cpp` | `pageItems` from the full content height while `render()` reserves a preview row on an RTC board: on a 15-row list the side pair was dead. Latent (needs a custom offset row). | **FIXED** — one row fewer when the preview shows. |
| 8 | `lib/GfxRenderer/GfxRenderer.cpp` `fillPolygon` | The triangle branch read `RENDER_SCALE` (the ceiling) where every other primitive reads `cp::renderScale()` — the misuse the header's own comment names. Invisible while the latch equals the ceiling. | **FIXED** — reads the latched factor. |

No survivor in: raw global `getHeldTime()` (only the documented single-button
site in `EpubReaderActivity`), overflow/allocation from untrusted input in
`Bitmap.cpp` / `PrettyView` / `BitmapHelpers`, the wake path.

CLEAN, with what was checked: `BootActivity` (draw-only); `SleepActivity`
RTC-less fallback, polarity reset order, rotation window, cover fallbacks;
`SleepScreenPolicy` (pure, tested); `CalendarSleepScreen` legend arrays, row
math, week clamp, 24 labels vs the enum; `HolidayCalculator` Zeller, Meeus,
`addDays`, local-date bound, `nth == -1`; `main.cpp` wake path; the web-server
activity's popup/abandon/hand-off paths (the double header is cosmetic);
`OnlineFirmwareUpdate` beyond #4; `SdFirmwareUpdate` recovery loop, TOCTOU
re-validation, SUCCESS gating; `LibraryUpdater::syncBook` `.part` handling
(never reports success on a partial; `remove` before `rename` could lose the
old copy on a rename failure, reported FAILED — not filed); `Colophon` clamps
and `appendAddress`; `ClearCache` popup semantics; `XtcReader` end-of-book,
progress clamp, 2-bit planes (a dead `wasReleased(Back)` at end-of-book —
cosmetic); the Xtc chapter list; `EpubReaderFootnotes`; `PrettyView`'s every
binary reader bounds-checked against `data.size()`; `KeyboardPanel` clamps
(its file-scope comment describes a static mirror that no longer exists —
stale comment); `Bitmap.cpp` re-checked for the 24-byte class (`planes == 0`
first, dimensions ≤ 0 rejected, `rowBytes` capped, palette ≤ 256, all six bpp
row indices); `BitmapHelpers` error rows; `GfxRenderer.cpp` `drawPixel`,
`fillRectImpl`, `drawBitmap*` allocations, draw/measure kern parity,
`fillRoundedRect`, `readFramebufferRegion` (the bottom-up `break` at
`:1829` would drop an image whose bottom rows land off-screen; every in-scope
caller fits it first — not filed).
