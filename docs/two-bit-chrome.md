# Two-bit chrome, and where anti-aliasing pays

Written 2026-08-14. Records what the UI chrome fonts cost at 2-bit, which
surfaces the anti-aliasing overlay is worth running on, and — mostly — which
ones it is not. Every number here is measured; the ones that could not be are
labelled **UNMEASURED**.

## Recommendation, and the number behind it

**Rebuild the chrome cuts 2-bit, uncompressed. Run the overlay on the colophon
and the file viewer, and on nothing else.**

The number that decides the font side is `+122,528 B` of flash against
`2,253,003 B` of headroom in a 6,553,600 B app partition — i.e. it is
affordable, and it is time-neutral, which no other option is.

The number that decides the surface side is `3,249 us -> 33,597 us`: the
overlay makes a settings-list repaint **10.3x** its cost, on a screen where
every Down press repaints and the side buttons auto-repeat.

## Status

**SHIPPED TO BRANCH — UNCONFIRMED on device.** Nothing in this document has
been seen on a panel. `pio run -e default` and `-e simulator` are both green
with no new warnings, and the host suite is 290/290 after merging current
`main`. Panel timing — the gray waveform's real duration, which dominates every
latency decision below — is reasoned about from the LUTs and was never
measured.

Every flash figure below was taken against `48a3ca8c9`, the commit this branch
was cut from, with the twelve headers as the *only* difference between builds.
`main` has since gained dark-mode antialiasing and the PragmataPro editor face,
so the absolute numbers have moved — this branch's tip measures **4,303,811 B /
65.7%** — but the deltas are controlled and transfer unchanged.

## 1. The fonts

`lib/EpdFont/builtinFonts` held exactly twelve 1-bit headers: the Libre
Franklin 8/10/12 pt regular+bold matrix `main.cpp` binds to `SMALL_FONT_ID`,
`UI_10_FONT_ID` and `UI_12_FONT_ID`, plus a `_2x` host companion of each. Every
other builtin was already 2-bit.

That mattered because a grayscale plane is flagged **only** from the 2-bit
branch of `GfxRenderer::renderCharImpl:468-524`, which reads a glyph's four
coverage levels. A 1-bit glyph has no partial coverage; its single write in a
plane pass clears a bit in a plane `clearScreen(0x00)` just cleared. So chrome
anti-aliasing was not "subtle", it was arithmetically nothing — which is how
`feat/antialias-everywhere` came to wire up the colophon and the file viewer,
find the A/B frames byte-identical, and revert both.

### Only six of the twelve reach the device

No device environment in `platformio.ini` sets `CROSSPOINT_RENDER_SCALE`; only
`scripts/sim_render_scale.py` and `test/system_font` do, and `hiResFontMap_` is
`#if CROSSPOINT_RENDER_SCALE > 1`. The `_2x` arrays are therefore unreferenced
statics on device. Verified rather than assumed — `riscv32-esp-elf-nm` on
`.pio/build/default/firmware.elf` reports **zero** `_2x` symbols, in the
baseline and in every variant. All flash figures below are device flash.

### Flash, `-e default`, same tree, only the twelve headers differing

| chrome cuts | Flash | of 6,553,600 B | vs baseline | headroom left |
|---|---:|---:|---:|---:|
| 1-bit, uncompressed (what shipped) | 4,178,069 B | 63.8% | — | 2,375,531 B |
| **2-bit, uncompressed (chosen)** | **4,300,597 B** | **65.6%** | **+122,528 B** | **2,253,003 B** |
| 2-bit, compressed | 4,152,733 B | 63.4% | −25,336 B | 2,400,867 B |

`app0` is `0x640000` = 6,553,600 B (`partitions.csv`), and there is a second
identical `app1` OTA slot, so the ceiling above is the real one.

Static RAM is **54,036 B (16.5%) in all three builds** — the glyph tables are
`.rodata` in the flash-mapped DROM region (`0x3C…`), not DRAM.

