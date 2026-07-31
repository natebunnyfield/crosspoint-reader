// CrossPoint off-device simulator.
//
// One binary, two modes, both driving the REAL firmware GfxRenderer / EpdFont /
// SdCardFont against a host-side framebuffer:
//
//   sim calendar [YYYY MM DD]      -> fs_/sleep.bmp
//   sim fonts    FAMILY_A FAMILY_B -> fs_/kern_specimen_{12,14,16,18}.bmp
//   sim YYYY MM DD                 -> legacy calendar form (sweep.py, check_centering.py)
//
// Supersedes render_harness.cpp + kern_specimen.cpp, which were two mains
// duplicating the same framebuffer setup and BMP writer.
//
// The BMP writer lives HERE and not in the firmware on purpose: the device
// draws straight to the panel, so serialisation is purely a development
// convenience. Keeping it out of src/ means the firmware carries no file
// format, no SD write and no cache to go stale.
//
// PORTABILITY: renderCalendar() and renderFontSpecimen() take every output
// path as an argument and touch no globals beyond the renderer singletons, so a
// GUI host (an iOS app target, say) can link this translation unit with
// SIM_NO_MAIN defined and call them directly. Keep it that way — no getenv, no
// cwd-relative literals, and no argv parsing below the main() boundary.
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontSystem.h>
#include <builtinFonts/all.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "HalGPIO.h"
#include "activities/boot_sleep/CalendarSleepScreen.h"
#include "activities/boot_sleep/HolidayCalculator.h"
#include "fontIds.h"

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

extern SdCardFontSystem sdFontSystem;

// Global font objects, mirroring src/main.cpp — including ALL FOUR styles per
// family. Registering only regular+bold here would make italic silently fall
// back inside EpdFontFamily::getFont(), so the sim would render something the
// device does not.
EpdFont notoserif12R(&notoserif_12_regular), notoserif12B(&notoserif_12_bold),
    notoserif12I(&notoserif_12_italic), notoserif12BI(&notoserif_12_bolditalic);
EpdFontFamily notoserif12Family(&notoserif12R, &notoserif12B, &notoserif12I, &notoserif12BI);
EpdFont notoserif14R(&notoserif_14_regular), notoserif14B(&notoserif_14_bold),
    notoserif14I(&notoserif_14_italic), notoserif14BI(&notoserif_14_bolditalic);
EpdFontFamily notoserif14Family(&notoserif14R, &notoserif14B, &notoserif14I, &notoserif14BI);
EpdFont notoserif18R(&notoserif_18_regular), notoserif18B(&notoserif_18_bold),
    notoserif18I(&notoserif_18_italic), notoserif18BI(&notoserif_18_bolditalic);
EpdFontFamily notoserif18Family(&notoserif18R, &notoserif18B, &notoserif18I, &notoserif18BI);

EpdFont notosans14R(&notosans_14_regular), notosans14B(&notosans_14_bold),
    notosans14I(&notosans_14_italic), notosans14BI(&notosans_14_bolditalic);
EpdFontFamily notosans14Family(&notosans14R, &notosans14B, &notosans14I, &notosans14BI);
EpdFont notosans18R(&notosans_18_regular), notosans18B(&notosans_18_bold),
    notosans18I(&notosans_18_italic), notosans18BI(&notosans_18_bolditalic);
EpdFontFamily notosans18Family(&notosans18R, &notosans18B, &notosans18I, &notosans18BI);

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFamily(&smallFont);
EpdFont ui12Regular(&ubuntu_12_regular), ui12Bold(&ubuntu_12_bold);
EpdFontFamily ui12Family(&ui12Regular, &ui12Bold);

