# Words per full page: the owner's metric, measured

*2026-08-26. Firmware `bc065233d`, simulator `5fde54c`, seed tree
`crosspoint-simulator/build/seedfonts`. Every number is READ BACK from the real
firmware paginator — `ChapterHtmlSlimParser` + `ParsedText` + the Liang
hyphenator + `GfxRenderer` + `SdCardFont`, linked host-side the way
`test/table_keep_together` links them — over real prose at the shipped X3
geometry. **Nothing was applied: no font, recipe or ramp was changed.** The
candidate cuts live in a scratch tree outside both repos.*

Owner, 2026-08-26: *"a better way to normalize the font is by how many words fit
in a text filled page. make sense? so that page length a book is close to the
same for each font size, irrespective of font used."*

He is right, and this supersedes the metric
[size-ramp-band-2026-08-26.md](size-ramp-band-2026-08-26.md) anchors on. x-height,
cap height and ink per character are all *proxies* predicting how much text
fits. Words per full page *is* how much text fits: it composes advance widths,
x-height, leading, word spacing, hyphenation and line breaking into the single
number a reader experiences.

> **Superseded in part, 2026-08-27.** The METRIC and the METHOD here stand and
> are reused unchanged. The TARGET in §6 — the tier median — was replaced by the
> owner with Almendra: see
> [almendra-anchored-sizing-2026-08-27.md](almendra-anchored-sizing-2026-08-27.md),
> which carries the built-and-measured multipliers, what they cost in absolute
> book length, and a correction to `sd-fonts.yaml`'s claim about Almendra's
> x-height at the L slot.

Rendered proof: the Artifact **Words Per Page**,
<https://claude.ai/code/artifact/f1e772ba-e3a0-40a0-aa6d-305b817cad42>.

---

## 0. The answer

1. **The property does not hold today.** At the default M slot the eight
   families run 110.1 to 132.2 words on a full page — a ratio of **1.200**, and
   a 100,000-word book that is **757 pages in TeX Gyre Heros and 908 in Inknut
   Junicode: 151 pages longer for the same book.** The widest slot is XXS at
   1.304; the largest absolute page gap is XL, at 327 pages per 100,000 words.

2. **It cannot be fixed by choosing point sizes.** One point size is worth
   **10.4 % to 27.6 %** of words per page across the ramp, and the whole defect
   is 20 % to 30 %. The grid step is the size of the error. Optimal integer
   re-rounding takes the M ratio from 1.200 to 1.158 and leaves several
   families further from the target than they are now.

3. **It can be fixed with the recipe's existing fine levers**, which are
   continuous. One per-family multiplier `k` applied to BOTH `scale:` and
   `metrics:` — a fractional point size, in effect — collapses the spread:

   | slot | XXS | XS | S | M | L | XL |
   |---|---:|---:|---:|---:|---:|---:|
   | ratio now | 1.304 | 1.251 | 1.227 | **1.200** | 1.259 | 1.259 |
   | ratio after | 1.079 | 1.078 | 1.162 | **1.061** | 1.054 | 1.049 |
   | pages/100k span now | 80 | 105 | 125 | **151** | 258 | 327 |
   | pages/100k span after | 22 | 35 | 95 | **49** | 59 | 67 |

   Eight values, none further from 1.000 than 5.1 %: Almendra 1.023,
   Coelacanth 0.955, Edgar 0.978, Inknut Junicode 0.949, Libre Franklin 1.035,
   Libris ADF 1.040, TeX Gyre Heros 1.045, TeX Gyre Schola 0.967.

4. **One ramp serves both tiers.** Layout metrics come from the 1x cut at every
   render scale — `getTextWidth` and `getLineHeight` read `fontMap`, and the
   hi-res family is consulted only inside `renderChar` (`GfxRenderer.cpp:775`,
   `:838`). So words per page is **tier-independent**, and the 1x/2x
   disagreement §6 of the band doc measures (Libris ADF at 2.11–2.20) does not
   change book length at all. Proved twice: bit-identical over all 48 cells
   between a `-DCROSSPOINT_RENDER_SCALE=1` and a `=2` build of the harness, and
   in the shipped reader's own ledger — `reading.jsonl`, built-in Libre
   Franklin 14 pt on one book, **110.0 words per 21-line page at scale 2
   against 109.5 at scale 1**.