The surprise is the third row: a 2-bit glyph deflates to well under half its
size, so compressed 2-bit lands *smaller* than the 1-bit cuts it replaces. Four
coverage levels for −25 KB looks like a free win. It is not, and the reason is
time.

### Why not compressed

A chrome screen has no `PrewarmScope`. The readers do
(`TxtReaderActivity.cpp:408-415`), which is what keeps their compressed cuts
cheap: one scan pass, one prewarm, then every glyph is a page-buffer hit. Chrome
draws straight through, so every glyph goes to `FontDecompressor`'s hot-group
fallback, and `getBitmap` re-inflates a whole group on **every switch of font id
or style** (`FontDecompressor.cpp:186-193` — the hot slot is keyed on
`fontData` *and* `groupIndex`, and holds one).

Measured in the simulator with three variant binaries interleaved round-robin,
so machine load hit all three equally (this matters: a first, non-interleaved
pass produced a 3x spread that was entirely other builds on the same machine).
Median over 33–45 repaints, host arm64 microseconds:

| repaint | 1-bit | 2-bit | 2-bit + compressed |
|---|---:|---:|---:|
| colophon, one page turn | 3,331 us | 3,327 us | **20,395 us** |
| settings list, one Down | 3,249 us | 3,300 us | 5,436 us |
| home menu, one Down | 856 us | 863 us | 1,741 us |

Two readings, both load-bearing:

* **The extra bit is free.** 1-bit and 2-bit uncompressed are within 1.5% of
  each other on every screen. Whatever the plot loop costs, it is not the
  unpacking.
* **The compression is not.** On the colophon it is 6.1x, because that screen
  alternates bold (names, section titles) with regular (addresses, prose) and
  each alternation is a fresh inflate.

Device-independent, so it survives the move off the host:

| repaint | group inflates | uncompressed bytes produced |
|---|---:|---:|
| settings, BW pass | 5 | 19,288 B |
| settings, + overlay | 30 | 113,622 B |
| home, BW pass | 2 | 8,377 B |
| colophon, BW pass | 45 | 194,633 B |
| colophon, + overlay | 162 | 685,006 B |

A colophon page turn would inflate **879,639 B** on a 160 MHz single-core
RISC-V. **UNMEASURED on device**, but there is no plausible uzlib throughput
that makes that acceptable for one button press.

Compression also costs DRAM the chrome never spent. `ensureCapacity` grows the
hot-group buffer monotonically to the largest group it decompresses and frees it
only on `clearCache()`; across the six chrome faces the largest group is
**19,612 B** (`librefranklin_12_bold`), mean 3,278 B. A session that never opens
a book previously touched the decompressor not at all. Uncompressed chrome reads
its glyphs straight out of flash: no inflate, no buffer, and no dependency on
`main.cpp:367-368` having wired a decompressor before anything draws —
`test/system_font`'s `ChromeDrawsWithNoDecompressorAttached` pins that.

Compression is pixel-lossless, incidentally: a Crisp colophon frame rendered
from the compressed cuts is byte-identical to one from the uncompressed cuts.
The choice is purely flash-versus-time.

## 2. The overlay, and why only two screens get it

`TextAa::overlay` (from `feat/antialias-everywhere`, merged into this branch)
prefers a tiled path: two plane passes, each over six 80-row bands, into 8 KB of
scratch — instead of a 48 KB whole-frame save. Cheap on DRAM, expensive on CPU,
because the callback runs **12 times**. `renderCharImpl` culls out-of-band
glyphs before decoding them, so the *blitting* stays near one render per plane,
but any layout the callback redoes is paid per band. `GUI.drawList` re-measures
and re-truncates every row, so a list screen pays all of it twelve times over.

That is the dominant cost, and it is a property of the pass, not of the fonts:

| screen | BW repaint | + overlay | total | multiplier |
|---|---:|---:|---:|---:|
| settings list, one Down | 3,300 us | 29,603 us | 32,903 us | 10.0x |
| home menu, one Down | 863 us | 16,665 us | 17,528 us | 20.3x |
| colophon, one page turn | 3,327 us | 26,726 us | 30,053 us | 9.0x |