namespace {

constexpr uint8_t kPointSizes[] = {12, 14, 16, 18};

const char* kSamples[] = {
    "Fjord waffle",
    "Tree Try Trust Yttrium",
    "backdrop Kydd Wry Vamp",
    "AVATAR Wavy Yak Toy",
};

// Read one logical (Portrait) pixel out of the physical landscape framebuffer.
// GfxRenderer::drawPixel rotates Portrait coords with (rotateCoordinates,
// GfxRenderer.cpp:187): phyX = y, phyY = panelHeight - 1 - x, writing 1bpp
// MSB-first where a SET bit is white. This inverts that mapping.
bool logicalPixelIsWhite(const uint8_t* fb, int panelHeight, int panelWidthBytes, int lx, int ly) {
  const int px = ly;
  const int py = (panelHeight - 1) - lx;
  return (fb[py * panelWidthBytes + (px >> 3)] >> (7 - (px & 7))) & 0x1;
}

bool writeMonoPortraitBmp(const char* path, const GfxRenderer& r) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
  const int panelH = r.getDisplayHeight();
  const int panelWB = r.getDisplayWidthBytes();
  const int rowBytes = ((W + 31) / 32) * 4;
  const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes) * H;
  constexpr int HEADER = 14 + 40 + 8;
  const uint32_t fileSize = HEADER + pixelBytes;

  uint8_t hdr[HEADER] = {0};
  hdr[0] = 'B'; hdr[1] = 'M';
  hdr[2] = fileSize & 0xFF;         hdr[3] = (fileSize >> 8) & 0xFF;
  hdr[4] = (fileSize >> 16) & 0xFF; hdr[5] = (fileSize >> 24) & 0xFF;
  hdr[10] = HEADER & 0xFF;          hdr[11] = (HEADER >> 8) & 0xFF;
  hdr[14] = 40;
  hdr[18] = W & 0xFF;  hdr[19] = (W >> 8) & 0xFF;
  hdr[22] = H & 0xFF;  hdr[23] = (H >> 8) & 0xFF;   // positive => bottom-up
  hdr[26] = 1; hdr[28] = 1;                          // 1 plane, 1 bpp
  hdr[46] = 2;                                       // biClrUsed
  hdr[58] = hdr[59] = hdr[60] = 0xFF;                // palette[1] = white

  FILE* f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
  fwrite(hdr, 1, HEADER, f);

  const uint8_t* fb = r.getFrameBuffer();
  auto* row = static_cast<uint8_t*>(calloc(1, rowBytes));
  for (int y = H - 1; y >= 0; --y) {           // BMP rows are bottom-up
    memset(row, 0, rowBytes);
    for (int x = 0; x < W; ++x) {
      if (logicalPixelIsWhite(fb, panelH, panelWB, x, y)) row[x >> 3] |= (0x80 >> (x & 7));
    }
    fwrite(row, 1, rowBytes, f);
  }
  free(row);
  fclose(f);
  return true;
}

// mkdir(2) rather than system("mkdir -p"): no shell, and no process spawn —
// which a sandboxed GUI host would not be allowed to do anyway.
void ensureOutDir(const char* dir) { mkdir(dir, 0755); }

}  // namespace

// Wire up the renderer and every builtin font family. Must run before any
// drawText call; the builtin fonts are compressed, so the decompressor has to
// be attached first (mirrors src/main.cpp).
bool simInit() {
  renderer.begin();
  if (!fontDecompressor.init()) { fprintf(stderr, "decompressor init failed\n"); return false; }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12Family);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14Family);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18Family);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14Family);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18Family);
  renderer.insertFont(SMALL_FONT_ID, smallFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12Family);
  return true;
}

bool renderCalendar(int year, int month, int day, const char* outPath) {
  calendar::CalendarSleepScreen::render(renderer, calendar::YMD{static_cast<uint16_t>(year),
                                                                static_cast<uint8_t>(month),
                                                                static_cast<uint8_t>(day)});
  if (!writeMonoPortraitBmp(outPath, renderer)) return false;
  printf("wrote %s\n", outPath);
  return true;
}

