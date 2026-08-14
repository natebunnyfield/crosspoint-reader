# UI conventions

Established 2026-08-08 by the navigation/widget/settings consistency audit.
These are rules, not observations: new screens follow them, and drift from
them is a bug. File:line references are to the state at adoption.

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
live preview (Text Settings), a file listing (browser, firmware picker), or a
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
- `ActivityManager` arms `MappedInputManager::swallowUntilIdle()` on every
  activity swap: the incoming activity never sees the press/release edge that
  drove the transition. **Do not add per-activity release latches for
  child-exit leakage** — that central swallow is the mechanism. Latches remain
  legitimate only for intra-activity cases (popup closes, chord gestures) where
  no swap occurs.
- Readers route Back through `ReaderUtils::handleBackNavigation` (honors
  `backShortToFileBrowser`, short/long split). All of Epub/Xtc/Txt/Bmp do.
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

  All four questions are now settled. Not started. No branch.
