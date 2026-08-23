#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "EpdFont.h"
#include "EpdFontData.h"

// On-disk binary format version for .cpfont files. Kept as a preprocessor
// macro with no integer suffix so it can be stringified into URLs or labels
// (`4U` would stringify as "4U"); the on-device download UI that did so was
// removed 2026-08-08.
//
// The canonical version for the build tooling lives in
// lib/EpdFont/scripts/cpfont_version.py. This firmware-side copy must be
// bumped manually when the firmware is updated to support a new format.
// Reader enforcement: SdCardFont::load().
#define CPFONT_VERSION 4

class SdCardFont {
 public:
  static constexpr uint16_t MAX_PAGE_GLYPHS = 512;
  static constexpr uint8_t MAX_STYLES = 4;

  SdCardFont() = default;
  ~SdCardFont();
  // Owns raw buffers freed in dtor — no shallow-copy semantics. Make any
  // accidental pass-by-value or move a compile-time error.
  SdCardFont(const SdCardFont&) = delete;
  SdCardFont& operator=(const SdCardFont&) = delete;
  SdCardFont(SdCardFont&&) = delete;
  SdCardFont& operator=(SdCardFont&&) = delete;

  // Load .cpfont file: reads header + intervals into RAM, records file layout offsets.
  // Supports v4 (multi-style) format.
  // Returns true on success.
  bool load(const char* path);

  // Pre-read glyphs needed for the given UTF-8 text from SD card.
  // styleMask: bitmask of styles to prewarm (bit 0=regular, 1=bold, 2=italic, 3=bolditalic).
  // Default 0x0F = all present styles.
  // When metadataOnly=true, only glyph metrics are loaded (no bitmap data).
  // Returns number of glyphs that couldn't be loaded (0 on full success).
  int prewarm(const char* utf8Text, uint8_t styleMask = 0x0F, bool metadataOnly = false);

  // Build a compact advance-only table for layout measurement.
  // Extracts ALL unique codepoints from words (no MAX_PAGE_GLYPHS cap),
  // batch-reads advanceX from SD, stores in a sorted per-style table.
  // extraText: optional additional codepoints to warm in the same SD pass
  // (e.g. shaped Arabic presentation forms the measurement path will look up).
  // Returns number of codepoints not found in font coverage.
  int buildAdvanceTable(const char* utf8Text, uint8_t styleMask = 0x0F, const char* extraText = nullptr);
  int buildAdvanceTable(const std::deque<std::string>& words, bool includeHyphen, uint8_t styleMask = 0x0F,
                        const char* extraText = nullptr);

  // Look up advanceX for a codepoint from the advance table.
  // Returns the 12.4 fixed-point advance, or 0 if not found.
  uint16_t getAdvance(uint32_t codepoint, uint8_t style) const;

  // Returns true if advance table is populated for at least one style.
  bool hasAdvanceTable() const;

  // Layout-time kern lookup (2026-08-22, punctuation-kerning audit P0).
  // Returns the 4.4 fixed-point kern for a codepoint pair, read from full
  // kern-matrix ROWS loaded beside the advance table (loadMeasureKernRows) —
  // the same on-disk cells drawText's per-page mini matrix is built from, so
  // measure and draw agree. `styleIdx` is a resolved style INDEX. Returns 0
  // for any pair whose row is not resident, which is the pre-2026-08-22
  // measurement behavior.
  int8_t getMeasureKern(uint32_t leftCp, uint32_t rightCp, uint8_t styleIdx) const;

  // Free mini data for all styles and restore stub EpdFontData.
  // Preserves the persistent advance cache so repeated layout passes can reuse
  // previously fetched metrics.
  void clearCache();

  // Drop the persistent advance cache. Call when unloading the SD font or
  // when font/size/family/glyph-table state changes.
  void clearPersistentCache();

