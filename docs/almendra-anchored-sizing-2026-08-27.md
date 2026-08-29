# Every family resized to Almendra's book length

*2026-08-27. Firmware `3887ad69a`, simulator `5fde54c`, seed tree
`crosspoint-simulator/build/seedfonts`. Every number below is READ BACK from
the real firmware paginator over real prose at the shipped X3 geometry, and
every multiplier and ramp quoted was **BUILT and MEASURED on two independent
corpora**, not fitted. **Nothing was applied: no font, recipe or ramp was
changed.** The candidates live in a scratch tree outside both repos.*

Owner, 2026-08-27: *"make a proposal for all fonts to be resized for XXS XS S M
L sizes based on Almendra's current pages per book numbers."*

This supersedes the TARGET of
[words-per-page-2026-08-26.md](words-per-page-2026-08-26.md) §6 — the tier
median — and keeps its method, its instrument and its corpora unchanged.
Almendra is now the anchor and does not move.

Owner rulings taken during the work: **XL is in** (all six slots are one ramp),
and **Almendra's own slot-to-slot progression is accepted as it is**.

Rendered proof: the Artifact **Almendra as the Ruler**,
<https://claude.ai/code/artifact/4f0ec82b-1d1a-42db-a3ed-80cd579684b6>.

> **This document was revised after adversarial review.** The review overturned
> two conclusions in the first draft and they are corrected in place: a ramp
> change recommended for Coelacanth **does not replicate** on the second corpus
> and has been withdrawn (§2b), and a ramp change for Inknut Junicode that the
> first draft dismissed **does replicate on both** and has been adopted (§2b).
> Three smaller errors are corrected in §5 and §1. Everything is now reported
> on both corpora side by side, which is what caught them.

---

## 0. The answer

1. **One continuous multiplier per family, plus one point size.** `k` on both
   `scale:` and `metrics:`, shipped ramps untouched for six of seven families;
   Inknut Junicode's S slot moves 10 → 11 pt. The spread of words per full page
   collapses:

   | slot | XXS | XS | S | M | L | XL |
   |---|---:|---:|---:|---:|---:|---:|
   | ratio max/min now | 1.304 | 1.251 | 1.227 | **1.200** | 1.259 | 1.259 |
   | ratio max/min after | 1.074 | 1.114 | 1.140 | **1.071** | 1.116 | 1.069 |

   Tier rms against Almendra falls **9.32 % → 3.60 %** on passage A and
   **9.76 % → 3.53 %** on passage B. The two corpora now agree to 0.07 of a
   point, which they did not before the Inknut change.

2. **It costs book length, and that is the decision inside the decision.**
   Almendra is on the DENSE side of the tier, so anchoring to it makes the
   others denser too. A 100,000-word book is **5.1 % shorter on average at M**,
   and four families lose 8–15 % of their page count. §3. If that shortening is
   unacceptable the proposal should be rejected, not tuned.

3. **Point size alone cannot do it, with no model involved.** Every family was
   built at every integer size from 5 to 21 pt. The best achievable integer
   ramp with no multiplier leaves tier rms **5.93 %**, worst **15.4 %**. §2a.

4. **Ramp changes must be proved on both corpora, and mostly fail.** Three were
   tried. One replicates (Inknut), one relocates the error onto the default
   size (Libre Franklin), one reverses sign between corpora (Coelacanth). §2b.

5. **Six cells still miss by more than 5 % on both corpora**, four of them
   Libre Franklin's and Heros's — the families whose ramps carry a 1 pt step.
   §5.

6. **Almendra is a good anchor on the evidence**: its own ramp is the smoothest
   in the tier, rms 0.60 % against its own power-law fit. §6.

---

## 1. The proposal

`k` multiplies both `scale:` and `metrics:` (ascent and descent), on **all four
styles**, not regular alone. Inknut Junicode's italic and bold-italic already
carry `scale: 1.2` from a different source (JunicodeVF); `k` multiplies that to
1.101 rather than replacing it.

