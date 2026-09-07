# Dante, Lutetia Nova, Golden Cockerel, Doves — build notes and measurements

*2026-09-06, evening. Four commercial faces from the iCloud `dtl` folder,
built straight into `installed_families:` on the owner's instruction
("commit and ship"), taking the tier from nine families to thirteen. Every
number below is measured; the method is the one
[docs/dtl-trial-fonts-2026-09-06.md](dtl-trial-fonts-2026-09-06.md) arrived at
over the course of the same day, applied from the start rather than
discovered halfway. The recipes in `sd-fonts.yaml` carry the short form of
each decision; this file carries the sweeps and the things that were checked
and found clean.*

**The method, in one paragraph, because it changed today.** Slots are fitted
for x-height AND leading TOGETHER: every 6-size ramp from 6 to 29 pt at every
`scale:` from 0.88 to 1.30 is scored against the x-height anchor
(8/10/12/14/16/18 px) and the tier's leading target (23/28/34/40/46/51 px), the
metrics span is then raised above the ink floor only where that reduces the
leading drift, and the ascent/descent split is solved so descenders clear in
every style with accents allowed to poke. Romulus, fitted for x-height alone
that morning, shipped the loosest line in the tier and had to be refitted;
none of these three needed that.

## 1. Dante (`DanteMT`)

**Source.** Monotype's *Dante MT Std* webfont kit, Version 2.04, six TTFs:
Regular, Italic, Medium, Medium Italic, Bold, Bold Italic. `unitsPerEm` 2048,
`hhea` 1366/−682/410 on all six, sane. Native `liga` with ff fi fl ffi ffl all
cmapped at U+FB00–FB04; GPOS `kern` with 1,650–1,910 pairs per face; the
Regular alone carries `smcp` (144 small-cap glyphs) and `c2sc`, which the
reader cannot use. Latin-only: 692 codepoints in the Regular, 356–361 in the
others, three Greek letters as symbols, no Cyrillic. The `OS/2` `sxHeight`
(403/2048 = 0.197 em) is nonsense metadata — the rendered x-height is 0.44 em —
and was ignored.

**The converter needs no help.** `extract_ligatures_fonttools` returns five
pairs per style (ff, fi, fl, and the chained ffi/ffl) straight from the source
`liga`; no `synth_ligatures:`. The first commercial face of the day to arrive
complete.

**Which weight is "bold".** Six weights, four slots. **Bold and Bold Italic**,
by owner ruling the same evening: *"redo dante to use bold for bold and bold
italic for bold italic (this was wasteful on your part to choose medium and not
bold)."* Stems measured at 50 ppem from the middle row of `l`:

| face | stem | ratio to Regular |
|---|---|---|
| Regular | 60/1000 em | — |
| **Medium** | **100/1000** | **1.67×** |
| Bold | 120/1000 | 2.00× |

The first build of this recipe shipped **Medium** as the bold, on the reasoning
that the tier's real bolds measure GT Alpina 1.50×, Edgar 1.75×, Venetian 301
2.50×, that the synthetic-bold target is ~1.5×, and that 2× reads as a display
black in running text. **That was a judgement the owner had not asked for**, and
it was reversed within the hour: the face ships a Bold, and the Bold is what
"bold" means. The measurement is kept because it is true and may matter if the
weight is ever revisited — not as a case for re-arguing a settled ruling.
Medium and Medium Italic stay staged in `local_fonts/`; reversing is two
`path:` lines.

**The metrics did not move with the swap, which was checked rather than
assumed.** Bold's plain ink is *tighter* than Medium's — minimum ink-safe span
1080 against 1120, descent need 240 against 280 — so `838/−282` still clears
every descender in all four styles and `advY` stays 23/28/33/40/47/54. Kerning
came out slightly richer (`kernL=116 kernR=121` in the bold, against Medium's
107/116).

**Sizes.** `scale: 0.94`, ramp `10/12/14/17/20/23`:

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| x-height (target 8/10/12/14/16/18) | 8 | 10 | 12 | 14 | 16 | 18 |
| advY (target 23/28/34/40/46/51) | 23 | 28 | 33 | 40 | 47 | 54 |

