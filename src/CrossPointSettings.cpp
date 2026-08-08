#include "CrossPointSettings.h"

#include <I18n.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "SettingsList.h"
#include "fontIds.h"

namespace {

// Stack buffer for "<key>_obf" key construction — avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

void CrossPointSettings::toJson(JsonDocument& doc) const {
  const CrossPointSettings& s = *this;

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  }

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  // Font family and size — both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = fontFamily;
  doc["fontSizeSlot"] = fontSizeSlot;
  // Same reason: screenMargin became a drop-down over a value ramp, so its
  // SettingInfo carries a getter/setter and no valuePtr, and the loop above
  // skips it. Without this line the margin silently resets to its default on
  // every boot -- the row still worked, which is what made it invisible.
  doc["screenMargin"] = screenMargin;
  // The resolved point size is written too, and is what pre-slot firmware reads
  // back from "fontSize" if this card is moved to an older build.
  doc["fontSize"] = fontPointSize;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  // Owner name — not in SettingsList (free text), save manually
  if (ownerName[0] != '\0') {
    doc["ownerName"] = ownerName;
  }

  // Language -- picked via the in-place option popup in SettingsActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";
}

void CrossPointSettings::normalizeRetiredSettings() {
  // The reader is portrait-only and no longer exposes any way to rotate. A
  // save written before the controls were withdrawn can still hold a landscape
  // or inverted value, which would otherwise be permanent, so it is pinned
  // back on every load. The field itself stays: ReaderUtils, UITheme and the
  // sleep screens all still read it.
  //
  // Runs at the end of fromJson(), i.e. under PersistableStore's storeMutex —
  // no extra locking here.
  //
  // longPressButtonBehavior deliberately has NO line here. Index 2 was the
  // retired ORIENTATION_CHANGE and is now FONT_SIZE_STEP, a live choice with a
  // label of its own, so pinning it would silently wipe the setting — including
  // the factory default — on every load. Out-of-range values are still clamped
  // by the generic enum loop in fromJson().
  //

  // The Display tab, Manage Fonts and Customise Status Bar were withdrawn from
  // the device Settings UI. The settings below have no on-device control left,
  // so they are pinned to the values the UI used to be set to rather than left
  // wherever an older save happened to leave them. The fields stay live: the
  // status bar renderer and the sleep path still read them, and they remain in
  // getSettingsList() so both persistence and the web settings API keep
  // working. A change made over the web therefore lasts until the next load,
  // which is the same contract `orientation` has.
  //
  // `uiTheme` used to be pinned here. It no longer exists: on 2026-08-04 the
  // four other themes were deleted and UITheme::setTheme() builds Lyra Six
  // unconditionally, so there is nothing left to pin or to persist.
  // quickResumeSleepScreen deliberately has NO line here any more. It was pinned
  // when its only control lived on the withdrawn Display tab; it now has a real
  // row under System (SettingsList.h), and pinning a visible control would
  // silently revert the owner's choice on the next load.
  hideBatteryPercentage = HIDE_ALWAYS;

  // Status bar: every element hidden. Thickness is left alone — it only has an
  // effect while the progress bar is drawn, and it is not a visibility control.

  // The Controls tab was withdrawn from the device Settings UI, so these two have
  // no on-device control left and are pinned to the behaviour the owner wants
  // rather than left wherever an older save happened to leave them. They stay
  // live and web-settable: the reader reads shortPwrBtn every frame and
  // longPressButtonBehavior gates the font-size gesture.
  shortPwrBtn = SHORT_PWRBTN::SLEEP;
  longPressButtonBehavior = FONT_SIZE_STEP;

  // Libre Franklin is the only System font (owner ruling 2026-08-07) and its row
  // is withdrawn from the device UI, so a save written while the picker still
  // existed must not keep the chrome on Ubuntu or a Noto face forever. The field
  // initialiser in CrossPointSettings.h is already SYSTEM_FONT_LIBREFRANKLIN, so
  // a fresh install and an upgraded one now agree — pinning without that would
  // leave them disagreeing, which is the half-fix CLAUDE.md warns about.
  systemFont = SYSTEM_FONT_LIBREFRANKLIN;

  // The Reader tab was withdrawn from the device Settings UI (2026-08-04). Two
  // of its rows survived and moved to System — the Text Settings action (font
  // family and size) and Screen Margin — so neither appears here: pinning a
  // control the owner can still see would silently revert their choice on the
  // next load — which is exactly why quickResumeSleepScreen above lost its pin.
  //
  // Everything else the tab held has no on-device control left and is pinned to
  // the values the owner asked for. The fields stay live and web-settable, and
  // stay in getSettingsList() so persistence keeps working; a change made over
  // the web lasts until the next load, the same contract the pins above have.
  //
  // Two of these do not map onto "on":
  //  * lineSpacing is Tight/Normal/Wide, so NORMAL — the shipped default, and
  //    the value getReaderLineCompression() answers 1.0 for, i.e. the font's own
  //    leading rather than a squeeze or a stretch.
  //  * textAntiAliasing's values 0/1 ARE the legacy Off/On toggle (see
  //    TEXT_ANTIALIASING), so "on" is exactly TEXT_AA_STANDARD. CRISP and DARK
  //    were appended later and no "on" ever meant them.
  //
  // Each pin matches its field's initializer in CrossPointSettings.h, because a
  // fresh install never runs fromJson() (PersistableStore::loadFromFile()
  // returns early with no file) and would otherwise disagree with a loaded one.
  lineSpacing = NORMAL;
  paragraphAlignment = JUSTIFIED;
  embeddedStyle = 1;
  // OFF, unlike its neighbours: the owner named it as the exception.
  focusReadingEnabled = 0;
  hyphenationEnabled = 1;
  extraParagraphSpacing = 1;
  textAntiAliasing = TEXT_AA_STANDARD;
  imageRendering = IMAGES_DISPLAY;
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  CrossPointSettings& s = *this;
  bool needsResave = false;

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  for (const auto& info : getSettingsList()) {
    if (!info.key) continue;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) continue;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        continue;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        const std::string decoded = obfuscation::deobfuscateFromBase64(doc[obfKey] | "", &ok);
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          copyToField(destPtr, raw, info.stringMaxLen);
        }
      }
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        // enumCount(), not enumValues.size(): a runtime-labelled row keeps its
        // choices in enumStringValues and leaves enumValues empty, so the old
        // form clamped against 0. `val < 0` is never true, so EVERY load threw
        // the saved byte away and substituted the default — which is why
        // Typing Redraw Delay and Editor Font reverted on every single boot
        // while settings.json plainly held the chosen value.
        v = clamp(v, (uint8_t)info.enumCount(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  // Reader font size — an actual point size since 1.5. Files written by 1.4 and
  // earlier hold the old SMALL/MEDIUM/LARGE/EXTRA_LARGE slot in 0..3; no font is
  // renderable at those sizes, so the range is unambiguous and folds to the
  // point sizes those slots used to mean. Drop this once 1.4 upgrades are done.
  uint8_t storedFontSize = doc["fontSize"] | DEFAULT_FONT_POINT_SIZE;
  if (storedFontSize <= LEGACY_FONT_SIZE_MAX) {
    storedFontSize = 12 + storedFontSize * 2;  // 0,1,2,3 -> 12,14,16,18
    needsResave = true;
  }
  fontPointSize = storedFontSize;
  if (doc["fontSizeSlot"].isNull()) {
    // Pre-slot file. The slot has to come from the point size measured against
    // the ACTIVE family's ramp, and the registry does not exist yet at load time
    // (main.cpp loads settings well before sdFontSystem.begin()), so defer it.
    fontSlotNeedsMigration = true;
    needsResave = true;
  } else {
    fontSizeSlot = clamp(doc["fontSizeSlot"] | DEFAULT_FONT_SIZE_SLOT, READER_FONT_SLOT_COUNT, DEFAULT_FONT_SIZE_SLOT);
  }

  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | (uint8_t)0;
  fontFamily = clamp(storedFontFamily, BUILTIN_FONT_COUNT, 0);
  // Screen margin — same, a drop-down with a getter/setter and no valuePtr.
  // Stored in PIXELS, not as the picker's index. Clamped rather than snapped to
  // the ramp: a file written by the old 5..40 stepper, or by an API client, can
  // hold any value in range, and the picker resolves it to the nearest step
  // when it renders. Only an out-of-range byte falls back to the default.
  const uint8_t storedMargin = doc["screenMargin"] | SCREEN_MARGIN_DEFAULT;
  screenMargin = storedMargin <= SCREEN_MARGIN_MAX ? storedMargin : SCREEN_MARGIN_DEFAULT;
  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  // Owner name — not in SettingsList, load manually
  const char* own = doc["ownerName"] | "";
  strncpy(ownerName, own, sizeof(ownerName) - 1);
  ownerName[sizeof(ownerName) - 1] = '\0';
  if (storedFontFamily == LEGACY_OPENDYSLEXIC && sdFontFamilyName[0] == '\0') {
    fontFamily = BUILTIN_LIBRE_FRANKLIN;
    strncpy(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName) - 1);
    sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
    needsResave = true;
  } else if (storedFontFamily >= BUILTIN_FONT_COUNT) {
    needsResave = true;
  }

  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  // Pin values whose UI has been withdrawn back into a valid, reachable state.
  normalizeRetiredSettings();

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  // One ramp for every family. SD fonts always used these values ("the most
  // neutral", tuned against Bookerly), and the built-in fallback is Libre
  // Franklin, whose uniform-slot leading was derived under the same ramp — the
  // separate Noto Sans ramp went with that family's removal.
  switch (lineSpacing) {
    case TIGHT:
      return 0.95f;
    case NORMAL:
    default:
      return 1.0f;
    case WIDE:
      return 1.1f;
  }
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  // The slot is family-independent, so it carries over untouched; only the
  // resolved point size has to move onto the built-in ramp.
  fontPointSize = pointSizeForSlot(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontSizeSlot);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating — this runs
  // in the page render loop) so rendering is correct even before it has run.
  // Libre Franklin is the only built-in family; fontFamily no longer selects
  // anything here. The 14 pt default is the one cut registered even in
  // OMIT_FONTS builds (main.cpp), so this can always resolve.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  switch (pt) {
    case 12:
      return LIBREFRANKLIN_READER_12_FONT_ID;
    case 16:
      return LIBREFRANKLIN_READER_16_FONT_ID;
    case 18:
      return LIBREFRANKLIN_READER_18_FONT_ID;
    case 14:
    default:
      return LIBREFRANKLIN_READER_14_FONT_ID;
  }
}
