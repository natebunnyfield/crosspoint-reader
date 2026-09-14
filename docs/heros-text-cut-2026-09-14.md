# Heros Text Cut — a punchcutter's corrections to TeX Gyre Heros, measured on the reader's own renderer

**Date:** 2026-09-14. **Firmware surveyed:** `a8368eb` (main). **Status:** INSTALLED 2026-09-14 (owner: "install fonts in testflight and firmware and everywhere else") — `installed_families` **twelfth** entry, recipe `HerosTextCut` beside `TeXGyreHeros`, sources in `lib/EpdFont/local_fonts/` and the private `crosspoint-local-fonts` mirror (commit 067f727), picker entry in `src/FontDisplayNames.h`, row in `docs/font-dates.md`, sim card `fs_/fonts/HerosTextCut` at 1x/2x/3x and the three packaged Mac apps' cards, iOS seed tree `build/seedfonts/HerosTextCut` at 1x+2x through `validate_seed_fonts.py`. Installed knowing the 11–12 pt trade below. Owner asked for it, judged three rounds by eye, and the last round was measured. Read this before proposing "make Heros heavier for e-ink" — the answer is size-dependent and the 12 pt slot is a measured regression.

## What was asked, in order

1. "A short paragraph of a hand-cut version of TeX Gyre Heros, Dutch punchcutting and vintage techniques for readability." First pass was a raster effect (wobble, ink squash). **Rejected:** "reading like an effect … no wobble, just slight differences between letters that improve legibility."
2. "Do not round corners, put in ink traps instead of collecting ink. Vectors, not rasters." → outline operations, a real TTF.
3. "Optimize this to be a 4-bit epub reader at 9–13 pt e-ink long-text font." Then mid-turn: "the ink traps are distracting as is, make them less obvious."
4. "Way too much space between letters at 9 pt, just make it so the letters do not touch (like `th` does). The only goal here is a font file that works well for small font sizes." → tracking and class bearings removed; fit measured per pair.
5. "There is still too much space between characters like in `la` and `fa`, bring them in so they don't touch but don't space them out to the point of distraction." → side bearings abandoned entirely (a bearing on `a`'s left, added for `fa`, loosened `la`); fit is now per-pair KERNING (below).
6. "This version seems much shorter, the space between the top right strokes of `f` is too small." → both measured true and both one cause: the offset was ISOTROPIC and moved the alignments (below). Weight is horizontal-only now; the narrow-sort extra is gone.
7. (a 9 pt italic line, stock beside cut) "the before kerning is better." → fitted kerns REMOVED from italic and bold italic; those carry stock kerning only. The ink-gap rule misreads a slanted style — an italic `r`'s arm overhangs the next letter by design, so every `r`-pair read as a collision and `ry` went out two pixels. **Fitted kerns are an upright-only correction.**
8. (a 9 pt bold chapter line) "improve kerning, especially obvious in `harf`, but audit entire face for why this font size is not working as well." → audited; NOT kerning. Every kern in `Wharf` is byte-identical to stock. The advance was stock + 2d (+10 u = +0.19 px at 9 pt) and rounding turned that into +1 px on **33 of 67 glyphs** (h a r e o, every digit). Advances are stock in units now; 0 of 67 glyphs differ in rounded advance at 9/11/12/14 pt, all four styles.

## The cut (vector, deterministic, `tools/textcut/textcut_vector.py`)

CFF cubics flattened to 24-segment polylines → shapely → three operations → TrueType `glyf` via `fontTools.fontBuilder`. Units at 1000/em. A letter is the same letter wherever it recurs; nothing random survives from round 1.

| Operation | Regular | Bold | Why |
|---|---|---|---|
| **Weight** — mitre offset (`join_style="mitre"`, limit 8), **horizontal only**: the buffer runs under a 50× vertical scale, so stems gain 2d and the alignments move d/50 ≈ 0.1 u | +5 u/side on every sort (stem 88 → 98, ≈ 2 px at the 9 pt slot) | +3 | a text cut is heavier than its display drawing. **Why horizontal-only** (found round 6): an isotropic offset moves baseline, x-height and cap height by d, and `FT_LOAD_FORCE_AUTOHINT` then snaps them to different pixels than stock — measured 9 pt cap/ascender 13 px vs stock 14, 11 pt 18 vs 16 — and the 9 pt `f` lost the row between hook and crossbar. Horizontal-only restores hinted x/H/l/p heights to exactly stock at all four slots (10/14/14/14 · 12/16/16/17 · 14/19/19/20 · 16/22/22/22). The earlier +7 on "narrow sorts" was my theory, not the ask, and was closing the f's hook; removed. |
| **Ink traps** — a notch polygon *subtracted* at every ink-side crotch narrower than 75° | mouth 8 u along each edge, 20 u deep along the bisector, 6 u flat floor, straight sides | same | 0.4–0.55 px at 9–13 pt: turns the crotch pixel from black to dark gray instead of letting it clog. Round 2 used 36 deep / 14 mouth and the owner called it distracting. Right-angle joins (E H L T F) get none; punctuation keeps its curls. 62–65 per style. |
| **Fit** | no tracking, no bearing changes — outline bearings equal stock (the offset's own width is added back). Heros's own GPOS kerning is carried into the TTF (`read_gpos_kern`, formats 1+2, design units). Then `fit_pairs.py` adds a POSITIVE KERN to exactly the pairs that touch at 9 pt | per style | regular 10 pairs, bold 14 (never-touch); italic 3, bold italic 0 (match-stock); all +53 u — measured at 19.01 px/em with the renderer's own pen rounding and ink = level ≥ 2 **italic and bold italic: none** (round 7; stock kerning only) |

**How the fit is measured** (`tools/textcut/fit_pairs.py`). Rasterize exactly as the converter does — 150 dpi, `FT_LOAD_RENDER | FT_LOAD_FORCE_AUTOHINT`, advance `round(linearHoriAdvance)`, origin `bitmap_left`, plus the pair's kern (units → px, rounded) — at 9 pt. For 186 letter pairs drawn from the harness's own body text plus the tight bigrams, the gap is the smallest **per-row ink** distance: over every bitmap row where both letters have coverage ≥ 64 (the converter's floor for level 1), leftmost-ink-of-b − rightmost-ink-of-a − 1. Zero abuts, negative overlaps. A pair at or below zero gets +¼ px of kern (13 u), re-cut, re-measured; every style converges in two passes. **Stock Heros regular abuts on 17 pairs at 9 pt**: `Th dy ff fi fl ft ry th ti tl tr ts tt tu tw ty vy`.

Three wrong instruments, recorded so nobody reaches for them: (a) a bounding-box overlap test — reported 144 italic "touches" that were slanted boxes with no ink contact; (b) a summed side-bearing correction — `t` leads eleven touching pairs and summing gave it +386 u; (c) side bearings at all, even minimal — the owner saw `la` loosen when `a`'s left side was widened for `fa`. Fixes live in `tools/textcut/kern-<style>.json` (`{"fa": 53, ...}`) and are layered over the source pairs in the GPOS the cutter writes.

**The cpfont kern matrix is 4.4 fixed-point, ±8 px, 1/16 px resolution**, and `fontconvert_sdcard.py`'s `extract_kerning_fonttools()` returns THAT (×16, clamped to int8), not design units — raw GPOS `f/a` is −10 u; it reports −128. Right for a .cpfont, wrong for a font file; the cutter reads GPOS itself.

**Glyph set**: every cmap codepoint in Basic Latin, Latin-1, Latin Ext-A/B and General Punctuation plus U+20AC — 395 glyphs per style (274–289 traps). ASCII-only was the proof-of-concept set; the harness's `é` in "Café" was coming from the Noto fallback chain, not the font. Accented letters are cut but NOT pair-fitted (the corpus is ASCII); their bearings are stock plus the offset compensation.

**Advance (round 8)**: STOCK in units. The offset's 2d used to be added to every advance; at 9 pt that is +0.19 px and half the alphabet rounded up a pixel — the loosening the owner read as bad kerning in `harf`. Now the stems grow into the bearings by d a side (0.09 px) and every glyph's rounded advance equals stock's at every slot (measured: 0/67 differ, ×4 slots, ×4 styles). Consequence: more pairs abut in the cut than in stock, so the upright kern fit carries more pairs (regular 31, bold 29, each +53 u) — those kerns put back the pixel stock has.

**Audit of the whole face at 9 pt, regular vs stock** (per-glyph four-level bitmap diff on a common origin): the largest changes are the diagonals and joins — N K B m L A V — gaining black pixels, which is the weight doing its job; no glyph lost ink; heights identical; the `f` hook open. Bold: R G Q S W E L gained; `e` and `w` −2 black each (a counter greying a touch). Nothing else moved.

9. (a 9 pt line with "30,865 tons, then 41,072") "audit for these vertical misalignments like 5 and 7 have." → audited every glyph's hinted top/bottom, four styles × four slots, against stock. Three findings: (a) **5 and 7 are a row low in STOCK too** — they top out at 694 u vs 709 for the other digits; (b) **the polygon pipeline was adding misalignments of its own** — flattening curves to lines makes FreeType's autohinter classify every top as flat, so `C G O Q S` rose a row at 11 pt and `0 3 6 8 U` at 14 pt; (c) stock's digits are inconsistent at 12 and 14 pt anyway (five and nine of ten below the cap row). → **`tools/textcut/textcut_curves.py` replaces `textcut_vector.py`**: cubic contours edited in place, cu2qu to quadratics, no shapely. Weight = d·n_x per on-curve point, handles follow, no point ever moves vertically; traps inserted at line-line crotches only. **Every digit scaled to a 715 u top** — swept 709..729, 715 is the smallest that puts all ten on the cap row at 9/11/12/14 pt. Result: digits aligned at every slot in every style; non-digit differences from stock across 1 072 glyph-slots: 3, none a real misalignment (`Q` tail 14 pt −1 row; bold `t` 12 pt loses stock's gray fringe row and snaps solid).

10. (a 9 pt italic line) "italic kerning is an issue against [stock]" → measured: the cut italic abutted on 63 pairs where stock abuts on 29 — round 8's stock advances let the +5 u stems eat the bearings, and round 7 had removed every italic fix. Neither "never touch" nor "nothing" is right for a slanted style. **Italic rule: MATCH STOCK** (`fit_pairs.py --match STOCK.otf`): lift a pair only if it touches AND stock keeps it apart, and only to stock's gap. Italic 44 pairs, bold italic 46, +53 u each; new-touch count 0. Found underneath: FreeType's autohinter places the cut italic's bowls `a e o s u` one pixel RIGHT of stock's and `b p` one LEFT (unhinted they match exactly) — the autohinter grid-fitting the wider stem on a slanted style; the pair fit absorbs it (≈30 pairs a pixel looser than stock, none tighter). Also verified: the converter stores the LINEAR advance in 12.4 fixed point (`fontconvert_sdcard.py:1148-1153`), not the hinted `advance.x` — so "advances equal stock in units" holds in the .cpfont exactly; the hinted `advance.x` differs from linear on ~25 glyphs per font and is NOT what ships.

11. **The proof recipe was wrong from round 3 to round 10, and it flattered stock.** Heros's recipe carries `scale: 1.014` on every style; `proof.yaml` was generated by replacing each style dict with `{path: …}`, which dropped the scale for the cut and kept it for the reference. Every width and darkness comparison in that span had stock rendered 1.4 % larger. Found in round 11 by parsing both `_9.cpfont` files (`docs/cpfont-format.md` §2.3–2.6, §3.3) and laying the harness paragraph out with the renderer's own `(prevAdvanceFP + kernFP + 8) >> 4`: 44 stored advances differed, all by that ratio, and the cut paragraph fit in 8 lines where stock needed 9. The recipe generator now copies the style dict and swaps `url` for `path`; stored advances match on all 95 glyphs. `fit_pairs.py` and the alignment audits were redone at the real 19.01 px/em: regular 15 pairs, bold 21, **italic 8, bold italic 5** (most of the italic "crowding" was the missing scale, not the cut); digits still on the cap row at every slot; non-digit differences from stock still the same three. **Lesson for the file: verify a comparison in the built artifact, not in a model of the pipeline** — the freetype model agreed with itself for eight rounds while the cpfont disagreed with both.

12. **The fitter now uses the renderer's exact pen arithmetic** (`fit_pairs.py`): advance as 12.4 fixed point of the linear advance, kern as 4.4 clamped to int8, pen = `(advanceFP + kernFP + 8) >> 4` — one rounding of the sum, per `GfxRenderer.cpp:815-828`. Rounding them separately (rounds 4–11) put an italic `ry` a pixel wide of stock. Final fits: regular 12 pairs, bold 22 (never-touch); italic 17, bold italic 7 (match-stock); all +53 u. No pair touches that stock keeps apart, in any style.

13. **Ink, for the touch test, is level ≥ 2 (coverage ≥ 128), not ≥ 1.** Parsing the built italic `.cpfont` (matrix index is `(leftClass−1)·cols + (rightClass−1)`; my first parse forgot the −1 and read neighbouring cells) showed stock `r→y` at +0.31 px and the cut at +1.31 — the visible pixel in "Ever y" was the fit, triggered by ONE level-1 pixel the cut's `r` gained at its arm tip. A light-gray fringe is not a collision. With the threshold at the panel's dark gray, the fits drop to **regular 10, bold 14, italic 3 (`ew ow ty`), bold italic 0**, and the italic sets exactly as stock does.

**Left as is, with the reason**: the autohinter places the cut italic's bowl glyphs `a c d e g o q s u` one pixel right of stock's (unhinted they match), so ~35 italic pairs whose right letter is a bowl read a pixel looser than stock. Tested a leftward outline nudge of those glyphs at −8/−13/−18/−26/−35 u: mismatched ink boxes vs stock went 13/14/12/12 → at best 7/9/9/9 (−26 u) and got WORSE at 12–14 pt beyond that. Not a lever; not applied.

**Kerning**: carried. The cutter reads the source GPOS PairPos (formats 1 and 2, extension lookups unwrapped) in design units, expands class pairs to glyph pairs for the glyphs it carries, layers the fitted fixes on top, and writes one `kern` feature through `feaLib` — 4 813 pairs in regular, 5 296 italic. The converter picks them up as it does for any font.

The trap's mouth apex sits 6 u *out in the paper*; with it exactly on the boundary the boolean left the old crotch vertex as a hairline spike (`v` had 11 points with a doubled-back `(251,108)`; now 10, clean). The `v` crotch reads `(113,527) → (247,122) → (246,72) → (256,72) → (256,122) → (397,527)` at the round-2 depth.

**Not done, deliberately:** apertures (c e s a) — a per-glyph redraw, not an operation. x-height — Heros's tier slots are x-height matched (9 pt → 9 px); raising it would move the family off the fit and needs a ruling.

## Measured on the firmware renderer, four gray levels

`tools/textcut/proof.yaml` builds two families with identical settings (`latin-ext`, `force_autohint`, `sizes: [9, 11, 12, 14]`, Heros's `metrics`): `HerosRef` from the stock CTAN OTFs and `HerosTextCut` from the four cut TTFs. Both through `build-sd-fonts.py --config` at 150 dpi into 2-bit `.cpfont`s, then `render_harness aa <family>` — the real layout, the real gray planes, 528×792. Whole-page histogram of the four levels (`tools/textcut/compose_eink.py`):

| slot | ink px A → B | solid-black share | dark (96) share | light (200) share | mean darkness |
|---|---|---|---|---|---|
| **9 pt** | 32 968 → 33 957 | **60.5 % → 71.8 %** | 24.6 → 15.0 | 14.9 → 13.1 | **0.791 → 0.840** |
| 11 pt | 42 360 → 47 129 | 77.4 → **71.1** | 11.7 → 10.7 | 10.9 → 18.3 | 0.871 → **0.817** |
| **12 pt** | 40 664 → 45 251 | 77.8 → **73.0** | 10.4 → 10.4 | 11.8 → 16.6 | 0.868 → **0.831** |
| 14 pt | 38 377 → 40 097 | 73.6 → 78.2 | 13.9 → 9.2 | 12.5 → 12.6 | 0.850 → 0.866 |

(Round 11 — the first table at true parity: both families from one recipe with `scale: 1.014`. Earlier tables had stock 1.4 % larger; they are superseded and were, if anything, kind to the cut at 11 pt.)

(Round 9, curve cutter, digits at 715. Round 6's isotropic offset read 73.1 / 78.5 / 72.9 / 79.5 — better at 11 pt only because it thickened horizontals and moved the alignments.)

(Round 6, horizontal-only weight. The round-3 isotropic offset read 73.1 / 78.5 / 72.9 / 79.5 black share — better at 11 pt only because it was also thickening horizontals and moving the alignments, which is what made the page look shorter. Kept here so the trade is visible.)

Shares are the fair comparison; absolute counts move with line breaks. The histogram is byte-for-byte within noise of the earlier loose-fit build (9 pt black share 73.1 → 72.9 %), which is expected: fit moves bearings, not ink.

**9 pt is the win**: a tenth more of the ink lands solid black and the dark-gray fringe drops by four tenths. **11 and 12 pt are regressions**, and the mechanism is the pixel grid, not the design: at 23–25 px/em the cut stem is 2.25–2.45 px, which the autohinter cannot land the way it lands stock's 2.0–2.2 px, so more edge falls to gray. One outline cannot serve four slots — the fix is a stem target per slot (optical masters), which the pipeline does not carry. 14 pt improves.

## Where things are

- `tools/textcut/textcut_vector.py <style>` — the cutter. Reads `downloaded_fonts/QHERO/texgyreheros-<style>.otf`, writes `BUILD_DIR/HerosTextCut-<style>.ttf`. The three non-regular OTFs were fetched from the recipe's CTAN URLs into the same cache dir.
- `tools/textcut/fit_pairs.py FONT [--fix bearings-<style>.json]` — the touch test and the minimal fix; exit 1 while any pair touches, so it loops.
- `tools/textcut/kern-{regular,bold,italic,bolditalic}.json` — the fitted per-pair kerns; uprights under the never-touch rule, italics under `--match`, in units, `{"fa": 53, ...}`; the cutter also dumps `HerosTextCut-<style>.kern.json`, the EFFECTIVE pairs (source + fixes) the fitter reads back.
- `tools/textcut/proof.yaml` — the two-family recipe; `path:` entries need `BUILD_DIR` replaced.
- `tools/textcut/compose_eink.py` — the figure and the histogram.
- `tools/textcut/textcut_curves.py <style>` is the cutter as of round 9; `textcut_vector.py` is kept for the history above and is superseded.
- Built cpfonts left in `tools/calendar_preview/fs_/.fonts/{HerosRef,HerosTextCut}/`, the way `proofsheet.py` leaves its variants.
- Proof pages (the owner judges by these): https://claude.ai/code/artifact/69c2a6b2-1964-4389-8e12-5353da9966f1 (e-ink, measured) and https://claude.ai/code/artifact/b7caa190-8903-4925-bf13-70be036d3980 (the print cut, three-tone overlay of the traps).

## When it was installed, and what the next person should check

Done as described below on 2026-09-14. Recipe carries `scale: 1.014` per style like Heros (dropping it is the round-11 trap). Still owed: a words-per-page re-sweep — the fitted kerns change a few line breaks, so the words-per-page ledger moves a little. Installed 2026-09-14 WITH the 11-12 pt regression unanswered, by owner decision. A reader at those two slots is handed a slightly lighter page than stock; 9 and 14 pt are darker.

## Round 14 — adversarial review, and the fitted kerns withdrawn

A read-only refuting pass over the install commit found one real defect and I
could not disprove it. **A kern is in DESIGN UNITS, so a value chosen to flip
one pixel at the 9 pt slot scales with ppem and over-applies at every larger
one.** Measured in the shipped `.cpfont`s, cut vs stock, 1/16 px:

| pair | 9 pt | 12 pt | 14 pt | 16 pt |
|---|---|---|---|---|
| `th` | +0.94 | +1.19 | +1.44 | +1.62 |
| `ti` `fi` | +1.00 | +1.38 | +1.56 | +1.81 |
| `tw` `ty` | +2.00 | +2.69 | +3.12 | **+3.56** |

`th` and `ti` are among the commonest bigrams in English, so 14 and 16 pt read
visibly looser than stock — and at 14 pt regular *nothing was touching*.

**The fitted kerns are withdrawn. None ships.** Measured rather than argued: the
stems take 10 u of ink gap (5 u a side), which is 0.19 px at 9 pt and sub-pixel
at every slot; every kern big enough to move a pixel is 3–7x that. Fixing a
sub-pixel encroachment with a 1 px instrument is what produced the 3.56 px.
Verified after the rebuild: **216,600 ASCII pairs x 6 slots x 4 styles, 0 differ
from stock.** The family carries Heros's own kerning exactly.

What that costs, abutting pairs of the 186-pair corpus, cut/stock per slot:

| style | 7 pt | 9 pt | 11 pt | 12 pt | 14 pt | 16 pt |
|---|---|---|---|---|---|---|
| regular | 12/12 | **10/10** | 8/4 | 4/2 | 2/1 | 0/1 |
| bold | 9/8 | 14/3 | 3/2 | 3/2 | 3/0 | 1/1 |
| italic | 22/10 | 5/2 | 3/2 | 6/0 | 4/0 | 2/2 |
| bold italic | **43/9** | 1/1 | 4/0 | 6/0 | 4/1 | 0/0 |

At 9 pt regular — the size and style every round of this was judged at — the cut
abuts on exactly as many pairs as stock. **Stock abuts too**: it is normal for
this face at these sizes, and the ask was that letters not touch at the size
being read, not that abutment be eliminated at six slots. **The one place the
cut is clearly worse is bold italic at the 7 pt slot** (43 against 9). Not the
stem weight — swept to 1 u a side, still 32 — so it is the trap geometry or the
cu2qu conversion at 14.8 px/em. Left as a known limitation: bold italic is
emphasis inside emphasis at the smallest slot.

Also from that review, fixed here: the status line said "ninth entry" (it is the
twelfth) and claimed a 3x seed tier that was never built. Two comments elsewhere
now undercount the local-source families — `.github/workflows/release-fonts.yml:15`
and `src/network/FontUpdater.cpp:32` — not load-bearing, not touched.

**Coverage, disclosed here and now in the recipe**: the cut carries 395
codepoints per style against stock's 1053. Latin-1, Latin Ext-A/B and General
Punctuation are complete; Greek (54), Latin Extended Additional (133), combining
marks (21), math operators (14) and the `ff`-`ffl` ligatures (5) are NOT cut and
fall to the `reading` fallback chain, as they do for any family that lacks them.
The shipped `.cpfont` still carries 2676 glyphs, identical to TeXGyreHeros.
