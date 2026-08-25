# What else typography could expose — the capability layer, 2026-08-25

Owner ask, 2026-08-25: *"what other typography settings are possible. dive into
the code for options, research what is possible, ask me."*

`docs/typography-settings-inventory.md` (2026-08-24) answers **what is a setting
today**. This file answers the layer beneath it: **what the layout engine, the
paginator, the renderer and the font format are already capable of**, whether or
not anything reaches it. Read that one first; this one assumes it.

Surveyed at `f5287c630` (`feat(settings): three rows move to Typography, and
Text Settings is Reader Font`). Every capability claim below carries a
`file:line` I read. Where I inferred rather than confirmed, it says so.

Two standing rulings are honored and not re-proposed: **Screen Margin** stays out
(2026-08-22, layout exactness) and **letter spacing** was declined 2026-08-24.

---

## 0. Two corrections to the inventory doc

The 2026-08-24 inventory's category 5 ("does not exist — real feature work")
lists two things that **do** exist. Both landed on 2026-08-22, two days before it
was written.

| Inventory said | Actually | Where |
|---|---|---|
| "orphan and widow control" does not exist | **Implemented, keep-2/2, always on.** Lines are held back three deep; the first line of a paragraph will not sit alone at the foot of a page and the last will not sit alone at the head. | `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp:2668-2765` |
| "hanging punctuation" does not exist | **Implemented, both edges, always on.** A justified line ending in `.` or `,` hangs that glyph's full advance past the measure; quotes, dashes, `;:!?` hang half. A line beginning with an opening quote, bracket or dash is shifted left by a quarter to a half. | `lib/Epub/Epub/ParsedText.cpp:288-384`, applied `:1409-1454` |

Correct the inventory when you next touch it. Everything else in it held up.

---

## 1. The cost model — read this before the tables

Three tiers, as asked:

* **`unfreeze`** — delete a `static constexpr` in `src/CrossPointSettings.h`,
  add a row to `getSettingsList()`, name it in
  `TypographySettingsActivity::rebuildRows`'s `kSettingRows`. The code behind it
  already runs. Hours.
* **`plumb`** — the engine does it, but the value is a literal in a `.cpp` with
  no field, no persistence and no spec entry. Add the field, thread it, add the
  row. A day.
* **`build`** — new rendering or new layout arithmetic.

**The thing that decides cost inside those tiers is the section cache.**

`TextBlock` bakes **`int16_t xpos[wordCount]`** — the final painted x of every
word — into the section file (`lib/Epub/Epub/blocks/TextBlock.h:24,53`, written
by `TextBlock::serialize`, `TextBlock.cpp:353-368`). So:

* Anything that changes **where a word sits horizontally** — justification
  slack, hanging punctuation, indent width, tracking, word spacing — is baked,
  and needs a `ReaderRenderSpec` field so a stale section is rebuilt
  (`lib/Epub/Epub/ReaderRenderSpec.h:29-76`). If the field changes the header's
  shape, `SECTION_FILE_VERSION` (currently **51**, `lib/Epub/Epub/Section.cpp:170`)
  bumps too, which invalidates every paginated book on every card.
* Anything **vertical** — line pitch, paragraph gaps, page breaks — is the same,
  because y is baked in `PageLine`.
* Anything computed **at paint time from the TextBlock** is free of all of that:
  anti-aliasing, underline geometry, sup/sub baseline shift, ink polarity. These
  are the cheapest controls this device can offer.

Eight of the twelve `ReaderRenderSpec` fields are already there and already
compared, so **un-freezing `extraParagraphSpacing`, `hyphenationEnabled`,
`embeddedStyle`, `focusReadingEnabled` or `paragraphAlignment` costs no cache
work at all** — the plumbing was built when they were live rows and was never
removed.

---

## 2. The CSS surface — what the parser understands, and what a setting over it means

This is the highest-value part of the survey, per the ask: anything the parser
interprets is already applied, so a global setting over it is plumbing.

**`CssParser::parseDeclarationIntoStyle` is the whole list**
(`lib/Epub/Epub/css/CssParser.cpp:467-569`). Fourteen properties, no more:

