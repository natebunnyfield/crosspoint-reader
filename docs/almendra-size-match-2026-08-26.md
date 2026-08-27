# Almendra reads small: which metric explains it, and the ramp that fixes it

*2026-08-26. Firmware `0d58d4e9d`, simulator seed tree
`crosspoint-simulator/build/seedfonts`. Every number below is READ BACK from
built `.cpfont` files or from the real `GfxRenderer` through
`tools/calendar_preview/render_harness`, never computed from the outlines —
the yaml's own header records that the computed column has been wrong by up to
three pixels before.*

Owner, 2026-08-26: *"increase all of almendra's font sizes to better match
other fonts."*

Earlier the same day the XS/XXS work had put Almendra's six cuts at
6/8/10/12/14/17 pt, chosen so its measured x-height hit the tier's 8/10/12/14/16/18 px
**exactly at all six slots**. It was the tightest x-height fit in the set. And
it still read small. So the interesting question is not "how much bigger" but
**why the x-height match was not enough**, because that answer decides the
size of the correction.

---

## 1. The measurement

Eight installed families, shipped 1x `.cpfont`s, regular style, every slot.
Ink is the sum of the stored 2-bit levels (0-3) divided by 3 — the count of
fully-inked pixel equivalents the file actually stores. The passage is a fixed
176-character sample.

### 1a. What matches, and what does not, at slot 3 (M)

| family | pt | x-height | cap | ascender (`h`) | mean advance | ink/char | chars/page |
|---|---:|---:|---:|---:|---:|---:|---:|
| Edgar | 14 | 14 | 20 | 22 | 15.02 | 68.15 | 780 |
| TeX Gyre Schola | 14 | 14 | 21 | 21 | 15.19 | 72.15 | 752 |
| Libris ADF | 14 | 14 | 21 | 21 | 12.20 | 54.64 | 911 |
| Coelacanth | 15 | 14 | 23 | 24 | 13.99 | 57.24 | 764 |
| Libre Franklin | 12 | 14 | 19 | 19 | 13.24 | 51.79 | 890 |
| TeX Gyre Heros | 12 | 14 | 19 | 19 | 12.23 | 56.29 | 933 |
| InknutJunicode | 12 | 15 | 20 | 22 | 15.74 | 75.17 | 741 |
| **Almendra (old)** | **12** | **14** | **18** | **20** | **11.81** | **41.20** | **1005** |

Read the columns as candidate explanations:

* **x-height** — 14, exactly the tier's. Explains nothing.
* **ascender height** — 20, inside the tier's 19-24 band. Explains nothing.
* **cap height** — 18, one under the band's floor of 19. A weak signal.
* **mean advance** — 11.81, below the band's 12.20 floor. A weak signal.
* **ink per character** — 41.20 against a band of 51.79-75.17. **20 % below
  the tier's own lightest family**, and the only column outside the band by
  more than a rounding step.

Scale the ink for size (ink per character ÷ x-height²) and the outlier gets
cleaner still: Almendra 0.210, Libre Franklin 0.264, Libris 0.279, TeX Gyre
Heros 0.287, Coelacanth 0.292, InknutJunicode 0.334, Edgar 0.348, Schola
0.368. **At matched x-height Almendra carries a fifth less ink per letter
than any other installed face.** That is what "reads small" is.

### 1b. And it is every slot, not one

Ink per character, all six slots, Almendra on its old ramp against the tier's
lightest and median:

| slot | 0 (XXS) | 1 (XS) | 2 (S) | 3 (M) | 4 (L) | 5 (XL) |
|---|---:|---:|---:|---:|---:|---:|
| Almendra, old ramp | 10.59 | 17.96 | 30.06 | 41.20 | 55.64 | 81.59 |
| tier's lightest | 17.87 | 29.28 | 35.19 | 51.79 | 70.99 | 91.64 |
| tier median | 20.62 | 31.60 | 45.75 | 57.24 | 83.00 | 103.10 |
| Almendra's shortfall vs lightest | −41 % | −39 % | −15 % | −20 % | −22 % | −11 % |

Worst at the small end, which is where the complaint is loudest on a 528 px
column.

### 1c. Page blackness — CORRECTED: this change DID fix it

**This section's conclusion was wrong and is superseded (2026-08-26, same day,
`docs/size-ramp-band-2026-08-26.md`).** It reported page blackness as the
residual the +2 leaves behind, on the reasoning that blackness is
scale-invariant so only a heavier roman could move it. Scale-invariance is true
and the inference from it was not: **the metrics span came down with the ramp,
and that is what moved it.** Measured 10.10–11.00% before and **11.42–12.19%**
after — in band at all six slots.

