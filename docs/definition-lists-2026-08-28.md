# Definition lists: `<dl>`, `<dt>`, `<dd>`

2026-08-28. Owner report, a quiz book (`triviaaimed.epub`, 18 `<dl>`, 255
`<dt>`, 510 `<dd>` — exactly two definitions per term, the second always
`<dd class="why">`). Surveyed against firmware `main` at `af41a6374`;
every finding below is verified against source or against a render.

## The mechanism, confirmed

`dl`, `dt` and `dd` appeared **nowhere** in `lib/Epub/`. They were not in
`BLOCK_TAGS` (`ChapterHtmlSlimParser.cpp:50` as it stood), so `isHeaderOrBlock`
answered false for all three and `startElement` fell through to its last
branch — `else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name))`, the
INLINE branch. No block opened, no line broke, and the whole list ran together:

> "It was the best of times, it was the worst of times" opens which
> novel?**A Tale of Two Cities (Dickens, 1859)**The two cities are London and
> Paris during the French Revolution…

Reproduced headlessly and rendered: `docs/images/definition-list-before.png`.

There was not even a SPACE between them, and that is the same cause seen at the
word level rather than the block level: `endElement`'s flush logic sets
`nextWordContinues = true` when it closes an INLINE tag, on the reasoning that
`</b>` in the middle of a word must not break it. `</dt>` took that branch, so
"novel?" and "A" were glued into one visual word. Being in `BLOCK_TAGS` puts all
three closers on the `headerOrBlockTag` side of that test, where `isInlineTag`
is false and no glue is set.

**What was already working, and is why the answer is bold in the screenshot.**
That inline branch reads `font-weight` off the resolved CSS and pushes an inline
style entry, so the book's `dt { font-weight: bold }` was honored the whole
time. Only the BLOCK half of a definition list was missing. Worth stating
plainly because it is the thing that makes the bug look like a styling problem
when it is a structural one.

## What the CSS layer already gives, and what it does not

Checked before writing any layout code, because a fix that hardcoded an indent
the book was already supplying would misrender every book whose `dd` margin
differs. The reported book's `OEBPS/style.css`:

```css
dl { margin: 0.8em 0; }
dt { font-weight: bold; margin-top: 0.9em; }
dd { margin: 0.25em 0 0.25em 1em; }
dd.why { color: #555; font-size: 0.92em; border-left: 2px solid #ccc;
         padding-left: 0.6em; margin-top: 0.35em; }
```

`CssParser` supports element selectors, class selectors and `element.class`
(`CssParser.h`, "Supported selectors"), so every one of those rules resolves.
`CrossPointSettings::embeddedStyle` is a `constexpr 1`, so a book's stylesheet
is always live — there is no setting that turns this off.

| Declaration | Honored? | Where |
|---|---|---|
| `dl { margin: 0.8em 0 }` | yes, subject to the half-line inter-block gap cap | `BlockStyle::fromCssStyle` → `makePages` |
| `dt { font-weight: bold }` | yes — and was, before this fix | inline style entry |
| `dt { margin-top: 0.9em }` | yes, capped at half a line | `makePages` top pass |
| `dd { margin-left: 1em }` | **yes, and it DECIDES the indent** | see below |
| `dd.why { padding-left: 0.6em }` | yes, adds to the left inset | `BlockStyle::leftInset()` |
| `dd.why { font-size: 0.92em }` | **no** — one reading face per page, no per-block size | — |
| `dd.why { color: #555 }` | **no**, and meaningless on a 1-bit panel | — |
| `dd.why { border-left }` | **no** — no border rendering exists | — |

Nothing was added for the last three, deliberately. The report is about the
block break and the indent; extending the renderer for a color a monochrome
panel cannot show, or a rule nobody asked for, is scope this fix does not take.

## The fix

Three tags into `BLOCK_TAGS`, and one rule for the indent.

- **`dl`** is a container in the sense `<ul>` is: it takes the container
  text-indent reset (an ancestor paragraph's first-line indent must not leak
  onto the items) and **no step of its own**. A step on the `dl` would indent
  the term as well, and the term's position relative to its definition is the
  entire relationship.
- **`dt`** is an ordinary block. No marker, no synthesized bold, no invented
  treatment — a definition list has none, and the book that wants a bold term
  says so in its own CSS, as this one does.