| Property | Line | What a device setting over it would be | Tier |
|---|---|---|---|
| `text-align` | `:476` | **Already exists, in two forms.** `paragraphAlignment` (frozen `JUSTIFIED`, `CrossPointSettings.h:363`) overrides CSS entirely unless set to `None`/"Book's Style" (`BlockStyle.h:140-145`); and the measure-driven auto-justify threshold is a live row. Re-adding the alignment picker **undoes the 2026-08-23 automatic ruling** — do not. | — |
| `font-style` | `:479` | italic on/off. Nobody wants this. | — |
| `font-weight` | `:482` | bold on/off. Same. | — |
| `text-decoration` / `-line` | `:485` | "Ignore underlines / strikethroughs". Real but marginal. | plumb |
| `text-indent` | `:488` | **First-line indent — see §3.1. The one genuinely interesting entry in this table.** | unfreeze |
| `margin-*`, `padding-*`, `margin`, `padding` | `:490-524` | Block insets. Already clamped hard: 2 em per side (`BlockStyle.h:16`), accumulated insets clamped to 2/5 viewport, paragraph gaps capped at half a line (`ChapterHtmlSlimParser.cpp:2900-2923`). A "flatten publisher indents" setting is `embeddedStyle` off, §3.5. | — |
| `height`, `width` | `:525-536` | Image sizing, not typography. | — |
| `display` | `:537` | `none` only. | — |
| `direction` | `:541` | RTL. Auto-detected per paragraph anyway (`ParsedText.cpp:723-734`). | — |
| `vertical-align` | `:550` | `super`/`sub` only. See §4.3. | — |
| `writing-mode` | `:559` | **Parsed for a book note only** — there is no vertical layout. A `vertical-rl` book renders horizontally and raises `VerticalWritingIgnored`. | build (large) |

**What the parser deliberately drops, and why that matters more than the list
above.** There is no branch for any of these, so a declaration is discarded
silently:

`font-size` · `line-height` · `font-family` · `letter-spacing` ·
`word-spacing` · `text-transform` · `font-variant` · `color` ·
`background` · `page-break-before/after/inside` · `white-space` · `hyphens` ·
`orphans` · `widows` · `text-shadow` · `float` · `columns`

Two consequences, and they cut in opposite directions:

1. **The conflict surface for any new global setting is tiny.** A device setting
   for line spacing, hyphenation, widow control or page breaks cannot fight a
   book's stylesheet, because the stylesheet's opinion about those never
   survived parsing. There is nothing to reconcile. That is exactly why Line
   Spacing exists as a device setting today — `line-height` is dropped
   (`CssParser.cpp:467-569`, no branch), so the device is the only voice.
2. **Symmetrically, a per-book override for any of them is meaningless.** There
   is no stored value to override.

The only two properties where a device setting genuinely *fights* the book are
`text-align` (already resolved by the 2026-08-23 automatic ruling) and
`text-indent` (§3.1).

Note also the `pt` anchor: every absolute unit converts at **150 dpi**
(`lib/Epub/Epub/css/CssUnits.h:52`), chosen to match the font converters. A book
cannot set 12 pt type — `font-size` is dropped — so the parity is between a
book's `pt` margins and the reader's own type size.

---

## 3. Layout-engine capabilities with no control

### 3.1 Paragraph style: indented, or spaced — `extraParagraphSpacing`

**What the reader would see:** paragraphs either separated by half a blank line
with no indents (today), or run on with an indented first line and no gap — the
way a printed novel is set.

**Engine:** fully implemented, both halves, driven by one flag.
`ParsedText::resolveFirstLineIndent` (`lib/Epub/Epub/ParsedText.cpp:700-714`):

```
if (blockStyle.textIndentDefined) {
  if (blockStyle.textIndent < 0 || !extraParagraphSpacing) return blockStyle.textIndent;
  return 0;                                    // ON: the book's own indent is DISCARDED
}
if (!extraParagraphSpacing) return renderer.getSpaceWidth(...) * 3;   // OFF: 3-space default
return 0;
```

and the vertical half at `ChapterHtmlSlimParser.cpp:2909-2923`: `+ lineHeight/2`
when on, capped at `lineHeight/2`.

