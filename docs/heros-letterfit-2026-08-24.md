# TeX Gyre Heros letterfit — the "treatment" report

**Date:** 2026-08-24
**Commit surveyed:** `238ff6725` (main), fonts as installed in `fs_/fonts/`
**Owner report, verbatim:** *"check kerning of tex gyre heros. 'treatment' has 't's that are very close"*

**Verdict: the report is correct and nothing in our pipeline is broken.**
The unevenness is TeX Gyre Heros' own metrics meeting the pixel grid at reading
size. Three candidate causes were each tested and each ruled out by measurement.
No code was changed. One regression test was added to cover an optimization that
had shipped untested.

Everything below is measured against the shipped `.cpfont` files through the
real `GfxRenderer` / `SdCardFont` / `EpdFont` path, not reasoned about.

---

## 1. The measurement

Rendered "treatment" at reading size 16, regular, through the firmware's own
antialiased render (BW base + LSB/MSB grayscale planes, `AA_STANDARD`), then
counted the **white gutter** between adjacent letters — columns whose ink
coverage is exactly zero.

| family | tr | re | ea | at | tm | me | en | nt | spread |
|---|---|---|---|---|---|---|---|---|---|
| **TeXGyreHeros** | 2 | **1** | 3 | 2 | 2 | **4** | **4** | **4** | **1–4 px** |
| LibreFranklin | 3 | 2 | 2 | 3 | 3 | 3 | 3 | 3 | 2–3 px |
| LibrisADF | 2 | 2 | 2 | 3 | 2 | 3 | 3 | 3 | 2–3 px |
| TeXGyreSchola | 1 | 2 | — | 1 | 1 | 2 | 2 | — | 1–2 px (two junctions touch) |

Heros is the only installed family whose gutters vary by more than 1 px inside a
single word. Its left half (`treat`, mean 2.0 px) is set half as open as its
right half (`ment`, mean 4.0 px), and the break falls on the `m`. The middle `t`
sits between two 2 px gutters immediately before a run of three 4 px gutters,
which is exactly the letter the report names.

Kern values actually applied for this word in Heros: `re` = −5 (4.4 fixed-point,
−0.31 px). **Every other pair is 0.** That is not a loss — see §3.

## 2. Design-space letterfit (the cause)

Sidebearing sums per junction, in 1/1000 em so families compare directly,
read from the source outlines with fontTools:

| family | xh/em | tr | re | ea | **at** | tm | me | en | nt | max/min |
|---|---|---|---|---|---|---|---|---|---|---|
| **TeXGyreHeros** | 524 | 94 | 52 | 85 | **35** | 95 | 113 | 113 | 83 | **3.2x** |
| TeXGyreSchola | 466 | 36 | 44 | 79 | 31 | 38 | 56 | 62 | 36 | 2.5x |
| LibrisADF | 475 | 79 | 49 | 49 | 72 | 78 | 91 | 83 | 75 | 1.9x |
| LibreFranklin | 530 | 125 | 82 | 97 | 107 | 125 | 140 | 140 | 110 | 1.7x |

Two separate effects stack, and both land on `t`:

**(a) Heros' `a`→`t` junction is genuinely tight in the outlines.** 35/1000 em,
against Libre Franklin's 107 for the same pair — three times tighter, and the
tightest junction in the whole table apart from Schola, which is uniformly tight
everywhere and therefore reads as even. It comes from `t`'s left sidebearing
(14/1000 em, the crossbar tip) meeting `a`'s right sidebearing (21/1000 em, the
terminal). This is Helvetica's fit; TeX Gyre Heros reproduces URW Nimbus Sans
faithfully.

**(b) At 16 px the `t` bitmap eats its own right sidebearing.** Rendered right
sidebearings in the shipped build, in pixels:

| glyph | t | r | e | a | m | n |
|---|---|---|---|---|---|---|
| advance (px) | 9.25 | 11.13 | 18.50 | 18.50 | 27.75 | 18.50 |
| rendered RSB (px) | **0.25** | **0.13** | 1.50 | 0.50 | 2.75 | 2.50 |

`t`'s advance is 0.278 em = 9.27 px and its ink rasterises 8 px wide starting at
column 1, so 0.25 px of sidebearing survives. `m` and `n` keep 2.5–2.75 px. That
is the whole 2-vs-4 split: `tr` and `tm` render a pixel tighter than their design
gap warrants, and `nt` renders a pixel looser. Every bitmap font loses up to a
pixel per side to the glyph bounding box; it lands unluckily here because Heros'
`t` advance sits almost exactly on a quarter-pixel.

## 3. What was ruled out, and how

Recorded so none of these is re-proposed.

### Lead 1 — the ASCII kern-class shortcut (`ca4cf1056`) — CLEAN

That commit replaced two binary searches per character pair in
`SdCardFont::getMeasureKern` with a 256-byte direct-mapped table
(`kernClassAscii`), measured at 19% of a paginate. It is a pure memoisation of
`miniLookupKernClass`, the tables it reads are written once at kern-table load
and never mutated afterwards, and it is freed with them
(`SdCardFont.cpp:157`, `:309-317`, `:648-670`).

Verified exhaustively rather than by inspection: `getMeasureKern` (shortcut +
measure-kern rows) compared against `EpdFont::getKerning` (per-page mini class
tables + mini matrix) — two independent lookups over the same file data — for
every printable ASCII pair in every installed family.

**0 mismatches in 79,524 pair comparisons across 9 families.**

Heros' resident ASCII kern count is 337, byte-identical to what fontTools reads
straight out of `texgyreheros-regular.otf`. Nothing was dropped or corrupted.

