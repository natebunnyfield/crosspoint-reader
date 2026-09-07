# Tuning the borrowed Coelacanth italic to the Doves roman

Date: 2026-09-07. Branch: `feature/dtl-romulus-fonts`.
Renderer: `tools/calendar_preview/render_harness` (the device rasterizer).
Sweep tool: `tools/proofsheet/proofsheet.py`, 3x3 cartesian product.

Doves (renamed from `DovesType` 2026-09-07) ships Green's recovery of the Doves Press roman with no italic of its
own. The italic is borrowed from Coelacanth (a Jenson revival), currently at
`scale: 1.030`. The complaint: at that scale the italic sits under the roman on
x-height and reads lighter than it in the same line.

## What was swept

    tools/proofsheet/proofsheet.py --family DovesType \
      --vary styles.italic.scale=1.030,1.042,1.055 \
      --vary styles.italic.synthetic.embolden_em=0.0,0.012,0.020 \
      --vary styles.italic.synthetic.y_ratio=0.35 \
      --mode inline --slots 2,3

`y_ratio` held at 0.35 per the ruling in `docs/synthetic-font-styles.md`: it
keeps hairline growth below stem growth, which is exactly "thicker stroke, thin
stroke as is".

This required an uncommitted change to `build-sd-fonts.py` (~line 1150) so a
style may carry `synthetic:` alongside its own `url:`/`path:` rather than only
via `from:`. Before it, `synth_flags` was populated only for derived styles.

## Measurements

Taken by replicating the converter's rasterize-time embolden exactly
(`fontconvert_sdcard.py:836-865` — `FT_Outline_EmboldenXY` then
`FT_Outline_Translate(-x/2, -y/2)`), because the staged/scaled font file on disk
does NOT carry it. Two earlier attempts were invalid and are recorded so they
are not repeated: measuring `scaled_fonts/*.ttf` directly (embolden happens
later), and ink-density crops of rendered PNGs (line positions shift with scale,
so the crops are not comparable).

x-height and cap ramps are pixel heights at the six reading slots (roman ppem
ramp 16/21/25/29/34/38). Stem and hairline at 83 ppem (40 pt @ 150 dpi).

| build | scale | embolden | x-height ramp | cap ramp | stem | hairline | asc+desc @slot5 |
|---|---|---|---|---|---|---|---|
| Doves roman | 1.140 | — | 8 10 12 14 16 18 | 13 16 19 22 25 29 | 7 | 7 | 43 |
| V1 (shipped) | 1.030 | 0.000 | 7 9 11 13 15 17 | 14 17 20 22 26 29 | 5 | 6 | 43 |
| V2 | 1.030 | 0.012 | 9 11 13 15 17 18 | 14 17 20 22 26 29 | 6 | 7 | 43 |
| V3 | 1.030 | 0.020 | 9 11 13 15 17 18 | 14 17 20 22 26 29 | 7 | 7 | 44 |
| V4 | 1.042 | 0.000 | 7 9 11 13 15 17 | 14 17 20 23 26 29 | 5 | 6 | 44 |
| V5 | 1.042 | 0.012 | 9 11 13 15 17 18 | 14 17 20 23 26 29 | 6 | 7 | 44 |
| V6 | 1.042 | 0.020 | 9 11 13 15 17 18 | 14 17 20 23 26 29 | 6 | 7 | 44 |
| V7 | 1.055 | 0.000 | 8 10 11 13 15 17 | 14 17 20 23 26 31 | 6 | 6 | 46 |
| V8 | 1.055 | 0.012 | 9 11 13 15 17 19 | 14 17 20 23 26 31 | 6 | 7 | 46 |
| V9 | 1.055 | 0.020 | 9 11 13 15 17 19 | 14 17 20 23 26 31 | 6 | 8 | 46 |

## Findings

1. **`embolden_em` buys x-height cheaply.** A swelling outline grows upward too,
   so 0.012 em adds ~1 px of x-height at every slot while leaving the cap ramp
   untouched. That is the lever for "bigger x-height without taller overall" —
   `scale` cannot do it, because scale moves caps and descenders with it.
2. **`scale` past 1.042 overshoots.** At 1.055 the cap ramp reaches 31 px
   against the roman's 29 and the descender 15 against 12: a taller italic, not
   a better-matched one. This is the failure mode the request named.
3. **`embolden_em: 0.012, y_ratio: 0.35` matches the roman's ink.** Stem goes
   5 -> 6 px (roman 7), hairline 6 -> 7 px (roman 7). At 0.020 the stem reaches
   the roman's 7 at scale 1.030 but the hairline starts overshooting at 1.055
   (8 vs 7) — the thin stroke stops being thin, which the request ruled out.

