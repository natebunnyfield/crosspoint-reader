# Line breaking: the two breakers, why they share one flag, and what each costs

Written 2026-08-25, while unfreezing `hyphenationEnabled` into the **Line
Breaks** row on Typography Settings (owner ruling the same day: *"unfreeze
hyphenation and get the better line breaker"*).

Everything in §0–§7 was read or measured at `81c5a8a05`. **§8 is a second dated
pass, 2026-08-26, measured at `8f6e0294f`**, added after the owner asked how
"winning" was being defined in §3 — it re-runs the comparison as a 2×2 with
worst-line, river and hyphen-run metrics. **§9 is a third, 2026-08-27**: the
ragged hyphenation gate that §7 and §8j both list as untouched, swept 40–100 at
14 pt / 512 px. Its verdict is that **70 stays** and that nothing about what the
device draws has changed. Where a claim is inferred rather than measured, it
says so.

**Read this before touching `ParsedText::computeLineBreaks` or
`computeHyphenatedLineBreaks`.** Two of the sections below overturn something
that was written down as settled, and §8 qualifies a third — §3's verdict
stands, but not for the reason §3 gives.

---

## 0. What the row is

`CrossPointSettings::hyphenationEnabled` was `static constexpr uint8_t = 1` from
the 2026-08-21 reduction until today. It is now a live field with a row:

| | |
|---|---|
| Row | **Line Breaks**, on Typography Settings, above Justified Text |
| Stored key | `hyphenationEnabled`, unchanged — a settings.json written before the freeze round-trips with its old meaning |
| `1` **Hyphenated** | `computeHyphenatedLineBreaks` — first-fit greedy that splits words. **The default, and what every shipped build has drawn.** |
| `0` **Whole Words** | `computeLineBreaks` — the total-fit dynamic program. Dead code in every shipped configuration until today. |
| Model | `lib/Epub/Epub/LineBreakMode.h` (pure, host-tested by `test/line_break_mode`) |
| Quality + cost | `test/line_break_quality` — eleven live tests, plus `DISABLED_Sweep`, the instrument that produced §3 and §8 |
| Corpus | not checked in (book text); rebuild it with `tools/linebreak_corpus.py` — §8a |

The row is **not** called Hyphenation because that name describes a side-effect
and hides the switch. It is not called anything promising evenness either; §3
says why, and §8 says why that verdict survived a re-measurement that was built
to overturn it.

### One class of card moves on its own

"Nothing changes until the row is touched" is true of a fresh install and of any
card saved since 2026-08-04, and **false for one narrow window** — found by
adversarial review, not by me. From `cc6937b97` until the freeze,
`normalizeRetiredSettings()` pinned this field to 1 on every load, so a card
still carrying `"hyphenationEnabled": 0` from the era of the old Hyphenation
toggle has been rendering at 1 regardless of what it stores. That pin is gone,
so `fromJson` honors the 0 again: such a device switches to the DP and
repaginates on the next boot without the row being touched.

Deliberate. It is a real choice made when a row for it existed, and a row for it
exists again; discarding a stored preference because the project changed its
mind twice is the worse option. Recorded rather than described as impossible.

### Cache cost: none

`hyphenationEnabled` is already a `ReaderRenderSpec` field
(`ReaderRenderSpec.h:41`), already written into the section file
(`Section.cpp:259`), already in the header-size arithmetic (`:244`) and already
compared on load (`:322`). So moving it invalidates every cached section by
itself, with **no new spec field and no `SECTION_FILE_VERSION` bump** — the
version stays at 51. Verified by reading all four sites, and end to end: a
simulator run that stored `0` repaginated and rendered different breaks with the
version untouched.

### CSS override: nothing to honor

The standing ruling that a typography setting is a default a book's own CSS beats
where the book was explicit **does not apply here, and cannot**.
`CssParser::parseDeclarationIntoStyle` (`CssParser.cpp:467-569`) is the complete
list of properties the parser understands, and it has no branch for `hyphens`,
`orphans` or `widows`. A stylesheet's opinion about line breaking is discarded
before layout ever sees it, so there is no stored per-book value to override and
no conflict to resolve. The device is the only voice. (The hyphenation LANGUAGE
does come from the book — `dc:language` via `Hyphenator::setPreferredLanguage`,
`Section.cpp:573` — but that selects a pattern trie, not a policy.)

---

## 1. Why the two are coupled to one flag

Asked directly, and answered from the code rather than assumed. **It is not a
policy pairing and not a performance budget. It is that hyphenation in this
engine is destructive, and the DP indexes the array it destroys.**

`ParsedText::hyphenateWordAtIndex` (`ParsedText.cpp:1245-1336`) does not report a
break; it performs one. It resizes `words[i]`, appends the hyphen, and inserts
the remainder into **eight** parallel containers — `words`, `wordStyles`,
`wordWidths`, `wordContinues`, `wordNoSpaceBefore`, `wordSourceStart`,
`wordIsFocusSuffix`, `rubyTexts` — shifting every index above it.

Three consequences, each fatal to a naive "let the DP hyphenate too":

1. **The DP's tables are pre-sized.** `computeLineBreaks` captures
   `totalWordCount = words.size()` once, allocates `dp[]` and `ans[]` to it, and
   walks `i` downward from `n-2`. A split inside that loop invalidates the
   indices, the tables and the loop variable at once.
2. **There is no non-destructive "what if".** `hyphenateWordAtIndex` takes an
   `availableWidth` and greedily commits to the widest prefix that fits. The raw
   candidate list *is* available without mutation
   (`Hyphenator::breakOffsets`), which is the one piece of the puzzle that
   already exists — but nothing downstream consumes it.
3. **The output contract cannot express a mid-word break.**
   `computeLineBreaks` returns `std::vector<size_t>` of WORD indices, and
   `extractLine` slices `words[lastBreakAt .. lineBreak)` by them. "Break inside
   word 12" is unsayable unless word 12 has already been split in `words`.

The greedy breaker can hyphenate precisely because it re-reads
`wordWidths.size()` on every iteration and never looks back.

**Historically:** nothing to find. The fork's history is flattened at
`3da2cd3cf` (a squashed import, where both functions and the
`if (hyphenationEnabled)` dispatch arrive together already coupled). `git log -S`
on the flag and on both function names turns up no commit that chose the pairing.

**What the DP still does with hyphenation:** it runs a *pre-pass* over words that
would overflow even alone on a line, splitting them with
`allowFallbackBreaks=true`. So **Whole Words is not "no hyphens ever"** — a
German compound or a URL still breaks, because the alternative is a word running
off the glass. What goes away is opportunistic hyphenation. The row's label must
not promise zero.

### Is DP + hyphenation reachable? Not as a flag — but it is closer than I first wrote

The paragraph above originally concluded "the two cannot be combined." That is
too strong, and adversarial review caught it. **This DP cannot WEIGH a hyphen
candidate** — that part holds. But it already tolerates destructive hyphenation
in its own pre-pass, which runs before `totalWordCount` is captured, so each
fragment is simply another word. **Splitting eagerly there at every legal
breakpoint would hand the DP hyphen candidates with no change to its indexing
and none to its output contract.**

Two things are genuinely missing, and neither is the index problem:

* **A per-break penalty.** Without one the optimizer takes hyphens for free — a
  break inside a word costs it nothing in squared slack — and sets a page of
  them. Knuth's hyphen penalty is not decoration.
* **A no-space-between-fragments flag.** `hyphenateWordAtIndex` gives the
  remainder `wordContinues = false`, so two fragments of one word landing on the
  same line would be set with a full word space between them. That cannot happen
  today only because the prefix is the widest that fits and prefix + remainder is
  at least the original word, which did not fit — a guarantee eager splitting
  destroys immediately. Load-bearing, undocumented until now.

So: still `build` tier, still real work, but a smaller piece than "cannot be
combined" implies. §3 says it is the combination worth having; §4 says the
budget is there.

---

## 2. Method

Two instruments, because they answer different questions and one of them lies.

**The host suite is the authority for numbers.** `test/line_break_quality` links
the real `ParsedText` + `GfxRenderer` + hyphenation stack and reads geometry
straight off the `TextBlock`s the paginator would bake. `DISABLED_Sweep` runs it
over a corpus:

```
CROSSPOINT_LINEBREAK_CORPUS=/path/to/corpus.txt \
  build/test/line_break_quality/LineBreakQualityTest \
  --gtest_also_run_disabled_tests --gtest_filter='*Sweep*'
```

The corpus for §3 is **394 paragraphs / 23,075 words**, every paragraph of 30+
words from `ai-engineering-from-zero.epub` and `wingspan-the-whole-bird.epub` on
the test card.

**It was not checked in, and that cost a reconstruction** — §8a. The extraction
rule now lives in [tools/linebreak_corpus.py](../tools/linebreak_corpus.py)
instead of in this paragraph, so the next measurement rebuilds the same corpus
rather than guessing at one. The rebuilt copy is 394 paragraphs / 22,881 words
and reproduces this section's table to within 1.7% on the means; §8a records
the one column that does not.

**The simulator is the authority for pictures**, and for pagination wall time.

**Two traps, both of which cost a wrong reading first:**

* **`Hyphenator::cachedHyphenator_` starts null.** Nothing sets it but
  `setPreferredLanguage`, which the reader calls from `dc:language`. A host test
  that forgets it measures two breakers that both set whole words: the first run
  of this suite produced **one** hyphen in 122 lines of English and looked like a
  finding.
* **The read-aloud capture REJOINS hyphenated fragments.** `buildCapture` exists
  to speak "remembering", not "remem- bering", so
  `CROSSPOINT_SIM_READALOUD_LOG=2` is useless for counting hyphens and distorts
  line geometry on any line that ends in one. It is still the right tool for
  reading back which words landed on which line where no split occurred. Counted
  from that channel, a 1,102-word run showed 1 hyphen where the page plainly
  renders several.

Measured on an Apple Silicon Mac, `-O` host build. Simulator numbers are
`simulator_x3`, X3 portrait, 512 px measure (screen margin 5, matching
`docs/auto-justification.md`'s calibration table).

---

## 3. What each breaker does to a page

### The sweep

394 paragraphs, six measure × size configurations, both alignments forced so the
same measure can be read either way. Justified rows report **inter-word gap
width** (mean gap per line, then the spread of that across lines); ragged rows
report **line end position**. `hyphen` is lines ending in an inserted hyphen.

| Config | Align | Mode | lines | mean | sd | max dev | hyphen |
|---|---|---|---:|---:|---:|---:|---:|
| LF 12 pt @ 400 | justified | Hyphenated | 3993 | 12.68 | **8.89** | 202.32 | 681 |
| LF 12 pt @ 400 | justified | Whole Words | 4092 | 16.67 | 12.01 | 217.33 | 56 |
| LF 12 pt @ 400 | ragged | Hyphenated | 4078 | 359.94 | 29.23 | 299.94 | 172 |
| LF 12 pt @ 400 | ragged | Whole Words | 4102 | 357.11 | **27.54** | 297.11 | 56 |
| LF 12 pt @ 512 | justified | Hyphenated | 3029 | 10.45 | **5.14** | 70.55 | 489 |
| LF 12 pt @ 512 | justified | Whole Words | 3090 | 12.87 | 6.83 | 68.13 | 33 |
| LF 12 pt @ 512 | ragged | Hyphenated | 3088 | 469.80 | 33.00 | 409.80 | 71 |
| LF 12 pt @ 512 | ragged | Whole Words | 3093 | 468.78 | **28.15** | 408.78 | 33 |
| LF 12 pt @ 640 | justified | Hyphenated | 2348 | 8.95 | **3.52** | 40.05 | 389 |
| LF 12 pt @ 640 | justified | Whole Words | 2394 | 10.95 | 5.71 | 111.05 | 24 |
| LF 12 pt @ 640 | ragged | Hyphenated | 2392 | 596.78 | 33.19 | 148.78 | 46 |
| LF 12 pt @ 640 | ragged | Whole Words | 2394 | 596.01 | **28.64** | 152.01 | 24 |
| LF 18 pt @ 400 | justified | Hyphenated | 6266 | 30.92 | **24.47** | 229.08 | 1081 |
| LF 18 pt @ 400 | justified | Whole Words | 6441 | 43.08 | 33.75 | 192.92 | 83 |
| LF 18 pt @ 400 | ragged | Hyphenated | 6370 | 348.25 | **35.34** | 283.25 | 775 |
| LF 18 pt @ 400 | ragged | Whole Words | 6561 | 335.38 | 39.41 | 246.38 | 83 |
| LF 18 pt @ 512 | justified | Hyphenated | 4744 | 23.13 | **17.09** | 391.87 | 815 |
| LF 18 pt @ 512 | justified | Whole Words | 4875 | 31.34 | 24.61 | 277.66 | 56 |
| LF 18 pt @ 512 | ragged | Hyphenated | 4840 | 454.11 | 40.36 | 377.11 | 301 |
| LF 18 pt @ 512 | ragged | Whole Words | 4892 | 447.32 | **39.88** | 358.32 | 56 |
| LF 18 pt @ 640 | justified | Hyphenated | 3695 | 18.51 | **10.05** | 130.49 | 682 |
| LF 18 pt @ 640 | justified | Whole Words | 3783 | 24.06 | 17.52 | 367.94 | 58 |
| LF 18 pt @ 640 | ragged | Hyphenated | 3760 | 579.66 | 44.90 | 490.66 | 155 |
| LF 18 pt @ 640 | ragged | Whole Words | 3789 | 575.32 | **42.10** | 486.32 | 58 |

> **Qualified by §8 (2026-08-26).** This table is not algorithm against
> algorithm — the stored byte couples the two, so it compares greedy WITH
> hyphens against total fit WITHOUT them — and mean/sd cannot see what total fit
> optimizes. §8 re-measures it on a 2×2 with worst-line, river and hyphen-run
> metrics. **The verdict below survives**, and by a wider margin than it claims;
> what changes is the reason, and the strength of the case for the missing cell.

### THE SURVEY'S PREDICTION IS WRONG, and this is the finding

`docs/typography-possible-2026-08-25.md` §3.3 says: *"Off, the right edge of a
ragged paragraph gets noticeably more even (that is what the DP buys)."* The row
was drafted with a second label of **"Even Spacing"** on that basis.

**On a justified page the greedy breaker is both tighter and more even, in all
six configurations.** At the X3's own 512 px measure and 12 pt, word spacing
runs 10.45 px against 12.87 and its spread 5.14 against 6.83. The optimizer
loses for exactly the reason §1 gives: every break it would like to take inside a
word is unavailable to it, so it pays the shortfall in slack, and justification
turns slack into word space. Word spacing is what a reader means by "even", so
that label would have been a claim the page disproves.

**On a ragged page the two are close and the sign moves with the size.**
Total-fit is ahead at 12 pt (28.15 against 33.00 at 512 px), level at 18 pt @
512, behind at 18 pt @ 400. The **max dev** column says nothing useful about
either: on a ragged page the deepest hole is a one-word line, both breakers
produce them, and the two figures land within a pixel or two of each other in
every configuration. An earlier revision of this table read 134.92 against
301.93 there and made a paragraph out of it; that gap was an artifact of the
measurement dropping one-word lines, found by adversarial review and corrected.

**The one large, reliable effect is the hyphens themselves**: 489 hyphenated
lines against 33 at 512 px / 12 pt, a factor of 15.

So the row is a genuine taste trade, not an upgrade, and its labels say so:
**Hyphenated** against **Whole Words**. Both are true in every configuration.

### And the same numbers say what the real prize is

The DP's entire deficit on justified text is the candidate set it is denied.
That is a direct argument that **total-fit WITH hyphen points — Knuth-Plass as
it is actually specified — would beat both of these**, and it is the one
combination the setting cannot select. §1 says what it would take.

---

## 4. What it costs to run

### The breaker itself

`test/line_break_quality`'s cost test, 96 paragraphs × 5 passes, host:

| Mode | ms | lines |
|---|---:|---:|
| Hyphenated | 2.98 | 1356 |
| Whole Words | 4.68 | 1380 |
| **ratio** | **1.57×** | |

This times `layoutAndExtractLines` and nothing else. The first version of the
test timed the measurement helper around it — text splitting, `ParsedText`
construction, two `getTextAdvanceX` calls per word — all of it identical in both
modes, which diluted the ratio and published **1.35×**, a floor rather than the
figure. Found by adversarial review.

The DP is **not** O(n²) in the paragraph: its inner loop `break`s the moment the
line overflows, so it is O(n × words-per-line) — a small constant multiple of the
greedy pass, roughly the number of words a line holds. The test asserts a ceiling
of 4× so a rewrite that made it quadratic fails the build rather than shipping.

### Whole-book pagination, on the simulator

`ai-engineering-from-zero.epub` read end to end (90 page turns, 6 chapters,
92–93 pages, ~23,000 words), summing contiguous `[SCT] Page N processed`
intervals — i.e. actual pagination work, with idle excluded. Three runs each:

| Mode | run 1 | run 2 | run 3 | median | per page |
|---|---:|---:|---:|---:|---:|
| Hyphenated | 38 | 40 | 37 | **38 ms** | 0.59 ms |
| Whole Words | 52 | 60 | 56 | **56 ms** | 0.86 ms |

**+47% on total pagination work, +0.27 ms per page**, ranges non-overlapping
across runs. It sits below the 1.57× isolated figure above, which is the right
direction: the per-page interval includes parsing and glyph measurement that
neither mode changes.

**What that implies for the ESP32-C3 is NOT measured** — there is no paired
device here, and the honest statement is the ratio, not a scaled millisecond
count. Two things bound it. The +47% is an **upper** bound on the relative
slowdown of a page turn that must paginate, because everything the device adds
on top (SD reads, the zip inflate, the section file write) is identical in both
modes and dilutes it. And the device already paginates lazily a screenful ahead,
so the cost lands on a chapter boundary rather than on every turn.

If that ever needs to go: the DP's dominant term is the inner loop over
candidate line ends, and it already breaks on overflow. There is no cheap
constant to shave.

---

## 5. Rendered proof

Simulator, X3 portrait, 512 px measure, light page with grain/fade/beam off so
the picture is about the type. Each pair is the SAME page of
`ai-engineering-from-zero.epub`, reached by the same script from a wiped
`.crosspoint/`, with only `hyphenationEnabled` different.

**Justified, 12 pt** — "com-plete" and "previ-ous" against no split words, and
the visibly wider gaps that buys. Measured off the rendered pixels for the
paragraph shown, last line excluded:

| Mode | lines | gap mean | gap sd | gap range |
|---|---:|---:|---:|---|
| Hyphenated | 8 | 13.35 px | 4.01 | 8–21 |
| Whole Words | 8 | 14.66 px | 2.49 | 11–18 |

On this single paragraph Whole Words is the more even of the two — the opposite
of the corpus result, and a good demonstration of why one page is not evidence.
n = 8 lines against the corpus's 3,029.

**Ragged, 18 pt (the shipped default at this measure)** — the same paragraph,
line ends:

| Mode | lines | end sd | end range | spread |
|---|---:|---:|---|---:|
| Hyphenated | 10 | 47.2 | 374–514 | 140 px |
| Whole Words | 10 | 28.5 | 409–498 | 89 px |

Figures are PNG at native panel pixels, cropped to the paragraph under judgment
and magnified 2× NEAREST. Content coverage 15.7% (justified) and 14.9% (ragged);
effect delta 13.6% and 19.2% of pixels changed by more than 4 levels.

---

## 6. Verified

* Host suite **528 → 540**, all passing. `test/line_break_mode` (8) and
  `test/line_break_quality` (4 + 1 disabled instrument). §8 later took it to
  **547** with seven more in the same suite.
* Both `line_break_mode` byte assertions validated failing-first, by re-pointing
  the fallback and by swapping the two stored values.
* `pio run -e simulator_x3` green; `pio run -e default` (ESP32-C3) green.
  Flash **5,038,877 -> 5,039,227 bytes, +350**, measured against a stashed HEAD
  on the same machine. 76.9% either way.
* Round trip on the device UI, headlessly: the row renders, the picker offers
  **Hyphenated** first and **Whole Words** second, and picking the second writes
  `"hyphenationEnabled": 0` to `settings.json`.

### The adversarial pass

Run read-only against the diff, and it earned its keep: it found the
pre-2026-08-04 settings window (§0), the one-word lines missing from the rag
figures (§3), the timing test measuring mode-identical work (§4), and the
overstated coupling claim (§1). It also reported CLEAN on the parts that matter
most and are easiest to get wrong: `fromJson`'s default and its ENUM clamp both
resolve to 1 for a missing or corrupt byte, agreeing with `linebreak::modeFor`;
the section-file write / size / compare / `layoutFingerprint` net covers every
consumer of the value except the font prewarm (fixed, `ParsedText.cpp`); the row
renderer indexes labels by stored value and the web API reports and clamps in
stored order, both ignoring `displayOrder` correctly; the `size() > 2` popup gate
the header comments warn about no longer exists (it is `enumCount() > 1`); and
nothing anywhere took `sizeof(CrossPointSettings)` or `memcpy`'d it, so the
extra byte is invisible.

Two smaller corrections from the same pass: the specimen comment claimed a
480 px X3 panel (it is 540, which is what makes a 512 px measure possible), and
the ragged quality test relied on autojustify demoting the block without
asserting it — a moved threshold would have made it silently compare two
justified pages. Both fixed. It did not compile or run anything, so §6's figures
are mine, not its.

## 7. Deliberately not done

* **DP + hyphenation.** §1. It is the interesting item and it is `build` tier;
  scoping it is a separate piece of work.
* **A help line under the row.** The Typography screen has no help-text slot,
  and inventing one for this row alone was not in the ask. The caveat that
  Whole Words still breaks an over-wide word therefore lives only in
  `LineBreakMode.h`.
* **Raising the greedy breaker's ragged hyphenation gate.** `raggedSkipsHyphen`
  (`ParsedText.cpp`) suppresses hyphenation once a ragged line has reached 70% of
  the measure, which is most of why the greedy rag is uneven at 18 pt. Moving it
  is a change to the DEFAULT rendering and nobody asked for one.
  **SUPERSEDED 2026-08-27 — the owner asked, and §9 is the sweep.** It kept 70.
  Note that the "uneven at 18 pt" claim above **remains unverified**: §9 measures
  14 pt only, on the owner's instruction, and says so.
* **A per-book override.** §0: there is nothing to override.

---

## 8. Re-measured 2026-08-26: the 2×2, the worst line, rivers, hyphen runs

Written the day after §3, because §3's verdict was reported to the owner as
"the greedy breaker beats the total-fit DP" and he asked how *winning* was being
defined. Three things were wrong with the answer, and this section is the
re-measurement.

1. **It was not algorithm against algorithm.** The stored byte couples the two,
   so what §3 compared was greedy **with** hyphens against total fit
   **without** them — and hyphenation is exactly what lets the greedy breaker
   pack tight. It flatters the number being measured.
2. **Mean and standard deviation are the wrong summary for what total fit is
   FOR.** Knuth-Plass cubes its badness so that one terrible line outweighs many
   slightly loose ones. Averaging hides precisely that.
3. **Rivers were not measured at all** — the classic justified-text defect a
   reader actually notices.

Everything below is measured at **`8f6e0294f`** — the commit that shipped the
row, one past the `81c5a8a05` §3 quotes — plus the harness changes described in
§8i, on the same corpus and the same six configurations as §3. `8f6e0294f` is
the only commit between the two that touches `ParsedText.cpp` or the
hyphenation tree, and it is the one that introduced the dispatch being measured,
so the layout engine under test here is the layout engine §3 described.

### 8a. The corpus, and one honest discrepancy

394 paragraphs, **22,881 words** — every `<p>` of 30 or more words from
`ai-engineering-from-zero.epub` and `wingspan-the-whole-bird.epub` on the test
card. §3 quotes 23,075 words for the same 394 paragraphs, a 0.8% difference in
the word count with the paragraph count identical; the corpus itself was never
checked in, so this one is a **reconstruction** and the extraction rule is now
[tools/linebreak_corpus.py](../tools/linebreak_corpus.py) rather than a sentence
in a doc.

Measured against all 24 rows of §3's table: **line counts within 0.85%, means
within 1.7%, standard deviations within 11.1%** — and every deviation above 5% is
in a greedy-with-hyphens row and in the same direction (this corpus slightly
higher). That is what says the two corpora are the same text with a small
difference in what got extracted, rather than two different texts.

One column does not reproduce at all: §3 reports 24–83 hyphenated lines in the
Whole Words arm and this corpus produces 0–13, so §3's copy held a handful of
tokens wide enough to trip the oversized-word pre-pass that this extraction does
not. It changes no conclusion in either section — the effect being compared is
400–1000 lines — but it is the one figure below that should not be read against
§3's, and it is the likeliest explanation for the sd spread above.

### 8b. The 2×2: three cells are reachable, and the fourth is not an algorithm

Hyphenation turns out to be an independent axis after all, and not a new one:
`Hyphenator::cachedHyphenator_` is filled only by `setPreferredLanguage`, and
only English and Spanish tries ship (`LanguageRegistry.cpp`). Point it at any
other tag and pattern hyphenation is gone.

**That cell is not synthetic.** It is what a French, German or untagged EPUB
renders as on a shipped device *today*, with the default stored byte — the same
code path minus a trie, and `setPreferredLanguage` raises
`BookNotes::NoHyphenationForLanguage` on exactly that transition.

| | hyphenation ON | hyphenation OFF |
|---|---|---|
| **greedy** (`computeHyphenatedLineBreaks`) | **the shipped default** | reachable — an untagged or non-en/es book |
| **total fit** (`computeLineBreaks`) | not an algorithm, see below | **the shipped alternative** |

**The fourth cell was settled by measurement, not by argument.** Handing the DP
an English trie is a legal input, so "total fit with hyphenation" *looks*
reachable. It is not: the DP can only ever see the hyphens its own
oversized-word pre-pass already committed, so the trie moves *where* such a word
splits and never *whether* the optimizer takes a hyphen. If that reasoning is
right the two runs must land on the same page — and they do:

| config | cell | lines | mean | sd | p95 | hyphenated |
|---|---|---:|---:|---:|---:|---:|
| LF 12 pt @ 512 | total fit | 3074 | 13.070 | 7.108 | 26.000 | 0 |
| LF 12 pt @ 512 | total fit + trie | 3074 | 13.070 | 7.108 | 26.000 | 0 |
| LF 18 pt @ 400 | total fit | 6413 | 43.714 | 34.573 | 124.000 | 13 |
| LF 18 pt @ 400 | total fit + trie | 6413 | 43.687 | 34.526 | 124.000 | 13 |

(`lines` here counts lines that have at least one measurable GAP, which is the
sample the mean and p95 are taken over — 3074 at 12 pt / 512 px. §8f's table
carries two other counts for the same configuration and neither is this one:
3075 lines the breaker had a choice about (the one line with no measurable gap
is the difference), and 3469 lines on the page once each paragraph's last line
is added back. Three denominators, three questions; an earlier draft of this
note conflated two of them.)

Identical to three decimals in five of six configurations; the sixth differs by
0.06% of the mean, which is one over-wide word splitting at a pattern point
instead of a fallback point. Same lines, same hyphens, same p95, everywhere.
`LineBreakQuality.TheDpCannotUseHyphenPointsSoThereIsNoFourthCell` pins it, and
carries a precondition proving the axis is live — two identical rows are also
what a dead hyphenation axis produces, and a dead axis is this suite's oldest
failure mode.

So classical Knuth-Plass remains **the missing cell and the real prize**, exactly
as §1 and §3 say. §8d is now a direct measurement of how much it would be worth.

**One residue to disclose.** "Hyphenation off" is not "no hyphens". Three things
survive with no trie, and all three are in `Hyphenator::breakOffsets` above the
`if (hyphenator)` guard: an explicit `-` or soft hyphen already inside a word
(`buildExplicitBreakInfos`, which returns before the trie is consulted at all),
an apostrophe contraction boundary (`appendApostropheContractionBreaks`, applied
regardless), and the every-N-character fallback on a word too wide to fit a line
alone. In the greedy-minus-hyphens cell that residue is **1.7–2.4% of lines**
(65 of 3063 at 12 pt / 512 px), against 0–0.2% for the DP, whose only mid-word
breaks come from its own oversized-word pre-pass. So the comparison is not
perfectly clean, and it is unclean in the direction that **flatters the greedy
cell** — which still loses §8d. The conclusion there is conservative.

### 8c. De-confounded: hyphenation moves the page far more than the algorithm

Justified, all six configurations, mean inter-word gap in px:

| config | greedy +hy | greedy −hy | total fit | hyphenation moves | algorithm moves |
|---|---:|---:|---:|---:|---:|
| LF 12 pt @ 400 | 12.83 | 16.10 | 16.64 | **3.27** | 0.54 |
| LF 12 pt @ 512 | 10.59 | 12.76 | 13.07 | **2.17** | 0.31 |
| LF 12 pt @ 640 | 8.94 | 10.65 | 11.03 | **1.71** | 0.38 |
| LF 18 pt @ 400 | 31.33 | 43.37 | 43.71 | **12.04** | 0.34 |
| LF 18 pt @ 512 | 23.21 | 31.00 | 31.30 | **7.79** | 0.30 |
| LF 18 pt @ 640 | 18.82 | 23.53 | 24.40 | **4.71** | 0.87 |

**The hyphenation axis moves the mean 4.5–35× as far as the algorithm axis does.**
§3's headline was therefore a report about hyphenation wearing an algorithm's
name: nearly the whole of "greedy sets tighter and more evenly" is the trie, not
the breaker. `LineBreakQuality.HyphenationMovesThePageMoreThanTheAlgorithmDoes`
asserts the ratio so that a future coupling change cannot quietly re-confound it.

### 8d. Worst-line badness — the metric §3 could not see

Same per-line gap as §3's `mean` column, read as order statistics instead of an
average, and quoted as a **multiple of the font's own word space** so 12 pt and
18 pt can share a table (12 pt: 5.0 px, 18 pt: 8.0 px). `paraWorst` is the mean
across paragraphs of that paragraph's loosest line — the "how bad is the worst
line on this page" figure, and the most robust of the four.

One thing worth pinning before reading the table: on a justified line every gap
is the same width **to the pixel** — measured over the whole specimen at both
sizes and in all three cells, the within-line spread is exactly 0.000 px. So
"the line's mean gap" and "the line's gap" are the same number, and these are
order statistics over LINES rather than a summary of a summary.

| config | cell | mean | p95 | p99 | max | paraWorst |
|---|---|---:|---:|---:|---:|---:|
| LF 12 pt @ 400 | greedy +hy | **2.57** | **5.20** | **8.20** | 67.00 | **5.28** |
| | greedy −hy | 3.22 | 7.60 | 12.80 | **43.00** | 7.19 |
| | total fit | 3.33 | 7.20 | 12.00 | 46.80 | 6.82 |
| LF 12 pt @ 512 | greedy +hy | **2.12** | **4.00** | **5.80** | **16.20** | **3.58** |
| | greedy −hy | 2.55 | 5.60 | 8.60 | 29.00 | 4.69 |
| | total fit | 2.61 | 5.20 | 8.00 | **16.20** | 4.35 |
| LF 12 pt @ 640 | greedy +hy | **1.79** | **3.20** | **4.20** | **9.80** | **2.62** |
| | greedy −hy | 2.13 | 4.20 | 6.20 | 19.40 | 3.41 |
| | total fit | 2.21 | 4.20 | 6.20 | 13.20 | 3.27 |
| LF 18 pt @ 400 | greedy +hy | **3.92** | **9.88** | **17.50** | 37.00 | **10.86** |
| | greedy −hy | 5.42 | 17.38 | 24.50 | 34.25 | 16.13 |
| | total fit | 5.46 | 15.50 | 20.62 | **29.50** | 13.80 |
| LF 18 pt @ 512 | greedy +hy | **2.90** | **6.38** | **10.62** | 51.88 | **6.74** |
| | greedy −hy | 3.87 | 9.62 | 18.75 | 39.38 | 10.10 |
| | total fit | 3.91 | 8.75 | 16.88 | **38.62** | 8.91 |
| LF 18 pt @ 640 | greedy +hy | **2.35** | **4.50** | **7.50** | **18.62** | **4.47** |
| | greedy −hy | 2.94 | 6.88 | 11.88 | 30.88 | 6.20 |
| | total fit | 3.05 | 6.38 | 10.25 | 49.00 | 5.85 |

#### At equal hyphenation the DP does exactly what it is for

Reading only the two whole-word cells — the pure algorithm comparison §3 never
made:

| metric | total fit better | tied | worse |
|---|---:|---:|---:|
| mean | 0 | 0 | **6** |
| sd | 5 | 0 | 1 |
| **p95** | **5** | 1 | **0** |
| **p99** | **5** | 1 | **0** |
| max | 4 | 0 | 2 |
| **paraWorst** | **6** | 0 | **0** |

**Total fit gives up 0.7–3.8% on the average line and buys back 4.1–14.4% on
the loosest line of a paragraph, and it never loses that trade in any of the six
configurations.** (Percentages here are relative to `greedy −hy`, the arm it is
being compared against. The next table's are relative to the shipped default,
which is a different baseline — the doc states which each time from here on,
because an earlier draft did not and the same effect reads 21–32% or 17.7–24.4%
depending on the choice.) That is the Knuth-Plass bargain, stated in the units it is
made in, and it is invisible to a mean and a standard deviation. §3's "the
optimizer loses" was true of the setting and false of the algorithm.

#### But against the shipped default it is not close

| metric | default (greedy +hy) better | tied | worse |
|---|---:|---:|---:|
| mean | **6** | 0 | 0 |
| **p95** | **6** | 0 | 0 |
| **p99** | **6** | 0 | 0 |
| max | 2 | 1 | 3 |
| **paraWorst** | **6** | 0 | 0 |

The default is 21–32% better on `paraWorst` and 30–57% better at p95, both
relative to the default itself as baseline. **The
worst-line metric, the one introduced specifically because it might overturn
§3, does not overturn it** — it strengthens it, because the DP's own advantage
is worth less than the hyphens it is denied.

### 8e. Rivers

**The definition, and the one that failed.** A river is a maximal chain of gaps
on consecutive lines of the **same paragraph** spanning **three or more** lines,
where each consecutive pair is *linked*. Three lines because two aligned gaps
happen constantly by chance; within a paragraph because a chain crossing a
paragraph break is not one stripe; tolerances in **space widths** so one number
means the same thing at 12 pt and 18 pt. Chains are counted at their **endpoint**,
so a forking stripe counts as two.

*Linkage 1 — centres.* Linked when the two gaps' horizontal centres lie within
`tol`. This is the obvious definition and **it does not work here**. Under it the
justified rate does not clear the ragged null — at 12 pt / 512 px it is
3.18–4.29 rivers per 1000 gaps justified against 3.18–3.40 ragged, and at 18 pt
the ragged rate is *higher*. A fixed window quoted in natural word spaces is a
far looser *relative* window on a ragged page, whose gaps are one space, than on
a justified page, whose gaps run 2–5×.

*Linkage 2 — overlap.* Linked when the two gaps' horizontal **spans** overlap by
at least `minOverlap`. This is the perceptual model — a river is visible when a
column of white runs through consecutive lines — and it has the property the
centre rule lacks: a wider gap can overlap more, so a loosely set line is
genuinely more river-prone, which is why justified text has rivers and ragged
text does not. **This is the one the numbers below use.**

**But be precise about what the null then proves, because it is less than it
looks.** Adversarial review derived the identity and the sweep confirms it to
the last digit: on a ragged page every gap is exactly one space `w`, so two
gaps overlap by ≥ `t·w` **iff** their centres differ by ≤ `(1−t)·w`. On the null
the two linkages are the *same metric with `t` reversed* — §3a's ragged column
reads 6.79 / 3.96 / 1.64 at t = 0.25 / 0.50 / 0.75 and §3b's reads
1.64 / 3.96 / 6.79, and at t = 0.50 they are the identical number. So "overlap
clears the null and centres do not" is **not** a property of the two rules on
the null; the entire difference is on the justified side, where requiring half a
space of overlap corresponds to a centre window of `w − 0.5` ≈ 1.6–5.0 spaces.

The overlap rule is still the right one — a river *is* a column of white, and
its width-sensitivity is the mechanism, not a bug — but the ratios below are
partly a consequence of the units, not independent validation of the linkage.
Read them as "the metric fires on justified text and not on the null", which is
a sanity check, and not as "the metric discovered something the mean gap did
not". It did not, and the last part of this section is the measurement that
says so.

#### It fires on justified text and not on the null, by 6.7–21×

Rivers per 1000 **gaps** (not per 1000 lines: a tighter breaker puts more words,
and so more gaps, on each line, which mechanically gives a river more chances to
start). Overlap ≥ 0.50 space widths.

| config | cell | justified | ragged null | ratio |
|---|---|---:|---:|---:|
| LF 12 pt @ 400 | greedy +hy | 41.17 | 3.96 | 10.4× |
| | greedy −hy | 56.15 | 3.93 | 14.3× |
| | total fit | 56.61 | 3.83 | 14.8× |
| LF 12 pt @ 512 | greedy +hy | **28.64** | 3.18 | 9.0× |
| | greedy −hy | 38.94 | 3.40 | 11.5× |
| | total fit | 39.02 | 3.24 | 12.0× |
| LF 12 pt @ 640 | greedy +hy | **17.39** | 2.59 | 6.7× |
| | greedy −hy | 25.23 | 2.59 | 9.7× |
| | total fit | 26.43 | 2.81 | 9.4× |
| LF 18 pt @ 400 | greedy +hy | **85.77** | 4.90 | 17.5× |
| | greedy −hy | 103.01 | 4.93 | 20.9× |
| | total fit | 102.46 | 5.04 | 20.3× |
| LF 18 pt @ 512 | greedy +hy | **57.34** | 6.23 | 9.2× |
| | greedy −hy | 77.28 | 6.13 | 12.6× |
| | total fit | 76.53 | 5.21 | 14.7× |
| LF 18 pt @ 640 | greedy +hy | **40.82** | 4.43 | 9.2× |
| | greedy −hy | 56.05 | 4.50 | 12.5× |
| | total fit | 55.24 | 5.29 | 10.4× |

#### The tolerance sweep: the ranking is stable

Justified rate per 1000 gaps at five overlap thresholds, in space widths:

| config | cell | 0.25 | 0.50 | 0.75 | 1.00 | 1.50 |
|---|---|---:|---:|---:|---:|---:|
| LF 12 pt @ 400 | greedy +hy | **48.02** | **41.17** | **34.21** | **28.34** | **9.21** |
| | greedy −hy | 63.64 | 56.15 | 49.63 | 43.45 | 19.51 |
| | total fit | 63.98 | 56.61 | 49.76 | 42.84 | 23.42 |
| LF 12 pt @ 512 | greedy +hy | **35.03** | **28.64** | **22.85** | **17.49** | **4.13** |
| | greedy −hy | 45.94 | 38.94 | 32.77 | 26.66 | 8.79 |
| | total fit | 46.49 | 39.02 | 32.38 | 27.17 | 12.24 |
| LF 12 pt @ 640 | greedy +hy | **23.29** | **17.39** | **12.34** | **9.09** | **1.06** |
| | greedy −hy | 31.05 | 25.23 | 19.78 | 15.36 | 3.88 |
| | total fit | 32.16 | 26.43 | 20.76 | 16.59 | 5.24 |
| LF 18 pt @ 400 | greedy +hy | **94.95** | **85.77** | **77.50** | **69.22** | **43.19** |
| | greedy −hy | 110.87 | 103.01 | 97.39 | 89.72 | 64.54 |
| | total fit | 110.21 | 102.46 | 94.90 | 86.18 | 66.61 |
| LF 18 pt @ 512 | greedy +hy | **66.07** | **57.34** | **46.43** | **38.10** | **17.39** |
| | greedy −hy | 85.22 | 77.28 | 68.40 | 59.93 | 36.58 |
| | total fit | 85.06 | 76.53 | 67.41 | 58.76 | 39.86 |
| LF 18 pt @ 640 | greedy +hy | **51.40** | **40.82** | **32.78** | **24.48** | **9.17** |
| | greedy −hy | 65.13 | 56.05 | 47.92 | 39.12 | 19.95 |
| | total fit | 64.42 | 55.24 | 46.74 | 38.12 | 21.79 |

**The shipped default has the fewest rivers in all 6 configurations at all 5
tolerances — 30 of 30 cells, no exceptions.** The mechanism is the same one that
makes it tighter: hyphenation narrows the gaps, narrower gaps overlap less. So
the one new metric that could have gone against the default goes *for* it.

Longest river at 0.50 runs **5–10 lines at 12 pt and 9–18 at 18 pt** — 13–18 at
18 pt / 400 px alone, where gaps average 5.4 space widths and almost every gap
overlaps something on the next line. That column is saturating and is reported
for scale only. (An earlier draft said 5–7 and 9–18, understating the 12 pt
worst case by three lines and attributing the 18 pt range to one measure.)

#### But it does not separate the two ALGORITHMS, and it adds no new axis

`greedy −hy` and `total fit` are within **0.2–4.8%** of each other at every
tolerance up to 0.50, and within 8.1% up to 1.00 — the algorithm axis is
essentially invisible to the metric. And across the 18 justified rows at
tolerance 0.50, river rate is a near-perfect linear function of the mean gap:
**r = 0.976, r² = 0.95** with both quantities in space widths (r = 0.967 in raw
pixels), and no consistent residual by cell — per-config residuals track font
size, not breaker. Rivers there are the mean gap restated.

One exception, stated because it is the only place the two whole-word cells
part: at the **1.50** threshold total fit is higher in 6/6, by 3.2–9.2% at 18 pt
and by 20–39% at 12 pt, where the rate has fallen to 4–23 per 1000 gaps. That is the
metric at its thinnest and it moves nothing — greedy +hy is still the lowest of
the three at 1.50 in all six configurations.

So: the metric is real, it clears its null, its ranking is stable, and it
**confirms §3 rather than adding to it**. Treat it as corroboration, not as an
independent argument.

### 8f. Hyphen quality, not hyphen count

§3's headline was 489 hyphenated lines against 33. Whether those 489 are *well
behaved* was unmeasured. The typographic limit is two hyphenated lines in a row;
three is a **ladder**. Runs are bounded by the paragraph.

**Two denominators, and the difference matters.** A paragraph-final line has no
break after it, so it can never end in a hyphen — it is a structural zero. Divide
by the *breakable* lines and you answer "of the lines the breaker had a choice
about, how many did it split", which is the question about the ALGORITHM; divide
by *all* lines and you answer "how much of what I see is hyphens", which is the
question about the PAGE. Over this corpus that is 394 lines of difference and the
two figures are 8–17% apart in relative terms. **The page figure is the one
quoted below.** The first version of this measurement reported the breakable
figure under the all-lines wording; adversarial review caught it.

Justified:

| config | cell | all lines | breakable | hyphenated | density (all) | (breakable) | runs of 2 | ladders (3+) | longest |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| LF 12 pt @ 400 | greedy +hy | 4367 | 3973 | 610 | **13.97%** | 15.35% | 67 | **10** | 4 |
|  | greedy −hy | 4437 | 4043 | 98 | 2.21% | 2.42% | 3 | 0 | 2 |
|  | total fit | 4461 | 4067 | 1 | 0.02% | 0.02% | 0 | 0 | 1 |
| LF 12 pt @ 512 | greedy +hy | 3403 | 3009 | 448 | **13.16%** | 14.89% | 47 | **6** | 4 |
|  | greedy −hy | 3457 | 3063 | 65 | 1.88% | 2.12% | 0 | 1 | 4 |
|  | total fit | 3469 | 3075 | 0 | 0.00% | 0.00% | 0 | 0 | 0 |
| LF 12 pt @ 640 | greedy +hy | 2722 | 2328 | 353 | **12.97%** | 15.16% | 38 | **8** | 3 |
|  | greedy −hy | 2762 | 2368 | 57 | 2.06% | 2.41% | 1 | 0 | 2 |
|  | total fit | 2774 | 2380 | 0 | 0.00% | 0.00% | 0 | 0 | 0 |
| LF 18 pt @ 400 | greedy +hy | 6666 | 6272 | 966 | **14.49%** | 15.40% | 86 | **19** | 4 |
|  | greedy −hy | 6884 | 6490 | 143 | 2.08% | 2.20% | 6 | 0 | 2 |
|  | total fit | 6926 | 6532 | 13 | 0.19% | 0.20% | 1 | 0 | 2 |
| LF 18 pt @ 512 | greedy +hy | 5118 | 4724 | 739 | **14.44%** | 15.64% | 87 | **7** | 3 |
|  | greedy −hy | 5239 | 4845 | 81 | 1.55% | 1.67% | 1 | 0 | 2 |
|  | total fit | 5257 | 4863 | 6 | 0.11% | 0.12% | 0 | 0 | 1 |
| LF 18 pt @ 640 | greedy +hy | 4066 | 3672 | 617 | **15.17%** | 16.80% | 83 | **13** | 5 |
|  | greedy −hy | 4133 | 3739 | 86 | 2.08% | 2.30% | 0 | 1 | 3 |
|  | total fit | 4160 | 3766 | 1 | 0.02% | 0.03% | 0 | 0 | 1 |

Two findings, pointing opposite ways.

**The density is high.** **13.0–15.2%** of lines on the page end in a hyphen,
steady across every measure and size. That is a lot of hyphens by any
hand-setting standard, and it is a real description of what the shipped default
draws. (Of the lines that *could* be hyphenated it is 14.9–16.8%, which is the
figure to quote when comparing breakers rather than pages.)

**The runs are mostly well behaved.** A ladder appears once every **312–731
lines**, and the longest run anywhere in 23,000 words is **five**. So the 448
hyphens at the X3's own measure are not stacking up: 47 pairs, 6 ladders, none
longer than four.

(Lines, not pages, deliberately. This harness lays out paragraphs and has no
page height, so a per-page figure would be a conversion I cannot make honestly —
the device's own lines-per-page is the missing factor and it is not measured
here.)

This is the one metric that finds *against* the shipped default, and what it
finds is small.

### 8g. Does the shipped default still stand? Yes — and by more than §3 said

Plainly, since that is what was asked. **Hyphenated (greedy + hyphens) is still
the right default**, and the new metrics strengthen the case rather than
weakening it:

* On **worst-line badness** — the metric introduced specifically because it
  might overturn §3 — the default wins 6/6 on p95, p99 and paragraph-worst, by
  21–57% (relative to the default as baseline; 17.7–36% the other way round).
* On **rivers** — the metric that had never been measured, and the classic
  argument *for* a total-fit breaker — the default wins 30/30 cells.
* The **only** thing it loses is a coin flip on the single loosest line in
  23,000 words (2 better, 1 tied, 3 worse), which is one line either way.
* The one genuine cost it carries is **hyphen density at 13.0–15.2% of lines**
  with a ladder every 312–731. Real, small, and exactly the taste trade the
  row's two labels already describe.

**What did change is the argument for the missing cell.** §3 inferred that
total fit with hyphen points would beat both, from the fact that the DP's
deficit is the candidates it is denied. §8d measures that inference directly:
held at equal hyphenation, the DP buys 4–14% on the worst line of a paragraph
and never loses the trade. That advantage is real but small; the other 23–39% —
the whole of the default's lead on the mean — is the hyphen candidates the DP
cannot see. Give it those candidates and it should carry both. It remains
`build` tier and it remains the interesting item, and it is now the only
plausible way to improve on what ships.

### 8h. Metrics measured and dropped

Recorded so the next pass does not pay for them again.

* **Rivers by gap centre** — dropped, does not clear its own null (§8e).
  Justified 3.18–4.29 per 1000 gaps against a ragged null of 3.18–3.40 at
  12 pt / 512 px, and *below* the null at 18 pt. Kept in the harness as
  `riversByCentre` with the reason attached, and
  `LineBreakQuality.TheRiverMetricClearsItsRaggedNull` fails if anyone swaps the
  linkage back — validated by doing so. Note §8e's caveat: on the null the two
  linkages are the same metric with the tolerance reversed, so this is a
  statement about the justified side, not about the rules in the abstract.
* **Loose-line counts (lines above 2× / 3× the natural word space)** — dropped
  as redundant. At these measures the *typical* line already exceeds 2× (the
  mean runs 1.79–5.46 space widths), so the threshold names no defect: 39–57% of
  lines are "loose" at 12 pt / 512 px. The counts rank the three cells
  identically to the mean in all six configurations, which is what makes them
  a restatement of it rather than a tail metric.
* **Single worst line (`max`)** — reported but **not load-bearing**. Across six
  configurations the default is better in 2, tied in 1, worse in 3, and the
  swings are enormous (+163% in one cell, −30% in another). It is one line out
  of 23,000 words and it behaves like noise; `paraWorst` and p99 are the
  statistics that carry the same idea with a sample size behind them.
* **Rivers and gap percentiles on ragged text** — not applicable. Every gap on a
  ragged line is exactly one word space, so the gap distribution is degenerate
  (p95 = p99 = max = one space in every cell) and the river rate *is* the null.
  The ragged rows in the harness's table 1 print these for completeness and mean
  nothing.
* **Rivers as an algorithm discriminator** — dropped. `greedy −hy` and
  `total fit` land within 0.2–4.8% at tolerances up to 0.50 and within 8.1% up
  to 1.00, and river rate is r² = 0.95 explained by the mean gap. It discriminates
  *hyphenation*, which the mean gap already did.

### 8i. Verified

* Host suite **540 → 547**, all passing, plus the one disabled instrument.
  The seven new tests are
  `TheDpCannotUseHyphenPointsSoThereIsNoFourthCell`,
  `HyphenationMovesThePageMoreThanTheAlgorithmDoes`,
  `AtEqualHyphenationTotalFitProtectsTheWorstLine`,
  `TheRiverMetricClearsItsRaggedNull`,
  `HyphenRunsAreCountedAndTheDefaultsDensityIsHigh`,
  `HyphenRunCountingMatchesHandWorkedAnswers` and
  `RiverChainCountingMatchesHandWorkedAnswers`.
* Four of them validated **failing-first by mutation**, not by assertion:
  swapping `riversByOverlap` for `riversByCentre` fails the null test on all
  three cells (justified 2.17 against a null of 13.42); stubbing
  `hyphenationOnFor` to `false` fails both the fourth-cell precondition and the
  de-confounding ratio; deleting the `run = 0;` reset in `hyphenRunsOf` fails
  the hand-worked run fixture (it reports longest 4 and 3 ladders where the
  truth is 1 and 0); and dropping the "was this chain extended" check in
  `riversWith` fails the hand-worked river fixture (a 5-line chain counts as 3
  rivers instead of 1).
* **The last two of those exist because the first version did not catch them.**
  The original pair of run assertions — `longest <= hyphenatedLines` and "no
  ladder without three hyphens" — are tautologies for any implementation that
  only increments on a hyphenated line, and adversarial review showed both stay
  green under exactly the mutation their comment claimed to catch. A counter can
  only be checked against an answer computed a different way, so the fixtures
  are hand-built corpora with the answers worked out on paper.
* The four §3 tests are **unchanged in behavior, and this was checked rather
  than asserted.** `layoutParagraph` now returns whole `LineRecord`s grouped by
  paragraph instead of two flat scalar vectors — which is what rivers and hyphen
  runs need — but the gap rule, the one-word-line rule and the last-line
  exclusion produce the same samples in the same order, the four test bodies are
  textually identical, and all four print the same figures they printed before.
  One thing did have to be corrected: `measure()` first routed the Whole Words
  arm through the *no-trie* cell, which would have moved it onto a different
  oversized-word pre-pass (2/2 minimum instead of English's 3/3). It routes
  through the with-trie cell now, which is what those tests always ran. Caught
  by adversarial review, not by the suite.
* **Sections 0–3b of the sweep are byte-identical before and after the
  denominator fix**, which is how the fix is known to have moved only the hyphen
  table: the geometry columns were diffed against the pre-fix run and every one
  matches.
* `pio run -e simulator_x3` green, and `pio run -e default` (ESP32-C3) green.
  No firmware source was touched — everything in this section is `test/` plus
  `tools/linebreak_corpus.py` — so there is no flash delta to report.
* **Every table in §8 was machine-checked against the raw sweep output**, not
  proofread: **408 values** across §8c, §8d, §8e's two tables and §8f (the last
  two re-verified after the denominator fix), re-parsed
  out of the Markdown and compared cell by cell to the instrument's stdout, with
  the derived columns (the two "moves" columns, the ratio column) recomputed
  rather than trusted. Zero mismatches. Transcription is the cheapest way to
  publish a wrong number, and the five tables here hold more figures than §3's
  one did.
* **The sweep is deterministic.** Two back-to-back runs produce byte-identical
  tables, and so does a run after the clang-format and hardening edits — which
  is what lets the numbers above be quoted rather than described. Nothing in
  these metrics draws on a seed; contrast the surface passes in the simulator,
  where `CROSSPOINT_SIM_GRAIN_SEED` has to be pinned for an A/B to mean
  anything.
* The sweep stays **disabled by default** and now prints five sections (the
  fourth cell, the 2×2, worst-line in space widths, rivers with both linkages
  against the null, hyphen runs):

```bash
python3 tools/linebreak_corpus.py \
    fs_/books/ai-engineering-from-zero.epub \
    fs_/books/wingspan-the-whole-bird.epub /tmp/corpus.txt
CROSSPOINT_LINEBREAK_CORPUS=/tmp/corpus.txt \
  build/test/line_break_quality/LineBreakQualityTest \
  --gtest_also_run_disabled_tests --gtest_filter='*Sweep*'
```

#### The adversarial pass on §8

Run read-only against the diff, as the standing rule requires, and it paid for
itself twice over. It found **the one wrong number in the section** — the hyphen
density, whose denominator silently excluded the 394 paragraph-final lines that
can never be hyphenated, making "15–17%" out of a true 13.0–15.2% — and it found
that the two assertions guarding the run counter were tautologies, by mutating
the counter and showing both stayed green. It also caught the reversed-tolerance
identity behind the river null (§8e), the `measure()` arm that had drifted onto
the no-trie pre-pass, the 5–10 rather than 5–7 longest-river range, the 2/2
against 3/3 fallback asymmetry that "hyphenation off" quietly introduces, and
six range-endpoint slips (6.7 not 7, 8.1 not 8, 11.1 not 11, 9.2 not 9).

It reported CLEAN, with citations, on the parts easiest to get wrong: the DP
genuinely cannot see a hyphen candidate it did not already commit
(`hyphenateWordAtIndex` has exactly two call sites, one of them the pre-pass);
the river chain bookkeeping, which it lifted into a standalone harness and
exercised with 21 hand-built cases — no off-by-one, no double counting, chains
correctly broken by a gapless line and by a paragraph boundary, `prevLine` never
dereferenced when empty; the refactor, where all four pre-existing test bodies
diff byte-identical and no sample is added, dropped or reordered; and the other
new assertions, none of which is tautological. It re-derived the win/loss counts
and most of the ranges independently and confirmed them.

It did not build or run the suite, so §8i's pass counts and the mutation results
are mine.

### 8j. Deliberately not done

* **Nothing was changed about what the device draws.** The default is unchanged,
  the row is unchanged, no threshold moved. This section is a measurement.
* **No rendered proof.** §5's figures are of §3's comparison and still stand;
  the 2×2's third cell has no picture, because the effect being reported is a
  distribution over 23,000 words rather than something one page shows. Saying so
  is better than cropping a paragraph that happens to agree.
* **`raggedSkipsHyphen` still not touched**, for the same reason as §7 — it is a
  change to the default rendering and nobody asked for one. It is worth noting
  that §8f now measures its effect: the ragged hyphen density is 0.5–2.7% at
  12 pt against 15% justified, which is that 70% gate doing its work.
  **SUPERSEDED 2026-08-27 — §9 sweeps it, and keeps 70.** §9d's 1.86% at 14 pt
  agrees with the 12 pt band quoted here.

---

## 9. The ragged hyphenation gate, swept — 2026-08-27

Owner: *"rerun for dialing in the optimal ragged gate."* Scope narrowed twice
during the run — *"just measure at 14"*, then *"only 512"* — so this is one
configuration measured densely rather than a grid measured coarsely.

`raggedSkipsHyphen` suppresses hyphenation once a ragged line has already
reached 70% of the measure. §7 and §8j both list it as untouched and both give
the same reason (it is a change to DEFAULT rendering and nobody asked for one).
The owner has now asked.

**The answer is that 70 stays.** The curve has a knee, the knee is in the
neighbourhood of 70–75, and nothing in the band is worth what moving it costs.
The rest of this section is the evidence, including the two places where a
different threshold would have picked 75 and the one place where moving up makes
a line worse.

### 9a. What was measured, and what was not

| | |
|---|---|
| Configuration | **14 pt Libre Franklin at a 512 px measure**, ragged. One only. |
| Why that one | 14 is `CrossPointSettings::DEFAULT_FONT_POINT_SIZE` and 512 is the X3's portrait measure at the default screen margin — this is the page a reader who has changed nothing is looking at. |
| Gate values | **40 to 100 in single-point steps.** 61 points. |
| Corpus | The §8a corpus, rebuilt by `tools/linebreak_corpus.py` from the same two books: **394 paragraphs, 22,881 words** — reproduced exactly, to the paragraph and to the word. |
| Instrument | `test/line_break_quality`, `DISABLED_RaggedGateSweep`. No new harness. |
| Commit | measured at `81d0098`-era working tree, with the three live tests below. |

**Is 14 pt at 512 px even a ragged page?** Yes, and it was printed rather than
assumed: the alphabet measures 401 px, which is **~36 characters per line**,
under `autojustify::THRESHOLD_CHARS` of 40, so auto-justification demotes the
block and the ragged branch runs. This gate is therefore live on the default
device in the default configuration — which is more than could be said for it
before this was checked.

**Explicitly NOT covered, and not to be inferred from the above:**

* **12, 16 and 18 pt.** Only 14 was measured.
* **400 px and 640 px.** Only 512 was measured.
* **§7's claim that this gate is "most of why the greedy rag is uneven at
  18 pt" is UNVERIFIED and stays that way.** It was not measured here and the
  14 pt result must not be read as settling it. 18 pt is the size where this
  gate is most likely to matter — a wider glyph on the same measure means fewer
  words per line, so the gate's threshold is crossed by a different population
  of lines — and it is the one corner this sweep deliberately did not visit.

### 9b. Justified pages are untouched — measured, not argued

The gate's condition begins `blockStyle.alignment != CssTextAlign::Justify`, so
a justified block cannot reach it. That is an argument. This is the measurement,
at both ends of the legal range and at the shipped value:

| gate | lines | mean gap | sd | p95 | hyphenated | ladders |
|---:|---:|---:|---:|---:|---:|---:|
| 40 | 3564 | 14.2677 | 10.1408 | 28.0000 | 590 | 13 |
| 70 | 3564 | 14.2677 | 10.1408 | 28.0000 | 590 | 13 |
| 100 | 3564 | 14.2677 | 10.1408 | 28.0000 | 590 | 13 |

Identical in every column at every precision printed.
`LineBreakQuality.MovingTheRaggedGateCannotChangeAJustifiedPage` pins it.

**"Justified" here means AFTER auto-justification**, and that distinction is the
whole reason this sweep is about the default page at all: a block demoted for a
narrow measure IS ragged, and the gate applies to it in full. 14 pt at 512 px is
exactly such a block.

One consistency check falls out of the sweep and is worth keeping: at gate 100
the gate can never fire, so the ragged page hyphenates on the same rule a
justified page does — and it produces **590 hyphenated lines, the same 590** the
justified arm produces. Two independent paths to one number.

### 9c. What "good rag" means, stated before anything was ranked

Ranking on "less rag" would quietly re-derive justified text, so the definition
came first:

* **A ragged setting is supposed to look ragged.** A mean shortfall near zero is
  a failure, not a win. **Depth is reported and is not the ranking key.**
* **The named defect of ragged setting is a HOLE** — one line conspicuously
  shorter than the lines around it, which reads as a paragraph break that is not
  there. This is the defect the gate's own comment says it exists to rescue
  against, so it is what the gate is judged on. Measured two ways: **absolutely**
  (a line ending short of 75% / 67% / 60% of the measure) and **relative to the
  line's own neighbours** (shorter than both by more than 15% / 25% of the
  measure), because a short line among short lines is not a hole.
* **The other named defect is a rag so deep the column loses its shape**, which
  p95, p99 and the per-paragraph worst carry.

All thresholds were fixed before the first run and none was moved afterwards.
Two of them would have picked a different winner; §9f says so rather than
quietly dropping them.

**There is no run-to-run noise here.** The layout is deterministic — two full
sweeps diff byte-identical — so "inside noise" below means "too small to be
seen", not "inside a sampling error". Where that judgment is made, the numbers
are given.

### 9d. The curve

14 pt @ 512 px, ragged, 394 paragraphs. Shortfall figures are percentages OF THE
MEASURE; `h25/h33/h40` are counts of lines ending short of 75% / 67% / 60% of
it; `rel15` is the count of lines shorter than BOTH neighbours by more than 15%.
Density is on the **page denominator** (all lines), the convention §8f settled.

| gate | lines | hy | dens/all | dens/brk | run2 | run3+ | longest | mean | sd | p95 | p99 | paraWorst | max | h25 | h33 | h40 | rel15 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 40 | 4042 | 4 | 0.10% | 0.11% | 0 | 0 | 1 | 10.07 | 7.82 | 25.39 | 34.18 | 22.53 | 57.03 | 187 | 45 | 16 | 123 |
| 50 | 4042 | 6 | 0.15% | 0.16% | 0 | 0 | 1 | 10.05 | 7.76 | 25.20 | 34.18 | 22.40 | 51.17 | 186 | 44 | 14 | 123 |
| 60 | 4037 | 18 | 0.45% | 0.49% | 0 | 0 | 1 | 9.95 | 7.53 | 24.80 | 32.62 | 21.86 | 51.17 | 174 | 34 | 4 | 114 |
| 65 | 4034 | 32 | 0.79% | 0.88% | 1 | 0 | 2 | 9.85 | 7.32 | 24.22 | 31.05 | 21.38 | 51.17 | 158 | 18 | 4 | 107 |
| 67 | 4031 | 42 | 1.04% | 1.15% | 1 | 0 | 2 | 9.80 | 7.22 | 24.02 | 30.08 | 21.04 | 51.17 | 148 | **8** | 4 | 97 |
| 69 | 4025 | 64 | 1.59% | 1.76% | 2 | 1 | 3 | 9.66 | 7.02 | 23.44 | 28.32 | 20.53 | 51.17 | 126 | 7 | **3** | 89 |
| **70** | **4023** | **75** | **1.86%** | **2.07%** | **2** | **1** | **3** | **9.56** | **6.92** | **23.05** | **27.54** | **20.27** | **51.17** | **114** | **7** | **3** | **83** |
| 72 | 4018 | 106 | 2.64% | 2.92% | 3 | 1 | 3 | 9.43 | 6.74 | 22.46 | 26.95 | 19.82 | 51.17 | 84 | 7 | 3 | 72 |
| 75 | 4012 | 170 | 4.24% | 4.70% | 7 | 1 | 3 | 9.16 | 6.41 | 20.90 | 24.41 | 18.87 | **68.36** | **19** | 7 | 3 | 46 |
| 80 | 3989 | 330 | 8.27% | 9.18% | 25 | 2 | 3 | 8.39 | 5.74 | 18.16 | 21.88 | 16.89 | 68.36 | 16 | 7 | 3 | **18** |
| 85 | 3966 | 529 | 13.34% | 14.81% | 64 | 12 | 5 | 7.56 | 5.44 | 16.80 | 22.27 | 15.93 | 68.36 | 15 | 8 | 4 | 22 |
| 90 | 3959 | 588 | 14.85% | 16.49% | 73 | 13 | 6 | 7.36 | 5.44 | 16.80 | 22.27 | 15.80 | 68.36 | 16 | 8 | 4 | 21 |
| 100 | 3959 | 590 | 14.90% | 16.55% | 75 | 13 | 6 | 7.35 | 5.44 | 16.80 | 22.27 | 15.79 | 68.36 | 16 | 8 | 4 | 21 |

The full 61-row table is what the instrument prints; the rows above are the ones
that carry the shape. **The shape is three regions, and two of them are dead:**

* **40–65 — a plateau where the gate is effectively OFF.** Hyphen density runs
  0.10–0.79% and every rag statistic is within a few percent of its value at 40.
  Twenty-five points of gate buy almost nothing, because a ragged line that is
  under 65% full when a word overflows is uncommon.
* **66–88 — the live band.** Everything moves, and it moves fast: density goes
  from 0.92% to 14.72% and the deep-hole counts collapse.
* **89–100 — a plateau where the gate is effectively ALWAYS OFF.** From 89 up
  the page barely moves — 3959 lines throughout, 588 hyphens becoming 590, every
  rag figure fixed to within 0.01 of the measure — and from **92 upward it is
  bit-identical to gate 100** in every column. A line is essentially never 89%
  full at the moment a word overflows, so the gate stops binding.

**So the exact value only matters between about 66 and 88, and 70 is inside that
band near its lower edge.** If the answer had been "the curve is flat, the number
does not matter", this section would say so; it is not flat.

### 9e. The knee

The trade is rag against hyphens, so the knee is where a further point of gate
stops buying rag and only buys hyphens. Measured as holes removed per hyphen
added, from 70:

| move | hyphens added | h25 removed | rate |
|---|---:|---:|---:|
| 70 → 72 | +31 | −30 | 0.97 |
| 70 → 75 | +95 | −95 | 1.00 |
| 70 → 80 | +255 | −98 | 0.38 |
| 70 → 85 | +454 | −99 | 0.22 |
| 70 → 100 | +515 | −98 | 0.19 |

**The knee on that metric is 75.** Up to it, one hyphen removes one moderately
short line; past it the same hyphens buy nothing, because the population of
moderately short lines has been exhausted.

But the three hole thresholds do not knee together, and that is the finding:

| metric | what it counts | knees at | value at 70 |
|---|---|---:|---:|
| **h40** | lines ending short of **60%** of the measure | steep part ends at 60, **floor of 3 reached at 69** and held to 81 | 3 — its floor |
| **h33** | short of **67%** | steep part ends at 67, **floor of 7 reached at 69** and held to 81 | 7 — its floor |
| **h25** | short of **75%** | **75–76** | 114 → floors at 15–16 |
| **rel>15** | shorter than both neighbours by >15% | **80**, then REVERSES (18 at 80, 22 at 85) | 83 |

**And both strict thresholds REVERSE above 81**: h33 goes 7 → 8 at gate 82 and
stays there to 100, h40 goes 3 → 4 at the same point. So the top of the live
band is not merely a waste of hyphens — on the metrics that name the
conspicuous defect it is actively worse than 70.

**The two strictest thresholds — the ones that name a line a reader would
actually take for a paragraph break — both reach their floor at 69 and hold it,
unchanged, all the way to 81. 70 sits one point past both.** What a higher gate buys is
h25: lines that end between 67% and 75% of the measure, which on a 36-character
line is a line four to nine characters short of full. That is ordinary ragged
setting, not a defect.

### 9f. The two readings that would have picked 75, reported rather than buried

Honesty requires both, since the thresholds were fixed in advance:

1. **h25 says 75.** 114 lines short of 75% of the measure at gate 70 against 19
   at gate 75 — one line in 35 becoming one line in 211 — bought with 95
   additional hyphens, taking density from 1.86% to 4.24%. Ladders do not move
   (1 either way), the longest run does not move (3), and 4.24% is still far
   under the 14.90% a justified page at this size carries. That is a real,
   cheap, defensible improvement and it is why this is a **marginal** call rather
   than an obvious one.
2. **relP95 keeps improving all the way up** — 12.30 at 70, 10.94 at 75, 9.77 at
   80, 9.57 at 90 — and never reverses. Read alone it argues for the top of the
   band.

Against them: `rel>15` **reverses** above 80 (18 at 80, 22 at 85, 21 at 100), the
strict absolute thresholds are already flat at 70, and §9g is worse than either.

### 9g. The line that gets worse, which the mean hides

The lesson of the previous two rounds, applied. Every mean, sd and percentile in
the table improves monotonically as the gate rises. **The single deepest line
does not.**

| gate | five deepest shortfalls, % of measure |
|---:|---|
| 65 | 51.17 · 50.20 · 42.58 · 40.04 · 37.11 |
| 70 | 51.17 · 50.20 · 42.58 · 37.11 · 36.52 |
| 74 | 51.17 · 50.20 · 42.58 · 37.11 · 36.52 |
| **75** | **68.36** · 51.17 · 50.20 · 37.11 · 36.52 |
| 80 | 68.36 · 51.17 · 50.20 · 37.11 · 36.52 |
| 85 | 68.36 · 51.17 · 50.20 · **46.09** · 37.11 |
| 100 | 68.36 · 51.17 · 50.20 · 46.09 · 37.11 |

At exactly the gate where h25 bottoms out, a line appears that ends at **31.6%
of the measure** — seventeen points of measure worse than anything below gate
75 — and it never goes away. The neighbour-relative worst moves with it, 40.82
to 49.41. A fifth line at 46.09 joins them at 85.

**The mechanism is not the obvious one, and the obvious one was tested and
found absent.** The natural guess is that hyphenating leaves a wide remainder
which starts the next line and digs a deeper hole than the hyphen prevented. The
instrument counts exactly that (`afterHy`: very deep lines whose predecessor
ended in a hyphen) and it is **0 at every gate from 40 to 100**. So the deep line
is not downstream of a hyphen; it is a paragraph that broke differently several
lines earlier and left one line facing a word it cannot split at all.

**Weight it honestly: it is one line in 22,881 words**, and §8h already ruled the
single worst line non-load-bearing because it behaves like a coin flip. This one
is not a coin flip — it is a monotone step that persists from 75 to 100 — but it
is still one line, and it is reported as a reason not to move rather than as a
result on its own.

### 9h. Recommendation: leave it at 70. No change shipped.

Plainly, since that is what was asked. **`raggedSkipsHyphen` stays at 70 and
nothing about what the device draws has changed.**

* The two strictest hole metrics — the ones that name the defect the gate exists
  to rescue against — **hit their floor at 69 and hold it to 81, then get
  WORSE.** 70 is one point past both knees, and no higher value improves either.
* The one metric that wants 75 is counting lines four to nine characters short
  of full, which is what ragged setting looks like.
* Moving to 75 costs **2.3× the hyphens** and makes **the deepest line in the
  corpus 17 points of measure worse**, permanently.
* Above 80 the rag stops improving (`rel>15` reverses) while density climbs to
  13–15% and ladders go from 2 to 13. Above 88 the gate is inert.
* And the price of any move at all is §9i.

This is the **marginal** outcome the brief named, and marginal means leave it.

### 9i. What a change would have cost, recorded because it nearly applied

Stated in full so that the next person to reach for this number knows the bill
before they start, and so that a future change does not skip it:

**Moving this constant changes DEFAULT rendering.** Every ragged block in every
book breaks differently. That is not a repaint — it is different line breaks,
which means different page boundaries, which means **every already-paginated
book on every card is stale**.

`hyphenationEnabled` got away without a `SECTION_FILE_VERSION` bump (§0) because
it is *itself* a `ReaderRenderSpec` field, already written into the section file
and already compared on load, so moving it invalidates caches by itself. **The
ragged gate is not a spec field and cannot become one** — it is a compile-time
constant of the layout engine, identical for every book and every setting, so
there is nothing for a header comparison to notice. A build with a different
gate loads a v51 section file, finds every spec field equal, accepts it, and
renders **the old line breaks out of cache** — exactly the silent failure
`justifyThresholdChars` and the ligature fingerprint were both added to
`ReaderRenderSpec` to avoid.

So a change here requires **`SECTION_FILE_VERSION` 51 → 52**, and the cost of
that is a full repagination of every book on every card on the next open. Not
a corruption risk, but not free either, and invisible until the reader wonders
why a familiar book is grinding.

**A 95-line reduction in moderately short lines does not buy that.**

### 9j. Metrics measured and dropped

Recorded with their numbers so the next pass does not pay for them again.

* **Mean shortfall and its sd** — reported, **not ranked**, by construction
  (§9c). Across the entire 40→100 sweep the mean moves only 10.07% → 7.35% of
  the measure and the sd 7.82 → 5.44. Both are monotone and both measure *rag
  depth*, which is the thing a ragged setting is supposed to have. Ranking on
  either is a request for justified text.
* **`afterHy`, deep lines following a hyphenated line** — dropped as a null.
  **0 at every one of the 61 gate values.** The mechanism it was built to test
  (a hyphen's remainder digging the next line's hole) does not occur in this
  corpus at all. Kept in the instrument with its reason, because a null that
  nobody records gets re-guessed.
* **`h33` and `h40`** — measured, and they are the reason for the verdict, but
  they are **flat across most of the sweep and cannot rank inside the live
  band**: h33 is 7 and h40 is 3 for every gate from 69 to 81, and both step
  back up (to 8 and 4) at 82 and hold that to 100. They say "70 is past the
  knee, and the top of the band is worse" and nothing finer. Quoting them as though they
  discriminate between 70 and 75 would be reading four significant figures off a
  flat line.
* **Line count / book length** — measured, and too small to weigh. The whole
  sweep spans 4042 lines at gate 40 to 3959 at gate 100, **2.05%**; the
  70→75 move is 11 lines in 4023, **0.27%**. A quarter of one percent of book
  length is not an argument in either direction.
* **`p99` shortfall** — dropped as non-monotone in the wrong way to be useful:
  it falls 27.54 → 21.88 from 70 to 80 and then **rises** to 22.27 at 85 and
  stays. It carries the same story as §9g's max with less resolution, and the
  max tells it better.
* **`run2` and the longest run** — reported, not decisive at the values in
  question. Runs of two go 2 → 7 across 70→75, which is 7 pairs in 4012 lines;
  the longest run is 3 at both. They only become an argument above 82, where
  ladders go 5 → 8 → 10 → 12 → 13, and nothing was proposing to go there.

### 9k. The adversarial pass on §9

Run read-only against the diff, as the standing rule requires. **It reported
CLEAN on all seven areas it was pointed at and found no structural defect**,
which is worth recording in full so the next pass does not re-derive it:

* the ×100 rewrite is exact, both operands are `int`, the largest reachable
  product is 2048 × 100 = 204,800, and `raggedHyphenGatePct()` folds to the
  literal 70 in every shipping build;
* `CROSSPOINT_RAGGED_GATE_TUNABLE` exists in exactly two places — the `#ifdef`
  and the one test target — and is absent from `platformio.ini` and every other
  build file;
* the appended namespace in `LineBreakMode.h` opens once and closes once with
  nothing orphaned;
* none of the three new tests is vacuous or tautological — it confirmed that
  `threshold=0` really justifies and `threshold=255` really goes ragged from the
  instrument's own log lines, and that `GateScope` cannot leak a value between
  tests;
* the sweep's shortfall arithmetic, its paragraph-final exclusion and the
  neighbour-relative index range are correct, and the 14 pt face is genuinely
  installed rather than silently falling back;
* every number, ratio and range endpoint it spot-checked in §9d–§9g matches
  the instrument's output, including the two claims already corrected once.

**One judgment of its own was overruled, and the assertion outlived it.** It
argued that no negative `effectivePageWidth` is reachable, so the exactness test
need only sweep positive widths. `effectivePageWidth` is `pageWidth -
firstLineIndent` with nothing clamping the difference, and a book whose CSS asks
for a text-indent wider than the measure makes it negative;
`TheRaggedGateIsSeventyAndTheRewriteIsExact` now sweeps a negative band as well.
Cheap, and it does not depend on anyone's reading of reachability.

It also noted that the saved sweep artifact predated §4 of the instrument; that
was an artifact-capture order problem, not a doc error, and the artifact has
been regenerated whole.

It agreed the verdict is **marginal but defensible**, and it built the strongest
case for 75 that the data supports — which is §9f, and is the reason §9f is in
the section at all.

It did not build or run anything, so §9l's pass counts and build results are
mine.

### 9l. Verified

* Host suite **547 → 550**, all passing, plus two disabled instruments
  (`Sweep` and the new `RaggedGateSweep`). Measured with this diff alone; a
  concurrent unrelated change was landing in the same tree while this ran and
  takes the tree total past 550, which is not this section's doing. The three
  new live tests are
  `TheRaggedGateIsSeventyAndTheRewriteIsExact`,
  `MovingTheRaggedGateCannotChangeAJustifiedPage` and
  `TheRaggedGateBindsOnARaggedPage`.
* Desktop canary `pio run -e simulator` SUCCESS.
* ESP32 device build `pio run -e default` SUCCESS (flash 76.9%).
* Two full sweeps diff byte-identical, which is what says there is no noise
  floor to read the small differences against.

### 9m. What DID change in the tree, and why it is a no-op

The verdict is "no change", but the sweep needed the gate to move, so three
things landed:

1. **`lib/Epub/Epub/LineBreakMode.h`** now owns the constant as
   `linebreak::RAGGED_HYPHEN_GATE_PCT = 70`, with the sweep's conclusion beside
   it, and exposes `raggedHyphenGatePct()`. In every shipped configuration that
   is a `constexpr` returning 70.
2. **`ParsedText.cpp`'s `raggedSkipsHyphen`** reads
   `lineWidth * 100 >= effectivePageWidth * raggedHyphenGatePct()` where it read
   `lineWidth * 10 >= effectivePageWidth * 7`. **That is the same comparison
   scaled by ten on both sides**, so it is exact rather than merely equivalent
   at typical widths — an integer rewrite that agreed at 512 px and disagreed at
   some other measure would move line breaks in books nobody sweeps.
   `TheRaggedGateIsSeventyAndTheRewriteIsExact` checks both forms against each
   other for **every width from 1 to 2048 px and every line width inside it**,
   and that test also fails if anyone edits the 70. A negative band is swept
   too — see §9k.
3. **`CROSSPOINT_RAGGED_GATE_TUNABLE`** makes the gate settable at runtime, and
   is defined by **exactly one target**: `test/line_break_quality`. Sweeping 61
   points by rebuilding the layout engine and the built-in faces once per point
   was the alternative. Nothing else may define it — on a device a mutable
   layout parameter is a way for two pages of the same book to disagree about
   where the lines go.

`tools/linebreak_corpus.py` is unchanged and reproduced the §8a corpus exactly
(394 paragraphs, 22,881 words), which is the second time that script has paid
for itself.
