# CSS length units, and two leaks — 2026-08-23

Sweep item **#23** of
[book-notes-and-sparse-ruby-2026-08-23.md](book-notes-and-sparse-ruby-2026-08-23.md),
which that document's later re-rank put at the top: *"unknown length units
(`cm`, `mm`, `in`, `ex`, `ch`, `vw`, `vh`) silently fall through to PIXELS —
`margin: 1cm` becomes 1 px."* Taken with two of the latent bugs the same sweep
filed and did not fix, **#62** (a TOC href matched by hash without comparing the
string) and **#69** (`SKIP_TAGS` is only `{head, rp}`).

`SECTION_FILE_VERSION` 46 → **47**. `CssParser::CSS_CACHE_VERSION` 8 → **9**.
`notes.bin` version 2 → **3** (45 bytes → 53). Sixteen book notes → **seventeen**.

---

## 1. What each unit did before

`CssParser::tryInterpretLength` scanned the number, then compared the remainder
against exactly four spellings. Everything else fell out of the `if` chain
holding `CssUnit::Pixels`, which is the enum's zero value — so an unrecognized
unit was not an error, it was **pixels**, and nothing logged.

| Unit | Before | After |
|---|---|---|
| *(none)*, `px` | pixels | unchanged |
| `em`, `rem` | × em size | unchanged (both still × em size; there is no separate root size) |
| `%` | × container width / 100 | unchanged |
| `pt` | **× 1.33** — the 96 dpi answer, arrived at without the question being asked | **× 2.0833** (150 dpi) |
| `cm` | **× 1** — `1cm` was ONE PIXEL | × 59.06 |
| `mm` | **× 1** | × 5.906 |
| `in` | **× 1** — `1in` was one pixel, `0.5in` was zero | × 150 |
| `pc` | **× 1** | × 25 |
| `Q` | **× 1** | × 1.476 |
| `ex`, `ch` | **× 1** | declaration DROPPED, book note raised |
| `vw`, `vh`, `vmin`, `vmax` | **× 1** | declaration DROPPED, book note raised |
| anything else (`fr`, `deg`, a typo like `pts`) | **× 1** | declaration DROPPED, book note raised |

A second, separate bug in the same function: **`!important` reached the unit
scan glued to the unit.** `margin-top: 1cm !important` presented the unit as
`"cm !important"`, which matched nothing and read as pixels; and in the
shorthand, `margin: 1em !important` tokenized as TWO values, so the right and
left sides came out zero. It is stripped before the number is read now. This had
to be fixed as part of the change rather than after it — with an unrecognized
unit now DROPPING its declaration, leaving `!important` attached would have
fired the new book note on the majority of styled books and named a unit that
does not exist.

## 2. The conversion basis, and how it was established

**150 dpi.** [lib/Epub/Epub/css/CssUnits.h](../lib/Epub/Epub/css/CssUnits.h) is
the single definition; the whole argument is in that header's comment and is
summarized here.

Three numbers were candidates:

| Basis | `1cm` on X3 | Where it comes from |
|---|---|---|
| 96 dpi | 37.8 px | the CSS reference pixel — what a browser uses, and what this firmware would get by accident |
| **150 dpi** | **59.1 px** | **this firmware's own font rasterization** |
| ~257 ppi | 101 px | the X3 panel's true resolution: 792 × 528 over a 3.7" diagonal ([hardware-dimensions.md](hardware-dimensions.md)) |

The panel's true ppi is the wrong anchor, and this is the part worth recording
because "use the real panel geometry" is the obvious instinct. **This renderer
already has a physical-to-pixel ratio, and has had one since its first font was
built.** Every reading face, built-in and on the card, is rasterized by
`lib/EpdFont/scripts/fontconvert.py` and `fontconvert_sdcard.py` at
`face.set_char_size(size << 6, size << 6, 150, 150)`, and both scripts state the
consequence in their own comments as `ppem = pt * 150 / 72`. Confirmed against
the shipped data rather than the build script: `librefranklin_reader_18_regular`
reports `advanceY = 45`, which is 1.2 × the 37.5 px ppem that formula gives.

