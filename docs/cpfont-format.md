# The `.cpfont` format

*Written 2026-08-24 against firmware commit `aa144fd77`
("fix(css,notes): the spaced `! important`, and a count that doubled") and the
simulator working tree at `~/src/crosspoint-simulator`. Everything in the
"Worked example" section was run against a real shipped file and the numbers
printed there are measurements, not derivations. Where I could not establish
something from the source it is listed at the foot under
[What I could not establish](#what-i-could-not-establish) rather than guessed.*

This document is meant to be sufficient on its own. A competent implementer
should be able to write both a generator and a reader for `.cpfont` from this
page without opening the source — and every structural claim carries a
`file:line` so that when the code moves, the disagreement is findable.

**Related docs, which this one does not repeat:**
[docs/sd-card-fonts.md](sd-card-fonts.md) (which families ship, and why),
[docs/font-fallback-chain.md](font-fallback-chain.md) (the coverage chain),
[docs/synthetic-font-styles.md](synthetic-font-styles.md) (build-time embolden
and shear), [docs/render-scale.md](render-scale.md) (the tier machinery),
[docs/font-unicode-coverage.md](font-unicode-coverage.md) (what the books
actually ask for), and
[crosspoint-simulator/docs/seed-font-compression.md](../../crosspoint-simulator/docs/seed-font-compression.md)
(the CPZ1 container, summarized in §7 here).

---

## Table of contents

1. [What a `.cpfont` is](#1-what-a-cpfont-is)
2. [The byte layout, completely](#2-the-byte-layout-completely)
3. [Glyph bitmaps](#3-glyph-bitmaps)
4. [What is generated, and by what](#4-what-is-generated-and-by-what)
5. [What is dynamic and what is fixed](#5-what-is-dynamic-and-what-is-fixed)
6. [The runtime reader](#6-the-runtime-reader)
7. [The CPZ1 compression container](#7-the-cpz1-compression-container)
8. [Versioning and compatibility](#8-versioning-and-compatibility)
9. [Gotchas and failure modes](#9-gotchas-and-failure-modes)
10. [Worked example: `Almendra_14.cpfont`](#10-worked-example-almendra_14cpfont)
11. [What I could not establish](#what-i-could-not-establish)

---

## 1. What a `.cpfont` is

A `.cpfont` file is one **typeface family at one point size, already
rasterized** — four styles (regular, bold, italic, bold-italic) of pre-rendered
2-bit-per-pixel glyph bitmaps, plus the metrics, kerning and ligature tables
needed to set them into lines, in a single file laid out so that a reader can
seek straight to any one glyph without parsing anything before it.

It exists because the device cannot rasterize. CrossPoint runs on an ESP32-C3
with roughly 380 KB of usable heap — the number that governs every design
decision below is quoted in the source as a 380 KB device
([SdCardFont.h:184-188](../lib/EpdFont/SdCardFont.h)) — and a page of book text
must be laid out and painted inside it, alongside the framebuffer. Shipping
TTF/OTF outlines would mean carrying a hinting and rasterizing engine
(FreeType is larger than the entire firmware), running it per glyph per page
turn on a single 160 MHz RISC-V core, and holding an outline cache. None of
that fits, and none of it buys anything: an e-ink panel with four gray levels
cannot show more than a `.cpfont` already stores.

So the rasterizing is moved off the device entirely. `fontconvert_sdcard.py`
runs FreeType on a desktop at build time, quantizes each glyph's antialiased
coverage down to the four levels the panel can actually drive, and writes the
result to a file the device streams from the SD card. On the device the whole
"font engine" is a binary search over an interval table and a `memcpy` from
the card.

**Why 2 bits and not 1, and not 8.** The panels drive four levels: the X3's
UC8253 and the X4's SSD1677 both expose a grayscale nudge that can lift a black
pixel part of the way toward white but cannot darken a white one
([GlyphAaPlanes.h:14-33](../lib/GfxRenderer/GlyphAaPlanes.h)). Two bits is
exactly the panel's own resolution, so nothing is stored that cannot be shown
and nothing shown is approximated. It also halves the bitmap payload against a
4-bit store, and the payload is where the entire file's weight lives: measured
across the shipped seven-family 1x+2x seed tree, **108,068,437 of 117,654,860
bytes are glyph bitmap**
([seed-font-compression.md](../../crosspoint-simulator/docs/seed-font-compression.md)).

**What one file covers, and what it does not.**

| One `.cpfont` holds | It does not hold |
|---|---|
| Up to 4 styles (regular / bold / italic / bold-italic), each complete and independent | More than one point size — the size is in the filename |
| One glyph raster per codepoint per style, at one rendering scale | Outlines, hinting instructions, or anything rescalable |
| Per-glyph advance, bearings and bitmap extent | Vertical metrics per glyph (only per style) |
| A class-based kerning matrix per style | GPOS anchors, mark attachment, contextual positioning |
| A flat two-codepoint ligature substitution table per style | Any other GSUB (contextual alternates, small caps, swashes) |
| Per-style line advance, ascender and descender | A name table, a license string, or any metadata at all |

There is no metadata section. The family name comes from the containing
directory and the point size from the filename
([SdCardFontRegistry.h:7-13](../lib/EpdFont/SdCardFontRegistry.h)).

---

## 2. The byte layout, completely

### 2.0 Conventions

* **Every multi-byte field is little-endian.** The format is defined as the
  in-memory struct layout of the ESP32-C3 (little-endian RISC-V), and the
  reader reads several tables by `read()`ing bytes straight into the C structs
  (`file.read(reinterpret_cast<uint8_t*>(&iv), sizeof(iv))`,
  [SdCardFont.cpp:830](../lib/EpdFont/SdCardFont.cpp)). A big-endian host
  cannot read a `.cpfont` without byte-swapping every table.
* **Struct padding is part of the format.** `EpdGlyph` is *not* declared
  packed; it gets 2 bytes of tail padding from natural alignment and the file
  reserves them. `EpdKernClassEntry` and `EpdLigaturePair` *are* packed
  (`EPD_PACKED_BEGIN` / `EPD_PACKED_ATTR` / `EPD_PACKED_END`,
  [EpdFontData.h:115-123](../lib/EpdFont/EpdFontData.h)) — those macros expand
  to `__attribute__((packed))` on GCC/Clang and to `#pragma pack(push,1)` on
  MSVC, precisely so the host unit tests and the firmware agree about the file.
  The four sizes are asserted at compile time
  ([SdCardFont.cpp:16-19](../lib/EpdFont/SdCardFont.cpp)):

  ```cpp
  static_assert(sizeof(EpdGlyph) == 16, "EpdGlyph must be 16 bytes to match .cpfont file layout");
  static_assert(sizeof(EpdUnicodeInterval) == 12, "EpdUnicodeInterval must be 12 bytes to match .cpfont file layout");
  static_assert(sizeof(EpdKernClassEntry) == 3, "EpdKernClassEntry must be 3 bytes to match .cpfont file layout");
  static_assert(sizeof(EpdLigaturePair) == 8, "EpdLigaturePair must be 8 bytes to match .cpfont file layout");
  ```
* **There is no padding or alignment between sections.** Each section begins
  where the previous one ended.
* **Units.** Pixel counts are whole pixels at the file's own rendering scale.
  `advanceX` is 12.4 unsigned fixed-point (uint16, 1/16 px). Kern values are
  4.4 signed fixed-point (int8, 1/16 px, range −8.0 to +7.9375). Both share 4
  fractional bits so they add directly in one accumulator before a single
  rounding step ([EpdFontData.h:7-31, 125-128](../lib/EpdFont/EpdFontData.h)).

### 2.1 The file at a glance

```
+-------------------------------------------+ 0
| Global header                    32 bytes |
+-------------------------------------------+ 32
| Style TOC entry [0]              32 bytes |
| Style TOC entry [1]              32 bytes |   styleCount entries,
| ...                                       |   written in ascending styleId
+-------------------------------------------+ 32 + 32*styleCount
| Style block for TOC[0]                    |   at TOC[0].dataOffset
|   intervals                               |
|   glyphs                                  |
|   kern left classes                       |
|   kern right classes                      |
|   kern matrix                             |
|   ligature pairs                          |
|   glyph bitmaps                           |
+-------------------------------------------+ at TOC[1].dataOffset
| Style block for TOC[1]  (same 7 sections) |
+-------------------------------------------+
| ...                                       |
+-------------------------------------------+ EOF
```

The writer emits the style blocks back-to-back in ascending `styleId` order and
sets each `dataOffset` to the running total
([fontconvert_sdcard.py:1365-1372, 1410-1412](../lib/EpdFont/scripts/fontconvert_sdcard.py)),
so in every shipped file the blocks are contiguous and in order. **A reader
must not rely on that**: it seeks to `dataOffset` per style and derives the six
other section offsets by arithmetic
([SdCardFont.cpp:695-704](../lib/EpdFont/SdCardFont.cpp)). Only the last
style's bitmap section has an implicit end (EOF); every other section's length
is a count times a size.

### 2.2 Global header — 32 bytes at offset 0

Writer: `struct.pack("<8sHHB19s", MAGIC, CPFONT_VERSION, flags, style_count, bytes(19))`
([fontconvert_sdcard.py:1376](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
Reader: [SdCardFont.cpp:724-750](../lib/EpdFont/SdCardFont.cpp).

| Offset | Size | Type | Field | Value / meaning |
|---:|---:|---|---|---|
| 0 | 8 | bytes | `magic` | `43 50 46 4F 4E 54 00 00` — ASCII `"CPFONT"` then two NUL bytes. Compared with `memcmp` over all 8 ([SdCardFont.cpp:36, 730](../lib/EpdFont/SdCardFont.cpp)). |
| 8 | 2 | uint16 LE | `version` | `4` today. Compared for **exact equality** with `CPFONT_VERSION` ([SdCardFont.cpp:735-739](../lib/EpdFont/SdCardFont.cpp)). |
| 10 | 2 | uint16 LE | `flags` | Bit 0 = `is2Bit`. The reader reads only bit 0 (`(readU16(headerBuf + 10) & 1) != 0`, [SdCardFont.cpp:744](../lib/EpdFont/SdCardFont.cpp)); the writer always writes `1` ([fontconvert_sdcard.py:1338](../lib/EpdFont/scripts/fontconvert_sdcard.py), `flags = 1  # always 2-bit grayscale`). Bits 1–15 are unassigned and unread. |
| 12 | 1 | uint8 | `styleCount` | Number of style TOC entries that follow. Rejected unless `1 <= styleCount <= 4` (`MAX_STYLES`, [SdCardFont.h:25](../lib/EpdFont/SdCardFont.h); check at [SdCardFont.cpp:746-750](../lib/EpdFont/SdCardFont.cpp)). |
| 13 | 19 | bytes | `reserved` | Written as 19 zero bytes. Not read, but it **is** hashed (see §2.7). |

`flags` is a **file-global** property, not per-style: the single `is2Bit` bit is
copied into every style's header at load
([SdCardFont.cpp:784](../lib/EpdFont/SdCardFont.cpp)). A file cannot mix 1-bit
and 2-bit styles.

### 2.3 Style TOC entry — 32 bytes each, `styleCount` of them, starting at offset 32

Writer format string `"<B3xIIBhhHHBBBI4x"`
([fontconvert_sdcard.py:1383-1402](../lib/EpdFont/scripts/fontconvert_sdcard.py));
reader parses it **field by field with byte-wise helpers**, never by casting a
struct over it ([SdCardFont.cpp:764-802](../lib/EpdFont/SdCardFont.cpp)) —
which is why the 16-bit fields at odd offsets 13, 15, 17 and 19 are legal.

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 1 | uint8 | `styleId` | `0`=regular, `1`=bold, `2`=italic, `3`=bold-italic ([fontconvert_sdcard.py:1421](../lib/EpdFont/scripts/fontconvert_sdcard.py); matches `EpdFontFamily::Style` bits 0–1, [EpdFontFamily.h:10-20](../lib/EpdFont/EpdFontFamily.h)). Rejected if `>= 4` ([SdCardFont.cpp:765-770](../lib/EpdFont/SdCardFont.cpp)). |
| 1 | 3 | — | pad | Written as zero (`3x`). Not read. |
| 4 | 4 | uint32 LE | `intervalCount` | Entries in this style's interval table. Rejected if `> 4096` (`MAX_INTERVALS`). |
| 8 | 4 | uint32 LE | `glyphCount` | Entries in this style's glyph table. Rejected if `> 65536` (`MAX_GLYPHS`). |
| 12 | 1 | uint8 | `advanceY` | Line advance (leading) in whole pixels. The writer `sys.exit(1)`s if it would exceed 255 ([fontconvert_sdcard.py:1389-1394](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |
| 13 | 2 | int16 LE | `ascender` | Maximum glyph height above the baseline, whole pixels. Widened to `int` in RAM (`EpdFontData::ascender`). |
| 15 | 2 | int16 LE | `descender` | Maximum extent below the baseline, whole pixels, **negative** in every shipped file. |
| 17 | 2 | uint16 LE | `kernLeftEntryCount` | Entries in the left kern-class table. Rejected if `> 4096` (`MAX_KERN_ENTRIES`). |
| 19 | 2 | uint16 LE | `kernRightEntryCount` | Entries in the right kern-class table. Same cap. |
| 21 | 1 | uint8 | `kernLeftClassCount` | Rows in the kern matrix. Bounded by its own type; **no separate check**. |
| 22 | 1 | uint8 | `kernRightClassCount` | Columns in the kern matrix. Same. |
| 23 | 1 | uint8 | `ligaturePairCount` | Entries in the ligature table. Bounded by its own type; the writer truncates at 255 with a warning ([fontconvert_sdcard.py:1216-1219](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |
| 24 | 4 | uint32 LE | `dataOffset` | **Absolute** byte offset of this style's first section (its interval table). |
| 28 | 4 | — | reserved | Written as zero (`4x`). Not read; hashed. |

**Presence is per-`styleId`, not per-slot.** The reader marks
`styles_[styleId].present = true` as it walks the TOC
([SdCardFont.cpp:772-773](../lib/EpdFont/SdCardFont.cpp)), so a legal file may
carry, say, only styles 0 and 2. Every shipped family carries all four.

### 2.4 Deriving the seven section offsets

Given a style's `dataOffset` (call it `base`), all seven section offsets are
arithmetic — the file stores none of them
([SdCardFont.cpp:695-704](../lib/EpdFont/SdCardFont.cpp)):

| Section | Offset | Length in bytes |
|---|---|---|
| 1. Intervals | `base` | `intervalCount * 12` |
| 2. Glyphs | `intervals + intervalCount*12` | `glyphCount * 16` |
| 3. Kern left classes | `glyphs + glyphCount*16` | `kernLeftEntryCount * 3` |
| 4. Kern right classes | `kernLeft + kernLeftEntryCount*3` | `kernRightEntryCount * 3` |
| 5. Kern matrix | `kernRight + kernRightEntryCount*3` | `kernLeftClassCount * kernRightClassCount` |
| 6. Ligature pairs | `kernMatrix + kernLeftClassCount*kernRightClassCount` | `ligaturePairCount * 8` |
| 7. Glyph bitmaps | `ligatures + ligaturePairCount*8` | *implicit* — the sum of every glyph's `dataLength` |

Any count may be zero, in which case that section occupies no bytes. A style
with no kerning has `kernLeftEntryCount = kernRightEntryCount =
kernLeftClassCount = kernRightClassCount = 0` and therefore no kern sections at
all.

The bitmap section's length is never written down. The reader does not need it
— it seeks to `bitmapFileOffset + glyph.dataOffset` and reads
`glyph.dataLength` bytes
([SdCardFont.cpp:1226-1262](../lib/EpdFont/SdCardFont.cpp), the prewarm bitmap
loop). A validator can reconstruct it as
`max(dataOffset + dataLength)` over the glyph table, which in every shipped
file equals `sum(dataLength)` because the writer lays bitmaps out contiguously
in glyph order ([fontconvert_sdcard.py:1307-1310](../lib/EpdFont/scripts/fontconvert_sdcard.py)).

### 2.5 Section 1 — the interval table

**What it is.** The codepoint→glyph-index map, stored as sorted ranges rather
than a per-codepoint list, because a font's coverage is naturally blocky:
Almendra 14 covers 2,676 codepoints in 61 intervals.

`EpdUnicodeInterval`, [EpdFontData.h:150-155](../lib/EpdFont/EpdFontData.h) —
12 bytes, **not** packed (natural alignment already gives 12):

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 4 | uint32 LE | `first` | First codepoint of the range, inclusive |
| 4 | 4 | uint32 LE | `last` | Last codepoint of the range, inclusive |
| 8 | 4 | uint32 LE | `offset` | Index into the glyph table of the glyph for `first` |

**Lookup is a binary search**, and the glyph index is then direct arithmetic:

```
glyphIndex = interval.offset + (codepoint - interval.first)
```

The device uses `std::upper_bound` on `first` and then one bounds check against
`last` ([EpdFont.cpp:165-180](../lib/EpdFont/EpdFont.cpp)); `SdCardFont` uses
its own hand-rolled binary search over the same table
([SdCardFont.cpp:925-942](../lib/EpdFont/SdCardFont.cpp)). Both return −1 /
`nullptr` on a miss.

**The invariants a reader enforces** (all at
[SdCardFont.cpp:822-859](../lib/EpdFont/SdCardFont.cpp), checked before
anything trusts the table):

1. `first <= last` for every interval.
2. Intervals are strictly ascending and non-overlapping: `interval[j].first > interval[j-1].last`.
3. `span = last - first + 1` must be `<= glyphCount`.
4. **`offset` must equal the exact running sum of every preceding span**, starting at 0. Not merely consistent — exact. So the interval table's `offset` column is fully redundant with the rest of the table, and a generator that computes it any other way fails the load with `invalid interval layout`.
5. `offset <= glyphCount - span` (no overrun).

Consequently `sum(span)` over all intervals **equals** `glyphCount`, and glyph
indices are dense: every glyph in the table belongs to exactly one interval.

**The compact in-RAM form.** After validation, if `glyphCount <= 65535` and
every `first`, `last` and `offset` in the table fits in 16 bits, the reader
re-reads the table into a 6-byte `BmpInterval16` instead of keeping the 12-byte
on-disk form resident ([SdCardFont.h:172-181](../lib/EpdFont/SdCardFont.h),
[SdCardFont.cpp:867-883](../lib/EpdFont/SdCardFont.cpp)). This halves the
always-resident coverage index; the comment says why it matters — "large sparse
CJK subsets otherwise keep tens of KB of always-resident heap just for lookup
metadata" ([SdCardFont.cpp:808-811](../lib/EpdFont/SdCardFont.cpp)). **This is
a RAM representation only. Nothing about the file changes.**

### 2.6 Section 2 — the glyph table

**What it is.** One fixed-size record per glyph, indexed directly by the number
the interval table produced. No search, no per-glyph header in the payload.

`EpdGlyph`, [EpdFontData.h:130-139](../lib/EpdFont/EpdFontData.h); writer
format `"<BBHhhH2xI"`
([fontconvert_sdcard.py:1242](../lib/EpdFont/scripts/fontconvert_sdcard.py)) —
16 bytes:

| Offset | Size | Type | Field | Units / meaning |
|---:|---:|---|---|---|
| 0 | 1 | uint8 | `width` | Bitmap width, whole pixels. **Hard cap 255.** |
| 1 | 1 | uint8 | `height` | Bitmap height, whole pixels. **Hard cap 255.** |
| 2 | 2 | uint16 LE | `advanceX` | Pen advance, **12.4 fixed-point** (divide by 16 for pixels). Max 4095.9375 px. |
| 4 | 2 | int16 LE | `left` | Signed X offset from the pen position to the bitmap's left edge, whole pixels. Negative for glyphs that overhang left (Almendra's `A` is −2). |
| 6 | 2 | int16 LE | `top` | Signed Y offset from the **baseline** to the bitmap's top edge, whole pixels, positive upward. |
| 8 | 2 | uint16 LE | `dataLength` | Bitmap byte count. Equals `ceil(width * height / 4)`. Max 65535 bytes ⇒ max 262,140 pixels. |
| 10 | 2 | — | pad | Alignment padding for the uint32 that follows (`2x` in the writer). Zero. |
| 12 | 4 | uint32 LE | `dataOffset` | Byte offset of this glyph's bitmap **relative to the start of this style's bitmap section**, not to the file. |

A glyph the face cannot supply at all is written as all zeros with `dataLength
= 0` and `dataOffset` = the current bitmap high-water mark
([fontconvert_sdcard.py:946-949](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
So is a space: `U+0020` in Almendra 14 is `width=0 height=0 dataLength=0
advanceX=84` — a real advance with no ink. **`dataLength == 0` is normal and
must not be treated as an error**; the runtime distinguishes "no bitmap" from
"lookup failed" explicitly
([GfxRenderer.cpp:79-89](../lib/GfxRenderer/GfxRenderer.cpp)).

`dataOffset` being section-relative is what lets `SdCardFont` rewrite it in
place when it copies a page's glyphs into its own small arena: the prewarmed
`miniGlyphs[i].dataOffset` is re-pointed at the mini bitmap buffer
([SdCardFont.cpp:1261](../lib/EpdFont/SdCardFont.cpp)), and the same
struct then serves as an in-RAM font.

### 2.7 Sections 3 and 4 — the kern class tables

**What they are.** Kerning is stored the way OpenType stores it: not as a pair
list, but as two codepoint→class maps and a dense matrix indexed by the two
classes. A face with 12 left classes and 9 right classes covers up to 108 pairs
in 60 matrix bytes plus 63 bytes of class table.

`EpdKernClassEntry`, [EpdFontData.h:157-164](../lib/EpdFont/EpdFontData.h) —
**3 bytes, explicitly packed**:

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 2 | uint16 LE | `codepoint` | **BMP only.** Codepoints above U+FFFF cannot be expressed and are dropped by the generator ([fontconvert_sdcard.py:1197-1200](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |
| 2 | 1 | uint8 | `classId` | **1-based.** A codepoint absent from the table has implicit class 0, which means "no kerning" ([EpdFontData.h:157-158](../lib/EpdFont/EpdFontData.h)). |

Both tables are **sorted ascending by codepoint** and looked up by binary
search (`std::lower_bound` plus an equality check,
[EpdFont.cpp:86-104](../lib/EpdFont/EpdFont.cpp)). The left table maps the
*preceding* character, the right table the *following* one; a codepoint may
appear in both, with different class IDs, and commonly does not appear in
either.

### 2.8 Section 5 — the kern matrix

A flat array of `kernLeftClassCount * kernRightClassCount` **`int8_t`** values
in **row-major** order, rows indexed by left class, columns by right class.
Each value is a kern in **4.4 signed fixed-point pixels** (÷16; range −8.0 to
+7.9375).

The full lookup, in one place
([EpdFont.cpp:106-118](../lib/EpdFont/EpdFont.cpp)):

```cpp
int8_t EpdFont::getKerning(const uint32_t leftCp, const uint32_t rightCp) const {
  if (utf8IsCjkBreakable(leftCp) || utf8IsCjkBreakable(rightCp)) return 0;
  if (!data->kernMatrix) return 0;
  const uint8_t lc = lookupKernClass(data->kernLeftClasses,  data->kernLeftEntryCount,  leftCp);
  if (lc == 0) return 0;
  const uint8_t rc = lookupKernClass(data->kernRightClasses, data->kernRightEntryCount, rightCp);
  if (rc == 0) return 0;
  return data->kernMatrix[(lc - 1) * data->kernRightClassCount + (rc - 1)];
}
```

Note the three things a reimplementer gets wrong here: the class IDs are
**1-based so the index subtracts one**; the row stride is
**`kernRightClassCount`, not `kernLeftClassCount`**; and CJK-breakable
codepoints are refused kerning before the tables are even consulted.

### 2.9 Section 6 — the ligature pair table

`EpdLigaturePair`, [EpdFontData.h:166-173](../lib/EpdFont/EpdFontData.h) —
**8 bytes, explicitly packed**:

| Offset | Size | Type | Field | Meaning |
|---:|---:|---|---|---|
| 0 | 4 | uint32 LE | `pair` | `(leftCodepoint << 16) \| rightCodepoint`. Both halves are therefore BMP-only. |
| 4 | 4 | uint32 LE | `ligatureCp` | Codepoint of the replacement glyph. Must itself be covered by this style's interval table, or the substitution renders as a missing glyph. |

**Sorted ascending by `pair`**, looked up by binary search on the packed key
([EpdFont.cpp:120-140](../lib/EpdFont/EpdFont.cpp)). Because the key packs both
codepoints into one uint32, the search is a single `lower_bound` — there is no
two-level lookup.

**Three-codepoint ligatures are expressed as chains.** The table only encodes
pairs, so `f`+`f`+`i` is stored as two entries: `(f,f) → U+FB00` and then
`(U+FB00, i) → U+FB03`. `applyLigatures` runs the substitution greedily in a
loop, feeding each result back in as the new left-hand codepoint
([EpdFont.cpp:142-158](../lib/EpdFont/EpdFont.cpp)), which is what makes the
chain work at runtime. The generator does the matching decomposition
([fontconvert_sdcard.py:611-624](../lib/EpdFont/scripts/fontconvert_sdcard.py))
and **drops** a 3+ sequence whose prefix has no ligature of its own, with a
warning.

### 2.10 Section 7 — the glyph bitmap payload

A single concatenated blob of every glyph's packed bitmap, in glyph-table
order, with no per-glyph header, no separator and no alignment. A glyph's bytes
are `[dataOffset, dataOffset + dataLength)` relative to the section start.
Zero-length glyphs occupy nothing. The encoding is §3.

### 2.11 Fields in `EpdFontData` that are *not* in the file

`EpdFontData` ([EpdFontData.h:175-216](../lib/EpdFont/EpdFontData.h)) is the
runtime view, and it is wider than the format. These members have **no on-disk
representation** and are always null/zero for an SD-card font:

| Member | Why it is not in a `.cpfont` |
|---|---|
| `groups`, `groupCount`, `glyphToGroup` | The DEFLATE-compressed group machinery (`EpdFontGroup`, [EpdFontData.h:141-148](../lib/EpdFont/EpdFontData.h)) used by the **flash-resident built-in** fonts through `FontDecompressor`. `SdCardFont` never sets these; `GfxRenderer::getGlyphBitmap` branches on `fontData->groups != nullptr` to choose between the two worlds ([GfxRenderer.cpp:66-92](../lib/GfxRenderer/GfxRenderer.cpp)). |
| `glyphMissHandler`, `glyphMissCtx` | The on-demand overflow path — a function pointer set at load ([SdCardFont.cpp:676-684](../lib/EpdFont/SdCardFont.cpp)). |
| `coverageHandler` | Answers `hasCodepoint()` from the full RAM coverage index when the *resident* interval table (a per-page subset) misses. |

---

## 3. Glyph bitmaps

**What they are.** Each glyph is a rectangle of `width × height` pixels, each
pixel two bits, packed four to a byte, with **no row padding at all** — the
bitstream runs continuously from the last pixel of one row into the first pixel
of the next. Only the very end of the glyph is padded, to the next whole byte.

### 3.1 The four levels

| Stored value | Meaning | FreeType 8-bit coverage that produces it |
|---:|---|---|
| `0` | No ink — paper | `0 … 63` |
| `1` | Low coverage — light gray | `64 … 127` |
| `2` | High coverage — dark gray | `128 … 191` |
| `3` | Full ink — black | `192 … 255` |

The generator quantizes in two steps — it first builds a 4-bit gray buffer
(`px = v >> 4`) and then thresholds the nibble at `>= 12 / >= 8 / >= 4`
([fontconvert_sdcard.py:1090-1121](../lib/EpdFont/scripts/fontconvert_sdcard.py))
— which is arithmetically the same as thresholding the 8-bit value at
192/128/64. The intermediate 4-bit stage is a legacy of `fontconvert.py` and
has no effect on the result.

The renderer states the same mapping in a comment and then **inverts it once**
so that the rest of the pipeline can think in image terms
([GfxRenderer.cpp:576-579](../lib/GfxRenderer/GfxRenderer.cpp)):

```cpp
// the direct bit from the font is 0 -> white, 1 -> light gray, 2 -> dark gray, 3 -> black
// we swap this to better match the way images and screen think about colors:
// 0 -> black, 1 -> dark gray, 2 -> light gray, 3 -> white
const uint8_t bmpVal = 3 - ((byte >> bit_index) & 0x3);
```

**Do not confuse the two numberings.** On disk, higher means more ink. Inside
`renderCharImpl` and inside
[GlyphAaPlanes.h](../lib/GfxRenderer/GlyphAaPlanes.h), *lower* means more ink
(`0 = full ink`, `3 = no ink`). Every mask in that header is written in the
swapped numbering.

### 3.2 The exact bit math

For a pixel at `(x, y)` with `0 <= x < width` and `0 <= y < height`:

```
p          = y * width + x            // one running counter, no row alignment
byteIndex  = p >> 2                   // == p / 4
shift      = (3 - (p & 3)) * 2        // MSB-first within the byte
rawLevel   = (bitmap[byteIndex] >> shift) & 0x3
```

So pixel `p % 4 == 0` occupies bits 7–6, `== 1` bits 5–4, `== 2` bits 3–2,
`== 3` bits 1–0. Reader:
[GfxRenderer.cpp:558-561, 574-579](../lib/GfxRenderer/GfxRenderer.cpp). Writer:
[fontconvert_sdcard.py:1107-1133](../lib/EpdFont/scripts/fontconvert_sdcard.py),
which shifts left by 2 and flushes on `(y * render_w + x) % 4 == 3` — the same
global counter.

Total bytes:

```
dataLength = ceil(width * height / 4)
```

with the final byte's unused low bits **shifted in as zeros**
(`px << ((4 - (render_w * render_rows) % 4) * 2)`,
[fontconvert_sdcard.py:1126-1133](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
A validator can and should check `dataLength == (width * height + 3) / 4` for
every glyph; it holds on every glyph of the worked example in §10.

**Corroboration that rows are not padded**, since this is the single easiest
thing to get wrong: [FontDecompressor.cpp:119-141](../lib/EpdFont/FontDecompressor.cpp)
exists *specifically to convert* row-padded data (`rowStride = (width + 3) / 4`,
which is how the built-in fonts' DEFLATE groups store glyphs) **into** this
continuous packing, and it takes a `memcpy` shortcut only when
`width % 4 == 0`, i.e. exactly when the two layouts coincide.

### 3.3 Origin, bearings and advance

The pen sits at `(cursorX, cursorY)` where **`cursorY` is the baseline** — the
draw path computes it as `y + getFontAscenderSize(fontId)`
([GfxRenderer.cpp:743](../lib/GfxRenderer/GfxRenderer.cpp)). The glyph bitmap's
top-left pixel lands at:

```
screenX = cursorX + glyph->left
screenY = cursorY - glyph->top
```

([GfxRenderer.cpp:501-504](../lib/GfxRenderer/GfxRenderer.cpp): `outerBase =
cursorY - top`, `innerBase = cursorX + left`.) `top` is measured **upward from
the baseline**, hence the subtraction; `left` is signed and negative for a
glyph that overhangs to the left of the pen.

The pen then advances by `advanceX`, in 12.4 fixed-point, **combined with the
kern for the pair before any rounding**. This is "differential rounding"
([EpdFontData.h:11-15](../lib/EpdFont/EpdFontData.h)) and it is the reason
`advanceX` and the kern share four fractional bits:

```cpp
const EpdGlyph* glyph = font.getGlyph(cp, style);          // fetched BEFORE the kern
if (prevCp != 0) {
  const int8_t kernFP = glyph ? font.getKerning(prevCp, cp, style) : 0;
  lastBaseX += fp4::toPixel(prevAdvanceFP + kernFP);       // ONE rounding step
}
prevAdvanceFP = glyph ? glyph->advanceX : 0;
```

([GfxRenderer.cpp:815-828](../lib/GfxRenderer/GfxRenderer.cpp); the same walk
appears in [EpdFont.cpp:52-55, 71](../lib/EpdFont/EpdFont.cpp) and twice more in
the rotated draw paths.) `fp4::toPixel(fp) = (fp + 8) >> 4`, round-to-nearest
([EpdFontData.h:27](../lib/EpdFont/EpdFontData.h)). Rounding the advance and
the kern separately would make identical letter pairs get different pixel gaps
depending on where they fall on the line, which is exactly what this avoids.

### 3.4 How the levels reach the panel

The panel's grayscale overlay is a **one-way nudge**: it can lift a black pixel
part of the way toward white and it cannot darken a white one — the X3's
UC8253 gray bank has a dead white→black cell and the X4's SSD1677 emits no
darkening group ([GlyphAaPlanes.h:11-33](../lib/GfxRenderer/GlyphAaPlanes.h)).
So a glyph is painted in up to three passes over the same pixels: a black/white
base pass and two plane-flag passes (MSB and LSB), and **which levels
participate in which pass depends on the polarity**
([GfxRenderer.cpp:553-595](../lib/GfxRenderer/GfxRenderer.cpp),
[GlyphAaPlanes.h:67-108](../lib/GfxRenderer/GlyphAaPlanes.h)). Bit *N* of each
mask corresponds to *swapped* level *N*:

| Polarity | Strength | `baseInk` | `msb` | `lsb` |
|---|---|---|---|---|
| light | Standard | L0 \| L1 \| L2 | L1 \| L2 | L1 |
| light | Crisp | L0 \| L1 \| L2 | L2 | — |
| light | Dark | L0 \| L1 \| L2 | L2 | L2 |
| dark (with AA overlay) | Standard | L0 | L1 \| L2 | L2 |
| dark (with AA overlay) | Crisp | L0 \| L1 | L2 | L2 |
| dark (with AA overlay) | Dark | L0 \| L1 | L2 | — |

Swapped level 3 — no ink — appears in **no** mask and is never touched, which
is what makes a glyph's paper transparent to whatever is behind it.

The framebuffer itself is inverted from the intuitive polarity: `drawPixel`
**clears** a bit for ink and sets it for paper
([GfxRenderer.cpp:683-690](../lib/GfxRenderer/GfxRenderer.cpp)).

### 3.5 The half-scale path (superscript and subscript)

`EpdFontFamily::SUP` / `SUB` draw the same bitmap at 50% through a separate
routine ([GfxRenderer.cpp:364-430](../lib/GfxRenderer/GfxRenderer.cpp)):
`dstW = (srcW + 1) / 2`, `dstH = (srcH + 1) / 2` (ceil, so odd widths are not
clipped), bearings halved, and each destination pixel is a 2×2 box sample of
the source that becomes ink when `maxRaw >= 2 || coverage >= 2` where
`coverage` is the **sum of the four raw levels**. Note this path works in the
**raw** (unswapped) numbering and ignores `renderMode` entirely — there is no
antialiasing on scaled glyphs. The advance is halved separately in the cursor
walk (`prevAdvanceFP = (prevAdvanceFP + 1) / 2`,
[GfxRenderer.cpp:830-835](../lib/GfxRenderer/GfxRenderer.cpp)).

---

## 4. What is generated, and by what

**In one sentence:** `build-sd-fonts.py` reads `sd-fonts.yaml`, fetches and
patches the source outlines, and fans out one `fontconvert_sdcard.py` process
per family; that script runs FreeType and fontTools over the outlines and
writes the `.cpfont` files.

```
sd-fonts.yaml
     |
     v
build-sd-fonts.py ──> download / unzip / instance variable axes / patch cmap
     |                / override metrics / rescale upem   (all mtime-cached)
     |                + resolve the FALLBACK CHAIN per family
     |
     |  one subprocess per family, ProcessPoolExecutor
     v
fontconvert_sdcard.py --intervals ... --sizes ... --regular/--bold/... 
     |                --fallback-<style> <chain>  --synth-<style> --space-<style>
     |
     +-- FreeType: rasterize every requested codepoint at 150 DPI
     +-- fontTools: read GPOS `kern` + legacy `kern`  -> kern classes + matrix
     +-- fontTools: read GSUB `liga`/`rlig`           -> ligature pairs
     |
     v
<output>/<Family>/<Family>_<size>.cpfont          (1x)
<output>/<Family>/<N>x/<Family>_<size>.cpfont     (hi-res tier, --scale N)
```

`optical_kern.py` is **not** in this pipeline — see §4.6.

### 4.1 `sd-fonts.yaml` — the recipe file

Two top-level keys and nothing else
([sd-fonts.yaml:143-153, 193](../lib/EpdFont/scripts/sd-fonts.yaml)):

* `installed_families:` — the eight-family S tier. `build-sd-fonts.py`
  **ignores it** ([build-sd-fonts.py:870](../lib/EpdFont/scripts/build-sd-fonts.py));
  `scripts/install-sim-fonts.py` consumes it.
* `families:` — **52 entries** as of this commit.

Family-level keys (schema block at
[sd-fonts.yaml:8-61](../lib/EpdFont/scripts/sd-fonts.yaml); the counts are how
many of the 52 families actually use each):

| Key | Required | Used by | Meaning |
|---|---|---:|---|
| `name` | yes | 52 | Output directory and filename stem. Also the family name the device shows. |
| `description` | yes | 52 | Prose only; no code consumer left (the download UI that read it was removed 2026-08-10). |
| `intervals` | yes | 52 | Comma-separated preset names and/or `(0xAAAA-0xBBBB)` ranges. §4.2. |
| `sizes` | yes | 52 | Four point sizes. §4.3. |
| `styles` | yes | 52 | Map of `regular` / `bold` / `italic` / `bolditalic` → source spec. §4.4. |
| `metrics: {ascent, descent, linegap}` | no | 28 | Per-1000-upem override written into `hhea` and `OS/2` before rasterizing. `descent` must be negative ([build-sd-fonts.py:410-412](../lib/EpdFont/scripts/build-sd-fonts.py)). |
| `force_autohint` | no | 2 | Passes `FT_LOAD_FORCE_AUTOHINT`. |
| `drop_codepoints` | no | 1 | Codepoints to omit from this family at every size. |
| `line_height_scale` | no | 1 | Multiplies the computed `advanceY`. |
| `line_height` | no | **0** | Absolute `advanceY` in pixels; supported ([build-sd-fonts.py:739-740](../lib/EpdFont/scripts/build-sd-fonts.py)) but no family uses it, and it is wrong across a size ramp because it pins every size to one leading. |

Style-level keys, inside `styles:`:

| Key | Used | Meaning |
|---|---:|---|
| `url` | 161 | Download the source outline. |
| `path` | 30 | A file committed under `lib/EpdFont/`. |
| `zip` + `member` | 6 | Download an archive, extract one member. |
| `variable: {axis: value}` | 36 | Pin a variable font's axes with the fontTools instancer (`wght`, `wdth`, `opsz`, `ENLA` seen). |
| `from: <style>` | 11 | Reuse another style's *resolved* source file in this family — the basis for a synthetic style. |
| `synthetic: {...}` | 14 | Build-time embolden / shear / overprint / underline join. §4.7. |
| `scale: <n>` | 2 | Rescale the source's units-per-em, for mixing faces of different design sizes. |
| `word_space_em` | 2 | Advance-only word-space delta. |
| `tracking_em` | 0 | Advance-only letterspacing; supported ([build-sd-fonts.py:643-647](../lib/EpdFont/scripts/build-sd-fonts.py)), unused. |

Every patching step is **mtime-cached into its own directory** and written
atomically: `downloaded_fonts/`, `instanced_fonts/`, `patched_fonts/`,
`scaled_fonts/`
([build-sd-fonts.py:45-53, 216-523](../lib/EpdFont/scripts/build-sd-fonts.py)).

### 4.2 `intervals:` — what a preset resolves to

`resolve_intervals()`
([fontconvert_sdcard.py:101-129](../lib/EpdFont/scripts/fontconvert_sdcard.py))
takes a comma-separated list, resolves each name against `INTERVAL_PRESETS`
([:41-82](../lib/EpdFont/scripts/fontconvert_sdcard.py)) or parses it as
`(0xSTART-0xEND)` ([:85-98](../lib/EpdFont/scripts/fontconvert_sdcard.py)),
**always appends U+FFFD**, then sorts and merges overlapping *or adjacent*
ranges.

The presets, verbatim:

| Preset | Ranges |
|---|---|
| `ascii` | 0020–007E |
| `latin1` | 0080–00FF |
| `latin-ext` | 0020–007E, 0080–00FF, 0100–024F, 02B0–02FF, 1E00–1EFF, 2000–206F, FB00–FB06 |
| `greek` | 0370–03FF, 1F00–1FFF |
| `cyrillic` | 0400–04FF, 0500–052F |
| `hebrew` | 0590–05FF, FB1D–FB4F |
| `georgian` | 10A0–10FF, 2D00–2D2F |
| `armenian` | 0530–058F |
| `ethiopic` | 1200–137F, 1380–139F, 2D80–2DDF |
| `vietnamese` | 01A0–01B0, 1EA0–1EF9 |
| `punctuation` | 2000–206F |
| `cjk` | 3000–303F, 3040–309F, 30A0–30FF, 4E00–9FFF, F900–FAFF, FF00–FFEF |
| `hangul` | AC00–D7AF, 1100–11FF, 3130–318F |
| `cherokee` | 13A0–13FF, AB70–ABBF |
| `tifinagh` | 2D30–2D7F |
| `symbols` | 2070–209F, 20A0–20CF, 2150–218F, 2190–21FF, 2200–22FF, 2500–257F, 25A0–25FF, 2600–26FF, 2700–27BF |
| `reading` | 0020–024F, 02B0–02FF, 0300–036F, 0370–03FF, 0400–04FF, 1E00–1EFF, 2000–206F, 2070–209F, 20A0–20CF, 2150–218F, 2190–21FF, 2200–22FF, 2500–257F, 25A0–25FF, 2600–26FF, 2700–27BF, 2900–29FF, 2E00–2E7F, 3000–303F, FB00–FB06 |
| `builtin` | 0000–007F, 0080–00FF, 0100–017F, 01A0–01A1, 01AF–01B0, 01C4–021F, 0300–036F, 0400–04FF, 1EA0–1EF9, 2000–206F, 20A0–20CF, 2070–209F, 2190–21FF, 2200–22FF, FB00–FB06 |

`reading` is the tier baseline: Latin plus combining marks, Greek and Cyrillic
(physics and transliteration), the full punctuation and symbol tail, and the
`FB00–FB06` presentation ligatures.

**A requested codepoint is not a shipped codepoint.** Every interval is
validated against the font *and its fallback chain*, and a codepoint no face
can supply **splits the interval and is dropped**
([fontconvert_sdcard.py:884-935](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
That is why the interval table in the file is 61 intervals where the recipe
asked for far fewer ranges. Since 2026-08-20 the pruning prints a `PRUNED` line
listing every lost range, and `build-sd-fonts.py` re-prints those lines even in
quiet mode ([build-sd-fonts.py:796-804](../lib/EpdFont/scripts/build-sd-fonts.py))
— because the silent version of this shipped six families with no arrows
(B-035, [BUGS.md](../BUGS.md)).

### 4.3 Sizes, slots and the hi-res tiers

**There is no global list of sizes.** Each family declares its own four in
`sizes:` ([build-sd-fonts.py:617, 625](../lib/EpdFont/scripts/build-sd-fonts.py)).
All 52 families declare exactly four; across the file the ramps range from
`[8,10,12,14]` to `[15,18,20,23]`, with `[12,14,16,18]` the most common (30
families).

They differ because the tier is harmonized by **slot, not by point size**: the
same ordinal slot must show the same measured x-height (12/14/16/18 px) and the
same `advanceY` (34/40/46/51 px) across families, so a face with a small
x-height is built at a larger point size to land on the same slot
([sd-fonts.yaml:76-101](../lib/EpdFont/scripts/sd-fonts.yaml)). The reader
selects by slot for exactly this reason
(`SdCardFontFamilyInfo::findClosestReaderSize`,
[SdCardFontRegistry.h:24-28](../lib/EpdFont/SdCardFontRegistry.h)).

**Hi-res tiers.** `build-sd-fonts.py --scale N`
([:836-843](../lib/EpdFont/scripts/build-sd-fonts.py)):

1. multiplies every size in `sizes:` by `N` ([:625](../lib/EpdFont/scripts/build-sd-fonts.py));
2. writes into `<output>/<Family>/<N>x/` ([:601](../lib/EpdFont/scripts/build-sd-fonts.py));
3. and then **renames each file back to its 1x number**
   ([:604-620](../lib/EpdFont/scripts/build-sd-fonts.py)) — a 3x cut of the
   13 pt slot rasterizes at 39 ppem and is written as `<Family>_39.cpfont`, then
   renamed to `<Family>_13.cpfont`.

Step 3 is the load-bearing one. `SdCardFontManager::hiResCompanionPath` splices
`"<scale>x/"` in front of the **1x basename**
([SdCardFontManager.cpp:32-36](../lib/EpdFont/SdCardFontManager.cpp)), and the
registry does not scan subdirectories at all
([SdCardFontRegistry.cpp:120-123](../lib/EpdFont/SdCardFontRegistry.cpp)) — so a
tier file whose name carries the multiplied size is simply invisible.

A tier is not a property of the format. **Nothing inside a `.cpfont` records
which tier it is**; the file is just a font built at a bigger point size, and
the directory name is the whole of the tier information. See
[docs/render-scale.md](render-scale.md) for how the active scale is chosen.

### 4.4 Style resolution and the fallback chain

`build-sd-fonts.py` resolves styles in two passes
([:657-707](../lib/EpdFont/scripts/build-sd-fonts.py)): real sources first
(`path` / `zip`+`member` / `url`, then `variable:` instancing, then `scale:`,
then `drop_codepoints`, then `metrics`), and only then `from:` aliases, which
copy an already-resolved path and attach a `synthetic:` spec. If more than one
style resolves, the converter is invoked in **multi-style mode**
(`--regular/--bold/--italic/--bolditalic`), producing one v4 file with several
TOC entries ([:715-729](../lib/EpdFont/scripts/build-sd-fonts.py)).

**The fallback chain is what actually determines coverage.** Per family
([build-sd-fonts.py:190-213](../lib/EpdFont/scripts/build-sd-fonts.py)):

| Family's intervals | Chain, in order |
|---|---|
| Latin-only | Libre Franklin (committed in-repo) |
| anything else | **TeX Gyre Schola → Noto Sans → Noto Sans Math → Noto Sans Symbols 2** |

joined with `os.pathsep` and passed as one `--fallback-<style>` value; the
converter splits it and tries each face in order
([fontconvert_sdcard.py:808-814, 866-882](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
The chain has to be a chain because no single face covers what `reading` asks
for — Noto Sans carries **0 of the 128 arrows**, and Noto Sans Math carries
112. Full measurement:
[docs/font-fallback-chain.md](font-fallback-chain.md).

Fallback glyphs are rasterized with the *same* synthetic treatment as the
primary face, and at the same ppem
([fontconvert_sdcard.py:875-881](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
**Kerning is read from the primary face only** — a fallback face contributes
glyphs but never kern pairs
([fontconvert_sdcard.py:1196](../lib/EpdFont/scripts/fontconvert_sdcard.py)).

### 4.5 What is read from the outline, and what is computed

**Rasterizing** ([fontconvert_sdcard.py:773-1190](../lib/EpdFont/scripts/fontconvert_sdcard.py)):

* `face.set_char_size(size << 6, size << 6, 150, 150)` — **150 DPI**, so
  `ppem = size * 150 / 72`.
* Glyph lookup: the face's `cmap` first, then unencoded ligature glyphs by
  index, then each fallback face in turn.
* FreeType's bitmap buffer is walked by `(row, col)` using the **real, possibly
  negative, `pitch`** rather than assumed to be `pitch == width` top-down
  ([:958-974](../lib/EpdFont/scripts/fontconvert_sdcard.py)) — a comment records
  that the linear-walk assumption corrupts padded and flipped bitmaps.
* `advanceX = fp4_from_ft16_16(linearHoriAdvance + synth_x*1024 + spacing)`,
  i.e. `(v + 2048) >> 12` from FreeType 16.16 to 12.4 with rounding
  ([:171-173, 1148, 1153](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
  `linearHoriAdvance` is the **unhinted linear** advance, so the advance does
  not inherit the hinter's per-size integer rounding.
* `left = bitmap_left`, `top = bitmap_top + pad_top` (the overprint may add
  rows above the glyph, and `top` moves with them).

**Per-style metrics** ([:1164-1189](../lib/EpdFont/scripts/fontconvert_sdcard.py)):

| Field | Computed as |
|---|---|
| `advanceY` | `ceil(face.size.height / 64)`, then optionally `* line_height_scale` or replaced by `line_height` |
| `ascender` | `ceil(face.size.ascender / 64)` |
| `descender` | `floor(face.size.descender / 64)` — negative |

Note the `load_glyph(ord('|'))` immediately before
([:1165](../lib/EpdFont/scripts/fontconvert_sdcard.py)) — a heuristic inherited
from `fontconvert.py` to make `face.size` current.

### 4.6 Kerning: extraction and class derivation

**Extraction** — `extract_kerning_fonttools`
([:330-437](../lib/EpdFont/scripts/fontconvert_sdcard.py)):

1. Build `glyph name → [codepoints]`, restricted to the codepoints actually
   rasterized. A glyph not reachable from a requested codepoint is invisible.
2. Read the **legacy `kern` table**, all subtables, accumulating with `+=`.
3. Read **GPOS, feature tag `kern` only**, into a *separate* dict. Extension
   lookups (type 9) are unwrapped and the effective type checked; only
   **PairPos (type 2)** is handled, in **both format 1 (individual pairs) and
   format 2 (class-based)**
   ([:275-327](../lib/EpdFont/scripts/fontconvert_sdcard.py)). Format 2 iterates
   by class, not by glyph, to avoid O(glyphs²) on CJK. Only `Value1.XAdvance`
   is read; `Value2` is ignored.
4. **Overlay, not sum**: `raw_kern.update(gpos_kern)` — GPOS supersedes legacy
   pair for pair, and legacy-only pairs survive
   ([:418-424](../lib/EpdFont/scripts/fontconvert_sdcard.py)). The comment at
   [:353-368](../lib/EpdFont/scripts/fontconvert_sdcard.py) records why summing
   is wrong: LibrisADF ships byte-identical pair sets in both tables, and
   summing took `T`+`o` from −107 to −214 per 1000 em and **saturated the int8
   floor at 18 pt**.
5. Scale to 4.4: `round(designUnits * ppem / unitsPerEm * 16)`, clamped to
   `[-128, 127]` ([:175-185](../lib/EpdFont/scripts/fontconvert_sdcard.py)).
   Pairs that round to **0 are dropped**.
6. SMP pairs (either side > U+FFFF) are dropped before class derivation,
   because the on-disk codepoint field is uint16
   ([:1197-1200](../lib/EpdFont/scripts/fontconvert_sdcard.py)).

**Class derivation** — `derive_kern_classes`
([:440-504](../lib/EpdFont/scripts/fontconvert_sdcard.py)):

* Build a dense pair matrix over all left and all right codepoints seen, with
  absent pairs as 0.
* **Left classes group codepoints with an identical ROW; right classes group
  codepoints with an identical COLUMN.** The two groupings are independent and
  both derived from the raw pair map — there is no iteration to a fixed point.
* Class IDs are assigned **1-based, in ascending codepoint order**. **0 is
  never assigned**; it is the reserved "no class, no kerning" sentinel.
* The matrix is filled at `(leftClass - 1) * rightClassCount + (rightClass - 1)`
  — row-major, left class is the row.
* **Overflow: if either side needs more than 255 classes, kerning is dropped
  wholesale for that style**, with a warning, returning empty tables
  ([:485-490](../lib/EpdFont/scripts/fontconvert_sdcard.py)). (The built-in
  `fontconvert.py` only warns and keeps going —
  [fontconvert.py:912-915](../lib/EpdFont/scripts/fontconvert.py) — which is a
  genuine behavioral divergence between the two generators.)
* Both class tables are emitted sorted by codepoint, because the device binary
  searches them.

**`optical_kern.py` is a separate, manually-run tool.** It synthesizes kern
pairs for faces whose own kerning is sparse, by rasterizing a character set at
96 ppem, measuring ink profiles and side-shape categories, and writing a fresh
**legacy `kern` table** into a copy of the source font
([optical_kern.py:428-439](../lib/EpdFont/scripts/optical_kern.py)). Its usage
line is `python3 optical_kern.py IN.ttf OUT.ttf`
([:15-16](../lib/EpdFont/scripts/optical_kern.py)). **Nothing invokes it from
the build**: there is no `optical_kern` key in `sd-fonts.yaml` and no call site
in `build-sd-fonts.py`. Its output reaches a `.cpfont` only if someone runs it
by hand and points a `path:` at the patched file. It never overwrites a pair
the designer already ruled on ([:397-400](../lib/EpdFont/scripts/optical_kern.py)),
which is what makes it safe to overlay under GPOS. Defaults: `--reach 1.6`,
`--deadband 0.015`, `--min-bucket 20`, `--minclear 0.020`, `--max-capped 0.05`,
cap = the designer's own p95 |kern| when at least 20 designer pairs exist, else
0.06 em ([:249-258, 443-459](../lib/EpdFont/scripts/optical_kern.py)).

### 4.7 Ligatures: extraction and the presentation-form filter

`extract_ligatures_fonttools`
([:507-628](../lib/EpdFont/scripts/fontconvert_sdcard.py)) reads **GSUB
features `liga` and `rlig` only**, unwraps Extension lookups (type 7), and
handles **LigatureSubst (type 4) only** — the subtable is duck-typed on
`hasattr(actual, 'ligatures')`. Contextual ligature rules are silently ignored.

The output codepoint is, in order: the ligature glyph's own cmap entry; else
`STANDARD_LIGATURE_MAP[sequence]`
([:187-195](../lib/EpdFont/scripts/fontconvert_sdcard.py): ff→FB00, fi→FB01,
fl→FB02, ffi→FB03, ffl→FB04, ſt→FB05, st→FB06); else the rule is dropped with a
warning.

**The presentation-form filter** is the one rule that changes what a reader
sees, and it is worth quoting
([:198-227](../lib/EpdFont/scripts/fontconvert_sdcard.py)):

> True if substituting this output changes only SHAPE, never the text. … a font
> may put rules in the SAME `liga` feature whose output is a DISTINCT LETTER —
> LibrisADF substitutes oe → oe-ligature and ae → ae-ligature, TeX Gyre Schola
> and Junicode do ij → ij-ligature. Those are correct in French, Latin and
> Dutch and wrong in English, where they turn "does" into a word spelled with a
> letter the author never wrote, and "Beijing" and "hijack" likewise. Nothing
> in this pipeline knows the language of the book, so the safe default is
> shape-only.

The test is `0xFB00 <= lig_cp <= 0xFB06 or 0xE000 <= lig_cp <= 0xF8FF`
([:227](../lib/EpdFont/scripts/fontconvert_sdcard.py)). The PUA half is
deliberate: Edgar ships nine ligatures at U+E000–E008 (fb fh fj fk gy ffb ffh
ffj ffk), and a rule allowing FB00–FB06 alone would silently delete all nine.
That is also why Edgar's `intervals:` line ends with `(0xE000-0xE008)`.

**Chaining.** The table encodes pairs only, so a 3-component ligature is stored
as `(intermediate, last) → result` where `intermediate` is the ligature
codepoint of the prefix
([:611-624](../lib/EpdFont/scripts/fontconvert_sdcard.py)) — `f,f,i → U+FB03`
becomes `(U+FB00, U+0069) → U+FB03`. If the prefix has no ligature of its own,
the rule is dropped with a warning. Only the immediate prefix is consulted, so
a 4-component ligature needs its 3-component prefix to also be a ligature.

**The 255 cap.** `ligaturePairCount` is a uint8, so a face with more than 255
surviving pairs is truncated with a warning
([:1216-1219](../lib/EpdFont/scripts/fontconvert_sdcard.py)). Because the list
is already sorted by packed key, truncation keeps the **255 lowest keys** — the
lowest left-hand codepoints — which is an accident of sort order, not a
priority.

### 4.8 Synthetic styles, spacing, overprint and underline joins

All four are **baked into the bitmaps and advances at build time**; nothing at
runtime knows they happened.

| Feature | YAML keys | What it does |
|---|---|---|
| **Embolden + shear** | `synthetic: {embolden_em, y_ratio, slant_deg}` | `FT_Outline_EmboldenXY` then `FT_Outline_Transform`, in that order — shearing first distorts the stroke stress the embolden then amplifies. The growth is re-centered with `FT_Outline_Translate(-x/2, -y/2)` so bearings stay honest, and the full x-strength is added back to the advance. `y_ratio` defaults to **0.5** because full vertical growth eats x-height and closes counters ([fontconvert_sdcard.py:631-652, 836-864, 1136-1148](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |
| **Tracking / word space** | `tracking_em`, `word_space_em` | Advance-only. Tracking lands on every glyph, the word-space delta only on U+0020 and U+00A0, so the two knobs stay independent. Outlines untouched. Clamped at zero ([:751-770, 1140-1148](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |
| **Double strike (overprint)** | `synthetic: {double_strike_em, double_strike_px, double_strike_dy, double_strike_jitter_px, double_strike_ink_jitter, double_strike_ink_min, double_strike_seed}` | Max-composites a second impression of the glyph, offset, at **8-bit coverage before quantization** — `max()` not `add()`, because a second impression of the same ribbon cannot print darker than the ribbon's own black. **The bitmap grows; the advance deliberately does not**, which is the whole point for a monospace face. Per-glyph jitter is keyed on `zlib.crc32(f"{seed}:{cp}")` rather than `hash()` or `random()`, because two builds of one recipe must be byte-identical ([:655-748, 1046-1088](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |
| **Underline join** | `synthetic: {underline_connect, underline_overlap_px, underline_min_cov}` | For a face that *draws* its underline inside the glyph bbox, extends the rule across the whole cell by **replicating one of the bar's own interior columns** — carrying the real vertical ink profile outward instead of pasting a synthetic bar — and reaches one pixel past the advance on each side so neighboring cells overlap rather than abut. `min_cov` defaults to **0.80**, not 0.85, because a measured 13-column bar reads 11/13 = 0.846 and a 0.85 threshold silently left 13 glyphs unjoined including SPACE, H and z ([:679-700, 976-1041](../lib/EpdFont/scripts/fontconvert_sdcard.py)). |

Details and the measurement history:
[docs/synthetic-font-styles.md](synthetic-font-styles.md).

### 4.9 Sanity gates the generator enforces

| Gate | Where | Consequence |
|---|---|---|
| Any glyph over 255 px on either axis | [fontconvert_sdcard.py:1265-1284](../lib/EpdFont/scripts/fontconvert_sdcard.py) | `ValueError` naming **every** offender with its size — deliberately not one at a time, because each hi-res rebuild costs minutes and a one-at-a-time report turns a 3-codepoint problem into three build cycles. |
| `advanceY > 255` | [:1389-1394](../lib/EpdFont/scripts/fontconvert_sdcard.py) | `sys.exit(1)` — "the font size is too large for this format". |
| More than 255 kern classes on a side | [:485-490](../lib/EpdFont/scripts/fontconvert_sdcard.py) | Kerning silently dropped for that style, with a warning. |
| More than 255 ligature pairs | [:1216-1219](../lib/EpdFont/scripts/fontconvert_sdcard.py) | Truncated to 255, with a warning. |
| Codepoints no face supplies | [:921-933](../lib/EpdFont/scripts/fontconvert_sdcard.py) | Pruned from the intervals; `PRUNED` line printed. |

### 4.10 `fontconvert.py` is a different format

The **built-in**, flash-resident fonts are produced by `fontconvert.py`, which
emits a **C header on stdout** — `<name>Bitmaps[]`, `<name>Glyphs[]`,
`<name>Intervals[]`, optionally `<name>Groups[]`, the kern and ligature arrays,
and a `static const EpdFontData <name> = {...}` aggregate
([fontconvert.py:1288-1375](../lib/EpdFont/scripts/fontconvert.py); callers
redirect, e.g. [convert-builtin-fonts.sh:80-81](../lib/EpdFont/scripts/convert-builtin-fonts.sh)).
**No `.cpfont` is involved.** The differences that matter to a `.cpfont`
implementer:

| | `fontconvert.py` (built-in) | `fontconvert_sdcard.py` (`.cpfont`) |
|---|---|---|
| Output | C header, compiled into flash | Binary file on the SD card |
| Bit depth | 1-bit or 2-bit (`--2bit`) | **Always 2-bit** (`flags = 1`) |
| Styles | One style per header | Up to 4 per file, with a TOC |
| Bitmap compression | `--compress`: DEFLATE groups by Unicode block, `EpdFontGroup` + `FontDecompressor` | **None.** Bitmaps are raw. |
| Kern class overflow | Warns, continues | Drops kerning for the style |
| Ligature filters | No presentation-form filter, no 16-bit filter, no 255 cap | All three |
| Extras | `--pnum` (proportional numerals), `--narrow-punct` | Neither |

The `"builtin"` interval preset in `fontconvert_sdcard.py` exists to reproduce
the built-in converter's hardcoded interval list exactly
([fontconvert_sdcard.py:75-81](../lib/EpdFont/scripts/fontconvert_sdcard.py)).

### 4.11 What the build does *not* emit

**No manifest.** `build-sd-fonts.py` writes `.cpfont` files and nothing else;
the `fonts.json` manifest and the on-device downloader that read it were
removed on 2026-08-08 and 2026-08-10
([build-sd-fonts.py:8-10](../lib/EpdFont/scripts/build-sd-fonts.py)).
`FONTS_MANIFEST_VERSION = 1` survives in
[cpfont_version.py:13](../lib/EpdFont/scripts/cpfont_version.py) but its only
remaining consumer interpolates it into a GitHub release **tag string**
(`.github/workflows/release-fonts.yml`). The `sd-fonts.yaml:4,10` comments
describing `description:` as "shown in download UI and manifest" are stale.

---

## 5. What is dynamic and what is fixed

This is the question the format's whole shape answers, so it is worth being
blunt about it: **a `.cpfont` is a photograph, not a description.** Almost
everything about how type looks is decided by the person who ran the build, and
the device chooses only *which* photograph to look at.

### 5.1 Frozen at build time — changing any of these means rebuilding the file

| Property | Fixed by |
|---|---|
| **Which codepoints exist at all** | `intervals:` ∩ (primary face ∪ fallback chain). A codepoint neither supplies is pruned and there is no runtime recovery — the renderer has no reading-font fallback (B-035). |
| **Every glyph's pixels**, including which of the four levels each pixel gets | FreeType rasterization + the 192/128/64 thresholds |
| **Point size / ppem**, and therefore stem weights, x-height in pixels, hinting decisions | `sizes:` × `--scale` |
| **Hinting mode** | `force_autohint:` |
| **`advanceX` per glyph** | `linearHoriAdvance` + synthetic embolden + tracking + word space |
| **`left` / `top` per glyph** | FreeType bearings, adjusted by the overprint and the underline join |
| **`advanceY`, `ascender`, `descender`** — the leading | face metrics, `metrics:` override, `line_height_scale` |
| **Every kern value, and the class partition** | GPOS/`kern` at build ppem, quantized to 4.4 |
| **Which ligatures exist**, and their outputs | GSUB `liga`/`rlig`, filtered to presentation forms |
| **Faux bold weight, faux italic slant** | `synthetic: {embolden_em, y_ratio, slant_deg}` |
| **Typewriter overprint, underline joins** | `synthetic:` double-strike / underline keys |
| **Which face supplied each glyph** (primary vs each fallback) | The chain order at build time. Not recorded anywhere in the file. |

Note what this implies: **the device cannot change the size of text without
loading a different file**, cannot letterspace, cannot synthesize a bold, and
cannot render a codepoint the build pruned. There is no scaling path except the
50% SUP/SUB box filter (§3.5), which is deliberately crude.

### 5.2 Chosen at runtime

| Choice | Mechanism | Granularity |
|---|---|---|
| **Which family** | `SdCardFontManager::loadFamily` — unloads everything, loads one file ([SdCardFontManager.cpp:148-167](../lib/EpdFont/SdCardFontManager.cpp)) | per family |
| **Which size** | Size *slot*, snapped to what the family actually ships (`findClosestReaderSize`, clamped to `size()-1`) | per file |
| **Which hi-res tier** | `cp::renderScale()` → `<Family>/<N>x/<same basename>`, and **only when a companion exists at that exact filename** — otherwise the page renders 1x-replicated, with one log line ([SdCardFontManager.cpp:32-36, 105-143](../lib/EpdFont/SdCardFontManager.cpp)) | per file, per size slot |
| **Which style** | Bits 0–1 of `EpdFontFamily::Style`, with a fallback ladder for absent styles | per run of text |
| **Underline / strikethrough** | `Style` bits 2–3, drawn by the renderer as rules — **not** a font property | per run |
| **Superscript / subscript** | `Style` bits 4–5, the 50% box filter | per run |
| **Light or dark polarity, and AA strength** | `GlyphAa::planes(strength, darkModeAa)` — which of the four levels reach the base pass and which flag a gray plane | per render pass |
| **Text orientation** | `TextRotation::Normal / Rotated90CW / Rotated90CCW`, a template parameter on the same decode | per draw |
| **Whether a size-matched CJK fallback font is loaded** | `loadFamilyExtraSize` at UI sizes 8/10/12 | additive |

### 5.3 Per-family / per-size / per-style: which is which

| Property | Scope |
|---|---|
| `is2Bit` | **Per file** (one flags bit, copied to every style) |
| `intervalCount`, `glyphCount`, `advanceY`, `ascender`, `descender` | **Per style** — each TOC entry carries its own |
| Kern tables and matrix | **Per style** |
| Ligature table | **Per style** |
| Glyph bitmaps | **Per style** |
| Point size | **Per file** (it is the filename) |
| Family name | **Per directory** |
| Render tier | **Per directory** (`<N>x/`) |

An italic can therefore have a different `advanceY` from the regular in the
same file, a different ligature set, and a completely different kern class
partition. In the worked example all four Almendra styles share `advanceY=40`,
`ascender=30`, `descender=−11`, but their kern class counts are 10×6, 8×8, 10×5
and 8×4 respectively — four genuinely different matrices.

### 5.4 "Kern and ligature application are runtime table lookups" — what that
actually buys

Both are **data-driven lookups against tables in the file**, so the *code* is
fixed and the *behavior* travels with the font. Concretely:

* **What is changeable without a firmware change:** every kern value, the class
  partition, which pairs kern at all, which ligatures exist and what they
  substitute to. Ship a rebuilt `.cpfont` and the device's typography changes.
* **What is NOT changeable at runtime, at commit `aa144fd77`:** whether
  kerning is applied (it always is, if a matrix is present), the *rounding*
  rule, the CJK exclusion, the greedy ligature chaining, and the substitution
  set. `applyLigatures` runs unconditionally whenever `ligaturePairs` is
  non-null ([EpdFont.cpp:142-145](../lib/EpdFont/EpdFont.cpp)).

  > **Possibly already stale.** A `lib/EpdFont/LigatureControl.*` module and a
  > `TypographySettingsActivity` were in the working tree, uncommitted, while
  > this document was being written. If they have since landed, a runtime
  > ligature switch exists and this bullet is wrong about ligatures — but
  > **only about the runtime gate**. Nothing such a switch could do changes the
  > file: the pair table is still what it is, and turning substitution off can
  > only *stop* using it. Re-read `EpdFont::applyLigatures` before relying on
  > this paragraph.
* **A ligature can vanish for a RAM reason.** `ligaturePairs` is only populated
  by `loadStyleKernLigatureData`, which runs on the first *full* (non-metadata)
  prewarm; a metadata-only prewarm skips it deliberately
  ([SdCardFont.cpp:1270-1282](../lib/EpdFont/SdCardFont.cpp)), and
  `releaseResidentCaches` frees it under heap pressure
  ([:215-224](../lib/EpdFont/SdCardFont.cpp)). Until it reloads, renders simply
  see no ligatures — stated in the comment at
  [:166-173](../lib/EpdFont/SdCardFont.cpp).
* **A kern can vanish for a RAM reason too, and this one is by design.** The
  full matrix is never resident (see §6.5). At *draw* time the resident matrix
  is a per-page mini matrix covering only this page's codepoints; at *measure*
  time it is a set of full rows for the classes the advance table reached, and
  `getMeasureKern` **returns 0 for any pair whose row is not resident**
  ([SdCardFont.h:65-73](../lib/EpdFont/SdCardFont.h)). So "the kern is in the
  file" and "the kern was applied to this line" are different statements.

---

## 6. The runtime reader

**The shape of it.** On the device the entire font engine is four objects:
`SdCardFontRegistry` finds files, `SdCardFontManager` loads one and registers
it with the renderer, `SdCardFont` owns the file handle and the RAM budget, and
`EpdFont` is a thin lookup view over an `EpdFontData` that `SdCardFont` swaps
underneath it. The design pressure behind all of it is a single number: an
ESP32-C3 with about 380 KB of usable heap, of which a page's framebuffer and
the EPUB layout already claim most.

### 6.1 Discovery

Two roots are scanned, both, every boot:
`/.fonts` then `/fonts`
([SdCardFontRegistry.h:35-39](../lib/EpdFont/SdCardFontRegistry.h),
[.cpp:215-216](../lib/EpdFont/SdCardFontRegistry.cpp)). Layout is
`/<root>/<Family>/<Family>_<size>.cpfont`, and **the family name is the
directory name verbatim** — nothing parses a family out of the filename
([SdCardFontRegistry.cpp:194](../lib/EpdFont/SdCardFontRegistry.cpp)).

`parseFilename` ([.cpp:76-110](../lib/EpdFont/SdCardFontRegistry.cpp)) is
strict, and each rule earns itself:

1. The name must **end** with `.cpfont` — compared on the tail with `strcmp`,
   not `strstr`, so `Foo_14.cpfont.tmp` and `Foo_14.cpfont~` are rejected.
2. The stem must be at most 127 characters.
3. There must be an underscore, and it must not be the first character.
4. Everything after the last underscore must parse as a decimal integer with
   **nothing trailing**, in `1 … 255`.
5. `style` is set to **0 always** — v4 bundles all four styles in one file, and
   the field is reserved.

Skips: any entry beginning `.` or `_` (macOS `._` resource forks), and **any
subdirectory** — which is exactly what makes the `<N>x/` hi-res tiers invisible
to discovery, by design
([.cpp:120-123, 129, 181](../lib/EpdFont/SdCardFontRegistry.cpp)). Duplicate
`(size, style)` within a family logs an error and is skipped; duplicate family
names **across** the two roots resolve to whichever was scanned first, i.e.
`/.fonts` wins. Families are sorted alphabetically and hard-truncated to
`MAX_SD_FAMILIES = 128`.

### 6.2 Opening a file: what `load()` reads into RAM

`SdCardFont::load` ([SdCardFont.cpp:708-921](../lib/EpdFont/SdCardFont.cpp))
does surprisingly little:

1. Reads the 32-byte header and validates magic, version and style count.
2. Reads each 32-byte TOC entry, validates the counts, and calls
   `computeStyleFileOffsets` to record **seven file offsets per style**.
3. Walks each style's interval table twice — once to validate, once to load
   into either `BmpInterval16[]` (6 bytes) or `EpdUnicodeInterval[]` (12).
4. Zeros a `stubData` and fills in only `advanceY`, `ascender`, `descender`,
   `is2Bit`, then installs `glyphMissHandler` and `coverageHandler`.

**That is the entire resident cost of an open font**: the interval table and
four small structs. No glyph, no bitmap, no kern matrix, no ligature table is
read. Everything else is faulted in later. That is the format's design paying
off — because every section's offset is derivable from the TOC, the reader can
defer every section independently.

`contentHash_` is computed during this walk: **FNV-1a over the 32-byte header
and every 32-byte TOC entry, and nothing else**
([SdCardFont.cpp:24-33, 742, 762](../lib/EpdFont/SdCardFont.cpp)). It becomes
the seed for `SdCardFontManager::computeFontId(contentHash, familyName,
pointSize)` ([SdCardFontManager.cpp:52-63](../lib/EpdFont/SdCardFontManager.cpp)),
which is what invalidates the section cache when a font changes. A collision
with an already-registered id makes `loadFile` **refuse the font**
([:81-85](../lib/EpdFont/SdCardFontManager.cpp)).

### 6.3 Prewarm — the normal path, and why it exists

Rendering a page from the card one glyph at a time would be an SD seek per
glyph. Instead the renderer runs the page **twice**: a `PrewarmScope` puts
`GfxRenderer::drawText` into scan mode, where it records text instead of
drawing ([GfxRenderer.cpp:750-753](../lib/GfxRenderer/GfxRenderer.cpp),
[FontCacheManager.cpp:102-157](../lib/GfxRenderer/FontCacheManager.cpp)), then
`SdCardFont::prewarm` pulls exactly that page's glyphs into a small arena and
the real draw runs entirely from RAM.

`prewarm(utf8Text, styleMask = 0x0F, metadataOnly = false)`
([SdCardFont.cpp:946-1048](../lib/EpdFont/SdCardFont.cpp)):

1. Resolve the requested style mask onto styles that actually exist.
2. Collect unique codepoints into a **heap** buffer of `MAX_PAGE_GLYPHS = 512`
   (2048 bytes), with an O(n²) linear dedupe — worst case ~131 K comparisons,
   which the comment says is cheaper than the alternative on this device.
3. **Always inject U+FFFD** if there is room.
4. Unless `metadataOnly`, load this style's kern/ligature tables and **inject
   every ligature output whose two inputs are both on the page** — otherwise
   `fi` would ligate to a glyph that was never warmed.
5. **Sort the codepoints.** This is a precondition for everything after it.
6. `prewarmStyle` per style.

`prewarmStyle` ([:1050-1312](../lib/EpdFont/SdCardFont.cpp)) is where the file
layout is exploited:

* **Subset fast path.** Mini data survives across scopes, so if the previous
  scope — typically the idle prewarm of this very page — already covered every
  requested codepoint, this returns with **zero SD reads**.
* **Pass 1, glyph metadata.** A permutation sorted by *global glyph index*,
  then a seek only when the index is not `lastReadIndex + 1`. Because the glyph
  table is a flat array in codepoint order and the codepoints are sorted,
  **the number of seeks is the number of contiguous runs, not the number of
  glyphs.** `lastReadIndex` starts at `INT32_MIN` specifically so glyph 0 still
  seeks.
* **Pass 2, bitmaps.** Skipped entirely when `metadataOnly`. The same
  permutation **re-sorted by `dataOffset`**, coalescing on
  `fileOff == lastBitmapEnd`. Since the writer lays bitmaps out in glyph order,
  a sorted codepoint set walks the bitmap section strictly forward.
* Each mini glyph's `dataOffset` is **rewritten in place** to index the mini
  bitmap arena ([:1261](../lib/EpdFont/SdCardFont.cpp)), so the same
  `EpdGlyph` struct serves as an in-RAM font with no second representation.
* Finally `miniData` is memset, populated, and `epdFont.data` swapped to it.

The forward-only, sorted access pattern is also **why the CPZ1 container in §7
can hold exactly one inflated block**.

**Buffer reuse is deliberate and is a heap-fragmentation fix, not a speed
one.** `ensureArrayCapacity` only ever grows
([SdCardFont.cpp:86-93](../lib/EpdFont/SdCardFont.cpp)); freeing and
reallocating slightly different sizes every page turn punched non-coalescing
holes and eroded the largest contiguous block all session. Retention is bounded
two ways in `resetStyleMiniData` ([:120-151](../lib/EpdFont/SdCardFont.cpp)): a
hard free below **40,960 bytes** of free heap, and an underuse hysteresis that
frees after **3** consecutive rebuilds using less than ¾ of the bitmap arena.

### 6.4 The overflow ring — the safety net

A string that was never prewarmed (a UI label, a filename, a Claude reply)
still has to draw. `EpdFont::getGlyph` calls `glyphMissHandler` when the
resident interval table misses ([EpdFont.cpp:184-187](../lib/EpdFont/EpdFont.cpp)),
and `SdCardFont::onGlyphMiss`
([SdCardFont.cpp:1654-1755](../lib/EpdFont/SdCardFont.cpp)) loads that one
glyph from the card.

| Property | Value |
|---|---|
| Capacity | **16** entries (`OVERFLOW_CAPACITY`, [SdCardFont.h:312](../lib/EpdFont/SdCardFont.h)) |
| Eviction | **LRU**, not round-robin |
| In-object cost | 32 bytes/slot ⇒ **512 bytes**, plus one heap bitmap per occupied slot (~65 B at 12 pt, ~144 B at 18 pt) |
| Cost per miss | **1 file open + 2 seeks + 2 reads** (1 + 1 for a zero-bitmap glyph) |
| Log throttle | first 16 loads individually, then 1-in-256 |

LRU is not a preference: with round-robin, a working set one glyph larger than
the cache degenerated to a miss on **every** lookup, because the slot reused
was always the one needed next, and a single un-prewarmed UI string re-read the
same handful of letters hundreds of times per render
([SdCardFont.h:299-304](../lib/EpdFont/SdCardFont.h)). Capacity 16 is sized for
a filename title (~10–14 unique glyphs plus an ellipsis). `overflowLoadsSinceClear()`
is the diagnostic: a healthy prewarmed render leaves it at **zero**, and a
number climbing into the thousands means the drawn working set exceeds 16 and
the ring is thrashing.

**Overflow glyphs do not live in the mini bitmap arena.** Their `dataOffset` is
*not* rewritten; `getOverflowBitmap` hands back a standalone heap buffer, and
`GfxRenderer::getGlyphBitmap` checks `isOverflowGlyph` before falling through
to `&fontData->bitmap[glyph->dataOffset]`
([GfxRenderer.cpp:79-89](../lib/GfxRenderer/GfxRenderer.cpp)). A `nullptr`
return from `getOverflowBitmap` is legitimate — it means a zero-width glyph.

### 6.5 Why the kern matrix is never resident, and the two things that replace it

This is the clearest place the format's design shows its cost. A single style
of a Literata-class face has a kern matrix around **36–42 KB contiguous**, and
four styles' worth will not fit beside the bitmaps and the framebuffer on a
380 KB device ([SdCardFont.h:184-188](../lib/EpdFont/SdCardFont.h)). Only the
two small class tables (~3 KB each) stay resident. The matrix is reconstructed
twice, differently, for the two things that need it:

**(a) Draw time — `buildMiniKernMatrix`**
([SdCardFont.cpp:376-546](../lib/EpdFont/SdCardFont.cpp)). Marks the classes
this page's codepoints reach, **renumbers them 1..N preserving order**, and
reads **one full matrix row per used left class**, selecting the used columns
in RAM. A typical Latin page yields a ~25×25 matrix — about **625 bytes per
style against ~36 KB**, a ~50× reduction. Exact I/O: 1 open, `numLeft` seeks,
`numLeft` reads. The 1,536 bytes of scratch it needs are **heap, not stack**:
six 256-byte stack arrays made a 1,648-byte frame against a 256-byte budget and
blew the render task's 8,192-byte stack
([:396-421](../lib/EpdFont/SdCardFont.cpp)).

**(b) Measure time — `loadMeasureKernRows`**
([:560-646](../lib/EpdFont/SdCardFont.cpp)). Layout runs before any page is
prewarmed, so the mini matrix is absent or stale from an earlier page. Instead,
full rows for the left classes the advance table reached are loaded beside the
advance table, **with original class IDs and no renumbering**, so a row loaded
once serves every later paragraph. Bounded by `MEASURE_KERN_ARENA_LIMIT =
16,384` bytes; the richest shipped face (Edgar) needs 36 × 48 = 1.7 KB per
style. `getMeasureKern` returns **0 for any pair whose row is not resident** —
that is the pre-2026-08-22 behavior, deliberately preserved as the fallback.

`getMeasureKern` also carries a 256-byte-per-style ASCII shortcut
(`kernClassAscii`: 128 left bytes then 128 right) because layout binary-searches
both class tables for **every adjacent pair it measures — about 2.5 million
pairs on a novel, and 28% of the pagination build's CPU**
([SdCardFont.h:194-204](../lib/EpdFont/SdCardFont.h)).

Ligatures, by contrast, are **small enough to stay resident whole** (typically
under 1 KB), so `applyKernLigaturePointers` points the mini font at the full
ligature table while pointing all seven kern fields at the mini ones
([SdCardFont.cpp:251-264](../lib/EpdFont/SdCardFont.cpp)).

### 6.6 The RAM budget, in one table

| Resident structure | Size | Lifetime |
|---|---|---|
| Interval table (compact form) | `intervalCount × 6` bytes per style | Whole font lifetime — survives `releaseResidentCaches` so `hasCodepoint` keeps working |
| Interval table (wide form) | `intervalCount × 12` bytes per style | Only when the table does not fit 16-bit fields |
| Kern class tables | `(kernLeft + kernRight) × 3` bytes per style, ~3 KB each on a rich face | First full prewarm → `freeStyleKernLigatureData` |
| `kernClassAscii` | 256 bytes per style | With the class tables |
| Ligature table | `ligaturePairCount × 8` bytes, < 1 KB | With the class tables |
| Mini glyph arena | one page's glyphs × 16 bytes | Across scopes, subject to the heap floor and the hysteresis |
| Mini bitmap arena | Σ `dataLength` for one page | Same |
| Mini kern matrix | `numLeft × numRight`, ~625 B/style | Rebuilt per full prewarm |
| Measure kern rows | ≤ 16,384 B/style, ~1.7 KB in practice | `clearPersistentCache` |
| Advance table | ≤ `ADVANCE_CACHE_LIMIT = 768` entries × 6 B per style | `clearPersistentCache` |
| Overflow ring | 512 B + one bitmap per occupied slot | `clearOverflow` |

Not resident, ever: the glyph table, the bitmap section, and the kern matrix.

### 6.7 The missing-glyph ladder

`EpdFont::getGlyph` substitutes rather than failing, in a fixed order
([EpdFont.cpp:160-221](../lib/EpdFont/EpdFont.cpp)): interval table →
`glyphMissHandler` (the overflow ring) → **U+FFFD** → **`'?'`** → `nullptr`.
Both substitutes are excluded from recursing, because `U+FFFD → '?' → U+FFFD`
is a cycle that would overflow the stack on a face carrying neither. Every
substitution is counted in `missingglyphs::current().note(cp)` — a 64-slot
open-addressed set ([MissingGlyphLedger.h](../lib/EpdFont/MissingGlyphLedger.h))
— because `getGlyph` is the one choke point that cannot disagree with what
lands on the page.

The `'?'` step is not cosmetic. `drawText` reads `glyph ? glyph->advanceX : 0`,
so returning `nullptr` did not merely lose the character: the pen did not move,
the rest of the line slid left into the gap, and a width measured before the
draw no longer matched it (B-009).

---

## 7. The CPZ1 compression container

**What it is.** A block-compressed wrapper the **simulator's** file layer opens
transparently, on **iOS only**. It is not part of the `.cpfont` format, the
device never sees it, and the firmware above it never learns it exists.

**Why it exists.** `.cpfont` stores its bitmaps raw, and that is 108,068,437 of
the 117,654,860 bytes the iOS app's 1x+2x seed tree occupies. The IPA's zip
squeezes that to ~34 MB for the *download* and the phone expands it right back
to 118 MB for the *install*. CPZ1 closes exactly that gap: **117,654,860 →
34,837,381 installed (−70.4%), against a download that grows 1,398,702 bytes
(+4.2%)** because a container no longer compresses inside the zip.

**Why at the file layer and not in the format.** Three reasons, and all three
are measurements rather than preferences
([seed-font-compression.md](../../crosspoint-simulator/docs/seed-font-compression.md)):

1. `.cpfont` is **random-access by construction** — `prewarmStyle` seeks per
   glyph run and the overflow ring does an open+seek+read for a single glyph —
   so a whole-file stream would mean inflating 9.6 MB to serve one glyph.
2. Per-block groups **inside** the format would bump `CPFONT_VERSION`, add a
   read path to firmware code the ESP32-C3 also compiles, and require
   re-emitting every published font pack. The ruling was about the iOS app.
3. `HalFile` is the single choke point every firmware read already goes
   through, it is host-only by definition (the device has SdFat), and **the
   filename survives** — `SdCardFontRegistry` still finds `Edgar_14.cpfont`,
   `SdCardFontManager` still finds `<family>/2x/<same basename>`, and WebDAV
   still serves the real bytes at the real length, because `size()` answers the
   *payload's* length.

### 7.1 The container layout

Writer: [crosspoint-simulator/tools/compress_seed_fonts.py](../../crosspoint-simulator/tools/compress_seed_fonts.py).
Reader: [crosspoint-simulator/src/SimCompressedFile.h](../../crosspoint-simulator/src/SimCompressedFile.h).

| Offset | Size | Type | Field |
|---:|---:|---|---|
| 0 | 4 | bytes | magic `"CPZ1"` (`43 50 5A 31`) |
| 4 | 4 | uint32 LE | `blockSize` — **uncompressed** bytes per block |
| 8 | 8 | uint64 LE | `originalSize` — the payload's length |
| 16 | 4 | uint32 LE | `blockCount` |
| 20 | 4 | — | reserved (0) |
| 24 | `4 × blockCount` | uint32 LE[] | `blockEnd[]` — **cumulative** compressed end offsets |
| `24 + 4×blockCount` | … | — | raw-DEFLATE blocks, back to back |

Block *i* occupies compressed bytes `[blockEnd[i-1], blockEnd[i])` (with
`blockEnd[-1]` taken as 0) measured from `dataStart = 24 + 4 × blockCount`, and
inflates to `min(blockSize, originalSize - i × blockSize)` bytes.

Blocks are **raw DEFLATE with no zlib header** (`zlib.compressobj(level,
DEFLATED, -15)`) at level 9 by default, because the index already carries every
length. Shipped `blockSize` is **32,768**, chosen on measurement over the whole
tree — 8 KiB gives 41.9 MB, 16 KiB 40.7 MB, 32 KiB 39.8 MB, and past that the
curve is flat while the per-access inflate cost keeps rising. It is a **header
field, not a constant on both sides**, so changing the writer's block size does
not strand containers already written; both sides bound it to
**[1024, 1048576]**.

### 7.2 How random access survives

`SimCpzFile::read(off, dst, count)` maps a logical offset to
`index = off / blockSize`, inflates that block if it is not the cached one, and
`memcpy`s from `within = off % blockSize`
([SimCompressedFile.h:168-186](../../crosspoint-simulator/src/SimCompressedFile.h)).

**One inflated block of cache is enough**, and the reason is a property of the
`.cpfont` reader described in §6.3: `prewarmStyle` sorts its glyph reads by
file offset before issuing them, so a page walks blocks strictly forward and
touches each once. The pathological case — the overflow ring asking for one
glyph at a time from scattered offsets — costs one 32 KB inflate per glyph,
which is the same order as the SD read it replaces.

Inflation runs in **one-shot mode**: the destination holds the whole block, so
back-references resolve inside it and no 32 KB sliding window is allocated.
`init()` is called per block to reset the stream while reusing the
decompressor's state.

### 7.3 Failure modes, and why they are loud

**A font that fails to load is a blank page with successful renders in the
log.** So nothing here is allowed to degrade quietly
([SimCompressedFile.h:27-32](../../crosspoint-simulator/src/SimCompressedFile.h)):

| Condition | Behavior |
|---|---|
| Header short read | **Open fails**, `[SIM] cpz <path>: header short read` |
| Magic present but header inconsistent | **Open fails** — it does **not** fall back to handing the caller container bytes as if they were font bytes ([HalStorage.cpp:199-206](../../crosspoint-simulator/src/HalStorage.cpp)) |
| Block index short read, or non-monotonic | **Open fails** |
| A block will not inflate | `read()` returns **−1**, never a short read — a short read looks like end of file to `SdCardFont` |

Two header checks deserve naming because they are hostile-input defenses, not
tidiness:

* **`blockCount` is derived, not trusted.** It must equal
  `ceil(originalSize / blockSize)` exactly.
* **The ceiling divide is checked for overflow first.** A crafted header can
  aim the wrap at zero — `0xFFFF...FFFF` with a 1024-byte block wraps to 1022,
  giving `expected = 0`, matching a declared `blockCount` of 0. The header then
  parsed, `open()` succeeded with an empty block index, and the first read
  indexed it: **a segfault on file content, reachable from any read-only open
  of any file on the card, not only a font.** Found by adversarial review
  2026-08-23 and reproduced under ASan from a 24-byte file
  ([SimCompressedFile.h:87-100](../../crosspoint-simulator/src/SimCompressedFile.h)).

The sniff is **read-only opens only** — a writer that found the magic and then
wrote through the decoder would corrupt the file
([HalStorage.cpp:190-197](../../crosspoint-simulator/src/HalStorage.cpp)).

### 7.4 Operational notes

* The writer is **idempotent and incremental**: a file already carrying the
  magic is copied through untouched, and an output newer than its input is
  skipped, so the CMake configure step that calls it can run every configure.
* It preserves the directory shape *and* the filenames, because both
  `SdCardFontRegistry` and `SdCardFontManager` find files by exact name.
* Anything that is not a `.cpfont` is carried across verbatim.
* `-DCROSSPOINT_IOS_COMPRESS_SEED_FONTS=OFF` bundles the raw tree, and it still
  works because the reader sniffs per file. That is also what the desktop canary
  exercises by default — nothing compresses `fs_/fonts` unless someone runs the
  tool over it.

---

## 8. Versioning and compatibility

### 8.1 Where the version number lives

**Two copies, and they must be bumped together by hand.**

| Copy | File | Value |
|---|---|---|
| Build tooling (canonical) | [lib/EpdFont/scripts/cpfont_version.py:10](../lib/EpdFont/scripts/cpfont_version.py) | `CPFONT_VERSION = 4` |
| Firmware | [lib/EpdFont/SdCardFont.h:20](../lib/EpdFont/SdCardFont.h) | `#define CPFONT_VERSION 4` |

The firmware copy is a `#define` with **no integer suffix** deliberately: it
used to be stringified into a manifest URL, and `4U` would have stringified as
`"4U"`. That URL is gone (the on-device downloader was removed 2026-08-08/10);
the `#define` is kept only so the tooling's copy can match it
([SdCardFont.cpp:37-40](../lib/EpdFont/SdCardFont.cpp)).

There is also `FONTS_MANIFEST_VERSION = 1`
([cpfont_version.py:13](../lib/EpdFont/scripts/cpfont_version.py)), which is
**dead with respect to the format** — its only consumer builds a GitHub release
tag.

### 8.2 How a reader detects a version it cannot parse

```cpp
uint16_t fileVersion = readU16(headerBuf + 8);
if (fileVersion != CPFONT_VERSION) {
  LOG_ERR("SDCF", "Unsupported version: %u (expected %u)", fileVersion, CPFONT_VERSION);
  return false;
}
```

([SdCardFont.cpp:735-739](../lib/EpdFont/SdCardFont.cpp).) Note the comparison
is **exact equality, not `>=` or a range**. A v3 file and a hypothetical v5
file are refused identically. There is no forward compatibility, no minimum
version, and no partial parse.

The failure is a clean `false` from `load()`. `SdCardFontManager::loadFile`
deletes the object and returns 0
([SdCardFontManager.cpp:72-76](../lib/EpdFont/SdCardFontManager.cpp)), so the
family is simply not available; the reader falls back to a built-in font. The
message reaches the log, not the screen.

### 8.3 What changing the format costs

Because `.cpfont` files live on users' SD cards and in a separately published
font repository, a version bump is a **fleet-wide re-emit**:

1. Both `CPFONT_VERSION` copies bump.
2. `SdCardFont.cpp` is **firmware code the ESP32-C3 compiles**, so any new read
   path costs flash and heap on the device — not just on the host.
3. Every published font pack must be regenerated; the
   [crosspoint-fonts repository](https://github.com/crosspoint-reader/crosspoint-fonts)
   and every provisioned card go stale at once.
4. Every surface must be re-provisioned: device cards, the simulator's
   `fs_/fonts/`, and the iOS app's bundled seed set.

Point 4 is not hypothetical. B-035 is a font-*config* fix that was correct on
2026-08-17 and still broken on the device three days later, because **nothing
rebuilt the files** — and a second, narrower version of the same fault left the
2x and 3x tiers on the old cut after 1x had been fixed
([BUGS.md](../BUGS.md), `[B-035]`).

This cost is exactly why the CPZ1 container in §7 sits at the file layer
instead of becoming a `.cpfont` feature, even though per-block compression
*inside* the format was measured to be the technically better fit.

### 8.4 A field-by-field forward-compatibility audit

If v5 ever happens, these are the slots already reserved for it:

| Slot | Size | Currently |
|---|---|---|
| Header `flags` bits 1–15 | 15 bits | Unassigned; the reader masks bit 0 only, so setting them today is silently ignored by a v4 reader — but the version check would refuse the file first anyway |
| Header offsets 13–31 | 19 bytes | Written zero, never read, **hashed into `contentHash`** |
| TOC offsets 1–3 | 3 bytes | Written zero, never read, hashed |
| TOC offsets 28–31 | 4 bytes | Written zero, never read, hashed |
| `EpdGlyph` offsets 10–11 | 2 bytes | Alignment padding; written zero |

Note the consequence of "hashed": any use of a reserved byte changes
`contentHash` and therefore every font id, which invalidates the section cache
— probably what you want, but it is not free.

---

## 9. Gotchas and failure modes

The silent ones first, since those are what cost time.

### 9.1 Silent, and they will produce a plausible-looking wrong page

1. **Row padding.** There is none. A reimplementer who pads each glyph row to a
   byte boundary (which is what the built-in fonts' *group* format does, and
   what most bitmap formats do) will render every glyph after the first row as
   a diagonal smear. See §3.2 and
   [FontDecompressor.cpp:119-141](../lib/EpdFont/FontDecompressor.cpp), which
   exists to convert between the two.
2. **MSB-first within the byte.** Pixel 0 is bits 7–6, not bits 1–0.
3. **Level polarity flips inside the renderer.** On disk 3 is ink; in
   `renderCharImpl` and `GlyphAaPlanes.h` **0** is ink. Reading a mask table in
   the wrong numbering produces text that looks antialiased but is
   antialiased backwards.
4. **The kern matrix row stride is `kernRightClassCount`.** Using
   `kernLeftClassCount` gives a matrix that is square-ish, indexes in bounds,
   and returns garbage kerns for a near-square matrix.
5. **Class IDs are 1-based; index with `-1`.** Class 0 means "no kerning" and
   is never a row.
6. **`dataOffset` is section-relative, not file-relative** — and the runtime
   *rewrites it in place* to be arena-relative
   ([SdCardFont.cpp:1261](../lib/EpdFont/SdCardFont.cpp)). A tool that reads a
   glyph struct out of a live `EpdFontData` and treats its `dataOffset` as a
   file offset is reading the wrong bytes.
7. **`contentHash` covers only the header and the TOC**
   ([SdCardFont.cpp:742, 762](../lib/EpdFont/SdCardFont.cpp)). Two files whose
   headers agree but whose glyph bitmaps differ hash identically — a rebuild
   that changes rasterization without changing any count will reuse the section
   cache. (Only a hazard for a hand-crafted rebuild; a real re-rasterization
   almost always moves a count or an offset.)
8. **`dataLength == 0` is normal.** Space, no-break space and any glyph the
   face could not supply are all zero-length with a real advance. Treating that
   as an error loses word spacing.
9. **The runtime `intervalCount` is not the font's coverage.** After a prewarm,
   `EpdFontData::intervals` is a **per-page subset**. Asking "does this font
   have U+2192" through the interval table gives the wrong answer; that is what
   `coverageHandler` and `hasCodepoint()` are for
   ([EpdFontData.h:210-215](../lib/EpdFont/EpdFontData.h)).
10. **A pruned codepoint is not an error.** The generator drops any codepoint
    no face in the chain supplies and splits the interval around it. This
    shipped six families with no Unicode arrows and the first anyone knew was
    tofu on the device (B-035). Read the `PRUNED` line.
11. **A `.cpfont` fix only reaches a surface when that surface is
    regenerated**, and the hi-res tiers are separate surfaces. B-035's second
    half: 1x was rebuilt and fixed while 2x and 3x stayed on the old glyphless
    cut — and the simulator, which is how the project is actually inspected,
    was the tier that stayed broken.
12. **A missing hi-res companion degrades in near-silence.** The lookup is by
    exact filename and the filename carries the point size, so changing the
    reader's size can move onto a slot with no `2x/` file; the page then renders
    1x-replicated at half the resolution the build asked for, with one log line
    (`No hi-res companion ... renders 1x-replicated`).
13. **Kerning that is in the file may not be applied.** At measure time
    `getMeasureKern` returns 0 for any pair whose matrix row is not resident;
    at draw time the mini matrix covers only the page's codepoints. "The kern
    is in the file" ≠ "the kern was applied."
14. **Measure and draw can disagree about a line's width** after a missing
    glyph — B-036, fixed 2026-08-20 by fetching the glyph *before* the kern and
    severing `prevCp` on a miss ([BUGS.md](../BUGS.md) `[B-036]`;
    [EpdFont.cpp:34-47](../lib/EpdFont/EpdFont.cpp),
    [GfxRenderer.cpp:815-828, 864](../lib/GfxRenderer/GfxRenderer.cpp)). A
    wrapped line measured as exactly fitting could draw a kerned pixel or two
    wider and clip at the margin.
15. **The SD advance-table fast path does not apply ligatures.** Stated in
    source at [GfxRenderer.cpp:2689-2692](../lib/GfxRenderer/GfxRenderer.cpp):
    a pre-existing `fi`/`fl` measure-versus-draw delta, out of scope of the fix
    that noted it. Still live.
16. **`EpdFontFamily::getFont` and `SdCardFont::resolveStyle` use *different*
    fallback ladders.** For a bold-italic request with only bold and italic
    present, the family picks `bold` while `resolveStyle` also picks
    `BOLD_ITALIC → BOLD` — they agree here, but the two tables are written
    independently ([EpdFontFamily.cpp:3-19](../lib/EpdFont/EpdFontFamily.cpp)
    vs [SdCardFont.cpp:1623-1640](../lib/EpdFont/SdCardFont.cpp)) and a change
    to one will not track the other.
17. **The ligature table is only as correct as the source font's cmap.**
    Measured on the shipped `Almendra_14.cpfont` for this document: it carries
    `f`+`h` → **U+FB00 (ff)**. That is faithful extraction, not a pipeline bug —
    Almendra's own cmap maps U+FB00 to a glyph named `f_h`, so
    `glyph_to_cp['f_h'] = 0xFB00` and the rule is recorded exactly as the font
    states it. The visible consequence is that typing "fh" in Almendra renders
    the ff ligature, while "ff" — which Almendra has no `f_f` rule for —
    renders unligated. Nothing in the pipeline can detect this; it needs a
    proofing pass against the rendered page. (Verified against
    `lib/EpdFont/scripts/downloaded_fonts/Almendra/H4ckBXKAlMnTn0CskyY6.ttf` with
    fontTools 4.63.0.)

### 9.2 Loud, but easy to trigger

18. **Interval `offset` must be the exact prefix sum.** Not merely consistent —
    exact, starting at 0. A generator that computes it any other way fails the
    load with `Style N: invalid interval layout` and the font vanishes.
19. **Intervals must be strictly ascending and non-overlapping**, and each
    span must fit inside `glyphCount`.
20. **Hard caps.** `width`/`height` ≤ 255 (uint8); `advanceY` ≤ 255;
    `ligaturePairCount` ≤ 255; kern codepoints ≤ U+FFFF; `intervalCount` ≤ 4096;
    `glyphCount` ≤ 65536; kern entry counts ≤ 4096; `styleCount` ∈ [1, 4];
    `styleId` < 4. The 255 px glyph cap is a **tier** problem, not a family
    problem — U+2E3B THREE-EM DASH rasterizes 276×8 at 3x and is dropped there
    while still shipping at 1x and 2x.
21. **More than 255 kern classes drops kerning for the whole style**, with a
    warning and no other symptom.
22. **The 255-ligature truncation keeps the lowest packed keys**, which is sort
    order, not importance.

### 9.3 Structural traps in the reader worth knowing about

23. **`load()` is not idempotent about the file handle.** It calls `freeAll()`
    first, so re-`load()`ing the same object is safe, but `SdCardFont` is
    non-copyable and non-movable by explicit deletion
    ([SdCardFont.h:29-34](../lib/EpdFont/SdCardFont.h)) — it owns raw buffers
    freed in the destructor, and an accidental pass-by-value is made a compile
    error on purpose.
24. **`prewarm` caps at 512 unique codepoints per page.** A page with more
    simply falls through to the overflow ring for the excess, which is a
    16-entry LRU — so a genuinely dense CJK page will thrash. `overflowLoadsSinceClear()`
    is the instrument.
25. **A metadata-only prewarm cannot serve a render.** The subset fast path
    checks for it explicitly; a mini built without bitmaps forces a rebuild.
26. **`buildMiniKernMatrix`'s scratch must stay on the heap.** Six 256-byte
    stack arrays made a 1,648-byte frame against a 256-byte budget and blew the
    render task's 8,192-byte stack.

---

## 10. Worked example: `Almendra_14.cpfont`

*Every number below was read out of the real file at
`~/src/crosspoint-simulator/build/seedfonts/Almendra/Almendra_14.cpfont`
(1,089,413 bytes) on 2026-08-24, by walking the tables described above. Where
this walk and the tables in §2 disagreed, the tables were wrong and were
corrected.*

*Re-read 2026-08-26, after Almendra's metrics span moved 1543 → 1368 with its
new point-size ramp (`docs/almendra-size-match-2026-08-26.md`). Six cells
changed and nothing else did: `advanceY` 45 → **40**, `ascender` 33 → **30**,
`descender` −13 → **−11**, in all four styles, plus the three bytes those
occupy in the TOC hexdump. Everything structural — 61 intervals, 2,676 glyphs,
`dataOffset` 160, four ligature pairs, the four kern matrices, the file's
1,089,413 bytes — is unchanged, because the point size did not move: 14 pt was
slot 4 on the old ramp and is slot 3 on the new one, and only the leading was
retuned.*

### 10.1 The header

```
$ xxd -l 32 Almendra_14.cpfont
00000000: 4350 464f 4e54 0000 0400 0100 0400 0000  CPFONT..........
00000010: 0000 0000 0000 0000 0000 0000 0000 0000  ................
```

| Offset | Bytes | Field | Value |
|---:|---|---|---|
| 0 | `43 50 46 4F 4E 54 00 00` | `magic` | `"CPFONT\0\0"` ✔ |
| 8 | `04 00` | `version` | **4** ✔ |
| 10 | `01 00` | `flags` | `0x0001` → bit 0 set → **2-bit** ✔ |
| 12 | `04` | `styleCount` | **4** — all four styles present |
| 13 | `00 × 19` | reserved | zero |

### 10.2 The style TOC

Four entries, 32 bytes each, at offsets 32 / 64 / 96 / 128. The first, at
offset 32:

```
00000020: 0000 0000 3d00 0000 740a 0000 281e 00f5  ....=...t...(...
00000030: ff0c 0009 000a 0604 a000 0000 0000 0000  ................
```

Field by field:

| Offset in entry | Bytes | Field | Value |
|---:|---|---|---|
| 0 | `00` | `styleId` | **0** (regular) |
| 1 | `00 00 00` | pad | zero |
| 4 | `3D 00 00 00` | `intervalCount` | **61** |
| 8 | `74 0A 00 00` | `glyphCount` | **2676** |
| 12 | `28` | `advanceY` | **40** px |
| 13 | `1E 00` | `ascender` | **30** px |
| 15 | `F5 FF` | `descender` | **−11** px |
| 17 | `0C 00` | `kernLeftEntryCount` | **12** |
| 19 | `09 00` | `kernRightEntryCount` | **9** |
| 21 | `0A` | `kernLeftClassCount` | **10** |
| 22 | `06` | `kernRightClassCount` | **6** |
| 23 | `04` | `ligaturePairCount` | **4** |
| 24 | `A0 00 00 00` | `dataOffset` | **160** = `0xA0` |
| 28 | `00 00 00 00` | reserved | zero |

`dataOffset = 160` is exactly `HEADER_SIZE + styleCount × STYLE_TOC_ENTRY_SIZE
= 32 + 4 × 32`, i.e. the first byte after the TOC — which is what §2.1 predicts
for the first style. ✔

All four entries:

| styleId | intervals | glyphs | advY | asc | desc | kernL | kernR | kLC | kRC | ligs | dataOffset |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 regular | 61 | 2676 | 40 | 30 | −11 | 12 | 9 | 10 | 6 | 4 | 160 |
| 1 bold | 61 | 2676 | 40 | 30 | −11 | 9 | 9 | 8 | 8 | 2 | 270,649 |
| 2 italic | 61 | 2676 | 40 | 30 | −11 | 11 | 8 | 10 | 5 | 2 | 545,120 |
| 3 bold-italic | 61 | 2676 | 40 | 30 | −11 | 9 | 13 | 8 | 4 | 2 | 815,861 |

Note §5.3 in the concrete: the four styles agree on coverage and vertical
metrics and share nothing else — four different kern matrices (10×6, 8×8, 10×5,
8×4) and four different ligature sets.

### 10.3 Section offsets for style 0

Applying `computeStyleFileOffsets` with `base = 160`:

| Section | Offset | Length | Arithmetic |
|---|---:|---:|---|
| Intervals | 160 | 732 | 61 × 12 |
| Glyphs | 892 | 42,816 | 2676 × 16 |
| Kern left | 43,708 | 36 | 12 × 3 |
| Kern right | 43,744 | 27 | 9 × 3 |
| Kern matrix | 43,771 | 60 | 10 × 6 |
| Ligatures | 43,831 | 32 | 4 × 8 |
| Bitmaps | 43,863 | 226,786 | implicit |

**Cross-check.** Style 1 begins at 270,649, so style 0 occupies
`270,649 − 160 = 270,489` bytes. The six sized sections total
`732 + 42,816 + 36 + 27 + 60 + 32 = 43,703`, leaving **226,786** for bitmaps.
And the last glyph in the table has `dataOffset + dataLength = 226,786`
exactly. ✔ The bitmap section is contiguous and fully accounted for.

### 10.4 The interval table

61 intervals, 732 bytes at offset 160. The first five and the last:

| # | `first` | `last` | `offset` | span | running sum |
|---:|---|---|---:|---:|---:|
| 0 | U+0020 | U+007F | 0 | 96 | 0 |
| 1 | U+00A0 | U+024F | 96 | 432 | 96 |
| 2 | U+02B0 | U+0377 | 528 | 200 | 528 |
| 3 | U+037A | U+037F | 728 | 6 | 728 |
| 4 | U+0384 | U+038A | 734 | 7 | 734 |
| … | | | | | |
| 60 | U+FFFD | U+FFFD | 2675 | 1 | 2675 |

**Every `offset` equals the running sum of preceding spans**, and the spans sum
to **2,676 = `glyphCount`**. ✔ Both invariants from §2.5 hold.

The gaps are the pruning described in §4.2: Almendra asks for `reading` but its
faces (plus the fallback chain) cannot supply U+0080–U+009F, U+0250–U+02AF,
U+0378–U+0379, U+0380–U+0383 and so on, so those codepoints are absent and the
intervals split around them. `intervalCount = 61` where `reading` names 20
ranges is entirely this effect.

Since `glyphCount = 2676 ≤ 65535` and every `first`/`last`/`offset` fits in 16
bits, the reader stores this table as `BmpInterval16[61]` — **366 bytes
resident instead of 732**.

### 10.5 A glyph: `U+0041 LATIN CAPITAL LETTER A`

Interval 0 covers U+0020–U+007F at offset 0, so
`glyphIndex = 0 + (0x41 − 0x20) = 33`, and the record sits at
`892 + 33 × 16 = 1420`:

```
0000058c: 14 15 18 01 fe ff 14 00 69 00 00 00 fb 05 00 00
```

| Offset | Bytes | Field | Value |
|---:|---|---|---|
| 0 | `14` | `width` | **20** px |
| 1 | `15` | `height` | **21** px |
| 2 | `18 01` | `advanceX` | `0x0118` = 280 → **17.5 px** (280 / 16) |
| 4 | `FE FF` | `left` | **−2** px (overhangs left of the pen) |
| 6 | `14 00` | `top` | **20** px above the baseline |
| 8 | `69 00` | `dataLength` | **105** bytes |
| 10 | `00 00` | pad | zero |
| 12 | `FB 05 00 00` | `dataOffset` | **1531** (section-relative) |

`ceil(20 × 21 / 4) = ceil(105) = 105 = dataLength` ✔

Decoding those 105 bytes at file offset `43,863 + 1531 = 45,394` with
`p = y*20 + x`, `level = (b[p>>2] >> ((3 - (p&3)) * 2)) & 3`, printing
` `/`.`/`:`/`#` for levels 0–3:

```
|                    |
|     :############. |
|        #:  .###    |
|       .#    ###    |
|       :#    :##    |
|       #:    :#:    |
|      .#.    :#:    |
|      :#     :#:    |
|      ##     :#:    |
|      #:     :#:    |
|     .#.     :#:    |
|     :##:::::##:    |
|     ##::::::##:    |
|    .##      :#:    |
|    :#:      :#:    |
|    ##.      :#:    |
|    ##.      ###    |
|   .##.      ###    |
|   :##:      ###    |
|.:#####.  .#######: |
|                    |
```

That is a serif capital A with a flat apex, and the antialiasing reads
correctly — level 3 in the stems, levels 1–2 on the diagonals. The table in
§3.1 and §3.2 is therefore right about both the bit order and the polarity: had
the levels been inverted this would render as a negative, and had the bit order
been LSB-first the strokes would be mirrored within each 4-pixel group.

### 10.6 The same, on a glyph whose rows are *not* byte-aligned

`U+0066 f` is index 70, `width = 11`, `height = 27`, `dataLength = 75`
(`ceil(11 × 27 / 4) = ceil(74.25) = 75` ✔). Because 11 is not a multiple of 4,
each row starts at a different bit position — which is exactly the property
§9.1's first gotcha is about:

| Row | Starting pixel | Byte | Shift |
|---:|---:|---:|---:|
| 0 | 0 | 0 | 6 |
| 1 | 11 | 2 | 0 |
| 2 | 22 | 5 | 2 |
| 3 | 33 | 8 | 4 |
| 4 | 44 | 11 | 6 |

The first six bytes, decoded:

```
byte 0 = 0x00 = 00000000 -> pixels  0..3  levels 0,0,0,0
byte 1 = 0x02 = 00000010 -> pixels  4..7  levels 0,0,0,2
byte 2 = 0x90 = 10010000 -> pixels  8..11 levels 2,1,0,0
byte 3 = 0x00 = 00000000 -> pixels 12..15 levels 0,0,0,0
byte 4 = 0x7F = 01111111 -> pixels 16..19 levels 1,3,3,3
byte 5 = 0xC0 = 11000000 -> pixels 20..23 levels 3,0,0,0
```

**Byte 2 straddles the row break**: pixels 8, 9 and 10 are the last three of
row 0, and pixel 11 is the *first pixel of row 1*, in the same byte. The two
rows read:

```
 y=0: |       ::. |
 y=1: |     .#### |
```

An implementation that padded rows to byte boundaries would place row 1 at byte
3 and produce garbage from here on.

### 10.7 Kerning for style 0

Twelve left entries (36 bytes at 43,708) and nine right entries (27 bytes at
43,744):

| Left table | | Right table | |
|---|---|---|---|
| U+0041 `A` | L1 | U+0021 `!` | R1 |
| U+0046 `F` | L2 | U+0022 `"` | R1 |
| U+0049 `I` | L3 | U+002A `*` | R2 |
| U+004B `K` | L4 | U+003F `?` | R3 |
| U+004F `O` | L5 | U+0069 `i` | R4 |
| U+0050 `P` | L6 | U+006F `o` | R5 |
| U+0054 `T` | L7 | U+0076 `v` | R6 |
| U+0055 `U` | **L1** | U+2019 `’` | **R2** |
| U+0056 `V` | L8 | U+201D `”` | **R2** |
| U+0057 `W` | **L8** | | |
| U+0059 `Y` | L9 | | |
| U+0066 `f` | L10 | | |

Both tables are sorted ascending by codepoint ✔, and the class sharing is
visible: `A` and `U` share L1, `V` and `W` share L8, and the three right-hand
quote-like characters `*`, `’`, `”` share R2 — which is `derive_kern_classes`
grouping identical rows and columns (§4.6).

The 10 × 6 matrix at 43,771, in 4.4 signed fixed point (÷16 for pixels):

```
        R1     R2     R3     R4     R5     R6
 L1      0      0      0      0      0     -3
 L2      0      0      0     -7    -10      0
 L3      0      0      0      0      0     -7
 L4      0      0      0      0     -3      0
 L5      0      0      0      0      0     -2
 L6      0      0      0      0     -8      0
 L7      0      0      0    -10    -25      0
 L8      0      0      0      0    -19      0
 L9      0      0      0      0    -21      0
 L10    15     14     17      0      0      0
```

Reading a cell: `T` + `o` is `(L7, R5)` ⇒ index `(7−1) × 6 + (5−1) = 40` ⇒
**−25**, i.e. **−1.5625 px** at this size. And `f` + `?` is `(L10, R3)` ⇒
index `9 × 6 + 2 = 56` ⇒ **+17** = **+1.0625 px** — the positive row is the `f`
whose hook needs clearance before a tall right-hand mark, which is the classic
case for a *positive* kern.

The all-zero columns R1–R3 for L1–L9 are the cost of the class model: `A`
through `Y` kern against nothing in the quote group, but the cells exist
because `f` does. Sixty bytes buys sixty pairs; a pair list for the same data
would be 15 non-zero pairs × 8 bytes = 120 bytes plus the lookup.

### 10.8 Ligatures for style 0

Four entries, 32 bytes at 43,831:

| `pair` | Decoded | `ligatureCp` |
|---|---|---|
| `0x00660068` | U+0066 `f` + U+0068 `h` | **U+FB00** (ff) |
| `0x00660069` | U+0066 `f` + U+0069 `i` | U+FB01 (fi) |
| `0x0066006C` | U+0066 `f` + U+006C `l` | U+FB02 (fl) |
| `0x00730074` | U+0073 `s` + U+0074 `t` | U+FB06 (st) |

Sorted ascending by the packed key ✔, which is what the runtime's
`std::lower_bound` requires.

**The first row is gotcha 17 from §9.1, caught by this very walk.** `f`+`h`
mapping to the *ff* ligature looks like a pipeline bug and is not. Almendra's
own `GSUB` carries a rule `f` + `h` → glyph `f_h`, and Almendra's own `cmap`
maps **U+FB00 to the glyph named `f_h`** — verified directly against
`lib/EpdFont/scripts/downloaded_fonts/Almendra/H4ckBXKAlMnTn0CskyY6.ttf` with
fontTools 4.63.0. `extract_ligatures_fonttools` resolves the output codepoint
through `glyph_to_cp[lig.LigGlyph]`
([fontconvert_sdcard.py:563-564](../lib/EpdFont/scripts/fontconvert_sdcard.py)),
so it records the font's own claim faithfully. The face has **no `f_f` rule at
all**, so in Almendra "fh" ligates and "ff" does not. Nothing in the pipeline
can detect this; only proofing the rendered page can.

Almendra's GSUB also contains `T`+`h`, `c`+`t`, `f`+`t`, `f`+`j`, `f`+`b`,
`f`+`k` and `a`+`rdilla`→`apple` rules whose output glyphs are unencoded — all
correctly dropped, because `glyph_to_cp` has no entry for them and the sequences
are not in `STANDARD_LIGATURE_MAP`.

### 10.9 The same file as a CPZ1 container

Running the writer over it:

```
$ python3 crosspoint-simulator/tools/compress_seed_fonts.py --input in --output out
  Almendra/Almendra_14.cpfont: 1,089,413 -> 470,128 (0.432)

$ xxd -l 40 out/Almendra/Almendra_14.cpfont
00000000: 4350 5a31 0080 0000 859f 1000 0000 0000  CPZ1............
00000010: 2200 0000 0000 0000 883a 0000 9271 0000  "........:...q..
00000020: 96aa 0000 02df 0000                      ........
```

| Offset | Bytes | Field | Value |
|---:|---|---|---|
| 0 | `43 50 5A 31` | magic | `"CPZ1"` ✔ |
| 4 | `00 80 00 00` | `blockSize` | **32,768** ✔ (the shipped default) |
| 8 | `85 9F 10 00 00 00 00 00` | `originalSize` | **1,089,413** — the `.cpfont`'s exact length ✔ |
| 16 | `22 00 00 00` | `blockCount` | **34** |
| 20 | `00 00 00 00` | reserved | zero |
| 24 | `88 3A 00 00` | `blockEnd[0]` | 14,984 |
| 28 | `92 71 00 00` | `blockEnd[1]` | 29,074 |
| 32 | `96 AA 00 00` | `blockEnd[2]` | 43,670 |
| 36 | `02 DF 00 00` | `blockEnd[3]` | 57,090 |

Checks: `ceil(1,089,413 / 32,768) = 34 = blockCount` ✔ (the derived-not-trusted
rule). `dataStart = 24 + 4 × 34 = 160`, `blockEnd[33] = 469,968`, and
`160 + 469,968 = 470,128` — the file's exact length ✔. Compression ratio
**0.432** on this file, against 0.338 measured for the whole shipped tree,
because Almendra's bitmap fraction is lower than a family with a symbol tail.

---

## What I could not establish

Listed rather than guessed. Each entry says what would settle it.

1. **What `EpdFontGroup` / `glyphToGroup` would mean *inside* a `.cpfont`.**
   The fields exist in `EpdFontData` and the built-in fonts use them, but
   `SdCardFont` never sets them (`grep -n "groups\|glyphToGroup\|groupCount"
   lib/EpdFont/SdCardFont.cpp` returns zero matches, and both `stubData` and
   `miniData` are memset before population). **There is no group section in the
   `.cpfont` layout**, so their on-disk encoding is undefined — the struct at
   [EpdFontData.h:141-148](../lib/EpdFont/EpdFontData.h) describes a C array in
   flash, not a file section. If a future version adds one, its layout is a new
   decision, not a documented one.
2. **The intended meaning of `flags` bits 1–15.** The writer hardcodes
   `flags = 1` and the reader masks bit 0. Nothing states what a 1-bit
   `.cpfont` would look like or whether one was ever written; the format's
   ancestry (epdiy) supported 1-bit, and `fontconvert.py` still does for
   built-ins. I could not find any 1-bit `.cpfont` or any code that would read
   one. Settling it: `git log -S` on the flags handling, or the v1–v3 format
   history, neither of which I traced.
3. **What v1, v2 and v3 looked like.** Only v4 is readable and only v4 is
   described anywhere I found. A migration note would have to come from the
   history of `SdCardFont.cpp` and `fontconvert_sdcard.py`.
4. **Whether `HalFile`'s destructor closes the handle.** Two error paths in
   `buildMiniKernMatrix` ([SdCardFont.cpp:511-515, 520-530](../lib/EpdFont/SdCardFont.cpp))
   and one in `loadMeasureKernRows` ([:631-635](../lib/EpdFont/SdCardFont.cpp))
   return without an explicit `file.close()`, relying on destruction. I did not
   read `HalStorage.h`, so I state the observation and make no claim about
   whether it leaks. Settling it: read `HalFile`'s destructor.
5. **The runtime selection of `GlyphAa::Strength` and `darkModeAa`.** The pure
   table at [GlyphAaPlanes.h:67-108](../lib/GfxRenderer/GlyphAaPlanes.h) is
   transcribed exactly in §3.4, but which strength a given render uses, and how
   `darkModeAa` is decided, live in a `GfxRenderer` member
   (`getGlyphAaPlanes()`) that I did not trace. It does not affect the file
   format.
6. **Whether any shipped family's source outlines were pre-processed with
   `optical_kern.py`.** It is a standalone tool with no build hook and nothing
   records its use per family; a face patched by it is indistinguishable from
   one whose designer kerned it, except by comparing against the upstream
   original.
7. **The exact `styleCount == 1` file layout in the wild.** The format allows
   it and `fontconvert_sdcard.py` has a single-style mode, but every file I
   examined carries four styles, so the single-style path is described from the
   code rather than from a specimen.
8. **Whether the 255-glyph-pixel cap has ever been hit on a shipped 1x or 2x
   file.** It is documented as a 3x problem (U+2E3B at 276×8) and the generator
   raises on it, but I did not scan the shipped tree for near-misses. A scan of
   `max(width, height)` across `build/seedfonts` would answer it.