**Tier:** `unfreeze`. It is `static constexpr uint8_t extraParagraphSpacing = 1`
(`src/CrossPointSettings.h:299`) and it is **already a `ReaderRenderSpec` field**
(`ReaderRenderSpec.h:37`, in the file and in the comparison), so no cache work.

**Per-book or global:** global, and it is an **override** — with it on, a book's
own `text-indent` is thrown away. Turning it off *restores* the book's indent
where one is declared and supplies three space-widths where it is not. That is
the honest framing for the row: on = the device's paragraph style, off = the
book's.

**Effect size:** large. Every paragraph boundary on every page changes, and a
page of five paragraphs recovers about 2.5 lines of text.

### 3.2 Line spacing — the ramp is narrower than it looks

**Engine:** `getReaderLineCompression()` (`src/CrossPointSettings.cpp:378-392`)
returns **0.95 / 1.00 / 1.10**, multiplied into `advanceY` and rounded
(`lib/GfxRenderer/GfxRenderer.cpp:2861-2863`).

**COMPUTED, not measured** — X3 portrait, 18 pt Libre Franklin
(`advanceY` = 45 px, stated at `CssUnits.h:25`), viewport height 770 px
(792 − (9+5) − (3+5), margins at `GfxRenderer.h:160-163` plus the default
`screenMargin` 5):

| Setting | Pitch | Lines/page |
|---|---|---|
| TIGHT 0.95 | 43 px | 17 |
| NORMAL 1.00 | 45 px | 17 |
| WIDE 1.10 | 50 px | 15 |

So **TIGHT and NORMAL are the same page at 18 pt** — the pitch differs by 2 px
and the last-line ink rule (`ChapterHtmlSlimParser.cpp:2639-2646`) absorbs it.
The ramp only spans 16%. Adding rungs at, say, 0.85 (38 px, 20 lines) and 1.25
(56 px, 13 lines) would make the control mean something at both ends.

**Tier:** `plumb`, and a cheap one: one `switch` and one label array.
`lineCompression` is already a spec field compared as a float, so appending
rungs invalidates only the books whose value actually moves. **Append only** —
`lineSpacing` persists as the enum index, so re-pointing an existing rung
silently restyles a saved choice.

### 3.3 Hyphenation — and the line breaker hiding behind it

**What the reader would see:** hyphens at line ends, or none. **And, invisibly, a
different line breaker.**

`ParsedText::layoutAndExtractLines` (`ParsedText.cpp:810-816`):

```
if (hyphenationEnabled) lineBreakIndices = computeHyphenatedLineBreaks(...);   // GREEDY
else                    lineBreakIndices = computeLineBreaks(...);             // DP, total-fit
```

`computeLineBreaks` (`:949-1146`) is a **total-fit dynamic program** minimizing
the sum of squared trailing slack over the whole paragraph — a Knuth-Plass-shaped
optimizer. `computeHyphenatedLineBreaks` (`:1149-1233`) is first-fit greedy.
**Hyphenation is frozen ON** (`static constexpr uint8_t hyphenationEnabled = 1`,
`CrossPointSettings.h:427`), so **the DP breaker is dead code in every shipped
configuration.** It is written, it works, and nothing can reach it.

**Tier:** `unfreeze`. Already a spec field (`ReaderRenderSpec.h:41`).

**Per-book or global:** global; the book has no say (`hyphens` is dropped, and
the pattern language comes from `dc:language` via
`Hyphenator::setPreferredLanguage`).

**Effect size:** large and two-sided. Off, the right edge of a ragged paragraph
gets noticeably more even (that is what the DP buys) and the rag gets deeper on
justified text. This is the one row on the page where the label understates what
it does — worth saying so in the row's help text, or worth splitting into two
rows.

### 3.4 Widow and orphan control — implemented, hardcoded keep-2/2

**Engine:** `ChapterHtmlSlimParser::addLineToPage` / `flushPendingLines`
(`:2668-2765`). Three lines are buffered; the orphan test is at `:2685-2694`,
the widow test at `:2746-2756`, the 3-line all-or-nothing case at `:2719-2736`,
and 1–2-line paragraphs are exempt at `:2708`.