- **`dd`** takes `DEFINITION_INDENT_STEP_EM` (1.5 em, the same constant
  `<ul>`/`<ol>` use) **only when the publisher gave it no left inset of its
  own** — `!cssStyle.hasMarginLeft() && !cssStyle.hasPaddingLeft()`, exactly the
  test the list container has always applied. So the reported book's `1em` wins,
  an explicit `dd { margin: 0 }` wins, and a book that ships a `<dl>` and styles
  nothing still gets an indent. `padding-left` is in the test as well as
  `margin-left` because a book that rules a note off with `border-left` states
  its offset as padding — `dd.why` does exactly that, and double-indenting it
  would invent a nesting level the document does not have.

Not the browser UA's 40 px (~2.5 em): this measure is ~27 characters at 18 pt
and 2.5 em of it is a quarter of the line.

## Keep-together: DECIDED, and what was decided

**A `<dt>` keeps with ROOM FOR ITS DEFINITION TO START — three lines of it.**
Not with the whole `<dd>`.

The stranded-term defect is the stranded-table-header defect one element over —
the reader turns the page carrying the term and finds what defines it overleaf —
and it gets the same shape of answer as `breakBeforeStrandedTableHeader`:
measure the block that is about to be laid out, and if it would end the page,
end the page before it instead (`breakBeforeStrandedTerm`, called from the top
of `makePages`).

What differs, and why the rule cannot be the whole group: **the table path has
its rows buffered and the `<dl>` path is streamed.** A term is laid out at
`</dt>`, before anything after it has been read, so its definition's height is
not a number anyone has at that moment. Buffering a whole `<dl>` the way
`tableBuf` buffers a table is not on: a table is bounded by
`tablecolumns::kMaxBufferedBytes` and a chapter-length definition list is not.

### Why three lines, and not the classical one

**The first version of this shipped the classical typesetter's rule — one line
of what follows must fit — and it did not work.** Found by adversarial review
the same day, reproduced independently before acting on it. This layout engine
runs **widow/orphan keep-2/2** (`addLineToPage`, `flushPendingLines`) on the
`<dd>` *after* the term's keep has already declined to break. So the room a
definition needs before any of it can land is a function of its length:

| `<dd>` length | room it needs | why |
|---|---|---|
| 1–2 lines | 1 line | `flushPendingLines` exempts short paragraphs outright |
| 3 lines | 3 lines | all-or-nothing — a 2/1 split widows, a 1/2 orphans |
| 4+ lines | 2 lines | orphan control: the first line needs a second under it |

The term is laid out before the definition has been read, so the requirement is
the worst case. Measured strand rate, 41 page alignments per cell, 600 px
viewport, `test/definition_list`:

| room demanded | 1-line `<dd>` | **3-line `<dd>`** | 4+ line `<dd>` |
|---|---|---|---|
| none (no keep) | 3/41 | 7/41 | 4/41 |
| 1 line — *the rule that shipped first* | 0/41 | **4/41** | 1/41 |
| 2 lines | 0/41 | **3/41** | 0/41 |
| **3 lines — `kKeepRoomLines`** | **0/41** | **0/41** | **0/41** |

Three is the only value that reaches zero everywhere, and the middle column is
the one that matters: at the device measure every definition in the reported
book is three lines or more.

**What it costs, measured rather than asserted.** Same sweep: on a one-line
definition the keep spends **3 extra pages per 41 alignments**; on a definition
of 16 words or more the page count is **identical to no keep at all**. On the
owner's own book, paged end to end, the strict rule costs **zero** pages — 68
both ways. The over-break case is a one-line definition with one or two lines of
room left, which spends white space at a page foot; that is the direction this
whole pass is allowed to err in.

### The two refusals, and the one case it cannot see

Three refusals mean "breaking would not help": an empty page (nothing above to
strand the term against, and the break would publish a blank page); a term that
already overflows this page (the natural break is moving it anyway); and a term
that would not fit a fresh page with its room (it would be stranded there too).

