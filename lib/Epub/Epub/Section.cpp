#include "Section.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include "BookNotes.h"
#include "Epub/css/CssParser.h"
#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

namespace {
// v28: text decoration bits now include line-through in serialized wordStyles.
// v29: TextBlock word data stored as one flat arena (offset table + NUL-terminated
// text blob) instead of length-prefixed strings and per-field arrays.
// v30: Arabic shaping changed both drawing and measurement (getTextAdvanceX now
//      measures the shaped visual text); cached word positions from v29 no longer
//      match what drawText renders.
// v32: ImageBlock serializes the book-internal source href after the cache path
//      (lazy extraction: images are header-probed at build time and extracted on
//      first render).
// v33: Support <ruby> and <rt> tags. Skip <rp> tags
// v34: Word gaps are only suppressed for tokens glued in the source, so spaces between
//      Hangul words survive again; ruby element boundaries carry the continuation flag
//      instead. Invalidates v33 caches, whose word positions have the spaces collapsed.

// v34: <br> handling changed layout — a <br> after text is now a margin-stripped
//      line break (browser-like) and only a <br> whose block stays empty injects
//      the scene-break gap, so cached pages laid out by older versions no longer
//      match. Keeps <br>-per-paragraph books (common CJK formatting) from
//      re-adding container spacing at every paragraph.
// v35: page word-anchor LUT appended (chapter-global source byte offset of each
//      page boundary, uint32 per page) plus its header offset slot. Anchors let
//      a font/size/spacing reflow reposition to the page containing the exact
//      word the reader was on, instead of the paragraph's first page (which for
//      poem chapters — no <p> at all — meant every reflow rewound to page 0).
//      Same version also makes h1-h3 headings open a fresh page (parser), so a
//      heading at the top of a page keeps that position across reflows.
// v38: layout exactness pass (2026-08-22). Two pagination changes with no
//      ReaderRenderSpec field moving, so the version must carry them: the last
//      line of a page now fits by its INK extent (ascender + descender + ruby)
//      instead of the full leaded line box, and a block's marginTop/paddingTop
//      collapse away at the top of a page. Stale v37 caches would misalign
//      against the new layout rather than fail, hence the bump.
// v39: typography batch (2026-08-22). Header grows one byte — the Line Grid
//      flag (spec.lineGridEnabled) after focusReadingEnabled — and two
//      pagination changes ride the bump: widow/orphan keep-2/2 (a paragraph's
//      first line never sits alone at a page bottom, its last never alone at a
//      page top; 1–2 line paragraphs exempt) and SD-font layout measuring with
//      kerning + ligatures (docs/punctuation-kerning-audit-2026-08-22.md P0),
//      which moves line breaks for SD reading fonts. Hanging punctuation is
//      justification-slack-only and break-neutral, but its painted x lives in
//      the cached TextBlocks, so it needs the bump too.
// v40: inter-block gap cap (owner ruling 2026-08-22, the Wingspan title
//      page: "keep half-line gap, but collapse any gap that is more than a
//      half-line gap"). No header change; pure pagination: the vertical gap
//      between two blocks in flow is capped at lineHeight/2 instead of
//      summing marginBottom + half-line + marginTop (the <br> scene-break
//      line rides on top of the cap). Stale v39 caches would keep the
//      triple-dipped gaps, hence the bump.
// v41: block-rendering audit fixes (2026-08-22,
//      docs/block-rendering-audit-2026-08-22.md). Two pagination changes, no
//      header change: a line can no longer begin with a spaced em/en dash or
//      horizontal bar (the breakers move the preceding word down with it,
//      ParsedText), and a block's ACCUMULATED horizontal insets are capped at
//      2/5 of the viewport (nested blockquote margins used to push text off
//      the right edge of the panel, ChapterHtmlSlimParser). Stale v40 caches
//      would keep dash-initial lines and clipped nested quotes, hence the bump.
// v42: proper list rendering (2026-08-22, block audit finding 3): <ul>/<ol>
//      join the block-style stack (their CSS now applies, default 1.5 em
//      indent per level when they carry none), <ol> items get real decimal
//      numbers (start/value honored), and every item hangs its marker in a
//      gutter via negative text-indent so wrapped lines align under the text.
//      Stale v41 caches would keep flat, bullet-only lists, hence the bump.
// v43: hanging punctuation gains its LEFT edge (2026-08-22, surface roadmap
//      T3). A line that BEGINS with an opening quote, bracket or dash is
//      painted shifted left by a fraction of that glyph's advance, and on a
//      justified line the shift is handed back to the gaps. Break-neutral for
//      the same reason the trailing hang is, but the painted x lives in the
//      cached TextBlocks — the v39 note above — so stale v42 caches would keep
//      flush-left quotes, hence the bump.
// v44: automatic justification (2026-08-23, owner ruling). The Text Alignment
//      setting is withdrawn and the measure decides per block, so pagination
//      moves twice over: the base intent goes back to JUSTIFIED (v43 caches
//      were built ragged, whose hyphenation rule differs — see
//      computeHyphenatedLineBreaks's raggedSkipsHyphen), and a narrow block
//      now demotes itself. Line BREAKS change, not just painted x, so a stale
//      v43 cache would keep the old ragged breaks for the life of the book.
//      The spec's paragraphAlignment byte can no longer catch this: it is a
//      compile-time constant now, identical in both eras' files.
// v45: sparse ruby (2026-08-23, owner ruling; the deferred item 2 of
//      docs/performance-indexing-2026-08-23.md). TextBlock stopped writing a
//      writeString per WORD for its furigana -- 4 bytes of empty-string length
//      for every word of every non-CJK book, 13% of a section file -- and
//      writes one presence byte per block plus an index-prefixed entry for each
//      word that actually carries an annotation (lib/Epub/Epub/blocks/
//      RubySerialization.h). A STRUCTURAL change: a v44 file's ruby bytes decode
//      as a presence byte and garbage, so the header check is the only thing
//      standing between a stale cache and a mis-parsed page. Pagination is
//      unchanged; the bump is the format, per the note below that a layout OR a
//      structural change bumps this.
// v46: missing-glyph accounting (2026-08-23, sweep item #38). Neither the
//      bytes nor the pagination move: the .notdef box is drawn in the
//      substitute glyph's own cell, so every advance is the one v45 measured.
//      What changes is what a chapter parse DISCOVERS -- the count of
//      codepoints the reading face has no shape for is a layout-scope book
//      note, and a chapter served from a v45 cache is never parsed again, so
//      the note would be silently absent on every book already on the card.
//      A note that is missing looks exactly like a book with nothing to report,
//      which is the failure the whole feature exists to prevent.
// v47: CSS lengths in real units (2026-08-23, sweep item #23). A LAYOUT change,
//      which bumps this exactly as a structural one does. `cm`, `mm`, `Q`,
//      `in` and `pc` were read as PIXELS -- `margin: 1cm` was one pixel -- and
//      now convert at 150 dpi; `pt` moved with them, from a fixed x1.33 (the
//      96 dpi answer) to the 2.083 px this renderer's own type is rasterized
//      at. `!important` also stopped being glued to the unit, so
//      `margin: 1em !important` now sets four sides instead of two. Every
//      section built before this measured different insets, and a book served
//      from a v46 cache would keep them for its whole life.
// v48: text left unlaid in front of a table no longer prints under the table's
//      own header row (2026-08-23, B-037). A <caption> is the reported case but
//      NOT the only one -- any text in an unclosed block immediately before
//      <table> took the same route, because <table>'s handler flushes only the
//      part-word buffer. Such text was still unlaid when </table> was reached,
//      so the columns emitter took its row top before that text had been
//      placed, the first cell's block flush then laid it out AT that row top,
//      and every later column rewound on top of it. A LAYOUT change: the text
//      now consumes its own lines above the table, so every page after one
//      moves. The same commit gives a <li> whose whole content is a table its
//      marker on a line of its own, which moves those pages too.
// v49: the automatic-justification threshold becomes a setting (owner ruling
//      2026-08-24, "make justified or ragged right character count an ios app
//      setting" -> the firmware's own Settings screen, as Justified Text).
//      Header grows one byte -- spec.justifyThresholdChars, after
//      lineGridEnabled -- and that byte is what makes the change visible at
//      all: the DECISION is unchanged (the measure still decides, per block)
//      but the count it decides against now moves, and moving it moves line
//      BREAKS, since a demoted block also stops hyphenating lines already past
//      70% of the measure. Every field a v48 file compares would have matched
//      across a threshold change, so without the new byte a card full of
//      paginated books would have kept its old breaks for their whole life and
//      the setting would have looked inert. The bump itself is for the v48
//      files already written, which carry no such byte.
// v50: `! important` is recognized (2026-08-24). A LAYOUT change, and the same
//      one v47 already made for the fused spelling -- the bang and the keyword
//      are two tokens in the CSS grammar, so the space between them is legal
//      and only the fused spelling was matched. The annotation therefore stayed
//      in the value: `margin: 1em ! important` failed the unit scan and dropped
//      the declaration outright, and `text-align: center ! important` fell
//      through interpretAlignment's unmatched default, which is Left rather
//      than neutral. Both change insets and line breaks on a book that uses the
//      spelling, and a section served from a v49 cache is never parsed again.
//      CSS_CACHE_VERSION moves with it: nothing links the two mechanically
//      (grep -- the section header does not carry it), so the pairing is by
//      hand, exactly as it was for v47.
// v51: per-ligature control becomes a setting (owner ruling 2026-08-24, "give
//      a full subpage of Typography Settings that gives all available
//      typography options with full granularity, including toggling each
//      individual ligature"). Header grows four bytes --
//      spec.ligatureFingerprint, after justifyThresholdChars -- and that word
//      is the whole point: a ligature sets two or three letters as one glyph
//      with one advance, so switching `st` off widens every word containing it
//      and moves the line breaks after it. Every other field a v50 file
//      compares would have matched across such a change, so without the new
//      word the setting would have been inert on every already-paginated book
//      until the cache was cleared by hand. The bump itself is for the v50
//      files already on cards, which carry no such word.
// v52: a table's header row, and any caption above it, keep with the first body
//      rows (owner ruling 2026-08-26, "don't split up table header or caption
//      from rest (when possible). intact is best"). A LAYOUT change and nothing
//      else -- no header field grows, exactly as for v50. It moves page
//      BOUNDARIES: emitBufferedTableAsColumns now completes the page ahead of a
//      header that would otherwise be the last thing on it, so every page after
//      that table in the section shifts. A section served from a v51 cache is
//      never parsed again, so without the bump the fix would apply only to
//      books that had never been opened -- and a reader who saw the reported
//      page would go on seeing it. Every book on every card repaginates once,
//      which is the cost of the bump and is accepted.
// v53: a flattened table emits a full-width hairline between records (owner
// 2026-08-27). It adds PageHorizontalRule elements and consumes vertical
// space, so pagination moves -- a cache built by v52 would render the same
// book with no separators and different page breaks, silently.
// v54: the same again for the KEY BLOCK fallback, which v53 missed. That is
// the emitter a table reaches when it does not FIT (as opposed to the abandon
// path, for a table too wide), and it is the one most readers actually see,
// because a narrow table stops fitting the moment the font goes up. A v53
// cache would still hold separator-less pages for those tables.
// v55: <dl>, <dt> and <dd> are block-level (owner report 2026-08-28, a quiz
// book whose every question ran straight into its own answer mid-line). Three
// pagination changes ride the one bump: the three tags now open blocks where
// they were inline, so lines break in different places; a <dd> with no
// publisher left inset takes a 1.5 em step, which narrows its measure and
// rewraps it; and a <dt> keeps with one line of what follows, which completes
// the page ahead of a term that would otherwise end it. A section served from
// a v54 cache is never parsed again, so without the bump the reader who sent
// the screenshot would go on seeing exactly that page.
constexpr uint8_t SECTION_FILE_VERSION = 55;
// Written into the version field while a build is in progress; patched to
// SECTION_FILE_VERSION only when the build is finalized. An abandoned /
// crash-interrupted .bin therefore carries version 0, which loadSectionFile rejects
// as unknown and clears -- so an incomplete file is never mistaken for a valid one.
constexpr uint8_t SECTION_FILE_INCOMPLETE_VERSION = 0;
// Written when a build is suspended partway (reader exited or device slept mid-build).
// The file carries valid pages 0..pageCount-1, all LUTs, and a trailer with the parse
// watermark (bytesConsumed, totalBytes) appended after the li LUT. loadSectionFile
// accepts it so a resume shows those pages instantly; the reader extends it by
// rebuilding in the background. Uses the same header layout as SECTION_FILE_VERSION,
// so finalized files are untouched by this feature; older firmware treats the sentinel
// as an unknown version and rebuilds, which is a safe downgrade.
// MUST change in lockstep with SECTION_FILE_VERSION: the sentinel IS the partial's
// format version, so a stale-format partial otherwise passes the header check and
// only fails (noisily, via the block-decode error path) when a page is loaded.
// Derived so the pairing can't be forgotten: 0xFE for v28, 0xFD for v29, ...
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION = 0xFE - (SECTION_FILE_VERSION - 28);
// Write buffer for the page stream (see BuildContext::pageWriter).
constexpr size_t PAGE_WRITE_BUFFER_SIZE = 1024;

constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) + sizeof(bool) +
                                 sizeof(uint8_t) + sizeof(bool) + sizeof(bool) + sizeof(uint8_t) + sizeof(uint32_t) +
                                 sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
                                 sizeof(uint32_t);
}  // namespace

