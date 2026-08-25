# Automatic justification — the measure decides

**Owner ruling, 2026-08-23, verbatim:** "remove ragged right or justified ios
app settings, instead make it automatic by letting the character length decide
what is optimal."

The Text Alignment row shipped on 2026-08-22 (`ae6981ddf`, Justified / Ragged
right, defaulting to Ragged) and is withdrawn here. In its place: every block
that asks to be justified keeps it only when **that block's own measure** holds
at least 40 characters per line. Below that it is set ragged — flush left for
LTR, flush right for RTL.

**Follow-up ruling, 2026-08-24, verbatim:** "make justified or ragged right
character count an ios app setting."

This does **not** reverse the above. The decision stays automatic and stays per
block — there is still no Justified/Ragged row and there will not be one. What
became settable is the **threshold**: the character count the measure is
compared against. It ships as **Justified Text** on the firmware's own Settings
screen, offering 32 / 36 / 40 / 45 / 50 with Bringhurst's 40 as the default.

Why it landed there rather than in the iOS Settings.app bundle is
[its own section below](#why-this-is-a-firmware-setting-and-not-an-ios-settingsapp-row)
— short version: it moves line breaks, so it has to be a `ReaderRenderSpec`
field for the section cache to notice it, and that struct is built from
`CrossPointSettings`.

---

## Where it lives

| Piece | File | Note |
|---|---|---|
| The threshold, the constant, the arithmetic | `lib/Epub/Epub/AutoJustify.h` | Pure and host-tested — every failure mode is a wrong-looking page that compiles |
| The decision | `lib/Epub/Epub/ParsedText.cpp`, `layoutAndExtractLines` | The only place the block's measure, its font and its requested alignment are all in scope at once |
| The alphabet measurement | `lib/Epub/Epub/ParsedText.cpp`, `measureLowercaseAlphabet` | Goes through `GfxRenderer::getTextAdvanceX`, the same call the line breaker measures words with |
| The retired setting | `src/CrossPointSettings.h` (`static constexpr paragraphAlignment = JUSTIFIED`), `src/SettingsList.h` (row deleted) | This repo's retirement pattern |
| The threshold's stored value | `CrossPointSettings::justifyThresholdChars` | The character COUNT, not a picker index. Persisted by hand in `toJson`/`fromJson` — its row has a getter/setter and no `valuePtr`, which the generic loop skips |
| The threshold's row | `src/SettingsList.h`, `SettingInfo::DynamicEnum`, `STR_CAT_SYSTEM` | Reader is a withdrawn category; System is what `rebuildSettingsLists()` keeps |
| The threshold's route into layout | `ReaderRenderSpec::justifyThresholdChars` → `ChapterHtmlSlimParser` ctor → `layoutAndExtractLines` | Mirrors `lineGridEnabled` exactly |
| The test | `test/auto_justify/AutoJustifyTest.cpp` | 22 tests (12 for the arithmetic, 10 for the threshold and its route to the cache) |

---

## The threshold: 40 characters, and the citation

**Robert Bringhurst, *The Elements of Typographic Style*, §2.1.2 "Choose a
comfortable measure", p. 27.** Verified identical in the 2nd edition (Hartley &
Marks, 1996) and the 3rd edition / version 3.0 (2004):

> "A reasonable working minimum for justified text in English is the
> 40-character line. Shorter lines may compose perfectly well with sufficient
> luck and patience, but in the long run, justified lines averaging less than 38
> or 40 characters will lead to white acne or pig bristles: a rash of erratic
> and splotchy word spaces or an epidemic of hyphenation. **When the line is
> short, the text should be set ragged right.**"

That is the whole rule, in his units, with his remedy. 40 is not a number picked
for being round.

### Why the mechanism is real, not decorative

Justification hands a line's leftover space to its word gaps, so the damage is
per-GAP, and the gap count follows the measure. Bringhurst's own elastic budget
(§2.1.1, p. 26) is a word space of M/4 desired, M/5 minimum, **M/2 maximum**. A
40-character English line holds about seven words and therefore six gaps, so the
total legal stretch before every gap is at its maximum is 6 × M/4 ≈ 1.5 em —
barely more than half an average word. Below 40 the line has nowhere to put the
slack, and it either gapes or hyphenates. Those are exactly the two failure
modes he names.

### Corroboration, and where sources disagree