5. **It agrees emphatically with this morning's Almendra change**, and
   **contradicts the claim that XXS now overshoots** (§4).

6. **One family cannot hit the target without leaving the leading band**:
   Inknut Junicode (§5).

---

## 1. The matrix: words per full page, 8 families x 6 slots

Passage A, 199 paragraphs / 8,631 words of real prose lifted from
`wingspan-the-whole-bird.epub` (a book on the test card); mean word length 4.32
characters. Point size in brackets. **Identical at 1x and 2x** — see §0.4.

| family | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra | 349.5 (8) | 227.5 (10) | 162.8 (12) | 122.6 (14) | 96.3 (16) | 76.1 (18) |
| Coelacanth | 292.6 (9) | 206.7 (11) | 150.2 (13) | 113.7 (15) | **79.7** (18) | **64.6** (20) |
| Edgar | 314.1 (8) | 218.6 (10) | 150.2 (12) | 114.8 (14) | 87.3 (16) | 67.5 (18) |
| InknutJunicode | 298.5 (7) | **190.3** (9) | 156.4 (10) | **110.1** (12) | 81.0 (14) | 62.9 (16) |
| LibreFranklin | 345.3 (7) | 223.5 (9) | **181.1** (10) | 130.4 (12) | 96.3 (14) | 75.3 (16) |
| LibrisADF | 362.9 (8) | **238.1** (10) | 171.1 (12) | 124.2 (14) | 96.5 (16) | **79.2** (18) |
| TeXGyreHeros | **381.5** (7) | 235.5 (9) | 162.0 (11) | **132.2** (12) | **100.3** (14) | 77.3 (16) |
| TeXGyreSchola | 312.7 (8) | 210.9 (10) | 147.6 (12) | 112.0 (14) | 84.4 (16) | 66.3 (18) |
| **median** | *329.7* | *221.0* | *159.2* | *118.7* | *91.8* | *71.4* |
| **ratio max/min** | *1.304* | *1.251* | *1.227* | *1.200* | *1.259* | *1.259* |

Bold marks each slot's extremes. A page here is a FULL page: the chapter's first
page (which carries the sinkage, `viewportHeight / 5` rounded down to whole
lines) and the trailing partial page are both dropped, and the figure is the
mean over the 21 to 136 pages that remain.

### 1a. The unit he actually feels — pages per 100,000 words

| family | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra | 286 | 439 | 614 | 816 | 1038 | 1314 |
| Coelacanth | 342 | 484 | 666 | 879 | 1255 | 1547 |
| Edgar | 318 | 457 | 666 | 871 | 1145 | 1482 |
| InknutJunicode | 335 | 525 | 640 | 908 | 1234 | 1590 |
| LibreFranklin | 290 | 448 | 552 | 767 | 1038 | 1329 |
| LibrisADF | 276 | 420 | 584 | 805 | 1036 | 1263 |
| TeXGyreHeros | 262 | 425 | 617 | 757 | 997 | 1294 |
| TeXGyreSchola | 320 | 474 | 678 | 893 | 1185 | 1507 |
| **span** | **80** | **105** | **125** | **151** | **258** | **327** |

At the default M slot, *Middlemarch* (about 316,000 words) is **2,392 pages in
Heros and 2,869 in Inknut** — 477 pages, twenty percent of the book.

### 1b. It is not an artifact of the passage

The whole matrix was re-run over Passage B — 247 paragraphs / 15,358 words of
technical prose from `ai-engineering-from-zero.epub`, mean word length 4.98,
a deliberately different corpus. Every family's deviation from its slot median
moves by at most 2.4 percentage points, and no family changes sign at any slot:

