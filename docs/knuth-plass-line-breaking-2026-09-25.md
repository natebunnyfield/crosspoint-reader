# Knuth-Plass total fit, measured against the shipped breaker

Written 2026-09-25. It is experiment **E3** of the layout research plan
(`crosspoint-simulator/docs/research-claude-for-kerning-and-layout-2026-09-24.md`,
§3f and §4 Stage 2). It builds the cell of `docs/line-breaking-2026-08-25.md`'s
2×2 that no stored byte can reach, total fit WITH hyphen points, and measures it
against the shipped default on the owner's own books.

**Nothing on the device changed.** The breaker is host-only: it lives in the
test harness and was not wired into `ParsedText` or the renderer.

* Surveyed at `9b78f5d5c`, plus the harness files named below.
* Every number is **measured on the host** unless it is marked as an estimate.
* The ESP32-C3 figures in §6 are **estimates**. No device timing exists.

**Verdict in one paragraph.** On a **justified** page, Knuth-Plass beats the
shipped greedy breaker where a reader notices: on the worst lines. It removes
the catastrophic lines, 22–59 word spaces wide, in all six configurations, and
it removes hyphen ladders. With the tuned parameters it does this mostly while
setting FEWER hyphens. The typical line barely moves. A version that may also
narrow gaps (shrink) is much better again on every metric, rivers included, but
it needs an `extractLine` change as well as a new breaker. On a **ragged** page,
Knuth-Plass adds nothing over the Whole Words DP that already ships. That
matters because **at the default settings the X3 page is ragged**. Both 14 pt
faces measure 36–38 characters at 512 px, under the Justified Text threshold of
40 (`AutoJustify.h:120`). So the break-even question is how often the owner
reads justified text. That, and whether he prefers the result by eye, are the
two things this document cannot settle. §7 lays out the options.

---

## 1. What was built

| File | What it is |
|---|---|
| `test/line_break_quality/KnuthPlass.h` | The breaker. Pure, integer-width inputs, no mutation of anything. |
| `test/line_break_quality/KnuthPlassSweep.cpp` | A second executable, `LineBreakKnuthPlassTest`. It holds two live tests plus three `DISABLED_` instruments: `Sweep`, `HyphenPenaltySweep` and `Render`. |
| `test/line_break_quality/CMakeLists.txt` | The new target. It needs the SD font stack, because Albo is a `.cpfont` and not a built-in face. |
| `tools/calendar_preview/HalDisplay.h` | A two-line stub fix that every host suite needed (§8). |
| `docs/data/knuth-plass-2026-09-25/` | The raw sweep output (`sweep.txt`), the hyphen grid (`hyphen-grid.txt`) and four proof PNGs. |

**Why it is a second executable.** Loading Albo needs the directory-capable
`HalStorage` stub from `test/sd_kern_measure/stubs`. Swapping that stub in under
`LineBreakQualityTest` would change the environment its pinned numbers were
measured in.

### 1a. The model: boxes, glue, penalties

A paragraph is its words: one token per word, as `ParsedText::addWord` makes for
space-separated Latin text (`ParsedText.cpp:434`). Each word is composed to NFC
first, as `addWord` does.

A **position** is a place where a line may begin:

* the paragraph start;
* the space after a word, except before a spaced em or en dash
  (`startsWithLineForbiddenDash`, `ParsedText.cpp:395`);
* every hyphenation point from `Hyphenator::breakOffsets(word, false)`,
  `Hyphenator.h`, with the fallback breaks added only for a word wider than the
  measure;
* the paragraph end.

The Knuth-Plass pieces map onto that like this:

* **Glue.** Each word space is glue. Its natural width is
  `getSpaceAdvance(prevLastCp, nextFirstCp)`, the kerned space
  `computeHyphenatedLineBreaks` uses. Its stretch is ½ a word space and its
  shrink is ⅓ of one, TeX's Computer Modern ratios.
* **Penalties.** A hyphenation point is a *flagged* penalty. Its hyphen width
  exists only if the break is taken.
* **\parfillskip.** The last line has badness 0.

**Line widths are measured, not summed.** Widths are not additive across a
split: the kern at the split point is lost and the hyphen is gained. So a line's
width is built from pieces, each measured with the firmware's own
`GfxRenderer::getTextAdvanceX`:

* the suffix the line starts with;
* the full words;
* the prefix-plus-`-` the line ends with.

That is exactly what `measureWordWidth` does (`ParsedText.cpp:269`). The first
line is shortened by the 3-space indent from `resolveFirstLineIndent`
(`ParsedText.cpp:701`).

**Scoring.**

* **Badness** is `100·|r|³`, with `r` the adjustment ratio. It is **uncapped**,
  and the reason is in §4.
* **Demerits** are `(linepenalty + b)² + p²`. On top of that come:
  * a double-hyphen demerit;
  * a final-hyphen demerit;
  * an adjacent-fitness demerit, using TeX's four classes with boundaries at
    `r` = −0.5, 0.5 and 1.
* **Tolerance and looseness.** There is no tolerance, so every line that fits
  is feasible. Looseness is 0.
