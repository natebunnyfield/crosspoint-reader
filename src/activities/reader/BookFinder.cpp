#include "BookFinder.h"

#include <Arduino.h>
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <utility>

namespace {
// Reserved once and reused for every page. Sized for an ordinary reading page
// (the simulator's X3 pages measure 20-130 words and 130-710 bytes of text); a
// denser page grows the vector once and keeps the capacity. Kept modest on
// purpose: tokens are 12 B and rects 16 B each on the C3, so these three are
// ~7 KB of heap for as long as the search runs.
constexpr size_t kReserveTokens = 256;
constexpr size_t kReserveTextBytes = 2048;
}  // namespace

BookFinder::BookFinder(std::shared_ptr<Epub> epub, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                       const int fontId)
    : epub(std::move(epub)), renderer(renderer), spec(spec), fontId(fontId) {
  spineCount = this->epub ? this->epub->getSpineItemsCount() : 0;
  matcher.setQuery(query);
}

BookFinder::~BookFinder() { close(); }

void BookFinder::close() { section.reset(); }

int BookFinder::chapterNumber() const { return chaptersOpened < spineCount ? chaptersOpened : spineCount; }

bool BookFinder::begin(const std::string& raw, const int spine, const uint16_t page, const int32_t offset) {
  close();
  phase = Phase::Done;
  result = Status::NotFound;
  found = Hit{};
  chaptersOpened = 0;
  pagesScannedCount = 0;
  chaptersBuiltCount = 0;
  if (!epub || spineCount <= 0 || !query.set(raw)) return false;

  startSpine = spine < 0 ? 0 : (spine >= spineCount ? spineCount - 1 : spine);
  startPos = findtext::TextPos{page, offset};
  nextSpine = startSpine;
  curSpine = -1;
  phase = Phase::Forward;

  scratch.tokens.reserve(kReserveTokens);
  scratch.spans.reserve(64);
  scratch.lines.reserve(64);
  rects.reserve(kReserveTokens);
  pageText.reserve(kReserveTextBytes);
  LOG_DBG("FIND", "Find armed: %u codepoints from spine %d page %u offset %d", static_cast<unsigned>(query.size()),
          startSpine, page, static_cast<int>(offset));
  return true;
}

bool BookFinder::openNextChapter() {
  for (;;) {
    if (phase == Phase::Forward && nextSpine >= spineCount) {
      phase = Phase::Wrap;
      nextSpine = 0;
      LOG_DBG("FIND", "Reached the end of the book; wrapping to the start");
    }
    if (phase == Phase::Wrap && nextSpine > startSpine) return false;
    curSpine = nextSpine++;
    section = makeUniqueNoThrow<Section>(epub, curSpine, renderer);
    if (!section) {
      // Skipping one chapter is better than aborting the whole search.
      LOG_ERR("FIND", "OOM: Section for spine %d; skipping it", curSpine);
      continue;
    }
    chaptersOpened++;
    matcher.resetStream();
    curPage = 0;
    if (curSpine == startSpine && phase == Phase::Forward) {
      matcher.setBounds(&startPos, nullptr);
      curPage = startPos.page;
    } else if (curSpine == startSpine && phase == Phase::Wrap) {
      matcher.setBounds(nullptr, &startPos);
    } else {
      matcher.setBounds(nullptr, nullptr);
    }
    const bool loaded = section->loadSectionFile(spec);
    sectionComplete = loaded && !section->isPartial();
    LOG_DBG("FIND", "Chapter %d of %d (spine %d): %s; heap free=%u maxAlloc=%u", chapterNumber(), spineCount, curSpine,
            sectionComplete ? "laid out" : (loaded ? "partial, will extend" : "not laid out, will build"),
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return true;
  }
}

void BookFinder::closeChapter() { section.reset(); }

bool BookFinder::startChapterBuild() {
  bool started;
  if (lendFrameBuffer) {
    // The reader's own practice: the framebuffer's 48 KB covers the XHTML
    // inflate peak of startBuild only, and is restored (white) at scope exit.
    GfxRenderer::FrameBufferLoan loan(renderer);
    started = section->startBuild(spec);
  } else {
    started = section->startBuild(spec);
  }
  if (started) chaptersBuiltCount++;
  return started;
}

bool BookFinder::scanPage(const uint16_t pageIndex, findtext::TextPos& hitPos) {
  const auto page = section->loadPage(pageIndex);
  if (!page) {
    LOG_ERR("FIND", "Spine %d page %u did not load; skipping it", curSpine, pageIndex);
    return false;
  }
  pagesScannedCount++;
  readaloud::flattenPage(*page, renderer, fontId, 0, 0, scratch);
  readaloud::CaptureMetrics metrics;
  metrics.lineHeight = renderer.getLineHeight(fontId);
  metrics.spaceWidth = renderer.getSpaceWidth(fontId);
  metrics.fallbackLineHeight = 1;  // rects are discarded; any positive height
  pageText.clear();
  rects.clear();
  readaloud::buildCapture(scratch.lines.data(), scratch.lines.size(), metrics, pageText, rects);
  const bool barrierBefore = scratch.lines.empty() || scratch.lines.front().hyphenBarrierBefore;
  const bool joinable = !pageText.empty() && pageText.back() == '-' && !scratch.endsWithBarrier;
  return matcher.feedPage(pageIndex, pageText, barrierBefore, joinable, hitPos);
}

BookFinder::Status BookFinder::step() {
  if (phase == Phase::Done) return result;

  if (!section) {
    if (!openNextChapter()) {
      phase = Phase::Done;
      result = Status::NotFound;
      LOG_INF("FIND", "Not found (%u pages scanned, %u chapters laid out)", pagesScannedCount, chaptersBuiltCount);
    }
    return phase == Phase::Done ? result : Status::Running;
  }

  // The wrap's return to the start chapter ends once it is past the start page
  // and no match beginning at or before the start position can still complete.
  if (phase == Phase::Wrap && curSpine == startSpine && curPage > startPos.page && matcher.exhausted()) {
    closeChapter();
    return Status::Running;
  }

  if (curPage < section->pageCount) {
    findtext::TextPos hitPos;
    if (scanPage(curPage, hitPos)) {
      found = Hit{curSpine, hitPos.page, hitPos.offset};
      phase = Phase::Done;
      result = Status::Found;
      LOG_INF("FIND", "Found at spine %d page %u offset %d (%u pages scanned, %u chapters laid out)", found.spine,
              found.page, static_cast<int>(found.offset), pagesScannedCount, chaptersBuiltCount);
      // The section's destructor suspends an unfinished build into a partial
      // file whose pages include this one, so the reader opens it from the card.
      closeChapter();
      return result;
    }
    curPage++;
    return Status::Running;
  }

  if (section->isBuilding()) {
    if (!section->buildSomeMore(BUILD_PAGES_PER_STEP)) {
      LOG_ERR("FIND", "Layout failed in spine %d; skipping the rest of it", curSpine);
      closeChapter();
      return Status::Running;
    }
    if (section->isBuildComplete()) sectionComplete = true;
    return Status::Running;
  }

  if (!sectionComplete) {
    if (!startChapterBuild()) {
      LOG_ERR("FIND", "Could not start layout of spine %d; skipping it", curSpine);
      closeChapter();
    }
    return Status::Running;
  }

  closeChapter();  // every page of this chapter has been read
  return Status::Running;
}
