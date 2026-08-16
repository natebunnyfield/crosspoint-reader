# Dark mode

Two decisions carry the whole feature, and both are counter-intuitive enough
that changing either one back will look like a simplification.

## 1. The flip lives in the firmware, not in the SDK

`FreeInkDisplay::setInverted()` exists and does exactly the byte flip dark mode
needs (`FreeInkDisplay.cpp:577-579`). Dark mode originally shipped on it
(`c2d26ae29`). `HalDisplay` no longer uses it, because of everything else that
flag switches off while it is set:

| While `_inverted` | Where |
|---|---|
| every grayscale write dropped | `FreeInkDisplay.cpp:779`, `:797`, `:826-839` |
| `supportsStripGrayscale()` → false | `:858-860` |
| `supportsBusyGrayscaleStaging()` → false | `:848-850` |
| async refresh refused, blocking path forced | `:557-559`, `:609-612` |
| window diffs promoted to a full refresh | `:757-762` |
| grayscale cleanup skips the driver resync | `:864-868` |

That is where dark mode's antialiasing went, and the overlapped page turn with
it. `lib/hal/PanelPolarity.h` does the same flip on this side, so the SDK stays
in its normal, fully featured state.

**The invariant** (`PanelPolarity`): the framebuffer is LOGICAL — 1 is white —
whenever a caller may draw into it, and carries panel polarity only for the
duration of a call into the SDK. The one extension is an async refresh: the
buffer stays flipped from `displayBufferAsync()` until `waitRefreshComplete()`,
which is safe precisely because the async contract already forbids touching it
in that window (and X3's `displayFinish` re-reads it, so it must still be in
panel polarity when the refresh drains).

**What this costs, and it is a real regression.** The panel drivers carry a
dark-background corrective: on a differential refresh they write the *complement*
of the target as the baseline so every pixel re-drives, because otherwise the
light residue of each white→black transition parks in the black background and
accumulates between absolute cleans (`Ssd1677Driver.cpp:427-439`,
`Uc8253X3Driver.cpp:195-210`). It is reached only through
`setBackgroundHint()`, which is private to the SDK and called only from
`FreeInkDisplay::setInverted()`. There is no public way to ask for it. So dark
mode now ghosts more between the reader's periodic HALF refreshes than it did on
the driver-side flip.

The one-line SDK change that would fix this is exposing `setBackgroundHint()` on
`FreeInkDisplay`; do not work around it by feeding a complement through
`cleanupGrayscaleBuffers()`, which happens to land in the right controller RAM
on both C3 drivers but also resets their grayscale sequencing state.

## 2. Antialiasing in dark mode is a level remap, not an unblocking

Removing the SDK gate is necessary and **not sufficient**, because the grayscale
overlay is a one-way waveform. It lifts a BLACK pixel part of the way toward
white. It cannot darken a white one.

* X3: the OEM gray bank's white→black cell is deliberately dead —
  `freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8253X3Luts.h:117-120`,
  "stock's gc bank does not drive white-to-black pixels during the gray nudge".
  The two cells that do drive (`ww_gc` 0x20, `bw_gc` 0x80) are both single VDL
  phases of different length — same direction, two depths.
* X4: same shape. `Ssd1677Luts.h:11-20` — the leave-alone group is all zeroes
  and the firmware only ever emits three of the four groups; none of the three
  darkens.
* Stated independently in `crosspoint-simulator/src/GrayscalePreview.h:16-18`.

So an inverted page cannot antialias by flagging its white glyph edges. It has
to leave those edge levels ON the black background and let the same lift produce
them. `lib/GfxRenderer/GlyphAaPlanes.h` is that table: in dark mode the BW base
pass paints fewer levels, and the two gray targets swap, because more ink now
means lighter.

Consequence worth stating plainly: **both polarities ask the panel for the
identical physical transition.** Dark-mode AA is not a new waveform, a new LUT
or a new risk to the panel; it is the same black→gray nudge pointed at different
pixels.

The renderer flag is `setDarkModeAntiAliasing()`, not `isDisplayInverted()`, and
the difference matters: with no overlay coming, skipping the antialiased levels
in the base pass leaves glyphs skeletal with nothing to fill them in. It is
scoped to a single reader page render and cleared on every exit.

## Images keep their own polarity

Unchanged from `c2d26ae29`: content images counter-invert in the framebuffer
(`GfxRenderer::preserveImagePolarity`) so the output flip lands on them twice and
cancels. `XtcReaderActivity` now does the same for its whole frame — an XTC page
is a pre-rendered picture, and it was rendering as a negative.

Grayscale **planes** are never counter-inverted. A plane bit means "nudge this
pixel", not "this pixel is white"; it carries no polarity, and flipping one
would corrupt the overlay. `HalDisplay::copyGrayscale*Buffers` and
`writeGrayscalePlaneStrip` therefore pass their buffers straight through, while
`cleanupGrayscaleBuffers` — which seeds the differential baseline with actual
picture data — does flip.

## 3. On a host, the SYSTEM owns the appearance and the setting follows it

Added 2026-08-16. On device `SETTINGS.darkMode` is the only input: the owner
toggles it, `main.cpp` applies it at boot, `SettingsActivity` applies it on
change. On a host that has an appearance of its own — the iOS app — the phone
owns it instead, and the setting is kept in step with the phone.

`applyTheme()` in the simulator's `ios/CrossPointIOSShim.cpp` writes
`SETTINGS.darkMode` whenever the system appearance flips, guarded on change so a
repaint does not rewrite the settings file.

**Why the write is not optional.** `main.cpp` runs
`display.setInverted(SETTINGS.darkMode != 0)` during `setup()`, which happens
AFTER the harness installs. A stale value therefore does not merely display
wrong — it is pushed back onto the panel and undoes the appearance the phone
asked for. That same ordering is why `CROSSPOINT_SIM_DARK` cannot force a
desktop capture; set `darkMode` in `fs_/.crosspoint/settings.json` instead.

**And the screen has to be re-RENDERED, not just re-presented.**
`SimulatorOverlay::requestPresent()` only pushes the framebuffer that already
exists, so anything drawn from the old value stays on screen — most visibly the
System > Dark Mode row, which kept painting "OFF" over a dark page until the
owner navigated away and back. The polarity flip inverts those pixels, so the
stale row was perfectly legible and perfectly wrong. `applyTheme()` now calls
`crosspointRequestRender()` (`src/SimulatorRenderRequest.cpp`), a one-call seam
that exists because `ActivityManager.h` holds `unique_ptr<Activity>` and would
otherwise drag the whole activity header set into an Objective-C++ translation
unit.

## Status

**UNCONFIRMED on device** — nothing here has been seen on a real panel.

It IS confirmed in the simulator, which the previous version of this note said
was impossible. That blocker is gone: inversion no longer needs a 255-level
flip at all, because the dark palette's ink→paper direction already runs
light-on-dark (`HalDisplay.cpp:411-413`), so the same interpolation serves both
polarities.

Verified live on the iOS Simulator, 2026-08-16, on the Settings screen and
without navigating away: flipping the system appearance moved the Dark Mode row
from OFF to ON in place, the log carried
`[harness] SETTINGS.darkMode -> 1 (system appearance)`, and the page flipped to
the dark half of the chosen palette. Flipping back returned it to OFF, so it
tracks both directions rather than latching.