* **Ragged mode** is TeX's `\raggedright`: rigid word spaces, and a line-end
  glue that stretches by 6 word spaces (about 2 em).

**The implementation** is the array form of the algorithm, a best-demerits table
per `(position, fitness, hyphen-run)`. Each line end looks back until the first
start that overflows. With no tolerance, this finds the same optimum as the
active-list form, and it is the shape a device port would take: fixed states per
position, no allocation per node.

**The optional ladder limit** is a HARD cap of two hyphenated lines in a row. It
is implemented as a third state dimension, so it triples the table.

### 1b. How the widths are proved exact, not assumed

All arms reduce to one thing: a list of cuts `(word, byte offset, hyphen)`. That
covers the two real `ParsedText` breakers and the new one. One geometry model
turns cuts into lines the way `ParsedText::extractLine` does
(`ParsedText.cpp:1356`–`1700`):

* measured pieces;
* kerned natural gaps;
* the indent;
* both hanging-punctuation edges, whose table is copied from
  `ParsedText.cpp:326` and whose left budget is from `:1492`;
* stretch-only justification, `spare / gaps` with the integer remainder dropped
  (`computeJustifyExtra`, `:248`).

For the two real arms, the cuts are read back off the `TextBlock`s the paginator
would bake. The model's x position for every word is then compared with the
`TextBlock`'s own.

**Result: 0 differing lines out of 517,871** produced by the two real breakers
across all twelve face/size/alignment runs. `KnuthPlass.ModelReproducesTheShippedBreakersExactly`
pins the same property on the built-in paragraphs, in both alignments and both
modes, and it asserts that hyphenated lines are present, so the test is not
vacuous. A Knuth-Plass line is therefore laid out by a model that agrees with
the firmware to the pixel on every line the firmware itself produced.

`KnuthPlass.EveryWordIsSetOnceAndNoLineOverflows` checks two more things: that
the new breaker's lines rebuild the source text exactly, and that no non-final
line exceeds the measure.

---

## 2. Corpus, faces, measure

**Corpus.** The owner's books, canonical per subproject in `~/src/claude-tools`:

* `ai/ai-engineering-from-zero`
* `atlas/eighth-atlas`
* `poly/Polyamory-The-Whole-Thing`
* `trivia/trivia-aimed-v2`
* `wbn/WBN-Book-One-Complete`
* `wbn/WBN-Solari`

All six are `dc:language` en. The *Essentials* cuts were skipped as subsets, and
the Spanish book because only its own trie would be fair to it. They were
extracted with `tools/linebreak_corpus.py` (every `<p>` of 30+ words). That gave
**1,472 paragraphs and 117,045 words**. The book text is not checked in; rebuild
it with the tool:

```
python3 tools/linebreak_corpus.py ~/src/claude-tools/{ai/epub/ai-engineering-from-zero,atlas/epub/eighth-atlas,poly/epub/Polyamory-The-Whole-Thing,trivia/epub/trivia-aimed-v2,wbn/epub/WBN-Book-One-Complete,wbn/epub/WBN-Solari}.epub /tmp/corpus.txt
```

**Faces.**

* **Libre Franklin 14 pt.** The shipped default: `fontFamily =
  BUILTIN_LIBRE_FRANKLIN`, `DEFAULT_FONT_POINT_SIZE = 14`,
  `CrossPointSettings.h:98`, `:362`. It is the built-in face.
* **Albo 14 pt.** The owner's face, from `fs_/fonts/Albo/Albo_14.cpfont`.
* Both faces were also measured at 12 pt and 18 pt, the size range of the
  2026-08-26 tables.

**Albo was rebuilt by concurrent work mid-session (21:09).** Its line counts
moved by about 0.1% between two runs. The tables below therefore use a snapshot
with these hashes:

| File | md5 |
|---|---|
| `Albo_12` | `c050264f…` |
| `Albo_14` | `c729f85b…` |
| `Albo_18` | `cc2168a1…` |

Pointing `CROSSPOINT_TEST_SD` at a copy is how to hold a face still.

**Measure.** 512 px, the X3 portrait measure at screen margin 5. This is the
same figure as `LineBreakQualityTest` and `docs/auto-justification.md`.

**Alignment.** Both alignments were forced (justify threshold 0), so each face
is read both ways. At the **default** threshold of 40, these pairs justify:

| Face | Chars per line | Default |
|---|---:|---|
| LF 12 | 41.6 | justified |
| Albo 12 | 44.0 | justified |
| LF 14 | 35.9 | **ragged** |
| Albo 14 | 37.8 | **ragged** |
| LF 18 | 28.0 | ragged |
| Albo 18 | 29.4 | ragged |

**Arms.**

| Arm | What it is |
|---|---|
| greedy+hy | The shipped default, `STORED_HYPHENATED`, `computeHyphenatedLineBreaks`. |
| totalfit whole words | The shipped alternative, `STORED_WHOLE_WORDS`, `computeLineBreaks`. |
| KP TeX defaults | Hyphen penalty 50, double-hyphen 10,000, no ladder cap. |
| **KP candidate** | Hyphen penalty 10,000, double-hyphen 10⁶, at most 2 hyphens in a row. |
| KP candidate+shrink | The candidate, plus a model of an `extractLine` that may also narrow gaps. |

