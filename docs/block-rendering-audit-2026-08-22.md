# Block rendering audit — verse, blockquotes, nested lists, typographic details (2026-08-22)

Owner order (ruling 5, docs/punctuation-kerning-audit-2026-08-22.md): render
real samples of poetry, blockquotes and nested lists at this reader's short
measure, plus the typographic-details check (em/en dashes, curly quotes,
ellipsis, non-breaking spaces in the line breaker); document everything; fix
only the indefensible.

Method: a scratch-generated 5-chapter EPUB (poetry with `<br>` stanzas, a
`<pre>` block, CSS-aligned verse; bare / publisher-styled / 2-level and
worst-case 3-level blockquotes with digit rulers; `ul`/`ol` nested 3 deep;
dash / quote / ellipsis / NBSP torture text; a 40-dash sweep paragraph),
rendered headlessly on the desktop **X3 build** (`pio run -e simulator_x3`,
792x528 logical, portrait page 528 wide, `screenMargin` 10 → 508 px text
measure), firmware settings `fontSize` 18, `fontFamily` 0, under BOTH
alignment settings (`paragraphAlignment` 1 = Ragged default, 0 = Justified).
Audited at working tree of eecac1780. Full measure at 18 pt: **~27 prose
chars / 20 digits per line**; em here ≈ 37 px (the 2 em element cap measured
75 px of indent on screen). All page screenshots taken before and after each
fix; the load-bearing ones are checked into
`docs/images/block-audit-2026-08-22/`. Card state was backed up
(`fs_/.crosspoint.qabak-blockaudit`, `settings.json.qabak-blockaudit`) and
restored.

Certainty: everything below marked "measured" was read off a rendered
screenshot of this build; code claims are cited to file:line of the current
tree.

---

## Fixed (the indefensible), each with its one-line justification

### F1. A line could begin with an em/en dash — FIXED

**Justification: a spaced dash must hang with the word before it; a
line-initial "— word" is wrong in every style manual and both breakers
allowed it.**

- Mechanism: the parser splits on whitespace, so "quiet — almost" tokenizes
  the dash as its own word with `continues=false` — an ordinary break
  opportunity. Both breakers took it. Measured before the fix: dash-initial
  lines under Ragged (`dash-ragged-before.png`, line 2 "— mmmmmmmm") and
  three of them under Justified (`dash-justified-before.png`), including a
  lone "—" heavily stretched mid-line-start.
- Fix: `startsWithLineForbiddenDash()`
  ([lib/Epub/Epub/ParsedText.cpp:299](../lib/Epub/Epub/ParsedText.cpp)) —
  U+2013 en dash, U+2014 em dash, U+2015 horizontal bar as a token's FIRST
  codepoint. The greedy breaker treats such a token like a continuation word
  and backtracks so the preceding word travels down with the dash
  (ParsedText.cpp:1071-1082); the DP breaker refuses to break after a word
  whose successor is dash-initial (ParsedText.cpp:899-903).
- Deliberately NOT included: the ASCII hyphen (ends hyphenated prefixes and
  plain-text bullets legitimately) and dashes INSIDE tokens ("1914–1918",
  "harbor—quiet" — single tokens, never line-initial via this path).
- Cost, accepted: the backed-off line is a word shorter; under Justified it
  stretches a little more (visible in `dash-justified-after.png`, still
  well-formed). The guard `currentIndex > lineStart + 1` means a line
  consisting of one word + dash still keeps the dash at position 2 rather
  than emptying the line — only reachable at absurdly narrow measures.
- After: zero dash-initial lines in either alignment
  (`dash-ragged-after.png`, `dash-justified-after.png`).

### F2. Accumulated nested insets pushed text OFF THE PANEL — FIXED

**Justification: three nested blockquotes at margin+padding 2 em each laid
text out at FULL width but painted it 444 px right — most of every line was
clipped off the panel edge, which is unreadable, not merely narrow.**

