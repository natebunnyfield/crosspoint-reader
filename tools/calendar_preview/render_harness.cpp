// Off-device render harness for CalendarSleepScreen.
//
// Links the REAL firmware GfxRenderer, EpdFont and compressed builtin fonts,
// renders the calendar into a host-side framebuffer, and dumps it as a 1-bit
// BMP for inspection.
//
// The BMP writer lives HERE and not in the firmware on purpose: the device
// draws the calendar straight to the panel, so serialisation is purely a
// development convenience. Keeping it out of src/ means the firmware carries
// no file format, no SD write and no cache to go stale.
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HalGPIO.h"
#include "activities/boot_sleep/CalendarSleepScreen.h"
#include "activities/boot_sleep/HolidayCalculator.h"
#include "fontIds.h"

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Global font objects, mirroring src/main.cpp — including ALL FOUR styles per
// family. Registering only regular+bold here would make italic silently fall
// back inside EpdFontFamily::getFont(), so the harness would render something
// the device does not.
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

}  // namespace

int main(int argc, char** argv) {
  int y = 2026, m = 7, d = 27;
  if (argc == 4) { y = atoi(argv[1]); m = atoi(argv[2]); d = atoi(argv[3]); }

  renderer.begin();
  // Builtin fonts are compressed; the decompressor must be wired in before
  // any drawText call (mirrors src/main.cpp).
  if (!fontDecompressor.init()) { fprintf(stderr, "decompressor init failed\n"); return 1; }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_12_FONT_ID, notoserif12Family);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, notoserif14Family);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, notoserif18Family);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14Family);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18Family);
  renderer.insertFont(SMALL_FONT_ID, smallFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12Family);

  printf("screen %dx%d  panel %ux%u\n", renderer.getScreenWidth(), renderer.getScreenHeight(),
         renderer.getDisplayWidth(), renderer.getDisplayHeight());

  calendar::CalendarSleepScreen::render(renderer, calendar::YMD{static_cast<uint16_t>(y),
                                                                static_cast<uint8_t>(m),
                                                                static_cast<uint8_t>(d)});
  system("mkdir -p fs_");
  if (!writeMonoPortraitBmp("./fs_/sleep.bmp", renderer)) return 1;
  printf("wrote ./fs_/sleep.bmp\n");
  return 0;
}
