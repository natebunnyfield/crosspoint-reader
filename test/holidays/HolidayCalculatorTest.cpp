#include <gtest/gtest.h>

#include "src/activities/boot_sleep/HolidayCalculator.h"

using calendar::addDays;
using calendar::computeEaster;
using calendar::daysInMonth;
using calendar::holidayRegions;
using calendar::HolidayId;
using calendar::REGION_CR;
using calendar::REGION_US;
using calendar::isValidDate;
using calendar::resolveHoliday;
using calendar::sameDate;
using calendar::weekdayOf;
using calendar::YMD;

// ============================================================================
// weekdayOf: known fixed points across a wide date range.
// Reference values verified against the Gregorian calendar.
// ============================================================================
TEST(WeekdayOf, KnownDates) {
  EXPECT_EQ(weekdayOf(2000, 1, 1), 6u);   // Sat
  EXPECT_EQ(weekdayOf(2000, 2, 29), 2u);  // Tue (leap day)
  EXPECT_EQ(weekdayOf(2026, 7, 27), 1u);  // Mon
  EXPECT_EQ(weekdayOf(2026, 1, 1), 4u);   // Thu
  EXPECT_EQ(weekdayOf(1969, 7, 20), 0u);  // Sun (Apollo 11)
  EXPECT_EQ(weekdayOf(2100, 3, 1), 1u);   // Mon (2100 is NOT a leap year)
}

// ============================================================================
// daysInMonth
// ============================================================================
TEST(DaysInMonth, StandardMonths) {
  EXPECT_EQ(daysInMonth(2026, 1), 31u);
  EXPECT_EQ(daysInMonth(2026, 4), 30u);
  EXPECT_EQ(daysInMonth(2026, 12), 31u);
}

TEST(DaysInMonth, FebruaryLeapRules) {
  EXPECT_EQ(daysInMonth(2024, 2), 29u);
  EXPECT_EQ(daysInMonth(2025, 2), 28u);
  EXPECT_EQ(daysInMonth(2100, 2), 28u);
  EXPECT_EQ(daysInMonth(2000, 2), 29u);
}

TEST(DaysInMonth, OutOfRangeReturnsZero) {
  EXPECT_EQ(daysInMonth(2026, 0), 0u);
  EXPECT_EQ(daysInMonth(2026, 13), 0u);
}

// ============================================================================
// isValidDate
// ============================================================================
TEST(IsValidDate, ValidAndInvalid) {
  EXPECT_TRUE(isValidDate(2024, 2, 29));
  EXPECT_FALSE(isValidDate(2025, 2, 29));
  EXPECT_FALSE(isValidDate(2026, 4, 31));
  EXPECT_FALSE(isValidDate(2026, 0, 1));
  EXPECT_FALSE(isValidDate(2026, 13, 1));
  EXPECT_FALSE(isValidDate(2026, 6, 0));
}

// ============================================================================
// addDays: month/year rollover, negative offsets, no-op zero.
// ============================================================================
TEST(AddDays, ForwardWithinMonth) {
  YMD d{2026, 7, 27};
  addDays(d, 3);
  EXPECT_TRUE(sameDate(d, YMD{2026, 7, 30}));
}

TEST(AddDays, ForwardAcrossMonth) {
  YMD d{2026, 7, 27};
  addDays(d, 10);
  EXPECT_TRUE(sameDate(d, YMD{2026, 8, 6}));
}

TEST(AddDays, ForwardAcrossYear) {
  YMD d{2026, 12, 28};
  addDays(d, 5);
  EXPECT_TRUE(sameDate(d, YMD{2027, 1, 2}));
}

TEST(AddDays, ForwardMultiMonthLeap) {
  YMD d{2024, 1, 30};
  addDays(d, 40);  // crosses Feb 29 leap
  EXPECT_TRUE(sameDate(d, YMD{2024, 3, 10}));
}

TEST(AddDays, BackwardWithinMonth) {
  YMD d{2026, 7, 27};
  addDays(d, -5);
  EXPECT_TRUE(sameDate(d, YMD{2026, 7, 22}));
}

TEST(AddDays, BackwardAcrossMonth) {
  YMD d{2026, 3, 3};
  addDays(d, -5);
  EXPECT_TRUE(sameDate(d, YMD{2026, 2, 26}));
}

TEST(AddDays, BackwardAcrossYear) {
  YMD d{2027, 1, 2};
  addDays(d, -5);
  EXPECT_TRUE(sameDate(d, YMD{2026, 12, 28}));
}

TEST(AddDays, Zero) {
  YMD d{2026, 7, 27};
  addDays(d, 0);
  EXPECT_TRUE(sameDate(d, YMD{2026, 7, 27}));
}

