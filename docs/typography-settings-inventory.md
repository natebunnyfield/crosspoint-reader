# Every possible typography setting — inventory, 2026-08-24

Owner asked for the full list while deciding what belongs on the Typography
Settings screen. Compiled from the source, not from memory; the useful part is
which CATEGORY each falls into, because that is what decides the cost.

Read with `docs/ligature-control.md` (what shipped) and `TODO.md`.

## 1. On the screen already, or landing with it

| Setting | Field | Values |
|---|---|---|
| Reader Font (family) | `fontFamily` | its own screen (`FontSelectionActivity`) |
| Reader Font Size | `fontSizeSlot` | the slots the family ships |
| Line Spacing | `lineSpacing` | `TIGHT / NORMAL / WIDE` (`LINE_COMPRESSION`) |
| Line Grid | `lineGridEnabled` | on / off |
| Justified Text | `justifyThresholdChars` | 5 thresholds, almost-always to wide-only |
| Ligatures | `ligaturesEnabled` + `ligaturesOff` | master, plus one row per pair the loaded font carries |

## 2. Frozen constants — un-freezing is deleting `static constexpr` and adding a row

These already work. They were hardcoded by the 2026-08-21 reduction, which
assumed each was web-only taste; the code behind them is intact.

| Setting | Constant | Values |
|---|---|---|
| **Text Anti-aliasing** | `textAntiAliasing = TEXT_AA_STANDARD` | `OFF / STANDARD / CRISP / DARK` — CRISP hardens dark-gray edges to black and keeps light; DARK darkens every edge one level |
| Extra Paragraph Spacing | `extraParagraphSpacing = 1` | on / off |
| Hyphenation | `hyphenationEnabled = 1` | on / off |
| Paragraph Alignment | `paragraphAlignment = JUSTIFIED` | **conflicts with the 2026-08-23 ruling** that made this automatic via the threshold; re-adding it would undo that control |
| System Font | `systemFont` | UI chrome, not reading text |
| Long-press behavior | `longPressButtonBehavior` | input mapping rather than typography |

**Text Anti-aliasing is the strongest candidate on this whole page**: four modes
already implemented, no engine work, and the only item that changes how every
glyph looks.

## 3. Live field, no row

| Setting | Field | Status |
|---|---|---|
| Screen Margin | `screenMargin`, 0–45 step 5 | **RULED OUT 2026-08-22** (owner, layout exactness). The field persists and is card-controlled. Do not re-propose. |

## 4. The engine supports it, but nothing exposes it

| Setting | Where it lives |
|---|---|
| First-line indent | `BlockStyle.textIndent` / `ParsedText::resolveFirstLineIndent` — driven by the book's CSS today. A setting would be an OVERRIDE, so this is plumbing rather than new rendering. |

## 5. Does not exist — real feature work

Letter spacing (tracking) · word spacing · orphan and widow control · drop caps ·
small caps and any OpenType feature beyond `liga`/`rlig` · hanging punctuation ·
hyphenation zone and minimum word length.

**Letter spacing was asked for on 2026-08-24 and declined the same day** — owner:
*"Skip it."* It is not a move: there is no `letterSpacing` field and no tracking
support anywhere in `src` or `lib`. Implementing it means folding a signed value
into the same 12.4 accumulator the advances and kerns share, which changes every
line break and therefore needs a cache bump.

## Why the categories matter

Category 2 is nearly free and category 5 is not, and both look identical in a
feature request. The 2026-08-21 reduction is what put six working controls into
category 2; anything taken out of it is a reversal of that ruling and goes to the
owner first, which is why the Typography screen shipped with Line Spacing only
after he asked for it by name.