// Out-of-line so the unique_ptr<ChapterHtmlSlimParser> in BuildContext can be
// constructed/destroyed where the parser's full definition is visible.
Section::Section(const std::shared_ptr<Epub>& epub, const int spineIndex, GfxRenderer& renderer)
    : epub(epub),
      spineIndex(spineIndex),
      renderer(renderer),
      filePath(epub->getCachePath() + "/sections/" + std::to_string(spineIndex) + ".bin") {}

// Suspend any in-progress build so every section.reset() / navigation / sleep path
// persists the pages already laid out as a partial .bin instead of discarding them
// (no-op once a build has completed or never started).
Section::~Section() { suspendBuild(); }

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file || !build_ || !build_->pageWriter) {
    LOG_ERR("SCT", "File not open for writing page %d", builtPageCount_);
    return 0;
  }

  // Logical position, not file.position(): bytes still sitting in the writer's
  // buffer have not reached the file yet, so the LUT would point at the page
  // before this one.
  const uint32_t position = build_->pageWriter->position();
  if (!page->serialize(*build_->pageWriter)) {
    LOG_ERR("SCT", "Failed to serialize page %d", builtPageCount_);
    return 0;
  }
  LOG_DBG("SCT", "Page %d processed", builtPageCount_);

  builtPageCount_++;
  // pageCount is the pages available to read: a rebuild over a partial only raises it
  // once it has laid out more pages than the partial already covers.
  if (builtPageCount_ > pageCount) {
    pageCount = builtPageCount_;
  }
  return position;
}

