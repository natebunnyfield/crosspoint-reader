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

#include <utility>

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
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_DARK,
    CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE_DARK,
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
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_DARK:
      return "Calendar (dark)";
    case CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE_DARK:
      return "Calendar Westside (dark)";
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

// --- Which calendar, in which polarity --------------------------------------

// calendarPlanFor decodes ONE persisted integer into three answers, and every
// way it can be wrong is silent: the panel still shows a calendar, just not the
// one that was picked, and nobody is awake to see it happen. These pin the map
// rather than the rendering.

// The four rows the picker offers each reach a different screen. Stated as a
// distinctness property, because the failure that matters is two rows landing
// on the same (style, polarity) pair -- which is exactly what CALENDAR_FIVE
// turned out to be against CALENDAR, and why it was withdrawn.
TEST(SleepScreenPolicy, EveryOfferedCalendarRowDrawsADifferentScreen) {
  constexpr uint8_t kOffered[] = {
      CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR,
      CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_DARK,
      CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE,
      CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE_DARK,
  };
  for (const uint8_t a : kOffered) {
    const auto pa = sleepscreen::calendarPlanFor(a);
    EXPECT_TRUE(pa.isCalendar) << modeName(a) << " is an offered calendar row and must draw one";
    for (const uint8_t b : kOffered) {
      if (a == b) continue;
      const auto pb = sleepscreen::calendarPlanFor(b);
      EXPECT_FALSE(pa.style == pb.style && pa.dark == pb.dark)
          << modeName(a) << " and " << modeName(b) << " draw the identical screen, so one of the two rows is a lie";
    }
  }
}

// The stored value each row means. Written out one by one rather than derived,
// because a persisted integer re-pointed at the other style or the other
// polarity is precisely what this must catch, and a formula would move with it.
TEST(SleepScreenPolicy, StoredValuesKeepTheirMeaning) {
  const auto cr = sleepscreen::calendarPlanFor(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR);
  EXPECT_EQ(cr.style, calendar::Style::SpanishCR);
  EXPECT_FALSE(cr.dark);

  const auto crDark = sleepscreen::calendarPlanFor(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_DARK);
  EXPECT_EQ(crDark.style, calendar::Style::SpanishCR) << "the dark row must draw the SAME calendar, inverted";
  EXPECT_TRUE(crDark.dark);

  const auto ws = sleepscreen::calendarPlanFor(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE);
  EXPECT_EQ(ws.style, calendar::Style::WestsideEN);
  EXPECT_FALSE(ws.dark);

  const auto wsDark = sleepscreen::calendarPlanFor(CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE_DARK);
  EXPECT_EQ(wsDark.style, calendar::Style::WestsideEN) << "the dark row must draw the SAME calendar, inverted";
  EXPECT_TRUE(wsDark.dark);
}

// A dark row is its light row plus the invert, and nothing else. If a future
// change makes dark mean a different data source or a different locale, this is
// the assertion that should have to be deleted deliberately.
TEST(SleepScreenPolicy, DarkChangesOnlyThePolarity) {
  const std::pair<uint8_t, uint8_t> kPairs[] = {
      {CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR, CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_DARK},
      {CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE,
       CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE_DARK},
  };
  for (const auto& [light, dark] : kPairs) {
    const auto l = sleepscreen::calendarPlanFor(light);
    const auto d = sleepscreen::calendarPlanFor(dark);
    EXPECT_EQ(l.style, d.style) << modeName(dark) << " must be " << modeName(light) << " with the frame flipped";
    EXPECT_FALSE(l.dark);
    EXPECT_TRUE(d.dark);
  }
}

// The withdrawn week-count rows still decode. Their enum values are frozen by
// persistence and normalizeRetiredSettings() remaps them on load, but a save
// that slips past it must still land on a calendar rather than the stock logo.
// All three fold onto the LIGHT classic screen: they were withdrawn before a
// dark rendition existed, so inventing one for them would change what an old
// save draws.
TEST(SleepScreenPolicy, WithdrawnWeekCountRowsFoldOntoTheLightClassicScreen) {
  for (const uint8_t mode :
       {CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FOUR, CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FIVE,
        CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_SIX}) {
    const auto p = sleepscreen::calendarPlanFor(mode);
    EXPECT_TRUE(p.isCalendar) << modeName(mode) << " must still draw a calendar";
    EXPECT_EQ(p.style, calendar::Style::SpanishCR);
    EXPECT_FALSE(p.dark) << modeName(mode) << " predates the dark rendition and must not acquire one";
  }
}

// Nothing that is not a calendar may decode as one. Swept over the whole uint8
// range, not just the named modes, because the value arrives from a JSON file
// that anyone can edit and an out-of-range one must fall through to the default
// screen rather than into the calendar branch.
TEST(SleepScreenPolicy, NoOtherStoredValueDecodesAsACalendar) {
  for (int v = 0; v <= 0xFF; ++v) {
    const auto p = sleepscreen::calendarPlanFor(static_cast<uint8_t>(v));
    const bool isCalendarValue = v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR ||
                                 v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FOUR ||
                                 v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_FIVE ||
                                 v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_SIX ||
                                 v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE ||
                                 v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_DARK ||
                                 v == CrossPointSettings::SLEEP_SCREEN_MODE::CALENDAR_WESTSIDE_DARK;
    EXPECT_EQ(p.isCalendar, isCalendarValue) << "stored sleepScreen value " << v << " decoded wrongly";
    if (!isCalendarValue) {
      EXPECT_FALSE(p.dark) << "a non-calendar value must not carry a polarity";
    }
  }
}
