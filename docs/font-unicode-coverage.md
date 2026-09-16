# SD-font unicode coverage — audit and gaps

Date: 2026-08-20, at commit 4ed1abe13 (post arrow fix), fs_/fonts and the iOS
seedfonts byte-in-sync. Three-agent audit (coverage scan, pipeline trace,
runtime trace); every number below re-verified by hand against the .cpfont
interval tables — the scan agent's prose summary contradicted its own matrix,
so trust this table, not that summary.

## The symptom that triggered this

Claude chat, dark mode: "�2 for the eggs" — the model wrote −2 with U+2212
MINUS SIGN and the active family had no glyph, so the font's own U+FFFD glyph
rendered. Not a decode error: the UTF-8 was valid.

## Ground truth (probed directly, base-scale regular of each family)

| codepoint | Edgar | LibreFranklin | TeXGyre | Coelacanth | Inknut | LibrisADF |
|---|---|---|---|---|---|---|
| U+2212 MINUS | Y | Y | Y | — | — | — |
| U+2190/92 arrows | Y | Y | Y | Y | Y | Y |
| U+00B0 DEGREE | Y | Y | Y | Y | Y | Y |
| U+2260 ≠ | Y | Y | Y | — | — | — |
| U+2264 ≤ | Y | Y | Y | — | — | — |
| U+221E ∞ | Y | Y | Y | — | — | — |
| U+03B1 α | Y | Y | Y | Y | — | — |
| U+0394 Δ | Y | Y | Y | Y | — | — |

Totals (of the 2,942-codepoint union): Edgar 2,628 · LibreFranklin 2,443 ·
TeXGyreSchola 2,443 · Coelacanth 1,785 · InknutJunicode 1,094 · LibrisADF 1,094.

Smart quotes, en/em dash, ellipsis, bullet, NBSP, ×, ÷: **all six families have
them** — the everyday-punctuation layer is fine. The gap is the math/technical
layer, and it splits the families in two: the three at ≥2,443 carry it, the
three below don't.

## Why the gaps exist (pipeline)

- Each family opts into coverage via its own `intervals:` in
  `lib/EpdFont/scripts/sd-fonts.yaml` — comma-separated preset names
  (`reading`, `latin-ext`, `greek`, `cyrillic`, `symbols`; defined in
  `fontconvert_sdcard.py:41-82`) plus explicit hex ranges. **No shared
  baseline**: a family that never asked for a block simply lacks it.
- A codepoint the primary face lacks is filled from a **fallback chain**
  (TeXGyreSchola → NotoSans → NotoSansMath, `build-sd-fonts.py:176-199`) —
  built for the arrow fix (7c764cd9f), which added only `(0x2190-0x21FF)` and
  only to the families then missing it. The mechanism generalizes; the arrow
  fix just applied it narrowly.
- If no face in the chain has the glyph the codepoint is **silently pruned**
  (`fontconvert_sdcard.py:905-908`). No coverage audit exists anywhere in the
  pipeline (`verify_compression.py` checks bitmap encoding only).
- U+FFFD is force-appended to every request (`fontconvert_sdcard.py:119`),
  which is why a gap renders as the diamond-? box in the family's own style.

## Runtime behavior (no fallback on device)

- Glyph resolution: builtin intervals → SD miss handler → U+FFFD → '?' →
  nullptr (`lib/EpdFont/EpdFont.cpp:158-207`). **No per-glyph fallback** to
  another family/size/style; the only font-level fallback is the CJK path
  (`GfxRenderer.cpp:188-223`).
- **No transliteration/normalization** anywhere (only NFC composition for
  combining marks). Claude chat text flows through the same pipeline as EPUB
  with no sanitizing (`ClaudeChatActivity.cpp:174,561`).
- **Bug, filed as B-036**: measurement and rendering disagree after a missing
  glyph. `getTextBounds` resets `prevCp = 0` (`EpdFont.cpp:38`) so the next
  pair takes no kerning, while `drawText` and `getTextAdvanceX` keep
  `prevCp = cp` (`GfxRenderer.cpp:802,2662`) and do kern — a line measured as
  fitting can overflow when drawn.

## Size context

