#include "SdCardFontSystem.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <iterator>

#include "CrossPointSettings.h"
#include "ReaderFontSizes.h"
#include "fontIds.h"

namespace {

// Refresh the DERIVED point size for the slot the owner selected. Deliberately
// does NOT write SPIFFS: fontSizeSlot is the persisted truth and has not changed,
// so there is nothing new to save. This replaced a snap that rewrote and SAVED
// fontPointSize on every family switch — that write destroyed the owner's
// original choice, so switching Lora -> Venetian301 -> Lora landed on a
// different size than it started at, permanently.
void resolveReaderPointSize(const uint8_t availablePointSize) {
  if (availablePointSize == 0 || availablePointSize == SETTINGS.fontPointSize) return;
  LOG_DBG("SDFS", "Slot %u resolves to %u pt (was %u)", SETTINGS.fontSizeSlot, availablePointSize,
          SETTINGS.fontPointSize);
  SETTINGS.fontPointSize = availablePointSize;
  // Persist the refreshed mirror. Unlike the snap this replaced, this cannot
  // destroy the owner's choice — fontSizeSlot is the truth and is untouched — but
  // "fontSize" is what pre-slot firmware reads if the card is moved to an older
  // build, and a stale mirror there would select the wrong size. The value-change
  // guard above keeps this to real changes only (Resource Protocol 8).
  SETTINGS.saveToFile();
}

// Built-in UI fonts and their physical point sizes (at 150 DPI, matching the
// SD-font converter). Each is paired with a same-size SD fallback so CJK UI
// text matches the surrounding Latin. See SdCardFontSystem::setupUiFallbacks.
struct UiFontSize {
  int fontId;
  uint8_t pointSize;
};
constexpr UiFontSize kUiFontSizes[] = {
    {SMALL_FONT_ID, 8},
    {UI_10_FONT_ID, 10},
    {UI_12_FONT_ID, 12},
};

}  // namespace

void SdCardFontSystem::begin(GfxRenderer& renderer) {
  registry_.discover();

  // One-time migration from the pre-slot format, done here because it is the
  // first point at which the registry can tell us the active family's ramp.
  // The stored point size is matched against that ramp to recover the slot the
  // owner was actually on.
  if (SETTINGS.fontSlotNeedsMigration) {
    const auto sizes = readerFontPointSizes(&registry_, SETTINGS.sdFontFamilyName);
    SETTINGS.fontSizeSlot = slotForPointSize(sizes, SETTINGS.fontPointSize);
    SETTINGS.fontSlotNeedsMigration = false;
    LOG_INF("SDFS", "Migrated font size %u pt -> slot %u", SETTINGS.fontPointSize, SETTINGS.fontSizeSlot);
    SETTINGS.saveToFile();
  }

  // Register this system as the SD font ID resolver in settings.
  // Uses a static trampoline since CrossPointSettings stores a plain function pointer.
  SETTINGS.sdFontIdResolver = [](void* ctx, const char* familyName, uint8_t pointSize) -> int {
    return static_cast<SdCardFontSystem*>(ctx)->resolveFontId(familyName, pointSize);
  };
  SETTINGS.sdFontResolverCtx = this;

  // If user has a saved SD font selection, load it
  if (SETTINGS.sdFontFamilyName[0] != '\0') {
    const auto* family = registry_.findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const auto* slotFile = family->findClosestReaderSize(SETTINGS.fontSizeSlot);
      if (manager_.loadFamily(*family, renderer, slotFile ? slotFile->pointSize : SETTINGS.fontPointSize)) {
        resolveReaderPointSize(manager_.currentPointSize());
        setupUiFallbacks(renderer);
        LOG_DBG("SDFS", "Loaded SD card font family: %s", SETTINGS.sdFontFamilyName);
      } else {
        LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", SETTINGS.sdFontFamilyName);
        SETTINGS.clearSdFontFamily();
      }
    } else {
      LOG_DBG("SDFS", "SD font family not found on card: %s (clearing)", SETTINGS.sdFontFamilyName);
      SETTINGS.clearSdFontFamily();
    }
  }

  LOG_DBG("SDFS", "SD font system ready (%d families discovered)", registry_.getFamilyCount());
}

