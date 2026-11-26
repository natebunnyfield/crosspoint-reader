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