**A `<dt>` that nothing follows is kept anyway**, because "is a definition
coming" is unanswerable at `</dt>`. When such a term lands at a page foot it
opens the next page, so a short page is spent. Accepted on two grounds: it only
fires on invalid markup (a `<dl>`'s content model is one-or-more `<dt>` followed
by one-or-more `<dd>`, so a list ending on a term is malformed), and the
reported book has none. Pinned by
`DefinitionListKeep.ATrailingTermIsKeptAnywayAndCostsAtMostAShortPage`.

**A term longer than `kMaxTermLines` (3) gets no keep at all.** That ceiling is
a heap guard — it bounds the probe copy — but it is also a limitation of the
feature, and the two should not be confused: at the ~27-character device
measure a long quiz question runs to five or six lines and is simply not kept.
Raising it costs a larger probe copy on a fragmented C3 heap. Measured, 1-line
definition, 31 alignments: terms of 1/2/3 lines strand 0 times, terms of 4/5/6
lines strand 2 times each — exactly the ceiling, working as designed.

`finishParse()` clears the flag before its trailing `makePages()`. That covers
only a chapter whose `</dt>` never arrived — truncated or unclosed — since a
well-formed `</dt>` retires its block long before. It is not a fix for the
trailing-term case above.

### The fit measurement is asymmetric, deliberately

The probe tracks two heights. `linesHeight` is what `placeLineOnPage` actually
spends — the full leaded advance of every line, snapped after each — and it is
what the cursor arithmetic uses. The **fit** test instead uses
`heightBeforeLast + lineFitExtentOf(last)`, because a page's last line fits by
its INK extent, which for the built-in faces is up to a whole line-height
smaller than the advance. Measuring the fit with advances refuses keeps the real
layout would have accepted — and that refusal is a SPLIT, not white space, so it
is not on the safe side of the trade. (The first version got this wrong in both
places; adversarial review, F2.)

## `xpathListItemIndex`: deliberately not touched

`<li>` increments it; `<dd>` does not. The counter is written into the section
LUT (`Section.cpp:789`) and **read nowhere** — grepped across `src/`, `lib/`,
`tools/` and `test/`, the only hits are the write and the struct field. Inventing
a counting rule for terms and definitions now (do both count? separately?) would
be speculation baked into a cached file format. If a CFI-style consumer ever
appears, that is when the question has an answer.

## Cache version

`SECTION_FILE_VERSION` 54 → **55**. Three pagination changes ride the one bump:
the tags open blocks where they were inline, so lines break differently; a `<dd>`
with no publisher inset narrows its measure and rewraps; and the term keep
completes pages early. A section served from a v54 cache is never parsed again,
so without the bump the reader who sent the screenshot would go on seeing
exactly that page.

## Verification

- `test/definition_list/` — 16 cases, real parser over real XHTML on disk, one
  case loading a real `CssParser` from the reported book's own stylesheet.
  **9 of them fail against the pre-fix tree.** The keep cases additionally fail
  against the one-line rule that shipped first, which is the point of sweeping
  DEFINITION LENGTH and not just page alignment: the original fixture gave
  every `<dd>` a single line, and that is the one length at which the keep
  cannot fail. Both keep cases sweep with **Line Grid on** as well, because
  every height in `breakBeforeStrandedTerm` passes through a snap that is a
  no-op with the grid off and the probe accumulates advances RELATIVELY where
  `placeLineOnPage` snaps the ABSOLUTE cursor.
- **The whole book, paged end to end on the fixed build**: 68 pages from the
  cover to the last entry of the Lifestyle section, captured through
  `CROSSPOINT_SIM_READALOUD_LOG=1`. **Zero blank pages**, **zero pages ending
  on a question mark** (no stranded term), and **zero fused tokens** — no word
  anywhere still contains a `?` or `)` glued to the letter after it, which is
  the report's signature at the word level. Across all 255 terms and 510
  definitions. Note what this does and does not prove: the read-aloud capture
  joins words with spaces, so it is proof about the GLUE; the render and
  `test/definition_list/` are the proof about the block break. And note the
  honest negative — **this book at its default font strands no term under
  either rule**, so it is the synthetic length sweep, not the book, that
  demonstrates the keep bug. The book pays zero pages for the strict rule (68
  both ways).
- Full host suite: **594/594** (2 disabled `LineBreakQuality` sweeps, disabled
  before this change too).
