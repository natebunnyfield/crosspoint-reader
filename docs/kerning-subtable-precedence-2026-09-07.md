# GPOS kern subtable precedence — investigation and fix, 2026-09-07

Trigger: the owner reported that DanteMT's letter spacing "is not right" while
reading a book on the device.

Scope: `lib/EpdFont/scripts/fontconvert_sdcard.py` only. Nothing in `src/`,
`lib/GfxRenderer/` or `lib/EpdFont/*.cpp` was changed. Per the SD-card font
rule, a change here ships as regenerated `.cpfont` files, **not** in a firmware
binary.

---

## Summary

| | |
|---|---|
| Leading hypothesis (Format-2 class kerning not read) | **REFUTED** — see "Checked and CLEAN" |
| Cause of the reported Dante symptom in ENGLISH text | **not found; the pipeline is faithful** (proved twice) |
| Two real defects found | subtable precedence + dropped zero-exceptions |
| Dante pairs corrected | 26–34 per style, **all accented**, worst 5× too tight |
| Other installed families corrected | Doves, WarblerText, Edgar, LutetiaNova, InknutJunicode, Coelacanth |
| Largest visible win | **Doves** `Te Ve We Ye` in plain English, 2.1× too tight |
| Verification | every corrected value re-derived with `hb-shape` (HarfBuzz) |

**Owner ruling, 2026-09-07: ship all seven rebuilt families** — DanteMT, Doves,
WarblerText, Edgar, LutetiaNova, InknutJunicode, Coelacanth. The ruling was
taken on the InknutJunicode win (1158 pairs, 85 ASCII-visible) and accepted the
unrelated recipe changes those rebuilds also carry, which §10 now characterizes
in full. Staging is BLOCKED — see §11.

---

## 1. What the extractor was doing wrong

`fontconvert_sdcard.py` reads the GPOS `kern` feature and flattens it to a pair
table. Before this change it walked every PairPos subtable of a lookup and
**summed** them, and it **discarded any value of zero**.

Both are wrong, and the OpenType shaping model says why:

* The subtables of ONE lookup are searched **in order** and only the **first**
  match is applied. HarfBuzz implements exactly that (`OT::Lookup::dispatch`
  returns at the first subtable whose `apply()` returns true).
* A Format-1 `PairValueRecord` with `XAdvance = 0` is a **match**, not an
  absence. It is how a designer writes "this specific pair is an exception —
  do NOT kern it" in front of a class rule that would otherwise fire.
  (`PairPosFormat1` returns true the moment the second glyph is found,
  regardless of the value; `PairPosFormat2` returns false unless a nonzero
  value was applied, so a zero class cell correctly falls through.)

This matters because that is exactly the shape a Monotype-style kern lookup is
built in: **one small Format-1 subtable of exceptions in front of one big
Format-2 class matrix.** DanteMT-Regular is 1 lookup / 2 subtables — 622
explicit pairs, then a 72 × 71 class matrix. Every pair listed in both got the
exception AND the rule it was written to override.

### Ground truth, measured with `hb-shape`

DanteMT-Regular, `--font-size=2048` (= upem), `T` unkerned advance 1300:

| text | HarfBuzz advance | implied kern | our old value | our new value |
|---|---|---|---|---|
| `Ta` | 1161 | −139 (class only) | −139 ✓ | −139 ✓ |
| `Tà` | 1265 | **−35** (exception WINS) | −174 (sum) ✗ | −35 ✓ |
| `Tä` | 1300 | **0** (zero exception) | −139 (leaked) ✗ | 0 ✓ |

`Tà` was **five times** the correction the designer wrote.

## 2. The fix

`lib/EpdFont/scripts/fontconvert_sdcard.py`:

* `_extract_pairpos_subtable` now writes one subtable into a fresh dict with
  `setdefault` instead of accumulating with `+=`, and records Format-1 zeros.
