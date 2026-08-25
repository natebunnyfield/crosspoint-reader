# Line breaking: the two breakers, why they share one flag, and what each costs

Written 2026-08-25, while unfreezing `hyphenationEnabled` into the **Line
Breaks** row on Typography Settings (owner ruling the same day: *"unfreeze
hyphenation and get the better line breaker"*).

Everything here was read or measured at `81c5a8a05`. Where a claim is inferred
rather than measured, it says so.

**Read this before touching `ParsedText::computeLineBreaks` or
`computeHyphenatedLineBreaks`.** Two of the three sections below overturn
something that was written down as settled.

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
| Quality + cost | `test/line_break_quality` — four live tests, plus `DISABLED_Sweep`, the instrument that produced §3 |

The row is **not** called Hyphenation because that name describes a side-effect
and hides the switch. It is not called anything promising evenness either; §3
says why.

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
  `test/line_break_quality` (4 + 1 disabled instrument).
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
* **A per-book override.** §0: there is nothing to override.
