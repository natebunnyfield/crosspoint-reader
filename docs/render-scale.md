# Render scale: a compile-time ceiling with a runtime factor

*Written 2026-08-15, against `f828196a1` (firmware) and `7630d87` (simulator),
on branch `ios-render-scale`. Everything below marked "measured" was run; the
one section marked UNVERIFIED says so and why.*

The supersampling factor between the LOGICAL coordinate space the firmware
lays out in and the PHYSICAL framebuffer it paints into. It is 1 on device and
has always been 1 on device; the whole subject is host builds.

## The question this document exists to answer

> Can the render scale be changed at runtime, or does it require an app
> relaunch?

**Neither, as the code stood on 2026-08-15.** It could not be changed at all
without recompiling, and a relaunch would not have helped, because a relaunch
runs the same binary and the factor was a `constexpr`. That answer is worth
stating plainly because "it takes effect next launch" is the intuitive
fallback, and it was not available.

The evidence, all against `main` before this branch:

| Site | What made it compile-time |
|---|---|
| `crosspoint-simulator/src/HalDisplay.h:56-60` | `static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH * RENDER_SCALE`, and `BUFFER_SIZE` from it |
| `crosspoint-simulator/src/HalDisplay.cpp:155-164` | four `std::array<uint8_t, HalDisplay::BUFFER_SIZE>` — an array bound cannot move after the compiler has run |
| `lib/GfxRenderer/GfxRenderer.cpp` (~14 sites) | `constexpr int S = CROSSPOINT_RENDER_SCALE;` in the fill, glyph, arc and rect paths |
| `lib/GfxRenderer/GfxRenderer.cpp:1261` | `constexpr int MASK_PERIOD = 2 * S;` sizing a stack array |
| `lib/GfxRenderer/GfxRenderer.h`, `FontCacheManager.h`, `BackspaceIcon.h`, `KeyboardIconTarget.h` | `#if CROSSPOINT_RENDER_SCALE > 1` gates whole declarations in and out — including a `FontCacheManager` constructor parameter, i.e. a **struct layout** difference |
| `src/main.cpp:208` | `#define CP_HR(base) CP_CAT(...CROSSPOINT_RENDER_SCALE...)` pastes the tier into C++ **identifier names** (`librefranklin_8_bold_3x`), so only one tier of built-in hi-res glyph data was ever compiled in |
| `crosspoint-simulator/ios/CMakeLists.txt:122` | `file(GLOB ... "${_family_dir}/${CROSSPOINT_IOS_RENDER_SCALE}x/*.cpfont")` — only one tier of SD hi-res fonts was ever bundled into the `.app` |

The last two are the ones that make "relaunch" insufficient rather than merely
awkward: even with the arithmetic made dynamic, a binary built at 3 contained
no 2x glyph tables and the app bundle contained no 2x `.cpfont` files. There
was nothing on the device to relaunch into.

## What this branch changed

The macro `CROSSPOINT_RENDER_SCALE` keeps its name and becomes the **ceiling**.
A new [`lib/GfxRenderer/RenderScale.h`](../lib/GfxRenderer/RenderScale.h) adds
`cp::renderScale()`, the **active** factor, latched once at startup:

```cpp
inline constexpr int kRenderScaleMax = CROSSPOINT_RENDER_SCALE;   // arrays, #if
int  cp::renderScale();                                            // arithmetic
void cp::setRenderScale(int);                                      // clamps to [1, max]
```

On device, and on any host build that did not opt in,
`CROSSPOINT_RENDER_SCALE_RUNTIME` is undefined, `renderScale()` is a `constexpr`
function returning the ceiling, and every `const int S = cp::renderScale();` in
the tree folds to a literal exactly as `constexpr int S = ...` did. **The device
build is textually and semantically unchanged.**

Where the two differ:

* **Ceiling** — the four simulator framebuffers, the ARGB present buffer,
  `blackMasks[2 * kRenderScaleMax]`, every `#if ... > 1`, which builtin font
  tiers get compiled, which SD font tiers get bundled.
* **Active** — `drawPixel`'s block size, `getScreenWidth/Height`, the
  logical↔device conversions, the SDL texture size, the presented panel rect,
  the SD hi-res tier directory name, which builtin companions get registered.

`HalDisplay` grew `activeWidth() / activeHeight() / activeWidthBytes() /
activeBufferSize()` alongside the unchanged `DISPLAY_*` constants; the constants
now mean "the ceiling", and the header says so. `GfxRenderer` needed no change
to pick this up — it already read its panel geometry from
`display.getDisplayWidth()` at construction
(`lib/GfxRenderer/GfxRenderer.cpp:130-133`), so the members were runtime
already and only the getters had to start telling the truth.

### Why latched and not live