Proved at a FIXED point size, which is what separates the two causes: 6 pt on
the old 1543 span reads 10.89%, and on today's 1368 span reads **12.165%**,
matching the advanceY change 19 → 17 exactly. Nothing about the glyphs changed;
the lines got tighter, so the same ink covers less page.

**So the heavier roman this section calls for is not needed**, and the family
having none is not the limitation it was written up as. The original reasoning
is kept below because the scale-invariance measurement is sound and worth not
re-running — it is only the conclusion drawn from it that failed.

### 1c (superseded). Page blackness — the residual this change does NOT fix

Ink as a fraction of line-box area (advance width × advanceY), which is what
the eye integrates as type colour:

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---:|---:|---:|---:|---:|---:|
| Almendra, old ramp | 10.89 | 10.10 | 11.00 | 10.31 | 10.34 | 10.22 |
| tier band, low | 11.08 | 11.27 | 11.07 | 11.22 | 11.36 | 11.32 |
| tier band, high | 13.93 | 14.22 | 13.90 | 13.87 | 13.99 | 14.19 |

Almendra is under the band at every slot — and **this measure is
scale-invariant**. Sweeping the face 6-21 pt moves it between 9.98 % and
10.66 % with no trend. Raising the point size cannot fix it; only a heavier
roman could, and the family's only heavier roman is the Bold this recipe
already uses as its bold style. Recorded here so the next pass does not
re-derive it and expect a different answer.

---

## 2. The ramp

**+2 pt at every slot**, uniformly, is the smallest whole-point rise that puts
all six slots inside the tier's ink-per-character band, and no slot needs
more. It lands on the tier's own canonical ramp — the one Edgar, TeX Gyre
Schola and Libris ADF already use.

| | slot 0 | 1 | 2 | 3 | 4 | 5 |
|---|---:|---:|---:|---:|---:|---:|
| **old ramp** | 6 pt | 8 | 10 | 12 | 14 | 17 |
| **new ramp** | 8 pt | 10 | 12 | 14 | 16 | 18 |
| x-height, new | 10 | 12 | 14 | 16 | 17 | 20 |
| tier x-height | 8 | 10 | 12 | 14 | 16 | 18 |
| cap height, new | 13 | 15 | 18 | 21 | 23 | 27 |
| tier median cap | 12 | 15 | 17 | 20 | 23 | 27 |
| advanceY, new | 23 | 29 | 34 | 40 | 46 | 51 |
| tier target advY | 23 | 28 | 34 | 40 | 46 | 51 |
| ink/char, new | 17.96 | 30.06 | 41.20 | 55.64 | 72.76 | 95.52 |
| tier band, low | 17.87 | 29.28 | 35.19 | 51.79 | 70.99 | 91.64 |

Ink is in band at all six. Cap height lands on the tier median at three slots
and one pixel over at the other three, which is the closest any single metric
gets. x-height is deliberately +2 — that is the whole point of the change, and
it is why the recipe now says in as many words that this is the one installed
family not anchored on x-height.

### 2a. The span came down with it, and that is not a separate feature

`advanceY = ceil((ascent − descent)/1000 × pt × 150/72)`. At the old span of
1543 the new point sizes would have produced leading of 26/33/39/46/52/58 —
+3..+7 over the tier's 23/28/34/40/46/51, and **11 lines per page at slot 3
where every other family gives 13**. Raising the sizes without touching the
span would have traded one visible mismatch for another.

`metrics: {ascent: 1010, descent: -358}` (span 1368) lands 23/29/34/40/46/51 —
Libris ADF's shipped ramp exactly. Found by sweeping spans 1280-1460 in steps
of 4, patching hhea/OS-2 the way `build-sd-fonts.py` does and reading
`norm_ceil(face.size.height)` back per point size, because the ceil formula is
wrong by up to 20 units (FreeType rounds `face.size.height` to whole pixels
before `norm_ceil` sees it). The 1352-1372 band is the one that lands it; 1368
is inside it and splits ascent/descent to keep the accent clearance.

Both floors hold at the new sizes, measured on the built files across all four
styles:

| slot | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---:|---:|---:|---:|---:|---:|
| advanceY over the ink floor (span + 0.13 em) | +3 | +5 | +5 | +7 | +8 | +8 |
| declared ascender over the tallest accented cap | +1 | +2 | +2 | +2 | +2 | +2 |

