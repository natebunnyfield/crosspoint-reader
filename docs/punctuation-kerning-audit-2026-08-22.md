# Punctuation kerning audit — periods and commas, all faces (2026-08-22)

Owner order: "an audit of how periods and commas are kerned in all editor and
reading fonts" (P0 context: optical margin alignment). Read-only audit at
firmware 8f97e7184; every number extracted from the shipped font data (SD
.cpfont files parsed per fontconvert_sdcard.py's format; built-ins parsed from
the compiled arrays). Feeds the hanging-punctuation implementation.

Audit complete. Here is the factual map.

---

# 1. Kerning machinery

## Representation — class matrix, not pair list
`/Users/natebunnyfield/src/crosspoint-reader/lib/EpdFont/EpdFontData.h:170-180` (`EpdKernClassEntry`), `:204-211` (the `EpdFontData` kern fields).

- Two sorted `(uint16 codepoint, uint8 classId)` tables — `kernLeftClasses` / `kernRightClasses`. Class IDs are 1-based; **absent codepoint ⇒ class 0 ⇒ no kerning, full stop.**
- `kernMatrix` is a flat `int8_t[leftClassCount * rightClassCount]`, **4.4 signed fixed-point px** (±8.0 px range, quantised to 1/16 px).
- Advances are 12.4 unsigned fixed-point in `EpdGlyph.advanceX` (`EpdFontData.h:139`). Same 4 fractional bits, so advance+kern are summed before one `fp4::toPixel` snap ("differential rounding", `EpdFontData.h:8-17`).
- Ligatures: separate sorted `EpdLigaturePair` table keyed on `(left<<16|right)` (`EpdFontData.h:183-188`).

**Consequence that matters for you:** `.` and `,` are *members of classes*, and in every reading face they are collapsed into the **same** left class and (mostly) the same right class. There is no way to give `.` a different kern from `,` without splitting classes at build time.

## Lookup
`/Users/natebunnyfield/src/crosspoint-reader/lib/EpdFont/EpdFont.cpp:104-118`:
```
104: int8_t EpdFont::getKerning(leftCp, rightCp) const {
105:   if (utf8IsCjkBreakable(leftCp) || utf8IsCjkBreakable(rightCp)) return 0;
108:   if (!data->kernMatrix) return 0;          <-- kills all editor faces
112:   if (lc == 0) return 0;                    <-- kills LibrisADF '.'/',' as LEFT
114:   if (rc == 0) return 0;
```

## Where it is applied
| Path | File:line | Kerns? |
|---|---|---|
| draw (horizontal) | `GfxRenderer.cpp:770-771` | yes |
| draw (rot 90CW / 90CCW) | `GfxRenderer.cpp:2862`, `:2969` | yes |
| measure `getTextAdvanceX` — **generic path** | `GfxRenderer.cpp:2666-2667` | yes |
| measure `getTextAdvanceX` — **SD advance-table fast path** | `GfxRenderer.cpp:2612-2637` | **NO** |
| `getSpaceAdvance` — generic | `GfxRenderer.cpp:2586-2588` | yes (kern(L,' ')+kern(' ',R)) |
| `getSpaceAdvance` — **SD fast path** | `GfxRenderer.cpp:2571-2577` | **NO** (explicit comment) |
| `getKerning` public | `GfxRenderer.cpp:2591-2597` | yes, no SD fast path |
| `EpdFont::getTextBounds` (ink bbox) | `EpdFont.cpp:51-53` | yes |

`prevCp` is reset to 0 whenever `getGlyph` fails, on **both** sides (B-036, `GfxRenderer.cpp:759-762`, `:807-812`) — a missing glyph severs the kern pair in both directions.

## Reader layout (ParsedText/TextBlock) — kerning IS wired in, three ways
- word width: `ParsedText.cpp:228` → `getTextAdvanceX` (intra-word kerning)
- inter-word gap: `ParsedText.cpp:786`, `:959`, `:1157`, `:1251`, `:1311`, `:1358`, `:1399` → `getSpaceAdvance(lastCp(prev), firstCp(next))`
- attached/continuation tokens (no space): `ParsedText.cpp:789`, `:963`, `:1166`, `:1259`, `:1298`, `:1343`, `:1382` → `getKerning(lastCp(prev), firstCp(next))`

## Editors
`src/notes/MarkdownRender.cpp:38` (`getTextAdvanceX`) and `:169` / `NoteEditorActivity.cpp:778,793` (`drawText`). Same machinery, no special path. But see §4 — every editor face has `kernMatrix == nullptr`, so `EpdFont.cpp:108` returns 0 on the first branch, always.

