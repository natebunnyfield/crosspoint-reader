#include "FontSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "FontDisplayNames.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";

// Label for a FONT_SIZE slot. The enum is ordinal, not absolute — the point
// sizes behind it depend on which sizes are installed — so these stay generic.
const char* fontSizeLabel(uint8_t fontSize) {
  switch (fontSize) {
    case CrossPointSettings::SMALL:
      return tr(STR_SMALL);
    case CrossPointSettings::MEDIUM:
      return tr(STR_MEDIUM);
    case CrossPointSettings::LARGE:
      return tr(STR_LARGE);
    case CrossPointSettings::EXTRA_LARGE:
      return tr(STR_X_LARGE);
    default:
      return "";
  }
}

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
  // Back on RELEASE, not press. This screen is reachable from the reader as
  // well as from Settings, and EpubReaderActivity acts on the Back RELEASE
  // (short press -> onGoHome, EpubReaderActivity.cpp). Finishing on the press
  // left the release edge unconsumed: the reader resumed mid-gesture, saw it,
  // and threw the user out of the book to Home on a single Back tap.
  //
  // Measured before the fix: Back at t=13000 -> FontSelect exited t+13ms (the
  // press), EpubReader exited t+80ms (the release). The two other activities
  // the reader opens — EpubReaderMenuActivity and
  // EpubReaderPercentSelectionActivity — both already use wasReleased for
  // exactly this reason; this now matches them.
  //
  // Safe from the Settings side too: SettingsActivity keys off the press,
  // which is consumed here while this activity is still current, so it sees
  // neither edge.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Choices apply as they are made, so Back just leaves. There is no
    // separate confirm step and nothing to roll back.
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    applySelectedFont();
    return;
  }

  // Side buttons step the reader font size, so a font can be judged at the
  // size it will actually be read at.
  //
  // PageBack/PageForward (rather than Up/Down) so the user's side-button swap
  // preference is honoured, consistent with page turns in the reader. Note
  // that this means the size control is unavailable when side buttons are set
  // to Disabled — the same tradeoff page turning already makes.
  if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
    changeFontSize(+1);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
    changeFontSize(-1);
    return;
  }

  const int listSize = static_cast<int>(fonts_.size());
  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, previewHeight + metrics_.verticalSpacing);

  // List navigation is bound to the FRONT buttons only. ButtonNavigator's
  // NavNext/NavPrevious resolve to "side Down OR front Right" and "side Up OR
  // front Left" (MappedInputManager.cpp), so using them here would make the
  // side buttons both change size and move the selection.
  // ButtonNavigator::Buttons is a private alias, so spell out the vector type.
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

void FontSelectionActivity::changeFontSize(int delta) {
  const int current = static_cast<int>(SETTINGS.fontSize);
  // Clamp rather than wrap: at four slots, wrapping from XL back to S reads as
  // a glitch, and clamping makes the ends of the range perceptible.
  const int next = std::clamp(current + delta, 0, static_cast<int>(CrossPointSettings::FONT_SIZE_COUNT) - 1);
  if (next == current) return;

  {
    // RenderLock is REQUIRED here, not defensive. ActivityManager::loop() calls
    // us with no lock held by design ("the loop() method must be responsible
    // for acquire one if needed", ActivityManager.cpp:64), while the render
    // task is concurrently inside render() holding a reference into the font
    // map (`const auto& font = fontIt->second`, GfxRenderer.cpp). ensureLoaded
    // -> SdCardFontManager::unloadAll does
    // clearSdCardFonts() / removeFont(id) / delete lf.font — it erases that
    // map node and frees the glyph tables under the renderer's feet. On device
    // that is a LoadProhibited panic or garbled glyphs; the host simulator
    // renders in 5-8ms so the window never opens there.
    //
    // Also serialises the SETTINGS.fontSize write itself, which the render task
    // reads every frame (resolvedPointSize / getReaderFontId).
    RenderLock lock(*this);
    SETTINGS.fontSize = static_cast<uint8_t>(next);
    // An SD family keeps only one point size resident, so the new size has to be
    // loaded before the preview can draw at it. Built-in faces just resolve to a
    // different font ID.
    sdFontSystem.ensureLoaded(renderer);
  }
  // Outside the lock: requestUpdate only sets a deferred flag / notifies the
  // render task. requestUpdateAndWait would deadlock (ActivityManager.cpp:303).
  requestUpdate();
}

