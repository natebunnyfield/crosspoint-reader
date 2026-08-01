#pragma once

#include <cstdint>

// Westside Community Schools (District 66, Omaha) + Westside Early Childhood
// Centers / Club 66 events for the 2026-2027 school year, in English.
//
// FIXED ANNUAL DATA, baked by design (owner: "the westside and us calendar are
// fixed annually and do not need refetching — just name the calendar
// 2026-2027"). Next school year = a sibling header + table swap.
//
// Sources (fetched 2026-08-01):
//   D66:  westside66.org 2026-27 Student Calendar (board-approved 10-21-24,
//         revised 4-24-25)
//   WECC: weccomaha.com ECC and Club 66 2026/27 School Calendar ("subject to
//         change")
//
// Static-storage string literals only — the whole table lives in Flash per the
// "static const lookup table" rule in CLAUDE.md.
namespace calendar {

struct WestsideEvent {
  const char* label;
  uint16_t year;
  uint8_t month;
  uint8_t day;     // first day of the range
  uint8_t endDay;  // last day (== day for single-day events), same month
  bool shaded;     // shade the grid square (no school / closed), vs legend-only
};

// Ordered chronologically. Ranges never cross a month boundary — multi-month
// breaks are split so the per-day lookup stays a flat scan.
constexpr WestsideEvent WESTSIDE_2026_2027[] = {
    {"WECC closed (Kickoff)", 2026, 8, 10, 10, true},
    {"WECC closed (Open Houses)", 2026, 8, 11, 11, true},
    {"WECC closed (Prof. Dev.)", 2026, 8, 12, 12, true},
    {"First day of school", 2026, 8, 13, 13, false},
    {"No school (Labor Day)", 2026, 9, 7, 7, true},
    {"No school (Prof. Learning)", 2026, 9, 8, 8, true},
    {"Parent-teacher conferences", 2026, 10, 7, 8, false},
    {"No school", 2026, 10, 8, 9, true},
    {"No school (Prof. Learning)", 2026, 10, 12, 12, true},
    {"WECC Trunk or Treat", 2026, 10, 23, 23, false},
    {"Club 66 reserved care", 2026, 11, 25, 25, false},
    {"Thanksgiving break", 2026, 11, 25, 27, true},
    {"Early dismissal", 2026, 12, 18, 18, false},
    {"Winter break", 2026, 12, 21, 31, true},
    {"WECC closed (holidays)", 2026, 12, 24, 25, true},
    {"WECC closed (New Year's)", 2026, 12, 31, 31, true},
    {"Winter break", 2027, 1, 1, 4, true},
    {"2nd semester begins", 2027, 1, 5, 5, false},
    {"No school (Prof. Learning)", 2027, 1, 15, 15, true},
    {"No school (MLK Jr. Day)", 2027, 1, 18, 18, true},
    {"No school (Teacher Work Day)", 2027, 2, 12, 12, true},
    {"No school (Prof. Learning)", 2027, 2, 15, 15, true},
    {"Parent-teacher conferences", 2027, 3, 17, 18, false},
    {"No school", 2027, 3, 18, 19, true},
    {"Early dismissal", 2027, 3, 26, 26, false},
    {"Spring break", 2027, 3, 29, 31, true},
    {"Spring break", 2027, 4, 1, 2, true},
    {"No school (Prof. Learning)", 2027, 4, 26, 26, true},
    {"Graduation", 2027, 5, 16, 16, false},
    {"Early dismissal", 2027, 5, 26, 26, false},
    {"Last day of school (12:00)", 2027, 5, 28, 28, false},
};

constexpr size_t WESTSIDE_2026_2027_COUNT =
    sizeof(WESTSIDE_2026_2027) / sizeof(WESTSIDE_2026_2027[0]);

}  // namespace calendar
