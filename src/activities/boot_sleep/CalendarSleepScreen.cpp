#include "CalendarSleepScreen.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "HolidayCalculator.h"
#include "fontIds.h"

// On-device renderer for the 6-week Costa Rican holiday calendar sleep screen.
//
// All geometry is derived from renderer.getScreenWidth()/getScreenHeight() at
// runtime rather than hardcoded, per the "No Hardcoding" rule in CLAUDE.md.
// The X3 portrait panel is 528x792 and the X4 is 480x800; the proportional
// layout below works on both.
namespace calendar {
namespace {

// ---------------------------------------------------------------------------
// Layout, computed from the live panel dimensions.
// ---------------------------------------------------------------------------
constexpr int ROW_COUNT = 6;
constexpr int COL_COUNT = 7;
constexpr int LEGEND_MAX_ROWS = 5;

struct Layout {
  int screenW;
  int screenH;
  int marginX;
  int titleTopY;        // top-y of the header text box
  int weekdayTopY;      // top-y of the weekday abbreviation row
  int gridTop;          // top of the first day row
  int rowH;             // height of one week row
  int colW;             // width of one weekday column
  int cellBoxSide;      // highlight rect is square, like the reference
  int cellRadius;
  int legendTop;
  int legendRowH;
  int legendSwatchSide;
  int legendMaxRows;
  // Digit ink metrics, measured from the font rather than assumed. See
  // digitTopYForCentre() for why the ascender alone is not enough.
  int numAscender;      // baseline offset drawText applies to its `y` argument
  int digitInkTop;      // glyph.top for '0' — baseline to ink top
  int digitInkH;        // glyph.height for '0'

  int colCentreX(int col) const { return marginX + col * colW + colW / 2; }
  int rowCentreY(int row) const { return gridTop + row * rowH + rowH / 2; }

