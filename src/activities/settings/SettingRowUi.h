#pragma once

#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/settings/SettingsActivity.h"
#include "components/OptionPopup.h"

// How a settings ROW draws its value, and what pressing Confirm on it does.
//
// Extracted 2026-08-24, when Line Grid, Line Spacing and Justified Text moved
// onto the Typography screen (owner ruling: "put or move line grid, line
// spacing, letter spacing, justified text to Typography Settings"). Two screens
// now render rows out of the same SettingInfo, and the alternative was a second
// copy of this logic on the new one.
//
// A second copy would have been the wrong kind of duplicate. Every trap it
// encodes was paid for once already and none of them is visible in a diff:
//
//   * enumCount(), never enumValues.size(). A runtime-labeled row keeps its
//     choices in enumStringValues and leaves enumValues EMPTY, so the naive
//     form saw 0 choices -- which suppressed the picker, made `% 0` undefined
//     behavior that on RISC-V returns the dividend, and let a stored index walk
//     past its label list until the value column drew blank.
//   * WHERE THE VALUE COMES FROM AND WHERE THE LABEL COMES FROM ARE
//     INDEPENDENT. A row supplies its value through a member pointer or a
//     getter, and its labels through translated StrIds or runtime strings; any
//     combination is legal. Picking the label source off the value source
//     segfaulted the UTC offset row, which builds its own labels and leaves
//     enumValues empty, by indexing that empty vector with 48.
//   * Both indexes are bounds-checked, because a stored value outlives the
//     label list it was written against -- a font uninstalled, an enum
//     shortened.
//   * The picker lists choices in DISPLAY order while the setting stores an
//     INDEX, so the order table has to be carried into the callback BY VALUE:
//     onSelect calls back into the owner, which rebuilds the row vector the
//     SettingInfo reference points into.
//
// Scope: the GENERIC row kinds only. A caller that special-cases a particular
// row -- Settings opens a sub-activity for Editor Font and the clock offset,
// and renders the clock's byte through formatUtcOffset() -- checks for it
// BEFORE delegating here. This file deliberately knows nothing about which
// screen it is drawing on.
namespace settingrow {

// The value column. Empty string for rows that have no value to show (ACTION
// rows, and anything whose type/accessor pair is not one of the shapes below).
inline std::string valueText(const SettingInfo& setting) {
  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    return std::string(I18N.get(SETTINGS.*(setting.valuePtr) ? StrId::STR_STATE_ON : StrId::STR_STATE_OFF));
  }
  if (setting.type == SettingType::ENUM && (setting.valuePtr != nullptr || setting.valueGetter)) {
    const uint8_t value = setting.valuePtr != nullptr ? SETTINGS.*(setting.valuePtr) : setting.valueGetter();
    if (!setting.enumStringValues.empty()) {
      if (value < setting.enumStringValues.size()) return setting.enumStringValues[value];
      return {};
    }
    if (value < setting.enumValues.size()) return std::string(I18N.get(setting.enumValues[value]));
    return {};
  }
  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    return std::to_string(SETTINGS.*(setting.valuePtr));
  }
  return {};
}

// Whether Confirm on this row opens something (a picker or a sub-screen) rather
// than changing a value where it stands. Drives the button hint's label, so it
// must agree with activate() below or the hint says "Toggle" and a popup opens.
inline bool opensPicker(const SettingInfo& setting) {
  return setting.type == SettingType::ENUM && setting.enumCount() > 1;
}

// Confirm. Writes the new value for an in-place row, or shows `popup` for a
// multi-choice enum and writes when the pick lands. `onChanged` runs after any
// write -- persist, re-derive, repaint -- and is the ONLY place the two callers
// differ.
//
// Returns true if a popup was opened, i.e. the caller should repaint and wait
// rather than treat the row as already changed.
inline bool activate(const SettingInfo& setting, OptionPopup& popup, const std::function<void()>& onChanged) {
  const size_t choiceCount = setting.enumCount();

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    SETTINGS.*(setting.valuePtr) = SETTINGS.*(setting.valuePtr) ? 0 : 1;
    onChanged();
    return false;
  }

  const bool byPointer = setting.type == SettingType::ENUM && setting.valuePtr != nullptr;
  const bool byAccessor = setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter;
  if (byPointer || byAccessor) {
    const uint8_t current = byPointer ? SETTINGS.*(setting.valuePtr) : setting.valueGetter();
    if (choiceCount > 1) {
      // Captured BY VALUE, all of it: onSelect calls onChanged(), which
      // rebuilds the vector `setting` refers into.
      const std::vector<uint8_t> order = setting.resolvedDisplayOrder();
      const auto valuePtr = setting.valuePtr;
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [valuePtr, valueSetter, order, onChanged](const int idx) {
        if (idx < 0 || idx >= static_cast<int>(order.size())) return;
        const uint8_t picked = order[static_cast<size_t>(idx)];
        if (valuePtr != nullptr) {
          SETTINGS.*valuePtr = picked;
        } else {
          valueSetter(picked);
        }
        onChanged();
      };
      const int currentPos = static_cast<int>(setting.positionOfStored(current));
      if (!setting.enumStringValues.empty()) {
        popup.show(setting.nameId, setting.orderedEnumStringValues(), currentPos, std::move(onSelect));
      } else {
        const std::vector<StrId> ordered = setting.orderedEnumValues();
        popup.show(setting.nameId, ordered.data(), static_cast<int>(ordered.size()), currentPos, std::move(onSelect));
      }
      return true;
    }
    // A row with no choices at all must not reach the modulus: on RISC-V a
    // divide by zero does not trap, it returns the dividend, so the stored
    // index climbs until the value column renders blank.
    if (choiceCount == 0) return false;
    const uint8_t next = static_cast<uint8_t>((current + 1) % static_cast<uint8_t>(choiceCount));
    if (byPointer) {
      SETTINGS.*(setting.valuePtr) = next;
    } else {
      setting.valueSetter(next);
    }
    onChanged();
    return false;
  }

  if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const uint8_t current = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = (current + setting.valueRange.step > setting.valueRange.max)
                                       ? setting.valueRange.min
                                       : static_cast<uint8_t>(current + setting.valueRange.step);
    onChanged();
    return false;
  }

  return false;
}

}  // namespace settingrow
