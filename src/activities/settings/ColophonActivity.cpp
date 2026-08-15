#include "ColophonActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

#include "ColophonData.h"
#include "MappedInputManager.h"
#include "TextAntiAliasing.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int BODY_FONT_ID = UI_10_FONT_ID;
constexpr int MIN_THUMB_HEIGHT = 12;
// Addresses and technology details sit under the name they belong to rather
// than against the margin, so the list reads as pairs and not as one column.
constexpr int DETAIL_INDENT = 16;
// Longest address in the table is 51 characters; this leaves room for the
// separator, the date and a good deal of growth.
constexpr size_t META_BUF = 96;
// wrappedText() ellipsises anything past maxLines, and an ellipsised credit is
// worse than none. Nothing in these tables comes near 200 lines.
constexpr int WRAP_LINE_CEILING = 200;
}  // namespace

void ColophonActivity::onEnter() {
  Activity::onEnter();

  const auto& metrics = UITheme::getInstance().getMetrics();
  lineHeight = renderer.getLineHeight(BODY_FONT_ID);
  if (lineHeight < 1) lineHeight = 1;
  contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  linesPerPage = (contentBottom - contentTop) / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;
  // Keep clear of the persistent scrollbar on the right edge.
  maxWidth = renderer.getScreenWidth() - metrics.contentSidePadding * 2 - metrics.scrollBarWidth -
             metrics.scrollBarRightOffset;
  if (maxWidth < 1) maxWidth = 1;

  layout();
  requestUpdate();
}

void ColophonActivity::formatPersonMeta(const uint16_t index, char* buf, const size_t bufLen) {
  const auto& person = COLOPHON_PEOPLE[index];
  // U+00B7 MIDDLE DOT, the separator FontDisplayNames::subtitle already uses.
  snprintf(buf, bufLen, "%s \xC2\xB7 %s", person.email, person.firstContribution);
}

void ColophonActivity::appendWrapped(const char* text, const LineKind kind, const int indent) {
  if (text == nullptr || *text == '\0') return;
  auto wrapped = renderer.wrappedText(BODY_FONT_ID, text, maxWidth - indent, WRAP_LINE_CEILING);
  for (auto& wrappedLine : wrapped) {
    prose.push_back(std::move(wrappedLine));
    lines.push_back(Line{kind, static_cast<uint8_t>(indent), static_cast<uint16_t>(prose.size() - 1)});
  }
}

// GfxRenderer::wrappedText breaks on spaces and ellipsises any single word too
// wide to fit -- and an address is exactly that: one unbreakable token. Several
// of the users.noreply.github.com forms are over 50 characters, so left to
// wrappedText they arrive on screen cut off mid-domain, which credits nobody.
// Break them at their own seams instead, and at a character boundary if a run
// between seams is still too long.
void ColophonActivity::appendAddress(const char* text, const int indent) {
  const int width = maxWidth - indent;
  const auto push = [this, indent](std::string&& value) {
    prose.push_back(std::move(value));
    lines.push_back(Line{LineKind::Prose, static_cast<uint8_t>(indent), static_cast<uint16_t>(prose.size() - 1)});
  };
  if (width < 1 || text == nullptr) {  // degenerate geometry: one line, uncut
    push(std::string(text == nullptr ? "" : text));
    return;
  }

  const size_t len = strlen(text);
  std::string line;
  size_t lastSeam = 0;  // byte offset in `line` just past the last seam character
  size_t i = 0;
  while (i < len) {
    // Advance by a whole UTF-8 sequence so a multi-byte codepoint is never split.
    size_t seq = 1;
    while (i + seq < len && (static_cast<unsigned char>(text[i + seq]) & 0xC0) == 0x80) seq++;

    std::string candidate = line;
    candidate.append(text + i, seq);
    if (!line.empty() && renderer.getTextAdvanceX(BODY_FONT_ID, candidate.c_str(), EpdFontFamily::REGULAR) > width) {
      // Emit up to the last seam if there was one, otherwise wherever we are.
      const size_t cut = lastSeam > 0 ? lastSeam : line.size();
      push(line.substr(0, cut));
      line.erase(0, cut);
      lastSeam = 0;
      continue;  // re-test this same codepoint against the now-shorter line
    }

    line = std::move(candidate);
    const char c = text[i];
    if (seq == 1 && (c == '+' || c == '.' || c == '@' || c == '-' || c == '_')) lastSeam = line.size();
    i += seq;
  }
  if (!line.empty()) push(std::move(line));
}