Host microseconds; **device time is UNMEASURED**. The ratios are what transfer.

On top of the CPU there is a second panel refresh. Each overlay pushes both
planes to controller RAM, uploads five 49-byte gray LUTs
(`Uc8279X4Driver.cpp:292-296`, `GRAY_LUT_LEN = 49`) or a 42-byte set on the X3
(`Uc8179Driver.cpp:41`), runs `DRF`, waits busy, then re-streams the frame to
re-seed the differential baseline. The LUT upload itself is trivial — 245 bytes
of SPI — but the waveform it selects is a multi-phase gray drive, and its
duration is the real cost. **UNMEASURED, and not measurable off-device**;
nothing in the simulator or the LUT tables gives a wall-clock figure.

### Ruling

| surface | AA | why |
|---|---|---|
| **Colophon** | **yes** | Prose you read and page through. One panel refresh per page, which is the shape the reader has always paid the overlay on. |
| **TextViewer** | **yes** | Same shape. Text pages only — an image page returns early, since `TextOnlyScope` would stop the picture reaching the plane but not stop `renderImage()` re-decoding the file once per band. |
| Settings list | **no** | Implemented, measured at 10.0x, removed. Every Up/Down repaints the whole screen and the side buttons auto-repeat; the overlay would put a gray waveform behind a held button. |
| Home | **no** | Implemented, measured at 20.3x, removed. Same repaint shape, and its covers are pictures the one-way overlay cannot help. |
| Keyboard | no | Repaints per keystroke, and its block cursor and selected key are white-on-black, which the overlay cannot touch at all. |
| File browser | no | A list. Same reasoning as Settings. |

The two removals are the honest result of the exercise, not an omission: both
were wired, both were A/B-verified to move real pixels, and both were taken out
on their own numbers.

## 3. Which strength, and is it worth it at 8/10/12 pt

This is where the answer is closest to "no", and it depends entirely on the
strength.

`AA_STANDARD` sends glyph level 1 (high coverage) to dark gray and level 2 (low
coverage) to light gray. At reading sizes level 1 is edge pixels. At 8–12 pt it
is a large share of the **stem**, so the whole face lifts off black: it reads
lighter, thinner and washed out rather than smoother. At 8 pt the `«` chevron in
the button hints nearly disappears. That is a regression, plainly, and it is the
strength the device would have used — `SETTINGS.textAntiAliasing` is pinned to
`TEXT_AA_STANDARD` by `normalizeRetiredSettings()` and has no device control.

`AA_CRISP` leaves level 1 painted black by the base pass and lifts only level 2.
Stems keep full weight; only the faintest coverage — the shoulders of round
letters, the steps on diagonals — softens. That is the part that was ever
aliasing.

So the chrome overlay pins `AA_CRISP` (`TextAa::overlayChromeIfEnabled`) rather
than following the setting. `enabled()` is still honoured, so Off is Off.

**Honest assessment of the gain, per size:**

* **12 pt** (headers): the clearest win. The `S` and `g` bowls lose their
  staircase while keeping weight.
* **10 pt** (list rows, colophon body, file viewer): real but small. The change
  is confined to the shoulders of `C`, `o`, `s`, `P`. Worth having; not worth
  paying much for.
* **8 pt** (button hints, page counters): marginal at best. There are barely
  enough pixels for a coverage level to mean anything, and even Crisp lightens
  the chevrons.

If this has to be cut further on device, cut it by size before cutting it by
screen: 12 pt only would keep most of the visible benefit.

**What the simulator cannot tell you.** Whether four levels at 10 pt read as
*smoother* or merely *softer* on a real e-ink panel, through a gray waveform,
at ~150 ppi, is not answerable here. Grayscale can reduce perceived sharpness at
UI sizes. That judgement needs the panel.

## 4. Reproducing the A/B

