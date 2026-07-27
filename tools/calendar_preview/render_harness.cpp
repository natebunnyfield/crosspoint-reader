#include <GfxRenderer.h>
#include <FontDecompressor.h>
#include <FontCacheManager.h>
#include "HalGPIO.h"
#include <builtinFonts/all.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fontIds.h"
#include "activities/boot_sleep/CalendarSleepScreen.h"
#include "activities/boot_sleep/HolidayCalculator.h"

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

// Same global font objects main.cpp creates on device.
EpdFont notosans14Regular(&notosans_14_regular);
EpdFont notosans14Bold(&notosans_14_bold);
EpdFontFamily notosans14Family(&notosans14Regular, &notosans14Bold);
EpdFont notosans18Regular(&notosans_18_regular);
EpdFont notosans18Bold(&notosans_18_bold);
EpdFontFamily notosans18Family(&notosans18Regular, &notosans18Bold);
EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFamily(&smallFont);
EpdFont ui12Regular(&ubuntu_12_regular);
EpdFont ui12Bold(&ubuntu_12_bold);
EpdFontFamily ui12Family(&ui12Regular, &ui12Bold);

int main(int argc, char** argv) {
  int y = 2026, m = 7, d = 27;
  if (argc == 4) { y = atoi(argv[1]); m = atoi(argv[2]); d = atoi(argv[3]); }

  renderer.begin();
  // Mirror main.cpp's font pipeline setup — builtin fonts are compressed, so
  // the decompressor must be wired in before any drawText call.
  if (!fontDecompressor.init()) { fprintf(stderr, "decompressor init failed\n"); return 1; }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSANS_14_FONT_ID, notosans14Family);
  renderer.insertFont(NOTOSANS_18_FONT_ID, notosans18Family);
  renderer.insertFont(SMALL_FONT_ID, smallFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12Family);

  printf("screen %dx%d  panel %ux%u  widthBytes %u\n",
         renderer.getScreenWidth(), renderer.getScreenHeight(),
         renderer.getDisplayWidth(), renderer.getDisplayHeight(),
         renderer.getDisplayWidthBytes());
  printf("NOTOSANS_18 ascender=%d lineHeight=%d\n",
         renderer.getFontAscenderSize(NOTOSANS_18_FONT_ID),
         renderer.getLineHeight(NOTOSANS_18_FONT_ID));

  const calendar::YMD today{(uint16_t)y, (uint8_t)m, (uint8_t)d};
  calendar::CalendarSleepScreen::render(renderer, today);
  if (!calendar::CalendarSleepScreen::serialiseFramebufferAsPortraitBmp(renderer, "/sleep.bmp")) {
    fprintf(stderr, "serialise FAILED\n");
    return 1;
  }
  printf("wrote ./fs_/sleep.bmp\n");
  return 0;
}