| family | k | ramp | XXS | XS | S | M | L | XL | rms A | rms B |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Almendra | — | 8 10 12 14 16 18 | +0.0% | +0.0% | +0.0% | +0.0% | +0.0% | +0.0% | 0.00 | 0.00 |
| Coelacanth | 0.944 | 9 11 13 15 18 20 | -2.0% | +4.3% | +3.3% | +3.9% | -7.2% | -6.5% | 4.88 | 3.92 |
| Edgar | 0.960 | 8 10 12 14 16 18 | -3.3% | +2.5% | +1.5% | -0.3% | +1.1% | -2.5% | 2.12 | 1.78 |
| Inknut Junicode | 0.917 | **7 9 11 12 14 16** | +0.8% | -2.7% | -4.5% | +6.4% | +3.6% | -1.9% | 3.78 | 4.07 |
| Libre Franklin | 1.008 | 7 9 10 12 14 16 | -2.0% | -6.4% | +8.9% | +5.6% | -0.8% | -2.8% | 5.23 | 5.44 |
| Libris ADF | 1.027 | 8 10 12 14 16 18 | +2.3% | +1.9% | +2.1% | +0.1% | -2.8% | -2.7% | 2.18 | 1.80 |
| TeX Gyre Heros | 1.014 | 7 9 11 12 14 16 | +3.8% | +0.0% | -3.4% | +6.8% | -1.7% | -2.8% | 3.72 | 3.92 |
| TeX Gyre Schola | 0.947 | 8 10 12 14 16 18 | +2.0% | -0.8% | +1.0% | +0.2% | -2.2% | -1.6% | 1.48 | 2.03 |

tier rms A 9.32→3.6, B 9.76→3.53; worst A 17.4→8.9

Bold marks the one ramp change. Residuals are MEASURED on built `.cpfont`
files, on both corpora. Almendra's row is zero by construction.

**`k` is determined to about ±1 %, not to four decimals.** Three multipliers
per family were built and measured (rounds 1–3), and the lowest-measuring one
kept — but adversarial review quantified what that selection is worth and the
answer is *almost nothing*: every inter-round change in `k` is smaller than one
pixel of leading, and one pixel of leading is worth ~5–6 % of words per page.
Best-of-three buys 4.11 → 3.78 rms, which is inside the quantization noise.
The four decimals above are the numbers that were actually built; they should
be read as "about 0.94", not as a precision claim. (The first draft said the
three rounds differ by under 1 % of `k` for every family — that is wrong for
Libris ADF, which differs by **1.15 %**.)

## 2. What it does to the page

Words per full page, today → proposed, on passage A. A page here is a
FULL page: the chapter's sinkage page and the trailing partial page are
both dropped, and the figure is a mean over 21–136 pages.

| family | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra | 349.5 → 349.5 | 227.5 → 227.5 | 162.8 → 162.8 | 122.6 → 122.6 | 96.3 → 96.3 | 76.1 → 76.1 |
| Coelacanth | 292.6 → 342.6 | 206.7 → 237.3 | 150.2 → 168.1 | 113.7 → 127.3 | 79.7 → 89.3 | 64.6 → 71.2 |
| Edgar | 314.1 → 338.1 | 218.6 → 233.2 | 150.2 → 165.3 | 114.8 → 122.2 | 87.3 → 97.4 | 67.5 → 74.2 |
| Inknut Junicode | 298.5 → 352.4 | 190.3 → 221.3 | 156.4 → 155.5 | 110.1 → 130.4 | 81.0 → 99.7 | 62.9 → 74.6 |
| Libre Franklin | 345.3 → 342.6 | 223.5 → 213.0 | 181.1 → 177.2 | 130.4 → 129.4 | 96.3 → 95.5 | 75.3 → 74.0 |
| Libris ADF | 362.9 → 357.5 | 238.1 → 231.9 | 171.1 → 166.2 | 124.2 → 122.8 | 96.5 → 93.7 | 79.2 → 74.0 |
| TeX Gyre Heros | 381.5 → 362.9 | 235.5 → 227.5 | 162.0 → 157.2 | 132.2 → 130.9 | 100.3 → 94.7 | 77.3 → 74.0 |
| TeX Gyre Schola | 312.7 → 356.5 | 210.9 → 225.7 | 147.6 → 164.4 | 112.0 → 122.9 | 84.4 → 94.2 | 66.3 → 74.9 |

| **ratio max/min** | 1.304 → 1.074 | 1.251 → 1.114 | 1.227 → 1.140 | 1.200 → 1.071 | 1.259 → 1.116 | 1.259 → 1.069 |

---

## 2a. Point size alone cannot do it

