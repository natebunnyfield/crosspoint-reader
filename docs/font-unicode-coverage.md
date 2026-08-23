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
