#include "TextViewerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include "components/UITheme.h"
#include "fontIds.h"

namespace {
// One sliding window over the file; the whole file is never loaded.
constexpr size_t CHUNK_SIZE = 4096;
constexpr size_t PAGE_STACK_RESERVE = 64;
constexpr int VIEWER_FONT_ID = UI_10_FONT_ID;
}  // namespace

void TextViewerActivity::onEnter() {
  Activity::onEnter();

  chunk = makeUniqueNoThrow<char[]>(CHUNK_SIZE);
  if (!chunk) {
    LOG_ERR("TextViewer", "OOM: %u bytes", static_cast<unsigned>(CHUNK_SIZE));
    openFailed = true;
  } else if (!Storage.openFileForRead("TextViewer", path, file)) {
    LOG_ERR("TextViewer", "Failed to open: %s", path.c_str());
    openFailed = true;
  } else {
    fileSize = static_cast<uint32_t>(file.size());
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(VIEWER_FONT_ID);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  maxLines = (contentBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;

  pageStarts.reserve(PAGE_STACK_RESERVE);
  pageStarts.push_back(0);
  lines.reserve(maxLines);
  layoutCurrentPage();
  requestUpdate();
}

void TextViewerActivity::onExit() {
  // Member handle: released here, not by a local's destructor.
  file.close();
  chunk.reset();
  lines.clear();
  pageStarts.clear();
  Activity::onExit();
}

// Returns the byte at `offset`, refilling the chunk window as needed, or -1 at
// EOF / on read error.
int TextViewerActivity::byteAt(const uint32_t offset) {
  if (openFailed || offset >= fileSize) return -1;
  if (offset < chunkStart || offset >= chunkStart + chunkLen) {
    if (!file.seek(offset)) return -1;
    const int n = file.read(chunk.get(), CHUNK_SIZE);
    if (n <= 0) return -1;
    chunkStart = offset;
    chunkLen = static_cast<uint32_t>(n);
  }
  return static_cast<unsigned char>(chunk[offset - chunkStart]);
}

// Lays out one page of wrapped lines starting at byte `start`. Fills `lines`
// and returns the byte offset the next page starts at (>= fileSize at EOF).
uint32_t TextViewerActivity::layoutPage(const uint32_t start) {
  lines.clear();

  std::string line;
  line.reserve(128);
  int lineWidth = 0;
  // Word-wrap state: length of `line` to keep, and the file offset to resume
  // from (just past the space), when the word being appended overflows.
  int lastSpaceLen = -1;
  uint32_t lastSpaceResume = 0;

  // Emits the completed line; returns true when the page is full.
  const auto emitLine = [&](std::string&& content) {
    lines.push_back(std::move(content));
    line.clear();
    lineWidth = 0;
    lastSpaceLen = -1;
    return static_cast<int>(lines.size()) >= maxLines;
  };

  uint32_t offset = start;
  while (offset < fileSize) {
    const int lead = byteAt(offset);
    if (lead < 0) break;  // read error: treat as EOF

    if (lead == '\n') {
      offset++;
      if (emitLine(std::move(line))) return offset;
      continue;
    }
    if (lead == '\r') {
      offset++;
      continue;
    }

    // Build the next atom: a tab becomes two spaces, other control bytes render
    // as nothing, and a lead byte >= 0x80 collects its UTF-8 continuation bytes
    // so multi-byte sequences never split across a wrap.
    char atom[8];
    int atomLen = 0;
    bool isSpace = false;
    const uint32_t atomStart = offset;
    if (lead == '\t') {
      atom[atomLen++] = ' ';
      atom[atomLen++] = ' ';
      isSpace = true;
      offset++;
    } else if (lead < 0x20) {
      offset++;
      continue;
    } else {
      atom[atomLen++] = static_cast<char>(lead);
      offset++;
      isSpace = lead == ' ';
      if (lead >= 0x80) {
        while (atomLen < 4) {
          const int cont = byteAt(offset);
          if (cont < 0 || (cont & 0xC0) != 0x80) break;
          atom[atomLen++] = static_cast<char>(cont);
          offset++;
        }
      }
    }
    atom[atomLen] = '\0';

    // getTextAdvanceX, not getTextWidth: width measures ink extents, so a lone
    // space is 0 wide and summed atoms under-count — lines then overflow the
    // screen. Advance matches what drawText actually moves per glyph.
    const int atomWidth = renderer.getTextAdvanceX(VIEWER_FONT_ID, atom, EpdFontFamily::REGULAR);
    if (lineWidth + atomWidth > maxWidth && !line.empty()) {
      if (isSpace) {
        // Break at this space and drop it.
        if (emitLine(std::move(line))) return offset;
        continue;
      }
      if (lastSpaceLen >= 0) {
        // Word-wrap: keep the line up to the last space and re-lay the
        // overflowing word out from the file on the next line.
        const uint32_t resume = lastSpaceResume;
        line.resize(lastSpaceLen);
        if (emitLine(std::move(line))) return resume;
        offset = resume;
        continue;
      }
      // No space to break at (one long word): char-wrap before this atom.
      if (emitLine(std::move(line))) return atomStart;
      offset = atomStart;
      continue;
    }

    if (isSpace) {
      lastSpaceLen = static_cast<int>(line.size());
      lastSpaceResume = offset;
    }
    line.append(atom, atomLen);
    lineWidth += atomWidth;
  }

  if (!line.empty()) lines.push_back(std::move(line));
  return fileSize;
}

void TextViewerActivity::layoutCurrentPage() { nextPageOffset = layoutPage(pageStarts.back()); }

void TextViewerActivity::pageForward() {
  if (nextPageOffset >= fileSize) return;  // at EOF: no wrap-around
  pageStarts.push_back(nextPageOffset);
  layoutCurrentPage();
  requestUpdate();
}

void TextViewerActivity::pageBack() {
  if (pageStarts.size() <= 1) return;
  pageStarts.pop_back();
  layoutCurrentPage();
  requestUpdate();
}

void TextViewerActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    pageForward();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    pageBack();
    return;
  }

  buttonNavigator.onNextRelease([this] { pageForward(); });
  buttonNavigator.onPreviousRelease([this] { pageBack(); });
  buttonNavigator.onNextContinuous([this] { pageForward(); });
  buttonNavigator.onPreviousContinuous([this] { pageBack(); });
}

void TextViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, displayTitle.c_str());

  if (lines.empty()) {
    renderer.drawText(VIEWER_FONT_ID, metrics.contentSidePadding, contentTop,
                      openFailed ? tr(STR_PAGE_LOAD_ERROR) : tr(STR_EMPTY_FILE));
  } else {
    for (size_t i = 0; i < lines.size(); i++) {
      renderer.drawText(VIEWER_FONT_ID, metrics.contentSidePadding, contentTop + static_cast<int>(i) * lineHeight,
                        lines[i].c_str());
    }
  }

  const bool atStart = pageStarts.size() <= 1;
  const bool atEnd = nextPageOffset >= fileSize;
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), "", atStart ? "" : tr(STR_DIR_UP), atEnd ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
