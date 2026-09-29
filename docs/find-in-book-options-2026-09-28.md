# Find in book: phased options, 2026-09-28

**Research only. Nothing is implemented.** This doc sets out the options for the
owner to choose from, per the standing rule that architectural choices go to him
before anything is built.

**Surveyed at:** firmware `main` @ `ca55a1677`, simulator `main` @ `62e1d0d`.
Upstream `upstream/develop` was fetched on 2026-09-28. The other six fork
remotes (`cpjp crossink crossmux folio matcha vcodex`) were NOT re-fetched, so
the fork result below reflects their refs as of their last fetch.

**Confidence labels.** *Verified* means read in the source at the cited
file:line (or measured in an earlier dated doc, cited). *Inferred* means reasoned
from the code, not run. *Estimate* means a number nobody has measured. **No
device timing exists for anything in this doc.** Every host timing quoted comes
from `docs/performance-indexing-2026-08-23.md`, and that doc says itself that the
host over-weights writes and prices some device-only costs at zero.

---

## 1. Summary

- **The pieces exist.** Pages are stored as rendered words
  (`TextBlock::wordText`, `lib/Epub/Epub/blocks/TextBlock.h:82`). There is a
  pure, host-tested function that turns one page into text plus per-word
  rectangles (`readaloud::buildCapture`,
  `src/activities/reader/ReadAloudCapture.h:85`). There is a text-entry activity
  with host-keyboard support (`makeTextEntryActivity`,
  `src/activities/util/TextEntryFactory.h:14`). And the reader can already jump
  to a spine plus a page (`pendingPageJump`, `EpubReaderActivity.h:15`,
  `EpubReaderActivity.cpp:1187`).
- **The two hard parts.** First, a chapter has to be *laid out* before its words
  exist in page form, and layout is lazy and per chapter. Second, the reader has
  **no menu**: the owner removed it on 2026-08-01 (commit `9494d88e8`), so where
  Find opens from is itself a decision.