Every family was built at every integer point size from 5 to 21 pt and
paginated, and the best achievable ramp was then chosen against the anchor with
**no multiplier at all**. It leaves tier rms at **5.93 %** and a worst cell at
**15.4 %** — better than today's 9.32 %, and still worse than the multiplier
proposal's 3.60 %.

The reason is quantization, and it is structural rather than a fitting failure:
one point size is worth 10.4–27.6 % of words per page depending on family and
slot, while the defect being corrected is 20–30 %. There is frequently no
integer that lands inside the target. The multiplier exists because the lever
the problem needs is continuous and the point-size ramp is not.

Raw sweep: `data/almendra-anchored-2026-08-27/sweep.txt`.

## 2b. Ramp changes must replicate on both corpora, and mostly do not

Three ramp changes were built and measured on both passages. **Only one
survived.** This section is the negative result, and it is the half that stops
the other two being re-proposed.

| candidate | ramp tried | verdict |
|---|---|---|
| Inknut Junicode | S slot 10 → **11 pt** | **ADOPTED** — improves on both corpora, same sign, same order of magnitude |
| Libre Franklin | 7 9 **11 13** 14 16 | rejected — does not reduce the error, it **relocates** it onto the default reading size |
| Coelacanth | L/XL 18 20 → **17 19** | rejected — **reverses sign between corpora**; helps on one passage and hurts on the other |

The Coelacanth case is the instructive one. On passage A the 17 pt L slot
measures 99.65 words per page against the anchor's 96.3 — an overshoot — while
the shipped 18 pt measures 79.7, an undershoot of similar size. On passage B the
same pair lands differently. A change whose sign depends on which book you open
is not a correction, and adopting it would have been fitting to one corpus.

The first draft of this document had these two backwards: it recommended the
Coelacanth change and dismissed the Inknut one. Adversarial review caught both
by demanding every arm be reported on both corpora side by side, which is now
how the whole document reports.

Raw arms: `data/almendra-anchored-2026-08-27/coelAB.txt`,
`qcheckAB.txt`.

## 3. What it costs in book length

Pages for a 100,000-word book. This is the decision inside the decision:
Almendra is on the dense side of the tier, so anchoring to it makes six of the
other seven denser as well, and books get **shorter**.

See T3 in `data/almendra-anchored-2026-08-27/tables.md` for the full grid. The
summary:

- **Tier mean −4.2 %** across all six slots; **−5.1 % at M** specifically.
- Four families lose 8–15 % of their page count: Inknut Junicode −13.1 %,
  Coelacanth −11.5 %, TeX Gyre Schola −10.0 %, Edgar −7.9 %.
- Three gain slightly: Heros +3.8 %, Libris ADF +3.1 %, Libre Franklin +1.9 %.
- Almendra, by construction, does not move.

**If that shortening is unacceptable, the proposal should be rejected rather
than tuned.** Anchoring on a denser family and keeping the old book lengths are
not both available: the whole mechanism of the fix is making the others match
Almendra's density.

## 4. The rendered proof

Artifact **Almendra as the Ruler**:
<https://claude.ai/code/artifact/4f0ec82b-1d1a-42db-a3ed-80cd579684b6>

It shows the two extreme families at M today, and the same two at their
proposed sizes beside Almendra. Renders are from the real `render_harness` at
native panel pixels, lossless PNG.

**The page-level figure does not sell this proposal, and it should not be asked
to.** At the M slot the whole correction is worth about a line and a half of
text on a page — visible if you count lines, not obvious to the eye. The claim
here is about a **book**, not a page: the honest figure is T3 in §3, where the
same change is 151 pages of difference across a 100,000-word novel. A reader
notices the book length, not the line count.

## 5. The six cells that still miss by more than 5 % on both corpora

Reported because a proposal that only shows its wins is not a measurement.

| family | slot | pt | passage A | passage B |
|---|---|---:|---:|---:|
| Coelacanth | L | 18 | −7.2 % | −6.6 % |
| Inknut Junicode | M | 12 | +6.4 % | +5.5 % |
| Libre Franklin | XS | 9 | −6.4 % | −6.8 % |
| Libre Franklin | S | 10 | +8.9 % | +9.7 % |
| Libre Franklin | M | 12 | +5.6 % | +5.4 % |
| TeX Gyre Heros | M | 12 | +6.8 % | +6.3 % |