| family | A, worst slot | B, same slot |
|---|---:|---:|
| TeXGyreHeros | +15.7 % (XXS) | +15.4 % |
| InknutJunicode | −13.9 % (XS) | −15.6 % |
| Coelacanth | −13.2 % (L) | −11.8 % |
| LibreFranklin | +13.7 % (S) | +16.1 % |
| LibrisADF | +10.9 % (XL) | +11.1 % |

---

## 2. Why the point-size grid cannot deliver it

Words per page falls as roughly the **square** of the size: fitting
`w = exp(a) · pt^b` over each family's six shipped cuts gives b = −1.85 to
−1.94, and the fit is tight (residuals mostly under 2 %; leave-one-out
prediction error 1.8 % mean).

So one point size is worth `|b| / pt`:

| family | XXS | XS | S | M | L | XL |
|---|---:|---:|---:|---:|---:|---:|
| Almendra | 23.3 % | 18.7 % | 15.6 % | 13.3 % | 11.7 % | 10.4 % |
| InknutJunicode | 27.1 % | 21.0 % | 18.9 % | 15.8 % | 13.5 % | 11.8 % |
| TeXGyreHeros | 27.6 % | 21.5 % | 17.6 % | 16.1 % | 13.8 % | 12.1 % |
| *(the other five sit between)* | | | | | | |

**The defect is 20–30 % and the smallest available step is 10–28 %.** Solving
the fit for the point size that lands each cut on its slot median gives numbers
that are almost all fractional and within 0.7 pt of what ships — Almendra wants
8.23 / 10.20 / 12.16 / 14.23 / 16.33 / 18.69 against its shipped
8 / 10 / 12 / 14 / 16 / 18. Rounding those to integers takes the M ratio from
1.200 to 1.158 and leaves residuals as large as 10.5 %.

**Verified rather than assumed.** Edgar was rebuilt at 11 pt and 13 pt — sizes
it has never shipped — through the ordinary `build-sd-fonts.py` path with its
shipped recipe otherwise unmodified. The fit predicted 177.00 and 128.91 words
per page; the built cuts measure **182.61 and 130.82** (−3.1 %, −1.5 %). The
control arm, the same recipe at 12 and 14 pt under a different family name,
reproduces the shipped numbers **exactly** (150.21 / 114.81).

---

## 3. The three levers, measured on one family

`scale:` and `metrics:` are both continuous, and they are **not the same
lever**. TeX Gyre Heros was rebuilt four ways — control, `scale: 1.045` alone,
`metrics: x1.045` alone, and both — with everything else in its shipped recipe
untouched. At M (12 pt):

| arm | leading | words/line | words/page | vs control |
|---|---:|---:|---:|---:|
| control | 39 | 7.47 | 132.17 | — |
| `scale: 1.045` (glyphs only) | **39** | 7.19 | 125.91 | −4.7 % |
| `metrics: x1.045` (leading only) | **40** | 7.47 | 126.69 | −4.1 % |
| both | **40** | 7.18 | 122.77 | **−7.1 %** |

Three things follow, and the middle one is the finding:

* **`scale:` alone does NOT move the leading.** The recipe applies `scale:`
  before `metrics:`, and `metrics:` then re-pins hhea in absolute em terms, so
  the glyphs shrink or grow on an unchanged advanceY. That makes `scale:` a
  purely HORIZONTAL lever with an exponent of about −1.0 on words per page —
  half the effect of a point size, and it changes the face's proportions on the
  page.
* **The two together behave like a fractional point size**, exponent −2.11
  measured against the point-size fit's −1.94. This is the lever the
  recommendation uses.
* **Leading is quantized to whole pixels** and glyph advances are not. A 4.5 %
  metrics change on a 39 px leading buys 40 px, which is 2.6 % and not 4.5 %.
  So the vertical half of any correction lands within one pixel — 2.0 % of the
  page at XL, 4.5 % at XXS — and the horizontal half is continuous. That is the
  real precision floor of this method, and it is an order of magnitude finer
  than the point-size grid.

---

## 4. Almendra: the change agrees, the overshoot does not

