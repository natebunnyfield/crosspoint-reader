#include "CalendarSleepScreen.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <string>

#include "HolidayCalculator.h"
#include "SdCardFontSystem.h"
#include "WestsideCalendar_2026_2027.h"
#include "fontIds.h"

// Costa Rican + US holiday calendar (4/5/6 week rows — see render()'s `weeks`
// parameter), drawn straight into the panel framebuffer.
//
// All geometry derives from renderer.getScreenWidth()/getScreenHeight() at
// runtime rather than hardcoded, per the "No Hardcoding" rule in CLAUDE.md.
// The X3 portrait panel is 528x792, the X4 is 480x800; this layout works on
// both.
//
// Vertical placement note that governs most of this file: GfxRenderer::drawText
// treats its `y` as the TOP OF THE ASCENDER BOX and adds the ascender
// internally to reach the baseline (GfxRenderer.cpp:477). The ascender is much
// taller than most ink — Noto Sans 18's is 41px while digits are 28px — so
// aligning anything to that `y` puts it visibly high. Everything here is
// therefore positioned against measured glyph ink via inkBox()/topYForInkTop().
namespace calendar {
namespace {

// ---------------------------------------------------------------------------
// Grid shape
// ---------------------------------------------------------------------------
// Week-row count is a render() parameter ("Calendar Four/Five/Six" styles pick
// 4/5/6; the classic "Calendar" style keeps the original 5). These bound it:
// fewer than 4 rows makes the look-ahead pointless, more than 6 leaves rows
// shorter than the digit ink on the 792px panel.
constexpr int WEEKS_MIN = 4;
constexpr int WEEKS_MAX = 6;
constexpr int COL_COUNT = 7;
constexpr int LEGEND_MAX_ROWS = 6;
// Border weight shared by the today and holiday squares — they are the same
// shape, distinguished only by fill.
constexpr int CELL_BORDER_PX = 1;

// Type styles. Day numbers carry the grid, so they stay upright and quiet;
// the header is set in italic to read as labelling rather than data.
constexpr EpdFontFamily::Style HEADER_STYLE = EpdFontFamily::ITALIC;
constexpr EpdFontFamily::Style DAY_STYLE = EpdFontFamily::REGULAR;
constexpr EpdFontFamily::Style LEGEND_STYLE = EpdFontFamily::REGULAR;

// ---------------------------------------------------------------------------
// Spanish month labels. This calendar is a Spanish-learning aid, so
// localisation is baked in rather than sourced from I18n. To port to another
// locale, replace these arrays and HolidayCalculator.cpp's HOLIDAY_TABLE
// together.
// ---------------------------------------------------------------------------
constexpr const char* MONTHS_LONG[12] = {
    "Enero",   "Febrero", "Marzo",      "Abril",   "Mayo",      "Junio",
    "Julio",   "Agosto",  "Septiembre", "Octubre", "Noviembre", "Diciembre"};
constexpr const char* MONTHS_SHORT[12] = {"ene", "feb", "mar", "abr", "may", "jun",
                                          "jul", "ago", "sep", "oct", "nov", "dic"};
// Capitalised abbreviations for the header fallback when full month names
// would overflow (e.g. "Diciembre 2026 – Febrero 2027" exceeds 528px at 18pt).
constexpr const char* MONTHS_ABBR[12] = {"Ene", "Feb", "Mar", "Abr", "May", "Jun",
                                         "Jul", "Ago", "Sep", "Oct", "Nov", "Dic"};

// ---------------------------------------------------------------------------
// English month labels (WestsideEN style).
// ---------------------------------------------------------------------------
constexpr const char* MONTHS_LONG_EN[12] = {
    "January", "February", "March",     "April",   "May",      "June",
    "July",    "August",   "September", "October", "November", "December"};
constexpr const char* MONTHS_SHORT_EN[12] = {"jan", "feb", "mar", "apr", "may", "jun",
                                             "jul", "aug", "sep", "oct", "nov", "dec"};
// Capitalised abbreviations for the header fallback in the WestsideEN path.
constexpr const char* MONTHS_ABBR_EN[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// English short labels for REGION_US holidays, indexed by HolidayId.
// CR-only entries are nullptr and are never reached in the WestsideEN legend.
// Order must match the HolidayId enum in HolidayCalculator.h exactly.
static const char* const US_HOLIDAY_LABEL_EN[static_cast<size_t>(HolidayId::HolidayIdCount)] = {
    "New Year's Day",          // AnoNuevo            CR+US
    "MLK Jr. Day",             // DiaDeMlk            US
    "Presidents' Day",         // DiaDeLosPresidentes US
    nullptr,                   // JuevesSanto         CR only
    nullptr,                   // ViernesSanto        CR only
    nullptr,                   // JuanSantamaria      CR only
    nullptr,                   // DiaDelTrabajoCR     CR only
    "Memorial Day",            // DiaDeLosCaidos      US
    "Juneteenth",              // Juneteenth          US
    "Independence Day",        // IndependenciaUS     US
    nullptr,                   // AnexionNicoya       CR only
    nullptr,                   // VirgenDeLosAngeles  CR only
    nullptr,                   // DiaDeLaMadre        CR only
    nullptr,                   // DiaDeLaPersonaNegra CR only
    "Labor Day",               // DiaDelTrabajoUS     US
    nullptr,                   // IndependenciaCR     CR only
    "Indigenous Peoples' Day", // DiaDeLosPueblosIndigenas US
    nullptr,                   // EncuentroDeLasCulturas   CR only
    "Veterans Day",            // DiaDeLosVeteranos   US
    "Thanksgiving",            // AccionDeGracias     US
    "Black Friday",            // ViernesNegro        US
    nullptr,                   // AbolicionDelEjercito CR only
    "Christmas Eve",           // NocheBuena          US
    "Christmas",               // Navidad             CR+US
};

// ---------------------------------------------------------------------------
// Font selection
//
// The calendar prefers GT Alpina Condensed from the SD card, falling back to
// the built-in Noto Serif.
//
// SdCardFontManager keeps exactly ONE family at ONE point size resident (see
// SdCardFontManager.h) to bound interval/kern table memory, so an SD face can
// only cover one of the calendar's three text roles. It is spent on the largest
// and most identity-defining one — the header and day numbers. The legend and
// month subscript stay on built-in faces at their own sizes, because giving
// each its own SD size would mean two more resident .cpfont loads on a 380KB
// device.
//
// Loading here evicts the reader font, which is safe on this path only: sleep
// is a deep sleep, so the chip resets on wake and setup() rebuilds font state
// from the saved selection.
// ---------------------------------------------------------------------------
constexpr const char* PREFERRED_SD_FAMILY = "GTAlpinaCond";  // GT Alpina Condensed

struct CalendarFonts {
  int display;    // header + day numbers
  int legend;     // legend rows
  int subscript;  // month-rollover subscript
};

// First of `candidates` that is actually registered with the renderer. Guards
// against builds that omit a face (OMIT_FONTS) rather than drawing nothing.
int firstAvailable(const GfxRenderer& renderer, std::initializer_list<int> candidates) {
  const auto& fontMap = renderer.getFontMap();
  for (const int id : candidates) {
    if (id != 0 && fontMap.count(id) > 0) return id;
  }
  return 0;
}

CalendarFonts resolveFonts(GfxRenderer& renderer) {
  CalendarFonts f{};
  // 18pt is the largest size in the standard pack; loadForDisplay snaps to the
  // closest installed size if this exact one is missing.
  const int sdId = sdFontSystem.loadForDisplay(PREFERRED_SD_FAMILY, 18, renderer);
  if (sdId != 0) {
    f.display = sdId;
  } else {
    LOG_DBG("CAL", "%s unavailable; using Noto Serif", PREFERRED_SD_FAMILY);
    f.display = firstAvailable(renderer, {NOTOSERIF_18_FONT_ID, NOTOSANS_18_FONT_ID});
  }
  f.legend = firstAvailable(renderer, {NOTOSERIF_12_FONT_ID, UI_12_FONT_ID});
  f.subscript = firstAvailable(renderer, {SMALL_FONT_ID, UI_12_FONT_ID});
  return f;
}

// ---------------------------------------------------------------------------
// Glyph ink measurement
// ---------------------------------------------------------------------------

// Ink extent of `text` relative to the `y` handed to drawText.
// `topOffset` is the gap from that y down to the first inked row; `height` is
// the inked height. Both are 0 when metrics are unavailable, which degrades to
// the old ascender-box behaviour instead of mispositioning.
struct InkBox {
  int topOffset = 0;
  int height = 0;
};

InkBox inkBox(const GfxRenderer& renderer, int fontId, const char* text,
              EpdFontFamily::Style style) {
  InkBox box;
  const auto& fontMap = renderer.getFontMap();
  const auto it = fontMap.find(fontId);
  if (it == fontMap.end()) return box;

  int maxTop = 0;      // tallest ascent above the baseline
  int maxBelow = 0;    // deepest descent below it
  bool any = false;
  for (const char* p = text; *p; ++p) {
    // ASCII scan is enough: the strings measured here are digits and unaccented
    // month abbreviations. Accented bytes are skipped, not mismeasured.
    if (static_cast<unsigned char>(*p) >= 0x80) continue;
    if (const EpdGlyph* g = it->second.getGlyph(static_cast<uint32_t>(*p), style)) {
      if (g->height == 0) continue;  // spaces carry no ink
      maxTop = std::max(maxTop, static_cast<int>(g->top));
      maxBelow = std::max(maxBelow, static_cast<int>(g->height) - static_cast<int>(g->top));
      any = true;
    }
  }
  if (!any || maxTop == 0) return box;

  box.topOffset = renderer.getFontAscenderSize(fontId) - maxTop;
  box.height = maxTop + maxBelow;
  return box;
}

// `y` for drawText such that `text`'s ink is vertically centred on `centreY`.
int topYForInkCentre(const GfxRenderer& renderer, int fontId, const char* text,
                     EpdFontFamily::Style style, int centreY) {
  const InkBox box = inkBox(renderer, fontId, text, style);
  return centreY - box.height / 2 - box.topOffset;
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
struct Layout {
  CalendarFonts fonts;
  Style style;  // data source / locale selection
  int rows;  // week rows in this render
  int screenW;
  int screenH;
  int marginX;
  int titleCentreY;  // ink centre of the header
  int gridTop;
  int rowH;
  int colW;
  int cellBoxSide;  // square highlight, like the reference
  int cellRadius;

  // Legend geometry. Column x-positions are NOT fixed here: they depend on
  // which holidays are actually in view, so LegendColumns computes them per
  // render (see legendColumnsFor).
  int legendTop;     // earliest the legend may start, i.e. just under the grid
  int legendBottomY;  // usable bottom edge; the legend block is flush to this
  int legendRowH;
  int legendMaxRows;

  int colCentreX(int col) const { return marginX + col * colW + colW / 2; }
  int rowCentreY(int row) const { return gridTop + row * rowH + rowH / 2; }
  // blockTop rather than legendTop: the legend is bottom-aligned, so its origin
  // depends on how many rows are actually in view and is only known at draw
  // time. See drawLegend().
  int legendRowCentreY(int row, int blockTop) const {
    return blockTop + row * legendRowH + legendRowH / 2;
  }
};

Layout computeLayout(const GfxRenderer& renderer, const CalendarFonts& fonts, const int weeks,
                     const Style style) {
  Layout L{};
  L.fonts = fonts;
  L.style = style;
  L.rows = weeks;
  L.screenW = renderer.getScreenWidth();
  L.screenH = renderer.getScreenHeight();

  int vTop = 0, vRight = 0, vBottom = 0, vLeft = 0;
  renderer.getOrientedViewableTRBL(&vTop, &vRight, &vBottom, &vLeft);

  L.marginX = std::max(vLeft, vRight) + 20;
  L.colW = (L.screenW - 2 * L.marginX) / COL_COUNT;

  const int titleH = renderer.getLineHeight(fonts.display);
  const int legendLineH = renderer.getLineHeight(fonts.legend);

  L.titleCentreY = vTop + 16 + titleH / 2;
  // Grid hangs directly off the header now that the weekday row is gone. The
  // space it occupied is not reclaimed as padding: gridTop rising while
  // gridBottom stays put feeds straight into rowH below, so the week rows grow.
  L.gridTop = L.titleCentreY + titleH / 2 + 20;

  // Reserve the legend block, then split what remains across the week rows.
  L.legendRowH = legendLineH + 8;
  const int legendBlockH = LEGEND_MAX_ROWS * L.legendRowH;
  // The grid band (header bottom to legend top) is fixed; the week count only
  // decides how many rows it is divided into, so "Calendar Four" gets taller
  // cells and "Calendar Six" shorter ones, never a taller or shorter screen.
  const int gridBottom = L.screenH - vBottom - legendBlockH - 20;
  L.rowH = (gridBottom - L.gridTop) / L.rows;
  L.legendTop = L.gridTop + L.rows * L.rowH + 20;
  L.legendBottomY = L.screenH - vBottom;

  const int legendSpace = L.legendBottomY - L.legendTop;
  L.legendMaxRows = std::max(1, std::min(LEGEND_MAX_ROWS, legendSpace / L.legendRowH));

  // Square highlight sized from the digit ink, clamped to the cell.
  const InkBox digits = inkBox(renderer, fonts.display, "0", DAY_STYLE);
  const int widest = renderer.getTextWidth(fonts.display, "30", DAY_STYLE);
  L.cellBoxSide = std::min({L.colW - 6, L.rowH - 6, std::max(widest, digits.height) + 20});
  L.cellRadius = 6;

  return L;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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

// Sunday of today's week. If today IS Sunday, that is the anchor.
YMD anchorSundayOf(const YMD& today) {
  const uint8_t wd = weekdayOf(today.year, today.month, today.day);  // 0=Sun
  YMD anchor = today;
  if (wd != 0) addDays(anchor, -static_cast<int32_t>(wd));
  return anchor;
}

const char* regionTag(uint8_t regions) {
  if ((regions & (REGION_CR | REGION_US)) == (REGION_CR | REGION_US)) return "CR+US";
  return (regions & REGION_US) ? "US" : "CR";
}

// Header text, degrading to a shorter form when the full one will not fit.
//
// The year is deliberately omitted. The window is always the 4-6 weeks around
// today, so the year is never genuinely in question, and dropping it removes
// the two widest cases the old code had to degrade through (a full
// "Diciembre 2026 – Febrero 2027" and its ’26/’27 last resort).
void formatHeader(char* buf, size_t bufSize, const YMD& first, const YMD& last,
                  const GfxRenderer& renderer, int displayFontId, int maxWidth, Style style) {
  const char* const* longNames = (style == Style::WestsideEN) ? MONTHS_LONG_EN : MONTHS_LONG;
  const char* const* abbrNames = (style == Style::WestsideEN) ? MONTHS_ABBR_EN : MONTHS_ABBR;

  auto fits = [&](const char* s) {
    return renderer.getTextWidth(displayFontId, s, HEADER_STYLE) <= maxWidth;
  };

  // A December–February window spans two years but still reads as one month
  // range, so only the months are compared here.
  if (first.month == last.month) {
    std::snprintf(buf, bufSize, "%s", longNames[first.month - 1]);
    return;
  }

  std::snprintf(buf, bufSize, "%s \xe2\x80\x93 %s", longNames[first.month - 1],
                longNames[last.month - 1]);
  if (fits(buf)) return;
  std::snprintf(buf, bufSize, "%s \xe2\x80\x93 %s", abbrNames[first.month - 1],
                abbrNames[last.month - 1]);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void drawDayCell(GfxRenderer& renderer, const Layout& L, int row, int col, const YMD& date,
                 bool isToday, bool isHoliday, bool showMonthSubscript) {
  const int cx = L.colCentreX(col);
  const int cy = L.rowCentreY(row);

  char numBuf[4];
  std::snprintf(numBuf, sizeof(numBuf), "%u", date.day);
  const int textW = renderer.getTextWidth(L.fonts.display, numBuf, DAY_STYLE);
  const int numTopY =
      topYForInkCentre(renderer, L.fonts.display, numBuf, DAY_STYLE, cy);

  // The square is centred on the same point as the digit ink, so the two
  // cannot drift apart.
  const int half = L.cellBoxSide / 2;
  const int rectX = cx - half;
  const int rectY = cy - half;

  // Every marked cell is the same square with the same hairline border; only
  // the fill differs, so the shapes read as one family:
  //   today    -> solid black fill, number knocked out in white
  //   holiday  -> mid-grey fill (dithered on the 1-bit panel), black number
  // Today wins when a day is both, since black is the stronger fill and the
  // date still appears in the legend as a holiday.
  const bool inverted = isToday;
  if (isToday || isHoliday) {
    renderer.fillRoundedRect(rectX, rectY, L.cellBoxSide, L.cellBoxSide, L.cellRadius,
                             isToday ? Color::Black : Color::LightGray);
    renderer.drawRoundedRect(rectX, rectY, L.cellBoxSide, L.cellBoxSide, CELL_BORDER_PX,
                             L.cellRadius, /*state=*/true);
  }

  renderer.drawText(L.fonts.display, cx - textW / 2, numTopY, numBuf,
                    /*black=*/!inverted, DAY_STYLE);

  // Month-rollover subscript, below both the digit and the highlight square.
  if (showMonthSubscript) {
    const char* mon = (L.style == Style::WestsideEN) ? MONTHS_SHORT_EN[date.month - 1]
                                                     : MONTHS_SHORT[date.month - 1];
    const int subW = renderer.getTextWidth(L.fonts.subscript, mon);
    const InkBox sub = inkBox(renderer, L.fonts.subscript, mon, EpdFontFamily::REGULAR);
    const InkBox num = inkBox(renderer, L.fonts.display, numBuf, DAY_STYLE);
    int inkTop = numTopY + num.topOffset + num.height + 3;
    if (isHoliday || isToday) inkTop = std::max(inkTop, rectY + L.cellBoxSide + 2);
    renderer.drawText(L.fonts.subscript, cx - subW / 2, inkTop - sub.topOffset, mon, /*black=*/true);
  }
}

void drawHeader(GfxRenderer& renderer, const Layout& L, const YMD& first, const YMD& last) {
  char header[80];
  const int avail = L.screenW - 2 * L.marginX;
  formatHeader(header, sizeof(header), first, last, renderer, L.fonts.display, avail, L.style);
  const std::string fitted =
      renderer.truncatedText(L.fonts.display, header, avail, HEADER_STYLE);
  const int y = topYForInkCentre(renderer, L.fonts.display, fitted.c_str(),
                                 HEADER_STYLE, L.titleCentreY);
  renderer.drawCenteredText(L.fonts.display, y, fitted.c_str(), /*black=*/true,
                            HEADER_STYLE);
}

// Legend: one row per visible holiday, ordered by date, laid out as columns so
// the day numbers, months, region tags and names each line up down the page.
//
//   25  jul  CR     Anexión del Partido de Nicoya
//    2  ago  CR+US  Navidad
//  right left left  left, given every pixel the other columns do not need
int drawLegend(GfxRenderer& renderer, const Layout& L, const YMD& first, const YMD& last) {
  struct Entry {
    YMD date;
    HolidayId id;
  };
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
      if (!dateLess(d, first) && !dateLess(last, d)) found[found_n++] = {d, id};
    }
  }
  if (found_n == 0) return 0;

  // Chronological. Insertion sort: n is at most a couple of dozen, and this
  // avoids instantiating <algorithm>'s sort for a tiny array.
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
  const bool overflow = found_n > L.legendMaxRows;
  const int listed = overflow ? rows - 1 : rows;

  // Bottom-align the block. The reserve in computeLayout is sized for the
  // worst case (LEGEND_MAX_ROWS), so on a typical month the legend would
  // otherwise hang under the grid with the unused rows as dead space trailing
  // off the bottom of the screen. Anchoring to the bottom edge instead puts
  // all of that slack into a single gap between the grid and the legend.
  // Clamped at legendTop so a full-length legend can never ride up into the
  // last week row.
  const int blockTop = std::max(L.legendTop, L.legendBottomY - rows * L.legendRowH);

  // Size the leading columns to the widest value ACTUALLY in view, not the
  // widest possible one. Reserving room for "30", the longest month and
  // "CR+US" costs the name column real width in the common case where every
  // entry is single-digit or single-region — and the name is what gets
  // truncated. Measured over the listed rows only; the overflow row does not
  // use these columns.
  constexpr int GUTTER = 10;
  int dayW = 0, monthW = 0, tagW = 0;
  for (int i = 0; i < listed; ++i) {
    char dayBuf[4];
    std::snprintf(dayBuf, sizeof(dayBuf), "%u", found[i].date.day);
    dayW = std::max(dayW, renderer.getTextWidth(L.fonts.legend, dayBuf, LEGEND_STYLE));
    monthW = std::max(monthW, renderer.getTextWidth(L.fonts.legend,
                                                    MONTHS_SHORT[found[i].date.month - 1],
                                                    LEGEND_STYLE));
    const uint8_t regions = HOLIDAY_TABLE[static_cast<size_t>(found[i].id)].regions;
    tagW = std::max(tagW, renderer.getTextWidth(L.fonts.legend, regionTag(regions), LEGEND_STYLE));
  }

  const int dayRight = L.marginX + dayW;
  const int monthLeft = dayRight + GUTTER;
  const int tagLeft = monthLeft + monthW + GUTTER;
  const int nameLeft = tagLeft + tagW + GUTTER;
  const int nameW = L.screenW - L.marginX - nameLeft;

  for (int i = 0; i < listed; ++i) {
    const int cy = L.legendRowCentreY(i, blockTop);
    const HolidayEntry& he = HOLIDAY_TABLE[static_cast<size_t>(found[i].id)];
    const int y = topYForInkCentre(renderer, L.fonts.legend, "0", LEGEND_STYLE, cy);

    char dayBuf[4];
    std::snprintf(dayBuf, sizeof(dayBuf), "%u", found[i].date.day);
    const int w = renderer.getTextWidth(L.fonts.legend, dayBuf, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, dayRight - w, y, dayBuf, /*black=*/true, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, monthLeft, y, MONTHS_SHORT[found[i].date.month - 1],
                      /*black=*/true, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, tagLeft, y, regionTag(he.regions), /*black=*/true,
                      LEGEND_STYLE);
    const std::string name =
        renderer.truncatedText(L.fonts.legend, he.shortLabel, nameW, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, nameLeft, y, name.c_str(), /*black=*/true, LEGEND_STYLE);
  }

  if (overflow) {
    const int cy = L.legendRowCentreY(listed, blockTop);
    const int y = topYForInkCentre(renderer, L.fonts.legend, "0", LEGEND_STYLE, cy);
    char more[32];
    std::snprintf(more, sizeof(more), "+%d más", found_n - listed);
    renderer.drawText(L.fonts.legend, L.marginX, y, more, /*black=*/true, LEGEND_STYLE);
  }
  return rows;
}

// Legend for Style::WestsideEN. Columns: day (right-aligned), short month
// (left-aligned), label (left-aligned). No region-tag column.
//
// Sources merged and deduped:
//   (a) REGION_US holidays in the visible window, labeled in English.
//   (b) WESTSIDE_2026_2027 entries whose range intersects the window.
//
// If a Westside entry and a US holiday share the same date, the Westside row
// wins (it is more specific). Sorted chronologically; respects legendMaxRows.
int drawLegendWestside(GfxRenderer& renderer, const Layout& L, const YMD& first,
                       const YMD& last) {
  struct Entry {
    YMD date;
    const char* label;
    bool isWestside;  // used for dedup: Westside beats US holiday on same date
  };
  constexpr int MAX_EN_FOUND = static_cast<int>(HolidayId::HolidayIdCount) +
                               static_cast<int>(WESTSIDE_2026_2027_COUNT);
  Entry found[MAX_EN_FOUND];
  int found_n = 0;

  // (a) REGION_US holidays in the visible window.
  const uint16_t years[2] = {first.year, last.year};
  const int yearIterations = (first.year != last.year) ? 2 : 1;
  for (int yi = 0; yi < yearIterations; ++yi) {
    for (uint8_t i = 0;
         i < static_cast<uint8_t>(HolidayId::HolidayIdCount) && found_n < MAX_EN_FOUND; ++i) {
      const auto id = static_cast<HolidayId>(i);
      if (!(HOLIDAY_TABLE[i].regions & REGION_US)) continue;
      const char* lbl = US_HOLIDAY_LABEL_EN[i];
      if (!lbl) continue;  // belt-and-suspenders (should never happen for REGION_US entries)
      const YMD d = resolveHoliday(id, years[yi]);
      if (!dateLess(d, first) && !dateLess(last, d)) {
        found[found_n++] = {d, lbl, false};
      }
    }
  }

  // (b) WESTSIDE_2026_2027 entries whose [day..endDay] range intersects
  // the visible window [first..last]. Legend date = first visible day.
  for (size_t i = 0; i < WESTSIDE_2026_2027_COUNT && found_n < MAX_EN_FOUND; ++i) {
    const WestsideEvent& e = WESTSIDE_2026_2027[i];
    const YMD rangeFirst = {e.year, e.month, e.day};
    const YMD rangeLast = {e.year, e.month, e.endDay};
    if (dateLess(rangeLast, first) || dateLess(last, rangeFirst)) continue;
    const YMD legendDate = dateLess(rangeFirst, first) ? first : rangeFirst;
    found[found_n++] = {legendDate, e.label, true};
  }

  if (found_n == 0) return 0;

  // Chronological insertion sort (n is small — at most a few dozen entries).
  for (int i = 1; i < found_n; ++i) {
    const Entry key = found[i];
    int j = i - 1;
    while (j >= 0 && dateLess(key.date, found[j].date)) {
      found[j + 1] = found[j];
      --j;
    }
    found[j + 1] = key;
  }

  // Dedup: when a Westside entry and a US holiday share a date, keep only the
  // Westside row. After the sort, same-date entries are contiguous.
  {
    int out = 0;
    for (int i = 0; i < found_n;) {
      int j = i + 1;
      while (j < found_n && sameDate(found[j].date, found[i].date)) ++j;
      // Determine whether any entry in [i..j) is Westside.
      bool anyWest = false;
      for (int k = i; k < j; ++k) {
        if (found[k].isWestside) { anyWest = true; break; }
      }
      for (int k = i; k < j; ++k) {
        if (!anyWest || found[k].isWestside) found[out++] = found[k];
      }
      i = j;
    }
    found_n = out;
  }

  const int rows = std::min(found_n, L.legendMaxRows);
  const bool overflow = found_n > L.legendMaxRows;
  const int listed = overflow ? rows - 1 : rows;

  const int blockTop = std::max(L.legendTop, L.legendBottomY - rows * L.legendRowH);

  constexpr int GUTTER = 10;
  int dayW = 0, monthW = 0;
  for (int i = 0; i < listed; ++i) {
    char dayBuf[4];
    std::snprintf(dayBuf, sizeof(dayBuf), "%u", found[i].date.day);
    dayW = std::max(dayW, renderer.getTextWidth(L.fonts.legend, dayBuf, LEGEND_STYLE));
    monthW = std::max(monthW, renderer.getTextWidth(L.fonts.legend,
                                                    MONTHS_SHORT_EN[found[i].date.month - 1],
                                                    LEGEND_STYLE));
  }

  const int dayRight = L.marginX + dayW;
  const int monthLeft = dayRight + GUTTER;
  const int nameLeft = monthLeft + monthW + GUTTER;
  const int nameW = L.screenW - L.marginX - nameLeft;

  for (int i = 0; i < listed; ++i) {
    const int cy = L.legendRowCentreY(i, blockTop);
    const int y = topYForInkCentre(renderer, L.fonts.legend, "0", LEGEND_STYLE, cy);

    char dayBuf[4];
    std::snprintf(dayBuf, sizeof(dayBuf), "%u", found[i].date.day);
    const int w = renderer.getTextWidth(L.fonts.legend, dayBuf, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, dayRight - w, y, dayBuf, /*black=*/true, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, monthLeft, y, MONTHS_SHORT_EN[found[i].date.month - 1],
                      /*black=*/true, LEGEND_STYLE);
    const std::string name =
        renderer.truncatedText(L.fonts.legend, found[i].label, nameW, LEGEND_STYLE);
    renderer.drawText(L.fonts.legend, nameLeft, y, name.c_str(), /*black=*/true, LEGEND_STYLE);
  }

  if (overflow) {
    const int cy = L.legendRowCentreY(listed, blockTop);
    const int y = topYForInkCentre(renderer, L.fonts.legend, "0", LEGEND_STYLE, cy);
    char more[32];
    std::snprintf(more, sizeof(more), "+%d more", found_n - listed);
    renderer.drawText(L.fonts.legend, L.marginX, y, more, /*black=*/true, LEGEND_STYLE);
  }
  return rows;
}


}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CalendarSleepScreen::render(GfxRenderer& renderer, const YMD& today, uint8_t weeks,
                                Style style) {
  if (!isValidDate(today.year, today.month, today.day)) {
    LOG_ERR("CAL", "invalid today: %u-%u-%u", today.year, today.month, today.day);
    return;
  }
  if (weeks < WEEKS_MIN) weeks = WEEKS_MIN;
  if (weeks > WEEKS_MAX) weeks = WEEKS_MAX;

  renderer.clearScreen();

  const CalendarFonts fonts = resolveFonts(renderer);
  if (fonts.display == 0) {
    LOG_ERR("CAL", "no usable display font");
    return;
  }
  const Layout L = computeLayout(renderer, fonts, weeks, style);

  const YMD anchor = anchorSundayOf(today);
  YMD last = anchor;
  addDays(last, L.rows * COL_COUNT - 1);

  // Warm the glyph cache for everything the display face is about to draw.
  //
  // FontCacheManager::prewarmCache has a dedicated SD path (SdCardFont::prewarm)
  // that pulls the glyph bitmaps in one pass. Without it, an SD face falls back
  // to its 8-slot on-demand overflow ring and thrashes — one SD read per glyph
  // mid-render ("SDCF Overflow: loaded U+xxxx on demand"), which on real
  // hardware is far more expensive than the whole layout.
  //
  // ensureSdCardFontReady() is NOT sufficient here: it builds the advance table
  // used for measurement, not the bitmaps used for drawing.
  if (auto* fcm = renderer.getFontCacheManager()) {
    char warm[128];
    const char* const* longNames = (style == Style::WestsideEN) ? MONTHS_LONG_EN : MONTHS_LONG;
    std::snprintf(warm, sizeof(warm), "0123456789%s%s", longNames[anchor.month - 1],
                  longNames[last.month - 1]);
    // Bit 0 (regular) for the day numbers, bit 2 (italic) for the header.
    fcm->prewarmCache(L.fonts.display, warm, 0x05);
  }

  drawHeader(renderer, L, anchor, last);

  YMD cursor = anchor;
  for (int row = 0; row < L.rows; ++row) {
    for (int col = 0; col < COL_COUNT; ++col) {
      bool isHoliday = false;
      if (style == Style::WestsideEN) {
        // WestsideEN shading: REGION_US federal holiday OR a Westside entry
        // with shaded=true that covers this date. CR-only holidays do not shade.
        HolidayId hid;
        if (findHolidayFor(cursor, hid) && (HOLIDAY_TABLE[static_cast<size_t>(hid)].regions & REGION_US)) {
          isHoliday = true;
        }
        if (!isHoliday) {
          for (size_t i = 0; i < WESTSIDE_2026_2027_COUNT; ++i) {
            const WestsideEvent& e = WESTSIDE_2026_2027[i];
            if (e.shaded && e.year == cursor.year && e.month == cursor.month &&
                cursor.day >= e.day && cursor.day <= e.endDay) {
              isHoliday = true;
              break;
            }
          }
        }
      } else {
        // SpanishCR: shade any CR or US holiday (original behavior).
        HolidayId hid;
        isHoliday = findHolidayFor(cursor, hid);
      }
      const bool isToday = sameDate(cursor, today);
      const bool showSub = cursor.day == 1 && cursor.month != anchor.month;
      drawDayCell(renderer, L, row, col, cursor, isToday, isHoliday, showSub);
      addDays(cursor, 1);
    }
  }

  if (style == Style::WestsideEN) {
    drawLegendWestside(renderer, L, anchor, last);
  } else {
    drawLegend(renderer, L, anchor, last);
  }
}

}  // namespace calendar
