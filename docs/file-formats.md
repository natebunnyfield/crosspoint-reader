# File Formats

These formats describe the SD-card cache files under `/.crosspoint/epub_<hash>/`.
All POD fields are written in the ESP32 little-endian representation used by
`Serialization.h`; strings are length-prefixed UTF-8.

## `book.bin`

### Version 10, superseded by Version 11 (2026-08-23) — struct unchanged, meaning changed

**Current version is 11**, not 10 — `BOOK_CACHE_VERSION` at
`lib/Epub/Epub/BookMetadataCache.cpp:29`. Check the live value with
`grep -n 'BOOK_CACHE_VERSION = ' lib/Epub/Epub/BookMetadataCache.cpp` rather
than trusting a number here; it will drift again. The ImHex pattern below still
describes the v10 byte layout, which v11 does not change — no field was added
or resized.

**Why it bumped anyway, `ef33faef4` ("fix(cache): bump BOOK_CACHE_VERSION — a
warm book skipped the day's work"):** `Epub::load()` returns early on a warm
metadata cache, above the only call to `scanZipForBookNotes()`, so a book
already indexed on a card silently kept whatever DRM/embedded-font/TOC
conclusions an OLDER firmware had reached for it — including a non-UTF-8 TOC
that had failed to parse and committed an empty one, with no way back short of
a manual Clear Cache. The rule this restates, from `Section.cpp`'s own
version-bump ladder below: bump when the format changes, when pagination
changes, **or when anything the cached pass DECIDED changes** — a cache is a
record of conclusions, and a conclusion reached by code that has since been
fixed is stale even though the bytes on disk still parse the same way.

`book.bin` stores EPUB metadata plus lookup tables for spine and TOC entries.
The current firmware writes this version from `BookMetadataCache`.

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 11  // was 10; the v11 bump changed no byte layout, see above
#define MAX_STRING_LENGTH 65535

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

struct Metadata {
    String title [[comment("Book title")]];
    String author [[comment("Book author")]];
    String language [[comment("Book language code")]];
    String coverItemHref [[comment("Path to cover image")]];
    String textReferenceHref [[comment("Path to guided first text reference")]];
};

struct SpineEntry {
    String href [[comment("Resource path")]];
    u32 cumulativeSize [[comment("Cumulative uncompressed spine size through this entry")]];
    s16 tocIndex [[comment("Index into TOC, or inherited/previous TOC index when no direct entry exists")]];
};

struct TocEntry {
    String title [[comment("Chapter/section title")]];
    String href [[comment("Resource path")]];
    String anchor [[comment("Fragment identifier")]];
    u8 level [[comment("Nesting level")]];
    s16 spineIndex [[comment("Index into spine (-1 if none)")]];
};

struct BookBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    u32 lutOffset [[comment("Offset to lookup tables")]];
    u16 spineCount;
    u16 tocCount;

    Metadata metadata;

    u32 currentOffset = $;
    if (currentOffset != lutOffset) {
        std::warning(std::format("LUT offset mismatch: expected 0x{:X}, got 0x{:X}", lutOffset, currentOffset));
    }

    u32 spineLut[spineCount] [[comment("Spine entry offsets")]];
    u32 tocLut[tocCount] [[comment("TOC entry offsets")]];

    SpineEntry spines[spineCount];
    TocEntry toc[tocCount];
};