**The +2 was right, and the metric says so far more loudly than ink did.** The
retired ramp's cuts are the current ramp's cuts one slot down, so five of its
six sizes are measured here directly rather than modelled:

| slot | retired pt | words/page | vs slot median | pages/100k | shipped pt | words/page | vs median |
|---|---:|---:|---:|---:|---:|---:|---:|
| XXS | 6 | 595.0 *(extrapolated)* | +80.5 % | 168 | 8 | 349.5 | +6.0 % |
| XS | 8 | 349.5 | +58.2 % | 286 | 10 | 227.5 | +3.0 % |
| S | 10 | 227.5 | +42.9 % | 439 | 12 | 162.8 | +2.3 % |
| M | 12 | 162.8 | +37.2 % | 614 | 14 | 122.6 | +3.3 % |
| L | 14 | 122.6 | +33.5 % | 816 | 16 | 96.3 | +4.9 % |
| XL | 16 | 96.3 | +34.9 % | 1038 | 18 | 76.1 | +6.6 % |

A 100,000-word book was **614 pages in the retired Almendra against a tier
median of 842** — 228 pages short, a quarter of the book missing. After the +2
it is 816 against 842. **The change removed 91 % of the error at M and between
81 % and 95 % at every slot.**

**And it did not overshoot at XXS.** Band doc §5 records the XXS proof sheet
measuring Almendra's lowercase 25 % over the tier and concluding it "reads a
size up"; §8 records ink disagreeing and says the disagreement is the finding.
Words per page settles it in the other direction: Almendra XXS fits **6.0 % MORE
words than the tier median**, which means it sets *smaller* than the tier, not
larger. Reducing it to 7 pt would put it at **+35.4 %** of the median — a book a
quarter shorter again, and the exact defect the +2 was made to remove.

Both readings are true and they describe different things. Almendra has a tall
lowercase on a narrow set: at XXS its x-height is 10 px against the tier's 8,
and its mean advance is 6.84 px against Edgar's 7.54. x-height sees a big
letter; the page counts a narrow one. **For the property the owner asked for —
a book the same length whichever font he picks — the narrow set is what
matters, and Almendra XXS is fine.**

The one thing words per page confirms about Almendra is the direction of its
residual: it runs +2.3 % to +6.6 % above the median at every slot, so it is
still marginally on the small side and its recommended `k` is **1.023**, up not
down.

---

## 5. What it would take, per family

`k` multiplies both `scale:` and `metrics:` (see §3). It is one number per
family, not one per slot, because the required per-slot multiplier is nearly
constant within a family:

| family | k | required-multiplier spread across the ramp | ramp steps | current ramp |
|---|---:|---:|---|---|
| Edgar | 0.978 | 0.021 | 2 | 8 10 12 14 16 18 |
| LibrisADF | 1.040 | 0.022 | 2 | 8 10 12 14 16 18 |
| TeXGyreSchola | 0.967 | 0.024 | 2 | 8 10 12 14 16 18 |
| Almendra | 1.023 | 0.025 | 2 | 8 10 12 14 16 18 |
| Coelacanth | 0.955 | 0.045 | 2/3 | 9 11 13 15 **18** 20 |
| InknutJunicode | 0.949 | 0.065 | **1**/2 | 7 9 **10** 12 14 16 |
| LibreFranklin | 1.035 | 0.074 | **1**/2 | 7 9 **10** 12 14 16 |
| TeXGyreHeros | 1.045 | 0.079 | **1**/2 | 7 9 **11 12** 14 16 |

**The four families whose ramps step by an even 2 pt have a spread of
0.021–0.025 — one constant fits their whole ramp to within a percent. The four
with an uneven step have 0.045–0.079, three times worse.** The uneven step *is*
the residual: a 1 pt step where every other step is 2 puts one slot half a step
from where the ramp wants it, and no single multiplier can fix a slot that is
misplaced relative to its own neighbours.

