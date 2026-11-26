# Picco layout audit against the X3/X4 written guidance (2026-11-26)

Owner, 2026-11-26: *"let's plan out how to improve the layout of picco to be
better, using md file guidance from x3 and x4. there is a lot written up and it
was not followed."*

This is the evidence a plan can be built from. It lists every written layout
rule found for the X3, the X4 and the iOS X3 app, says whether it applies to the
Picco, and says whether the Picco follows it. Nothing was changed. **No fix is
proposed here.** Where a rule's reach to the Picco is unclear, it is listed as a
question for the owner rather than decided.

Surveyed at firmware `picco-fix` 21eed8e86 and simulator `picco-fix` cd32c0f3
(both trees clean). "The device" means the Picco firmware (`FREEINK_DEVICE_PICCO`,
800x480 panel, portrait 480x800 logical, `hasTouch()` true). "The app" means the
iOS "CrossPoint Picco" harness (`-DCROSSPOINT_IOS_DEVICE=PICCO`). Most firmware
findings hold for both, since the app runs the same firmware core.

## Method

**Desktop captures.** I ran three builds headlessly, each from a fresh
directory on an empty card, or on a card holding one seed EPUB for the reader.
The script holds BACK through boot so the run lands on Home
(`docs/headless-qa.md` §4 in the simulator repo). It then sends one press every
900 ms and captures 2 s after the last one. The script is
`/private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/picco-layout/shot.sh`.
At its core it runs:

```
CROSSPOINT_SIM_INPUT_SCRIPT="200:QTAP:BACK:2500;3500:UP;4400:UP;…" \
CROSSPOINT_SIM_SCREENSHOTS="<t>:shot.bmp" SDL_VIDEODRIVER=dummy timeout 120 <program>
sips -s format png shot.bmp --out <board>-<screen>.png
```

- Picco: `.pio/build/simulator_picco/program` (picco-fix). Next = `UP` (the
  rocker's upper end) and Confirm = `CONFIRM` (the GPIO5 key).
- X3: `.pio/build/simulator_x3/program` (picco-fix). Next = `RIGHT`.
- **X4: picco-fix has no `.pio/build/simulator/program`.** The X4 runs used
  the only X4 simulator binary on the machine,
  `.claude/worktrees/ios-deploy/.pio/build/simulator/program`, built
  2026-11-13. So the X4 column is 13 days older than the other two. Its
  Settings rows and Home menu match the X3 build of today, and so do its
  reader page and popup. No layout claim below rests on the X4 capture alone.

Every run's activity was confirmed from its log (`Entering activity: …`) before
the capture was used. Every desktop capture is in dark mode, which is the
simulator's default.

Captures are in `/private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/picco-layout/`.
Each board has its own capture, named `picco-<screen>.png`, `x4-<screen>.png` or
`x3-<screen>.png`. Each comparison is `cmp-<screen>.png`, laid out left to right
as Picco | X4 | X3.

| screen | how it was reached |
|---|---|
| `home` | boot on an empty card |
| `settings` | Home row 8 |
| `popup` | Settings > Sleep Screen (an `OptionPopup`) |
| `claude` | Home row 5 (`ClaudeChat`, keyboard strip) |
| `note` | Home row 4 (`NoteEditor`) |
| `kbentry` | Settings > Device owner (`KeyboardEntry`, full screen) |
| `reader` | Browse Files > the seed EPUB, six pages in |

**iOS captures.** These were made earlier the same day by another session
(iPhone Air simulator, Release, with the switch commit in the build). They live
in `/private/tmp/claude-501/-Users-natebunnyfield-src/2f7ddd19-354b-4b40-85d2-4aff02275054/scratchpad/hb/`:
`ios-after-claude.png` (Home), `ios-after-clearcache.png` (Settings),
`ios-before-claude.png` (Claude) and `ios-paste.png` (a keyboard with the
system keyboard up). All are 1260x2736 at 3x. Reduced copies are in
`picco-layout/` (`ios-home-after-claude-s.png`, `ios-after-clearcache-s.png`)
and `hb/ios-before-claude-s.png`, `hb/ios-paste-s.png`. Positions were measured
in Python over the full-resolution PNGs, scanning luminance by column. The page
edge was located from the firmware's battery icon, which is at logical y 16 on
the desktop and at device y 412 in the app, so the page top is 380 px, ±2 px.

**Physical sizes.** The Picco panel is 3.97" at 800x480, which is 235 ppi, so
1 px = 0.108 mm. The X4 is ~220 ppi (`docs/hardware-dimensions.md:16`). In the
app the page is 960 device px wide for 480 logical px, so 1 logical px =
0.667 pt. The thumb-reach mm figures use that doc's own conversion (460 ppi,
1 pt = 0.166 mm).