x-height exact on all six; leading drift 5, beside Libris ADF (6). The joint
sweep's other candidates, none taken: `1.08` × `8/11/13/15/17/20` (drift 6,
slot 0 at 21); `1.00` × `9/11/14/16/18/21` (drift 3 but x-height 17 at the top
slot). The anchor won.

**Metrics.** Span 1120 is the ink floor at the top slot. Split `838/−282`:
descenders clear at every size in all four styles; accented capitals need 857
and poke 19/1000 em (under 1 px) above the declared ascender. Raising the span
to cover them would have cost 1–4 px of leading at every slot — descenders win,
as in Venetian 301 and Romulus.

**Hi-res drops.** U+2E3B THREE-EM DASH rasterizes 264 px wide at the 2× cut
of the top slots, over `EpdGlyph`'s uint8 width; U+2E3A TWO-EM DASH joins it at
3×. `hires_drops: {2: [0x2E3B], 3: [0x2E3A, 0x2E3B]}` — the Coelacanth
pointing-hands shape: a property of the size, not the family, so the 1× cut
keeps both. Both codepoints come from the fallback chain, not from Dante, so
**every family on `reading` will hit this at 2× once its top slot passes
~22 pt** — Lutetia Nova and Golden Cockerel carry the same two lines
pre-emptively and built first time.

**Confirmed through the renderer:** `line 23/28/33/40/47/54`, `ligs=5`,
`kernL=106–109 kernR=114–116` in all four styles — the richest kerning in the
tier.

## 2. Lutetia Nova (`LutetiaNova`)

**Source.** *Lutetia Nova W00 Book*, RMU Typedesign (Ralph M. Unger) 2014,
Version 1.00, two TTFs: Book and Book Italic. `unitsPerEm` 1000, `hhea`
922/−360/0. Native `liga` (ff fi fl in the Book; ff fi fl ffi ffl in the
Italic), `dlig`, `smcp` in the Book, `swsh` in the Italic; GPOS `kern`. 343/347
codepoints with **85 of Latin Ext-A** — the widest coverage of the three.

**No bold ships, so both bolds are synthetic**, the Caledonia CC / Coelacanth
pattern: `bold: {from: regular, synthetic: {embolden_em: 0.048, y_ratio: 0.35}}`
and `bolditalic` from the italic at `0.036`. The strengths are derived, not
judged: stems 80/1000 em (Book) and 60/1000 (Italic), target 1.6× — the middle
of the tier's real bolds — so `embolden_em = 0.6 × stem`. `y_ratio 0.35` as
both precedents use; full vertical growth eats x-height and closes counters.
**UNCONFIRMED ON DEVICE** — the acceptance criteria are in
[docs/synthetic-font-styles.md](synthetic-font-styles.md).

**Sizes.** `scale: 0.88`, ramp `8/10/13/15/18/20`. The largest downward scale
in the file: van Krimpen drew Lutetia big-on-body for 12 pt lead, and at k=1
the ramp reaching the anchor runs 7–17 pt with the line drifting 8+.

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| x-height | 8 | 10 | 12 | 14 | 16 | 18 |
| advY | 21 | 26 | 33 | 39 | 46 | 51 |

Exact x-height; drift 6, tight at the small slots and exact at the two
largest. Not taken: `8/10/13/15/17/20` at span 1273 (21/27/34/40/45/53, also
6) — this one lands the top two slots.

**Metrics.** Span 1234, split `880/−354`; accents need 864 and clear.

**Renderer:** `line 21/26/33/39/46/51`, `ligs=3` (Book) / 5 (Italic),
`kernL=167 kernR=209` — the Book alone out-kerns every other face in the tier.

## 3. Golden Cockerel (`GoldenCockerel`)

