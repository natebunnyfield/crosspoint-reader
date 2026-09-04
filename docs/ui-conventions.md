# UI conventions

Established 2026-08-08 by the navigation/widget/settings consistency audit.
These are rules, not observations: new screens follow them, and drift from
them is a bug. File:line references are to the state at adoption.

## Rotation is clockwise. Always.

**Owner ruling 2026-08-19: never offer a counter-clockwise rotation again.** He
is right-handed, so a page the reader turns clockwise puts the device's side
rockers where his hand already is; the mirrored option is not a trade-off worth
rendering, discussing or re-proposing.

The trap that produced one anyway, recorded because the name invites it:
`GfxRenderer::drawTextRotated90CW` draws **counter-clockwise content**. Its name
describes the turn the READER makes, not the transform applied to the glyphs —
text drawn with it climbs bottom-to-top and reads once the device is turned
clockwise. There is no clockwise-content call. `tools/table_preview` composes
one by drawing with that call and turning the finished framebuffer 180 degrees
(CCW + 180 = CW), which flips the page as a unit so row order survives.


## Choice surfaces — what shape of UI a decision gets

| Decision shape | Surface | Example |
|---|---|---|
| Boolean preference | `TOGGLE` row, value cycles in place | Clock synced |
| Enumerated pick, 2+ options | `OptionPopup` (in place, both/all options visible) | Sleep screen, clock format, language |
| Scalar ramp, ≤ ~12 steps | ENUM-via-getter/setter row → popup | Screen margin |
| Scalar ramp, larger or continuous | Slider activity (with `drawHeader`) | Time to sleep (1–31) |
| Confirmation (cancel/do-it) | In-place `OptionPopup` over the current screen | Delete book, flash firmware, clear cache |
| Alert (single OK) | `OptionPopup` with one option | File Manager message popup |

A **dedicated screen** is justified only by content that a popup cannot carry:
live preview (Reader Font), a file listing (browser, firmware picker), or a
multi-step flow (WiFi join). "It's a choice" is never by itself a reason for a
screen — the audit deleted four activities whose entire content was one choice
(NetworkModeSelection, Confirmation, LanguageSelect, and the end-of-book menu).

Settings mechanics: every ENUM row with more than one choice opens the popup
(`SettingsActivity.cpp`, both ENUM branches gate on `> 1`). Two-option enums
never silently cycle; the popup is what shows the user the alternative.

## Popup idioms

- `GUI.drawPopup(...)` — fire-and-forget banner (progress, "indexing"). No input.
- `OptionPopup` (`src/components/OptionPopup.h`) — the only interactive modal.
  Owns its input and its own hint bar. Pattern: member + `show()` +
  `handleInput`/`processRender` gates at the top of `loop()`/`render()`; copy
  `ClearCacheActivity.cpp`.
- Never push an activity whose only content is a popup. `ConfirmationActivity`
  existed for exactly that and was deleted.

## Navigation contracts

- `goTo*` wrappers **replace** and clear the stack; `startActivityForResult`
  **pushes**; `finish()` pops, and an empty-stack pop lands on Home. Where
  Back-to-Home highlights (`lastHomeMenuItem`) is the launcher's job.
- Activities exit on Back **press**, not release.
- **Confirm fires on PRESS on some lists and on RELEASE on others, and that is
  a convention, not a bug** (audit 2026-09-02, F13 — an inventory, ruled
  "write it down"). Press-edge screens: Settings, Wi-Fi selection, Typography,
  Colophon, the editor font list. Release-edge screens: the file browser, Manage
  Files, Recent Books, the chapter list. The rule that decides which: **a screen
  whose Confirm ALSO has a hold meaning (delete, action menu, recent-book
  action, font de/reactivate) must fire its tap on RELEASE**, because a press
  edge cannot know yet whether a hold is coming — `FontSelectionActivity`'s
  migration comment is the worked example. A screen with no Confirm hold may
  fire on press, and should keep doing so (a release-fired tap feels a frame
  late on e-ink). Do not "fix" the inventory screen by screen; a screen moves
  from press to release only when it grows a hold.
- `ActivityManager` arms `MappedInputManager::swallowUntilIdle()` on every
  activity swap: the incoming activity never sees the press/release edge that
  drove the transition. **Do not add per-activity release latches for
  child-exit leakage** — that central swallow is the mechanism. Latches remain
  legitimate only for intra-activity cases (popup closes, chord gestures) where
  no swap occurs.