void ColophonActivity::layout() {
  lines.clear();
  prose.clear();
  lines.reserve(COLOPHON_PEOPLE_COUNT * 2 + COLOPHON_TECH_COUNT * 3 + 16);

  appendWrapped(COLOPHON_INTRO, LineKind::Prose, 0);

  lines.push_back(Line{LineKind::Blank, 0, 0});
  appendWrapped(tr(STR_COLOPHON_CONTRIBUTORS), LineKind::SectionTitle, 0);
  lines.push_back(Line{LineKind::Blank, 0, 0});

  char buf[META_BUF];
  for (size_t i = 0; i < COLOPHON_PEOPLE_COUNT; i++) {
    const auto index = static_cast<uint16_t>(i);
    lines.push_back(Line{LineKind::PersonName, 0, index});
    // Almost every address fits on one line; the few that do not are wrapped
    // rather than cut, because half an address credits nobody.
    formatPersonMeta(index, buf, sizeof(buf));
    if (renderer.getTextAdvanceX(BODY_FONT_ID, buf, EpdFontFamily::REGULAR) <= maxWidth - DETAIL_INDENT) {
      lines.push_back(Line{LineKind::PersonMeta, DETAIL_INDENT, index});
    } else {
      appendAddress(buf, DETAIL_INDENT);
    }
  }

  lines.push_back(Line{LineKind::Blank, 0, 0});
  appendWrapped(tr(STR_COLOPHON_TECHNOLOGY), LineKind::SectionTitle, 0);

  for (size_t i = 0; i < COLOPHON_TECH_COUNT; i++) {
    const auto& entry = COLOPHON_TECH[i];
    lines.push_back(Line{LineKind::Blank, 0, 0});
    if (entry.label == nullptr) {
      appendWrapped(entry.detail, LineKind::SectionTitle, 0);
      continue;
    }
    lines.push_back(Line{LineKind::TechLabel, 0, static_cast<uint16_t>(i)});
    appendWrapped(entry.detail, LineKind::Prose, DETAIL_INDENT);
  }
}

bool ColophonActivity::atEnd() const { return topLine + linesPerPage >= static_cast<int>(lines.size()); }

void ColophonActivity::pageForward() {
  if (atEnd()) return;  // no wrap-around: the end of the credits is the end
  // Overlap by one line so nothing falls into the seam between pages.
  const int step = linesPerPage > 1 ? linesPerPage - 1 : 1;
  const int maxTop = static_cast<int>(lines.size()) - linesPerPage;
  topLine += step;
  if (topLine > maxTop) topLine = maxTop > 0 ? maxTop : 0;
  requestUpdate();
}

void ColophonActivity::pageBack() {
  if (topLine <= 0) return;
  const int step = linesPerPage > 1 ? linesPerPage - 1 : 1;
  topLine = topLine > step ? topLine - step : 0;
  requestUpdate();
}

void ColophonActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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

// Position and height of the thumb track the visible window over the whole
// document, so the length of the credits is visible at a glance.
void ColophonActivity::drawScrollBar() const {
  const int total = static_cast<int>(lines.size());
  if (total <= linesPerPage) return;  // nothing to scroll: draw no bar at all

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int trackX = renderer.getScreenWidth() - metrics.scrollBarRightOffset - metrics.scrollBarWidth;
  const int trackHeight = linesPerPage * lineHeight;
  if (trackHeight <= 0) return;
  renderer.drawRect(trackX, contentTop, metrics.scrollBarWidth, trackHeight);

  int thumbH = trackHeight * linesPerPage / total;
  if (thumbH < MIN_THUMB_HEIGHT) thumbH = MIN_THUMB_HEIGHT;
  if (thumbH > trackHeight) thumbH = trackHeight;
  const int span = total - linesPerPage;
  const int thumbY = contentTop + (trackHeight - thumbH) * topLine / (span > 0 ? span : 1);
  renderer.fillRect(trackX, thumbY, metrics.scrollBarWidth, thumbH, true);
}

// Everything the credits page puts on the panel, minus the clear and the
// refresh. Split out so the grayscale overlay can re-run the identical body
// (see render()) rather than a text-only copy of it that would drift.
void ColophonActivity::drawFrame() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_COLOPHON));

  char buf[META_BUF];
  const int total = static_cast<int>(lines.size());
  for (int row = 0; row < linesPerPage && topLine + row < total; row++) {
    const Line& line = lines[topLine + row];
    const int x = metrics.contentSidePadding + line.indent;
    const int y = contentTop + row * lineHeight;
    switch (line.kind) {
      case LineKind::Blank:
        break;
      case LineKind::Prose:
        renderer.drawText(BODY_FONT_ID, x, y, prose[line.index].c_str());
        break;
      case LineKind::SectionTitle:
        renderer.drawText(BODY_FONT_ID, x, y, prose[line.index].c_str(), true, EpdFontFamily::BOLD);
        break;
      case LineKind::PersonName:
        renderer.drawText(BODY_FONT_ID, x, y, COLOPHON_PEOPLE[line.index].name, true, EpdFontFamily::BOLD);
        break;
      case LineKind::PersonMeta:
        formatPersonMeta(line.index, buf, sizeof(buf));
        renderer.drawText(BODY_FONT_ID, x, y, buf);
        break;
      case LineKind::TechLabel:
        renderer.drawText(BODY_FONT_ID, x, y, COLOPHON_TECH[line.index].label, true, EpdFontFamily::BOLD);
        break;
    }
  }

  drawScrollBar();

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), "", topLine > 0 ? tr(STR_DIR_UP) : "", atEnd() ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void ColophonActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawFrame();
  renderer.displayBuffer();

  // The credits are prose you read and page through with Up/Down, one panel
  // refresh per page, which is the same repaint shape the reader has always
  // paid the overlay on -- not a list you walk a selection down. The chrome
  // faces became 2-bit on 2026-08-14, so this pass now moves pixels; against
  // the 1-bit cuts it was byte-identical to no pass at all.
  TextAa::overlayChromeIfEnabled(renderer, [this] {
    const GfxRenderer::TextOnlyScope textOnly(renderer);
    drawFrame();
  });
}
