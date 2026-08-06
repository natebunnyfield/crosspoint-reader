#include "NoteEditorActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/BleHidHost.h"
#include "notes/EditorFonts.h"
#include "notes/HidKeymap.h"
#include "notes/MarkdownSpans.h"

namespace {
constexpr const char* TAG = "NOTEEDIT";
constexpr int EDITOR_FONT_ID_FALLBACK = UI_10_FONT_ID;

// The Editor Font setting picks from the editor-group families (see
// src/notes/EditorFonts.h). If that family is not installed on the card the UI
// face stands in, so the screen is never blank because of a missing font.
int resolveEditorFont() {
  const char* family = editorfonts::selectedFamily(SETTINGS.editorFont);
  if (SETTINGS.sdFontIdResolver != nullptr) {
    const int id = SETTINGS.sdFontIdResolver(SETTINGS.sdFontResolverCtx, family, 12);
    if (id != 0) return id;
  }
  return EDITOR_FONT_ID_FALLBACK;
}
}  // namespace

// getTextWidth() does not count a TRAILING space, so measuring each styled span
// on its own loses the gap before the next one — "Plain **bold** and" rendered
// as "Plainbold and". Measuring with a sentinel appended keeps the space's
// advance, then subtracts the sentinel back off.
int NoteEditorActivity::advanceOf(const char* piece, EpdFontFamily::Style style) const {
  char probe[200];
  snprintf(probe, sizeof(probe), "%s|", piece);
  return renderer.getTextWidth(editorFontId, probe, style) - renderer.getTextWidth(editorFontId, "|", style);
}

void NoteEditorActivity::onEnter() {
  Activity::onEnter();

  // BLE before the buffer: nimble_port_init() wants ~65 KB contiguous and hangs
  // rather than failing when it cannot get it, so give it the roomiest heap.
  blekbd::begin();

  storage = makeUniqueNoThrow<char[]>(BUF_SIZE);
  if (!storage) {
    LOG_ERR(TAG, "OOM: %u bytes", (unsigned)BUF_SIZE);
    return;
  }
  buf = makeUniqueNoThrow<textbuf::TextBuffer>(storage.get(), BUF_SIZE);
  if (!buf) {
    LOG_ERR(TAG, "OOM: TextBuffer");
    return;
  }

  editorFontId = resolveEditorFont();
  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(editorFontId);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  maxLines = (contentBottom - contentTop) / lineHeight;
  if (maxLines < 1) maxLines = 1;
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2;

  // Load an existing note so Edit-from-Manage-Files is a real edit, not a
  // silent overwrite. Anything past the cap is refused rather than truncated.
  HalFile f;
  if (Storage.openFileForRead(TAG, path, f)) {
    const size_t size = static_cast<size_t>(f.size());
    if (size >= BUF_SIZE) {
      LOG_ERR(TAG, "%s is %u bytes, cap is %u — refusing to open", path.c_str(), (unsigned)size, (unsigned)BUF_SIZE);
      bufferFull = true;
    } else {
      char chunk[256];
      int n;
      while ((n = f.read(chunk, sizeof(chunk))) > 0) {
        for (int i = 0; i < n; ++i) buf->insert(chunk[i]);
      }
      buf->cursorTo(0);
      LOG_INF(TAG, "loaded %u bytes from %s", (unsigned)buf->size(), path.c_str());
    }
  }

  lines.reserve(64);
  relayout();
  requestUpdate();
}

void NoteEditorActivity::onExit() {
  save();
  blekbd::end();
  buf.reset();
  storage.reset();
  Activity::onExit();
}