(The tallest is `Å` at every slot but 18 pt, where it is `Ä`.) The 2x tier
clears by +8..+18 and +2..+5.

**A 3x tier of this family would need `--drop-codepoints 0x2E3B`, and it
already did before this change** — worth writing down before someone re-enables
the tier, and worth writing down that it is NOT a regression, because the first
draft of this paragraph claimed it was and the card disproved it.

`EpdGlyph`'s width and height are `uint8`, cap 255. The widest glyph in the new
set is 105 px at 1x/18 pt and **207 px at 2x/18 pt** — 48 px of headroom at the
tier that ships. It is **U+2E3B THREE-EM DASH**, the same glyph that already
breaks TeX Gyre Heros at 3x (its recipe's `--drop-codepoints 0x2E3B` note);
Almendra has no such outline, so both families are drawing the same fallback.
Linear projection, NOT measured: ≈310 px at 3x on the new ramp's 18 pt (54 pt
rendered) and ≈293 px on the old ramp's 17 pt (51 pt) — **both over the cap**,
so the +2 changed nothing about this. The shipped 3x tree still on
`fs_/fonts/Almendra/3x/` agrees: U+2E3B is present in the 18 and 24 pt files
and absent from every file above them, whose widest glyph is U+2E3A instead.
It costs nothing today — 3x was dropped 2026-08-23 and neither the device nor
the phone reads it.

### 2b. The old recipe's objection was true and is now moot

The retired recipe declined a span change on the ground that no family-wide
multiplier could pull slot 3 down without dragging the exact slots off with it.
That was correct **about a ramp anchored on x-height**, where Almendra's
nonlinear x-height curve forced 17 pt for 18 px while 16 px needed only 14 pt —
a 14→17 jump no single span fits. Re-anchored on ink the point sizes are evenly
spaced again (8/10/12/14/16/18), and one span fits all six.

---

## 3. Proof through the real renderer

`CPFONT_DIR=/fonts render_harness reading <family>` — the same page of real
text, one render per ordinal slot, through the firmware's own `GfxRenderer`
and `SdCardFont`. The lowercase-alphabet width it reports is the classical
"alphabet length" measure of set size, and it is the tidiest single summary of
this change:

| slot | Almendra, old | Almendra, new | Edgar | Schola | old as % of Edgar | new as % of Edgar |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 153 px | 206 | 225 | 225 | 68 % | **92 %** |
| 1 | 206 | 257 | 276 | 282 | 75 % | **93 %** |
| 2 | 257 | 309 | 335 | 334 | 77 % | **92 %** |
| 3 | 309 | 358 | 388 | 396 | 80 % | **92 %** |
| 4 | 358 | 409 | 443 | 453 | 81 % | **92 %** |
| 5 | 434 | 457 | 501 | 509 | 87 % | **91 %** |

The old ramp ran 68-87 % of Edgar's set width and DRIFTED across the ramp; the
new one is a flat 91-93 % at every slot. It stops short of 100 % on purpose:
Almendra is a narrow face, and 92 % sits comfortably inside the tier's own
spread (Libris ADF is 81 % of Edgar at slot 3 by mean advance and nobody has
reported it small).

Whole-page ink on those renders, slot 3: old Almendra 8.58 %, new Almendra
10.08 %, Edgar 10.28 %.

Rendered proof: the Artifact published 2026-08-26, native-pixel lossless PNG
crops of slots 0, 3 and 5, before and after, with Edgar and TeX Gyre Schola
beside them.

---

## 4. What was checked and found clean, and what was rejected

* **Ascender height does not explain it.** Almendra's `h` sits at 20 px at
  slot 3, inside the tier's 19-24. Checked so it is not re-proposed.