void Section::writeSectionFileHeader(const ReaderRenderSpec& spec) {
  if (!file) {
    LOG_DBG("SCT", "File not open for writing header");
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(spec.fontId) + sizeof(spec.lineCompression) +
                                   sizeof(spec.extraParagraphSpacing) + sizeof(spec.paragraphAlignment) +
                                   sizeof(spec.viewportWidth) + sizeof(spec.viewportHeight) + sizeof(pageCount) +
                                   sizeof(spec.hyphenationEnabled) + sizeof(spec.embeddedStyle) +
                                   sizeof(spec.imageRendering) + sizeof(spec.focusReadingEnabled) +
                                   sizeof(spec.lineGridEnabled) + sizeof(spec.justifyThresholdChars) +
                                   sizeof(spec.ligatureFingerprint) + sizeof(uint32_t) + sizeof(uint32_t) +
                                   sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t),
                "Header size mismatch");
  // Written as the incomplete sentinel; finalizeBuild() patches it to
  // SECTION_FILE_VERSION as the last step, committing the file.
  serialization::writePod(file, SECTION_FILE_INCOMPLETE_VERSION);
  serialization::writePod(file, spec.fontId);
  serialization::writePod(file, spec.lineCompression);
  serialization::writePod(file, spec.extraParagraphSpacing);
  serialization::writePod(file, spec.paragraphAlignment);
  serialization::writePod(file, spec.viewportWidth);
  serialization::writePod(file, spec.viewportHeight);
  serialization::writePod(file, spec.hyphenationEnabled);
  serialization::writePod(file, spec.embeddedStyle);
  serialization::writePod(file, spec.imageRendering);
  serialization::writePod(file, spec.focusReadingEnabled);
  serialization::writePod(file, spec.lineGridEnabled);
  serialization::writePod(file, spec.justifyThresholdChars);
  serialization::writePod(file, spec.ligatureFingerprint);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0, patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for anchor map offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for paragraph LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for li LUT offset (patched later)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for word-anchor LUT offset (patched later)
}

bool Section::loadSectionFile(const ReaderRenderSpec& spec) {
  if (!Storage.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  bool filePartial = false;
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION && version != SECTION_FILE_PARTIAL_VERSION) {
      // Explicit close() required: member variable persists beyond function scope
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Unknown version %u", version);
      clearCache();
      return false;
    }
    filePartial = (version == SECTION_FILE_PARTIAL_VERSION);

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileEmbeddedStyle;
    uint8_t fileImageRendering;
    bool fileFocusReadingEnabled;
    bool fileLineGridEnabled;
    uint8_t fileJustifyThresholdChars;
    uint32_t fileLigatureFingerprint;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileEmbeddedStyle);
    serialization::readPod(file, fileImageRendering);
    serialization::readPod(file, fileFocusReadingEnabled);
    serialization::readPod(file, fileLineGridEnabled);
    serialization::readPod(file, fileJustifyThresholdChars);
    serialization::readPod(file, fileLigatureFingerprint);

    if (spec.fontId != fileFontId || spec.lineCompression != fileLineCompression ||
        spec.extraParagraphSpacing != fileExtraParagraphSpacing || spec.paragraphAlignment != fileParagraphAlignment ||
        spec.viewportWidth != fileViewportWidth || spec.viewportHeight != fileViewportHeight ||
        spec.hyphenationEnabled != fileHyphenationEnabled || spec.embeddedStyle != fileEmbeddedStyle ||
        spec.imageRendering != fileImageRendering || spec.focusReadingEnabled != fileFocusReadingEnabled ||
        spec.lineGridEnabled != fileLineGridEnabled ||
        spec.justifyThresholdChars != fileJustifyThresholdChars ||
        spec.ligatureFingerprint != fileLigatureFingerprint) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: Parameters do not match");
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);

  if (filePartial) {
    // A partial's pageCount is the watermark of a suspended build. Read the watermark
    // trailer (appended after the li LUT) so estimatedTotalPages can extrapolate.
    uint32_t wordLutOffset = 0;
    file.seek(HEADER_SIZE - sizeof(uint32_t));
    serialization::readPod(file, wordLutOffset);
    const uint32_t trailerOffset = wordLutOffset + static_cast<uint32_t>(pageCount) * sizeof(uint32_t);
    const bool trailerValid =
        pageCount > 0 && wordLutOffset >= HEADER_SIZE && trailerOffset + 2 * sizeof(uint32_t) <= file.size();
    if (!trailerValid) {
      file.close();
      LOG_ERR("SCT", "Deserialization failed: malformed partial section");
      clearCache();
      pageCount = 0;
      return false;
    }
    file.seek(trailerOffset);
    serialization::readPod(file, partialBytesConsumed_);
    serialization::readPod(file, partialTotalBytes_);
    partial_ = true;
    partialPageCount_ = pageCount;
  }

  // Explicit close() required: member variable persists beyond function scope
  file.close();
  LOG_DBG("SCT", "Deserialization succeeded: %d pages%s", pageCount, filePartial ? " (partial)" : "");
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  const std::string tmpBin = binTmpPath();
  if (Storage.exists(tmpBin.c_str())) {
    Storage.remove(tmpBin.c_str());
  }
  if (!Storage.exists(filePath.c_str())) {
    LOG_DBG("SCT", "Cache does not exist, no action needed");
    return true;
  }

  if (!Storage.remove(filePath.c_str())) {
    LOG_ERR("SCT", "Failed to clear cache");
    return false;
  }

  LOG_DBG("SCT", "Cache cleared successfully");
  return true;
}