  // Return the `y` to hand drawText so the digit's INK is centred on centreY.
  //
  // drawText treats `y` as the top of the ascender box and adds the ascender
  // to reach the baseline (GfxRenderer.cpp:477). For Noto Sans 18 the ascender
  // is 41px but digits are only 28px of actual ink sitting on the baseline, so
  // centring the ascender box leaves digits ~8px low. We instead solve for the
  // baseline that centres the ink, then subtract the ascender:
  //     inkCentre = baseline - inkTop + inkH/2  ==>  baseline = centreY + inkTop - inkH/2
  //     y = baseline - ascender
  int digitTopYForCentre(int centreY) const {
    return centreY + digitInkTop - digitInkH / 2 - numAscender;
  }
};

// Derive the layout from the renderer's current (Portrait) screen size and
// font metrics. Proportional so the grid fills the panel on both X3 and X4.
Layout computeLayout(const GfxRenderer& renderer) {
  Layout L{};
  L.screenW = renderer.getScreenWidth();
  L.screenH = renderer.getScreenHeight();

  // Respect the physical bezel margins the panel reports.
  int vTop = 0, vRight = 0, vBottom = 0, vLeft = 0;
  renderer.getOrientedViewableTRBL(&vTop, &vRight, &vBottom, &vLeft);

  L.marginX = std::max(vLeft, vRight) + 20;
  L.colW = (L.screenW - 2 * L.marginX) / COL_COUNT;

  const int titleH = renderer.getLineHeight(NOTOSANS_18_FONT_ID);
  const int weekdayH = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  const int legendLineH = renderer.getLineHeight(UI_12_FONT_ID);

  L.titleTopY = vTop + 16;
  L.weekdayTopY = L.titleTopY + titleH + 24;
  L.gridTop = L.weekdayTopY + weekdayH + 12;

  // Reserve space at the bottom for the legend, then split what remains
  // between the six week rows.
  L.legendRowH = legendLineH + 6;
  const int legendBlockH = LEGEND_MAX_ROWS * L.legendRowH;
  const int gridBottom = L.screenH - vBottom - legendBlockH - 16;
  L.rowH = (gridBottom - L.gridTop) / ROW_COUNT;
  L.legendTop = L.gridTop + ROW_COUNT * L.rowH + 16;
  // How many legend rows actually fit between the grid and the bottom bezel.
  // Covering both countries can surface more holidays than the nominal five,
  // so use the real space available instead of assuming.
  const int legendSpace = L.screenH - vBottom - L.legendTop;
  L.legendMaxRows = std::max(1, std::min(LEGEND_MAX_ROWS + 1, legendSpace / L.legendRowH));

  // Digit ink metrics, read from the font itself. Falls back to a reasonable
  // approximation if the glyph is unavailable (e.g. an SD-card font whose
  // metrics live elsewhere), so layout degrades rather than breaking.
  L.numAscender = renderer.getFontAscenderSize(NOTOSANS_18_FONT_ID);
  L.digitInkTop = (L.numAscender * 2) / 3;
  L.digitInkH = L.digitInkTop;
  const auto& fontMap = renderer.getFontMap();
  const auto fontIt = fontMap.find(NOTOSANS_18_FONT_ID);
  if (fontIt != fontMap.end()) {
    if (const EpdGlyph* zero = fontIt->second.getGlyph('0', EpdFontFamily::BOLD)) {
      L.digitInkTop = zero->top;
      L.digitInkH = zero->height;
    }
  }

  // Highlight box: square, like the reference (63x63 there), sized from the
  // digit ink and clamped so it never overflows its cell.
  const int widest = renderer.getTextWidth(NOTOSANS_18_FONT_ID, "30", EpdFontFamily::BOLD);
  L.cellBoxSide = std::min({L.colW - 6, L.rowH - 6, std::max(widest, L.digitInkH) + 20});
  L.cellRadius = 6;

  L.legendSwatchSide = std::min(16, std::max(10, legendLineH - 6));
  return L;
}

constexpr int TODAY_OUTLINE_PX = 2;

// ---------------------------------------------------------------------------
// Spanish weekday and month labels — this calendar exists so the reader can
// study Spanish and Costa Rica's public holidays, so localisation is baked in
// rather than sourced from I18n. If you're porting to another locale, replace
// these arrays (and HolidayCalculator.cpp's HOLIDAY_TABLE) together.
// ---------------------------------------------------------------------------
constexpr const char* WEEKDAYS[COL_COUNT] = {"Do", "Lu", "Ma", "Mi", "Ju", "Vi", "Sá"};
constexpr const char* MONTHS_LONG[12] = {
    "Enero",   "Febrero", "Marzo",      "Abril",   "Mayo",      "Junio",
    "Julio",   "Agosto",  "Septiembre", "Octubre", "Noviembre", "Diciembre"};
constexpr const char* MONTHS_SHORT[12] = {"ene", "feb", "mar", "abr", "may", "jun",
                                          "jul", "ago", "sep", "oct", "nov", "dic"};
// Capitalised abbreviations, used as the header fallback when the full month
// names would overflow the panel (e.g. "Diciembre 2026 – Febrero 2027").
constexpr const char* MONTHS_ABBR[12] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun",
                                         "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Distance between the `y` handed to drawText and the first row of actual ink
// for `text`. drawText's `y` is the top of the ascender box, but most glyphs
// do not reach the ascender line, so the visible gap varies per string. We
// take the tallest glyph in the string to get the true offset.
//
// Returns 0 when metrics are unavailable, which degrades to the old
// ascender-box behaviour rather than mispositioning.
int inkTopOffset(const GfxRenderer& renderer, int fontId, const char* text,
                 EpdFontFamily::Style style) {
  const auto& fontMap = renderer.getFontMap();
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) return 0;
  int maxTop = 0;
  for (const char* p = text; *p; ++p) {
    // ASCII-only scan is sufficient: month abbreviations are unaccented.
    if (static_cast<unsigned char>(*p) >= 0x80) continue;
    if (const EpdGlyph* g = it->second.getGlyph(static_cast<uint32_t>(*p), style)) {
      maxTop = std::max(maxTop, static_cast<int>(g->top));
    }
  }
  if (maxTop == 0) return 0;
  return renderer.getFontAscenderSize(fontId) - maxTop;
}

// Return the `y` for drawText such that `text`'s ink starts at `desiredInkTopY`.
int topYForInkTop(const GfxRenderer& renderer, int fontId, const char* text,
                  EpdFontFamily::Style style, int desiredInkTopY) {
  return desiredInkTopY - inkTopOffset(renderer, fontId, text, style);
}

// Return true if `date` falls on a Costa Rican holiday. Each cell resolves
// against its own year, so a grid straddling a year boundary works naturally.
bool findHolidayFor(const YMD& date, HolidayId& outId) {
  for (uint8_t i = 0; i < static_cast<uint8_t>(HolidayId::HolidayIdCount); ++i) {
    const auto id = static_cast<HolidayId>(i);
    if (sameDate(resolveHoliday(id, date.year), date)) {
      outId = id;
      return true;
    }
  }
  return false;
}

// Anchor Sunday: the Sunday of today's week. If today IS Sunday, anchor is today.
YMD anchorSundayOf(const YMD& today) {
  const uint8_t wd = weekdayOf(today.year, today.month, today.day);  // 0=Sun
  YMD anchor = today;
  if (wd != 0) addDays(anchor, -static_cast<int32_t>(wd));
  return anchor;
}

// Compose the Spanish header, e.g. "Julio – Septiembre 2026" or
// "Diciembre 2026 – Enero 2027". The dash is U+2013 (UTF-8 e2 80 93).
//
// The full-month cross-year form ("Diciembre 2026 – Febrero 2027") is wider
// than the 528px panel at 18pt bold, so we build the nicest form first and
// fall back to capitalised abbreviations when it will not fit. `maxWidth` of
// 0 disables the fallback (used by tests that only care about the text).
void formatHeader(char* buf, size_t bufSize, const YMD& first, const YMD& last,
                  const GfxRenderer* renderer = nullptr, int maxWidth = 0) {
  const bool sameYear = first.year == last.year;
  const bool sameMonth = sameYear && first.month == last.month;

  auto fits = [&](const char* s) {
    if (renderer == nullptr || maxWidth <= 0) return true;
    return renderer->getTextWidth(NOTOSANS_18_FONT_ID, s, EpdFontFamily::BOLD) <= maxWidth;
  };

  if (sameMonth) {
    // Single month always fits.
    std::snprintf(buf, bufSize, "%s %u", MONTHS_LONG[first.month - 1], first.year);
    return;
  }

  if (sameYear) {
    std::snprintf(buf, bufSize, "%s \xe2\x80\x93 %s %u", MONTHS_LONG[first.month - 1],
                  MONTHS_LONG[last.month - 1], first.year);
    if (fits(buf)) return;
    std::snprintf(buf, bufSize, "%s \xe2\x80\x93 %s %u", MONTHS_ABBR[first.month - 1],
                  MONTHS_ABBR[last.month - 1], first.year);
    return;
  }

  // Cross-year: try full names, then abbreviations, then two-digit years.
  std::snprintf(buf, bufSize, "%s %u \xe2\x80\x93 %s %u", MONTHS_LONG[first.month - 1], first.year,
                MONTHS_LONG[last.month - 1], last.year);
  if (fits(buf)) return;
  std::snprintf(buf, bufSize, "%s %u \xe2\x80\x93 %s %u", MONTHS_ABBR[first.month - 1], first.year,
                MONTHS_ABBR[last.month - 1], last.year);
  if (fits(buf)) return;
  std::snprintf(buf, bufSize, "%s \xe2\x80\x99%02u \xe2\x80\x93 %s \xe2\x80\x99%02u",
                MONTHS_ABBR[first.month - 1], first.year % 100, MONTHS_ABBR[last.month - 1],
                last.year % 100);
}

// Draw one day cell.
//
// GfxRenderer::drawText treats its `y` argument as the TOP of the text's
// ascender box (GfxRenderer.cpp:477 adds getFontAscenderSize internally to
// reach the baseline). Any rect drawn with the same top-y therefore aligns
// with the text — the idiom throughout the codebase (see ClockOffsetActivity).
// We compute the number's top-y first and derive both the highlight rect and
// the subscript from it, so they can never drift apart.
void drawDayCell(GfxRenderer& renderer, const Layout& L, int row, int col, const YMD& date,
                 bool isToday, bool isHoliday, bool showMonthSubscript) {
  const int cx = L.colCentreX(col);
  const int cy = L.rowCentreY(row);

  char numBuf[4];
  std::snprintf(numBuf, sizeof(numBuf), "%u", date.day);
  const int numFontId = NOTOSANS_18_FONT_ID;
  const int textW = renderer.getTextWidth(numFontId, numBuf, EpdFontFamily::BOLD);
  // Position by INK, not by the ascender box — see Layout::digitTopYForCentre.
  const int numTopY = L.digitTopYForCentre(cy);

  // The square highlight is centred on the same point the ink is centred on,
  // so the two can never drift apart.
  const int half = L.cellBoxSide / 2;
  const int rectX = cx - half;
  const int rectY = cy - half;

  if (isHoliday) {
    renderer.fillRoundedRect(rectX, rectY, L.cellBoxSide, L.cellBoxSide, L.cellRadius,
                             Color::LightGray);
    renderer.drawRoundedRect(rectX, rectY, L.cellBoxSide, L.cellBoxSide, /*lineWidth=*/1,
                             L.cellRadius, /*state=*/true);
  }
  if (isToday) {
    // Bold outline; visually distinct from the holiday shade so the two can
    // coexist in the same cell.
    renderer.drawRoundedRect(rectX, rectY, L.cellBoxSide, L.cellBoxSide, TODAY_OUTLINE_PX,
                             L.cellRadius, /*state=*/true);
  }

  renderer.drawText(numFontId, cx - textW / 2, numTopY, numBuf, /*black=*/true,
                    EpdFontFamily::BOLD);

  // Month-rollover subscript ("ago" under the "1" of the next month). Its ink
  // starts just below the digit's baseline — and below the highlight box when
  // one is present, so the two never collide.
  if (showMonthSubscript) {
    const char* mon = MONTHS_SHORT[date.month - 1];
    const int subW = renderer.getTextWidth(SMALL_FONT_ID, mon);
    const int baseline = numTopY + L.numAscender;
    int inkTop = baseline + 3;
    if (isHoliday || isToday) {
      inkTop = std::max(inkTop, rectY + L.cellBoxSide + 2);
    }
    const int subY = topYForInkTop(renderer, SMALL_FONT_ID, mon, EpdFontFamily::REGULAR, inkTop);
    renderer.drawText(SMALL_FONT_ID, cx - subW / 2, subY, mon, /*black=*/true);
  }
}

void drawHeader(GfxRenderer& renderer, const Layout& L, const YMD& first, const YMD& last) {
  char header[80];
  // drawCenteredText does not truncate, so an over-wide header would clip at
  // both panel edges. Give formatHeader the budget so it can pick a form that
  // fits; truncate as a last resort.
  const int avail = L.screenW - 2 * L.marginX;
  formatHeader(header, sizeof(header), first, last, &renderer, avail);
  const std::string fitted = renderer.truncatedText(NOTOSANS_18_FONT_ID, header, avail,
                                                    EpdFontFamily::BOLD);
  renderer.drawCenteredText(NOTOSANS_18_FONT_ID, L.titleTopY, fitted.c_str(), /*black=*/true,
                            EpdFontFamily::BOLD);
}

void drawWeekdayRow(GfxRenderer& renderer, const Layout& L) {
  for (int c = 0; c < COL_COUNT; ++c) {
    const int textW = renderer.getTextWidth(NOTOSANS_14_FONT_ID, WEEKDAYS[c], EpdFontFamily::BOLD);
    renderer.drawText(NOTOSANS_14_FONT_ID, L.colCentreX(c) - textW / 2, L.weekdayTopY, WEEKDAYS[c],
                      /*black=*/true, EpdFontFamily::BOLD);
  }
}

// Collect the holidays visible in the grid and draw a legend below it,
// ordered by date. Entries are tagged CR / US / CR·US so the reader can tell
// which country observes each one.
//
// With both countries in play a 6-week window can hold more holidays than
// there are legend rows (late November alone has Acción de Gracias, Viernes
// Negro, Día de los Veteranos and Abolición del Ejército). When the list
// overflows we show as many as fit and a final "+N más" line rather than
// silently dropping the tail.
int drawLegend(GfxRenderer& renderer, const Layout& L, const YMD& first, const YMD& last) {
  struct Entry {
    YMD date;
    HolidayId id;
  };
  // Upper bound: every holiday in the table could in principle fall inside the
  // window across two years. Stack-allocated, no heap.
  constexpr int MAX_FOUND = static_cast<int>(HolidayId::HolidayIdCount);
  Entry found[MAX_FOUND];
  int found_n = 0;

  const uint16_t years[2] = {first.year, last.year};
  const int yearIterations = (first.year != last.year) ? 2 : 1;

  for (int yi = 0; yi < yearIterations; ++yi) {
    for (uint8_t i = 0;
         i < static_cast<uint8_t>(HolidayId::HolidayIdCount) && found_n < MAX_FOUND; ++i) {
      const auto id = static_cast<HolidayId>(i);
      const YMD d = resolveHoliday(id, years[yi]);
      const bool inRange = !dateLess(d, first) && !dateLess(last, d);
      if (inRange) {
        found[found_n++] = {d, id};
      }
    }
  }

  // Chronological order. Insertion sort: n is at most a couple of dozen and
  // this avoids pulling in <algorithm>'s sort machinery for a tiny array.
  for (int i = 1; i < found_n; ++i) {
    const Entry key = found[i];
    int j = i - 1;
    while (j >= 0 && dateLess(key.date, found[j].date)) {
      found[j + 1] = found[j];
      --j;
    }
    found[j + 1] = key;
  }

  const int rows = std::min(found_n, L.legendMaxRows);
  // If we cannot show everything, give the last row over to the "+N más" note.
  const bool overflow = found_n > L.legendMaxRows;
  const int listed = overflow ? rows - 1 : rows;

  for (int i = 0; i < listed; ++i) {
    const int y = L.legendTop + i * L.legendRowH;
    renderer.fillRoundedRect(L.marginX, y, L.legendSwatchSide, L.legendSwatchSide, 3,
                             Color::LightGray);
    renderer.drawRoundedRect(L.marginX, y, L.legendSwatchSide, L.legendSwatchSide, 1, 3, true);

    const HolidayEntry& he = HOLIDAY_TABLE[static_cast<size_t>(found[i].id)];
    const uint8_t regions = he.regions;
    const char* tag = (regions == (REGION_CR | REGION_US)) ? "CR+US"
                      : (regions & REGION_US)              ? "US"
                                                           : "CR";
    char line[112];
    // "3 abr  CR  Viernes Santo"
    std::snprintf(line, sizeof(line), "%u %s  %s  %s", found[i].date.day,
                  MONTHS_SHORT[found[i].date.month - 1], tag, he.shortLabel);
    const int availW = L.screenW - 2 * L.marginX - L.legendSwatchSide - 10;
    const std::string fitted = renderer.truncatedText(UI_12_FONT_ID, line, availW);
    renderer.drawText(UI_12_FONT_ID, L.marginX + L.legendSwatchSide + 10, y, fitted.c_str(),
                      /*black=*/true);
  }

  if (overflow) {
    const int y = L.legendTop + listed * L.legendRowH;
    char more[32];
    std::snprintf(more, sizeof(more), "+%d más", found_n - listed);
    renderer.drawText(UI_12_FONT_ID, L.marginX + L.legendSwatchSide + 10, y, more, /*black=*/true);
  }

  return rows;
}

// ---------------------------------------------------------------------------
// BMP serialisation
// ---------------------------------------------------------------------------

// Read one logical (Portrait) pixel out of the physical landscape framebuffer.
//
// GfxRenderer::drawPixel rotates Portrait logical coords into the panel with
// (see rotateCoordinates, GfxRenderer.cpp:187):
//     phyX = y
//     phyY = panelHeight - 1 - x
// and writes 1bpp MSB-first where a SET bit is white (drawPixel clears the bit
// for state=true / black). We invert that mapping here.
inline bool logicalPixelIsWhite(const uint8_t* fb, int panelHeight, int panelWidthBytes,
                                int logicalX, int logicalY) {
  const int px = logicalY;
  const int py = (panelHeight - 1) - logicalX;
  const int byte = py * panelWidthBytes + (px >> 3);
  const int bit = 7 - (px & 7);
  return (fb[byte] >> bit) & 0x1;
}

// Write a 1-bit portrait BMP (bottom-up rows, standard Windows layout)
// reflecting the current framebuffer content.
bool writeMonoPortraitBmp(HalFile& file, const GfxRenderer& renderer) {
  const int W = renderer.getScreenWidth();
  const int H = renderer.getScreenHeight();
  const int panelHeight = static_cast<int>(renderer.getDisplayHeight());
  const int panelWidthBytes = static_cast<int>(renderer.getDisplayWidthBytes());

  const int rowBytes = ((W + 31) / 32) * 4;  // BMP rows are 4-byte aligned
  const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes) * static_cast<uint32_t>(H);
  constexpr int PALETTE_BYTES = 8;                       // two 4-byte RGBQuads
  constexpr int HEADER_BYTES = 14 + 40 + PALETTE_BYTES;  // 62
  const uint32_t fileSize = HEADER_BYTES + pixelBytes;

