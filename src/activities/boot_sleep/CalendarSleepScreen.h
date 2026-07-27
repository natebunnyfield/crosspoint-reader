#pragma once

#include <cstdint>

class GfxRenderer;

// On-device renderer for the 6-week holiday calendar sleep screen.
//
// Consumes a GfxRenderer (which owns the framebuffer + fonts) and paints a
// portrait 528x792 6-week grid centred on "today". Holidays and today itself
// are highlighted per the reference layout in ~/Downloads/sleep_6week.bmp:
// - holidays: mid-gray filled rounded rect
// - today:    bold dark outline (distinct so today+holiday both read)
// A month rollover mid-grid earns a small subscript ("aug", "sep", ...) under
// the day number, and a short legend of the visible holidays sits at the base.
//
// The renderer holds no state and does not allocate; call render() as often
// as staleness demands. On device the whole call is <200ms (dominated by
// glyph rendering, not layout math).
namespace calendar {

struct YMD;  // fwd — defined in HolidayCalculator.h

class CalendarSleepScreen {
 public:
  // Paint the calendar into `renderer`'s framebuffer. The renderer's
  // orientation must be Portrait; the caller is responsible for driving the
  // display refresh (displayBuffer or serialising to /sleep.bmp).
  //
  // `today` anchors the grid: the top row is the Sunday of today's week, and
  // the grid extends 6 weeks (42 cells) forward. `today` is highlighted in
  // its cell. Passing an invalid date is a no-op.
  static void render(GfxRenderer& renderer, const YMD& today);

  // Serialise the renderer's current 1-bit framebuffer to a 1-bit BMP written
  // at `sdPath` on the SD card via HalStorage. Rotates the internal landscape
  // framebuffer into portrait so the on-disk BMP matches the reference
  // dimensions (528 wide x 792 tall). Returns true on success.
  //
  // Split out from render() so callers can either write-then-display via the
  // existing CUSTOM path, or display directly (skipping the round-trip).
  static bool serialiseFramebufferAsPortraitBmp(GfxRenderer& renderer, const char* sdPath);

  // Read/write the "last regenerated" date marker on SD. Sleep entry uses this
  // to skip regen when today == last stamped date, keeping SD write churn to
  // one small file per calendar-day even when the user sleeps repeatedly.
  static bool readStamp(YMD& out);
  static bool writeStamp(const YMD& date);
  static bool clearStamp();  // used by the on-demand "regenerate now" action
};

}  // namespace calendar