  // Release every rebuildable cache while keeping the font loaded and usable:
  // mini glyph/kern arenas, kern/ligature class tables, the overflow ring, and
  // the persistent advance tables. Coverage intervals stay so hasCodepoint()
  // and reloads keep working; glyphs fault back in on demand and the next
  // prewarm rebuilds the arenas. For heap-critical transitions (e.g. starting
  // WiFi + the web server), where retained font data is the difference between
  // a clean start and an OOM abort.
  void releaseResidentCaches();

  // Returns pointer to the managed EpdFont for a given style.
  // Returns nullptr if the style is not present.
  EpdFont* getEpdFont(uint8_t style = 0);

  // Returns true if the given style is present in this font file.
  bool hasStyle(uint8_t style) const;

  // Resolve requested style bits to the closest present style.
  uint8_t resolveStyle(uint8_t style) const;

  // Resolve every requested style bit through fallback and return the actual
  // styles that need cache/advance preparation.
  uint8_t resolveStyleMask(uint8_t styleMask) const;

  // Number of styles present in this font file.
  uint8_t styleCount() const { return styleCount_; }

  // Returns true if the glyph pointer points into the overflow buffer.
  // On-demand (overflow) glyph loads since the last cache clear. Each one is a
  // .cpfont open+seek+read. A healthy prewarmed render leaves this at zero; a
  // number climbing into the thousands means the drawn working set exceeds
  // OVERFLOW_CAPACITY and the LRU ring is thrashing. The 1-in-256 summary in
  // glyphMissHandler reports the same value.
  uint32_t overflowLoadsSinceClear() const { return overflowLoadsSinceClear_; }

  bool isOverflowGlyph(const EpdGlyph* glyph) const;

  // Returns the bitmap for an on-demand-loaded (overflow) glyph.
  const uint8_t* getOverflowBitmap(const EpdGlyph* glyph) const;

  // Extract SdCardFont* from an opaque glyphMissCtx pointer.
  // Used by GfxRenderer::getGlyphBitmap() to recover the SdCardFont from EpdFontData::glyphMissCtx.
  static SdCardFont* fromMissCtx(void* ctx);

  struct Stats {
    uint32_t prewarmTotalMs = 0;
    uint32_t sdReadTimeMs = 0;
    uint32_t seekCount = 0;
    uint32_t uniqueGlyphs = 0;
    uint32_t bitmapBytes = 0;
  };
  void logStats(const char* label = "SDCF");
  void resetStats();
  const Stats& getStats() const { return stats_; }

  // Content hash of the file header + style TOC entries (computed during load).
  // Used to generate deterministic font IDs for section cache invalidation.
  uint32_t contentHash() const { return contentHash_; }

 private:
  // Per-style metadata (parsed from file header/TOC)
  struct CpFontHeader {
    uint32_t intervalCount = 0;
    uint32_t glyphCount = 0;
    uint8_t advanceY = 0;
    int16_t ascender = 0;
    int16_t descender = 0;
    bool is2Bit = false;
    uint16_t kernLeftEntryCount = 0;
    uint16_t kernRightEntryCount = 0;
    uint8_t kernLeftClassCount = 0;
    uint8_t kernRightClassCount = 0;
    uint8_t ligaturePairCount = 0;
  };

  // All per-style data: file offsets, intervals, kern/lig, prewarm cache, EpdFont
  struct PerStyle {
    CpFontHeader header{};

    // File layout offsets for this style's data sections
    uint32_t intervalsFileOffset = 0;
    uint32_t glyphsFileOffset = 0;
    uint32_t kernLeftFileOffset = 0;
    uint32_t kernRightFileOffset = 0;
    uint32_t kernMatrixFileOffset = 0;
    uint32_t ligatureFileOffset = 0;
    uint32_t bitmapFileOffset = 0;

    // Full intervals loaded from file (kept in RAM for codepoint lookup)
    EpdUnicodeInterval* fullIntervals = nullptr;
    EPD_PACKED_BEGIN
    struct BmpInterval16 {
      uint16_t first;
      uint16_t last;
      uint16_t offset;
    } EPD_PACKED_ATTR;
    EPD_PACKED_END
    static_assert(sizeof(BmpInterval16) == 6, "BmpInterval16 must remain compact");
    BmpInterval16* bmpIntervals = nullptr;
    bool intervalsAreBmp16 = false;