The ceiling gates preprocessor conditionals and sizes static arrays; those
cannot move after the compiler has run. What the latch buys is that the active
factor no longer has to *equal* the ceiling — one binary compiled at 3 renders
at 1, 2 or 3. It must run before `HalDisplay::begin()` creates the panel
texture and before `setupDisplayAndFonts()` registers the hi-res companions;
after that the framebuffer geometry and the glyph tier are committed for the
life of the process.

So: **a change takes effect on the next launch**, and the owner-facing setting
says so in its own footer rather than leaving it to be discovered.

The latch lives in `crosspoint-simulator/src/simulator_main.cpp`'s
`latchRenderScale()`, called as the first statement of `main()`. It is
deliberately **outside** the iOS deep-sleep `setjmp` target: a wake re-runs
`setup()` against a live process whose texture and font maps already exist, and
re-reading the setting there would change the arithmetic out from under geometry
that cannot follow it.

## Both font tiers, on both sides

`CP_HR(base)` became `CP_HR(base, tier)` and is instantiated once per tier from
2 up to the ceiling, so a ceiling-3 binary carries `..._2x` **and** `..._3x`
symbols. Registration picks by `cp::renderScale()` in a `switch`, and its
`default:` — scale 1 — registers **nothing**, which is correct rather than an
oversight: at 1x the 1x face already matches the framebuffer's density, and a
companion would blit glyphs three times too big.

`lib/EpdFont/builtinFonts/all.h` moved from `== 2` / `== 3` to `>= 2` / `>= 3`
for the same reason. `SdCardFontManager::hiResCompanionPath` derives the tier
directory from `cp::renderScale()`, and the lookup is skipped outright at scale
1 so the "No hi-res companion" INF line does not fire on every load reporting a
`1x/` directory that is not supposed to exist.

On the iOS side, `ios/CMakeLists.txt` bundles every tier from 2 to the ceiling,
and `ios/CrossPointFsPrep.cpp` seeds every tier onto the emulated card. Seeding
only the active tier would leave the other missing on the launch that starts
using it — the same silent fallback to replicated 1x glyphs, arriving one
launch after the setting was changed, which is the hardest possible version of
that bug to attribute.

**Cost: about +53 MB of app bundle** (measured on the six families installed
at the time: the 1x set is 16 MB, 2x is 53 MB, 3x is 114 MB; the app already
carried 1x+3x. TeX Gyre Heros made it seven on 2026-08-23 and the figure has
not been re-measured since).

## The filter each scale ends up with

`panelScaleModeFor()` (`crosspoint-simulator/src/HalDisplay.cpp:148`) returns
`SDL_SCALEMODE_LINEAR` below a presented scale of 1 and `kPanelScaleMode`
(`SDL_SCALEMODE_NEAREST` under `CROSSPOINT_SIM_PIXEL_EXACT`, which the iOS build
sets at `CMakeLists.txt:235`) at or above it. It is applied per present, keyed
on the settled scale, not on the build flag
(`HalDisplay.cpp`, `SDL_SetTextureScaleMode(texture, panelScaleModeFor(scale))`).

Changing the render scale changes the presented scale, so it changes which
branch is taken. On an iPhone Air (1260 px wide, portrait; the panel presents as
`activeHeight() x activeWidth()`):

| Setting | Framebuffer | Presented panel | Fit | Settled | Filter |
|---|---|---|---|---|---|
| Panel (1x) | 792x528 | 528x792 | 2.386 | **2.0** (integer-floored) | NEAREST — one panel pixel per clean 2x2 block |
| Exact (2x) | 1584x1056 | 1056x1584 | 1.193 | **1.0** (integer-floored) | NEAREST, but at 1:1 nothing is resampled at all |
| Fine (3x) | 2376x1584 | 1584x2376 | 0.7955 | **0.7955** (quantised, below 1) | LINEAR — the 2026-08-15 moire fix, unchanged |

So the moire fix keeps doing the right thing at each setting, and the two
settings *below* 3x are the two that never reach its branch. The arithmetic is
derived from the code and the measured 1260 px width; **the presented scales on
a real iPhone are UNVERIFIED on device** — see "What was not verified".

`kPixelQuantum` became the function `pixelQuantum()` for the same reason
everything else did: it is `gcd(activeWidth, activeHeight)/2`, which is 132 at
1x, 264 at 2x and 396 at 3x on X3. Quantising a 2x present to the 3x ceiling's
step would land the panel on fractional device pixels, which is precisely the
fault that constant exists to prevent.

## What was verified, and how

**The strongest check available, and it passes: a runtime binary is
byte-identical to a fixed-scale one, at every scale.**

`scripts/sim_render_scale.py` gained `CROSSPOINT_RENDER_SCALE_RUNTIME=0`, which
builds a binary whose scale is a compile-time constant again — the shape this
tree had before the branch. Rendering the same reader page (Wingspan,
InknutJunicode 16, `-e simulator_x3`) through both and comparing the captured
BMPs:

| Scale | Runtime (ceiling 3) vs fixed | SHA-256 |
|---|---|---|
| 1 | **identical** | `653a26e4…7373` |
| 2 | **identical** | `99ced1a6…5a81d` |
| 3 | **identical** | `f7f931c4…27c76` |

That is the proof that all ~40 converted arithmetic sites read the active factor
and none silently kept reading the ceiling: a single one that did would move
pixels at scale 1 or 2.

Also measured:

* Framebuffer geometry follows the setting: `792x528` / `1584x1056` /
  `2376x1584` on X3, logged at boot as
  `Framebuffer WxH, render scale N (ceiling 3)`.
* The SD hi-res tier follows it:
  `Loaded hi-res /fonts/InknutJunicode/2x/InknutJunicode_16.cpfont` at scale 2,
  `3x/` at scale 3, and nothing at all at scale 1.
* **Layout does not move.** The three page captures break the same words on the
  same lines; only the glyph raster differs. That is the invariant the whole
  supersampling design rests on and it survived.
* `pio run -e default` (ESP32-C3 device) SUCCESS.
* `pio run -e simulator` at the default ceiling of 1, with no runtime switch:
  SUCCESS, and the pre-build script confirms `CROSSPOINT_RENDER_SCALE=1
  (default)` with no `_RUNTIME`.
* `crosspoint-simulator` `tests/run_all.sh`: **18 passed, 0 skipped.**

The rebuild stamp in `sim_render_scale.py` now includes the runtime switch, not
just the scale. It had to: the switch changes whether `cp::renderScale()` is a
`constexpr` function or an `extern` variable, and it changes
`DirectPixelWriter`'s layout (a member under runtime, nothing under fixed). A
stamp on the scale alone would have let a reference build link against runtime
objects — the mixed-object bug that check was written for, in a new hat.

## What was NOT verified

* **Nothing was run on an iPhone, or in the iOS Simulator.** The presented
  scales, the filters, and the Settings.app group are reasoned from the code and
  from the measured 1260 px width. Status: **SHIPPED — UNCONFIRMED on device.**
* **The iOS CMake configure was not run**, so the `foreach(_tier RANGE ...)`
  seed-font loop and the `CROSSPOINT_RENDER_SCALE_RUNTIME=1` definition are
  unexercised. Configuring needs the 183 MB gitignored `ios/seedfonts/` tree and
  an iOS SDK toolchain. The `RANGE` is guarded against a ceiling of 1, where
  CMake treats `RANGE 2 1` as an error rather than an empty loop.
* **The +53 MB bundle figure is a directory measurement, not an IPA
  measurement.** `.cpfont` files compress; the archived delta will be smaller,
  by an unmeasured amount.
* **1x on a phone was not looked at on a phone.** It is offered because it
  works and because it is the only setting that shows what the e-ink hardware
  actually draws; the captures show it is legible at 2.0 presented scale, but
  whether it is *pleasant* on glass is a judgment nobody has made yet.

## What to look at, on device

1. Settings > CrossPoint X3 > **Page Sharpness**. Default reads
   **Fine (3x)** on a fresh install and on an upgrade.
2. Change it to **Exact (2x)**. Return to the app: **nothing changes**, which is
   correct and is what the footer says.
3. Force-quit from the app switcher, relaunch, open a book. The page should
   look very slightly coarser in the letterforms and *cleaner* in the greys —
   selection fills and dithered covers in particular, because at 2x the panel
   presents at exactly 1.0 and nothing is resampled.
4. Set **Panel (1x)**, relaunch. The page should be visibly blocky — each e-ink
   pixel a clean 2x2 square — with identical line breaks to the other two.
5. Back to **Fine (3x)**, relaunch, and confirm it is indistinguishable from the
   build before this change.

## Standing ruling 2026-08-15 — all three scales ship, +53 MB accepted

The owner ruled: keep 1x, 2x and 3x, and accept the ~53 MB of bundle needed to
carry both hi-res font tiers.

The reasoning, so it is not re-litigated. 2x is the only setting that presents
the panel at **exactly 1.0** on a 1260 px-wide phone — nothing resampled, the
framebuffer reaching the glass untouched. It is therefore the only mode that
provably cannot moire, and dropping it would have left a choice between blocky
(1x, integer-floored to 2.0) and bilinear-filtered (3x). Dropping the ceiling to
2 instead was rejected for the mirror reason: 3x is the only scale that reaches
the minified branch, so it is the only one the bilinear fix exists for.

Bundle size is an install-time cost and nothing more here — TestFlight on this
project is single-user.

**Do not re-propose trimming a tier as a size saving.** It was measured, costed
and declined. Reopen only if a future panel or device geometry changes which
scales land on the pixel grid.