Per-family on-card weight today (all sizes + 2x + 3x tiers): Edgar 57M,
TeXGyre 54M, Coelacanth 51M, LibreFranklin 41M, Inknut 26M, LibrisADF 24M.
One base-scale .cpfont runs 0.5-0.9 MB; coverage scales roughly with glyph
count, so lifting the three sparse families toward the 2,443 level costs very
roughly 2x their current bytes at worst.

## Negative results (checked, do not re-propose blind)

- Arrows are NOT still missing anywhere — the 08-17 fix reached all six
  families and both render-scale tiers.
- fs_/fonts vs ios/seedfonts drift: none, byte-identical today.
- `synth_prototype.py` is a standalone experiment; nothing in the build uses
  it. Style synthesis (embolden/shear) exists but never fills coverage.

## Resolution (2026-08-20, same day)

Owner triage: conversion-side fill, `reading` baseline for all six, full scope.

* `sd-fonts.yaml`: InknutJunicode and LibrisADF -> `reading`; Coelacanth ->
  `reading,greek,cyrillic` (plain reading would have silently dropped the
  polytonic Greek and Cyrillic-ext blocks it already shipped).
* Rebuilt and installed, all six families, all three tiers. Verified by
  parsing the installed interval tables: U+2212, ≠, ≤, ∞, α, Δ, arrows present
  in EVERY family at EVERY tier. Floor rose 1,094 -> 2,443 codepoints
  (Coelacanth 2,827). iOS seedfonts mirrored and verified.
* Two new uint8-overflow offenders surfaced by the wider set, both handled as
  per-family tier-local drops in `install-sim-fonts.py` (`FAMILY_TIER_DROPS`)
  rather than global ones, which would have stripped glyphs Edgar/TeXGyre/
  LibreFranklin ship today: InknutJunicode U+2E3B at 2x + U+2E3A at 3x,
  Coelacanth U+261C/261E (pointing hands, 265x126 px) at 3x.
* Silent pruning is silent no more: `fontconvert_sdcard.py` now prints
  `PRUNED N requested codepoint(s) no face in the chain can supply` with
  compacted ranges, per style. This rebuild pruned zero.
* B-036 fixed the same pass (see BUGS.md): kern severed in both directions
  across a hole, `test/missing_glyph_kern/` holds it.

Physical cards still carry the pre-fix cuts until rewritten.

## Fix options (as triaged)

1. **Conversion-side fill** — extend `intervals:` for the sparse families and
   let the existing fallback chain fill what their faces lack. No firmware
   change, fixes rebuilt cards only.
2. **Runtime per-glyph fallback** — missing glyph borrows from builtin.
   Fixes every card forever; costs code in the hot glyph path and mixes faces.
3. **Transliteration** — map codepoints we choose not to carry (U+2212 → '-')
   before layout. Cheap; wrong for anything that isn't punctuation.

## The claude-tools epub diff (2026-08-20, later the same day)

Owner ask: which glyphs the epubs generated from `~/src/claude-tools` use that
the installed families lack. Method: unzip all 18 canonical epubs (worktree
duplicates excluded), strip tags/entities from every xhtml/ncx/opf, take the
distinct codepoint set (135), diff against every family's installed interval
table.

**Result: one glyph — U+2717 ✗ BALLOT X** (`tico-spanish-sealed.epub`, used
beside the ✓ that was already covered). Missing from all six families: it sits
in the Dingbats block `reading` requests, but NO face in the fallback chain
carried any of that block — NotoSansMath is math-only, NotoSans proper has
none, and plain NotoSansSymbols (846 glyphs) has none either, probed directly.
**NotoSansSymbols2** (2,660 glyphs) is the one that carries 2700-27BF; it is
now the chain's last link (`DINGBAT_FALLBACK_FONT`). Verified after rebuild:
U+2717 present in all six families at all three tiers, and the epub diff
reports **zero missing codepoints**. Per-style pruning fell 733 → ~450-500
(the residue is genuinely unfillable: C1 controls, U+2160-2182 Roman numerals,
rare Greek).

Found on the way: **the pruning audit printed into a void.** `build-sd-fonts.py`
runs the converter with `capture_output=True` and, on success, discarded its
stderr — so the PRUNED lines added earlier today were invisible in exactly the
runs that mattered. Success paths now surface them.