- **Prior art.** Upstream has an open request (issue #1984) and a closed PR
  (#2451, "feat: in-book search", +1,997/−448). The PR was closed on UX grounds,
  not technical ones: "the UX for typing text using the navigation buttons is
  just too slow". Its design is summarized in section 4.
- **Recommendation: Phase 1** (search the current chapter, list the hits, jump to
  one). Size M. It builds nothing that a later phase has to throw away, and it
  needs no cache-format change. Details in section 5.

---

## 2. What the code does today (verified)

### 2.1 How books are stored and paginated

| Fact | Where |
|---|---|
| Pagination is **per spine item, and lazy**. A book is never laid out as a whole: the reader lays out 8 pages, shows one, and a background tick keeps it 5 pages ahead | `BUILD_PAGES_PER_CHUNK = 8`, `BUILD_WINDOW_AHEAD = 5` (`EpubReaderActivity.h:108`, `:144`); `docs/performance-indexing-2026-08-23.md` "What indexing turned out to be" |
| Each spine's unzipped XHTML is cached at `<bookcache>/html/<spine>.html`. It is promoted as soon as it is inflated, it is keyed only on the book, and it **survives** the settings changes that wipe the layout caches | `Section.cpp:497`-`:566`; the comment at `:505`-`:510` |
| The layout cache is `<bookcache>/sections/<spine>.bin`: the serialized pages, then four tables (page offset, paragraph index, list-item index, word anchor), then an anchor map | `Section.h:28`-`:38` (`PageLutEntry`), `Section.cpp:881`; `SECTION_FILE_VERSION = 62` at `Section.cpp:251` |
| A section file is about **3.1x the chapter's XHTML** (7.84 MB for 2.56 MB, measured on `giant.epub`) | perf doc, "Device weighting" |
| A giant single-spine book may **never finalize** its `.bin` in one sitting. It is saved as a *partial*, and only the pages up to the watermark exist | `Section.h:73`-`:80`, `:119`-`:125`; `EpubReaderActivity.h:137`-`:144` |
| Host build cost after the 2026-08-23 fixes: **16 ms** for a 246-page chapter, **267 ms** for a 5,816-page spine (`-O2`). **Device cost is unmeasured** | perf doc, "Cumulative" |
| **Word anchor**: a layout-invariant, chapter-global position. It is the running total of the bytes of every word passed to `addWord()`, counted after NFC composition and with no spaces. It survives any font, size or spacing reflow | `ParsedText.cpp:527`-`:528`; `ChapterHtmlSlimParser.h:314`-`:325` |
| `getPageForWordAnchor(anchor)` maps an anchor to a page under the current pagination. While a build is running it **refuses** (returns nullopt) until the build has laid out past that anchor | `Section.h:182`; `Section.cpp:1255`-`:1260`, `:1310` |

### 2.2 Where a page's text can be read back

- `Section::loadPage(n)` returns a `Page` whose `PageLine` elements each hold a
  `TextBlock` of words (`Section.cpp:1074`, `Page.h:37`-`:48`). The arena layout
  means one heap allocation per line (`TextBlock.h:15`-`:19`). That undercuts
  upstream PR #2451's reason for not deserializing pages: it measured upstream's
  older layout, which made about 250 allocations per page.
- `Section::getTextFromSectionFile()` (`Section.cpp:1090`) joins the current
  page's words with spaces. **It has no callers**: a grep of `src/` and `lib/`
  outside `Section.*` finds none. It is dead code, and it is the obvious seed for
  a page matcher.
- **The read-aloud capture path.** `captureReadAloudPage`
  (`EpubReaderActivity.cpp:1543`) walks the display list and measures each
  token's advance. It then calls `readaloud::buildCapture`
  (`ReadAloudCapture.h:85`), which rejoins words the layout split with a hyphen,
  strips soft hyphens, and emits `text` plus rectangles
  `{x, y, w, h, byteOffset, byteLen}`. The *capture* is gated on
  `gpio.readAloudCaptureWanted()`, which is hard-false on the device
  (`EpubReaderActivity.cpp:1545`-`:1550`). The *grouping* function has no gate:
  it is pure and header-only, and `test/read_aloud_capture` covers it. **For
  Find this is the key primitive**: search `text`, and a hit's byte offset maps
  straight to on-screen rectangles for highlighting.
- There is **no plain-text form of a book on disk**. The nearest things are the
  cached XHTML (markup, entities, hidden subtrees) and the rendered words inside
  `sections/*.bin`.

### 2.3 Which text the layout skips or adds

This matters to any approach that searches the XHTML instead of the laid-out
pages:

- Skipped: `head rp script style noscript title desc annotation annotation-xml
  template iframe` (`ChapterHtmlSlimParser.cpp:113`), and any element whose CSS
  says `display:none` (`:1558`-`:1562`).
- Added text that is not in the source character data: image alt text
  (`:2066`) and synthesized header text (`:1747`).

So a raw XHTML byte stream is not the same text stream as the one the word
anchor counts.

### 2.4 Text entry a Find prompt could reuse

- `makeTextEntryActivity(renderer, mappedInput, title, initialText, maxLength)`
  (`TextEntryFactory.h:14`) routes to the daisywheel (`DaisyEntryActivity`) or
  to `KeyboardEntryActivity` (13-grid, which is the default since
  `keyboardLayout = KEYBOARD_GRID13` at `CrossPointSettings.h:524`, or QWERTY).
  It returns `KeyboardResult{text}` (`ActivityResult.h:16`).
- Existing callers: owner name (`SettingsActivity.cpp:402`), file rename
  (`FileManagerActivity.cpp:396`), Wi-Fi password and SSID
  (`WifiSelectionActivity.cpp:321`, `:344`).
- **A host keyboard already reaches it.** It calls
  `setTextEntryActive(true/false)` at `KeyboardEntryActivity.cpp:179`/`:184`
  and `consumeTypedText` at `:610`. So on the iOS app (on-screen or Bluetooth
  keyboard) and on the Mac, typing a query is real typing. On the X3 it means
  pecking at the grid, which is the objection that closed upstream PR #2451.
- Case folding for more than ASCII already exists:
  `toLowerLatin` / `toLowerCyrillic` (`hyphenation/HyphenationCommon.h:13`-`:14`,
  Latin-1 Supplement and Latin Extended included). `lib/Utf8` has no folding
  helper.

### 2.5 Memory and storage constraints

- ESP32-C3, about 380 KB of heap, no PSRAM (`.skills/SKILL.md`, "Hardware Specs").
- During a build chunk, the peak reached **31 to 118 KB** on the owner's own
  books. The CSS rule map (up to 64 KB) stays resident from page 0 until the
  build finalizes (`docs/reading-path-heap-budget-2026-09-10.md`, "The floor").
  The owner's crashes on 2026-09-10 were heap exhaustion during ordinary reading.
- Background builds already stand down below 32 KB free or a 16 KB largest block
  (`EpubReaderActivity.h:118`, `:124`).
- **A pushed sub-activity keeps the reader resident.** `startActivityForResult`
  calls `pushActivity` (`Activity.cpp:46`-`:48`), and the reader stays in
  `stackActivities` (`ActivityManager.h:52`), together with its `Section`, its
  build context and its CSS map. A Find activity that lays out *other* chapters
  while the reader's build is still live would therefore hold two build working
  sets at once. PR #2451 released the reader's section before searching for
  exactly this reason.
- Streaming is available. The parser reads the XHTML in 1 KB chunks
  (`PARSE_BUFFER_SIZE`, `ChapterHtmlSlimParser.cpp:28`). Inflating a spine that
  is not cached needs a 32 KB `InflateStream` window (heap doc, "Limits"). The
  cached XHTML avoids that cost.

### 2.6 How the reader navigates to an arbitrary position

- **Chapter pick**: sets `currentSpineIndex` and `pendingAnchor`, then calls
  `section.reset()` (`EpubReaderActivity.cpp:903`-`:935`). The next render
  builds only until the anchor resolves (`:1159`-`:1162`).
- **Page jump**: `pendingPageJump` is applied at `:1187`. The build-to-target
  loop builds until that page exists.
- **Word-anchor reposition** (used after a font change): `:1216`. It is guarded
  by `currentSpineIndex == cachedSpineIndex`, and the build-to-target loop does
  **not** build until a word anchor resolves. A Find jump by anchor would need
  one more condition in that loop (inferred; not tried).
- **Go-to-percent is dead.** `pendingPercentJump` is still read (`:1095`,
  `:1266`), but nothing sets it any more. Its only setter went with the reader
  menu (`9494d88e8`). Grep for `pendingPercentJump = true` finds nothing.
- **Entry points.** A short press of Confirm opens Chapter Select; "there is no
  reader menu" (`EpubReaderActivity.cpp:448`-`:462`). Chapter Select already
  prepends one optional row, the book-notes row (`noteRowCount`,
  `EpubReaderChapterSelectionActivity.h:27`-`:41`, `.cpp:34`, `:60`-`:66`). That
  is a working precedent for a "Find…" row. The iOS gesture table has appendable
  actions (`crosspoint-simulator/ios/GestureBindings.h:236`-`:255`). A host→reader
  channel of the same shape already exists (`consumeFontFamilyStep`,
  `EpubReaderActivity.cpp:405`). No long-press of Confirm is bound: grep for
  `LP_MENU` finds nothing in `src/` or `lib/`, and the comment at `:452` that
  mentions a KOReader-sync long-press looks stale.

### 2.7 Other formats

- **TXT**: the reader keeps `pageOffsets` (the file offset where each page
  starts, `TxtReaderActivity.h:18`). Searching the `.txt` file directly and
  mapping a hit with an upper-bound lookup on that vector would be exact and
  cheap (inferred).
- **XTC/XTCH**: pages are pre-rendered bitmaps (`lib/Xtc/README`). There is no
  text to search, so this format is out of scope.

---

## 3. Prior art: upstream and forks

- **Upstream issue #1984** (open, 2026-05-14): "Search book for text". The
  reporter's use is finding their place again after reading in print or
  listening to the audiobook.
- **Upstream PR #2451** (chongfun, opened 2026-06-27, **closed 2026-06-28, not
  merged**). Its design, taken from its `docs/search-architecture.md` at
  `d552131b`:
  - Each page in `sections/*.bin` gets a text record written after it: the
    page's words joined by single spaces. The page table gains a second offset
    per page. The section cache version was bumped, costing one extra copy of
    the rendered text plus 8 bytes per page.
  - The search starts at the current page and runs forward through the later
    spines, wrapping once. It returns the **first matching page**. Repeating
    the query means "find next". There is **no result list and no
    highlighting**.
  - Matching is streaming KMP over 64-byte SD reads, with the query capped at
    64 bytes. Case folding is ASCII only. Spaces and hyphens are ignored so a
    match survives a layout hyphen. The partial match carries across a page
    boundary within a spine.
  - A spine with no cache is laid out by the normal path (a blocking unit that
    cannot be cancelled). The scan moves one page per loop iteration. The reader
    releases its section before search starts.
  - Measured on upstream: flash +8,174 B, static RAM unchanged.
  - **Why it closed:** a maintainer wrote "the UX for typing text using the
    navigation buttons is just too slow to be practical", and suggested waiting
    for touch or Bluetooth-keyboard support. That objection does not apply to
    this fork's iOS app, where the host keyboard is live (section 2.4), and it
    still applies on the X3.
- **Forks**: 128 fetched refs across 8 remotes were checked (file names under
  `src/activities/**`, and grep for search/find in the reader and Epub code). No
  in-book search was found anywhere. Forks do carry dictionary
  word-selection activities (`DictionaryWordSelectActivity`,
  `EpubReaderWordLookupActivity`), which is a word cursor over page words. That
  UI pattern would suit a "which hit on this page" cursor. This repo had a
  `docs/dictionary.md` that the reader-menu removal deleted.
- **Not checked:** GitHub code search beyond upstream's issues and PRs. KOReader
  has search, but its engine (crengine, on hardware with megabytes of RAM) does
  not transfer.

---

## 4. The core choice: where the searched text comes from

Every phase has to pick one of these. It is the architectural decision.

| | A. Scan laid-out pages | B. Text record written at layout (PR #2451) | C. Scan cached XHTML, then fix up the position | D. Prebuilt inverted index |
|---|---|---|---|---|
| Text matches what is on screen | **Exactly** (the rendered words) | Exactly | Approximately: hidden CSS subtrees, alt text and entities differ (section 2.3) | Depends on its source |
| Needs the chapter laid out first | Yes | Yes | **No**, only for the chapter you jump to | Yes, at build time |
| Hit → page | Direct | Direct | Approximate word anchor, then verify on the landing page ±1 | Direct |
| Highlight rectangles | Free (`buildCapture`) | Needs a page load anyway | Needs a page load on landing | Needs a page load |
| Cache-format change | None | `SECTION_FILE_VERSION` 62→63, so **every cached book re-lays out once** | None | New file per book |
| Extra SD space | None | About 1x the rendered text | None | Postings; unbounded |
| Warm-scan read volume | About 3.1x the XHTML (whole pages) | About 1x the text (records only) | 1x the XHTML | Small |
| Heap per step | One page (one arena per line) | 64-byte buffer | 1 KB read buffer; +32 KB if the spine must be inflated | Index working set |
| Main risk | Scan speed on a big book | A cache bump (PR #2451 targeted v28; this fork is at v62, so it is a port, not a merge) | False hits and drift | RAM and size grow with the book; build cost on every open |

**Verdict per column (inferred):**

- **A** is the right start. It is exact, needs no format change, and turns into
  highlighting for free.
- **B** is an optimization worth doing only after A is shown to be too slow on
  hardware.
- **C** is the only route to whole-book search *without* laying out the whole
  book, so it is the genuine alternative (section 5.4).
- **D** is rejected. It costs RAM that grows with book length, which is the
  thing that crashes this device, and it charges every open for a feature most
  opens never use. PR #2451 rejected it for the same reasons.

---

## 5. Phased options

### Phase 1: Find in this chapter (recommended start). Size **M**

**What the user sees and presses**

1. In the reader, press Confirm. Chapter Select opens with a new top row,
   **Find…** (entry point to be decided, see section 6, question 1).
2. Press Confirm on the row. The usual text entry opens, prefilled with the last
   query. On iOS or Mac you type; on the X3 you use the grid or the daisywheel.
3. Commit. If the chapter is not fully laid out yet, the existing "Indexing"
   popup appears, the same one a percent jump used to show
   (`EpubReaderActivity.cpp:1095`-`:1117`).
4. A list appears: **"p. 12 — …the lighthouse keeper's…"**, one row per hit,
   capped at 50 with a "+N more" line, in page order.
5. Press Confirm on a row and the reader opens that page. Back returns to the
   list, then to the reader without moving.

**How it works (inferred design)**

- Matching runs over the reader's **own** `Section`. No second section and no
  second build context, so the section 2.5 double-residency problem cannot
  happen.
- Finish the current chapter's build. Then, for each page, call `loadPage(p)`,
  rebuild the page text with the grouping logic from `buildCapture` (advances
  are not needed for matching), and search it. Case folding uses
  `toLowerLatin`/`toLowerCyrillic`; soft hyphens and line-break hyphens are
  already rejoined by that logic.
- One page lives in memory at a time. A hit stores `{page, byteOffset, snippet}`.
- A phrase split across a page boundary: carry the last `len(query)−1` bytes of
  each page's text into the next page's search, as PR #2451 does.
- Jump with `pendingPageJump = hit.page`. The spine is unchanged and the
  pagination is unchanged, because the spec cannot change while the list is
  open.

**Code it touches**

- New: `src/activities/reader/EpubReaderFindActivity.{h,cpp}` (prompt, then
  scan, then list).
- New: `src/activities/reader/FindMatcher.h`, pure and host-testable, the same
  pattern as `ReadAloudCapture.h`.
- Edits: `EpubReaderChapterSelectionActivity.{h,cpp}` (the row),
  `EpubReaderActivity.{h,cpp}` (result handler, finish-build call),
  `ActivityResult.h` (`FindResult`), `lib/I18n/translations/english.yaml`
  (3 to 5 strings; the other languages fall back).
- Optional: delete the dead `getTextFromSectionFile`.

**Costs**

- RAM (estimate): hits are about 50 × (4 + 4 + 48) ≈ **3 KB**, plus one page's
  arenas (already the page-turn working set) and the query (≤64 B). Nothing
  stays allocated after the activity exits.
- Flash (estimate): **6–10 KB**. PR #2451's whole feature measured 8,174 B.
- Time on host: the finish-build is ≤16 ms for an ordinary chapter and 267 ms
  for the 5,816-page giant. The scan is one page load per page, and nobody has
  measured the host cost of a tight loop of loads; page turns (with
  saveProgress) measure 1–3 ms each (perf doc item 3).
- Time on device: **unmeasured**. The first thing to measure is
  `[EHP] Time to parse and build pages` (`ChapterHtmlSlimParser.cpp:3150`) plus
  a new per-scan `LOG_DBG` line, over `scripts/debugging_monitor.py`.
- SD writes: only the layout the reader would have done anyway.

**Risks**

- **Giant single-spine books**: "this chapter" means the whole book, and the
  finish-build is the partial-extension rebuild from byte 0 (perf doc,
  remaining item 4). It needs a Back-to-cancel between build chunks and a
  progress line (`estimatedTotalPages()`, `Section.h:118`).
- The heap during the finish-build is the same as reading to the end of the
  chapter, so it is **no worse than reading**, but it arrives all at once.
- The hit cap must be enforced, or `vector` growth aborts under
  `-fno-exceptions` (skill "Resource Protocol" 7 and 9).

**Headless test**

1. **Host gtest** `test/find_in_chapter`, from the `test/chapter_jump` template
   (real `.epub`, then `Section::startBuild`, then pages). It asserts every
   reported hit's page text contains the query, and that a planted phrase split
   across a page boundary and one broken by a hyphen are both found. A
   no-match query must return zero hits.
2. **Simulator script**: open a book, `CONFIRM` (Chapter Select), `CONFIRM` on
   the Find row, `TYPE:lighthouse\n`, screenshot the list, `CONFIRM`,
   screenshot the page. Confirm the landing activity by grepping
   `[ACT] Entering activity:` (`docs/headless-qa.md`). Confirm the landing page
   with `CROSSPOINT_SIM_READALOUD_LOG=2`, which dumps the displayed page's full
   text: assert the query appears in it. That gives a text oracle rather than
   eyeballing a screenshot.

### Phase 2: Whole book. Size **L**

**What the user sees**

Same prompt. The list fills while a progress line reads "Searching… 34%" (by
book position, from `Epub::calculateProgress`, `Epub.cpp:1076`). Rows are
grouped by chapter title. Back cancels, keeping the hits found so far. A
first-hit mode (open the first hit forward of here, wrap once) is a variant the
owner can choose instead of a list.

**How it works (inferred)**

- Before scanning, the reader parks its build: `suspendBuild()`
  (`Section.h:125`) saves progress as a partial and frees the CSS map and build
  context. The Find activity then drives one reusable `Section` spine by spine,
  forward from the current spine with a single wrap.
- It uses `SETTINGS.readerRenderSpec(w, h)` (`EpubReaderActivity.cpp:1041`), so
  every section it lays out is the reader's own cache and makes later reading
  faster.
- It stays cooperative: one build chunk or N page scans per `loop()` tick, and
  the existing heap gates (`buildTickHeapGate`, `EpubReaderActivity.h:127`)
  apply between chunks.
- A hit stores `{spine, page}`. The jump sets `currentSpineIndex` and
  `pendingPageJump`, then calls `section.reset()`.

**Code it touches**: everything in Phase 1, plus the reader's suspend-and-resume
around the sub-activity, plus a chapter-title lookup for the grouping.

**Costs**

- Cold search = **laying out the whole book**.
  - Host (estimate): about 24 × 16 ms ≈ 0.4 s for the 24-chapter
    `measure.epub`.
  - Device: unmeasured. It could plausibly be tens of seconds to minutes on a
    large book (**estimate, no basis beyond the host-to-device clock ratio**).
  - SD (estimate): about 3.1x the book's XHTML is written, roughly 3 MB for a
    1 MB book.
- Warm search: reads about 3.1x the XHTML from SD.

**Risks**

- Cold latency on the X3.
- A layout that runs as one blocking unit cannot be cancelled mid-chapter, so
  cancel waits for the current build chunk.
- Layout in a pushed activity depends on the reader font staying resident
  (inferred, not verified).
- The owner's heap crashes: this is the phase most likely to reproduce them,
  because it runs the build path back to back across the whole book.

**Headless test**: the Phase 1 gtest extended across spines (a hit in a later
and in an earlier spine, a wrap, cancel). Simulator: the same script on a
multi-chapter fixture, plus `CROSSPOINT_SIM_LOG_TIMING=1`-style logging of the
scan time per spine.

**Phase 2b (optional, only if 2 measures too slow on device)**: adopt approach B
(the per-page text records) to make warm scans read about 1x the text instead
of about 3.1x. It costs a `SECTION_FILE_VERSION` bump, so every cached book
re-lays out once. Size **M** on top of Phase 2.

### Phase 3: Highlight, next and previous, remembered query. Size **M**

**What the user sees**

- The landing page underlines (or boxes) every occurrence of the query.
- A slim status line reads "3 / 17". Two controls step to the next or previous
  hit, moving across pages and chapters.
- The last query is prefilled next time, per book.

**How it works (inferred)**

- `renderContents` (`EpubReaderActivity.cpp:1672`) already has the page. Running
  `buildCapture` there, measuring advances with the font resident, gives
  `byteOffset → rects`. Drawing is `drawLine`/`drawRect`
  (`GfxRenderer.h:452`-`:456`), and these compose in dark mode because they go
  through the renderer's inversion.
- The query persists in `<bookcache>/find.txt`, written only when it changes
  (skill "Resource Protocol" 8).

**The blocker is buttons, not code.** Every reader button is already taken:

| Button | Current use |
|---|---|
| Front Left/Right | Turn pages |
| Side pair | Font size (tap), font family (hold) |
| Confirm | Chapter Select |
| Confirm + side | Line spacing |
| Back | Home or footnote return |

So next and previous need a mode ("while a query is active, the side pair steps
hits") or a gesture binding on iOS. That is an owner ruling (section 6,
question 3).

**Headless test**: a gtest on the pure offset-to-rectangle mapping. A simulator
capture with a pixel diff against the same page without a query, cropped to the
hit, following the proof-figure rules in the simulator's `CLAUDE.md`. Next and
previous scripted with QTAP.

### 5.4 Alternative: search the cached XHTML, verify on landing. Size **M–L**

This replaces Phase 2's whole-book layout with a text-only pass. It is the
genuinely different route.

- **Scan**: stream `<bookcache>/html/<spine>.html` (or inflate it, if that
  spine was never opened) through a small tag stripper that honors the parser's
  `SKIP_TAGS`, decodes entities with the existing `htmlEntities`, NFC-composes,
  and counts non-space bytes. That count is the same unit the word anchor uses.
- **Hit → position**: the byte count at the hit is an *approximate* word anchor.
- **Jump**: lay out only that chapter, up to `getPageForWordAnchor(approx)`.
  Then search that page ±1 with the exact Phase 1 matcher and settle on the
  exact page.
- **Pros**: no whole-book layout, no SD writes, reads 1x the XHTML. A cold
  whole-book search costs one pass over the text.
- **Cons**:
  - The anchor drifts wherever the parser adds text (alt text, header text,
    list markers) or skips it (`display:none`, which needs CSS to see, and the
    stripper would not have it). So the stripper can report **false hits in
    hidden text** that the verify step then fails to find.
  - It adds a second tokenizer that has to track the real one.
  - Snippets show source text, not rendered text.
- **When to prefer it**: if Phase 2's cold layout measures unacceptably slow on
  the X3 and the owner wants whole-book search anyway.

### 5.5 Small add-on: TXT books. Size **S**

Scan the `.txt` file in chunks, map each hit with an upper-bound lookup on
`pageOffsets` (`TxtReaderActivity.h:18`), and jump. It is exact, needs no
layout, and reuses the same prompt, matcher and list. It can ride with any
phase.

---

## 6. Owner decisions, in the order they are needed

The first question depends on seeing how each entry point looks, so it needs a
rendered mockup before it is asked. It is recorded here only as the list.

1. **Entry point.** (a) a "Find…" row at the top of Chapter Select (the
   book-notes row is the precedent); (b) an iOS gesture action only; (c) both.
   The reader-menu removal ruling ("the book is the whole surface") argues
   against anything that puts chrome over the page.
2. **Scope for the first build.** Phase 1 alone, or Phase 1 plus 5.5 (TXT).
3. **Phase 3 controls** (only when Phase 3 is reached): which buttons step
   hits.
4. **Phase 2 route** (only when Phase 2 is reached): lay out everything (A),
   text records (B), or the XHTML scan (C).

---

## 7. Checked and found absent, or clean

- **No search code in this fork.** No `Search`/`Find` activity, and no matcher
  in `src/` or `lib/`. The fork's git history has no search commit (the
  `--grep=search` hits are unrelated: OPDS, fonts, plugins).
- **No plain-text sidecar** for EPUB anywhere in the cache layout (section 2.1).
- `Section::getTextFromSectionFile()` is **dead**: no callers.
- `pendingPercentJump` is **unreachable**: no setter since `9494d88e8`.
- **No `LP_MENU` / long-press-Confirm binding** remains in `src/`.
- `lib/Utf8` has **no case folding**. The hyphenation helpers do.
- **Upstream**: nothing merged. Issue #1984 is open, PR #2451 is closed. Forks:
  none found.
- **The word anchor is layout-invariant, confirmed at the source** (every
  fragment of one `addWord()` call shares one offset, `ParsedText.cpp:521`-`:536`).
  It is the right key for a hit that has to survive a font change. Pages are
  not.

## 8. Top risks

1. **Cold whole-book latency on the X3 is unknown.** Every timing is from the
   host. Measure a single chapter's build time on the device before committing
   to Phase 2 or to route A.
2. **Heap.** The owner's device already crashes from heap exhaustion in
   ordinary reading. Any phase that lays out chapters while the reader's build
   context is resident doubles the peak, so Phase 2 must suspend the reader's
   build first. Phase 1 avoids the problem by using the reader's own section.
3. **Input on the X3.** Typing a query on the 13-grid is slow. That is the exact
   reason upstream closed PR #2451. On iOS and Mac the host keyboard removes
   it, so the feature's value is uneven across the owner's devices.

---

## 8. Owner, 2026-09-28: what Find is FOR

Asked whether the first build should be Phase 1 alone or Phase 1 plus TXT, the owner answered: *"this is mostly for finding within the book where I left off, do better"*.

That reframes this whole document:
- **The need is getting back to a lost reading position, not general search.**
- Phase 1 as scoped (search the CURRENT chapter) misses it, because a lost place is usually in another chapter.
- The next step is to establish WHY the position gets lost: book updates, cache resets, reading on the other device, accidental jumps. The answer may be that the position should never be lost, or should follow the reader between devices, with text search as the fallback.
- The investigation and the options that follow from it are appended below as they land. Do not start Phase 1 as written.