**Inventory.** I read `docs/picco-port-2026-11-05.md`, `docs/ui-conventions.md`,
`docs/hardware-dimensions.md`, the simulator's `docs/thumb-reach-2026-10-06.md`
and `docs/picco-touch-on-ios-2026-11-25.md`, and the layout parts of
`ios/README.md` in full. Two read-only agents swept the rest:

- firmware: ux-navigation-audit, render-scale, dark-mode, daisywheel,
  two-bit-chrome, page-space, the three picco-* docs, hold-gestures,
  focus-reading, settings-reduction-plan, manage-files, notes-and-claude and
  lag-reduction-plan, plus a grep of all docs, TODO, BUGS and the theme and
  Home comments;
- simulator: zen-mode, zen-page-margins, ios-dynamic-island, ios-render-scale,
  pad-outline, surface-roadmap, ux-navigation-audit, ipad-layout,
  turned-page-landscape, power-off-collapse, CLAUDE.md, and a ruling grep of
  ios/README.md.

I re-read every rule the table relies on at its cited line.

## Summary

- **38 rules checked.**
  - **2 NOT FOLLOWED.** The touch counters are always on screen in the app.
    The keyboard tips tell a Picco user to press UP or DOWN, which the Picco
    cannot send.
  - **6 PARTLY.**
  - **5 whose reach to the Picco is genuinely ambiguous**, listed as owner
    questions.
  - **3 UNVERIFIED** (not measurable here).
  - The rest FOLLOWED or N/A.
- **Three of the four suspicions in the brief are refuted or mostly refuted:**
  - **"Hint bars for buttons it doesn't have": refuted.** No hint bar or side
    hint is drawn on the Picco. What remains is the keyboard tip text.
  - **"Pad controls placed ad hoc": mostly refuted.** Every Picco control sits
    in a slot computed by the X3 pad's own code. The pad's two rows land
    within ~0.6 mm of the X3 app's measured rows. The real issue is that the
    Picco's most-used control sits one row below a slot that is left empty.
  - **"uiScale 1.2 versus touch targets": the premise does not hold.** No
    firmware code reads `uiScale`, so the Picco's metrics are exactly the X4's.
- **Confirmed:** the debug counters.

## The rules

Status key:

- **FOLLOWED**
- **PARTLY**: the letter is kept but a piece is missing, or it is kept on one
  surface but not another.
- **NOT FOLLOWED**: the rule applies and the code breaks it, with both cited.
- **AMBIG**: whether the rule reaches the Picco is an owner question.
- **UNVERIFIED**: cannot be measured here.
- **N/A**

"Device" = Picco firmware on the device; "app" = the iOS Picco app; "both" = the
firmware core, which both run.

### Firmware chrome (both)

