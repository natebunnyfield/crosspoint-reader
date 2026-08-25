#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "activities/settings/SettingDisplayOrder.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };

enum class SettingAction {
  None,
  Network,
  ClearCache,
  SdFirmwareUpdate,
  Language,
  DeviceOwner,
  TextSettings,
  // The granular typographic controls, chiefly the per-ligature toggles
  // (owner ruling 2026-08-24). Not persisted anywhere -- this enum names
  // device-only ACTIONS, which carry no key -- so it may be reordered freely.
  Typography,
  PairBluetoothKeyboard,
  ForgetBluetoothKeyboard,
  Colophon,
};

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::vector<std::string> enumStringValues;  // runtime alternative to StrId enumValues (for SD card fonts etc.)
  SettingAction action = SettingAction::None;

  // How many choices this ENUM row actually has. ALWAYS size an ENUM row with
  // this, never with enumValues.size().
  //
  // A row supplies EITHER translated enumValues or runtime enumStringValues,
  // and a row of the second kind leaves enumValues EMPTY. Code that reached for
  // enumValues.size() directly therefore saw 0, and did three separate damaging
  // things to the two rows built that way (Typing Redraw Delay, Editor Font):
  // the popup gate `size() > 2` never opened a picker; the fall-through toggle
  // evaluated `(v + 1) % 0`, undefined behavior that on RISC-V returns the
  // dividend, letting the stored index climb past the end of the label list
  // until the value column rendered blank; and fromJson's clamp `val < 0` was
  // never true, so the saved byte was discarded on every boot.
  size_t enumCount() const { return enumStringValues.empty() ? enumValues.size() : enumStringValues.size(); }

  // The order the PICKER lists the choices in, as display position -> stored
  // index. Empty means identity: display position IS the stored index.
  //
  // An ENUM setting persists the row's INDEX, so for years the only way to keep
  // saved settings.json files pointing at the same thing was to freeze the list
  // order -- new choices had to be appended, whatever they meant. That is why
  // Typing Redraw Delay read "25, 50, 100, 250, 500, 1000, 0 ms" with the zero
  // last, why the sleep-screen picker had "None" sitting between "Cover Custom"
  // and "Quick Resume", and why the two editor fonts that need no SD card came
  // after the three that do nothing until one is installed.
  //
  // This decouples the two. The stored index never moves; only the order it is
  // presented in does, so the reader-facing order can be fixed at any time
  // without a migration and without re-pointing anyone's saved settings.
  std::vector<uint8_t> displayOrder;
  // True only for rows that deliberately WITHDRAW choices: the order may then
  // list a subset of the enum, and the picker shows only what it lists.
  // Stored values outside the subset still decode (the row renderer indexes
  // the raw vectors), they just cannot be re-picked.
  bool displayOrderIsSubset = false;

  // See SettingDisplayOrder.h for what these do and why a bad table degrades to
  // identity instead of hiding a choice.
  std::vector<uint8_t> resolvedDisplayOrder() const {
    return settingorder::resolve(displayOrder, enumCount(), displayOrderIsSubset);
  }

  size_t positionOfStored(uint8_t stored) const {
    return settingorder::positionOf(displayOrder, enumCount(), stored, displayOrderIsSubset);
  }

  // Labels in display order. The row renderer does NOT use these -- it indexes
  // the raw vectors by the stored value, which is unaffected by presentation
  // order -- so these exist only for the picker.
  std::vector<StrId> orderedEnumValues() const {
    return settingorder::reorder(displayOrder, enumValues, displayOrderIsSubset);
  }

  std::vector<std::string> orderedEnumStringValues() const {
    return settingorder::reorder(displayOrder, enumStringValues, displayOrderIsSubset);
  }

  SettingInfo& withDisplayOrder(std::vector<uint8_t> order) {
    displayOrder = std::move(order);
    return *this;
  }

  // Deliberate withdrawal: show ONLY the listed values, in this order.
  SettingInfo& withDisplaySubset(std::vector<uint8_t> order) {
    displayOrder = std::move(order);
    displayOrderIsSubset = true;
    return *this;
  }

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;             // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;  // Category for web UI grouping
  bool obfuscated = false;               // Save/load via base64 obfuscation (passwords)
  // There is no inTextSettings flag any more. It marked the two rows the
  // Text Settings screen covers (font family and size) so the flat Reader list
  // could hide them; the Reader tab is withdrawn, so nothing reads it and the
  // list it referred to does not exist.

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public Activity {
  ButtonNavigator buttonNavigator;

  // Index into deviceSettings[]. Plain 0-based: there is no tab bar, so no
  // row 0 standing in for one.
  int selectedSettingIndex = 0;
  int settingsCount = 0;

  // The one list the device shows: the STR_CAT_SYSTEM entries of the shared
  // list, plus the device-only actions. There is no displaySettings and no
  // readerSettings — the Display, Controls and Reader tabs are all withdrawn
  // from the device UI. Their entries stay in getSettingsList() because that
  // list also drives persistence (CrossPointSettings::fromJson/toJson) and the
  // web settings API.
  std::vector<SettingInfo> deviceSettings;


  OptionPopup optionPopup;

  void toggleCurrentSetting();
  void rebuildSettingsLists();

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Settings", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