So **a point already means 2.083 px in this renderer.** Anchoring CSS lengths
anywhere else would make `margin: 12pt` and `font-size: 12pt` two different
physical sizes inside one book — at the panel's true 257 ppi a 12 pt margin
would stand 2.4× the height of the 12 pt type beside it. One renderer, one
point. `tests/CssUnitsTest.cpp:APointOfMarginIsAPointOfType` is that argument as
an assertion.

This also closes survey item **#24** ("`pt`→`px` is a fixed ×1.33 regardless of
DPI"), which was filed as a unit policy rather than a bug. It was the 96 dpi
answer, and it was wrong for the same reason the rest were.

Note the basis is a **compile-time constant on both boards**, not a per-device
number: the fonts are rasterized at 150 dpi for X3 and X4 alike, so the coherent
anchor is the same on both even though their panels are 257 and 219 ppi.

## 3. What an unconvertible unit does now, and why that

**The declaration is dropped**, exactly as a browser drops a declaration with an
invalid value: the property keeps whatever the cascade left it, which for a
margin is the reader's own. Not clamped, not zeroed, and emphatically not
"treated as pixels".

The alternatives were weighed:

* **Approximate it.** `ex` and `ch` have spec-sanctioned fallbacks (0.5 em).
  Rejected because the `emSize` this layer is handed is the font's LINE height
  (45 px at 18 pt), not its em (37.5 px) — deriving an x-height from it would
  compound one approximation with another, and the result would be wrong by a
  fifth before the 0.5 was applied.
* **Clamp to zero.** Indistinguishable from the bug being fixed.
* **Inherit.** This is what dropping the declaration achieves.

`vw`/`vh`/`vmin`/`vmax` are in the same bucket, and they are the ones **left on
the table**: they are convertible in principle, but `CssLength::toPixels` is
given an em size and a container width and no viewport HEIGHT, so resolving `vh`
would mean threading a second dimension through `BlockStyle::fromCssStyle` and
four call sites in `ChapterHtmlSlimParser`. Worth doing for one case in
particular — `img { height: 100vh }` on a cover page is real and common — and
deliberately not done here.