That is the same defect [size-ramp-band-2026-08-26.md](size-ramp-band-2026-08-26.md)
§4 found at Libre Franklin's S slot on ink per character, reached from a
completely different measurement — and it **extends** it. LF's S wants 1.071
against its family's 1.035; Inknut's S wants 0.984 against 0.949; Heros' S wants
1.001 against 1.045. All three are the 1 pt step. The band doc's Libre Franklin
S candidate (10 → 11 pt) is confirmed: words per page wants 10.71 pt there.

### 5a. Verified on real builds, not only on the fit

Three families were rebuilt with their `k` applied to both `scale:` and
`metrics:`, through the ordinary build path, and measured the same way as the
shipped tree. Control arms of Heros and Coelacanth at their shipped recipes
reproduce the shipped numbers exactly, which is what makes the candidates valid
stand-ins.

| family | predicted residual (worst slot) | measured residual (same slot) |
|---|---:|---:|
| TeXGyreHeros | −7.9 % (S) | −6.6 % |
| InknutJunicode | +7.0 % (S) | +7.6 % |
| Coelacanth | +3.9 % (M) | +6.8 % |

Over all 18 measured cells the model is out by **1.4 percentage points on
average, 3.4 at worst** — smaller than the effect it is predicting everywhere
except where the ramp step is uneven.

### 5b. The leading cost, which is the constraint

Leading moves with `k`, so the band the families occupy widens:

| slot | leading band now | after | family at the bottom |
|---|---|---|---|
| XXS | 22–25 (3 px) | 21–24 (3 px) | InknutJunicode |
| XS | 28–30 (2 px) | 27–30 (3 px) | Edgar, Inknut, Schola |
| S | 32–36 (4 px) | 31–37 (6 px) | InknutJunicode |
| M | 39–41 (2 px) | 37–42 (5 px) | InknutJunicode |
| L | 45–49 (4 px) | 43–48 (5 px) | Inknut, Schola |
| XL | 51–55 (4 px) | 49–54 (5 px) | Inknut, Schola |

The band widens by 1–3 px, worst at M where it goes from 2 px to 5. **The family
at the bottom of it at five of six slots is Inknut Junicode**, and that is the
answer to "which family can only hit the target by leaving the leading band":

> **Inknut Junicode is a family whose fix is not a size change.** It is the
> widest-set and heaviest face in the tier — its lowercase alphabet is 411 px at
> M against Libre Franklin's 346 — so matching it on words per page means
> shrinking it 5 % and pulling its leading to 37 px while the rest sit at 38–42.
> Its ramp also carries the +1 px x-height offset `sd-fonts.yaml` records as an
> unexplained quirk, and one of the two uneven steps. It should be looked at as
> a fitting problem, not given a `k`, and it is the one family in this document
> whose recommendation is "not yet".

Nothing here suggests a leading change is *needed* on its own account: the
current band is 2–4 px wide and nobody has reported it, which is exactly the
shape of the band doc's finding that the observed floor is a conservative bound.

---

## 6. The target, and why the median

**The median of the eight families at each slot**, excluding nothing.

* It is derived from the families rather than invented, so the target moves if
  the tier changes and no external number has to be defended.
* It is robust to the extremes, which matters because the extremes are
  identities: Heros is a neo-grotesque with a large x-height on a narrow set,
  Inknut a heavy Devanagari-lineage oldstyle. Neither should drag the target.
* The mean was checked and never differs from the median by more than 1.8 %
  (the worst is L, 90.2 against 91.8), so the choice does not turn on it. The
  median is preferred only because it cannot be moved by one outlier.

**What it costs the families that are already fine.** This is the honest part:
a median target moves *everybody*, because the spread here is broad rather than
two outliers against six. The smallest `k` is Edgar's 0.978 and Almendra's
1.023 — a 2 % change, under a pixel of leading at every slot and invisible on
the page. The largest are Heros' 1.045 and Inknut's 0.949, about 5 %. **No
family is asked to move by as much as half a point size.** If a smaller
intervention is wanted, the four families within 2.5 % of the median at every
slot (Almendra, Edgar, LibrisADF, Schola — all four with even ramps) can be
left alone entirely, and the ratio at M still falls from 1.200 to about 1.09.