* New `_resolve_lookup_subtables(lookup, glyph_to_cp)` resolves the subtables of
  one lookup first-match-wins.
* `extract_kerning_fonttools` calls it per lookup and **sums across lookups**
  (correct: a shaper runs each lookup of the feature in turn).
* The legacy-`kern`/GPOS overlay comment now says why an explicit GPOS zero must
  survive the overlay and erase the legacy value.

Nothing else in the file changed. The class-derivation, packing, and 4.4
quantization paths are untouched.

## 3. Verification against HarfBuzz

`hb-shape` was run on up to 1200 pairs per face for every installed family, kern
on vs kern off, and the difference compared to the extractor's design-unit
output. Mismatch counts, same sample, same process:

| family / style | mismatches BEFORE | AFTER |
|---|---|---|
| DanteMT regular / bold / italic / bolditalic | 28 / 31 / 33 / 32 | **0 / 0 / 0 / 0** |
| Doves regular | 16 | **0** |
| WarblerText bold / bolditalic | 2 / 2 | **0 / 0** |
| InknutJunicode regular / bold | 8 / 7 | **0 / 0** |
| InknutJunicode italic / bolditalic | 19 / 19 | 6 / 6 (artifact, see below) |
| Edgar (all four) | 2–4 | 2–4 (artifact, see below) |
| Coelacanth regular / bold | 60 | 60 (pre-existing, see §6) |
| LutetiaNova, TeXGyreSchola, Almendra, LibrisADF, TeXGyreHeros | 0 | 0 |

The two residual "artifacts" are not defects and are **not** introduced by this
change:

* **Edgar**: the pairs are `"`+U+00AD, `(`+U+00AD etc. HarfBuzz treats SOFT
  HYPHEN as default-ignorable and skips it, so it reports 0. The reader draws a
  visible hyphen there, so kerning it is arguably the better behavior. Left
  alone.
* **InknutJunicode italic**: measured against the *variable* `JunicodeVF-Italic`
  default instance, while the build uses the wght 600 / wdth 125 instance. Re-run
  against the actual instance (`instanced_fonts/InknutJunicode/italic_ENLA14_wdth125_wght600_*.ttf`),
  all five spot pairs match exactly: `Y p` −89, `V o` −116, `r .` −146,
  `r ,` −148, `g ,` +12.

## 4. Per-family impact — isolated

Measured in one process by running BOTH resolutions over the same source font,
so no unrelated code drift can leak in. "ASCII" counts pairs where both sides
are ASCII letters/digits/punctuation — i.e. visible in ordinary English.

| family / style | pairs changed | ASCII-visible | worst change (per 1000 em) |
|---|---|---|---|
| DanteMT regular | 29 | **0** | `V ë` −168 → −56 |
| DanteMT bold | 32 | 0 | `T ò` −196 → −84 |
| DanteMT italic | 34 | 0 | `W ö` −177 → −76 |
| DanteMT bolditalic | 33 | 0 | `T ò` −196 → −84 |
| Doves regular | 910 | **11** | `T e` −208 → −100 |
| Doves bold (synthetic from regular) | 910 | 11 | same |
| Doves italic / bolditalic (Coelacanth-derived) | 0 | 0 | — |
| WarblerText regular | 395 | 3 | `Ỳ č` −185 → −60 |
| WarblerText italic | 939 | 1 | `ś “` −230 → −100 |
| WarblerText bold | 478 | 4 | `Ỷ ŏ` −240 → −115 |
| WarblerText bolditalic | 1516 | 4 | `ổ “` −250 → −110 |
| Edgar regular / bold | 105 / 108 | 1 / 1 | `Ý ð` −185 → −90 |
| Edgar italic / bolditalic | 33 / 36 | 1 / 1 | `‚ ĵ` +45 → +25 |
| LutetiaNova regular | **0** | 0 | — |
| LutetiaNova italic | 11 | 0 | `W à` −300 → −100 |
| Coelacanth regular / bold | **1** | 1 | `y ,` −74 → −50 |
| Coelacanth italic | 0 | 0 | — |
| InknutJunicode regular / bold | 41 / 41 | 0 / 0 | `ť ?` +250 → +80 |
| InknutJunicode italic (instanced) | 1158 | **85** | `Y p` −225 → −89 |
| InknutJunicode bolditalic (instanced) | 1158 | 85 | same |
| Almendra (×4) | 0 | 0 | unchanged, byte-for-byte |
| TeXGyreSchola (×4) | 0 | 0 | unchanged |
| LibrisADF (×4) | 0 | 0 | unchanged |
| TeXGyreHeros (×4) | 0 | 0 | unchanged |
| LibreFranklin (×4) | 0 | 0 | variable-font instances, no multi-subtable kern lookup |