TEST(AddDays, SixWeeksGrid) {
  // Real usage: iterate 41 forward days from the anchor Sunday.
  YMD d{2026, 7, 26};
  addDays(d, 41);
  EXPECT_TRUE(sameDate(d, YMD{2026, 9, 5}));
}

// ============================================================================
// Easter Sunday — Anonymous Gregorian algorithm (Meeus/Jones/Butcher).
// Reference values from published Easter tables.
// ============================================================================
TEST(ComputeEaster, KnownYears) {
  EXPECT_TRUE(sameDate(computeEaster(2024), YMD{2024, 3, 31}));
  EXPECT_TRUE(sameDate(computeEaster(2025), YMD{2025, 4, 20}));
  EXPECT_TRUE(sameDate(computeEaster(2026), YMD{2026, 4, 5}));
  EXPECT_TRUE(sameDate(computeEaster(2027), YMD{2027, 3, 28}));
  EXPECT_TRUE(sameDate(computeEaster(2000), YMD{2000, 4, 23}));
  // Very early Easter — 2038 falls on March 25 (rare).
  EXPECT_TRUE(sameDate(computeEaster(2038), YMD{2038, 4, 25}));
}

TEST(ComputeEaster, AlwaysSunday) {
  // Easter is always on a Sunday. Check a chunk of years.
  for (uint16_t y = 2020; y <= 2040; ++y) {
    const auto e = computeEaster(y);
    EXPECT_EQ(weekdayOf(e.year, e.month, e.day), 0u) << "year " << y;
  }
}

// ============================================================================
// resolveHoliday: Costa Rican holidays.
// Cross-checked against the Costa Rican Código de Trabajo and the reference
// legend in ~/Downloads/sleep_6week.bmp.
// ============================================================================
TEST(ResolveHoliday, CostaRicaFixedDates2026) {
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::AnoNuevo, 2026), YMD{2026, 1, 1}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::JuanSantamaria, 2026), YMD{2026, 4, 11}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDelTrabajoCR, 2026), YMD{2026, 5, 1}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::AnexionNicoya, 2026), YMD{2026, 7, 25}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::VirgenDeLosAngeles, 2026), YMD{2026, 8, 2}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLaMadre, 2026), YMD{2026, 8, 15}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLaPersonaNegra, 2026), YMD{2026, 8, 31}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::IndependenciaCR, 2026), YMD{2026, 9, 15}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::EncuentroDeLasCulturas, 2026), YMD{2026, 10, 12}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::AbolicionDelEjercito, 2026), YMD{2026, 12, 1}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::Navidad, 2026), YMD{2026, 12, 25}));
}

TEST(ResolveHoliday, SemanaSantaIsEasterRelative) {
  // Easter 2026 = April 5. Jueves Santo = Easter-3 (Thu Apr 2),
  // Viernes Santo = Easter-2 (Fri Apr 3).
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::JuevesSanto, 2026), YMD{2026, 4, 2}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::ViernesSanto, 2026), YMD{2026, 4, 3}));
  // Easter 2024 = March 31 — Semana Santa crosses the March/April boundary.
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::JuevesSanto, 2024), YMD{2024, 3, 28}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::ViernesSanto, 2024), YMD{2024, 3, 29}));
  // Easter 2027 = March 28.
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::JuevesSanto, 2027), YMD{2027, 3, 25}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::ViernesSanto, 2027), YMD{2027, 3, 26}));
}

// ============================================================================
// resolveHoliday: US federal holidays.
// Cross-checked against OPM's published federal holiday dates.
// ============================================================================
TEST(ResolveHoliday, UnitedStatesFixedDates2026) {
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::Juneteenth, 2026), YMD{2026, 6, 19}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::IndependenciaUS, 2026), YMD{2026, 7, 4}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLosVeteranos, 2026), YMD{2026, 11, 11}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::NocheBuena, 2026), YMD{2026, 12, 24}));
}

TEST(ResolveHoliday, UnitedStatesNthWeekday2026) {
  // MLK: 3rd Mon Jan 2026 = Jan 19
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeMlk, 2026), YMD{2026, 1, 19}));
  // Presidents': 3rd Mon Feb 2026 = Feb 16
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLosPresidentes, 2026), YMD{2026, 2, 16}));
  // Memorial: last Mon May 2026 = May 25
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLosCaidos, 2026), YMD{2026, 5, 25}));
  // Labor: 1st Mon Sep 2026 = Sep 7
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDelTrabajoUS, 2026), YMD{2026, 9, 7}));
  // Indigenous Peoples'/Columbus: 2nd Mon Oct 2026 = Oct 12
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLosPueblosIndigenas, 2026), YMD{2026, 10, 12}));
  // Thanksgiving: 4th Thu Nov 2026 = Nov 26; Viernes Negro = Nov 27
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::AccionDeGracias, 2026), YMD{2026, 11, 26}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::ViernesNegro, 2026), YMD{2026, 11, 27}));
}