Negative results: NotoSansSymbols (the first) is useless for this — despite the
name it carries none of the probed dingbats/symbols. Box-drawing and misc
symbols supplied by Symbols2 raised no 255px overflows at any tier.

## Addendum: the seventh family (TeX Gyre Heros, 2026-08-23)

Everything above says "six families" and is left as written — it is the record
of what was audited on 2026-08-17/20, and rewriting the count would falsify the
runs. TeX Gyre Heros joined `installed_families` on 2026-08-23 (owner ruling,
docs/sd-card-fonts.md) and was built against the same `reading` baseline and
the same four-face fallback chain, so the conclusions above extend to it. That
was measured, not assumed — reading the installed interval tables the same way
the 2026-08-20 verification did:

| Family | 1x | 2x | 3x |
|---|---|---|---|
| TeXGyreHeros | 2,676 | 2,676 | 2,675 |
| TeXGyreSchola | 2,676 | 2,676 | 2,675 |
| LibrisADF | 2,676 | 2,676 | 2,675 |
| Edgar | 2,861 | 2,861 | 2,860 |

Identical to the two families it sits beside, and the 3x figure is one lower
for all of them for the documented reason: U+2E3B THREE-EM DASH rasterises over
EpdGlyph's 255 px limit at that tier and is dropped there only. The eight
probes the 08-20 pass used — U+2212, ≠, ≤, ∞, α, Δ, → and U+2717 ✗ — are all
present in TeXGyreHeros at all three tiers.

The face's OWN cmap is narrow (Latin-1 223/224, Latin Extended-A 124/128,
Greek 54/144, Cyrillic 0/256, 1,087 codepoints), so most of that 2,676 comes
from the fallback chain. That is not a defect specific to this family: TeX Gyre
Schola's own cmap is the same 1,087 codepoints, because GUST could not
relicense the base-35 Cyrillic (`qhv-hist.txt`) and shipped no Cyrillic in any
TeX Gyre face.

## Corpus diff: Eighth Atlas, 2026-09-15

The claude-tools epubs are the reader's real-world codepoint corpus, so each
new one gets diffed against the installed interval tables. `atlas/epub/
eighth-atlas-2026-09-15.epub` (62,406 words, 136 entries) uses **38 distinct
non-ASCII codepoints**. Measured by parsing the `.cpfont` interval tables of
all nine installed families at 16 px, the same way the 2026-08-20 pass did —
not inferred from `sd-fonts.yaml`, which records what was *requested* and not
what survived pruning.

**Present in all nine families**, including the ones that would be plausible
gaps: U+215C ⅜, U+00BC/BD/BE ¼ ½ ¾, U+2248 ≈, U+2032/2033 ′ ″, U+03C6 φ,
U+00D7 ×, U+014D ō, U+016B ū, U+017A ź, U+0131 ı, U+00E6 æ, U+2192 →,
U+00B7 ·, U+2014/2013 — –, U+2026 …, and the accented Latin set. The
`reading` preset's 0x0020–0x024F, 0x2000–0x206F, 0x2150–0x218F, 0x2190–0x21FF
and 0x2200–0x22FF blocks cover every one of them.

**Absent from all nine**, and therefore rendering as the family's own U+FFFD
box:

| codepoint | char | entry |
|---|---|---|
| U+3131 · U+3141 · U+3145 | ㄱ ㅁ ㅅ | `topics/hangul.md` |
| U+3057 · U+56DE · U+6839 | し 回 根 | `topics/nemawashi.md` |
| U+5B88 · U+7834 · U+96E2 | 守 破 離 | `topics/shuhari.md` |

Ten codepoints, twelve occurrences, three entries — every one of them a
parenthetical gloss beside its romanization, so the sentence still reads. The
cause is the same structural one this document already describes: `reading`
stops at 0x303F, so Hiragana (0x3040–0x309F), Hangul Compatibility Jamo
(0x3130–0x318F) and the CJK ideographs (0x4E00–0x9FFF) are outside every
installed family, and `resolveTextFontId`'s CJK redirect
(`lib/GfxRenderer/GfxRenderer.cpp:196-219`) needs a registered fallback font
that this card does not carry.