**Tier:** `plumb`. The holdback `3` (`:2681`), the exemption `<= 2` (`:2708`) and
the pair-count 2 in both tests are literals. Off / Keep 2 / Keep 3 means a
`ReaderRenderSpec` field and — because the field is new and the header's shape
changes — a `SECTION_FILE_VERSION` bump.

**Effect size: small and negative-valued.** It is already on and already right.
The only reason to expose it is to turn it OFF to recover a line at the foot of a
page on a small panel. I would not lead with this.

### 3.5 Ignore publisher styles — `embeddedStyle`

**What the reader would see:** every book set the same way, with the publisher's
margins, centered blocks, indents and decorations discarded. The standard
e-reader escape hatch for a badly-styled EPUB.

**Engine:** `ReaderActivity.cpp:69` — `epub->load(true, SETTINGS.embeddedStyle == 0)`
— plus two behavioral branches at `ChapterHtmlSlimParser.cpp:1646` and `:1667`
(heading alignment) and a soft-flush threshold at `:2116`.

**Tier:** `unfreeze` (`static constexpr uint8_t embeddedStyle = 1`,
`CrossPointSettings.h:472`). Already a spec field (`ReaderRenderSpec.h:42`).

**Per-book or global:** global. This is the one setting whose entire purpose is
to fight the stylesheet, so the conflict question answers itself. **The honest
caveat:** since the parser only understands fourteen properties, "publisher
styles" here means margins, padding, indent, alignment, italic/bold, decoration
and `display:none` — considerably less than the phrase implies on a phone
e-reader. It will do less than a reader expects. Name the row for what it does.

### 3.6 First-line indent width

Hardcoded at **three space-widths** (`ParsedText.cpp:711`), and only reachable
when §3.1 is off. A 0 / 1 em / 1.5 em / 2 em picker is `plumb` and moves
`xpos`, so it needs a spec field. Low value on its own; it is really a
sub-setting of §3.1 and should ship with it or not at all.

### 3.7 Chapter opening drop (sinkage)

Every spine section's first page begins one fifth of the viewport down, snapped
to a whole number of line-heights (`kChapterSinkageFraction = 5`,
`ChapterHtmlSlimParser.cpp:2609-2617`; owner ruling 2026-08-22). A None / Small
/ Classic picker is `plumb` + spec field + version bump.

**Effect size: large but rare** — it changes one page per chapter. On a 15-line
page a fifth is 3 lines.

### 3.8 Chapter headings force a page break

`h1`–`h3` always open a fresh page; `h4`–`h6` flow inline
(`ChapterHtmlSlimParser.cpp:1685-1695`). The comment there records a real
non-typographic reason: a forced break is what makes a heading stay at the top of
its page across a font or size reflow, because the word-anchor reposition lands
on a page that begins at the same source position under every pagination.
**Exposing this as a setting weakens position restore.** `plumb`, and I would
argue against it.

### 3.9 Heading alignment

Headings default to **centered**, and the book's `text-align` only wins when
`embeddedStyle` is on (`ChapterHtmlSlimParser.cpp:1665-1669`). A Centered / Left
/ Book's picker is `plumb` + spec field.

**Effect size: visible, and it is the second thing a reader notices after the
body face.** Every chapter title on every book moves. Cheap for what it changes.

### 3.10 Justification: no maximum word space

`computeJustifyExtra` (`ParsedText.cpp:247-255`) distributes slack evenly across
gaps with **no cap** — the comment says so explicitly, and says the uncapped
behavior is deliberate (capping used to leave the line left-aligned, which was
the mismatched-alignment bug). Bringhurst's M/2 ceiling is not enforced; the
auto-justify threshold (`AutoJustify.h`) is the device's answer to gaping lines
instead, and it is already a row.

Also: justification **stretches only, never compresses**. There is no negative
slack path.

**Do not add a word-space cap.** Under a "Justified" label, a capped line has a
ragged right edge; that is worse than a wide one and it is the exact failure the
comment records.

### 3.11 Hanging punctuation

