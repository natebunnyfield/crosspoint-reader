# Book notes, and sparse ruby — 2026-08-23

Two owner rulings, landed together because both are about a book being handled
in a way nobody was told about.

1. *"take it"* — the 4-byte empty ruby string per word, deferred as item 2 of
   [performance-indexing-2026-08-23.md](performance-indexing-2026-08-23.md).
2. *"override book css, but give a verbose note that it is happening in the
   select chapter screen (do this for all concerning book specific issues)."*

`SECTION_FILE_VERSION` 44 → **45** ([Section.cpp:93-104](../lib/Epub/Epub/Section.cpp)).
`BOOK_CACHE_VERSION` is untouched at 10 — the notes are a separate 19-byte file.

> **Superseded in part, later the same day.** Items #52 and #38 from the survey
> below were taken; see
> [encodings-glyphs-and-library-sync-2026-08-23.md](encodings-glyphs-and-library-sync-2026-08-23.md).
> That work took `SECTION_FILE_VERSION` to **46** and `notes.bin` to version
> **2** (19 bytes → 45), and added two notes, `TextEncodingUnsupported` and
> `MissingGlyphs`, so the fourteen below are now **sixteen**. It also found that
> **#38 as written here is wrong** — a missing codepoint has not drawn nothing
> since B-009 — and the correction is in that document.
>
> **Superseded again, later the same day.** Items **#23**, **#24**, **#62** and
> **#69** were taken; see
> [css-length-units-2026-08-23.md](css-length-units-2026-08-23.md). That work
> took `SECTION_FILE_VERSION` to **47**, `CSS_CACHE_VERSION` to **9** and
> `notes.bin` to version **3** (45 bytes → 53), and added one note,
> `CssUnitsUnsupported`, so the count is now **seventeen**. It also found that
> **#69 as written here is wrong for `svg`** — skipping that subtree would drop
> the cover of a large share of books, because an SVG-wrapped cover is
> `<svg><image/></svg>` and `image` is in `IMAGE_TAGS`. The correction is in
> that document, along with an int16 overflow that absolute units made
> reachable, and a pre-existing `text-align: center !important` that forced
> LEFT.

---

## 1. Sparse ruby

### What was there

`TextBlock::serialize` wrote a `serialization::writeString` for **every word**,
carrying that word's furigana. `writeString` is a uint32 length then the bytes,
so a book with no ruby in it anywhere paid **4 bytes per word, forever**, to
record "nothing here". `rubyTexts` is empty for every non-ruby block — the
constructor drops an all-empty vector on the spot
([TextBlock.cpp:47-50](../lib/Epub/Epub/blocks/TextBlock.cpp)) — so those four
bytes were an empty-string length and nothing else.

### The encoding chosen, and why

[lib/Epub/Epub/blocks/RubySerialization.h](../lib/Epub/Epub/blocks/RubySerialization.h),
a pure header templated on the stream so one definition serves
`BufferedFileWriter` (the build), `HalFile` (page load) and
`std::ostream`/`std::istream` (the host test):

```
uint8_t  present      0 = this block has no ruby; NOTHING follows
-- present == 1 only:
uint16_t count        annotated words, 1..wordCount
count x:
  uint16_t index      word index, strictly increasing, < wordCount
  writeString text    never empty
```

**A presence flag AND a sparse list, not one or the other.** The deferred item
proposed "one presence byte per block", which fixes the common case. It leaves
the ruby case paying a string per word, and even in a Japanese book most words
on a ruby line carry no annotation of their own: ruby attaches to the group
LEADER and the continuation words are flagged `EpdFontFamily::RUBY_CONTINUE`
with an empty string (the `RUBY_CONTINUE` scan in `TextBlock::render`).
Index-prefixing costs 2 bytes on a word that has ruby and saves 4 on every word
that does not, so the sparse list is cheaper on both sides. Measured on the
fixture: a 60-word block with two annotations is 33 bytes against 258.

**The reader refuses rather than repairs.** A count past `wordCount`, an index
out of order or out of range, or an empty annotation fails the block, which is
already the path that discards and rebuilds the section cache
([TextBlock.cpp deserialize](../lib/Epub/Epub/blocks/TextBlock.cpp)). Repairing
silently would leave a half-decoded page on screen with nothing to say so.

`TextBlock::hasRuby()` now calls `rubyserial::hasAnnotations`, so the
serializer's presence flag and the renderer's "does this line have ruby" test
cannot drift apart.

### Measured

Desktop simulator, `simulator_x3` (X3 profile, 792x528, LibrisADF 18 pt),
`PLATFORMIO_BUILD_FLAGS="-O2 -g"`, one cold build of spine 0 forced by writing a
deep `progress.bin` and deleting `sections/` — the same harness as the indexing
doc. "Before" is a real build: the two functions in `RubySerialization.h` were
temporarily replaced with the v44 dense form and the binary rebuilt, so both
columns are measurements, not arithmetic.