    // Persistent kern-class + ligature tables (lazy-loaded on first prewarm).
    // The full kern MATRIX is NOT resident — on Literata-class fonts a single
    // style's matrix is ~36-42KB contiguous, and 4 styles' worth won't fit
    // alongside bitmaps + framebuffer on a 380KB device. Only kernLeftClasses
    // and kernRightClasses (small codepoint→classId tables, ~3KB each) stay
    // resident; the matrix is reconstructed per-page as miniKernMatrix.
    EpdKernClassEntry* kernLeftClasses = nullptr;
    EpdKernClassEntry* kernRightClasses = nullptr;
    EpdLigaturePair* ligaturePairs = nullptr;
    bool kernLigLoaded = false;

    // Direct-mapped left/right class for U+0000-U+007F: 128 left bytes then 128
    // right bytes, filled from the two tables above the moment they load.
    // getMeasureKern runs a binary search of each table for EVERY adjacent
    // character pair the layout measures -- ~2.5M pairs on a novel, and 28% of
    // the pagination build's CPU once the page writes were buffered. ASCII is
    // what an English book is made of, so the shortcut covers nearly all of it;
    // anything above U+007F still takes the search. Heap-allocated (256 B per
    // style, freed with the class tables) rather than inline, so a font that
    // carries no kern data costs a pointer. nullptr = fall through to the
    // search, which is also the allocation-failure path.
    uint8_t* kernClassAscii = nullptr;

    // Measure-time kern rows (2026-08-22, punctuation-kerning audit P0): full
    // kern-matrix ROWS (kernRightClassCount bytes each) for the LEFT classes
    // the advance-table codepoints reach, loaded by loadMeasureKernRows on the
    // same pass that builds the advance table and with the same lifetime
    // (clearPersistentCache frees them). Original class IDs, no renumbering:
    // lookups go through the resident kernLeftClasses/kernRightClasses tables,
    // so a row loaded once serves every later paragraph. Bounded: worst case
    // is kernLeftClassCount rows (Edgar, the richest shipped face: 36 × 48 =
    // 1.7 KB per style) and MEASURE_KERN_ARENA_LIMIT caps pathological fonts.
    uint8_t* measureKernRowClasses = nullptr;  // sorted original left-class ids
    int8_t* measureKernRows = nullptr;         // rowCount × kernRightClassCount
    uint16_t measureKernRowCount = 0;
    uint16_t measureKernRowCapacity = 0;

    // Stub EpdFontData returned when not prewarmed
    EpdFontData stubData{};

    // Mini EpdFontData built during prewarm. Buffers are kept-if-fits across pages
    // (capacities below track allocated sizes): freeing and reallocating slightly
    // different sizes on every page turn was a primary heap fragmenter — each page's
    // freed hole rarely fit the next page's need, so maxAlloc eroded all session.
    // The per-render PrewarmScope calls clearCache() -> resetStyleMiniData(), which
    // keeps both the allocations AND the loaded data. Buffers: reuse means
    // ensureArrayCapacity early-returns once capacities converge on the book's
    // max, so page turns stop touching the allocator (the free/realloc-per-page
    // pattern was a primary heap fragmenter). Data: the next prewarm
    // subset-checks against the resident tables (see prewarmStyle), so the idle
    // prewarm of page N+1 serves the actual turn with zero SD reads. Retention
    // is bounded two ways in resetStyleMiniData(): a heap floor frees outright
    // under pressure, and sustained underuse (an outlier page's oversized bitmap
    // arena) frees after a few consecutive low-use rebuilds. freeStyleMiniData()
    // remains the full teardown (zeroes capacities) for style eviction / font
    // unload.
    EpdFontData miniData{};
    EpdUnicodeInterval* miniIntervals = nullptr;
    EpdGlyph* miniGlyphs = nullptr;
    uint8_t* miniBitmap = nullptr;
    uint32_t miniIntervalCount = 0;
    uint32_t miniGlyphCount = 0;
    uint32_t miniIntervalCapacity = 0;
    uint32_t miniGlyphCapacity = 0;
    uint32_t miniBitmapCapacity = 0;
    // Bitmap bytes the current page actually used (set by prewarmStyle), the
    // underuse-hysteresis signal; 0 = no bitmap built this scope (metadata-only
    // prewarm), which leaves the hysteresis counter untouched.
    uint32_t miniBitmapUsed = 0;
    uint8_t miniUnderuseRuns = 0;
    // True when the resident mini was built metadata-only (no bitmaps): it can
    // serve metadata requests but a full render request must rebuild.
    bool miniMetadataOnly = false;
    // Set by a rebuild, consumed by resetStyleMiniData: gates the underuse
    // hysteresis to one evaluation per rebuild (scopes reset twice, and subset
    // hits load nothing new to judge).
    bool miniHysteresisPending = false;

