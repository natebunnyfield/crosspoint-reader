#pragma once

#include <GfxRenderer.h>

#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Render a biased quarter-hour offset as "UTC+5:45" / "UTC-12:00".
// Upstream keeps this next to the menu row that displays it, in
// StatusBarSettingsActivity.cpp; this fork has no status bar screen, so it lives
// with the picker that owns the encoding and the two cannot drift apart.
std::string formatUtcOffset(uint8_t biasedQ);

// Time zone picker for the clock. Presents a short flash-resident list of named
// zones, each bound to a fixed UTC offset in the same biased quarter-hour
// encoding SETTINGS.clockUtcOffsetQ has always used (48 = UTC+0). Confirm
// stores the highlighted zone's offset; Back leaves the stored value untouched.
//
// A stored offset that matches no list entry (written by older firmware or the
// web settings API, e.g. Nepal +5:45) is surfaced as an extra "Custom offset"
// row rather than discarded — arbitrary offsets remain settable via the web
// settings control, which edits the raw byte.
class ClockOffsetActivity final : public Activity {
 public:
  explicit ClockOffsetActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockOffset", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  int selectedIndex = 0;
  // TZ_COUNT named rows, plus one trailing "Custom offset" row when the stored
  // byte matches no named zone. Fixed for the lifetime of the activity.
  int itemCount = 0;
  // Set when the stored offset matches no named zone; the raw byte it carries
  // is what the custom row displays and preserves.
  bool hasCustomRow = false;
  uint8_t customOffsetQ = 48;

  void handleSelection();
};
