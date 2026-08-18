#include "EditorFontSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontDisplayNames.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/EditorFonts.h"
#include "notes/MarkdownRender.h"

namespace {
// Two subtitle lines, the same as the reading picker's kColophonLines, and for
// the same reason: the rows carry a "Designer · YEAR PLACE" colophon that does
// not fit on one. Both the page stride and drawList have to be told, or paging
// skips rows.
//
// Owner ruling 2026-08-09: this list presents and sorts identically to Text
// Settings. The subtitle used to be the availability note alone; it is now the
// colophon with that note prepended (editorfonts::rowSubtitle).
constexpr int kColophonLines = 5;

// Faces visible at once. Five rows is the whole list, so the pane takes
// whatever they leave rather than the list being capped like the reading
// picker's. A two-line row is taller, so the clamp below (whole rows only,
// bounded by the space that exists) is what actually decides it.
constexpr int kVisibleRows = 5;

// Air between the pane's label and the colophon beneath it, same as the
// reading picker.
constexpr int kColophonGap = 6;

// The editor asks the SD resolver for this and nothing else
// (NoteEditorActivity::onEnter), so the specimen is drawn at it. Previewing at
// any other size would be showing a rendering the editor never produces.
// The size the PREVIEW draws at, and the size the picker's rows report. Follows
// the Editor Font Size setting rather than a constant 12, so the specimen shows
// the face at the size it will actually be typed in -- a preview at a size the
// editor will not use is a preview of the wrong thing.
inline uint8_t editorPointSize() { return editorfonts::nearestOfferedSize(SETTINGS.editorFontSize); }
}  // namespace

EditorFontSelectionActivity::EditorFontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("EditorFontSelect", renderer, mappedInput) {}

void EditorFontSelectionActivity::onEnter() {
  Activity::onEnter();

  // Pick up fonts installed or deleted over the web server since this screen
  // last ran, so a family the owner just uploaded is not reported missing.
  sdFontSystem.refreshIfDirty();

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;

  // List first, pane takes the remainder -- whole rows only, so the last row is
  // never drawn clipped. Same ordering as FontSelectionActivity, and the same
  // reason: the row count is the fixed quantity.
  const int listBudget = std::max(0, usableHeight - metrics_.verticalSpacing);
  const int rowStep = GUI.getListRowStep(false, 1);
  listHeight = rowStep > 0 ? std::min(rowStep * kVisibleRows, (listBudget / rowStep) * rowStep)
                           : usableHeight * (100 - metrics_.previewHeightPercent) / 100;
  previewHeight = usableHeight - metrics_.verticalSpacing - listHeight;

  rows_ = editorfonts::displayOrder();

  // Open on whatever is stored. positionOf-style search rather than arithmetic:
  // the stored value is an index into FAMILIES, and the rows are a permutation
  // of those, so the two only coincide by accident.
  appliedIndex_ = 0;
  for (int i = 0; i < static_cast<int>(rows_.size()); i++) {
    if (rows_[i] == SETTINGS.editorFont) {
      appliedIndex_ = i;
      break;
    }
  }
  selectedIndex_ = appliedIndex_;

  {
    // Resolving may LOAD a card family, which evicts fonts the render task is
    // reading from. Same lock discipline as FontSelectionActivity::changeFontSize.
    RenderLock lock(*this);
    appliedFontId_ = resolveAppliedFontId();
  }

  requestUpdate();
}

void EditorFontSelectionActivity::onExit() { Activity::onExit(); }

bool EditorFontSelectionActivity::isAvailable(uint8_t storedIndex) const {
  if (storedIndex >= editorfonts::FAMILY_COUNT) return false;
  // Compiled in: present by construction, but only if this build really has it
  // -- a font id is just an int and holding one implies nothing.
  // Asked at the CURRENT editor size: a row is available if the cut the editor
  // would actually load is present, not if some other size of it is.
  if (const int builtin = editorfonts::builtinFontIdFor(storedIndex, editorPointSize()); builtin != 0) {
    return renderer.getFontMap().count(builtin) > 0;
  }
  // Card-only: a registry lookup, which is in memory. Deliberately NOT
  // loadForDisplay -- this runs per row, per frame, and that one does file IO
  // and can evict the resident family.
  return sdFontSystem.registry().findFamily(editorfonts::FAMILIES[storedIndex].family) != nullptr;
}