  uint8_t hdr[HEADER_BYTES] = {0};
  // -- BITMAPFILEHEADER (14 bytes) --
  hdr[0] = 'B';
  hdr[1] = 'M';
  hdr[2] = fileSize & 0xFF;
  hdr[3] = (fileSize >> 8) & 0xFF;
  hdr[4] = (fileSize >> 16) & 0xFF;
  hdr[5] = (fileSize >> 24) & 0xFF;
  hdr[10] = HEADER_BYTES & 0xFF;  // bfOffBits
  hdr[11] = (HEADER_BYTES >> 8) & 0xFF;
  // -- BITMAPINFOHEADER (40 bytes, starts at offset 14) --
  hdr[14] = 40;  // biSize
  hdr[18] = W & 0xFF;
  hdr[19] = (W >> 8) & 0xFF;  // biWidth
  hdr[22] = H & 0xFF;
  hdr[23] = (H >> 8) & 0xFF;  // biHeight (positive => bottom-up)
  hdr[26] = 1;                // biPlanes
  hdr[28] = 1;                // biBitCount
  // biCompression = 0 (BI_RGB); biSizeImage = 0 is legal for BI_RGB
  hdr[38] = 0x13;
  hdr[39] = 0x0B;  // biXPelsPerMeter ~2835 (72dpi)
  hdr[42] = 0x13;
  hdr[43] = 0x0B;  // biYPelsPerMeter
  hdr[46] = 2;     // biClrUsed
  // -- Palette at offset 54: index 0 = black, index 1 = white (BGRA) --
  hdr[58] = 0xFF;
  hdr[59] = 0xFF;
  hdr[60] = 0xFF;

