# Which families would benefit from enlarging: the band, and the answer

> **SUPERSEDED THE SAME DAY by `docs/words-per-page-2026-08-26.md`.** The owner
> proposed a better normalisation — *"how many words fit in a text filled page…
> so that page length a book is close to the same for each font size,
> irrespective of font used"* — and he is right: that is the OUTCOME, where
> x-height and ink per character are proxies for it. Two conclusions here did
> not survive the better metric. **This document's XXS overshoot claim for
> Almendra is WRONG** — words per page says Almendra's XXS fits 6% MORE than the
> tier median, i.e. still marginally small, and dropping it to 7 pt would put it
> 35% over. x-height sees a tall lowercase; the page counts a narrow set. And
> the Libre Franklin S candidate this document rated WEAK is **confirmed and
> generalised**: the same defect sits at Inknut's S and Heros' S, and all three
> are the families whose ramp has a 1 pt step.
>
> What still stands: the anchor-error mechanism, the ranking that put Almendra
> first, and the finding that ink per character cannot detect a family set too
> large. Read those; do not quote the recommendations.

*2026-08-26. Firmware `135eb0769`, simulator `5fde54c`, seed tree
`crosspoint-simulator/build/seedfonts`. Every number is READ BACK from built
`.cpfont` files or from the real `GfxRenderer` through
`tools/calendar_preview/render_harness`. Nothing here was applied: no font,
recipe or ramp was changed to produce it, and the candidate cuts live in a
scratch tree.*

Owner, 2026-08-26: *"which fonts would benefit from enlarging and how much?
show proofs for verification."* — generalising
[almendra-size-match-2026-08-26.md](almendra-size-match-2026-08-26.md), which
raised one family on an ink-per-character measurement, to the other seven.

Rendered proof: the Artifact **The Ink Band**,
<https://claude.ai/code/artifact/7efcc086-237c-4d53-9cce-ae64db9a0d9a>.

---

## 0. The answer, ranked

| rank | family | recommendation | tier |
|---|---|---|---|
| 1 | **Libre Franklin** | **S slot only, 10 -> 11 pt. WEAK — presented, not shipped.** | both (the point size is shared) |
| 2 | **Almendra** | keep this morning's +2 at every slot, including XXS where it overshoots most | both |
| 3-8 | Coelacanth, Edgar, Inknut Junicode, Libris ADF, TeX Gyre Heros, TeX Gyre Schola | **no change at any slot, at either tier** | — |

**No cut in the shipping set is below the band's floor**, at either tier, at any
of the 96 family-and-slot-and-tier combinations. The literal answer to "which
fonts would benefit from enlarging" is *none, on the evidence*. The one
candidate is offered on a second-order test, not on the band, and it does not
clearly pay.

---

## 1. The band, and why it is a floor rather than a target

The eight families span 1.5:1 in ink at every slot (Libre Franklin 51.79 at M
against Inknut Junicode 75.17). That spread is typeface identity, not error, so
**the band has no upper edge** — nobody has ever reported a family for being
too heavy, and the heaviest page in the set was called an advantage on a dark
ground on the XXS proof sheet the same day.

What it has is a floor, and the evidence *brackets* it rather than fixing it:

* **Below:** Almendra's retired ramp — the only ink value in this project's
  history a reader has called small.
* **At or above:** every cut of the seven families never reported. The lightest
  is Libre Franklin at five of six slots and Libris ADF at XS.

Per slot, 1x, the gap between those two lines:

| slot | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| floor (7 families) | 17.87 | 29.28 | 35.19 | 51.79 | 70.99 | 91.64 |
| reported small | 10.59 | 17.96 | 30.06 | 41.20 | 55.64 | 81.59 |
| floor is this much above it | +69 % | +63 % | +17 % | +26 % | +28 % | +12 % |

The threshold is somewhere in each of those intervals and the evidence cannot
say where. **12 % to 69 % wide**, so the observed floor is a conservative bound,
not a tight one.

**Why min-max and not mean +/- a tolerance.** The complaint data is binary and
one-sided, and the mean is the average of eight deliberately different faces. A
mean-relative band fails Libre Franklin at S by 23 % and passes the same family
at XS by 13 %, on a spread that is mostly identity. Recorded so it is not
re-proposed.

---

