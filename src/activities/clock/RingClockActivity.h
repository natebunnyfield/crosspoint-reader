#pragma once
#include "activities/Activity.h"

// Full-screen segmented ring clock (port of the CoreInk "simple ring clock,
// inverted"). White-on-black; updates when the displayed value changes
// (5-minute minute-ring granularity). Requires the X3's DS3231 RTC.
class RingClockActivity final : public Activity {
  bool rtcAvailable = false;
  // Last displayed tuple; render() draws exactly this state.
  int shownHour12 = -1;
  int shownMinuteRing = -2;
  int shownMonth = -1;
  int shownDay = -1;
  uint8_t rendersSinceFullRefresh = 0;

  bool readDisplayState(int& hour12, int& minuteRing, int& month, int& day) const;

 public:
  explicit RingClockActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("RingClock", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // An always-on clock face is the point; the user exits with Back. Only
  // while actually showing the clock — the no-RTC fallback may sleep.
  bool preventAutoSleep() override { return rtcAvailable; }
};