**Five of the twelve installed families are completely unaffected**: Almendra,
TeXGyreSchola, LibrisADF, TeXGyreHeros, LibreFranklin.

### The ASCII-visible ones, spelled out

Doves regular/bold at 14 pt (built pixels, 4.4 fixed point):

| pair | before | after |
|---|---|---|
| `T e` | −6.875 px | −3.312 px |
| `V e` | −6.500 px | −3.062 px |
| `W e` | −6.500 px | −3.062 px |
| `Y e` | −6.500 px | −3.062 px |
| `A Y` / `Y A` | −3.062 px | −1.312 px |
| `J e` | −2.750 px | −1.188 px |
| `z W` | −1.812 px | −0.312 px |
| `T V` / `T W` / `T Y` | +1.812 px | +0.875 px |

InknutJunicode italic (design units, upem 1000): `Y p` −225→−89, `V o` −227→−116,
`r .` −210→−146, `r ,` −212→−148, `g ,` −60→+12, `) ,` −114→−70, `( J` +160→+112.
The italic is what every emphasis in every book is set in, so this is the
change with the widest reach of the lot.

WarblerText: `D ,` `O ,` `Q ,` and `, J`. Edgar: `, J` and `g ,`.
Coelacanth: `y ,` only.

### Side effect this also removes: int8 saturation

The kern field is `int8_t` in 4.4 fixed point, so ±8.0 px is the ceiling. The
doubled values were hitting it. DanteMT regular at size 23 saturated **12**
pairs before the fix (`T ò`, `V à`, `V ã`, `V å`, `Y ö`, …) and **0** after;
italic saturated 1 at size 20 and 2 at size 23, now 0.

## 5. Before / after renders

Produced with `tools/calendar_preview/render_harness fonts <Before> <After>`,
which draws both families on one page through the real firmware renderer and
prints each string's measured width. The "before" families were built from the
pre-change extractor; for InknutJunicode that meant a deliberate temporary
revert and rebuild, because the staged copy under `fs_/.fonts/` dates from
2026-08-15 and carries a month of unrelated extractor changes.

| image | what it shows |
|---|---|
| `kernfix/ab_Dante_18.png` | DanteBefore (top) vs DanteAfter (bottom), 18 pt |
| `kernfix/dante_zoom_accents.png` | 3× zoom: `Tübingen Tòrino` / `Väinö Wörter fèves` |
| `kernfix/ab_Doves_18.png` | DovesBefore vs DovesAfter, 18 pt |
| `kernfix/doves_zoom_Te.png` | 3× zoom: `Tell Verse Well Yes` |
| `kernfix/ab_Dante_14.png`, `kernfix/ab_Doves_14.png` | the same at 14 pt |
| `kernfix/inknut_italic_ba.png` | InknutJunicode reading page, italic paragraph |

(Session scratchpad, not committed:
`/private/tmp/claude-501/-Users-natebunnyfield/3d74ef2f-bd8b-4b5c-9e55-30b69125fb35/scratchpad/`.)

Measured widths, DanteBefore → DanteAfter:

```
12pt  Tübingen Tòrino          153px -> 156px   (+3)
14pt  Tübingen Tòrino          178px -> 182px   (+4)
16pt  Tübingen Tòrino          215px -> 221px   (+6)
12pt  Väinö Wörter fèves       171px -> 175px   (+4)
16pt  Väinö Wörter fèves       240px -> 247px   (+7)
      People / does / Froggy / Tools You / Tell Verse Well Yes / Jenny AYE
                                  IDENTICAL at every size (delta 0)
```

DovesBefore → DovesAfter:

```
12pt  Tell Verse Well Yes      188px -> 199px   (+11)
14pt  Tell Verse Well Yes      218px -> 233px   (+15)
16pt  Tell Verse Well Yes      251px -> 267px   (+16)
18pt  Tell Verse Well Yes      286px -> 302px   (+16)
14pt  Jenny AYE                141px -> 144px   (+3)
      People / does / Froggy / Tools You   IDENTICAL
```

In the Dante zoom the `T` crossbar overlaps the grave on `ò` and the dieresis on
`ü` in the BEFORE row and clears them in the AFTER row. In the Doves zoom the
`e` sits tucked under the `T`/`V`/`W`/`Y` in the BEFORE row.

`render_harness.cpp`'s `kSamples` was temporarily extended to carry those
strings, then **reverted**, and the harness rebuilt from the reverted source.
`git status tools/` is clean.

## 6. Checked and found CLEAN — do not re-investigate these

Everything in this section was measured, not assumed. Record kept so the next
session does not pay for it again.

1. **The leading hypothesis is wrong.** `_extract_pairpos_subtable` has handled
   PairPos **Format 2** since before this session (`fontconvert_sdcard.py`, the
   `elif subtable.Format == 2` branch), iterating by class rather than by glyph.
   Dante's class matrix reaches the device in full.
2. **Extension lookups (type 9) are unwrapped correctly**, and the *effective*
   lookup type is checked rather than the outer one.
3. **`derive_kern_classes` is lossless.** It groups left codepoints by identical
   adjustment ROW and right codepoints by identical COLUMN, so two codepoints in
   a class have provably identical kerning. The "5 right classes" figure that
   prompted the lossy-approximation suspicion is simply how few distinct columns
   Coelacanth has. DanteMT at 17 pt is 49 × 61, and the resulting matrix
   reproduces the pair map exactly.
4. **Dante's ASCII kerning was already byte-exact.** Every pair over
   `[A-Za-z0-9 .,;:!?'"()-]` was read back out of the built `.cpfont` and
   compared against the source font's own first-match GPOS value at sizes 12,
   14 and 17: **316 nonzero pairs, 0 mismatches at all three sizes.** Smart
   quotes, em dash, `y.`, `r.`, `f)`, `’s`, `“A`, `A’`, `L’` all correct. This
   is why the fix does not change a page of English Dante.
5. **The renderer's positioning is faithful to the designer.** A ground-truth
   render (FreeType outlines at the same ppem, subpixel positions, first-match
   GPOS kerning) was compared glyph-by-glyph with the device path
   (`.cpfont` 12.4 advance + 4.4 kern, `fp4::toPixel` per pair step,
   `GfxRenderer.cpp:817-821`). Over a 43-character line the accumulated
   difference was −3.6 px on 530 px, i.e. **0.7%**, all of it the unavoidable
   ±0.5 px per-step rounding. The two renders look the same, including the
   places that look tight (`turned`, `d p` across a word space) — those are the
   face, not the pipeline.
6. **Hinting is not distorting the letterfit.** Rendered four ways at 12 pt
   (8×-downsampled outline ideal / native TT hinting / no hinting / forced
   autohinter): visually indistinguishable letterfit. Hinted-vs-unhinted left
   side bearing differs by mean +0.04 px, max 1 px; right side bearing mean
   −0.04 px. `scale: 0.94` (upem 2048 → 2179) does not make it worse — the
   unscaled source measures the same.
