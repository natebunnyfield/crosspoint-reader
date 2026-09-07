# Dante, Lutetia Nova, Golden Cockerel — build notes and measurements

*2026-09-06, evening. Three commercial faces from the iCloud `dtl` folder,
built straight into `installed_families:` on the owner's instruction
("commit and ship"), taking the tier from nine families to twelve. Every
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

**Which weight is "bold".** Six weights, four slots. Stems measured at 50 ppem
from the middle row of `l`:

| face | stem | ratio to Regular |
|---|---|---|
| Regular | 60/1000 em | — |
| **Medium** | **100/1000** | **1.67×** |
| Bold | 120/1000 | 2.00× |

The tier's real bolds measure GT Alpina 1.50×, Edgar 1.75×, Venetian 301 2.50×,
and the repo's synthetic-bold target is ~1.5× stems. Medium sits in the middle
of that; Dante Bold at 2× is a display black in running text. **Medium and
Medium Italic are the reader's bold pair.** Bold and Bold Italic stay staged in
`local_fonts/` — two `path:` lines and a rebuild to reverse.

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
- **Dante's Medium-as-bold** was judged from the 1× plate at slot 3, where it
  reads as a true bold beside the Regular; it was not compared against Bold on
  device.

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
