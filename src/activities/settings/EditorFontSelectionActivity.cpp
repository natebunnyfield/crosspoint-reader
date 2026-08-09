#include "EditorFontSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "notes/EditorFonts.h"
#include "notes/MarkdownRender.h"

namespace {
// One subtitle line: the availability note. Reading families carry a two-line
// designer colophon; these faces have no such data and do not want it.
constexpr int kSubtitleLines = 1;

// Faces visible at once. Five rows is the whole list, so the pane takes
// whatever they leave rather than the list being capped like the reading
// picker's.
constexpr int kVisibleRows = 5;

// The editor asks the SD resolver for this and nothing else
// (NoteEditorActivity::onEnter), so the specimen is drawn at it. Previewing at
// any other size would be showing a rendering the editor never produces.
constexpr uint8_t kEditorPointSize = 12;
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
  const int rowStep = GUI.getListRowStep(true, kSubtitleLines);
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
  if (const int builtin = editorfonts::builtinFontIdFor(storedIndex); builtin != 0) {
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
      SETTINGS.editorFont, [this](int id) { return renderer.getFontMap().count(id) > 0; },
      [this](const char* family) { return sdFontSystem.loadForDisplay(family, kEditorPointSize, renderer); },
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

  const int listSize = static_cast<int>(rows_.size());
  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, true,
                                                         previewHeight + metrics_.verticalSpacing, kSubtitleLines);

  // FRONT buttons only, like the reading picker: ButtonNavigator's Nav* aliases
  // also resolve to the side buttons, which would make them move the selection.
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
  const int labelY = top + height - metrics_.previewPadding - labelH;
  char label[96];
  if (faceName == nullptr) return;
  if (available) {
    snprintf(label, sizeof(label), "%s — %u pt", faceName, static_cast<unsigned>(kEditorPointSize));
  } else {
    snprintf(label, sizeof(label), "%s — %s", faceName, I18N.get(StrId::STR_FONT_NOT_ON_CARD));
  }
  renderer.drawText(UI_10_FONT_ID, left, labelY, label);
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
      [this](int index) -> std::string { return editorfonts::FAMILIES[rows_[index]].label; },
      // Subtitle carries the availability note and nothing else. A row that is
      // reachable says nothing -- a "present" badge on four rows out of five is
      // noise, while the absent one needs to stand out.
      [this](int index) -> std::string {
        return isAvailable(rows_[index]) ? "" : std::string(I18N.get(StrId::STR_FONT_NOT_ON_CARD));
      },
      nullptr,
      [this](int index) -> std::string { return index == appliedIndex_ ? tr(STR_SELECTED) : ""; }, true, nullptr,
      kSubtitleLines);

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