---

# 2. Per-font data for `.` and `,`

All numbers are **pixels**; `adv` from `EpdGlyph.advanceX/16`, `LSB = glyph.left`, `ink = glyph.width`, `RSB = adv − LSB − ink`. Kern-pair counts are **nonzero matrix cells reachable from that codepoint's class**, i.e. the number of *classes*, each covering many codepoints.

## 2a. SD `.cpfont` reading faces (parsed from `/Users/natebunnyfield/src/crosspoint-simulator/build/seedfonts/`)

Format confirmed at `lib/EpdFont/scripts/fontconvert_sdcard.py:1335-1412`: `CPFONT\0\0` + `<8sHHB19s` header (32 B) + N×32 B style TOC `<B3xIIBhhHHBBBI4x`, then per style, in order: intervals(12B) · glyphs(16B `<BBHhhH2xI`) · kernLeft(3B) · kernRight(3B) · kernMatrix(int8) · ligatures(8B) · bitmaps. All shipped files are v4, 4 styles, parsed cleanly.

**Regular style, all four slots:**

| Family / size | space | `.` adv | `.` LSB/ink/RSB | `,` adv | `,` LSB/ink/RSB | `.` kern classes L/R | `,` kern classes L/R |
|---|---|---|---|---|---|---|---|
| Coelacanth 13 | 5.56 | 5.44 | 0/5/**0.44** | 5.44 | 0/5/0.44 | 10 / 15 | 10 / 15 |
| Coelacanth 15 | 6.44 | 6.25 | 0/6/0.25 | 6.25 | 0/6/0.25 | 10 / 15 | 10 / 15 |
| Coelacanth 18 | 7.69 | 7.50 | 1/6/0.50 | 7.50 | 1/6/0.50 | 10 / 15 | 10 / 15 |
| Coelacanth 20 | 8.56 | 8.31 | 1/7/0.31 | 8.31 | 1/7/0.31 | 10 / 15 | 10 / 15 |
| Edgar 12 | 5.75 | 6.38 | 1/4/1.38 | 6.38 | 0/5/1.38 | 36 / 48 | 36 / 48 |
| Edgar 14 | 6.75 | 7.44 | 1/5/1.44 | 7.44 | 0/6/1.44 | 36 / 48 | 36 / 48 |
| Edgar 16 | 7.69 | 8.56 | 2/5/1.56 | 8.56 | 0/7/1.56 | 36 / 48 | 36 / 48 |
| Edgar 18 | 8.69 | 9.62 | 2/6/1.62 | 9.62 | 0/8/1.62 | 36 / 48 | 36 / 48 |
| InknutJunicode 10 | 5.81 | 7.06 | 1/5/1.06 | 7.69 | 1/5/1.69 | **3** / 9 | **1** / 9 |
| InknutJunicode 12 | 7.00 | 8.50 | 2/5/1.50 | 9.25 | 1/6/**2.25** | 3 / 9 | 1 / 9 |
| InknutJunicode 14 | 8.12 | 9.88 | 2/6/1.88 | 10.81 | 1/8/1.81 | 3 / 9 | 1 / 9 |
| InknutJunicode 16 | 9.31 | 11.31 | 3/6/**2.31** | 12.31 | 1/9/**2.31** | 3 / 9 | 1 / 9 |
| LibreFranklin 10 | 4.44 | 4.38 | 1/3/0.38 | 4.44 | 0/4/0.44 | 11 / 13 | 11 / 13 |
| LibreFranklin 12 | 5.38 | 5.25 | 1/3/1.25 | 5.31 | 1/3/1.31 | 11 / 13 | 11 / 13 |
| LibreFranklin 14 | 6.25 | 6.12 | 1/4/1.12 | 6.19 | 1/4/1.19 | 11 / 13 | 11 / 13 |
| LibreFranklin 16 | 7.12 | 6.94 | 1/5/0.94 | 7.06 | 1/5/1.06 | 11 / 13 | 11 / 13 |
| **LibrisADF 12** | 7.50 | 5.50 | 0/5/0.50 | 4.81 | 0/4/0.81 | **0** / 21 | **0** / 21 |
| **LibrisADF 14** | 8.75 | 6.38 | 1/5/0.38 | 5.62 | 1/4/0.62 | **0** / 21 | **0** / 21 |
| **LibrisADF 16** | 10.00 | 7.31 | 1/5/1.31 | 6.38 | 1/5/0.38 | **0** / 21 | **0** / 21 |
| **LibrisADF 18** | 11.25 | 8.19 | 1/6/1.19 | 7.19 | 1/5/1.19 | **0** / 21 | **0** / 21 |
| TeXGyreSchola 12 | 6.94 | 6.94 | 1/4/**1.94** | 6.94 | 1/5/0.94 | 3 / 23 | 3 / 23 |
| TeXGyreSchola 14 | 8.12 | 8.12 | 2/4/**2.12** | 8.12 | 1/6/1.12 | 3 / 23 | 3 / 23 |
| TeXGyreSchola 16 | 9.25 | 9.25 | 2/5/**2.25** | 9.25 | 2/6/1.25 | 3 / 23 | 3 / 23 |
| TeXGyreSchola 18 | 10.44 | 10.44 | 2/6/**2.44** | 10.44 | 2/7/1.44 | 3 / 23 | 3 / 23 |

**Italic cuts carry the worst RSB** (shear pushes ink left inside the same advance box):

| Face (Italic) | `.` adv | LSB/ink/RSB | `,` adv | LSB/ink/RSB |
|---|---|---|---|---|
| TeXGyreSchola 16 It | 9.25 | 0/5/**4.25** | 9.25 | −2/8/3.25 |
| TeXGyreSchola 18 It | 10.44 | 0/6/**4.44** | 10.44 | −2/8/**4.44** |
| InknutJunicode 16 It | 12.19 | 2/6/**4.19** | 11.75 | −2/11/2.75 |
| InknutJunicode 14 BoldIt | 12.06 | 1/7/**4.06** | 11.44 | −3/12/2.44 |
| Edgar 18 It | 9.50 | 0/6/**3.50** | 9.50 | −2/8/3.50 |
| Coelacanth 18 It | 6.88 | 1/6/−0.13 | 6.88 | 0/7/−0.13 |
| LibreFranklin 16 It | 6.94 | 0/5/1.94 | 7.06 | −1/6/2.06 |

**Actual kern pairs involving `.` / `,` — Regular, representative size** (values in px; every listed pair is one matrix cell shared across a whole class):

| Face | `X + .` (letter→period) | `. + X` |
|---|---|---|
| **Coelacanth 18** | T −4.00, V −5.00, W −4.56, Y −3.44, F −2.94, P −4.06, v/w/y −0.88, `’` −3.19, `”` −3.19 | `"` `'` `‘` `’` `“` `”` all −2.19; `)` `]` −1.50; T −2.88, V −5.88, W −5.88, Y −4.38, v/w/y −1.81 |
| **Edgar 16** | T −2.00, V −2.31, W −2.00, Y −1.69, F −2.69, P −2.31, **r −1.00**, v/w/y −1.81, o −0.31, **A +0.50**, c −0.69, f −1.19, **L +0.31**, J −1.00, `’`/`”` −3.50 | `"`/`'`/`’`/`”` −2.19, `‘`/`“` −2.81, `‹›«»—–` −1.00, `)` −1.19, T −2.00, V −2.31, W −2.00, Y −1.69, v/w −1.81, y −1.69, o/c/e/d/q/t −0.31, **A +0.50**, J −0.69 |
| **InknutJunicode 14** | T −6.44, V −4.38, W −4.38, Y −5.56, F −6.44, P −6.44, **r −1.75**, v/w/y −2.06, o/e −0.88, `’`/`”` −4.38 | all six quotes −4.06; v/w/y −2.31; o/c/e/d/q −0.56 |
| **LibreFranklin 16** | T −4.56, V −4.25, W −3.81, Y −4.75, F −4.31, P −4.12, **r −2.75**, v/w −2.31, y −2.19, o −0.88, f −1.44 | T −4.56, V −4.25, W −3.81, Y −4.75, v −2.31, w −2.25, y −1.81, o/c/e/d/q −0.88. **No quote pairs at all.** |
| **LibrisADF 16** | T −2.12, V −2.06, W −1.44, Y −2.44, F −3.62, P −4.31, **r −1.56**, v −1.25, w −0.88, y −1.25, **A +0.62**, f −0.31, **k +0.44** | **NONE — `.` and `,` have leftClass 0** |
| **TeXGyreSchola 16** | T −2.50, V −3.31, W −2.69, Y −2.50, F −3.19, P −3.31, **r −2.50**, v −2.69, w −2.50, y −2.69, **A +0.31**, `’` −1.00 | only `’` −0.69, `”` −0.69 |

Comma differs from period only where noted: InknutJunicode `y, −4.06` vs `y. −2.06`, `o, −1.75` vs `o. −0.88`, `P, −7.00` vs `P. −6.44`; and Inknut's `,`+X is quote-only (no letters). Everywhere else `.` and `,` share the identical class row/column.

## 2b. Built-in reader cuts (`librefranklin_reader_*`) — the on-flash reading face

| Cut | `.` adv | LSB/ink/RSB | `,` adv | LSB/ink/RSB | classes | pairs (`.` L/R) |
|---|---|---|---|---|---|---|
| 12 Regular | 5.25 | 1/3/1.25 | 5.31 | 1/3/1.31 | 48×41 | 11 / 13 |
| 14 Regular | 6.12 | 1/4/1.12 | 6.19 | 1/4/1.19 | 48×41 | 11 / 13 |
| 16 Regular | 6.94 | 1/5/0.94 | 7.06 | 1/5/1.06 | 48×41 | 11 / 13 |
| 18 Regular | 7.81 | 1/5/**1.81** | 7.94 | 1/5/**1.94** | 48×41 | 11 / 13 |
| 12/14/16/18 Bold | 6.25/7.31/8.38/9.44 | 1/5/0.25 … 1/7/1.44 | +0.06 | — | 47×41 | 11 / 13 |
| 16 Italic | 6.94 | **0**/5/1.94 | 7.06 | **−1**/6/2.06 | 48×41 | 11 / 13 |
| 18 Italic | 7.81 | 0/5/**2.81** | 7.94 | −1/7/1.94 | 48×41 | 11 / 13 |

Pairs are identical in shape to the SD LibreFranklin (same source): `T. −4.56, V. −4.25, W. −3.81, Y. −4.75, F. −4.31, P. −4.12, r. −2.75, v./w. −2.31, y. −2.19, o. −0.88, f. −1.44` at 16 pt; bold at 16 pt `P. −4.88, r. −2.88, y. −2.44`. Ligatures: `fi`→U+FB01, `fl`→U+FB02 only.

## 2c. Noto Sans fallback (built-in, 8/10/12 pt)

| Cut | `.` adv | LSB/ink/RSB | `,` adv | LSB/ink/RSB | classes | pairs (`.` L/R) |
|---|---|---|---|---|---|---|
| notosans_8_regular | 4.44 | 1/3/0.44 | 4.44 | 0/4/0.44 | 109×95 | 20 / 40 |
| notosans_10_regular | 5.56 | 1/4/0.56 | 5.56 | 0/4/1.56 | 109×95 | 20 / 40 |
| notosans_12_regular | 6.69 | 1/4/**1.69** | 6.69 | 1/4/**1.69** | 109×95 | 20 / 40 |

Richest table in the tree (534 left entries / 526 right, 10 355-cell matrix). At 12 pt: `P. −3.25, Þ. −3.25, **r. −4.00**, F. −1.50, T. −1.50, Y. −1.50, V./W. −1.25, D/O/Q −1.00, v/w/y −1.00, B −0.25`, plus full Greek and Cyrillic rows (`Γ. −2.00`, `Ч. −2.00`, `Д/Ц/Щ + . = +0.75` — positive). `.`+X side: `T −1.75, V/W −1.50, Y −1.50, C/G/O/Q −1.25, U −0.50, Ј +1.50`. No quote pairs. Ligatures `ff fi fl ffi ffl`.

## 2d. Editor faces (built-in) — **zero kerning**

| Face | `.` adv | LSB/ink/RSB | `,` adv | LSB/ink/RSB | `m` adv | `i` adv | kern entries |
|---|---|---|---|---|---|---|---|
| iawriterquattro_12_regular | **15.00** | 5/5/**5.00** | 15.00 | 5/5/5.00 | 22.50 | **7.50** | **0** |
| iawriterquattro_14_regular | **17.50** | 6/5/**6.50** | 17.50 | 5/7/5.50 | 26.25 | **8.75** | **0** |
| pragmatapro_12_regular | 12.94 | 3/6/**3.94** | 12.94 | 3/6/3.94 | 12.94 | 12.94 | **0** |
| pragmatapro_14_regular | 15.12 | 4/7/**4.12** | 15.12 | 4/7/4.12 | 15.12 | 15.12 | **0** |
| nittitypewriter_12_regular | 15.00 | 6/4/**5.00** | 15.00 | 5/5/5.00 | 15.00 | 15.00 | **0** |
| nittitypewriter_14_regular | 17.50 | 7/4/**6.50** | 17.50 | 6/5/6.50 | 17.50 | 17.50 | **0** |

**Monospace confirmed, not assumed:** PragmataPro and Nitti give `.` `,` `m` `i` the *identical* advance (12.94 / 15.12 / 15.00 / 17.50). **iA Writer Quattro is duospace, not mono** — `i`=7.50, `m`=22.50 at 12 pt — yet it still gives `.` and `,` the **full wide** advance 15.00 (2× the `i`). So Quattro's period is the single most over-advanced punctuation glyph in the whole build: 5.00 px of RSB at 12 pt, 6.50 px at 14 pt.

Also parsed but not currently offered (removed rows, still compiled/on-card): iAWriterDuo, iAWriterMono, IBM Plex Mono, Space Mono — all 0 kern entries, all full-advance periods.

---

# 3. The four failure modes

## (a) Excessive right side bearing opening a gap before the next word — **YES, and unkernable**

The inter-word gap after a period is `RSB(.) + space_advance + LSB(next)`. Kerning cannot close it, because **space is not a kern partner** for punctuation in any shipped font. Verified numerically:

```
Coelacanth_18:      space kL=None kR=1   kern('.',' ')=0 kern(' ','.')=0 kern(',',' ')=0
Edgar_16:           space kL=1    kR=1   kern('.',' ')=0 kern(' ','.')=0 kern(',',' ')=0
InknutJunicode_14:  space kL=None kR=None  all 0
LibreFranklin_16:   space kL=None kR=None  all 0
LibrisADF_16:       space kL=None kR=None  all 0
TeXGyreSchola_16:   space kL=None kR=None  all 0
```
Therefore `getSpaceAdvance`'s flanking-kern term at `GfxRenderer.cpp:2586-2588` is **identically zero for every sentence-ending period and every comma, in every font**. It is dead code for this case. The `. + T` −2.88 / `. + V` −5.88 pairs in Coelacanth are only reachable *inside* a word (`e.g.`, `U.S.A.`), never across a word boundary.

Worst absolute gaps after a period (Regular): TeXGyreSchola 18 → 2.44 + 10.44 = **12.9 px** before the next letter's own LSB. Inknut 16 → 2.31 + 9.31 = 11.6 px. Italic cuts add up to 4.4 px more.

## (b) Kern pairs tightening `T.` `V.` `r.` — **YES, substantial, present in 5 of 6 SD families + both built-in text faces**

Largest tighteners across the set:
- `T.` : Inknut −6.44 · Coelacanth −4.00 · LibreFranklin −4.56 · TeXGyreSchola −2.50 · Edgar −2.00 · LibrisADF −2.12
- `V.` : Coelacanth −5.00 · LibreFranklin −4.25 · Inknut −4.38 · Schola −3.31
- `P.` : Inknut −6.44 (comma −7.00) · Coelacanth −4.06 · LibrisADF −4.31 · Noto −3.25
- `r.` : Noto Sans −4.00 · LibreFranklin −2.75 · Schola −2.50 · Inknut −1.75 · LibrisADF −1.56 · Edgar −1.00
- **Positive (loosening) pairs exist too**: `A.` +0.50 (Edgar), `A.` +0.62 (LibrisADF), `k,` +0.56 (LibrisADF), `L,` +0.31 (Edgar), Noto Cyrillic `Д.` +0.75, `Ј` after `.` +1.50.

A hanging-punctuation implementation that shifts the period must *not* double-count these: the glyph's drawn x already includes the kern via `GfxRenderer.cpp:770-771`.

## (c) Justified right edge counts the period's full advance — **YES**

The chain, all in `/Users/natebunnyfield/src/crosspoint-reader/lib/Epub/Epub/ParsedText.cpp`:

1. `:228` `measureWordWidth` → `getTextAdvanceX(word)`. Trailing `.` is part of the word token (tokenizer at `:413-454` never splits it off) and `getTextAdvanceX` adds the **final glyph's full advance** unconditionally at `GfxRenderer.cpp:2676`: `widthPx += fp4::toPixel(prevAdvanceFP);`. So the period's RSB is inside `wordWidths[]`.
2. **Line-fit test:** `:824-826`
   ```
   824: currlen += wordWidths[j] + gap + (...extraStartOffset);
   826: if (currlen > effectivePageWidth) break;
   ```
   (greedy variant at `:967-970`). A trailing period's RSB can therefore push a word to the next line for 1–4.4 px of white space.
3. **Justification slack:** `:1149` `lineWordWidthSum += wordWidths[...]`, `:1183`
   ```
   1183: const int spareSpace = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
   1185: justifyExtra = computeJustifyExtra(spareSpace, actualGapCount);   // :200-208
   ```
   **`:1183` is the single hook point** for optical-margin alignment — subtract the last word's trailing-punctuation overhang here.
4. **Right/Center alignment origin:** `:1369` `xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;` and `:1330-1332` for the RTL branch. Three more sites needing the same correction.
5. Word x-positions are then baked into `lineXPos` (`:1374`, `:1404`) and `TextBlock::render` draws each word at its stored x — so a hang can be implemented purely as an x adjustment on the last word of a line without touching the renderer.

No existing hanging-punctuation / optical-margin code anywhere: `grep -rni "hanging|optical.margin|protrude"` returns only unrelated hits (markdown hanging *indent*, optical *size*).

## (d) Ligature or substitution touching punctuation — **NONE**

Complete ligature inventory, all faces:
- Coelacanth, LibreFranklin, LibrisADF, TeXGyreSchola: `ff fi fl ffi ffl` (5)
- InknutJunicode: `ff fi fl ſt ffi ffl` (6)
- Edgar: `fb ff fh fi fj fk fl ffb ffh ffi ffj ffk ffl` (13; Italic 14)
- built-in librefranklin/librefranklin_reader: `fi fl` (2)
- built-in notosans: `ff fi fl ffi ffl` (5)
- every editor face: **0**

Programmatic filter for pairs where either side is U+002E or U+002C returned the empty set on every file. `applyLigatures` (`EpdFont.cpp:150-166`) is a no-op for punctuation.

---

# 4. Ranked findings

### P0 — Reader layout measures SD reading fonts **without kerning at all**
`GfxRenderer.cpp:2612-2637` (`getTextAdvanceX`) and `:2571-2577` (`getSpaceAdvance`) short-circuit to `SdCardFont::getAdvance()` whenever `hasAdvanceTable()` is true, with the comment *"No kerning/ligature lookup — consistent with previous metadataOnly behavior."* Meanwhile `drawText` (`:770`) **does** kern via the per-page mini matrix. All six on-card reading families go through this path. Net effect: a word ending `-r.` in LibreFranklin 16 is measured 2.75 px wider than it draws; `Ty.`-type words up to 4.75 px. Measured > drawn ⇒ line ends short of the margin and justified gaps absorb the error. Root cause is deliberate: `SdCardFont.cpp:1122-1126` skips `buildMiniKernMatrix` when `metadataOnly` (layout), so the data genuinely isn't resident at measure time.

Corollary hazard: `GfxRenderer::getKerning` (`:2591-2597`) has **no** SD fast path and reads `miniKernMatrix` directly. During layout that pointer is either null (→0, `EpdFont.cpp:108`) or **left over from a previously rendered page**. `ParsedText.cpp:789, 963, 1166, 1259, 1298, 1343, 1382` all call it during layout. Non-deterministic values for attached-punctuation gaps on SD fonts.

### P1 — LibrisADF has **no** `.`/`,` left-kerning whatsoever
`kL['.'] == 0` and `kL[','] == 0` in all 4 styles × 4 sizes. Proof line: `EpdFont.cpp:112 — if (lc == 0) return 0;`. Its `X + .` column is fine (21 nonzero left classes, incl. `P. −4.31`), so it tightens *into* a period but never *out of* one. Also note `sd-fonts.yaml:966-978`: this is the one family that ships pairs twice (legacy `kern` + GPOS) and had a 2× double-count bug; **any LibrisADF `.cpfont` built before the overlay fix carries doubled values and is indistinguishable by filename**. The shipped files here read back at sane magnitudes (max −4.31), so these are post-fix.

### P2 — Three editor faces have zero kern data, and their periods are the widest in the build
`kernLeftEntryCount = kernRightEntryCount = kernLeftClassCount = kernRightClassCount = 0`, `kernMatrix = nullptr` in every `pragmatapro_*`, `nittitypewriter_*`, `iawriterquattro_*`, plus `iawriterduo_*`, `iawritermono_*`, `ibmplexmono_*`, `spacemono_*`. Guard: `EpdFont.cpp:108`. **This is the source font's doing, not a converter strip** — `fontconvert.py` has no `--no-kern` flag and always runs extraction; `iAWriterQuattroS-Regular.ttf` has GPOS features `['mark']` only, no `'kern'` feature (vs LibreFranklin's `['kern','mark','mkmk']`). Real periods: Quattro 12 pt `.` = 15.00 px advance for 5 px of ink (LSB 5, RSB 5) while `i` is only 7.50 px total. Hanging punctuation would pay off most here and costs nothing in kern-interaction risk.

### P3 — Italic cuts carry 2× the roman RSB on `.`
TeXGyreSchola 18 It RSB 4.44, Inknut 16 It 4.19, Edgar 18 It 3.50, librefranklin_reader 18 It 2.81. Same advance as roman, ink sheared left. Any hang amount computed from roman metrics will under-hang italics; compute per style.

### P4 — `.` and `,` are class-collapsed and cannot be tuned independently
Identical `(leftClass, rightClass)` in LibreFranklin (3,4), Noto (4,5), Coelacanth (3, 8/6), TeXGyreSchola (3/1, 3/1). Only Edgar (12/9 vs 11/9) and Inknut (4/6 vs 3/4) separate them. Any build-time correction applied via the kern matrix hits both.

### P5 — Built-in converter still has the legacy+GPOS double-count bug (latent)
`fontconvert.py:790` and `:808` both accumulate into the same `raw_kern` dict with `+=`, whereas the SD converter was fixed to a separate `gpos_kern` dict overlaid at `fontconvert_sdcard.py:424`. Currently harmless: the only kerned built-ins are Libre Franklin and Noto Sans, and `LibreFranklin[wght].ttf` / `LibreFranklin-Italic[wght].ttf` report `kern=False, GPOS=True`. It will bite the day someone compiles an old-style OTF into `builtinFonts/`.

### Faces ranked by usable punctuation kern data
1. **Noto Sans** (fallback) — 20 / 40 classes, Greek + Cyrillic, `r. −4.00`
2. **Edgar** — 36 / 48, letters *and* dashes/guillemets/quotes on the `.`+X side
3. **LibreFranklin** (SD + built-in reader cuts) — 11 / 13, letters only, **no quote pairs**
4. **Coelacanth** — 10 / 15, best quote coverage (`."` `.'` `.'` `.'` `."` `."` all −2.19, `.)` `.]` −1.50)
5. **TeXGyreSchola** — 3 / 23: rich *into* the period, only `.’` `.”` out of it
6. **InknutJunicode** — 3 / 9 (Regular). Its **Italic** is a different animal entirely (674/685 entries, 121×148 matrix, 123 nonzero cells)
7. **LibrisADF** — 0 / 21, no left-side kerning at all
8. **iA Writer Quattro / PragmataPro / Nitti Typewriter** — nothing

---

# Negative results, with the guard that proves each

| Claim | Proof |
|---|---|
| No kerning between punctuation and space, any font | `kL[0x20]`/`kR[0x20]` absent or class present but matrix cell = 0; measured `kern('.',' ') = kern(' ','.') = kern(',',' ') = 0` for all 6 SD families. `getSpaceAdvance`'s kern term (`GfxRenderer.cpp:2586-2588`) is therefore always 0 for these. |
| No ligature/substitution touches `.` or `,` | Filter over every `EpdLigaturePair` table (SD + built-in) for `(pair>>16)==0x2E/0x2C or (pair&0xFFFF)==0x2E/0x2C` → empty everywhere. |
| Editor faces never kern | `data->kernMatrix == nullptr` → `EpdFont.cpp:108 if (!data->kernMatrix) return 0;` |
| LibrisADF never kerns out of `.`/`,` | `lookupKernClass` returns 0 → `EpdFont.cpp:112 if (lc == 0) return 0;` |
| SD reading fonts never kern during layout measurement | `GfxRenderer.cpp:2615` `if (sdIt != end && sdIt->second->hasAdvanceTable())` — the branch returns before any `getKerning` call. Comment at `:2612-2613`. |
| SD mini kern matrix is absent during layout | `SdCardFont.cpp:1122-1126` `if (!metadataOnly) { … buildMiniKernMatrix … }`; `:1140-1142` only calls `applyKernLigaturePointers` when `kernLigOk`. |
| No hanging-punctuation / optical-margin code exists | `grep -rni "hanging\|optical.margin\|opticalMargin\|protrude" src lib` → only markdown hanging *indent* and optical *size* hits. |
| Monospace faces do give periods a full advance | PragmataPro 12: `.` `,` `m` `i` all advance 12.94; Nitti 12: all 15.00. **Except iA Writer Quattro**, which is duospace (`i` 7.50, `m` 22.50) yet still gives `.` the wide 15.00. |
| No kern across a missing glyph | B-036: `GfxRenderer.cpp:759-762`, `:807-812`, `:2666`, `:2673`; `EpdFont.cpp:38-46`. |

**Files an implementer will want open:** `lib/EpdFont/EpdFontData.h:139-216`, `lib/EpdFont/EpdFont.cpp:104-118`, `lib/GfxRenderer/GfxRenderer.cpp:770, 2571-2597, 2612-2677`, `lib/Epub/Epub/ParsedText.cpp:200-208, 824-826, 1144-1186, 1330-1332, 1369`, `lib/EpdFont/SdCardFont.cpp:1122-1145, 1247-1272`, `lib/EpdFont/scripts/fontconvert_sdcard.py:1246-1412`.
---

# Standing rulings (owner, 2026-08-22, asked one at a time)

1. **Alignment default: RAGGED RIGHT.** The new Justified/Ragged reader
   setting ships with ragged right as the default (justified one tap away).
2. **Editor faces: fix iA Writer Quattro's period and comma only.** '.' and
   ',' get the narrow (i-width) advance via font regeneration; PragmataPro and
   Nitti Typewriter stay at full advance — correct for true monospace. No
   synthesized kern tables for editor faces (option declined).
3. **Inter-block gap: capped at half a line.** "keep half-line gap, but
   collapse any gap that is more than a half-line gap" — any computed gap
   (CSS margins, padding, extra spacing) above lineHeight/2 collapses to
   exactly lineHeight/2; smaller gaps stay as computed. Page-top rules and
   the chapter sinkage (placement approved from the render) unchanged.
4. **Chapter-select progress bar**: ~2x taller, outlined, read-portion
   filled solid left of current position, no gray background, a tick line
   previewing the highlighted chapter's location; total height within one
   text line.
5. **Verse/blockquote/list + typographic details: audit and fix worst.**
   One agent renders real samples (poetry, blockquotes, nested lists, plus
   dashes/quotes/ellipsis/non-breaking-space line-break behavior),
   documents findings in an md, fixes the indefensible.
6. **Chapter-opener styling: NEITHER** drop cap nor small caps — the
   sinkage stands alone. Closed.

## Follow-up: the LEFT edge, landed 2026-08-22 (surface roadmap T3)

The trailing hang shipped first and was half the feature — §(c) above names
`:1183` as "the single hook point", which is true for the right edge only. The
leading hang is now in, at `ParsedText.cpp` (the `HANG_FRACTIONS` table, one
row per glyph with a trailing column and a leading column, and `leadingHang`
in `extractLine`).

What was verified rather than assumed:

- **The trailing hang really is measure-independent.** It enters exactly one
  expression, `spareSpace`, which feeds nothing but `computeJustifyExtra`;
  `computeLineBreaks` and `computeHyphenatedLineBreaks` never see it. The
  leading hang follows the same route, plus a negative paint x on the line's
  first word.
- **Line breaks are unchanged, measured.** Six headless X3 renders of a
  dialogue fixture (left-aligned and justified, screen margin 10 and 0) put
  every line band at an identical y and identical height before and after.
- **Only lines that begin with hanging punctuation move.** At 18 px /
  LibrisADF: `“` −5 px, `‘` −3 px, `(` −2 px, `—` −10 px, every other line
  0 px.
- **Justified lines stay flush right.** The shift is added to `spareSpace`, so
  the gaps give back what the left edge took; right-edge x within ±3 px of
  the unchanged value, which is `computeJustifyExtra`'s integer division.
- **The hang is capped by the page's own left margin.** At Screen Margin 0 an
  X3 leaves 4 px to the left of the measure; an uncapped half-em dash wanted
  14 and would have been clipped by the panel edge, not wrapped into the
  margin. `leftHangBudget` in `extractLine` is that cap, recovered from
  `getScreenWidth() - pageWidth` because ParsedText is handed a width and
  never a margin.
- **Stale caches would have kept flush-left quotes**, so `SECTION_FILE_VERSION`
  went to 43 for the reason v39 gives: a hang is break-neutral but its painted
  x lives in the cached TextBlocks.

Not implemented, and deliberately: the capital `T`/`W`/`A`/`V` optical
correction the roadmap mentions alongside this. That is side-bearing
compensation on letterforms, not punctuation — a different and far more
subjective feature, and it is not what "hanging punctuation" asked for.