They do not disagree on direction. They differ on whether they will name a
number, and in what unit.

| Source | Unit | What it says |
|---|---|---|
| **Bringhurst §2.1.2, p. 27** | characters | justified minimum **40**; breaks down "less than 38 or 40"; remedy is ragged right |
| Bringhurst §2.1.1, p. 26 | ems per gap | word space M/5 min, M/4 desired, M/2 max |
| *Chicago Manual of Style*, 17th ed. ¶16.136 | picas | "For very short lines, such as those in an index, justifying the text usually results in either gaping word spaces or excessive hyphenation, making for difficult reading. Chicago therefore sets all indexes without justification ('ragged right')." Its "very short" is the ¶16.135 index setting: 8 pt in a 13-pica column, which is about 40–45 characters. |
| Gregory, M. & Poulton, E. C. (1970), *Ergonomics* 13(4) 427–434, [doi:10.1080/00140137008931157](https://doi.org/10.1080/00140137008931157) | words per line | Measured it. Justification significantly WORSE than ragged at **seven words per line** (≈38–39 characters), **no** disadvantage at twelve (≈66). Their own conclusion: "The findings are explained in terms of the irregularities in spacing introduced by justification when the line length is short." |
| James Craig, *Designing with Type*, 5th ed. | characters | comfortable band 35–70; names ragged setting as the fix for bad word spacing, but sets no justification floor |
| Butterick, *Practical Typography* | characters | 45–90 per line; declines to set a justification floor at all ("Justification is a matter of personal preference") |
| **W3C** — CSS Text 3, CSS Text 4, *Requirements for Latin Text Layout* | — | **Nothing.** Verified negative: the specs never tie `text-align: justify` to a measure, and dpub-latinreq §4.2 "Justification" is an empty stub (three headings, no body). |

Bringhurst is the one who states a character threshold; nobody contradicts it;
the one empirical study that varies line length lands on the same place from a
different direction. So 40 ships.

The words↔characters conversion applied to Gregory & Poulton is **ours**, using
Spiekermann's published ratio (*Stop Stealing Sheep*, ~pp. 156–157: "5 words
(25+ characters)… 8 words (45 characters)… 11 words (60 characters)… 15 words
(90 characters)"), i.e. ≈5.5 characters per word including its space. Their
paper reports words per line, not characters.

**Not verified, do not cite from here:** Hochuli's *Detail in Typography* (no
accessible text), Felici's *Complete Manual of Typography*, McLean, and the
"1.5–2.0 lowercase alphabets = a good measure" rule often attributed to
American Type Founders — that last one is numerically consistent with
Bringhurst's table (it maps to 42–70 characters) but we could not find a primary
source for the attribution.

---

## The measurement method: alphabet length

### What it is not

**Not `measureWidth / fontSize`.** Point size is a design size, not a width.
Across the eight reading families on this card the same 14 pt spans a 1.4×
range of set width — TeXGyre Heros's lowercase alphabet is 323 px where Inknut
Junicode's is 412 — and that range straddles the threshold at a fixed measure.
A point-size proxy would put those two faces on the same side of the line while
the device renders 44.6 and 34.4 characters respectively.

**Not the advance of `n`, and not CSS's `ch`.** Both are ~0.5–0.63 em and
overstate the average advance of running text by 25–30%. `n` is a *reference*
glyph in type design (it sets x-height and the round-and-straight fitting), not
an average, and `ch` is defined by CSS Values 4 as the advance of `0` — so
`width: 40ch` actually holds about **50** characters, which is a real trap for
anyone implementing this rule in CSS.

### What it is

The **alphabet length**: the measured width of `abcdefghijklmnopqrstuvwxyz` set
in the actual face at the actual size. This is the instrument Bringhurst's own
copyfitting table (pp. 28–29) is indexed by, §2.1.2 p. 27:

> "Measure the length of the basic lowercase alphabet —
> abcdefghijklmnopqrstuvwxyz — in any face and size you are considering, and the
> table will tell you the average number of characters to expect on a given
> line."

His table is linear in the measure within each row, so it reduces to

```
characters per line  =  28.1 × (measure ÷ alphabet length)
```

