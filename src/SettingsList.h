#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <SdCardFontRegistry.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "ReaderFontSizes.h"
#include "activities/settings/SettingsActivity.h"
#include "util/DictionaryRegistry.h"

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
  s.inTextSettings = true;  // matches the static font-family entry it replaces

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
    strncpy(SETTINGS.sdFontFamilyName, sdFamilyNames[v].c_str(),
            sizeof(SETTINGS.sdFontFamilyName) - 1);
    SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  };

  return s;
}

// Build the font size setting dynamically: the options are the point sizes the
// active family actually ships, so an SD family built at 10/12/14 offers three
// sizes and a family built at 8..18 offers six. The selected point size persists
// in SETTINGS.fontPointSize (saved/loaded manually in CrossPointSettings::
// toJson/fromJson — the generic loop skips dynamic entries), while the ENUM
// contract shared with the web UI stays index-based.
inline SettingInfo buildFontSizeSetting(const SdCardFontRegistry* registry) {
  // Captured by copy: getSettingsList() returns by value and the lambdas outlive
  // this call, so they must not reference the registry.
  const std::vector<uint8_t> sizes = readerFontPointSizes(registry, SETTINGS.sdFontFamilyName);

  // "pt" is deliberately not translated: it is the typographic unit symbol,
  // written the same way in every language CrossPoint ships.
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (const uint8_t pt : sizes) {
    labels.push_back(std::to_string(pt) + " pt");
  }

  SettingInfo s;
  s.nameId = StrId::STR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "fontSize";
  s.category = StrId::STR_CAT_READER;
  s.inTextSettings = true;  // matches the static font-size entry it replaces

  s.valueGetter = [sizes]() -> uint8_t {
    const uint8_t pt = snapToNearestPointSize(sizes, SETTINGS.fontPointSize);
    for (int i = 0; i < static_cast<int>(sizes.size()); i++) {
      if (sizes[i] == pt) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [sizes](uint8_t v) {
    if (v < sizes.size()) SETTINGS.fontPointSize = sizes[v];
  };

  return s;
}

// Build the dictionary selection setting dynamically from the folders discovered
// under /dictionaries. "None" plus one option per dictionary; the selected folder
// name persists in SETTINGS.dictionaryName (saved/loaded manually in
// CrossPointSettings::toJson/fromJson — the generic loop skips dynamic entries).
inline SettingInfo buildDictionarySetting(const std::vector<DictionaryEntry>& dictionaries) {
  std::vector<std::string> folderNames;
  folderNames.reserve(dictionaries.size());
  std::transform(dictionaries.begin(), dictionaries.end(), std::back_inserter(folderNames),
                 [](const DictionaryEntry& d) { return d.name; });

  SettingInfo s;
  s.nameId = StrId::STR_DICTIONARY;
  s.type = SettingType::ENUM;
  s.enumStringValues.reserve(folderNames.size() + 1);
  s.enumStringValues.push_back(I18N.get(StrId::STR_NONE_OPT));
  s.enumStringValues.insert(s.enumStringValues.end(), folderNames.begin(), folderNames.end());
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = [folderNames]() -> uint8_t {
    for (size_t i = 0; i < folderNames.size(); i++) {
      // Compare within the settings field capacity: an over-long folder name is
      // stored truncated, and must still match its list entry.
      if (strncmp(folderNames[i].c_str(), SETTINGS.dictionaryName, sizeof(SETTINGS.dictionaryName) - 1) == 0) {
        return static_cast<uint8_t>(i + 1);
      }
    }
    return 0;  // "None", also when the stored folder no longer exists
  };

  s.valueSetter = [folderNames](uint8_t v) {
    if (v == 0 || v > folderNames.size()) {
      SETTINGS.dictionaryName[0] = '\0';
      return;
    }
    strncpy(SETTINGS.dictionaryName, folderNames[v - 1].c_str(), sizeof(SETTINGS.dictionaryName) - 1);
    SETTINGS.dictionaryName[sizeof(SETTINGS.dictionaryName) - 1] = '\0';
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
inline std::vector<SettingInfo> getSettingsList(const SdCardFontRegistry* registry = nullptr,
                                                const std::vector<DictionaryEntry>* dictionaries = nullptr) {
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

    std::vector<StrId> statusBarClockValues(CrossPointSettings::STATUS_BAR_CLOCK_MODE_COUNT);
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_HIDE] = StrId::STR_HIDE;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_RIGHT] = StrId::STR_DIR_RIGHT;
    statusBarClockValues[CrossPointSettings::STATUS_BAR_CLOCK_LEFT] = StrId::STR_DIR_LEFT;

    // Built one entry at a time instead of from a single braced initializer_list:
    // the whole list was materialised as one stack temporary (~50 x
    // sizeof(SettingInfo)), which made this frame the largest non-vendor frame in
    // the firmware measured with -fstack-usage on the riscv32 target — far over
    // the 256-byte Resource Protocol limit. push_back() keeps only one SettingInfo
    // on the stack at a time; the entries, their order and their values are
    // unchanged.
    //
    // Exact final capacity: the 50 fixed entries below plus the one conditional
    // tilt entry appended further down (X3 only), so neither the push_backs nor
    // that insert() reallocate. Keep in sync when adding an entry.
    constexpr size_t FIXED_ENTRY_COUNT = 50;
    std::vector<SettingInfo> v;
    v.reserve(FIXED_ENTRY_COUNT + (halTiltSensor.isAvailable() ? 1 : 0));

    // --- Display ---
    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen,
                                  std::move(sleepScreenValues), "sleepScreen", StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_MODE, &CrossPointSettings::sleepScreenCoverMode,
                                  {StrId::STR_FIT, StrId::STR_CROP}, "sleepScreenCoverMode", StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Enum(StrId::STR_SLEEP_COVER_FILTER, &CrossPointSettings::sleepScreenCoverFilter,
                                  {StrId::STR_NONE_OPT, StrId::STR_FILTER_CONTRAST, StrId::STR_INVERTED},
                                  "sleepScreenCoverFilter", StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Enum(StrId::STR_QUICK_RESUME_TIMEOUT, &CrossPointSettings::quickResumeSleepScreen,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "quickResumeSleepScreen",
                                  StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Enum(StrId::STR_HIDE_BATTERY, &CrossPointSettings::hideBatteryPercentage,
                                  {StrId::STR_NEVER, StrId::STR_IN_READER, StrId::STR_ALWAYS},
                                  "hideBatteryPercentage", StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Enum(StrId::STR_REFRESH_FREQ, &CrossPointSettings::refreshFrequency,
                                  {StrId::STR_PAGES_1, StrId::STR_PAGES_5, StrId::STR_PAGES_10, StrId::STR_PAGES_15,
                                   StrId::STR_PAGES_30},
                                  "refreshFrequency", StrId::STR_CAT_DISPLAY));
    // Order MUST match CrossPointSettings::UI_THEME — settings.json persists the
    // index into this list, so APPEND ONLY (see the enum's comment).
    v.push_back(SettingInfo::Enum(StrId::STR_UI_THEME, &CrossPointSettings::uiTheme,
                                  {StrId::STR_THEME_CLASSIC, StrId::STR_THEME_LYRA, StrId::STR_THEME_LYRA_EXTENDED,
                                   StrId::STR_THEME_ROUNDEDRAFF, StrId::STR_THEME_LYRA_SIX},
                                  "uiTheme", StrId::STR_CAT_DISPLAY));
    v.push_back(SettingInfo::Toggle(StrId::STR_SUNLIGHT_FADING_FIX, &CrossPointSettings::fadingFix, "fadingFix",
                                    StrId::STR_CAT_DISPLAY));

    // --- Reader ---
    // Built-in font-family entry. Replaced per-call with a registry-aware
    // version when SD fonts are installed.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily,
                                  {StrId::STR_NOTO_SERIF, StrId::STR_NOTO_SANS}, "fontFamily", StrId::STR_CAT_READER)
                    .withTextSettings());
    // Placeholder: the selectable sizes depend on the active font family, so
    // this entry is always replaced by buildFontSizeSetting() below. It only
    // fixes the setting's position in the Reader category.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER)
                    .withTextSettings());
    v.push_back(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing,
                                  {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE}, "lineSpacing",
                                  StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Value(StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin,
                                   {CrossPointSettings::SCREEN_MARGIN_MIN, CrossPointSettings::SCREEN_MARGIN_MAX,
                                    CrossPointSettings::SCREEN_MARGIN_STEP},
                                   "screenMargin", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Enum(StrId::STR_PARA_ALIGNMENT, &CrossPointSettings::paragraphAlignment,
                                  {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE},
                                  "paragraphAlignment", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Toggle(StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle, "embeddedStyle",
                                    StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                    "focusReadingEnabled", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Toggle(StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled,
                                    "hyphenationEnabled", StrId::STR_CAT_READER));
    // Reading orientation has no UI: the reader is portrait-only. The
    // `orientation` field is still read by ReaderUtils and the themes, and
    // is pinned to PORTRAIT on load by normalizeRetiredSettings().
    v.push_back(SettingInfo::Toggle(StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing,
                                    "extraParagraphSpacing", StrId::STR_CAT_READER));
    // Values follow CrossPointSettings::TEXT_ANTIALIASING: 0/1 are the
    // legacy Off/On toggle (persisted files round-trip), 2+ appended.
    v.push_back(SettingInfo::Enum(StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON, StrId::STR_AA_CRISP, StrId::STR_AA_DARK},
                                  "textAntiAliasing", StrId::STR_CAT_READER));
    v.push_back(SettingInfo::Enum(StrId::STR_IMAGES, &CrossPointSettings::imageRendering,
                                  {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS},
                                  "imageRendering", StrId::STR_CAT_READER));

    // --- Controls ---
    v.push_back(SettingInfo::Enum(StrId::STR_SIDE_BTN_LAYOUT, &CrossPointSettings::sideButtonLayout,
                                  {StrId::STR_PREV_NEXT, StrId::STR_NEXT_PREV, StrId::STR_DISABLED},
                                  "sideButtonLayout", StrId::STR_CAT_CONTROLS));
    v.push_back(SettingInfo::Enum(StrId::STR_TOUCH_READER_CONTROLS, &CrossPointSettings::touchReaderControls,
                                  {StrId::STR_STATE_OFF, StrId::STR_STATE_ON}, "touchReaderControls",
                                  StrId::STR_CAT_CONTROLS));
    v.push_back(SettingInfo::Toggle(StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION,
                                    &CrossPointSettings::frontButtonFollowOrientation,
                                    "frontButtonFollowOrientation", StrId::STR_CAT_CONTROLS));
    // Third label = stored value 2 = FONT_SIZE_STEP. The order of this array IS
    // the persisted encoding, so append only — see LONG_PRESS_BUTTON_BEHAVIOR in
    // CrossPointSettings.h for why the retired ORIENTATION_CHANGE slot was reused
    // rather than a fourth value added.
    v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_BEHAVIOR, &CrossPointSettings::longPressButtonBehavior,
                                  {StrId::STR_LONG_PRESS_BEHAVIOR_OFF, StrId::STR_LONG_PRESS_BEHAVIOR_SKIP,
                                   StrId::STR_LONG_PRESS_BEHAVIOR_FONT_SIZE},
                                  "longPressButtonBehavior", StrId::STR_CAT_CONTROLS));
    v.push_back(SettingInfo::Enum(StrId::STR_LONG_PRESS_MENU, &CrossPointSettings::longPressMenuFunction,
                                  {StrId::STR_KOSYNC, StrId::STR_DISABLED, StrId::STR_BOOKMARK_OPTION,
                                   StrId::STR_DICTIONARY},
                                  "longPressMenuFunction", StrId::STR_CAT_CONTROLS));
    v.push_back(SettingInfo::Enum(StrId::STR_SHORT_PWR_BTN, &CrossPointSettings::shortPwrBtn,
                                  {StrId::STR_IGNORE, StrId::STR_SLEEP, StrId::STR_PAGE_TURN, StrId::STR_FORCE_REFRESH,
                                   StrId::STR_FOOTNOTES},
                                  "shortPwrBtn", StrId::STR_CAT_CONTROLS));
    v.push_back(SettingInfo::Toggle(StrId::STR_PWR_BTN_FOOTNOTE_BACK, &CrossPointSettings::pwrBtnFootnoteBack,
                                    "pwrBtnFootnoteBack", StrId::STR_CAT_CONTROLS));
    v.push_back(SettingInfo::Toggle(StrId::STR_BACK_SHORT_TO_FILE_BROWSER, &CrossPointSettings::backShortToFileBrowser,
                                    "backShortToFileBrowser", StrId::STR_CAT_CONTROLS));

    // --- System ---
    v.push_back(SettingInfo::Value(StrId::STR_TIME_TO_SLEEP, &CrossPointSettings::sleepTimeoutMinutes,
                                   {CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES,
                                    CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1},
                                   "sleepTimeoutMinutes", StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Toggle(StrId::STR_SHOW_HIDDEN_FILES, &CrossPointSettings::showHiddenFiles,
                                    "showHiddenFiles", StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Toggle(StrId::STR_REMOVE_READ_FROM_RECENTS,
                                    &CrossPointSettings::removeReadBooksFromRecents, "removeReadBooksFromRecents",
                                    StrId::STR_CAT_SYSTEM));
    v.push_back(SettingInfo::Toggle(StrId::STR_MOVE_FINISHED_TO_READ, &CrossPointSettings::moveFinishedToReadFolder,
                                    "moveFinishedToReadFolder", StrId::STR_CAT_SYSTEM));

    // OPDS download folder: persisted + web-exposed, but category-less so it
    // is hidden from the on-device Settings screen (edited via OPDS UI).
    v.push_back(SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                                    sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"));
    // OPDS download filename format: persisted + web-exposed, category-less so it
    // is hidden from the on-device Settings screen (cycled from the OPDS UI).
    v.push_back(SettingInfo::Enum(StrId::STR_OPDS_FILENAME_FORMAT, &CrossPointSettings::opdsFilenameFormat,
                                  {StrId::STR_FMT_AUTHOR_TITLE, StrId::STR_FMT_TITLE_AUTHOR, StrId::STR_FMT_TITLE},
                                  "opdsFilenameFormat"));

    // --- KOReader Sync (web-only, uses KOReaderCredentialStore) ---
    v.push_back(SettingInfo::DynamicString(
        StrId::STR_KOREADER_USERNAME, [] { return KOREADER_STORE.getUsername(); },
        [](const std::string& val) {
          KOREADER_STORE.setCredentials(val, KOREADER_STORE.getPassword());
          KOREADER_STORE.saveToFile();
        },
        "koUsername", StrId::STR_KOREADER_SYNC));
    v.push_back(SettingInfo::DynamicString(
        StrId::STR_KOREADER_PASSWORD, [] { return KOREADER_STORE.getPassword(); },
        [](const std::string& val) {
          KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), val);
          KOREADER_STORE.saveToFile();
        },
        "koPassword", StrId::STR_KOREADER_SYNC));
    v.push_back(SettingInfo::DynamicString(
        StrId::STR_SYNC_SERVER_URL, [] { return KOREADER_STORE.getServerUrl(); },
        [](const std::string& val) {
          KOREADER_STORE.setServerUrl(val);
          KOREADER_STORE.saveToFile();
        },
        "koServerUrl", StrId::STR_KOREADER_SYNC));
    v.push_back(SettingInfo::DynamicEnum(
        StrId::STR_DOCUMENT_MATCHING, {StrId::STR_FILENAME, StrId::STR_BINARY},
        [] { return static_cast<uint8_t>(KOREADER_STORE.getMatchMethod()); },
        [](uint8_t val) {
          KOREADER_STORE.setMatchMethod(static_cast<DocumentMatchMethod>(val));
          KOREADER_STORE.saveToFile();
        },
        "koMatchMethod", StrId::STR_KOREADER_SYNC));
    v.push_back(SettingInfo::DynamicEnum(
        StrId::STR_SEND_METADATA, {StrId::STR_STATE_OFF, StrId::STR_STATE_ON},
        [] { return static_cast<uint8_t>(KOREADER_STORE.getSendMetadata()); },
        [](uint8_t val) {
          KOREADER_STORE.setSendMetadata(val != 0);
          KOREADER_STORE.saveToFile();
        },
        "koSendMetadata", StrId::STR_KOREADER_SYNC));
    v.push_back(SettingInfo::DynamicEnum(
        StrId::STR_SYNC_BEHAVIOR, {StrId::STR_ASK_EVERY_TIME, StrId::STR_SMART_SYNC},
        [] { return static_cast<uint8_t>(KOREADER_STORE.getSyncBehavior()); },
        [](uint8_t val) {
          KOREADER_STORE.setSyncBehavior(static_cast<KOReaderSyncBehavior>(val));
          KOREADER_STORE.saveToFile();
        },
        "koSyncBehavior", StrId::STR_KOREADER_SYNC));

    // --- Status Bar Settings (web-only, uses StatusBarSettingsActivity) ---
    v.push_back(SettingInfo::Toggle(StrId::STR_CHAPTER_PAGE_COUNT, &CrossPointSettings::statusBarChapterPageCount,
                                    "statusBarChapterPageCount", StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Toggle(StrId::STR_BOOK_PROGRESS_PERCENTAGE,
                                    &CrossPointSettings::statusBarBookProgressPercentage,
                                    "statusBarBookProgressPercentage", StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Enum(StrId::STR_PROGRESS_BAR, &CrossPointSettings::statusBarProgressBar,
                                  {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarProgressBar",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Enum(StrId::STR_PROGRESS_BAR_THICKNESS,
                                  &CrossPointSettings::statusBarProgressBarThickness,
                                  {StrId::STR_PROGRESS_BAR_THIN, StrId::STR_PROGRESS_BAR_MEDIUM,
                                   StrId::STR_PROGRESS_BAR_THICK},
                                  "statusBarProgressBarThickness", StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Enum(StrId::STR_TITLE, &CrossPointSettings::statusBarTitle,
                                  {StrId::STR_BOOK, StrId::STR_CHAPTER, StrId::STR_HIDE}, "statusBarTitle",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Toggle(StrId::STR_BATTERY, &CrossPointSettings::statusBarBattery, "statusBarBattery",
                                    StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Enum(StrId::STR_XTC_STATUS_BAR, &CrossPointSettings::xtcStatusBarMode,
                                  {StrId::STR_HIDE, StrId::STR_BOTTOM, StrId::STR_TOP}, "xtcStatusBarMode",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));
    // Clock entries (web settings only; device UI uses ClockOffsetActivity for the offset).
    // Range 0..104 = quarter-hour steps from UTC-12:00 to UTC+14:00, biased by 48.
    v.push_back(SettingInfo::Enum(StrId::STR_CLOCK, &CrossPointSettings::statusBarClock,
                                  std::move(statusBarClockValues), "statusBarClock", StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Value(StrId::STR_CLOCK_UTC_OFFSET, &CrossPointSettings::clockUtcOffsetQ, {0, 104, 1},
                                   "clockUtcOffsetQ", StrId::STR_CUSTOMISE_STATUS_BAR));
    v.push_back(SettingInfo::Enum(StrId::STR_CLOCK_FORMAT, &CrossPointSettings::clockFormat,
                                  {StrId::STR_CLOCK_FORMAT_24H, StrId::STR_CLOCK_FORMAT_12H}, "clockFormat",
                                  StrId::STR_CUSTOMISE_STATUS_BAR));
    // Persistence flag for NTP debounce. Resetting from the web UI forces a re-sync
    // on next WiFi connect, which is useful when crossing time zones.
    v.push_back(SettingInfo::Toggle(StrId::STR_CLOCK_SYNCED, &CrossPointSettings::clockHasBeenSynced,
                                    "clockHasBeenSynced", StrId::STR_CUSTOMISE_STATUS_BAR));
    // Only show tilt page turn setting when the QMI8658 IMU is present (X3)
    if (halTiltSensor.isAvailable()) {
      // Insert after the short power button setting (end of Controls section)
      for (auto it = v.begin(); it != v.end(); ++it) {
        if (it->nameId == StrId::STR_SHORT_PWR_BTN) {
          v.insert(it + 1, SettingInfo::Enum(StrId::STR_TILT_PAGE_TURN, &CrossPointSettings::tiltPageTurn,
                                             {StrId::STR_STATE_OFF, StrId::STR_NORMAL, StrId::STR_INVERTED},
                                             "tiltPageTurn", StrId::STR_CAT_CONTROLS));
          break;
        }
      }
    }
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
  if (dictionaries && !dictionaries->empty()) {
    // Insert at the end of the Reader category (just before the first Controls entry).
    auto it =
        std::find_if(v.begin(), v.end(), [](const SettingInfo& s) { return s.category == StrId::STR_CAT_CONTROLS; });
    v.insert(it, buildDictionarySetting(*dictionaries));
  }
  return v;
}