This is now `SdKernShortcut.AsciiShortcutAgreesWithTheUnmemoisedLookupInEveryFamily`
in `test/sd_kern_measure/`. It was validated failing-first: injecting a
one-character class-table swap into the shortcut makes it fail on Coelacanth and
Edgar with named pairs. Runtime 21 ms.

### Lead 2 — `force_autohint: true` — RULED OUT, and removing it is worse

Rebuilt Heros with the flag off and re-measured the same gutters:

| build | tr | re | ea | at | tm | me | en | nt | spread |
|---|---|---|---|---|---|---|---|---|---|
| shipped (`force_autohint: true`) | 2 | 1 | 3 | 2 | 2 | 4 | 4 | 4 | 1–4 |
| rebuilt (`force_autohint: false`) | 2 | 1 | 3 | **1** | 2 | 3 | 4 | 3 | 1–4 |

Same 1–4 spread, and `at` gets *tighter* (2 → 1 px). Advances are byte-identical
in both builds (142 px for the word) — the auto-hinter moves ink, not metrics.
The flag is load-bearing for the x-height ramp, as its recipe comment says, and
removing it would cost that ramp without fixing this.

### Lead 3 — the measure-vs-paint split (`punctuation-kerning-audit-2026-08-22` P0) — CLEAN for Heros

`GfxRenderer::getTextAdvanceX("treatment")` = 142 px, equal to `drawText`'s own
cursor walk over the same advances and kerns. The measure-kern rows are resident
and carry the same values the mini matrix does (§3 lead 1, exhaustively). The
2026-08-22 fix covers Heros.

### Did the converter drop Heros' kern data? — NO

`texgyreheros-regular.otf` has GPOS and no legacy `kern` table. Its GPOS kern
feature carries **337** ASCII pairs, 87 of them lowercase–lowercase. Of the eight
junctions in "treatment" the designer kerned exactly one, `re` = −5, and that is
the one value the `.cpfont` carries. `tr`, `ea`, `at`, `tm`, `me`, `en`, `nt`
have **no pair in the source font**. TeX Gyre Schola is the same: 356 ASCII
pairs, none of them for any junction in this word. Sparse lowercase kerning is
normal — designers fit lowercase by sidebearing and reserve kerning for
diagonals, round/flat clashes and punctuation.

### Would optical kerning fix it? — NO, and it fails its own gate

`lib/EpdFont/scripts/optical_kern.py` exists for faces whose kerning is
genuinely sparse. On Heros:

- `--validate`: **sign agreement 65%, against its own stated `want > 70%`.**
  (Pearson r +0.612 passes; the reference pairs `nn`/`oo`/`HH`/`no`/`on`/`mn`
  predict ~0 correctly.)
- Synthesis: **1527 of 3576 pairs (43%) hit the cap**, which the script's own
  docstring calls a failure meaning "the metric is wrong", not something to
  clamp past. The run aborts.
- It emitted **3574 negative and 2 positive** corrections, and **nothing at all**
  for any junction in "treatment" — it suppressed 1306 positive kerns by design.

It only ever tightens. It structurally cannot address an over-tight junction, so
it is the wrong instrument here even before its gate fails.

## 4. Options, if the owner wants this changed

Not built — this is an appearance choice about a shipped face, and reversing a
shipped typographic decision costs more than asking does.

1. **Accept it.** Heros is Helvetica; a tight `at` is the face's character, and
   at 16 px x-height the pixel grid exaggerates it. Costs nothing.
2. **Hand-author a few loosening pairs.** `fontconvert_sdcard.py` reads a legacy
   `kern` table and lets GPOS win per pair, so pairs the designer left alone can
   be added without overriding a single designer ruling — the same seam
   `optical_kern.py` uses, driven by hand instead of by a metric that fails here.
   `at` at +48/1000 em would bring it to Heros' own `nt` (83); the whole word is
   reachable with two or three pairs. Positive kerns survive the 4.4 fixed-point
   int8 encoding. Risk: a hand-tuned pair is a permanent fork of the face's
   metrics, and it must be checked against every word using it, not just this one.
3. **Drop `force_autohint`.** Measured above — makes `at` worse and costs the
   x-height ramp. Not recommended; recorded so it is not re-proposed.

## 5. Reproducing

- Gutter measurement: render through `GfxRenderer` with the three-pass AA
  capture that `tools/calendar_preview/render_harness.cpp:809` uses
  (`renderReadingAaSpecimen`), then count zero-coverage columns between the ink
  bbox edges.
- Design-space letterfit: `fontTools` `hmtx` advance and `BoundsPen` xMin/xMax
  per glyph; junction gap = `(adv_left − xMax_left) + xMin_right`, scaled to
  1/1000 em.
- Kern data in the source: `fontconvert_sdcard.extract_kerning_fonttools(path,
  range(0x20, 0x7F), 32)` — returns 4.4 fixed-point pixels, directly comparable
  to `SdCardFont::getMeasureKern`.

## 6. Confidence

Verified against source, all of it. The gutter table, the sidebearing table, the
337-pair count, the 79,524-pair cross-check, the autohint A/B and the
`optical_kern` gate results are all measured output, not inference. The one
judgment call is §2's attribution of the 2-vs-4 split to `t`'s quarter-pixel
advance; the rendered-RSB table is the evidence for it, and an independent
FreeType rasterization of the same OTF at 33 ppem reproduces the same 1–4 spread
while Schola reproduces 1–2, which is what rules our pipeline out.