bool Section::createSectionFile(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  // One-shot build: start, then lay out the whole section in a single pass.
  if (!startBuild(spec, popupFn)) {
    return false;
  }
  if (!buildSomeMore(0)) {  // 0 = build to completion
    return false;
  }
  return buildComplete_;
}

bool Section::startBuild(const ReaderRenderSpec& spec, const std::function<void()>& popupFn) {
  if (build_) {
    LOG_ERR("SCT", "startBuild called while a build is already active");
    return false;
  }
  buildComplete_ = false;
  builtPageCount_ = 0;
  // Pages from a loaded partial stay readable (from filePath) while this build writes
  // to the tmp .bin, so availability never drops below the partial's watermark.
  pageCount = partial_ ? partialPageCount_ : 0;

  // Remove a stale tmp .bin from a crash-interrupted build; this build recreates it.
  {
    const std::string staleTmp = binTmpPath();
    if (Storage.exists(staleTmp.c_str())) {
      Storage.remove(staleTmp.c_str());
    }
  }

  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto htmlDir = epub->getCachePath() + "/html";
  const auto htmlPath = htmlDir + "/" + std::to_string(spineIndex) + ".html";
  const auto tmpHtmlPath = htmlDir + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    Storage.mkdir(sectionsDir.c_str());
  }

  // Reuse the previously unzipped HTML if we already have it. The unzipped HTML is keyed only on the
  // book (it lives in the per-book cache dir), not on render settings, so it survives the invalidation
  // that wipes the layout (.bin) caches when font/margin/orientation change -- rebuilds then skip zip
  // inflation entirely. It's promoted by an atomic rename as soon as the inflate succeeds (below), so
  // even a window-only giant spine -- whose .bin never finalizes -- still caches its HTML, letting a
  // reopen skip the multi-second inflate. If htmlPath exists it is known-complete.
  const bool reusedHtml = Storage.exists(htmlPath.c_str());
  bool htmlCached = reusedHtml;
  if (reusedHtml) {
    LOG_DBG("SCT", "Reusing cached HTML %s", htmlPath.c_str());
  } else {
    Storage.mkdir(htmlDir.c_str());

    // Retry logic for SD card timing issues
    bool streamed = false;
    uint32_t fileSize = 0;
    for (int attempt = 0; attempt < 3 && !streamed; attempt++) {
      if (attempt > 0) {
        LOG_DBG("SCT", "Retrying stream (attempt %d)...", attempt + 1);
        delay(50);  // Brief delay before retry
      }

      // Remove any incomplete file from previous attempt before retrying
      if (Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
      }

      HalFile tmpHtml;
      if (!Storage.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
        continue;
      }
      // Larger chunks mean far fewer SD writes inflating the HTML; a 1KB chunk turned a 584KB
      // single-spine novel into ~570 tiny writes (multi-second). 8KB keeps the transient buffers
      // small while cutting the write count 8x.
      streamed = epub->readItemContentsToStream(localPath, tmpHtml, 8192);
      fileSize = tmpHtml.size();
      // Explicitly close() file before calling Storage.remove()
      tmpHtml.close();

      // If streaming failed, remove the incomplete file immediately
      if (!streamed && Storage.exists(tmpHtmlPath.c_str())) {
        Storage.remove(tmpHtmlPath.c_str());
        LOG_DBG("SCT", "Removed incomplete temp file after failed attempt");
      }
    }

    if (!streamed) {
      LOG_ERR("SCT", "Failed to stream item contents to temp file after retries");
      return false;
    }

    LOG_DBG("SCT", "Streamed temp HTML to %s (%d bytes)", tmpHtmlPath.c_str(), fileSize);

    // Promote to the persistent HTML cache immediately -- the inflate is complete and the bytes are
    // valid regardless of whether the layout build finishes, so reopening (even a window-only spine
    // that never finalizes its .bin) skips re-inflation. If the rename fails we just parse the temp.
    if (Storage.rename(tmpHtmlPath.c_str(), htmlPath.c_str())) {
      htmlCached = true;
    } else {
      LOG_DBG("SCT", "Failed to promote HTML cache; parsing from temp");
    }
  }

  if (!Storage.openFileForWrite("SCT", binTmpPath(), file)) {
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // Header is written with the incomplete-version sentinel; finalizeBuild() commits it.
  writeSectionFileHeader(spec);

  auto ctx = makeUniqueNoThrow<BuildContext>();
  if (!ctx) {
    LOG_ERR("SCT", "OOM: BuildContext");
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  // htmlCached == "htmlPath is the live cache" (reused, or just promoted). finalizeBuild/abandonBuild
  // then leave the cached HTML alone; only an un-promoted temp (rename failed) is theirs to clean up.
  // Constructed here, after the header write, so its logical cursor starts at
  // HEADER_SIZE. 1 KB rather than BookMetadataCache's 4 KB: this one is held for
  // the whole build (which can run for minutes on a giant spine) instead of a
  // single streaming pass, and a page is a few KB, so 1 KB already collapses the
  // per-page call count from ~600 to a handful.
  ctx->pageWriter = makeUniqueNoThrow<serialization::BufferedFileWriter>(file, PAGE_WRITE_BUFFER_SIZE);
  if (!ctx->pageWriter) {
    LOG_ERR("SCT", "OOM: page writer");
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }
  ctx->reusedHtml = htmlCached;
  ctx->htmlPath = htmlPath;
  ctx->tmpHtmlPath = tmpHtmlPath;
  ctx->parsePath = htmlCached ? htmlPath : tmpHtmlPath;

  // Derive the content base directory and image cache path prefix for the parser
  const size_t lastSlash = localPath.find_last_of('/');
  ctx->contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  ctx->imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";

  if (spec.embeddedStyle) {
    ctx->cssParser = epub->getCssParser();
    if (ctx->cssParser && !ctx->cssParser->loadFromCache()) {
      LOG_ERR("SCT", "Failed to load CSS from cache");
    }
  }

  // Collect TOC anchors for this spine so the parser can insert page breaks at chapter boundaries
  std::vector<std::string> tocAnchors;
  const int startTocIndex = epub->getTocIndexForSpineIndex(spineIndex);
  if (startTocIndex >= 0) {
    for (int i = startTocIndex; i < epub->getTocItemsCount(); i++) {
      auto entry = epub->getTocItem(i);
      if (entry.spineIndex != spineIndex) break;
      if (!entry.anchor.empty()) {
        tocAnchors.push_back(std::move(entry.anchor));
      }
    }
  }

  // The parser stores the path/contentBase/imageBasePath by reference, so they must
  // live in the BuildContext (which outlives the parser). The page-complete callback
  // captures the BuildContext pointer to append to its in-RAM LUT; build_ owns the
  // context for the parser's whole lifetime.
  BuildContext* ctxPtr = ctx.get();
  ctx->parser = makeUniqueNoThrow<ChapterHtmlSlimParser>(
      epub, ctxPtr->parsePath, renderer, spec.fontId, spec.smallFontId, spec.lineCompression, spec.extraParagraphSpacing,
      spec.paragraphAlignment, spec.viewportWidth, spec.viewportHeight, spec.hyphenationEnabled,
      spec.focusReadingEnabled, spec.lineGridEnabled, spec.justifyThresholdChars,
      [this, ctxPtr](std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
                     const uint32_t wordAnchor) {
        ctxPtr->lut.push_back({this->onPageComplete(std::move(page)), paragraphIndex, listItemIndex, wordAnchor});
      },
      spec.embeddedStyle, ctxPtr->contentBase, ctxPtr->imageBasePath, spec.imageRendering, std::move(tocAnchors),
      popupFn, ctxPtr->cssParser);
  if (!ctx->parser) {
    LOG_ERR("SCT", "OOM: ChapterHtmlSlimParser");
    if (ctx->cssParser) ctx->cssParser->clear();
    file.close();
    Storage.remove(binTmpPath().c_str());
    if (!reusedHtml) Storage.remove(tmpHtmlPath.c_str());
    return false;
  }

  Hyphenator::setPreferredLanguage(epub->getLanguage());
  build_ = std::move(ctx);

  if (!build_->parser->beginParse()) {
    LOG_ERR("SCT", "Failed to begin parse");
    abandonBuild();
    return false;
  }
  build_->totalBytes = build_->parser->parseTotalBytes();
  return true;
}

bool Section::buildSomeMore(const int maxPages) {
  if (!build_ || !build_->parser) {
    LOG_ERR("SCT", "buildSomeMore with no active build");
    return false;
  }
  // Pace on pages laid out by THIS build, not pageCount: during a rebuild over a partial,
  // pageCount stays pinned at the partial's watermark until the build passes it, which
  // would otherwise turn one "small" chunk into a blocking rebuild of the whole watermark.
  const int startCount = builtPageCount_;
  for (;;) {
    const auto status = build_->parser->parseStep();
    if (status == ChapterHtmlSlimParser::ParseStatus::Error) {
      LOG_ERR("SCT", "Parse error during incremental build");
      abandonBuild();
      return false;
    }
    if (status == ChapterHtmlSlimParser::ParseStatus::Done) {
      return finalizeBuild();
    }
    // ParseStatus::More: yield once we've laid out the requested number of pages.
    if (maxPages > 0 && (builtPageCount_ - startCount) >= maxPages) {
      build_->bytesConsumed = build_->parser->parseBytesConsumed();
      return true;
    }
  }
}

bool Section::hasHtmlCache() const {
  const std::string htmlPath = epub->getCachePath() + "/html/" + std::to_string(spineIndex) + ".html";
  return Storage.exists(htmlPath.c_str());
}

std::optional<uint16_t> Section::findAnchorDuringBuild(const std::string& anchor) const {
  if (!build_ || !build_->parser) return std::nullopt;
  for (const auto& [key, page] : build_->parser->getAnchors()) {
    if (key == anchor) return page;
  }
  return std::nullopt;
}

std::optional<uint16_t> Section::getParagraphIndexForPageDuringBuild(const uint16_t page) const {
  if (!build_ || page >= build_->lut.size()) return std::nullopt;
  return build_->lut[page].paragraphIndex;
}

std::optional<uint16_t> Section::getPageForParagraphIndexDuringBuild(const uint16_t pIndex) const {
  if (!build_ || build_->lut.empty()) return std::nullopt;
  // Refuse to answer until the build has actually passed pIndex. Otherwise every
  // paragraph beyond the watermark resolves to the watermark itself, and the
  // reader lands short of where it was — the caller must build further and retry,
  // exactly as it does for a named anchor.
  if (build_->lut.back().paragraphIndex < pIndex) return std::nullopt;
  // Last page whose paragraph starts at or before pIndex: a paragraph can span
  // several pages, and the one it STARTS on is where the reader was.
  std::optional<uint16_t> best;
  for (size_t i = 0; i < build_->lut.size(); i++) {
    if (build_->lut[i].paragraphIndex <= pIndex) {
      best = static_cast<uint16_t>(i);
    } else {
      break;  // lut paragraph indices are non-decreasing
    }
  }
  return best;
}

std::optional<uint16_t> Section::findAnchor(const std::string& anchor) const {
  if (const auto page = findAnchorDuringBuild(anchor)) {
    return page;
  }
  // Fall back to the on-disk anchor map: a finalized section, or a partial whose map
  // covers everything up to its watermark (nullopt past it -- build further and retry).
  return getPageForAnchor(anchor);
}

uint16_t Section::estimatedTotalPages() const {
  // Extrapolation from a suspended session's watermark trailer. A static snapshot, so no EMA
  // damping is needed. Also the best guess while a rebuild is running but hasn't laid out
  // enough pages yet to extrapolate from its own progress.
  const auto partialEstimate = [this]() -> uint16_t {
    if (!partial_ || partialBytesConsumed_ == 0 || partialTotalBytes_ <= partialBytesConsumed_) {
      return pageCount;
    }
    const uint64_t est = static_cast<uint64_t>(partialPageCount_) * partialTotalBytes_ / partialBytesConsumed_;
    if (est <= pageCount) return pageCount;
    return est > 60000 ? 60000 : static_cast<uint16_t>(est);
  };

  if (!build_) {
    return partial_ ? partialEstimate() : pageCount;  // partial -> extrapolate, finalized -> exact
  }
  const uint32_t consumed = build_->bytesConsumed;
  const uint32_t total = build_->totalBytes;
  if (builtPageCount_ == 0 || consumed == 0 || total <= consumed) return partialEstimate();

  // Raw extrapolation: scale the pages built so far by the fraction of HTML still unparsed. This
  // re-derives from a growing, non-uniform sample, so it jitters up and down as the build crosses
  // dense vs sparse regions of the chapter.
  const uint64_t raw = static_cast<uint64_t>(builtPageCount_) * total / consumed;

  // Damp that jitter with an exponential moving average. Step it once per build advance (keyed on
  // bytesConsumed) rather than per status-bar redraw, so the smoothing rate doesn't depend on how
  // often we repaint. As the build nears the end, consumed -> total and raw -> the built count, so
  // the average settles onto the true count (and finalizeBuild then returns the exact pageCount).
  constexpr float ALPHA = 0.25f;  // weight of each new sample; lower = steadier but slower to settle
  if (build_->smoothedEstimate <= 0) {
    build_->smoothedEstimate = static_cast<float>(raw);  // seed on the first estimate
  } else if (consumed != build_->smoothedAtConsumed) {
    build_->smoothedEstimate += ALPHA * (static_cast<float>(raw) - build_->smoothedEstimate);
  }
  build_->smoothedAtConsumed = consumed;

  const uint64_t est = static_cast<uint64_t>(build_->smoothedEstimate + 0.5f);
  if (est <= pageCount) return pageCount;  // never fewer than the pages already available
  return est > 60000 ? 60000 : static_cast<uint16_t>(est);
}

// Write the LUTs and anchor map into the open tmp .bin, patch the header with the built
// page count and table offsets, stamp `version` as the commit point, then swap the tmp
// file over filePath. For SECTION_FILE_PARTIAL_VERSION a watermark trailer
// (bytesConsumed, totalBytes) is appended after the li LUT so a later open can estimate
// the total page count. The parser must still be alive (anchors are read from it).
// On failure the tmp is removed and any pre-existing file at filePath is left intact.
bool Section::commitBuildFile(const uint8_t version, const uint32_t bytesConsumed, const uint32_t totalBytes) {
  const bool asPartial = (version == SECTION_FILE_PARTIAL_VERSION);

  const auto failCommit = [this]() {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
    return false;
  };

  // The page stream is buffered (BuildContext::pageWriter); its tail must reach
  // the file before the tables that follow it are positioned.
  if (build_->pageWriter && !build_->pageWriter->flush()) {
    LOG_ERR("SCT", "Short write flushing the page stream");
    return failCommit();
  }

  // Four parallel tables, one entry per page each: unbuffered that is 4 pods per
  // page (23,000 HalFile calls on a 5,800-page spine) for ~50 KB of data.
  uint32_t lutOffset;
  uint32_t anchorMapOffset;
  uint32_t paragraphLutOffset;
  uint32_t liLutFileOffset;
  uint32_t wordLutFileOffset;
  // Set instead of returning from inside the writer's scope: failCommit() closes
  // the file, and the writer's destructor would then flush into a closed handle.
  bool tablesOk = true;
  {
    serialization::BufferedFileWriter tables(file, PAGE_WRITE_BUFFER_SIZE);
    lutOffset = tables.position();
    for (const auto& entry : build_->lut) {
      if (entry.fileOffset == 0) {
        LOG_ERR("SCT", "Failed to write LUT due to invalid page positions");
        tablesOk = false;
        break;
      }
      serialization::writePod(tables, entry.fileOffset);
    }

    // Write anchor-to-page map for fragment navigation (e.g. footnote targets). For a
    // partial, skip anchors that landed on the incomplete trailing page the suspend drops.
    anchorMapOffset = tables.position();
    const auto& anchors = build_->parser->getAnchors();
    uint16_t anchorCount = 0;
    for (const auto& [anchor, page] : anchors) {
      if (!asPartial || page < builtPageCount_) anchorCount++;
    }
    serialization::writePod(tables, anchorCount);
    for (const auto& [anchor, page] : anchors) {
      if (asPartial && page >= builtPageCount_) continue;
      serialization::writeString(tables, anchor);
      serialization::writePod(tables, page);
    }

    paragraphLutOffset = tables.position();
    serialization::writePod(tables, static_cast<uint16_t>(build_->lut.size()));
    for (const auto& entry : build_->lut) {
      serialization::writePod(tables, entry.paragraphIndex);
    }

    liLutFileOffset = static_cast<uint32_t>(tables.position());
    for (const auto& entry : build_->lut) {
      serialization::writePod(tables, entry.listItemIndex);
    }

    wordLutFileOffset = static_cast<uint32_t>(tables.position());
    for (const auto& entry : build_->lut) {
      serialization::writePod(tables, entry.wordAnchor);
    }

    if (asPartial) {
      // Watermark trailer, located on load as wordLutOffset + pageCount * sizeof(uint32_t).
      serialization::writePod(tables, bytesConsumed);
      serialization::writePod(tables, totalBytes);
    }

    // Must land before the header patch below seeks backwards.
    if (!tables.flush()) {
      LOG_ERR("SCT", "Short write flushing the section tables");
      tablesOk = false;
    }
  }
  if (!tablesOk) {
    return failCommit();
  }

  // Patch header with the built page count and section offsets...
  file.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(builtPageCount_));
  serialization::writePod(file, builtPageCount_);
  serialization::writePod(file, lutOffset);
  serialization::writePod(file, anchorMapOffset);
  serialization::writePod(file, paragraphLutOffset);
  serialization::writePod(file, liLutFileOffset);
  serialization::writePod(file, wordLutFileOffset);
  // ...then commit by overwriting the sentinel version with the real one. Writing the
  // version last makes it the commit point: a crash before here leaves version 0.
  file.seek(0);
  serialization::writePod(file, version);
  // Explicit close() required: member variable persists beyond function scope
  file.close();

  // Swap into place. A crash between remove and rename loses the old file but keeps a
  // fully-committed tmp; the next build just removes it and rebuilds.
  if (Storage.exists(filePath.c_str())) {
    Storage.remove(filePath.c_str());
  }
  if (!Storage.rename(binTmpPath().c_str(), filePath.c_str())) {
    LOG_ERR("SCT", "Failed to move built section into place");
    Storage.remove(binTmpPath().c_str());
    return false;
  }
  return true;
}

