#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "ClearCacheActivity.h"
#include "ClockOffsetActivity.h"
#include "ColophonActivity.h"
#include "CrossPointSettings.h"
#include "EditorFontSelectionActivity.h"
#include "FontSelectionActivity.h"
#include "MappedInputManager.h"
#include "SystemFont.h"
#include "activities/boot_sleep/SleepScreenPolicy.h"
#include "notes/BleHidHost.h"
#ifndef CROSSPOINT_NO_DEVICE_FLASH
#include "SdFirmwareUpdateActivity.h"
#endif
#include "SdCardFontSystem.h"
#include "SettingRowUi.h"
#include "SettingsList.h"
#include "TypographySettingsActivity.h"
#include "activities/network/WifiSelectionActivity.h"
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
    // What the Reader tab kept: the Reader Font action (font family and size,
    // appended below) and Screen Margin, which now carries STR_CAT_SYSTEM and
    // so arrives through this loop.
    if (setting.category != StrId::STR_CAT_SYSTEM) continue;
    deviceSettings.push_back(setting);
  }

  // Append device-only ACTION items
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
  // Bluetooth keyboard: pairing lives here rather than only behind the
  // long-press gesture in Create Note / Claude, so it is discoverable.
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_PAIR_BT_KEYBOARD, SettingAction::PairBluetoothKeyboard));
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_FORGET_BT_KEYBOARD, SettingAction::ForgetBluetoothKeyboard));
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
#ifndef CROSSPOINT_NO_DEVICE_FLASH
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
#endif
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_DEVICE_OWNER, SettingAction::DeviceOwner));
  // Informational, so it sits last: who wrote this firmware and what it is
  // built out of. Nothing here changes a setting.
  deviceSettings.push_back(SettingInfo::Action(StrId::STR_COLOPHON, SettingAction::Colophon));
  // "Reader Font" was called "Text Settings" until 2026-08-24, when the owner
  // renamed it ("rename Text Settings to Reader Font"). Only the visible
  // string moved: StrId::STR_TEXT_SETTINGS is unchanged, so no stored value
  // and no web-API key is affected, and the id still reads TEXT_SETTINGS
  // everywhere. The translations file cannot carry this note -- its parser
  // accepts only KEY: "value" lines (scripts/gen_i18n.py) -- so it lives
  // here, at the row itself. Comments elsewhere that quote a dated ruling
  // verbatim still say "Text Settings"; they mean this row.
  //
  // THE HEAD OF THE LIST, in order, and this comment is the only place it is
  // written down -- the two rows it used to name (Typing Redraw Delay, Screen
  // Margin) had both been gone for days and it sent a reader to the owner with
  // a wrong question about ordering. As it actually stands:
  //
  //   Reader Font        (inserted here, and it leads: font family, size,
  //                         live preview -- the reading screen's own settings)
  //   Typography Settings  (inserted here, owner ruling 2026-08-24:
  //                         "Typography Settings should be between Text
  //                         Settings and Editing Font")
  //   Editor Font          | the STR_CAT_SYSTEM rows the shared list
  //   Dark Mode            | contributes, in getSettingsList() order
  //   Keyboard
  //   Sleep Screen
  //   Clock UTC Offset
  //   ... then the device-only ACTIONs appended above.
  //
  // Line Grid and Justified Text used to sit between Editor Font and Dark Mode.
  // They moved to Typography Settings on 2026-08-24 -- their category is
  // STR_CAT_READER now, so the loop above drops them here. This block is why
  // that had to be edited in the same commit as the move.
  //
  // Read off a rendered Settings screen on 2026-08-24, not off this file --
  // the loop above drops every row whose category is not STR_CAT_SYSTEM, so
  // reading getSettingsList() in order gives a longer list than the device
  // shows. Editor Font SIZE and Focus Reading are the two that look like they
  // belong here and do not: both carry STR_CAT_READER and never arrive.
  //
  // Two rows that a reader may go looking for and will not find: Typing Redraw
  // Delay and Screen Margin. Both were WITHDRAWN, not deleted, and both VALUES
  // still apply -- the debounce is `static constexpr displayDebounce` at 250 ms
  // (CrossPointSettings.h) and still governs how long typing settles before
  // the panel redraws; the margin persists and is card-controlled (removed
  // 2026-08-22 by owner layout-exactness order). Their StrIds are still in the
  // translations, along with 70-odd others, because deleting a key renumbers
  // StrId for every language for a few bytes of flash.
  //
  // The two inserts are written back-to-front on purpose: each goes to
  // begin(), so the LAST one inserted ends up first.
  deviceSettings.insert(deviceSettings.begin(),
                        SettingInfo::Action(StrId::STR_TYPOGRAPHY_SETTINGS, SettingAction::Typography));
  deviceSettings.insert(deviceSettings.begin(),
                        SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
  // Manage Fonts is withdrawn. SD card fonts are still discovered and
  // selectable from Reader Font.

  settingsCount = static_cast<int>(deviceSettings.size());
  if (selectedSettingIndex >= settingsCount) {
    selectedSettingIndex = settingsCount > 0 ? settingsCount - 1 : 0;
  }
}

