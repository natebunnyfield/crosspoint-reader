#include "TextViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include "Epub/converters/ImageDecoderFactory.h"
#include "TextAntiAliasing.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/MarkdownRender.h"

namespace {
// One sliding window over the file; the whole file is never loaded.
constexpr size_t CHUNK_SIZE = 4096;
constexpr size_t PAGE_STACK_RESERVE = 64;
constexpr int VIEWER_FONT_ID = UI_10_FONT_ID;
constexpr int MIN_THUMB_HEIGHT = 12;
// A markdown source line is read whole before it is wrapped, so it needs a
// bound. Longer lines are cut here and the remainder is laid out as the next
// source line -- which is a wrap in the middle of a sentence, but only for a
// line already four times the screen's width.
constexpr size_t MD_MAX_SOURCE_LINE = 512;
// Visual lines one source line may produce. A page cannot show more than
// maxLines anyway; this only bounds the stack array.
constexpr size_t MD_MAX_FRAGMENTS = 32;
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

  if (!openFailed) {
    // .md opens STYLED. It is the only kind whose pretty view is a way of
    // drawing rather than a document to generate, so it does not go through
    // PrettyView at all (see the header). Confirm still swaps to the source.
    markdownFile = FsHelpers::hasMarkdownExtension(path);
    markdownMode = markdownFile;
    prettyKind = PrettyView::detect(path, fileSize);
    // Raw bytes are illegible for images, books and binary caches — open those
    // in pretty view; json/html open raw (they are already text).
    if (prettyKind != PrettyView::Kind::None && prettyKind != PrettyView::Kind::Json &&
        prettyKind != PrettyView::Kind::Html) {
      prettyMode = true;
      if (!imageMode()) {
        prettyText = PrettyView::generate(prettyKind, path);
        if (prettyText.empty()) {
          prettyMode = false;
          prettyUnavailable = true;
        }
      }
    }
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(VIEWER_FONT_ID);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  maxLines = (contentBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;
  // Keep clear of the persistent scrollbar on the right edge.
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - metrics.scrollBarWidth -
             metrics.scrollBarRightOffset;

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
  prettyText.clear();
  prettyText.shrink_to_fit();
  Activity::onExit();
}

// Size of whatever the pagination is currently reading (file or pretty doc).
uint32_t TextViewerActivity::sourceSize() const {
  if (prettyMode && !imageMode()) return static_cast<uint32_t>(prettyText.size());
  return fileSize;
}

// Returns the byte at `offset` of the current source, refilling the chunk
// window as needed for file reads, or -1 at EOF / on read error.
int TextViewerActivity::byteAt(const uint32_t offset) {
  if (prettyMode && !imageMode()) {
    if (offset >= prettyText.size()) return -1;
    return static_cast<unsigned char>(prettyText[offset]);
  }
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
// and returns the byte offset the next page starts at (>= sourceSize at EOF).
uint32_t TextViewerActivity::layoutPage(const uint32_t start) {
  // Lay out into a LOCAL vector and publish it under the render lock, rather
  // than mutating `lines` in place.
  //
  // This runs on the activity task, from loop() via pageForward/pageBack, and
  // ActivityManager::loop() deliberately holds no lock ("the loop() method must
  // be responsible for acquire one if needed", ActivityManager.cpp:77). The
  // render task meanwhile walks `lines` inside render(). Clearing it here freed
  // the very strings render() was reading -- hold Down to page quickly and the
  // two overlap.
  //
  // The publish is a swap, so the lock is held for O(1) and never across the
  // file reads and text measurement below. The committer is RAII because this
  // function returns from five places.
  std::vector<std::string> built;
  struct Publish {
    TextViewerActivity* self;
    std::vector<std::string>* built;
    ~Publish() {
      RenderLock lock;
      self->lines.swap(*built);
    }
  } publish{this, &built};

  if (markdownMode) return layoutMarkdownPage(start, built);

  const uint32_t srcSize = sourceSize();

  std::string line;
  line.reserve(128);
  int lineWidth = 0;
  // Word-wrap state: length of `line` to keep, and the source offset to resume
  // from (just past the space), when the word being appended overflows.
  int lastSpaceLen = -1;
  uint32_t lastSpaceResume = 0;

  // Emits the completed line; returns true when the page is full.
  const auto emitLine = [&](std::string&& content) {
    built.push_back(std::move(content));
    line.clear();
    lineWidth = 0;
    lastSpaceLen = -1;
    return static_cast<int>(built.size()) >= maxLines;
  };

  uint32_t offset = start;
  while (offset < srcSize) {
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

  if (!line.empty()) built.push_back(std::move(line));
  return srcSize;
}

// Markdown pagination.
//
// The plain path above wraps atom by atom, which cannot work here: a wrap
// decision depends on the whole line's block kind and on which runs are styled,
// and both come from mdspans::analyze over a complete source line. So this
// reads one source line at a time and hands it to mdrender::wrapLine, which
// measures candidates with the SAME call the draw will use -- the wrap and the
// drawing therefore cannot disagree, which is the whole failure this file's
// sibling bug was made of.
//
// The returned offset is a byte position in the FILE, always at the start of a
// fragment or of a source line, so paging back and forward replays exactly.
// Resuming mid-line is safe because wrapLine is greedy left to right: wrapping
// the tail from a break point yields the same breaks as continuing would.
uint32_t TextViewerActivity::layoutMarkdownPage(const uint32_t start, std::vector<std::string>& built) {
  const uint32_t srcSize = sourceSize();
  uint32_t offset = start;

  std::string source;
  source.reserve(128);

  while (offset < srcSize && static_cast<int>(built.size()) < maxLines) {
    // Collect one source line (or MD_MAX_SOURCE_LINE bytes of it).
    //
    // Byte i of `source` is file byte lineStart + i, and it has to stay that
    // way: a fragment's offset is handed back as the next page's start. So
    // control bytes are SUBSTITUTED, never dropped or expanded -- a tab that
    // became two spaces, or a CR that vanished, would shift every offset after
    // it and make paging land in the wrong place.
    source.clear();
    const uint32_t lineStart = offset;
    while (offset < srcSize && source.size() < MD_MAX_SOURCE_LINE) {
      const int b = byteAt(offset);
      if (b < 0) break;  // read error: treat as EOF
      offset++;
      if (b == '\n') break;
      source += (b < 0x20 || b == 0x7F) ? ' ' : static_cast<char>(b);
    }
    // A CRLF leaves its CR as a trailing space, which measures as one. Only the
    // end of the line is trimmed, so no offset moves.
    while (!source.empty() && source.back() == ' ') source.pop_back();

    if (source.empty()) {
      built.emplace_back();  // blank source line keeps its blank row
      continue;
    }

    mdrender::Fragment frags[MD_MAX_FRAGMENTS];
    const size_t n = mdrender::wrapLine(renderer, VIEWER_FONT_ID, lineHeight, source.c_str(), source.size(), maxWidth,
                                        frags, MD_MAX_FRAGMENTS);
    for (size_t i = 0; i < n; ++i) {
      if (static_cast<int>(built.size()) >= maxLines) {
        // Page is full mid-line: resume at this fragment's byte in the file.
        return lineStart + frags[i].start;
      }
      built.emplace_back(source, frags[i].start, frags[i].end - frags[i].start);
    }
  }

  return offset >= srcSize ? srcSize : offset;
}

void TextViewerActivity::layoutCurrentPage() { nextPageOffset = layoutPage(pageStarts.back()); }

void TextViewerActivity::resetPagination() {
  pageStarts.clear();
  pageStarts.push_back(0);
  layoutCurrentPage();
}

void TextViewerActivity::togglePretty() {
  if (markdownFile) {
    // Styled <-> source. Nothing to generate and nothing that can fail, so
    // there is no unavailable state to fall into.
    markdownMode = !markdownMode;
    resetPagination();
    requestUpdate();
    return;
  }
  if (prettyKind == PrettyView::Kind::None || prettyUnavailable) return;
  if (!prettyMode && !imageMode() && prettyText.empty() && prettyKind != PrettyView::Kind::ImageBmp &&
      prettyKind != PrettyView::Kind::ImageDecoded) {
    prettyText = PrettyView::generate(prettyKind, path);
    if (prettyText.empty()) {
      prettyUnavailable = true;  // malformed/unreadable: stay raw for good
      return;
    }
  }
  prettyMode = !prettyMode;
  resetPagination();
  requestUpdate();
}

void TextViewerActivity::pageForward() {
  if (imageMode()) return;
  if (nextPageOffset >= sourceSize()) return;  // at EOF: no wrap-around
  pageStarts.push_back(nextPageOffset);
  layoutCurrentPage();
  requestUpdate();
}

void TextViewerActivity::pageBack() {
  if (imageMode()) return;
  if (pageStarts.size() <= 1) return;
  pageStarts.pop_back();
  layoutCurrentPage();
  requestUpdate();
}

void TextViewerActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    togglePretty();
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

// Persistent right-edge scrollbar: thumb position = page start / source size,
// thumb height = the page's share of the source. Always drawn in text modes so
// the file's size is visible at a glance (v2 plan ruling).
void TextViewerActivity::drawScrollBar() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const uint32_t srcSize = sourceSize();
  const int trackX = renderer.getScreenWidth() - metrics.scrollBarRightOffset - metrics.scrollBarWidth;
  const int trackTop = contentTop;
  const int trackHeight = maxLines * lineHeight;
  if (trackHeight <= 0) return;

  renderer.drawRect(trackX, trackTop, metrics.scrollBarWidth, trackHeight);

  const uint32_t pageStart = pageStarts.back();
  const uint32_t pageLen = nextPageOffset > pageStart ? nextPageOffset - pageStart : 0;
  int thumbY = trackTop;
  int thumbH = trackHeight;
  if (srcSize > 0) {
    thumbH = static_cast<int>(static_cast<uint64_t>(trackHeight) * pageLen / srcSize);
    if (thumbH < MIN_THUMB_HEIGHT) thumbH = MIN_THUMB_HEIGHT;
    if (thumbH > trackHeight) thumbH = trackHeight;
    thumbY = trackTop + static_cast<int>(static_cast<uint64_t>(trackHeight - thumbH) * pageStart /
                                         (srcSize - pageLen > 0 ? srcSize - pageLen : 1));
  }
  renderer.fillRect(trackX, thumbY, metrics.scrollBarWidth, thumbH, true);
}

// Renders the image kinds into the content area, centered and fit.
void TextViewerActivity::renderImage() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int areaX = metrics.contentSidePadding;
  const int areaW = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int areaH = maxLines * lineHeight;

  bool ok = false;
  if (prettyKind == PrettyView::Kind::ImageBmp) {
    HalFile bmpFile;
    if (Storage.openFileForRead("TextViewer", path, bmpFile)) {
      Bitmap bitmap(bmpFile, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        int w = bitmap.getWidth();
        int h = bitmap.getHeight();
        int x = areaX;
        int y = contentTop;
        if (w <= areaW && h <= areaH) {
          x = areaX + (areaW - w) / 2;
          y = contentTop + (areaH - h) / 2;
        }
        renderer.drawBitmap(bitmap, x, y, areaW, areaH, 0, 0);
        ok = true;
      }
    }
  } else {
    auto* decoder = ImageDecoderFactory::getDecoder(path);
    if (decoder) {
      RenderConfig config;
      config.x = areaX;
      config.y = contentTop;
      config.maxWidth = areaW;
      config.maxHeight = areaH;
      // Center images that fit; oversized ones scale within the area instead.
      ImageDimensions dims{};
      if (decoder->getDimensions(path, dims) && dims.width <= areaW && dims.height <= areaH) {
        config.x = areaX + (areaW - dims.width) / 2;
        config.y = contentTop + (areaH - dims.height) / 2;
      }
      ok = decoder->decodeToFramebuffer(path, renderer, config);
    }
  }
  if (!ok) {
    renderer.drawCenteredText(VIEWER_FONT_ID, contentTop + areaH / 2, tr(STR_IMAGE_DECODE_FAILED));
  }
}

// Everything the viewer puts on the panel, minus the clear and the refresh.
// Split out so the grayscale overlay re-runs the identical body (see render())
// instead of a text-only copy of it that would drift.
void TextViewerActivity::drawFrame() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, displayTitle.c_str());