bool Section::finalizeBuild() {
  // Flush the trailing page (emits the last page via the completePageFn into the LUT).
  build_->parser->finishParse();

  if (!build_->reusedHtml) {
    // Parse succeeded: promote the freshly unzipped HTML to the persistent cache so future
    // rebuilds skip zip inflation. If promotion fails, drop the temp -- the build still succeeded.
    if (!Storage.rename(build_->tmpHtmlPath.c_str(), build_->htmlPath.c_str())) {
      LOG_DBG("SCT", "Failed to promote HTML cache, removing temp");
      Storage.remove(build_->tmpHtmlPath.c_str());
    }
  }

  // Storage-call count, not byte count: the cost of this path on the device is
  // one SdFat transaction per call against a single shared sector cache, so this
  // is the figure that says whether the page stream is streaming or thrashing.
  LOG_DBG("SCT", "Build wrote %u pages in %u storage writes", builtPageCount_,
          static_cast<uint32_t>(build_->pageWriter ? build_->pageWriter->writeCalls() : 0));

  const bool committed = commitBuildFile(SECTION_FILE_VERSION, 0, 0);
  // A finished chapter is the point at which everything the layout engine
  // noticed about this book is known. Cheap: a no-op unless a note was raised.
  booknotes::current().flush();
  if (build_->cssParser) build_->cssParser->clear();
  build_.reset();
  if (!committed) {
    // commitBuildFile removed filePath before the failed swap, so nothing valid remains.
    partial_ = false;
    partialPageCount_ = 0;
    pageCount = 0;
    builtPageCount_ = 0;
    return false;
  }
  buildComplete_ = true;
  partial_ = false;
  partialPageCount_ = 0;
  pageCount = builtPageCount_;
  return true;
}