**The note.** `booknotes::Note::CssUnitsUnsupported`, book scope (the stylesheet
is parsed once, at book load; a layout-scope note would be dropped by the next
render pass's fingerprint). It carries the **name** of the first unconvertible
unit in `Details::unsupportedCssUnit[8]`, on the same terms as
`TextEncodingUnsupported` carries an encoding name: "vh" and "ex" are different
problems with the same symptom, and a reader comparing the page against the
publisher's file can only act on the name.

Deliberately NOT raised for `auto` / `inherit` / `initial`. Those are keywords,
not units, they have always resolved to zero here, and `margin: 0 auto` is on a
large share of books — a notice that fires on nearly every book says nothing
about any of them. `CssParser::LengthParse` exists precisely to keep "not a
length" and "a length in a unit I cannot convert" apart.

## 4. The measured page

Fixture: `tools/make_css_units_book.py` (four chapters). The first sets the
**same physical distance** five ways — `1cm`, `10mm`, `0.3937in`, `2.3622pc`,
`28.3465pt` — so the five paragraphs must land on one left edge or the
conversion is wrong.

Both arms are real builds. "Before" is `simulator_x3` with `CssUnits.h` reverted
to the fall-through-to-pixels behavior and `SKIP_TAGS` back to `{head, rp}`;
the cache was wiped and `state.json` re-seeded identically for every run, so no
arm inherited the other's page position.

```bash
python3 tools/make_css_units_book.py fs_/books/cssunits.epub
rm -rf ./fs_/.crosspoint/ && mkdir -p ./fs_/.crosspoint
printf '%s' '{"openEpubPath":"/books/cssunits.epub","readerActivityLoadCount":0,...}' \
  > fs_/.crosspoint/state.json
CROSSPOINT_SIM_GRAIN=0 CROSSPOINT_SIM_DARK=0 \
CROSSPOINT_SIM_PANEL_INK_LIGHT=000000 CROSSPOINT_SIM_PANEL_PAPER_LIGHT=FFFFFF \
SDL_VIDEODRIVER=dummy CROSSPOINT_SIM_INPUT_SCRIPT='9000:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='6000:after_ch0.bmp' .pio/build/simulator_x3/program
```

**Leftmost ink column, measured off the render** (X3, 792 × 528 portrait,
LibrisADF 18 pt; the `NONE` paragraph is the reader's own margin):

| Paragraph | Before | After | Asked for |
|---|---|---|---|
| `margin-left: 1cm` | **11 px** — flush | **68 px** | 59 px past the margin |
| `margin-left: 10mm` | 21 px | 68 px | same |
| `margin-left: 0.3937in` | **11 px** — flush | 69 px | same |
| `margin-left: 2.3622pc` | 13 px | 68 px | same |
| `margin-left: 28.3465pt` | 48 px | 69 px | same |
| `NONE` (control) | 9–11 px | 9–11 px | — |

Before: five different edges, four of them within 4 px of no margin at all.
After: one edge at 68–69 px — 57 px further right than the SAME paragraph in the
before arm, against the 58 px the change adds to it (59 px of margin where there
had been 1). The remaining pixel is the glyph origin's fixed-point phase, and
the ±1 px across the five rows is the glyphs' own left side bearings (`C`
against `M` against `I`), not the margin. Nothing here is clamped:
`MAX_HORIZONTAL_INSET_EM` is 2 em and the measured line pitch on this page is
~42 px, so the ceiling is ~84 px.

Chapter 3 sets `5ex`, `12ch` and `13vw`. All three sit flush with the control —
dropped, not turned into 5, 12 and 13 pixels.

### What the new numbers now meet: the clamps

A margin that was 1–2 px never met a clamp. Real ones do, and this is the
change's actual cost. Measured at the X3 default (viewport 450 px, `emSize` —
the font's line height — 45 px):

| Clamp | Threshold | What newly hits it |
|---|---|---|
| `BlockStyle::MAX_HORIZONTAL_INSET_EM`, 2 em per side | **90 px** | `2cm` (118 px) and `1in` (150 px) clamp to 90. `1cm` (59) and `0.5in` (75) do not |
| the same, for `pt` | **43.2 pt**, down from 67.7 pt | any horizontal `pt` inset between 44 and 67 pt newly clamps |
| `clampAccumulatedHorizontalInsets`, 2/5 of the viewport | **180 px total** | `blockquote { margin: 1cm; padding: 1cm }` is 236 px, rescaled to 180 — the measure drops 450 → 270 px. It was 4 px before |
| vertical margin + padding | **none** | `p { margin: 1in }` is 150 px above AND below every paragraph out of ~750 px of page: about two paragraphs to a page |

The vertical case is the one to watch, and it is **deliberately not clamped
here**. Honoring the publisher is the correct behavior and it is what the fix is
for; capping vertical insets at a fraction of the page is a separate typography
decision with its own blast radius, and it belongs to the owner rather than to
this change. What IS fixed is the undefined behavior underneath it (section 7).

### Figures

Lossless PNG at native panel pixels (528 × 792), converted from the simulator's
BMP, never JPEG. Crops are integer **2× NEAREST** and say so in the filename.
Coverage is the fraction of pixels away from the modal background by more than
16 levels. Written to the session scratchpad
(`.../scratchpad/cssunits/`).

| Figure | Size | Coverage | Kind |
|---|---|---|---|
| `units_before_crop_2x_nearest.png` — five units, five edges | 380×994 | 9.7% | evidence |
| `units_after_crop_2x_nearest.png` — five units, one edge | 380×994 | 10.4% | evidence |
| `noconvert_before_crop_2x_nearest.png` — `ex`/`ch`/`vw` as pixels | 380×704 | 10.0% | evidence |
| `noconvert_after_crop_2x_nearest.png` — dropped, flush with the control | 380×704 | 9.9% | evidence |
| `notprose_before_crop_2x_nearest.png` — JS, CSS and SVG title printed as prose | 1056×648 | 10.6% | evidence |
| `notprose_after_crop_2x_nearest.png` — gone; the SVG image still renders | 1056×648 | 10.3% | evidence |
| `notes_css_units_crop_2x_nearest.png` — the note, naming `ex` | 1056×580 | 18.6% | evidence |
| `before_ch0.png` / `after_ch0.png` — the whole page | 528×792 | 5.2% | context |
| `before_ch2.png` / `after_ch2.png` — the whole page | 528×792 | 3.9% | context |
| `before_ch3.png` / `after_ch3.png` — the whole page | 528×792 | 7.0% / 4.5% | context |
| `after_notes.png` — the Book Notes screen | 528×792 | 15.3% | context |
| `chaplist.png` — Select Chapter, `Book Notes (2)` at row 0 | 528×792 | 3.4% | context |

Effect delta against the figure's own baseline, whole page: absolute units
mean 16.40 / max 251 / 9.0% of pixels moved more than 4 levels; no-conversion
7.51 / 253 / 4.2%; not-prose 17.93 / 253 / 9.6%.

The reading pages are 4–7% ink at 18 pt with this leading, which is under the
10% floor, so every judgment figure is a **crop of the region under judgment**
rather than a page — the left-edge column for the unit chapters, and the top
half for the leak.

## 5. #69 — the skip set, and where the sweep is wrong

`SKIP_TAGS` was `{"head", "rp"}`, so a `<script>` or `<style>` anywhere in the
body emitted its **source** into the page as text. Proved by render: the before
arm prints `var LEAKED_SCRIPT = "this must not appear"; .LEAKED_STYLE { color:
red; }` as a paragraph.

Added: `script`, `style`, `noscript`, `title`, `desc`, `annotation`,
`annotation-xml`, `template`, `iframe`.

**The sweep's item #69 is wrong as written for `svg`.** It lists `<svg>` first
among the tags that ought to be skipped. Skipping the `<svg>` subtree would drop
the **cover of a large share of books**: an SVG-wrapped cover is
`<svg><image xlink:href="cover.jpg"/></svg>`, and `image` is in `IMAGE_TAGS`
(`ChapterHtmlSlimParser.cpp:65`, with the `xlink:href` attribute read at
`:1150`). The actual leak from an `<svg>` is its `<title>` and `<desc>`, which
are now skipped by name. The render proves both halves: the leaked title and
description are gone, and the image still draws.

Three more of #69's candidates are deliberately NOT skipped, for reasons that
belong in the record so they are not re-proposed:

* `math` — `<mi>`/`<mn>`/`<mo>` **are** the equation. Leaking `x2+1` is poor;
  dropping the equation entirely is worse. Only `<annotation>`, which repeats
  the same expression in TeX or content MathML, is skipped, and that one is pure
  duplication.
* `object`, `video`, `audio` — their child content is the **fallback**, shown
  precisely when the object cannot be rendered, which here is always. Skipping
  it would remove the only thing a reader gets.
* `form` — its text is mostly labels, which read as prose. Rare in an EPUB and
  not clearly a defect.

## 6. #62 — the TOC href that was never compared

`BookMetadataCache::createTocEntry` looked a contents href up in
`spineHrefIndex` (built only for spines of 400+ items) and **accepted the first
entry whose 64-bit FNV-1a hash and length matched, without ever comparing the
string**. A collision opened the wrong chapter, silently.

The hash is a filter now, never the answer: `SpineHrefIndexEntry` carries the
byte offset of that entry in the temp spine file, the loop walks every candidate
the filter admits, reads the stored href back and compares it. Verified rejects
log and move on.

**It costs no RAM.** The struct was `uint64_t + uint16_t + int16_t`, which pads
to 16 bytes; it is now `uint64_t + uint32_t + uint16_t + int16_t`, which packs
into the same 16. The added I/O is one seek and one string read per TOC entry,
against the linear scan of up to `spineCount` entries that this index replaced —
still the reason the index exists.

Not covered by a host test: `BookMetadataCache` needs real `HalStorage` and a
400-item spine, and forcing a genuine FNV-1a-64 collision to order is a
different piece of work. Stated rather than claimed.

## 7. What the adversarial review caught

A read-only refuting pass over the finished diff, before any of it was reported
as done. It found **one bug this change introduced** and four other things worth
recording. All are fixed here except where stated.

**1. The lookup key I broke while fixing #62 (HIGH).** Inserting `hrefOffset` as
the SECOND member of `SpineHrefIndexEntry` silently re-bound the braced probe
`SpineHrefIndexEntry{targetHash, targetLen, 0}` — the length went into the
offset and the probe's length became **0**. It compiles clean (uint16→uint32 is
not narrowing) and it is invisible on every book without a collision, which is
every book but the one the hardening exists for: `lower_bound` lands on the
shortest member of the hash group, the `hrefLen == targetLen` guard fails
immediately, and the entry resolves to nothing. The probe is built field by
field now, with a comment saying why it is not a braced list.