// Rebuild the soft-wrapped display lines. O(n) over the buffer, and only run
// when the text actually changes — not per render.
void NoteEditorActivity::relayout() {
  lines.clear();
  if (!buf) return;

  const char* text = buf->data();
  const size_t len = buf->size();
  char probe[256];
  size_t i = 0;

  while (i <= len) {
    const size_t start = i;
    size_t lastBreak = 0;
    size_t j = i;
    bool hard = false;

    while (j < len) {
      if (text[j] == '\n') {
        hard = true;
        break;
      }
      const size_t cand = j + 1 - start;
      if (cand >= sizeof(probe)) break;
      memcpy(probe, text + start, cand);
      probe[cand] = '\0';
      if (renderer.getTextWidth(editorFontId, probe) > maxWidth) break;
      if (text[j] == ' ') lastBreak = j + 1;
      ++j;
    }

    size_t end = j;
    if (!hard && j < len && lastBreak > start) end = lastBreak;
    lines.push_back({static_cast<uint16_t>(start), static_cast<uint16_t>(end)});

    if (hard) {
      i = end + 1;
      if (i > len) break;
      if (i == len) {  // trailing newline: show the empty line it creates
        lines.push_back({static_cast<uint16_t>(len), static_cast<uint16_t>(len)});
        break;
      }
    } else {
      if (end >= len) break;
      i = end;
    }
  }
  if (lines.empty()) lines.push_back({0, 0});
}

size_t NoteEditorActivity::lineOfCursor() const {
  const size_t c = buf ? buf->cursor() : 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (c >= lines[i].start && c <= lines[i].end) return i;
  }
  return lines.empty() ? 0 : lines.size() - 1;
}

// Scroll only as far as needed to bring the cursor back on screen, so typing
// mid-document does not jump the view.
void NoteEditorActivity::ensureCursorVisible() {
  const size_t cl = lineOfCursor();
  if (cl < topLine) {
    topLine = cl;
  } else if (cl >= topLine + static_cast<size_t>(maxLines)) {
    topLine = cl - maxLines + 1;
  }
}

void NoteEditorActivity::pageUp() {
  const size_t step = static_cast<size_t>(maxLines) > 1 ? maxLines - 1 : 1;
  topLine = topLine > step ? topLine - step : 0;
  if (buf && !lines.empty()) buf->cursorTo(lines[topLine].start);
}

void NoteEditorActivity::pageDown() {
  const size_t step = static_cast<size_t>(maxLines) > 1 ? maxLines - 1 : 1;
  if (topLine + step < lines.size()) topLine += step;
  if (buf && topLine < lines.size()) buf->cursorTo(lines[topLine].start);
}

void NoteEditorActivity::handleKey(int key) {
  if (!buf) return;
  switch (key) {
    case hidkeymap::KEY_LEFT:
      buf->cursorLeft();
      break;
    case hidkeymap::KEY_RIGHT:
      buf->cursorRight();
      break;
    case hidkeymap::KEY_UP:
      buf->cursorUp();
      break;
    case hidkeymap::KEY_DOWN:
      buf->cursorDown();
      break;
    case hidkeymap::KEY_HOME:
      buf->cursorHome();
      break;
    case hidkeymap::KEY_END:
      buf->cursorEnd();
      break;
    case hidkeymap::KEY_PAGE_UP:
      pageUp();
      break;
    case hidkeymap::KEY_PAGE_DOWN:
      pageDown();
      break;
    case hidkeymap::KEY_DELETE:
      buf->del();
      break;
    case '\b':
      buf->backspace();
      break;
    default:
      if (key < 0x100 && !buf->insert(static_cast<char>(key))) {
        bufferFull = true;  // surfaced on screen; never silently dropped
        LOG_ERR(TAG, "buffer full at %u bytes", (unsigned)buf->size());
      }
      break;
  }
}

void NoteEditorActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  // Side buttons page too, so the note is readable without a keyboard.
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    pageUp();
    requestUpdate();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    pageDown();
    requestUpdate();
    return;
  }

  bool got = false;
  for (int c = blekbd::popChar(); c >= 0; c = blekbd::popChar()) {
    handleKey(c);
    got = true;
    ++pendingChars;
  }

  if (got) {
    lastKeyMs = millis();
    dirty = true;
  }

  if (dirty && (pendingChars >= FLUSH_CHARS || millis() - lastKeyMs >= DEBOUNCE_MS)) {
    dirty = false;
    pendingChars = 0;
    relayout();
    ensureCursorVisible();
    requestUpdate();
  }
}