- Readers route Back through `ReaderUtils::handleBackNavigation`, which since
  the 2026-09-02 ruling (`docs/hold-gestures.md`) is ONE short press →
  `popActivity()`: the held-Back-to-file-browser split is dead and
  `backShortToFileBrowser` is a tombstone read by nothing. All of
  Epub/Xtc/Txt/Bmp still go through it, so the signature stayed.
- End of book: minimal centered end screen; Confirm/Back/forward go Home,
  paging back returns into the book (and disarms the read-folder move). There
  is deliberately no suggestions menu (owner ruling, 2026-08-08).

## Widgets

Render through the theme: `drawHeader`, `drawList` (+ theme paging helpers),
`drawButtonHints`, `drawProgressBar`, `drawTextField`, `drawCenteredText`.
Hand-rolling one of these in an activity is a defect — the audit converted the
XTC chapter list, the footnotes list, and two hand-rolled prompt screens.

Button-hint Back label names the **destination**: `STR_HOME` when it exits to
Home, `STR_BACK` when it returns to a parent screen or up a directory,
`STR_CANCEL` when it abandons a pending operation. Every screen that takes
input draws a hint bar (terminal sleep/boot screens excepted).

## Deleted in the audit (do not resurrect without a ruling)

`EndOfBookOptions`, `NetworkModeSelectionActivity`, `ConfirmationActivity`,
`LanguageSelectActivity`, `ButtonRemapActivity`, `OtaUpdateActivity`,
`FontDownloadActivity`, theme `drawTabBar`/`tabIndexFromPoint`. Note
`OtaUpdater` (src/network) **stays**: the simulator's `simulator_ota.cpp`
consumes it even though no firmware code does.

## TODO — deferred, explicitly wanted (ruling 2026-08-13)

- **Bottom-align the font colophon.** The picker reserves four subtitle lines
  (`FontSelectionActivity.cpp`, `kColophonLines`) because a mixed-source family
  like InknutJunicode needs one line per lineage stage. Families with fewer
  stages currently draw from the TOP of that block, so a one-line colophon
  leaves three empty lines under it and floats away from its own title. Draw
  from the bottom instead: a one-liner sits on the last line, a two-line
  colophon on the bottom two, and a four-line one fills the block exactly as
  now.

  Where: `LyraTheme::drawList`, the `subtitleLines > 1` branch. It currently
  starts at `itemY + 30` and steps down by `LyraMetrics::values
  .listSubtitleLineStep` per line. Bottom-aligning means offsetting the start
  by `(subtitleLines - actualLines) * listSubtitleLineStep` — which needs the
  real line count BEFORE the first draw, and today it is only known as the
  segments are wrapped and emitted. So it wants a counting pre-pass (or
  building the line vector first, then drawing it), not a one-line tweak.

  Watch two things. The row height is uniform across the list and derives from
  `getListRowStep(hasSubtitle, subtitleLines)`, so nothing about spacing or
  paging changes — only where the text sits inside the box it already owns.
  And the same widget serves the editor-font picker
  (`EditorFontSelectionActivity.cpp`, its own `kColophonLines = 2`), which
  prepends an availability marker to line 1 precisely because the tail can be
  truncated; bottom-aligning moves which line is line 1 on screen, so re-read
  that comment before changing it.