## 2. The matrix: ink per character, 8 families x 6 slots x 2 tiers

Sum of the stored 2-bit levels (0-3) / 3, over one fixed 181-character passage,
regular style. Point size in brackets.


### 2a. Tier 1x

| family | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra | 17.96 (8) | 30.06 (10) | 41.20 (12) | 55.64 (14) | 72.76 (16) | 95.52 (18) |
| Coelacanth | 20.62 (9) | 30.77 (11) | 43.51 (13) | 57.24 (15) | 83.00 (18) | 103.10 (20) |
| Edgar | 21.92 (8) | 35.20 (10) | 50.31 (12) | 68.15 (14) | 89.44 (16) | 113.02 (18) |
| InknutJunicode | 25.94 (7) | 42.99 (9) | 51.48 (10) | 75.17 (12) | 102.01 (14) | 134.05 (16) |
| LibreFranklin | **17.87** (7) | 29.53 (9) | **35.19** (10) | **51.79** (12) | **70.99** (14) | **91.64** (16) |
| LibrisADF | 18.31 (8) | **29.28** (10) | 42.10 (12) | 54.64 (14) | 73.27 (16) | 91.88 (18) |
| TeXGyreHeros | 17.92 (7) | 31.60 (9) | 45.75 (11) | 56.29 (12) | 78.48 (14) | 100.91 (16) |
| TeXGyreSchola | 22.69 (8) | 36.89 (10) | 53.00 (12) | 72.15 (14) | 93.39 (16) | 118.59 (18) |
| *seven-family median* | *20.62* | *31.60* | *45.75* | *57.24* | *83.00* | *103.10* |
| *reported small* | *10.59* | *17.96* | *30.06* | *41.20* | *55.64* | *81.59* |

### 2b. Tier 2x

| family | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra | 72.76 (8) | 117.03 (10) | 166.83 (12) | 223.89 (14) | 298.17 (16) | 372.54 (18) |
| Coelacanth | 83.00 (9) | 125.24 (11) | 172.45 (13) | 229.83 (15) | 330.51 (18) | 406.93 (20) |
| Edgar | 89.44 (8) | 140.30 (10) | 197.24 (12) | 270.43 (14) | 353.33 (16) | 451.15 (18) |
| InknutJunicode | 102.01 (7) | 169.14 (9) | 210.45 (10) | 302.40 (12) | 412.87 (14) | 537.80 (16) |
| LibreFranklin | **70.99** (7) | **115.42** (9) | **146.67** (10) | **206.35** (12) | **282.45** (14) | **370.57** (16) |
| LibrisADF | 73.27 (8) | 120.39 (10) | 171.97 (12) | 232.12 (14) | 304.64 (16) | 386.12 (18) |
| TeXGyreHeros | 78.48 (7) | 122.34 (9) | 185.94 (11) | 222.74 (12) | 305.63 (14) | 399.68 (16) |
| TeXGyreSchola | 93.39 (8) | 149.81 (10) | 213.80 (12) | 291.65 (14) | 379.32 (16) | 479.56 (18) |
| *seven-family median* | *83.00* | *125.24* | *185.94* | *232.12* | *330.51* | *406.93* |
| *reported small* | *41.20* | *72.76* | *117.03* | *166.83* | *223.89* | *not rebuilt* |

Bold is that slot's floor. **Almendra is excluded from the median and the
floor** — it is the family under investigation and must not move its own
reference line. The 2x reported-small row is measured from cuts rebuilt at the
retired point sizes; XL is absent because 17 pt was not rebuilt.

Supporting metrics, 1x, at every slot:

| family | slot | pt | x-ht | cap | advY | ink/ch | scaled | mean adv | ch/line (494 px) | lc-alphabet |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Almendra | XXS | 8 | 10 | 13 | 23 | 17.96 | 0.1796 | 6.84 | 72.2 | 206 |
| Almendra | XS | 10 | 12 | 15 | 29 | 30.06 | 0.2087 | 8.54 | 57.8 | 257 |
| Almendra | S | 12 | 14 | 18 | 34 | 41.20 | 0.2102 | 10.25 | 48.2 | 309 |
| Almendra | M | 14 | 16 | 21 | 40 | 55.64 | 0.2173 | 11.96 | 41.3 | 358 |
| Almendra | L | 16 | 17 | 23 | 46 | 72.76 | 0.2518 | 13.66 | 36.2 | 409 |
| Almendra | XL | 18 | 20 | 27 | 51 | 95.52 | 0.2388 | 15.37 | 32.1 | 457 |
| Coelacanth | XXS | 9 | 8 | 15 | 25 | 20.62 | 0.3221 | 7.44 | 66.4 | 221 |
| Coelacanth | XS | 11 | 10 | 18 | 30 | 30.77 | 0.3077 | 9.10 | 54.3 | 268 |
| Coelacanth | S | 13 | 12 | 21 | 36 | 43.51 | 0.3021 | 10.77 | 45.9 | 315 |
| Coelacanth | M | 15 | 14 | 23 | 41 | 57.24 | 0.2920 | 12.44 | 39.7 | 365 |
| Coelacanth | L | 18 | 16 | 28 | 49 | 83.00 | 0.3242 | 14.91 | 33.1 | 436 |
| Coelacanth | XL | 20 | 18 | 32 | 55 | 103.10 | 0.3182 | 16.57 | 29.8 | 483 |
| Edgar | XXS | 8 | 8 | 12 | 23 | 21.92 | 0.3425 | 7.54 | 65.5 | 225 |
| Edgar | XS | 10 | 10 | 14 | 28 | 35.20 | 0.3520 | 9.43 | 52.4 | 276 |
| Edgar | S | 12 | 12 | 17 | 34 | 50.31 | 0.3493 | 11.31 | 43.7 | 335 |
| Edgar | M | 14 | 14 | 20 | 39 | 68.15 | 0.3477 | 13.20 | 37.4 | 388 |
| Edgar | L | 16 | 16 | 23 | 45 | 89.44 | 0.3494 | 15.09 | 32.7 | 443 |
| Edgar | XL | 18 | 18 | 26 | 51 | 113.02 | 0.3488 | 16.98 | 29.1 | 501 |
| InknutJunicode | XXS | 7 | 9 | 12 | 23 | 25.94 | 0.3202 | 8.10 | 61.0 | 237 |
| InknutJunicode | XS | 9 | 11 | 15 | 29 | 42.99 | 0.3553 | 10.43 | 47.4 | 310 |
| InknutJunicode | S | 10 | 13 | 17 | 32 | 51.48 | 0.3046 | 11.58 | 42.7 | 343 |
| InknutJunicode | M | 12 | 15 | 20 | 39 | 75.17 | 0.3341 | 13.90 | 35.5 | 411 |
| InknutJunicode | L | 14 | 17 | 23 | 45 | 102.01 | 0.3530 | 16.20 | 30.5 | 474 |
| InknutJunicode | XL | 16 | 19 | 27 | 51 | 134.05 | 0.3713 | 18.52 | 26.7 | 546 |
| LibreFranklin | XXS | 7 | 8 | 11 | 23 | 17.87 | 0.2792 | 6.74 | 73.3 | 205 |
| LibreFranklin | XS | 9 | 10 | 14 | 29 | 29.53 | 0.2953 | 8.66 | 57.0 | 257 |
| LibreFranklin | S | 10 | 12 | 16 | 33 | 35.19 | 0.2444 | 9.63 | 51.3 | 286 |
| LibreFranklin | M | 12 | 14 | 19 | 39 | 51.79 | 0.2643 | 11.56 | 42.7 | 346 |
| LibreFranklin | L | 14 | 16 | 22 | 46 | 70.99 | 0.2773 | 13.49 | 36.6 | 401 |
| LibreFranklin | XL | 16 | 18 | 25 | 52 | 91.64 | 0.2828 | 15.39 | 32.1 | 455 |
| LibrisADF | XXS | 8 | 8 | 12 | 23 | 18.31 | 0.2862 | 6.46 | 76.4 | 180 |
| LibrisADF | XS | 10 | 10 | 15 | 29 | 29.28 | 0.2928 | 8.07 | 61.2 | 226 |
| LibrisADF | S | 12 | 12 | 18 | 34 | 42.10 | 0.2924 | 9.69 | 51.0 | 270 |
| LibrisADF | M | 14 | 14 | 21 | 40 | 54.64 | 0.2788 | 11.30 | 43.7 | 318 |
| LibrisADF | L | 16 | 16 | 24 | 46 | 73.27 | 0.2862 | 12.92 | 38.2 | 364 |
| LibrisADF | XL | 18 | 18 | 27 | 51 | 91.88 | 0.2836 | 14.53 | 34.0 | 412 |
| TeXGyreHeros | XXS | 7 | 8 | 11 | 22 | 17.92 | 0.2801 | 6.46 | 76.5 | 182 |
| TeXGyreHeros | XS | 9 | 10 | 14 | 29 | 31.60 | 0.3160 | 8.29 | 59.6 | 231 |
| TeXGyreHeros | S | 11 | 12 | 16 | 35 | 45.75 | 0.3177 | 10.13 | 48.8 | 291 |
| TeXGyreHeros | M | 12 | 14 | 19 | 39 | 56.29 | 0.2872 | 11.03 | 44.8 | 322 |
| TeXGyreHeros | L | 14 | 16 | 22 | 45 | 78.48 | 0.3066 | 12.91 | 38.3 | 370 |
| TeXGyreHeros | XL | 16 | 18 | 24 | 51 | 100.91 | 0.3114 | 14.71 | 33.6 | 428 |
| TeXGyreSchola | XXS | 8 | 8 | 12 | 23 | 22.69 | 0.3546 | 7.81 | 63.2 | 225 |
| TeXGyreSchola | XS | 10 | 10 | 15 | 28 | 36.89 | 0.3689 | 9.78 | 50.5 | 282 |
| TeXGyreSchola | S | 12 | 12 | 18 | 34 | 53.00 | 0.3680 | 11.72 | 42.1 | 334 |
| TeXGyreSchola | M | 14 | 14 | 21 | 39 | 72.15 | 0.3681 | 13.68 | 36.1 | 396 |
| TeXGyreSchola | L | 16 | 16 | 24 | 45 | 93.39 | 0.3648 | 15.63 | 31.6 | 453 |
| TeXGyreSchola | XL | 18 | 18 | 27 | 51 | 118.59 | 0.3660 | 17.59 | 28.1 | 509 |