The candidate was chosen by the grid in §4. The shrink arm's model allows gaps
down to ⅔ of a space; the firmware cannot paint that today.

**Units.** All gap figures are in multiples of the face's own word space
(`getSpaceAdvance('n','n')`).

| Column | Meaning |
|---|---|
| `pWorst` | The mean over paragraphs of that paragraph's loosest line. |
| `hyph` | Lines ending in a hyphen. |
| `r2` | Runs of exactly two hyphenated lines. |
| `lad` | Ladders: three or more hyphenated lines in a row. |
| rivers | Chains of three or more lines whose gaps overlap by at least half a space, per 1,000 gaps. The definition is `LineBreakQualityTest::riversByOverlap`. |
| `1word` | Non-final lines with no gap that stop short of the measure. Justification cannot stretch them. |

The gap statistics cannot see one-word lines, so `1word` is counted separately
to keep a breaker from hiding a bad line in one.

---

## 3. Results

### 3a. Justified pages

| config | arm | lines | mean | p95 | p99 | max | **pWorst** | hyph | r2 | lad | rivers/kg | 1word |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| LF 14 | greedy+hy (shipped) | 20431 | 2.37 | 4.67 | 7.17 | 59.33 | 4.94 | 3328 | 408 | 74 | 41.75 | 2 |
| | KP TeX defaults | 20469 | 2.27 | 3.83 | 5.00 | 12.83 | 3.80 | 4246 | 577 | 172 | 40.43 | 5 |
| | **KP candidate** | 20484 | 2.32 | 3.83 | 5.00 | 12.83 | **3.85** | 3259 | 445 | **0** | 40.95 | 5 |
| | KP candidate+shrink | 20094 | 1.96 | 3.50 | 4.67 | 12.83 | 3.43 | 2630 | 321 | 0 | 29.74 | 4 |
| Albo 14 | greedy+hy (shipped) | 19973 | 1.86 | 3.33 | 4.78 | 28.11 | 3.42 | 3164 | 366 | 71 | 46.53 | 2 |
| | KP TeX defaults | 20017 | 1.80 | 2.78 | 3.56 | 12.78 | 2.77 | 4083 | 546 | 155 | 45.92 | 3 |
| | **KP candidate** | 20111 | 1.94 | 3.00 | 3.67 | 12.78 | **2.99** | 1495 | 122 | **0** | 50.16 | 3 |
| | KP candidate+shrink | 19600 | 1.64 | 2.78 | 3.33 | 12.78 | 2.67 | 867 | 50 | 0 | 34.68 | 1 |
| LF 12 | greedy+hy (shipped) | 17587 | 2.12 | 4.00 | 5.80 | 37.00 | 3.95 | 2758 | 301 | 60 | 28.60 | 2 |
| | **KP candidate** | 17629 | 2.12 | 3.40 | 4.40 | 11.80 | **3.33** | 2273 | 257 | **0** | 29.55 | 3 |
| | KP candidate+shrink | 17345 | 1.82 | 3.20 | 3.80 | 11.80 | 2.98 | 1700 | 160 | 0 | 20.93 | 3 |
| Albo 12 | greedy+hy (shipped) | 17100 | 1.66 | 2.75 | 3.75 | 22.00 | 2.72 | 2571 | 283 | 49 | 42.07 | 1 |
| | **KP candidate** | 17276 | 1.80 | 2.75 | 3.12 | 11.62 | **2.63** | 632 | 26 | **0** | 48.11 | 2 |
| | KP candidate+shrink | 16809 | 1.48 | 2.50 | 3.00 | 8.00 | 2.31 | 306 | 10 | 0 | 32.28 | 2 |
| LF 18 | greedy+hy (shipped) | 26588 | 2.97 | 6.50 | 10.75 | 51.88 | 7.70 | 4276 | 519 | 97 | 60.94 | 16 |
| | **KP candidate** | 26687 | 2.81 | 5.00 | 6.62 | 11.88 | **5.21** | 5141 | 863 | **1** | 59.91 | 29 |
| | KP candidate+shrink | 26155 | 2.42 | 4.50 | 6.25 | 11.50 | 4.68 | 4849 | 791 | 0 | 46.26 | 21 |
| Albo 18 | greedy+hy (shipped) | 25697 | 2.32 | 4.55 | 7.36 | 32.73 | 5.39 | 3952 | 463 | 83 | 63.93 | 15 |
| | **KP candidate** | 25770 | 2.25 | 3.73 | 4.91 | 11.73 | **3.90** | 3858 | 510 | **0** | 63.40 | 9 |
| | KP candidate+shrink | 25098 | 1.90 | 3.27 | 4.27 | 11.73 | 3.42 | 2949 | 349 | 0 | 45.86 | 8 |

The whole-words DP rows, the TeX-default rows for every configuration, and the
deviation histogram (`|g/s − 1|` in five buckets) are in `sweep.txt`.

**Reading it: the candidate against the shipped default.**

**Worst line (`pWorst`) is better in 6/6.**

| Config | Change |
|---|---:|
| LF 14 | −22% |
| Albo 14 | −13% |
| LF 12 | −16% |
| Albo 12 | −3% |
| LF 18 | −32% |
| Albo 18 | −28% |