- **Side buttons should page, not repeat the front buttons.** Owner ask,
  2026-08-14: "make side buttons be page up and page down, when they are
  identical functionality to front buttons, like on the home screen."

  They are identical today, and it is one place, not many:
  `MappedInputManager.cpp:89-96` defines the logical pair as

      NavNext     = side Down  OR front Right
      NavPrevious = side Up    OR front Left

  and `ButtonNavigator::getNextButtons/getPreviousButtons`
  (`ButtonNavigator.h:49-50`) returns exactly those two. Every list screen goes
  through `ButtonNavigator::onNext/onPrevious`, so the four buttons collapse to
  two behaviours everywhere at once — the home screen is just where it is most
  obvious, since a press of side-Down and a press of front-Right move the
  selector by the same single row.

  The change wants a THIRD logical pair (`Button::PageNext` / `PagePrevious`)
  bound to the side buttons, with `NavNext`/`NavPrevious` narrowed to the front
  pair — not a special case inside HomeActivity. Doing it in the mapper keeps
  every list consistent and keeps activities using logical buttons, per the HAL
  rule above. The reader is already separate and must not be touched: it uses
  `Button::PageBack`/`PageForward`, which have their own swap setting
  (`SETTINGS.sideButtonLayout`).

  Open questions, worth settling before writing code:

  1. **What is a page in a list?** Most lists scroll rather than paginate, so
     "page" has to mean the visible row count for that screen. `LyraTheme`
     already knows its rows-per-screen from `getListRowStep(...)`, so the number
     exists — but it differs per theme metric and per whether rows carry
     subtitles, and no shared accessor exposes it yet. Settled by implication
     from question 2's ruling below: a page is one screenful of rows. Flagged
     as an inference rather than a spoken ruling, so say otherwise if the intent
     was "jump to the next section" (books ↔ menu on Home).
  2. ~~**Which screens?**~~ **RULED 2026-08-14: dead.** A list that fits on one
     screen has no page to turn, and the side buttons simply do nothing there —
     they do NOT fall back to stepping one row. This is the cheaper rule to
     reason about and the honest one: a side button means "page", and where
     there is no page there is no action. Consequence to handle deliberately —
     `ButtonNavigator::onContinuous` must not fire either, or a held side button
     on a short list burns a redraw per repeat doing nothing.
  3. ~~**Ends: wrap or clamp?**~~ **RULED 2026-08-14: clamp.** Paging stops at
     the ends; the last page stays on the last page. This is what
     `HomeActivity.cpp:210-217` already does for the Lyra Six two-page home, and
     for the reason given there — the ends of a list should be perceptible, and
     a press should move one step in the direction pressed rather than teleport.
     Two consequences to handle: that HomeActivity special case likely becomes
     redundant once paging clamps everywhere (verify before deleting it, since
     it governs the FRONT pair too), and side and front will now differ at the
     boundary — front keeps wrapping via `ButtonNavigator::nextIndex`, which is
     shared with many activities and stays untouched.
  4. ~~**Held-button repeat.**~~ **RULED 2026-08-14: repeat, at the same rate as
     the front buttons.** A held side button keeps paging on the existing
     interval — no second timing constant. `onNext`/`onPrevious` already wire
     press and continuous together (`ButtonNavigator.cpp:5-13`), so the page
     pair gets this for free by being built the same way.

     This makes question 2's ruling load-bearing rather than cosmetic: on a list
     that fits one screen the side buttons are dead, and with repeat enabled a
     held button would otherwise fire `onContinuous` several times a second
     against a list that cannot move. Suppress it at the source — the page
     callback should return early when there is no page to turn, before any
     `requestUpdate()`, so a held button on a short list costs nothing rather
     than burning a redraw per repeat.

  All four questions are now settled. **SHIPPED 2026-08-15 on branch
  `pageupdown`** — sim-verified, device-unconfirmed. What landed:

  * `MappedInputManager::Button::PageNext` / `PagePrevious`, the third logical
    pair, bound to the side buttons. `NavNext`/`NavPrevious` narrowed to the
    front pair, exactly as sketched above.
  * The side pair **honors `SETTINGS.sideButtonLayout`** so a list pages the
    same way round as the book, but `SIDE_BUTTONS_DISABLED` deliberately falls
    through to the natural order instead of going inert. That setting exists to
    stop the side buttons turning BOOK pages; honoring it here would leave every
    list with no page control at all. Same trap, same fix, as
    `ReaderUtils.h:112-125` records for the reader's side font controls.
  * `ButtonNavigator::pageDown/pageUp(int& index, total, perPage)` — the clamped
    arithmetic, returning false when nothing moved so question 4's redraw
    suppression is mechanical rather than per-screen discipline. `onPageNext` /
    `onPagePrevious` wire press + hold-repeat, on their **own** repeat clock:
    `onRelease()` drops a release whenever a continuous fired, so one shared
    timer would have let a held side button eat the next front-button step.
  * **Question 1, resolved as: a page is one screenful, and the selection keeps
    its position within the page** (`index ± perPage`, not a snap to the page
    boundary). `(i + p) / p == i / p + 1` for any `i`, so the drawn page — which
    `BaseTheme::drawList` derives as `selectedIndex / pageItems` — advances by
    exactly one either way; keeping the offset means the highlight does not jump
    to the top row and the eye does not have to re-find it. The clamp overrides
    it at the ends, sliding onto the first/last row.
  * **Home is the exception, and it is question 1's "next section" reading** —
    on the two-page home the covers ARE page 0 and the menu IS page 1, so the
    two readings coincide there and paging lands on the first row of the other
    page. Dead when the home is not split (no recent books).
  * The `HomeActivity` clamp special case was **kept**, per the "verify before
    deleting" note: it governs the FRONT pair, which still wraps everywhere else
    via `ButtonNavigator::nextIndex`.

  Two consequences worth knowing:

  * **`OptionPopup` side buttons are now dead.** It navigates on
    `NavNext`/`NavPrevious` and its options always fit the dialog, so question
    2's ruling applies verbatim: no page, no action. Front Left/Right still move
    the selection. Flagged rather than special-cased — say so if a popup should
    keep the side pair as a step.
  * **`TextViewerActivity` and `ColophonActivity` wire the page pair
    explicitly.** They are paged text, not row lists, so both pairs turn a page
    there and that is correct rather than the redundancy this ruling is about.
    Without the explicit wiring the narrowing of `NavNext` would have silently
    stopped their side buttons working.

  Exempt, unchanged: `FontSelectionActivity` and `EditorFontSelectionActivity`
  (side buttons step the font size), `IntervalSelectionActivity` (side buttons
  are the coarse value step), the keyboards / note editor / Claude chat (side
  buttons are bespoke picks), and every reader (`PageBack`/`PageForward`).

  **Exempt since 2026-09-02: `WifiSelectionActivity`** — the side pair STEPS
  the networks, one row per press, wrapping. Its front pair already means
  something else, which is what this list is for: Right is always Retry
  (`onNext` is dead for presses), Left is Forget on a network with a saved
  password and otherwise falls through to a step UP, unlabeled — a pre-existing
  asymmetry, not a clean split. `f278be2fc` made the side pair page anyway,
  and on a scan that fits one screen `pageDown` returns false, so no button
  moved the highlight DOWN at all (`docs/ux-navigation-audit-2026-09-02.md`,
  F3). The trade: a long list (30 SSIDs on an X3/X4, which have no swipe) is
  now stepped with a held DOWN rather than paged. Swipes still page.

  **The `EditorFontSelectionActivity` half of that exemption was ASPIRATIONAL
  when written, and is only true from 2026-08-18.** `f278be2fc` copied the
  sentence over from `FontSelectionActivity` and left the screen's own comment
  pointing at a "size handler above" that did not exist — `SETTINGS.editorFontSize`
  had not shipped yet, and when it did (`62905d8e2`) it arrived as a row in the
  System settings list instead. The exemption was still correct in its effect
  the whole time (the screen wires `Button::Right`/`Left` explicitly, so it
  never picked up the page handlers), just wrong about the reason. Owner ruling
  2026-08-18 moved the size onto the screen and the sentence became accurate:
  `EditorFontSelectionActivity::changeFontSize` now steps
  `editorfonts::SIZES` on `PageForward`/`PageBack`, clamped at both ends, and
  the System row is withdrawn by category. The lesson worth keeping is that a
  comment describing a handler is not evidence the handler exists — grep for it.

  Coverage: `test/activity_input/ListPagingTest.cpp` (12 cases — the mapping
  seam, both clamps, a short list, an exact multiple, and the shared-timer
  hazard).

