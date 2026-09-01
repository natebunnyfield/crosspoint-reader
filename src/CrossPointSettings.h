#pragma once
#include <ArduinoJson.h>
#include <Epub/AutoJustify.h>
#include <Epub/LineBreakMode.h>
#include <Epub/ReaderRenderSpec.h>
#include <LigatureControl.h>
#include <PersistableStore.h>

#include <cstdint>

#include "FontActivation.h"

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
    // Dark renditions of the two surviving calendars. Two more picker VALUES
    // rather than a flag on the two above, for the same reason DARK and LIGHT
    // are two values instead of one screen plus a toggle: a drawn sleep screen
    // carries its own polarity, because the frame stays on the panel with the
    // device powered off and so must not depend on a runtime flag to look right
    // (SleepActivity::onEnter). SETTINGS.darkMode is deliberately NOT consulted
    // on this path -- it is cleared before any sleep screen draws.
    CALENDAR_DARK = 12,
    CALENDAR_WESTSIDE_DARK = 13,
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

  // Font family options (built-in fonts only; SD card fonts use sdFontFamilyName).
  // Libre Franklin is the only built-in reading family (owner ruling
  // 2026-08-07). Index 0 used to be Noto Serif and 1 Noto Sans; both were
  // removed, and fromJson()'s clamp maps any stored 1 back to 0, so an old
  // settings.json migrates on its first load.
  enum FONT_FAMILY { BUILTIN_LIBRE_FRANKLIN = 0, FONT_FAMILY_COUNT };
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
  // M. Moved 1 -> 3 on 2026-08-26 when XXS and XS were inserted below S: the
  // slot is an INDEX, so the default has to move with the insertion or a fresh
  // install reads at XS. Still 14 pt on the built-in ramp.
  static constexpr uint8_t DEFAULT_FONT_SIZE_SLOT = 3;  // M, = 14pt on the built-in ramp
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
  //
  // RETIRED 2026-09-01 (owner ruling, docs/hold-gestures.md: "kill chapter skip"):
  // CHAPTER_SKIP was already unreachable before this — longPressButtonBehavior below
  // has been a `static constexpr = FONT_SIZE_STEP` since the 2026-08-21 settings
  // reduction, so it is never read from settings.json and no picker row has offered
  // CHAPTER_SKIP since that date. This is a tombstone, not a live migration: the
  // value stays DEFINED (never renumber a persisted enum — SettingsList.h's
  // append-only rule) but nothing in the firmware compares against it any more,
  // and every caller that used to branch on it now takes the FONT_SIZE_STEP path
  // unconditionally — the least surprising landing for anyone who had chapter
  // skip selected, since it is also what every other value already collapses to.
  enum LONG_PRESS_BUTTON_BEHAVIOR {
    OFF = 0,
    CHAPTER_SKIP = 1,  // retired; see comment above — never reassign this slot
    FONT_SIZE_STEP = 2,
    LONG_PRESS_BUTTON_BEHAVIOR_COUNT
  };

  // Order is the picker's order and the persisted value, so it is frozen:
  // Ubuntu must stay 0 or every settings.json that already names a system font
  // would silently change the UI face on the next boot. New families append.
  enum SYSTEM_FONT {
    SYSTEM_FONT_UBUNTU = 0,
    SYSTEM_FONT_NOTOSANS = 1,
    SYSTEM_FONT_NOTOSERIF = 2,
    SYSTEM_FONT_LIBREFRANKLIN = 3,
    SYSTEM_FONT_COUNT
  };

  // Text-entry keyboard. Order is the picker's order and the persisted value,
  // so it is frozen: new keyboards append at the END.
  enum KEYBOARD_LAYOUT { KEYBOARD_DAISY = 0, KEYBOARD_GRID13 = 1, KEYBOARD_QWERTY = 2, KEYBOARD_LAYOUT_COUNT };

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

  // Output polarity: white-on-black instead of black-on-white. Applied by the
  // display driver on the way to the panel, so the framebuffer stays logical
  // and no drawing code changes (HalDisplay::setInverted). A DRAWN sleep screen
  // is the one exception and is always normal polarity: it carries its own
  // light/dark choice in `sleepScreen` below, and it stays on the panel while
  // the device is powered off, where no runtime flag is left to interpret it.
  // SETTINGS REDUCTION, owner ruling 2026-08-21 ("yes to all"): every field
  // below declared `static constexpr` is a former setting hardcoded at its
  // ruled value -- see docs/settings-reduction-plan.md for the per-field
  // rationale and odds table. Readers compile unchanged; anything that tries
  // to WRITE one fails to compile, which is the point. The JSON keys of
  // removed rows are simply ignored by fromJson (ArduinoJson lookups miss),
  // so old settings.json files still load.
  uint8_t darkMode = 0;

  // Sleep screen settings
  uint8_t sleepScreen = DARK;
  // Sleep screen cover mode settings
  static constexpr uint8_t sleepScreenCoverMode = FIT;
  // Sleep screen cover filter
  static constexpr uint8_t sleepScreenCoverFilter = NO_FILTER;
  // Status bar settings. Every element defaults to hidden, matching the pin in
  // normalizeRetiredSettings() — the Customise Status Bar screen is no longer
  // reachable, and a fresh install never runs fromJson() (PersistableStore's
  // loadFromFile() returns early when there is no file), so the pin alone would
  // not cover it.
  // Clock display in status bar (X3 only, requires DS3231 RTC)
  // Clock UTC offset in quarter-hour steps, biased by 48 so it fits in uint8_t.
  // Value 48 = UTC+0, 0 = UTC-12:00, 104 = UTC+14:00.
  // Quarter-hour granularity supports oddball zones like Nepal (+5:45) and Chatham (+12:45).
  // Fresh devices default to US Central daylight time (Chicago, UTC-5, biased 28).
  // The firmware has no DST rules, so the user flips between the CDT and CST
  // list entries seasonally; existing devices keep whatever byte they stored.
  uint8_t clockUtcOffsetQ = 28;
  // Clock display format: 0 = 24-hour, 1 = 12-hour
  static constexpr uint8_t clockFormat = 0;  // 24-hour, hardcoded 2026-08-21
  // Set once an NTP sync succeeds. Used to skip re-syncing on every WiFi connect.
  // Resetting to 0 (e.g. via the web UI) forces a re-sync on next WiFi connect.
  uint8_t clockHasBeenSynced = 0;
  // Set once a Bluetooth keyboard has bonded, cleared by Forget.
  //
  // This exists because the bond count can only be read from the NimBLE host,
  // and Create Note / Claude need the answer BEFORE deciding whether to start
  // the host at all. Calling ble_store_util_count() with the stack down panics
  // the device (reset reason 4, immediately on entering the editor) — which is
  // exactly what shipped in 190fe423.
  uint8_t btKeyboardPaired = 0;

  // How long typing settles before the screen redraws, as an INDEX into
  // DISPLAY_DEBOUNCE_MS. Index, not milliseconds: an ENUM row persists the
  // picker index, so storing the raw value here would reinterpret every saved
  // settings.json (see "An ENUM row persists its INDEX" in CLAUDE.md).
  // Order is the persisted encoding — append only.
  // 0 IS LAST, not first, even though it belongs at the front numerically: the
  // INDEX is what settings.json persists, so inserting at the head would
  // silently re-map every card in the field (a saved 250 ms would come back as
  // 100 ms). Append-only is the rule; the picker showing "0 ms" after "1000 ms"
  // is the price of not corrupting existing settings.
  static constexpr uint16_t DISPLAY_DEBOUNCE_MS[] = {25, 50, 100, 250, 500, 1000, 0};
  static constexpr uint8_t DISPLAY_DEBOUNCE_COUNT = sizeof(DISPLAY_DEBOUNCE_MS) / sizeof(DISPLAY_DEBOUNCE_MS[0]);
  // 250 ms (owner ruling 2026-08-06: optimize for typing feel). This is the
  // value that collapses a normal typing burst into ONE ~500 ms panel refresh.
  // Shorter settings do not make a character appear sooner -- the waveform is
  // ~500 ms regardless -- they just queue refreshes back to back, which costs
  // panel energy and accelerates ghosting for no throughput gain.
  static constexpr uint8_t DEFAULT_DISPLAY_DEBOUNCE = 3;  // 250 ms
  static constexpr uint8_t displayDebounce = DEFAULT_DISPLAY_DEBOUNCE;

  // Milliseconds of quiet after the last keystroke before the panel redraws.
  // Low values refresh more often; on e-ink the refresh itself is ~570 ms, so
  // anything under that trades battery and flicker for immediacy rather than
  // actually feeling faster.
  //
  // 0 ms is offered for the SIMULATOR AND THE PHONE ONLY, where a "refresh" is
  // a texture upload with no waveform, no ghosting and no battery cost -- the
  // entire reason this setting exists does not apply there. On real e-ink 0
  // would mean a full refresh per keystroke, so the device clamps it to the
  // shortest real step instead of obeying it. A card carrying 0 (written on a
  // phone, then moved to hardware) therefore behaves sanely rather than
  // thrashing the panel.
  unsigned long getDisplayDebounceMs() const {
    const uint8_t i = displayDebounce < DISPLAY_DEBOUNCE_COUNT ? displayDebounce : DEFAULT_DISPLAY_DEBOUNCE;
    const uint16_t ms = DISPLAY_DEBOUNCE_MS[i];
#ifndef SIMULATOR
    if (ms == 0) return DISPLAY_DEBOUNCE_MS[0];  // clamp on real e-ink
#endif
    return ms;
  }

  // Owner ruling 2026-08-05: the EDITOR font group is separate from the
  // reading S tier. This is a live position in editorfonts::FAMILIES, NOT the
  // persisted form — settings.json stores the family NAME under
  // "editorFontFamily" (2026-08-15), so removing or reordering a writing face
  // no longer needs a migration and "Enum order is frozen" does not bind this
  // one row. The byte is still written for the web API and for older builds.
  uint8_t editorFont = 0;

  // Editor font POINT SIZE (owner ruling 2026-08-15, "allow editor font to be
  // resized"). The stored byte is the real size -- 12 or 14 -- never a picker
  // position, which is why its SettingsList row is a getter/setter with
  // valuePtr left null and why toJson/fromJson carry it explicitly. Storing an
  // index here would have repeated on the size axis the exact bug the family
  // axis had just been rescued from.
  //
  // 13 was asked for in the same ruling and declined: no ramp carries it and
  // loadForDisplay snaps to nearest, so the row would have claimed 13 and drawn
  // 12. See src/notes/EditorFonts.h SIZES.
  uint8_t editorFontSize = 12;

  // Text rendering settings
  static constexpr uint8_t extraParagraphSpacing = 1;
  // TEXT_ANTIALIASING value (0=Off, 1=Standard, 2=Crisp, 3=Dark). Non-zero
  // still reads as "AA enabled" everywhere the old toggle was tested as a bool.
  static constexpr uint8_t textAntiAliasing = TEXT_AA_STANDARD;
  // Short power button click behavior.
  //
  // SLEEP, not IGNORE, and it must stay in step with the pin in
  // normalizeRetiredSettings(). The Controls tab is withdrawn from the device
  // UI, so nothing on the device can correct a disagreement: this initialiser
  // is the ONLY value a factory-fresh unit ever sees (fromJson never runs
  // without a settings.json), while every device that has saved once gets the
  // pinned value. It read IGNORE against a pin of SLEEP until 2026-08-15, so a
  // fresh unit's power button did nothing while an upgraded one slept — the
  // exact "pinning is only half" trap CLAUDE.md documents. Owner ruling: SLEEP
  // on both.
  static constexpr uint8_t shortPwrBtn = SLEEP;
  // EPUB reading orientation settings
  // 0 = portrait (default), 1 = landscape clockwise, 2 = inverted, 3 = landscape counter-clockwise
  // Button layouts (front layout retained for migration only)
  static constexpr uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
  static constexpr uint8_t sideButtonLayout = PREV_NEXT;
  // Front button remap (logical -> hardware)
  // Used by MappedInputManager to translate logical buttons into physical front buttons.
  static constexpr uint8_t frontButtonBack = FRONT_HW_BACK;
  static constexpr uint8_t frontButtonConfirm = FRONT_HW_CONFIRM;
  static constexpr uint8_t frontButtonLeft = FRONT_HW_LEFT;
  static constexpr uint8_t frontButtonRight = FRONT_HW_RIGHT;
  // Reader font settings
  uint8_t fontFamily = BUILTIN_LIBRE_FRANKLIN;
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
  // NOT constexpr, alone in this block: the 2026-08-21 reduction assumed every
  // reading-taste field was web-only, and this one is not -- the reader has a
  // designed CHORD (Confirm held + side button, EpubReaderActivity ~:526) that
  // steps it on-device, the same gesture family as the font-size ramp. The
  // settings ROW is gone; the field persists via the manual key in toJson().
  uint8_t lineSpacing = NORMAL;
  // Text alignment. The Justified / Ragged right ROW is withdrawn again on
  // 2026-08-23 — owner ruling: "remove ragged right or justified ios app
  // settings, instead make it automatic by letting the character length decide
  // what is optimal." The base intent is JUSTIFIED, exactly as the 2026-08-21
  // reduction hardcoded it, and the ragged decision has moved to where the
  // measure is actually known: ParsedText::layoutAndExtractLines demotes a
  // justified block to its natural ragged edge when that block's own measure
  // carries fewer than autojustify::THRESHOLD_CHARS characters per line
  // (lib/Epub/Epub/AutoJustify.h, docs/auto-justification.md).
  //
  // `static constexpr` is this repo's retirement pattern (see the 2026-08-21
  // block above): fromJson iterates getSettingsList(), so a key that no longer
  // has a row is never read back and a stored 0..4 from either era is ignored
  // rather than re-pointed at something it did not mean. toJson stops writing
  // it. The stored integers themselves are untouched — CssTextAlign still maps
  // 1:1 onto PARAGRAPH_ALIGNMENT — so a future row could be reinstated on the
  // same values.
  static constexpr uint8_t paragraphAlignment = JUSTIFIED;
  // Line Grid (owner order 2026-08-22, default off): when ON every vertical
  // advance the paginator makes rounds UP to a whole line-height, so every
  // baseline on every page sits on the same grid. Participates in
  // ReaderRenderSpec — flipping it repaginates.
  uint8_t lineGridEnabled = 0;
  // Automatic justification THRESHOLD, in characters per line. The decision
  // stays automatic -- the measure still decides, per block, in
  // ParsedText::layoutAndExtractLines -- and this is the count it decides
  // against. Owner ruling 2026-08-24: "make justified or ragged right character
  // count an ios app setting."
  //
  // It landed here rather than in the iOS Settings.app bundle because it moves
  // line BREAKS, so it has to be a ReaderRenderSpec field for the section cache
  // to notice it, and ReaderRenderSpec is built from this struct. A host-side
  // value would have left every already-paginated book serving stale breaks
  // with a header that compared equal. Being a firmware setting also means the
  // X3/X4 get it, not only the phone, and it is served by the web settings API.
  //
  // Stored as the character COUNT, not a picker index -- the same shape as
  // screenMargin, so the ladder can gain a rung without migrating a save. Rows
  // offered by autojustify::THRESHOLD_CHOICES; anything else read off the card
  // falls back to autojustify::THRESHOLD_CHARS via autojustify::clampThreshold.
  // Its row has a getter/setter and no valuePtr, so it persists BY HAND in
  // toJson/fromJson -- see the note on screenMargin there.
  uint8_t justifyThresholdChars = autojustify::THRESHOLD_CHARS;
  // Ligature substitution, master switch. Owner ruling 2026-08-24: "give a
  // full subpage of Typography Settings that gives all available typography
  // options with full granularity, including toggling each individual
  // ligature." Defaults ON, which is what every build before it did, so an
  // upgraded card renders identically until a row is touched.
  //
  // It is a switch rather than a mode, and it DOMINATES the per-pair list
  // below: with it off the Typography screen stops offering the individual
  // rows instead of drawing rows whose value is a lie. Turning it back on
  // restores whatever per-pair choices were stored, because those live in
  // their own field and are never cleared by this one.
  uint8_t ligaturesEnabled = 1;
  // Which INDIVIDUAL ligature pairs are switched off, as the comma-separated
  // spec ligatures::canonicalize() defines -- "st" is the s+t pair, "fh" is
  // f+h. Empty means every ligature the face carries is allowed, which is the
  // shipped default.
  //
  // A string, not a bitmask, because the set of ligatures is PER FAMILY and
  // open-ended: Edgar ships nine Private Use Area pairs that Almendra does not
  // have, so there is no fixed enumeration a bit index could refer to. Keyed
  // by the INPUT pair rather than the output codepoint for the same reason --
  // U+E000 means `fb` only in Edgar, while s+t is s+t in every face ever cut.
  // The whole argument, including Almendra emitting its `fh` as the codepoint
  // that means `ff`, is in lib/EpdFont/LigatureControl.h.
  //
  // Sized from ligatures::SPEC_BUF_SIZE so the model owns the cap and this
  // field cannot silently truncate a spec the model would accept. Persisted by
  // the generic string path in toJson/fromJson via its SettingInfo::String row
  // in SettingsList.h, which also serves it over the web settings API.
  char ligaturesOff[ligatures::SPEC_BUF_SIZE] = "";
  // Auto-sleep timeout setting (default 10 minutes). Legacy sleepTimeout enum values are migration-only.
  static constexpr uint8_t sleepTimeoutMinutes = 10;
  // E-ink refresh frequency (default 15 pages)
  static constexpr uint8_t refreshFrequency = REFRESH_15;
  // WHICH LINE BREAKER RUNS. Not a hyphen switch, whatever the field is called:
  // 1 is the first-fit greedy breaker that splits words, 0 is the total-fit
  // dynamic program that does not. The values, the default and the whole
  // argument are in lib/Epub/Epub/LineBreakMode.h; the row a reader sees is
  // Typography Settings > Line Breaks, whose two labels name the trade rather
  // than the flag.
  //
  // Frozen at 1 by the 2026-08-21 reduction and UNFROZEN on 2026-08-25 (owner
  // ruling: "unfreeze hyphenation and get the better line breaker"). The
  // default is unchanged and a fresh install renders exactly as before.
  //
  // ONE CLASS OF CARD DOES MOVE ON ITS OWN, and it is deliberate. Between
  // 2026-08-04 (cc6937b97) and the freeze, normalizeRetiredSettings() PINNED
  // this to 1 on every load, so a card still carrying "hyphenationEnabled": 0
  // from the era when the old Hyphenation toggle was on screen has been
  // rendering at 1 regardless. That pin is gone, so fromJson honors the 0 again
  // and the device switches to the DP and repaginates without the row being
  // touched. Honoring it is the right call — it is a real choice made when a
  // row for it existed, and a row for it exists again; discarding a stored
  // preference because we changed our minds twice is the worse option. The
  // window is narrow (a save after 2026-08-04 wrote 1; a save after 2026-08-21
  // wrote no key at all), but it is not empty, so it is written down here
  // rather than described as impossible.
  //
  // It is part of ReaderRenderSpec and is written into every section file and
  // compared on load (Section.cpp), so moving it repaginates cached books by
  // itself. No new spec field, no SECTION_FILE_VERSION bump.
  uint8_t hyphenationEnabled = linebreak::STORED_DEFAULT;

  // Reader screen margin settings
  // Extra margin in pixels, added on top of the panel's bezel margins
  // (GfxRenderer::VIEWABLE_MARGIN_*), so 0 means bezel-only rather than text
  // against the glass. The picker offers this whole ramp; the value stored is
  // the pixel count, never the picker's index.
  static constexpr uint8_t SCREEN_MARGIN_MIN = 0;
  static constexpr uint8_t SCREEN_MARGIN_MAX = 45;
  static constexpr uint8_t SCREEN_MARGIN_STEP = 5;
  // Separate from MIN since 2026-08-04, when the range grew down to 0: the
  // default was written as SCREEN_MARGIN_MIN and would have silently become 0
  // for every fresh install.
  static constexpr uint8_t SCREEN_MARGIN_DEFAULT = 5;
  uint8_t screenMargin = SCREEN_MARGIN_DEFAULT;
  // Hide battery percentage. Pinned to HIDE_ALWAYS by normalizeRetiredSettings();
  // the default matches so fresh installs agree.
  static constexpr uint8_t hideBatteryPercentage = HIDE_ALWAYS;
  // Long-press page turn button behavior. Defaults to stepping the reader font size
  // (long-press page-back = smaller, page-forward = larger). Note this default also
  // decides the page-turn EDGE: ReaderUtils::detectPageTurn() turns on button PRESS only
  // while this is OFF, and on RELEASE otherwise, so that a hold can be told apart from a
  // tap. Every fresh install therefore turns pages on release.
  static constexpr uint8_t longPressButtonBehavior = FONT_SIZE_STEP;
  // Typeface the UI chrome is drawn in -- headers, list rows, button hints,
  // popups, the battery readout. Swaps all three UI sizes (8/10/12 pt) at once;
  // the reader's body face is a separate setting and is not affected.
  // Libre Franklin is the default: the grotesque bench picked it as the fork's
  // text sans, and the chrome should be set in the same face the reader is.
  // This DOES change the UI face on a device upgrading from a build that had no
  // systemFont key -- there is no value to preserve there, and the alternative
  // is a default nobody chose. A settings.json that already names a font keeps
  // it; Ubuntu stays available and unchanged at index 0.
  static constexpr uint8_t systemFont = SYSTEM_FONT_LIBREFRANKLIN;
  // Text-entry keyboard for every entry field (searches, WiFi passwords, owner
  // name, renames). 13-grid is the default: the 2026-08-04 design ruling picked
  // it as THE keyboard; QWERTY stays selectable for the unconvinced, and the
  // daisywheel ships per docs/daisywheel.md. A settings.json without the key
  // gets the ruling's default, same reasoning as systemFont above.
  uint8_t keyboardLayout = KEYBOARD_GRID13;
  // Sunlight fading compensation
  static constexpr uint8_t fadingFix = 0;
  // Power button return from footnotes (1 = enabled, 0 = disabled)
  static constexpr uint8_t pwrBtnFootnoteBack = 1;
  // Use book's embedded CSS styles for EPUB rendering (1 = enabled, 0 = disabled)
  static constexpr uint8_t embeddedStyle = 1;
  // Focus Reading - emphasizes the first part of words with bold
  uint8_t focusReadingEnabled = 0;
  // SD card font family name (empty = use built-in fontFamily)
  char sdFontFamilyName[32] = "";
  // WHICH INSTALLED FAMILIES THE READER CURRENTLY WANTS -- a comma-separated
  // list of the ones switched OFF, toggled by a long hold in the font pickers.
  //
  // The OFF set rather than the ON set, deliberately: a family installed over
  // WebDAV must arrive ACTIVE, and with an ON list every new font would land
  // deactivated and look broken.
  //
  // A string for the same reason ligaturesOff is one -- the set of families is
  // open-ended (a card carries whatever was put on it, including families this
  // project has never heard of), so there is no fixed enumeration a bit index
  // could refer to. Keying by directory name also means the list survives the
  // registry being reordered and a family being removed and re-added.
  //
  // The model, every refusal, and the rule that the last active family cannot
  // be switched off live in src/FontActivation.h. Persisted by the generic
  // string path in toJson/fromJson via its SettingInfo::String row in
  // SettingsList.h, which also serves it over the web settings API.
  char fontsOff[fontactivation::SPEC_BUF_SIZE] = "";
  // Owner name, shown on the sleep screens ("whose device is this"). Set from
  // Settings > System > Device owner; empty hides the line.
  char ownerName[48] = "";
  // GitHub fine-grained personal access token for Update Library (the epub set
  // is a release on a PRIVATE repo, so the fetch must authenticate). Sized for
  // fine-grained PATs ("github_pat_" + 82 chars); classic 40-char tokens fit
  // too. Empty means "not configured" and the Update Library screen says so
  // instead of fetching. NEVER log this value.
  char githubToken[104] = "";
  // Remove a book from the Recent Books list when its End-of-Book screen is reached (0 = off, 1 = on)
  uint8_t removeReadBooksFromRecents = 0;
  // Move epub to /Read/ folder on SD card when finished (0 = disabled, 1 = enabled)
  uint8_t moveFinishedToReadFolder = 0;
  // Short press Back goes to file browser instead of home (0 = disabled, 1 = enabled)
  static constexpr uint8_t backShortToFileBrowser = 0;
  // Image rendering mode in EPUB reader
  static constexpr uint8_t imageRendering = IMAGES_DISPLAY;
  // Touch screen reader zones/gestures on boards with a touch controller.
  static constexpr uint8_t touchReaderControls = TOUCH_READER_ON;
  // Language setting (Language enum index, default 0 = EN)
  uint8_t language = 0;
  // Quick Resume: keep current content visible with moon icon instead of showing a static sleep screen.
  // While ON, an inactivity-timeout sleep takes this path and never consults
  // sleepScreen (see sleepscreen::shouldQuickResume).
  //
  // Defaults OFF since 2026-08-04. It shipped ON, and because an inactivity
  // timeout is how a reader sleeps nearly every time, that made the whole Sleep
  // Screen setting look broken out of the box: pick Calendar, watch the row
  // update, never see it. Turning it on is still one row away, and picking the
  // Quick Resume sleep screen turns it on for you.
  static constexpr uint8_t quickResumeSleepScreen = QUICK_RESUME_NEVER;
  // Keep the HOST screen awake while CrossPoint is in the foreground
  // (0 = let the host dim/lock normally, 1 = suppress it).
  //
  // Simulator/iOS only. The field itself is unconditional so the struct layout
  // and this header stay identical across builds; only the Settings row and its
  // persistence are #ifdef SIMULATOR (see SettingsList.h). On an X4/X3 there is
  // no backlight and no idle timer to suppress — the e-ink panel holds its image
  // with no power — so the value is simply never read on device.
  static constexpr uint8_t keepScreenAwake = 0;

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
  // The smallest cut of the SAME family the reader is using. Falls back to the
  // reading font itself when the family has nothing smaller.
  int getSmallestReaderFontId() const;

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

  // Rewrite `ligaturesOff` into the canonical form the model uses: parsed,
  // sorted, deduped, malformed tokens dropped. Returns true if the stored text
  // changed. Idempotent.
  //
  // EVERY WRITER MUST GO THROUGH THIS. The spec arrives from three places that
  // can all spell it loosely -- a hand-edited settings.json, the web settings
  // API, and an older build's file -- and canonicalize() is what makes
  // `"fh,st,st"` and `"st,fh"` the same preference with the same fingerprint.
  // Until 2026-08-25 only fromJson() called it, so a POST of `"st, fh"` was
  // stored verbatim, echoed back verbatim by the settings GET, and then
  // silently became `"st"` on the next boot: the space makes ` fh` three
  // codepoints, which is a malformed token and is dropped. The web UI showed a
  // value the device did not have and would never have again.
  bool normalizeLigatureSpec();

  // Push the stored ligature preference down into lib/EpdFont, which cannot
  // include this header. Idempotent; call it after anything writes either
  // ligature field.
  void applyLigaturePreference() const;

  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

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
