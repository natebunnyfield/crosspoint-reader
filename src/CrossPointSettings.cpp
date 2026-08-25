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
#include "notes/EditorFonts.h"

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

  // frontButton* keys: gone 2026-08-21. The comment here used to credit a
  // "RemapFrontButtons sub-activity" that does not exist anywhere in the tree
  // -- the four bytes were write-only persistence. Hardcoded to the identity
  // mapping in CrossPointSettings.h; old keys in settings.json are ignored.
  // Font family and size — both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = fontFamily;
  doc["fontSizeSlot"] = fontSizeSlot;
  // Same reason: screenMargin became a drop-down over a value ramp, so its
  // SettingInfo carries a getter/setter and no valuePtr, and the loop above
  // skips it. Without this line the margin silently resets to its default on
  // every boot -- the row still worked, which is what made it invisible.
  doc["screenMargin"] = screenMargin;
  // Same again: the justification threshold is a drop-down over a character
  // ramp, so its row carries a getter/setter and no valuePtr and the loop
  // above skips it. Stored as the character COUNT, never the picker index.
  doc["justifyThreshold"] = justifyThresholdChars;
  // lineSpacing: its settings row was deleted 2026-08-21 but the reader's
  // Confirm+side chord still steps it, so it persists by hand like screenMargin.
  doc["lineSpacing"] = lineSpacing;
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
  // GitHub token for Update Library — same manual free-text pattern.
  if (githubToken[0] != '\0') {
    doc["githubToken"] = githubToken;
  }
  // Editor font family by NAME. The SettingsList loop above has already written
  // the "editorFont" byte, and that stays: the web settings API reads and
  // writes the row through valuePtr, and an older build reads the byte. But it
  // is the NAME that fromJson believes, so a face removed or reordered later
  // cannot silently become a different face — the failure this key exists to
  // end. A literal from FAMILIES, so no copy: static storage duration.
  doc["editorFontFamily"] = editorfonts::selectedFamily(editorFont);
  // Editor font SIZE, in points. Written here rather than by the SettingsList
  // loop because its row has no valuePtr (it is a getter/setter row so the byte
  // stays a point size instead of a picker index), and that loop persists only
  // valuePtr rows -- a row with neither is silently dropped from settings.json.
  // Same reason fontSizeSlot and screenMargin appear by hand above.
  doc["editorFontSize"] = editorFontSize;

  // Language -- picked via the in-place option popup in SettingsActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";
}