7. **`scale:` does not break kerning.** `apply_upem_scale` changes only
   `head.unitsPerEm`, so kern values in design units scale with the outlines.
   Confirmed numerically against `hb-shape` for Doves (`scale: 1.14`) and
   InknutJunicode italic (`scale: 0.966`) — every value matches to the
   quantization step.
8. **Advances are unhinted-linear and 1/16 px accurate.**
   `fp4_from_ft16_16` rounds to nearest; `linearHoriAdvance` is used, not the
   grid-fit advance.
9. **No installed family uses `spacing:`** (tracking / word-space). Grep is
   empty in `sd-fonts.yaml`, so Dante is not tight relative to a tier that gets
   extra tracking.
10. **Summing ACROSS lookups is safe.** Cross-lookup pair overlap is **0** for
    every installed family, so no pair is double-applied by that path.
11. **`optical_kern.py` was not needed and was not run.** Dante is not sparsely
    kerned (622 explicit pairs + 5112 class cells in the regular), and the fix
    turned out to be a decoding bug, not a coverage gap. No synthesized pair was
    added; nothing overrides a designer's value.

## 7. Known, pre-existing, NOT fixed here

* **The script/language graph is ignored.** `extract_kerning_fonttools` takes
  every `kern` FeatureRecord regardless of which script registers it. The one
  measurable consequence found: Coelacanth's `(` + Greek capitals get a −53 du
  kern that HarfBuzz does not apply for Greek-script text (60 pairs, regular and
  bold). For a reader that never runs script-specific shaping this is arguably
  the friendlier behavior, and it predates this change. WarblerText is the only
  installed family whose `kern` feature is genuinely script-dependent, and its
  two lookup sets have zero pair overlap, so nothing is affected there.
* **Soft hyphen kerning** — see §3.
* **The `int8_t` 4.4 kern ceiling still bites the hi-res tiers.** At
  `--scale 3` the ppem triples while the ±8.0 px ceiling does not, so strong
  pairs saturate on a 3× host (iOS / desktop). The device is `RENDER_SCALE=1`
  and unaffected. Not measured in detail; flagged only.

## 8. Correction: my first "a month of drift" claim was wrong

The first version of this document warned that InknutJunicode and Coelacanth
carried a month of unrelated change. **That was a false alarm and it is
withdrawn.** I had diffed against `tools/calendar_preview/fs_/.fonts/`, which is
the render-harness's own scratch tree — last written 2026-08-15, still on the
pre-08-26 FOUR-slot ramps. It is not a shipping surface.

The shipping surface is `fs_/fonts/` at the repo root (plus device cards and the
iOS seed bundle), and it was already current: six-slot ramps, right metrics.
Everything I attributed to drift — `advanceY 36→34`, every advance scaled by
0.944, `ascender 26→24` — was the harness tree being a month behind, not the
rebuild changing anything.

The lesson, worth keeping: **`tools/calendar_preview/fs_/.fonts/` and
`fs_/fonts/` are different trees with different lifetimes.** The harness reads
the first, the simulator and the card provisioning read the second. Never take a
baseline from the harness tree.

## 9. Cache invalidation — READ THIS BEFORE PUTTING THESE ON A CARD

**Replacing a `.cpfont` in place does NOT invalidate the section cache.**
`Section::writeSectionFileHeader` (`lib/Epub/Epub/Section.cpp:279-313`) keys a
cached section on `SECTION_FILE_VERSION`, `spec.fontId`, viewport, and a set of
settings flags including `spec.ligatureFingerprint` — and that fingerprint comes
from `ligatures::fingerprint(ligaturesEnabled, ligaturesOff)`
(`src/CrossPointSettings.cpp:359`), i.e. from SETTINGS, not from the font file.
**Nothing in the cache key depends on the content of the `.cpfont`.**

So a book already cached in one of these seven families keeps page layouts
computed with the OLD kerning. Doves' `Tell Verse Well Yes` is 16 px wider at
18 pt; that moves line breaks. The stale pages will not crash, they will just be
laid out to the wrong measure until the cache is rebuilt.