Already on, both edges, table at `ParsedText.cpp:325-340`. Applied only to
justified non-last LTR lines (trailing, `:1418-1422`) and to any non-right,
non-center LTR line (leading, `:1442-1446`), with the left hang capped at the
available margin (`:1441`). Pagination is byte-identical either way — only
`xpos` moves.

An off switch is `plumb` + spec field. **Effect size: a few pixels per line.**
Nobody would ask for it, and nobody would see it turned off. Listed for
completeness; I would not build it.

### 3.12 Preformatted text is discarded

`<pre>` collapses like any prose — line breaks, indentation and space runs are
gone — and the reader is told via a book note
(`ChapterHtmlSlimParser.cpp:1050-1057`, `BookNotes.h:67`). `white-space` is not
parsed. Preserving it is `build`: a whitespace-preserving tokenizer path, plus
no monospace face is guaranteed on the card. Real, known, and only matters for
technical books.

---

## 4. Renderer capabilities with no control

### 4.1 Text anti-aliasing — the strongest candidate on this page

**What the reader would see:** the same text set lighter or heavier, without the
size or the line breaks moving at all.

**Engine:** three fully implemented level→plane mappings,
`lib/GfxRenderer/GlyphAaPlanes.h:67-108`. The panel offers four tones; glyphs
carry four coverage levels.

| Mode | Light-mode masks | What happens |
|---|---|---|
| OFF | no gray pass runs | every non-white level paints solid black — glyphs get **fatter and fully aliased**, not thinner |
| STANDARD (shipped) | `{L0\|L1\|L2, L1\|L2, L1}` | L0 black, L1 → dark gray, L2 → light gray. Four tones — the maximum this panel can show. |
| CRISP | `{L0\|L1\|L2, L2, 0}` | L1 **hardens to black**, L2 stays light gray. Three tones. |
| DARK | `{L0\|L1\|L2, L2, L2}` | L1 hardens to black, L2 → dark gray. **Every antialiased pixel darkens exactly one step.** |

Dark-mode masks are at `:93-107`, with the two grays swapped for the inversion.
Selection: `src/TextAntiAliasing.h:67-76`; renderer state, not a `drawText`
argument, deliberately (`GfxRenderer.h:521-525`).

**Tier:** `unfreeze`. `static constexpr uint8_t textAntiAliasing = TEXT_AA_STANDARD`
(`CrossPointSettings.h:302`). It is **not** in `ReaderRenderSpec` and does not
need to be: it is applied at paint time, so **no repagination, no cache
invalidation, and the change is visible on the current page immediately.** It is
the cheapest control in this entire document.

**Per-book or global:** global, no conflict possible — CSS `color` is dropped.

**Effect size: the largest per unit cost of anything here.** It changes the
weight of every glyph on every screen. Note that UI chrome already pins CRISP at
8/10/12 pt for exactly this reason (`TextAntiAliasing.h:108-113`) — the effect is
real enough that the chrome needed its own answer.

Two honest limits to put in the row's help text:
* **Sup/sub glyphs are never antialiased** — `renderCharScaled` thresholds to
  solid ink and never reads the plane masks (`GfxRenderer.cpp:364-430`).
* **White-on-black text cannot be antialiased at all** — the plane branch
  hardcodes `false` for ink polarity (`GfxRenderer.cpp:594`).

### 4.2 Underline and strikethrough geometry

Underline sits **2 px below the baseline**, strikethrough at **4/5 of the
ascender**, both drawn **2 px thick at every size**
(`lib/Epub/Epub/blocks/TextBlock.cpp:237-243`). Paint-time, so a control is
cache-free. But 2 px at 12 pt and 2 px at 18 pt is the actual defect here, and
the fix is to derive thickness from the size — **a bug to fix, not a setting to
offer**. Recorded so it is not proposed as a row.

### 4.3 Superscript and subscript

Fixed **50% scale** (`GfxRenderer.cpp:377-378`, a 2×2 box downsample with a
hardcoded ink threshold at `:402`), advance halved in 12.4
(`:830-835`), baseline raised **40%** of ascender for SUP and lowered **25%** for
SUB (`TextBlock.cpp:259-263`). The shifts are paint-time and free to change; the
50% is not — it is the only scaling path in the renderer and it is a box filter,
so any other ratio is `build`. Footnote markers and chemical formulas are the
whole population. **Low value.**