void Section::suspendBuild() {
  if (!build_) return;

  // Only worth persisting if this build produced pages a pre-existing partial doesn't
  // already cover; otherwise keep the older (bigger) partial and just drop the tmp.
  const bool worthKeeping = builtPageCount_ > 0 && (!partial_ || builtPageCount_ > partialPageCount_);

  bool committed = false;
  if (worthKeeping) {
    // Capture the parse watermark and commit before tearing the parser down (the anchor
    // map is read from it). The incomplete trailing page is intentionally not flushed:
    // only fully laid-out pages are persisted, and the rebuild re-derives the rest.
    const uint32_t consumed = static_cast<uint32_t>(build_->parser->parseBytesConsumed());
    committed = commitBuildFile(SECTION_FILE_PARTIAL_VERSION, consumed, build_->totalBytes);
    if (committed) {
      partial_ = true;
      partialPageCount_ = builtPageCount_;
      partialBytesConsumed_ = consumed;
      partialTotalBytes_ = build_->totalBytes;
      LOG_INF("SCT", "Suspended build: %u pages persisted", builtPageCount_);
    }
  }

  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (!committed && file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  pageCount = partial_ ? partialPageCount_ : 0;
  builtPageCount_ = 0;
}

void Section::abandonBuild() {
  if (!build_) return;
  if (build_->parser) build_->parser->abortParse();
  if (build_->cssParser) build_->cssParser->clear();
  if (file) {
    // Explicit close() required before remove (member variable, O_RDWR handle).
    file.close();
    Storage.remove(binTmpPath().c_str());
  }
  // A parse error would recur against the same HTML, so drop any partial too -- resuming
  // from it would just re-enter the failing build every open.
  if (Storage.exists(filePath.c_str())) {
    Storage.remove(filePath.c_str());
  }
  if (!build_->reusedHtml && Storage.exists(build_->tmpHtmlPath.c_str())) {
    Storage.remove(build_->tmpHtmlPath.c_str());
  }
  build_.reset();
  buildComplete_ = false;
  partial_ = false;
  partialPageCount_ = 0;
  pageCount = 0;
  builtPageCount_ = 0;
}

std::unique_ptr<Page> Section::loadPageDuringBuild(const int page) {
  if (!build_ || page < 0 || page >= static_cast<int>(build_->lut.size()) || !file) {
    return nullptr;
  }
  const uint32_t pos = build_->lut[page].fileOffset;
  if (pos == 0) {
    return nullptr;
  }
  // The .bin is open O_RDWR for the build. Flush first -- the page may still be
  // in the writer's buffer, and the seek below would otherwise leave the writer
  // appending at the read position.
  if (build_->pageWriter) build_->pageWriter->flush();
  // Read the already-written page, then restore the write cursor so the next
  // onPageComplete keeps appending where it left off.
  const uint32_t writePos = file.position();
  file.seek(pos);
  auto p = Page::deserialize(file);
  file.seek(writePos);
  return p;
}

// Read a page from the committed file at filePath (finalized section or partial from a
// previous session). Uses a local handle so it is safe while a build holds the member
// `file` open on the tmp .bin.
std::unique_ptr<Page> Section::loadPageAt(const int page) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return nullptr;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5);
  uint32_t lutOffset;
  serialization::readPod(f, lutOffset);
  f.seek(lutOffset + sizeof(uint32_t) * page);
  uint32_t pagePos;
  serialization::readPod(f, pagePos);
  f.seek(pagePos);

  return Page::deserialize(f);
  // No f.close() needed -- DESTRUCTOR_CLOSES_FILE=1 handles it at scope exit
}