Four of the six are Libre Franklin's and Heros's, and both of those ramps carry
a **1 pt step** (7 9 10 12 … and 7 9 11 12 …). A 1 pt step is the smallest move
the ramp can make and it is still coarse relative to the residual, so these
cells are quantization-bound: no value of `k` fixes them, because `k` moves the
whole ramp and the error here is between two adjacent slots. Coelacanth's L is
the cell whose ramp fix failed to replicate (§2b).

Note the signs agree across corpora in all six. These are real, not noise.

## 6. Almendra is a good anchor on the evidence

The owner ruled that Almendra's own progression is accepted as it is, so this
section was not required to reach a verdict. It reached a favorable one anyway,
which is worth recording because every other family now inherits this ramp's
shape.

Almendra's slot-to-slot progression is the **smoothest in the tier**: rms
**0.60 %** against its own power-law fit. Whatever unevenness the other seven
inherit from it is smaller than the residuals in §1. Anchoring on it does not
propagate a defect.

---

## Provenance

Every table here was read back from the real firmware paginator — slim parser,
CSS parser, `ParsedText`, Liang hyphenator, `GfxRenderer`, `SdCardFont` — over
two independent prose corpora at the shipped X3 geometry, on built `.cpfont`
files. Nothing was modeled.

Raw measurement files are preserved in
`data/almendra-anchored-2026-08-27/`: the three multiplier rounds
(`r1.txt`, `r2.txt`, `r3.txt` with their `k_round*.json` and `round*.yaml`), the
full integer sweep (`sweep.txt`), the two ramp-change replication arms
(`coelAB.txt`, `qcheckAB.txt`), and the final tables (`tables.md`,
`cannot.md`, `FINAL.json`, `PROPOSAL.json`).

## ADOPTED, 2026-08-27, and re-measured on the shipped build

Owner: yes. Applied to `lib/EpdFont/scripts/sd-fonts.yaml` — seven families
carry a family-wide `scale:` on all four styles with `metrics:` multiplied by
the same k, and Inknut Junicode's S slot moved 10 → 11 pt. Almendra is verified
**byte-identical**: it is the anchor and does not move.

**The proposal was re-measured against the tree that actually shipped**, not
against the scratch candidates it was fitted on — that tree was lost when the
run producing it was killed, so a proposal verified only there would have been
verified on nothing.

| | proposal predicted | measured on the shipped build |
|---|---:|---:|
| tier rms vs anchor | 3.60 % | **3.58 %** |
| ratio max/min at M | 1.071 | **1.068** |
| ratio at XXS / XS / S / L / XL | 1.074 / 1.114 / 1.140 / 1.116 / 1.069 | 1.074 / 1.114 / 1.140 / 1.116 / **1.083** |

Same instrument, same corpus (passage A), so the comparison is like for like.
Five of seven families reproduce to **0.05 words per page**. Two do not: Edgar's
M slot by 2.43 and TeX Gyre Heros's XL by 3.11, which is the whole of the XL
ratio's drift from 1.069 to 1.083. Both are within the ±1 % the document already
claims for k and are consistent with integer rounding of `metrics:`; neither was
re-tuned, because tuning a family to one corpus is the mistake §2b exists to
record.

**Two operational notes for the next ramp change.** Moving a slot's point size
leaves the OLD file behind — Inknut's 10 pt survived the rebuild at all three
tiers and had to be deleted by hand; `tools/validate_seed_fonts.py` catches it
as an orphan, which is what that gate is for. And the 3x trees on disk are now
**stale**, built against the pre-adoption recipe; they are excluded at bundle
time so nothing ships from them, but re-enabling 3x means rebuilding them first.

The instrument is now in the repo at `tools/words_per_page/` rather than in a
scratch directory, for the reason its README gives.

---

## WHAT THIS DOCUMENT DID NOT MEASURE — Inknut's S → M step, 2026-08-28

*Written from an owner report the day after adoption — "check inknut's sizes.
they seem fucked and switching between doesn't work as spec'd" — and the
measurement it triggered. Numbers below are read back from the shipped
`.cpfont` headers in `crosspoint-simulator/build/seedfonts`, not modeled.*

**Every table above optimises one slot's ABSOLUTE words per page against the
anchor. Nothing above constrains a slot against its own NEIGHBOUR.** §6 is the
only progression measurement in the document and its subject is Almendra's
progression, not the progression of the family whose ramp changed. So the one
ramp change §2b adopted — Inknut Junicode's S slot, 10 → 11 pt — went in with
its neighbour effect unmeasured.