  if (imageMode()) {
    renderImage();
  } else if (lines.empty()) {
    renderer.drawText(VIEWER_FONT_ID, metrics.contentSidePadding, contentTop,
                      openFailed ? tr(STR_PAGE_LOAD_ERROR) : tr(STR_EMPTY_FILE));
  } else if (markdownMode) {
    // Styled, through the same renderer the note editor and the Editor Font
    // specimen draw with. maxX bounds the right edge as a backstop; wrapLine
    // already fitted every fragment, so it should never cut.
    const int rightBound = metrics.contentSidePadding + maxWidth;
    for (size_t i = 0; i < lines.size(); i++) {
      if (lines[i].empty()) continue;
      mdrender::drawLine(renderer, VIEWER_FONT_ID, lineHeight, metrics.contentSidePadding,
                         contentTop + static_cast<int>(i) * lineHeight, lines[i].c_str(), lines[i].size(), rightBound);
    }
  } else {
    for (size_t i = 0; i < lines.size(); i++) {
      renderer.drawText(VIEWER_FONT_ID, metrics.contentSidePadding, contentTop + static_cast<int>(i) * lineHeight,
                        lines[i].c_str());
    }
  }
  if (!imageMode()) drawScrollBar();

  const bool atStart = pageStarts.size() <= 1;
  const bool atEnd = nextPageOffset >= sourceSize();
  const bool canToggle = markdownFile || (prettyKind != PrettyView::Kind::None && !prettyUnavailable);
  const char* toggleLabel = "";
  if (markdownFile) {
    toggleLabel = markdownMode ? tr(STR_RAW) : tr(STR_PRETTY);
  } else if (canToggle) {
    toggleLabel = prettyMode ? tr(STR_RAW) : tr(STR_PRETTY);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), toggleLabel, (imageMode() || atStart) ? "" : tr(STR_DIR_UP),
                                            (imageMode() || atEnd) ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void TextViewerActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawFrame();
  renderer.displayBuffer();

  // Text pages only. TextOnlyScope would stop an image reaching the plane, but
  // it would not stop renderImage() re-opening the file and re-decoding it once
  // per band -- the expensive half -- and an image page has no glyph coverage
  // to lift anyway.
  if (imageMode()) return;
  TextAa::overlayChromeIfEnabled(renderer, [this] {
    const GfxRenderer::TextOnlyScope textOnly(renderer);
    drawFrame();
  });
}