std::unique_ptr<Page> Section::loadPage(const int page) {
  if (page < 0) {
    return nullptr;
  }
  if (build_ && page < static_cast<int>(build_->lut.size())) {
    return loadPageDuringBuild(page);
  }
  // Not (yet) in the active build: serve from the file on disk -- a finalized section,
  // or a partial from a previous session whose pages the rebuild hasn't reached again.
  const int onDisk = partial_ ? partialPageCount_ : (build_ ? 0 : pageCount);
  if (page >= onDisk) {
    return nullptr;
  }
  return loadPageAt(page);
}

std::string Section::getTextFromSectionFile() {
  std::string fullText;
  auto p = loadPage(currentPage);
  if (p) {
    for (const auto& el : p->elements) {
      if (el->getTag() == TAG_PageLine) {
        const auto& line = static_cast<const PageLine&>(*el);
        if (line.getBlock()) {
          const auto& block = *line.getBlock();
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!fullText.empty()) fullText += " ";
            fullText += block.wordText(i);
          }
        }
      }
    }
  }
  return fullText;
}

std::optional<uint16_t> Section::getCachedPageCount() const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  if (fileSize < HEADER_SIZE) {
    return std::nullopt;
  }

  // Only a finalized section's count is the chapter total; a partial's count is just the
  // suspended build's watermark, which would skew progress mapping. Callers fall back to
  // their own estimates.
  uint8_t version;
  serialization::readPod(f, version);
  if (version != SECTION_FILE_VERSION) {
    return std::nullopt;
  }

  f.seek(HEADER_SIZE - sizeof(uint32_t) * 5 - sizeof(uint16_t));
  uint16_t count;
  serialization::readPod(f, count);
  return count;
}