`scaled` = ink per character / x-height^2, the size-free reading. `ch/line` is
the reader's own 494 px column, confirmed against `render_harness`.
`lc-alphabet` is the real renderer's lowercase alphabet width — the classical
set-size measure.

---

## 3. The mechanism, which is sharper than "x-height is a proxy"

A `.cpfont` stores x-height as a **whole number of pixels**. At 1x a slot's
lowercase is 8-18 px, so rounding is worth up to 12 % of the value — and the
slot scheme is anchored on exactly that rounded number. The 2x companion
rasterizes the same outlines at twice the resolution, so `xh(2x) / 2` is the
better estimate of what the letter is. The difference is each family's
**anchor error**: a family that over-reads gets matched at too small a point
size and therefore sets small.

| family | 1x px per pt | true px per pt | anchor error | consequence |
|---|---:|---:|---:|---|
| Almendra | 1.156 | 1.077 | +7.3 % | set 7 % small — REPORTED, corrected this morning |
| InknutJunicode | 1.243 | 1.174 | +5.9 % | set 6 % small — already paid for by the +1 px x-height offset its ramp has always carried |
| LibrisADF | 1.000 | 1.062 | -5.9 % | set 6 % **large** at 1x — the only family the anchor pushes the other way |
| TeXGyreSchola | 1.000 | 0.969 | +3.2 % | inside the rounding noise |
| TeXGyreHeros | 1.130 | 1.096 | +3.1 % | inside the rounding noise |
| Edgar | 1.000 | 0.977 | +2.3 % | inside the rounding noise |
| LibreFranklin | 1.148 | 1.127 | +1.9 % | inside the noise on average; its S cut alone is +4.3 % |
| Coelacanth | 0.907 | 0.891 | +1.8 % | inside the rounding noise |

**The ranking falls out of it.** Almendra's anchor error is the largest in the
set and Almendra is the family that was reported — the first time the size
complaint has had a mechanism rather than a correlate, and the reason +2 was
the right size of correction (7.3 % of a ramp is about one point size at every
slot). The second largest is Inknut Junicode's, and it is **already
compensated**: that family's ramp has always run one x-height pixel over the
tier (9/11/13/15/17/19), an offset `sd-fonts.yaml` records as a quirk with no
reason attached — it is worth 8-12 %, points the right way, and is why Inknut
sits 12-36 % *above* the median rather than below it. **After those two nothing
exceeds 3.2 %, which is under one pixel at every slot: there is no third family
at risk on the anchor.**

