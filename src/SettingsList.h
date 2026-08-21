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
#include "notes/EditorFonts.h"  // Editor font group (owner ruling 2026-08-05)

// Build the reader font-family picker dynamically. getSettingsList() swaps this
// in only when a registry with at least one family is present (see below), and
// the result lists ONLY those SD card families — the built-in Libre Franklin
// entry is not appended alongside them.
//
// The built-in Libre Franklin face is hidden from the reader font picker whenever
// SD card fonts are installed, so the list shows only the user's own curated set.
//
// It is NOT removed from the firmware: it remains the reader's built-in face, and
// CrossPointSettings::getReaderFontId() still falls back to Libre Franklin when a
// selected SD font cannot be resolved (card pulled, .cpfont deleted). Hiding it
// is therefore a menu-only change with a working safety net behind it.
//
// When NO SD fonts are installed this function is not used at all — see
// getSettingsList() below, which keeps the single built-in entry (Libre Franklin)
// so the picker can never be empty and leave the reader with nothing to render
// text in.
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

// Editor font SIZE, as a drop-down over editorfonts::SIZES.
//
// The STORED value stays the size in POINTS, never the picker's index — the
// same rule buildScreenMarginSetting() above follows, and for a sharper reason
// here: the editor font FAMILY was rescued from index persistence hours before
// this row was written (see src/notes/EditorFonts.h), and putting the size on
// an index would have reintroduced the identical failure one field over. A
// stored 12 must mean twelve points forever, whatever the list looks like
// later.
//
// valuePtr is deliberately left null. CrossPointWebServer's ENUM case prefers
// valuePtr over valueSetter when both are set, and would write the raw index
// into the byte.
//
// WITHDRAWN from the device Settings UI on 2026-08-18 (owner ruling): the size
// is adjusted on the Editor Font screen itself, with the side buttons, next to
// the specimen it changes — see EditorFontSelectionActivity::changeFontSize.
// Two places to set one value is one too many, so this row MOVED rather than
// being duplicated.
//
// The entry is NOT deleted, and that distinction is the whole trap: this list
// is also what CrossPointSettings' fromJson/toJson iterate and what the web
// settings API serves, so removing the row would drop "editorFontSize" from the
// HTTP API. Instead the CATEGORY moves off STR_CAT_SYSTEM —
// rebuildSettingsLists() keeps STR_CAT_SYSTEM rows and drops the rest — which
// is the same mechanism the Controls rows and systemFont already use. Category
// is not persisted, so no saved settings.json is affected.
//
// STR_CAT_READER of the three withdrawn categories: it is a text setting, and
// that is where the web UI's grouping should file it.
inline SettingInfo buildEditorFontSizeSetting() {
  // Bare numbers, like the screen-margin ramp: a "pt" suffix would need
  // translating and the row title already says what it is.
  std::vector<std::string> labels;
  labels.reserve(editorfonts::SIZE_COUNT);
  for (size_t i = 0; i < editorfonts::SIZE_COUNT; i++) {
    labels.push_back(std::to_string(editorfonts::SIZES[i]));
  }

  SettingInfo s;
  s.nameId = StrId::STR_EDITOR_FONT_SIZE;
  s.type = SettingType::ENUM;
  s.enumStringValues = std::move(labels);
  s.key = "editorFontSize";
  s.category = StrId::STR_CAT_READER;

  s.valueGetter = []() -> uint8_t {
    // Nearest, not exact — a byte posted by an API client, or written when the
    // offered list differed, must not silently reset the row to the first size.
    // nearestOfferedSize() owns that rule so the picker and the renderer cannot
    // disagree about which size a stored byte means.
    const uint8_t snapped = editorfonts::nearestOfferedSize(SETTINGS.editorFontSize);
    for (size_t i = 0; i < editorfonts::SIZE_COUNT; i++) {
      if (editorfonts::SIZES[i] == snapped) return static_cast<uint8_t>(i);
    }
    return 0;
  };

  s.valueSetter = [](const uint8_t v) {
    if (v >= editorfonts::SIZE_COUNT) return;
    SETTINGS.editorFontSize = editorfonts::SIZES[v];
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
    // 12 rows after the 2026-08-21 settings reduction ("yes to all",
    // docs/settings-reduction-plan.md): 23 rows deleted, their fields now
    // static constexpr in CrossPointSettings.h. The SIMULATOR split went with
    // keepScreenAwake's row.
    constexpr size_t FIXED_ENTRY_COUNT = 10;
    std::vector<SettingInfo> v;
    v.reserve(FIXED_ENTRY_COUNT);

    // --- Display ---
            
    // --- Reader ---
    // Built-in font-family entry. Replaced per-call with a registry-aware
    // version when SD fonts are installed. Libre Franklin is the only built-in
    // reading family, so with no SD fonts this is a one-entry list — kept so
    // the picker can never be empty and the reader always has a face to name.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_FAMILY, &CrossPointSettings::fontFamily, {StrId::STR_LIBRE_FRANKLIN},
                                  "fontFamily", StrId::STR_CAT_READER));
    // Placeholder: the selectable sizes depend on the active font family, so
    // this entry is always replaced by buildFontSizeSetting() below. It only
    // fixes the setting's position in the Reader category.
    v.push_back(SettingInfo::Enum(StrId::STR_FONT_SIZE, nullptr, {}, "fontSize", StrId::STR_CAT_READER));

    // EDITOR font group (owner ruling 2026-08-05; cut to three faces
    // 2026-08-15). Its own list, separate from the reading families above; see
    // src/notes/EditorFonts.h. Plain ENUM with valuePtr, so the row keeps
    // working over the web settings API and the byte keeps being written — but
    // the byte is NO LONGER what the setting is restored from. fromJson()
    // reads the family NAME out of "editorFontFamily" and ignores this byte
    // whenever that key is present, which is what lets the list be edited
    // without a migration. Do not "simplify" this back to index-only.
    {
      SettingInfo s;
      s.nameId = StrId::STR_EDITOR_FONT;
      s.type = SettingType::ENUM;
      s.key = "editorFont";
      // STR_CAT_SYSTEM, not STR_CAT_READER. The Reader category is WITHDRAWN
      // from the device UI — rebuildSettingsLists() keeps STR_CAT_SYSTEM rows
      // and drops the rest — so this row existed but was unreachable on an X4
      // or X3, served only by the web settings API, which does not filter by
      // category. It is not a reading setting either: it picks the writing
      // face for Create Note and Claude.
      s.category = StrId::STR_CAT_SYSTEM;
      s.valuePtr = &CrossPointSettings::editorFont;
      s.enumStringValues.reserve(editorfonts::FAMILY_COUNT);
      for (size_t i = 0; i < editorfonts::FAMILY_COUNT; i++) {
        s.enumStringValues.emplace_back(editorfonts::FAMILIES[i].label);
      }
      // Reverse chronological by lineage, exactly like Text Settings' reading
      // list (owner ruling 2026-08-09). See editorfonts::displayOrder().
      s.withDisplayOrder(editorfonts::displayOrder());
      v.push_back(std::move(s));
    }
    // Directly after Editor Font: the two are one decision split in two rows,
    // and the size is meaningless without knowing which face it applies to.
    v.push_back(buildEditorFontSizeSetting());
    v.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                    "focusReadingEnabled", StrId::STR_CAT_READER));
        // The reader is portrait-only; there is no orientation setting.
        // Values follow CrossPointSettings::TEXT_ANTIALIASING: 0/1 are the
    // legacy Off/On toggle (persisted files round-trip), 2+ appended.
        
    // --- Controls ---
    // Category is STR_CAT_CONTROLS (not STR_CAT_SYSTEM), so rebuildSettingsLists()
    // drops these rows from the device UI — Controls is a withdrawn tab. They
    // stay in getSettingsList() for persistence (fromJson/toJson) and the web
    // settings API. Category is not persisted, so this change has no impact on
    // saved settings.json files.
            // Third label = stored value 2 = FONT_SIZE_STEP. The order of this array IS
    // the persisted encoding, so append only — see LONG_PRESS_BUTTON_BEHAVIOR in
    // CrossPointSettings.h for why the retired ORIENTATION_CHANGE slot was reused
    // rather than a fourth value added.
                
    // --- System ---
    // The System block is contributed to the device list in the order below.
    // Typing Redraw Delay and Editor Font (both STR_CAT_SYSTEM) already appear
    // above in the Reader section so they sort to their natural position; Screen
    // Margin follows them. Text Settings — the device-only action — is inserted
    // above the whole shared list by SettingsActivity::rebuildSettingsLists().
    v.push_back(buildScreenMarginSetting());
    // Whole-screen polarity. A TOGGLE rather than a Light/Dark ENUM because the
    // decision is boolean and docs/ui-conventions.md files boolean preferences
    // as toggle rows; a two-option enum would open a popup to show one
    // alternative. STR_CAT_SYSTEM, not STR_CAT_DISPLAY — Display is a retired
    // category that rebuildSettingsLists() drops, so the row would persist and
    // serve the web API but be invisible on the device, which is the whole
    // point of adding it. SettingsActivity pushes the new value into the
    // display driver as soon as it flips.
    v.push_back(
        SettingInfo::Toggle(StrId::STR_DARK_MODE, &CrossPointSettings::darkMode, "darkMode", StrId::STR_CAT_SYSTEM));
    // The typeface the chrome itself is drawn in. Filed under System rather than
    // Reader because it is not about books: it changes headers, list rows,
    // button hints, popups and the battery readout, and leaves the reader's body
    // face entirely alone. Ubuntu first, because it is the value every existing
    // settings.json already holds by omission.
    // Order must match CrossPointSettings::SYSTEM_FONT -- the index IS the
    // persisted value.
    // WITHDRAWN from the device Settings UI on 2026-08-07 (owner ruling): Libre
    // Franklin is the only System font now. STR_CAT_DISPLAY is a retired
    // category, so rebuildSettingsLists() drops this row while getSettingsList()
    // still carries it — deleting the entry instead would stop systemFont
    // persisting at all and remove it from the web settings API, which is the
    // trap documented in CLAUDE.md. The value is pinned in
    // normalizeRetiredSettings() and the field initialiser already matches, so
    // fresh and upgraded devices agree.
    //
    // The four labels stay listed because the index IS the persisted value: a
    // settings.json holding 0..2 must still decode for the web API until
    // normalizeRetiredSettings() pins it to 3 on the next load. The Ubuntu and
    // Noto font DATA is gone — applySystemFont() binds Libre Franklin
    // regardless of the stored byte — so the labels are a decode surface, not
    // an offer.
        // Which text-entry keyboard every entry field opens (searches, WiFi
    // passwords, owner name, renames). Order must match
    // CrossPointSettings::KEYBOARD_LAYOUT -- the index IS the persisted value.
    v.push_back(SettingInfo::Enum(StrId::STR_KEYBOARD, &CrossPointSettings::keyboardLayout,
                                  {StrId::STR_KEYBOARD_DAISY, StrId::STR_KEYBOARD_GRID13, StrId::STR_KEYBOARD_QWERTY},
                                  "keyboard", StrId::STR_CAT_SYSTEM));
        // Filed under System, next to Time to Sleep, because that is the row it
    // qualifies: it decides what an inactivity-timeout sleep DRAWS, and while it
    // is ON the Sleep Screen setting is bypassed entirely on that path
    // (SleepActivity::onEnter checks it before the sleepScreen switch). It was
    // originally under Display, which the device UI withdrew, so the only
    // control over the dominant sleep path lived on the web UI. It is also no
    // longer pinned in normalizeRetiredSettings() — a visible row that a reload
    // silently reverts is worse than no row.
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
    // "None" first (it is the off switch for this whole row), then the two
    // plain screens, the custom image, the two cover modes, Quick Resume, and
    // the calendars last as a block. The enum values are frozen by persistence
    // -- BLANK is 5 and stays 5 -- so this reorders only what the picker draws.
    v.push_back(
        SettingInfo::Enum(StrId::STR_SLEEP_SCREEN, &CrossPointSettings::sleepScreen, std::move(sleepScreenValues),
                          "sleepScreen", StrId::STR_CAT_SYSTEM)
            .withDisplayOrder({CrossPointSettings::BLANK, CrossPointSettings::DARK, CrossPointSettings::LIGHT,
                               CrossPointSettings::CUSTOM, CrossPointSettings::COVER, CrossPointSettings::COVER_CUSTOM,
                               CrossPointSettings::QUICK_RESUME, CrossPointSettings::CALENDAR,
                               CrossPointSettings::CALENDAR_FOUR, CrossPointSettings::CALENDAR_FIVE,
                               CrossPointSettings::CALENDAR_SIX, CrossPointSettings::CALENDAR_WESTSIDE}));
        
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
    return v;
  }();

  std::vector<SettingInfo> v = baseList;
  // The per-board hasTouch() filters are gone with the rows they filtered:
  // touchReaderControls and fadingFix are hardcoded now (2026-08-21), so the
  // list is identical on every board.
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