Scope note: verified against the interval tables, **not observed on glass**.
The book itself was confirmed to parse, paginate and render in the simulator
on 2026-09-15; the three affected pages were not paged to.

## The cut carries 395 of the 647 it asks for, 2026-09-15

Two findings, one mechanism, both from one question: a `render_harness aa
HerosRef` proof page at slot 1 showing eleven replacement boxes on the math
line, and the ask "use a better fallback (probably some math or tex gyre
typeface) to address the missing characters there."

**A better fallback cannot address them, and that is worth stating first.** The
fallback chain only fills a codepoint the family's `intervals:` ASKED for --
`build-sd-fonts.py:apply_cmap_drops` says it in its own docstring, "a codepoint
outside every interval is dropped from the build entirely rather than fallen
back," and `fontconvert_sdcard.py` reaches for the chain only when
`face.get_char_index(cp) == 0` for a requested codepoint. So a face added to
the chain changes nothing for a codepoint nobody requested.

### 1. The proof recipe, not the fallback (`tools/textcut/proof.yaml`)

`HerosRef` and `HerosTextCut` are built there at `intervals: latin-ext`, which
is `0x0020-0x024F, 0x02B0-0x02FF, 0x1E00-0x1EFF, 0x2000-0x206F, 0xFB00-0xFB06`
-- no Greek, no Arrows, no Mathematical Operators, no Dingbats. The specimen's
math line asks for all four blocks, so eleven of its thirteen probes were
dropped at build time: `-` U+2212, `!= <= >=` U+2260/2264/2265, `inf` U+221E,
`alpha beta Delta pi` U+03B1/03B2/0394/03C0, `<- ->` U+2190/2192, plus the two
dingbats U+2713/2717. `x` and `/` survived because they are Latin-1.

The shipped recipes for both families in `sd-fonts.yaml` use `intervals:
reading`. The proof was narrower than the thing it proves. Rebuilt at
`reading`: 2,676 glyphs per style, which is exactly the shipped
`TeXGyreHeros` figure from the 2026-08-23 addendum above, build time 6 s for
both families, 1.1M -> 3.4M per family at the proof's four slots. Every probe
renders.

### 2. The shipped cut is a sans face with a serif for 252 of its codepoints

Measured against `lib/EpdFont/local_fonts/HerosTextCut-regular.ttf` and
`downloaded_fonts/HerosRef/texgyreheros-regular.otf`, per style, identical for
all four styles:

| | codepoints |
|---|---|
| TeX Gyre Heros own cmap | 1,053 |
| ...of which inside `reading` | 647 |
| HerosTextCut carries | 395 |
| **Heros has, the cut does not** | **252** |

The cutter's charset gate is `tools/textcut/textcut_curves.py:65`,
`wanted(cp) = 0x20 <= cp < 0x0250 or 0x2000 <= cp <= 0x206F or cp == 0x20AC`.
Everything Heros has outside that gate is filled on the card from
`[TeXGyreSchola, NotoSans, NotoSansMath, NotoSansSymbols2]` -- Schola first, by
the 2026-08-17 ruling -- and Schola is Century Schoolbook, a serif. Rendered
from the card the device reads (`fs_/fonts/`, slot 3, `render_harness aa`),
HerosTextCut sets `alpha beta Delta pi` and `!= <= >= inf <- ->` in serif while
TeXGyreHeros sets them in its own sans, on the same page, under names that
differ by two words.

The 252, by block:

| block | n | what it is |
|---|---|---|
| U+1E00-1EFF | 133 | Latin Extended Additional -- Vietnamese and the dotted/barred accents |
| U+0300-03FF | 75 | combining diacritics and the 54 Greek Heros carries |
| U+02B0-02FF | 10 | spacing modifiers, the circumflex/caron/breve set |
| U+2200-22FF | 14 | `d Sum - -+ / * sqrt inf angle ~~ != <= >= star` |
| U+2000-20CF | 6 | currency: colon, lira, naira, won, dong, peso |
| U+FB00-FB06 | 5 | **the f-ligatures** -- the built cpfont reports `ligs=0` against Heros's `ligs=5` |
| U+2190-21FF | 4 | the four cardinal arrows |
| U+2500-26FF | 5 | lozenge, white bullet, eighth note, the two marriage signs |