int EditorFontSelectionActivity::resolveAppliedFontId() {
  // The editor's own resolution, verbatim (NoteEditorActivity::onEnter). If the
  // specimen resolved any other way it could show a face the editor will not
  // use -- which is the exact failure this screen exists to prevent, and the one
  // the Editor Font setting originally shipped with.
  return editorfonts::resolve(
      SETTINGS.editorFont, SETTINGS.editorFontSize, [this](int id) { return renderer.getFontMap().count(id) > 0; },
      [this](const char* family) { return sdFontSystem.loadForDisplay(family, editorPointSize(), renderer); },
      UI_10_FONT_ID);
}

void EditorFontSelectionActivity::applySelectedFont() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(rows_.size())) return;

  {
    RenderLock lock(*this);
    SETTINGS.editorFont = rows_[selectedIndex_];
    appliedFontId_ = resolveAppliedFontId();
  }
  appliedIndex_ = selectedIndex_;
  // Persisted here rather than left to the caller: this screen is reached from
  // a settings row whose result handler saves anyway, but a power-off between
  // the two would otherwise lose the choice.
  SETTINGS.saveToFile();
  requestUpdate();
}

void EditorFontSelectionActivity::changeFontSize(const int delta) {
  // Steps a POSITION in editorfonts::SIZES, but what gets stored is the real
  // point size (12 or 14), never the position -- EditorFonts.h is explicit that
  // SIZES is not order-frozen precisely because nothing persists an index into
  // it. Snap first, so a settings.json holding a size that is no longer offered
  // (a 13 from an API client, or a value written when the list differed) steps
  // to a neighbour instead of jumping to the first size.
  const uint8_t current = editorfonts::nearestOfferedSize(SETTINGS.editorFontSize);
  int position = 0;
  for (size_t i = 0; i < editorfonts::SIZE_COUNT; i++) {
    if (editorfonts::SIZES[i] == current) {
      position = static_cast<int>(i);
      break;
    }
  }

  // Clamp, not wrap, exactly as FontSelectionActivity::changeFontSize does:
  // wrapping from the largest size back to the smallest reads as a glitch, and
  // the ends of the range should be perceptible. With two sizes that means one
  // button is always dead, which is the honest thing for it to be.
  const int next = std::clamp(position + delta, 0, static_cast<int>(editorfonts::SIZE_COUNT) - 1);
  if (next == position) return;

  {
    // RenderLock is REQUIRED, not defensive -- same hazard FontSelectionActivity
    // documents at length. resolveAppliedFontId() can call
    // SdCardFontSystem::loadForDisplay, which evicts the resident family the
    // render task is holding a reference into. It also serialises the
    // SETTINGS.editorFontSize write, which render() reads every frame through
    // editorPointSize().
    RenderLock lock(*this);
    SETTINGS.editorFontSize = editorfonts::SIZES[next];
    // appliedFontId_ is CACHED per apply and was resolved AT the old size, so
    // the specimen keeps drawing the old cut until this line runs. The pane's
    // label and isAvailable() read editorPointSize() live and would have
    // followed on their own -- which is exactly how this could have shipped
    // announcing "14 pt" over 12 pt glyphs.
    appliedFontId_ = resolveAppliedFontId();
  }

  // Persisted here for the same reason applySelectedFont() persists: Settings'
  // result handler saves on the way out, but a power-off before that would
  // otherwise lose the choice. SETTINGS.editorFontSize has NO valuePtr, so the
  // generic toJson loop does not carry it -- CrossPointSettings.cpp:123 and
  // :391 write and read it by hand, and this save is what reaches those lines.
  SETTINGS.saveToFile();
  requestUpdate();
}

