#pragma once

#include <stdint.h>

// Pure integer rasterizer for the segmented ring clock face.
// Port of ~/src/simple-ring-clock-inverted/main.py (M5Stack CoreInk, 200x200)
// generalized to an arbitrary face radius. White-on-black ("inverted") design:
// 12 concentric rings, four quadrants:
//   top-right    (0-90)    hours   - summed rings (greedy largest-first)
//   top-left     (90-180)  minutes - single ring, ring N = N*5 minutes
//   bottom-left  (180-270) month   - summed rings
//   bottom-right (270-360) day     - summed rings
// Active arcs are zebra-striped into ring-value segments, white first.
//
// Header is freestanding (no Arduino/FreeRTOS includes) so the exact same
// code can be compiled and rendered on the host for verification.

namespace ringclock {

constexpr int NUM_RINGS = 12;

struct Geometry {
  int innerR[NUM_RINGS];
  int outerR[NUM_RINGS];
};

// Port of get_ring_radii(). Original proportions on a 100-unit face radius:
// rings start at r=10 and occupy 88 units, 1-unit gaps between rings.
inline Geometry computeGeometry(int faceRadius) {
  Geometry g;
  const int startR = faceRadius * 10 / 100;
  int gap = faceRadius / 100;
  if (gap < 1) gap = 1;
  const int totalSpace = faceRadius * 88 / 100;
  const int ringWidth = (totalSpace - NUM_RINGS * gap) / NUM_RINGS;
  int inner = startR;
  for (int i = 0; i < NUM_RINGS; i++) {
    g.innerR[i] = inner;
    g.outerR[i] = inner + ringWidth;
    inner = g.outerR[i] + gap;
  }
  return g;
}

// Integer angle approximation, 0..360 (can return exactly 360 near the +x
// axis from below, which no quadrant claims - same as the original).
// 0 = +x axis, counter-clockwise; dy positive = up (dy = centerY - screenY).
// Exact port of the octant classification in main.py fill_arc_segment();
// C++ '/' truncation equals Python '//' floor here because every division
// has non-negative operands.
inline int angleOf(int dx, int dy) {
  if (dx >= 0 && dy >= 0) {
    int angle = dx > dy ? 0 : 90;
    if (dx > 0 && dy > 0) angle = dx >= dy ? (45 * dy) / dx : 90 - (45 * dx) / dy;
    return angle;
  }
  if (dx < 0 && dy >= 0) {
    int angle = dy > -dx ? 90 : 180;
    if (dy > 0) angle = dy >= -dx ? 90 + (45 * -dx) / dy : 180 - (45 * dy) / -dx;
    return angle;
  }
  if (dx < 0 && dy < 0) {
    int angle = -dx > -dy ? 180 : 270;
    angle = -dx >= -dy ? 180 + (45 * -dy) / -dx : 270 - (45 * -dx) / -dy;
    return angle;
  }
  // dx >= 0, dy < 0
  int angle = -dy > dx ? 270 : 360;
  angle = -dy >= dx ? 270 + (45 * dx) / -dy : 360 - (45 * -dy) / dx;
  return angle;
}

// Port of find_sum_combination(): greedy largest-first ring sum.
// Returns a bitmask of active ring indices (bit i = ring value i+1), or 0
// when target is out of the representable range.
inline uint16_t sumCombinationMask(int target) {
  if (target <= 0) return 0;
  uint16_t mask = 0;
  int remaining = target;
  for (int i = NUM_RINGS - 1; i >= 0; i--) {
    const int val = i + 1;
    if (val <= remaining) {
      mask |= static_cast<uint16_t>(1u << i);
      remaining -= val;
      if (remaining == 0) break;
    }
  }
  return remaining == 0 ? mask : 0;
}

struct FaceState {
  uint16_t hourMask;   // quadrant 0 (top-right), summed rings
  int minuteRing;      // quadrant 1 (top-left), single ring index, -1 = none
  uint16_t monthMask;  // quadrant 2 (bottom-left), summed rings
  uint16_t dayMask;    // quadrant 3 (bottom-right), summed rings
};

// hour12 in 1..12, minute in 0..59, month in 1..12, day in 1..31.
inline FaceState computeState(int hour12, int minute, int month, int day) {
  return {sumCombinationMask(hour12), minute / 5 - 1, sumCombinationMask(month), sumCombinationMask(day)};
}

// Classify one pixel relative to the face center; dy positive = up.
// Returns true when the pixel is WHITE (everything else stays background black).
inline bool pixelIsWhite(int dx, int dy, const Geometry& g, const FaceState& st) {
  const int d2 = dx * dx + dy * dy;
  const int minR = g.innerR[0];
  const int maxR = g.outerR[NUM_RINGS - 1];
  if (d2 > maxR * maxR + maxR) return false;  // outside the face

  // Quadrant divider lines: 1px along both axes, spanning minR..maxR
  // (crosses ring gaps, like the drawLine calls in main.py).
  if ((dx == 0 || dy == 0) && d2 >= minR * minR) return true;

  for (int i = 0; i < NUM_RINGS; i++) {
    const int ri = g.innerR[i];
    const int ro = g.outerR[i];
    // Boundary bands |d - r| <= ~0.5  <=>  r^2 - r + 1 <= d^2 <= r^2 + r.
    // Replaces main.py's midpoint-ellipse outlines (visually equivalent,
    // guaranteed gap-free). Ring 0's inner band is the inner-circle outline.
    if (d2 < ri * ri - ri + 1) return false;  // gap below ring i (or inner disc): black
    if (d2 <= ri * ri + ri) return true;      // inner boundary outline
    if (d2 > ro * ro + ro) continue;          // beyond ring i, try next
    if (d2 >= ro * ro - ro + 1) return true;  // outer boundary outline

    // Interior of ring i: segmented arc fill.
    const int angle = angleOf(dx, dy);
    const int quad = angle / 90;
    if (quad > 3) return false;  // angle == 360: unclaimed, stays black (as in main.py)

    bool active = false;
    switch (quad) {
      case 0: active = (st.hourMask >> i) & 1; break;
      case 1: active = i == st.minuteRing; break;
      case 2: active = (st.monthMask >> i) & 1; break;
      case 3: active = (st.dayMask >> i) & 1; break;
    }
    if (!active) return false;  // inactive arc: black

    // fill_segmented_arc(): ring value = segment count, zebra white-first,
    // integer-division remainder wedge stays background black.
    const int segs = i + 1;
    const int segAngle = 90 / segs;
    const int rel = angle - quad * 90;
    if (rel >= segs * segAngle) return false;
    return (rel / segAngle) % 2 == 0;
  }
  return false;
}

// Minimal date/time carrier for RTC (UTC) -> local conversion.
struct DateTime {
  int year;    // e.g. 2026
  int month;   // 1..12
  int day;     // 1..31
  int hour;    // 0..23
  int minute;  // 0..59
};

inline bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int daysInMonth(int year, int month) {
  static constexpr uint8_t DAYS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (month == 2 && isLeapYear(year)) ? 29 : DAYS[month - 1];
}

// Apply the CrossPoint clock offset (biased quarter-hours: 48 = UTC+0,
// 0 = UTC-12:00, 104 = UTC+14:00) to a UTC date-time, rolling the date
// across midnight. Mirrors the clamp in HalClock::formatTime().
inline void applyUtcOffsetQ(DateTime& dt, uint8_t offsetQBiased) {
  if (offsetQBiased > 104) offsetQBiased = 104;
  int total = dt.hour * 60 + dt.minute + (static_cast<int>(offsetQBiased) - 48) * 15;
  while (total < 0) {
    total += 1440;
    if (--dt.day < 1) {
      if (--dt.month < 1) {
        dt.month = 12;
        dt.year--;
      }
      dt.day = daysInMonth(dt.year, dt.month);
    }
  }
  while (total >= 1440) {
    total -= 1440;
    if (++dt.day > daysInMonth(dt.year, dt.month)) {
      dt.day = 1;
      if (++dt.month > 12) {
        dt.month = 1;
        dt.year++;
      }
    }
  }
  dt.hour = total / 60;
  dt.minute = total % 60;
}

}  // namespace ringclock