| Book | spine 0 | section bytes before | after | saved | storage writes | build |
|---|---|---|---|---|---|---|
| `giant.epub` (generated, 1 chapter) | 5,816 pages | 7,840,446 | **6,850,554** | 989,892 — **12.6%** | 7,663 → 6,725 | 251 → 244 ms |
| `measure.epub` (generated novel chapter) | 246 pages | 333,690 | **291,382** | 42,308 — **12.7%** | 325 → 285 | 16 → 16 ms |
| `wingspan-the-whole-bird.epub` (real) | 10 pages | 14,245 | **11,621** | 2,624 — **18.4%** | 13 → 11 | 4 → 1 ms |

**The byte saving is the point; the time saving is not.** 12.6-12.7% of every
section file, written once during indexing and read back on every page turn —
which is what the deferred item predicted at 13%. Wall clock barely moves on the
host because the page stream has been buffered since earlier the same day, so
the bytes no longer cost a syscall each; the 12% fewer `HalFile::write` calls is
the figure that transfers to the device, where each one is a recursive-mutex
acquire plus an SdFat transaction against a single shared 512-byte sector cache.
On a slow card the volume itself is the remaining cost of indexing (the section
file was 3.1x the size of the chapter HTML; it is now 2.7x).

The real book saves proportionally more because its pages are shorter — the
per-word overhead was a larger share of a page that is mostly headings and short
paragraphs.

### Ruby still works — proved by rendering, not by inspection

`tools/make_ruby_book.py` generates the fixture: a Latin chapter (glosses over
`colonel`, `Worcestershire`, `quay` — visible on a card carrying only the Latin
reading faces this repo ships) and a Japanese chapter with real furigana,
including a multi-character base whose annotation spans the group.

The same book was rendered headlessly with the **v44 dense** encoding and with
the **v45 sparse** encoding, cache wiped between runs:

```
bbox of difference (None = pixel-identical): None
```

Pixel-identical. The annotations land on the same words at the same x. Figures:
`ruby_latin.png`, `ruby_annotations_crop_2x_nearest.png`.

Multibyte is covered by the unit test rather than by a render, because this card
has no CJK font and a Japanese chapter renders as blank boxes on it — which
looks exactly like a dropped annotation and would prove nothing.

### Tests

`test/ruby_serialization/` — 18 tests, all passing. They drive the PRODUCTION
instantiations (`BufferedFileWriter` in, `HalFile` out) through an in-memory
`HalFile` stub rather than a `stringstream` lookalike. What they pin:

* one byte for a ruby-less block at any word count, and that an all-empty vector
  sized to the words is the same record as no vector;
* the empty case decodes to an EMPTY vector, not `wordCount` empty
  `std::string`s — that lazy allocation is a DRAM decision, and losing it costs
  24 bytes per word for the life of a resident page;
* single, sparse, all-annotated, and last-word round trips, with the word
  INDICES preserved (losing the index would silently move every annotation onto
  the wrong word);
* multibyte survives byte-for-byte, not merely length-for-length;
* every refusal: a presence byte that is neither 0 nor 1, a count past the word
  count, an index out of range, out-of-order indices, a truncated record, a
  present flag with nothing after it;
* `hasAnnotations` agrees with the presence byte the writer emits.

---

## 2. Book notes

### (a) The override is real — confirmed by reading

`BlockStyle::fromCssStyle`
([BlockStyle.h:141-146](../lib/Epub/Epub/blocks/BlockStyle.h)):

```cpp
if (paragraphAlignment == CssTextAlign::None) {
  blockStyle.alignment = blockStyle.textAlignDefined ? cssStyle.textAlign : CssTextAlign::Justify;
} else {
  blockStyle.alignment = paragraphAlignment;
}
```

`CrossPointSettings::paragraphAlignment` is a `static constexpr` JUSTIFIED since
the automatic-justification ruling ([CrossPointSettings.h:352](../src/CrossPointSettings.h)),
so the `None` branch is **dead code**: there is no configuration in which a
non-heading block's CSS `text-align` is honored. `textAlignDefined` still records
that the book asked; the value it asked for is discarded. Headings take a
different path and DO keep their CSS
([ChapterHtmlSlimParser.cpp:1546-1549](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp)).

The behavior is kept, per the owner's ruling. It is now told.

Auto-justification then demotes a justified block to ragged when its own measure
carries fewer than 40 characters ([ParsedText.cpp:767-786](../lib/Epub/Epub/ParsedText.cpp),
threshold and sources in [AutoJustify.h](../lib/Epub/Epub/AutoJustify.h)). At the
shipped X3 default — LibrisADF 18 pt, 792x528 portrait — the BODY measure is
about 35 characters, so at that size the demotion is not an edge case: it is
what the reader is looking at. That is precisely why it needed a notice with the
number in it.