- Desktop canary `pio run -e simulator`: SUCCESS.
- Render, the owner's own book at the page in his screenshot, same pinned card
  state both arms: `docs/images/definition-list-before.png` and
  `docs/images/definition-list-after.png`. The after render is from the
  corrected build; the keep is visible in it, as the fifth entry travelling
  whole to the next page rather than splitting after its answer.

## Checked and found CLEAN

- `<ul>` / `<ol>` / `<li>`: untouched. `isListContainer` still names only `ul`
  and `ol`; only the text-indent reset was widened to `isGroupContainer`, and
  the marker, gutter and counter paths are byte-identical. `table_columns`,
  `table_keep_together` and `table_cell_label` all pass.
- The keep flag cannot leak onto a non-term block: `startNewTextBlock` clears
  it and restores it only past both merge early-returns, so a block that is not
  a term never inherits it. Pinned by
  `DefinitionListKeep.TheKeepDoesNotApplyToOrdinaryParagraphs`, which compares
  full-page line counts with and without a leading `<dl>`.
- The inter-block margin collapse needed nothing new. The three tags go through
  `startNewTextBlock` / `makePages` like `<p>`, so they participate by
  construction; the explicit `resetInterBlockCollapse()` calls are for non-text
  elements (rules, images, tables) and none of them is on this path.
- No new heap allocation in the steady state. `breakBeforeStrandedTerm`'s probe
  copies one short block per `<dt>` and only after the word-count guard, the
  same trade `measureUnlaidLeadHeight` already makes per table. Worst-case
  bound is `3 x (viewportWidth/spaceWidth + 1)` words.
- The anchor `breakPageBefore(chapterSourceBytes_)` passes is the block's first
  byte: the `<dt>` block is non-empty and unlaid, so `pendingTextIsUnlaid()` is
  true and `breakBeforeStrandedTableHeader`'s own ternary would choose the same
  value.
- `SECTION_FILE_PARTIAL_VERSION` at 55 is `0xFE - 27 = 227`, distinct from 55
  and from 0; the two formulas only collide at version 141.

## What adversarial review found, and what came of it

Run before reporting, read-only, on the finished change. It is recorded here
because the next person to touch this code needs to know the keep was WRONG
once and how that was caught.

| Finding | Outcome |
|---|---|
| **F1 HIGH** — keep-2/2 defeats a one-line keep; the reported defect survives for any 3+ line definition | **Real.** Reproduced independently before acting (4/41 at three lines, matching its measurement). Fixed: `kKeepRoomLines = 3`, derived from the table above. |
| **F2** — the fit test over-measured by using leaded advances for the last line, silently declining keeps | **Real.** Fixed: the fit uses `lineFitExtentOf`, the cursor keeps advances. |
| **F3** — `breakPageBefore` can leave `currentPage` null on OOM and `makePages` then dereferenced it | **Real**, and not the house pattern — every `breakBeforeStrandedTableHeader` caller re-tests. Fixed with the same re-test. |
| **F4** — the fixture's one-line `<dd>` was the only shape that could not catch F1 | **Real.** The keep cases now sweep definition LENGTH as well as page alignment. |
| **F5** — `kMaxTermLines` was documented as a heap guard but is also a keep limitation | **Fair.** Now stated as both, with the measurement. |
| **F6** — `TheTermItselfIsNotIndented` passes pre-fix vacuously | **Fair.** Its comment now says it is only meaningful paired with the indent case. |
| **F7** — unguarded pointer deref in a test would segfault the runner instead of failing | **Real.** `ASSERT_NE` added. |
| **F8** — the dirty tree also carries an unrelated `TODO.md` change | **Correct and pre-existing** — that edit was in the tree before this work started and is not ours to commit with it. |

Reported CLEAN by the same pass, and independently spot-checked here: the
`keepTermWithNext_` lifetime across all 14 `startNewTextBlock` and 6 `makePages`
call sites; the arithmetic order against `makePages`; no loop and no blank page;
the `ParsedText` copy and its ceiling; the anchor; `<ul>`/`<ol>`/`<li>`
untouched; the `BLOCK_TAGS` fan-out including `endElement`'s
`shouldFlush`/`isInlineTag`; that `embeddedStyle` is NOT what gates CSS
resolution (the `cssParser` pointer is, at `:1435`), so the stylesheet cases are
genuinely exercising publisher CSS.
