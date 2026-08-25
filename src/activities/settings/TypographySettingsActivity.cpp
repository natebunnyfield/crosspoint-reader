#include "TypographySettingsActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"

namespace {

// The four style slots a family can carry. EpdFontFamily::getFont() falls back
// to the regular face for a style the family does not ship, so asking for all
// four never returns null and the duplicate tables it hands back are collapsed
// by the dedupe below.
constexpr EpdFontFamily::Style ALL_STYLES[] = {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC,
                                               EpdFontFamily::BOLD_ITALIC};

}  // namespace

void TypographySettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex_ = 0;
  rebuildRows();
  requestUpdate();
}

void TypographySettingsActivity::onExit() {
  Activity::onExit();
  // Saved here as well as on every change: the screen writes SETTINGS directly
  // so a change is live immediately, and a device that sleeps or loses power
  // between the change and the parent's own save must not lose it.
  SETTINGS.saveToFile();
}

bool TypographySettingsActivity::isEmptyState() const {
  return rows_.size() <= 1 && SETTINGS.ligaturesEnabled != 0;
}

void TypographySettingsActivity::rebuildRows() {
  rows_.clear();

  // The master switch always leads, whatever the family carries. It is the one
  // row that means something on a face with no ligatures at all, and it is
  // what makes "no ligatures anywhere" reachable without visiting every family
  // on the card and switching its pairs off one at a time.
  rows_.push_back(Row{StrId::STR_LIGATURES, "", 0, 0, false});

  // With substitution off the per-pair rows would be drawing values that
  // describe nothing on the page. Rather than render them as lies, or invent a
  // third "unavailable" row state this list has no widget for, the screen
  // simply stops offering them. The stored choices are untouched and come back
  // with the switch, because they live in their own settings field.
  if (SETTINGS.ligaturesEnabled == 0) return;

  // The reading family, as the reader itself resolves it. Not the registry's
  // idea of the selected family: an SD font that failed to load falls back to
  // the built-in face, and the rows have to describe what will actually be
  // drawn.
  const int fontId = SETTINGS.getReaderFontId();
  const auto& fontMap = renderer.getFontMap();
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) return;

  // Union across the styles, deduped on the input pair. `seen` is the packed
  // pair, so a ligature that exists in three styles contributes one row.
  std::vector<uint32_t> seen;
  for (const EpdFontFamily::Style style : ALL_STYLES) {
    const EpdFontData* data = it->second.getData(style);
    if (!data || !data->ligaturePairs) continue;
    for (uint32_t i = 0; i < data->ligaturePairCount; i++) {
      const uint32_t packed = data->ligaturePairs[i].pair;
      if (std::find(seen.begin(), seen.end(), packed) != seen.end()) continue;
      seen.push_back(packed);

      Row row;
      row.leftCp = packed >> 16;
      row.rightCp = packed & 0xFFFFu;
      row.isLigature = true;
      // The letters, not the glyph. A row labeled with the ligature CHARACTER
      // would be drawn as the very shape the row exists to switch off, so the
      // reader could not tell "ffi" from "ffi"; and a PUA row would be labeled
      // with a codepoint no UI face has a glyph for, i.e. with a blank. The
      // spelling walks the font's own table back through any chained left
      // side, so Edgar's U+FB00 + i reads "ffi".
      row.label = ligatures::spellPair(data->ligaturePairs, data->ligaturePairCount, row.leftCp, row.rightCp);
      rows_.push_back(std::move(row));
    }
  }

  if (selectedIndex_ >= static_cast<int>(rows_.size())) {
    selectedIndex_ = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
  }
}

void TypographySettingsActivity::toggleCurrentRow() {
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(rows_.size())) return;
  const Row row = rows_[selectedIndex_];  // by value: rebuildRows() replaces the vector below

  if (!row.isLigature) {
    SETTINGS.ligaturesEnabled = SETTINGS.ligaturesEnabled ? 0 : 1;
  } else {
    const bool nowSuppressed = !ligatures::specSuppresses(SETTINGS.ligaturesOff, row.leftCp, row.rightCp);
    const std::string next = ligatures::specWith(SETTINGS.ligaturesOff, row.leftCp, row.rightCp, nowSuppressed);
    strncpy(SETTINGS.ligaturesOff, next.c_str(), sizeof(SETTINGS.ligaturesOff) - 1);
    SETTINGS.ligaturesOff[sizeof(SETTINGS.ligaturesOff) - 1] = '\0';
  }

  // Push to the font layer before anything else can draw. The rows themselves
  // are set in the UI face, which carries its own ligatures, so a stale
  // preference would be visible on this very screen.
  SETTINGS.applyLigaturePreference();
  SETTINGS.saveToFile();

  // The row set depends on the master switch, so it is rebuilt rather than
  // repainted. It cannot shrink under the cursor without rebuildRows()
  // clamping the index, which it does.
  rebuildRows();
}

void TypographySettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentRow();
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int rowCount = static_cast<int>(rows_.size());

  const bool hasSubtitle = isEmptyState();
  switch (handleListTouch(selectedIndex_, rowCount, listTop, listHeight, hasSubtitle)) {
    case ListTouchResult::Activated:
      toggleCurrentRow();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = GUI.getListPageItems(listHeight, hasSubtitle);

  buttonNavigator_.onNextRelease([this, rowCount] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, rowCount);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, rowCount] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, rowCount);
    requestUpdate();
  });
  // A hold pages, and the SIDE pair pages by a screenful — the same division
  // the Settings list uses, so the gesture means the same thing one screen in.
  buttonNavigator_.onNextContinuous([this, rowCount, pageItems] {
    selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, rowCount, pageItems);
    requestUpdate();
  });
  buttonNavigator_.onPreviousContinuous([this, rowCount, pageItems] {
    selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, rowCount, pageItems);
    requestUpdate();
  });
  buttonNavigator_.onPageNext([this, rowCount, pageItems] {
    if (!ButtonNavigator::pageDown(selectedIndex_, rowCount, pageItems)) return;
    requestUpdate();
  });
  buttonNavigator_.onPagePrevious([this, rowCount, pageItems] {
    if (!ButtonNavigator::pageUp(selectedIndex_, rowCount, pageItems)) return;
    requestUpdate();
  });
}

void TypographySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TYPOGRAPHY_SETTINGS));

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const Rect listRect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing};

  if (isEmptyState()) {
    // The master row alone: this face carries nothing to switch. Say so, rather
    // than leaving a one-row list that reads as a screen that failed to load.
    GUI.drawList(
        renderer, listRect, 1, selectedIndex_, [](int) { return std::string(tr(STR_LIGATURES)); },
        [](int) { return std::string(tr(STR_NO_LIGATURES)); }, nullptr,
        [](int) {
          // I18N.get, not tr(): the macro pastes StrId:: onto its argument, so
          // it cannot take an expression that chooses between two ids.
          return std::string(I18N.get(SETTINGS.ligaturesEnabled ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
        },
        true);
  } else {
    const auto& rows = rows_;
    GUI.drawList(
        renderer, listRect, static_cast<int>(rows.size()), selectedIndex_,
        [&rows](int i) { return rows[i].label.empty() ? std::string(I18N.get(rows[i].nameId)) : rows[i].label; },
        nullptr, nullptr,
        [&rows](int i) {
          const Row& row = rows[i];
          const bool on = row.isLigature ? !ligatures::specSuppresses(SETTINGS.ligaturesOff, row.leftCp, row.rightCp)
                                         : SETTINGS.ligaturesEnabled != 0;
          return std::string(I18N.get(on ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
        },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