**2. `toPixelsInt16` had no bound, and absolute units made overflow reachable
(MEDIUM).** `static_cast<int16_t>` from a float outside the type's range is
undefined behavior. It used to need `margin-top: 40000px`; `500in` is 75,000 px
and `220in` already passes 32,767. The vertical margins have no layout clamp
above it (see the table in section 4), so nothing else stood in the way. Clamped
in `CssStyle.h`, NaN included — `strtof` accepts the spelling.

**3. The header's own argument overstated what is observable (MEDIUM).** It said
`margin: 12pt` and `font-size: 12pt` would otherwise be different sizes "in the
same book". A book cannot set 12 pt type here: `font-size` is read and
discarded. And `em` resolves against the font's **advanceY**, not its em box, so
`margin: 18pt` (37.5 px) and `margin: 1em` (45 px) differ by 20% in this
renderer. The 150 dpi basis survives both — the parity that fixes the number is
between a book's `pt` and the type the reader is actually looking at — but the
comment now says so, and both limits are written down rather than glossed.
`APointOfMarginIsAPointOfType` asserts against the converter's formula, which is
the claim that remains true.

**4. The clamp thresholds** — quantified in section 4 above, which is where they
belong.

**5. `!important` was stripped on the length paths and still not on the keyword
ones, and one of them failed CLOSED TO THE WRONG VALUE (LOW).** Pre-existing,
not caused by this diff, and fixed because the diff's own rationale applies to
them identically: `interpretAlignment` returns `Left` for anything it does not
match, and the caller then sets `defined.textAlign` — so
`text-align: center !important` did not fall back, it **forced left**. Headings
are the one block whose CSS alignment is honored, so it was visible on the page.
`font-style`, `font-weight`, `text-decoration` and `vertical-align` took the
same treatment.