- Mechanism: `BlockStyle::fromCssStyle` caps each side of each ELEMENT at
  2 em ([lib/Epub/Epub/blocks/BlockStyle.h:16](../lib/Epub/Epub/blocks/BlockStyle.h)
  `MAX_HORIZONTAL_INSET_EM`), but `getCombinedBlockStyle` sums ancestors
  unbounded. At 3 levels x (2 em margin + 2 em padding) = ~888 px of total
  inset against a 508 px viewport, the `effectiveWidth` guard
  ([ChapterHtmlSlimParser.cpp:2613](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp))
  falls back to FULL width, while `placeLineOnPage` still applies the whole
  `leftInset()` as the paint x (:2538-2540). Measured before: quote text
  began at x=458 and ran to the clipped panel edge at x=527 — 4-character
  fragments visible, the rest gone (`nested-quote-before.png`).
- Fix: `clampAccumulatedHorizontalInsets()`
  ([ChapterHtmlSlimParser.cpp:69](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp)),
  applied at the two CSS block-style accumulation sites (:1528, :1582): the
  accumulated total is capped at 2/5 of the viewport, both sides scaled
  proportionally, so every block keeps at least 3/5 of the measure — ~16 of
  the ~27 chars per line at 18 pt. Measured after: worst-case quote at
  x=114, right edge 402, 13 digits / ~15-16 prose chars per line, fully
  on-panel (`nested-quote-after.png`).
- Deliberately NOT applied in `startNewTextBlock`/`makePages`: table cell
  placement encodes column x-offsets as margins
  (`emitBufferedTableAsColumns`, :727-733) and a rescale there would
  misplace every right-hand column. The clamp lives only where CSS
  accumulates.

Both fixes move pagination → **`SECTION_FILE_VERSION` bumped 40 → 41**
([lib/Epub/Epub/Section.cpp:70](../lib/Epub/Epub/Section.cpp)), the
established pattern; the partial-version sentinel derives automatically.

---

## Findings ranked, worst first (fixed and not)

