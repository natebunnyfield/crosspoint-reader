#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "ButtonRemapActivity.h"
#include "ClearCacheActivity.h"
#include "ClockOffsetActivity.h"
#include "CrossPointSettings.h"
#ifndef CROSSPOINT_NO_NETWORK
#include "FontDownloadActivity.h"
#endif
#include "FontSelectionActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "SystemFont.h"
#include "activities/boot_sleep/SleepScreenPolicy.h"
#ifndef CROSSPOINT_NO_NETWORK
#include "OtaUpdateActivity.h"
#include "SdFirmwareUpdateActivity.h"
#endif
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#ifndef CROSSPOINT_NO_NETWORK
#include "activities/network/WifiSelectionActivity.h"
#endif
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/TextEntryFactory.h"
#include "components/UITheme.h"
#include "fontIds.h"

void SettingsActivity::rebuildSettingsLists() {
  deviceSettings.clear();

  // Pick up any fonts uploaded/deleted over the web server since the last
  // reader activity ran — otherwise the font-family picker shows stale list.
  sdFontSystem.refreshIfDirty();

  for (auto& setting : getSettingsList(&sdFontSystem.registry())) {
    // Display, Controls and Reader are all withdrawn from the device UI, so
    // their entries are dropped here rather than removed from
    // getSettingsList() — that list is also what fromJson()/toJson() iterate,
    // so deleting entries would stop those settings persisting at all, and it
    // is what the web settings API serves. Every withdrawn row is either pinned
    // by normalizeRetiredSettings() or left holding whatever is stored; see
    // that function for which is which.
    //
    // What the Reader tab kept: the Text Settings action (font family and size,
    // appended below) and Screen Margin, which now carries STR_CAT_SYSTEM and
    // so arrives through this loop.
    if (setting.category != StrId::STR_CAT_SYSTEM) continue;
    deviceSettings.push_back(setting);
  }

  // Append device-only ACTION items
#ifndef CROSSPOINT_NO_NETWORK
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
#endif
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
#ifndef CROSSPOINT_NO_NETWORK
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
#endif
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_DEVICE_OWNER, SettingAction::DeviceOwner));
  // Text Settings leads the list: it is the reading screen's own settings
  // (font family, size, live preview), and Screen Margin sits directly under it
  // as the first entry the shared list contributes.
  deviceSettings.insert(deviceSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  // Manage Fonts is withdrawn. SD card fonts are still discovered and
  // selectable from Text Settings.

  settingsCount = static_cast<int>(deviceSettings.size());
  if (selectedSettingIndex >= settingsCount) {
    selectedSettingIndex = settingsCount > 0 ? settingsCount - 1 : 0;
  }
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Reset selection to the first row
  selectedSettingIndex = 0;
  {
    const sleepscreen::QuickResumeState start = sleepscreen::initialState(SETTINGS.quickResumeSleepScreen);
    quickResumeTimeoutAutoEnabled = start.autoEnabled;
    preserveQuickResumeTimeoutOn = start.preserveOn;
  }
  // sleepScreenChanged is FALSE: opening this screen is not the owner picking a
  // sleep screen. The pass still runs, because the Quick Resume MODE implies the
  // timeout row and that has to hold however the screen was reached.
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/false, /*quickResumeTimeoutChanged=*/false);

  rebuildSettingsLists();

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
}

void SettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Handle actions with early return
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggleCurrentSetting();
    requestUpdate();
    return;
  }

  // Back leaves outright. It used to send the selection back to the tab bar
  // first and only exit on a second press; with the tab bar gone there is
  // nothing to go back TO, and a two-press exit would be worse than what every
  // other list screen in the firmware does.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    SETTINGS.saveToFile();
    onGoHome();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // The bespoke hit-test this replaced existed only to undo the tab bar's +1
  // row offset. Without it the shared helper is exact.
  switch (handleListTouch(selectedSettingIndex, settingsCount, listTop, listHeight, false)) {
    case ListTouchResult::Activated:
      toggleCurrentSetting();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  // Handle navigation
  const int settingsPageItems = GUI.getListPageItems(listHeight, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectedSettingIndex = ButtonNavigator::nextPageIndex(selectedSettingIndex, settingsCount, settingsPageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectedSettingIndex = ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount, settingsPageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount);
    requestUpdate();
  });

  // A held Up/Down used to switch category. There are no categories left, so it
  // pages the list instead — the same thing a hold does on every other list
  // screen (LanguageSelectActivity.cpp) — rather than becoming a dead gesture on
  // what is now the longest list in the firmware.
  buttonNavigator.onNextContinuous([this, settingsPageItems] {
    selectedSettingIndex = ButtonNavigator::nextPageIndex(selectedSettingIndex, settingsCount, settingsPageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, settingsPageItems] {
    selectedSettingIndex = ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount, settingsPageItems);
    requestUpdate();
  });
}

void SettingsActivity::toggleCurrentSetting() {
  if (selectedSettingIndex < 0 || selectedSettingIndex >= settingsCount) {
    return;
  }

  const auto& setting = deviceSettings[selectedSettingIndex];
  const bool sleepScreenChanged = setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;
  // The system font has to be pushed into the renderer before anything repaints,
  // otherwise the popup closes onto a screen still drawn in the old face.
  const bool systemFontChanged = setting.valuePtr == &CrossPointSettings::systemFont;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.nameId == StrId::STR_CLOCK_UTC_OFFSET) {
    // Upstream's device UI never steps this value in place. The stored byte is
    // quarter-hours biased by 48, so a VALUE row would walk 0..104 one 15-minute
    // notch at a time -- 96 presses to reach UTC+12. Confirm opens the named
    // time zone list instead, which saves the picked zone's offset itself
    // (hence no result handler). The row still renders the stored byte through
    // formatUtcOffset(), so any offset -- including ones no list entry uses --
    // reads back correctly.
    startActivityForResult(std::make_unique<ClockOffsetActivity>(renderer, mappedInput), nullptr);
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (setting.enumValues.size() > 2) {
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()),
                       currentValue,
                       [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged, systemFontChanged](int idx) {
                         SETTINGS.*valuePtr = idx;
                         // Before rebuildSettingsLists(), which measures rows with the UI
                         // font and would otherwise size them against the outgoing face.
                         if (systemFontChanged) applySystemFont(renderer);
                         syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
                         SETTINGS.saveToFile();
                         rebuildSettingsLists();
                       });
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % static_cast<uint8_t>(setting.enumValues.size());
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        rebuildSettingsLists();
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const int8_t currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
        break;
        break;
        break;
#ifndef CROSSPOINT_NO_NETWORK
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
#endif
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
#ifndef CROSSPOINT_NO_NETWORK
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
#endif
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<FontSelectionActivity>(renderer, mappedInput, &sdFontSystem.registry()),
                               [this](const ActivityResult&) {
                                 // The picker applies changes live but does not persist them.
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DeviceOwner:
        // Free-text owner name, shown on the sleep screens. Saved immediately:
        // sleep can power the device off, and an unsaved name would vanish.
        startActivityForResult(
            makeTextEntryActivity(renderer, mappedInput, tr(STR_DEVICE_OWNER), SETTINGS.ownerName,
                                  sizeof(SETTINGS.ownerName) - 1, InputType::Text),
            [this](const ActivityResult& result) {
              if (!result.isCancelled) {
                const auto& kb = std::get<KeyboardResult>(result.data);
                strncpy(SETTINGS.ownerName, kb.text.c_str(), sizeof(SETTINGS.ownerName) - 1);
                SETTINGS.ownerName[sizeof(SETTINGS.ownerName) - 1] = '\0';
                SETTINGS.saveToFile();
              }
            });
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  // rebuildSettingsLists() clamps selectedSettingIndex to the new count itself.
  rebuildSettingsLists();
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  // The decision itself lives in sleepscreen::reconcile so it can be tested
  // without this activity (test/sleep_screen). This is the seam that applies it.
  const sleepscreen::QuickResumeState next = sleepscreen::reconcile(
      SETTINGS.sleepScreen,
      {SETTINGS.quickResumeSleepScreen, quickResumeTimeoutAutoEnabled, preserveQuickResumeTimeoutOn},
      sleepScreenChanged, quickResumeTimeoutChanged);

  SETTINGS.quickResumeSleepScreen = next.quickResumeOnTimeout;
  quickResumeTimeoutAutoEnabled = next.autoEnabled;
  preserveQuickResumeTimeoutOn = next.preserveOn;
}