The f-ligature row is the one that reaches ordinary English prose: an epub
using U+FB01/FB02 gets a Century Schoolbook `fi` inside a neo-grotesque
paragraph, and `SdCardFont`'s own liga table is empty for the family.

### Three options, with what each costs

1. **Proof only** -- `proof.yaml` to `reading`. One line, 6 s, makes the
   specimen match what ships. Does NOT change the card; the cut arm then
   renders its serif fallback beside the ref arm's own glyphs, which is honest
   and is how the two families actually differ today.
2. **Widen the cut** -- `wanted()` to `reading`'s ranges, so the cutter carries
   every glyph the source face has inside them, 647 instead of 395. The weight,
   trap and digit-alignment operations then run over Greek, math and arrows
   they were never tuned on (the trap rule is crotch-angle driven and would
   fire on `alpha`, `Delta`, the arrowheads), and `fit_pairs.py`'s corpus is
   ASCII so none of them would be pair-fitted -- the same accepted tradeoff the
   accented Latin already carries. Costs a rebuild and a reinstall of an
   INSTALLED family: card, three Mac app cards, the iOS seed tree, all tiers.
3. **Order the chain by classification** -- put a sans (NotoSans /
   NotoSansMath) ahead of Schola for sans families. This reverses the
   2026-08-17 ruling ("make texgyre with primary"), which was itself made about
   Libre Franklin, a sans; it would move glyphs in every family, not just this
   one. Not taken without a ruling.

A correction to this document, from the same review: the reason U+2E3B does not
move at the 3x tier is NOT that the head displaced Schola for it. Measured at
16/32/48 ppem, **U+2E3B is absent from the Heros head, from Schola, from Noto
Sans Math and from Noto Sans Symbols 2** — it comes from **Noto Sans**, at
132x4 px at 48 ppem. So the global `tier_drops: {3: [11835]}` is untouched
because nothing moved, not because the head answers for it. The conclusion
holds and the rationale did not; all three tiers built, and
`fontconvert_sdcard.py` aborts rather than truncating on a glyph over 255 px,
so those builds are proof rather than assumption. Largest rendered dimension
over every `reading` codepoint the head can supply, at the largest 3x ppem:
head 66 px, Schola 65 px — nothing near the ceiling either way.

Checked and found CLEAN on the way: the arrows are present in every installed
family (the 08-17 fix holds); `HerosRef`'s own face supplies eleven of the
thirteen probes once they are requested, so stock Heros needs no new fallback
face at all; the pruned residue at `reading` is 500 codepoints per style and is
the same unfillable set the 2026-08-20 pass recorded (C1 controls, Roman
numerals, rare Greek, box drawing), not a regression.

## Resolution, 2026-09-16: a per-style head on the chain, and Noto stays

### The cut now falls back to the face it was cut from

Owner ruling: *"if heros has symbols, text cut should use those before using
Schola."* Taken as written and generalized one step, because the same shape
will recur for any family cut or derived from another typeface.

`fallback_head:` is a new optional family key in `sd-fonts.yaml` — a
`{style: source-spec}` map of faces spliced in FRONT of the chain. Each entry
takes the same keys as a style source (`path`/`url`/`zip`, `variable:`,
`scale:`). HerosTextCut names the same four CTAN TeX Gyre Heros OTFs its parent
recipe uses, at the same `scale: 1.014`, so a fallback glyph lands at the size
the cut renders at rather than 1.4 % under it.

**PER STYLE, and that is a fix in its own right.** `--fallback-<style>` was
already per style in `fontconvert_sdcard.py`; `build-sd-fonts.py` was passing
the same string to all four. So every style of every family has been falling
back to TeX Gyre Schola **regular** — a bold page's fallback glyphs have always
come back at regular weight, in every family, since the chain was built. The
head is per style, so the family that uses it is the first to get a bold
fallback glyph that is bold. The generic chain behind it is unchanged, and a
family with no `fallback_head:` builds byte-for-byte as before.