  if (file.write(hdr, HEADER_BYTES) != static_cast<size_t>(HEADER_BYTES)) {
    LOG_ERR("CAL", "BMP header short write");
    return false;
  }

  const uint8_t* fb = renderer.getFrameBuffer();
  if (fb == nullptr) {
    LOG_ERR("CAL", "framebuffer null; cannot serialise");
    return false;
  }

  // One heap row buffer (68 bytes on X3) instead of a variable-length array —
  // rowBytes is runtime-derived, and CLAUDE.md caps stack locals at 256 bytes.
  auto rowBuf = makeUniqueNoThrow<uint8_t[]>(static_cast<size_t>(rowBytes));
  if (!rowBuf) {
    LOG_ERR("CAL", "OOM: %d bytes for BMP row", rowBytes);
    return false;
  }

  for (int y = H - 1; y >= 0; --y) {  // BMP rows are stored bottom-up
    std::memset(rowBuf.get(), 0, static_cast<size_t>(rowBytes));
    for (int x = 0; x < W; ++x) {
      if (logicalPixelIsWhite(fb, panelHeight, panelWidthBytes, x, y)) {
        rowBuf[x >> 3] |= (0x80 >> (x & 7));  // set bit = white
      }
    }
    if (file.write(rowBuf.get(), rowBytes) != static_cast<size_t>(rowBytes)) {
      LOG_ERR("CAL", "BMP row %d short write", y);
      return false;
    }
  }
  return true;
}

