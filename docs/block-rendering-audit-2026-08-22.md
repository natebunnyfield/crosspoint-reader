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
3. **Lists are structurally flat** — **FIXED 2026-08-22** (owner-approved:
   "Numbers + hanging indent"; see the addendum at the end of this file for
   the implementation and measurements). The original finding, kept for the
   record:
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

---

## Addendum 2026-08-22 — finding 3 FIXED: numbers + hanging indent

Owner-approved follow-up to finding 3 ("Numbers + hanging indent"). Same
method as the audit proper: scratch-generated EPUB (nested `ul`/`ol` 3-5
deep, long wrapped items, `ol start="5"` with `li value="10"`, publisher-CSS
lists, and a measurement chapter whose every line begins with the same glyph
so ink-edge x is bearing-identical), rendered headlessly on the X3 build at
fontSize 18 / fontFamily 0 under BOTH alignment settings, plus a Line Grid ON
spot check. All claims below marked "measured" were read off those renders;
screenshots in `docs/images/block-audit-2026-08-22/lists-*-after.png`
(`lists-flat.png` is the before).

### What changed (all in the parser; pagination → SECTION_FILE_VERSION 41 → 42)

- **`ul`/`ol` joined `BLOCK_TAGS`**
  ([ChapterHtmlSlimParser.cpp:47](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp)),
  so publisher CSS on list containers now accumulates through the existing —
  clamped — block-style machinery, exactly the blockquote pattern. A list
  container with NO publisher left inset gets a default 1.5 em step per level
  (`LIST_INDENT_STEP_EM`, :57): 1.5 em ≈ 56 px at 18 pt swallows a
  three-digit "999. " gutter, and three levels still leave ~18 chars of the
  ~27-char measure. A list-context stack (`ListContext { ordered, nextValue }`,
  ChapterHtmlSlimParser.h:222) carries depth + counter; pushed at `<ul>`/`<ol>`
  (:1617), popped at the close tag, so nested `ol` numbering restarts per level.
- **`ol` items get real decimal numbers** — `start` on `ol` and `value` on
  `li` honored when present (lowercase, as XHTML mandates; parsed with
  `strtol`, non-numeric ignored). Plain decimal only: the CSS parser has no
  `list-style-type`, so roman/alpha are not available.
- **Hanging indent** — the marker ("1. " / "• ") is an ordinary word carrying
  its own trailing space; the item block gets `textIndent = -advance(marker)`
  (:1663-1665), so the first line starts in the gutter and every wrapped line
  returns to the item's text edge. The item's first text word is GLUED to the
  marker (`listMarkerGluePending_`, consumed in `flushPartWordBuffer` :308,
  surviving the source-whitespace reset after `<li>`): under Justified the
  marker→text gap is a continuation advance, never a stretchable inter-word
  gap — and the breaker can no longer orphan a marker at a line's end (the
  audit's "bullet orphaned on its own line" observation). The gutter math
  relies on kern(x,' ') = kern(' ',x) = 0, which
  `SdKernMeasure.SpaceAdvanceStaysUnkernedForShippedFonts` pins for every
  shipped font. If the accumulated inset is narrower than the gutter
  (publisher `margin-left: 0`, or a bare `<li>`), the item's `marginLeft` is
  widened so the marker cannot paint off-panel (:1676-1683).
- A list container strips inherited `text-indent` (:1670-1674), so an
  ancestor paragraph's first-line indent — or an outer item's hanging indent,
  for a list nested in `<li>` — never leaks onto items.

### Measured (X3, 792x528, 18 pt, em ≈ 37 px, line height 45 px)

- **Hanging alignment is EXACT, both alignments.** Measurement chapter
  (every line "HHHH…"): ul L1 first-line text x=71, wrapped lines 71/71/71;
  ul L2 126, wrapped 126×3; ol "1." text 71, wrapped 71 — identical columns
  under Ragged and Justified (`lists-measure-justified-after.png`). Under
  Justified the inter-word gaps stretch while the marker gap does not.
- **Numbers correct at every level**: outer 1/2/3 with inner 1/2/3 and
  third-depth 1/2, outer numbering CONTINUING after the nest closes
  (`lists-ol-nested-after.png`); `start="5"` → 5, 6; `value="10"` → 10, then
  11 (`lists-ol-start-value-after.png`).
- **Per-level step**: text edge 71 → 126 → 181 (55-56 px ≈ 1.5 em per level).
- **The cap engages at depth 4 (18 pt)**: 4 × 56 px exceeds the 2/5-viewport
  clamp (203 px of 508), so depth 4 lands at text x=214 (= margin 10 + 203 +
  bearing) and depth 5 renders at the SAME x — clamped, not crushed, ~16
  chars of measure kept (`lists-depth-cap-after.png`). Max depth with full
  distinct steps at 18 pt is 3.
- **Publisher CSS honored**: `ul { margin-left:1.5em; padding-left:1.5em }`
  measured at text x=124-126 (= 10 + 3 em); `ol { margin-left:0 }` keeps its
  "1." at the page margin via the gutter bump, text x≈48.
- **Marker widths are per-item**: a two-digit "10." whose 60 px advance
  exceeds a level-1 list's 56 px inset bumps that item's text edge 4 px
  deeper than its one-digit siblings (71 → 75). Wrapped-line alignment stays
  exact per item; a common right-aligned marker column would need lookahead a
  single-pass SAX parse does not have. Accepted.
- **Interplay, verified not rebuilt**: half-line gap between items (67 px
  line pitch = 45 + 22, the existing cap — items are blocks, nothing
  list-specific added); widow/orphan keeps the 2/2 rule on a wrapped item
  split across pages (4 lines / 2 lines measured); Line Grid ON snaps item
  lines to the 45 px grid.

### Verification

- `ctest --test-dir build/test`: **367/369**, the 2 failures the known
  pre-existing pair (`EditorFontsTest`, `SettingDisplayOrderTest`);
  `ReadAloudCaptureTest` and `SdKernMeasure*` PASS.
- Desktop canaries `pio run -e simulator` and `simulator_x3`: green.
- Simulator repo `tests/run_all.sh`: **34 passed, 0 skipped**.
- Card state backed up (`.qabak-lists`) and restored; scratch epub and its
  section cache removed from `fs_`.