void FontSelectionActivity::applySelectedFont() {
  previewFontIndex_ = selectedIndex_;
  const auto& font = fonts_[selectedIndex_];
  {
    // Same reason as changeFontSize: the strncpy into SETTINGS.sdFontFamilyName
    // and SdCardFontManager::loadedFamilyName_ are both read by the render task
    // every frame, and ensureLoaded frees the resident SdCardFont. The write and
    // the unload have to be atomic with respect to render().
    RenderLock lock(*this);
    if (font.isBuiltin) {
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

    // Unconditional, and this is the fix for a real leak: the old code only
    // reloaded inside the SD branch, so choosing a built-in after an SD family
    // cleared sdFontFamilyName without ever calling unloadAll(). The previous
    // .cpfont stayed resident, and the waste compounded with every switch —
    // which on a 380KB device is enough to exhaust the heap.
    sdFontSystem.ensureLoaded(renderer);
  }
  requestUpdate();
}

uint8_t FontSelectionActivity::resolvedPointSize() const {
  // Mirrors CrossPointSettings::getReaderFontId()'s resolution order, so the
  // number shown always describes the specimen actually on screen: an SD
  // family wins if it resolves, otherwise it falls through to the built-in ramp.
  if (SETTINGS.sdFontFamilyName[0] != '\0' && registry_ != nullptr) {
    if (const auto* family = registry_->findFamily(SETTINGS.sdFontFamilyName)) {
      // An SD family only ships the sizes it ships; findClosestReaderSize maps
      // the ordinal slot onto that family's own set, which is why this can
      // differ from the built-in ramp for the same slot.
      if (const auto* file = family->findClosestReaderSize(SETTINGS.fontSize)) return file->pointSize;
    }
  }

  // Built-in faces are compiled at a fixed ramp; getReaderFontId() maps the
  // slots onto these point sizes for both NotoSerif and NotoSans.
  static constexpr uint8_t kBuiltinPointSizes[] = {12, 14, 16, 18};
  static_assert(sizeof(kBuiltinPointSizes) / sizeof(kBuiltinPointSizes[0]) ==
                    static_cast<size_t>(CrossPointSettings::FONT_SIZE_COUNT),
                "Built-in point size ramp must cover every FONT_SIZE slot");
  if (SETTINGS.fontSize < CrossPointSettings::FONT_SIZE_COUNT) return kBuiltinPointSizes[SETTINGS.fontSize];
  return 0;
}

void FontSelectionActivity::renderPreviewPane(int top, int height, int fontId, const char* fontName) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelFontId = UI_10_FONT_ID;
  const int labelH = renderer.getTextHeight(labelFontId);
  const int labelGap = 4;
  const int labelReserved = labelH + labelGap + metrics_.previewPadding;

  // Include the current size: the side buttons change it, but they have no
  // on-screen hint (the hint row covers the front buttons only), so this is
  // what makes the control discoverable and its effect legible.
  //
  // Read live from SETTINGS rather than cached at entry, so this stays correct
  // whether the size was changed here or on the Font Size settings screen.
  // The slot name matches the wording used there; the point size is appended
  // because the slot is ordinal and resolves per-family.
  // ONE scratch buffer, deliberately shared with the prewarm string below.
  //
  // This runs on the render task, whose stack is tight — freeink-ui.md records
  // the failure mode as a "Stack canary watchpoint triggered (loopTask)" panic.
  // This frame previously held labelBuf[160] + sizeBuf[48] + prewarmBuf[256] =
  // 464 bytes at once, on top of the std::strings that truncatedText() and
  // wrappedText() return. The label and the prewarm string are never live at
  // the same time, so they share storage and the frame roughly halves.
  char scratch[256];
  const uint8_t pointSize = resolvedPointSize();
  if (pointSize > 0) {
    snprintf(scratch, sizeof(scratch), "%s \"%s\" — %s (%upt)", tr(STR_PREVIEW), fontName ? fontName : "",
             fontSizeLabel(SETTINGS.fontSize), pointSize);
  } else {
    snprintf(scratch, sizeof(scratch), "%s \"%s\" — %s", tr(STR_PREVIEW), fontName ? fontName : "",
             fontSizeLabel(SETTINGS.fontSize));
  }

  const int labelY = top + height - metrics_.previewPadding - labelH;
  // Guard against a negative origin: a short pane, or a large label font, can
  // drive this above the top edge, and the renderer logs
  // "!! Outside range ... -> (x, -1)" and writes outside the framebuffer.
  if (labelY >= 0) {
    renderer.drawText(labelFontId, left, labelY, scratch);
  }

  if (fontId == 0) return;

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  const int innerHeight = height - metrics_.previewPadding - labelReserved;
  const int maxLines = std::max(1, innerHeight / (lineH + 2));

  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (auto* fcm = renderer.getFontCacheManager()) {
    // Reuses `scratch` — the label was drawn above and is no longer needed.
    snprintf(scratch, sizeof(scratch), "%s %s", previewText, ELLIPSIS_UTF8);
    // 0x0F warms all four style variants. The specimen below renders each one,
    // and an SD card font that misses the cache re-reads glyph data from the
    // card per character.
    fcm->prewarmCache(fontId, scratch, 0x0F);
  }

  int y = top + metrics_.previewPadding;
  const int textBottomLimit = top + height - labelReserved;

  // A specimen containing newlines is a per-style sample: one line each in
  // regular, bold, italic and bold-italic, so all four can be judged together.
  // The styles are left unlabelled deliberately — they are self-evident, and a
  // label sitting next to a specimen competes with the thing being evaluated.
  //
  // Translations that have not been given a multi-line specimen are a single
  // string and keep the original wrap-in-regular behaviour, so nothing breaks
  // for the other 28 languages.
  if (std::strchr(previewText, '\n') != nullptr) {
    static constexpr EpdFontFamily::Style kStyles[] = {EpdFontFamily::REGULAR, EpdFontFamily::BOLD,
                                                       EpdFontFamily::ITALIC,
                                                       EpdFontFamily::BOLD_ITALIC};
    constexpr int kStyleCount = static_cast<int>(sizeof(kStyles) / sizeof(kStyles[0]));
    const char* cursor = previewText;
    for (int i = 0; i < kStyleCount && cursor != nullptr; ++i) {
      const char* newline = std::strchr(cursor, '\n');
      const std::string segment =
          newline ? std::string(cursor, static_cast<size_t>(newline - cursor)) : std::string(cursor);
      cursor = newline ? newline + 1 : nullptr;
      if (segment.empty()) continue;
      if (y + lineH > textBottomLimit) break;  // fewer lines fit at large sizes
      // A font lacking a style falls back to regular inside EpdFontFamily, so
      // an incomplete family degrades rather than failing.
      const std::string fitted =
          renderer.truncatedText(fontId, segment.c_str(), width, kStyles[i]);
      renderer.drawText(fontId, left, y, fitted.c_str(), /*black=*/true, kStyles[i]);
      y += lineH + 2;
    }
    return;
  }

  const auto lines = renderer.wrappedText(fontId, previewText, width, maxLines);
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

  // Titled for what the screen now covers — family and size, applied live —
  // rather than just the family list it started as.
  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_TEXT_SETTINGS));

  const int previewTop = afterHeader;
  const int listTop = previewTop + previewHeight + metrics_.verticalSpacing;
  const int listHeight = usableHeight - previewHeight - metrics_.verticalSpacing;

  const int previewFontId = SETTINGS.getReaderFontId();
  // Typeface name WITHOUT the designer credit: the label already carries the
  // resolved point size, and the list row directly below shows the attribution.
  // Held in a local because renderPreviewPane takes a const char* — a temporary
  // from displayName() would dangle.
  std::string previewName;
  if (previewFontIndex_ >= 0 && previewFontIndex_ < static_cast<int>(fonts_.size())) {
    const auto& picked = fonts_[previewFontIndex_];
    previewName = picked.isBuiltin ? picked.name : FontDisplayNames::displayName(picked.name);
  }
  const char* previewFontName = previewName.empty() ? nullptr : previewName.c_str();
  renderPreviewPane(previewTop, previewHeight, previewFontId, previewFontName);

  // drawLine's horizontal fast path is inclusive of x2 (`for (x = x1; x <= x2)`),
  // so the last valid column is pageWidth - 1. Passing pageWidth drew one pixel
  // past the right edge; in Portrait that maps to phyY = panelHeight - 1 - 528
  // = -1 and logged "!! Outside range (528, 302) -> (302, -1)". drawRect gets
  // this right (`x + width - 1`); this call did not.
  const int separatorY = listTop - metrics_.verticalSpacing / 2;
  renderer.drawLine(0, separatorY, pageWidth - 1, separatorY);

  // previewFontIndex_ is now simply "what is applied": choices take effect on
  // Confirm and are never rolled back, so there is no longer a tentative
  // preview distinct from the real selection, and no need to remember what the
  // user arrived with. One badge, on the applied font.
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(fonts_.size()), selectedIndex_,
      // Directory name -> "Typeface - Designer" (FontDisplayNames.h). Built-in
      // entries already carry a translated display name, so only SD families are
      // mapped; an unlisted family falls through to its own name.
      [this](int index) {
        return fonts_[index].isBuiltin ? fonts_[index].name : FontDisplayNames::displayLabel(fonts_[index].name);
      },
      nullptr, nullptr,
      [this](int index) -> std::string {
        if (index == previewFontIndex_) return tr(STR_SELECTED);
        return "";
      },
      true);

  // Confirm always means the same thing now, so the label no longer alternates
  // between Preview and Select depending on where the cursor is.
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