**Source.** *ITC Golden Cockerel*, Version 2.0, copyright 1994–1996 ITC. Four
TTFs in the kit, **two staged**: Roman and Italic. The Titling and the
Initials & Ornaments are display cuts with no slot in the reader. `unitsPerEm`
1000, `hhea` 914/−312/0. 249 codepoints, Mac charset, ten of Latin Ext-A.

**No GSUB at all** — the Romulus case. `fi` and `fl` are drawn and cmapped at
U+FB01/FB02 and nothing reaches them; `synth_ligatures: [fi, fl]` supplies the
rule, and nothing more is drawn (no ff, ffi, ffl). GPOS `kern` present.

**No bold ships** — ITC never cut one; Gill's press had no use for one — so
both bolds are synthetic at `0.048` (stems 80/1000 em in both real faces),
`y_ratio 0.35`. Unconfirmed on device, as Lutetia's.

**Sizes.** `scale: 1.02`, ramp `9/11/14/16/18/20` — the best fit of the three:

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| x-height | 8 | 10 | 12 | 14 | 16 | 18 |
| advY | 23 | 28 | 36 | 41 | 46 | 51 |

Drift 3. Not taken: `1.14` × `8/10/12/14/16/18` (drift 4) — tempting for its
point sizes equalling the slot numbers, but 1.02 is closer to the drawing.

**Metrics.** Span 1234, split `885/−349`. Gill's accented capitals sit high:
they need 960 against the declared 885 and poke up to 75/1000 em (2–4 px at
the top slot). Covering them would cost 3–5 px of leading at every slot, so
descenders win; the poke is into the line above's descender space and only on
accented capitals.

**Renderer:** `line 23/28/36/41/46/51`, `ligs=2`, `kernL=89 kernR=94`.

## 4. What the three cost the app

Seed tree after the three: **641 MB** on disk at 1×+2×+3× across 12 families
(3× is built and not bundled). The bundled 1×+2× delta is measured at ship
time by the compressor; each family is on the order of Romulus's 10.5 MB
compressed.

## 5. Unconfirmed on device, stated plainly

- **Both synthetic bolds.** Derived from stems to a 1.6× target; never seen on
  glass. If they read heavy or thin, `embolden_em` is one number per style.
- **Golden Cockerel's accent poke** — 2–4 px at the top slot on accented
  capitals only.
- **Dante's Bold** (the shipped pair since the same-evening ruling) was seen
  only on the 1× plate at slot 3, where it reads as a true bold beside the
  Regular. Whether 2× stems are too black on the phone's glass is exactly the
  judgement that belongs on device, not here.

## 6. Checked and found clean

- Every face's cmap, `hhea`, and `GSUB`/`GPOS` inventory read before any
  recipe was written; nothing here inherited the Romulus surprises (zeroed
  `hhea`, absent `GSUB`) undetected.
- Dante's five native ligature pairs and Lutetia's three/five extract through
  `fontconvert_sdcard.py`'s own extractor with no PUA work.
- `tools/validate_seed_fonts.py`: `seed fonts OK: 12 families, 1x + tiers up
  to 2x, header-verified against sd-fonts.yaml`.
- `install-sim-fonts.py --families DanteMT`, `LutetiaNova,GoldenCockerel`:
  all three at 1×, 2×, 3× on `fs_/fonts/`, first attempt after the
  `hires_drops` lines.
- `src/FontDisplayNames.h` compiles with all three rows and `find()` resolves
  each; the Schwäbisch Gmünd literal is split at `\xA4` so the hex escape
  cannot swallow the `b` — the hazard `docs/font-dates.md` records.
- The Golden Cockerel Titling and Initials cuts were deliberately not staged;
  `local_fonts/` holds only the two text faces.


---

## 7. Doves Type (`DovesType`) — and the borrowed italic

Added after the other three, on a separate instruction: *"create a Doves font
and create/borrow an italic from Coelacanth or Junicode depending on what fits
best with Doves (look at size and stroke width and counterspace)."*