Across the alphabet lengths a 10 pt text roman actually occupies (his figure:
120–140 pt; measuring eight real text faces gives Times 119, Baskerville 117,
Charter 127, Georgia 130, Iowan 130, Palatino 133 — he is right) the table's own
constant runs 28.0–28.3. His worked example checks it: a 25-pica measure with a
128 pt alphabet is 2.344 alphabets × 28.1 = 65.9, and he calls it "roughly 65
characters per line."

**The constant is not 26.** A line of running prose is not an alphabet: it leans
on the narrow high-frequency letters, and every ~5 letters it spends a space,
which is narrower still. The frequency model is published — the OpenType OS/2
`xAvgCharWidth` weights (space 166/1000, `e` 100/1000, `z` 2/1000, summing to
1000) — and evaluating it on real text faces gives an average character of
≈0.44 em against a per-letter a–z mean of ≈0.49 em. That ratio *is* 26 → 28.
(Caution: the `xAvgCharWidth` **field** in a modern font is no longer that
number — OS/2 version 3 redefined it as the mean advance of all non-zero glyphs
and Microsoft's spec now says outright not to use it for layout. Only the
deprecated *formula* is the frequency model.)

### Which measurement path this calls, and why it is safe

`measureLowercaseAlphabet` calls `GfxRenderer::getTextAdvanceX` — the same call
`calculateWordWidths` and both line breakers use. That matters because of the
2026-08-22 P0 in `docs/punctuation-kerning-audit-2026-08-22.md`: the SD
advance-table fast path used to measure **without** kerning while `drawText`
kerned, so a word measured wider than it drew. That is fixed
(`GfxRenderer.cpp:2612-2637`, kerning now comes from
`SdCardFont::getMeasureKern`, deterministic during layout), so the alphabet is
kerned exactly as a word would be and the estimate cannot disagree with the
layout it is deciding for.

For SD faces the alphabet's glyphs are ensured resident first —
`ensureSdCardFontReady(fontId, ALPHABET, /*styleMask=*/0x01)` — because the
persistent advance table is built from each paragraph's own words, and a
paragraph with no `q` or `z` would otherwise measure them as missing.

**Not cached.** It is 26 advance lookups against a paragraph's hundreds of
words. A memo keyed on `fontId` would go stale the moment a font unload hands
the same id to a different face — which `test/font_switch_churn` exists because
of.

---

## Calibration: does the estimate match what this device renders?

Measured 2026-08-23. X3, portrait, screen margin 5 → a 512 px measure. Rendered
characters per line come from the firmware's **own** read-aloud page capture
(`CROSSPOINT_SIM_READALOUD_LOG=2`), grouping word rects by baseline, body lines
only, on a controlled prose EPUB.

| Face | pt | alphabet a–z (px) | measure ÷ alphabet | estimated ch/line | **rendered ch/line** | error | verdict | flips at |
|---|---|---|---|---|---|---|---|---|
| LibrisADF | 12 | 270 | 1.90 | 53 | 50.0 | +3.0 | justified | 380 px |
| LibrisADF | 14 | 318 | 1.61 | 45 | 43.6 | +1.4 | justified | 448 px |
| LibrisADF | 16 | 364 | 1.41 | 40 | 37.0 | +3.0 | justified | 512 px |
| LibrisADF | 18 | 412 | 1.24 | 35 | 35.6 | −0.6 | ragged | 580 px |
| LibreFranklin | 14 | 346 | 1.48 | 42 | 43.1 | −1.1 | justified | 487 px |
| LibreFranklin | 18 | 456 | 1.12 | 32 | 31.1 | +0.9 | ragged | 641 px |
| TeXGyreSchola | 14 | 397 | 1.29 | 36 | 38.4 | −2.4 | ragged | 559 px |
| TeXGyreSchola | 18 | 510 | 1.00 | 28 | 25.7 | +2.3 | ragged | 717 px |
| Coelacanth | 14 | 365 | 1.40 | 39 | 41.9 | −2.9 | ragged | 514 px |
| Coelacanth | 18 | 483 | 1.06 | 30 | 28.6 | +1.4 | ragged | 679 px |
| InknutJunicode | 14 | 412 | 1.24 | 35 | 34.4 | +0.6 | ragged | 580 px |
| Edgar | 14 | 388 | 1.32 | 37 | 39.5 | −2.5 | ragged | 546 px |
| TeXGyreHeros | 14 | 323 | 1.59 | 45 | 44.6 | +0.4 | justified | 455 px |