**p95** is better in 5/6 (−10% to −23%) and tied in Albo 12.

**The single loosest line falls from 22–59 spaces to 11.6–12.9 in every
configuration.** Those greedy outliers are lines like "pricing to external-" or
"stores sequences, not", where one long token forced three words across a whole
line.

**Ladders go from 49–97 to 0 in 5/6, and to 1 in LF 18.** The LF 18 ladder was
not traced. The cap binds only on flagged breaks, while the counter counts any
line ending in `-`, including a whole word that already ends in one; that is the
likely source, but it is inferred, not checked.

**Hyphenated lines** fall in 5/6:

| Config | Change |
|---|---:|
| LF 14 | −2% |
| Albo 14 | −53% |
| LF 12 | −18% |
| Albo 12 | −75% |
| Albo 18 | −2% |

**LF 18 is the exception:** +20% hyphenated lines, and pairs up 519 → 863.

**Pairs (`r2`)** rise in 3/6: LF 14, LF 18 and Albo 18.

**The mean line barely moves.** It goes from −5% to +8%, and it is worse in both
Albo sizes, where the candidate sets far fewer hyphens and pays for it in
average spacing.

**Rivers are flat for Libre Franklin and a little worse for Albo 12 and 14**
(+8% and +14%). That is the same trade: fewer hyphens make the average line
looser.

**Line count rises 0.2–1.0%.** More lines means more pages.

**One-word lines** stay at 1–29 in any arm, against 17–27k lines per run. The
candidate has more of them than greedy in 5/6 (+1 to +3, and +13 in LF 18) and
fewer in Albo 18 (15 → 9). `docs/data/knuth-plass-2026-09-25/kp_LibreFranklin_14_J_678.png`
shows the failure mode: the new breaker sets "owner-knowledge:" alone, rather
than stretching one gap 13 spaces wide.

**Per paragraph** (paragraphs of ≤ 11 lines), the candidate's worst line against
greedy's:

| Config | Better | Worse | Paragraphs |
|---|---:|---:|---:|
| LF 14 | 320 | 42 | 857 |
| Albo 14 | 236 | 186 | 880 |
| LF 12 | 281 | 85 | 995 |
| Albo 12 | 241 | 331 | 1014 |
| LF 18 | 270 | 12 | 604 |
| Albo 18 | 249 | 34 | 636 |

**The median paragraph's WORST LINE is the same under both breakers in every
configuration**, so the win is a tail effect. (Corrected 2026-09-25, same day:
an earlier wording said the median paragraph was *identical*. It is not. At 14 pt
justified, the two breakers put at least one break in a different place in 1,132
of 1,472 Albo paragraphs and 1,047 of 1,472 Libre Franklin ones, per
`DISABLED_BlindStats`. What stays the same is the loosest line.)

**Against the research plan's E3 bar.** The bar was "paraWorst 5–20% better,
holding in ≥ 5 of 6 configurations, and hyphen runs must not rise."

* **The candidate** clears the 5% bar in 5/6; Albo 12 is 3%. It removes ladders
  in 6/6 and lowers hyphen count in 5/6, but it raises pairs in 3/6. **Partial
  pass**: the runs criterion holds for ladders and fails for pairs.
* **Shrink**:
  * worst line 15–39% better in 6/6;
  * p95 better in 6/6;
  * rivers down 23–29% in 6/6;
  * 1.4–2.3% fewer lines;
  * ladders 0;
  * hyphens lower in 5/6 (LF 18 is +13%);
  * pairs lower in 5/6 (LF 18 is +52%).

  **Pass, except LF 18.**

### 3b. Ragged pages, which include the default 14 pt page

| config | arm | lines | slack mean | slack sd | p95 | pWorst | hyph | r2 | lad |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| LF 14 | greedy+hy (shipped) | 20821 | 8.25 | 6.01 | 20.00 | 18.75 | 384 | 6 | 1 |
| | totalfit whole words | 20913 | 8.70 | 5.33 | 18.17 | 17.68 | 5 | 0 | 0 |
| | KP TeX defaults | 20438 | 6.07 | 3.59 | 12.17 | 11.76 | 4047 | 506 | 137 |
| | **KP candidate** | 20920 | 8.70 | 5.11 | 17.83 | 16.91 | 57 | 1 | 0 |
| Albo 14 | greedy+hy (shipped) | 20323 | 5.35 | 3.90 | 13.00 | 12.09 | 271 | 7 | 1 |
| | totalfit whole words | 20386 | 5.58 | 3.43 | 11.56 | 11.23 | 4 | 0 | 0 |
| | KP TeX defaults | 19978 | 3.93 | 2.35 | 8.00 | 7.66 | 3741 | 432 | 110 |
| | **KP candidate** | 20401 | 5.64 | 3.40 | 11.56 | 11.04 | 8 | 0 | 0 |

(Slack is in word spaces; the other four configurations are in `sweep.txt` and
read the same way.)

