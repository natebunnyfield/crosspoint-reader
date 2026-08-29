#pragma once

#include <cstdint>

class GfxRenderer;

// On-device renderer for the holiday calendar sleep screens.
//
// Paints a portrait grid directly into the GfxRenderer framebuffer: `weeks`
// rows (the classic "Calendar" style uses five; "Calendar Four/Five/Six" pick
// 4/5/6) anchored on the Sunday of today's week, with Costa Rican and US
// holidays highlighted and named in a legend below. One parameterized renderer
// backs every style — only the row count differs; the layout redistributes the
// fixed grid band across however many rows are asked for.
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

// Data source and locale selection for CalendarSleepScreen::render().
//
//   SpanishCR  — original: CR + US holidays, Spanish month names and labels.
//   WestsideEN — Westside Community Schools (D66, Omaha) + REGION_US federal
//                holidays, English month names and labels, no region-tag column.
enum class Style : uint8_t {
  SpanishCR,
  WestsideEN,
};

class CalendarSleepScreen {
 public:
  // Paint the calendar into `renderer`'s framebuffer. Sets Portrait
  // orientation. The caller drives the panel refresh (displayBuffer) so it can
  // choose the waveform. Passing an invalid date is a no-op.
  //
  // `weeks` is the number of week rows (defaulted so existing callers — the
  // classic style and the render harness — keep the five-week look). Clamped
  // to a sane range rather than trusted: an out-of-range count would divide
  // the grid band into rows too short for the digit ink.
  //
  // `style` selects the holiday data source and locale; defaults to SpanishCR
  // so all existing callers are unaffected.
  static void render(GfxRenderer& renderer, const YMD& today, uint8_t weeks = 5, Style style = Style::SpanishCR);
};

}  // namespace calendar
