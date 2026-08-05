#pragma once
#include <memory>
#include <string>
#include <utility>

#include "CrossPointSettings.h"
#include "DaisyEntryActivity.h"
#include "KeyboardEntryActivity.h"

// The one place SETTINGS.keyboardLayout is routed: every text-entry call site
// asks here instead of naming an activity. The daisywheel is its own activity;
// 13-grid and QWERTY are both KeyboardEntryActivity (it reads the setting
// itself in onEnter). Both return the same KeyboardResult contract.
inline std::unique_ptr<Activity> makeTextEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                       std::string title, std::string initialText = "",
                                                       const size_t maxLength = 0,
                                                       const InputType inputType = InputType::Text) {
  if (SETTINGS.keyboardLayout == CrossPointSettings::KEYBOARD_DAISY) {
    return std::make_unique<DaisyEntryActivity>(renderer, mappedInput, std::move(title), std::move(initialText),
                                                maxLength, inputType);
  }
  return std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::move(title), std::move(initialText),
                                                 maxLength, inputType);
}
