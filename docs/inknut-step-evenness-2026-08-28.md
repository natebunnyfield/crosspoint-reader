# Inknut Junicode re-fitted for STEP EVENNESS as well as absolute length

*2026-08-28. Measured against firmware `aeda1e234` (a display-name commit,
`26c976249`, landed in the same tree from another session while this ran; it
touches no size, scale or metric), simulator `a589b78`, seed tree
`crosspoint-simulator/build/seedfonts`. Every number below is READ BACK from
built `.cpfont` files or from the real firmware paginator over two independent
prose corpora at the shipped X3 geometry. Ten candidate `(k, ramp)` arms plus a
rebuild of the shipped configuration were BUILT and MEASURED; none of the tables
here is modeled. Raw data, including the six proof
renders at native pixels: `docs/data/inknut-step-evenness-2026-08-28/`
(its `README.md` is the index).*

Owner ruling, 2026-08-28: **re-fit Inknut Junicode's `k` optimising STEP
EVENNESS as well as absolute words-per-page.**

This closes the open question at the foot of
[almendra-anchored-sizing-2026-08-27.md](almendra-anchored-sizing-2026-08-27.md)
§ "WHAT THIS DOCUMENT DID NOT MEASURE". That section offered three options;
this document measured all three and adopted a fourth.

Rendered proof: the Artifact **Inknut's Missing Step**,
<https://claude.ai/code/artifact/6976e43f-3144-4e9a-9f9f-86cfb6f71eef>.

---

## 0. The answer

**`scale: 0.805`, ramp `[8, 10, 12, 14, 16, 18]`** — the anchor Almendra's own
point ramp, at a multiplier that puts it where Almendra's page is.
`metrics: {ascent: 968, descent: -274}` (the unchanged 1203/−340 base × k) and
the borrowed Junicode italic's ×1.2 becomes 0.966. Nothing else in the recipe
moves, and **no other family changes at all**.

| | shipped (k 0.917, 7 9 11 12 14 16) | adopted (k 0.805, 8 10 12 14 16 18) |
|---|---|---|
| rendered advanceY | 21 27 32 **35** 41 47 | 21 26 31 36 41 47 |
| advanceY step | ×1.286 ×1.185 **×1.094** ×1.171 ×1.146 | ×1.238 ×1.192 ×1.161 ×1.139 ×1.146 |
| a–z ink width, rendered (px) | 216 284 344 **373** 438 499 | 216 273 329 384 439 492 |
| set-width step vs anchor | +5.4 +0.7 **−6.5** +2.8 +2.0 % | +1.3 +0.2 **+0.7** +0.1 +0.4 % |
| words-per-page step, worst error vs anchor | **−10.2 / −11.2 %** | **+2.2 / +1.1 %** |
| words-per-page rms vs anchor | 3.77 / 4.17 % | **2.30 / 1.61 %** |

Two numbers per cell where two appear: passage A / passage B.

**The a–z row is the RENDERER's number, not a sum of advances**, and adversarial
review was right that the document did not say so. It is
`GfxRenderer::getTextWidth` through `tools/calendar_preview/render_harness`,
which returns the **ink extent** and advances the pen with a **per-glyph** pixel
rounding (`EpdFont.cpp:52-55`, `fp4::toPixel(prevAdvanceFP + kernFP)`) — so it
is not exactly proportional to the em, and it must not be, because that rounding
is what the page actually lays out. Summing `advanceX` from the glyph table
instead gives 219.4 274.4 329.4 384.2 439.1 494.1 (exactly 34.09 × effective
size at all six slots, as a pure em scale demands), and on THAT reading the
adopted set-width steps are +0.1 +0.1 −0.1 +0.1 −0.0 % — an order of magnitude
more even than the rendered row claims. Both readings are measurements of the
built files, they differ by ≤ 1 %, and the conclusion is the same on either;
the rendered one is quoted because it is what a reader sees. Class kerning is
not the difference — it is under 0.7 px across the whole alphabet at every slot.

**It improves BOTH objectives at once, so no weighting had to be chosen** — see
§4. That is the finding, not a lucky reading: the S → M collapse was never a
tension between evenness and absolute fit, it was a **1 pt step in the ramp**,
and removing the 1 pt step fixes both.

---

## 1. The defect, restated in one line