// Draw one display line with markdown styling. Spans come from MarkdownSpans
// (host-tested); this function only turns them into draw calls.
void NoteEditorActivity::drawLine(const char* text, size_t len, int y, bool showCursor, size_t cursorCol) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const mdspans::Line md = mdspans::analyze(text, len);

  int x = metrics.contentSidePadding + md.indent * (lineHeight);
  const char* body = text + md.bodyStart;

  // List/quote markers stay visible so the source is still recognisable while
  // editing. The numbered marker is copied from the text rather than invented,
  // so "3." and "12)" keep their real value.
  if (md.block == mdspans::Block::Bullet) {
    renderer.drawText(editorFontId, metrics.contentSidePadding, y, "-");
  } else if (md.block == mdspans::Block::Quote) {
    renderer.drawText(editorFontId, metrics.contentSidePadding, y, ">");
  } else if (md.block == mdspans::Block::Numbered) {
    char marker[8];
    size_t m = 0;
    for (size_t k = 0; k < md.bodyStart && m < sizeof(marker) - 1; ++k) {
      if (text[k] != ' ' && text[k] != '\t') marker[m++] = text[k];
    }
    marker[m] = '\0';
    renderer.drawText(editorFontId, metrics.contentSidePadding, y, marker);
  }

  char piece[192];
  for (size_t i = 0; i < md.spanCount; ++i) {
    const mdspans::Span& sp = md.spans[i];
    size_t n = sp.len;
    if (n > sizeof(piece) - 1) n = sizeof(piece) - 1;
    memcpy(piece, body + sp.start, n);
    piece[n] = '\0';

    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    if (mdspans::blockIsBold(md.block) || sp.style == mdspans::Style::Bold) {
      style = EpdFontFamily::BOLD;
    } else if (sp.style == mdspans::Style::Italic) {
      style = EpdFontFamily::ITALIC;
    }
    renderer.drawText(editorFontId, x, y, piece, true, style);
    x += advanceOf(piece, style);
  }

  if (showCursor) {
    // Caret drawn at the cursor's column, measured in the raw source text so it
    // tracks what the typist is actually editing.
    char upto[192];
    size_t n = cursorCol < sizeof(upto) - 1 ? cursorCol : sizeof(upto) - 1;
    memcpy(upto, text, n);
    upto[n] = '\0';
    const int cx = metrics.contentSidePadding + renderer.getTextWidth(editorFontId, upto);
    renderer.drawLine(cx, y, cx, y + lineHeight - 2, true);
  }
}

void NoteEditorActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();

  char header[72];
  const size_t page = maxLines > 0 ? topLine / maxLines + 1 : 1;
  const size_t pages = maxLines > 0 ? (lines.size() + maxLines - 1) / maxLines : 1;
  snprintf(header, sizeof(header), "%s  %u/%u", path.c_str(), (unsigned)page, (unsigned)(pages ? pages : 1));
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);

  const size_t cl = lineOfCursor();
  for (size_t n = 0; n < static_cast<size_t>(maxLines) && topLine + n < lines.size(); ++n) {
    const DisplayLine& dl = lines[topLine + n];
    const size_t len = dl.end > dl.start ? dl.end - dl.start : 0;
    const bool onThisLine = (topLine + n) == cl;
    drawLine(buf ? buf->data() + dl.start : "", len, contentTop + static_cast<int>(n) * lineHeight, onThisLine,
             onThisLine && buf ? buf->cursor() - dl.start : 0);
  }

  char status[96];
  snprintf(status, sizeof(status), "%u ch%s  %s", (unsigned)(buf ? buf->size() : 0), bufferFull ? "  BUFFER FULL" : "",
           blekbd::stateName());
  const int statusY = renderer.getScreenHeight() - metrics.buttonHintsHeight - renderer.getLineHeight(SMALL_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, statusY, status);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "PgUp", "PgDn");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

bool NoteEditorActivity::save() {
  if (!buf || buf->empty()) {
    LOG_INF(TAG, "nothing to save");
    return false;
  }
  HalFile f;
  if (!Storage.openFileForWrite(TAG, path, f)) {
    LOG_ERR(TAG, "cannot open %s for write", path.c_str());
    return false;
  }
  const size_t written = f.write(buf->data(), buf->size());
  f.flush();
  savedOk = written == buf->size();
  LOG_INF(TAG, "saved %u/%u bytes to %s", (unsigned)written, (unsigned)buf->size(), path.c_str());
  return savedOk;
}
