#pragma once

#include <cstddef>
#include <cstdint>

// Costa Rican and US public holidays for the calendar sleep screen.
//
// The calendar is a Spanish-learning aid for a reader who tracks both
// countries' calendars, so every label is in Spanish and each entry is tagged
// with the region(s) that observe it. Two dates are shared (Año Nuevo,
// Navidad) and carry both tags rather than appearing twice.
//
// Everything is compile-time / offline: no network fetch, no heap allocation.
// Dates come from fixed-date, nth-weekday-of-month, and Easter-relative rules.
// See HolidayCalculatorTest.cpp for reference dates.
namespace calendar {

// Region bitmask. An entry may be observed in one country or both.
inline constexpr uint8_t REGION_CR = 1 << 0;
inline constexpr uint8_t REGION_US = 1 << 1;

enum class HolidayId : uint8_t {
  AnoNuevo = 0,            // Jan 1            CR + US
  DiaDeMlk,                // 3rd Mon Jan      US
  DiaDeLosPresidentes,     // 3rd Mon Feb      US
  JuevesSanto,             // Easter - 3       CR
  ViernesSanto,            // Easter - 2       CR
  JuanSantamaria,          // Apr 11           CR
  DiaDelTrabajoCR,         // May 1            CR
  DiaDeLosCaidos,          // last Mon May     US (Memorial Day)
  Juneteenth,              // Jun 19           US
  IndependenciaUS,         // Jul 4            US
  AnexionNicoya,           // Jul 25           CR
  VirgenDeLosAngeles,      // Aug 2            CR
  DiaDeLaMadre,            // Aug 15           CR
  DiaDeLaPersonaNegra,     // Aug 31           CR
  DiaDelTrabajoUS,         // 1st Mon Sep      US (Labor Day)
  IndependenciaCR,         // Sep 15           CR
  DiaDeLosPueblosIndigenas,// 2nd Mon Oct      US (Columbus / Indigenous Peoples')
  EncuentroDeLasCulturas,  // Oct 12           CR
  DiaDeLosVeteranos,       // Nov 11           US
  AccionDeGracias,         // 4th Thu Nov      US
  ViernesNegro,            // 4th Thu Nov + 1  US
  AbolicionDelEjercito,    // Dec 1            CR
  NocheBuena,              // Dec 24           US
  Navidad,                 // Dec 25           CR + US
  HolidayIdCount,
};

// Compact packed date. Firmware constrains RAM aggressively; keeping this
// 4 bytes and trivially copyable lets calendar cells stack in a small array
// without a heap allocation.
struct YMD {
  uint16_t year;
  uint8_t month;  // 1..12
  uint8_t day;    // 1..31
};

enum class HolidayRule : uint8_t {
  FixedDate,       // (month, day)
  NthWeekday,      // Nth (or Last, nth == -1) weekday of month
  EasterRelative,  // computeEaster(year) + dayOffset days
};

struct HolidayEntry {
  const char* shortLabel;  // Spanish label rendered in the legend
  HolidayRule rule;
  uint8_t month;     // FixedDate / NthWeekday: 1..12. Unused for EasterRelative.
  int8_t nth;        // FixedDate: day of month. NthWeekday: 1..5 or -1 for last.
  int8_t weekday;    // NthWeekday only: 0=Sun..6=Sat.
  int8_t dayOffset;  // signed day offset applied AFTER rule resolves.
  uint8_t regions;   // REGION_CR | REGION_US
};

// Compile-time table of holidays. Ordering must match HolidayId enum values
// so lookup is O(1) via static_cast<size_t>(id).
extern const HolidayEntry HOLIDAY_TABLE[static_cast<size_t>(HolidayId::HolidayIdCount)];

// Return the date for a holiday in a given year.
//
// Neither country's "observed" shifting is applied: Costa Rica's Ley 9803 can
// move some feriados to a Monday, and US federal holidays shift to Fri/Mon
// when they land on a weekend. This calendar highlights the actual cultural
// date, which is what the reference layout shows.
YMD resolveHoliday(HolidayId id, uint16_t year);

// Regions observing a holiday (REGION_CR / REGION_US bitmask).
uint8_t holidayRegions(HolidayId id);

// Easter Sunday for the given Gregorian year, via the Anonymous Gregorian
// algorithm (Meeus/Jones/Butcher). Valid for years >= 1583.
YMD computeEaster(uint16_t year);

// Compute the weekday for a date via Zeller's Congruence. Returns 0=Sun..6=Sat.
// Valid for the Gregorian calendar (year >= 1583).
uint8_t weekdayOf(uint16_t year, uint8_t month, uint8_t day);

// Number of days in the given month for the given year (handles leap years).
uint8_t daysInMonth(uint16_t year, uint8_t month);

// Add `days` to `date` in-place. `days` may be negative. Handles month/year
// rollover without any allocation.
void addDays(YMD& date, int32_t days);

// Return true if `a` and `b` refer to the same calendar date.
inline bool sameDate(const YMD& a, const YMD& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day;
}

// Order two dates chronologically.
inline bool dateLess(const YMD& a, const YMD& b) {
  if (a.year != b.year) return a.year < b.year;
  if (a.month != b.month) return a.month < b.month;
  return a.day < b.day;
}

// Return true if `date` is a leap-year Feb 29 or otherwise valid.
bool isValidDate(uint16_t year, uint8_t month, uint8_t day);

}  // namespace calendar