**6. `SKIP_TAGS` matches by exact `strcmp`**, so `<svg:title>` with an explicit
prefix, or `<TITLE>` in a non-lowercased document, does not match. Left as is:
default-namespaced SVG is the overwhelming case, and this is a limitation of the
existing matcher rather than anything the change introduced.

Reported CLEAN, checked and found nothing: the `unitIs` / `raiseUnsupportedCssUnit`
/ shorthand-array memory safety; the keyword and percentage regression surface
(`margin: 0 auto`, `text-indent: inherit`, `calc(...)`, `width: 100%`); the
`SKIP_TAGS` collateral (`<style>` does not break the CSS pipeline — the style
ATTRIBUTE is read in `startElement` before the skip branch; `ChapterHtmlSlimParser`
has exactly one caller, `Section.cpp:496`, so no plain-text or notes reader uses
it; metadata and cover detection go through the OPF); the TOC-pass file cursor
and `readString`'s bounds check; the `SpineHrefIndexEntry` size on the RISC-V
ILP32 ABI (**verified with the actual `riscv32-esp-elf-g++`**: old and new both
16 bytes); and `notes.bin` v3's version rejection, truncation check and forced
termination.

## 8. Tests

`test/css_units/` — **28 tests**. It compiles the **real `CssParser.cpp`**
host-side against local stubs for `Arduino.h`, `Logging.h` and `HalStorage.h`.
Nothing else in `test/` compiled that file, and that is not a coincidence: the
unit fall-through lived in the one file in `lib/Epub` with no host coverage at
all.

Validated failing-first: against the pre-fix semantics (unknown unit → pixels,
`pt` × 1.33, no absolute units) **18 of the 28 fail**.

What it pins:

* the basis is 150, with the derivation named, so moving it fails here rather
  than on a page — and the assertion that a point of margin equals a point of
  type, which is the argument for that number;
* every absolute unit's conversion, both as a table and end to end through the
  declaration path;