**Mean signed error +0.27 characters, mean |error| 1.73, max 3.0.** Solving each
row for the constant gives 25.6–30.0, mean **27.9** — Bringhurst's 28.1 sits
inside our own measurement, so the book's number is what the code carries rather
than a locally fitted one.

`AutoJustifyTest.MatchesWhatTheFirmwareActuallyRenders` pins this table at ±3
characters, and is the test that fails if anyone swaps the alphabet-length
method for a point-size proxy.

**The one boundary case, stated plainly:** LibrisADF 16 pt at a 512 px measure
estimates exactly 40 and renders 37 — inside Bringhurst's own 38–40 grey band.
It justifies. That is the largest overshoot in the sweep and it is on the record
here rather than discovered later.

**Direction of the residual.** The rendered counts come from justified lines,
whose stretched gaps hold slightly fewer characters than the natural setting the
estimate models, so the estimate leans conservative — it rags marginally sooner
than a perfect count would. That is the right way to be wrong: Bringhurst's
failure zone is "less than 38 or 40", so a late justification costs more than an
early rag. `AutoJustifyTest.ErrsTowardRaggedNearTheBoundary` holds the bias
under +1 character.

---

## Per block, not per book

The decision is taken inside `ParsedText::layoutAndExtractLines`, whose
`viewportWidth` argument is **this block's** measure — the page viewport already
reduced by the block's own margins and padding at the call site
(`ChapterHtmlSlimParser.cpp:1994` and `:2721`). So a blockquote, a
list item or a table cell decides for itself, and can come out ragged on a page
whose body text is justified.

Measured on `blocks.epub` (a body paragraph and a `margin: 0 2em` blockquote,
LibrisADF 14 pt, X3):

```
auto-justify: measure 512 px, alphabet 318 px, ~45 chars/line -> justified
auto-justify: measure 388 px, alphabet 318 px, ~34 chars/line -> ragged
```

and in the rendered pixels of that one page:

| Block | measure | estimate | right-edge ink | spread | σ |
|---|---|---|---|---|---|
| body paragraph | 512 px | 45 ch/line → justified | x = 510…519 | 9 px (1.8% of measure) | 2.70 px |
| blockquote | 388 px | 34 ch/line → ragged | x = 411…453 | 42 px (10.8% of measure) | 16.18 px |

Real books do this too. Wingspan at LibrisADF 16 pt produces three measures —
512 px (40 ch/line, justified), 467 px (36, ragged) and 450 px (35, ragged) —
and *AI Engineering from Zero* adds a fourth at 481 px (37, ragged).

The first line's text-indent is deliberately **not** subtracted. The body
measure is what the paragraph is set to; its opening line is not a different
column.

---

## Hysteresis: none, and why

**Decision: no dead band.** The scenario worth worrying about is a page whose
alignment flips back and forth. It cannot happen here, for two independent
reasons.

1. **Every input is deterministic and constant for the life of a block.** The
   measure is `viewportWidth − blockStyle.totalHorizontalInset()`, fixed once
   the block's style is resolved; the alphabet length is a property of
   `(face, size)`. Verified rather than assumed: the boundary configuration
   (LibrisADF 16 pt, estimate exactly 40) was run nine times across three books,
   cold and warm section caches, with the log emitting a line only when the
   measured value *changes* — the alphabet came back **364 px in every line of
   every run**, and every distinct measure kept its verdict. There is no jitter
   to damp.

2. **The demotion is idempotent.** It only ever reads `Justify` and only ever
   writes `Left`/`Right`. The paginator calls `layoutAndExtractLines` repeatedly
   on one `ParsedText` as it fills successive pages; the second call sees `Left`,
   matches nothing, and the block cannot change its mind between page 3 and
   page 4 even in principle.

What *can* cross the threshold is a deliberate settings change — font size, font
family, screen margin, and since 2026-08-24 the threshold itself — and each of
those already repaginates the whole book (they are all in `ReaderRenderSpec`). A user stepping LibrisADF from 14 pt
(45 ch/line, justified) to 16 pt (40, justified) to 18 pt (35, ragged) sees the
alignment change once, at one step, along with everything else about the page.
That is a reflow, not a flip-flop.

A dead band would also make things worse, not better: it has to remember a
previous state, so two devices with byte-identical settings would render the
same book differently depending on the order the owner happened to touch the
dials. Non-reproducible rendering is a worse defect than a one-time reflow the
user asked for.

