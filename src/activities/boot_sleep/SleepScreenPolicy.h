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

// The Settings screen's bookkeeping for the overlap above.
//
// autoEnabled: this row was switched on by the code, because the owner picked
//   the Quick Resume sleep screen -- not because they asked for it directly.
// preserveOn: the owner turned this row on themselves, in this settings
//   session. Their word beats any inference.
struct QuickResumeState {
  uint8_t quickResumeOnTimeout;
  bool autoEnabled = false;
  bool preserveOn = false;

  bool operator==(const QuickResumeState&) const = default;
};

// The state the Settings screen starts a session with, given what is stored.
//
// preserveOn starts FALSE however the row is currently set. It means "the owner
// asked for this, in this session", and a stored value cannot tell you that:
// the row shipped on, so on any device that never touched it the stored value
// is a default nobody chose. Seeding preserveOn from it -- which is what this
// used to do -- let that default outrank the sleep screen the owner did choose,
// and was why standing the override down never reached a real device.
inline QuickResumeState initialState(const uint8_t quickResumeOnTimeout) {
  return {quickResumeOnTimeout, /*autoEnabled=*/false, /*preserveOn=*/false};
}

// Recompute the state after one of the two rows changed.
inline QuickResumeState reconcile(const uint8_t sleepScreen, QuickResumeState s, const bool sleepScreenChanged,
                                  const bool quickResumeChanged) {
  constexpr uint8_t kOn = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  constexpr uint8_t kOff = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;

  if (quickResumeChanged) {
    s.preserveOn = s.quickResumeOnTimeout == kOn;
    s.autoEnabled = false;
  }

  // Picking the Quick Resume sleep screen implies the timeout row: without it
  // the mode would only apply to manual sleeps.
  if (sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (s.quickResumeOnTimeout != kOn) {
      s.quickResumeOnTimeout = kOn;
      s.autoEnabled = !s.preserveOn;
    } else if (sleepScreenChanged && !s.preserveOn) {
      s.autoEnabled = true;
    }
    return s;
  }

  // Picking any other sleep screen is a request to SEE it, so stand the override
  // down -- unless the owner turned it on themselves, whose word beats this.
  //
  // This used to require s.autoEnabled, i.e. it only undid an override the code
  // had switched on itself. That left out the case that mattered most: the row
  // shipped ON, so on the overwhelming majority of devices it was neither
  // auto-enabled nor owner-enabled, and nothing here ever stood it down. The
  // owner picked a sleep screen and it silently never drew.
  if (sleepScreenChanged && !s.preserveOn) {
    s.quickResumeOnTimeout = kOff;
    s.autoEnabled = false;
  }
  return s;
}

}  // namespace sleepscreen