### (b) The notice

**Model** — [lib/Epub/Epub/BookNotes.h](../lib/Epub/Epub/BookNotes.h) (inline;
only the two storage-touching methods are in the `.cpp`). A 14-bit set plus
three small figures, raised where each decision is taken and **never recomputed
to draw a screen**.

Two scopes, because they go stale differently:

| Scope | Raised while | Cleared by |
|---|---|---|
| Book | the OPF, TOC, zip directory and stylesheets are parsed. Font-independent. | deleting the book's cache directory |
| Layout | a chapter is paginated. Depends on the measure. | a change in `ReaderRenderSpec::layoutFingerprint()` — the reader pushes it once per render pass ([EpubReaderActivity.cpp:1046-1051](../src/activities/reader/EpubReaderActivity.cpp)) |

A stale layout note is a lie with a number in it — "your text is ragged at 30
characters" after the reader has picked a smaller size — so the fingerprint
mismatch drops the mask *and* the figures it quoted.

**Persistence** — `<bookCache>/notes.bin`, 19 bytes: version, two masks, the
fingerprint, and three uint16 figures. Written by `Notes::flush()`, which is a
no-op unless something changed, from two places: the end of `Epub::load` (book
scope, both the warm and the cold path) and the end of a committed section build
(layout scope). Without it, a warm resume into an already-paginated book would
know nothing — the parses that raised the notes were skipped.

**Layout-scope notes are incomplete by construction.** They know only about the
chapters paginated so far. That is honest and cheap; the alternative — paginating
the whole book to fill in a notice screen — is exactly what the design constraint
forbids.

**Where it appears.** One row at the top of the Select Chapter list, reading
`Book Notes (N)`, opening `BookNotesActivity`
([BookNotesActivity.cpp](../src/activities/reader/BookNotesActivity.cpp)) — a
scrolling screen with a headline and a full plain-language paragraph per note.

A ROW, not a banner band, because the constraint was that the notice must not
push the chapter list off screen and the chapter list is what the screen is for.
A notice verbose enough to be useful is several paragraphs, and several
paragraphs is the whole panel. One row costs one chapter of visible list, is
reachable with the four front buttons and by touch with no new gesture, and
**disappears entirely** when a book has nothing to say — which is what "show
nothing at all rather than an empty heading" asks for. Every chapter index on
that screen is shifted by `noteRowCount`, latched in `onEnter` so `loop()` and
`render()` cannot disagree about the shift.

Opening the screen costs one word-wrap pass over the notes that exist (at most
14 paragraphs), done once in `onEnter` and scrolled thereafter — not per render,
which on this device repaints for a cursor move.

**Strings** are English-only for now; `gen_i18n.py` fills a missing key from
English and says so in its report, so Spanish inherits them until translated.

---

## 3. Every book-specific decision found, and what happened to it

A survey of the whole EPUB pipeline (`lib/Epub/`, `lib/ZipFile/`, `lib/EpdFont/`,
`src/activities/reader/`) turned up **102** places where the firmware decides
something on a book's behalf, works around a defect in it, overrides what it
asked for, or drops content — where the reader is never told and it reaches at
most a log line.

**Only some of those are notes.** The selection rule, and it is the whole
argument of this section:

1. **Book-specific.** It happens to some books and not others. A decision that
   applies identically to every book is not a note about the book, it is this
   app's typography, and putting it on a per-book screen would be a lie about
   where it came from.
2. **Reader-visible.** Someone who knows what the publisher shipped could tell.
3. **Already known.** A parse the firmware performs anyway discovers it — no new
   pass, no new file read.

