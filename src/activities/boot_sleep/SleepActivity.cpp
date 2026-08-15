#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Txt.h>
#include <Xtc.h>

#include "CalendarSleepScreen.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "HolidayCalculator.h"
#include "SleepScreenPolicy.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "util/DeviceId.h"

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      sleepscreen::shouldQuickResume(SETTINGS.sleepScreen, SETTINGS.quickResumeSleepScreen, fromTimeout);

  if (renderQuickResume) {
    // Deliberately BEFORE the polarity reset below: this path keeps whatever is
    // already on the panel and only adds a moon, so it must stay in the polarity
    // that page was drawn in. main.cpp re-applies the same mode before
    // re-displaying the saved frame on the next boot, so the two agree.
    return renderLastScreenSleepScreen();
  }

  // A DRAWN sleep screen is always normal polarity. It already carries its own
  // light/dark choice in SETTINGS.sleepScreen, and inverting on top of that
  // would make "Dark" render light. This frame also stays on the panel with the
  // device powered off, so it must not depend on a runtime flag to look right.
  display.setInverted(false);

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR):
      // The classic style keeps its original five-week look.
      return renderCalendarSleepScreen(5);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FOUR):
      return renderCalendarSleepScreen(4);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FIVE):
      return renderCalendarSleepScreen(5);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_SIX):
      return renderCalendarSleepScreen(6);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE):
      return renderCalendarSleepScreen(5, calendar::Style::WestsideEN);
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCalendarSleepScreen(const uint8_t weeks, const calendar::Style style) const {
  // The calendar needs a trustworthy wall clock. X3 has a DS3231; X4 does not,
  // and its internal RTC drifts badly across deep sleep (see SCOPE.md), so
  // fall back to the stock sleep image rather than showing a wrong date.
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getDateTime(year, month, day, hour, minute)) {
    LOG_INF("SLP", "CALENDAR: no RTC available, falling back to default sleep screen");
    return renderDefaultSleepScreen();
  }

  // The DS3231 holds UTC (HalClock::syncFromNTP configures "UTC0" and writes
  // gmtime), so shift to the local date before deciding which day to highlight.
  // Without this the calendar showed the UTC day: with a negative offset it read
  // one day AHEAD during the local evening, and it silently cancelled out the
  // separate RTC date bug for part of the day, which is what made that bug look
  // intermittent.
  const calendar::YMD today =
      calendar::localDateFromUtc(calendar::YMD{year, month, day}, hour, minute, SETTINGS.clockUtcOffsetQ);

  // Drawn fresh on every sleep entry straight into the framebuffer — no
  // intermediate /sleep.bmp, no staleness stamp, and nothing cached on the SD
  // card to go out of date. Note the PANEL itself still holds this image with no
  // power, so what you see on a sleeping device is the date as of the last sleep
  // entry, not the current date; there is no timer wake to refresh it (and on
  // battery the MCU is fully powered off, so there could not be).
  calendar::CalendarSleepScreen::render(renderer, today, weeks, style);

  // Same single-pass HALF waveform the other sleep screens use (see the note
  // above renderDefaultSleepScreen).
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderCustomSleepScreen() const {
  // Custom sleep image priority (device-specific files win so one SD-card tree
  // can serve every unit, each showing its own image):
  //   1. /sleep_<id>.bmp — <id> is this unit's DeviceId (last 3 factory-MAC
  //      bytes as hex, shown on the File Transfer screen), so two units of the
  //      same model stay distinct. Wins over /sleep.bmp set on-device via
  //      "Set as sleep screen".
  //   2. /sleep.bmp — generic fallback (the path BmpViewerActivity writes).
  //   3. /.sleep / /sleep directory rotation.
  //   4. Calendar — renderCalendarSleepScreen itself falls back to the default
  //      logo screen on a device without a trustworthy RTC (the X4).

  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");

  // Try device-specific sleep image first.
  char deviceId[8];
  getDeviceIdHex(deviceId, sizeof(deviceId));
  char deviceBmpPath[24];
  snprintf(deviceBmpPath, sizeof(deviceBmpPath), "/sleep_%s.bmp", deviceId);
  {
    HalFile devFile;
    if (Storage.openFileForRead("SLP", deviceBmpPath, devFile)) {
      Bitmap bitmap(devFile, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        LOG_DBG("SLP", "Loading: %s", deviceBmpPath);
        renderBitmapSleepScreen(bitmap);
        devFile.close();
        if (dir) dir.close();
        return;
      }
      devFile.close();
    }
  }

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      file.close();
      if (dir) dir.close();
      return;
    }
    file.close();
  }

  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    std::vector<std::string> files;
    char name[500];
    // collect all valid BMP files
    for (auto dirFile = dir.openNextFile(); dirFile; dirFile = dir.openNextFile()) {
      if (dirFile.isDirectory()) {
        dirFile.close();
        continue;
      }
      dirFile.getName(name, sizeof(name));
      auto filename = std::string(name);
      if (filename[0] == '.') {
        dirFile.close();
        continue;
      }

      if (!FsHelpers::hasBmpExtension(filename)) {
        LOG_DBG("SLP", "Skipping non-.bmp file name: %s", name);
        dirFile.close();
        continue;
      }
      Bitmap bitmap(dirFile);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        LOG_DBG("SLP", "Skipping invalid BMP file: %s", name);
        dirFile.close();
        continue;
      }
      files.emplace_back(filename);
      dirFile.close();
    }
    const auto numFiles = files.size();
    if (numFiles > 0) {
      // Pick a random wallpaper, excluding recently shown ones.
      // Window: up to SLEEP_RECENT_COUNT entries, capped at numFiles-1.
      const uint16_t fileCount = static_cast<uint16_t>(std::min(numFiles, static_cast<size_t>(UINT16_MAX)));
      const uint8_t window =
          static_cast<uint8_t>(std::min(static_cast<size_t>(APP_STATE.recentSleepFill), numFiles - 1));
      auto randomFileIndex = static_cast<uint16_t>(random(fileCount));
      for (uint8_t attempt = 0; attempt < 20 && APP_STATE.isRecentSleep(randomFileIndex, window); attempt++) {
        randomFileIndex = static_cast<uint16_t>(random(fileCount));
      }
      APP_STATE.pushRecentSleep(randomFileIndex);
      APP_STATE.saveToFile();
      const auto filename = std::string(sleepDir) + "/" + files[randomFileIndex];
      HalFile randFile;
      if (Storage.openFileForRead("SLP", filename, randFile)) {
        LOG_DBG("SLP", "Randomly loading: %s/%s", sleepDir, files[randomFileIndex].c_str());
        delay(100);
        Bitmap bitmap(randFile, true);
        if (bitmap.parseHeaders() == BmpReaderError::Ok) {
          renderBitmapSleepScreen(bitmap);
          randFile.close();
          dir.close();
          return;
        }
        randFile.close();
      }
    }
  }
  if (dir) dir.close();

  // No custom image anywhere on the card: show the calendar rather than the
  // logo. On the X4 renderCalendarSleepScreen refuses without an RTC and
  // paints the default screen itself.
  renderCalendarSleepScreen(5);
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  // Mark and owner only (owner ruling 2026-08-15, T-020). The wordmark and
  // "Sleeping" were removed: a sleep screen says whose device this is, and the
  // mark says what it is.
  if (SETTINGS.ownerName[0] != '\0') {
    const std::string owner = renderer.truncatedText(SMALL_FONT_ID, SETTINGS.ownerName, pageWidth - 40);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 60, owner.c_str());
  }

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      // COVER mode draws a generated typographic cover so the sleeping device
      // still names the open book; COVER_CUSTOM keeps the user's chosen
      // fallback image.
      if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER) {
        return renderGeneratedCoverSleepScreen(lastEpub.getTitle().c_str(), lastEpub.getAuthor().c_str());
      }
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderGeneratedCoverSleepScreen(const char* title, const char* author) const {
  renderer.clearScreen();
  const auto seed = static_cast<uint32_t>(std::hash<std::string>{}(APP_STATE.openEpubPath));
  GUI.drawGeneratedCover(renderer, Rect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight()), title, author,
                         seed);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  // The one thing a blank screen still says: whose device this is.
  if (SETTINGS.ownerName[0] != '\0') {
    const std::string owner = renderer.truncatedText(SMALL_FONT_ID, SETTINGS.ownerName, renderer.getScreenWidth() - 40);
    renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() - 60, owner.c_str());
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
