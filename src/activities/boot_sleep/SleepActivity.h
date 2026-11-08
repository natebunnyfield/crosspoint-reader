#pragma once
#include "activities/Activity.h"
#include "activities/boot_sleep/CalendarSleepScreen.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  // Full-screen generated cover (title/author + seeded band) for COVER mode
  // when the open book has no cover image to extract.
  void renderGeneratedCoverSleepScreen(const char* title, const char* author) const;
  // preserveBackground skips the clear, so the bitmap's white pixels leave the
  // retained screen showing (the Transparent sleep screen's plain-BMP path).
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  void renderTransparentCustomSleepScreen() const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  // CALENDAR: draw the holiday calendar straight into the framebuffer
  // and refresh. Nothing is written to the SD card. See CalendarSleepScreen.h.
  // `dark` inverts the finished frame; the definition says why that is the
  // whole of the dark rendition rather than a restyle.
  void renderCalendarSleepScreen(uint8_t weeks, calendar::Style style = calendar::Style::SpanishCR,
                                 bool dark = false) const;

  bool fromTimeout = false;
};