### 4.4 Where a tracking value would go, if the ruling ever changes

Recorded because it will be asked again, not as a proposal. The cursor advances
at exactly one expression, in four places:

```
lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);   // GfxRenderer.cpp:820-823
```
mirrored at `:2718-2719` and `:2751-2755` (measure) and `:2950-2951` / `:3057-3058`
(rotated). Advance is 12.4 unsigned, kern is 4.4 signed, and they already share
four fractional bits (`lib/EpdFont/EpdFontData.h:125-134`), so **a tracking term
in 1/16 px drops straight into that sum and inherits the existing
differential-rounding** — no accumulated error. It must land in all five sites
(add `getSpaceAdvance`, `:2619-2645`, for word spacing) in lockstep or measure and
draw disagree and lines clip at the margin. Plus a spec field and a version bump,
because `xpos` moves.

**Note it already exists at build time.** `tracking_em` and `word_space_em` are
per-face, per-style advance deltas baked into `.cpfont` by
`lib/EpdFont/scripts/fontconvert_sdcard.py:751-770`, and they have been *tuned
per family* against rendered benches (`sd-fonts.yaml:693-696` records a sweep
from 0 to 0.05 em that was rejected). A global runtime tracking dial would fight
that tuning on every mixed-source family. That is an additional argument against
it beyond the cost.

### 4.5 Render scale

Compile-time 1 on device (`RenderScale.h:72`); the whole supersampling path is
`#if`'d out of the firmware. Host and iOS only. Not a device setting and cannot
become one.

---

## 5. The font layer — the hard ceiling

This is where most plausible-sounding typography requests die. Recorded so they
are not re-proposed.

### 5.1 One reader-size cut in RAM

`SdCardFontSystem::resolveFontId` **ignores its `pointSize` argument**
(`src/SdCardFontSystem.cpp:243-248`): the manager holds exactly one reader-size
font. `getSmallestReaderFontId` documents the consequence in full
(`src/CrossPointSettings.cpp:425-445`): for an SD family it returns the same id
as `getReaderFontId`, so even the wide-table step-down it exists for does nothing
on any shipped configuration.

**Therefore: any setting that renders part of a page at a different size is
`build`, and the build is "hold a second font cut in RAM on a device with
320 KB".** That rules out, all at once:

* larger chapter headings
* smaller footnotes, captions or block quotes
* drop caps
* any per-block font-size honoring

Built-in families do ship four sizes (`src/ReaderFontSizes.h:24`,
`{12,14,16,18}`), so a size-varying feature would work on built-ins and silently
do nothing on SD families — which is every shipped configuration. That asymmetry
is worse than not having it.

### 5.2 What the `.cpfont` format does not carry

Confirmed against `docs/cpfont-format.md` and the headers:

* **No OpenType feature table.** No feature tag survives the build.
* **No small caps, oldstyle figures, swashes or alternate glyph slots.**
  `--pnum` exists only in the *built-in* generator `fontconvert.py`, and even
  there it is a build-time bitmap substitution, not a runtime alternate.
* **No discretionary or historical ligature class.** The generator reads only
  GSUB `liga` and `rlig`; `dlig`/`hlig` glyphs are **not in the file**. And the
  on-disk pair table (`lib/EpdFont/EpdFontData.h:168-173`) has no feature field,
  so even the pairs that are present cannot be sorted into classes. "Historical
  ligatures only" is not offerable **and not inferable** — it needs both a format
  change and a generator change.
* **No x-height, no cap height.** `getCapInkTrim` measures the ink top of `'H'`
  at runtime because the metric does not exist (`GfxRenderer.h:479-491`).
* **No second weight.** Style is 2 bits, `MAX_STYLES = 4`
  (`lib/EpdFont/SdCardFont.h:25`). A Light or Medium has to be a separate family.
