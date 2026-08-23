#pragma once

#include <cstdint>

#include "CalendarSleepScreen.h"
#include "CrossPointSettings.h"

// Which screen a sleep draws, and how the two settings that decide it are kept
// consistent.
//
// Pulled out of SleepActivity::onEnter and SettingsActivity so it can be tested
// without linking either: between them they pull in Epub, Xtc, Txt, the clock,
// the calendar renderer and the whole theme stack, and the part worth testing is
// three lines of policy.
//
// The two settings overlap by construction, which is the whole difficulty:
//   * SETTINGS.sleepScreen picks the image (Dark, Custom, Cover, Calendar...),
//     and one of its modes IS Quick Resume.
//   * SETTINGS.quickResumeSleepScreen says "on an inactivity timeout, show the
//     last screen instead" -- which silently overrides the choice above on the
//     path that fires far more often than any manual sleep.
namespace sleepscreen {

// True when this sleep draws the last screen rather than the configured sleep
// screen. Mirrors the first decision in SleepActivity::onEnter.
inline bool shouldQuickResume(const uint8_t sleepScreen, const uint8_t quickResumeOnTimeout, const bool fromTimeout) {
  return sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
         (fromTimeout &&
          quickResumeOnTimeout == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
}

// QuickResumeState, initialState() and reconcile() were deleted 2026-08-21
// with the quickResumeSleepScreen setting they reconciled: the timeout path is
// hardcoded OFF (docs/settings-reduction-plan.md), so there is no auto-enable
// dance left to model. shouldQuickResume above stays -- the QUICK_RESUME sleep
// SCREEN mode is still a live picker option and main.cpp / SleepActivity still
// route through it.

// Which calendar a sleep-screen mode draws, and in which polarity.
//
// Every field here is decoded from ONE persisted integer, which is the shape
// that fails silently: a value pointed at the wrong style or the wrong polarity
// still compiles, still draws a calendar, and is wrong only to the owner who
// knows which one they picked. Kept out of the switch in SleepActivity::onEnter
// for the same reason shouldQuickResume is -- the activity drags in the clock,
// the SD font system and the renderer, and the part worth testing is the map.
struct CalendarPlan {
  bool isCalendar = false;
  calendar::Style style = calendar::Style::SpanishCR;
  // Dark is a whole-frame invert of the finished page, applied by the caller.
  // See SleepActivity::renderCalendarSleepScreen for why that is the whole of
  // it rather than a restyle.
  bool dark = false;
};

inline CalendarPlan calendarPlanFor(const uint8_t sleepScreen) {
  using M = CrossPointSettings::SLEEP_SCREEN_MODE;
  switch (sleepScreen) {
    case M::CALENDAR:
    // The week-count variants were withdrawn 2026-08-21 ("keep calendar and
    // westside calendar"); normalizeRetiredSettings() remaps stale saves, and
    // these fold onto the classic screen so a value that slips through anyway
    // still draws something sensible rather than the default logo.
    case M::CALENDAR_FOUR:
    case M::CALENDAR_FIVE:
    case M::CALENDAR_SIX:
      return {true, calendar::Style::SpanishCR, false};
    case M::CALENDAR_DARK:
      return {true, calendar::Style::SpanishCR, true};
    case M::CALENDAR_WESTSIDE:
      return {true, calendar::Style::WestsideEN, false};
    case M::CALENDAR_WESTSIDE_DARK:
      return {true, calendar::Style::WestsideEN, true};
    default:
      return {};
  }
}

}  // namespace sleepscreen