Separately, and not an error: how large a face is *on its body*. Coelacanth's
true lowercase is 0.89 px/pt and Inknut's 1.17, so at matched x-height
Coelacanth is set at a 30 % larger point size. That is why Libre Franklin sits
at 10 pt where Edgar sits at 12 for the same slot. Permanent, and not something
to correct.

---

## 4. The one anomaly: Libre Franklin at S

Two independent screens over all 96 cuts; one cut is caught by both.

* **Low against the tier** — ink against the slot's seven-family median.
  Libre Franklin at S is **-23.1 % (1x) / -21.1 % (2x)**, where the same family
  runs -6.5 % to -14.5 % at its other five slots. Deepest figure in the matrix.
* **Low against its own family** — scaled ink against that family's own
  six-slot mean. Three cuts are more than 10 % under: Almendra XXS (-17.5 %),
  Inknut S (-10.3 %), Libre Franklin S (-10.8 %).

Intersect: Almendra XXS is AT the tier floor, not under it. Inknut S is +12.5 %
of the median — light for an Inknut, heavy for the tier. **Libre Franklin at S
is the only cut light for its family and light for the tier at once**, and the
only place where the floor comes within 17 % of the reported value.

It is also the same defect Almendra had, one slot wide: LF's anchor error
averages a harmless +1.9 % but at S is **+4.3 %**, its own worst, because 10 ppem
is where the hinter rounds this face's lowercase up hardest.

### 4a. The candidate, measured

| | 1x shipped (10 pt) | 1x candidate (11 pt) | 1x Edgar (12 pt) | 2x shipped | 2x candidate |
|---|---:|---:|---:|---:|---:|
| x-height | 12 | 13 | 12 | 23 | 25 |
| cap | 16 | 18 | 17 | 31 | 34 |
| advanceY | 33 | 36 | 34 | 65 | 72 |
| ink/char | 35.19 | **42.34** | 50.31 | 146.67 | **173.81** |
| vs slot median | -23.1 % | **-7.4 %** | +10.0 % | -21.1 % | **-6.5 %** |
| mean advance | 9.63 | 10.59 | 11.31 | 19.24 | 21.17 |
| chars/line | 51.3 | 46.7 | 43.7 | 51.3 | 46.7 |
| lc-alphabet | 286 (85.4 % of Edgar) | **316 (94.3 %)** | 335 | — | — |

**Buys:** ink into line with the family's other five slots; set width from
85.4 % of Edgar to 94.3 %, where this family runs 89-93 % everywhere else, so S
is currently its own outlier by four points in the wrong direction.

**Costs, and they are why it is weak:**

1. At 1x the x-height ladder becomes 8 / 10 / **13** / 14 / 16 / 18. Two
   adjacent slots one pixel apart is a real defect in a size picker, though the
   line advance (36 vs 39) and measure (46.7 vs 42.7 chars) still separate them.
   At 2x the same step is 25 against 27 and the objection largely goes away.
2. The ramp's uneven step **relocates rather than disappears**: ink would rise
   43 % into S and 22 % out of it, against today's 19 % and 47 %. Integer point
   sizes offer this family no ramp that is even at both ends.
3. It is **not tier-selectable** — one point size feeds both tiers.

---

## 5. Almendra now: the +2 stands, including where it overshoots

The XXS proof sheet measured Almendra's lowercase 25 % over the tier at slot 0
against 14 % at slot 3, and said it reads a size up. **The matrix agrees, and
still says leave it.**

| | XXS shipped (8 pt) | XXS reduced (7 pt) | tier |
|---|---:|---:|---:|
| x-height | 10 (+25 %) | 9 (+12.5 %) | 8 |
| cap | 13 | 11 | 11-15 |
| advanceY | 23 | **20** | 22-25 |
| ink/char | 17.96 (+0.5 % of floor) | **14.40 (-19.4 % of floor)** | floor 17.87 |
| lc-alphabet | 206 | 182 | Edgar 225 |