* **No runtime synthetic bold or oblique.** The missing-style ladder
  (`lib/EpdFont/EpdFontFamily.cpp:3-19`) returns *another real face*; there is no
  smear and no shear at runtime. Synthetic styles are baked at build
  (`docs/synthetic-font-styles.md`).
* **No kerning off switch and no kern scale.** Kerning is a class matrix applied
  unconditionally wherever one is present; there is no gate.

**Reserved space that does exist:** header `flags` bits 1–15, header bytes
13–31, style-TOC bytes 1–3 and 28–31. All written zero and never read — but all
**hashed into `contentHash`**, so using one invalidates every section cache on
every card.

### 5.3 One unused tool

`lib/EpdFont/scripts/optical_kern.py` synthesizes kern pairs into a patched TTF
and **has no build hook** — no `sd-fonts.yaml` key, no call site in
`build-sd-fonts.py`. It only reaches a `.cpfont` if run by hand. Not a setting;
noted because it looks like a shipped capability and is not.

---

## 6. Ranked shortlist — what I would put in front of him

Ordered by value per unit cost. The first three are the ones I would actually
ask about.

### 1. Text Anti-aliasing — `unfreeze`, paint-time, no cache

Four implemented modes; the only item here that changes how **every glyph on
every screen** looks; the only item that costs nothing in pagination and shows
its effect on the current page the instant it is changed. The 2026-08-24
inventory already named it the strongest candidate and everything I read
confirms that. It is un-pinning, not building.

### 2. Paragraph Style: Spaced / Indented — `unfreeze`, spec field already there

The single biggest change to whether a page reads as a book or as a web page,
and the machinery for both halves is complete and already keyed into the section
cache. Frame the row honestly: it decides whether the device's paragraph style
overrides the book's.

### 3. A wider Line Spacing ramp — `plumb`, one array, append-only

The existing control spans 16% and its two lower rungs give the same line count
at 18 pt. This is the cheapest way to make a row that already exists actually
mean something. Ask him for the range before picking rungs; that is an
architectural choice, not mine.

### 4. Hyphenation — `unfreeze`, spec field already there

Cheap, and it un-hides a total-fit line breaker that is currently dead code in
every shipped build. Worth putting in front of him **with** the fact that the row
does two things, so he can decide whether it should be two rows.

### Below the line, in order

5. **Heading Alignment** (`plumb` + spec field) — visible on every chapter
   title, moderate cost.
6. **Ignore Publisher Styles** (`unfreeze`) — cheap, but it will do less than
   the name promises given the fourteen-property parser. Name it carefully.
7. **Focus Reading** (`unfreeze` of a row that already exists but is hidden and
   pinned off, `CrossPointSettings.cpp:122-124`) — implemented at
   `ParsedText.cpp:557-600`, bolds the first 45% of each word. Already
   web-settable; making it visible is a UI decision, not an engineering one.
8. **Chapter Opening Drop** (`plumb` + version bump) — large effect, one page
   per chapter.
9. **Widow/Orphan strength** (`plumb` + version bump) — already correct; the
   only use is turning it off to recover a line.

---

## 7. Considered and rejected — with the reason

Negative results, so these are not re-proposed. Each was checked against source.