With the candidate's hyphen penalty, ragged Knuth-Plass sets almost no hyphens.
It lands within 5% of the **already shipped** Whole Words DP on every rag
metric, in all six configurations, and slightly ahead of it. Both of those beat
the shipped greedy on the rag at 14 pt, with sd 11–15% lower. They do it with
85–99% fewer hyphens, because greedy's 70% ragged gate (`LineBreakMode.h`, doc
§9) still hyphenates 270–380 lines at 14 pt.

With TeX's own hyphen penalty, ragged Knuth-Plass sets a far better rag, but it
hyphenates 10–30× as many lines as greedy. The 2026-09-11 Automatic ruling (doc
§10: no hyphens on a ragged block) rules that out.

**So for the default 14 pt page there is no new breaker to build.** The ragged
win is already reachable today:

* **Whole Words** gives it.
* **Automatic** gives it too, because it resolves every ragged block to whole
  words (`linebreak::resolvedMode`).

---

## 4. Negative results, recorded so they are not re-run

* **TeX exactly as TeX ships it does worse than greedy on the worst line.**
  "Exactly" means badness capped at 10,000, hyphen penalty 50 and double-hyphen
  demerits 10,000.

  | Config | Arm | pWorst | p99 |
  |---|---|---:|---:|
  | LF 14 | greedy | 4.94 | — |
  | LF 14 | TeX capped | 17.68 | 53.33 |
  | Albo 14 | greedy | 3.42 | — |
  | Albo 14 | TeX capped | 4.50 | — |

  The cap makes every very loose line cost the same, so the optimizer stops
  distinguishing between a bad line and a terrible one, which is the exact
  distinction the worst-line metric exists for. It also sets more hyphens and
  ladders (4,536 hyphens and 181 ladders in LF 14). Uncapped badness is what
  makes Knuth-Plass win here; `hyphen-grid.txt` has the rows.
* **The hyphen penalty and the double-hyphen demerits barely move anything at
  TeX's scale.** Uncapped, a loose line at `r ≈ 2` costs about 6.5×10⁵
  demerits, so a penalty of 50 (2,500 demerits) is noise.

  | Hyphen penalty | Albo 14 hyphens | LF 14 hyphens |
  |---:|---:|---:|
  | 50 | 4,083 | 4,246 |
  | 1,000 | 3,900 | 4,228 |
  | 10,000 | 1,495 | 3,259 |

  At hyphen penalty 50, raising the double-hyphen demerits to 10⁶ removed only
2–16% of ladders.
* **The ladder limit had to be a hard constraint.** A third state dimension
  (§1a) is what took ladders to 0 in every configuration. It costs a 3× table.
* **The penalty that matches greedy's hyphen count is face-dependent.** 3,000
  suits Albo 14 (2,966 hyphens) and leaves LF 14 at +17%. 10,000 matches LF 14
  and halves Albo's hyphens. A 12 pt face and an 18 pt face want different
  values again: LF 18 still rises 20% at 10,000. One fixed constant is a
  compromise.

---

## 5. Proofs

Each image shows the same paragraph under the shipped breaker (top) and the
candidate (bottom). They are drawn by the **firmware's own renderer**:
`TextBlock::render` over `GfxRenderer`, with three passes (BW base and both AA
planes) composed to the four preview levels `tools/calendar_preview` uses. They
are portrait, native pixels, 528 px wide, lossless PNG, 1:1.

**Nothing was scaled. These are the X3's own 1x pixels.**

The paragraph index is into the corpus built above.

| File | What it shows |
|---|---|
| `docs/data/knuth-plass-2026-09-25/kp_Albo_14_J_443.png` | Albo 14, justified. Greedy's line 5 is "standardization intended as" stretched across the measure. Knuth-Plass redistributes the paragraph and has no such line. |
| `docs/data/knuth-plass-2026-09-25/kp_Albo_14_J_645.png` | Albo 14, justified. Greedy's "stores sequences, not" becomes an even paragraph with one hyphen ("understand-ing"). |
| `docs/data/knuth-plass-2026-09-25/kp_Albo_14_R_443.png` | Albo 14, ragged. The rag evens out; no hyphens in either arm. |
| `docs/data/knuth-plass-2026-09-25/kp_LibreFranklin_14_J_678.png` | LF 14, justified. **The failure mode, included on purpose**: Knuth-Plass sets "owner-knowledge:" alone on a line, where greedy set it with a 13-space gap. Both are bad; they are bad differently. |

These are the largest-gain paragraphs, chosen to make the mechanism visible.
**They are not typical.** The median paragraph's worst line is
the same under both breakers (§3a). §12 is the unbiased sample.

Regenerate them with:

```
CROSSPOINT_TEST_SD=<sd copy> CROSSPOINT_LINEBREAK_CORPUS=/tmp/corpus.txt CROSSPOINT_KP_OUT=/tmp \
CROSSPOINT_KP_RENDER="Albo:14:J:443,Albo:14:J:645,Albo:14:R:443,LibreFranklin:14:J:678" \
  build/line_break_quality/LineBreakKnuthPlassTest --gtest_also_run_disabled_tests --gtest_filter='*Render*'
```

The output is PGM; convert it losslessly to PNG.

---

## 6. Cost

### 6a. Host, measured

