#include "TypographySettingsActivity.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SettingRowUi.h"
#include "SettingsList.h"
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
  // "Ligatures are on, and this face has none to offer." Counts LIGATURE rows
  // specifically -- it used to test the whole row count against 1, which was
  // right when the master switch was the only other row and became wrong the
  // moment Line Spacing, Line Grid and Justified Text moved in beside it.
  if (SETTINGS.ligaturesEnabled == 0) return false;
  return std::none_of(rows_.begin(), rows_.end(), [](const Row& r) { return r.isLigature; });
}

std::string TypographySettingsActivity::rowTitle(const Row& row) const {
  return row.isLigature ? row.label : std::string(I18N.get(row.setting.nameId));
}

void TypographySettingsActivity::rebuildRows() {
  rows_.clear();

  // THE SCREEN'S ORDER, and why it is this one. Vertical rhythm first (how far
  // apart the lines sit, then whether they share a grid), then the horizontal
  // decisions in the order the engine makes them -- where the line BREAKS, then
  // whether the slack is spread -- then the glyph-level detail (ligatures) and
  // its per-pair rows. Coarse to fine, so the rows a reader is most likely to
  // want are not below fourteen ligature toggles.
  //
  // SELECTED from getSettingsList(), not redefined here: each row keeps its one
  // definition, so this screen cannot drift from what persists or from what the
  // web settings API serves. A row that is not in that list -- because someone
  // withdrew it -- simply does not appear, rather than appearing dead.
  static constexpr StrId kSettingRows[] = {
      StrId::STR_LINE_SPACING,
      StrId::STR_LINE_GRID,
      // Which line breaker runs. Above Justified Text because the break is
      // chosen first and the justification then fills whatever slack the break
      // left -- and because on a ragged block the breaker is the ONLY thing
      // deciding the edge.
      StrId::STR_LINE_BREAKS,
      StrId::STR_JUSTIFY_THRESHOLD,
      // The ligature master. It always shows, whatever the family carries: it
      // is the one row that means something on a face with no ligatures at all,
      // and it is what makes "no ligatures anywhere" reachable without visiting
      // every family on the card and switching its pairs off one at a time.
      StrId::STR_LIGATURES,
  };
  const auto shared = getSettingsList(&sdFontSystem.registry());
  for (const StrId wanted : kSettingRows) {
    const auto found = std::find_if(shared.begin(), shared.end(),
                                    [wanted](const SettingInfo& s) { return s.nameId == wanted; });
    if (found == shared.end()) continue;
    Row row;
    row.setting = *found;
    rows_.push_back(std::move(row));
  }

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
    // Exactly what the Settings list does with the same SettingInfo --
    // settingrow::activate is shared with it precisely so a row cannot behave
    // one way there and another way here.
    const bool opened = settingrow::activate(row.setting, optionPopup_, [this] { applyAndRebuild(); });
    if (opened) requestUpdate();
    return;
  }

  {
    const bool nowSuppressed = !ligatures::specSuppresses(SETTINGS.ligaturesOff, row.leftCp, row.rightCp);
    const std::string next = ligatures::specWith(SETTINGS.ligaturesOff, row.leftCp, row.rightCp, nowSuppressed);
    strncpy(SETTINGS.ligaturesOff, next.c_str(), sizeof(SETTINGS.ligaturesOff) - 1);
    SETTINGS.ligaturesOff[sizeof(SETTINGS.ligaturesOff) - 1] = '\0';
  }

  applyAndRebuild();
}

void TypographySettingsActivity::applyAndRebuild() {
  // Push to the font layer before anything else can draw. The rows themselves
  // are set in the UI face, which carries its own ligatures, so a stale
  // preference would be visible on this very screen. Harmless for the three
  // spacing rows, which do not touch it -- it re-parses a bounded list.
  SETTINGS.applyLigaturePreference();
  SETTINGS.saveToFile();

  // The row set depends on the master switch, so it is rebuilt rather than
  // repainted. It cannot shrink under the cursor without rebuildRows()
  // clamping the index, which it does.
  rebuildRows();
}

void TypographySettingsActivity::loop() {
  if (optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); })) return;

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
  // The picker draws over everything and owns the frame while it is up, exactly
  // as on the Settings list.
  if (optionPopup_.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TYPOGRAPHY_SETTINGS));

  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const Rect listRect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing};

  // In the empty state the ligature master carries a SUBTITLE saying why there
  // are no pair rows under it. Only that row does, and only then -- which is
  // why isEmptyState() is also what loop() passes as `hasSubtitle`.
  const bool emptyState = isEmptyState();
  const auto& rows = rows_;
  GUI.drawList(
      renderer, listRect, static_cast<int>(rows.size()), selectedIndex_,
      [this, &rows](int i) { return rowTitle(rows[i]); },
      emptyState ? std::function<std::string(int)>([&rows](int i) {
        return rows[i].setting.nameId == StrId::STR_LIGATURES ? std::string(tr(STR_NO_LIGATURES)) : std::string();
      })
                 : nullptr,
      nullptr,
      [&rows](int i) {
        const Row& row = rows[i];
        if (!row.isLigature) return settingrow::valueText(row.setting);
        // A ligature row's value is not a stored byte but a membership test
        // against the suppression set, so it cannot go through valueText.
        const bool on = !ligatures::specSuppresses(SETTINGS.ligaturesOff, row.leftCp, row.rightCp);
        return std::string(I18N.get(on ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
      },
      true);

  // "Select" for the two rows that open a picker, "Toggle" for the rest -- the
  // same rule the Settings list states, through the same predicate.
  const bool onRow = selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(rows_.size());
  const bool picks = onRow && !rows_[selectedIndex_].isLigature && settingrow::opensPicker(rows_[selectedIndex_].setting);
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), picks ? tr(STR_SELECT) : tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