| Candidate | Why not |
|---|---|
| **Letter spacing / tracking** | Owner declined 2026-08-24. Also: five lockstep call sites, a spec field, a version bump, and it would fight the per-family `tracking_em` already tuned into every `.cpfont` (`sd-fonts.yaml:693-696`). |
| **Word spacing** | Same shape and same cost as tracking, plus `word_space_em` is already tuned per family for mixed-source italics (`fontconvert_sdcard.py:751-770`). A global dial would undo deliberate work. |
| **Small caps, oldstyle figures, swashes, alternates** | Not in the font format. No feature table, no alternate slots. `build` means changing the format and rebuilding all 52 families. |
| **Discretionary / historical ligatures** | The glyphs are not extracted (`liga`/`rlig` only) and the pair table carries no feature field. Not offerable and not inferable. |
| **Drop caps** | Needs a second font cut in RAM (§5.1). |
| **Larger headings / smaller footnotes** | Same. And it would work on built-in families and silently do nothing on SD ones. |
| **Font weight beyond bold** | Style is 2 bits, `MAX_STYLES = 4`. |
| **Synthetic bold / oblique for families missing a style** | No runtime shear or smear exists; the ladder falls back to a real face. `build`, and the build is a glyph-outline pass on a device with no outlines — the file stores bitmaps. |
| **Kerning off** | No gate exists anywhere, and nobody wants it off. |
| **Maximum word space cap on justification** | The uncapped behavior is deliberate (`ParsedText.cpp:249-253`); capping used to leave the line left-aligned under a "Justified" label. Auto-justify already solves the real problem and is already a row. |
| **Hanging punctuation off** | Already on, effect is a few pixels, and nobody would notice it turned off. |
| **Underline thickness / offset** | The 2 px-at-every-size constant is a bug to fix, not a knob to expose. |
| **Sup/sub size** | Fixed 50% box filter, the only scaling path in the renderer. Any other ratio is a new scaler for two glyph classes. |
| **Hyphenation min prefix / suffix** | `kDefaultMinPrefix/Suffix = 2` (`LiangHyphenation.h:15-16`). Standard values; the visible difference is one or two break points per page. Invisible in practice. |
| **Max consecutive hyphens** | Genuinely absent — the DP cost function has no hyphen penalty (`ParsedText.cpp:1094-1107`) and the greedy breaker has none either. But the greedy breaker is what ships, so a penalty would need the DP path, which is only reachable with hyphenation off, which is when there are no hyphens. Circular. `build`, low value. |
| **Hyphenation language override** | Comes from `dc:language` (`Hyphenator::setPreferredLanguage`, mapped at `Hyphenator.cpp:26-28`). A setting only helps a book with wrong or missing metadata, and is invisible otherwise. |
| **Vertical writing (tategaki)** | Parsed for a book note only (`CssParser.cpp:559-567`). No vertical layout exists anywhere. Very large `build`. |
| **Preformatted text preservation** | Real, known, book-noted defect (`BookNotes.h:67`). `build`: a whitespace-preserving tokenizer path, and no monospace face is guaranteed on the card. Only matters for technical books. |
| **Chapter headings force a page break** | Exposing it weakens position restore across a font reflow, by the code's own reasoning (`ChapterHtmlSlimParser.cpp:1675-1684`). |
| **Screen Margin** | Ruled out 2026-08-22. Not re-proposed. |
| **Paragraph Alignment picker** | Would undo the 2026-08-23 automatic-justification ruling. Not re-proposed. |
| **Render scale / sharpness** | Compile-time 1 on device; the path is `#if`'d out of the firmware entirely. |
| **List indent step** | `LIST_INDENT_STEP_EM = 1.5f` (`ChapterHtmlSlimParser.cpp:60`), sized against this panel's short measure with a documented argument. `plumb`, negligible value. |

---

## 8. What I could not determine

* **Whether `getSpaceAdvance` has callers outside the layout layer.** It is the
  single funnel for inter-word advance (`GfxRenderer.cpp:2619-2645`) and would be
  the cheap hook for word spacing, but I did not enumerate every call site. Check
  before pricing that one.
* **Whether a per-style `advanceY` divergence exists in any shipped family.**
  `getLineHeight` reads REGULAR's `advanceY` only (`GfxRenderer.cpp:2851-2859`,
  read directly), and the format stores `advanceY` per style — so a family whose
  italic leads differently would be laid out on the roman's pitch. Whether any
  shipped family actually diverges, I did not measure.
* **`sdCardFontScales_` / `registerSdCardFontScale`** (`GfxRenderer.h:266-271`),
  an 8.8 per-font scale multiplier already stored in the renderer. I did not find
  it consumed anywhere in the draw path. It may be dead, or it may be read
  somewhere I did not look. Worth five minutes before anyone builds on it.
* **Effect sizes are computed, not measured.** The lines-per-page table in §3.2
  is arithmetic off `advanceY` 45 px and a 770 px viewport; the last-line ink-fit
  rule can add one line. Nothing in this document was rendered and looked at. Any
  candidate that reaches the shortlist should be rendered before it is described
  to the owner as visible.