Measured on an Apple M4, Release build, over the full corpus, one pass per
configuration. The table gives means per paragraph; `sweep.txt` has every
configuration.

| | LF 14 J | Albo 14 J | range, all 12 runs |
|---|---:|---:|---:|
| greedy `layoutAndExtractLines`, whole call (measure + break + extract) | 34.7 µs | 28.3 µs | 20.7–36.6 µs |
| Knuth-Plass setup: word widths, gaps and the candidate pieces | 39.9 µs | 29.9 µs | 29.9–40.0 µs |
| Knuth-Plass DP, candidate (12 states per position) | 28.1 µs | 29.5 µs | 22.5–35.3 µs |
| Knuth-Plass DP, worst paragraph | 185 µs | 197 µs | ≤ 278 µs |
| line evaluations per paragraph | 883 | 900 | 707–1031 |
| positions per paragraph, mean / max | 104 / 719 | 104 / 719 | |
| extra glyph-advance measurements per paragraph | 50.7 | 50.7 | |

The extra measurements are two per hyphenation candidate, a prefix-with-hyphen
and a suffix.

**Read it as roughly 2× greedy on the host.** The setup includes host-only
`std::map` caching and the measurements greedy also does. Line extraction is not
included in the Knuth-Plass figures, and it would be the same code.

An earlier run on the same corpus, taken while the machine was busy with another
agent's font builds, read 1.5–2× higher across every arm. That is why the table
is from a quiet run.

### 6b. ESP32-C3: ESTIMATES, nothing here was measured on a device

**The chip.** The X3 is an ESP32-C3 (`platformio.ini:76`,
`board = esp32-c3-devkitm-1`):

* a single RV32IMC core at 160 MHz;
* **no FPU**;
* 400 KB SRAM, about 380 KB usable with no PSRAM (`docs/ble-editor-spike.md`,
  `docs/fork-sync.md:252`).

**Time.**

* **The assumption.** Clock is 27× lower than the M4 and IPC is perhaps 2–4×
  lower, so assume the C3 is 50–100× slower per operation on this loop.
* **Per paragraph.** The DP would take roughly **1–3.5 ms**, ported with integer
  demerits (int64 accumulators; the uncapped cube overflows int32 at `r` ≈ 4.6).
* **With doubles.** Keeping doubles means soft-float, several times worse. Do
  not port the host code as-is.
* **Per page.** At three to five paragraphs a page, that is roughly **3–15 ms**
  added to every page the paginator lays out.
* **Scale.** For comparison, the Whole Words DP measured +0.27 ms per page on
  the simulator (`line-breaking-2026-08-25.md` §4). This would be an order of
  magnitude more.
* **When the user pays it.** Pagination runs lazily, a screenful ahead, so the
  cost lands on chapter opens and background pagination rather than on every
  turn. That too is inferred from §4 of that doc, not re-measured.

**Memory.** The working set is transient, one paragraph at a time. In a device
layout:

| Item | Per position |
|---|---:|
| Demerits (int32) + back-pointer (uint16), × 12 states | 72 B |
| Candidate widths + position | ~8 B |
| **Total** | **~80 B** |

* **Mean paragraph:** about 8 KB.
* **Largest paragraph in the corpus:** 719 positions, about 58 KB.
* **Without the ladder dimension:** 32 B per position, a 23 KB worst case. But
  ladders then return (§4).
* **On a fragmented heap,** a worst case of 58 KB is the real risk. A port
  should cap the DP window: commit lines once a paragraph passes N positions.
  That is the standard remedy, and it gives up global optimality only on
  paragraphs longer than about 250 words.

---

## 7. Is it worth putting on the device? Options, not a decision

This is an architectural choice with more than one defensible answer, so it goes
to the owner (global rule, 2026-08-23). Costs are listed as measured or
estimated above.

| Option | What changes on the device | What it buys (measured, host) | What it costs |
|---|---|---|---|
| **A. Leave it** | nothing | nothing | nothing. A ragged default page already gets the whole-words rag through Whole Words or Automatic (§3b). |
| **B. Knuth-Plass replaces greedy on JUSTIFIED blocks, stretch-only** | A new breaker in `ParsedText` that consumes `breakOffsets` without mutating anything, then splits only the chosen words. `extractLine` is unchanged. There is a `SECTION_FILE_VERSION` bump, so every book re-paginates. | Worst line −3% to −32%. Catastrophic lines gone. Ladders gone. Hyphens down in 5/6. | ~300–400 lines plus tests. ~3–15 ms per paginated page (**estimate**). Up to 58 KB transient per long paragraph unless the window is capped. LF 18 sets +20% hyphens. Mean spacing +4–8% on Albo. |
| **C. B, plus shrink** | Also: `computeJustifyExtra` accepts a negative spare, and gaps may narrow to ⅔ of a space. | Worst line −15% to −39%. Rivers −23% to −29%. 1.4–2.3% fewer lines (pages). Hyphens down in 5/6. | B's costs, plus a visibly **tighter** page. That is a look, not only a number, so it needs his eye first. |

**Recommendation.**

* **Not A.** It rests on the ragged result, which is no reason to stop.
* **Not B or C yet.** Neither should be built until **E4** has run: the blind
  2AFC of about 40 paragraph pairs, in two sessions of five minutes or less
  (research plan §4 Stage 2).
