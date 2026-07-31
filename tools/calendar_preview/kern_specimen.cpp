// Off-device kerning specimen harness.
//
// Renders the same text through two installed SD font families side by side,
// using the REAL firmware GfxRenderer / SdCardFont / EpdFont path — so the
// glyph advances, the class-based kern lookup and the fp4 differential
// rounding in GfxRenderer.cpp:521-526 are the ones the device runs, not a
// reimplementation. Kerning changes are single-pixel effects at 12-18pt; a
// host-side approximation cannot be trusted to show them.
//
// Usage:
//   CPFONT_DIR=/.fonts ./kern_specimen FamilyA FamilyB
//   -> ./fs_/kern_specimen.bmp
//
// Both families must be installed under ./fs_<CPFONT_DIR>/<Family>/.
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontSystem.h>
#include <builtinFonts/all.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HalGPIO.h"
#include "fontIds.h"

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// The UI font is only used for the row labels, so one small family is enough.
EpdFont ui12Regular(&ubuntu_12_regular), ui12Bold(&ubuntu_12_bold);
EpdFontFamily ui12Family(&ui12Regular, &ui12Bold);

extern SdCardFontSystem sdFontSystem;

namespace {

constexpr uint8_t kPointSizes[] = {12, 14, 16, 18};

const char* kSamples[] = {
    "Fjord waffle",
    "Tree Try Trust Yttrium",
    "backdrop Kydd Wry Vamp",
    "AVATAR Wavy Yak Toy",
};

bool logicalPixelIsWhite(const uint8_t* fb, int panelHeight, int panelWidthBytes, int lx, int ly) {
  const int px = ly;
  const int py = (panelHeight - 1) - lx;
  return (fb[py * panelWidthBytes + (px >> 3)] >> (7 - (px & 7))) & 0x1;
}

// Same 1-bit portrait BMP writer as render_harness.cpp. Duplicated rather than
// shared because render_harness.cpp is the calendar's harness and this file
// must not perturb it.
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
  hdr[22] = H & 0xFF;  hdr[23] = (H >> 8) & 0xFF;
  hdr[26] = 1; hdr[28] = 1;
  hdr[46] = 2;
  hdr[58] = hdr[59] = hdr[60] = 0xFF;

  FILE* f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); return false; }
  fwrite(hdr, 1, HEADER, f);

  const uint8_t* fb = r.getFrameBuffer();
  auto* row = static_cast<uint8_t*>(calloc(1, rowBytes));
  for (int y = H - 1; y >= 0; --y) {
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

}  // namespace

int main(int argc, char** argv) {
  const char* famA = argc > 1 ? argv[1] : "RosarivoV1";
  const char* famB = argc > 2 ? argv[2] : "Rosarivo";

  renderer.begin();
  if (!fontDecompressor.init()) { fprintf(stderr, "decompressor init failed\n"); return 1; }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(UI_12_FONT_ID, ui12Family);
  renderer.clearScreen(0xFF);

  const int margin = 10;
  system("mkdir -p fs_");

  // One page per point size: four sizes of two families would overflow the
  // 792px panel and silently clip.
  for (uint8_t sizeEnum = 0; sizeEnum < 4; ++sizeEnum) {
    const int idA = sdFontSystem.loadForDisplay(famA, sizeEnum, renderer);
    const int idB = sdFontSystem.loadForDisplay(famB, sizeEnum, renderer);
    if (idA == 0 || idB == 0) {
      fprintf(stderr, "missing family at size %u (%s=%d %s=%d)\n",
              kPointSizes[sizeEnum], famA, idA, famB, idB);
      return 1;
    }
    auto itA = renderer.getSdCardFonts().find(idA);
    auto itB = renderer.getSdCardFonts().find(idB);
    if (itA == renderer.getSdCardFonts().end() || itB == renderer.getSdCardFonts().end()) {
      fprintf(stderr, "fonts not registered at size %u\n", kPointSizes[sizeEnum]);
      return 1;
    }

    renderer.clearScreen(0xFF);
    int y = 6;
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

    char path[64];
    snprintf(path, sizeof(path), "./fs_/kern_specimen_%u.bmp", kPointSizes[sizeEnum]);
    if (!writeMonoPortraitBmp(path, renderer)) return 1;
    printf("wrote %s\n", path);
  }
  return 0;
}
