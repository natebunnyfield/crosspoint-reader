#include "FontSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";

// Resolve the current selection to a POSITION IN `fonts_`.
//
// This must search the built list rather than compute an index: `settingIndex`
// is the persisted value ID (built-ins 0..BUILTIN_FONT_COUNT-1, SD fonts
// BUILTIN_FONT_COUNT + i), which only coincides with the list position while
// the built-in entries are present. They are hidden whenever SD fonts are
// installed, so the two numbering schemes diverge.
template <typename FontList>
int findCurrentFontIndex(const FontList& fonts, const SdCardFontRegistry* registry,
                         const char* sdFontFamilyName, uint8_t fontFamily) {
  // An SD font is selected: match by settingIndex derived from registry order.
  if (sdFontFamilyName[0] != '\0' && registry) {
    const auto& families = registry->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      if (families[i].name == sdFontFamilyName) {
        const auto target = static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
        for (int p = 0; p < static_cast<int>(fonts.size()); p++) {
          if (!fonts[p].isBuiltin && fonts[p].settingIndex == target) return p;
        }
      }
    }
  }

  // A built-in is selected: only findable when built-ins are being shown.
  for (int p = 0; p < static_cast<int>(fonts.size()); p++) {
    if (fonts[p].isBuiltin && fonts[p].settingIndex == fontFamily) return p;
  }

  // Selection is not in the list (e.g. a built-in was selected before SD fonts
  // were installed, or the stored SD font is no longer present). Fall back to
  // the first entry rather than leaving an out-of-range index.
  return 0;
}
}  // namespace

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  // Get metrics and calculate layout dimensions
  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  originalFontFamily_ = SETTINGS.fontFamily;
  strncpy(originalSdFontFamilyName_, SETTINGS.sdFontFamilyName, sizeof(originalSdFontFamilyName_) - 1);
  originalSdFontFamilyName_[sizeof(originalSdFontFamilyName_) - 1] = '\0';

  const int sdFontCount = registry_ ? static_cast<int>(registry_->getFamilyCount()) : 0;

  fonts_.clear();
  fonts_.reserve(sdFontCount > 0 ? sdFontCount : CrossPointSettings::BUILTIN_FONT_COUNT);

  // The built-in Noto faces are hidden once the user has installed their own
  // fonts, so this list shows only their set. They are still listed when no SD
  // fonts are present — the picker must never be empty, and Noto remains the
  // fallback CrossPointSettings::getReaderFontId() resolves to when a selected
  // SD font cannot be loaded.
  if (sdFontCount == 0) {
    fonts_.push_back({I18N.get(StrId::STR_NOTO_SERIF), true, static_cast<uint8_t>(CrossPointSettings::NOTOSERIF)});
    fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, static_cast<uint8_t>(CrossPointSettings::NOTOSANS)});
  }

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  selectedIndex_ = findCurrentFontIndex(fonts_, registry_, SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);
  previewFontIndex_ = selectedIndex_;

  requestUpdate();
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.fontFamily = originalFontFamily_;
    strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName_, sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    sdFontSystem.ensureLoaded(renderer);
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex_ == previewFontIndex_) {
      handleSelection();
    } else {
      previewFontIndex_ = selectedIndex_;
      const auto& font = fonts_[selectedIndex_];
      if (font.isBuiltin) {
        SETTINGS.fontFamily = font.settingIndex;
        SETTINGS.sdFontFamilyName[0] = '\0';
      } else if (registry_) {
        const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
        const auto& families = registry_->getFamilies();
        if (sdIdx < static_cast<int>(families.size())) {
          strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
          SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
          sdFontSystem.ensureLoaded(renderer);
        }
      }
      requestUpdate();
    }
    return;
  }

  const int listSize = static_cast<int>(fonts_.size());
  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, previewHeight + metrics_.verticalSpacing);

  buttonNavigator_.onNextRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this, listSize] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
    requestUpdate();
  });
}

void FontSelectionActivity::handleSelection() {
  const auto& font = fonts_[selectedIndex_];
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  finish();
}

void FontSelectionActivity::renderPreviewPane(int top, int height, int fontId, const char* fontName) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelFontId = UI_10_FONT_ID;
  const int labelH = renderer.getTextHeight(labelFontId);
  const int labelGap = 4;
  const int labelReserved = labelH + labelGap + metrics_.previewPadding;

  char labelBuf[128];
  snprintf(labelBuf, sizeof(labelBuf), "%s \"%s\"", tr(STR_PREVIEW), fontName ? fontName : "");
  const int labelY = top + height - metrics_.previewPadding - labelH;
  renderer.drawText(labelFontId, left, labelY, labelBuf);

  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int innerHeight = height - metrics_.previewPadding - labelReserved;
  const int maxLines = std::max(1, innerHeight / (lineH + 2));

  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (auto* fcm = renderer.getFontCacheManager()) {
    char prewarmBuf[256];
    snprintf(prewarmBuf, sizeof(prewarmBuf), "%s %s", previewText, ELLIPSIS_UTF8);
    fcm->prewarmCache(fontId, prewarmBuf, 0x01);
  }

  const auto lines = renderer.wrappedText(fontId, previewText, width, maxLines);

  int y = top + metrics_.previewPadding;
  const int textBottomLimit = top + height - labelReserved;
  for (const auto& line : lines) {
    if (y + lineH > textBottomLimit) break;
    renderer.drawText(fontId, left, y, line.c_str());
    y += lineH + 2;
  }
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_FONT_FAMILY));

  const int previewTop = afterHeader;
  const int listTop = previewTop + previewHeight + metrics_.verticalSpacing;
  const int listHeight = usableHeight - previewHeight - metrics_.verticalSpacing;

  const int previewFontId = SETTINGS.getReaderFontId();
  const char* previewFontName = (previewFontIndex_ >= 0 && previewFontIndex_ < static_cast<int>(fonts_.size()))
                                    ? fonts_[previewFontIndex_].name.c_str()
                                    : nullptr;
  renderPreviewPane(previewTop, previewHeight, previewFontId, previewFontName);

  renderer.drawLine(0, listTop - metrics_.verticalSpacing / 2, pageWidth, listTop - metrics_.verticalSpacing / 2);

  // Position (in fonts_) of the selection the user arrived with, so the list
  // can badge it "Selected" while another entry is being previewed.
  const int currentFontIndex =
      findCurrentFontIndex(fonts_, registry_, originalSdFontFamilyName_, originalFontFamily_);
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(fonts_.size()), selectedIndex_,
      [this](int index) { return fonts_[index].name; }, nullptr, nullptr,
      [this, currentFontIndex](int index) -> std::string {
        if (index == previewFontIndex_ && index != currentFontIndex) return tr(STR_PREVIEW);
        if (index == currentFontIndex) return tr(STR_SELECTED);
        return "";
      },
      true);

  const bool onPreviewed = selectedIndex_ == previewFontIndex_;
  const char* confirmLabel = onPreviewed ? tr(STR_SELECT) : tr(STR_PREVIEW);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