constexpr const char* STAMP_PATH = "/.crosspoint/sleep_calendar.stamp";

// Bump whenever the rendered output changes for the same date — layout
// constants, fonts, holiday table, label strings, anything visual.
//
// The stamp records this alongside the date. Keying staleness on the date
// alone means a firmware update that changes the layout keeps showing the
// previously cached /sleep.bmp until the calendar day rolls over, which
// silently masks the change (see the simulator's note about stale ./fs_
// caches hiding fixes). Same rationale as SECTION_FILE_VERSION in the EPUB
// cache: version the artifact, not just its inputs.
constexpr uint8_t CALENDAR_RENDER_VERSION = 2;

// Stamp payload: "YYYYMMDDvNN" — 8 date digits, 'v', 2 version digits.
constexpr size_t STAMP_LEN = 11;


}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CalendarSleepScreen::render(GfxRenderer& renderer, const YMD& today) {
  if (!isValidDate(today.year, today.month, today.day)) {
    LOG_ERR("CAL", "invalid today: %u-%u-%u", today.year, today.month, today.day);
    return;
  }

  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();

  const Layout L = computeLayout(renderer);

  const YMD anchor = anchorSundayOf(today);
  YMD last = anchor;
  addDays(last, ROW_COUNT * COL_COUNT - 1);

  drawHeader(renderer, L, anchor, last);
  drawWeekdayRow(renderer, L);

  YMD cursor = anchor;
  for (int row = 0; row < ROW_COUNT; ++row) {
    for (int col = 0; col < COL_COUNT; ++col) {
      HolidayId hid;
      const bool isHoliday = findHolidayFor(cursor, hid);
      const bool isToday = sameDate(cursor, today);
      const bool showSub = cursor.day == 1 && cursor.month != anchor.month;
      drawDayCell(renderer, L, row, col, cursor, isToday, isHoliday, showSub);
      addDays(cursor, 1);
    }
  }

  drawLegend(renderer, L, anchor, last);
}

