# Picco layout: the plan (2026-11-26)

Owner, 2026-11-26: *"let's plan out how to improve the layout of picco to be
better, using md file guidance from x3 and x4. there is a lot written up and it
was not followed."*

Evidence: `docs/picco-layout-audit-2026-11-26.md` (38 written rules from the
X3/X4 firmware docs and the X3 iOS app docs, each checked against the Picco
with code citations and Picco | X4 | X3 captures). This file turns it into
work. Nothing here is built yet.

## A. Already ruled: fix without asking

These break a ruling that already applies to the Picco, so they need no new
decision.

| # | What | Ruling it breaks | Change |
|---|---|---|---|
| A1 | The iOS app draws three lines of touch counters above the page on every frame, Diagnostics off or on | Diagnostics are off by default (ios/README.md:1516-1522, 2026-08-09); "nothing on the page moves" (:1980-1986) | Draw the counters only while Settings > Diagnostics Log is on (the counters were added for TestFlight build 3's question, answered the same day) |
| A2 | Keyboard tips name controls the Picco cannot send: "Hold UP to edit entry", "Press DOWN to return to keyboard", "Hold DEL to clear all text" (a touch hold on Del deletes one character by ruling) | "Don't label controls the device does not have" (BaseTheme.cpp:169-172, 2026-08-09); the Del-hold ruling (picco-port 2026-11-06) | Picco-specific tips: none for unreachable modes; the clear-all tip names the held Select it actually is |
| A3 | Brightness/Warmth stepping asks for a "Done" hint that a touch board never draws, so the way out of the mode is invisible | picco-input-settings-2026-11-06.md:29-37 (design, not a ruling) | Show the way out on the row itself (e.g. `< 60 >` plus a tap or Back ends it), Picco only |
| A4 | The new slide switch's knob is filled at rest | "Controls are hollow: outline at rest, wash only while pressed" (ios/README.md:381-387, 2026-08-03) | Knob drawn as an outline at rest; its side is the state |

## B. Needs the owner (one question at a time)

| # | Question | Why it is open |
|---|---|---|
| B1 | On a touch board, is "header back arrow, no hint bar" the design, or should the Picco show what its own controls do (a touch hint bar, or labels), including a visible way out of Create Note? | ui-conventions.md:146-149 ("every screen that takes input draws a hint bar") predates touch boards; the theme suppresses the bar on every touch board, so the Picco shows no on-screen hint of what Confirm or the rocker do |
| B2 | Rows: finger-sized on the Picco, or the X4's 40 px? | The SDK's PICCO profile carries `uiScale 1.2` "bump chrome toward finger size" but nothing reads it; Picco rows are 40 px = 4.3 mm on the 3.97" panel. The only written touch floor is the iOS pad's 44 pt |
| B3 | iOS pad: keep the X3 slots, or move the rocker (the Picco's only next/previous) into the empty top-right slot on the thumb row? | thumb-reach-2026-10-06.md:87-99 puts the most-used control in the thumb band; the Picco's most-used control sits a row below an empty slot |
| B4 | Measure the Picco's bezel-covered edges? | page-space-2026-11-08.md:11-18: insets are per board "as boards are measured"; the Picco uses the X4 defaults, and Home's menu runs to the panel's bottom edge with no hint band to keep it off |
| B5 | Does the X3 app's "the gap stays" (empty half-row, 2026-08-19) extend to the Picco's empty slot? | Moot if B3 moves the rocker |

B1, B3 and B5 are visual: each goes with a rendered page before it is asked.

## C. Order of work

1. A1-A4 in one change on `picco-fix` (iOS shim, keyboard tips, settings
   stepping, switch paint), verified with captures and the back-button sweep.
2. The B questions, one at a time, each with its picture first.
3. Each ruling built and captured against X4/X3 as the audit did.

## D. Checked and found clean (from the audit, so it is not re-read)

Hint bars and side hints are correctly absent on touch boards; the pad has
exactly one control per physical input; pad cells sit on the X3 grid (within
~0.6 mm); POWER half height hanging from the row bottom; rocker halves
previous/next; paper 1:2 spacing out of zen; card top at the safe area; integer
scale; the keyboard overlaps rather than shrinking the page; popups, list rows,
reader margins and the header back arrow match the X4.

## Rulings

- **2026-11-26, B4: "Ruler screen".** Settings > Edge Ruler (Picco only,
  `src/activities/settings/EdgeRulerActivity.h`): along each edge, block N
  starts N logical px in, N = 0..30 by 2. The owner reads, per edge, the
  smallest N whose block shows its full outer edge; the four numbers become
  the Picco's viewable insets. Do not re-ask.
- **2026-11-26, B1: "Arrow only, + Note's".** On touch boards the header back
  arrow is the design; no hint bar, rows keep their full height. Create Note,
  which has no header, gets the same back arrow in its top-left corner. Do not
  re-ask.
- **2026-11-26, B2: "Grow 20%".** Picco only: the board profile's uiScale 1.2
  applies to row heights, 40 -> 48 px (about 5.2 mm on the 3.97" panel).
  X4/X3 unchanged. Do not re-ask.
- **2026-11-26, B3: "Rocker up a row".** In the iOS Picco app the rocker moves
  into the empty upper-right pad slot (the thumb row), beside the slide switch
  and the third key; Power stays where it is. Do not re-ask.
- **B5 is moot**: with the rocker in the upper-right slot there is no empty slot
  for "the gap stays" to cover.
- **2026-11-26, Create Note top band: "Text beside the arrow".** On touch
  boards the note's first line starts right of the corner arrow, at the old
  top (y 5), and later lines use the full width. Do not re-ask.
- **2026-11-26, subtitled rows: "Grow them too".** On the Picco, rows with a
  subtitle (60 px and up) grow by the same 1.2 as B2. Do not re-ask.
- **2026-11-26, the iPad pad edge: "Line it up".** The owner ruled on my
  report that the iPad mini's Confirm cell ended about 3 px short of the page.
  That report was wrong; see Built, round 2. The ruling is recorded, but its
  premise does not hold.
- **2026-11-26, Clock UTC Offset:** the 8 px taller clock band on the Picco is
  B2 working as ruled. Do not re-ask.
- **2026-11-26, text fields: "as before, text field needs to be one line".**
  This extends the keys ruling ("do not wrap long secret inputs or outputs")
  to every single-line text field, on every board: one line, never wrapping,
  at a fixed height. A longer text scrolls sideways so the cursor stays
  visible, with an ellipsis at each end that is cut off. The multi-line
  editors (Create Note, the Claude chat input, anything opened with
  `TextEntryLines::Multi`) stay multi-line by design. Do not re-ask.
- **2026-11-26, the slide switch: "immediate right of power".** Owner: *"move
  picco switch to the immediate right of power and match vertical and
  horizontal layout optics without resizing or fucking it up"*. iOS pad: the
  switch keeps its size and sits one 8 pt step right of POWER, top and bottom
  level with POWER, its right edge on Confirm's right edge in the row above.
  The Back slot it used is empty again. Captured on iPhone Air and iPad mini.
  Do not re-ask.

## Built (2026-11-26)

On `picco-layout` in both worktrees of the `key-paste` pair, firmware on top of
`aedd0761a` and simulator on top of `1740b8e8`. Nothing is pushed or merged.
Captures are in
`/private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/picco-layout/`,
with `before/` taken from the unchanged tree first and `after/` taken from the
built one. The capture script is `shot2.sh` there. It is the audit's `shot.sh`
pointed at this worktree's binaries, plus `CROSSPOINT_SIM_GRAIN_SEED=1`.
Without the seed, two runs of the same binary differ in every pixel, because
the grain is re-rolled each launch. With it, eight X4/X3 before-runs matched
eight repeat runs byte for byte before anything was changed.

| Item | Commit | Change | Where |
|---|---|---|---|
| A1 | sim `dfc4a657` | `paintPiccoTouchDbg` returns at once unless `CrossPointPrefs_diagnosticsEnabled()` | `ios/CrossPointIOSShim.cpp:3460-3461` |
| A2 | fw `908775559` | Picco: no "Hold UP to edit entry", no cursor-mode tip; the clear-all tip is `STR_KB_HINT_CLEAR_TEXT_PICCO` "On DEL, hold SELECT to clear all text" | `KeyboardEntryActivity.cpp:1157-1162`, `:1171-1186`; `english.yaml:397`, `spanish.yaml:345` |
| A3 | fw `2c9060646` | The stepped row's title reads "Brightness (tap when done)"; a tap anywhere ends the step and saves, as Confirm and Back still do. The tap is claimed, so it no longer also steps the value as the rocker's Right | `SettingsActivity.cpp:306-314`, `:628-634`; `english.yaml:464`, `spanish.yaml:387` |
| A4 | sim `ec3a392d` | The knob is stroke + face (`p.face`, was `p.faceDown`); its side is still the state | `ios/CrossPointIOSShim.cpp:3263-3293` |
| B1 | fw `14e8ed7e9` | The arrow and its tap rect are one function, `drawBackArrow`, used by `drawHeader` and by the new `drawCornerBackButton` / `cornerBackButtonBottom` (Base: clear, no arrow). Create Note draws it at the header's rect. On touch boards the note's text starts below the tap rect (y 93, was 5) | `LyraTheme.cpp:116-137`; `BaseTheme.h:296-304`, `BaseTheme.cpp:384-389`; `NoteEditorActivity.cpp:24-27`, `:117-118`, `:794` |
| B1 sweep | fw `14e8ed7e9`, `fcde7a2bd` | The Create Note case is back. The About case moves 21 -> 24 presses: the key rows and Edge Ruler pushed it down, so the old count opened the GitHub Token entry and failed "screen never opened". That failure predates this work | `tools/picco_back_sweep.sh:35-37`, `:52-55` |
| B2 | fw `452e4c0f8`, sim `b50fe582` | `UITheme::getMetrics` scales `listRowHeight` (40 -> 48) and `menuRowHeight` (64 -> 77) by `BoardConfig::ACTIVE.uiScale`, Picco only. `getListRowStep` and `getButtonMenuRows` read the live metric, so `drawList`, `drawButtonMenu`, every list's hit rects and Home's grid move together. A grown single-line row keeps its title and icon centered (`growPad`). The simulator's `BoardProfile` gains `uiScale` with the SDK's values | `UITheme.cpp:16-18`, `:49-58`; `LyraTheme.cpp:226`, `:366-370`, `:710-713`; sim `src/BoardConfig.h:131-134`, `:160-170` |
| B3 | sim `2abd9094` | In the Picco block of `layoutPad`, Up/Down take the Left/Right rects. All three layouts place that pair directly above the rocker's, at the same x. Power, the chip and the halves' mapping are unchanged | `ios/CrossPointIOSShim.cpp:1007-1017` |

### Verification

- **Builds:** `pio run -e simulator_picco`, `-e simulator_x3`, `-e simulator`
  (X4), `-e picco` (RAM 22.1%, Flash 33.7%) and `-e default` all succeed. The
  `BleHidHost.cpp.o` object exists for both `default` and `picco`. The iOS
  build `cmake --build build/ios-sim-picco --config Release --target
  CrossPointX3` succeeds.
- **X4 and X3 are unchanged.** Home, Settings, Create Note and text entry
  (Device owner, "abc" typed) are pixel-identical before and after on both
  boards (`after/x3x4-pixel-diff.txt`).
- **Picco before against after** (diff threshold 40 levels): Home is
  identical. With no open book, Home's menu is squeezed to fit its rect, so
  its rows are 46 px pitch either way, so the sweep's Home-row y centers
  (404 + 46 i) still hold. Settings differs only in the box from y 117 to
  761, which is the rows. Create Note differs only in the top-left corner (x
  20-46, y 5-125), which is the arrow and the text's new top.
- **`tools/picco_back_sweep.sh`:** 30 cases: 19 arrow cases (Create Note
  included), 9 Home rows and 2 new Settings row taps. On the final tree, one
  full run (`after/sweep-final.txt`) passed 28 of 30. File Transfer and Claude
  each opened the row below their target, so the arrow tap had no screen to
  leave. Both are the button walk on Home landing one row too far, and both
  passed when rerun alone. That run shared the machine with another session's
  load (fseventsd at 100%, a Paix OCR job at 82%). The previous full run, on
  the same code before the include move below, passed both. Its only failure
  was About, fixed in `fcde7a2bd`. A second full run on the final tree passed
  30 of 30 (`after/sweep-final2.txt`). The Settings taps aim at row 1 (Typography)
  and row 9 (Clock UTC Offset). Their centers are 129 + 48 i, measured from
  the selection band in `after/picco-settings.png`: row 0 runs y 108-150 and
  row 10 runs 588-630. Under the old 40 px grid, row 9's y of 561 would land
  on Warmth, so the case tells the two grids apart.
- **`tools/key_store_check.sh`:** 15 of 15.
- **Firmware host tests:** 958 of 958 (`cmake -S test -B build/test && cmake
  --build build/test && ctest --test-dir build/test -j8`). The first build
  failed: the host test target has no `BoardConfig.h` on its include path.
  The include in `UITheme.cpp` is now behind `#if FREEINK_DEVICE_PICCO`.
  CssParserTest passed first time.
- **A2 on the desktop Picco:** after two Del taps, no tip names Up
  (`after/picco-kbentry-deltwice.png`). A tap on Del, then Select held for
  2.5 s, clears the field, so the new tip is true
  (`after/picco-kbentry-clearall.png`; the pair side by side:
  `after/kbentry-del-checks.png`).
- **A3 on the desktop Picco:** the row title while stepping is in
  `after/picco-brightness.png`. A tap ends the step without stepping the value
  (70 stays 70) and saves `"frontlightBrightness":70`
  (`after/picco-brightness-tapdone.png`).
- **iOS, iPhone Air simulator:** with Diagnostics off there are no counters
  (`after/ios-reader-diag-off.png`), and with it on they show
  (`after/ios-reader-diag-on.png`). The pad crop `after/ios-pad-crop.png`
  shows the hollow knob and the rocker up on the thumb row; Power alone stays
  below. On the iPad mini (`after/ios-ipadmini-pad-crop.png`) the rocker is
  on the right of the thumb row, at the full cell height of that slot (it was
  a half cell below). The simulator's Diagnostics pref was 1 before the run;
  it was set to 0 for the capture and restored to 1. The iPad was shut down
  again.
- **Picco | X4 | X3 comparisons:** `after/cmp-{home,settings,note,kbentry}.png`.

### Not done, or checked by code only

- **The keyboard chip and the turned landscape were not captured.** Three
  attempts to open a field from a scripted iOS launch did not reach one. The
  app resumed in the book, and the scripted presses landed on Select Chapter
  (`after/ios-chapter-list.png`). The chip's geometry is unchanged. On the
  phone it is centered in the bottom row, which now holds only Power at the
  left. On the tablet it sits at the bottom center. In the turned landscape
  it sits above Back/Select in the left margin. The rocker moved to the
  upper-right in every case, so it cannot meet the chip. The turned landscape
  needs a wide-table page. Its rects (`src/TurnedPageLandscape.h:139-161`)
  put Left directly above Up, which is all the move relies on.
- **Home with an open book was not captured.** The menu page's rows there
  grow from 64 to 77 px when they fit their rect. Drawing and the touch grid
  share `getButtonMenuRows` (`HomeActivity.cpp:377`).
- **One more reader of `metrics.listRowHeight` grows with it.**
  `ClockOffsetActivity.cpp:132` reserves a band for its clock line of one
  list row plus spacing, and only on a board with a clock. On the Picco that
  band is now 64 px instead of 56. Not captured.
- **Subtitled list rows (60 px) do not grow.** The ruling says "row
  heights, 40 -> 48"; the 60 px subtitled rows are already 6.5 mm.
- **B4 is waiting** on the owner's Edge Ruler reading.

- **Seen while checking, not changed (WRONG, corrected in round 2):** I
  reported here that on the iPad mini the Picco pad's Confirm cell ends about
  3 device px short of the paper. A full-resolution measurement shows the
  cell's stroke at x 263 and the paper starting at x 264, so they touch.

### Checked and found clean

- `DaisyEntryActivity` names no control beyond the hint bar
  (`DaisyEntryActivity.cpp:411-412`), which `drawButtonHints` drops on touch
  boards (`LyraTheme.cpp`, `gpio.hasTouch()`). Nothing to change.
- Every list row reaches row geometry through the theme (`getListRowStep`,
  `getListPageItems`, `getButtonMenuRows`). The callers are
  `MappedInputManager.cpp:255-258`, `UiListActivity.cpp:167-168`, Font and
  Editor Font selection, Wi-Fi and Home. The non-touch `metrics.listRowHeight`
  reads in `UiListActivity.cpp:144` and `UiTabListActivity.cpp:83` never run
  on the Picco.

## Built, round 2 (2026-11-26)

Same branches. Captures are in
`/private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/picco-layout/r2after/`
(`r2before/` for the before state) and, for the note, `after/`. The scripts
there are `shot2.sh`, which now takes an optional book, and `recent.sh`. The
seed book is `seed.epub`.

| Item | Commit | Change | Where |
|---|---|---|---|
| Text beside the arrow | fw `d90058915` | The note's text starts at topPadding again (y 5). The arrow is centered on the first text row: icon y 5-36, tap rect x 0-64. The first display line starts at x 64 and is wrapped to the width left there; later lines get the full width. A view scrolled past line 0 leaves the arrow's row empty. One per-line geometry (`lineX`, `lineWidth`, `rowOffsetAt`, `rowsShownAt`) feeds wrapping (`relayout`), drawing, the caret x, `ensureCursorVisible` and page up/down. There is no tap-to-place-cursor in the editor, and its caret moves by logical line (`TextBuffer::cursorUp/Down`). The theme call is now `drawCornerBackButton(renderer, rowTop, rowHeight)` plus `cornerBackButtonRight()`: same icon, size, x and tap rect as the header's, moved to the row's height | `NoteEditorActivity.cpp` (`lineX` ... `rowsShownAt`, `relayout`, `render`); `LyraTheme.cpp` `drawBackArrow`, `drawCornerBackButton`; `BaseTheme.h` |
| Grow them too | fw `9b3fc1045` | `getListRowStep` returns a subtitled row's base step x (live row / 40): 60 -> 72 on the Picco, unchanged elsewhere. `listWithSubtitleRowHeight` is scaled in `UITheme::getMetrics` too. `growPad` now centers subtitle lines and the multi-line selection band as well, and the band and badge caps follow the live row height | `LyraTheme.cpp` `baseListRowStep`, `getListRowStep`, `drawList`; `UITheme.cpp` `getMetrics` |
| Line it up | not built | See below | |
| Clock UTC Offset | none | Captured: `r2after/picco-clockoffset.png`. The "Current time" band is 48 + 16 = 64 px | |

**Verification**

- **Builds:** `simulator_picco`, `simulator_x3`, `simulator` and `picco` (RAM
  22.1%, Flash 33.7%) all build, and so does the iOS app.
- **Host tests:** 958 of 958.
- **X4 and X3 are pixel-identical** to before on Home, Settings, Create Note,
  text entry, Browse Files and Recent Books (`r2after/x3x4-pixel-diff.txt`).
  The before captures for Browse Files and Recent Books came from the round-1
  commit's binaries. A repeat run of X4 Recent Books matched byte for byte.
- **Notes:**
  - `after/note-beside-arrow.png` shows three states, left to right:
    - empty: the arrow, with the caret beside it at x 64;
    - wrapping: the first line starts at x 64 and the later lines at x 20;
    - scrolled: the arrow's row is empty and the caret stays visible on the
      last line.
  - Single captures of each: `after/picco-note-{empty,wrap,scrolled}.png`.
  - The back sweep's Create Note case taps the arrow where it now is,
    (36, 21), and passes.
- **Subtitled rows:** `r2after/cmp-recent.png` shows, left to right, Picco
  before (60 px band), Picco after (72 px, content centered), X4 and X3.
  Browse Files has no subtitle; its rows are the 48 px of B2
  (`r2after/picco-browse.png`).
- **Back sweep:** 30 of 30 in one run (`r2after/sweep.txt`).

**Line it up: not built, needs the owner.** Measured at full resolution:

- **iPad mini (A17 Pro):** the Picco pad is already flush with the page.
  Confirm's stroke is at x 263 and the paper starts at x 264. On the right,
  the paper ends at x 1223 and the rocker's stroke is at x 1224
  (`r2after/ios-ipadmini-edge-3x.png`, 3x nearest). The 3 px gap I reported
  was a misreading of a reduced capture.
- **iPad Pro 13 (M5):** the pairs are centered in the margin band
  (`layoutPad`'s tablet path, "Centred inside each inset band"). Confirm ends
  at x 411 and the paper starts at x 552, a 141 px (70 pt) gap. The right
  side mirrors it: paper ends at x 1512, the rocker starts at x 1652
  (`r2after/ios-ipadpro13-pad.png`). On the mini the gap is 0 only because the
  cell is limited by the band there.
- **The decision:** making every tablet flush would move the larger iPads'
  pairs about 70 pt inward, away from the thumbs that T-B placed them for
  (`thumb-reach-2026-10-06.md`). That trade was not part of the question the
  owner answered, so it waits for him. The X3 app is not configured as an iOS
  simulator build in this worktree (`build/` holds only the Picco's), so only
  the Picco app was measured. The turned landscape is unaffected, since
  nothing was changed.

## Built, round 3 (2026-11-26): one-line text fields

The captures are in
`/private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/picco-layout/r3/`,
and the scripts there are `field.sh` (Find in Book: a Text field, 128 bytes)
and `wifipw.sh` (the Wi-Fi password field). This item is a text-field rule
for all boards, so X4/X3 pixel identity does not apply to it.

- **The window,** `src/components/SingleLineField.h` (new, header-only),
  `singleline::fit`. It is pure and stateless. When the text fits, all of it
  shows. Otherwise the tail shows behind a leading ellipsis whenever the
  cursor is inside it. If the cursor is left of the tail, the window starts
  at the cursor, with an ellipsis at whichever ends are cut off. Every
  boundary is a UTF-8 boundary of the shown string. A password's mask is one
  '*' per byte, so the revealed character stays whole.
- **KeyboardEntryActivity:**
  - `render()` draws the one window (`fieldWindow`) at `inputStartY`, and
    `inputHeight` is always 0, so the field, the tips and the keyboard never
    move.
  - The caret and the password's three-part cursor-mode draw are offset by
    the window start.
  - `cursorPositionFromPoint()` maps a tap through the same window. A tap on
    a leading or trailing ellipsis goes to that end of the text: by touch,
    that is the only way to reach the hidden part.
  - The wrapping helper `lineBreakEnd` is gone. Short text draws exactly as
    before.
- **DaisyEntryActivity** already showed one line, with the tail. It now
  takes the same window and shows the leading ellipsis. Its cursor is always
  at the end.
- **Secret** is unchanged: 8 '*' plus the length, clipped from the front.

**Verification**

- **Builds:** `simulator_picco`, `simulator_x3`, `simulator` and `picco` all
  build.
- **Host tests:** 959 of 959, including the new `SingleLineFieldTest`
  (`test/single_line_field/`, 7 cases):
  - fits whole;
  - the tail with its offset;
  - head with a trailing ellipsis;
  - mid window;
  - the window always holds the cursor (every position);
  - UTF-8 boundaries, including a cursor inside a character;
  - a masked password whose revealed multibyte character stays whole.
- **A 120-character Text entry (Find in Book):**
  - Empty, typed and returned to the start, on Picco | X4 | X3:
    `picco-find-cmp.png`, `x4-find-cmp.png` and the strips
    `picco-find-fields.png`, `x4x3-find-fields.png`.
  - X4 and X3: Up held for cursor mode, then Left held for 125 s, brings the
    cursor to "T". The window follows, with a trailing ellipsis.
  - Picco (no cursor mode): a tap on the leading ellipsis puts the cursor at
    the start, and a later mid-field tap places it there, with both
    ellipses.
  - Short against long text on Picco and X4: the only pixels that differ are
    the text line (y 162-192). The field height, the tips and the keyboard
    are identical.
- **A 63-character Wi-Fi password** (the real field, Settings > Wi-Fi
  Networks > Local Test Network (fake)), Picco and X4, empty and typed:
  `wifipw-cmp.png`. It shows the tail behind an ellipsis.
- **Not reachable on screen:** no caller opens `InputType::Password` or
  `InputType::Url`. The Wi-Fi password field is `InputType::Text`
  (`WifiSelectionActivity.cpp:331-336`, upstream #3080: shown while typed).
  So the masked one-line path and the Url field are covered only by the host
  test, which models the mask, and by code reading.
- **`tools/key_store_check.sh`:** 15 of 15 (Secret still one line, 8 '*' plus the length). **The back sweep:** 30 of 30 (`r3/checks.txt`).
