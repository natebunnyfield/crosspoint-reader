// The Sleep Screen setting has to be the thing that decides the sleep screen.
//
// Reported as "sleep screen is not set from the System setting", and it is not a
// rendering bug: SleepActivity::onEnter never reaches the sleepScreen switch on
// the path that matters. Two settings overlap --
//
//   Sleep Screen           (SETTINGS.sleepScreen)             picks the image
//   Quick Resume on Timeout(SETTINGS.quickResumeSleepScreen)  overrides it when
//                                                             sleep came from an
//                                                             inactivity timeout
//
// -- and the override ships ON. An inactivity timeout is how a reader sleeps
// almost every time; a manual sleep is the exception. So out of the box the
// owner can pick Calendar, Custom or Blank, watch the row update, and never see
// it, because the only path they exercise returns before the switch.
//
// These tests are written against the policy rather than the activity: between
// them SleepActivity and SettingsActivity pull in Epub, Xtc, Txt, the clock, the
// calendar renderer and the theme stack, and the part that is wrong is three
// lines of decision.
//
// CrossPointSettings is compiled but its .cpp is not linked (same arrangement as
// test/activity_input), so SETTINGS here holds the real shipped field defaults
// and nothing has loaded a file over them. That matters: the first test below is
// about what a device does out of the box.

#include <gtest/gtest.h>

#include "CrossPointSettings.h"
#include "activities/boot_sleep/SleepScreenPolicy.h"

namespace {

constexpr uint8_t kQrOn = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
constexpr uint8_t kQrOff = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;

// Every mode that draws something of the owner's choosing. Quick Resume is
// excluded on purpose -- it is the one mode that IS the override.
constexpr uint8_t kChosenModes[] = {
    CrossPointSettings::SLEEP_SCREEN_MODE::DARK,
    CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT,
    CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM,
    CrossPointSettings::SLEEP_SCREEN_MODE::COVER,
    CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM,
    CrossPointSettings::SLEEP_SCREEN_MODE::BLANK,
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR,
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FOUR,
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FIVE,
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_SIX,
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE,
};

const char* modeName(const uint8_t m) {
  switch (m) {
    case CrossPointSettings::SLEEP_SCREEN_MODE::DARK:
      return "Dark";
    case CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT:
      return "Light";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM:
      return "Custom";
    case CrossPointSettings::SLEEP_SCREEN_MODE::COVER:
      return "Cover";
    case CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM:
      return "Cover+Custom";
    case CrossPointSettings::SLEEP_SCREEN_MODE::BLANK:
      return "Blank";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR:
      return "Calendar";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FOUR:
      return "Calendar Four";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FIVE:
      return "Calendar Five";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_SIX:
      return "Calendar Six";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE:
      return "Calendar Westside";
    default:
      return "?";
  }
}

}  // namespace

// --- The bug, stated as the owner sees it -----------------------------------

// Out of the box, on the path a reader actually takes to sleep.
// Eight reconcile()/initialState() tests were deleted 2026-08-21 with the
// quickResumeSleepScreen setting and its auto-enable state machine
// (docs/settings-reduction-plan.md). shouldQuickResume -- the half that still
// runs, deciding what a sleep DRAWS -- keeps its coverage below.
TEST(SleepScreenPolicy, FactoryDefaultsDrawTheChosenScreenOnATimeoutSleep) {
  const uint8_t asShipped = SETTINGS.quickResumeSleepScreen;

  for (const uint8_t mode : kChosenModes) {
    EXPECT_FALSE(sleepscreen::shouldQuickResume(mode, asShipped, /*fromTimeout=*/true))
        << modeName(mode) << " never draws on a fresh device: an inactivity-timeout sleep shows the last screen "
        << "instead, because Quick Resume on Timeout ships enabled and is checked first";
  }
}

// A device that already carries the shipped default in its settings.json is not
// helped by changing that default, so picking a screen has to stand the override
// down as well.

// Opening Settings must not be mistaken for the owner asking for the override.
//
// This is the half that kept the fix above from reaching a real device.
// SettingsActivity::onEnter seeded "the owner turned this on" from whatever was
// stored, so on any device carrying the shipped default -- which is every
// device that never touched the row -- a value nobody chose was treated as an
// explicit preference, and it then outranked the sleep screen the owner did
// choose. Entering the screen is also not itself a pick, so the consistency
// pass onEnter runs must change nothing on its own.

// ...but a toggle inside the session does count, and survives a later pick.

// --- What must keep working -------------------------------------------------

// The override is a real feature; an owner who asks for it keeps it, even if
// they then change which screen a manual sleep draws.

// Toggling the row itself records the owner's word, in both directions.

// Picking the Quick Resume MODE implies the timeout row -- otherwise the mode
// would only apply to manual sleeps, which is not what it means.

// The Quick Resume mode applies to every sleep, not only timeouts.
TEST(SleepScreenPolicy, QuickResumeModeAppliesToManualSleepToo) {
  EXPECT_TRUE(sleepscreen::shouldQuickResume(CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME, kQrOff,
                                             /*fromTimeout=*/false));
  EXPECT_TRUE(sleepscreen::shouldQuickResume(CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME, kQrOff,
                                             /*fromTimeout=*/true));
}

// With the override deliberately on, a MANUAL sleep still draws the chosen
// screen -- the row is scoped to timeouts and must stay that way.
TEST(SleepScreenPolicy, TheOverrideIsScopedToTimeoutSleeps) {
  for (const uint8_t mode : kChosenModes) {
    EXPECT_FALSE(sleepscreen::shouldQuickResume(mode, kQrOn, /*fromTimeout=*/false))
        << modeName(mode) << " was overridden on a manual sleep, which the timeout row must not affect";
  }
}

// Moving off Quick Resume, having only ever had the row auto-enabled, puts it
// back. This is the behaviour that already worked and must survive the fix.
