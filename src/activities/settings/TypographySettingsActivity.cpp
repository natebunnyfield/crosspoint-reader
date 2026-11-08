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

namespace fui = freeink::ui;

namespace {

// The four style slots a family can carry. EpdFontFamily::getFont() falls back
// to the regular face for a style the family does not ship, so asking for all
// four never returns null and the duplicate tables it hands back are collapsed
// by the dedupe below.
constexpr EpdFontFamily::Style ALL_STYLES[] = {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC,
                                               EpdFontFamily::BOLD_ITALIC};

}  // namespace

void TypographySettingsActivity::onEnter() {
  UiListActivity::onEnter();
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
    const auto found =
        std::find_if(shared.begin(), shared.end(), [wanted](const SettingInfo& s) { return s.nameId == wanted; });
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

  if (nav.selected >= static_cast<int>(rows_.size())) {
    nav.selected = rows_.empty() ? 0 : static_cast<int>(rows_.size()) - 1;
  }
}

void TypographySettingsActivity::toggleCurrentRow() {
  if (nav.selected < 0 || nav.selected >= static_cast<int>(rows_.size())) return;
  const Row row = rows_[nav.selected];  // by value: rebuildRows() replaces the vector below

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

bool TypographySettingsActivity::handleButtons() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentRow();
    requestUpdate();
    return true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  return false;
}

void TypographySettingsActivity::activateIndex(const int index) {
  nav.selected = index;
  toggleCurrentRow();
  requestUpdate();
}

void TypographySettingsActivity::describeRow(const int index, ListRow& out) const {
  const Row& row = rows_[index];
  out.title = rowTitle(row);
  // In the empty state the ligature master carries a SUBTITLE saying why there
  // are no pair rows under it. Only that row does, and only then.
  if (isEmptyState() && row.setting.nameId == StrId::STR_LIGATURES) out.subtitle = tr(STR_NO_LIGATURES);
  if (!row.isLigature) {
    out.value = settingrow::valueText(row.setting);
    return;
  }
  // A ligature row's value is not a stored byte but a membership test
  // against the suppression set, so it cannot go through valueText.
  const bool on = !ligatures::specSuppresses(SETTINGS.ligaturesOff, row.leftCp, row.rightCp);
  out.value = I18N.get(on ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF);
}

void TypographySettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  ListLook look;
  // Only the empty state carries a subtitle row (isEmptyState()).
  look.subtitles = isEmptyState();
  look.highlightValue = true;
  drawRows(screen, look);
}

void TypographySettingsActivity::drawFooter() {
  // "Select" for the two rows that open a picker, "Toggle" for the rest -- the
  // same rule the Settings list states, through the same predicate.
  const bool onRow = nav.selected >= 0 && nav.selected < static_cast<int>(rows_.size());
  const bool picks = onRow && !rows_[nav.selected].isLigature && settingrow::opensPicker(rows_[nav.selected].setting);
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), picks ? tr(STR_SELECT) : tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