---

## 7. Method

**Measured** by linking the firmware's own paginator host-side —
`ChapterHtmlSlimParser`, `CssParser`, `ParsedText`, `TextBlock`, the Liang
hyphenator, expat, `GfxRenderer`, `SdCardFont` — the exact source list
`test/table_keep_together/CMakeLists.txt` uses, with its two `<img>`-path stubs.
So the pages counted are the pages the device emits: real hyphenated line
breaking, real auto-justify demotion at 40 characters, real widow/orphan
holdback, the real half-line paragraph gap, the real chapter sinkage.

Configuration is the shipped default at every point: X3 portrait, screenMargin
5, viewport **512 x 770**, lineSpacing NORMAL (compression 1.0),
extraParagraphSpacing on, hyphenation on, line grid off, justify threshold 40,
English trie installed.

Words are counted the way `EpubReaderActivity::publishReadingSample` counts
them — walk the page's `TAG_PageLine` elements, count non-blank tokens on each
block — with one correction: a word the breaker hyphenated arrives as two
tokens, so any token ending in `-` is dropped, which restores the source word
count. That is the bias `PageTextMetrics.h` warns about, and it is worth
0.0–1.9 % here. Characters per page are reported alongside and agree.

**Built** into a scratch directory outside both repos — never
`build/seedfonts` — through the ordinary `build-sd-fonts.py` path with the
shipped recipes unmodified except for the size list, the `scale:` and the
`metrics:` under test. Every candidate has a control arm at the shipped recipe
under a different family name, and every control reproduces the shipped numbers
exactly.

### Checked, and found CLEAN — so the next pass does not re-derive it

* **Tier independence.** Not assumed. `getTextWidth` / `getLineHeight` read
  `fontMap`; `getHiResFamily` appears only inside the three `renderChar` paths.
  Confirmed by a `=2` build of the harness producing bit-identical words per
  page over all 48 cells, and by the shipped reader's ledger at both scales.
* **Passage sensitivity.** Two corpora, 8,631 and 15,358 words, mean word
  lengths 4.32 and 4.98. No family changes sign at any slot; worst deviation
  moves 2.4 points (§1b).
* **Interpolation.** Edgar built at two unshipped point sizes; fit error 1.5 %
  and 3.1 % (§2).
* **The model end to end.** Three families built at their recommended `k`; mean
  error 1.4 percentage points over 18 cells (§5a).
* **Characters per page** ranks the families identically to words per page at
  every slot, so nothing here turns on the word/character choice.
* **Page-fill honesty.** Every figure is a mean over 21–136 full pages with the
  sinkage page and the trailing page excluded. Text-block ink coverage on the
  rendered proofs is 8.6–11.2 %, inside the 11–14 % band the blackness audit
  reports for the same geometry.

### What this does NOT measure

* **Reading comfort.** A family can hit the word target and still be the wrong
  size to read. Words per page is the owner's stated property and not a
  replacement for the band doc's floor: ink per character still detects a family
  set too small, and x-height still detects one set too large. Almendra XXS is
  the case where all three disagree, and §4 is what that disagreement means.
* **Anything but regular roman.** Both passages are body text. A family whose
  italic or bold is fitted differently would need its own pass.
* **Images, tables and headings**, which a real chapter has and neither passage
  does. They cost lines on a page equally for every family, so they dilute the
  effect rather than distorting it.

## 8. Files

* `lib/EpdFont/scripts/sd-fonts.yaml` — **unchanged**. If §5 is accepted it
  wants one `scale:` and one `metrics:` edit per family, and a note that `k`
  belongs on all four styles rather than regular alone.
* `crosspoint-simulator/build/seedfonts/` — read only; unchanged.
* [size-ramp-band-2026-08-26.md](size-ramp-band-2026-08-26.md) — its §4 Libre
  Franklin S finding is **confirmed** from an independent metric and generalised
  to three families (§5); its §5 "Almendra XXS overshoots" is **contradicted**
  (§4); its §6 tier disagreement is shown to have **no effect on book length**
  (§0.4).