    // Per-page mini kern matrix (built by buildMiniKernMatrix on each full
    // prewarm). miniKernLeftClasses/miniKernRightClasses map ONLY the codepoints
    // used on the current page to renumbered class IDs (1..miniKern*ClassCount).
    // miniKernMatrix is a small miniKernLeftClassCount × miniKernRightClassCount
    // flat matrix. Typical Latin page: ~25×25 matrix = ~625 bytes per style vs
    // ~36KB for the full Literata matrix — ~50× reduction.
    EpdKernClassEntry* miniKernLeftClasses = nullptr;
    EpdKernClassEntry* miniKernRightClasses = nullptr;
    uint16_t miniKernLeftEntryCount = 0;
    uint16_t miniKernRightEntryCount = 0;
    uint8_t miniKernLeftClassCount = 0;
    uint8_t miniKernRightClassCount = 0;
    int8_t* miniKernMatrix = nullptr;
    // Kept-if-fits capacities, same rationale as the mini glyph buffers above.
    uint16_t miniKernLeftCapacity = 0;
    uint16_t miniKernRightCapacity = 0;
    uint32_t miniKernMatrixCapacity = 0;

    // The EpdFont whose data pointer we manage
    EpdFont epdFont{&stubData};

    bool present = false;
  };

  PerStyle styles_[MAX_STYLES] = {};
  uint8_t styleCount_ = 0;

  char filePath_[128] = {};

  // Overflow context: glyphMissHandler needs to know which style it's serving
  struct OverflowContext {
    SdCardFont* self;
    uint8_t styleIdx;
  };
  OverflowContext overflowCtx_[MAX_STYLES] = {};

  // Shared on-demand overflow buffer (glyphs loaded via glyphMissHandler).
  //
  // Eviction is LRU, not round-robin: with round-robin reuse a working set one
  // glyph larger than the cache degenerated to a miss on every lookup (the slot
  // reused was always the one needed next), so a single un-prewarmed UI string
  // re-read the same handful of letters from SD hundreds of times per render.
  // LRU guarantees any working set <= OVERFLOW_CAPACITY loads each glyph once.
  //
  // Capacity 16 covers typical short UI strings (a filename title is ~10-14
  // unique glyphs plus the ellipsis). Per-slot cost on the 32-bit target:
  // 16 (EpdGlyph) + 4 (bitmap ptr) + 4 (codepoint) + 4 (lastUse) + 1 (+3 pad)
  // = 32 bytes in-object, 512 bytes total, plus one heap bitmap per occupied
  // slot (~65 B/glyph at 12pt, ~144 B at 18pt for a 2-bit font), freed on
  // every clearOverflow(). Longer text must go through prewarm() instead —
  // this cache is a safety net for stragglers, not a rendering path.
  static constexpr uint32_t OVERFLOW_CAPACITY = 16;
  struct OverflowEntry {
    EpdGlyph glyph;
    uint8_t* bitmap = nullptr;
    uint32_t codepoint = 0;
    uint32_t lastUse = 0;  // LRU stamp from overflowUseTick_ (0 = never used)
    uint8_t styleIdx = 0;
  };
  OverflowEntry overflow_[OVERFLOW_CAPACITY] = {};
  uint32_t overflowCount_ = 0;
  uint32_t overflowUseTick_ = 0;
  // Loads since the last clearOverflow(). Drives the log throttle in
  // onGlyphMiss(): per-load logging is capped, then only a periodic summary,
  // so a thrashing caller can no longer flood the log.
  uint32_t overflowLoadsSinceClear_ = 0;

