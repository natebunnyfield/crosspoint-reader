#pragma once
#include <ArduinoJson.h>
#include <Epub/ReaderRenderSpec.h>
#include <PersistableStore.h>

#include <cstdint>

class CrossPointSettings : public PersistableStore<CrossPointSettings> {
 private:
  // Private constructor for singleton
  CrossPointSettings() = default;

  friend class PersistableStore<CrossPointSettings>;

 public:
  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    COVER_CUSTOM = 4,
    BLANK = 5,
    QUICK_RESUME = 6,
    // New enum values MUST be appended so persisted settings.json indices
    // stay stable.
    CALENDAR = 7,
    // Week-count variants of the calendar sleep screen ("Calendar Four/Five/
    // Six"). CALENDAR above predates them and keeps its original five-week
    // meaning so existing saves are untouched; these append after it.
    CALENDAR_FOUR = 8,
    CALENDAR_FIVE = 9,
    CALENDAR_SIX = 10,
    // Westside Community Schools (D66, Omaha) + US federal holidays, English.
    CALENDAR_WESTSIDE = 11,
    SLEEP_SCREEN_MODE_COUNT
  };
  enum SLEEP_SCREEN_COVER_MODE { FIT = 0, CROP = 1, SLEEP_SCREEN_COVER_MODE_COUNT };
  enum SLEEP_SCREEN_COVER_FILTER {
    NO_FILTER = 0,
    BLACK_AND_WHITE = 1,
    INVERTED_BLACK_AND_WHITE = 2,
    SLEEP_SCREEN_COVER_FILTER_COUNT
  };




  // Front button layout options (legacy)
  // Default: Back, Confirm, Left, Right
  // Swapped: Left, Right, Back, Confirm
  enum FRONT_BUTTON_LAYOUT {
    BACK_CONFIRM_LEFT_RIGHT = 0,
    LEFT_RIGHT_BACK_CONFIRM = 1,
    LEFT_BACK_CONFIRM_RIGHT = 2,
    BACK_CONFIRM_RIGHT_LEFT = 3,
    FRONT_BUTTON_LAYOUT_COUNT
  };

  // Front button hardware identifiers (for remapping)
  enum FRONT_BUTTON_HARDWARE {
    FRONT_HW_BACK = 0,
    FRONT_HW_CONFIRM = 1,
    FRONT_HW_LEFT = 2,
    FRONT_HW_RIGHT = 3,
    FRONT_BUTTON_HARDWARE_COUNT
  };

  // Side button layout options
  // Default: Up = Previous, Down = Next
  enum SIDE_BUTTON_LAYOUT { PREV_NEXT = 0, NEXT_PREV = 1, SIDE_BUTTONS_DISABLED = 2, SIDE_BUTTON_LAYOUT_COUNT };

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName)
  enum FONT_FAMILY { NOTOSERIF = 0, NOTOSANS = 1, FONT_FAMILY_COUNT };
  static constexpr uint8_t LEGACY_OPENDYSLEXIC = 2;
  static constexpr uint8_t BUILTIN_FONT_COUNT = FONT_FAMILY_COUNT;
  // Reader font size is a SLOT (S/M/L/XL) again — see fontSizeSlot. 1.5 stored an
  // absolute point size, which broke every family switch because families ship
  // harmonized-but-different ramps; that is what "fontSize" still holds on disk.
  // 1.4-and-earlier ALSO stored a 0..3 slot there, so a value in 0..3 read from
  // "fontSize" is a 1.4 file and folds up via LEGACY_FONT_SIZE_MAX. The new
  // "fontSizeSlot" key exists precisely because 0..3 is ambiguous in the old one.
  static constexpr uint8_t LEGACY_FONT_SIZE_MAX = 3;
  static constexpr uint8_t DEFAULT_FONT_POINT_SIZE = 14;
  static constexpr uint8_t DEFAULT_FONT_SIZE_SLOT = 1;  // M, = 14pt on the built-in ramp
  enum LINE_COMPRESSION { TIGHT = 0, NORMAL = 1, WIDE = 2, LINE_COMPRESSION_COUNT };
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    BOOK_STYLE = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  // Auto-sleep timeout options (in minutes)
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_TIMEOUT_COUNT
  };

  // E-ink refresh frequency (pages between full refreshes)
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_FREQUENCY_COUNT
  };

  // Short power button press actions
  enum SHORT_PWRBTN { IGNORE = 0, SLEEP = 1, PAGE_TURN = 2, FORCE_REFRESH = 3, FOOTNOTES = 4, SHORT_PWRBTN_COUNT };

  // Long-press Confirm action while reading an EPUB. The setting cycles through these values.
  // Persisted in settings.json by index: any new function MUST use a
  // value >= 2 and be appended at the END of the enumValues array in SettingsList.h, otherwise the
  // stored indices shift and existing saves are silently misinterpreted.
  // Hide battery percentage
  enum HIDE_BATTERY_PERCENTAGE { HIDE_NEVER = 0, HIDE_READER = 1, HIDE_ALWAYS = 2, HIDE_BATTERY_PERCENTAGE_COUNT };

  // Page turn button long press behavior.
  //
  // Index 2 was ORIENTATION_CHANGE (long-press rotated the screen). That choice was
  // retired when the reader became portrait-only, so nothing could select index 2 any
  // more: the UI listed two labels and normalizeRetiredSettings() mapped a stored 2 back
  // to OFF. FONT_SIZE_STEP now REUSES that free slot rather than appending a 3, because
  // the settings UI persists the *index into the label list* (SettingsList.h) and index 3
  // would be unreachable from a 3-label list. The consequence is that a settings.json old
  // enough to still hold 2 now reads as FONT_SIZE_STEP instead of OFF — which is the new
  // default anyway — and the ORIENTATION_CHANGE line in normalizeRetiredSettings() had to
  // go, or it would wipe this value on every single load.
  enum LONG_PRESS_BUTTON_BEHAVIOR { OFF = 0, CHAPTER_SKIP = 1, FONT_SIZE_STEP = 2, LONG_PRESS_BUTTON_BEHAVIOR_COUNT };

  // UI Theme.
  //
  // These are INDICES into the label list in SettingsList.h (settings.json
  // persists the index, not the label), so new themes may only be APPENDED —
  // inserting one would silently re-point every device's saved uiTheme at a
  // different theme. LYRA_SIX is therefore 4, at the end, even though it
  // belongs with the other Lyra entries.
  enum UI_THEME { CLASSIC = 0, LYRA = 1, LYRA_3_COVERS = 2, ROUNDEDRAFF = 3, LYRA_SIX = 4 };

  // Image rendering in EPUB reader
  enum IMAGE_RENDERING { IMAGES_DISPLAY = 0, IMAGES_PLACEHOLDER = 1, IMAGES_SUPPRESS = 2, IMAGE_RENDERING_COUNT };


  enum TOUCH_READER_CONTROLS { TOUCH_READER_OFF = 0, TOUCH_READER_ON = 1, TOUCH_READER_CONTROLS_COUNT };

  enum QUICK_RESUME_SLEEP_SCREEN {
    QUICK_RESUME_NEVER = 0,
    QUICK_RESUME_AFTER_TIMEOUT = 1,
    QUICK_RESUME_SLEEP_SCREEN_COUNT
  };

  // Text anti-aliasing strength. Values 0/1 are the legacy Off/On toggle and
  // MUST keep their meaning so persisted settings from older builds round-trip;
  // new strengths append at the END.
  // The strengths pick how the 2-bit glyph gray levels map onto the panel's two
  // grayscale planes — see GfxRenderer::GrayscaleAaStrength.
  enum TEXT_ANTIALIASING {
    TEXT_AA_OFF = 0,       // 1-bit text, no grayscale passes
    TEXT_AA_STANDARD = 1,  // legacy On: light-gray + dark-gray edge pixels
    TEXT_AA_CRISP = 2,     // dark-gray edge pixels harden to black, light kept
    TEXT_AA_DARK = 3,      // all edge pixels darken one level (boldest)
    TEXT_ANTIALIASING_COUNT
  };

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Status bar settings. Every element defaults to hidden, matching the pin in
  // normalizeRetiredSettings() — the Customise Status Bar screen is no longer
  // reachable, and a fresh install never runs fromJson() (PersistableStore's
  // loadFromFile() returns early when there is no file), so the pin alone would
  // not cover it.
  // Clock display in status bar (X3 only, requires DS3231 RTC)
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t.
  // Value 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
  // Quarter-hour granularity supports oddball zones like Nepal (+5:45) and Chatham (+12:45).
  uint8_t clockUtcOffsetQ = 48;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  uint8_t clockFormat = 0;
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;
  // TEXT_ANTIALIASING value (0=Off, 1=Standard, 2=Crisp, 3=Dark). Non-zero
  // still reads as "AA enabled" everywhere the old toggle was tested as a bool.
  uint8_t textAntiAliasing = TEXT_AA_STANDARD;
  // Short power button click behaviour
  uint8_t shortPwrBtn = IGNORE;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  // Button layouts (front layout retained for migration only)
  uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  uint8_t sideButtonLayout = PREV_NEXT;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  uint8_t frontButtonBack = FRONT_HW_BACK;
  uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  uint8_t frontButtonLeft = FRONT_HW_LEFT;
  uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = NOTOSERIF;
  // Reader font size, as a slot into the active family's ascending size ramp.
  // THIS is the persisted truth; it is family-independent, so switching family
  // keeps the apparent size instead of dragging an absolute pt across ramps.
  uint8_t fontSizeSlot = DEFAULT_FONT_SIZE_SLOT;
  // Point size the slot currently resolves to. DERIVED, not persisted truth:
  // SdCardFontSystem::resolveReaderPointSize() refreshes it whenever the family
  // or slot changes. It exists so getReaderFontId() — which runs in the page
  // render loop and must not allocate — has a plain point size to switch on.
  uint8_t fontPointSize = DEFAULT_FONT_POINT_SIZE;
  // Set when a pre-slot settings file was read: the slot cannot be derived until
  // the font registry exists, so SdCardFontSystem::begin() does it once and saves.
  bool fontSlotNeedsMigration = false;
  uint8_t lineSpacing = NORMAL;
  uint8_t paragraphAlignment = JUSTIFIED;
  // Auto-sleep timeout setting (default 10 minutes). Legacy sleepTimeout enum values are migration-only.
  uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  uint8_t refreshFrequency = REFRESH_15;
  uint8_t hyphenationEnabled = 0;

  // Reader screen margin settings
  static constexpr uint8_t SCREEN_MARGIN_MIN = 5;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 40;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 5;
  uint8_t screenMargin = SCREEN_MARGIN_MIN;
  // Hide battery percentage. Pinned to HIDE_ALWAYS by normalizeRetiredSettings();
  // the default matches so fresh installs agree.
  uint8_t hideBatteryPercentage = HIDE_ALWAYS;
  // Long-press page turn button behavior. Defaults to stepping the reader font size
  // (long-press page-back = smaller, page-forward = larger). Note this default also
  // decides the page-turn EDGE: ReaderUtils::detectPageTurn() turns on button PRESS only
  // while this is OFF, and on RELEASE otherwise, so that a hold can be told apart from a
  // tap. Every fresh install therefore turns pages on release.
  uint8_t longPressButtonBehavior = FONT_SIZE_STEP;
  // UI Theme. Fresh installs only: any device that has ever written
  // settings.json already has a uiTheme key and keeps whatever it holds.
  uint8_t uiTheme = LYRA_SIX;
  // Sunlight fading compensation
  uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  uint8_t embeddedStyle = 1;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // Owner name, shown on the sleep screens ("whose device is this"). Set from
  // Settings > System > Device owner; empty hides the line.
  char ownerName[48] = "";
  // Show hidden files/directories (starting with '.') in the file browser (0 = hidden, 1 = show)
  uint8_t showHiddenFiles = 0;
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on)
  uint8_t removeReadBooksFromRecents = 0;
  // Move epub to /Read/ folder on SD card when finished (0 = disabled, 1 = enabled)
  uint8_t moveFinishedToReadFolder = 0;
  // Short press Back goes to file browser instead of home (0 = disabled, 1 = enabled)
  uint8_t backShortToFileBrowser = 0;
  // Image rendering mode in EPUB reader
  uint8_t imageRendering = IMAGES_DISPLAY;
  // Touch screen reader zones/gestures on boards with a touch controller.
  uint8_t touchReaderControls = TOUCH_READER_ON;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  // Pinned on by normalizeRetiredSettings(); the default matches so fresh installs agree.
  uint8_t quickResumeSleepScreen = QUICK_RESUME_AFTER_TIMEOUT;
  // Keep the HOST screen awake while CrossPoint is in the foreground
  // (0 = let the host dim/lock normally, 1 = suppress it).
  //
  // Simulator/iOS only. The field itself is unconditional so the struct layout
  // and this header stay identical across builds; only the Settings row and its
  // persistence are #ifdef SIMULATOR (see SettingsList.h). On an X4/X3 there is
  // no backlight and no idle timer to suppress — the e-ink panel holds its image
  // with no power — so the value is simply never read on device.
  uint8_t keepScreenAwake = 0;

  static constexpr uint8_t MIN_SLEEP_TIMEOUT_MINUTES = 1;
  static constexpr uint8_t SLEEP_TIMEOUT_NEVER_MINUTES = 31;
  static constexpr uint8_t MAX_SLEEP_TIMEOUT_MINUTES = SLEEP_TIMEOUT_NEVER_MINUTES;

  // Callback to resolve SD card font IDs. Set by SdCardFontSystem::begin().
  // Returns font ID or 0 if not found.
  using SdFontIdResolver = int (*)(void* ctx, const char* familyName, uint8_t fontSize);
  SdFontIdResolver sdFontIdResolver = nullptr;
  void* sdFontResolverCtx = nullptr;

  uint16_t getPowerButtonDuration() const {
    return (shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) ? 10 : 400;
  }
  int getReaderFontId() const;

  // Drop the SD font selection and fall back to the built-in family. The reader
  // point size comes back into BUILTIN_READER_POINT_SIZES with it, since that is
  // the only set a built-in family ships — otherwise the settings UI would keep
  // offering a size nothing renders at. Both fields are persisted in one write.
  void clearSdFontFamily();

  // Resolved status-bar composition. Consumers read the spec; only settings
  // editors read the raw fields.
  //
  // Deliberately NOT built under storeMutex: every field it reads is a single
  // byte, so a concurrent settings write can never produce a corrupt value —
  // only a snapshot mixing pre- and post-change fields. That costs at most one
  // e-ink frame drawn with a mixed status bar, which self-corrects on the next
  // refresh. Locking here would instead put a mutex on the render path and
  // stall it behind the SD write inside saveToFile(). Don't add one back.

  // Resolved text-rendering configuration for the Epub layout engine. The
  // viewport is renderer/orientation-derived, so the caller supplies it —
  // passing it in keeps a spec from ever existing in a half-filled state.
  // Unlocked for the same reason as readerRenderSpec(); see the note above.
  ReaderRenderSpec readerRenderSpec(uint16_t viewportWidth, uint16_t viewportHeight) const;

  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  static void validateFrontButtonMapping(CrossPointSettings& settings);
  static uint8_t sleepTimeoutEnumToMinutes(uint8_t legacyValue);

 private:
  // Pins values whose UI has been withdrawn back into a valid, reachable
  // state. Runs at the end of fromJson(), under PersistableStore's storeMutex.
  void normalizeRetiredSettings();

 public:
  float getReaderLineCompression() const;
  unsigned long getSleepTimeoutMs() const;
  int getRefreshFrequency() const;
};

// Helper macro to access settings
#define SETTINGS CrossPointSettings::getInstance()
