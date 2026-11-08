# Glyph fast path: pixel-identity proof (2026-11-08)

Upstream `04ca1d53c` ("perf: reduce per-pixel overhead in font rendering")
replaces a `drawPixel()` call per glyph pixel with a per-glyph rasterizer
(`lib/GfxRenderer/GlyphBitmap.h`, `GfxRenderer::drawGlyphBitmap`). It was ported
by hand on branch `b5-render` on top of `3555ff556`'s port (`f98d26f0e`), because
this fork's glyph path had diverged (AA masks per strength and dark mode,
Rotated90CCW, hi-res deviceSpace glyphs, band culling, the dirty rect). The port
was not committed until the frames were shown identical. This is the record.

## Result

| what | compared | result |
|---|---|---|
| host differential, scale 1 (`GlyphFastPathTest`) | 46,872 renders x 2 paths, framebuffer + strip scratch + out-of-range count | all byte-identical; fast path drew 122,280 glyphs |
| host differential, scale 2 + hi-res companions (`GlyphFastPathTest2x`) | same matrix | all byte-identical; 103,728 glyphs |
| dirty rect, same runs | fast rect must contain the per-pixel rect | always contains it; equal in 41,757 / 46,872 at scale 1 (light BW: 7,011 / 7,812), 41,836 / 46,872 at scale 2 |
| simulator, 4 sessions x 13 screenshots | commit-1 build vs commit-2 build | 52/52 screenshots byte-identical (composed, gray levels included) |
| simulator, same sessions | BW frame hash at every refresh (14 per session), present windows, gray-level histograms (8 composes per session), activity sequence | identical in all 4 |
| simulator control | commit-1 build against itself | identical (only after pinning the grain seed, see below) |
| `tools/latency/gate.sh` | commit-2 build | `lag gate OK: 14 turns, 2 Home entries, 2 Settings moves` (commit-1: the same line) |

The host matrix is 3 render modes x 3 AA strengths x dark-mode AA on/off x 3
faces (Reader 14, compressed 2-bit; chrome 10, uncompressed 2-bit; a synthetic
1-bit face) x 3 rotations (upright, Rotated90CW, Rotated90CCW) x black/white x
4 strip configurations (none and three bands) x 6 x 6 positions, including
text that starts left of or above the panel and text that runs off its right or
bottom edge; plus bold, italic, bold italic and SUP once per face
and mode. The text carries ligatures, kern pairs, descenders and U+2603, which
no built-in carries (the notdef box, still per-pixel).

The four simulator sessions: light/built-in Libre Franklin, light/SD Edgar,
dark/built-in, dark/SD. Each boots into a reader on a generated book, turns six
pages (the second turn is a T-021 rotated table page, drawn with
`drawTextRotated90CCW`), goes Back to Home, moves, Confirms into Recent Books,
moves, Backs out twice. Text antialiasing ran its gray pass (8 gray composes per
session, all compared).

### Why the dirty rect is not always equal

The per-pixel loops marked each pixel they painted; the fast path marks the
glyph's whole cell once. Some Reader 14 bitmaps carry a blank last row or
column, so at the extreme edge of a run of text the rect can be one pixel larger
(seen: `slow=7,3-33,524` vs `fast=7,3-34,525`). GfxRenderer.h's dirty-rect
contract treats over-reporting as safe; it can only widen a windowed present. In
the dark-mode BW base pass and the gray passes the painted subset is smaller
than the cell, which accounts for the rest of the 5,115 unequal cases at
scale 1. The
simulator sessions' present windows were identical between builds.

## Speed (host only -- no device number exists)

* `GlyphFastPathTest.HostTimingOfAFullPageOfText`: 200 full pages of Reader 14,
  BW, every glyph on the panel, arm64 Mac, -O2. Per-pixel 62.6-75.5 ms, fast
  path 44.3-48.2 ms (a first, cold run: 124.3 vs 85.4): **1.41x-1.68x** across
  five runs. This is the whole
  `drawText` (cursor walk, kerning, glyph lookup) and not just rasterization.
* Simulator page-turn latency (press to refresh, `tools/latency/run_scenario.sh`
  on `gate.sh`'s card, 20 turns x 2 runs per build, section-build outlier
  dropped): median **1.20 ms -> 0.90 ms**.
* Device: not measured. The C3 is a 160 MHz RISC-V at -Os, where the removed
  per-pixel work (call, rotate, four bound compares, four dirty compares, band
  test, index multiply) is a larger share; the ratio there is unknown.
* Size: X4 `default` flash 4,854,631 -> 4,856,171 B (+1,540); static RAM 58,724
  B both.

## Method

Host differential (re-run with the suite):

```bash
cmake -S test -B /tmp/b5r-tests && cmake --build /tmp/b5r-tests -j8 --target GlyphFastPathTest GlyphFastPathTest2x
/tmp/b5r-tests/glyph_bitmap/GlyphFastPathTest 2>/dev/null | grep glyph-fastpath
```

`GFX_GLYPH_FASTPATH_TOGGLE` (defined only for those two targets) gives
`GfxRenderer` a static switch that declines every glyph, so one binary renders
both ways; `GFX_BOUNDS_COUNTER` makes the out-of-range count comparable.

