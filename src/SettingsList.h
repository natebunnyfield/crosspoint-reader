#pragma once

#include <BoardConfig.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
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
// CrossPoint ships) get XXS/XS/S/M/L/XL names. A family with a different count — a
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
  std::vector<std::string> labels;
  labels.reserve(sizes.size());
  for (size_t i = 0; i < sizes.size(); i++) {
    labels.push_back(readerSlotLabel(sizes, static_cast<uint8_t>(i)));
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

// Screen Margin's picker row was REMOVED 2026-08-22 (owner layout-exactness
// order; research in crosspoint-simulator/docs/zen-page-margins.md). The
// `screenMargin` FIELD stays: it persists (toJson/fromJson by hand in
// CrossPointSettings.cpp) and the reader still adds it to its oriented
// margins, so the value is card-controlled — the device's card keeps whatever
// it holds while the iOS harness pins its own card to 5 at boot. With no row
// here the web settings API drops it too (the loop walks getAllSettings and
// POST skips unknown keys).

// Editor font SIZE, as a drop-down over editorfonts::SIZES.
//
// The STORED value stays the size in POINTS, never the picker's index — the
// same rule the retired Screen Margin picker followed, and for a sharper reason
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
    sleepScreenValues[CrossPointSettings::CALENDAR_DARK] = StrId::STR_SLEEP_CALENDAR_DARK;
    sleepScreenValues[CrossPointSettings::CALENDAR_WESTSIDE_DARK] = StrId::STR_SLEEP_WESTSIDE_DARK;

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
    // 14 since 2026-08-24: the two ligature rows below, plus Line Spacing,
    // whose row came back with the Typography screen. They are entries in
    // this list purely for PERSISTENCE and the web settings API -- the device
    // edits them on the Typography screen, which builds its own rows because
    // the per-pair ones depend on which family is loaded.
    // 15 since 2026-08-25: Line Breaks. It arrived without bumping this, which
    // is the drift in the other direction from the one described above -- an
    // UNDER-reserve is just as invisible as an over-reserve, it costs one
    // realloc and a full move of the vector on the first call and nothing
    // announces it. NOTHING ENFORCES THIS NUMBER: it is not derivable at
    // compile time, no host test builds this list, and the loop below is the
    // cheapest thing that turns a drift into something a log will say. Whoever
    // adds the sixteenth unconditional push_back moves it.
    constexpr size_t FIXED_ENTRY_COUNT = 15;
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
      // Reverse chronological by lineage, exactly like the Reader Font screen's reading
      // list (owner ruling 2026-08-09). See editorfonts::displayOrder().
      s.withDisplayOrder(editorfonts::displayOrder());
      v.push_back(std::move(s));
    }
    // Directly after Editor Font: the two are one decision split in two rows,
    // and the size is meaningless without knowing which face it applies to.
    v.push_back(buildEditorFontSizeSetting());
    v.push_back(SettingInfo::Toggle(StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled,
                                    "focusReadingEnabled", StrId::STR_CAT_READER));
    // NO Text Alignment row. It shipped 2026-08-22 (Justified / Ragged right)
    // and is withdrawn 2026-08-23 — owner ruling: "remove ragged right or
    // justified ios app settings, instead make it automatic by letting the
    // character length decide what is optimal." The decision is now made per
    // BLOCK, where the measure is known, by ParsedText::layoutAndExtractLines
    // against autojustify::THRESHOLD_CHARS. CrossPointSettings::
    // paragraphAlignment is `static constexpr` JUSTIFIED, so the row cannot be
    // reinstated by accident: &CrossPointSettings::paragraphAlignment no longer
    // forms. STR_TEXT_ALIGNMENT / STR_ALIGN_JUSTIFIED / STR_ALIGN_RAGGED_RIGHT
    // stay in the string table — an unused string costs a few bytes of flash,
    // while deleting them renumbers StrId for every translation.
    //
    // THE TYPOGRAPHY BLOCK. Owner ruling 2026-08-24: "put or move line grid,
    // line spacing, letter spacing, justified text to Typography Settings."
    //
    // All three carry STR_CAT_READER, which is a WITHDRAWN category that
    // rebuildSettingsLists() drops -- so they leave the device's flat Settings
    // list while this list goes on persisting them (toJson/fromJson iterate it)
    // and the web settings API goes on serving them. That is the whole move:
    // the rows are not redefined on the Typography screen, they are SELECTED
    // out of this list by nameId (see TypographySettingsActivity::rebuildRows),
    // so each row still has exactly one definition -- one label, one key, one
    // accessor -- and moving it between screens cannot change what it stores.
    //
    // LETTER SPACING IS DELIBERATELY ABSENT from that ruling's four. There is
    // no letterSpacing field and no tracking anywhere in the layout engine, so
    // it is a new feature (a per-glyph advance adjustment, therefore
    // re-pagination and a section-cache bump), not a move. A row for a setting
    // with no renderer behind it is worse than no row; it is being scoped
    // separately.
    //
    // Line Grid (2026-08-22, default off): every vertical advance rounds UP to
    // a whole line-height so all baselines share one grid. Part of
    // ReaderRenderSpec — flipping it repaginates on the next section load.
    v.push_back(SettingInfo::Toggle(StrId::STR_LINE_GRID, &CrossPointSettings::lineGridEnabled, "lineGrid",
                                    StrId::STR_CAT_READER));
    // LINE BREAKS (owner ruling 2026-08-25: "unfreeze hyphenation and get the
    // better line breaker"). This is the `hyphenationEnabled` field, unfrozen.
    // The row is NOT called Hyphenation because that name describes a
    // side-effect and hides the switch: the flag picks between two different
    // line-breaking algorithms, and one of them had never run in a shipped
    // build. See lib/Epub/Epub/LineBreakMode.h.
    //
    //   Hyphenated   first-fit greedy that splits words at legal hyphenation
    //                points. Lines fill to the measure. THE SHIPPED DEFAULT,
    //                and what every build since the 2026-08-21 freeze has
    //                drawn.
    //   Whole Words  the total-fit dynamic program, minimizing the sum of
    //                squared trailing slack across the paragraph. No split
    //                words -- except one too wide for a line on its own, which
    //                still breaks rather than run off the glass.
    //
    // The second label is "Whole Words" and not "Even Spacing" BECAUSE THE PAGE
    // WAS MEASURED. The survey predicted the optimizer would set more evenly;
    // over 394 paragraphs at six measure/size pairs the opposite held on every
    // justified one, since the DP may not use the hyphen points that let the
    // greedy fill fit. Whole words is the claim that survives. The trade the
    // row actually sells is hyphens against no hyphens -- 489 hyphenated lines
    // against 33 in the X3's own 512 px, 12 pt configuration.
    // (test/line_break_quality, docs/line-breaking-2026-08-25.md.)
    //
    // ENUM rather than Toggle, and the LABELS are indexed BY STORED VALUE --
    // linebreak::STORED_WHOLE_WORDS is 0 and STORED_HYPHENATED is 1, which is
    // what a settings.json written before the freeze already carries under this
    // exact key. A Toggle would have rendered "On/Off" against a name that is
    // not a question, and re-pointing the two values to read better would have
    // silently restyled every book on a device that still has an old save.
    // withDisplayOrder puts the default FIRST in the picker without touching
    // what either choice stores.
    //
    // No book can override this and none tries: CssParser has no branch for
    // `hyphens`, `orphans` or `widows` (CssParser.cpp:467-569 is the whole
    // property list), so a stylesheet's opinion about line breaking never
    // survives parsing. The 2026-08-25 override ruling -- a typography setting
    // is a default a book's own CSS beats where the book was explicit -- has
    // nothing to bite on here, and there is no stored per-book value to honor.
    {
      std::vector<StrId> breakLabels(2);
      breakLabels[linebreak::STORED_WHOLE_WORDS] = StrId::STR_LINE_BREAKS_WHOLE_WORDS;
      breakLabels[linebreak::STORED_HYPHENATED] = StrId::STR_LINE_BREAKS_HYPHENATED;
      v.push_back(SettingInfo::Enum(StrId::STR_LINE_BREAKS, &CrossPointSettings::hyphenationEnabled,
                                    std::move(breakLabels), "hyphenationEnabled", StrId::STR_CAT_READER)
                      .withDisplayOrder({linebreak::STORED_HYPHENATED, linebreak::STORED_WHOLE_WORDS}));
    }
    // Line Spacing. Its row was deleted by the 2026-08-21 reduction and is
    // REINSTATED here by the 2026-08-24 ruling above -- which supersedes that
    // one FOR THIS FIELD ONLY. The field itself never went: it is the one entry
    // in CrossPointSettings' reading-taste block that stayed non-constexpr,
    // because the reader has a designed chord (Confirm held + a side button)
    // that steps it on-device, and deleting a live gesture's backing value
    // would have been silent capability removal.
    //
    // The labels are indexed BY ENUM VALUE, not pushed in list order, so a
    // reordered LINE_COMPRESSION can never silently re-point a saved
    // settings.json at a different spacing.
    {
      std::vector<StrId> spacingLabels(CrossPointSettings::LINE_COMPRESSION_COUNT);
      spacingLabels[CrossPointSettings::TIGHT] = StrId::STR_TIGHT;
      spacingLabels[CrossPointSettings::NORMAL] = StrId::STR_NORMAL;
      spacingLabels[CrossPointSettings::WIDE] = StrId::STR_WIDE;
      v.push_back(SettingInfo::Enum(StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing, std::move(spacingLabels),
                                    "lineSpacing", StrId::STR_CAT_READER));
    }
    // Justified Text (owner ruling 2026-08-24: "make justified or ragged right
    // character count an ios app setting"). The DECISION stays automatic -- the
    // measure still decides, per block -- and this row sets the character count
    // it decides against. It is the threshold that is settable, not the
    // alignment; there is still no Justified/Ragged row and there will not be.
    //
    // Why it is here rather than in the iOS Settings.app bundle: the threshold
    // moves line BREAKS, so it has to reach ReaderRenderSpec for the section
    // cache to notice it, and that struct is built from CrossPointSettings. A
    // value living in NSUserDefaults would have been compared against nothing,
    // so every already-paginated book would have kept its old breaks with a
    // header that matched. Being here also means the X3 and X4 get the control,
    // not only a phone, and the web settings API serves it for free.
    //
    // STR_CAT_READER, and that is not the same thing as invisible. This said
    // STR_CAT_SYSTEM "for the reason Line Grid above and Dark Mode below carry
    // it" until 2026-08-25, and both halves of that had stopped being true: the
    // row below carries STR_CAT_READER, and so does Line Grid. The comment
    // survived the commit that moved them (f5287c630, owner: "put or move line
    // grid, line spacing, letter spacing, justified text to Typography
    // Settings"), where the move was literally one word each,
    // STR_CAT_SYSTEM -> STR_CAT_READER. Left standing it gave a confident
    // reason to change them back, which would have silently reversed the
    // ruling.
    //
    // What the category decides is only whether rebuildSettingsLists() keeps
    // the row on the MAIN Settings list; it does drop STR_CAT_READER. The
    // Typography screen does not consult it at all -- TypographySettingsActivity
    // SELECTS rows out of getSettingsList() by nameId -- so this row is on
    // Typography, persists through toJson/fromJson, and serves the web settings
    // API exactly as before. Dark Mode below is still STR_CAT_SYSTEM because it
    // is on no subpage and the main list is the only place it can live.
    //
    // DynamicEnum, not Enum, so the stored byte is the character COUNT and not
    // this list's index. Same shape as screenMargin, and for the same payoff: a
    // rung can be inserted in autojustify::THRESHOLD_CHOICES without migrating
    // a single settings.json. The cost is that the generic toJson/fromJson loop
    // skips getter/setter rows, so this key is persisted BY HAND in
    // CrossPointSettings.cpp -- forgetting that half is what silently reset
    // screenMargin on every boot.
    {
      std::vector<StrId> justifyLabels;
      justifyLabels.reserve(autojustify::THRESHOLD_CHOICE_COUNT);
      for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
        switch (autojustify::THRESHOLD_CHOICES[i]) {
          case 32:
            justifyLabels.push_back(StrId::STR_JUSTIFY_ALMOST_ALWAYS);
            break;
          case 36:
            justifyLabels.push_back(StrId::STR_JUSTIFY_MORE_OFTEN);
            break;
          case 40:
            justifyLabels.push_back(StrId::STR_JUSTIFY_BALANCED);
            break;
          case 45:
            justifyLabels.push_back(StrId::STR_JUSTIFY_LESS_OFTEN);
            break;
          default:
            justifyLabels.push_back(StrId::STR_JUSTIFY_WIDE_ONLY);
            break;
        }
      }
      v.push_back(SettingInfo::DynamicEnum(
          StrId::STR_JUSTIFY_THRESHOLD, std::move(justifyLabels),
          []() -> uint8_t {
            // Nearest is NOT wanted here, unlike the editor font size: a byte
            // off the ladder means a value nobody chose, and clampThreshold
            // sends it to the documented default instead of a neighbour.
            const int live = autojustify::clampThreshold(SETTINGS.justifyThresholdChars);
            for (int i = 0; i < autojustify::THRESHOLD_CHOICE_COUNT; i++) {
              if (autojustify::THRESHOLD_CHOICES[i] == live) return static_cast<uint8_t>(i);
            }
            return 0;
          },
          [](const uint8_t index) {
            if (index >= static_cast<uint8_t>(autojustify::THRESHOLD_CHOICE_COUNT)) return;
            SETTINGS.justifyThresholdChars = static_cast<uint8_t>(autojustify::THRESHOLD_CHOICES[index]);
          },
          "justifyThreshold", StrId::STR_CAT_READER));
    }
    // LIGATURES (owner ruling 2026-08-24: "give a full subpage of Typography
    // Settings that gives all available typography options with full
    // granularity, including toggling each individual ligature"). Two rows,
    // and BOTH carry STR_CAT_READER on purpose.
    //
    // Reader is a withdrawn category, so rebuildSettingsLists() drops these
    // from the main device Settings list -- which is what is wanted here. This
    // said it was "the opposite of the reason Line Grid and Justified Text
    // above carry STR_CAT_SYSTEM" until 2026-08-25; those two rows carry
    // STR_CAT_READER, and have since f5287c630 moved them to this same screen,
    // so it is the SAME reason and not the opposite one. The device edits
    // ligatures on the TYPOGRAPHY screen
    // (SettingAction::Typography), and it has to: the individual rows are one
    // per pair the CURRENTLY LOADED family carries, so they cannot be a fixed
    // list here at all. Edgar ships fourteen pairs across its styles and
    // Almendra four, and neither set is knowable when this static list is
    // built.
    //
    // Being here anyway is not decoration. This list is what
    // CrossPointSettings::toJson/fromJson iterate and what the web settings
    // API serves, so a row deleted from it is a setting that stops persisting
    // -- the trap documented at the head of this file and paid for twice. The
    // generic loop handles both of these without another hand-written key:
    // valuePtr for the toggle, stringOffset for the spec.
    v.push_back(SettingInfo::Toggle(StrId::STR_LIGATURES, &CrossPointSettings::ligaturesEnabled, "ligatures",
                                    StrId::STR_CAT_READER));
    v.push_back(SettingInfo::String(StrId::STR_LIGATURES_OFF, SETTINGS.ligaturesOff, sizeof(SETTINGS.ligaturesOff),
                                    "ligaturesOff", StrId::STR_CAT_READER));

    // Which installed families are switched OFF (src/FontActivation.h). Here
    // for the same reason ligaturesOff is: this list is what toJson/fromJson
    // iterate, so a field with no row here is a field that stops persisting.
    // There is no device UI row -- the toggle is a long hold in the font
    // pickers -- but the web settings API serves it, which is also the only
    // way to clear the whole set at once.
    v.push_back(SettingInfo::String(StrId::STR_FONTS_OFF, SETTINGS.fontsOff, sizeof(SETTINGS.fontsOff), "fontsOff",
                                    StrId::STR_CAT_READER));
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
    // above in the Reader section so they sort to their natural position. Reader
    // Font — the device-only action — is inserted above the whole shared
    // list by SettingsActivity::rebuildSettingsLists(). Screen Margin's row was
    // removed 2026-08-22; see the note above buildEditorFontSizeSetting().
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
            // CALENDAR_FOUR/FIVE/SIX withdrawn from the picker (owner ruling
            // 2026-08-21: "keep calendar and westside calendar"). FIVE was a
            // literal duplicate of CALENDAR -- both draw the 5-week Spanish/CR
            // layout (SleepActivity.cpp). The enum values stay, because they
            // are the persisted encoding; stale saves are remapped to CALENDAR
            // in normalizeRetiredSettings(), and their labels remain a decode
            // surface exactly like systemFont's did.
            // Each calendar sits next to its own dark rendition, and the four
            // stay one contiguous block at the end.
            .withDisplaySubset({CrossPointSettings::BLANK, CrossPointSettings::DARK, CrossPointSettings::LIGHT,
                                CrossPointSettings::CUSTOM, CrossPointSettings::COVER, CrossPointSettings::COVER_CUSTOM,
                                CrossPointSettings::QUICK_RESUME, CrossPointSettings::CALENDAR,
                                CrossPointSettings::CALENDAR_DARK, CrossPointSettings::CALENDAR_WESTSIDE,
                                CrossPointSettings::CALENDAR_WESTSIDE_DARK}));

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

    // The gate for FIXED_ENTRY_COUNT above. Runs ONCE, on the first call, and
    // says nothing when the count is right. It deliberately does not assert:
    // a reserve that is one short costs a realloc, and aborting a reader over
    // that would be far worse than the drift.
    if (v.size() != FIXED_ENTRY_COUNT) {
      LOG_ERR("SET", "FIXED_ENTRY_COUNT is %zu but the fixed block pushes %zu; update it", FIXED_ENTRY_COUNT, v.size());
    }
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
