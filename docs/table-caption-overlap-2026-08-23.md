# The table `<caption>` that printed under its own header row

**Date** 2026-08-23 · **Reported** by the owner, with a screenshot of a
three-column Catalan phrasebook table on a dark page: `The middle of`,
`English`, `hello` and `Say it` drawn on top of each other on one line, and
`Catalan` fallen to the line below. Body rows underneath were correct.
**Surveyed at** `ef33faef4`. **Status** fixed; reproduced and re-rendered in the
desktop simulator before and after.

## 1. What the screenshot actually showed

The words that looked like stray body text were not body text. The book is
`catalan-the-whole-evening.epub` (built by `~/src/claude-tools`), chapter
`OEBPS/04-courtesy.xhtml`, and the table is marked up like this:

```html
<table>
  <caption>The middle of the hello</caption>
  <thead><tr><th>Catalan</th><th>English</th><th>Say it</th></tr></thead>
  ...
```

`The middle of ... hello` is the **caption**. `English` and `Say it` were
printed over the middle of it, and the header's own first cell, `Catalan`, was
one line lower than its two siblings. One defect, two symptoms — not a
horizontal collision plus a separate vertical one.

## 2. Mechanism, traced

`grep -rn caption lib/Epub/` returns nothing: `<caption>` is not handled
anywhere. It is therefore an unprocessed tag
(`ChapterHtmlSlimParser.cpp:1901`, "Unprocessed tag, just increasing depth"),
and its text reaches `characterData`.

1. `characterData` diverts text into the buffered-table path only while a CELL
   is open — `if (self->tableBuffering && self->tableCellOpen)`
   (`ChapterHtmlSlimParser.cpp:1928`). A caption is inside `<table>` but outside
   any cell, so it falls through and takes the ordinary streaming route into
   `currentTextBlock`. Nothing lays it out; it is still **pending** at
   `</table>`, and has consumed **no vertical space**.
2. `</table>` plans the columns and calls
   `emitBufferedTableAsColumns(plan)` (`ChapterHtmlSlimParser.cpp:2289`).
3. That emitter takes the first row's top edge from the page cursor —
   `const int16_t rowTop = currentPageNextY;` — while the caption is still owed
   a place at exactly that y.
4. For column 0 it calls `startNewTextBlock(cellStyle)`. Because
   `currentTextBlock` is non-empty, that reaches `makePages()`
   (`ChapterHtmlSlimParser.cpp:387`), which lays the **caption** out at
   `rowTop` and advances the cursor past it. Column 0's own text — `Catalan` —
   therefore lands one line lower. That is the fallen cell.
5. Every later column begins with `currentPageNextY = rowTop;` — straight back
   on top of the caption. That is `English` and `Say it` over
   `The middle of the hello`.

The rotated emitter (`emitBufferedTableRotated`) had the same seam pointed the
other way: it completes the current page before drawing, so a pending caption
surfaced on the page **after** the table it names.

## 3. The SD step-down lead was NOT the cause

A strong lead was open when this started: `getSmallestReaderFontId()`
(`src/CrossPointSettings.cpp:376`) loops point sizes asking `sdFontIdResolver`
for a smaller cut, but `SdCardFontSystem::resolveFontId` ignores its
`pointSize` argument by design (one loaded reader-size font, for RAM), so for
an SD family it returns the same id as the reader font and the wide-table
step-down does nothing — in every shipped configuration.

**That is true, and it is not this bug.** `smallFontId` is read in exactly one
place, `emitBufferedTableRotated` (`ChapterHtmlSlimParser.cpp:605`, via
`tableFontForRotation()` at `ChapterHtmlSlimParser.h:205`). The screenshot is
the **upright columns** path, which is not the rotated one. That path measures
and draws with the SAME font at every step:

* the `</table>` measure lambda uses `self->fontId` (`:2270`)
* `measureRowHeight` uses `fontId` (`:763`)
* `emitBufferedTableAsColumns` takes its line height from `fontId` (`:782`)

So no seam between measurement and drawing exists here. The step-down remains a
documented no-op and an open size-versus-RAM question for the owner; it is
recorded where it lives (`ReaderRenderSpec.h`, `CrossPointSettings.cpp:376`)
and was not touched.

## 4. Reproduction

Desktop simulator, `pio run -e simulator_x3` (X3, 528x792 portrait viewport,
render scale 1), FiraSansBook at **12 pt**, `screenMargin` 5,
`CROSSPOINT_SIM_DARK=0`. Book: a one-chapter EPUB carrying
`04-courtesy.xhtml` verbatim (`fs_/books/table-caption-repro.epub`), so the
page is reachable without navigation guesswork.

```bash
CROSSPOINT_SIM_INPUT_SCRIPT='2000:QTAP:RIGHT;3000:QTAP:RIGHT;...;14200:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='1800:p00.bmp;2800:p01.bmp;...' \
SDL_VIDEODRIVER=dummy CROSSPOINT_SIM_DARK=0 .pio/build/simulator_x3/program
```

