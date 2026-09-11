# Reading-path and font heap budget, 2026-09-10

Measured because the owner reports the device **crashes regularly in daily
use**, and both crash reports on his card are heap exhaustion during ordinary
reading rather than anything crafted.

Method: counting-allocator harnesses over the **real firmware sources** — one
driving `Epub`/`Section`/`ChapterHtmlSlimParser`/`ParsedText`/`TextBlock`/`Page`/
`CssParser` over **all 90 EPUBs on the owner's card**, one driving the real
`SdCardFontRegistry`/`SdCardFontManager`/`SdCardFont`/`GfxRenderer` over his
card's `/.fonts` and a real 1,234-paragraph book. Load-bearing type sizes were
re-checked with the **actual device toolchain** (`riscv32-esp-elf-g++
-march=rv32imc`), so they transfer.

**Limits, stated up front.** The host allocator is not ESP-IDF's `multi_heap`,
so these measure *sizes, counts and lifetimes* — not fragmentation. The host
harness counts `operator new` only, so `malloc` users (`InflateStream`'s 32 KB
window, `PixelCache`'s band) do not appear. `std::deque`'s node is 4080 on the
host and ~512 on device; it has been excluded from every device claim.

## The headline: there is no leak

**4,445 consecutive page loads → 0 bytes drift. Six full open/read/close cycles
→ 0 bytes residue.** A 90-book sweep produced exactly one non-zero result, 96
bytes, a one-shot lazy init. A parallel read-only survey of the reader, Epub,
Memory, Serialization, ZipFile, miniz and InflateReader found no unbounded
container, no `shared_ptr` cycle and no retaining lambda capture.

**Retire "a slow leak" as the explanation.** The problem is residency and block
size, not accumulation.

## Where the memory actually goes

### The floor: the CSS rule map, resident for the whole chapter

`Section::startBuild` loads the book's entire stylesheet and clears it only at
finalize/suspend/abandon. The build does **not** finalize when the reader
reaches page 1 — it parks once the watermark is 5 pages ahead — so the map is
resident from page 0 until the reader nears the end of the chapter.

MEASURED, on his own books:

| book | rules | map resident | peak during a build chunk |
|---|---|---|---|
| Procrastination | 462 | **63,904 B** | 118,157 B |
| The Urban Homestead | 461 | 63,744 B | 107,747 B |
| Atlas of the Heart | 289 | 40,864 B | 77,518 B |
| Worlds Beyond Number x9 | 86 | 12,320 B | 52-57 KB |
| typical Gutenberg | 21-36 | 3,056-5,520 B | 31-53 KB |

Device arithmetic agrees to within 1%: ~140 B per rule x 462 = 64.7 KB. So on
three books already on his card, **~64 KB — 17% of total RAM — sits in a hash
map for the entire reading session.** `MAX_RULES` is 1500; at that cap the map
would be 210 KB, and nothing enforces a *byte* budget.

### One loaded SD font, his configuration (Edgar 18 pt)

| component | bytes |
|---|---|
| mini glyph/bitmap/kern arenas | 39,122 |
| kern class + ligature + ASCII shortcut | 10,757 |
| measure-kern rows | 13,212 |
| advance tables | 1,512 |
| intervals (4 styles) | 624 |
| **total** | **65,227** |

**Regular-only is 21,664.** So the two rarely-used styles cost **43,563 bytes —
11.5% of the device's RAM — to serve one paragraph in thirteen.** Exactly one SD
font is resident while reading (MEASURED: none of his six families has CJK
coverage, so the 8/10/12 pt fallback companions never load), and **nothing
evicts on pressure**.

### Page turn on a finalized chapter is cheap

Resident between turns 1,684-6,176 B; one `Page` in hand 5,773-6,161 B;
**largest single request 320 B**, because `TextBlock` flattens a line into one
nothrow arena. This path is not the problem.

## Largest contiguous requests, ranked (device bytes)

| # | request | gated on max-alloc? | throws? |
|---|---|---|---|
| 1 | `sizeof(PNG)` = **45,604**, per image | **was free-heap only — FIXED** | no |
| 2 | pixel-cache slot, 16,384 x6 | yes | no |
| 3 | `sizeof(JPEGDEC)` = **17,884**, per image | **was free-heap only — FIXED** | no |
| 4 | `anchorData` doubling, to **28,672** (43,008 live across the copy) | none — **FIXED** | **YES → abort** |
| 5 | `Section` LUT doubling, 4,608 on his card … 73,728 worst case | none | **YES → abort** (B-070) |
| 6 | mini bitmap arena, 10,688 | n/a | no |

`sizeof(PNG)` and `sizeof(JPEGDEC)` are MEASURED with `riscv32-esp-elf-nm`
against the patched libdeps the firmware actually builds — the source comments
("~42 KB", "20 KB") are both wrong.

## The amplifier: what an OOM costs on the font path

MEASURED over 20 real paragraphs, Edgar 16 pt:

| | allocations | bytes | SD glyph loads |
|---|---|---|---|
| advance table present | 0 | 0 | 0 |
| advance table absent | **2,829** | **190,789** | **1,415** |

A refused ~1 KB codepoint buffer converts that block's measurement into ~140
small allocations and ~70 file opens, and nothing cached the failure — so a page
retried it once per text block. **That amplification is how a small font-path
OOM becomes an abort somewhere else a few hundred milliseconds later**, which is
the B-040 shape. Fixed by latching the failure per cache generation.

## Two exact-size realloc ladders (font path)

- `mergeIntoAdvanceTable`: 54 reallocations per book, 256 → 488 B, 22 KB churned.
- `loadMeasureKernRows`: **49 reallocations per book, 2.3 KB → 4.5 KB blocks,
  170 KB churned** — mid-size blocks, the worst class for the
  largest-contiguous metric, planted at unpredictable moments mid-read.

Both are the anti-pattern `SdCardFont.h`'s own arena comment says was fixed for
the arenas. **Not fixed here**: an attempt at capacity-tracking for the advance
table was written and reverted the same session — reusing the block would have
left the recorded capacity larger than the allocation, and an in-place backward
merge would silently change *which* entries are dropped at the cap (tail today,
front then). The measured win (22 KB of the two) did not justify that risk in a
hot path. `loadMeasureKernRows` is the larger one and is still open.

## OOM escalation on the reading path

An OOM inside `Page::deserialize` correctly returns null — and
`EpubReaderActivity::render` then treats it as a **corrupt cache**: abandons the
build, **deletes the section `.bin`**, resets the section and re-renders, which
re-runs `startBuild`, reloads the 64 KB CSS map and re-paginates from page 0, up
to three times. So a transient OOM permanently destroys that chapter's cached
pagination and immediately demands more memory than the allocation that failed.
Same shape as B-052's escalation, different path. **Not fixed** — the fix needs
to distinguish "allocation failed" from "file is corrupt", which is an owner
decision about what to do with a cache that might be fine.

## CLEAN — checked and bounded, do not re-derive

Page-turn path (largest request 320 B, no in-RAM tables, one `Page` alive at a
time, RAII pixel-cache release on every exit). `ChapterHtmlSlimParser`'s other
containers all capped (`pendingLines_` ≤ 3, table buffers, label stacks, fixed
`char[201]`). `BookMetadataCache`'s vectors clear-then-reserve.
`ZipFile::fileStatSlimCache` never grows in practice. `InflateStream` /
`InflateReader` init-deinit pairing. `BuildScratch` backed by the framebuffer.
The full font-path CLEAN list — overflow LRU bounded at 16 slots, no full kern
matrix ever allocated (max mini matrix 1,800 B vs the 9,546 B the log implies),
keep-if-fits working (7 arena reallocations per book, not per page), one font
resident, hi-res companions simulator-only, `FontInstaller` containing no
allocation at all — is recorded here rather than re-measured.

**Disproved before reporting:** a file-handle leak in `onGlyphMiss` (three early
returns skip `close()`, but `HalFile`'s destructor closes under `StorageLock`);
`anchorData` as the source of the 73,728-byte request (ruled out on element
size); the 4,080-byte deque node (host artifact).

## Corrections to earlier entries

- **B-040's 16,384-byte line cannot recur.** Both archived reports predate the
  2026-08-28 fix (`crash_1.txt` is 1.5.9-BD, `crash_0.txt` is 1.5.0-BNY; the
  tree is 1.5.33-BD). The first request is now 1,032 bytes. It is evidence about
  heap state, not an open bug.
- **B-070's severity was understated in one direction and overstated in the
  other.** The worst-case LUT request is 73,728 B, not "multi-KB" — but his
  card's real chapters have a median of 18 pages and exactly one above 96, so it
  tops out at 4,608 B in his actual use. Worth fixing for correctness; not the
  daily driver.
- **`archiveCrashReport` only runs on `isRebootFromPanic()`.** A watchdog reset,
  a brownout, or a hang the owner power-cycles writes **nothing**. If the device
  crashes regularly and the archive still holds two indices, the recent failures
  may not be panic reboots at all — worth one device check.