void CrossPointSettings::normalizeRetiredSettings() {
  // Nearly this whole function retired itself on 2026-08-21: the settings it
  // pinned every load (hideBatteryPercentage, shortPwrBtn, longPress,
  // clockFormat, the two cover rows, systemFont, the reading-taste block) are
  // now `static constexpr` in CrossPointSettings.h -- hardcoded at exactly the
  // values the pins held, per the owner's "yes to all" on
  // docs/settings-reduction-plan.md. A pin was a promise the value could not
  // drift; the type system now keeps it.
  //
  // What remains is the one live field the owner named as the exception:
  // focusReadingEnabled stays a real (hidden, web-settable) row, pinned OFF so
  // a save written while its picker existed cannot hold it on forever.
  focusReadingEnabled = 0;

  // The three withdrawn calendar rows (owner ruling 2026-08-21: "keep calendar
  // and westside calendar"). Their enum values are frozen by persistence, so a
  // stale save is remapped to the classic calendar rather than left holding a
  // row the picker no longer offers. FIVE drew the identical screen already;
  // FOUR and SIX collapse to the nearest survivor.
  if (sleepScreen == CALENDAR_FOUR || sleepScreen == CALENDAR_FIVE || sleepScreen == CALENDAR_SIX) {
    sleepScreen = CALENDAR;
  }
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
        bool tooLong = false;
        const std::string decoded =
            obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong);
        if (tooLong) {
          LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", info.key);
          needsResave = true;
        }
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
        // enumCount(), not enumValues.size(): a runtime-labeled row keeps its
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

  // sleepTimeoutMinutes is hardcoded (10) since 2026-08-21; the legacy
  // "sleepTimeout" enum migration went with it.
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.

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
  // Justification threshold — same shape as the margin above, and read back as
  // a character COUNT. Unlike the margin it is NOT range-clamped but matched
  // against the offered ladder: the rungs are not a contiguous ramp, and a byte
  // between two of them (a hand edit, an API client, a file written when the
  // ladder differed) means a value nobody chose. clampThreshold sends those to
  // the documented default rather than snapping to a neighbour.
  justifyThresholdChars =
      static_cast<uint8_t>(autojustify::clampThreshold(doc["justifyThreshold"] | autojustify::THRESHOLD_CHARS));
  // lineSpacing: manual for the same reason as its toJson line -- row deleted,
  // chord lives. Clamped to the enum, defaulting NORMAL.
  const uint8_t storedLs = doc["lineSpacing"] | (uint8_t)NORMAL;
  lineSpacing = storedLs < LINE_COMPRESSION_COUNT ? storedLs : (uint8_t)NORMAL;
  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  // Editor font family — persisted by NAME since 2026-08-15, so that removing
  // or reordering a writing face can never re-point a saved value at a
  // different one. The generic loop above has already loaded the legacy
  // "editorFont" byte into the field and clamped it against the CURRENT
  // FAMILY_COUNT; both of those are undone here.
  const char* efn = doc["editorFontFamily"] | "";
  if (efn[0] != '\0') {
    const size_t idx = editorfonts::indexOfFamily(efn);
    // An unknown name means a family that has since been removed (or a bad web
    // API write). Fall back to the first row rather than to whatever the clamp
    // happened to leave behind.
    editorFont = idx < editorfonts::FAMILY_COUNT ? static_cast<uint8_t>(idx) : 0;
  } else {
    // No name key: written by a build that persisted a POSITION. The RAW byte
    // is read straight from the document because the ENUM clamp above already
    // replaced every out-of-range value — which is every value worth migrating
    // — with the default.
    editorFont = editorfonts::migrateLegacyStoredIndex(doc["editorFont"] | static_cast<uint8_t>(0));
    // Write the name back at once. That is what makes this a ONE-SHOT: after
    // the resave the key exists, this branch is unreachable for this device,
    // and the migration cannot re-apply the way its predecessor did.
    needsResave = true;
  }

  // Editor font size -- see the toJson note; a getter/setter row is not carried
  // by the generic loop. Snapped through nearestOfferedSize so a byte written
  // when a different set of sizes was offered lands on a real one rather than
  // being clamped away or resolving to a cut that is not compiled in.
  editorFontSize = editorfonts::nearestOfferedSize(doc["editorFontSize"] | static_cast<uint8_t>(12));

  // Owner name — not in SettingsList, load manually
  const char* own = doc["ownerName"] | "";
  strncpy(ownerName, own, sizeof(ownerName) - 1);
  ownerName[sizeof(ownerName) - 1] = '\0';
  // GitHub token for Update Library — free text like ownerName, load manually.
  // Never logged anywhere.
  const char* ghTok = doc["githubToken"] | "";
  strncpy(githubToken, ghTok, sizeof(githubToken) - 1);
  githubToken[sizeof(githubToken) - 1] = '\0';
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

  // The ligature spec is loaded above by the generic string path, but the font
  // layer holds its own parsed copy and cannot read this struct. Push it now,
  // before the first page is measured: a preference applied one render late
  // would show as a page that changes under the reader.
  //
  // Canonicalized on the way in so a hand-edited or API-written value is
  // stored the way the model spells it. The re-save is deliberate but NOT
  // requested here -- an unusual spelling costs nothing until the next
  // ordinary save, and requesting one on every boot that reads an unordered
  // file would write the card on every boot.
  const std::string canonical = ligatures::canonicalize(ligaturesOff);
  copyToField(ligaturesOff, canonical.c_str(), sizeof(ligaturesOff));
  applyLigaturePreference();

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
  spec.smallFontId = getSmallestReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  spec.imageRendering = imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  spec.lineGridEnabled = lineGridEnabled != 0;
  // Clamped on the way OUT as well as on the way in: the web settings API
  // writes the byte directly, so the store can hold a value the ladder never
  // offered. The spec is what the cache compares and what layout obeys, so it
  // is the last place worth being sure.
  spec.justifyThresholdChars = static_cast<uint8_t>(autojustify::clampThreshold(justifyThresholdChars));
  // Canonicalized inside fingerprint(), so a hand edit that only re-ordered
  // the spec produces the same word and does not repaginate the card. See
  // lib/EpdFont/LigatureControl.h.
  spec.ligatureFingerprint = ligatures::fingerprint(ligaturesEnabled != 0, ligaturesOff);
  return spec;
}

void CrossPointSettings::applyLigaturePreference() const {
  // Pushes the stored preference down to the font layer, which sits under this
  // struct and cannot read it. Called from fromJson() below and from the
  // Typography screen -- the two places the value can change.
  //
  // A fresh install calls NEITHER: PersistableStore::loadFromFile() returns
  // early when there is no settings.json, so fromJson never runs. That is why
  // ligatures:: defaults to "everything allowed" rather than to an unset
  // state -- it is already correct for the device nobody tests on.
  ligatures::configure(ligaturesEnabled != 0, ligaturesOff);
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

int CrossPointSettings::getSmallestReaderFontId() const {
  // SD families: ask the resolver for the smallest point size the family has,
  // walking up only until something resolves. Built-ins: 12 is the floor of
  // BUILTIN_READER_POINT_SIZES and always registered.
  //
  // MEASURED 2026-08-23: for an SD family this loop returns on its FIRST
  // iteration whatever size it asks for, because resolveFontId ignores
  // pointSize -- the manager holds exactly one reader-size cut, by design and
  // for RAM. So this returns the SAME id as getReaderFontId(), and the
  // wide-table step-down it exists for does nothing for any SD family, which
  // is every shipped configuration.
  //
  // Left as it is rather than quietly deleted: the built-in path below is real,
  // and making the SD path work means holding a second cut in RAM on a device
  // with 320 KB of it. That is a size-versus-feature call for the owner, not a
  // cleanup. Recorded in docs/ so it is a decision rather than a mystery.
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    for (const uint8_t pt : BUILTIN_READER_POINT_SIZES) {
      const int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, pt);
      if (id != 0) return id;
    }
  }
  return LIBREFRANKLIN_READER_12_FONT_ID;
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