**Source.** *Doves Type Text*, Version 4.810, © Robert Green 2021. `upem` 2048,
`hhea` 1832/−618/140. 551 glyphs, 335 codepoints, 89 of Latin Ext-A. GSUB with
**21 features** — `liga`, `dlig`, `calt`, `ccmp`, `smcp`, `c2sc`, `ss01`–`ss08`,
`swsh`, `locl`, `case`, `frac`, `ordn`, `sups`, `aalt` — and GPOS `kern`, `mark`,
`mkmk` with **18,852 kern pairs**, the richest in the file. The Headline cut in
the same kit is a display face and is not staged.

### 7.1 The Doves Press had no italic

Cobden-Sanderson set everything roman; no italic was ever cut, and Green cut
none. So this family cannot have its own italic — it is the only one in the
picker that borrows, and the owner's instruction was to pick the better of two.

**The comparison, normalised to x-height**, because an italic borrowed for a
roman is scaled to that roman's x-height and the comparison has to happen where
the type will actually sit:

| face | stem / x-ht | vs Doves | counter / x-ht | ink / x-ht |
|---|---|---|---|---|
| **Doves Text** (the roman) | 0.182 | 100% | 0.636 | 2.515 |
| Coelacanth Italic | 0.143 | 79% | 0.486 | 2.179 *(+5 px combined)* |
| Junicode SemiCondLight Italic | 0.111 | 61% | 0.444 | 2.179 |
| **Junicode VF @ wght 550 / wdth 125** | **0.179** | **99%** | **0.538** | **2.179** |

**Coelacanth loses on all three**, which is worth stating because it is the
obvious first thought — both faces descend from Jenson and it is already in the
tier. Its stroke is 79% of the roman's where a borrowed italic wants to read as
the same printer's ink; its counters are the tightest of the three against
Doves' very open ones; and its extenders are the longest in the whole file, so
at matched x-height it adds 5 px to Doves' own 52 px ink span. That last one is
not cosmetic: it raises the leading floor and costs the family **five points of
drift** — a swept best of 10 against this recipe's 2.

**Junicode is variable**, so neither stock weight had to be accepted. Instanced
at `wght 550 / wdth 125 / ENLA 14` it is 99% of Doves' stroke, has the closest
counters of the three, and fits inside the roman's own ink so it costs no
leading at all. `ENLA 14` matches `InknutJunicode`'s instance of the same file —
the enlarged-x-height axis, which is what makes the per-style scale land near
1.0.

**Per-style scale on the italic is the schema's FIRST sanctioned use** — a
mixed-source family whose roman and italic were never drawn to a shared
x-height.

**It shipped wrong the first time, at `0.985`, and the owner caught it by eye**:
*"x height seems too low, height of uppers is too low"* — of the italic, not the
roman. He was right. The sweep that produced 0.985 had an arithmetic error in
its cross-family ratio, and the built italic rendered **8% small**: x-height 12
against the roman's 14 and cap 19 against 22 at slot 3.

**The lesson, and it is cheap to apply:** a computed cross-family scale must be
verified by measuring the two BUILT faces against each other, at every slot, not
by trusting the arithmetic that produced it. The sweep measures candidate
sources; only the built pair proves the fit. Two lines of freetype.

**The corrected value is `1.130`**, fitted against both things the report
named, measured from the built sources:

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| roman x-height | 8 | 10 | 12 | 14 | 16 | 18 |
| italic x-height | 8 | 10 | 12 | 15 | 17 | 19 |
| roman cap | 13 | 16 | 19 | 22 | 25 | 29 |
| italic cap | **13** | **16** | **19** | **22** | **25** | **29** |

Caps land exactly at all six slots; x-height matches at the lower three and runs
1 px generous at the upper three. **The two cannot both be exact** — Doves'
capitals are 1.61× its x-height and Junicode's are 1.50×, so matching one
overshoots the other. Caps were made exact and the x-height allowed to run over,
because the report was that both read LOW and generous is the direction of the
complaint. Matching x-height exactly instead is `scale: 1.065`, which leaves the
caps 2–3 px short.

**Leading was unaffected by THAT correction**: the ink floor stayed 1380 and the
drift unchanged at every scale from 0.985 to 1.14, so the scale fix cost nothing
elsewhere. (The drift figure quoted here was 2 and is really 3 — see §7.2. The
italic swap the next day moved it to 5.)

