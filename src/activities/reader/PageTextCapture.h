#pragma once

// Flattening a laid-out Page into readaloud::buildCapture's inputs. Shared by
// the read-aloud capture (EpubReaderActivity) and whole-book Find (BookFinder),
// so the text Find searches is the text read-aloud speaks -- the same token
// walk, the same measured advances, the same barrier rule -- rather than a
// second copy of the walk that could drift from it.
//
// Header-only because both callers are also exercised host-side, and the host
// test links Page/TextBlock/GfxRenderer but not the reader activity.

#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>

#include <cstddef>
#include <vector>

#include "ReadAloudCapture.h"

namespace readaloud {

// Buffers for one page. Owned by the caller and cleared here on every call, so a
// loop over many pages keeps their capacity instead of allocating per page.
struct PageCaptureScratch {
  struct LineSpan {
    int x;
    int yTop;
    size_t begin;
    size_t count;
    bool barrierBefore;
  };
  std::vector<CaptureToken> tokens;
  std::vector<LineSpan> spans;
  std::vector<CaptureLine> lines;
  // A non-line element (image, rule) follows the page's last line, so a
  // line-final hyphen there does not join onto the next page.
  bool endsWithBarrier = false;
};

// Token text points straight into the page's block arenas: `lines` is valid
// only while `page` is alive. Each token's advance is measured HERE so the
// grouping in ReadAloudCapture.h stays pure -- and so the value that goes to 0
// for a non-resident font is passed in as data rather than re-queried where it
// cannot be seen.
inline void flattenPage(const Page& page, const GfxRenderer& renderer, const int fontId, const int xOffset,
                        const int yOffset, PageCaptureScratch& s) {
  s.tokens.clear();
  s.spans.clear();
  s.lines.clear();
  bool sawNonLineSinceLastLine = false;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) {
      // A non-line element (image, rule) breaks any pending hyphen join.
      sawNonLineSinceLastLine = true;
      continue;
    }
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || !block->valid() || block->isEmpty()) continue;
    const size_t begin = s.tokens.size();
    const uint16_t n = block->wordCount();
    for (uint16_t i = 0; i < n; i++) {
      const char* t = block->wordText(i);
      const int adv = tokenIsBlank(t) ? 0 : renderer.getTextAdvanceX(fontId, t, block->wordStyle(i));
      s.tokens.push_back({t, block->wordXpos(i), adv});
    }
    // yPos is the line's TOP, not its baseline — it is handed to block->render()
    // as the y origin (Page.cpp:24), and measuring the rendered panel confirms
    // it: with yOffset=9 and lineH=36, lines at yPos 0/54/90/126 put their ink
    // at y 15/68/104/137, i.e. yPos + yOffset plus a few px of internal leading
    // above cap height. Subtracting the ascender lifted every rect a full line,
    // so the highlight sat one line above the word being spoken.
    s.spans.push_back(
        {line.xPos + xOffset, line.yPos + yOffset, begin, s.tokens.size() - begin, sawNonLineSinceLastLine});
    sawNonLineSinceLastLine = false;
  }
  s.endsWithBarrier = sawNonLineSinceLastLine;

  // `tokens` is complete and will not reallocate; now point the lines at it.
  s.lines.reserve(s.spans.size());
  for (const auto& span : s.spans) {
    s.lines.push_back(CaptureLine{span.x, span.yTop, s.tokens.data() + span.begin, span.count, span.barrierBefore});
  }
}

}  // namespace readaloud