The check that caught the original no-op, and the one to re-run after any change
to a chrome font or the overlay: render the same screen with Text AA On and Off
and compare the frames. **Byte-identical means the pass did nothing.**

`SETTINGS.textAntiAliasing` cannot be turned off from `settings.json` — it is
pinned. The A/B here was run with a temporary lift, reverted before commit:

```cpp
// src/CrossPointSettings.cpp, in normalizeRetiredSettings()
if (const char* aaOverride = getenv("CROSSPOINT_AA_OVERRIDE")) {
  textAntiAliasing = static_cast<uint8_t>(atoi(aaOverride));
} else {
  textAntiAliasing = TEXT_AA_STANDARD;
}
```

Then, per screen, two runs from an identical restored `fs_/.crosspoint`:

```bash
CROSSPOINT_AA_OVERRIDE=0 CROSSPOINT_SIM_INPUT_SCRIPT="…" \
  CROSSPOINT_SIM_SCREENSHOTS="19000:off.bmp" SDL_VIDEODRIVER=dummy \
  .pio/build/simulator/program
CROSSPOINT_AA_OVERRIDE=1 … CROSSPOINT_SIM_SCREENSHOTS="19000:on.bmp" …
cmp off.bmp on.bmp
```

Result on the shipped configuration:

| screen | outcome |
|---|---|
| colophon | DIFFERS, 30,900 pixel bytes |
| file viewer | DIFFERS, 3,462 pixel bytes |
| settings | IDENTICAL — no call site, as ruled |
| home | IDENTICAL — no call site, as ruled |

The last two are the negative control: they prove the harness discriminates.

## 5. `TextOnlyScope`

`TextAntiAliasingPass.h` states that the overlay callback must draw TEXT ONLY —
in a plane pass every write means "lift this pixel toward white", so a fill,
rule, icon or bitmap redrawn there comes back gray. That was a rule the caller
had to remember. `GfxRenderer::TextOnlyScope` enforces it: while it is
constructed every non-glyph primitive returns immediately, so an existing
`render()` body can be handed to the overlay unchanged instead of maintaining a
second text-only copy that drifts.

Suppressed: lines, rects, rounded rects, arcs, polygons, dithered fills, images,
icons, bitmaps, `invertScreen`, `writeFramebufferRegion`. **Not** suppressed:
`drawText` / `drawCenteredText` / `drawTextRotated90CW`, `clearScreen` (the pass
clears each band itself), and every measurement call — layout must come out
identical to the base pass or the overlay lands on the wrong pixels.

## 6. Things worth knowing before changing any of this

* **`--compress` requires `--2bit`** (`fontconvert.py:808`). The byte-aligned
  group format only exists for 2-bit, so "1-bit compressed" is not an option
  that was passed over — it does not exist.
* **The `_2x` companions must move in lockstep with the 1x cuts.** `drawText`
  hands the companion's `EpdFontData` to the same `renderCharImpl`, which reads
  the depth off the data it was given. A 1-bit companion under a 2-bit 1x face
  renders as noise.
* **Regenerating is verifiable.** Before rebuilding at 2-bit, the 1-bit set was
  rebuilt with the same toolchain and diffed: all twelve byte-identical to the
  committed headers. Do that first; it separates "the flags changed" from "the
  toolchain moved".
* **Prewarm is the mitigation nobody should reach for casually.** Giving chrome
  a `PrewarmScope` would collapse the inflate count and make compression viable,
  but `PrewarmScope` tracks a single font id (`FontCacheManager.cpp:104`) and
  `MAX_PAGE_SLOTS` is 4. Chrome uses three ids in two styles — six slots. That
  is a rework of shared reader infrastructure plus a DRAM page buffer, to buy
  back 148 KB of a resource with 2.25 MB spare.
* **Dark mode.** `feat/dark-mode-antialiasing` remaps which level each pass
  paints (`GlyphAaPlanes.h`). It is not merged here and does not conflict, but
  once chrome is 2-bit its level split applies to chrome too — worth re-reading
  that branch's `AA_CRISP` masks before the two land together.
