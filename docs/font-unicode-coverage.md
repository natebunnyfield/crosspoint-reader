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

## Fix options (triage pending)

1. **Conversion-side fill** — extend `intervals:` for the sparse families and
   let the existing fallback chain fill what their faces lack. No firmware
   change, fixes rebuilt cards only.
2. **Runtime per-glyph fallback** — missing glyph borrows from builtin.
   Fixes every card forever; costs code in the hot glyph path and mixes faces.
3. **Transliteration** — map codepoints we choose not to carry (U+2212 → '-')
   before layout. Cheap; wrong for anything that isn't punctuation.