`sizes: [7, 9, 11, 12, 14, 16]` at `scale: 0.917` renders 11 pt as
11 × 0.917 = 10.09 — byte for byte what the retired 10 pt file rendered
(advanceY 32, x-height 12, cap 17). The S slot stayed where it was while M, L
and XL shrank with `k`, so the S → M step collapsed to ×1.192 in words per page
against the anchor's ×1.328, the smallest step anywhere in the tier.

The 2026-08-27 fit could not see it because it scored each slot's ABSOLUTE
words per page against Almendra and never scored a slot against its NEIGHBOUR.
Its own residual row already contained the answer — S −4.5 %, M +6.4 %, an
eleven-point swing across one step — and was read as within tolerance.

---

## 2. Why the cause is the 1 pt step and not `k`

Inknut needs a fixed FRACTION of Almendra's rendered size at every slot. Solved
per slot from the measured response curve, that fraction is

| slot | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Inknut size / Almendra size | 0.806 | 0.813 | 0.819 | 0.814 | 0.816 | 0.808 |

— flat to ±0.8 %, which is what makes a single multiplier the right instrument
at all. Call it 0.813. The ideal point ramp is then Almendra's `8 10 12 14 16
18` divided by whatever `k` is chosen:

| k | ideal ramp before rounding | rounded | S → M gap |
|---:|---|---|---|
| 0.917 | 7.03 8.87 **10.72 12.43** 14.23 15.86 | 7 9 **11 12** 14 16 | **1 pt** |
| 0.805 | 8.01 10.10 12.21 14.16 16.22 18.07 | 8 10 12 14 16 18 | 2 pt |

Both rows are `anchor pt × the per-slot fraction ÷ k` — the same formula, which
an earlier draft did not manage (it computed the second row from a uniform
0.800). The conclusion is unchanged under either: with a uniform 0.813 the
k = 0.917 row still rounds 10.64 → 11 and 12.41 → 12, still 1 pt apart, and the
k = 0.805 row still lands on 8 10 12 14 16 18.

At a `k` near 0.92 the two middle values straddle the midpoint from opposite
sides — 10.72 rounds UP, 12.43 rounds DOWN — and the collapse is arithmetic. At
a `k` near 0.81 the ideal ramp is already integral, because it IS the anchor's
ramp; the step shape is inherited rather than approximated.

**This is `almendra-anchored-sizing` §2a restated, not contradicted.** That
section proved point size alone cannot normalise the tier because one point is
worth 10–28 % of a page. The corollary nobody drew is that a ramp whose steps
are 1 pt somewhere and 2 pt elsewhere carries that same quantum as a *step*
error — which is exactly the defect being fixed.

---

## 3. The ten candidate arms, measured

Every arm was built with `build-sd-fonts.py` and paginated with
`tools/words_per_page/wpp1` on **both** corpora. `rms` is against Almendra over
the six slots; `worst step` is the largest disagreement between an adjacent-slot
words-per-page ratio and the anchor's own ratio at that step.

| arm | k | ramp | rms A / B | worst step err A / B | verdict |
|---|---:|---|---:|---:|---|
| shipped | 0.917 | 7 9 11 12 14 16 | 3.77 / 4.17 % | −10.2 / −11.2 % | the defect |
| revert S | 0.917 | 7 9 **10** 12 14 16 | 7.66 / 7.20 % | **−16.8 / −17.7 %** | **worse on both** |
| M → 13 | 0.905 | 7 9 11 **13** 14 16 | 4.41 / 5.04 % | −10.7 / −10.9 % | relocates it to L |
| even 2 pt from 7 | 0.883 | 7 9 11 13 15 17 | 5.34 / 6.01 % | +5.3 / +7.5 % | evenness yes, absolutes no |
| anchor ramp | 0.800 | 8 10 12 14 16 18 | 3.08 / 2.39 % | +3.8 / +2.9 % | |
| anchor ramp | **0.805** | 8 10 12 14 16 18 | **2.30 / 1.61 %** | **+2.2 / +1.1 %** | **ADOPTED** |
| anchor ramp | 0.809 | 8 10 12 14 16 18 | 2.19 / 1.30 % | +3.5 / +2.1 % | |
| anchor ramp | 0.813 | 8 10 12 14 16 18 | 2.42 / 1.67 % | +5.0 / +4.4 % | |
| anchor ramp | 0.818 | 8 10 12 14 16 18 | 1.86 / 2.19 % | +3.1 / +2.0 % | |
| anchor ramp | 0.822 | 8 10 12 14 16 18 | 3.11 / 3.34 % | +3.8 / +2.7 % | |
| even x-height (§8) | 0.898 | 8 10 12 14 16 18 | **17.6 / 18.6 %** | +6.3 / +4.0 % | even ramp, wrong size |

Residuals are `w/anchor − 1` in percent, as in `almendra-anchored-sizing`; the
`J` in §4 uses the log form, which is why its ordering can differ in the third
decimal from a reader's own arithmetic on this table.

**The RAMP is the finding; `k` inside 0.805–0.818 is not.** Every arm on the
anchor ramp beats every arm off it, on both corpora, on both objectives. Among
them the joint score moves by less than the quantisation — this is the same
conclusion `almendra-anchored-sizing` reached about best-of-three `k` rounds,
and the four decimals should be read as "about 0.81". 0.805 is the argmin of the
pre-committed objective in §4 (5.42e-4, against 0.818 at 7.13e-4 and 0.809 at
7.29e-4), which is why it was taken rather than 0.809 or 0.818.

**It is not top-two on every metric, and an earlier draft of this sentence said
it was.** On rms alone, passage A, the order is 0.818 (1.86), 0.809 (2.19),
0.805 (2.30) — the adopted k is *third*. What it does win outright is the step
metric, on both corpora (+2.2 / +1.1 % against 0.818's +3.1 / +2.0 % and 0.809's
+3.5 / +2.1 %), and the joint objective that weighs the two. Anyone preferring
absolutes alone should take 0.818; the ruling here was step evenness as a
co-equal objective, so the joint argmin is the honest pick.

### 3a. The negative results, which are the durable half

**Reverting S to 10 pt — option (b) in the superseded document — is WORSE, and
this was built and measured rather than reasoned.** It restores the pre-2026-08-27
even x-height ramp and it takes the collapse with it to XS → S at ×1.162
(−16.8 % against the anchor, half again worse than the defect it removes) while
leaving S 15.6 % over the anchor in absolute terms. **Do not re-propose it.**

**Moving M to 13 pt — option (c) as a ramp tweak — relocates the collapse to
L.** ×1.137 at M → L, −10.7 % against the anchor: the same defect, one slot up.
This is what the superseded document predicted without building it, and the
build agrees.

**`7 9 11 13 15 17` at k = 0.883 is the near-miss.** It is a genuinely even ramp
and its step errors are good (+5.3 / +7.5 % worst), but the absolutes go the
wrong way: XXS lands +8.6 / +9.8 % over the anchor and L −5.7 / −7.0 % under,
because a 2 pt step from a 7 pt base is proportionally too wide at the bottom
and too narrow at the top. It is the arm that shows the two objectives CAN
conflict, and that the adopted arm is not merely the only even one.

---

## 4. How the two objectives were weighted

They were not, in the end, and that is reported rather than hidden.

The objective was pre-committed before any arm was built. With
`r_i = ln(wpp_i / anchor_i)`:

```
J = mean(r_i^2)  +  lambda * mean((r_{i+1} - r_i)^2)
```

The first term is objective 2 (absolute words per page against Almendra); the
second is objective 1 (step evenness), because a step-ratio error is exactly the
difference of two adjacent absolute residuals. `lambda = 1` gives the two equal
weight, and J is averaged over the two corpora.

**The ranking is identical for every lambda from 0 to 30** — the anchor ramp
wins at pure-absolutes (lambda 0) and at near-pure-evenness (lambda 30) alike,
in the model sweep over all 12,376 integer ramps × 401 values of k
(`data/.../search.py`), and in the built arms. So no weighting decision was
actually load-bearing. Had one been, `lambda = 1` was the pre-committed value.

The model sweep was used ONLY to shortlist. It was validated first by
reproducing the shipped configuration's measured residuals (+1.2 −2.8 −5.7 +6.1
−0.0 −2.0 modeled against +0.8 −2.7 −4.5 +6.4 +3.6 −1.9 measured — worst error
3.6 points, at L), and every conclusion above is from a built file.

---

## 5. The leading band still holds

Measured off the built `.cpfont` files: worst-of-four-styles plain-text ink
span, against the `advanceY` that has to clear it plus the ±65/1000 (0.13 em)
rule.

| | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| clearance, shipped (px) | 2 | 3 | 4 | 4 | 6 | **8** |
| clearance, adopted (px) | 2 | 3 | 4 | **5** | 6 | 7 |
| as a fraction of the drawn em, adopted | 0.149 | 0.179 | 0.199 | 0.213 | 0.224 | 0.232 |
| ascender over the tallest accented cap, shipped (px) | 1 | 1 | 1 | 1 | 2 | 1 |
| ascender over the tallest accented cap, adopted (px) | 1 | 1 | 1 | 2 | 2 | 2 |

Equal or looser at four slots, 1 px tighter at XL, and never below the 0.13 em
floor — the tightest slot is XXS at 0.149, which is also what ships today
(0.150). The ascender clearance is equal or better at every slot. **The
candidate is not hitting its target by going cramped.**

**Which em — and the first version of this paragraph got the reason wrong.**
`scale:` divides `head.unitsPerEm`, so the point size's em (`em_ppem = pt ×
150/72`) and the em of the type as drawn (`em_drawn = em_ppem × k`) are no
longer the same number. Read against `em_ppem` the adopted XXS is **0.120 and
fails the floor**; read against `em_drawn` it is 0.149 and passes. So the choice
decides the verdict and has to be argued, not asserted.

This document originally argued it by claiming `metrics:` per-mille is expressed
in the drawn em. **That is false, and adversarial review caught it.**
`build-sd-fonts.py:720-731` applies `scale:` FIRST and then computes the metrics
override as `ascent × upem/1000` against the already-scaled upem — deliberately,
so line metrics stay em-relative. Measured: `ascent: 968` at 12 pt renders
`0.968 × 25 = 24.20 → 25 px`, and the built header says **25**; against the drawn
em it would be 19.48 → 20. `metrics:` per-mille lives in `em_ppem`.

The reading survives on a different argument, which is the one to keep.
**`em_ppem` is not a property of the file.** `advanceY` and every glyph are
functions of `pt × k` alone, so two different `(pt, k)` pairs that draw the same
size produce the same bytes — the pre-normalisation 12 pt at k = 1 still on the
simulator card renders advanceY 39, and 15.07 pt at k = 0.805 computes the same
39. Those two files would score differently on `clear/em_ppem` (0.13 apart) while
being pixel-identical. Only `clear/em_drawn` is a property of what is actually
drawn, so it is the reading a floor about overlapping lines has to use.

**And no gate enforces this either way.** `INK_PAD_PER_MILLE` appears only in
`tools/sans-bench/sweep_sans.py:34` and `tools/blind-bench/sweep_blind.py:23`,
neither of which runs over Inknut and neither of which has a `scale:` to
disambiguate the two ems. The floor is settled by this document alone, which is
why both readings are printed in `data/.../leading.txt` — so the next pass can
overturn the choice rather than inherit it.

---

## 6. What it costs in book length

Pages for a 100,000-word book, passage A:

| slot | XXS | XS | S | M | L | XL | mean |
|---|---:|---:|---:|---:|---:|---:|---:|
| shipped | 284 | 452 | 643 | 767 | 1003 | 1340 | |
| adopted | 284 | 428 | 590 | 801 | 1019 | 1317 | |
| change | 0.0 % | **−5.3 %** | **−8.2 %** | **+4.4 %** | +1.7 % | −1.7 % | **−1.5 %** |

Small and two-signed, unlike the 2026-08-27 adoption, which shortened Inknut by
13.1 %. The default reading size gets 4.4 % LONGER; S and XS get shorter. Net
across the ramp is −1.5 %.

---

## 7. Two things that got better and were not asked for

**The italic's fit improves.** The borrowed Junicode italic carries ×1.2 from
round 17, multiplied by `k` as always, so the fit is preserved exactly in design
units. In rendered pixels it improves, because the point sizes moved: the
italic's x-height sat 2 px under the roman's at the shipped 12 pt slot — the
worst mismatch in the family, and the M slot, where it is read most — and is now
1 px under at five slots and level at XL. `data/.../per_style.txt`.

**The tier gets tighter.** With Inknut on the anchor ramp, tier rms against
Almendra over all 48 cells falls 3.35 → **3.18 %** on passage A and 3.40 →
**3.12 %** on passage B, and the max/min spread at S narrows 1.140 → 1.127 (A)
and 1.166 → 1.141 (B). Nothing else moved; this is Inknut's own improvement
propagating.

---

## 8. The x-height ramp is NOT restored to 8 / 10 / 12 / 14 / 16 / 18, and what that would cost

Stated plainly because the superseded document names that even ramp as what
regressed. The adopted ramp renders x-height **8 / 9 / 11 / 13 / 15 / 17** —
one 1 px step at XXS → XS, then four 2 px steps.

Measured across four built arms, Inknut renders **1.113 px of x-height per unit
of effective size**. So a uniform 2 pt step at k = 0.805 buys **1.79 px**, and
five steps of a whole 2 px need k ≥ **0.898**. That is not a derivation left
hanging: **the k = 0.898 arm was built.**

| k = 0.898, ramp 8 10 12 14 16 18 | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| x-height | 8 | 10 | 12 | 14 | 16 | 18 |
| residual vs anchor, passage A | −16.3 | −15.5 | −17.3 | −16.2 | −21.1 | −18.9 % |
| residual vs anchor, passage B | −16.4 | −16.7 | −20.0 | −18.3 | −21.0 | −18.9 % |

**It works, and it costs the whole normalisation.** The even x-height ramp comes
back exactly, its step errors are fine (+6.3 / +4.0 % worst, because the ramp
shape is still the anchor's) — and Inknut is then 16–21 % under Almendra's words
per page at every single slot, rms 17.6 / 18.6 %. That is a family a fifth larger
than everything else on the card, which is precisely the defect the 2026-08-27
anchoring exists to remove.

So the two properties are separable and priced: **the RAMP buys step evenness,
`k` sets absolute size, and the even x-height ramp is only reachable at a `k`
that abandons the anchor.** The even 8/10/12/14/16/18 that shipped before
2026-08-27 came from an IRREGULAR point ramp (7 9 10 12 14 16, steps 2/1/2/2/2)
at k = 1 — the same effect, reached the same way: by being too big.

**Even x-height PIXELS and even STEP RATIOS are different targets**, and the
ruling here is the latter. The anchor's own rendered x-height is 9/11/13/15/16/19
— steps 2/2/2/1/3 — so the reference is not even either, and its page is the
thing being matched.

---

## 9. Checked, and found CLEAN

- **Only Inknut moved.** The recipe diff is four value lines plus comment; the
  other seven families' `scale:`, `metrics:` and `sizes:` are byte-identical
  (`git diff lib/EpdFont/scripts/sd-fonts.yaml`).
- **No two slots render alike.** advanceY 21/26/31/36/41/47, x-height
  8/9/11/13/15/17, cap 11/14/16/19/22/24 — the trap that produced the defect
  (a point size whose product with k lands where another slot already renders)
  was checked explicitly and does not recur.
- **advanceY is identical across all four styles at every slot**, in both the
  shipped and the adopted build.
- **Orphans deleted.** The vacated 7, 9 and 11 pt files were removed at 1x and
  2x. `build-sd-fonts.py` reported the 2x set as "3 file(s) from an older ramp
  that nothing can load" — the gate `almendra-anchored-sizing` asked for, doing
  its job.
- **`validate_seed_fonts.py --recipe --max-tier 2` passes**: 8 families,
  1x + 2x, header-verified against the recipe.
- **Full firmware host suite passes**: 594/594 (2 disabled sweeps unchanged).
- **Desktop simulator canary builds**: `pio run -e simulator` SUCCESS.
- **The shipped tree reproduces the scratch arm byte for byte.** Words per page
  read back off `crosspoint-simulator/build/seedfonts` after the real rebuild
  equals the candidate arm at all six slots on both corpora, to the last digit —
  the check `almendra-anchored-sizing` added after its own scratch tree was lost.
- **The ink band is not at risk.** Inknut is the heaviest family in the tier at
  every slot (25.9/43.0/51.5/75.2/102.0/134.1 against a floor of
  17.9/29.3/35.2/51.8/71.0/91.6, `size-ramp-band-2026-08-26.md` §2a) and the
  adopted effective sizes are within 4.3 % of the shipped ones at every slot
  (table in the next bullet), so every slot stays 30–50 % above the floor. Not
  re-measured; the margin is an order of magnitude larger than the move.
- **`hires_drops` unchanged and still correct.** The cap it exists for is
  `EpdGlyph`'s uint8 glyph extent, and that binds at the LARGEST slot, which
  got smaller: 18 × 0.805 = 14.49 against the shipped 16 × 0.917 = 14.67
  (−1.2 %), and at 2x 28.98 against 29.34. **Three slots did grow** — XXS
  +0.33 %, M +2.42 %, L +0.33 % — so "every size is smaller" would be false and
  is not the argument; the argument is that the binding slot shrank and that
  `fontconvert_sdcard.py` RAISES on an over-extent glyph rather than truncating
  (fontconvert_sdcard.py:1265-1281), so a build that completes has proved it.
  Both the 1x and 2x builds completed clean.

  | effective size | XXS | XS | S | M | L | XL |
  |---|---:|---:|---:|---:|---:|---:|
  | shipped, pt × k | 6.419 | 8.253 | 10.087 | 11.004 | 12.838 | 14.672 |
  | adopted, pt × k | 6.440 | 8.050 | 9.660 | 11.270 | 12.880 | 14.490 |
  | change | +0.3 % | −2.5 % | −4.2 % | +2.4 % | +0.3 % | −1.2 % |

## 10. What was NOT done

- **The 3x tree was not rebuilt.** `build/seedfonts/InknutJunicode/3x/` still
  holds 12/14/16 from the pre-2026-08-27 recipe. It was already stale before
  this change, is excluded at bundle time, and `--max-tier 2` reports it as
  skipped rather than passed. Re-enabling 3x means rebuilding it, as it did
  yesterday.
- **The simulator's card (`fs_/`) and the download manifest were not
  reprovisioned.** Only `crosspoint-simulator/build/seedfonts` was rebuilt.
- **Nothing was committed or deployed**, per the brief.

---

## 11. The adversarial review, and what it checked and found CLEAN

Run as a phase, read-only, by an agent that did not do the work. **It overturned
four things in this document and none of them changed the decision** — which is
the useful shape: the numbers held, the reasoning around them did not.

**Corrected in place, from its findings:**

1. **§5's justification for choosing `em_drawn` was FALSE.** It claimed
   `metrics:` per-mille is expressed in the drawn em. It is not — `scale:` is
   applied first and the override is computed against the already-scaled upem
   (`build-sd-fonts.py:720-731`), so `ascent: 968` renders 24.20 → 25 px at
   12 pt and the header says 25. Reproduced before acting. The reading survives
   on the invariance argument now in §5; the old reason is gone.
2. **§3's "top-two on both metrics on both corpora" was FALSE.** On rms alone,
   passage A, 0.805 is *third*. Corrected, and the honest basis for the pick —
   the joint argmin, plus an outright win on the step metric — is stated instead.
3. **§0's a–z row had unstated provenance** and could not be reproduced from the
   glyph table. It is the renderer's ink extent with per-glyph pixel rounding,
   not an advance sum; both readings are now given, and they agree on the
   conclusion. Confirmed by reimplementing `EpdFont::getTextDimensions` — it
   reproduces the harness to ±1 px at every slot.
4. **§2's two table rows were computed by different formulas.** Both are now
   `anchor pt × per-slot fraction ÷ k`, and the conclusion is shown to hold
   under either convention.

**CHECKED AND FOUND CLEAN — do not re-derive these:**

- **The derived numbers, including the base.** `round(1203×0.805)=968`,
  `round(340×0.805)=274`, `1.2×0.805=0.966` exactly. The 1203/−340 base is
  confirmed from history (`git show c450fa685^` has Inknut at
  `{ascent: 1203, descent: -340}`, italic `scale: 1.2`) as well as by
  `round(1203×0.917)=1103`. `scale:` never touches `metrics:`, so there is no
  double-count: built advanceY equals `round((968+274)/1000 × pt × 150/72)` at
  all six slots, and the same formula reproduces the shipped ramp from
  1103/−312. Incidentally the change makes the italic EXACT — the shipped
  `1.101` should have been `1.2 × 0.917 = 1.1004`.
- **Only Inknut moved.** The diff is two hunks, both inside the
  `InknutJunicode` block. `hires_drops`, `word_space_em`, `variable:`,
  `synthetic:`, the URLs and every other family are byte-identical.
- **The seed tree, including a deliberate hunt for a wrong tree the validator
  would pass.** 1x and 2x each hold exactly 8/10/12/14/16/18; 2x advanceY
  41/52/62/72/83/93 against 2× the 1x, max deviation 1; glyph counts 2692 vs
  2693 — exactly the one `hires_drops` codepoint, so check H's 0.99 floor is
  doing real work. **`2 × 8 = 16` is itself a slot**, so `2x/_8` and `1x/_16`
  are both 16 ppem renders — the same benign coincidence the old ramp had at
  `2 × 7 = 14`, and not the B-039 fault, which would show `|21 − 42| = 21`
  against a tolerance of 3. (The review called those two files "verified
  identical"; **they are not** — I checked the md5s and they differ, because the
  2x tier drops U+2E3B and the 1x tier does not: 2692 glyphs against 2693, 342
  bytes apart. Every metric matches — advY 41, asc 33, desc −10, x-height 15,
  cap 22 — which is the substantive point, but "identical" was too strong.)
  No wrong tree could be constructed that passes.
- **The duplicate-render trap**, verified with an independently written header
  reader: all six slots distinct on advanceY, x-height, cap, ascender,
  descender and set width. The §1 premise checks out too — the retired 10 pt
  file still on the simulator card reads advY 32 / asc 26 / desc −8, byte-identical
  to the shipped 11 pt.
- **Every words-per-page figure, recomputed from raw `arms.txt` for all ten
  arms on both corpora.** Every published cell in §3 matches to the last
  decimal, and the definitions in §3's preamble are the ones actually used.
  §6 (book length) and §7 (tier effect) recompute exactly. §4's model
  validation reproduces. `C(17,6) = 12376` and 401 k values confirmed.
  §3's harder claim — every on-ramp arm beats every off-ramp arm on both
  corpora on both objectives — holds. The review called the margin "narrow" at
  +5.00 % against +5.35 %; **recomputed, it is not narrow**: taking each arm's
  worst step error across BOTH corpora, the worst on-ramp arm is 0.813 at
  +5.00 % and the best off-ramp arm is 0.883 at **+7.55 %**. The +5.35 % figure
  is a single-corpus reading of a claim stated over both.
- **The leading-band numbers**, re-derived independently: clearance 2/3/4/5/6/7,
  both em readings, accented-cap margins 1/1/1/2/2/2, Almendra's 0.347–0.390.
- **`hires_drops` in both directions.** Still needed at 2x — the widest U+2E3B
  is bold-italic at ~268 px, over the 255 cap. No new drop needed — U+2E3A
  bold-italic is 176 px at 2x/18 pt. And `tier_drops: 3: [0x2E3B]`
  (`sd-fonts.yaml:288-291`) covers the 3x case that `hires_drops` alone would
  miss.
- **No live duplicate of Inknut's ramp exists.**
  `test/reader_slot_label/ReaderSlotLabelTest.cpp:34`'s `{7,9,11,12,14,16}` is
  not Inknut-coupled — TeXGyreHeros still ships exactly that ramp
  (`sd-fonts.yaml:1388`), so the fixture's comment is still true.

**Pre-existing defects it surfaced that this change did NOT introduce and did
NOT fix** — recorded so they are found once rather than re-found:

- **`sd-fonts.yaml` ~line 147 states the wrong formula.** It says
  `advanceY = ceil((ascent − descent)/1000 × pt × 150/72)`. The build **rounds**
  (FreeType pixel-rounds `face.size.height` before `norm_ceil` sees it). `ceil`
  mispredicts 12/14/16 pt on the new ramp and two slots on the old one; twelve
  of twelve slots across both ramps fit `round`, none fit `ceil`.
- **`crosspoint-reader/fs_/` and `tools/words_per_page/gen.py:16` carry stale
  Inknut ramps** (`7 9 10 12 14 16` at k = 1), and were stale before this change.
  Neither is a gate and neither is read by `wpp1` or the seed tree. The card's
  divergence grows from one slot to six.
- **No gate enforces the 0.13 em leading floor** over the shipping families at
  all (§5).

**Not re-verified by the review, and flagged rather than claimed:** the 594/594
host suite and the `pio run -e simulator` canary — the tree was dirty with
another session's work, so a second run would not have been attributable to this
change. The ink-band floor figures in §9 are cited from
`size-ramp-band-2026-08-26.md`, not remeasured, by both passes.