* the headline figures (`1cm` = 59.055 px, `1in` = 150, `1pt` = 2.0833), so a
  silent basis change cannot pass;
* units are ASCII case-insensitive but `%` is not reached by the letter compare;
* a PREFIX of a known unit is not that unit — `pts`, `inch`, `cms`, `emu` must
  not be taken for `pt`, `in`, `cm`, `em`;
* an unconvertible unit leaves `defined` clear, and **leaves an earlier
  declaration standing** — writing `out` on the way to rejecting it would
  replace a good margin with a bad one;
* the note names the unit, the first one wins, and it does not churn;
* one bad component drops the whole shorthand, and the 1-to-4 expansion is
  otherwise unchanged;
* `!important` comes off before the unit is read, in both the single-value and
  the shorthand path;
* a KEYWORD (`auto`, `inherit`, `initial`) is not an unconvertible unit and
  raises nothing, and neither is a value that is not one length at all
  (`10px 20px`, `1cm/2`) — naming `px 20px` as a unit in a notice a person reads
  would be worse than the old silence;
* the int16 boundary: `500in` clamps to `INT16_MAX`, `-900cm` to `INT16_MIN`,
  32,000 px still converts exactly, and NaN lands on 0 rather than on undefined
  behavior;
* `!important` comes off the KEYWORD paths too, `text-align: center !important`
  above all — it used to force LEFT;
* the note is book scope and survives a layout-fingerprint change.

Host suite: **452/454 → 480/482**. The two failures are unchanged and
pre-existing — `EditorFontsTest` and `SettingDisplayOrderTest`, the two halves of
the unfinished 2026-08-09 iA Writer Quattro ruling.

## 9. Build

| | Before | After |
|---|---|---|
| `pio run -e simulator_x3` | SUCCESS | SUCCESS |
| `pio run -e gh_release` | SUCCESS, flash 4,987,521 (76.1%), RAM 54,556 (16.6%) | SUCCESS, flash **4,989,237 (76.1%)**, RAM **54,564 (16.7%)** |

Flash **+1,716 bytes**, and it is almost all the two new i18n strings: the note's
headline and its paragraph are 730 B of text between them, and the generator
reports 20,501 B of deduped string data for the whole app. The conversion itself
is a `constexpr` table the compiler folds — `cssunits::classify` costs a handful
of string compares in a function that already did four. RAM **+8 bytes**: the
`unsupportedCssUnit[8]` field on the one `booknotes::Notes` singleton. The
`SpineHrefIndexEntry` offset costs nothing (section 6).

## 10. Left on the table

* **`vw`/`vh`/`vmin`/`vmax`.** Convertible in principle; needs a viewport height
  threaded through `CssLength::toPixels`, `BlockStyle::fromCssStyle` and four
  `ChapterHtmlSlimParser` call sites. `img { height: 100vh }` on a cover page is
  the case that would pay for it.
* **`ex`/`ch`.** Would need the reading face's x-height and zero-advance at the
  point the CSS is applied. The em size this layer receives is a LINE height, so
  the spec's 0.5 em fallback would be wrong by a fifth before it started.
* **`rem` is still `em`.** There is no root font size distinct from the reading
  size here, so a `rem` inside a scaled block resolves against the block rather
  than the root. Pre-existing, unrelated to units, and invisible unless a book
  scales a container.
* **A host test for #62.** See section 6. The review confirmed there is none,
  and that the bug it caught there (section 7, finding 1) would have been caught
  instantly by one.
* **A test that resolves a converted length through `BlockStyle::fromCssStyle`.**
  Nothing covers the point where the new numbers meet `MAX_HORIZONTAL_INSET_EM`
  and the accumulated-inset clamp, which is where they now actually bite.
* **A vertical inset clamp.** Section 4's last row. `p { margin: 1in }` is two
  paragraphs to a page, and honoring it is correct — capping it is a typography
  decision for the owner, not a bug fix.
* **`em` resolves against the font's line height, not its em box** — a 20%
  error, pre-existing, and section 7 finding 3. Correcting it moves every
  em-based margin in every book.
* **`margin: auto` still resolves to zero.** Turning it into an inherited margin
  — or into centering — is a different change with a different blast radius.
