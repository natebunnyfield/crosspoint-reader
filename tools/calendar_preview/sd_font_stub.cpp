// Host stub for the SD card font system.
//
// The firmware's SdCardFontSystem needs directory iteration, the settings
// store and the serialisation layer — far more of the HAL than this harness
// stubs. The calendar only ever calls loadForDisplay(), so that is all this
// provides: it loads a .cpfont straight off disk with the real SdCardFont
// loader, exercising the same glyph path the device uses.
//
// CPFONT_DIR pins ONE device-style root (e.g. /.fonts); unset searches both
// /.fonts and /fonts the way SdCardFontRegistry does. Set it empty to
// simulate "family not installed" and exercise the Noto Serif fallback.
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontSystem.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <vector>

SdCardFontSystem sdFontSystem;

namespace {

// Nominal point size per reader slot. NOT the filename: a family may ship any
// four sizes, and the device picks the nearest installed one
// (SdCardFontManager.cpp:68, family.findNearestSize). Venetian301 ships
// 15/17/19/21, Inknut 10/16, Coelacanth 14/20 — assuming a 12/14/16/18 ladder
// here made every such family fail to load.
uint8_t nominalPointSize(uint8_t sizeEnum) {
  static const uint8_t kSizes[] = {12, 14, 16, 18};
  return kSizes[sizeEnum < 4 ? sizeEnum : 3];
}

// The registry scans TWO roots and dedupes by family name, hidden first
// (SdCardFontRegistry.h FONTS_DIR_HIDDEN / FONTS_DIR_VISIBLE). The installed
// set routinely straddles both — locally-sourced families in /.fonts, the
// rebuildable ones in /fonts — so a stub that looked in one root only saw a
// fraction of what the device sees.
const char* const kRoots[] = {"/.fonts", "/fonts"};

// Nearest installed size for a family, scanning <Family>_<size>.cpfont.
// Returns 0 if the directory holds no parseable sizes.
uint8_t nearestInstalledSize(const std::string& hostDir, const char* familyName, uint8_t want) {
  DIR* d = opendir(hostDir.c_str());
  if (d == nullptr) return 0;
  const std::string prefix = std::string(familyName) + "_";
  uint8_t best = 0;
  int bestDelta = 1 << 30;
  while (dirent* e = readdir(d)) {
    const std::string n = e->d_name;
    if (n.compare(0, prefix.size(), prefix) != 0) continue;
    if (n.size() < prefix.size() + 1 || n.rfind(".cpfont") == std::string::npos) continue;
    const int size = atoi(n.c_str() + prefix.size());
    if (size <= 0) continue;
    const int delta = abs(size - static_cast<int>(want));
    // Ties go to the smaller size, matching a stable nearest-match.
    if (delta < bestDelta || (delta == bestDelta && size < best)) {
      bestDelta = delta;
      best = static_cast<uint8_t>(size);
    }
  }
  closedir(d);
  return best;
}

}  // namespace

int SdCardFontSystem::loadForDisplay(const char* familyName, uint8_t fontSizeEnum,
                                     GfxRenderer& renderer) {
  if (familyName == nullptr || familyName[0] == '\0') return 0;

  // CPFONT_DIR pins a single device-style root (e.g. "/.fonts"); unset means
  // search both the way the registry does. Empty means "family not installed",
  // which is how the calendar's Noto fallback gets exercised.
  const char* pinned = getenv("CPFONT_DIR");
  if (pinned != nullptr && pinned[0] == '\0') return 0;

  const uint8_t want = nominalPointSize(fontSizeEnum);
  std::string path;
  for (const char* root : (pinned != nullptr ? std::vector<const char*>{pinned}
                                             : std::vector<const char*>{kRoots[0], kRoots[1]})) {
    // SdCardFont::load() goes through the stub HalStorage, which sandboxes SD
    // paths under ./fs_ — so the scan must use that prefix while the
    // device-style path is what gets passed in.
    const std::string hostDir = std::string("./fs_") + root + "/" + familyName;
    const uint8_t size = nearestInstalledSize(hostDir, familyName, want);
    if (size == 0) continue;
    char buf[512];
    snprintf(buf, sizeof(buf), "%s/%s/%s_%u.cpfont", root, familyName, familyName, size);
    path = buf;
    if (size != want) {
      fprintf(stderr, "[stub] %s: no %upt, using nearest %upt\n", familyName, want, size);
    }
    break;
  }
  if (path.empty()) {
    fprintf(stderr, "[stub] not installed: %s (searched %s)\n", familyName,
            pinned != nullptr ? pinned : "/.fonts and /fonts");
    return 0;
  }

  static int nextId = 900000;  // outside the builtin font-ID space
  auto* font = new SdCardFont();
  if (!font->load(path.c_str())) {
    fprintf(stderr, "[stub] load failed: %s\n", path.c_str());
    delete font;
    return 0;
  }
  const int id = nextId++;
  // Mirror SdCardFontManager::loadFamily: an SD font must be BOTH registered
  // (for the glyph-miss/advance-table path) AND inserted into the flash font
  // map as an EpdFontFamily wrapping its four styles, which is what
  // getFontAscenderSize/getGlyph/drawText look up.
  renderer.registerSdCardFont(id, font);
  EpdFontFamily family(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2),
                       font->getEpdFont(3));
  renderer.insertFont(id, family);
  fprintf(stderr, "[stub] loaded %s as id %d (%u styles)\n", path.c_str(), id, font->styleCount());
  return id;
}

// Unused by the calendar, but required for linking.
void SdCardFontSystem::begin(GfxRenderer&) {}
void SdCardFontSystem::ensureLoaded(GfxRenderer&) {}
int SdCardFontSystem::resolveFontId(const char*, uint8_t) const { return 0; }

// SdCardFontSystem owns an SdCardFontManager by value, so its destructor is
// referenced even though this stub never loads through the manager.
SdCardFontManager::~SdCardFontManager() = default;
