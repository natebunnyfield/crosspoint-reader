# Input-edge audit — activity transitions (2026-08-21)

Read-only audit of the stale-edge bug class, run after the owner's
"BACK from settings opens the book" report (fixed same day, dfeb1232e).
Surveyed at the commit before that fix; confidence per row. The class:
an edge produced for one consumer read by a second after a boundary.

## The model (verified)

- Edges recomputed and cleared at the top of every `update()`
  (`freeink-sdk/.../InputManager.cpp:413-414`, `:250-251`); one update per
  main-loop iteration before any activity runs (`src/main.cpp:1068→1140`).
- Transitions are processed after the departing activity's loop, same
  iteration; the arriving activity runs NEXT iteration
  (`ActivityManager.cpp:82-169`). So a PRESS edge cannot cross a boundary;
  the matching RELEASE can, 1..N frames later.
- `swallowUntilIdle()` arms only if a button is held at swap time
  (`MappedInputManager.cpp:330-335`). `isPressed()`, `getHeldTime()`,
  `wasAnyPressed/Released()`, `getPressedFrontButton()` and ALL touch
  readers bypass the swallow.
- dfeb1232e fixed: the clear checked only LEVELS, so the frame's first
  gated query cleared the flag and a later query read the latched release
  (the owner's bug); and pop-to-empty→goHome never swallowed.

## Remaining findings (post-fix), ranked

Findings 1-4 FIXED 2026-08-22 (commit pending) — see "Fixes" below. 5-9 remain
documented only.

| # | Mechanism | Producer | Stale consumer | Failure | Conf |
|---|---|---|---|---|---|
| 1 | Hold crosses boundary: `isPressed`+`getHeldTime` bypass swallow; hold timer never resets on transition | NoteEditorActivity.cpp:571 exit on wasPressed(Back) | FileManagerActivity.cpp:608 `isPressed(Back) && getHeldTime()>=GO_HOME_MS` | Hold Back ~1 s to leave a note → Manage Files jumps to SD root, not the note's folder | verified |
| 2 | Modal close-press → host release-handler (no swap, swallow never applies) | OptionPopup.h:112-115 Back press closes | RecentBooksActivity.cpp:94 wasReleased(Back)→Home | Dismiss "Remove from recents?" with Back → lands on Home | verified |
| 3 | Lock cleared by a swallowed edge read — B-026 fixed FileManager, not its twin | ReaderUtils.h:262-267 long-press Back → Browse; FileBrowserActivity.cpp:85 sets lockLongPressBack | FileBrowserActivity.cpp:124 clears via wasReleased(Back), which the swallow suppresses | Long-press-Back-to-root dead for the session; next Back tap eaten once (fix pattern: level read, FileManagerActivity.cpp:617-625) | verified |
| 4 | `replaceActivity` immediate-launch branch never swallows (fix patched one call site) | ActivityManager.cpp:195-199 | boot routing main.cpp:983-996; recovery main.cpp:937 entered with UP held (:861) | Recovery picker selection steps one row on UP release; class re-opens for any goTo* from null-current | verified, low freq |
| 5 | Re-entrant loop() in goToSleep(): outgoing activity runs with the frame's edges; its push discards pending SleepActivity | ActivityManager.cpp:238-241→:82 | :297-303/:193-194 | Menu press same frame as sleep timeout → sleeps without sleep screen, half-entered activity saved | verified, rare |
| 6 | New clear condition is edge-sensitive: latch survives any frame with ANY edge (incl. touch-synth CONFIRM InputManager.cpp:747) | MappedInputManager.cpp:346,356 | any | Genuine second press in the same 50 ms idle window as the stale release is dropped | suspected |
| 7 | Mid-frame update() inside File Transfer inner loop destroys edges for other consumers | CrossPointWebServerActivity.cpp:399 | main.cpp:1103 inactivity timer + all non-Back buttons | Presses vanish during transfer; inactivity not reset | verified, low |
| 8 | `getPressedFrontButton()` bypasses swallow; its consumer was deleted | MappedInputManager.cpp:419-435 | none | latent | verified |
| 9 | ButtonNavigator::lastContinuousNavTime never reset at entry | ButtonNavigator.cpp:50-55 | same activity after pop | first Left/Right after return could be swallowed mid-auto-repeat | suspected, no path found |

## Fixes (2026-08-22, commit pending)

All four verified headlessly on the desktop simulator, before (bug) and after
(fixed), same script both times; card state built and restored per run.

- **1 — FIXED at the mechanism**: `swallowUntilIdle()` now gates the LEVEL
  reads too — `isPressed()` returns false and `getHeldTime()` returns 0 while
  the swallow is active (`src/MappedInputManager.cpp`, same clear condition as
  the edge reads). Chosen over per-consumer pairing flags because the swallow
  already arms exactly at the boundary while a button is held, so every hold
  branch in every activity is covered at once. Evidence: editor → hold Back
  1.5 s; before, Manage Files landed at `/` (root listing, path line `/`);
  after, it lands in the note's folder (`/!qa`, title and path line show it).
  `[ACT]` path identical both runs: FileManager → NoteEditor → FileManager.
- **2 — FIXED generically in the framework**: `OptionPopup` now consumes the
  whole closing press — `closeOnPress()` records the button and `handleInput()`
  keeps returning true after close until level low AND no edge latched
  (`src/components/OptionPopup.h`). RecentBooks and FileBrowser now gate on
  `handleInput()` instead of `isActive()` so the drain is reachable;
  FileManager keeps its own release locks. Evidence: Recents → long-press
  Confirm → Back dismiss; before, `[ACT]` shows RecentBooks → Home 85 ms after
  the tap; after, no transition, screenshot still in Recents.
- **3 — FIXED with the level-read form** (B-026's twin):
  `FileBrowserActivity.cpp` clears `lockLongPressBack` on `!isPressed(Back)`
  instead of the swallow-suppressed `wasReleased(Back)`. Evidence: reader →
  long-press Back → Browse → long-press Back again; before, still `/zzqa` and
  the following short Back only went up one level; after, the second
  long-press jumps to SD root and the next short Back goes Home (`[ACT]`
  FileBrowser → Home).
- **4 — FIXED**: the swallow moved INTO `replaceActivity()`'s immediate-launch
  branch (`ActivityManager.cpp`), the pop-to-empty call removed as redundant,
  and `test/activity_input/HostHarness.cpp` mirrors both, so the
  test/production divergence noted under "Prior art" is closed. Covered by the
  activity_input suite (links the real `MappedInputManager`).

ctest: 367/369, the two failures pre-existing and unrelated (EditorFontsTest,
SettingDisplayOrderTest — font-list assertions). Desktop canary SUCCESS;
simulator repo `tests/run_all.sh` 31/31.

Closed by dfeb1232e, worth regression pins: action-menu Confirm press → TextViewer togglePretty (FileManagerActivity.cpp:634-641 → TextViewerActivity.cpp:364); every wasPressed-pop → HomeActivity.cpp:323/369 (crash screen / SD error / "Library updated" dismissals opening the last book or relaunching the updater).

## Audited clean (guards cited)

- Text entry: paired setTextEntryActive in all four editors; onExit runs on pop AND replace; none pushes a child.
- Editors' key activation: press/release pairing flags (KeyboardEntry:761-790, NoteEditor:682-696, ClaudeChat:459-472, Daisy:205-219). This pairing pattern is what ce652c05e DELETED from HomeActivity — the deleted guard would have blocked the owner's bug.
- Manage Files ← viewer/rename: locks re-armed in result handler, cleared with a level read (:617-625).
- Sleep/wake: waitForPowerRelease drains the hold (main.cpp:1017); Sleep/Boot read no input.
- Silent-reboot boot: blocking paint + two spaced gpio.update() absorb a held button (main.cpp:1005-1013).
- Boot-into-book: book opens on Confirm RELEASE; detectPageTurn doesn't read Confirm.
- Reader chords: intra-activity latches (EpubReaderActivity.cpp:430-585).
- Settings root + press-driven sub-screens: press edges only; stale release inert.
- Touch taps/swipes: one-frame latch cleared before the arriving activity's first loop.

## Prior art

- ce652c05e (2026-08-08) centralized child-exit swallow and deleted the per-activity pairing guards — the regression's origin.
- 8efbd5e6c re-fixed one consequence (B-026) in FileManagerActivity only; finding 3 is the missed twin.
- Test/production divergence: HostHarness.cpp:253-255 had the swallow the firmware lacked, so the suite stayed green while the device was broken. dfeb1232e mirrors them; moving the swallow INTO replaceActivity's immediate branch would close finding 4 and make divergence impossible.