1. **Nested-inset clipping** — FIXED (F2 above).
2. **Line-initial dash** — FIXED (F1 above).
3. **Lists are structurally flat** — documented, NOT fixed (not in the
   ordered indefensible set; it is a feature gap, not a collision).
   Measured: all three `ul` nesting levels and both `ol` levels render at
   x=14, identical to a plain paragraph (`lists-flat.png`). Three causes,
   all in the parser:
   - `ul`/`ol` are UNPROCESSED tags (:1700; `BLOCK_TAGS` at :47 is
     `p/li/div/br/blockquote`), so `ul.pub { margin-left:1.5em;
     padding-left:1.5em }` in the test CSS had ZERO effect — the style is
     resolved for known tags only, and list containers never join the
     block-style stack. Publisher CSS on `li` itself DOES work (li is a
     block tag).
   - There is no user-agent default indent for any element (deliberate — no
     UA stylesheet exists; bare `blockquote` is flat too, see 6).
   - Every `<li>` gets an inline "•" word (:1591-1593) — ordered lists lose
     their numbers, and wrapped item lines have no hanging indent (they
     return to the block's left edge under the bullet).
   Also observed: a bullet is orphaned on its own line when the item's
   first word does not fit after it (measured with an 80-char token; rare
   in prose). **Recommendation** if the owner wants list fidelity: add
   `ul`/`ol` to the block-style stack (so their CSS accumulates through the
   existing, now-clamped, machinery), give `li` a modest default left inset
   per depth plus a hanging indent for wrapped lines, and a counter for
   `ol`. That is a feature, priced beyond this audit's writ.
4. **Verse gains a half-line gap per `<br>` line** — documented, NOT fixed
   (debatable; touching it risks the <br>-per-paragraph CJK web-novel
   behavior the current code deliberately serves —
   ChapterHtmlSlimParser.cpp:1560-1582). Each `<br>` opens a new block, and
   every block boundary carries the default half-line gap
   (`makePages`/bottom-spacing, the v40-capped rhythm), so stanzas read
   ~1.5-spaced rather than single-spaced (`poetry-br-ragged.png`). Line
   INTEGRITY is perfect: every intentional break held, no unwanted joins,
   in both alignments. **Recommendation:** suppress the inter-block gap
   when the previous block was created by `<br>` AND is non-empty (a true
   line break, not a scene break) — the `fromBrElement` flag already
   distinguishes the cases.
5. **The epub's `text-align` never survives the user setting** —
   documented, NOT fixed (deliberate: the 2026-08-22 owner ruling made the
   row a two-option Justified/Ragged choice, and
   `BlockStyle::fromCssStyle` (BlockStyle.h:141-146) only honors CSS when
   `paragraphAlignment == None`, a value the settings row never produces).
   Measured: `text-align: center` on a verse line renders left; under
   Justified, a poem div's explicit `text-align: left` justifies its
   wrapped lines. Single-line verse blocks are SAFE either way — each is
   its own block's last line, and last lines never stretch
   (ParsedText.cpp `isLastLine`). Headers keep their own centering
   (separate path, :1521-1527). **Recommendation:** if book-respecting
   alignment is ever wanted, it is a third row option ("Book's style" →
   pass `CssTextAlign::None`), not a parser change.
6. **Bare `blockquote` is visually a plain paragraph** — documented, NOT
   fixed. Measured x=14, identical ruler count (20 digits). With no UA
   stylesheet this is consistent engine policy, and real EPUBs almost
   always style their blockquotes (the styled ones measured fine: 2 em cap
   → 75 px indent, 15 digits / ~19 prose cpl at one level, 12 digits at
   two levels — both above any indefensibility line). Flagged only so
   nobody re-audits it.
7. **`<pre>` loses its line structure** — documented, NOT fixed. `pre` is
   an unprocessed tag and `isWhitespace` (:55) folds `\n`, so a preformatted
   poem renders as one wrapped paragraph (measured, Poetry ch. p2). Rare in
   reading EPUBs; a fix needs white-space:pre handling in the tokenizer.
   Out of audit scope.

## Negative results (checked, correct, do not re-fix)

- **NBSP is honored, both directions.** U+00A0 and U+202F become a visible
  space token glued to both neighbors with `continues=true`
  (ChapterHtmlSlimParser.cpp:1785-1841); neither is in `isWhitespace`
  (:55), so the breaker never sees a break opportunity there — the
  backtrack loop (ParsedText.cpp:1071-1082) moves the whole group down.
  Measured: "10 km", "200 L", "32 m", "45 min", "Mr. Bell", "Dr. Marsh",
  "A. Lincoln", "snow goose" — dozens of wrap-adjacent instances, zero
  splits, in both alignments (`nbsp-names.png`). A break AFTER a
  non-breaking space was not observed anywhere. Hyphenation inside a
  NBSP group keeps the prefix attached and only frees the remainder
  (ParsedText.cpp:1160-1200) — by design.
- **Ellipsis:** "…" is part of its word's token; no break before it is
  possible. Hyphenation may split the carrying word ("ring-/ing…") —
  normal.
- **En dash ranges** ("1914–1918", "118–142"): single tokens, never split.
- **Curly quotes:** attached to their words (no whitespace between), so no
  orphaned open/close quote at a wrap in any sample; they are also in the
  justified right-edge half-hang set (`trailingHangWidth`,
  ParsedText.cpp:255-288).
- **Marker/text collisions:** none anywhere — the bullet is an inline word
  with a normal, kerned space gap.
- **Hanging punctuation** (justified only, right edge only, by design) and
  the v40 half-line gap cap behaved as documented in their own notes; no
  interaction with any fix here.
- **Poetry under Justified:** intentional short lines never stretch (each
  `<br>` block's only line is a "last line").

## Verification

- Firmware host suite: `ctest --test-dir build/test` → **367/369 passed**;
  the 2 failures (`EditorFontsTest`, `SettingDisplayOrderTest`) are the
  known pre-existing pair, unrelated to layout (neither compiles the files
  touched), and fail identically on the pristine tree.
  `ReadAloudCaptureTest`, `SdKernMeasure*`, `WordAnchor*`, `Hyphenation*`
  all PASS.
- Desktop canary `pio run -e simulator` (X4) and `simulator_x3`: green.
- Simulator repo `tests/run_all.sh`: **31 passed, 0 skipped**.
- Before/after screenshots for both fixes in
  `docs/images/block-audit-2026-08-22/` (dash: ragged + justified;
  nested quote: ragged; the justified nested-quote after-shot matched).
- Card state restored from the `.qabak` copies; the scratch epub and its
  section cache removed from `fs_`.

Nothing here was committed (per the order); the diff is
`ParsedText.cpp`, `ChapterHtmlSlimParser.cpp`, `Section.cpp`, this file and
`docs/images/block-audit-2026-08-22/`.