void SettingsActivity::openSleepTimeoutPicker() {
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, true, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  // No tab bar. With Reader withdrawn there is one category left, and a tab bar
  // that cannot switch to anything is not a control — it would only occupy a
  // band of the panel and a focus stop whose Confirm does nothing. The rows it
  // used to cost are given back to the list, which now holds every setting the
  // device exposes.
  const int listTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto& settings = deviceSettings;
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing},
      settingsCount, selectedSettingIndex,
      [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
      [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && (setting.valuePtr != nullptr || setting.valueGetter)) {
          // WHERE THE VALUE COMES FROM AND WHERE THE LABEL COMES FROM ARE
          // INDEPENDENT. A row supplies its value through either a member
          // pointer or a getter, and its labels through either translated
          // StrIds (enumValues) or strings it builds at runtime
          // (enumStringValues) -- any combination is legal. This used to pick
          // the label source off the value source, so a row with valuePtr was
          // forced down the enumValues path: the UTC offset row builds its own
          // labels and leaves enumValues empty, and rendering it indexed that
          // empty vector with 48 (UTC+00:00) and segfaulted at address 0x60.
          // enumStringValues wins whenever populated, matching what the option
          // popup (totalValues, above) and the web settings API already do.
          const uint8_t value = setting.valuePtr != nullptr ? SETTINGS.*(setting.valuePtr) : setting.valueGetter();
          // Both indexes are bounds-checked: a stored value can outlive the
          // label list it was written against (a font uninstalled, an enum
          // shortened), and neither vector is guaranteed to cover it.
          if (!setting.enumStringValues.empty()) {
            if (value < setting.enumStringValues.size()) valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          if (setting.nameId == StrId::STR_CLOCK_UTC_OFFSET) {
            // The stored byte is a biased quarter-hour count; show the offset it
            // means. Same helper the picker uses.
            valueText = formatUtcOffset(SETTINGS.*(setting.valuePtr));
          } else if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
            char valueBuffer[32];
            if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
              valueText = tr(STR_SLEEP_NEVER);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                       static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
              valueText = valueBuffer;
            }
          } else {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr));
          }
        }
        return valueText;
      },
      true);

  // Draw help text. Rows that open a screen say "Select", not "Toggle" --
  // Confirm on them does not cycle a value. (Upstream's status bar screen used a
  // blanket "Toggle" hint for every row; this fork already made the distinction
  // for Time to Sleep, and the offset row behaves the same way.)
  //
  // ACTION rows are the same case and were saying "Toggle": every one of them
  // opens a screen. Text Settings — the row this list now leads with — is one.
  //
  // There is no category name in this hint any more. It named the tab Confirm
  // would switch to, and there is no second tab to switch to.
  auto opensSubScreen = [](const SettingInfo& setting) {
    return setting.type == SettingType::ACTION || setting.nameId == StrId::STR_TIME_TO_SLEEP ||
           setting.nameId == StrId::STR_CLOCK_UTC_OFFSET;
  };
  const bool onRow = selectedSettingIndex >= 0 && selectedSettingIndex < settingsCount;
  const auto confirmLabel = (onRow && opensSubScreen(settings[selectedSettingIndex])) ? tr(STR_SELECT) : tr(STR_TOGGLE);

  // Back exits straight to Home (see loop()); label it as what it does.
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
