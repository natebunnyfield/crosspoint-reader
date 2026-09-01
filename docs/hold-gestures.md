# Hold-for-action: the survey, and why T-027 was rewritten

**Dated 2026-08-29. Surveyed at `b398d45`.** Every threshold and call site below
was found by grep and read in place; the citations are what was actually there
on that commit, not what an earlier doc claimed.

**Citation check, 2026-08-30: `b398d45` does not resolve in this repository**
(`git cat-file -t b398d45` -> "Not a valid object name"; no matching sha under
`git log --all --oneline`). Left as written rather than repointed to a guess —
whoever wrote it either mistyped the short sha or the commit it named was later
rewritten. **The counts below have also since moved**: firmware work landed
between 2026-08-29 and 2026-08-30 (the font-activation feature, T-026) added at
least one more file (`ReaderUtils.h` now carries live `getHeldTime()` call
sites it did not before). A raw `grep -rn "getHeldTime()" src/ lib/` today
returns matches in 15 files rather than the 10 listed below, but that count
mixes call sites with declarations and comments and is not a clean recount
against this doc's own methodology — re-survey properly (call sites only,
`.cpp`/inline-header logic, not `MappedInputManager.h`'s declaration or
comments) before relying on an exact number for T-027; do not read "15" as this
doc's replacement figure.

## Why this file exists

T-027 was filed as "two-finger hold on Manage Files should open the Menu
action", and it was going to be built as one of three shapes: a new action in
the iOS gesture table, a hard-wired binding in `FileManagerActivity`, or not at
all. The owner rejected all three:

> *"we need to rewrite that and any 'hold for action' to be friendly to how we
> do gestures now"*

So the item is not "add one binding". It is: the firmware grew its
hold-for-action behavior one activity at a time, before the gesture model
existed, and it needs to be re-expressed in terms of that model. This file is
the measurement that has to exist before anything is proposed, because the
scope was not knowable from T-027's text.

## What is actually there: nine thresholds

Not one hold. Nine, spread across ten files, none of them agreeing.

| ms | Constant | Defined at | What it gates |
|---|---|---|---|
| 90 | `TOUCH_DOWN_SELECT_DELAY_MS` | `src/MappedInputManager.cpp:138` | touch tap candidate (`:162-164`) |
| 350 | `TOUCH_LONG_PRESS_MS` | `src/activities/util/KeyboardEntryActivity.h:141` | touch alt-output on a key |
| 500 | `LONG_PRESS_MS` | `src/activities/util/DaisyEntryActivity.h:84` | daisywheel alt output |
| 500 | `LONG_PRESS_MS` | `src/activities/util/KeyboardEntryActivity.h:139` | button alt output |
| 500 | `LONG_PRESS_MS` | `src/activities/util/ClaudeChatActivity.cpp:456` | (local constant) |
| 500 | `LONG_PRESS_MS` | `src/activities/util/NoteEditorActivity.cpp:670` | (local constant) |
| 700 | `SKIP_HOLD_MS` | `src/activities/reader/ReaderUtils.h:19` | chapter skip; font-family hold; font de/reactivate |
| **750** | `zenhold::kHoldMs` | `crosspoint-simulator/ios/ZenHoldRouting.h:38` | **the iOS gesture model's ONE hold** |
| 900 | `TOUCH_DEL_LONG_PRESS_MS` | `src/activities/util/KeyboardEntryActivity.h:142` | touch clear-all |
| 1000 | `LONG_PRESS_MS` | `src/activities/home/RecentBooksActivity.cpp:17` | recent-book action |
| 1000 | `GO_HOME_MS` | `src/activities/home/FileBrowserActivity.cpp:19` | held Back → Home; held Confirm → menu |
| 1500 | `DEL_LONG_PRESS_MS` | `src/activities/util/KeyboardEntryActivity.h:140` | button clear-all |
| 1500 | `HOLD_MS` | `src/activities/util/ClaudeChatActivity.cpp:265` | (local constant) |
| 1500 | `HOLD_MS` | `src/activities/util/NoteEditorActivity.cpp:328` | (local constant) |

Four of these are file-local `constexpr` declared inside a `.cpp`, so nothing
can see or reuse them. Two different constants are both named `LONG_PRESS_MS`
and hold different values (500 and 1000). Four sites named `HOLD_MS` /
`LONG_PRESS_MS` are declared inside function bodies.