bool CalendarSleepScreen::serialiseFramebufferAsPortraitBmp(GfxRenderer& renderer,
                                                            const char* sdPath) {
  HalFile file;
  if (!Storage.openFileForWrite("CAL", sdPath, file)) {
    LOG_ERR("CAL", "openFileForWrite failed: %s", sdPath);
    return false;
  }
  const bool ok = writeMonoPortraitBmp(file, renderer);
  file.close();  // explicit: renderCustomSleepScreen reopens this path next
  if (!ok) LOG_ERR("CAL", "writeMonoPortraitBmp failed: %s", sdPath);
  return ok;
}

bool CalendarSleepScreen::readStamp(YMD& out) {
  HalFile file;
  if (!Storage.openFileForRead("CAL", STAMP_PATH, file)) {
    return false;
  }
  char buf[16] = {0};
  const int n = file.read(buf, sizeof(buf) - 1);
  if (n < static_cast<int>(STAMP_LEN)) return false;  // pre-versioned or truncated stamp
  for (int i = 0; i < 8; ++i) {
    if (buf[i] < '0' || buf[i] > '9') return false;  // reject a corrupted stamp
  }
  if (buf[8] != 'v' || buf[9] < '0' || buf[9] > '9' || buf[10] < '0' || buf[10] > '9') {
    return false;
  }
  // A stamp written by a build with different render code is treated as
  // stale, so a layout change regenerates immediately instead of waiting for
  // the date to roll over.
  const uint8_t version = static_cast<uint8_t>((buf[9] - '0') * 10 + (buf[10] - '0'));
  if (version != CALENDAR_RENDER_VERSION) {
    LOG_INF("CAL", "stamp version %u != %u, forcing regen", version, CALENDAR_RENDER_VERSION);
    return false;
  }
  // Format: "YYYYMMDD"
  uint16_t y = 0;
  uint8_t m = 0, d = 0;
  for (int i = 0; i < 4; ++i) y = static_cast<uint16_t>(y * 10 + (buf[i] - '0'));
  for (int i = 4; i < 6; ++i) m = static_cast<uint8_t>(m * 10 + (buf[i] - '0'));
  for (int i = 6; i < 8; ++i) d = static_cast<uint8_t>(d * 10 + (buf[i] - '0'));
  if (!isValidDate(y, m, d)) return false;
  out = YMD{y, m, d};
  return true;
}

bool CalendarSleepScreen::writeStamp(const YMD& date) {
  Storage.mkdir("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("CAL", STAMP_PATH, file)) {
    LOG_ERR("CAL", "openFileForWrite failed: %s", STAMP_PATH);
    return false;
  }
  char buf[STAMP_LEN + 1];
  const int n = std::snprintf(buf, sizeof(buf), "%04u%02u%02uv%02u", date.year, date.month,
                              date.day, CALENDAR_RENDER_VERSION);
  const bool ok = (n == static_cast<int>(STAMP_LEN)) &&
                  (file.write(buf, STAMP_LEN) == STAMP_LEN);
  file.close();
  return ok;
}

bool CalendarSleepScreen::clearStamp() { return Storage.remove(STAMP_PATH); }

}  // namespace calendar
