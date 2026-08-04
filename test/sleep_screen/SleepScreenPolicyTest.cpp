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
TEST(SleepScreenPolicy, PickingAScreenStandsDownTheTimeoutOverride) {
  for (const uint8_t mode : kChosenModes) {
    const sleepscreen::QuickResumeState before{kQrOn, /*autoEnabled=*/false, /*preserveOn=*/false};

    const auto after = sleepscreen::reconcile(mode, before, /*sleepScreenChanged=*/true,
                                              /*quickResumeChanged=*/false);

    EXPECT_EQ(after.quickResumeOnTimeout, kQrOff)
        << "picked " << modeName(mode) << " and the timeout override stayed on, so the choice still never draws";
    EXPECT_FALSE(sleepscreen::shouldQuickResume(mode, after.quickResumeOnTimeout, /*fromTimeout=*/true))
        << modeName(mode) << " still overridden after being picked";
  }
}

// Opening Settings must not be mistaken for the owner asking for the override.
//
// This is the half that kept the fix above from reaching a real device.
// SettingsActivity::onEnter seeded "the owner turned this on" from whatever was
// stored, so on any device carrying the shipped default -- which is every
// device that never touched the row -- a value nobody chose was treated as an
// explicit preference, and it then outranked the sleep screen the owner did
// choose. Entering the screen is also not itself a pick, so the consistency
// pass onEnter runs must change nothing on its own.
TEST(SleepScreenPolicy, OpeningSettingsIsNotAnOwnerOptIn) {
  auto s = sleepscreen::initialState(kQrOn);
  EXPECT_FALSE(s.preserveOn) << "the stored value was read as an explicit opt-in";

  s = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::DARK, s, /*sleepScreenChanged=*/false,
                             /*quickResumeChanged=*/false);
  EXPECT_EQ(s.quickResumeOnTimeout, kQrOn) << "merely opening Settings changed a stored setting";

  s = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR, s, /*sleepScreenChanged=*/true,
                             /*quickResumeChanged=*/false);
  EXPECT_EQ(s.quickResumeOnTimeout, kQrOff) << "picking a screen still did not stand the override down";
}

// ...but a toggle inside the session does count, and survives a later pick.
TEST(SleepScreenPolicy, AnOptInWithinTheSessionOutranksALaterPick) {
  auto s = sleepscreen::initialState(kQrOff);
  s.quickResumeOnTimeout = kQrOn;  // owner toggles the row on
  s = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::DARK, s, /*sleepScreenChanged=*/false,
                             /*quickResumeChanged=*/true);
  ASSERT_TRUE(s.preserveOn);

  s = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR, s, /*sleepScreenChanged=*/true,
                             /*quickResumeChanged=*/false);
  EXPECT_EQ(s.quickResumeOnTimeout, kQrOn) << "an opt-in made this session was overwritten";
}

// --- What must keep working -------------------------------------------------

// The override is a real feature; an owner who asks for it keeps it, even if
// they then change which screen a manual sleep draws.
TEST(SleepScreenPolicy, AnOwnerWhoTurnedTheOverrideOnKeepsIt) {
  const sleepscreen::QuickResumeState theyTurnedItOn{kQrOn, /*autoEnabled=*/false, /*preserveOn=*/true};

  const auto after = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR, theyTurnedItOn,
                                            /*sleepScreenChanged=*/true, /*quickResumeChanged=*/false);

  EXPECT_EQ(after.quickResumeOnTimeout, kQrOn) << "an explicit opt-in was overwritten by a sleep-screen change";
}

// Toggling the row itself records the owner's word, in both directions.
TEST(SleepScreenPolicy, TogglingTheRowRecordsIntent) {
  const auto turnedOn = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR,
                                               {kQrOn, /*autoEnabled=*/true, /*preserveOn=*/false},
                                               /*sleepScreenChanged=*/false, /*quickResumeChanged=*/true);
  EXPECT_TRUE(turnedOn.preserveOn);
  EXPECT_FALSE(turnedOn.autoEnabled);
  EXPECT_EQ(turnedOn.quickResumeOnTimeout, kQrOn) << "toggling the row on must not be undone by the reconcile";

  const auto turnedOff = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR,
                                                {kQrOff, /*autoEnabled=*/false, /*preserveOn=*/true},
                                                /*sleepScreenChanged=*/false, /*quickResumeChanged=*/true);
  EXPECT_FALSE(turnedOff.preserveOn);
  EXPECT_EQ(turnedOff.quickResumeOnTimeout, kQrOff);
}

// Picking the Quick Resume MODE implies the timeout row -- otherwise the mode
// would only apply to manual sleeps, which is not what it means.
TEST(SleepScreenPolicy, ChoosingQuickResumeModeEnablesTheTimeoutRow) {
  const auto after = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME,
                                            {kQrOff, /*autoEnabled=*/false, /*preserveOn=*/false},
                                            /*sleepScreenChanged=*/true, /*quickResumeChanged=*/false);

  EXPECT_EQ(after.quickResumeOnTimeout, kQrOn);
  EXPECT_TRUE(after.autoEnabled) << "enabling it on the owner's behalf must be remembered as such, so moving off "
                                    "Quick Resume can put it back";
  EXPECT_TRUE(sleepscreen::shouldQuickResume(CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME,
                                             after.quickResumeOnTimeout, /*fromTimeout=*/false));
}

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
TEST(SleepScreenPolicy, LeavingQuickResumeModeUndoesTheAutoEnable) {
  const auto enabled = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME,
                                              {kQrOff, false, false}, /*sleepScreenChanged=*/true,
                                              /*quickResumeChanged=*/false);
  ASSERT_EQ(enabled.quickResumeOnTimeout, kQrOn);

  const auto after = sleepscreen::reconcile(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR, enabled,
                                            /*sleepScreenChanged=*/true, /*quickResumeChanged=*/false);
  EXPECT_EQ(after.quickResumeOnTimeout, kQrOff);
  EXPECT_FALSE(after.autoEnabled);
}