---

## How this interacts with hyphenation (T1 is untouched)

Established before changing anything, because it is the only coupling that
exists.

`CrossPointSettings::hyphenationEnabled` is `static constexpr 1`
(`src/CrossPointSettings.h:366`), so `layoutAndExtractLines` **always** takes
`computeHyphenatedLineBreaks` — the greedy breaker — and never the total-fit DP
in `computeLineBreaks`. That DP is dead code in every shipped configuration.
Fixing that is roadmap **T1** and is out of scope here; nothing below changes it.

Alignment reaches line breaking at exactly **one** point, `ParsedText.cpp`'s
`raggedSkipsHyphen` (grep the name — this citation has already moved once):

```cpp
const bool raggedSkipsHyphen = blockStyle.alignment != CssTextAlign::Justify && !isFirstWord &&
                               lineWidth * 100 >= effectivePageWidth * linebreak::raggedHyphenGatePct();
```

The 70 now lives in `lib/Epub/Epub/LineBreakMode.h` as
`linebreak::RAGGED_HYPHEN_GATE_PCT`. It read `lineWidth * 10 >=
effectivePageWidth * 7` until 2026-08-27; the ×100 form is that comparison
scaled by ten on both sides, exact for every width, and the value is unchanged.
It was swept 40–100 and kept — `docs/line-breaking-2026-08-25.md` §9.

A non-justified block skips the hyphenation attempt once the line has already
reached 70% of the measure and lets the word wrap whole; a justified block
always attempts the split. That is the whole interaction. `hyphenateWordAtIndex`
never consults alignment, and `extractLine`'s alignment and justify-slack math
never consults hyphenation.

Two consequences worth naming:

- **Alignment changes line BREAKS, not just painted x.** That is why the section
  cache version has to move (below).
- **The pairing is the right way round.** Bringhurst's two failure modes for a
  narrow justified measure are splotchy word spaces *or* "an epidemic of
  hyphenation"; going ragged at a narrow measure relieves both at once, because
  the ragged branch also stops hyphenating lines that are already nearly full.

## Hanging punctuation is unaffected

Left- and right-edge optical margin alignment (`bdfe5f663`, cache v43) lives in
`extractLine` and keys off `effectiveAlignment`, which is derived from
`blockStyle.alignment` *after* this demotion. Both branches already existed and
both still run. Verified in pixels on the justified regime: the line ending
"…through the type." puts ink at x = 520, against a mean of 514.5 for the
letter-terminated lines on the same page — the period still hangs 5–6 px past
the measure.

## The threshold is a setting (2026-08-24)

### The ladder, and why the rungs are not round numbers

| stored | row label | what it does |
|---|---|---|
| 32 | Almost always (32) | even narrow columns keep a flush edge |
| 36 | More often (36) | |
| **40** | **Balanced (40)** | Bringhurst's stated minimum — **default** |
| 45 | Less often (45) | |
| 50 | Only wide pages (50) | only a genuinely comfortable measure justifies |

Both **endpoints are fixed by measurement, not taste**: each is the last rung
that still leaves the *other* regime reachable on this device. One step further
in either direction and the row stops being a measure and becomes an on/off
switch that lies about what it is. The instrument is the 13 face/size pairs in
the calibration sweep above, at the X3's 512 px portrait measure:

* **Top rung 50.** The widest setting on the card is LibrisADF 12 pt, estimated
  53 characters per line. A threshold of 55 would justify **nothing** — a dead
  row. 50 is the highest rung that still leaves a justified regime.
* **Bottom rung 32.** The narrowest is TeXGyre Schola 18 pt, estimated 28. A
  threshold of 28 would justify **everything** — equally dead, from the other
  end. 32 is the lowest rung that still leaves a ragged regime: at 32, Libre
  Franklin 18 pt (est 32) justifies while Coelacanth 18 pt (30) and Schola 18 pt
  (28) do not.

Between them, 36 sits at the lower approach to Bringhurst's own grey band
("less than 38 or 40"), which is also where Gregory & Poulton measured
justification becoming significantly worse than ragged — seven words per line,
≈38–39 characters. 45 is Butterick's comfortable floor (45–90). 40 is
Bringhurst's number and the default.