There is no size between 7 and 8. The reduction gives up a fifth of the ink to
land 19 % under the lightest anything installed carries, and closes the line to
20 px — *"a family whose glyphs match on x-height and whose LINE is four pixels
short is a family whose letters are carrying less than the slot is sized for"*,
which is `sd-fonts.yaml`'s own account of the cut retired this morning.
Reducing XXS would re-create it.

**And the overshoot is a 1x phenomenon.** At 2x Almendra's XXS lowercase is 17
against the tier's 16, a 6 % gap. Its 1x-to-2x x-height ratio at XXS is 1.70,
the largest tier disagreement in the set.

---

## 6. The tiers do not agree, and it is per family

Only **13 of 48** family-and-slot pairs have a 2x companion whose x-height is
exactly twice the 1x. A single departure is within a pixel and means nothing;
the sign holding across a ramp is what is real.

| family | XXS | XS | S | M | L | XL | true px/pt |
|---|---:|---:|---:|---:|---:|---:|---:|
| Almendra | **1.70** | **1.83** | **1.86** | **1.88** | 2.00 | **1.95** | 1.06-1.10 |
| Coelacanth | 2.00 | 2.00 | 1.92 | 1.93 | 2.00 | **1.94** | 0.88-0.91 |
| Edgar | 2.00 | 2.00 | 1.92 | 1.93 | 1.94 | **1.94** | 0.96-1.00 |
| InknutJunicode | **1.89** | 1.91 | **1.85** | **1.87** | **1.88** | 1.95 | 1.14-1.21 |
| LibreFranklin | 2.00 | 2.00 | 1.92 | 1.93 | 1.94 | 2.00 | 1.11-1.15 |
| LibrisADF | 2.00 | **2.20** | **2.17** | **2.14** | **2.12** | **2.11** | 1.00-1.10 |
| TeXGyreHeros | 2.00 | **1.90** | 2.00 | **1.86** | 1.94 | **1.94** | 1.06-1.14 |
| TeXGyreSchola | 2.00 | **1.90** | 1.92 | 1.93 | 1.94 | **1.94** | 0.95-1.00 |

Bold marks a departure larger than that cell's own whole-pixel rounding
uncertainty (0.5/xh1x + 0.5k/xh2x — worth 0.125 at XXS and 0.056 at XL).

* **Libris ADF is the only family whose 1x runs SMALLER than half its 2x**, and
  it does so at five consecutive slots (2.11-2.20). Its outlines want 1.06 px/pt
  and the 1x raster rounds them to exactly 1.00 at all six sizes. It sets ~6 %
  larger on the phone than on the device at the same slot. Unreported, ink
  mid-band, and **no point size fixes it without breaking the other tier.**
* **Almendra is the mirror**, largest at XXS (1.70): bigger on the device than
  on the phone.
* **Inknut Junicode** is below 2.00 at all six and beyond rounding at three —
  the same direction as Almendra, half the size, already paid for.
* The remaining four land 1.86-2.00, all inside a pixel.

---

## 7. Two corrections to this morning's record

### 7a. Page blackness WAS fixed, and `almendra-size-match-2026-08-26.md` says it cannot be

That doc's §1c records blackness as *"the residual this change does NOT fix"*,
on the correct ground that the measure is scale-invariant and no point size
moves it. Sound reasoning, wrong conclusion: **the `metrics:` span change that
shipped in the same commit is not a point size.**

| | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra, retired ramp (§1c) | 10.89 | 10.10 | 11.00 | 10.31 | 10.34 | 10.22 |
| Almendra, shipped files | **11.42** | **12.13** | **11.83** | **11.63** | **11.58** | **12.19** |
| tier band, low | 11.08 | 11.27 | 11.07 | 11.22 | 11.36 | 11.32 |

In band at all six. Proof it is the span and not the size, at a **fixed** point
size: 6 pt built on the retired 1543-unit span measured 10.89 %; 6 pt built on
today's 1368-unit span measures **12.165 %**. Same outlines, same size, +11.7 %,
matching advanceY moving 19 px -> 17 exactly. **The heavier roman §1c calls for
is not needed.** §1c should carry a pointer here.

### 7b. The 1x->2x ratio is not "2.00 and two exceptions"

Working assumption going in: 1.70 Almendra, 1.89 Inknut, exactly 2.00 for the
other six. Measured over all 48 pairs, exactly 2.00 holds in 13, and the family
furthest from it is **Libris ADF at 2.11-2.20, in the opposite direction, at
five consecutive slots**.

