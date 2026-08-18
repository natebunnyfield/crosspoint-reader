# The font fallback chain, and why arrows were missing

Date: 2026-08-17. Reported by the owner as *"only edgar seems to have unicode
arrows"*, corrected by him to *"it is texgyre not edgar"* — and the built fonts
agreed with the correction, not the first report.

## What was wrong

`NotoSans-Regular.ttf` — the fallback face every non-Latin family leaned on —
contains **none of the 128 codepoints in the Arrows block** (U+2190–21FF). Noto
keeps arrows in `NotoSansSymbols` and `NotoSansMath`, neither of which this
project had.

The `reading` interval preset asks for U+2150–22FF. The build resolves an
interval against the family's own face and then the fallback, and **drops any
codepoint neither supplies**. So arrows were requested, found nowhere, and
pruned — silently, because a pruned interval is not an error.

Only **TeX Gyre Schola** shipped arrows, because its own face carries four of
them. That is the whole of "only one font has arrows".

Three of the installed families never even asked: `Coelacanth`,
`InknutJunicode` and `LibrisADF` use `latin-ext`, which does not include the
block. No fallback can supply what was not requested.

## What it is now

A **chain**, tried in order, `os.pathsep`-separated:

| Order | Face | Why |
|---|---|---|
| 1 | **TeX Gyre Schola** | Owner ruling: *"make texgyre with primary"*. Already a shipped family here, no new dependency. |
| 2 | Noto Sans | The language net: Greek, Cyrillic, Latin Extended. |
| 3 | Noto Sans Math | The only face here that answers for arrows. |

### Why Noto was not dropped

The owner asked *"drop noto"*. It was not taken, and the reason is measured
rather than argued:

| Face | codepoints | Arrows block |
|---|---|---|
| TeX Gyre Schola | 1,053 | 4 / 128 |
| Noto Sans | 2,965 | 0 / 128 |
| Noto Sans Symbols | 840 | 10 / 128 |
| Noto Sans Math | — | **112 / 128** |

Replacing Noto Sans with TeX Gyre outright takes **Coelacanth from 79 uncovered
codepoints to 1,123**, and **Edgar from 1,301 to 2,692** — TeX Gyre has no Greek
and no Cyrillic. Ordering TeX Gyre *first* gives it every glyph it has, which is
the instruction, while leaving Noto to answer for what it does not.

## Verified in the app, not in the files

A generated test EPUB with an Arrows row, opened on the iPhone Air simulator:

- **before**: twelve replacement boxes
- **after**: ← ↑ → ↓ ↔ ↕ ↩ ↪ ↵ ⇐ ⇒ ⇔

All nine installed families (six reading + three iA editor faces) now carry the
block at both the 1x and 2x tiers.

## Three traps this cost, recorded

1. **The app re-seeds its bundled fonts over `Documents` on every launch.**
   Patching the simulator's container proves nothing; the app must be rebuilt.
   Three false negatives before this was noticed.
2. **`simctl install` moves the data container.** Fonts were copied into a
   stale UUID and coverage read back from files the app never opened.
3. **Replacing a family directory wholesale destroys its `2x/` tier**, because
   a 1x build output has no `2x` subdirectory. All six reading families lost
   their hi-res fonts this way and had to be rebuilt.

## Noto Sans as a built-in (same day)

Separate owner instruction: *"put noto sans in as builtin"*. It is **not** a
second chrome family — Libre Franklin remains the only face the UI draws with
(ruling 2026-08-07) — it is the **coverage face behind it**, registered under
its own ids and attached with `setFallbackFont`, the same hook the SD-card CJK
path already uses. 2,965 codepoints against Libre Franklin's 918.

**Measured cost**, because a hex-source estimate is worthless here:

| | flash | % |
|---|---|---|
| before | 4,422,757 B | 67.5% |
| after | 4,941,959 B | 75.4% |
| delta | **+519,202 B** | +7.9 pts |

RAM +72 B. 1.61 MB still free. Including the headers alone cost **zero** — the
linker drops what nothing references — so that figure comes from the wired
build, not the include.

**It carries no arrows**: 0 of 128, measured. Adding it for arrows would have
been adding it for something it cannot do.

Hi-res (2x/3x) companions are generated and registered, so a fallback glyph on a
scaled host build blits at the page's density rather than one third of it. None
of them reach the device binary — no device env sets `CROSSPOINT_RENDER_SCALE`.