BookBin book @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```

## `section.bin`

### Version 30, twenty-five bumps behind — current is 55

**Current version is 55**, not 30 — `SECTION_FILE_VERSION` at
`lib/Epub/Epub/Section.cpp:199`. Check the live value with
`grep -n 'SECTION_FILE_VERSION = ' lib/Epub/Epub/Section.cpp` rather than
trusting a number here; this constant has moved on every few days of work and
the ImHex pattern below (still `EXPECTED_VERSION 30`, matching the prose
changelog's newest fully-described entry, v37) has not been kept in step with
the fields v38-v55 added. Treat the pattern as a v30 snapshot, not as
current-format documentation, until someone rewrites it against the live
`SectionBin::write`/`::read` in `Section.cpp`.

**Versions 38-55, not yet described in prose here — one line each, from the
commit that bumped the constant, verified 2026-08-30 by walking
`git log -p --follow -- lib/Epub/Epub/Section.cpp` for every added
`SECTION_FILE_VERSION = N` line.** These are pointers for whoever writes the
real changelog entries next, not full descriptions — read the cited commit
before relying on any of them for a field layout:

| v | commit | what the commit subject says |
|---:|---|---|
| 38 | `8f97e7184` | exact page geometry — margin row retired, baseline, bottom, sinkage, insets |
| 39 | `ae6981ddf` | hanging punctuation, alignment setting, widows, line grid, chapter bar |
| 40 | `2f182a86f` | inter-block gaps cap at half a line |
| 41 | `8e2d4afdb` | blockquote inset clipping and dash-led lines; block audit recorded |
| 42 | `6378eb167` | lists: real numbers, hanging indents, capped nesting |
| 43 | `bdfe5f663` | punctuation hangs off the LEFT edge too |
| 44 | `7b75aa06d` | the measure decides justified or ragged, not a setting (matches the inline comment on `paragraphAlignment` below) |
| 45 | `ac8dc109c` | sparse ruby, and the book tells you what was done to it |
| 46 | `2efd1ff70` | non-UTF-8 books open, missing glyphs show, library sync skips unchanged |
| 47 | `3c631dd75` | a centimetre is 59 pixels, not one |
| 48 | `a72786082` | a caption nobody laid out was printed over by every column |
| 49 | `eb35311b9` | the justification threshold is a setting, on the screen that can hold it |
| 50 | `aa144fd77` | the spaced `! important`, and a count that doubled |
| 51 | `c07025310` | Typography Settings, with per-ligature control |
| 52 | `0d58d4e9d` | XS and XXS sizes, and a table header keeps its rows |
| 53 | `070ab4e64` | a hairline between the records of a flattened table |
| 54 | `d624ae638` | the separators went in the wrong emitter — the key block had none |
| 55 | `03a5a027f` | a definition list reads as one, instead of as a wall of text ([B-042]) |

Each file in `sections/*.bin` stores one laid-out spine section. The header is
also the cache-busting key: if any layout-affecting setting differs from the
current reader settings, the section is discarded and rebuilt.

Version 37 adds a fourth page-element tag, `TAG_PageRotatedText = 4`: one
already-wrapped line of a table shown on a clockwise-turned page (T-021), stored
as x, y, a bold byte and the string. Tags are appended, never renumbered, so a
v36 cache read by v37 code would still parse — the version is bumped anyway
because the LAYOUT decision changed: a wide table that used to flatten now
becomes a rotated page, or the key-block form, and a cached section would
otherwise keep the old shape forever.

Version 36 is binary-identical to version 35 — no field changed. It was bumped
because table LAYOUT changed: a table that plans as columns is now emitted as
columns with a rule under the header row, instead of one paragraph per cell in
reading order (T-012, ruled 2026-08-18). A cached section from before that
would render the old shape forever, since nothing else in the header describes
how tables were laid out.

Version 35 appends a word-anchor LUT after the list-item LUT — one uint32 per
page, no count prefix (the page count bounds it), located by a fifth offset
slot appended to the header (so every earlier offset's distance from the
header end shifts by 4). Entry `i` is the chapter-global source byte position
where page `i+1` begins, sampled by the parser as page `i` completed; page 0
starts at 0 by definition. Anchors are layout-invariant — they depend only on
the source text, not on font, size, or spacing — which is what lets a reflow
reposition to the page containing the exact word the reader was on. A partial
file's watermark trailer now follows the word LUT.

Version 35 also changes pagination itself: h1-h3 headings force a page break
(as TOC chapter boundaries always did), which pins a page-top heading to the
page top under every font, size, and spacing. h4-h6 still flow inline.

`progress.bin` gained a 12-byte form at the same time: the 8-byte form plus
the current page's word anchor (uint32 LE). Length remains the discriminator;
shorter files degrade to paragraph, then proportional, repositioning.

Version 34 is binary-identical to version 33. The version was bumped because
word-gap suppression was narrowed to tokens glued together in the source: v33
dropped the gap between any two words meeting at a CJK break opportunity, which
collapsed the spaces between Hangul words, so v33 word positions no longer match
what the layout engine now produces.

Version 30 is binary-identical to version 29. The version was bumped because
Arabic contextual shaping changed text measurement (`getTextAdvanceX` now
measures the shaped visual text), so word positions cached by v29 no longer
match what `drawText` renders.

Version 28 introduced serialized word style bits for underline, strikethrough,
superscript, and subscript. The format also includes:

- cache-busting fields for paragraph alignment, hyphenation, embedded CSS,
  image rendering mode, and Focus Reading
- page offset LUT
- anchor-to-page map for fragment and footnote navigation
- paragraph and list-item LUTs used by KOReader sync page refinement
- optional per-word Focus Reading split metadata
- per-page footnote entries
- serialized word style bits for underline, strikethrough, superscript, and
  subscript
- flat TextBlock word storage (v29): per-word arrays plus one shared
  NUL-terminated text blob, replacing v28's length-prefixed word strings. The
  on-disk order mirrors the in-RAM arena so the firmware reads a whole block
  payload with a single allocation and a single SD read

ImHex pattern:

```c++
import std.mem;
import std.string;
import std.core;

#define EXPECTED_VERSION 30
#define MAX_STRING_LENGTH 65535
#define FOOTNOTE_NUMBER_LEN 32
#define FOOTNOTE_HREF_LEN 96

struct String {
    u32 length [[hidden, comment("String byte length")]];
    if (length > MAX_STRING_LENGTH) {
        std::warning(std::format("Unusually large string length: {} bytes", length));
    }
    char data[length] [[comment("UTF-8 string data")]];
} [[sealed, format("format_string"), comment("Length-prefixed UTF-8 string")]];

fn format_string(String s) {
    return s.data;
};

enum PageElementTag : u8 {
    TAG_PageLine = 1,
    TAG_PageImage = 2,
    TAG_PageHorizontalRule = 3
};

enum WordStyle : u8 {
    REGULAR = 0,
    BOLD = 1,
    ITALIC = 2,
    BOLD_ITALIC = 3,
    UNDERLINE = 4,
    STRIKETHROUGH = 8,
    SUP = 16,
    SUB = 32
};

enum TextAlign : u8 {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    NONE = 4
};

struct BlockStyle {
    TextAlign alignment;
    bool textAlignDefined;
    s16 marginTop;
    s16 marginBottom;
    s16 marginLeft;
    s16 marginRight;
    s16 paddingTop;
    s16 paddingBottom;
    s16 paddingLeft;
    s16 paddingRight;
    s16 textIndent;
    bool textIndentDefined;
    bool isRtl;
    bool directionDefined;
};

struct TextBlock {
    u16 wordCount;
    u8 hasFocus;
    u16 textBytes [[comment("Total size of text[], including one NUL per word")]];

    if (wordCount > 0) {
        u16 textOff[wordCount] [[comment("Byte offset of word i's text within text[]")]];
        s16 wordXPos[wordCount];
        if (hasFocus != 0) {
            u16 wordFocusSuffixX[wordCount] [[comment("Suffix x offset from word start")]];
        }
        WordStyle wordStyle[wordCount];
        if (hasFocus != 0) {
            u8 wordFocusBoundary[wordCount] [[comment("UTF-8 byte boundary between bold prefix and suffix")]];
        }
        char text[textBytes] [[comment("All words back to back, each NUL-terminated")]];
    }

    BlockStyle blockStyle;
};

struct ImageBlock {
    String imagePath;
    s16 width;
    s16 height;
};

struct PageLine {
    s16 xPos;
    s16 yPos;
    TextBlock block;
};

struct PageImage {
    s16 xPos;
    s16 yPos;
    ImageBlock image;
};

struct PageHorizontalRule {
    s16 xPos;
    s16 yPos;
    u16 width;
    u8 thickness;
};

struct PageElement {
    PageElementTag pageElementType;
    if (pageElementType == TAG_PageLine) {
        PageLine pageLine [[inline]];
    } else if (pageElementType == TAG_PageImage) {
        PageImage pageImage [[inline]];
    } else if (pageElementType == TAG_PageHorizontalRule) {
        PageHorizontalRule horizontalRule [[inline]];
    } else {
        std::error(std::format("Unknown page element type: {}", pageElementType));
    }
};

struct FootnoteEntry {
    char number[FOOTNOTE_NUMBER_LEN];
    char href[FOOTNOTE_HREF_LEN];
};

struct Page {
    u16 elementCount;
    PageElement elements[elementCount] [[inline]];

    u16 footnoteCount;
    FootnoteEntry footnotes[footnoteCount];
};

struct AnchorEntry {
    String anchor;
    u16 page;
};

struct AnchorMap {
    u16 count;
    AnchorEntry entries[count];
};

struct ParagraphLut {
    u16 count;
    u16 paragraphIndex[count];
};

struct SectionBin {
    u8 version;
    if (version != EXPECTED_VERSION) {
        std::error(std::format("Unsupported version: {} (expected {})", version, EXPECTED_VERSION));
    }

    s32 fontId;
    float lineCompression;
    bool extraParagraphSpacing;
    u8 paragraphAlignment;  // constant since v44 -- the setting is gone and the
                            // measure decides per block (docs/auto-justification.md)
    u16 viewportWidth;
    u16 viewportHeight;
    bool hyphenationEnabled;
    bool embeddedStyle;
    u8 imageRendering;
    bool focusReadingEnabled;
    bool lineGridEnabled;  // v39+

    u16 pageCount;
    u32 pageLutOffset;
    u32 anchorMapOffset;
    u32 paragraphLutOffset;
    u32 listItemLutOffset;

    Page pages[pageCount];

    u32 currentOffset = $;
    if (currentOffset != pageLutOffset) {
        std::warning(std::format("Page LUT offset mismatch: expected 0x{:X}, got 0x{:X}", pageLutOffset, currentOffset));
    }

    u32 pageLut[pageCount] [[comment("Page data offsets")]];

    if (anchorMapOffset != 0) {
        AnchorMap anchorMap @ anchorMapOffset;
    }

    if (paragraphLutOffset != 0) {
        ParagraphLut paragraphLut @ paragraphLutOffset;
    }

    if (listItemLutOffset != 0 && paragraphLutOffset != 0) {
        u16 listItemIndex[paragraphLut.count] @ listItemLutOffset;
    }
};

SectionBin section @ 0x00;

u32 fileSize = std::mem::size();
u32 parsedSize = $;
if (parsedSize != fileSize) {
    std::warning(std::format("Unparsed data detected: {} bytes remaining at offset 0x{:X}", fileSize - parsedSize, parsedSize));
}
```
