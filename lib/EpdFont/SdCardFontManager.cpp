#include "SdCardFontManager.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <SdCardFontRegistry.h>

#if CROSSPOINT_RENDER_SCALE > 1
#include <HalStorage.h>

namespace {
// Hi-res companion path for a 1x .cpfont: the SAME filename inside a
// "<RENDER_SCALE>x" subdirectory of the family folder, e.g.
//   /fonts/Coelacanth/Coelacanth_19.cpfont        (1x, rasterised at 19 pt)
//   /fonts/Coelacanth/2x/Coelacanth_19.cpfont     (same face at 38 pt)
//
// Why there and not a new root or a new family directory:
//   * SdCardFontRegistry::scanDirectory() skips subdirectories outright
//     (SdCardFontRegistry.cpp:120-123), so the companion is INVISIBLE to
//     discovery -- it cannot show up in the font picker, cannot be selected,
//     and cannot shift SETTINGS.sdFontFamilyName.
//   * It travels with the family: copying /fonts/Coelacanth to a phone brings
//     the hi-res set along, and it works from either scan root.
//   * The filename keeps the 1x POINT SIZE, so the companion for a loaded font
//     is a pure string transform -- no second registry, no size matching.
// The point size in the name is a label only; nothing in the .cpfont header
// records ppem (SdCardFont::load reads magic/version/styleCount/TOC and no
// size), so a 2x-rasterised file named _19 loads fine and simply carries
// bigger glyphs and a bigger advanceY -- neither of which layout ever reads,
// because the companion is registered for GLYPH BLITTING ONLY.
std::string hiResCompanionPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return {};
  return path.substr(0, slash + 1) + std::to_string(cp::renderScale()) + "x/" + path.substr(slash + 1);
}
}  // namespace
#endif

SdCardFontManager::~SdCardFontManager() {
  for (auto& lf : loaded_) {
    delete lf.font;
  }
#if CROSSPOINT_RENDER_SCALE > 1
  for (auto* f : hiResFonts_) delete f;
#endif
}

// FNV-1a continuation: seeds with contentHash, then hashes family name + point size.
// Produces a deterministic ID that is stable across load/unload cycles and reboots,
// and changes when font content changes (different header/TOC = different contentHash).
int SdCardFontManager::computeFontId(uint32_t contentHash, const char* familyName, uint8_t pointSize) {
  static constexpr uint32_t FNV_PRIME = 16777619u;
  uint32_t hash = contentHash;
  while (*familyName) {
    hash ^= static_cast<uint8_t>(*familyName++);
    hash *= FNV_PRIME;
  }
  hash ^= pointSize;
  hash *= FNV_PRIME;
  int id = static_cast<int>(hash);
  return id != 0 ? id : 1;  // 0 is reserved as "not found" sentinel
}

int SdCardFontManager::loadFile(const SdCardFontFileInfo& file, const char* familyName, GfxRenderer& renderer) {
  auto* font = new (std::nothrow) SdCardFont();
  if (!font) {
    LOG_ERR("SDMGR", "Failed to allocate SdCardFont for %s", file.path.c_str());
    return 0;
  }

  if (!font->load(file.path.c_str())) {
    LOG_ERR("SDMGR", "Failed to load %s", file.path.c_str());
    delete font;
    return 0;
  }

  int fontId = computeFontId(font->contentHash(), familyName, file.pointSize);
  // Guard against collision with built-in font IDs (astronomically unlikely
  // with FNV-1a hashes, but provides a safety net)
  if (renderer.getFontMap().count(fontId) != 0) {
    LOG_ERR("SDMGR", "Font ID %d collides with existing font, skipping %s", fontId, file.path.c_str());
    delete font;
    return 0;
  }
  renderer.registerSdCardFont(fontId, font);
  loaded_.push_back({font, fontId, file.pointSize});

  LOG_DBG("SDMGR", "Loaded %s size=%u id=%d styles=%u", file.path.c_str(), file.pointSize, fontId, font->styleCount());

  EpdFontFamily fontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3));
  renderer.insertFont(fontId, fontFamily);

