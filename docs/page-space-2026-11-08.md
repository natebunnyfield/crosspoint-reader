# Space above and below the text: 1 unit above, 2 below

**Owner ruling, 2026-11-08, verbatim:** *"there should be a note about the space
above text on a page is 1 unit and below is 2 units."* It came with two
screenshots of a chapter opening (`one-small-block.epub`, chapter 3) whose first
line sat about a fifth of the way down the page, and the instruction to remove
that space.

## What the rule means in pixels

**The unit is the Screen Margin setting** (`SETTINGS.screenMargin`). The
panel's viewable margins (`GfxRenderer::VIEWABLE_MARGIN_*`, 9/3/3/3 in
portrait) are pixels hidden behind the bezel, and they rotate with the
orientation (`GfxRenderer::getOrientedViewableTRBL`), so they are not part of
the white a reader sees. The first line's cap ink is painted exactly at
`viewable top + screenMargin` (the paint-time cap-ink trim,
`EpubReaderActivity::renderContents`), so the visible space above the text is
exactly one `screenMargin`.

| | before | after |
|---|---|---|
| above the text | `screenMargin` | `screenMargin` (unchanged) |
| below the text | `screenMargin` | **`2 × screenMargin`** |
| a section's first page | began `viewportHeight / 5` down (chapter sinkage) | **begins at the top, like every page** |

Where it lives:

- `src/activities/reader/EpubReaderActivity.cpp`, the margin block:
  `orientedMarginBottom += 2 * SETTINGS.screenMargin`.
- `src/activities/reader/TxtReaderActivity.cpp`, the same line for plain text;
  its `CACHE_VERSION` 5 → 6, because its cache checks the margin setting but
  not the inset derived from it.
- `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`: `newPageStartY()` and
  `kChapterSinkageFraction` deleted; every fresh page starts at `y = 0`.
  `SECTION_FILE_VERSION` 66 → 67 (`lib/Epub/Epub/Section.cpp`).

## What it supersedes

The 2026-08-22 ruling *"include chapter drop as the only exception to collapse
space above rule"* created the chapter sinkage. This ruling ends that
exception: space above text now collapses on every page, chapter openings
included (`docs/typography-possible-2026-08-25.md` §3.7 carries the history).

## Limits, stated plainly

- **The 2 units below are a minimum, not an exact amount.** Pages hold whole
  lines, so whatever is left after the last line that fits is added below it,
  anywhere from 0 to just under one line. A short last page of a chapter is
  short by however much text there is. Making the bottom exact would mean
  stretching the space between lines page by page, which nothing here does.
- **At Screen Margin 0 both are 0**, so the rule holds trivially and the text
  meets the bezel edge as before.
- **The space a book's CSS asks for above a heading** (for example
  `h1 { margin-top: 1.2em }`) was already dropped at the top of a page by the
  2026-08-22 page-top collapse, and still is.
- The XTC reader (pre-rendered pages) is untouched: its pages are images laid
  out by whoever made the file.

## Proof (2026-11-08, simulator, before vs after)

`one-small-block.epub` in the desktop simulator (`pio run -e simulator`), Screen
Margin 15, SD font Edgar, dark mode; before = `origin/main` `f40cc4d85`, after =
this change. Ink measured on the 480×800 framebuffer, minus the 9/3 px hidden
behind the bezel:

| page | before: above / below | after: above / below |
|---|---|---|
| chapter 1 opens | 127 / 65 | 13 / 109 |
| chapter 2 opens | 127 / 299 | 13 / 413 |
| a full body page | 13 / 47 | 13 / 46 |

Above is 13 px against a setting of 15: the first line is placed by cap height,
and ascenders (f, l) rise slightly above it. Below on a full page is the 30 px
minimum plus the partial line left over (16 px here), close to the old 15 + 32.
The chapter 2 opening ends early in both runs (heading kept with its text); that
break is not changed by this work. Rendered page with the screenshots: Artifact
`https://claude.ai/artifact/17p6XaZUJKoXiBTwk93ZnV`, which REPLACED the old
"Albo 1.9" page at that URL (artifact limit; Albo 1.9's record stays in
`~/src/albo/docs/albo-1.9-2026-10-18.md`).

Tests: `TableKeepTogetherTest` (14), `CascadeLinesTest` (6),
`DefinitionListTest` (16), `RotatedTextTest` (8), `TocAnchorPageTest` (7),
`ChapterJumpTest` (3) all pass. Two table tests leaned on the sinkage and were
reworked: the row-separator count is now derived from the layout, and the
keep-together fixture's rows are one sentence longer. Removing the sinkage also
exposed a real bug, fixed here with a failing-first test
(`TableRowSeparator.SitsBetweenTheRowsNotInsideOne`): on a streamed flat table
every row separator was drawn between the previous row's LAST TWO cells,
because that row's last cell was still unlaid when the rule went down.