CLAUDE.md already states the governing rule — "A layout change bumps
`SECTION_FILE_VERSION` too, not only a structure change… If a change alters what
a page LOOKS like, ask whether a cache built before it would still be right."
This change does alter what a page looks like.

Two ways to satisfy it, and the choice is the owner's:

* **Bump `SECTION_FILE_VERSION`** (currently 57, `Section.cpp:213`). Correct and
  automatic, invalidates every book for every reader, and it is a FIRMWARE
  change — it ships in the binary, not with the fonts, so the two halves have to
  land together or the fonts arrive before the invalidation does. Out of my
  territory; not done.
* **Delete `.crosspoint/` on the card** when the fonts are deployed. Costs a
  re-parse of every book; no firmware change; only fixes the cards you touch.

`cpcards` does not clear `.crosspoint/`; check before assuming it does.

## 10. Non-kern drift in the rebuilds, characterized

Every field except the kern tables, diffed against `fs_/fonts/`, all sizes, all
four styles. **No regression found.** Every difference traces to a commit
already on `main` that had simply never been staged.

| family | non-kern differences | cause |
|---|---|---|
| **DanteMT** | **none** — advances 0, bbox 0, bitmaps 0, glyph count 2676 = 2676, ligatures identical, advanceY/asc/desc identical, at all six sizes and all four styles. Files grow exactly +100 bytes. | kerning only |
| **WarblerText** | **none**, same as above. +106 B (+759 B at size 18, more kern-class entries). | kerning only |
| **Edgar** | **none**, same as above. +512 B. | kerning only |
| **LutetiaNova** | regular and italic: none. **bold + bolditalic**: bbox/top moves on ~1800–2250 glyphs, bitmaps differ on ~2620–2646, **advances unchanged (0)**. | `ff3e7c8ca` |
| **Coelacanth** | regular, bold, italic: none. **bolditalic only**: bbox/top ~1640–2373, bitmaps ~2958, advances 0. | `ff3e7c8ca` |
| **InknutJunicode** | regular, bold, italic: none. **bolditalic only**: bbox/top ~1885–2320, bitmaps ~2693, advances 0. | `ff3e7c8ca` |
| **Doves** (was `DovesType`) | regular: none. **bold**: bitmaps only. **italic + bolditalic**: every advance changes (×1.115), advanceY 52→53 at size 18. | `ff3e7c8ca`, plus `8aa3ec4bf`, `d5ee10330`, `189372ab9` |

Commits responsible, all authored by the owner, all already merged:

| commit | date | what it does |
|---|---|---|
| `ff3e7c8ca` | 2026-09-07 | *fonts: correct the baseline of every synthetic bold in the file* — adds `baseline_shift_em` to every synthetic bold/bolditalic. This is the whole of the Lutetia / Coelacanth / Inknut drift, and it is a FIX being staged for the first time. |
| `8aa3ec4bf` | 2026-09-07 | Doves takes the Coelacanth italic |
| `d5ee10330` | 2026-09-07 | Doves renamed from DovesType; italic to `scale: 1.115` |
| `189372ab9` | 2026-09-07 | Doves takes V15, fixes two baseline faults |

The signature that made this readable: in every synthetic-bold case the
**advances are unchanged and only the ink moves**, which is exactly what a
baseline shift does and exactly what a kerning change cannot do.

### One thing that IS worth the owner's attention, though not a regression

**`scale:` does not reach the fallback face.** `apply_upem_scale` is applied to
`resolved_styles[style_name]` (`build-sd-fonts.py:1087`); the Noto fallback is
resolved separately and passed through raw (`:1168`, `:1175`). Measured on the
staged size-14 regular cuts, in pixels:

| codepoint | DanteMT (`scale: 0.94`) | Edgar (no scale) |
|---|---|---|
| `n` (family's own glyph) | 14.000 | 16.3125 |
| `←` U+2190 (fallback) | **29.1875** | **29.1875** |
| `α` U+03B1 (fallback) | **22.5625** | **22.5625** |
| `Ж` U+0416 (fallback) | **26.375** | **26.375** |

The fallback glyphs are byte-identical between a scaled family and an unscaled
one, so a Dante page renders its arrows and Greek 1/0.94 = 6.4% larger relative
to its own Latin than the recipe intends; InknutJunicode at `scale: 0.805` is
24% out, and Doves at `1.14` is 12% the other way. Pre-existing, unrelated to
this change, not fixed here. Whether it is a bug or an accepted trade is the
owner's call — flagged, not acted on.

## 11. BLOCKER — Doves cannot build at 3×, and it is not this change

The staging run **failed**, and the cause is a hole in the Doves recipe that
predates this work by hours. `fontconvert_sdcard.py:1374`:

```
ValueError: 2 glyph(s) rasterise over the 255 px limit of EpdGlyph's uint8
width/height: U+261C (268x127 px), U+261E (268x127 px).
```

at `output/Doves/3x/Doves_54.cpfont` (18 pt × 3). The mechanism:

* Doves took the **Coelacanth italic** today (`8aa3ec4bf`), which brought
  Coelacanth's glyph set with it — including the pointing hands U+261C / U+261E.
* Coelacanth's own recipe has carried the drop for exactly those two glyphs at
  exactly that tier since it hit the same wall: `hires_drops: {3: [0x261C, 0x261E]}`.
* Doves' `hires_drops:` is `{2: [0x2E3B], 3: [0x2E3A, 0x2E3B]}` — the dashes
  only. The italic arrived; the drop entry that makes it buildable did not.

**Consequence: any build of the 3× tier for Doves fails, today, on `main`, with
or without the kerning change.** It is a rasterization size limit and has
nothing to do with kerning. It will stop any publish that builds all three
tiers for the twelve installed families.

The fix is one line in `lib/EpdFont/scripts/sd-fonts.yaml`, mirroring
Coelacanth's own entry for the same two glyphs from the same face:

```yaml
    hires_drops:
      2: [0x2E3B]
      3: [0x2E3A, 0x2E3B, 0x261C, 0x261E]   # + the Coelacanth italic's hands
```

NOT applied here — it is a recipe change with a publish in flight, so it goes to
the owner rather than into a frozen build tree. Cost of the drop is what
Coelacanth already pays: on a 3× host those two glyphs fall back per-glyph; the
device tier keeps them.

## 12. Staging record — ATTEMPTED, NOT COMPLETED

Staging was run with the canonical tool, `scripts/install-sim-fonts.py`, which
is how every other family reached the surface:

```bash
python3 scripts/install-sim-fonts.py \
    --families DanteMT,Doves,WarblerText,Edgar,LutetiaNova,InknutJunicode,Coelacanth
```

It builds **all three render-scale tiers** (1×, 2×, 3×) and installs into
`fs_/fonts/<Family>/` and `fs_/fonts/<Family>/<N>x/`, pruning sizes the current
ramp no longer carries. Passing `--families` leaves `reference` at `None`, which
gates off the cut-family pruning, so the five untouched families
(Almendra, TeXGyreSchola, LibrisADF, TeXGyreHeros, LibreFranklin) are not
disturbed. `tools/calendar_preview/fs_/.fonts/` was NOT written to.

**It got as far as the 3× tier and stopped on the Doves fault in §11.**
`install-sim-fonts.py:228-230` returns on a non-zero build, and the install
loop is BELOW that return — so the copy step never ran:

* **`fs_/fonts/` is UNTOUCHED.** `fs_/fonts/DanteMT/` still carries its 2026-09-06
  files; `fs_/fonts/Doves/` does not exist. Verified by directory mtime. There is
  no half-staged state to clean up, which is the one piece of luck here.
* **`lib/EpdFont/scripts/output/` is NOT a complete twelve-family set.** The run
  passed `--clean`, which rmtree's the whole directory, so it now holds ONLY the
  seven families it was given: Coelacanth, DanteMT, Doves, Edgar,
  InknutJunicode, LutetiaNova, WarblerText. Almendra, TeXGyreSchola, LibrisADF,
  TeXGyreHeros and LibreFranklin are **absent**. Within those seven, six have a
  complete 1×/2×/3× set; **`output/Doves/3x/` is empty**.

  Anything that reads `output/` as if it were the shipping set would ship seven
  families and a Doves with no 3× tier. Anything that rebuilds all twelve from
  the recipe is fine — and will hit §11 on Doves.

Redo, once §11 is fixed and the tree is free:

```bash
python3 scripts/install-sim-fonts.py \
    --families DanteMT,Doves,WarblerText,Edgar,LutetiaNova,InknutJunicode,Coelacanth
```

**Hi-res tiers: rebuilt.** This matters and the answer is not "the device is
1×, so who cares":

* **Device (`CROSSPOINT_RENDER_SCALE=1`)** reads the 1× cut only. A stale 2×/3×
  would not affect it.
* **iOS and desktop host builds** read the tier matching their render scale
  (`docs/render-scale.md`). A corrected 1× beside a stale 3× is precisely the
  B-035 failure `install-sim-fonts.py`'s own docstring documents: the 2026-08-17
  arrow fix landed in 1× and left every scaled host build on the old glyphs, so
  the fix looked applied and was not — and the owner inspects this project
  through the simulator, which is a scaled host build.
* The standing ruling of 2026-08-15 is that all three tiers ship together.

So all three were built. Had they not been, this would have shipped kerning-
corrected text on the device and uncorrected text everywhere the owner actually
looks at it.

**Left alone deliberately:** `fs_/fonts/DovesType/` is now an orphan — the family
was renamed to `Doves` today (`d5ee10330`) and the new directory sits beside it.
`SETTINGS.sdFontFamilyName` persists the directory STRING, so deleting
`DovesType` strands anyone whose setting still names it, and the SD-card font
rules call directory names effectively frozen. That is a migration decision, not
a cleanup, so it is reported rather than performed.

## 13. Still to do

* **§11 first** — Doves' missing 3× drop entry blocks every full build.
* Re-run the staging command in §12; confirm `fs_/fonts/` then carries the seven.
* Device cards (`cpcards`) and the iOS seed bundle
  (`crosspoint-simulator/ios/seedfonts/`) — not touched.
* The section-cache question in §9.
* Nothing is committed. The tree carries
  `lib/EpdFont/scripts/fontconvert_sdcard.py` and this file.

## 14. Reproducing the measurements

`hb-shape` (Homebrew harfbuzz) is the oracle:

```bash
hb-shape --font-size=2048 --features=kern  --no-glyph-names --no-clusters \
    lib/EpdFont/local_fonts/DanteMT-Regular.ttf 'Tà'
hb-shape --font-size=2048 --features=-kern --no-glyph-names --no-clusters \
    lib/EpdFont/local_fonts/DanteMT-Regular.ttf 'Tà'
# difference of the FIRST advance is the kern, in design units
```

Reading a built file back is the other half — the `.cpfont` v4 layout is header
32 B, then one 32-B style TOC entry per style
(`<B3xIIBhhHHBBBI4x`), then per style: intervals `<III`, glyphs `<BBHhhH2xI`,
kern-left `<HB`, kern-right `<HB`, the `kernLCls × kernRCls` `int8` matrix,
ligatures `<II`, bitmaps.

**Kerning is zero until the page is prewarmed.** `SdCardFont` keeps only a
per-page mini kern matrix; measuring straight after `load()` reports an unkerned
font. `prewarm(text, 0x0F, /*metadataOnly=*/false)` per sample, immediately
before use.
