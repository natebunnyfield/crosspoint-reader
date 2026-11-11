<!-- Copied into the repo from the reviewer's report so the clean list below
     stops the next review re-reading the same code. Commit shas in the body are
     as they stood on b5-work at review time (tip 7f9c2d7cc), before the rebase
     onto main; search by subject to find each on main. -->

**Fixed before batch 5 shipped (2026-11-08):**

| finding | fix |
|---|---|
| F1 hidden `<rt>` swallows later text | `fix(epub): a hidden <rt> no longer swallows what follows it` (test `SkippedBlock.AHiddenRubyTextDoesNotSwallowWhatFollows`) |
| F2 ligatures lost after a cache release | `fix(fonts): a subset hit re-points a style's ligatures after a cache release` (inferred, not reproduced) |
| F3 metadata clamp mid-character | `fix(epub): the OPF metadata clamp never ends inside a character` (test `TheClampNeverEndsInsideACharacter`) |
| P1 dot-folder files over HTTP | `fix(webserver): refuse a protected folder anywhere in the path`. WebDAV still mirrors dot paths on purpose; whether secrets leave that mirror went to the owner. |
| P2 unescaped image names | `fix(webserver): escape EPUB image names on the files page` |
| P3 image OOM exits | `fix(epub): an image's allocation failure keeps the parser balanced` |
| stale `docs/file-formats.md:120` "v57" | left; that paragraph says to grep the constant, and the table rows are current |

# Adversarial review: batches 3-5 (a5e076efd..b5-work 7f9c2d7cc), 2026-11-08

Reviewer: read-only agent. Tree: `~/src/crosspoint-reader/.claude/worktrees/probe` at `7f9c2d7cc` (59 commits).
Areas 1-5, 8 and 9 and the unassigned commits were reviewed directly. Areas 6, 7, 10 and 11 and the five
other-session commits (4604eb750, 6f30e3d26, a269f9720, 1846a08a3, c764f2821) went to a forked reviewer
under the same rules. Its results are merged below and marked [fork].

Probe: `/tmp/claude-501/advrev/rt/`. It is a host build of the real parser chain. It copies the
`test/cascade_lines` harness, compiles the tip's sources, and uses gtest from
`/tmp/claude-501/b5r-tests/_deps`. To rebuild it: `cmake -S . -B build && cmake --build build && ./build/Probe`.

## Findings (most severe first)

### F1. A hidden `<rt>` swallows later text and corrupts the block-style stack

- **Severity:** would-ship-a-visible-bug, though the trigger is narrow.
- **Status:** verified with the probe.
- **Where:** `lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`
  - the `</rt>` branch, `:2797-2827` (it decrements depth at `:2826` and returns);
  - the `</ruby>` branch, `:2829-2839` (`:2837`);
  - both run before `insideSkippedSubtree` is computed (`:2858`) and before the "Leaving skip" reset (`:2906`).

**Mechanism.**
1. `startElement` treats a hidden `<rt>` (`hidden` attribute, `:1571`/`:1594`, or `display:none`) as a skipped element. It sets `skipUntilDepth = depth` and returns before the ruby handler at `:2155`.
2. Its close takes the `</rt>` early return. `depth` is decremented, but `skipUntilDepth` is never reset to `INT_MAX`.
3. From then on, any text at a depth greater than the stale value is treated as inside a skipped subtree, and is dropped by `characterData` (`:2588`).
4. The first element to close at that depth is also misread as skipped:
   - c6835b757's new guard (`:3036`) then skips that element's `blockStyleStack` pop;
   - the stack is now off by one, so a container's margins leak onto content outside it.

**Trigger (probe `Probe.HiddenRtAttr`, `Probe.HiddenRtLeaksBlockStyle`).**
```html
<p>alpha<ruby>X<rt hidden="">kan</rt></ruby>beta</p>
<div class="cascade-d1"><div><p>lostpara</p></div></div><p>afterpara</p>
```
- Control (plain `<rt>`): `alpha` (x=0), `lostpara` (x=29), `afterpara` (x=0).
- Tip: `alpha Xbeta`, then `afterpara` at x=29. The paragraph `lostpara` is gone, and `afterpara` carries the div's 1em indent.
- `style="display:none"` on the `<rt>` gives the same result.
- A hidden `<span>` (control) is fine.