void SettingsActivity::onEnter() {
  Activity::onEnter();

  // Reset selection to the first row
  selectedSettingIndex = 0;
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
  // screen — rather than becoming a dead gesture on what is now the longest
  // list in the firmware.
  buttonNavigator.onNextContinuous([this, settingsPageItems] {
    selectedSettingIndex = ButtonNavigator::nextPageIndex(selectedSettingIndex, settingsCount, settingsPageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, settingsPageItems] {
    selectedSettingIndex = ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount, settingsPageItems);
    requestUpdate();
  });

  // The SIDE pair pages by a whole screenful; the FRONT pair above steps one
  // row. They used to be the same action (docs/ui-conventions.md, "Side buttons
  // should page, not repeat the front buttons"). pageDown/pageUp clamp at the
  // ends and return false when nothing moved, so a short list costs no redraw.
  buttonNavigator.onPageNext([this, settingsPageItems] {
    if (!ButtonNavigator::pageDown(selectedSettingIndex, settingsCount, settingsPageItems)) return;
    requestUpdate();
  });

  buttonNavigator.onPagePrevious([this, settingsPageItems] {
    if (!ButtonNavigator::pageUp(selectedSettingIndex, settingsCount, settingsPageItems)) return;
    requestUpdate();
  });
}

void SettingsActivity::toggleCurrentSetting() {
  if (selectedSettingIndex < 0 || selectedSettingIndex >= settingsCount) {
    return;
  }

  const auto& setting = deviceSettings[selectedSettingIndex];
  // quickResumeSleepScreen is hardcoded OFF since 2026-08-21, so the sleep
  // screen row no longer has a timeout row to reconcile with -- the whole
  // sleepscreen::reconcile seam left with it.
  // Panel polarity has to reach the driver before this screen repaints, or the
  // settings list redraws in the outgoing polarity and the row reads "on" over
  // a still-white page until something else forces a refresh.
  const bool darkModeChanged = setting.valuePtr == &CrossPointSettings::darkMode;

  if (setting.nameId == StrId::STR_EDITOR_FONT) {
    // Confirm opens the picker with its specimen pane rather than a five-name
    // popup. These faces differ only in glyph-width behavior, which no list of
    // names can convey.
    //
    // The row stays an ENUM in getSettingsList() on purpose: that list is also
    // what fromJson()/toJson() and the web settings API iterate, so turning it
    // into an ACTION would stop editorFont persisting and drop it from the web
    // UI. Only the way the DEVICE reaches it changes. Same shape as the clock
    // offset row below.
    startActivityForResult(std::make_unique<EditorFontSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             // The picker applies and saves as it goes; this
                             // just refreshes the row's rendered value.
                             rebuildSettingsLists();
                             requestUpdate();
                           });
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

  // The generic row kinds -- toggle in place, step a lone choice, or open the
  // picker -- are settingrow::activate(), shared with the Typography screen so
  // the two cannot drift. Everything above this point is a row Settings treats
  // specially; everything below is the ACTION rows, which are device-only and
  // have no counterpart there.
  if (setting.type != SettingType::ACTION) {
    const bool opened = settingrow::activate(setting, optionPopup, [this, darkModeChanged] {
      // Panel polarity has to reach the driver before this screen repaints,
      // or the row reads "on" over a still-white page until something else
      // forces a refresh.
      if (darkModeChanged) display.setInverted(SETTINGS.darkMode != 0);
      SETTINGS.saveToFile();
      // rebuildSettingsLists() clamps selectedSettingIndex to the new count.
      rebuildSettingsLists();
    });
    if (opened) requestUpdate();
    return;
  }

  {
    auto resultHandler = [this](const ActivityResult&) { SETTINGS.saveToFile(); };

    switch (setting.action) {
      case SettingAction::PairBluetoothKeyboard: {
        // Bring the radio up and scan. The host connects to the first HID
        // keyboard it sees and bonds; NVS keeps the bond so Create Note and
        // Claude reconnect on their own afterwards.
        const bool started = blekbd::begin();
        GUI.drawPopup(renderer, started ? tr(STR_PAIR_BT_SCANNING) : tr(STR_PAIR_BT_FAILED));
        requestUpdate();
        break;
      }
      case SettingAction::ForgetBluetoothKeyboard:
        blekbd::forgetAllBonds();
        blekbd::end();
        GUI.drawPopup(renderer, tr(STR_FORGET_BT_DONE));
        requestUpdate();
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
#ifndef CROSSPOINT_NO_DEVICE_FLASH
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
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
      case SettingAction::Typography:
        // The screen applies and saves as it goes, exactly like Reader Font
        // above; this refreshes nothing on the parent list, which carries no
        // typography row to re-render, and only re-saves for the same
        // belt-and-braces reason every other sub-screen here does.
        startActivityForResult(std::make_unique<TypographySettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Language: {
        // Show the language list in the in-place option popup rather than
        // pushing a full sub-screen. Only two languages exist, so this avoids
        // the cost of an extra activity for what is a two-item pick.
        const uint8_t langCount = getLanguageCount();
        std::vector<std::string> langNames;
        langNames.reserve(langCount);
        for (uint8_t i = 0; i < langCount; i++) {
          langNames.emplace_back(I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[i])));
        }
        const uint8_t currentLang = static_cast<uint8_t>(I18N.getLanguage());
        int currentLangIdx = 0;
        for (int i = 0; i < static_cast<int>(langCount); i++) {
          if (SORTED_LANGUAGE_INDICES[i] == currentLang) {
            currentLangIdx = i;
            break;
          }
        }
        optionPopup.show(StrId::STR_LANGUAGE, langNames, currentLangIdx, [this](int idx) {
          const uint8_t langIndex = SORTED_LANGUAGE_INDICES[idx];
          I18N.setLanguage(static_cast<Language>(langIndex));
          SETTINGS.language = langIndex;
          SETTINGS.saveToFile();
          // Rebuild so all row labels re-translate in the new language.
          rebuildSettingsLists();
        });
        requestUpdate();
        break;
      }
      case SettingAction::DeviceOwner:
        // Free-text owner name, shown on the sleep screens. Saved immediately:
        // sleep can power the device off, and an unsaved name would vanish.
        startActivityForResult(makeTextEntryActivity(renderer, mappedInput, tr(STR_DEVICE_OWNER), SETTINGS.ownerName,
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
      case SettingAction::Colophon:
        // Read-only screen: nothing to persist, so it takes no result handler.
        startActivityForResult(std::make_unique<ColophonActivity>(renderer, mappedInput), nullptr);
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
  }
  // Every ACTION either opens a sub-activity whose result handler persists, or
  // has already done its work above. Nothing to save here: the generic rows
  // returned earlier, through settingrow::activate's onChanged.
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
        // The stored byte here is a biased quarter-hour count; show the offset
        // it means, using the same helper the picker does. Every other row is
        // the generic shape, shared with the Typography screen.
        if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr &&
            setting.nameId == StrId::STR_CLOCK_UTC_OFFSET) {
          return formatUtcOffset(SETTINGS.*(setting.valuePtr));
        }
        return settingrow::valueText(setting);
      },
      true);

  // Draw help text. Rows that open a popup or a sub-screen say "Select";
  // rows that toggle a value in place say "Toggle". There is no category name
  // in this hint — there is no second tab to switch to.
  auto opensSubScreen = [](const SettingInfo& setting) {
    if (setting.type == SettingType::ACTION) return true;
    // Two rows Settings intercepts before the generic path, both opening a
    // sub-activity rather than a popup.
    if (setting.nameId == StrId::STR_CLOCK_UTC_OFFSET) return true;
    if (setting.nameId == StrId::STR_EDITOR_FONT) return true;
    return settingrow::opensPicker(setting);
  };
  const bool onRow = selectedSettingIndex >= 0 && selectedSettingIndex < settingsCount;
  const auto confirmLabel = (onRow && opensSubScreen(settings[selectedSettingIndex])) ? tr(STR_SELECT) : tr(STR_TOGGLE);

  // Back exits straight to Home (see loop()); label it as what it does.
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
}