* **Cap height alone would have given the same ramp** (8/10/11/13-14/16/18
  against the tier's median caps) — it agrees with ink to within one point
  size at every slot, which is why the answer is robust. It is the weaker
  reading because Almendra's cap deficit is one pixel and Libre Franklin and
  TeX Gyre Heros have nearly the same x-height/cap ratio without drawing a
  complaint.
* **Matching ink per character to the tier MEDIAN rather than entering the
  band** would have asked for 8/10/13/14/17/19, up to +3 pt, and put Almendra
  two to three x-height pixels over the tier. Rejected as overshoot: the band
  is the contract, the median is not.
* **`line_height_scale` instead of a `metrics:` span** — both are family-wide
  multipliers, so neither can do anything the other cannot; the span is what
  every other tuned family in the file uses.
* **Leaving the span alone** — rejected on the measurement in §2a (11 lines
  per page against everyone's 13).

### Does another family have the same problem, less visibly?

**Libre Franklin is the one to watch.** It is the second-lightest at every
slot (11.44-11.76 % page blackness against Almendra's old 10.10-11.00 %) and
the second-lowest on scaled ink per character (0.264 at slot 3 against
Almendra's old 0.210). It has NOT been reported, and it sits inside the band
rather than below it, so nothing is being changed. If a second "reads small"
report ever arrives, this is the family it will be about.

**Libris ADF is narrow but not light**: 12.20 px mean advance at slot 3, the
narrowest in the set (TeX Gyre Heros is a hair behind at 12.23), but
12.09-12.51 % blackness — mid-band.
Narrowness on its own has not produced a complaint, which is part of why ink
rather than set width is named as the explanation here.

**Coelacanth is the mirror image** and is fine: long extenders, loose leading
(25/30/36/41/49/55, floor-bound), blackness 11.22-11.36 % — low in the band but
in it, with the largest cap heights in the set carrying the page.

---

## 5. Cost

| | 1x (device SD card) | 2x | total |
|---|---:|---:|---:|
| raw, old ramp | 4,977,126 | 16,136,704 | 21,113,830 |
| raw, new ramp | 6,151,936 | 20,684,135 | 26,836,071 |
| **raw delta** | **+1,174,810** | **+4,547,431** | **+5,722,241 (+27.1 %)** |
| CPZ1 installed, old | 2,188,679 | 4,926,350 | 7,115,029 |
| CPZ1 installed, new | 2,591,033 | 5,836,722 | 8,427,755 |
| **installed delta** | **+402,354** | **+910,372** | **+1,312,726 (+18.4 %)** |

The whole rise is glyph bitmaps at larger point sizes; the charset, the
`reading` interval set and the style count are unchanged.

## 6. Files

* `lib/EpdFont/scripts/sd-fonts.yaml` — the recipe, and the derivation in its
  comment. The `Slot uniformity` block at the head now says Almendra is not
  one of its x-height matches.
* `docs/sd-card-fonts.md` §"Six size slots" — the same exception, one
  paragraph.
* `test/reader_font_sizes/ReaderFontSizesTest.cpp` — the six-size fixture is
  named Almendra and carried the retired ramp.
* `crosspoint-simulator/build/seedfonts/Almendra/` — rebuilt at 1x and 2x;
  the superseded `Almendra_6` and `Almendra_17` files were deleted at both
  tiers, or the family would ship ten cuts into six picker slots.

**Three surfaces still carry the retired ramp**, and only one of them was in
scope here. `docs/sd-card-fonts.md` already says a card is verified per
provisioning rather than per ruling; this is one of those times.

| surface | what it holds | why not done |
|---|---|---|
| `crosspoint-simulator/build/seedfonts/Almendra/` | the new ramp, 1x and 2x | **done** — this is the tree the iOS build is pointed at |
| `crosspoint-reader/fs_/fonts/Almendra/` (the simulator's card) | 6/8/10/12/14/17 at 1x, 2x and 3x | re-provisioning, not a code change; a 3x rebuild would need `--drop-codepoints 0x2E3B` |
| `crosspoint-simulator/ios/seedfonts/Almendra/` | **10/12/14/17** — the four-slot ramp, mtime 2026-08-24 | out of scope: that whole tree is pre-XS/XXS for every family (Edgar is 12/14/16/18 there), so it was already stale at `0d58d4e9d`. `ios/CMakeLists.txt` globs it into `Resources/SeedFonts/` when `CROSSPOINT_IOS_SEED_FONTS_DIR` is not overridden, so a TestFlight build taken from it would ship four cuts on the 1543 span |
| provisioned device SD cards (BUNNYFIELDS, OWEN_BNF) | whatever the last provisioning wrote | by hash, per `docs/sd-card-fonts.md` |

**And `crosspoint-simulator/docs/trial-fonts.md` states the retired ramp as
fact** — its promotion table row reads `10/12/14/17 → 12/14/16/18 → advY
32/39/45/55 … no span fixes it`, all three of which this change overturns, and
its size row is for a file set that no longer exists. Not edited here (that
repo was out of scope for source changes); it needs a dated superseded-by
pointer at this doc.
