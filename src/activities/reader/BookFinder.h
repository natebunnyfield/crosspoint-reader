#pragma once

// Whole-book Find: the engine. Walks the book forward from a reading position,
// one laid-out page at a time, and stops at the first page whose rendered text
// contains the query (docs/find-in-book-options-2026-09-28.md section 9).
//
// ORDER. The rest of the start chapter from the start page, then every later
// chapter, then a WRAP from the book's first chapter back up to the start
// position. The first accepted match wins; nothing collects a list of hits.
//
// ONE PAGE, ONE SECTION. Exactly one Section is open at a time, and one Page is
// deserialized at a time, into scratch buffers that keep their capacity from
// page to page. A chapter with no finished layout on the card is laid out with
// the reader's own Section build and the reader's own render spec, so the
// section file it leaves behind is the one reading that chapter would have
// written anyway.
//
// COOPERATIVE. step() does one bounded unit of work -- scan one page, lay out
// one chunk of pages, or open the next chapter -- and returns, so the caller
// can poll for cancel between units. The only unit that is not bounded by pages
// is Section::startBuild (inflating a chapter's XHTML), exactly as for reading.
//
// Pure of UI: no activity, no settings, no drawing. The host test drives this
// class on a real .epub.

#include <Epub.h>
#include <Epub/Section.h>

#include <cstdint>
#include <memory>
#include <string>

#include "FindMatcher.h"
#include "PageTextCapture.h"

class GfxRenderer;

class BookFinder {
 public:
  enum class Status : uint8_t { Running, Found, NotFound };

  struct Hit {
    int spine = -1;
    uint16_t page = 0;
    // Byte offset of the match's first character in the page's capture text
    // (readaloud::buildCapture). Feeding it back as the start offset asks for
    // the NEXT instance, which may be on the same page.
    int32_t offset = findtext::kPageStart;
  };

  // Pages laid out per step() when a chapter has to be built. The reader's own
  // blocking catch-up chunk (EpubReaderActivity::BUILD_PAGES_PER_CHUNK).
  static constexpr int BUILD_PAGES_PER_STEP = 8;

  BookFinder(std::shared_ptr<Epub> epub, GfxRenderer& renderer, const ReaderRenderSpec& spec, int fontId);
  ~BookFinder();
  BookFinder(const BookFinder&) = delete;
  BookFinder& operator=(const BookFinder&) = delete;

  // Lend the framebuffer to Section::startBuild, as the reader does for the
  // XHTML inflate peak. Off by default (the host test has no panel).
  void setLendFrameBuffer(const bool lend) { lendFrameBuffer = lend; }

  // Arm a search. Matches are accepted strictly after (startSpine, startPage,
  // startOffset); findtext::kPageStart searches the start page from its top.
  // False when the query has nothing searchable in it.
  bool begin(const std::string& query, int startSpine, uint16_t startPage, int32_t startOffset);

  // One bounded unit of work. Running until the search ends; then Found or
  // NotFound, and every later call returns the same answer.
  Status step();

  // Release the open section now (a cancel). Its destructor suspends any
  // in-progress build into a partial section file, as leaving the reader does.
  void close();

  const Hit& hit() const { return found; }
  // 1-based count of chapters opened so far, clamped to the book's spine count
  // (the wrap re-enters the start chapter, which would otherwise read N+1 of N).
  int chapterNumber() const;
  int chapterTotal() const { return spineCount; }
  uint32_t pagesScanned() const { return pagesScannedCount; }
  uint32_t chaptersBuilt() const { return chaptersBuiltCount; }

 private:
  enum class Phase : uint8_t { Forward, Wrap, Done };

  std::shared_ptr<Epub> epub;
  GfxRenderer& renderer;
  const ReaderRenderSpec spec;
  const int fontId;
  bool lendFrameBuffer = false;
  int spineCount = 0;

  findtext::Query query;
  findtext::Matcher matcher;
  readaloud::PageCaptureScratch scratch;
  std::string pageText;
  // buildCapture wants a rect sink; Find ignores the rects, but keeping the
  // vector across pages means it allocates once, not once per page.
  struct Rect {
    uint16_t x, y, w, h;
    uint32_t byteOffset;
    uint16_t byteLen;
  };
  std::vector<Rect> rects;

  int startSpine = 0;
  findtext::TextPos startPos;

  Phase phase = Phase::Done;
  Status result = Status::NotFound;
  int nextSpine = 0;
  int curSpine = -1;
  uint16_t curPage = 0;
  // Stop scanning this chapter after this page (the wrap's return to the start
  // chapter only needs to reach the start page, plus one for a match that
  // starts on it and runs over the edge). -1 = no limit.
  int lastPage = -1;
  std::unique_ptr<Section> section;
  // The open section's layout is finished: every page exists (finalized file,
  // or a build that has completed). False for no file, or a partial whose
  // watermark still has to be extended.
  bool sectionComplete = false;
  int chaptersOpened = 0;
  uint32_t pagesScannedCount = 0;
  uint32_t chaptersBuiltCount = 0;
  Hit found;

  bool openNextChapter();
  void closeChapter();
  bool startChapterBuild();
  bool scanPage(uint16_t page, findtext::TextPos& hitPos);
};