The residuals in §1 already show it. Inknut's per-slot error against the anchor
runs +0.8 / −2.7 / **−4.5 / +6.4** / +3.6 / −1.9 %. Adjacent pairs therefore
disagree by 3.5, 1.8, **10.9**, 2.8 and 5.5 points: S and M straddle the anchor
from opposite sides and the swing across that one step is twice the next worst
in the family. A collapsed step is exactly what that looks like.

Measured on the built files, regular style:

| | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| point size | 7 | 9 | 11 | 12 | 14 | 16 |
| x-height, px | 8 | 10 | 12 | **13** | 15 | 17 |
| advanceY, px | 21 | 27 | 32 | **35** | 41 | 47 |
| step in advanceY | — | ×1.286 | ×1.185 | **×1.094** | ×1.171 | ×1.146 |
| words per full page (§2) | 352.4 | 221.3 | 155.5 | 130.4 | 99.7 | 74.6 |
| step in words per page | — | ×1.593 | ×1.423 | **×1.192** | ×1.308 | ×1.336 |

The anchor's own S → M is ×1.328. Inknut's is ×1.192 — the smallest step
anywhere in the tier, and the only one under ×1.2 that this document created.

**Before adoption Inknut's x-height ramp was 8 / 10 / 12 / 14 / 16 / 18 —
even at every step, which only Libris ADF also manages.** It is 8 / 10 / 12 /
**13** / 15 / 17 now. Rendered book pages at all six slots show S and M at
visibly the same size while every other step reads as a step.

**Why the mechanism is the S-slot move and not `k`.** `k = 0.917` shrinks all
six slots together and would have left the ramp's shape alone. The S slot's
point size was then raised to cancel `k` at that one slot (11 × 0.917 = 10.09,
and the 11 pt file measures byte-for-byte what the old 10 pt file measured —
advanceY 32, x-height 12, cap 17). Nothing compensated M, L or XL. So the
bottom half of the ramp sits on the tier median and the top half sits a slot
low, and the discontinuity lands between them:

| against tier median | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| advanceY | −2.0 | −1.5 | −1.5 | **−4.0** | **−5.0** | **−4.5** |
| x-height | 0 | 0 | 0 | −0.5 | −0.5 | −0.5 |

Inknut is now the SMALLEST family in the tier at M, L and XL. That part is the
normalisation working as ruled — it is the widest face in the set and had to
shrink — but it is also what an owner sees first, and this document should have
said so in the adoption notes.

**No integer ramp fixes it at `k = 0.917`, which is §2a restated.** S at 10 pt
overshoots the anchor by about +15 %; M at 13 pt undershoots by about −9.5 %
and merely relocates the collapse onto M → L. The three honest options are
(a) accept the step as the price of the per-slot match, (b) revert Inknut's S
slot to 10 pt, which restores the even x-height ramp and puts the collapse back
at XS → S where it sat before this document and where nothing was reported, or
(c) re-fit Inknut's `k` against progression as well as absolutes. **This is an
owner ruling and nothing here has been changed.**

**A gate is not free either.** `validate_seed_fonts.py` check E requires only
that advanceY strictly INCREASE, so a half step passes. A step-evenness check
would have caught this before the build — but TeX Gyre Heros already ships
×1.083 at the same S → M pair and Libre Franklin ×1.100 at XS → S, both
predating this document, so such a check cannot land as a hard failure without
a ruling on those two as well.

**Checked and CLEAN, same pass:** the seed tree passes
`validate_seed_fonts.py --recipe` (8 families, 1x + 2x); the 2x companions pass
the tier-scale check; a family switch preserves the slot (rendered: Almendra
"M (14pt)" → Inknut "M (12pt)"); and `ensureLoaded` re-resolves
`fontPointSize` from the slot on every family change
(`SdCardFontSystem.cpp:128-146`), so the label and the rendered page cannot
disagree.

---

**Status: ADOPTED and shipped. Superseding "PROPOSAL" below.**

**Status was: PROPOSAL. Nothing was applied.** No font, recipe or ramp in
`lib/EpdFont/scripts/sd-fonts.yaml` was changed. Adopting this means editing
`scale:` and `metrics:` for seven families and one point size for Inknut
Junicode, then rebuilding the seed tree and re-running
`tools/validate_seed_fonts.py`.