* **E4 is warranted.** E3's bar is met in part for B and nearly in full for C
  (§3a). `DISABLED_Render` already produces the pairs.
* **Present B and C as separate arms,** because they differ in look and not
  only in cost.
* **The question for him** is whether he reads justified text. If he does not
  (at 14 pt on the X3 the default page is ragged), then A is the honest answer,
  and this document is where that was found.

---

## 8. Found and fixed in passing: every host suite on the calendar-preview stub had stopped compiling

`3eccd843b` (2026-09-14, "wire windowed refresh in, via a dirty band") made
`GfxRenderer.cpp` call `display.supportsWindowedRefresh()` and
`display.displayWindow(...)` (`GfxRenderer.cpp:2339`, `:2347`).
`tools/calendar_preview/HalDisplay.h`, the host stub most suites include, had
neither. From that commit on, `LineBreakQualityTest` and `SdKernMeasureTest`
(both checked) failed to compile, along with presumably every suite that links
`GfxRenderer.cpp` against that stub.

The fix adds the two methods: a no-op, and `false`, meaning there is no windowed
path. That was the smallest change available and it is host-only.

After the fix:

* `LineBreakQualityTest` builds and passes 14/14.
* `SdKernMeasureTest` builds.

The rest of the host tree was not rebuilt here.

---

## 9. What was checked and found clean

* **The geometry model against the firmware:** 0 differing lines out of 517,871
  (§1b). The copied hanging-punctuation table is verified by that same
  comparison, not trusted.
* **Tokenization.** With focus reading off, `addWord` makes one token per
  space-separated word for Latin text (`ParsedText.cpp:434`–`560`). The corpus
  has no soft hyphens (checked: 0 occurrences of U+00AD).
* **Hyphenation is live.** The trie is installed by `setPreferredLanguage("en")`
  in the harness constructor; this was the 2026-08-25 trap. The greedy arm sets
  2,500–4,300 hyphenated lines per run, so the axis is live.
* **Every word is set exactly once, and no line overflows the measure.** Pinned
  by the live test.
* **Nothing reaches the device.** `KnuthPlass.h` is included only by the new test
  target: grep finds it in `test/line_break_quality/` alone. `ParsedText.cpp`,
  `platformio.ini` and `fs_/` are untouched; `fs_` was read and copied, never
  written.

## 10. Deliberately not done

* **No device timing.** There is no paired device here. The §6b figures are
  estimates and say so.
* **No owner-eye test.** That is E4, and it is the next step.
* **No sweep of the stretch and shrink ratios.** They were fixed at TeX's ½ and
  ⅓. The ragged glue was not swept either (6 spaces), and the fitness-class
  boundaries were left at TeX's.
* **No per-measure hyphen penalty.** §4 shows one would help LF 18.
* **Automatic mode (stored byte 2) was not run as its own arm.** On ragged
  blocks it is the Whole Words row. On justified blocks in its 40–50-character
  band it is greedy+hy, which covers LF 12 and Albo 12.
* **No phone measure (2x).** Only the X3's 512 px was measured.

## 11. Reproduce

```
cd build && cmake . && make LineBreakKnuthPlassTest
cd .. && CROSSPOINT_TEST_SD=<fs_ or a snapshot of it> CROSSPOINT_LINEBREAK_CORPUS=/tmp/corpus.txt \
  build/line_break_quality/LineBreakKnuthPlassTest --gtest_also_run_disabled_tests --gtest_filter='KnuthPlass.DISABLED_Sweep'
# the hyphen grid: CROSSPOINT_KP_FACES=Albo:14 [CROSSPOINT_KP_MODE=R] ... --gtest_filter='*HyphenPenaltySweep*'
```

---

## 12. The blind side-by-side (owner ruling 2026-09-25: "Blind side-by-side first")

This is §7's E4, built. **No answers exist yet.** This section records how the
test is built, so that it can be scored and read without re-deriving anything.

### 12a. Where everything is

| What | Where |
|---|---|
| **The page** | `docs/data/knuth-plass-2026-09-25/blind/index.html`. Self-contained: inline CSS and JS, no network. |
| **The images** | `docs/data/knuth-plass-2026-09-25/blind/pNN_A.png` and `pNN_B.png`, NN = 01–40. The page references them relatively. |
| **The key** | `docs/data/knuth-plass-2026-09-25/blind-key.json` |
| **The builder** | `tools/knuth_plass_blind.py` |
| **The scorer** | `tools/knuth_plass_blind_score.py` |
| **The instruments** | `KnuthPlass.DISABLED_BlindStats` (one CSV row per paragraph) and `KnuthPlass.DISABLED_BlindRender` (unlabeled PGMs, one per arm). |

### 12b. What each pair is

* **One paragraph, set two ways, in JUSTIFIED mode.** The top arm is the shipped
  greedy breaker (`STORED_HYPHENATED`). The other is the §2 **candidate**:
  stretch-only, hyphen penalty 10,000, at most 2 hyphens in a row. Shrink was
  not tested here.