// One page per point size: four sizes of two families would overflow the 792px
// panel and silently clip.
bool renderFontSpecimen(const char* famA, const char* famB, const char* outDir) {
  for (uint8_t sizeEnum = 0; sizeEnum < 4; ++sizeEnum) {
    const int idA = sdFontSystem.loadForDisplay(famA, sizeEnum, renderer);
    const int idB = sdFontSystem.loadForDisplay(famB, sizeEnum, renderer);
    if (idA == 0 || idB == 0) {
      fprintf(stderr, "missing family at size %u (%s=%d %s=%d)\n",
              kPointSizes[sizeEnum], famA, idA, famB, idB);
      return false;
    }
    auto itA = renderer.getSdCardFonts().find(idA);
    auto itB = renderer.getSdCardFonts().find(idB);
    if (itA == renderer.getSdCardFonts().end() || itB == renderer.getSdCardFonts().end()) {
      fprintf(stderr, "fonts not registered at size %u\n", kPointSizes[sizeEnum]);
      return false;
    }

    renderer.clearScreen(0xFF);
    int y = 6;
    const int margin = 10;
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "%upt  --  %s (top) vs %s (bottom)",
             kPointSizes[sizeEnum], famA, famB);
    renderer.drawText(UI_12_FONT_ID, margin, y, hdr, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

    for (const char* sample : kSamples) {
      // Kerning is NOT resident until the page is prewarmed: SdCardFont keeps
      // only a per-page mini kern matrix (SdCardFont.h:179), built at
      // SdCardFont.cpp:956 from prewarmStyle. Measuring straight after load()
      // silently reports an UNKERNED font. Each prewarm replaces the previous
      // page's tables, so it has to happen per sample, immediately before use,
      // and metadataOnly must stay false — that path skips the mini kern.
      itA->second->prewarm(sample, 0x0F, /*metadataOnly=*/false);
      const int wA = renderer.getTextWidth(idA, sample);
      renderer.drawText(idA, margin, y, sample);
      char note[32];
      snprintf(note, sizeof(note), "%dpx", wA);
      renderer.drawText(UI_12_FONT_ID, renderer.getScreenWidth() - 52, y, note);
      y += renderer.getLineHeight(idA);

      itB->second->prewarm(sample, 0x0F, /*metadataOnly=*/false);
      const int wB = renderer.getTextWidth(idB, sample);
      renderer.drawText(idB, margin, y, sample);
      snprintf(note, sizeof(note), "%dpx%s", wB, wA == wB ? "" : " *");
      renderer.drawText(UI_12_FONT_ID, renderer.getScreenWidth() - 52, y, note);
      y += renderer.getLineHeight(idB) + 10;

      printf("%2upt  %-26s  %s=%3dpx  %s=%3dpx  delta=%+d\n",
             kPointSizes[sizeEnum], sample, famA, wA, famB, wB, wB - wA);
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/kern_specimen_%u.bmp", outDir, kPointSizes[sizeEnum]);
    if (!writeMonoPortraitBmp(path, renderer)) return false;
    printf("wrote %s\n", path);
  }
  return true;
}

#ifndef SIM_NO_MAIN

namespace {

int usage(const char* argv0) {
  fprintf(stderr,
          "CrossPoint off-device simulator\n\n"
          "  %s calendar [YYYY MM DD]        render the sleep screen -> fs_/sleep.bmp\n"
          "  %s fonts FAMILY_A FAMILY_B      A-B two SD font families -> fs_/kern_specimen_*.bmp\n"
          "  %s YYYY MM DD                   legacy calendar form\n\n"
          "fonts mode reads CPFONT_DIR (default /.fonts), a DEVICE-style path;\n"
          "the stub HalStorage prefixes ./fs_ to it.\n",
          argv0, argv0, argv0);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (!simInit()) return 1;
  ensureOutDir("./fs_");

  printf("screen %dx%d  panel %ux%u\n", renderer.getScreenWidth(), renderer.getScreenHeight(),
         renderer.getDisplayWidth(), renderer.getDisplayHeight());

  const char* mode = argc > 1 ? argv[1] : "calendar";

  if (strcmp(mode, "fonts") == 0) {
    if (argc != 4) return usage(argv[0]);
    return renderFontSpecimen(argv[2], argv[3], "./fs_") ? 0 : 1;
  }

  // "calendar [Y M D]", bare "calendar", and the legacy bare "Y M D" that
  // sweep.py and check_centering.py drive.
  int y = 2026, m = 7, d = 27;
  if (strcmp(mode, "calendar") == 0) {
    if (argc == 5) { y = atoi(argv[2]); m = atoi(argv[3]); d = atoi(argv[4]); }
    else if (argc != 2) return usage(argv[0]);
  } else if (argc == 4 && atoi(argv[1]) > 1000) {
    y = atoi(argv[1]); m = atoi(argv[2]); d = atoi(argv[3]);
  } else {
    return usage(argv[0]);
  }

  return renderCalendar(y, m, d, "./fs_/sleep.bmp") ? 0 : 1;
}

#endif  // SIM_NO_MAIN