## Levers checked and NOT used (negative results)

- `word_space_em` — real and supported (Junicode ships -0.07). The inline
  specimen's word-space proof line (`render_harness.cpp:565`) sets the same
  sentence roman then italic on consecutive lines; the italic's word space
  already reads even against the roman. No change needed.
- `tracking_em` — supported by the recipe, unused by any shipped family. Nothing
  in the specimen asks for it.
- `slant_deg` — for synthesizing an italic from a roman. Coelacanth Italic is a
  real drawn italic; shearing it would be wrong.
- `metrics:` — already unified across the family at 998/-402/0. Changing it moves
  leading, not the italic's fit, and would desynchronize the roman.

## Artifact

Nine builds rendered inline with the roman, flippable in place:
https://claude.ai/code/artifact/8f85764e-891d-4821-ad95-804e82a879ef
The artifact is delivery; this file is the record.

## Standing rulings

- (pending owner's pick from the artifact)

## Round two — the overshoot call was wrong

The owner read the round-one sheet and ruled: V8 (`scale 1.055`, `embolden_em
0.012`) is closest but the italic is STILL short, and the "overshoot past 1.042"
finding above does not hold. Taken at face value. Finding 2 is retracted: the
cap-ramp numbers in it are real, but an italic's caps standing above the roman's
is normal and is not what the eye reads — x-height is. The real bound is overall
height (ascender + descender), because that is what collides with the line above
when an italic phrase falls in running text.

Second sweep, same tool, `--family Doves`:

    --vary styles.italic.scale=1.055,1.075,1.095,1.115
    --vary styles.italic.synthetic.embolden_em=0.012,0.020
    --vary styles.italic.synthetic.y_ratio=0.35

| build | scale | embolden | x-height ramp | cap ramp | stem | hairline | asc+desc |
|---|---|---|---|---|---|---|---|
| Doves roman | 1.140 | — | 8 10 12 14 16 18 | 13 16 19 22 25 29 | 7 | 7 | 43 |
| A1 (round one's pick) | 1.055 | 0.012 | 9 11 13 15 17 19 | 14 17 20 23 26 31 | 6 | 7 | 46 |
| A2 | 1.055 | 0.020 | 9 11 13 15 17 19 | 14 17 20 23 26 31 | 6 | 8 | 46 |
| B1 | 1.075 | 0.012 | 9 11 13 15 17 19 | 14 18 21 23 27 32 | 6 | 7 | 47 |
| B2 | 1.075 | 0.020 | 9 11 13 15 17 19 | 14 18 21 23 27 32 | 7 | 8 | 47 |
| C1 | 1.095 | 0.012 | 9 11 13 15 18 19 | 14 18 21 24 27 32 | 7 | 7 | 48 |
| C2 | 1.095 | 0.020 | 9 11 13 15 18 19 | 14 18 21 24 27 32 | 7 | 8 | 48 |
| D1 | 1.115 | 0.012 | 9 12 14 16 18 19 | 14 18 21 24 28 33 | 6 | 8 | 49 |
| D2 | 1.115 | 0.020 | 9 12 14 16 18 19 | 14 18 21 24 28 33 | 7 | 8 | 49 |

**The x-height ramp stalls.** 1.055, 1.075 and 1.095 all round to the same six
pixels; 1.115 is the first step that moves it. So a build that reads short at
1.055 reads short at 1.095 too — the next real change is at the top of the
sweep, and a mid-value is wasted.

Artifact: https://claude.ai/code/artifact/fb2f2c3e-75da-4add-9555-8aef86ab8f3c

## Rename

`DovesType` -> `Doves`, family id and display name both, 2026-09-07 on the
owner's instruction. Touched: `sd-fonts.yaml` (`installed_families:` + the
family's `name:`, and a stale description still crediting a Junicode italic),
`src/FontDisplayNames.h`, `test/settings_display_order/SettingDisplayOrderTest.cpp`.
The source file on disk is still `local_fonts/DovesType-Text.ttf` — that is
Green's own filename and is not ours to rename.

## Ruling and what shipped

**`scale: 1.115`, no `synthetic:` on the italic** (owner, 2026-09-07: "commit D1
scale but 0 for embolden"). Both ink options in the second sweep were declined:
Coelacanth's lighter, more calligraphic color against the Doves roman is the
reason this italic was chosen over Junicode's, and emboldening it argues with
that choice. `bolditalic` still emboldens from it at 0.036 — a bold has to.

That means the `build-sd-fonts.py` change this investigation needed (allowing
`synthetic:` on a style that has its own `url:`/`path:` rather than only via
`from:`) is NOT load-bearing for the shipped recipe. It is kept anyway, because
without it `proofsheet.py --vary styles.italic.synthetic.*` silently builds
nine identical variants — the flags are computed, the sweep runs, and every
output is the same font. That is exactly the silent failure a gate exists for,
so `_synth_flags` now covers both cases.

### Verified against the BUILT files, not the sweep's prediction

Read back off `output/Doves/Doves_*.cpfont` style TOC entries after the build:

| slot | 8 | 10 | 12 | 14 | 16 | 18 |
|---|---|---|---|---|---|---|
| target leading | 23 | 28 | 34 | 40 | 46 | 51 |
| regular advanceY | 23 | 29 | 35 | 41 | 47 | 52 |
| italic advanceY | 23 | 29 | 35 | 41 | 47 | 53 |

Leading drift 5 for the roman, 6 for the italic — **the feared jump did not
happen.** The 1.030-era note predicted the drift would double past 1.040 because
the shared ink floor crosses a pixel; it does not, because `metrics:` is patched
to 998/-402 and that declared span, not the italic's ink, is what sets the line.
The old table was measured before the metrics patch existed and its warning no
longer describes this recipe.

One real cost, recorded so it is not discovered later: **at slot 18 the italic's
line is 53 px against the roman's 52.** A paragraph set wholly in italic runs one
pixel looser per line at the largest size only. Ligature count 5 and kern-left
class count 306 are unchanged from the 1.030 build, so the scale change cost no
coverage.

## Round three — the 5x5 matrix, all six sizes

Owner, 2026-09-07: "make another 5x5 matrix of scale and embolden with current
settings in middle. this is to see if slight tweaking would help at all xxs to
xl sizes." Scale +/- 0.030 in steps of 0.015, embolden +/- 0.010 in steps of
0.005 — **embolden goes NEGATIVE**, which `FT_Outline_EmboldenXY` accepts and
which thins the outline instead of swelling it. 25 families, rendered at all six
slots (52 s wall clock for the whole sweep).

Two numbers per cell, both measured at every slot rather than at one reference
ppem: how many of the six sizes land the italic's x-height EXACTLY on the
roman's, and the italic's ink as a percent of the roman's (measured coverage of
a rendered `n`, same size, same rasterizer).

| scale \ embolden | -0.010 | -0.005 | +0.000 | +0.005 | +0.010 |
|---|---|---|---|---|---|
| 1.085 | 67% · 6/6 | 72% · 6/6 | 76% · 6/6 | 81% · 0/6 | 86% · 0/6 |
| 1.100 | 69% · 6/6 | 73% · 6/6 | 78% · 6/6 | 83% · 0/6 | 88% · 0/6 |
| **1.115** | 71% · 6/6 | 76% · 6/6 | **81% · 6/6 (shipped)** | 85% · 0/6 | 91% · 0/6 |
| 1.130 | 74% · 5/6 | 79% · 5/6 | 84% · 5/6 | 88% · 0/6 | 94% · 0/6 |
| 1.145 | 76% · 5/6 | 81% · 5/6 | 86% · 5/6 | 90% · 0/6 | 96% · 0/6 |

### Three findings

1. **Scale barely moves the x-height in this band.** 1.085, 1.100 and 1.115 all
   land the x-height exactly on the roman at ALL SIX sizes; 1.130 and 1.145 miss
   at one size only. A 5.5% span of scale and the pixel grid rounds nearly all of
   it away. Scale is not the lever left to pull — which also means the shipped
   1.115 was not a lucky value, it is the middle of a wide flat.
2. **Embolden costs a whole pixel of x-height, instantly, and there is no
   "slight" setting.** Every positive-embolden cell drops to 0/6: a swelling
   outline grows upward too, and at 16-38 ppem that is a full pixel at every
   size. +0.005 already breaks the match that +0.000 holds. This retires the
   idea from rounds one and two that a small embolden is a cheap way to gain
   x-height — it is, but it gains a WHOLE pixel and cannot gain less.
3. **The remaining gap is ink, ~19%, and it is worst at the smallest size.**
   The shipped build carries 81% of the roman's ink on average and only 74% at
   xxs (8 pt). That is the lightness visible on the page. The only cells that
   close it without breaking the x-height are pure scale increases at zero
   embolden: 1.130 reaches 84%, 1.145 reaches 86% — each costing one size's
   x-height match and nothing else.

Raw ink and stroke darkness pull opposite ways once scale moves: 1.145/+0.010
carries 96% of the roman's ink but only 71% of its darkness per unit area,
because the ink is spread over a bigger glyph. Both are on the artifact.

Artifact: https://claude.ai/code/artifact/adef4939-b023-453d-8df2-4b8698665ad5

**No change committed from this round** — it is a survey of the neighborhood
around the shipped value, and the shipped value is still the only cell that is
6/6 on x-height at its ink level.

## Round four — V15 picked, and the baseline misalignment traced and fixed

Owner off the 5x5 sheet, 2026-09-07: "V15 wins. can the vertical baseline
misalignment be fixed?" V15 is `scale 1.115`, `embolden_em +0.010`,
`y_ratio 0.35` — so the embolden is back, superseding the round-two ruling that
set it to zero. It costs a whole pixel of x-height at every size; that is the
smallest step the lever has (finding 2 above).

### The misalignment is real, and it is two separate faults

Measured off the built `.cpfont` glyph records — `top - height` per glyph, which
is 0 for a letter sitting exactly on the baseline:

**Fault 1, the italic: the two faces disagree about where the baseline is.**
A face draws its flat-bottomed letters a couple of units below y=0 so the
rasterizer has something to round; how far is the designer's choice. yMin
histograms over 50 sampled glyphs:

| face | flat-bottom mode | count |
|---|---|---|
| Doves roman | **-2**/1000 em | 24 of 50 |
| Coelacanth Italic | **-10**/1000 em | 19 of 50 |

Eight thousandths of an em, which at these ppems is the difference between
rounding into the baseline row and rounding one row below it. Every flat-bottomed
italic glyph sat one row low at 8-16 pt, and TWO rows low at 18 pt.

**Fault 2, the synthetic bold: the embolden's own centering translate.**
Emboldening grows the outline both ways and is then re-centered by translating
back half the growth — including half the VERTICAL growth, which walks the glyph
down off the baseline. Every flat-bottomed bold glyph was one row low at all six
sizes. **This is not specific to Doves**: every synthetic bold in `sd-fonts.yaml`
is built the same way and is presumably a row low the same way. Only Doves is
corrected, because only Doves was measured. Fixing the rest wants its own
measuring pass, not a copied constant.

### 18 pt needed its own number, and here is why

Not a fudge. Reading the outline's yMin in pixels before rounding, at
`embolden 0.010`:

| size | italic yMin (px) |
|---|---|
| 8 pt | -0.031 |
| 12 pt | -0.047 |
| **18 pt** | **-1.062** |

At ppem 38 the CFF hinter snaps Coelacanth's baseline zone a whole pixel down.
It is a discontinuity in the hinter, so no single em-relative number covers it.
Forcing the autohinter and disabling hinting were both tried and both came out
worse (23 and 43 misaligned glyph/size pairs against 6). Hence a per-size
override.

### What was added

`baseline_shift_em` in the `synthetic:` block, applied as an
`FT_Outline_Translate` in y AFTER the embolden and shear, so the number in the
recipe is the shift that actually lands. `bitmap_top` comes out of
`FT_Render_Glyph`, so it follows the outline and needs no correction.

Any synthetic key may now also be written `key@<size>` to override that one
point size. `synth_params_for_size()` already took the size, so this cost no
plumbing; `build-sd-fonts.py` serializes the `synthetic:` dict generically and
needed no change at all.

Doves' values, all measured rather than fitted:

| style | baseline_shift_em | @18 | why |
|---|---|---|---|
| bold | 0.0075 | — | exactly half the vertical growth: 0.043 x 0.35 / 2 |
| italic | 0.010 | 0.036 | (10/897 - 2/1796) = 0.01003, the arithmetic of the two conventions |
| bolditalic | 0.016 | 0.040 | the italic's fault plus its own heavier embolden |

The italic's 0.010 sits mid-plateau: anything from 0.008 to 0.026 gives the same
answer at 8-16 pt. 18 pt is clean from 0.033 upward, so 0.036 is mid-window too.

### Verified on the built files

Flat-bottomed glyphs (`x n o l H b E I`), italic and bolditalic, all six sizes:
**0 of 96 off the roman's baseline row**, from 42 of 48 before. Bold: 0 of 48,
from 48 of 48. The glyphs that still differ are `p q` (Coelacanth's descenders
are genuinely deeper, -336/1000 em against Doves' -278) and `d u` (design, the
italic simply does not dip where the roman does) — descender depth and drawing,
not alignment.

15/15 in `test/settings_display_order` still pass; Coelacanth rebuilds unchanged,
so the converter change is inert for recipes that do not use the new key.

## Round five — every synthetic bold in the file

Owner, 2026-09-07: "redo synthetic bolds, show me before and after." The fault
found in Doves' bold (round four, fault 2) was never Doves-specific; this is the
pass that measures and corrects all of them.

### The measurement that isolates it

Comparing a synthetic style against the family's *regular* mixes the fault with
real design differences — a face's own italic legitimately sits where it likes.
The right comparison is **each synthetic style against the style it was cut
from**, in the same built file. Embolden is symmetric and re-centered and shear
leaves y alone, so every glyph should keep its baseline row. Anything that moves
is the fault.

`GoudyBookletter1911`'s italic is the control: it is `from: regular` with
`slant_deg: 11` and NO embolden. It measured **0 of 2372** off, before and
after. The shear is innocent; the embolden's centering translate is the cause.

### Before and after

Glyphs whose baseline row differs from their source style's, across every size:

| family | style | cut from | before | after |
|---|---|---|---|---|
| Coelacanth | bolditalic | italic | 11006 / 17760 (61%) | 740 (4%) |
| InknutJunicode | bolditalic | italic | 12148 / 16188 (75%) | 655 (4%) |
| LutetiaNova | bold | regular | 11984 / 15876 (75%) | 357 (2%) |
| LutetiaNova | bolditalic | italic | 11399 / 15876 (71%) | 340 (2%) |
| CaledoniaCC | bold | regular | 1215 / 2376 (51%) | 13 (0%) |
| CaledoniaCC | bolditalic | italic | 1246 / 2376 (52%) | 0 |
| GoldenCockerel | bold | regular | 12444 / 15876 (78%) | 569 (3%) |
| GoldenCockerel | bolditalic | italic | 12436 / 15876 (78%) | 629 (3%) |
| GoudyBookletter1911 | bold | regular | 1283 / 2372 (54%) | 6 (0%) |
| GoudyBookletter1911 | *italic (control)* | regular | **0** | **0** |
| GoudyBookletter1911 | bolditalic | regular | 1283 / 2372 (54%) | 6 (0%) |
| LibreCaslonText | bolditalic | italic | 1593 / 2376 (67%) | 25 (1%) |
| Rosarivo | bold | regular | 588 / 2356 (24%) | 4 (0%) |
| Rosarivo | bolditalic | italic | 635 / 2356 (26%) | 4 (0%) |
| Doves | bold / bolditalic | regular / italic | (fixed in round four) | 329 / 316 |
| **TOTAL** | | | **79260 / 116408 — 68%** | **3993 / 148358 — 2%** |

### The value is arithmetic, not fitting

`baseline_shift_em = embolden_em * y_ratio / 2` — exactly half the vertical
growth the centering translate gives back. Note `y_ratio` DEFAULTS TO 0.5 when
the recipe omits it, which is why InknutJunicode and GoudyBookletter1911 take
larger shifts than their embolden alone suggests.

A sweep of +-0.002 and +0.004 around the arithmetic value was run on the five
families still above 1%, and it is mostly a negative result: LutetiaNova and
Doves got WORSE at every offset, InknutJunicode improved 3% (noise). Only
Coelacanth's bolditalic gained enough to take — 1148 to 740, a third — so it
carries 0.0085 against an arithmetic 0.0065. **Do not tune the others**; the
residual 2% is glyphs whose rounded bottoms grow across a pixel boundary under
the embolden itself, and chasing it is curve-fitting to rounding noise.

### Two things found on the way, both worth knowing

1. **A `str.replace` of mine had cloned Doves' round-four edit into LutetiaNova's
   bolditalic and Rosarivo's bold** — identical one-liners elsewhere in the file.
   Both carried Doves' numbers and Doves' comment text. Reverted before this
   pass measured anything, so the before column above is honest.
2. **`InknutJunicode`'s bolditalic got heavier, and that is correct.** Its
   `synthetic: {embolden_em: 0.042}` sits on a style with its OWN source, so it
   was silently ignored until the build fix in `0cdfb922f`. Commit `15546fe75`
   ("give InknutJunicode a bold italic that is actually bold", 2026-08-13) added
   it deliberately, having measured that wght 700 alone produced no visible
   bold — so the feature it intended had never once worked. It works now: at
   14 pt the bolditalic's `l` is 10 px against the italic's 8 and the bold's 9.
   This is a real, visible weight change to a shipped family, arrived at by
   fixing a bug rather than by choosing it.
