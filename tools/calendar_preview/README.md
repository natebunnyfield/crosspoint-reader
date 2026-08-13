# CrossPoint off-device render harness

One binary, `./render_harness`, that drives the **real** firmware `GfxRenderer`, `EpdFont`
and `SdCardFont` against a host-side framebuffer and dumps 1-bit BMPs.

This exercises the actual on-device code — font metrics, glyph rasterisation,
the Portrait→panel rotation, kerning, and the BMP writer — without needing SDL2
or the PlatformIO simulator. It exists because layout bugs (text baseline vs.
box top, ink-vs-ascender centring, header overflow, a kern change worth one
pixel) are invisible in a reimplementation and only show up against the real
renderer.

The `Hal*.h` / `Arduino.h` files here are minimal host stubs: a RAM
framebuffer, `millis()`, and a `HalStorage` that maps SD paths onto `./fs_/`.

Was two binaries (`render_test`, `kern_specimen`) until they were consolidated;
they duplicated the framebuffer setup and the BMP writer.

## This is not the simulator

The simulator is a separate project: [`~/src/crosspoint-simulator`](https://github.com/crosspoint-reader/crosspoint-simulator).
It boots the **whole firmware** as an SDL app on Mac/Linux and as an iOS app.
This repo consumes it through `lib_deps: simulator=symlink://...` plus
`[env:simulator]` in `platformio.ini`, and you run it with:

    pio run -e simulator -t run_simulator

Use the simulator to *use the reader* — navigate, turn pages, check a flow end
to end, or ship to TestFlight. Use this harness to *measure a rendering*.

|  | this harness | the simulator |
|---|---|---|
| what it runs | a few firmware TUs, one draw call | the entire firmware, booted |
| output | raw 528×792 1-bit framebuffer | SDL screenshot at host Retina size |
| cost | ~9 ms per render (203 dates in 1.87 s) | full app launch per run |
| determinism | same input, same bytes | depends on scripted input timing |
| good for | pixel-exact layout + kerning regressions | interaction, flows, shipping |

The precision difference is the reason both exist: `check_centering.py` asserts
digits sit within 2 **device** pixels of their box center, which only means
something against the real framebuffer, not a Retina-scaled screenshot of it.

## Build

    cd tools/calendar_preview
    ./build.sh          # -> ./render_harness

## Calendar sleep screen

    ./render_harness calendar 2026 7 27     # year month day (defaults to 2026-07-27)
    ./render_harness 2026 7 27              # legacy form, same thing
    # -> ./fs_/sleep.bmp  (528x792, 1-bit)

## Font / kerning specimen

Renders the same text through two installed SD font families, one above the
other, at 12/14/16/18pt — for judging a `.cpfont` kerning change against the
real `GfxRenderer` / `SdCardFont` path rather than a host reimplementation.

    CPFONT_DIR=/.fonts ./render_harness fonts RosarivoV1 Rosarivo
    # -> ./fs_/kern_specimen_{12,14,16,18}.bmp, plus per-line widths on stdout

## Inline italic specimen

    ./render_harness inline InknutJunicode
    # -> ./fs_/inline_<FAMILY>_{0,1,2,3}.bmp, one per ordinal size slot

Renders three paragraphs with italic (and one bold) set **inline, mid-sentence**
rather than as their own block. That distinction is the whole point: `reading`
draws each style as a separate paragraph, so an italic is judged against
nothing, while an italic beside the roman word touching it is judged against
the thing it has to match. It is the mode that settled the Inknut/Junicode
pairing — a borrowed italic from another typeface looked fine as a block and
obviously wrong inline.

The paragraph source uses `<i>`/`<b>` markers, which may sit inside a word so
punctuation stays with the roman (`<i>rounding</i>,`). A word butted straight
against the previous one across a tag is *glued*: no space, and the pair wraps
as one unit so a comma can never start a line alone. Word gaps are measured
differentially (`"n n"` minus two `"n"`) because `getTextWidth(" ")` returns 0
through this path — measuring it directly ran every word together into one
unbroken string. Kerning is not applied across a style boundary, matching how a
styled run is laid out in the reader rather than being a shortcut here.

Families live under `./fs_/.fonts/<Family>/` **and** `./fs_/fonts/<Family>/` —
`SdCardFontRegistry` scans both roots and dedupes by name, hidden first, and the
installed set routinely straddles the two (locally-sourced families in `.fonts`,
`install-sim-fonts.py`-rebuildable ones in `fonts`). Leave `CPFONT_DIR` unset and
the harness searches both the same way. Set it to pin one *device-style* root
(`/.fonts`), or to empty to simulate "family not installed" and exercise the
calendar's Noto fallback.

Sizes are resolved **nearest-match, not by exact filename**, mirroring
`family.findNearestSize()` ([SdCardFontManager.cpp:68](../../lib/EpdFont/SdCardFontManager.cpp)).
Families ship different ladders — Venetian301 is 15/17/19/21, Inknut 10/12/14/16,
Coelacanth 14/16/18/20 — and an earlier version of this stub assumed 12/14/16/18,
which made every one of those fail to load with "not installed".

**Kerning is not resident until a page is prewarmed.** `SdCardFont` keeps only
a per-page mini kern matrix ([SdCardFont.h:179](../../lib/EpdFont/SdCardFont.h)),
built by `prewarmStyle` ([SdCardFont.cpp:956](../../lib/EpdFont/SdCardFont.cpp)).
Measure straight after `load()` and you silently measure an *unkerned* font —
the first version of this harness reported identical widths for two very
different fonts. Call `prewarm(text, styleMask, /*metadataOnly=*/false)` before
each measurement; each call replaces the previous page's tables, and the
`metadataOnly=true` path skips the mini kern entirely.

## Regression sweep

`sweep.py` renders several hundred dates and asserts nothing touches the panel
edges — this is what caught the cross-year header overflow
("Diciembre 2026 – Febrero 2027" is wider than 528px at 18pt bold).

    python3 sweep.py

## Centring check

`check_centering.py` renders several dates and, for every highlight box,
measures the offset between the box center and the center of the digit ink
inside it. This is the check that would have caught the original
baseline-vs-box-top bug, where digits sat ~8px low because `drawText`'s `y` is
the top of the ascender box (41px for Noto Sans 18) while digits are only 28px
of ink resting on the baseline.

    python3 check_centering.py
