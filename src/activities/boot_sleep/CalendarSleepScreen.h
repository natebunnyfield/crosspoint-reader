#pragma once

#include <cstdint>

class GfxRenderer;

// On-device renderer for the 5-week holiday calendar sleep screen.
//
// Paints a portrait grid directly into the GfxRenderer framebuffer: five week
// rows anchored on the Sunday of today's week, with Costa Rican and US
// holidays highlighted and named in a legend below.
//
// There is no intermediate file. Earlier revisions serialised the frame to
// /sleep.bmp and let renderCustomSleepScreen() read it back, which meant an SD
// write per day, a staleness stamp to avoid rewriting, and a version counter so
// firmware changes invalidated the cache — all of which could and did leave a
// stale image on screen. Drawing straight to the panel (what every other sleep
// mode does) removes the file, the stamp, and that entire failure mode. The
// render is cheap enough to redo on every sleep entry.
namespace calendar {

struct YMD;  // fwd — defined in HolidayCalculator.h

class CalendarSleepScreen {
 public:
  // Paint the calendar into `renderer`'s framebuffer. Sets Portrait
  // orientation. The caller drives the panel refresh (displayBuffer) so it can
  // choose the waveform. Passing an invalid date is a no-op.
  static void render(GfxRenderer& renderer, const YMD& today);
};

}  // namespace calendar