`getHeldTime()` has **30 call sites across 10 files**:
`MappedInputManager.cpp`, `util/ButtonNavigator.cpp`,
`reader/XtcReaderActivity.cpp`, `reader/EpubReaderActivity.cpp`,
`settings/FontSelectionActivity.cpp`, `home/FileBrowserActivity.cpp`,
`home/FileManagerActivity.cpp`, `home/RecentBooksActivity.cpp`,
`util/KeyboardEntryActivity.cpp`, `util/DaisyEntryActivity.cpp`.

## The one that is already a setting, and is therefore different

`SETTINGS.longPressButtonBehavior` (`src/CrossPointSettings.h:154`) is an
`OFF / CHAPTER_SKIP / FONT_SIZE_STEP` enum defaulting to `FONT_SIZE_STEP`
(`:479`). It is the only hold in the firmware the reader can already
reconfigure, and it is a *behavior* selector rather than a binding — the
gesture model's shape is the other way round (a gesture names an action; the
action list appends).

It also has non-obvious edge behavior that any rewrite must preserve, recorded
in the code rather than here: the reader's holds are RELEASE-triggered whenever
`longPressButtonBehavior != OFF` (`EpubReaderActivity.h:58-60`,
`EpubReaderActivity.cpp:521,577,585`), and the chapter-skip hold fires the
moment it crosses `SKIP_HOLD_MS` rather than on release, so the reflow starts
while the finger is still down (`EpubReaderActivity.h:46`). `EpubReaderActivity.cpp:444`
records a real bug caused by two hold branches sharing one threshold.

## What "friendly to how we do gestures now" means

The gesture model (`crosspoint-simulator/ios/GestureBindings.h`, truth-tabled in
`tests/gesture_bindings_test.cpp`) has properties none of the above has:

- **One table** is the single authority on what a gesture does.
- **One hold threshold**, 750 ms, tuned by the owner and arrived at by two
  revisions the same day.
- Bindings **persist as integers keyed by a string**, so the action list appends
  freely and the gesture enum can be re-sorted.
- A gesture can be bound to **Nothing** explicitly, distinct from inheriting.
- Zone overrides are **layered**, and blank falls through.

The firmware has none of that: no table, nine thresholds, no persistence, no way
to rebind, and hold semantics decided per activity.

## What is NOT decided here

Whether the firmware should grow its own binding table, adopt the simulator's,
or simply converge on one threshold and one hold helper — and whether this
applies to hardware at all, given that X3 and X4 have no touch
(`HalGPIO::hasEdgeSideButtons()` is the per-board authority; X4 Pro and Sticky
do have touch). That is an architectural choice with more than one defensible
answer, and it goes to the owner before any code moves.

**Nothing in this file has been changed in code.** It is measurement only.

## RULING, owner 2026-09-01: which holds die and which live

Asked what replaces hold, against full menu-ization and a staged plan, he drew
the line himself, verbatim:

> "kill ble pairing, kill back to home, kill chapter skip, keep daisywheel
> uppercase and clear-all"

So hold as a trigger DIES where it is invisible or navigational, and SURVIVES
where it is typing ergonomics:

| Hold | Fate | Notes |
|---|---|---|
| Up/Down 1500 ms -> BLE pairing (ClaudeChat, NoteEditor) | **KILL** | The silent display-stall trigger. Pairing itself must remain reachable -- the code's own comment says Daisy users pair from Settings, so that path exists; verify it covers everyone before deleting the gesture. |
| Held Back 1000 ms -> Home (FileBrowser, FileManager `GO_HOME_MS`) | **KILL** | Back keeps its ordinary single-press meaning. |
| `SKIP_HOLD_MS` chapter skip (`longPressButtonBehavior = CHAPTER_SKIP`) | **KILL** | FONT_SIZE_STEP was NOT named and stays. The setting persists as an integer, so CHAPTER_SKIP is retired the way renderScale=3 was: a stored value maps to a survivor, the option leaves the picker, nothing renumbers. |
| Daisywheel / keyboard hold -> uppercase | **KEEP** | |
| Delete hold -> clear-all | **KEEP** | |

Not named, therefore untouched: font-size step (the shipped default),
font de/reactivate in the reader font list (shipped 2026-08-29), the
recent-books hold, the touch-side keyboard holds, and the iOS gesture model's
own 750 ms hold, which is bindable and was never in question.
