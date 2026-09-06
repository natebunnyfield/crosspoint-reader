# The DTL trial pair — build notes and measurements

*DTL Fleischmann and DTL Romulus, built the same day for the same iOS
TestFlight trial. Fleischmann is §1–§6; Romulus is §7. Both are commercial
(Dutch Type Library), both are Latin-only Mac-charset cuts, and **neither has a
`liga` feature** — that shared defect is §5.*

> ## PROMOTED, same day: Romulus is the ninth installed family
>
> Owner ruling 2026-09-06, *"add"*: DTL Romulus goes into
> `installed_families:` and ships on **every** surface — device SD cards, the
> simulator's `fs_/fonts/` (installed at 1x, 2x and 3x), and the iOS seed set.
> Its name came back out of `CROSSPOINT_IOS_TRIAL_FAMILIES`, which is FATAL to
> leave alongside `installed_families:`.
>
> **Two things about this promotion are firsts, and both are load-bearing:**
>
> 1. **It is the first COMMERCIAL family in `installed_families:`.** The other
>    eight rebuild from a URL or a committed file on any machine; this one's
>    outlines are licensed and gitignored, so a clean clone builds eight
>    families and skips the ninth with a note. Nothing in the repo can fix
>    that for someone who does not have the files.
> 2. **The trial it was built for never ran.** The build carrying it could not
>    be archived — codesign `errSecInternalComponent`, and there is no Apple
>    Distribution certificate on the Mac (the Development cert present was
>    issued 2026-09-04, two days after the last archive that worked). So the
>    ruling came from rendered specimens, not from the phone. Everything below
>    is measured; none of it is device-confirmed.
>
> ## Earlier the same day: Romulus stays, Fleischmann is cut
>
> Owner ruling 2026-09-06, after seeing both rendered at all six slots:
> *"drop dtlfleischmann, keep romulus."*
>
> **DTL Romulus** is on the iOS seed tree
> (`~/src/crosspoint-simulator/build/seedfonts/DTLRomulus/`, 1x + 2x) and is
> the family the TestFlight build carries. It is still NOT in
> `installed_families:` — it reaches no device SD card and no simulator default
> install, and promoting it needs its own ruling.
>
> **DTL Fleischmann** is C-tier and gone from every surface: the build output,
> the iOS seed tree and the render harness's font root were all deleted. It
> reached no device card, so there was nothing else to strip. Its recipe,
> picker row and everything in §1–§6 below stay deliberately — the sources are
> gitignored and the sweep took real work, so a future reconsideration should
> not have to pay for it twice. `python3 build-sd-fonts.py --only
> DTLFleischmann` rebuilds it exactly as measured here.
>
> **Ligatures were added later the same day** — see §8. Romulus now ships
> `fi` and `fl`.
>
> The seed gate after the removal: `seed fonts OK: 9 families, 1x + tiers up to
> 2x`. The pair's +19.1 MB bundle cost in §7.5 is now Romulus alone. Re-measured
> after the cut, not scaled down from the pair's figure:
> `12 written; 37,802,942 -> 10,512,787 bytes (0.278)` — **+10.5 MB to the
> installed app**, 8.6 MB less than the pair.

---


*2026-09-06. Firmware tree at `HEAD` on branch `main`; recipe added to
`lib/EpdFont/scripts/sd-fonts.yaml`, display row to `src/FontDisplayNames.h`,
lineage row to [docs/font-dates.md](font-dates.md). Every number below is
measured, not derived — the sweeps are reproducible from the sources named.*

Built for an **iOS TestFlight trial and cut the same day** — see the outcome
box above. Everything in this section is a live measurement of a family that is
no longer on any surface; it is kept so the face can be rebuilt or reconsidered
without re-deriving any of it.

## 1. DTL Fleischmann — the source

Four "T" (text) cuts, found in `~/Library/Mobile Documents/com~apple~CloudDocs/dtl/`
and staged into `lib/EpdFont/local_fonts/` (gitignored — the face is commercial,
Dutch Type Library, and the outlines are never committed):

| Staged name | Source file | Bytes |
|---|---|---|
| `DTLFleischmann-Regular.ttf` | `DTLFleischmann-TRegular.ttf` | 101,740 |
| `DTLFleischmann-Bold.ttf` | `DTLFleischmann-TBold.ttf` | 118,220 |
| `DTLFleischmann-Italic.ttf` | `DTLFleischmann-TItalic.ttf` | 100,004 |
| `DTLFleischmann-BoldItalic.ttf` | `DTLFleischmann-TBoldIt.ttf` | 119,300 |