### 7.2 Slots

`scale: 1.14`, ramp `8/10/12/14/16/18` — the point sizes land on the slot
numbers.

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| x-height | 8 | 10 | 12 | 14 | 16 | 18 |
| advY, as shipped **2026-09-07** | 23 | 29 | 35 | 41 | 47 | 52 |
| advY, the Junicode fit it replaced | 23 | 29 | 35 | 40 | 46 | 52 |

**CORRECTED TWICE, and both corrections are worth keeping.**

This section first claimed `23/29/34/40/46/52` and **drift 2**. Two things were
wrong with that. Slot 2 measured **35**, not 34 — found by the WarblerText build
validating its advY model against real files rather than against this table, so
the Junicode fit's real drift was **3**, not 2. And the italic changed the next
day: Doves takes **Coelacanth's** italic now (owner ruling; see
`docs/font-dates.md`), whose deeper extenders lift the shared ink floor, so the
shipped leading is `23/29/35/41/47/52` and the drift is **5**.

Metrics moved with it: **`998/−402`**, span 1400, against the Junicode fit's
`1018/−362`. Coelacanth's descenders need 400/1000 where Junicode's needed 360,
so descent grew and ascent gave way; accented capitals now poke 82/1000 em
rather than 62. Descenders win, as everywhere else.

**The lesson is the first correction, not the second.** A figure in a doc is not
a measurement — it was copied from a sweep's prediction and never re-read off
the built file, and it survived a build, a ship and a review before another
family's arithmetic caught it. Read advY out of the `.cpfont` headers or the
render harness, both of which print it.

### 7.3 The missing `fi`, and a new merge path

Doves **draws `fi`, cmaps it at U+FB01, and its own `liga` never reaches it** —
the feature covers ff, fl, ffi and ffl and omits the commonest ligature in
English. Four pairs extracted where five were drawn.

`synth_ligatures:` could not fix this as it stood: it builds a GSUB with feaLib,
which **replaces** the table, and Doves carries twenty other features that would
have gone with it. The stage now has a second path:

- **No GSUB, or no `liga`/`rlig` lookup** → build one with feaLib, as before,
  still refusing a face with other features it would destroy.
- **A ligature lookup already exists** → **merge**: append a `LigatureSubst`
  entry to the face's own lookup with fontTools, touching no other lookup, no
  other feature, and no other table.

Verified on a `/tmp` copy: GSUB features 21 → 21, GPOS `kern`/`mark`/`mkmk`
unchanged, glyph order identical, only `DSIG` removed, kern pairs 18,852 →
18,852, and ligatures `FB00/FB02/FB03/FB04` → **`FB00/FB01/FB02/FB03/FB04`**.

**An already-present rule is now a skip, not a refusal**, and Doves is exactly
why: `synth_ligatures:` is a FAMILY key, the italic is Junicode, and Junicode
has had f+i all along. Refusing the build because one style already does what
was asked would make the key unusable on the families that most need it. The
post-condition gate still proves the ligature through the real extractor, so a
face that only *appears* to have the rule is still caught.

**Not fixed, and deliberately:** Doves draws eight more ligatures — `f_b`,
`f_h`, `f_j`, `f_k` and the four `ff`-prefixed ones — as unencoded `.liga`
glyphs. The converter can only carry a ligature whose output has a codepoint,
so they are dropped with a warning, exactly as Edgar's were before someone
PUA-encoded that face's set. Doing the same here is a separate job.

### 7.4 Built

Renderer: `line 23/29/34/40/46/52`, `ligs=5` in every style, `kernL=246
kernR=279` — the richest kern classes in the file by a wide margin.

**Unconfirmed on device:** both bolds are synthetic (0.043 roman, 0.036
italic), and the borrowed italic's *fit* was judged from a 1× plate at slot 3,
where it reads as the same printer's ink. Whether it still does at slot 0 on
glass is the part only the phone can answer.
