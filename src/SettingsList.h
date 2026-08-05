#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "activities/settings/SettingsActivity.h"

// Build the font family setting dynamically. When registry is non-null, SD card fonts
// are appended after the built-in fonts. Otherwise only built-in fonts are listed.
// The built-in Noto Serif / Noto Sans faces are hidden from the reader font
// picker whenever SD card fonts are installed, so the list shows only the
// user's own curated set.
//
// They are NOT removed from the firmware: they remain the UI faces, and
// CrossPointSettings::getReaderFontId() still falls back to Noto Serif when a
// selected SD font cannot be resolved (card pulled, .cpfont deleted). Hiding
// them is therefore a menu-only change with a working safety net behind it.
//
// When NO SD fonts are installed this function is not used at all — see
// getSettingsList() below, which keeps the two built-in entries so the picker
// can never be empty and leave the reader with nothing to render text in.
inline SettingInfo buildFontFamilySetting(const SdCardFontRegistry* registry) {
  // SD card family names, in registry order. These are the only options shown.
  std::vector<std::string> sdFamilyNames;
  if (registry) {
    const auto& families = registry->getFamilies();
    sdFamilyNames.reserve(families.size());
    std::transform(families.begin(), families.end(), std::back_inserter(sdFamilyNames),
                   [](const SdCardFontFamilyInfo& f) { return f.name; });
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_FAMILY;
  s.type = SettingType::ENUM;
  // enumValues stays empty: the render path prefers enumStringValues when set.
  s.enumStringValues = sdFamilyNames;
  s.key = "fontFamily";
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [sdFamilyNames]() -> uint8_t {
    for (int i = 0; i < static_cast<int>(sdFamilyNames.size()); i++) {
      if (sdFamilyNames[i] == SETTINGS.sdFontFamilyName) {
        return static_cast<uint8_t>(i);
      }
    }
    // Nothing selected yet, or the stored name is no longer installed —
    // show the first available SD font.
    return 0;
  };

  s.valueSetter = [sdFamilyNames](uint8_t v) {
    if (v >= sdFamilyNames.size()) return;
    strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[v].c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  };

  return s;
}

// Build the font size setting dynamically. The VALUE is the slot
// (SETTINGS.fontSizeSlot), which is family-independent; the LABELS are built
// from the point sizes the active family actually ships, so the owner can still
// see what a slot resolves to on this family — "M (15pt)".
//
// Families that ship exactly READER_FONT_SLOT_COUNT sizes (every family
// CrossPoint ships) get S/M/L/XL names. A family with a different count — a
// partial install, or a user-built family — falls back to point-size-only
// labels rather than inventing names, so no installed size becomes unreachable.
//
// The ENUM contract shared with the web UI stays index-based, and the index IS
// the slot, so nothing on the wire changes shape.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated: it is the typographic unit symbol,
  // written the same way in every language CrossPoint ships.
  static constexpr const char* SLOT_NAMES[READER_FONT_SLOT_COUNT] = {"S", "M", "L", "XL"};
  const bool named = sizes.size() == READER_FONT_SLOT_COUNT;
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (size_t i = 0; i < sizes.size(); i++) {
    const std::string pt = std::to_string(sizes[i]) + "pt";
    labels.push_back(named ? std::string(SLOT_NAMES[i]) + " (" + pt + ")" : pt);
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t slot = SETTINGS.fontSizeSlot;
    return slot < sizes.size() ? slot : static_cast<uint8_t>(sizes.size() - 1);
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v >= sizes.size()) return;
    SETTINGS.fontSizeSlot = v;
    SETTINGS.fontPointSize = sizes[v];  // keep the derived size in step
  };

  return s;
}