void EditorFontSelectionActivity::loop() {
  // Back on PRESS, matching every other settings sub-screen. Choices apply as
  // they are made, so there is nothing to roll back.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  // Confirm always applies. The reading picker overloads Confirm on the applied
  // row to swap between two specimens; there is only one specimen here, so an
  // overload would be a hidden gesture with nothing behind it. Applying the
  // already-applied row is a no-op the user cannot get wrong.
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelectedFont();
    return;
  }

  // Side buttons step the EDITOR font size, so a face can be judged at the size
  // it will actually be written at. Same control, same pair and same reason as
  // the reading picker (FontSelectionActivity.cpp:212-221); the size setting
  // lives here rather than in the System settings list, by owner ruling
  // 2026-08-18.
  //
  // PageBack/PageForward rather than Up/Down so the owner's side-button swap
  // preference is honored, consistent with page turns in the reader. As there,
  // that means the size control is unavailable when side buttons are set to
  // Disabled — the same tradeoff page turning already makes.
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    changeFontSize(+1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    changeFontSize(-1);
    return;
  }

  const int listSize = static_cast<int>(rows_.size());
  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, previewHeight + metrics_.verticalSpacing, 1);

  // FRONT buttons only, like the reading picker. Nav* is the front pair now
  // (2026-08-15) so getNextButtons() would work, but spell it out: this screen
  // must NOT pick up the side pair's page handlers either, because the side
  // buttons here step the editor font size — the handler directly above. This
  // is one of the two screens the "side buttons page" ruling deliberately
  // exempts; see docs/ui-conventions.md. That exemption was written in
  // f278be2fc (2026-08-15) describing a handler that did not exist yet, and
  // was still describing one on 2026-08-18 when this screen finally grew it.
  static const std::vector<MappedInputManager::Button> kNextButtons = {MappedInputManager::Button::Right};
  static const std::vector<MappedInputManager::Button> kPreviousButtons = {MappedInputManager::Button::Left};

  buttonNavigator_.onRelease(kNextButtons, [this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });
  buttonNavigator_.onRelease(kPreviousButtons, [this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });
  buttonNavigator_.onContinuous(kNextButtons, [this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator_.onContinuous(kPreviousButtons, [this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });
}

void EditorFontSelectionActivity::renderPreviewSpecimen(int top, int height, int fontId) const {
  if (fontId == 0 || height <= 0) return;

  const int left = metrics_.previewPadding;
  const int right = renderer.getScreenWidth() - metrics_.previewPadding;
  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int labelReserved = labelH + 4 + metrics_.previewPadding;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;
  const int lineStep = lineH + 2;

  const int innerHeight = height - metrics_.previewPadding - labelReserved;
  const int maxLines = std::max(1, innerHeight / lineStep);

  // Each source line is drawn through the editor's own markdown renderer, so
  // the gutter markers, the hanging indent and the bold/italic runs land where
  // they will while typing. indentStep is the line height, matching what
  // NoteEditorActivity passes.
  const char* specimen = I18N.get(StrId::STR_EDITOR_FONT_PREVIEW_TEXT);
  const int textBottomLimit = top + height - labelReserved;

  // Count first, draw second, so the block can be CENTRED in the pane. The pane
  // is around half the screen and the specimen is five short lines; pinned to
  // the top it left a band of white between the text and its label. Same
  // collect-then-draw shape as FontSelectionActivity::renderPreviewSpecimen,
  // and it must stay deterministic from these inputs alone — the grayscale AA
  // pass re-runs this and has to land on the identical glyphs.
  int lineCount = 0;
  for (const char* c = specimen; c != nullptr && *c != '\0' && lineCount < maxLines;) {
    const char* nl = std::strchr(c, '\n');
    ++lineCount;
    c = nl ? nl + 1 : nullptr;
  }
  const int blockHeight = std::max(0, lineCount * lineStep - 2);

  int y = top + metrics_.previewPadding + std::max(0, (innerHeight - blockHeight) / 2);
  int drawn = 0;
  const char* cursor = specimen;
  while (cursor != nullptr && *cursor != '\0' && drawn < maxLines) {
    const char* newline = std::strchr(cursor, '\n');
    const size_t len = newline ? static_cast<size_t>(newline - cursor) : std::strlen(cursor);
    if (y + lineH > textBottomLimit) break;
    if (len > 0) mdrender::drawLine(renderer, fontId, lineH, left, y, cursor, len, right);
    y += lineStep;
    ++drawn;
    cursor = newline ? newline + 1 : nullptr;
  }
}

void EditorFontSelectionActivity::renderPreviewPane(int top, int height, int fontId, const char* faceName,
                                                    bool available) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  renderPreviewSpecimen(top, height, fontId);

  // Label under the specimen: the face and the size it will be written at. When
  // the chosen face is not reachable the label says so and names what is being
  // drawn instead, because the alternative is a specimen quietly showing the
  // fallback under the missing face's name.
  const int labelH = renderer.getTextHeight(UI_10_FONT_ID);
  const int colophonBlockH = previewColophonHeight(width);
  const int labelY = top + height - metrics_.previewPadding - colophonBlockH - labelH;
  char label[96];
  if (faceName == nullptr) return;
  if (available) {
    snprintf(label, sizeof(label), "%s — %u pt", faceName, static_cast<unsigned>(editorPointSize()));
  } else {
    snprintf(label, sizeof(label), "%s — %s", faceName, I18N.get(StrId::STR_FONT_NOT_ON_CARD));
  }
  if (labelY < 0) return;
  renderer.drawText(UI_10_FONT_ID, left, labelY, label);

  // Name, then credit — same order the reading picker uses.
  const int colophonStep = renderer.getLineHeight(SMALL_FONT_ID);
  int colophonY = labelY + labelH + kColophonGap;
  for (const auto& line : previewColophonLines(width)) {
    if (!line.empty()) renderer.drawText(SMALL_FONT_ID, left, colophonY, line.c_str(), true);
    colophonY += colophonStep;
  }
}