TEST(ResolveHoliday, MemorialDayLastMondayEdgeCases) {
  // 2025 May has five Mondays; the last is May 26.
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLosCaidos, 2025), YMD{2025, 5, 26}));
  // 2027 May: Mondays 3, 10, 17, 24, 31 -> last is May 31.
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLosCaidos, 2027), YMD{2027, 5, 31}));
}

TEST(ResolveHoliday, ViernesNegroAlwaysFollowsThanksgiving) {
  for (uint16_t y = 2024; y <= 2032; ++y) {
    YMD tg = resolveHoliday(HolidayId::AccionDeGracias, y);
    const YMD bf = resolveHoliday(HolidayId::ViernesNegro, y);
    addDays(tg, 1);
    EXPECT_TRUE(sameDate(bf, tg)) << "year " << y;
    // Thanksgiving is always a Thursday, Viernes Negro always a Friday.
    EXPECT_EQ(weekdayOf(bf.year, bf.month, bf.day), 5u) << "year " << y;
  }
}

// ============================================================================
// Region tagging
// ============================================================================
TEST(HolidayRegions, SharedDatesCarryBothTags) {
  // Año Nuevo and Navidad are observed in both countries and must appear once
  // with both tags rather than as duplicate entries.
  EXPECT_EQ(holidayRegions(HolidayId::AnoNuevo), REGION_CR | REGION_US);
  EXPECT_EQ(holidayRegions(HolidayId::Navidad), REGION_CR | REGION_US);
}

TEST(HolidayRegions, CountrySpecificTags) {
  EXPECT_EQ(holidayRegions(HolidayId::JuevesSanto), REGION_CR);
  EXPECT_EQ(holidayRegions(HolidayId::AnexionNicoya), REGION_CR);
  EXPECT_EQ(holidayRegions(HolidayId::IndependenciaCR), REGION_CR);
  EXPECT_EQ(holidayRegions(HolidayId::AccionDeGracias), REGION_US);
  EXPECT_EQ(holidayRegions(HolidayId::Juneteenth), REGION_US);
  EXPECT_EQ(holidayRegions(HolidayId::DiaDeMlk), REGION_US);
}

TEST(HolidayRegions, EveryEntryTaggedWithAtLeastOneRegion) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(HolidayId::HolidayIdCount); ++i) {
    const uint8_t r = holidayRegions(static_cast<HolidayId>(i));
    EXPECT_TRUE((r & (REGION_CR | REGION_US)) != 0) << "untagged entry " << (int)i;
  }
}

// The two countries' Labor Days are distinct dates and must not collide.
TEST(ResolveHoliday, LabourDaysAreDistinct) {
  for (uint16_t y = 2024; y <= 2032; ++y) {
    const YMD cr = resolveHoliday(HolidayId::DiaDelTrabajoCR, y);
    const YMD us = resolveHoliday(HolidayId::DiaDelTrabajoUS, y);
    EXPECT_FALSE(sameDate(cr, us)) << "year " << y;
    EXPECT_EQ(cr.month, 5u);
    EXPECT_EQ(us.month, 9u);
  }
}

TEST(ResolveHoliday, HolidaysAlwaysReturnValidDates) {
  // Regression guard: every holiday for a broad year range must resolve to a
  // date that isValidDate accepts.
  for (uint16_t y = 2020; y <= 2050; ++y) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(HolidayId::HolidayIdCount); ++i) {
      const YMD d = resolveHoliday(static_cast<HolidayId>(i), y);
      EXPECT_TRUE(isValidDate(d.year, d.month, d.day))
          << "invalid holiday date for id=" << (int)i << " year=" << y;
    }
  }
}

TEST(ResolveHoliday, ReferenceBmpHolidaysAppearInJulyAug2026Grid) {
  // The reference BMP (Sunday Jul 19 - Saturday Aug 29, 2026) highlights
  // three CR holidays. Pin them so calendar-fill drift shows up as a failure.
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::AnexionNicoya, 2026), YMD{2026, 7, 25}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::VirgenDeLosAngeles, 2026), YMD{2026, 8, 2}));
  EXPECT_TRUE(sameDate(resolveHoliday(HolidayId::DiaDeLaMadre, 2026), YMD{2026, 8, 15}));
}