Page 4 of the chapter is the owner's page, and the pre-fix render is his
screenshot: `The middle of` / `English` / `hello` / `Say it` on one line,
`Catalan` below, and the identical six-line wrap of "how are you? (informal —
right for a friend)" underneath. Two more tables in the same chapter
("The politeness layer", "Farewells") show the same defect.

**A fixture now carries this**, because the phrasebook only shows it at 12 pt:
`test/epubs/test_table_caption.epub` has two captioned tables — one with short
cells that plans as columns at the DEFAULT 14 pt, and the wide five-column one
that rotates. Rendered against the pre-fix tree it shows the bug at the default
size ("Gloss" and "Say it" through the caption, "Term" fallen below); after the
fix both captions sit on their own lines, and the rotated table's caption lands
on the page BEFORE it rather than after.

**Size matters only because it decides which path the table takes.** At 14 pt
the same tables fail `planColumns` and go to the key-block fallback, which
calls `startNewTextBlock` first and therefore never had the bug. Render scale
does NOT change this: built at `CROSSPOINT_RENDER_SCALE=2` the 14 pt tables
still flatten. The hypothesis that the absolute-pixel constants
(`kMinColumnWidth = 48`, `kColumnSlack = 2`, the gutter's floor of 12) make the
phone accept tables the device rejects was **measured and refuted** — the
binding floor is `widestWord`, which scales with the font, so the ratio is
unchanged.

## 5. The fix

Two parts. The first removes the cause; the second makes the failure mode
unreachable rather than merely absent.

**a. Retire the pending block before a table takes the page cursor.**
`ChapterHtmlSlimParser::retirePendingBlockBeforeTable()` flushes any part-built
word and, if the pending block still holds text, lays it out where it stands
and starts a fresh one. `emitBufferedTableAsColumns` calls it first thing;
`emitBufferedTableRotated` calls it at the first point emitting is allowed —
below its last `return false`, so that function's own "nothing is emitted on a
false return" contract still holds. The caption becomes an ordinary paragraph
directly above the table, which is what the flattened and key-block paths
always did with it.

**The successor block is given a NEUTRAL `BlockStyle()`, and that line is the
whole fix's sharpest edge.** The argument does not touch the caption —
`makePages()` reads the style off the block it is retiring — it dresses only the
EMPTY block left behind, and the next thing to open that block is the table's
first cell. An empty block is REUSED rather than flushed, so whatever it carries
merges into that cell's style: a container's `marginTop`/`paddingTop` pushes
column 0 down, and a `<br>`-flagged block arms a full line of `sceneBreakLift`
for it. Either one puts column 0 below columns 1..n — the reported bug,
reintroduced from inside its own fix. The first version of this change carried
the retired block's own style and did exactly that; see §7.

**A `<li>` whose whole content is a table is retired too**, by clearing
`listItemBulletOnly` first. Otherwise `startNewTextBlock` merges the list marker
into the table's first cell, where it wraps inside a column planned for the
cell's own text — bullet on line 1, cell on line 2, every other column on line
1. Same broken shape, different cause, and PRE-EXISTING: confirmed by rendering
the fixture against the pre-fix tree before touching it.

**b. State the rewind rule as a pure function, with its preconditions.**
`tablecolumns::columnStartY(rowTop, cursorY, rowTopIsClear, samePageAsRowStart)`
(`TableColumnLayout.h`) is now the only place the per-column rewind is decided.
It returns `rowTop` only when both preconditions hold and `cursorY` otherwise —
so the answer can never be ABOVE the page cursor unless nothing has been drawn
between the two. `planColumns` already made horizontal overlap impossible via
the `widestWord` floor; this is the vertical half, which was missing.

The second precondition is a bug that had not been reported yet: a cell that
overflows and completes a page leaves `rowTop` naming a y on a **finished**
page, and the old code rewound to it anyway. The row-height pre-check makes
that rare, not impossible — it measures with `renderer.wrappedText` while the
real layout runs the full `ParsedText` engine. The same commit stops
`rowBottom` mixing y values from two different pages.

**Files**

| File | Change |
|---|---|
| `lib/Epub/Epub/parsers/TableColumnLayout.h` | `columnStartY()` — the rule and its preconditions |
| `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.{h,cpp}` | `pendingTextIsUnlaid()`, `retirePendingBlockBeforeTable()`, both emitters, the rewind, `rowBottom` |
| `test/epubs/test_table_caption.epub` | new render fixture: five captioned tables — columns at the default size, wide/rotated, after a `<br>`, inside a margin-bearing `<div>`, and inside a `<li>` |
| `lib/Epub/Epub/Section.cpp` | `SECTION_FILE_VERSION` 47 → 48 (layout change: the caption now costs lines) |
| `test/table_columns/TableColumnLayoutTest.cpp` | four tests, including a swept property |

## 6. Verification

* `pio run -e simulator_x3` — SUCCESS.
* `pio run -e gh_release` — SUCCESS. Flash 4,989,369 / 6,553,600 (76.1%), RAM
  54,564 (16.7%). Measured against the same build at `HEAD` with the four files
  reverted: 4,989,221 → **+148 bytes**, RAM unchanged.
* Host tests: **486 total, 483 pass, 3 fail** (was 482 total, 480 pass, 2 fail).
  The four added tests all pass. None of the three failures is in this area:
  `EditorFontsTest` and `SettingDisplayOrderTest` are the known iA Writer
  Quattro pair, and the third,
  `SdFontArrows.EveryInstalledFamilyDrawsRealArrowGlyphs`, is a CARD-state
  failure — it reports the installed `.cpfont` files as stale and names its own
  remedy, `python3 scripts/install-sim-fonts.py`. It appeared during this
  session while concurrent work was regenerating `fs_/fonts`, and no file in
  this change is compiled into that suite.
* Renders: five before/after pairs, PNG at native pixels plus 3x
  integer-NEAREST crops — the owner's page, the fixture at the default size,
  the `<br>` regression the fix itself introduced, and the `<li>` case. Delta
  on the owner's crop: mean 20.5 levels, max 209, 78.0% of pixels changed by
  more than 4 levels; coverage 29.3% before, 26.6% after. Index and per-figure
  numbers in the render set's own `README.md`.
* No regression on the two table fixtures: `test/epubs/test_tables.epub`
  (flattened / key-block forms) and `test/epubs/test_wide_table.epub` (the
  rotated page) both render as before.

## 7. What adversarial review changed

A read-only refuting pass ran over the finished diff before it shipped, and it
earned its place: it found a defect in the fix that the fix's own reproduction
could not have shown.

**F1 — the fix reintroduced the bug it was fixing, in a case the repro did not
cover.** `retirePendingBlockBeforeTable()` originally carried the retired
block's own style onto the successor block. That block is EMPTY, so the first
cell's `startNewTextBlock` reuses rather than flushes it and merges the carried
vertical style into the cell — pushing column 0 below its siblings whenever the
caption's block came from a margin-bearing container or a `<br>`. **Confirmed by
render before it was believed:** `<div>a<br/>b<table>` printed `Term` exactly
one line under `Gloss` and `Say it`. Fixed by passing a neutral `BlockStyle()`,
and the fixture now carries both shapes.

**F2 — `retirePendingBlockBeforeTable()` silently failed its own postcondition
for `<li><table>`,** because `startNewTextBlock` takes a bullet-only early
return that lays nothing out. Chasing that turned up a SEPARATE, PRE-EXISTING
defect with the same broken shape: the marker was merged into the first cell and
wrapped inside a column planned for the cell's own text. Verified present on the
pre-fix tree, then fixed by clearing the flag so the marker takes its own line.

**F3 — a stale contract.** `emitBufferedTableRotated`'s header promises
"nothing is emitted on a false return", and the first version of the fix
retired the pending block above three later `return false`s. No caller was
harmed (the fallback would have flushed the same block at the same point), but
the comment is what the next change will trust — so the call moved below the
last `return false` instead of the comment being softened.

Reported CLEAN and re-checked here: the `BlockStyle` copy is a real copy
(`ParsedText.h:91` returns a reference; `BlockStyle` is all scalars); anchors,
footnotes, `chapterSourceBytes_` and `wordsExtractedInBlock` accounting are
unchanged; `completedPageCount` is incremented at every `completePageFn` call
and nowhere else, so it is a sound page witness; the `rowBottom` change is
strictly better than the `max` it replaces; and `SECTION_FILE_VERSION` is the
right and sufficient cache key, with `SECTION_FILE_PARTIAL_VERSION` derived from
it.

**Known coverage gap, stated rather than papered over.** No host test links
`ChapterHtmlSlimParser` — `grep -rln ChapterHtmlSlimParser test/` returns
nothing, and `test/epubs/` is not referenced by any CMakeLists. So the four new
tests cover `columnStartY` only; `retirePendingBlockBeforeTable`, the per-row
derivation of `rowTopIsClear`/`rowStartPage`, and the `rowBottom` change are
covered by RENDER alone, against `test/epubs/test_table_caption.epub`. Building
a parser-level host harness (it needs `Epub`, `HalFile` and expat) is a real
piece of work and is not in this change.

## 8. Checked and found clean

* The flattened path (`emitBufferedTableFlattened`) and the key-block path
  (`emitBufferedTableKeyBlock`) both open with `startNewTextBlock`, so a pending
  caption was already retired in the right order. Unchanged.
* `abandonTableBuffer()` emits flattened output mid-parse and inherits that
  same ordering. Unchanged.
* `planColumns` itself is not implicated: its `widestWord` floor was already
  holding, and the columns in the report were at their correct x.
* No `BOOK_CACHE_VERSION` bump is needed. Nothing the metadata pass DECIDES —
  spine, TOC, book notes raised by `Epub::load()` — changes here;
  `TablesFlattened` is raised during chapter parse, which the section version
  already invalidates.