| # | Rule | Source | Applies to | Status | Evidence |
|---|---|---|---|---|---|
| F1 | Every screen that takes input draws a hint bar. The Back label names the destination. | `ui-conventions.md:146-149` (audit 2026-08-08) | both | AMBIG | Suppressed on every touch board: `LyraTheme.cpp:466-470` and `BaseTheme.cpp:134-138` return on `gpio.hasTouch()`, and `UITheme.cpp:39-45` zeroes `buttonHintsHeight`. The header arrow replaces only the Back hint. Nothing labels Confirm (GPIO5) or the rocker. No Picco ruling either way; the gate is upstream's. Compare `picco-settings.png` with `x4-settings.png`. |
| F2 | Don't label controls the device does not have. | `BaseTheme.cpp:169-172` (owner 2026-08-09, the iOS side hints) | both | **NOT FOLLOWED** (the keyboard tips) | Hint bars and side hints obey it (F1). The keyboard tips do not. `STR_KB_HINT_EDIT_ENTRY` "Hold UP to edit entry" (`english.yaml:392`) shows after Del is pressed twice (`KeyboardEntryActivity.cpp:410-415`, drawn at `:1129-1130`). `STR_KB_HINT_RETURN_KEYBOARD` "Press DOWN to return to keyboard" (`:394`, drawn at `:1157`). On the Picco the rocker never sends Up or Down: `PiccoInput.h:58-66` routes them to `Route::None`, and `picco-port-2026-11-05.md:362-365` records "keyboard row moves … become unreachable". The cursor mode that "Hold UP" opens (`KeyboardEntryActivity.cpp:757-763`) is therefore unreachable on the Picco. A tap in the field places the cursor but leaves cursor mode off (`:660-670`), and no tip says so. |
| F3 | Stepping a Brightness/Warmth row shows `< 60 >` and hints `Done / Down / Up`. | `picco-input-settings-2026-11-06.md:29-37` (a design decision, not a ruling) | both | PARTLY | `SettingsActivity.cpp:611-616` asks for the hints, and `LyraTheme.cpp:468-470` drops them on a touch board. Only `< 60 >` shows, so nothing says Confirm ends the step. |
| F4 | On a touch board a titled header leads with a 32 px back arrow. Root screens have none. | `picco-port-2026-11-05.md:617-643` (owner 2026-11-25, "Port upstream's button"); `:654-715` | both | FOLLOWED | `LyraTheme.cpp:117-133`. `tools/picco_back_sweep.sh` passes 27 of 27 (`picco-port:709`). Visible in `picco-settings.png`, `picco-claude.png` and `picco-kbentry.png`. The tap rect is 64x56 px, about 6.9 x 6.1 mm (`LyraTheme.cpp:6-8`, `:127-130`). |
| F5 | A way out on every screen, shown on screen (F1's hint bar on X4/X3). | `ui-conventions.md:146-149`; `picco-port-2026-11-05.md:311-315` (Left-edge swipe = Back, "Do not re-ask") | both | AMBIG | Create Note has no header and no hint bar, so it shows no on-screen Back. Its Back is the left-edge swipe (`picco-port:665-666`). See `picco-note.png` against `x4-note.png`. The swipe itself is ruled; whether an on-screen cue is wanted is not. |
| F6 | Reader: 1 unit above the text and 2 below; the unit is Screen Margin. | `page-space-2026-11-08.md:3-18` (owner 2026-11-08) | both | FOLLOWED | `cmp-reader.png`: the Picco page matches the X4's pixel for pixel in layout. |
| F7 | Content stays out of the bezel-covered rows (viewable insets 9/3/3/3), per board, "override per profile as boards are measured". | `page-space-2026-11-08.md:11-18`; SDK `BoardConfig.h:681-690` | device | UNVERIFIED | PICCO profile `BoardConfig.h:1489`: `{}` ("none measured"), so the X4 bezel values apply. Nobody has measured whether the Picco's bezel covers more. |
| F8 | Menu rows fit their rect (squeeze, never fall off), and the touch grid uses the same rows. | `BUGS.md:4339` (B-012); `picco-port:701-707` | both | FOLLOWED | `LyraTheme.cpp:674-696`; `HomeActivity.cpp:371-377`, `:415-437`. In `picco-home.png` all 9 rows are on the panel. With no book the pitch is 45 px and the rows 37 px (4.0 mm). Because the Picco has no hint band, its Home menu runs to the panel's bottom (last row ends ~785) where the X4's stops above its hints. |
| F9 | Touch boards get finger-sized chrome: SDK `uiScale` 1.2 ("bump chrome toward finger size"). | SDK `BoardConfig.h:1484` (PICCO); not a fork ruling | both | AMBIG | No firmware or simulator code reads `uiScale`. `grep -rn uiScale` hits only comments and the SDK table. `UIScale.h:4-7`: "One fixed tier for every board". `picco-port:532-534` records that the FUI's touch row height is not used because Lyra Six paints the rows. So the Picco's list rows are the X4's 40 px (`LyraTheme.h:18`): 4.3 mm on the Picco, 4.6 mm on the X4, and about 27 pt in the app (`ios-after-clearcache-s.png`). The project's only written touch floor is the 44 pt HIG floor, stated for the iOS pad (`ios/README.md:1146`, `:1235-1237`). |
| F10 | `OptionPopup` is the one interactive modal; same geometry everywhere. | `ui-conventions.md:60-68` | both | FOLLOWED | `cmp-popup.png`: Picco, X4 and X3 draw the same popup. Option rows have a 57 px pitch. The popup takes touch on the Picco since 2f50f7cbd. |
| F11 | List screens derive `UiListActivity`; the theme paints rows; never `fui::list`. | `ui-conventions.md:134-144` | both | FOLLOWED | `picco-port:503-534`; the Settings rows in `cmp-settings.png` are Lyra rows. |
| F12 | Editor and Claude bands, top to bottom: text, status, keyboard, hints. | `notes-and-claude.md:139-144` | both | FOLLOWED (as adapted) | With no hint band the keyboard drops ~40 px and sits ~10 px off the panel bottom (`picco-claude.png`, `picco-note.png`). `picco-port:278-280` kept `hasTouch()` deliberately for this. |
| F13 | Touch does not change key size, layout or highlight. | `daisywheel.md:71-82` (owner 2026-11-05) | both | FOLLOWED | Keys are 48 px tall (`LyraTheme.h:52`) on Picco and X4 alike (`cmp-kbentry.png`). Picco strip keys are about 35x48 px, 3.7 x 5.2 mm. The owner reported touch typing works (`picco-port:476-480`). |
| F14 | Touch hold on Del deletes one character; clear-all stays on a held Select. | `picco-port:419-420` (owner 2026-11-06, "Keep the guard") | both | PARTLY | Behavior follows the ruling. The tip `STR_KB_HINT_CLEAR_TEXT` "Hold DEL to clear all text" (`english.yaml:396`) still shows on the Picco (`hb/ios-paste-s.png`), where a touch hold on Del does not clear. It is true only for a rocker walk to Del plus a held GPIO5 key. |
| F15 | A tap on a list or menu row selects it on the Picco; elsewhere a tap is Right. | `picco-port:590-615` (owner 2026-11-25) | both | FOLLOWED | The sweep's 9 Home-row touch cases pass (`picco-port:709`). |
| F16 | Home touch hit-test answers only the page shown (touch boards). | `ux-navigation-audit-2026-09-02.md:18`, `:191-204` (lists F1/F2 as open) | both | FOLLOWED | Fixed 2026-11-25 (`picco-port:607-611`, `HomeActivity.cpp:336-344`). The audit doc still says "open", which is stale. |
| F17 | Side buttons page lists; the front pair steps. | `ui-conventions.md:188-335` (2026-08-14/15) | device | N/A | The Picco has no page pair. List paging was given up by ruling (`picco-port:370-380`). |
| F18 | Battery percentage hidden; chrome fonts 2-bit; iconography sweep closed. | `settings-reduction-plan.md:13-14`; `TODO.md:1335`; `ui-conventions.md:337-360` | both | FOLLOWED | Same theme code as the X4; no Picco branch in these paths. |

### iOS app pad and surround (app)

| # | Rule | Source | Applies to | Status | Evidence |
|---|---|---|---|---|---|
| I1 | One pad control per physical button, "no more, no fewer". | `ios/README.md:347` (layout approved 2026-08-02) | app | FOLLOWED | Since cd32c0f3 the pad has Power, the rocker (two halves), Confirm (GPIO5) and the slide switch: the Picco's five inputs (`picco-touch-on-ios-2026-11-25.md:398-404`). Back, Left and Right are zero rects (`CrossPointIOSShim.cpp:1007-1011`). |
| I2 | The keyboard chip is the one non-button control: centered in the bottom row, only while a field is open. | `ios/README.md:1704`; simulator `CLAUDE.md:592` | app | FOLLOWED | `hb/ios-before-claude-s.png`: the chip sits between Power and the rocker. |
| I3 | Controls are unlabeled: no glyph, no text. | `ios/README.md:479-480` | app | FOLLOWED | The switch's knob is state, not a label (`CrossPointIOSShim.cpp:3253-3256`). |
| I4 | Controls are hollow: an outline at rest, a wash only while pressed. iPad: "transparent is best". | `ios/README.md:381-387` (owner 2026-08-03); `ipad-layout-2026-08-29.md:1133-1134` (2026-09-30) | app | PARTLY | The switch knob is filled with the pressed tone at rest (`CrossPointIOSShim.cpp:3274-3279`, `p.faceDown`), so in light mode it is the one solid shape on the pad (`ios-home-after-claude-s.png`). |
| I5 | Strict square grid: cell near `kOptimalSquare` 60 pt, heights on the 8 pt grid. | `ios/README.md:1229-1236` (2026-08-02); `CrossPointIOSShim.cpp:1113-1140` (2026-08-02, 2026-08-11) | app | FOLLOWED | Confirm, Power and the rocker are X3 cells. The switch is Back's cell, 8 pt narrower, at POWER's height and centered (`:994-1006`), so it stays on the grid. |
| I6 | Fused pairs paint as one capsule with a center tick. | `ios/README.md:376-379` | app | FOLLOWED | The rocker capsule shows its tick. The lone Confirm draws alone after the pair-loop fix (`picco-touch:421-425`). |
| I7 | POWER stays half height and hangs from its row's bottom edge (owner: "changing power button was a fuckup"). | `CrossPointIOSShim.cpp:1238-1251` | app | FOLLOWED | Measured: POWER 2507-2602, rocker 2411-2602 device px, a shared bottom edge. |
| I8 | Rocker halves: left = previous, right = next. | `picco-touch:261-271` (owner 2026-11-25, "picco buttons seem backwards") | app | FOLLOWED | `CrossPointIOSShim.cpp:1012-1019`. |
| I9 | Placement mirrors the chassis where it is measurable. The gap to the front row is the X3's 11.6/78.2 of panel height. | `ios/README.md:1219-1227`; `docs/hardware-dimensions.md:41-49`, `:66-70` | app | AMBIG | The Picco app uses the X3's gap ratio and the X3's slots (`CrossPointIOSShim.cpp:1074`, `:1232-1258`). Nothing records where the Picco's Power, rocker, GPIO5 key or switch physically sit (`CrossPointIOSShim.cpp:986-993`; `picco-touch:406-407`). The rule cannot be applied until someone measures them. |
| I10 | Thumb reach: put the most-used control in the thumbs' band. The 2026-10-08 S1 trade lowered the X3 rocker to 10.5-21 mm above the device bottom, knowingly. | `thumb-reach-2026-10-06.md:87-99`; `zen-mode.md:781-788` (owner 2026-10-08) | app (phone) | PARTLY | Rows measured on the Air capture: front row 723.7-787.3 pt (20.7-31.2 mm above the screen bottom); rocker 803.7-867.3 pt (7.4-17.9 mm). These match the X3 rows after S1 to within ~0.6 mm, derived from `zen-mode.md:785` plus one 80 pt row step. But next and previous on the Picco live **only** on the rocker, the lower row (`MappedInputManager.cpp:65-79`), while the front-row slot where the X3 app has Left/Right is empty (`CrossPointIOSShim.cpp:1009-1011`). On the X3 app, next and previous are also on the front row. |
| I11 | iPad: the front pairs sit on the ruled thumb row, 448 pt up; POWER and the rocker hang 16 pt under them (T-B). | owner 2026-08-09 (`CrossPointIOSShim.cpp:606`, `:805`); `thumb-reach-2026-10-06.md:128-133` | app (iPad) | PARTLY (code only, no capture) | On the Picco iPad the thumb-row cells hold the switch and Confirm on the left, and nothing on the right. The rocker, the most-used control, is a cell plus 16 pt below the thumb row (`:824`, `:854-855`). |
| I12 | Out of zen on the phone, the paper extends 1 unit (24 pt) above the page and 2 below, and the black bands are 1:2. | `zen-mode.md:781-788` (owner 2026-10-08, "W2 but the paper needs one top and two bottom spacing", "S1 …") | app | FOLLOWED | Measured: paper 307-2122 px, page 380-1980 px (73 px = 24 pt above, 142 px ≈ 48 pt below), bands 307:613 = 1:2.0. |
| I13 | Out of zen the paper is the page alone, with four rounded corners. | `zen-mode.md:747-756` (owner 2026-10-07) | app | FOLLOWED | Captures: the paper is 960 px wide, the page's width. |
| I14 | Black band above the card; the card top sits at the safe area, clear of the Island. | `ios-dynamic-island.md:18`, `:248`, `:368-411` (2026-08-17/18) | app | FOLLOWED | Log `ios-after-claude.log`: "safe top 68.0 pt -> card top 68.0 pt". |
| I15 | The page presents at an integer scale. | `ios/README.md:1238-1240` | app | FOLLOWED | A 960x1600 framebuffer presented at 960x1600 device px. |
| I16 | The keyboard overlaps; it does not push or shrink the page. | `ios/README.md:24-31` (owner 2026-08-19) | app | FOLLOWED | `hb/ios-paste-s.png`. |
| I17 | **Diagnostics are OFF by default**, gated by Settings > Diagnostics Log. A diagnostic pattern "is a build flag, not on-screen UI". | `ios/README.md:1516-1522` (ruling 2026-08-09); `:144-147`; simulator `CLAUDE.md:552`, `:659` | app | **NOT FOLLOWED** | `paintPiccoTouchDbg` draws three lines of counters on every present (`CrossPointIOSShim.cpp:3440-3471`), run unconditionally from a scope guard in `paintPad` (`:3481-3489`). It is not gated on the Diagnostics switch; `diag a/b` is printed, not checked. Added for TestFlight build 3 as instrumentation-first (`picco-touch:333-351`); its question was answered the same day (`:353-360`). Visible at the top of every app capture (`ios-home-after-claude-s.png`). |
| I18 | Nothing on the page moves: no fades, auto-hide or reveal-on-touch. | `ios/README.md:1980-1986` (owner 2026-08-04) | app | PARTLY | The counters (I17) repaint with new numbers after every touch, in the black band beside the page. Nothing else in the Picco branch animates. |
| I19 | Zen layout rulings (C32 paper, zen corners, zen zones). | `zen-mode.md` passim | app | N/A | The Picco app forces zen off (`picco-touch:146-147`, under the owner's "Full Picco as a second app" and "touch is for picco only"). |
| I20 | Turned-page landscape: the G3 split pad and the zones follow the page. | `turned-page-landscape-plan-2026-10-04.md:41-71` | app | UNVERIFIED | Never exercised in the Picco app (`picco-touch:187-188`, `:232`). The switch lands level with the page in the left margin. The 2026-11-26 review fixed its finger, not its place (`picco-touch:444-447`). |

Rules F1-F18 and I1-I20 make 38.

## NOT FOLLOWED and PARTLY, ranked by how visible or harmful

1. **I17: the touch counters are always on screen in the app** (NOT FOLLOWED).
   They are on every screen, in every session, in the band above the page:
   `CrossPointIOSShim.cpp:3440-3489` against `ios/README.md:1516-1522`. They
   are instrumentation whose question was answered on 2026-11-25
   (`picco-touch:353-360`). They also break I18, because they repaint on every
   touch.
2. **F2: the keyboard tips name UP and DOWN, which the Picco cannot send** (NOT
   FOLLOWED). This affects both device and app. "Hold UP to edit entry" appears
   after a second Del, so a typist sees it. The mode it names cannot be reached
   on the Picco. Rule `BaseTheme.cpp:169-172`; code
   `KeyboardEntryActivity.cpp:410-415`, `:1129-1130`, `:1157`; mapping
   `PiccoInput.h:58-66`. F14 is the same kind of thing: "Hold DEL to clear all
   text" is untrue for a touch hold.
3. **I10 and I11: next and previous sit one row below an empty thumb-row slot**
   (PARTLY, phone and iPad). On the phone, the front-right cell is empty and the
   rocker, the Picco's only next/previous, is in the lower row. On the iPad, the
   thumb row holds the switch, a set-and-forget control, while the rocker hangs
   below. The rows themselves match the X3's, so this is about which control
   takes which slot, not about the rows.
4. **F1 and F5: no on-screen cue for the Picco's own keys, and no visible Back
   on Create Note** (AMBIG). Confirm (GPIO5) and the rocker are never labeled,
   and Create Note shows no way out. Every X4/X3 input screen carries a hint
   bar.
5. **F3: the Brightness/Warmth stepping mode has no "Done" cue** (PARTLY). The
   hints it asks for are dropped on a touch board.
6. **I4: the switch knob is filled at rest** (PARTLY). It is the one solid
   shape on an otherwise hollow pad.
7. **F9: Picco rows are X4-sized (40 px, 4.3 mm)**, despite the SDK's 1.2
   "finger size" note (AMBIG). That note is not a fork ruling.
8. **I9 and F7: the chassis gap ratio and the bezel insets are the X3's and
   X4's** (AMBIG / UNVERIFIED). There are no Picco measurements to apply.

## Owner questions (applicability genuinely ambiguous)

1. Should the Picco show on-screen labels for its own keys (Confirm, the rocker
   ends), and should Create Note show a Back cue? Or is "header arrow only, no
   hint bar" the intended touch-board design? (F1, F5)
2. Should the Picco's rows and other chrome be finger-sized (the SDK's
   `uiScale` 1.2; 44 pt on the phone), or stay at the X4's 40 px? (F9)
3. Should the app's pad keep the X3 slots, or follow the Picco's own body?
   Following the body needs a measurement of where Power, the rocker, the
   GPIO5 key and the switch sit on the device. Within either choice, should
   the rocker take the empty thumb-row slot? (I9, I10, I11)
4. Should the Picco's bezel-covered rows (`viewableInsets`) be measured on the
   unit? (F7)
5. On the X3 app, when the side rocker was briefly retired, the owner accepted
   an empty half-row ("POWER stays where it is, and the gap stays",
   `ios/README.md:14-21`, 2026-08-19). Does that acceptance extend to the
   Picco's empty front-right slot? The ruling may be moot: the rocker was
   restored the same day (`CrossPointIOSShim.cpp:849-853`).

## Checked and found clean

- No button-hint bar or side hint is drawn on the Picco, on any screen
  captured. The suspicion "hint bars for buttons it doesn't have" is
  **refuted** (`LyraTheme.cpp:466-470`, `:510-520`; `BaseTheme.cpp:134-138`,
  `:165-168`; `picco-*.png`).
- The pad is not ad hoc. Every Picco control sits in a slot computed by
  `layoutPadImpl`, the X3 code. The Picco block (`CrossPointIOSShim.cpp:983-1020`)
  only zeroes three slots, swaps the rocker halves and fits the switch into
  Back's slot.
- The app's rows match the X3 app's (I10), and the paper spacing (I12), card
  top (I14), integer scale (I15) and keyboard overlap (I16) all hold.
- The header arrow works on every titled screen (27/27 sweep). Home's touch
  grid matches its drawn rows.
- The reader page, the popup, the Settings rows and the keyboards have the
  same geometry as the X4 (`cmp-reader.png`, `cmp-popup.png`,
  `cmp-settings.png`, `cmp-kbentry.png`). The Picco only gains the hint band's
  40 px.
- **A finding retracted while checking:** a first pass read the app's paper top
  as 199 px. That made the bands look 1:3 and the paper's top extension 60 pt,
  so S1 looked broken. 199 px was the island band at one column. Re-measured
  at three columns, the paper starts at 307 px and S1 holds (I12).

## Limits

- **The X4 captures come from a 13-day-old binary** (ios-deploy worktree,
  2026-11-13), because picco-fix has no X4 simulator build and building was out
  of scope.
- **No new iOS captures.** The app evidence is the four captures another
  session made today on an iPhone Air, plus code reading. No iPad, iPhone SE,
  13 mini or landscape capture of the Picco app exists, so I11 and I20 are
  code-only.
- **No X3 app capture after S1.** I10's "rows match" uses
  `zen-mode.md:785`'s numbers, not a side-by-side capture.
- **Not checked:** dark-mode chrome on the device; the frontlight and switch
  rows' look in light mode; the daisy wheel and QWERTY layouts on the Picco
  (only the 13-Grid strip and full-screen grid were captured); the file browser
  and chapter lists; the sleep and boot screens; Wi-Fi selection; pad contrast
  presets in the Picco app's Settings page.
- **Physical sizes are computed** from published panel sizes (3.97" and 4.26"),
  not calipered.
- **Device feel is not covered.** Every finding here is about pixels and code,
  never how anything feels in the hand.