## Custom iconography — sweep closed (ruling 2026-08-15)

Two glyphs were redrawn after owner reports, and the sweep **stops there**.

Shipped:

* **Return arrowhead** — two fanned thick strokes could not form a point (each
  `drawLine` ends in a flat cap, and two caps overlap into a lozenge), so it is
  a filled triangle. Then a follow-up report — "the top half is missing ink on
  its left side" — turned out to be two faults in `fillPolygon` rather than in
  the glyph: division truncation that followed the edge-walk order, and a
  parity rule that dropped the row at each edge's minimum y. Triangles now
  rasterize by exact integer half-space tests.
* **Text cursor I-beam** — notched serifs plus a baseline crossbar, at the
  shipped 2 px serif. The serif notch is the shipped macOS cursor's shape; the
  crossbar is the classic Macintosh I-beam's baseline mark.

**Declined, and not to be re-proposed as an improvement:** redrawing the
battery and the charging bolt, and the lower-ranked glyphs behind them. The
faults are real and were measured — the bolt is eight literal scanlines with
two flare rows at double the stem weight and the halves offset by 1 px, and the
battery's terminal nub is two bare `drawPixel` calls at hardcoded offsets that
only centre at one height — but the owner ruled the sweep closed with the two
keyboard glyphs done. Header art stays as it is.

**`CoverIcon` stays, ruled 2026-08-15.** `src/components/icons/cover.h` is
included by no file in either repo, its array is referenced nowhere, and it does
NOT appear in the device binary -- verified with `nm` on `firmware.elf`, so it
occupies zero flash. Deleting it would buy nothing measurable, and on an
e-reader a cover icon is a plausible deliberate stub. Do not re-file it as an
orphan: the zero-reference grep is not news, it is the recorded state.

Still genuinely unverified: the icons agent reported two unreachable `BaseTheme`
shapes. Nobody has confirmed that independently, and it was never ruled on -- so
it is an open question, not a pending cleanup.