  // Compact advance-only table for layout measurement (per-style).
  // Built by buildAdvanceTable(), queried by getAdvance().
  struct AdvanceEntry {
    uint32_t codepoint;
    uint16_t advanceX;  // 12.4 fixed-point
  };
  // Per-style advance table. Sorted by codepoint for binary lookup.
  // Bounded to ADVANCE_CACHE_LIMIT entries; persists across layout passes
  // (across calls to clearCache()) so repeated indexing of the same font
  // amortizes SD reads. Cleared only on font unload or clearPersistentCache().
  static constexpr uint32_t ADVANCE_CACHE_LIMIT = 768;
  AdvanceEntry* advanceTable_[MAX_STYLES] = {};
  uint32_t advanceTableSize_[MAX_STYLES] = {};
  bool advanceTableLookup(uint8_t styleIdx, uint32_t codepoint, uint16_t* outAdvance) const;
  // Merge sortedNew (sorted by codepoint, no overlap with existing) into the
  // advance table for styleIdx, preserving sort order; cap-truncates the tail.
  void mergeIntoAdvanceTable(uint8_t styleIdx, const AdvanceEntry* sortedNew, uint32_t newCount);

  Stats stats_;
  uint32_t contentHash_ = 0;
  bool loaded_ = false;

  // Per-style helpers
  void freeStyleMiniData(PerStyle& s);
  // Per-scope variant: drop the page's data, keep the allocations (see the
  // PerStyle comment). May escalate to freeStyleMiniData under heap pressure
  // or sustained underuse.
  void resetStyleMiniData(PerStyle& s);
  void freeStyleAll(PerStyle& s);
  void freeStyleKernLigatureData(PerStyle& s);
  void freeStyleMiniKern(PerStyle& s);
  // Measure-kern rows: hard cap on the per-style row arena. The shipped SD
  // faces top out under 2 KB; a pathological font beyond this simply measures
  // its overflow pairs unkerned (the pre-2026-08-22 behavior).
  static constexpr uint32_t MEASURE_KERN_ARENA_LIMIT = 16 * 1024;
  void freeStyleMeasureKern(PerStyle& s);
  void loadMeasureKernRows(PerStyle& s, const uint32_t* codepoints, uint32_t cpCount);
  bool loadStyleKernLigatureData(PerStyle& s);
  bool buildMiniKernMatrix(PerStyle& s, const uint32_t* codepoints, uint32_t cpCount);
  void applyKernLigaturePointers(PerStyle& s, EpdFontData& data) const;
  void applyGlyphMissCallback(uint8_t styleIdx);
  int32_t findGlobalGlyphIndex(const PerStyle& s, uint32_t codepoint) const;
  int fetchAdvancesForCodepoints(uint32_t* codepoints, uint32_t cpCount, uint8_t styleMask);
  template <typename Iter>
  int buildAdvanceTableRange(Iter begin, Iter end, bool includeSpace, bool includeHyphen, uint8_t styleMask,
                             const char* extraText = nullptr);
  int prewarmStyle(uint8_t styleIdx, const uint32_t* codepoints, uint32_t cpCount, bool metadataOnly);

  // Global helpers
  void freeAll();
  void clearOverflow();
  static void computeStyleFileOffsets(PerStyle& s, uint32_t baseOffset);

  // Static callback for EpdFontData::glyphMissHandler (per-style via OverflowContext)
  static const EpdGlyph* onGlyphMiss(void* ctx, uint32_t codepoint);

  // Static callback for EpdFontData::coverageHandler: answers hasCodepoint()
  // from the RAM-resident full interval table, without SD I/O.
  static bool onCoverageQuery(void* ctx, uint32_t codepoint);
};