`AutoJustifyThreshold.BothEndpointsLeaveTheOtherRegimeReachable` and
`.EveryRungActuallyMovesTheVerdictForSomeRealSetting` pin both claims against
that sweep, so a rung that stops doing anything fails the build rather than
shipping as decoration.

### Why this is a firmware setting and not an iOS Settings.app row

Traced end to end before anything was built, because the request said "ios app
setting" and the honest answer was a different surface within the same app.

**Every row in the iOS `Settings.bundle` is host-side.** Exactly one reaches
firmware behavior at all — `readAloudEnabled` → `CrossPointPrefs_readAloudEnabled()`
→ `HalGPIO::setReadAloudCaptureWanted()` → the firmware's `readAloudCaptureWanted()`,
which is `return false;` inline on device. That channel toggles a **capture**
side-channel: it changes what the firmware publishes, not how it lays out a
page, and nothing it touches is cached. No Settings.app row has ever reached a
firmware layout decision, and three things made this the wrong one to be first.

1. **The cache would not have noticed.** Moving the threshold moves line
   BREAKS, and section files are validated against `ReaderRenderSpec`, which is
   built from `CrossPointSettings`. A value living in `NSUserDefaults` never
   enters that comparison, so every already-paginated book on the card would
   have kept its old breaks with a header that compared equal — the setting
   would have looked inert on exactly the books the owner reads.
