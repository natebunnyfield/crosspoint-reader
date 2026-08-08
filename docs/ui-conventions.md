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
