#pragma once

#include <cstdint>

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

}  // namespace sleepscreen