2. **It would have given one store two writers.** Injecting the host value into
   `CrossPointSettings` at boot is the shape of the panel-palette P1 fixed the
   previous day (`crosspoint-simulator/src/PanelSource.h`: "one READER over a
   store with two WRITERS is not one source").
3. **The device would have been pinned at 40 forever.** `readAloudCaptureWanted()`
   returns false on device because an X3 physically cannot speak. A typography
   threshold is not a host capability — the hardware can and should have it.

The precedent for where it went is one day older than the request: **Line Grid**
(2026-08-22) is a typography toggle that repaginates, and it shipped as
`STR_CAT_SYSTEM` in `getSettingsList()`. That category *is* the device UI —
`SettingsActivity::rebuildSettingsLists()` keeps System rows and drops Reader,
Display and Controls. Landing here means the phone gets the control (it renders
the firmware's own Settings screen), the X3 and X4 get it too, the web settings
API serves it, and it persists in `/.crosspoint/settings.json`.

### Measured proof that the row reaches the page

X3, portrait, FiraSansBook 14 pt, screen margin 5 → a 512 px measure holding an
estimated 38 characters per line. That sits **between** two rungs, so the same
page justifies at 36 and rags at 40 with nothing else changed. Captured
headless, `CROSSPOINT_SIM_GRAIN_SEED` pinned (its phase jitter is re-rolled per
launch and is larger than some real effects), restoring the same
`fs_/.crosspoint/` before each fresh arm.

```
auto-justify: measure 512 px, alphabet 383 px, ~38 chars/line, threshold 36 -> justified
auto-justify: measure 512 px, alphabet 383 px, ~38 chars/line, threshold 40 -> ragged
```

Right-edge ink x of the six body lines of that one page, measured from the
rendered framebuffer:

| threshold | verdict | right-edge x | spread | σ |
|---|---|---|---|---|
| 36 | justified | 516, 513, 514, 514, 517, 518 | **5 px** | 1.80 px |
| 40 | ragged | 492, 441, 454, 449, 499, 488 | **58 px** | 23.04 px |

Left edges are identical in both arms (8–11 px), and the paragraph's final short
line is identical in both — a last line is never justified. The 470 px
blockquote on the same page rags at *both* thresholds (34 ch/line), so the
per-block decision still works exactly as it did.

### Cache invalidation, proven rather than assumed

The whole reason this went to the firmware screen. Three runs against the same
card:

| run | card state | threshold | result |
|---|---|---|---|
| 1 | pristine | 40 | builds `sections/0.bin` at 40 |
| 2 | **cache from run 1 intact** | 40 (unchanged) | **0** `Parameters do not match` — the cache is reused |
| 3 | **cache from run 1 intact** | 40 → 36 | `[ERR] [SCT] Deserialization failed: Parameters do not match` → rebuilt → page renders justified |

Run 2 is the control that matters: without it, "the cache was discarded" is
indistinguishable from "this build always discards the cache." And the page
produced by run 3 — rebuilt from a stale threshold-40 cache — is **bit-identical**
to a fresh threshold-36 render, which is the strongest form the claim can take.

## Section cache: v44 → v49 (this change: v48 → v49)

`SECTION_FILE_VERSION` moves to **49** and the header grows one byte,
`spec.justifyThresholdChars`, written after `lineGridEnabled`. The byte is the
part that matters; the version bump only covers the v48 files already on cards,
which carry no such byte. Every other field a v48 file compares would have
matched across a threshold change, so without the new byte a card full of
paginated books would have kept its old breaks for their whole life.

## Section cache: v43 → v44

`lib/Epub/Epub/Section.cpp`. Required, and the usual guard cannot cover it:
`ReaderRenderSpec::paragraphAlignment` is now a compile-time constant, so it is
byte-identical in files written before and after this change and cannot
invalidate anything. Pagination moves twice over — the base intent goes back to
JUSTIFIED (v43 caches were built ragged, whose `raggedSkipsHyphen` rule differs)
and a narrow block now demotes itself — so a stale v43 cache would keep the old
ragged breaks for the life of the book.

## What happened to the stored setting

`CrossPointSettings::paragraphAlignment` became `static constexpr uint8_t = JUSTIFIED`,
which is this repo's retirement pattern (the same one the 2026-08-21 reduction
used for `hyphenationEnabled`, `lineSpacing`'s row, and the rest). Consequences,
all of them deliberate:

- `fromJson` iterates `getSettingsList()`, and there is no longer a row for the
  `"paragraphAlignment"` key, so **a stored value is ignored rather than read.**
  A save from either era holding 0…4 is simply not consulted.
- `toJson` stops writing it.
- **No integer is re-pointed.** `PARAGRAPH_ALIGNMENT` still maps 1:1 onto
  `CssTextAlign`, so if a row were ever reinstated it would land on exactly the
  values it had.
- `&CrossPointSettings::paragraphAlignment` no longer forms, so the row cannot
  be reinstated by accident — `src/SettingsList.h` would fail to compile.
- The three strings (`STR_TEXT_ALIGNMENT`, `STR_ALIGN_JUSTIFIED`,
  `STR_ALIGN_RAGGED_RIGHT`) stay in the table. An unused string costs a few
  bytes of flash; deleting them renumbers `StrId` for every translation.
- `TxtReaderActivity` still reads the field into `cachedParagraphAlignment` and
  is unaffected in output: its `JUSTIFIED` case is already "treated as
  left-aligned (true justification would require word spacing adjustments)",
  which is what `LEFT_ALIGN` did there before.

## The book note stops naming a constant

`STR_BOOK_NOTE_RAGGED_B` spelled the threshold out in prose — *"Below forty…
back above forty characters"* — which became untrue at four of the five rungs
the moment the row shipped. It now carries the live number instead, three `%u`
filled by `BookNotesActivity` from `autojustify::clampThreshold(SETTINGS.justifyThresholdChars)`.

Read **live**, not stored beside `narrowestCharsPerLine` in `notes.bin`: moving
the threshold repaginates the book, so the note is re-raised against the value
it is about to print, and a stored copy would only ever be a second thing to
keep in sync. Only `english.yaml` carries the string; other locales fall back to
it, so the reword is one string rather than a translation sweep.

## Driving it headlessly

`LOG_DBG("PTX", "auto-justify: …")` prints measure, alphabet, estimated
characters per line, the live threshold and the verdict, once per distinct
`(measure, face, threshold)` rather than once per block. The threshold is part
of that key deliberately: a run that changed it would otherwise print one line
for the old value and stay silent about the new one, which is exactly the
reading a headless A/B of this feature depends on. It is compiled out at `LOG_LEVEL 1` (every release env), so
it exists only in dev and simulator builds.

```bash
CROSSPOINT_SIM_INPUT_SCRIPT='12000:QUIT' SDL_VIDEODRIVER=dummy \
  .pio/build/simulator_x3/program 2>&1 | grep auto-justify
```

To count what actually rendered rather than what was estimated, use the
firmware's own page capture and group the word rects by baseline:

```bash
CROSSPOINT_SIM_READALOUD_LOG=2 CROSSPOINT_SIM_INPUT_SCRIPT='12000:QUIT' \
  SDL_VIDEODRIVER=dummy .pio/build/simulator_x3/program 2>&1 | grep READALOUD-RECT
```