Simulator: build each commit in its own tree state with
`PLATFORMIO_BUILD_DIR=.pio/build-sim PLATFORMIO_BUILD_FLAGS="-O2 -g" pio run -e simulator`,
copy `.pio/build-sim/simulator/program` aside, and run the session below with
each binary on a fresh card. **`CROSSPOINT_SIM_GRAIN_SEED` must be pinned**: the
paper grain is seeded from `std::random_device` otherwise
(`crosspoint-simulator/src/HalDisplay.cpp:2888-2901`), and the first control
run (commit 1 against itself) differed in every screenshot by up to 37 levels
over the whole frame while its frame hashes matched. Trace lines also carry an
SDL timestamp and pid that must be stripped before diffing.

The book (a four-column table that cannot stand upright, so it becomes a rotated
page; six columns of the same words fell to the key block instead):

```python
import sys, zipfile
prose = ("The harbour lights came on one by one while the ferry idled at the quay. "
         "Nobody aboard seemed in a hurry; a <i>cyclist</i> leaned on the rail, a child "
         "counted gulls aloud, and the <b>engineer</b> wiped his hands for the third time. "
         "Office efficiency, affluent waffles and fjord-side flights: ligatures fi ffl fl. ")
para = lambda n: "".join(f"<p>{prose}</p>" for _ in range(n))
head = ["Observatory", "Temperature", "Precipitation", "Visibility"]
rows = [["Aberdeenshire", "Mildly", "Intermittent", "Moderate"],
        ["Barcelonetta", "Pleasantly", "Nonexistent", "Excellent"],
        ["Copenhagener", "Bracingly", "Persistent", "Restricted"],
        ["Dubrovnikian", "Agreeably", "Occasional", "Unlimited"]]
table = ("<table><tr>" + "".join(f"<th>{h}</th>" for h in head) + "</tr>" +
         "".join("<tr>" + "".join(f"<td>{c}</td>" for c in r) + "</tr>" for r in rows) + "</table>")
chapters = [para(3) + table + para(4), para(12)]
# ...written as a minimal EPUB 3: mimetype (stored), META-INF/container.xml,
# OEBPS/content.opf (one item + itemref per chapter), OEBPS/nav.xhtml, and
# OEBPS/c<i>.xhtml = <h1>Chapter i</h1> + the body above.
```

The session (zsh; `$prog` the binary, `$out` a fresh directory, `$dark` 0/1,
`$sdf` an SD family under `fs_/fonts` or empty):

```bash
mkdir -p $out/card/books $out/card/.crosspoint $out/shots
cp wide.epub $out/card/books/; [[ -n $sdf ]] && mkdir -p $out/card/fonts && cp -R fs_/fonts/$sdf $out/card/fonts/
print -r -- '{"openEpubPath":"/books/wide.epub","showBootScreen":false}' > $out/card/.crosspoint/state.json
print -r -- '{"books":[{"path":"/books/wide.epub","title":"Wide","author":"","coverBmpPath":""}]}' > $out/card/.crosspoint/recent.json
print -r -- "{\"darkMode\":$dark,\"sdFontFamilyName\":\"$sdf\",\"fontSizeSlot\":3}" > $out/card/.crosspoint/settings.json
print -r -- "{ \"darkMode\": $dark }" > $out/settings.json
s=""; shots="4000:$out/shots/00.bmp;"; t=4000
for i in 1 2 3 4 5 6; do t=$((t+500)); s="${s}${t}:RIGHT:200;"; t=$((t+2500)); shots="${shots}${t}:$out/shots/0$i.bmp;"; done
for k in BACK DOWN CONFIRM DOWN BACK BACK; do t=$((t+500)); s="${s}${t}:$k:200;"; t=$((t+2500)); shots="${shots}${t}:$out/shots/$k-$t.bmp;"; done
s="${s}$((t+1000)):QUIT"
env SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CROSSPOINT_SIM_TUBE_OFF=1 CROSSPOINT_SIM_GRAIN_SEED=20261108 \
  CROSSPOINT_SIM_SD=$out/card CROSSPOINT_SIM_LOG_LATENCY=1 CROSSPOINT_SIM_LOG_AA=1 CROSSPOINT_SIM_LOG_PRESENTS=1 \
  CROSSPOINT_SIM_INPUT_SCRIPT="$s" CROSSPOINT_SIM_SCREENSHOTS="$shots" timeout 300 $prog > $out/run.log 2>&1
grep -E 'refresh fb=|\[window\] displayWindow|\[aa\]|Entering activity|Present windowed|Present skipped' $out/run.log \
  | sed -E 's/^[0-9-]+ [0-9:.]+ program[^ ]* //; s/^.*\[lat\] [0-9]+ /[lat] /; s/ opens=.*//; s/^(\[[0-9:]+\] )+//; s/ -> [0-9]+% of panel.*//' > $out/trace.txt
```

Then `cmp` every screenshot and `diff` the two `trace.txt` files.

## Limits

* Host and simulator only, both RENDER_SCALE 1 except `GlyphFastPathTest2x`.
  No device run: the device build compiles the same `renderCharImpl` and
  `drawGlyphBitmap`, and the only device-only difference is the compiler.
* The simulator sessions reach Rotated90CCW through the table page; Rotated90CW
  (side-button labels) is covered by the host matrix, not by a session.
* Only one windowed present per session (the boot overlay); the page turns are
  whole-frame presents after `clearScreen`, so the sessions say little about the
  dirty rect. The host test's containment check is the dirty-rect evidence.
* Declined, by design, and so unchanged: glyphs not wholly on the panel, logical
  glyphs at a runtime render scale above 1, the notdef box, SUP/SUB.