// Mirrors FontSelectionActivity's pair: a '\n' is an explicit break between the
// two halves of a lineage stage, and an EMPTY segment is the deliberate blank
// line between an originator and their digitiser, which wrappedText() cannot
// produce because it has no words to lay out.
std::vector<std::string> EditorFontSelectionActivity::previewColophonLines(int width) const {
  std::vector<std::string> out;
  if (width <= 0 || rows_.empty()) return out;
  const int idx = appliedIndex_ >= 0 && appliedIndex_ < static_cast<int>(rows_.size()) ? appliedIndex_ : 0;
  const std::string text = FontDisplayNames::subtitle(editorfonts::FAMILIES[rows_[idx]].family);
  if (text.empty()) return out;

  out.reserve(kColophonLines);
  size_t segStart = 0;
  while (segStart <= text.size() && static_cast<int>(out.size()) < kColophonLines) {
    const size_t nl = text.find('\n', segStart);
    const std::string segment = text.substr(segStart, nl == std::string::npos ? std::string::npos : nl - segStart);
    if (segment.empty()) {
      out.push_back(std::string());
    } else {
      for (auto& line :
           renderer.wrappedText(SMALL_FONT_ID, segment.c_str(), width, kColophonLines - static_cast<int>(out.size()))) {
        if (static_cast<int>(out.size()) >= kColophonLines) break;
        out.push_back(std::move(line));
      }
    }
    if (nl == std::string::npos) break;
    segStart = nl + 1;
  }
  return out;
}

int EditorFontSelectionActivity::previewColophonHeight(int width) const {
  const auto lines = previewColophonLines(width);
  if (lines.empty()) return 0;
  return kColophonGap + static_cast<int>(lines.size()) * renderer.getLineHeight(SMALL_FONT_ID);
}

void EditorFontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_EDITOR_FONT));

  const int previewTop = afterHeader;
  const int listTop = previewTop + previewHeight + metrics_.verticalSpacing;

  const uint8_t appliedStored =
      appliedIndex_ >= 0 && appliedIndex_ < static_cast<int>(rows_.size()) ? rows_[appliedIndex_] : 0;
  const char* appliedName =
      appliedStored < editorfonts::FAMILY_COUNT ? editorfonts::FAMILIES[appliedStored].label : nullptr;
  const bool appliedAvailable = isAvailable(appliedStored);
  renderPreviewPane(previewTop, previewHeight, appliedFontId_, appliedName, appliedAvailable);

  // pageWidth - 1: drawLine's horizontal fast path is inclusive of x2, so
  // passing pageWidth draws one pixel past the right edge.
  const int separatorY = listTop - metrics_.verticalSpacing / 2;
  renderer.drawLine(0, separatorY, pageWidth - 1, separatorY);

  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(rows_.size()), selectedIndex_,
      // Title: the typeface name alone, exactly as the reading picker draws it.
      [this](int index) -> std::string { return editorfonts::FAMILIES[rows_[index]].label; },
      // NO subtitle, matching the reading picker (owner ruling 2026-08-14). The
      // credit is about the face being judged, so it lives in the preview pane
      // beside the specimen; the list is just the set you step through.
      //
      // The availability note went with it. That costs nothing today — every
      // editor face is compiled into the firmware, so isAvailable() cannot
      // fail — and the pane's own label still says "not on card" for the
      // applied face. editorfonts::rowSubtitle() is kept for the day a face is
      // reachable again from the card.
      nullptr, nullptr, [this](int index) -> std::string { return index == appliedIndex_ ? tr(STR_SELECTED) : ""; },
      true, nullptr, 1);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  // Grayscale AA over the specimen only, mirroring the reading picker: the BW
  // base is already on the panel, and re-rendering just the specimen leaves
  // everything else unflagged so no extra full flash is introduced.
  if (SETTINGS.textAntiAliasing != CrossPointSettings::TEXT_AA_OFF) {
    ReaderUtils::renderAntiAliased(renderer, [&] { renderPreviewSpecimen(previewTop, previewHeight, appliedFontId_); });
  }
}