Fourteen passed all three and were surfaced on the day this was written; two
more (#52 and #38) were taken later the same day, and #23 later still, bringing
it to **seventeen**.
The other 85 are listed below with the test they fail, because "checked X, decided against, here is why" is the half a
summary drops and the half that stops the same candidate being re-proposed
forever.

### Surfaced (17, three of them added later the same day)

| Note | Raised at | Scope | Frequency |
|---|---|---|---|
| `Drm` — encryption.xml present | [Epub.cpp:224-252](../lib/Epub/Epub.cpp) `scanZipForBookNotes` | book | some books |
| `EmbeddedFontsIgnored` — the zip ships .ttf/.otf/.woff and nothing loads them | same pass | book | every book that ships fonts |
| `NoTableOfContents` — neither nav nor NCX parsed | [Epub.cpp](../lib/Epub/Epub.cpp), `tocParsed` false | book | some books |
| `TocEntriesUnresolved` — a contents entry points at no spine item | [BookMetadataCache.cpp:430,444](../lib/Epub/Epub/BookMetadataCache.cpp) | book | some books |
| `SpineEntriesMissing` — an itemref names a manifest id that does not exist | [ContentOpfParser.cpp:303-311](../lib/Epub/Epub/parsers/ContentOpfParser.cpp) | book | some books |
| `NoHyphenationForLanguage` — only en/es tries ship | [Hyphenator.cpp setPreferredLanguage](../lib/Epub/Epub/hyphenation/Hyphenator.cpp) | book | most non-EN/ES books |
| `StylesheetPartlyUnderstood` — unsupported selectors, and at-rule bodies (incl. every `@media`) | [CssParser.cpp](../lib/Epub/Epub/css/CssParser.cpp), selector reject + `@` branch | book | nearly every styled book |
| `StylesheetSkipped` — rule cap, oversized selector text, low heap | [CssParser.cpp](../lib/Epub/Epub/css/CssParser.cpp), four sites | book | some books |
| `VerticalWritingIgnored` — `writing-mode: vertical-*` | [CssParser.cpp](../lib/Epub/Epub/css/CssParser.cpp), parsed for the note only | book | rare, but total when it happens |
| `AlignmentOverridden` — the book asked for an alignment and got Justify | [ChapterHtmlSlimParser.cpp:1606-1616](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp) | layout | every book with a title page, epigraph or verse |
| `JustificationDemoted` — set ragged, with the character count | [ParsedText.cpp:769-778](../lib/Epub/Epub/ParsedText.cpp) | layout | every book at the shipped 18 pt X3 default |
| `ImagesDropped` — no picture and no alt text, counted | [ChapterHtmlSlimParser.cpp](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp), the no-alt skip | layout | common (SVG covers) |
| `TablesFlattened` — reflowed into paragraphs, or a nested one dropped | [ChapterHtmlSlimParser.cpp](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp), `<table>` branch | layout | some books |
| `PreformattedCollapsed` — `<pre>` loses its spacing and line breaks | [ChapterHtmlSlimParser.cpp](../lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp), tag check | layout | some books |
| `TextEncodingUnsupported` — a file declares an encoding with no table here, carrying its NAME (added 2026-08-23) | [XmlEncodingSupport.cpp](../lib/Epub/Epub/parsers/XmlEncodingSupport.cpp) | book | rare, and total when it happens |
| `MissingGlyphs` — distinct codepoints the reading face has no shape for (added 2026-08-23) | [MissingGlyphLedger.h](../lib/EpdFont/MissingGlyphLedger.h), raised from `ChapterHtmlSlimParser` | layout | any book outside the reading font's script |
| `CssUnitsUnsupported` — a length used a unit with no honest conversion, carrying its NAME (added 2026-08-23) | [CssParser.cpp](../lib/Epub/Epub/css/CssParser.cpp), `acceptLength` / `acceptEdgeShorthand` | book | some books |

Note the two that are effectively universal (`StylesheetPartlyUnderstood`,
`JustificationDemoted`) are kept anyway: the first because the COUNT is
book-specific even when the fact is not, the second because the owner asked for
it by name and its character figure is the answer to "why does this look loose".

### Not surfaced (85), by the test each fails

#### Fails test 1 — the app's typography, identical for every book

These are not facts about a book. Putting them on a per-book screen would tell
the reader something true and mislead him about its source, and would bury the
book-specific notes in a list nobody finishes.

| # | Decision | file:line |
|---|---|---|
| 3 | A heading with no CSS `text-align` is forced to Center | `ChapterHtmlSlimParser.cpp:1546` |
| 4 | `h1`–`h3` always force a page break | `ChapterHtmlSlimParser.cpp:1566` |
| 5 | Chapter sinkage: viewportHeight/5 at each section start, replacing the file's space-before | `ChapterHtmlSlimParser.cpp:2450` |
| 6 | Inter-block gap capped at lineHeight/2 | `ChapterHtmlSlimParser.cpp:2683` |
| 7 | Bottom spacing capped at lineHeight/2 | `ChapterHtmlSlimParser.cpp:2740` |
| 8 | Space-before collapses at page top | `ChapterHtmlSlimParser.cpp:2673` |
| 9 | Every vertical advance rounds up to a line-height multiple (line grid) | `ChapterHtmlSlimParser.cpp:2489` |
| 15 | Links are not underlined and cannot be followed inline | `ChapterHtmlSlimParser.cpp:1505` |
| 16 | Default first-line indent of 3 space-widths when the book defines none | `ParsedText.cpp:699` |
| 18 | Justification stretches gaps with no maximum | `ParsedText.cpp:245` |
| 22 | Only 18 CSS properties understood; `font-size`, `font-family`, `line-height`, `color`, `background`, `border`, `float`, `list-style`, `white-space`, `page-break-*`, `text-transform` read and discarded | `CssStyle.h:76`, `CssParser.cpp:370` |
| 33 | Every `.css` under the OPF dir merges into ONE global rule set; per-chapter `<link>` is never read | `Epub.cpp:222`; no `"link"` handler |
| 35 | `<style>` in `<head>` is never parsed as a stylesheet | `ChapterHtmlSlimParser.cpp:63` |
| 41 | Images downscaled to fit; never enlarged | `ChapterHtmlSlimParser.cpp:1300` |
| 45 | Alpha composited onto white unconditionally | `PngToFramebufferConverter.cpp:167` |
| 53 | U+FEFF and U+202F stripped | `ChapterHtmlSlimParser.cpp:2196` |
| 59 | Nav preferred, NCX only as fallback | `Epub.cpp:523` |
| 63 | A spine item with no TOC entry inherits the previous section's title | `BookMetadataCache.cpp:310` |
| 67 | EPUB2 guide `type="text"` ignored as a start position | `ContentOpfParser.cpp:322` |
| 71 | Tables never rendered as tables (this is the mechanism note 13 reports) | `ChapterHtmlSlimParser.cpp:387` |
| 74 | `epub:type="pagebreak"` elements dropped (print page numbers lost) | `ChapterHtmlSlimParser.cpp:1464` |
| 75 | `display:none` subtrees dropped — correct behavior, not a defect | `ChapterHtmlSlimParser.cpp:938` |
| 80 | Word buffer capped at 200 bytes; longer runs split with a continuation flag | `ChapterHtmlSlimParser.h:26` |
| 81 | Soft flush at 750 words (320 with CSS) re-lays out the block remainder | `ChapterHtmlSlimParser.cpp:29` |
| 86 | Bulk token reserve skipped on a tight heap (performance only) | `ParsedText.cpp:512` |

Items 10, 11, 12, 14, 17, 21 (partly), 25, 34, 37, 65, 73, 90, 98, 99 fall here
too: they are conditioned on the book but their effect is a spacing or a marker
shape the reader cannot compare against anything, and every one of them would
fire on a large fraction of books. Specifically —

| # | Decision | file:line | why not |
|---|---|---|---|
| 10 | Margin/padding clamped to 2 em per side | `BlockStyle.h:16,127` | a clamp that prevents a 1-word column; the book is more readable, not less |
| 11 | Accumulated inset clamped to 2/5 viewport | `ChapterHtmlSlimParser.cpp:79` | same, and it is the fix v41 shipped |
| 12 | Lists get an invented 1.5 em indent when the publisher gave none | `ChapterHtmlSlimParser.cpp:1610` | a UA default, which every renderer supplies |
| 13 | `list-style-type` unsupported (roman, alpha, none) | `ChapterHtmlSlimParser.cpp:1638` | borderline; left out because a marker shape is the least of what a 528 px page changes |
| 14 | `<li>` marginLeft widened to fit the marker gutter | `ChapterHtmlSlimParser.cpp:1674` | invisible correction |
| 17 | An unresolvable percentage `text-indent` falls back to the 3-space default | `BlockStyle.h:136` | identical to the no-indent default, so nothing distinguishes it on the page |
| 20 | Multi-class selectors never match | `CssParser.cpp:727` | already inside the `StylesheetPartlyUnderstood` count |
| 24 | ~~`pt`→`px` is a fixed x1.33 regardless of DPI~~ | `CssStyle.h:35` | **TAKEN 2026-08-23 with #23, and it was not a policy -- it was the 96 dpi answer reached without the question being asked.** x2.0833 now, and `CssUnit::Points` is gone: every absolute unit converts at parse time |
| 25 | Any `display:` but `none` normalizes to Block | `CssParser.cpp:442` | inside the same count in spirit; reporting it separately would fire on nearly every book with no actionable content |
| 34 | Byte-identical duplicate stylesheets deduped | `Epub.cpp:326` | provably lossless |
| 37 | CSS `font-family` ignored | `CssParser.cpp:370` | reported by `EmbeddedFontsIgnored` where it matters |
| 65 | No declared cover → first path containing "cover", else the largest image ≥ 8 KB | `Epub.cpp:268` | a guess about a thumbnail, visible on Home, not in the book |
| 73 | `<hr>` inside a table dropped | `ChapterHtmlSlimParser.cpp:1126` | rare and inconsequential |
| 90 | With no hyphenator a generic 2/2 fallback allows a break anywhere in a word | `Hyphenator.cpp:243` | the consequence of `NoHyphenationForLanguage`, already surfaced |
| 98 | `javascript:` links dropped | `ChapterHtmlSlimParser.cpp:1487` | open TODO, and no reader can act on it |
| 99 | `<aside epub:type="footnote">` rendered inline | `ChapterHtmlSlimParser.cpp:1476` | visible but reads as ordinary text; candidate for a later note |

#### Fails test 3 — would need a new pass, a new read, or plumbing that does not exist

| # | Decision | file:line | what it would cost |
|---|---|---|---|
| 23 | ~~Unknown length units silently fall through to PIXELS~~ | `CssParser.cpp`, `CssUnits.h` | **TAKEN 2026-08-23.** `cm`/`mm`/`Q`/`in`/`pt`/`pc` convert at 150 dpi -- the resolution this firmware's own type is rasterized at, not the panel's 257 ppi and not the web's 96, so a point of margin equals a point of type. A unit with no honest conversion (`ex`, `ch`, `vw`, `vh`, or a typo) DROPS its declaration and raises `CssUnitsUnsupported`, carrying the unit's name. `!important` also stopped being glued to the unit |
| 26/27/28/29/30/31 | CSS caps, oversized selectors, low heap, file > 128 KB | `CssParser.cpp:58,513,610,497`; `Epub.cpp:368,377` | **partly surfaced** as `StylesheetSkipped`; the 128 KB and heap refusals in `Epub.cpp` are not yet wired |
| 32 | `resolveStyle` returns an empty style for every element while free heap < 48 KB | `CssParser.cpp:708` | per-element and transient; a note raised from it would depend on what else was running |
| 38 | ~~A codepoint the reading face lacks draws NOTHING~~ | `EpdFont.cpp` last-resort branch; `SdCardFont.cpp` advance table | **TAKEN 2026-08-23, and the description above is wrong.** `EpdFont::getGlyph` has substituted U+FFFD, then `'?'`, since B-009: an SD reading face draws the diamond and the built-in Libre Franklin draws a bare question mark, which is worse than empty space because it reads as content. Now a `.notdef` box where the substitute was `'?'`, plus a layout-scope note counting distinct uncoverable codepoints. The seam was one level below `GfxRenderer`, in `lib/EpdFont`, in the two places that decide a face cannot draw a codepoint |
| 43 | Progressive JPEG decoded DC-only: 1/8 resolution, upscaled | `JpegToFramebufferConverter.cpp:429` | inside the image decoder, which runs at RENDER time, not parse time |
| 44 | PNG bit-depth combos that overflow the row buffer abort the decode | `PngToFramebufferConverter.cpp:388` | same |
| 46 | After 16 image failures in a session, further failures are not retried | `ImageBlock.cpp:67` | session state, not book state |
| 47 | An image positioned outside the panel is skipped | `ImageBlock.cpp:336` | render time |
| 52 | ~~No `XML_SetUnknownEncodingHandler` anywhere~~ | `XmlEncodingSupport.cpp`, installed at all five `XML_ParserCreate` sites | **TAKEN 2026-08-23.** 29 single-byte code pages (7,424 B of generated table); the multi-byte CJK encodings are refused on purpose, because this firmware carries no CJK reading face and decoding GBK into replacement marks buys nothing. A refusal now raises a book note NAMING the encoding. Reproduced first: the reader was not missing a chapter, it was stuck — page-forward retried the failing chapter forever |
| 54 | Unknown HTML entities emitted literally as `&foo;` into the prose | `ChapterHtmlSlimParser.cpp:2010` | per-entity, cheap to count — candidate |
| 55 | CDATA, PIs and DOCTYPE internal subset text dropped | `ChapterHtmlSlimParser.cpp:2015` | correct behavior |
| 62 | ~~TOC href→spine lookup accepts the first 64-bit hash + length match without comparing the href~~ | `BookMetadataCache.cpp:425` | **TAKEN 2026-08-23.** The hash is a filter now: the index carries each entry's byte offset in the temp spine file, and every candidate the filter admits has its stored href read back and compared. Costs no RAM -- the extra `uint32_t` fits the padding the old struct already had |
| 78 | After 1024 anchors per chapter no further IDs are recorded; those links land at page 0 | `ChapterHtmlSlimParser.cpp:44` | candidate; needs a counter |
| 79 | Consecutive non-block elements with IDs can overwrite a pending anchor | `ChapterHtmlSlimParser.cpp:892` | documented residual case |
| 82/83/84 | A laid-out LINE with > 10,000 words, > 65,535 text bytes, or a failed arena allocation is DROPPED | `TextBlock.cpp:57,76,87` | 82 and 83 effectively never fire (they guard a line, not a paragraph). 84 is real under memory pressure and is a candidate |
| 85 | A paragraph too big for the largest free heap block loses ALL its CJK break opportunities | `ParsedText.cpp:205` | candidate, CJK only |
| 87 | A corrupt cached page is discarded and re-indexed | `Page.cpp` (12 sites) | self-healing; nothing for a reader to do |
| 89 | Language comes from publication-level `dc:language` only; `xml:lang` never read | `Section.cpp:496` | candidate for a multilingual note |
| 91 | ISO-639-2→639-1 mapping covers 11 codes | `Hyphenator.cpp:25` | folded into `NoHyphenationForLanguage` |
| 92 | `writing-mode` — **surfaced**, listed here only because the CSS engine had to learn the property to notice it | `CssParser.cpp` | done |
| 93 | RTL resolved only from CSS `direction` and HTML `dir` | `ChapterHtmlSlimParser.cpp:919` | correct: there is no reliable heuristic |
| 94 | More than 16 footnote links on a page: the rest are not collected | `Page.h:128` | per-page, not per-book |
| 95 | Footnote href truncated to 95 chars | `FootnoteEntry.h:6` | a bug to fix, not to report |
| 96/97 | A footnote whose anchor does not resolve dumps the reader at page 0; an href that resolves to nothing does nothing at all | `EpubReaderActivity.cpp:1244,1948` | belongs in the moment, as a toast, not on a summary screen |
| 100 | Mid-chapter OOM abandons the chapter's layout | `ChapterHtmlSlimParser.cpp:2377` | the reader already gets `STR_INDEX_FAILED` |
| 101 | A BACKGROUND section build failure resets the section with no popup | `EpubReaderActivity.cpp:337` | candidate |
| 102 | A giant spine is paginated only to a watermark; the page count shown is the watermark | `Section.cpp:785` | candidate — it makes the progress figure wrong |
| 39/40/77 | Unsupported image formats, unobtainable dimensions, `<img>` with neither src nor alt | `ChapterHtmlSlimParser.cpp:1161,1401,1424` | **surfaced**, but only the no-alt-text case is COUNTED: an image replaced by its alt text is a substitution, not a loss |
| 48/49/50/51 | encryption.xml (**surfaced**); the zip encrypted bit never checked; members > 16 MB refused; non-STORE/DEFLATE refused | `ZipFile.cpp:410,397,456` | 49-51 all end as "failed to inflate", which is the same outcome the DRM note already explains |
| 56/57 | Missing spine idref (**surfaced**); `linear="no"` ignored, so back-matter appears inline | `ContentOpfParser.cpp:258` | `linear="no"` is a candidate — it changes the reading order visibly |
| 58 | Duplicate NCX entries: later ones ignored | `ContentOpfParser.cpp:227` | provably harmless |
| 60/61 | No TOC (**surfaced**); unresolved TOC hrefs (**surfaced**) | | done |
| 64 | A spine item whose file is missing from the zip gets size 0 and stays in the spine | `BookMetadataCache.cpp:322` | candidate — it is a blank chapter |
| 66 | A cover in an unsupported format yields no cover and no thumbnail | `Epub.cpp:746` | Home screen, not the book |
| 68 | Href→spine resolution falls back to bare filename matching | `Epub.cpp:1044` | latent bug, same family as 62 |
| 69 | ~~`SKIP_TAGS` is only `{"head","rp"}`~~ | `ChapterHtmlSlimParser.cpp:66` | **TAKEN 2026-08-23, and the list above is WRONG for `svg`.** Added: `script`, `style`, `noscript`, `title`, `desc`, `annotation`, `annotation-xml`, `template`, `iframe` -- a `<script>` in the body printed its own source as a paragraph, proved by render. NOT added, each for a reason: `svg` (an SVG-wrapped cover is `<svg><image/></svg>` and `image` is in `IMAGE_TAGS`, so skipping it drops the cover -- its `<title>`/`<desc>` were the actual leak), `math` (`<mi>`/`<mo>` ARE the equation; only `<annotation>`, which duplicates it in TeX, is skipped), `object`/`video`/`audio` (their children are the FALLBACK, shown precisely when the object cannot render, which here is always), `form` (labels read as prose) |
| 70/72 | Nested table text discarded (**surfaced**); column layout abandoned for a cell with an image/link/list (**surfaced** as the same note) | `ChapterHtmlSlimParser.cpp:947,394` | done |

### The four next candidates, in order

1. ~~**#38, missing glyphs draw nothing.**~~ **Done 2026-08-23**, and the
   premise was wrong: they drew a question mark. See
   [encodings-glyphs-and-library-sync-2026-08-23.md](encodings-glyphs-and-library-sync-2026-08-23.md).
2. ~~**#52, no unknown-encoding handler.**~~ **Done 2026-08-23**, single-byte
   code pages only, multi-byte refused with a note. Same document.
3. ~~**#23, unknown CSS units become pixels.**~~ **Done 2026-08-23**, with #24,
   #62 and #69 alongside it. See
   [css-length-units-2026-08-23.md](css-length-units-2026-08-23.md).
4. **#102, watermarked page counts.** It makes a number on screen wrong.

A fifth, promoted by the encoding work: a file whose bytes are windows-1252 and
which **declares no encoding at all** still fails, because expat assumes UTF-8
and the first high byte is invalid. That is sniffing rather than decoding, and
it is a separate repair.

### Re-ranked, after the CSS-unit work

1. **#102, watermarked page counts.** A giant spine is paginated only to a
   watermark and the page count shown IS the watermark, so the progress figure
   on screen is wrong. Still the only remaining item that puts a false NUMBER in
   front of a reader.
2. **The windows-1252 sniff** (the fifth above). A whole book that will not
   open, and the encoding work already built everything but the sniff.
3. **Viewport units, `vw`/`vh`/`vmin`/`vmax`.** Promoted BY the CSS-unit work
   rather than closed by it: they are now dropped-and-noted rather than silently
   read as pixels, which is honest, but they are convertible in principle. The
   case that pays for it is `img { height: 100vh }` on a cover page. It needs a
   viewport HEIGHT threaded through `CssLength::toPixels`,
   `BlockStyle::fromCssStyle` and four `ChapterHtmlSlimParser` call sites.
4. **#54, unknown HTML entities emitted literally as `&foo;`.** Per-entity,
   cheap to count, and the symptom is visible garbage in the prose.
5. **#64, a spine item whose file is missing from the zip** gets size 0 and
   stays in the spine — a blank chapter with nothing said.
6. **#57, `linear="no"` ignored**, so back-matter appears inline. It changes the
   reading order visibly, which is exactly test 2.
7. **#68, href→spine resolution falls back to bare filename matching.** Same
   family as #62, which is now fixed; this one is still open and is the
   remaining way the wrong chapter can be opened silently.

---

## Build, tests, figures

| | Before | After |
|---|---|---|
| `pio run -e simulator_x3` | SUCCESS | SUCCESS |
| `pio run -e gh_release` | SUCCESS, flash 4,960,155 (75.7%), RAM 54,220 (16.5%) | SUCCESS, flash **4,971,771 (75.9%)**, RAM **54,276 (16.6%)** |
| Host tests | 382/384 | **411/413** |

Flash +11,616 bytes, nearly all of it the 31 new i18n strings (the generator
reports 18,210 B of deduped string data for the whole app). RAM +56 bytes: the
`booknotes::Notes` singleton and its `std::string` cache path.

The two failures are unchanged and pre-existing: `EditorFontsTest.cpp:622` and
`SettingDisplayOrderTest.cpp:169`, the two halves of the unfinished 2026-08-09 iA
Writer Quattro ruling. Neither touches anything here.

New suites: `test/ruby_serialization` (18) and `test/book_notes` (11).

### Rendered proof

Headless `simulator_x3`, `SDL_VIDEODRIVER=dummy`, grain off and the palette
forced to black-on-white so the figures are about the text and not the CRT dial.
Landing confirmed from `[ACT] Entering activity:` in every run, not from the
screenshots.

```bash
CROSSPOINT_SIM_GRAIN=0 CROSSPOINT_SIM_DARK=0 \
CROSSPOINT_SIM_PANEL_INK_LIGHT=000000 CROSSPOINT_SIM_PANEL_PAPER_LIGHT=FFFFFF \
SDL_VIDEODRIVER=dummy \
CROSSPOINT_SIM_INPUT_SCRIPT='4000:ENTER;5200:LEFT;6400:ENTER;11000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='5100:chapters.bmp;7800:notes.bmp' \
  .pio/build/simulator_x3/program
```

PNG at native panel pixels (528x792), converted from the simulator's BMP, never
JPEG. Crops are integer NEAREST and say so in the filename. Content coverage is
the fraction of pixels away from the modal background by more than 16 levels.

| Figure | Size | Coverage | Kind |
|---|---|---|---|
| `chapters.png` — Select Chapter, `Book Notes (2)` at row 0, Wingspan | 528x792 | 7.9% | context |
| `notes_row_crop_2x_nearest.png` — that row at 2x | 1056x164 | 15.1% | evidence |
| `notes1.png` — the notes screen, Wingspan (30 chars/line, 7 CSS rules) | 528x792 | 14.0% | evidence |
| `notes_ai_engineering.png` — a second real book (30 chars/line, 9 rules) | 528x792 | 16.2% | evidence |
| `chapters_no_notes.png` — a book with NO notes: no row, nothing at all | 528x792 | 3.0% | evidence |
| `ruby_latin.png` — ruby fixture rendered through the v45 encoding | 528x792 | 6.4% | context |
| `ruby_annotations_crop_2x_nearest.png` — the annotations at 2x | 1056x300 | 10.3% | evidence |

The negative case was produced by dropping the reading size to 12 pt, which
takes the measure above 40 characters and retires the only note that book had —
the same remedy the note's own text recommends.