std::optional<uint16_t> Section::getPageForAnchor(const std::string& anchor) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 4);
  uint32_t anchorMapOffset;
  serialization::readPod(f, anchorMapOffset);
  if (anchorMapOffset == 0 || anchorMapOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(anchorMapOffset);
  uint16_t count;
  serialization::readPod(f, count);
  for (uint16_t i = 0; i < count; i++) {
    std::string key;
    uint16_t page;
    serialization::readString(f, key);
    serialization::readPod(f, page);
    if (key == anchor) {
      return page;
    }
  }

  return std::nullopt;
}

std::optional<uint16_t> Section::getPageForParagraphIndex(const uint16_t pIndex) const {
  // The active build's table wins for the same reason as the forward lookup: the
  // pages being repositioned onto may not be committed yet.
  if (const auto page = getPageForParagraphIndexDuringBuild(pIndex)) {
    return page;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = paragraphLutOffset + sizeof(uint16_t) + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pagePIdx;
    serialization::readPod(f, pagePIdx);
    if (pagePIdx >= pIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}

std::optional<uint16_t> Section::getParagraphIndexForPage(const uint16_t page) const {
  // The active build's table wins: it covers pages that are on screen but not yet
  // committed to disk.
  if (const auto p = getParagraphIndexForPageDuringBuild(page)) {
    return p;
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0 || page >= count) {
    return std::nullopt;
  }

  const uint32_t entryEnd = paragraphLutOffset + sizeof(uint16_t) + (page + 1) * sizeof(uint16_t);
  if (entryEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset + sizeof(uint16_t) + page * sizeof(uint16_t));
  uint16_t pIdx;
  serialization::readPod(f, pIdx);
  return pIdx;
}

std::optional<uint32_t> Section::getFirstWordAnchorForPageDuringBuild(const uint16_t page) const {
  if (!build_ || page > build_->lut.size()) return std::nullopt;
  if (page == 0) return 0;
  return build_->lut[page - 1].wordAnchor;
}

std::optional<uint16_t> Section::getPageForWordAnchorDuringBuild(const uint32_t anchor) const {
  if (!build_ || build_->lut.empty()) return std::nullopt;
  // Refuse to answer until the build has laid out PAST the anchor, mirroring
  // getPageForParagraphIndexDuringBuild: otherwise everything beyond the
  // watermark would resolve to the watermark itself and the reader lands short.
  if (build_->lut.back().wordAnchor < anchor) return std::nullopt;
  // Page starts: start(0) = 0, start(P) = lut[P-1].wordAnchor. Take the last
  // page whose start <= anchor, then walk back over an equal run so a page
  // that begins mid-word (hyphenated continuation) resolves to the page where
  // that word STARTS.
  uint16_t best = 0;
  for (size_t i = 0; i < build_->lut.size(); i++) {
    if (build_->lut[i].wordAnchor <= anchor) {
      best = static_cast<uint16_t>(i + 1);
    } else {
      break;  // page-start anchors are non-decreasing
    }
  }
  while (best > 0) {
    const uint32_t startOfBest = build_->lut[best - 1].wordAnchor;
    const uint32_t startOfPrev = best >= 2 ? build_->lut[best - 2].wordAnchor : 0;
    if (startOfPrev != startOfBest) break;
    best--;
  }
  return best;
}

std::optional<uint32_t> Section::getFirstWordAnchorForPage(const uint16_t page) const {
  if (const auto a = getFirstWordAnchorForPageDuringBuild(page)) {
    return a;
  }
  if (page == 0) return 0;

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t wordLutOffset;
  serialization::readPod(f, wordLutOffset);
  if (wordLutOffset == 0 || wordLutOffset >= fileSize) {
    return std::nullopt;
  }
  // The word LUT has one uint32 per page, no count prefix; pageCount bounds it.
  if (page > pageCount || wordLutOffset + static_cast<uint32_t>(page) * sizeof(uint32_t) > fileSize) {
    return std::nullopt;
  }
  f.seek(wordLutOffset + static_cast<uint32_t>(page - 1) * sizeof(uint32_t));
  uint32_t anchor;
  serialization::readPod(f, anchor);
  return anchor;
}

std::optional<uint16_t> Section::getPageForWordAnchor(const uint32_t anchor) const {
  // The active build's table wins for the same reason as the paragraph lookup:
  // after a settings change the pages being repositioned onto are not on disk.
  if (build_) {
    return getPageForWordAnchorDuringBuild(anchor);
  }

  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t wordLutOffset;
  serialization::readPod(f, wordLutOffset);
  if (wordLutOffset == 0 || wordLutOffset >= fileSize || pageCount == 0) {
    return std::nullopt;
  }
  if (wordLutOffset + static_cast<uint32_t>(pageCount) * sizeof(uint32_t) > fileSize) {
    return std::nullopt;
  }

  f.seek(wordLutOffset);
  uint16_t best = 0;
  uint32_t startOfBest = 0;
  for (uint16_t i = 0; i < pageCount; i++) {
    uint32_t entry;
    serialization::readPod(f, entry);
    if (entry <= anchor) {
      // entry is start(i + 1); walk-back over equal runs collapses here to
      // "only advance when the start strictly grows".
      if (entry != startOfBest || best == 0) {
        best = static_cast<uint16_t>(i + 1);
        startOfBest = entry;
      }
    } else {
      break;
    }
  }
  if (best >= pageCount) best = pageCount - 1;
  return best;
}

std::optional<uint16_t> Section::getPageForListItemIndex(const uint16_t liIndex) const {
  HalFile f;
  if (!Storage.openFileForRead("SCT", filePath, f)) {
    return std::nullopt;
  }

  const uint32_t fileSize = f.size();
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 2);
  uint32_t liLutOffset;
  serialization::readPod(f, liLutOffset);
  if (liLutOffset == 0 || liLutOffset >= fileSize) {
    return std::nullopt;
  }

  // The li LUT shares count with the paragraph LUT; read count from paragraphLutOffset
  f.seek(HEADER_SIZE - sizeof(uint32_t) * 3);
  uint32_t paragraphLutOffset;
  serialization::readPod(f, paragraphLutOffset);
  if (paragraphLutOffset == 0 || paragraphLutOffset >= fileSize) {
    return std::nullopt;
  }

  f.seek(paragraphLutOffset);
  uint16_t count;
  serialization::readPod(f, count);
  if (count == 0) {
    return std::nullopt;
  }

  const uint32_t lutEnd = liLutOffset + count * sizeof(uint16_t);
  if (lutEnd > fileSize) {
    return std::nullopt;
  }

  f.seek(liLutOffset);
  uint16_t resultPage = count - 1;
  for (uint16_t i = 0; i < count; i++) {
    uint16_t pageLiIdx;
    serialization::readPod(f, pageLiIdx);
    if (pageLiIdx >= liIndex) {
      resultPage = i;
      break;
    }
  }

  return resultPage;
}