Measured after the rebuild, `render_harness aa` slot 1, the Greek-and-arrows run
(`>= inf . alpha beta Delta pi . <-`) of HerosTextCut against HerosRef on the
same page: **mean |delta| 0.07 code values, 0.07 % of pixels differing by more
than 4**. It was a serif; it is now the same drawing the reference uses. The
252 are Heros's own again, and Schola/Noto/Math/Symbols2 still answer for what
Heros itself lacks — U+2713 and U+2717 among them.

`tools/textcut/proof.yaml` carries the same corrections — `reading`, the head
AND `synth_ligatures:` — so the specimen and the card agree. The ligature line
was missed on the first pass and caught by adversarial review: without it the
proof built `ligs=0` against `HerosRef`'s `ligs=5`, which is round 11's failure
again (an arm built from a different recipe measures the recipe). Its `path:`
entries were also still the literal `BUILD_DIR/...` placeholders; the file runs
as committed now.

One thing this does NOT fix: `ligs=0`. The cut has the f-ligature OUTLINES back
(U+FB01/FB02 now come from Heros), but the `liga` GSUB rule that reaches them
from `f`+`i` lives in the PRIMARY face's table, and the cutter writes no `liga`.
An epub that spells U+FB01 directly is now right; automatic ligature
substitution in HerosTextCut still does nothing. `synth_ligatures:` is the
existing lever for exactly this and is untested on a cut face.

### Can Noto Sans be safely removed? No — measured

The 2026-08-17 ruling was *"can the missing characters in libre franklin be
sourced from texgyre instead? drop noto"*, and the "drop noto" half has stood
unanswered ever since. Answering it now, against the chain as it is today
(Schola, Noto Sans, Noto Sans Math, Noto Sans Symbols 2), by resolving each
installed family's `intervals:` against its own cmap plus the chain with and
without Noto Sans:

| family | today | without Noto Sans | lost |
|---|---|---|---|
| Edgar | 2,861 | 1,837 | **1,024** |
| TeXGyreSchola | 2,676 | 1,681 | 995 |
| TeXGyreHeros | 2,676 | 1,681 | 995 |
| HerosTextCut | 2,676 | 1,681 | 995 |
| VandenKeere | 2,676 | 1,681 | 995 |
| Doves | 2,676 | 1,683 | 993 |
| AtkinsonHyperlegibleNext | 2,676 | 1,685 | 991 |
| LibrisADF | 2,676 | 1,689 | 987 |
| Almendra | 2,676 | 1,692 | 984 |
| InknutJunicode | 2,693 | 1,731 | 962 |
| LibreFranklin | 2,676 | 2,017 | 659 |
| Coelacanth | 3,004 | 2,488 | 516 |

All twelve installed families use `reading`, so all twelve are on the broad
chain; none is on the Latin-only Libre Franklin path. The union of what would
be lost is **1,163 codepoints**, and the single decisive block is **Cyrillic**:

> Cyrillic U+0400–U+052F — TeX Gyre Schola **0**, Noto Sans **304**,
> Noto Sans Math **0**, Noto Sans Symbols 2 **0**.

Noto Sans is the only face in the chain with any Cyrillic at all, and only two
installed families carry their own (Coelacanth 145, Libre Franklin 220). The
other ten would render a Russian epub entirely as replacement boxes. The rest of
the 1,163 is Latin Extended-B and the IPA/modifier blocks (319), Latin Extended
Additional (123), combining diacritics (93), Supplemental Punctuation (94),
General Punctuation's rare half (75), superscripts/subscripts/currency (54) and
Letterlike/Number Forms (46). Only 41–68 codepoints per family would shift to
Math/Symbols2 instead of vanishing.

**And the real corpus regresses too.** The 36 canonical `~/src/claude-tools`
epubs use 179 distinct codepoints; dropping Noto Sans loses, per family,
between one and five of them — the superscripts `⁴ ⁿ`, the subscripts `₂ ₄`,
and `⅜`. That last is one of the glyphs the 2026-09-15 Eighth Atlas diff had
just verified present in every installed family.

So: not safely, not as the chain stands. "Drop noto" is achievable only by
replacing it with a face of comparable breadth — Cyrillic, Latin Extended-B,
Latin Extended Additional and the combining marks in one file — which in
practice means DejaVu Sans or another Noto. That is a swap, not a removal, and
has not been priced.