---

## 8. Where the measurement and the picture disagree

**This is the finding that matters most**, because if the metric were simply
wrong the rest of this doc would be too.

At XXS, ink per character says Almendra is fine (17.96 against a floor of
17.87, inside by half a percent). x-height says it is 25 % too big. **The
rendered proof agrees with x-height** — Almendra's letters plainly stand taller
than Edgar's in the same rectangle, and no ink measurement reports that.

The metric is not wrong; its range is narrower than this morning's account
suggests:

> **Ink per character detects a family set too SMALL. It cannot detect one set
> too LARGE.** "Reads small" is a joint impression of size and weight and ink
> integrates both, which is why it found Almendra when x-height could not.
> "Reads big" is x-height and nothing else, and the integration is exactly what
> throws that away.

Both numbers belong beside every ramp. An anchor on x-height alone shipped the
Almendra that read small; an anchor on ink alone will ship one that reads a
size up. Where they disagree, the disagreement is the finding.

---

## 9. Method, and what came back clean

**Measured** from the built `.cpfont`s in
`crosspoint-simulator/build/seedfonts`, regular style, 8 families x 6 slots x 2
tiers = 96 files. Nothing computed from an outline.

**Built** into a scratch directory — never `build/seedfonts` — two stand-in
families (`LFsweep`, `ALMsweep`) carrying candidate point sizes at both tiers,
through the ordinary `build-sd-fonts.py` path with the shipped recipes
unmodified except for the size list. The stand-ins reproduce the shipped ink
values **exactly** at every shipped point size (LibreFranklin 17.87 / 29.53 /
35.19 / 51.79 / 70.99 / 91.64; Almendra 17.96 / 30.06 / 41.20 / 55.64 / 72.76 /
95.52), which is what makes the candidates valid stand-ins rather than a
different build.

**Rendered** through `tools/calendar_preview/render_harness` (and
`render_harness2`, rebuilt — the checked-out binary was from 2026-08-14 and
predates both the `aa` mode and six-slot support), composed through the
grayscale overlay so the shipped antialiasing is in the picture. PNG at native
panel pixels; magnifications are integer 4x nearest-neighbour and say so;
lowest figure coverage 12.99 %.

### Checked, and found CLEAN — so the next pass does not re-derive it

* **No cut anywhere is below the observed floor**, all 96. This is the literal
  answer to the owner's question.
* **Cap height** flags nothing ink does not. At M the eight span 19-23 px with
  no outlier; the shortest caps belong to Libre Franklin, which is also the
  lightest, so the column carries no independent signal.
* **Page blackness** is in band for all eight, all six slots, both tiers
  (11.07-14.22 % at 1x). Nothing draws a page too grey.
* **Characters per line** is unchanged from the XXS sheet and is not a size
  problem: Libris ADF and TeX Gyre Heros set 76.4 and 76.5 at XXS, just past the
  66-75 typography treats as comfortable. Column change, not font change.
* **The heavy end has no upper bound.** Inknut runs +12.5 % to +36.0 % of the
  median at every slot, unreported; on a dark ground the extra weight is the
  right side to err on. **No family needs reducing on ink.**
* **Almendra's new ramp** holds both recipe floors (advanceY over the ink span,
  declared ascender over the tallest accented capital) at both tiers.
* **A uniform per-family offset was tested and is not generally right.** Almendra
  took +2 at every slot on a slot-3 measurement and its deficit was NOT constant
  (-41 / -39 / -15 / -20 / -22 / -11 % against the floor). The screens above are
  per-slot for that reason, and the one candidate they produce is a single slot.

## 10. Files

* `crosspoint-simulator/build/seedfonts/` — read only; unchanged.
* `lib/EpdFont/scripts/sd-fonts.yaml` — unchanged. If §7a is accepted it wants
  a pointer in the Almendra block's blackness paragraph.
* [almendra-size-match-2026-08-26.md](almendra-size-match-2026-08-26.md) §1c
  and its §4 "does another family have the same problem" — superseded in part
  by §7a and §3 here. Its Libre Franklin watch-flag is **upheld and sharpened**:
  the family is fine at five slots and it is S alone that is worth a ruling.