void SdCardFontSystem::ensureLoaded(GfxRenderer& renderer) {
  // If the web server (or another task) installed/deleted fonts, re-discover.
  // Track whether we just re-discovered so we can force a reload below even
  // when the wanted family/size still maps to the same point size — the file
  // contents on disk may have changed (e.g. user re-uploaded a new build).
  const bool registryWasDirty = registryDirty_.exchange(false, std::memory_order_acquire);
  if (registryWasDirty) {
    LOG_DBG("SDFS", "Registry dirty — re-discovering fonts");
    registry_.discover();
  }

  const char* wantedFamily = SETTINGS.sdFontFamilyName;
  const std::string& currentFamily = manager_.currentFamilyName();

  if (wantedFamily[0] == '\0') {
    if (!currentFamily.empty()) {
      manager_.unloadAll(renderer);
    }
    // Back on a built-in family: resolve the same slot against the built-in ramp.
    resolveReaderPointSize(
        pointSizeForSlot(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), SETTINGS.fontSizeSlot));
    return;
  }

  // Reload if family changed OR if the user-selected size maps to a
  // different file than what's currently loaded OR if the registry was
  // just rediscovered (file may have been replaced on disk).
  bool familyMatches = (currentFamily == wantedFamily);
  if (familyMatches) {
    const auto* family = registry_.findFamily(wantedFamily);
    if (!family) {
      LOG_DBG("SDFS", "SD font family disappeared: %s (clearing)", wantedFamily);
      manager_.unloadAll(renderer);
      SETTINGS.clearSdFontFamily();
      return;
    }
    const auto* selected = family->findClosestReaderSize(SETTINGS.fontSizeSlot);
    const uint8_t wantedPt = selected ? selected->pointSize : 0;
    // Resolve before the early return: the wanted size can already be loaded
    // while fontPointSize still holds the previous family's resolution.
    resolveReaderPointSize(wantedPt);
    if (!registryWasDirty && wantedPt == manager_.currentPointSize()) return;
    LOG_DBG("SDFS", "Reloading %s: size %u -> %u%s", wantedFamily, manager_.currentPointSize(), wantedPt,
            registryWasDirty ? " [registry dirty]" : "");
  }

  if (!currentFamily.empty()) {
    manager_.unloadAll(renderer);
  }

  const auto* family = registry_.findFamily(wantedFamily);
  if (family) {
    const auto* slotFile = family->findClosestReaderSize(SETTINGS.fontSizeSlot);
    if (manager_.loadFamily(*family, renderer, slotFile ? slotFile->pointSize : SETTINGS.fontPointSize)) {
      resolveReaderPointSize(manager_.currentPointSize());
      setupUiFallbacks(renderer);
      LOG_DBG("SDFS", "Loaded SD font family: %s", wantedFamily);
    } else {
      LOG_ERR("SDFS", "Failed to load SD font family: %s (clearing)", wantedFamily);
      SETTINGS.clearSdFontFamily();
    }
  } else {
    LOG_DBG("SDFS", "SD font family not found: %s (clearing)", wantedFamily);
    SETTINGS.clearSdFontFamily();
  }
}