// Screen margin as a drop-down over the SCREEN_MARGIN_MIN..MAX ramp rather than
// a stepper that walks one notch per press.
//
// The STORED value stays the margin in PIXELS, never the picker's index. That
// is the whole reason this is a getter/setter entry instead of a plain
// SettingInfo::Enum over the member pointer: the reader adds
// SETTINGS.screenMargin straight onto its oriented margins
// (EpubReaderActivity::applyMargins) and the value is part of what invalidates
// the section cache, so storing an index would reinterpret every settings.json
// ever written — a saved 20 px margin would come back as 20, be read as index
// 20, and run off the end of a ten-entry ramp.
//
// valuePtr is deliberately left null. CrossPointWebServer's ENUM case prefers
// valuePtr over valueSetter when both are set, and would write the raw index
// into the byte.
//
// Filed under System since 2026-08-04. It is a reading setting by nature, but
// the Reader tab was withdrawn from the device UI and this row is one of the
// two the owner kept, so its category moves with it rather than the device
// growing a second, divergent notion of where a row lives. Same move the sleep
// group made out of the withdrawn Display tab, for the same reason.
inline SettingInfo buildScreenMarginSetting() {
  std::vector<uint8_t> steps;
  steps.reserve(CrossPointSettings::SCREEN_MARGIN_MAX / CrossPointSettings::SCREEN_MARGIN_STEP + 1);
  for (int v = CrossPointSettings::SCREEN_MARGIN_MIN; v <= CrossPointSettings::SCREEN_MARGIN_MAX;
       v += CrossPointSettings::SCREEN_MARGIN_STEP) {
    steps.push_back(static_cast<uint8_t>(v));
  }

  // Bare numbers: a unit suffix would have to be translated, and the row already
  // says what it is.
  std::vector<std::string> labels;
  labels.reserve(steps.size());
  for (const uint8_t v : steps) labels.push_back(std::to_string(v));

  SettingInfo s;
  s.nameId = StrId::STR_SCREEN_MARGIN;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "screenMargin";
  s.category = StrId::STR_CAT_SYSTEM;

  s.valueGetter = [steps]() -> uint8_t {
    const int cur = SETTINGS.screenMargin;
    // Nearest rather than exact. A byte saved under the old 5..40 stepper, or
    // posted by an API client, need not sit on the ramp; falling back to index
    // 0 would quietly move the reader's margin the next time this row rendered.
    uint8_t best = 0;
    int bestDist = 256;
    for (size_t i = 0; i < steps.size(); i++) {
      const int d = steps[i] > cur ? steps[i] - cur : cur - steps[i];
      if (d < bestDist) {
        bestDist = d;
        best = static_cast<uint8_t>(i);
      }
    }
    return best;
  };

  s.valueSetter = [steps](const uint8_t v) {
    if (v >= steps.size()) return;
    SETTINGS.screenMargin = steps[v];
  };

  return s;
}

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.
//
// The static list is constructed exactly once (master's optimization, #1086 +
// #1636) so the per-entry SettingInfo cost is paid once; every call then copies
// it. When an SdCardFontRegistry is supplied AND has SD card fonts installed,
// the font-family entry is replaced in that copy with a registry-aware version.
// The font-size entry is always rebuilt, since its options are point sizes read
// from the active family rather than a fixed enum.
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr) {
  static const std::vector<SettingInfo> baseList = [] {
    // Enum settings are persisted as numeric values. Assign these labels by enum
    // value so a reordered menu or enum cannot silently swap their behavior.
    std::vector<StrId> sleepScreenValues(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT);
    sleepScreenValues[CrossPointSettings::DARK] = StrId::STR_DARK;
    sleepScreenValues[CrossPointSettings::LIGHT] = StrId::STR_LIGHT;
    sleepScreenValues[CrossPointSettings::CUSTOM] = StrId::STR_CUSTOM;
    sleepScreenValues[CrossPointSettings::COVER] = StrId::STR_COVER;
    sleepScreenValues[CrossPointSettings::COVER_CUSTOM] = StrId::STR_COVER_CUSTOM;
    sleepScreenValues[CrossPointSettings::BLANK] = StrId::STR_NONE_OPT;
    sleepScreenValues[CrossPointSettings::QUICK_RESUME] = StrId::STR_QUICK_RESUME;
    sleepScreenValues[CrossPointSettings::CALENDAR] = StrId::STR_SLEEP_CALENDAR;
    sleepScreenValues[CrossPointSettings::CALENDAR_FOUR] = StrId::STR_CALENDAR_FOUR;
    sleepScreenValues[CrossPointSettings::CALENDAR_FIVE] = StrId::STR_CALENDAR_FIVE;
    sleepScreenValues[CrossPointSettings::CALENDAR_SIX] = StrId::STR_CALENDAR_SIX;
    sleepScreenValues[CrossPointSettings::CALENDAR_WESTSIDE] = StrId::STR_SLEEP_WESTSIDE;

    // Built one entry at a time instead of from a single braced initializer_list:
    // the whole list was materialised as one stack temporary (~50 x
    // sizeof(SettingInfo)), which made this frame the largest non-vendor frame in
    // the firmware measured with -fstack-usage on the riscv32 target — far over
    // the 256-byte Resource Protocol limit. push_back() keeps only one SettingInfo
    // on the stack at a time; the entries, their order and their values are
    // unchanged.
    //
    // Exact final capacity. Keep in sync when adding an entry so the push_backs
    // never reallocate.
    //
    // 32 unconditional entries below, plus the simulator-only keep-screen-awake
    // toggle in the System block. Nothing else is pushed: the SD-font-aware
    // font-family/font-size entries REPLACE elements in the returned copy
    // (`*it = ...`), they do not append, and the capability filters below only
    // erase.
    //
    // This constant read 50 with a comment claiming "50 fixed entries plus the
    // one conditional" while the real count was 32 and no conditional push_back
    // existed. Over-reserving is invisible (no realloc, just ~18 unused
    // SettingInfo slots held for the life of the process), which is why it
    // drifted. Corrected to the true count here.
#ifdef SIMULATOR
    constexpr size_t FIXED_ENTRY_COUNT = 33;
#else
    constexpr size_t FIXED_ENTRY_COUNT = 32;
#endif
    std::vector<SettingInfo> v;
    v.reserve(FIXED_ENTRY_COUNT);

    // --- Display ---
    v.push_back(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                                  {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS}, "hideBatteryPercentage",
                                  StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Enum(
        StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
        {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15, StrId::STR_PAGES_30},
        "refreshFrequency", StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                                    StrId::STR_CAT_DISPLAY));

    // --- Reader ---
    // Built-in font-family entry. Replaced per-call with a registry-aware
    // version when SD fonts are installed.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                                  {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER));
    // Placeholder: the selectable sizes depend on the active font family, so
    // this entry is always replaced by buildFontSizeSetting() below. It only
    // fixes the setting's position in the Reader category.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                                  {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing",
                                  StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Enum(
        StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
        {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT, StrId::STR_BOOK_S_STYLE},
        "paragraphAlignment", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                                    StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                    "focusReadingEnabled", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled,
                                    "hyphenationEnabled", StrId::STR_CAT_READER));
    // The reader is portrait-only; there is no orientation setting.
    v.push_back(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                                    "extraParagraphSpacing", StrId::STR_CAT_READER));
    // Values follow CrossPointSettings::TEXT_ANTIALIASING: 0/1 are the
    // legacy Off/On toggle (persisted files round-trip), 2+ appended.
    v.push_back(SettingInfo::Enum(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON, StrId::STR_AA_CRISP, StrId::STR_AA_DARK},
                                  "textAntiAliasing", StrId::STR_CAT_READER));
    v.push_back(
        SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                          {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                          "imageRendering", StrId::STR_CAT_READER));

    // --- Controls ---
    v.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                  {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED},
                                  "sideButtonLayout"));
    v.push_back(SettingInfo::Enum(StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "touchReaderControls"));
    // Third label = stored value 2 = FONT_SIZE_STEP. The order of this array IS
    // the persisted encoding, so append only — see LONG_PRESS_BUTTON_BEHAVIOR in
    // CrossPointSettings.h for why the retired ORIENTATION_CHANGE slot was reused
    // rather than a fourth value added.
    v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                                  {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                   StrId::STR_LONG_PRESS_BEHAVIOR_FONT_SIZE},
                                  "longPressButtonBehavior"));
    v.push_back(SettingInfo::Enum(
        StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
        {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH, StrId::STR_FOOTNOTES},
        "shortPwrBtn"));
    v.push_back(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                                    "pwrBtnFootnoteBack"));
    v.push_back(SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                                    "backShortToFileBrowser"));

    // --- System ---
    // Screen margin leads the System block because it is one of the two rows
    // the withdrawn Reader tab kept (the other is the Text Settings action,
    // which SettingsActivity inserts above this one), and the two belong
    // together at the top of the only list the device now shows: they are what
    // a page of a book looks like. Everything below is about the device.
    v.push_back(buildScreenMarginSetting());
    // The typeface the chrome itself is drawn in. Filed under System rather than
    // Reader because it is not about books: it changes headers, list rows,
    // button hints, popups and the battery readout, and leaves the reader's body
    // face entirely alone. Ubuntu first, because it is the value every existing
    // settings.json already holds by omission.
    // Order must match CrossPointSettings::SYSTEM_FONT -- the index IS the
    // persisted value.
    v.push_back(
        SettingInfo::Enum(StrId::STR_SYSTEM_FONT, &CrossPointSettings::systemFont,
                          {StrId::STR_UBUNTU, StrId::STR_NOTO_SANS, StrId::STR_NOTO_SERIF, StrId::STR_LIBRE_FRANKLIN},
                          "systemFont", StrId::STR_CAT_SYSTEM));
    // Which text-entry keyboard every entry field opens (searches, WiFi
    // passwords, owner name, renames). Order must match
    // CrossPointSettings::KEYBOARD_LAYOUT -- the index IS the persisted value.
    v.push_back(SettingInfo::Enum(StrId::STR_KEYBOARD, &CrossPointSettings::keyboardLayout,
                                  {StrId::STR_KEYBOARD_DAISY, StrId::STR_KEYBOARD_GRID13, StrId::STR_KEYBOARD_QWERTY},
                                  "keyboard", StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Value(
        StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
        {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
        "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));
    // Filed under System, next to Time to Sleep, because that is the row it
    // qualifies: it decides what an inactivity-timeout sleep DRAWS, and while it
    // is ON the Sleep Screen setting is bypassed entirely on that path
    // (SleepActivity::onEnter checks it before the sleepScreen switch). It was
    // originally under Display, which the device UI withdrew, so the only
    // control over the dominant sleep path lived on the web UI. It is also no
    // longer pinned in normalizeRetiredSettings() — a visible row that a reload
    // silently reverts is worse than no row.
    v.push_back(SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                                  StrId::STR_CAT_SYSTEM));
    // The sleep image itself, moved out of the withdrawn Display tab for the same
    // reason as the row above: with no device control, the only way to reach
    // Custom was BmpViewerActivity's "set as sleep screen" side effect or the web
    // UI, and an owner whose stored mode was anything else had no way back.
    //
    // The two cover rows come along because they are sub-options of this one,
    // not independent settings — they are read only when a COVER mode is
    // selected (SleepActivity::renderBitmapSleepScreen). Surfacing the parent
    // without them would let an owner pick Cover and then be unable to choose
    // fit/crop or a filter.
    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                                  std::move(sleepScreenValues), "sleepScreen", StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                                  {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                                  {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                                  "sleepScreenCoverFilter", StrId::STR_CAT_SYSTEM));
    // Simulator/iOS only, deliberately compiled out on device.
    //
    // On an X4/X3 this row would be a control that cannot do anything: e-ink
    // holds its image with no power, there is no backlight and no host idle
    // timer, and the firmware's own auto-sleep is the Time to Sleep row three
    // lines up. Shown on device, "Keep Screen Awake" reads as "disable
    // auto-sleep" and silently does nothing — a lying control, which is worse
    // than an absent one.
    //
    // This is a gate on a NEW row, not the withdrawal of an existing one: no
    // device owner can see or select this today, so nothing users rely on is
    // being hidden. Capability-gating rows in this list is the established
    // pattern — STR_TOUCH_READER_CONTROLS is filtered by BoardConfig::hasTouch()
    // just below. That one is a runtime check because one C3 binary serves both
    // X4 and X3; SIMULATOR is a genuine compile-time split between separate
    // binaries, so the preprocessor is the right tool and the device build pays
    // nothing for it.
    //
    // Consequence to know about: toJson/fromJson iterate this list, so
    // "keepScreenAwake" is absent from settings.json on device builds. A
    // settings.json written by the iOS app and then loaded and resaved by device
    // firmware drops the key. Acceptable — the device cannot act on it.
#ifdef SIMULATOR
    v.push_back(SettingInfo::Toggle(StrId::STR_KEEP_SCREEN_AWAKE, &CrossPointSettings::keepScreenAwake,
                                    "keepScreenAwake", StrId::STR_CAT_SYSTEM));
#endif

    // Clock entries. Kept after the status bar was removed because the calendar
    // sleep screen shifts the RTC's UTC date by clockUtcOffsetQ
    // (SleepActivity.cpp) and WifiSelectionActivity drives the NTP re-sync.
    // Range 0..104 = quarter-hour steps from UTC-12:00 to UTC+14:00, biased by 48.
    //
    // Declared exactly as upstream declares it, so the JSON key, the range clamp
    // and the web settings control are identical. Upstream files it under its
    // "Customise Status Bar" category, which its device UI never shows; with the
    // status bar screen gone this fork files it under System. On device the row
    // is not edited in place: SettingsActivity intercepts it and opens
    // ClockOffsetActivity, and renders the stored byte through formatUtcOffset()
    // so the row reads "UTC+5:45" rather than "141".
    v.push_back(SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                                   "clockUtcOffsetQ", StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                                  {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                                  StrId::STR_CAT_SYSTEM));
    // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
    // on next WiFi connect, which is useful when crossing time zones.
    v.push_back(SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced,
                                    "clockHasBeenSynced", StrId::STR_CAT_SYSTEM));
    return v;
  }();

  std::vector<SettingInfo> v = baseList;
  if (!BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) { return s.nameId == StrId::STR_TOUCH_READER_CONTROLS; }),
            v.end());
  }
  if (BoardConfig::hasTouch()) {
    v.erase(std::remove_if(v.begin(), v.end(),
                           [](const SettingInfo& s) {
                             return s.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
                                    s.nameId == StrId::STR_SUNLIGHT_FADING_FIX;
                           }),
            v.end());
  }
  if (registry && registry->getFamilyCount() > 0) {
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_FAMILY; });
    if (it != v.end()) {
      *it = buildFontFamilySetting(registry);
    }
  }
  {
    // Unconditional: even with no SD fonts installed the sizes come from the
    // built-in family rather than a fixed Small/Medium/Large/XL enum.
    auto it = std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.nameId == StrId::STR_FONT_SIZE; });
    if (it != v.end()) {
      *it = buildFontSizeSetting(registry);
    }
  }
  return v;
}