All four: `unitsPerEm` 1000, 248 glyphs, `Version 001.000`, TrueType outlines
with `fpgm`/`prep`/`cvt ` (so they are natively hinted and the pixel ramp
below is hinting's, not linear scaling's).

**Three properties of the source drove every decision that follows.**

**(a) Two of the four faces carry `hhea` 0/0/0.** Regular and Italic ship
`ascent = descent = lineGap = 0`; Bold is 744/-200/71 and Bold Italic
741/-200/71. A `.cpfont`'s `advanceY` is `norm_ceil(face.size.height)`
([fontconvert_sdcard.py:1178](../lib/EpdFont/scripts/fontconvert_sdcard.py)),
and FreeType computes that from `hhea`, so two of the four styles would have
fallen back to the bbox and taken a leading no one chose. A `metrics:` override
was therefore not a refinement here, it was mandatory.

**(b) It is a Mac-charset cut: 247 codepoints in the Regular and Italic, 245 in
the Bold and Bold Italic.** Full ASCII (95/95) and Latin-1 (95/128 — the 33
absent are C1 controls and the soft hyphen, so the block is complete as far as
printable characters go), ten of Latin Ext-A, one of Latin Ext-B, the accent
block U+02C6–02DD, four Greek letters used as symbols (Δ Ω μ π), the
quote/dash/dagger tail, `fi` and `fl`, and about twenty math signs.
**No Greek, no Cyrillic.** With `intervals: reading` the converter pruned 500
requested codepoints no face in the chain could supply — the same list the other
Latin-only families print — and the Noto/Schola fallback chain supplied Greek,
Cyrillic and the symbol tail. The built files carry 61 intervals and 2,676
glyphs per style, which is the tier's normal shape.

**(c) There is no `liga` feature at all.** `GSUB` is present but holds **0
features and 0 lookups**; `GPOS` holds `kern` only. `fi` and `fl` outlines exist
and are cmapped at U+FB01/FB02, but nothing substitutes `f`+`i` for them, so the
built files report `ligs=0` for every style where Edgar reports 13. **This is
the source's own state, not a build defect**, and it is the one open quality
question on this family — see §5.

## 2. Sizes: why `scale: 0.97` and the ramp 8/10/12/14/16/18

The tier is harmonized by SLOT, not by point size: the same ordinal slot must
show the same measured x-height (8/10/12/14/16/18 px) and the same `advanceY`
(23/28/34/40/46/51 px) across families
([sd-fonts.yaml](../lib/EpdFont/scripts/sd-fonts.yaml), 2026-08-26 six-slot
extension).

Swept the unmodified Regular at 6–24 pt through FreeType at 150 DPI with
`FT_LOAD_RENDER`, which is exactly what the converter does, and measured the
`x` bitmap's row count:

| pt | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| x-height px | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 19 | 20 | 21 |

