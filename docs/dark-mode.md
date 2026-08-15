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

## Status

**SHIPPED TO BRANCH — UNCONFIRMED on device.** Nothing here has been seen on a
panel. It cannot be seen in the simulator either without a matching change
there: `GrayscalePreview::previewLevel` gates the lift on the pixel being black
in the LOGICAL framebuffer, which is the wrong test once the flip is applied at
display time.
