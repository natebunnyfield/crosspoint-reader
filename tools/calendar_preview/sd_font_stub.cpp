// Host stub for the SD card font system.
//
// The firmware's SdCardFontSystem needs directory iteration, the settings
// store and the serialisation layer — far more of the HAL than this harness
// stubs. The calendar only ever calls loadForDisplay(), so that is all this
// provides: it loads a .cpfont straight off disk with the real SdCardFont
// loader, exercising the same glyph path the device uses.
//
// CPFONT_DIR points at a font tree (default ./fs_/.fonts). Set it empty to
// simulate "family not installed" and exercise the Noto Serif fallback.
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontManager.h>
#include <SdCardFontSystem.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

SdCardFontSystem sdFontSystem;

namespace {
// Reader size enum -> point size, matching the standard 12/14/16/18 pack.
uint8_t pointSizeFor(uint8_t sizeEnum) {
  static const uint8_t kSizes[] = {12, 14, 16, 18};
  return kSizes[sizeEnum < 4 ? sizeEnum : 3];
}
}  // namespace

int SdCardFontSystem::loadForDisplay(const char* familyName, uint8_t fontSizeEnum,
                                     GfxRenderer& renderer) {
  const char* root = getenv("CPFONT_DIR");
  if (root == nullptr) root = "./fs_/.fonts";
  if (root[0] == '\0' || familyName == nullptr || familyName[0] == '\0') return 0;

  char path[512];
  snprintf(path, sizeof(path), "%s/%s/%s_%u.cpfont", root, familyName, familyName,
           pointSizeFor(fontSizeEnum));
  // SdCardFont::load() goes through the stub HalStorage, which sandboxes SD
  // paths under ./fs_ — so existence must be checked against that same prefix
  // while the device-style path is what gets passed in.
  const std::string hostPath = std::string("./fs_") + path;
  struct stat st;
  if (stat(hostPath.c_str(), &st) != 0) {
    fprintf(stderr, "[stub] not installed: %s\n", hostPath.c_str());
    return 0;
  }

  static int nextId = 900000;  // outside the builtin font-ID space
  auto* font = new SdCardFont();
  if (!font->load(path)) {
    fprintf(stderr, "[stub] load failed: %s\n", path);
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
  fprintf(stderr, "[stub] loaded %s as id %d (%u styles)\n", path, id, font->styleCount());
  return id;
}

// Unused by the calendar, but required for linking.
void SdCardFontSystem::begin(GfxRenderer&) {}
void SdCardFontSystem::ensureLoaded(GfxRenderer&) {}
int SdCardFontSystem::resolveFontId(const char*, uint8_t) const { return 0; }

// SdCardFontSystem owns an SdCardFontManager by value, so its destructor is
// referenced even though this stub never loads through the manager.
SdCardFontManager::~SdCardFontManager() = default;