**18 px is unreachable.** The hinting jumps 17 → 19 between 17 pt and 18 pt, so
no point size lands the top slot; native, the best ramp available is
8/10/12/14/16/**19**, a whole pixel over target at slot 5 only.

Swept `scale` k from 0.88 to 1.08 in 0.01 steps (k rewrites `head.unitsPerEm` to
`round(1000/k)` and changes nothing else,
[build-sd-fonts.py:504-511](../lib/EpdFont/scripts/build-sd-fonts.py)), and at
each k re-picked the best six point sizes:

| k | ramp | x-heights | notes |
|---|---|---|---|
| 0.88 | 9/11/13/16/18/20 | 8/10/12/14/16/18 | exact, but the ramp is 3 pt wider than the tier's |
| 0.91–0.92 | 8/11/13/15/17/19 | 8/10/12/14/16/18 | exact |
| 0.93–0.94 | 8/10/13/15/17/19 | 8/10/12/14/16/18 | exact |
| 0.95 | 8/10/12/15/17/18 | 8/10/12/14/16/18 | exact |
| 0.96 | 8/10/12/14/17/18 | 8/10/12/14/16/18 | exact |
| **0.97** | **8/10/12/14/16/18** | **8/10/12/14/16/18** | **exact, and the point sizes are the slot numbers** |

**k = 0.97 is the value closest to 1.0 that lands all six**, and it is the only
one whose point sizes and x-heights coincide, which makes the recipe readable.
It is a measured slot fit — the same class of correction as Edgar's 0.96 and
TeX Gyre Schola's 0.947, not a preference.

**It is NOT the 2026-08-27 words-per-page normalisation.** DTL Fleischmann has
not been through the Almendra-anchored paginator measurement
([docs/almendra-anchored-sizing-2026-08-27.md](almendra-anchored-sizing-2026-08-27.md)),
and it must be before the family could join the tier. Flagged, not done.

## 3. Metrics: `ascent: 1046, descent: -320`

`advanceY` per size is `round(span/1000 × ppem)` where `span = ascent − descent
+ linegap` and `ppem = pt × 150/72`; the `metrics:` values are per-1000-upem and
are rescaled to the patched `upem` of 1031 on the way in
([build-sd-fonts.py:432-437](../lib/EpdFont/scripts/build-sd-fonts.py)).

Solving `round(span/1000 × ppem) = target` for all six slots at once gives a
window of **span ∈ [1365, 1368]** per 1000 — three units wide, which is why the
number could not be eyeballed:

| slot | pt | ppem | advY target | admissible span/1000 |
|---|---|---|---|---|
| 0 | 8 | 16.667 | 23 | 1.350–1.410 |
| 1 | 10 | 20.833 | 28 | 1.320–1.368 |
| 2 | 12 | 25.000 | 34 | 1.340–1.380 |
| 3 | 14 | 29.167 | 40 | 1.354–1.389 |
| 4 | 16 | 33.333 | 46 | 1.365–1.395 |
| 5 | 18 | 37.500 | 51 | 1.347–1.373 |

Slot 4 sets the floor and slot 1 the ceiling. **1366 is the middle of the
window**; the split into 1046/−320 puts the slack into the ascender, where it
buys accent clearance.

Verified against all four styles at all six sizes (measured ink extents over
the plain-text set plus the 26 accented capitals `ÀÁÂÃÄÅÇÈÉÊËÌÍÎÏÑÒÓÔÕÖÙÚÛÜÝ`):

| pt | advY | asc | desc | worst ink top (style) | worst ink bottom |
|---|---|---|---|---|---|
| 8 | **23** ✓ | 18 | −6 | 17 (regular, bolditalic) | −5 |
| 10 | **28** ✓ | 22 | −7 | 21 (regular) | −6 |
| 12 | **34** ✓ | 27 | −8 | 25 (regular) | −7 |
| 14 | **40** ✓ | 31 | −10 | 29 (regular) | −8 |
| 16 | **46** ✓ | 35 | −11 | 32 (regular, italic, bolditalic) | −9 |
| 18 | **51** ✓ | 40 | −12 | 37 (regular) | −10 |

**No clipping anywhere**: the declared ascender clears the accented capitals at
every size in every style (worst case 40 vs 37 at 18 pt), and the declared
descender clears the descenders at every size (worst case −12 vs −10). The
plain-ink-span + 0.13 em floor is satisfied with room to spare at both ends
(18 pt: span 39 px + 4.9 = 43.9 against 51; 8 pt: 18 + 2.2 = 20.2 against 23).

**Confirmed through the real renderer, not only through FreeType.**
`tools/calendar_preview/render_harness inline DTLFleischmann` reports
`line 23/28/34/40/46/51px` across slots 0–5 — the firmware's own
`GfxRenderer`/`SdCardFont` path agreeing with the table above.

## 4. What was built, and where it is

```
lib/EpdFont/scripts/output/DTLFleischmann/
    DTLFleischmann_{8,10,12,14,16,18}.cpfont          6.3 MB   (1x)
    2x/DTLFleischmann_{8,10,12,14,16,18}.cpfont      21.2 MB   (--scale 2)
```

Copied to `~/src/crosspoint-simulator/build/seedfonts/DTLFleischmann/`, which is
the tree `ios/testflight.sh` ships (NOT `ios/seedfonts/`, which was deleted
2026-08-26 as a stale trap). No 3x — it is above the bundling ceiling and is not
shipped.

`tools/validate_seed_fonts.py` passes on the combined tree:
`seed fonts OK: 9 families, 1x + tiers up to 2x, header-verified against
sd-fonts.yaml`. That is the check that the built `advanceY` and the recipe's
ramp actually agree, so §3's table is confirmed from the file headers as well as
from the rasterizer.

Each style carries GPOS kerning: 61 left classes and 72–74 right classes,
reduced to a 36×38 mini-kern matrix at load.

## 5. The missing f-ligatures (SOLVED for Romulus in §8)

*This section describes DTL Fleischmann's state as built, and it is kept as
written because the recipe is kept. The same defect in DTL Romulus was
solved later the same day by a new build stage — see §8 — and option 2 below,
which this section called "new build machinery", is the option that was
taken. Reviving Fleischmann would now be `synth_ligatures: [fi, fl]` and a
rebuild; it draws only those two as well.*

`ligs=0` in all four styles. The `fi` and `fl` outlines are present and cmapped
(U+FB01/FB02) but the source has no `liga` feature to reach them, and
`fontconvert_sdcard.py` reads ligature pairs from `GSUB` `liga`/`rlig` only.
A Dutch baroque text face is exactly the kind of face where the f-ligatures
matter, so this is worth a decision rather than a silent acceptance. The
options, none of them taken:

1. **Ship as is.** The inline specimens at every slot read cleanly and no
   collision is visible at 1x; Fleischmann's `f` has a short, high-arching
   terminal, which is likely why. Costs nothing, loses the ligatures.
2. **Patch a `liga` into the staged sources** (f+i→fi, f+l→fl, and f+f→ff /
   f+f+i / f+f+l if the outlines exist — they do not here; only `fi` and `fl`
   are drawn). This is new build machinery: nothing in this repo injects GSUB
   today. `add_pua_ligs.py`, referenced by Edgar's recipe comment, is not in
   this tree.
3. **Ask DTL for the OpenType cut.** The "T" files here are the older TrueType
   release; DTL's own shop lists OpenType "Text" and "Display" families which
   presumably carry the features, and small caps besides.

Also open, and listed so the next pass does not re-derive them:

- **The italic runs one x-height pixel over the roman at every slot**
  (9/11/13/15/17/19 against 8/10/12/14/16/18). That is the designer's
  relationship, not a build artifact, and the mixed-source `scale:` remedy
  explicitly does not apply — the roman and italic are one designer's work from
  one release. Left alone.
- **Words-per-page normalisation not run** (§2).
- **Bold at slot 4 measures x-height 15, one under the roman's 16.** Its own
  hinting skips the value; nothing in the recipe can fix only that slot.

## 6. Checked and found clean

Recorded so the next pass does not repeat them:

- All four styles' cmaps agree to within the two codepoints the bolds lack.
- The 2x tier's `_8` file is byte-for-byte the size of the 1x `_16` file
  (1,403,436), which is the expected consequence of a 2x cut of the 8 pt slot
  rasterizing at 16 pt and being renamed back.
- `src/ReadingFontList.cpp` filters only a `kRetired[]` list holding Rosarivo,
  so nothing hides the new family from the picker.
- The `PRUNED` list is identical across all four styles and matches the shape
  the other Latin-only families print; no style lost a range the others kept.


---

## 7. DTL Romulus

Van Krimpen's 1931 Enschedé roman, digitised by Frank E. Blokland for DTL.
Same trial, same terms, same day. **Its recipe answers the slot problem the
opposite way to Fleischmann's, and that is the interesting part.**

### 7.1 The source

Four "T" cuts, staged into `lib/EpdFont/local_fonts/` as
`DTLRomulus-{Regular,Bold,Italic,BoldItalic}.otf`. **CFF/OTF, not TrueType** —
so FreeType's CFF hinting engine, not the TrueType interpreter, decides the
pixel ramp.

| | Regular | Bold | Italic | Bold Italic |
|---|---|---|---|---|
| `unitsPerEm` | 1000 | 1000 | 1000 | 1000 |
| `OS/2` x-height | 398 | 418 | 398 | 418 |
| `OS/2` cap height | 662 | 662 | 662 | 662 |
| cmap entries | 247 | 249 | 247 | 247 |
| `GSUB` | **absent** | **absent** | **absent** | **absent** |
| `GPOS` | `kern` | `kern` | `kern` | `kern` |

The name table dates the files: `Copyright Dutch Type Library, 2003`, generated
by DTL DataMaster.

**No `GSUB` table at all**, so §5 applies here too and slightly harder — with
Fleischmann the table is present and empty, here it does not exist.

**x-height 398/1000 makes this the smallest-on-body face in the whole recipe
file**, against DTL Fleischmann's 484 and a tier norm near 480. Everything in
§7.2 and §7.3 follows from that one number.

### 7.2 Sizes: no `scale:`, and the tall ramp 9/12/14/17/19/22

Two ways to put it on the slots were measured, and **they render within about
2% of each other** — matching x-height fixes the rendered size whichever lever
gets you there:

| | ramp | x-heights | scale |
|---|---|---|---|
| (a) | 8/10/12/14/16/18 | 8/10/12/14/16/18 | 1.18–1.23 (a six-wide plateau, all identical) |
| (b) | **9/12/14/17/19/22** | **8/10/12/14/16/18** | **none** |

**(b) is taken**, on the Venetian 301 and Coelacanth precedent: both are
small-on-body faces, both answer with a tall point ramp, and neither carries a
`scale:` for it. It also keeps `scale:` meaning what its schema comment says it
means — a 1.20 would be the largest in the file by a wide margin and would read
as a styling knob rather than a measured correction.

The x-heights are **exact at all six slots** in the regular.

### 7.3 Metrics: `877 / −380`, and why the leading cannot hit the target

At matched x-height this face's ink does not fit the tier's leading. Measured
across all four styles at the six sizes:

| slot | pt | ppem | plain ink span | floor = ink + 0.13 em | advY target | advY built |
|---|---|---|---|---|---|---|
| 0 | 9 | 18.75 | 21 | 24 | 23 | **24** |
| 1 | 12 | 25.00 | 27 | 31 | 28 | **31** |
| 2 | 14 | 29.17 | 33 | 37 | 34 | **37** |
| 3 | 17 | 35.42 | 39 | 44 | 40 | **45** |
| 4 | 19 | 39.58 | 43 | 49 | 46 | **50** |
| 5 | 22 | 45.83 | 50 | 56 | 51 | **58** |

**The floor exceeds the target at every slot**, so the family is FLOOR-BOUND
exactly as Coelacanth and Venetian 301 are. Van Krimpen's extenders are long
and his x-height is short; at the top slot the ink alone is 50 px against a
51 px target line. `877/−380` is the minimum ink-safe leading, not a
preference, and it lands +1/+3/+3/+5/+4/+7 over target — between Coelacanth's
+2..+4 and Venetian 301's +4..+7.

The split is the Coelacanth split (`877/−362`) with eighteen more units of
descent, and it is chosen the same way: **descenders win**. Verified across all
four styles at all six sizes:

- **Descenders clear everywhere.** Worst case is the bold italic at 22 pt:
  declared −18 px against ink at −16.
- **Accented capitals poke 1–3 px above the declared ascender** at every slot.
  No span inside the ink floor covers both, which is the same trade Venetian
  301's recipe records ("accents may still poke 1-3 px over the declared
  ascender... descenders win").
- Dropping the descent to −377 would buy 1 px of leading at two slots and clip
  a descender at 14 pt. Not taken.

**Confirmed through the real renderer**: `render_harness inline DTLRomulus`
reports `line 24/31/37/45/50/58px` across slots 0–5, matching the table above.

### 7.4 Built, installed, verified

```
lib/EpdFont/scripts/output/DTLRomulus/
    DTLRomulus_{9,12,14,17,19,22}.cpfont          8.5 MB   (1x)
    2x/DTLRomulus_{9,12,14,17,19,22}.cpfont      29.4 MB   (--scale 2)
```

Copied to `~/src/crosspoint-simulator/build/seedfonts/DTLRomulus/`.
`tools/validate_seed_fonts.py` passes on the combined tree: **`seed fonts OK:
10 families, 1x + tiers up to 2x, header-verified against sd-fonts.yaml`**.
Kerning is the richest of the pair — **82 left classes and 86 right** (87 in
the bold at the top three sizes and in every 2x file), against Fleischmann's
61 × 72–74. Measured from the built files, post-ligature.

### 7.5 What the trial costs the app

Both families bundled at 1x + 2x, run through
`crosspoint-simulator/tools/compress_seed_fonts.py`:

```
both families:  24 files, 65,334,186 -> 19,089,510 bytes (0.292)   [superseded]
DTL Romulus:    12 files, 37,802,942 -> 10,512,787 bytes (0.278)   [shipped]
```

**+10.5 MB to the installed app** for Romulus, against a bundle that was
~202 MB with eight families. Both figures measured 2026-09-06, neither
estimated; the pair's line is kept because it is what the cut was decided
against. Undoing a family is one directory deletion — `CrossPointFsPrep.cpp`
seeds whatever `build/seedfonts/` holds and prunes the rest — which is exactly
how DTL Fleischmann came back out the same day.

### 7.6 One thing not to report as a bug

**Romulus's sloped form is an oblique, not a true italic**, by van Krimpen's
own design — Wikipedia's entry says so in the same breath as the date. The
inline specimens therefore show a sloped roman beside the upright rather than a
cursive, and that is the typeface, not the build. It is the historically
interesting half of the face: Romulus was drawn with the sloped roman as a
deliberate position in the 1930s argument about what an italic is for.

### 7.7 Checked and found clean (Romulus)

- x-heights exact at all six slots in the regular; bold runs +1 at slots 1, 3,
  4 and 5, which is its own hinting and not a recipe fault.
- Descender clearance holds in all four styles at all six sizes — the
  constraint the metrics were solved against, re-measured after the build.
- The 2x tier is a complete six-file set; no orphans, and the validator's
  companion check passes.
- The bold's stock `OS/2` typo metrics differ from the other three
  (717/−287/74 against ~920/−250/88) and its `hhea` differs again
  (949/−361 against 920/−315). All four are overridden, so the inconsistency
  reaches nothing — worth recording only so nobody re-derives it as a finding.


---

## 8. Synthesising the missing `liga` (2026-09-06, later the same day)

Owner: *"is it possible to generate ligatures?"* — yes, and it is now built.

### 8.1 What was wrong

Romulus **draws** `fi` and `fl` and **cmaps** them at U+FB01/FB02, and carries
**no `GSUB` table at all**. So both outlines shipped unreachable: every style
built with `ligs=0` where Edgar builds 13. The reader was never the problem —
`EpdFont::applyLigatures` is a draw-time walk over the `.cpfont` pair table,
and `LigatureControl` keys its per-pair toggles on the INPUT pair, so a new
pair gets its own switch in Typography Settings with no code change.

`ff`, `ffi` and `ffl` are **not** obtainable: this face draws only `fi` and
`fl` among its 251 glyphs. Nothing here draws an outline.

### 8.2 The new build stage

`apply_synth_ligatures()` in `build-sd-fonts.py`, driven by a new optional
recipe key:

```yaml
synth_ligatures: [fi, fl]
```

It writes a two-rule `liga` feature into a cached copy of the source, between
the cmap-drop stage and the metrics patch, mtime-cached into
`patched_fonts/<family>/liga/` and written atomically — the same shape as the
two patch stages that already existed. Allowed names are the presentation
forms only (`ff fi fl ffi ffl st`), because
`fontconvert_sdcard.py`'s `is_presentation_ligature()` refuses anything else
downstream anyway and is right to: nothing in this pipeline knows the book's
language, and `oe`/`ae`/`ij` are letter substitutions.

Three refusals, all fatal rather than silent:

* a name whose **glyph or components the face does not draw** — patching is not
  drawing;
* a ligature glyph with **no cmap entry** — `fontconvert` would drop the pair;
* a source that **already has `liga`/`rlig`** — a face that gains one in a
  later release gets re-examined, not patched over.

Proven before it was built, by running the repo's own extractor against a
scratch copy:

```
before: GSUB present? False
after:  GSUB present? True   feats ['liga']
extracted ligature pairs: [(6684777, 64257), (6684780, 64258)]
```

`0x66<<16|0x69 -> 0xFB01` and `0x66<<16|0x6C -> 0xFB02`.

### 8.3 The bug this stage shipped for one build, and the gate that now stops it

**`addOpenTypeFeaturesFromString(font, fea)` deleted the face's kerning.**
feaLib builds every table the feature file *could* describe and replaces them,
and a `.fea` carrying no positioning rules builds an **empty GPOS**. All four
styles lost GPOS.

It was nearly invisible. Three of the four styles also carry a **legacy `kern`
table**, which `extract_kerning_fonttools` reads as well, so they went on
reporting `kernL=82, kernR=86` from the fallback path. **Only the bold showed
it** — `DTLRomulus-Bold.otf` has no legacy `kern` table, so it built with
`kernL=0, kernR=0`. Measured on the untouched sources, all four hold about the
same kerning: regular 1409 pairs, bold 1412, italic 1410, bold italic 1417.

Two changes, both in `apply_synth_ligatures`:

1. **`tables={"GSUB"}`** on the feaLib call, so it builds the one table it is
   here to build.
2. **A post-condition gate**: the GPOS lookup count is counted before and
   after, and a change raises. A comment would not have caught this — the first
   version *had* comments about being careful, and shipped anyway. The check
   fails the build.

After the fix, all four styles: `ligs=2, kernL=82, kernR=86`, and the line
advances are unchanged at 24/31/37/45/50/58.

### 8.4 What the reader gets

Two switches in Typography Settings, `fi` and `fl`, each individually
toggleable, for free — no firmware change was needed or made.


---

## 9. Adversarial review, and what it changed

Run before the TestFlight upload, read-only, against the whole uncommitted
change. It produced eight findings; four were acted on and are fixed in §8's
stage, three are recorded here as accepted, one was a doc error.

### 9.1 Fixed

1. **The stage had a gate proving nothing was LOST and none proving anything
   was GAINED.** Three silent routes from "wrote `liga`" to `ligs=0` in the
   built file, all invisible because `build-sd-fonts.py:1019-1021` echoes only
   child-stderr lines containing `PRUNED` on success, and fontconvert's own
   `Ligatures: N pairs` goes to that same discarded stream:
   `intervals:` not covering U+FB00–FB06 (only `latin-ext`, `reading` and
   `builtin` do); `ffi`/`ffl` requested without `ff`; a ligature glyph mapped
   outside U+FB00–FB06. Fixed with `_verify_synth_ligatures()`, which runs
   fontconvert's **own** `resolve_intervals` + `extract_ligatures_fonttools`
   against the patched file and fails the build if a requested output
   codepoint is not visible. It runs on the **cache-hit path too** — the
   patched file does not depend on `intervals:`, so a family that later
   narrowed them would otherwise keep a file that is fine on its own terms and
   produces nothing.
2. **The cache key had no code component.** `<style>_<names>_<mtime>` does not
   move when this file is edited, so a patched file written by an older,
   wronger version of the stage stayed eligible forever — exactly what happened
   across the `tables={"GSUB"}` fix. Added `SYNTH_LIGATURE_STAGE_VERSION` and a
   hash of the component table into the filename; verified that bumping the
   version changes the cached name.
3. **feaLib REPLACES GSUB rather than merging.** The refusal only guarded
   `liga`/`rlig`, so a face carrying `smcp`, `onum`, `frac`, `dlig` would have
   lost them silently — the same shape as the GPOS bug. Reproduced on a copy
   carrying `frac`+`smcp`: two lookups in, one `liga` lookup out, no warning.
   Now **any** pre-existing GSUB feature is refused.
4. **Glyph names were assumed rather than resolved.** `sub f i by fi;`
   addresses glyphs by NAME, and the stage checked that a glyph called `fi`
   existed. A face using AGL names (`f_i`, `uniFB01`) was told it "does not
   draw" a ligature it draws, and a face whose `fi` glyph was mapped somewhere
   other than U+FB01 was accepted and then silently dropped downstream as a
   letter substitution. `SYNTH_LIGATURE_COMPONENTS` is now
   `SYNTH_LIGATURE_CODEPOINTS` and every glyph is looked up through the face's
   own cmap. `getBestCmap()` also gained `or {}` — it returns `None` for a
   Mac-only cmap, and `build_family` catches `RuntimeError` only, so the
   `AttributeError` would have killed the whole run instead of one family.

All seven refusals and both cache behaviours were then re-tested on `/tmp`
copies: happy path, cache hit, intervals without FB00–FB06 (refused **on the
cache-hit path**), `ffi` without `ff`, undrawn `ff`, a non-presentation name,
a pre-existing `frac`, an unmapped `fi`, and a stage-version bump busting the
cache.

### 9.2 Accepted, not changed

- **`install-sim-fonts.py` can now prune `fs_/fonts/DTLRomulus`.** Its `known`
  set is the yaml's `families:` and its `keep` set is `installed_families:`
  plus the editor group, so adding the recipe removed the "a directory with no
  recipe is the owner's own font" protection this family previously had by
  accident. This is the designed behaviour for every buildable-but-not-installed
  family in the file, not a regression particular to this one. No DTL directory
  exists under `fs_/fonts` or `fs_/.fonts` today.
- **The stale-file sweep runs before validation**, so a call that then raises
  has already deleted the previous good cached file. It costs a rebuild and
  fails loudly. `apply_cmap_drops` and `apply_metrics_override` both do the same
  thing; changing one of three would be worse than leaving all three.
- **`src/activities/settings/ColophonData.h:306`** reads *"48 buildable
  families. Six installed"*; the yaml now holds 54 and `installed_families:`
  holds 8. **Already wrong before this change** — it is a pre-existing drift
  this change makes two staler. Left alone deliberately: it is outside what was
  asked for, and it is a user-visible string that should be corrected on its
  own, against a counted number.

### 9.3 Checked and found clean

The review's own clean list, kept because it is the half that stops the next
pass re-reading the same ground:

- **Every table diffed before/after the feaLib call, on all four styles.** Only
  `GSUB` added and `DSIG` removed. `hhea`, `OS/2`, `maxp`, `post`, `name`,
  `hmtx` and legacy `kern` are **byte-identical**. `CFF `, `GPOS`, `cmap` and
  `head` differ in bytes but were checked semantically: glyph order identical
  (251, same list), all 251 CFF charstrings byte-identical, all three cmap
  subtables identical dict-for-dict, `head.modified` preserved (so
  `recalcTimestamp=False` holds through feaLib) and only `checkSumAdjustment`
  moved. No `GDEF` synthesised. The bold's CFF deltas are default-elision
  (`BlueScale` 0.039625, the CFF default; TopDict `Encoding` 0 → `None`, which
  OTF ignores in favour of cmap).
- **Kerning survival**, the bug that shipped once: 1410/1412/1411/1418 pairs
  before and after, zero lost, zero gained, zero value changes; GPOS lookup
  count and feature list unchanged; the legacy `kern` table still present in
  three styles and still absent in the bold. From the other end, all 24 built
  and seeded `.cpfont` files read `kernL=82`, `kernR=86/87` — no `kernL=0`
  anywhere.
- **The GPOS gate's branches**, all four traced: no vacuous-pass path. feaLib
  replaces GPOS with one whose `LookupList` is `None`, counted as 0, which
  mismatches and raises.
- **Blast radius on other families**: `synth_ligatures` appears twice in the
  yaml (schema comment and DTLRomulus). 54 families; the full key vocabulary
  across all of them is `description, drop_codepoints, force_autohint,
  hires_drops, intervals, line_height_scale, metrics, name, sizes, styles,
  synth_ligatures`. No family without the key can reach the stage. Stage
  ordering does not change any other family's input — each stage keys on its
  own input's mtime — and families build in separate processes that never share
  a `patched_fonts/<family>/`.
- **Sweep collisions**: `apply_metrics_override`'s `<style>_*` glob is
  non-recursive, so the `liga/` subdirectory survives it; `bold_*` cannot match
  `bolditalic_*`.
- **Shipped artifacts against the docs**: style TOCs parsed for all 12 built and
  12 seeded files. `advanceY` 24/31/37/45/50/58 at 1x and 47/63/73/89/100/115 at
  2x, four styles everywhere, `ligs=2` everywhere. `validate_seed_fonts.py`
  passes.
- **DTL Fleischmann's removal**: no `.cpfont` anywhere in either repo. What
  remains is `local_fonts/*.ttf` plus `patched_fonts/` and `scaled_fonts/`
  intermediates, all three covered by `.gitignore:34,35,38` — nothing
  commercial is committed.
- **`src/FontDisplayNames.h`**: compiles standalone, 34 entries, no duplicate
  directory keys, both new rows follow the file's stage-separator and
  `earliestYear` conventions, and row order is irrelevant (linear `find_if`).
- **`src/ReadingFontList.cpp:21`** `kRetired[]` still holds only `Rosarivo`, so
  nothing hides the new family from the picker.