* **Justified is forced.** At the default threshold, Albo 14 at 512 px would be
  ragged (§2). The owner ruled justified mode for this test.
* **Faces.** Albo 14 pt (34 pairs) and Libre Franklin 14 pt (6 pairs). Albo is
  the same `.cpfont` snapshot as §2.
* **Measure.** 512 px, the X3 portrait measure.
* **Rendering.** Drawn by `TextBlock::render` over `GfxRenderer`, the BW base
  plus both AA planes, composed to four levels. Portrait, 528 px wide, device
  pixels, lossless PNG, **unlabeled**.
* **Equal sizes.** Both arms of a pair are padded to the same height, the taller
  arm's line count, so the image size cannot say which breaker set fewer lines.
  The builder refuses to write a pair whose two images differ in size.
* **Display.** The page shows every image at 1:1 and never scales it. On a
  screen narrower than 528 CSS px, each image scrolls sideways inside its own
  box.

### 12c. How the 40 paragraphs were chosen

* **Pool.** Paragraphs where the two arms' cut lists differ, and where both arms
  set 3–12 lines (12 fits a phone screen at 1:1).
* **Pool size.** 625 of 1,472 paragraphs for Albo; 549 for Libre Franklin.
* **Stratified on d = greedy worst line − Knuth-Plass worst line**, in word
  spaces, so the test is not only easy wins:

| Stratum | d | Albo | LF |
|---|---|---:|---:|
| `kp_worse` | < −0.1 | 9 | 2 |
| `near_tie` | −0.1 … 0.1 | 8 | 1 |
| `kp_better` | 0.1 … 1 | 9 | 2 |
| `kp_much_better` | > 1 | 8 | 1 |

* **Deliberately not proportional to the corpus.** Here a quarter of the pairs
  are ones Knuth-Plass loses on the worst-line metric, which the corpus does not
  have. The per-stratum rows of the score are the honest reading; the overall
  rate answers "over this designed mix".

### 12d. Randomization

One `random.Random(20260925)` draws, in this order:

1. the paragraphs within each stratum;
2. the page order of the 40 pairs;
3. each pair's sides.

Greedy landed on A in 15 of 40. Re-running the builder on the same stats CSVs
reproduces the same pages byte for byte in content.

### 12e. How the key is kept hidden

* **It sits outside `blind/`.** `blind-key.json` is in the parent folder, so
  publishing the `blind/` folder, which is everything the page needs, cannot
  carry it.
* **The page never names it or fetches anything.** Its only external references
  are the 80 `pNN_A.png` / `pNN_B.png` files. It loops `p01`…`p40` in its own
  script.
* **The PNG filenames carry the pair and side only.** The arm name exists only in
  the temporary PGM names, which are deleted with the temp directory.
* **The key holds, per pair,** which arm is A and which is B, the face, the
  corpus index, the stratum, and both arms' worst line, hyphen count and line
  count. It lives in the repo, so it is hidden from the page and not from
  anyone reading the repo. The owner is the only subject, so that is enough.

### 12f. Answers and scoring

* **Answers.** Each pair takes A, B or No difference. They are stored in
  `localStorage` under `kp-blind-2026-09-25`. "Copy answers" writes
  `{"test": …, "answers": {"p01": "A", …}}` to the clipboard and to the text box
  at the foot of the page.
* **Score them with:**

  ```
  python3 tools/knuth_plass_blind_score.py answers.json
  ```

* **What the scorer reports,** overall, per face and per stratum:
  * the Knuth-Plass preference rate among decisive answers;
  * an exact Clopper-Pearson 95% interval;
  * a two-sided exact binomial p against 50%;
  * the No difference count, kept separate.
* **Checked on a synthetic answer file:** 30 of 38 decisive gives 78.9%, with
  CI [62.7%, 90.4%] and p < 0.001.
* **Power.** With 40 pairs, a preference must be about 70% or more over
  decisive answers before the interval clears 50%. A subtler preference needs
  a second session of fresh pairs, which the builder can make by changing
  `SEED`.

**Results, 2026-09-26.** Five pairs answered (`answers/2026-09-26T0356.json`):
Knuth-Plass preferred 5 of 5 (95% CI 47.8-100%, p = 0.062), including all 3
from the `kp_worse` stratum, where the worst-line metric expected greedy to win.

**Owner ruling, 2026-09-26: "go with k-p".** Option B of §7: Knuth-Plass
replaces greedy on JUSTIFIED blocks, stretch-only, the same candidate the blind
test showed. Shrink (C) was never shown to him and is not part of the ruling.
Ragged blocks are unchanged.

### 12g. Reproduce

```
CROSSPOINT_TEST_SD=<sd snapshot> CROSSPOINT_LINEBREAK_CORPUS=/tmp/corpus.txt CROSSPOINT_KP_OUT=/tmp/kpb \
CROSSPOINT_KP_FACES=Albo:14,LibreFranklin:14 \
  build/line_break_quality/LineBreakKnuthPlassTest --gtest_also_run_disabled_tests --gtest_filter='*BlindStats*'
python3 tools/knuth_plass_blind.py --stats-dir /tmp/kpb --corpus /tmp/corpus.txt --sd <sd snapshot>
```