void SdCardFontSystem::setupUiFallbacks(GfxRenderer& renderer) {
  const std::string& familyName = manager_.currentFamilyName();
  if (familyName.empty()) return;  // no SD family loaded — nothing to fall back to

  const auto* family = registry_.findFamily(familyName);
  if (!family) return;

  // Probe the already-loaded reader-size font before paying for the UI sizes:
  // resolveTextFontId only redirects on CJK codepoints, so a Latin-only family
  // can never act as a fallback and its UI sizes would be dead weight in RAM.
  const auto readerIt = renderer.getFontMap().find(manager_.getFontId(familyName));
  if (readerIt == renderer.getFontMap().end()) return;
  // One representative codepoint per script: Han, Hiragana, Katakana, Hangul.
  static constexpr uint32_t kCjkProbes[] = {0x4E00, 0x3042, 0x30A2, 0xAC00};
  bool hasCjk = false;
  for (const uint32_t cp : kCjkProbes) {
    if (readerIt->second.hasCodepoint(cp)) {
      hasCjk = true;
      break;
    }
  }
  if (!hasCjk) {
    LOG_DBG("SDFS", "%s has no CJK coverage - skipping UI fallback sizes", familyName.c_str());
    return;
  }

  for (const auto& ui : kUiFontSizes) {
    const int sdFontId = manager_.loadFamilyExtraSize(*family, renderer, ui.pointSize);
    if (sdFontId != 0) {
      renderer.setFallbackFont(ui.fontId, sdFontId);
    } else {
      LOG_DBG("SDFS", "No %u pt SD glyphs for UI fallback in %s", ui.pointSize, familyName.c_str());
    }
  }
}

// Make `familyName` resident for a non-reader consumer (the calendar sleep
// screen) at (closest to) `pointSize`. Reuses the resident reader font when the
// size already matches, and piggybacks on the extra-size path when only the
// size differs, so the reader font is not evicted needlessly. When a different
// family must be loaded, the reader font IS evicted — safe only because sleep
// is a deep sleep and setup() rebuilds font state on wake.
//
// getFontId matches on family NAME only, so an unconditional reuse would
// return whatever size happened to be loaded. The caller that hurts is
// CalendarSleepScreen, which asks for 18pt: if the user's reader font was the
// same family at any other size, the sleep screen silently got the reader's
// size instead, and computeLayout() then derived titleH, gridTop, rowH and the
// highlight square from the wrong metrics. No log line, no fallback: a
// whole-screen proportion regression.
int SdCardFontSystem::loadForDisplay(const char* familyName, uint8_t pointSize, GfxRenderer& renderer) {
  if (familyName == nullptr || familyName[0] == '\0') return 0;
  refreshIfDirty();

  const auto* family = registry_.findFamily(familyName);
  if (family == nullptr) return 0;  // not installed; caller falls back

  const auto* wanted = family->findNearestSize(pointSize);
  if (wanted == nullptr) return 0;

  if (const int existing = manager_.getFontId(familyName); existing != 0) {
    if (wanted->pointSize == manager_.currentPointSize()) {
      return existing;
    }
    // Same family resident at another size: load the wanted size as an extra
    // size, which shares the manager without unloading the reader font.
    if (const int extra = manager_.loadFamilyExtraSize(*family, renderer, wanted->pointSize); extra != 0) {
      return extra;
    }
    LOG_DBG("SDFS", "Resident %s is %upt but %upt wanted - reloading", familyName, manager_.currentPointSize(),
            wanted->pointSize);
  }

  if (!manager_.currentFamilyName().empty()) {
    manager_.unloadAll(renderer);
  }
  if (!manager_.loadFamily(*family, renderer, wanted->pointSize)) {
    LOG_ERR("SDFS", "loadForDisplay failed to load: %s", familyName);
    return 0;
  }
  LOG_DBG("SDFS", "Loaded %s for display use (%upt)", familyName, wanted->pointSize);
  return manager_.getFontId(familyName);
}

int SdCardFontSystem::resolveFontId(const char* familyName, uint8_t /*pointSize*/) const {
  // The manager holds exactly one reader-size font, already selected for
  // SETTINGS.fontPointSize, so the size argument is implicit — always return
  // that font's ID. ensureLoaded() must have run for the current settings first.
  return manager_.getFontId(familyName);
}