#if CROSSPOINT_RENDER_SCALE > 1
  // Optional hi-res companion. Absent = the 1x glyph replicated, i.e. exactly
  // what shipped before. Note this is registered under the SAME fontId, so the
  // section cache key (which is that id) is unaffected and pagination cannot
  // move because a companion appeared or disappeared.
  //
  // Skipped outright at an active scale of 1, where the 1x face already matches
  // the framebuffer's density and there is no `1x/` directory to find. Without
  // this the ceiling-3 binary asks for `<family>/1x/<file>` on every load and
  // reports each miss through the INF line below -- true, useless, and exactly
  // the kind of noise that trains people to ignore that line when it matters.
  const std::string hiResPath = cp::renderScale() > 1 ? hiResCompanionPath(file.path) : std::string();
  if (cp::renderScale() <= 1) {
    // Nothing to do: no companion is the correct state at 1x.
  } else if (hiResPath.empty()) {
    LOG_ERR("SDMGR", "No directory component in %s - cannot form a hi-res path", file.path.c_str());
  } else if (!Storage.exists(hiResPath.c_str())) {
    // SAY SO. A missing companion is a supported fallback rather than a
    // failure -- the 1x glyph replicated, exactly what shipped before hi-res
    // existed -- and this branch used to be absent entirely, which made it the
    // only outcome here that produced no output at all.
    //
    // That silence was the real defect. The companion is found by exact
    // filename and the filename carries the point size, so a set can be
    // complete for one size slot and missing for the next: the reader changes
    // size in Reader Font, the page quietly renders at half the resolution
    // the build asked for, and nothing says why. The fault also lives on the
    // CARD rather than in the code -- install-sim-fonts.py cannot regenerate
    // these and prunes orphans whenever a size ramp changes -- so without this
    // line a report blames the renderer for a file deleted three commits ago.
    //
    // INF not DBG so it survives a release LOG_LEVEL; INF not ERR because a
    // family may legitimately ship no 2x set at all. Only compiled where
    // CROSSPOINT_RENDER_SCALE > 1, i.e. never on device.
    LOG_INF("SDMGR", "No hi-res companion %s - %u pt renders 1x-replicated", hiResPath.c_str(), file.pointSize);
  } else {
    auto* hiRes = new (std::nothrow) SdCardFont();
    if (!hiRes) {
      LOG_ERR("SDMGR", "OOM: hi-res SdCardFont for %s", hiResPath.c_str());
    } else if (!hiRes->load(hiResPath.c_str())) {
      LOG_ERR("SDMGR", "Failed to load hi-res %s", hiResPath.c_str());
      delete hiRes;
    } else {
      hiResFonts_.push_back(hiRes);
      renderer.registerHiResFont(
          fontId, hiRes,
          EpdFontFamily(hiRes->getEpdFont(0), hiRes->getEpdFont(1), hiRes->getEpdFont(2), hiRes->getEpdFont(3)));
      LOG_DBG("SDMGR", "Loaded hi-res %s for id=%d", hiResPath.c_str(), fontId);
    }
  }
#endif
  return fontId;
}

bool SdCardFontManager::loadFamily(const SdCardFontFamilyInfo& family, GfxRenderer& renderer, uint8_t pointSize) {
  // Unload any previously loaded family first
  if (!loadedFamilyName_.empty()) {
    unloadAll(renderer);
  }

  const SdCardFontFileInfo* selected = family.findNearestSize(pointSize);
  if (!selected) {
    LOG_ERR("SDMGR", "Family %s has no files to load", family.name.c_str());
    return false;
  }

  if (loadFile(*selected, family.name.c_str(), renderer) == 0) {
    return false;
  }

  loadedFamilyName_ = family.name;
  loadedPointSize_ = selected->pointSize;
  return true;
}

int SdCardFontManager::loadFamilyExtraSize(const SdCardFontFamilyInfo& family, GfxRenderer& renderer,
                                           uint8_t pointSize) {
  const SdCardFontFileInfo* file = family.findFile(pointSize);
  if (!file) return 0;  // family has no .cpfont at this exact size

  // Reuse an already-loaded font of the same size (e.g. when a reader size
  // happens to match a UI size) instead of double-loading the file.
  for (const auto& lf : loaded_) {
    if (lf.size == pointSize) return lf.fontId;
  }

  return loadFile(*file, family.name.c_str(), renderer);
}

void SdCardFontManager::unloadAll(GfxRenderer& renderer) {
  // Drop UI CJK fallbacks before the SD fonts they point at are freed.
  renderer.clearFallbackFonts();
  renderer.clearSdCardFonts();
  for (auto& lf : loaded_) {
    renderer.removeFont(lf.fontId);  // also drops any hi-res companion registration
    delete lf.font;
  }
  loaded_.clear();
#if CROSSPOINT_RENDER_SCALE > 1
  for (auto* f : hiResFonts_) delete f;
  hiResFonts_.clear();
#endif
  loadedFamilyName_.clear();
  loadedPointSize_ = 0;
}

int SdCardFontManager::getFontId(const std::string& familyName) const {
  if (familyName != loadedFamilyName_ || loaded_.empty()) return 0;
  return loaded_.front().fontId;
}