**How old each part is.**
- The `display:none` form of the text loss predates the window.
- The `hidden`-attribute form is new with fe152b89e (#3390).
- The stack corruption is new with c6835b757. Before that commit, the misread close still popped.

**Fix.** Compute `insideSkippedSubtree` before the `rt`/`ruby` branches. Take those branches only when it is false. Otherwise fall through to the general path, which decrements depth and runs the "Leaving skip" reset. At minimum, after `self->depth -= 1` in both branches, add `if (self->skipUntilDepth == self->depth) self->skipUntilDepth = INT_MAX;`.

**Test.** Add the two probe cases to `SkippedBlock.*` in `test/cascade_lines`.

**Real-world reach.** Books that switch furigana off with `rt { display: none }` or a "no-ruby" class.

### F2. After the new advance-cache release, other styles lose their ligatures at render

- **Severity:** cosmetic / latent.
- **Status:** inferred from code, not reproduced.
- **Where:**
  - `lib/EpdFont/SdCardFont.cpp:1502`: `clearPersistentCache()` inside `prewarmStyle`'s bitmap-reservation path, new in c18f29e16 (#3521);
  - `:180`: `freeStyleKernLigatureData` nulls `miniData.ligaturePairs` for every style;
  - `:1257`: the subset-hit path re-points only when `miniKernLeftClassCount == 0`.

**Sequence.**
1. Under fragmentation (largest block smaller than the arena), the page scope prewarms its groups from high to low style (68602620a).
2. The prewarm of style A fails to reserve its arena while an advance table is resident.
3. `clearPersistentCache()` frees the kern/ligature data of all four styles and nulls their `miniData.ligaturePairs`.
4. Style B was already prewarmed in this scope and keeps its mini. It draws this page without ligatures.
5. Later pages that subset-hit B's mini do not re-point the ligatures either, because B's mini kern matrix is still resident. B stays ligature-free until its mini is rebuilt for a new codepoint.
6. Layout reloads ligatures through `loadStyleKernLigatureData` (`:617`), so measured and drawn words can differ by a pixel or two.

Style A is unaffected, because its recursion re-applies the pointers at `:1615`.

**Fix.** After `clearPersistentCache()` there, either re-point the other resident styles once their data is reloaded, or release only the advance tables and measure-kern rows and keep the ligature pairs (typically under 1 KB).

### F3. The 512-byte OPF metadata clamp can cut a UTF-8 character in half

- **Severity:** cosmetic.
- **Status:** verified by reading the code.
- **Where:** `lib/Epub/Epub/parsers/ContentOpfParser.cpp:41`, `:71`.
- **What breaks:** `appendMetadataText` appends byte by byte and stops at 512 bytes. It can stop inside a 2- to 4-byte sequence, so a title or author over 512 bytes ends in an orphaned lead byte. `utf8NextCodepoint` renders that as U+FFFD, and it is persisted in book.bin.
- **Trigger:** a CJK title of 171 or more characters.
- **Note:** upstream's code is identical. The UI truncates titles well before 512 bytes, so the stray byte rarely shows.
- **Fix:** when clamping, back up to a UTF-8 boundary (`utf8SafeTruncateBuffer`-style).

### Pre-window findings noticed in passing (not regressions; untouched or kept unchanged by this window)

**P1. Files inside dot-folders, including the saved WiFi passwords, can be downloaded over HTTP.** [fork, re-verified]
- Where: `src/network/CrossPointWebServer.cpp:637-647`. `handleDownload` tests only the last path component.
- Trigger: `GET /download?path=/.crosspoint/wifi.json` passes. Re-verified: `src/WifiCredentialStore.h:44` names that file.
- The passwords are XOR plus base64, which is reversible (`src/WifiCredentialStore.cpp:17`).
- `/api/files?path=/.crosspoint` and uploads into dot-folders pass the same way.
- Severity: latent (security, LAN only, while file transfer runs).
- Fix: reject any component that starts with "." or is in HIDDEN_ITEMS, in download, list and upload.

**P2. Image names from inside an EPUB go into the page unescaped.** [fork, not re-verified by me]
- Where: `src/network/html/FilesPage.html:2813-2830`, `:2877-2885`. The EPUB image-processing grid puts the zip entry name into `innerHTML` unescaped (`alt="${img.name}"` and the name text). Introduced in 7d56810ee.
- Trigger: a crafted EPUB can run script in the device's origin.
- Severity: latent.
- Fix: `escapeHtml()`.

**P3. An image-placement OOM returns without `depth += 1`.**
- Where: `ChapterHtmlSlimParser.cpp:2047`, `:2054`, `:2074`, `:2080`.
- What breaks: the void element's `endElement` still decrements depth, so depth runs one low for the rest of the chapter. An enclosing `<b>`'s inline entry is then never popped, and bold leaks.
- `:2047`/`:2054` also leave `currentPage` null without `noteAllocationFailure()`.
- 5853d4b27 touched `:2074`/`:2080` and kept the bare `return`.
- Severity: latent (needs a heap failure of about 100 bytes).
- Fix: call `noteAllocationFailure()` on all four, or `depth += 1` before returning.

## CLEAN: what was checked and found sound

### 1. #3518 unique ownership (5853d4b27)
- Read every `std::move` in `addLineToPage`, `flushPendingLines` and `placeLineOnPage` (`:3370-3530`):
  - each `line` is dereferenced only before its move;
  - `lineAdvanceOf(*line)` is taken before the PageLine move;
  - no line is read after it is moved.
- The two layout probes take the line by value. The std::function forwards an rvalue, so this is sound.
- No `Page*`, `PageLine*`, `TextBlock*` or `PageElement*` member exists anywhere in `src/activities/reader`.
- `getBlock()` has four callers, all local loops: `EpubReaderActivity.cpp:1806`, `PageTextCapture.h:59`, `Section.cpp:1192` and the tests.
- Copying an element is now a compile error, so double ownership is impossible.
- `isEmpty()` and `wordCount()==0` are identical (`TextBlock.h:78-80`).
- `PageImage::deserialize` rejecting a null block is correct; `ImageBlock::deserialize` returns null only on OOM.
- PageLine OOM goes through `noteAllocationFailure` → `XML_StopParser`. Later lines in the same `layoutAndExtractLines` loop are safe, because `currentPage` stays non-null and the null-page paths re-allocate.
- The serialized bytes are unchanged, so no version bump is needed. The commit message's "stays at 75" is stale prose only; the tree was at 77.

### 2. #3633 glyph fast path (7f9c2d7cc)
Derived by hand against `drawPixel`/`drawPixelDevice` (`GfxRenderer.cpp:730-790`), case by case:
- frames for None, CW and CCW match the per-pixel mapping;
- `rawLevels()` matches the swapped mask, bit for bit;
- `clearBits` matches for the BW pass, the gray passes and 1-bit glyphs;
- the bitmap index and 2-bit bit order are the same;
- the 4-at-a-time loop aligns on `source & 3`, which is correct for unpadded rows;
- `clipAxis` is right for step ±1;
- band clipping uses `getWriteTarget`, `getWriteOriginY` and `getWriteRows`, the same as drawPixel's strip redirect;
- the dirty rect covers the whole cell and is marked before the strip clip, as drawPixel does.

Other cases:
- Any partly off-panel glyph declines to the per-pixel path, so negative coordinates are covered.
- `rotateCoordinates` is affine for all four orientations, and `orientation` is a compile-time Portrait (`GfxRenderer.h:78`). The rasterizer test covers all four anyway.
- Render scale above 1: logical glyphs decline; deviceSpace glyphs match `drawPixelDevice`.
- No `int` overflow: rowBit is at most about 1.5M at 2x.

Gaps hunted for and not found:
- drawPixel has no clip rectangle and no other per-pixel transform that the fast path skips;
- the notdef box and SUP/SUB still take the per-pixel path.

### 3. #2332 image fragmentation (95dd7a326)
- All XTH plane-size sites were read: Xtc.cpp cover and thumb, XtcParser `loadPage` and `loadPageStreaming`, XtcReaderActivity buffer and plane. grep shows no other `(w*h+7)/8` site.
- Overflow: `width*ceil(h/8)*2` with uint16 is at most 1.07e9, under 2^32. The 1-bit `((w+7)/8)*h` is at most 5.4e8 in `int`. Adding the row size stays under 2^32.
- The new `dataSize` equality check matches upstream exactly. 480x800 and 800x480 pages are unaffected; only odd heights could differ.
- Ditherers:
  - `makeUniqueNoThrow<T[]>` value-initializes (`Memory.h:31-34`), so the error rows start zeroed as before;
  - when `!ok()`, `processPixel`, `nextRow` and `reset` cannot index null;
  - FloydSteinberg's `rowCount` freezing on `!ok()` only fixes the scan direction.
- PNG:
  - the scanline block is owned by a local unique_ptr, and `PngDecodeContext` has no destructor freeing `currentRow`, so there is no double free;
  - `rawRowBytes` is at most 16384, so `*2` is safe.
- `drawBitmap`: `readNextRow` writes exactly ceil(w/4) output bytes (`Bitmap.cpp:202-292`), so the combined block cannot overrun into `rowBytes`.
- `fillPolygon` ownership is fine.

### 4. Font cache: #3521, #3126 and #3222 (c18f29e16, 3767b412a, 68602620a)
In-place UTF-8 encoding in `endScanAndPrewarm` (`FontCacheManager.cpp`):
- Entry i is read before at most 4 bytes are written, ending at entry i's own end, so an unread codepoint is never overwritten.
- The terminator lands on the first byte of the next (already-processed) group, or on slot 513 for the top group. The array is `MAX_SCAN_CODEPOINTS + 1`.
- Packed sort order matches the group index: codepoints are at most 0x10FFFF in 21 bits (`utf8NextCodepoint` rejects more), style is 2 bits and the font slot is under 4.
- `recordText` runs only while scanning, and `scanMode_` is cleared first.

SdCardFont:
- `resolveStyle` always returns a value under 4.
- Per-group `accumulate=false` replaces only that style (`prewarmStyle` is per style).
- The arena retry is bounded (`fit /= 2`) and uses a prefix of the sorted request.
- The recursion after reserving the bitmap runs exactly one extra pass:
  - the capacity now equals the total;
  - the hysteresis block cannot free the reservation, because `miniBitmapUsed` is 0 after `freeStyleMiniData`;
  - `unionCps` outlives the recursive call.
- Built-in slots: the 4-slot limit is used no more than before, and fewer when styles share data.
- `clearPersistentCache` has no concurrent user. The background build runs under `RenderLock(Try)` (`EpubReaderActivity.cpp:418`), and `advanceTableLookup` re-reads the table pointer on every call.

The only issue found here is F2.

### 5. Parser state: c6835b757, b93bfdaa9, 8cdecf3bf, fe152b89e
- Nothing in `startElement` before the skip check (`:1505-1620`) pushes state. The id/anchor handling, xpath counters and direction inheritance push nothing.
- Every `endElement` pop is now depth-keyed or guarded:
  - inline style stack (`depth ==`);
  - bold and italic until-depth;
  - footnote link depth (set only past the skip);
  - table branches (guarded);
  - block, list and `listStack_` (guarded).
- A hidden nested `<table>` no longer ends its outer table (it used to). A non-hidden nested table's start and end both leave `depth` alone, consistently.
- RTL (#3198):
  - table cells get direction only through `updateEffectiveInlineStyle`, from `blockStyleStack.back()` or `<html>`/`<body>`, the same sources as before;
  - inline direction still feeds inheritance through `effectiveDirection`.
- list-style (#3500):
  - the wire byte and bit 18 are range-checked on decode;
  - `CssPropertyFlags` is 4 bytes, and its static_asserts hold;
  - `applyOver` and `reset` include the field.
- Disproved candidate: a `<ruby>` inside a hidden span joins the next word. Probe `RubyInsideHiddenJoinsWords` prints "foo bar", so it does not.

### 6. Web server: 62888ddf5, e9b032949 [fork]
- Every handler normalizes the path: list, download, upload, mkdir, rename, move, delete, WS START. WebDAV uses `normalisePath`.
- `..` cannot climb above root. Backslashes fail inside SdFat. Only one URL decode happens.
- `escapeHtml` covers `&<>"'` on every `data-*` attribute and link text.
- The ETag is quoted and compared exactly. `W/`, lists and `*` get a harmless 200.
- I re-checked `sendStaticContent` (`CrossPointWebServer.cpp:424-441`): `Content-Encoding: gzip` is kept on 200, and the 304 sends no body.
- The only issues here are the pre-window P1 and P2.

### 7. CSS cache: 0b17deda4, 8cdecf3bf [fork]
- `version == 12` is required at all three read sites, so a v10 or v11 file is never accepted.
- Header checks: unknown flag bits, `ruleCount <= 1500`, selector length 1-256, a 32 KB pool, duplicates, trailing bytes and enum ranges are all rejected.
- Every length is checked with `isfinite`, and the defined-bits mask is `(1<<19)-1`.
- Temp/backup promotion is safe at every interruption point.

### 8. EpdFontData layout (ce5c9f638)
- All 168 tracked headers in `builtinFonts/` have exactly 30 positional initializers, matching the 30 struct fields (awk count). `all.h` has none.
- A kerned header's field order was spot-checked (librefranklin_10_bold).
- The only other initializers are tests using designated (named) initializers, plus `{}` and value-init in SdCardFont.
- The simulator repo constructs no EpdFontData.
- Nothing else reads `kernMatrix`, `kernLeftClasses` or the class counts directly; everything goes through `EpdFont::getKerning`, which checks `lc`/`rc==0` before the `kernRowOffsets[lc - 1]` read.
- `applyKernLigaturePointers` nulls the split and sparse pointers for SD fonts.
- `uint16` row offsets are enough, since at most 255x255 entries exist.

### 9. OPF metadata and Hangul NFC: 1ad6c712c, 583a955ce
- The author separator fires only at a new `<dc:creator>`, and an empty creator is safe.
- Whitespace collapsing works across expat splits.
- Hangul LV/LVT arithmetic is per Unicode 3.12. T-jamo attaches only to an LV syllable (`%28==0`).
- 0xE1 appears only as a lead byte. The full pass does not change NFC text, and short words stay within the short-string buffer, so there is no new allocation.
- book.bin went 12→13→14 and section →74.
- The only issue found here is F3.

### 10. Picco and power: 48ad17f2c, 4fb35aa2e, 5206a02a4, e23b9725b [fork]
- SdFat's `sync()` reaches `cacheSync()` on both FAT and exFAT.
- The RTC payload is read only when its magic matches, and is cleared with it.
- The WiFi power-save guard restores the prior mode, and a nested guard is a no-op.
- The USB fallback is unreachable on X4, X3 and Picco.

### 11. Cache versions [fork, re-verified by me with `git log -p`]
- Section: 70→71 (326fe51b3) →72 (b93bfdaa9) →73 (9d5d8d25a) →74 (583a955ce) →75 (fe152b89e) →76 (c6835b757) →77 (8cdecf3bf) →78 (828a999ac).
- book.bin: 12→13 (583a955ce) →14 (1ad6c712c).
- CSS: 10→11 (0b17deda4) →12 (8cdecf3bf).
- No number is reused. No layout-changing commit lacks a bump; e7840e266's SD-font Arabic change is covered by the later bumps.
- One stale doc: `docs/file-formats.md:120` still says v57 (cosmetic).

### Other commits read
- **d9d903a18:** log only.
- **9205da119:**
  - `InflateStream` init/deinit leave nothing allocated on OOM, on the arena and malloc paths alike;
  - the image-failure memo is now cleared on every render.
- **f67ddf7dd:** the Xtensa stack capture checks the pointer and its DRAM range.
- **e7840e266:** every ligature path goes through `applyLigatures` (`GfxRenderer.cpp:981`, `:3090`, `:3295`, `:3412`, `EpdFont.cpp:32`), so layout and render agree.
- **9d5d8d25a:** `applyVerticalAlignToEntry` is pushed and popped by depth.
- **326fe51b3:** reviewed, nothing found.
- **828a999ac:**
  - the space fallback uses the same 12.4 units on both sides (`EpdFontData.h:134`, `getAdvance`);
  - all four ligature call sites agree.
- **8e62e558b:** the non-dithered PNG path is reached only from `SleepActivity.cpp:831`, since EPUB images always dither (`ImageBlock.cpp:401`).
- **c34aaf995:** both shipped YAML files carry `_bcp47`, and no other translation file exists to fail the generator.
- **1e0e03f6e:** reviewed, nothing found.
- **[fork]:** 4604eb750, 6f30e3d26, a269f9720, 1846a08a3 and c764f2821 are clean (lock nesting, queued turns, the 256-sheet cap, the JSON save gate, chunked size lookup).

### Not regressions, noted
- `list-style-type: none` is read only from the `<ul>`/`<ol>`. It is not inherited by nested lists, and not read from `li { list-style: none }`. Markers still show there, as before the window.
- The `hidden` attribute beats author CSS `display` (upstream semantics). In HTML, author CSS wins.

## Limits
- No device or simulator run (no `pio`).
- F2 is inferred, not reproduced.
- P2 was not re-read by me.
- The 16 other-session commits were reviewed only as far as listed above.
