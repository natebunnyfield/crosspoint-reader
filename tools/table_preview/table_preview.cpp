// Table layout mockups, drawn through the REAL firmware renderer.
//
// Decision aid for T-012 (tables in EPUB content). Today ChapterHtmlSlimParser
// flattens every <table> into ordinary paragraphs in reading order -- no column
// alignment, no rules, no colspan. This renders the same table four ways on the
// real 528x792 logical page, in the real reading face, so the choice is made
// against pixels rather than prose.
//
// It draws directly; it does NOT go through the parser. These are mockups of a
// target, not screenshots of a working feature -- the layout maths here is the
// proposal, and porting it into the parser is the work that follows a ruling.
//
// Build + run from this directory:  ./build.sh && ./table_preview
// Output: fs_/table_{flat,columns,headrule,grid}.bmp
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontSystem.h>
#include <builtinFonts/all.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "HalGPIO.h"
#include "fontIds.h"

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
// Mirrors src/main.cpp and the calendar harness.
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts()
#if CROSSPOINT_RENDER_SCALE > 1
                                                             ,
                                  renderer.getHiResSdCardFonts()
#endif
);
extern SdCardFontSystem sdFontSystem;  // sd_font_stub.cpp

// The reading face the device uses before an SD font is chosen, in all four
// styles -- registering only regular+bold would make the header row fall back
// silently inside EpdFontFamily::getFont() and draw something the device would
// not.
EpdFont lfr14R(&librefranklin_reader_14_regular), lfr14B(&librefranklin_reader_14_bold),
    lfr14I(&librefranklin_reader_14_italic), lfr14BI(&librefranklin_reader_14_bolditalic);
EpdFontFamily lfReader14Family(&lfr14R, &lfr14B, &lfr14I, &lfr14BI);
// The SMALLEST size the reader offers -- the floor of the 12/14/16/18 ramp, so
// "as small as a book is ever set" rather than an arbitrary shrink.
EpdFont lfr12R(&librefranklin_reader_12_regular), lfr12B(&librefranklin_reader_12_bold),
    lfr12I(&librefranklin_reader_12_italic), lfr12BI(&librefranklin_reader_12_bolditalic);
EpdFontFamily lfReader12Family(&lfr12R, &lfr12B, &lfr12I, &lfr12BI);
EpdFont smallFont(&librefranklin_8_regular);
EpdFontFamily smallFamily(&smallFont);
EpdFont ui12Regular(&librefranklin_12_regular), ui12Bold(&librefranklin_12_bold);
EpdFontFamily ui12Family(&ui12Regular, &ui12Bold);

// --- BMP output, copied verbatim from tools/calendar_preview/render_harness.cpp.
// Copied rather than shared: that file is one self-contained harness with its
// own two modes, and adding a third mode to it for a one-off decision aid
// would couple two tools that answer different questions.
// Read one PHYSICAL portrait pixel out of the physical landscape framebuffer.
// GfxRenderer::drawPixel rotates Portrait coords with (rotateCoordinates,
// GfxRenderer.cpp:187): phyX = y, phyY = panelHeight - 1 - x, writing 1bpp
// MSB-first where a SET bit is white. This inverts that mapping.
//
// At RENDER_SCALE 1 physical and logical coincide, which is why this was named
// for logical coords; at scale > 1 the caller walks the physical grid so the
// hi-res glyph pixels survive into the BMP.
bool logicalPixelIsWhite(const uint8_t* fb, int panelHeight, int panelWidthBytes, int lx, int ly) {
  const int px = ly;
  const int py = (panelHeight - 1) - lx;
  return (fb[py * panelWidthBytes + (px >> 3)] >> (7 - (px & 7))) & 0x1;
}

// Same writer, but emitting the page turned 180 degrees.
//
// The renderer offers exactly ONE rotated draw, drawTextRotated90CW, and what it
// puts on the page is COUNTER-clockwise content -- text climbing bottom to top,
// meant to be read by turning the device clockwise. Its name describes the
// device turn, not the glyph transform, which is the trap that produced a
// counter-clockwise mockup when a clockwise one was asked for. There is no call
// for a clockwise page, so this composes with the call that exists and turns the
// finished framebuffer 180 degrees: CCW + 180 = CW. The page flips as a unit, so
// row order stays right -- the first row moves from the left edge to the right,
// which is where the top lands once the device is turned counter-clockwise.
bool writeMonoPortraitBmp180(const char* path, const GfxRenderer& r);

bool writeMonoPortraitBmp(const char* path, const GfxRenderer& r) {
  // PHYSICAL pixels, not logical. At RENDER_SCALE > 1 the framebuffer carries
  // scale x the detail — that is the entire reason the 2x companions exist —
  // and sampling one physical pixel per LOGICAL pixel throws all of it away,
  // producing a 1x-looking page out of a 2x render. The logical page size is
  // unchanged by the scale (see HalDisplay.h), so multiplying by RENDER_SCALE
  // here writes the same page at the resolution it was actually drawn.
  const int S = GfxRenderer::RENDER_SCALE;
  const int W = r.getScreenWidth() * S;
  const int H = r.getScreenHeight() * S;
  const int panelH = r.getDisplayHeight();
  const int panelWB = r.getDisplayWidthBytes();
  const int rowBytes = ((W + 31) / 32) * 4;
  const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes) * H;
  constexpr int HEADER = 14 + 40 + 8;
  const uint32_t fileSize = HEADER + pixelBytes;

  uint8_t hdr[HEADER] = {0};
  hdr[0] = 'B';
  hdr[1] = 'M';
  hdr[2] = fileSize & 0xFF;
  hdr[3] = (fileSize >> 8) & 0xFF;
  hdr[4] = (fileSize >> 16) & 0xFF;
  hdr[5] = (fileSize >> 24) & 0xFF;
  hdr[10] = HEADER & 0xFF;
  hdr[11] = (HEADER >> 8) & 0xFF;
  hdr[14] = 40;
  hdr[18] = W & 0xFF;
  hdr[19] = (W >> 8) & 0xFF;
  hdr[22] = H & 0xFF;
  hdr[23] = (H >> 8) & 0xFF;  // positive => bottom-up
  hdr[26] = 1;
  hdr[28] = 1;                         // 1 plane, 1 bpp
  hdr[46] = 2;                         // biClrUsed
  hdr[58] = hdr[59] = hdr[60] = 0xFF;  // palette[1] = white

  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    return false;
  }
  fwrite(hdr, 1, HEADER, f);

  const uint8_t* fb = r.getFrameBuffer();
  auto* row = static_cast<uint8_t*>(calloc(1, rowBytes));
  for (int y = H - 1; y >= 0; --y) {  // BMP rows are bottom-up
    memset(row, 0, rowBytes);
    for (int x = 0; x < W; ++x) {
      if (logicalPixelIsWhite(fb, panelH, panelWB, x, y)) row[x >> 3] |= (0x80 >> (x & 7));
    }
    fwrite(row, 1, rowBytes, f);
  }
  free(row);
  fclose(f);
  return true;
}

bool writeMonoPortraitBmp180(const char* path, const GfxRenderer& r) {
  const int S = GfxRenderer::RENDER_SCALE;
  const int W = r.getScreenWidth() * S;
  const int H = r.getScreenHeight() * S;
  const int panelH = r.getDisplayHeight();
  const int panelWB = r.getDisplayWidthBytes();
  const int rowBytes = ((W + 31) / 32) * 4;
  const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes) * H;
  constexpr int HEADER = 14 + 40 + 8;
  const uint32_t fileSize = HEADER + pixelBytes;

  uint8_t hdr[HEADER] = {0};
  hdr[0] = 'B';
  hdr[1] = 'M';
  hdr[2] = fileSize & 0xFF;
  hdr[3] = (fileSize >> 8) & 0xFF;
  hdr[4] = (fileSize >> 16) & 0xFF;
  hdr[5] = (fileSize >> 24) & 0xFF;
  hdr[10] = HEADER & 0xFF;
  hdr[11] = (HEADER >> 8) & 0xFF;
  hdr[14] = 40;
  hdr[18] = W & 0xFF;
  hdr[19] = (W >> 8) & 0xFF;
  hdr[22] = H & 0xFF;
  hdr[23] = (H >> 8) & 0xFF;
  hdr[26] = 1;
  hdr[28] = 1;
  hdr[46] = 2;
  hdr[58] = hdr[59] = hdr[60] = 0xFF;

  FILE* f = fopen(path, "wb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    return false;
  }
  fwrite(hdr, 1, HEADER, f);

  const uint8_t* fb = r.getFrameBuffer();
  auto* row = static_cast<uint8_t*>(calloc(1, rowBytes));
  // 180 degrees: mirror both axes. BMP rows are written bottom-up already, so
  // reversing the y loop direction is the vertical half of the flip.
  for (int y = 0; y < H; ++y) {
    memset(row, 0, rowBytes);
    for (int x = 0; x < W; ++x) {
      if (logicalPixelIsWhite(fb, panelH, panelWB, (W - 1) - x, y)) row[x >> 3] |= (0x80 >> (x & 7));
    }
    fwrite(row, 1, rowBytes, f);
  }
  free(row);
  fclose(f);
  return true;
}

// ---------------------------------------------------------------------------
// The specimen. A real shape, not a toy: a three-column table with a header
// row, one long cell that must wrap, and a numeric column that is only useful
// when its digits line up. Anything narrower proves nothing about column rules.
struct Row {
  const char* c[3];
};
static const char* kHead[3] = {"Voyage", "Departed", "Days"};
static const Row kRows[] = {
    {{"Beagle, first survey", "1826", "1,461"}, },
    {{"Beagle, second survey with FitzRoy", "1831", "1,741"}, },
    {{"Erebus and Terror", "1839", "1,428"}, },
    {{"Challenger", "1872", "1,251"}, },
};
static constexpr int kRowCount = sizeof(kRows) / sizeof(kRows[0]);

// The WIDE specimen, for the fallback question: five columns of real text that
// cannot fit a 528px page at reading size however the width is divided. Same
// shape as a results table in a nonfiction book.
static const char* kWideHead[5] = {"Expedition", "Commander", "Departed", "Returned", "Crew"};
struct WideRow {
  const char* c[5];
};
static const WideRow kWideRows[] = {
    {{"Beagle, second survey", "Robert FitzRoy", "December 1831", "October 1836", "74"}},
    {{"Erebus and Terror", "James Clark Ross", "September 1839", "September 1843", "129"}},
    {{"Challenger", "George Nares", "December 1872", "May 1876", "243"}},
};
static constexpr int kWideRowCount = sizeof(kWideRows) / sizeof(kWideRows[0]);

static constexpr int FONT = LIBREFRANKLIN_READER_14_FONT_ID;
// The floor of the reader's 12/14/16/18 ramp. Used two ways: as the whole page's
// size in C/D/E, and as the LABEL size in H.
static constexpr int SMALL = LIBREFRANKLIN_READER_12_FONT_ID;
static constexpr int MARGIN = 28;
static constexpr int CAPTION_GAP = 10;
static constexpr int PAD = 8;  // inside the outer box, so ink never touches a rule

static int pageWidth() { return renderer.getScreenWidth(); }
static int lineH14() { return renderer.getLineHeight(FONT); }
static int lineH() { return renderer.getLineHeight(FONT); }

// Draw `text` wrapped inside [x, x+w), return the y after the last line.
static int drawWrapped(int x, int y, int w, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  for (const auto& line : renderer.wrappedText(FONT, text, w, 8, style)) {
    renderer.drawText(FONT, x, y, line.c_str(), true, style);
    y += lineH();
  }
  return y;
}

static int header(int y, const char* title, const char* note) {
  renderer.drawText(UI_12_FONT_ID, MARGIN, y, title, true, EpdFontFamily::BOLD);
  y += renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(SMALL_FONT_ID, MARGIN, y, note);
  y += renderer.getLineHeight(SMALL_FONT_ID) + CAPTION_GAP * 2;
  return y;
}

// --- 1. Flattened: what the firmware does today ---------------------------
static void renderFlat() {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, "1  Flattened (today)", "cells in reading order, header name repeated per cell");
  const int w = pageWidth() - MARGIN * 2;
  for (int r = 0; r < kRowCount; r++) {
    for (int c = 0; c < 3; c++) {
      char buf[160];
      snprintf(buf, sizeof(buf), "%s: %s", kHead[c], kRows[r].c[c]);
      y = drawWrapped(MARGIN, y, w, buf);
    }
    y += CAPTION_GAP;
  }
}

// --- shared column geometry ------------------------------------------------
// Column widths are proportional to the widest cell each column holds, then
// scaled to the text column. The first column takes what it needs and wraps;
// the numeric column is sized to its digits so they stay flush right.
struct Cols {
  int x[3];
  int w[3];
};
static Cols measure() {
  int natural[3] = {0, 0, 0};
  for (int c = 0; c < 3; c++) {
    natural[c] = renderer.getTextWidth(FONT, kHead[c], EpdFontFamily::BOLD);
    for (int r = 0; r < kRowCount; r++) {
      const int cw = renderer.getTextWidth(FONT, kRows[r].c[c]);
      if (cw > natural[c]) natural[c] = cw;
    }
  }
  const int gutter = renderer.getSpaceWidth(FONT) * 3;
  // PAD is breathing room INSIDE the outer box, so ink never touches a rule.
  // It comes out of the available width, not out of the margin -- a table that
  // reaches further into the margin than the body text does reads as a mistake.
  const int avail = pageWidth() - MARGIN * 2 - gutter * 2 - PAD * 2;
  const int total = natural[0] + natural[1] + natural[2];
  Cols cols{};
  // Only the first column is allowed to shrink and wrap; the short columns keep
  // their natural width, because a wrapped year or a wrapped figure is worse
  // than a wrapped phrase.
  cols.w[1] = natural[1];
  cols.w[2] = natural[2];
  cols.w[0] = (total <= avail) ? natural[0] : avail - natural[1] - natural[2];
  cols.x[0] = MARGIN + PAD;
  cols.x[1] = cols.x[0] + cols.w[0] + gutter;
  cols.x[2] = cols.x[1] + cols.w[1] + gutter;
  return cols;
}

// Right-align the numeric column; left-align the rest.
static int drawRow(const Cols& cols, int y, const char* const cells[3], EpdFontFamily::Style style) {
  int bottom = y;
  for (int c = 0; c < 3; c++) {
    if (c == 2) {
      const int tw = renderer.getTextWidth(FONT, cells[c], style);
      renderer.drawText(FONT, cols.x[c] + cols.w[c] - tw, y, cells[c], true, style);
      bottom = (y + lineH() > bottom) ? y + lineH() : bottom;
    } else {
      const int after = drawWrapped(cols.x[c], y, cols.w[c], cells[c], style);
      bottom = (after > bottom) ? after : bottom;
    }
  }
  return bottom;
}

static void renderColumns(const char* title, const char* note, bool headRule, bool grid) {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, title, note);
  const Cols cols = measure();
  const int left = cols.x[0] - PAD;
  const int right = cols.x[2] + cols.w[2] + PAD;
  const int pad = 6;  // vertical padding, above and below each row's text

  const int tableTop = y;
  int rowTops[kRowCount + 1];
  rowTops[0] = y;
  y = drawRow(cols, y + pad, kHead, EpdFontFamily::BOLD) + pad;
  if (headRule || grid) {
    renderer.drawLine(left, y, right, y, grid ? 1 : 2, true);
  }
  for (int r = 0; r < kRowCount; r++) {
    rowTops[r + 1] = y;
    y = drawRow(cols, y + pad, kRows[r].c, EpdFontFamily::REGULAR) + pad;
    if (grid && r < kRowCount - 1) {
      renderer.drawLine(left, y, right, y, 1, true);
    }
  }
  if (grid) {
    renderer.drawRect(left, tableTop, right - left, y - tableTop, 1, true);
    for (int c = 1; c < 3; c++) {
      const int vx = cols.x[c] - renderer.getSpaceWidth(FONT) * 3 / 2;
      renderer.drawLine(vx, tableTop, vx, y, 1, true);
    }
  }
  (void)rowTops;
}

// --- 5. The wide table, flattened: what ships today ------------------------
static void renderWideFlat() {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, "A  Flattened (what ships today)", "five columns, too wide to fit: every cell becomes a line");
  const int w = pageWidth() - MARGIN * 2;
  for (int r = 0; r < kWideRowCount; r++) {
    for (int c = 0; c < 5; c++) {
      char buf[160];
      snprintf(buf, sizeof(buf), "%s: %s", kWideHead[c], kWideRows[r].c[c]);
      y = drawWrapped(MARGIN, y, w, buf);
    }
    y += CAPTION_GAP;
  }
}

// --- 6. The wide table, squeezed to the floor ------------------------------
static void renderWideSqueezed() {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, "B  Columns, squeezed to the floor", "same table forced into columns; cells wrap hard");
  const int gutter = renderer.getSpaceWidth(FONT) * 2;
  const int avail = pageWidth() - MARGIN * 2 - PAD * 2 - gutter * 4;
  const int colW = avail / 5;  // equal shares: nothing else fits at this width
  int x[5];
  for (int c = 0; c < 5; c++) x[c] = MARGIN + PAD + c * (colW + gutter);

  const int pad = 6;
  const int tableTop = y;
  int rowTop = y + pad;
  for (int c = 0; c < 5; c++) {
    const int after = drawWrapped(x[c], rowTop, colW, kWideHead[c], EpdFontFamily::BOLD);
    y = (after > y) ? after : y;
  }
  y += pad;
  renderer.drawLine(MARGIN + PAD, y, x[4] + colW, y, 2, true);
  for (int r = 0; r < kWideRowCount; r++) {
    rowTop = y + pad;
    int bottom = rowTop;
    for (int c = 0; c < 5; c++) {
      const int after = drawWrapped(x[c], rowTop, colW, kWideRows[r].c[c]);
      bottom = (after > bottom) ? after : bottom;
    }
    y = bottom + pad;
  }
  (void)tableTop;
}

// Probe: where does drawTextRotated90CW actually put its ink? BaseTheme passes
// y as the BOTTOM of the run, so the text should climb from there.
static void renderRotProbe() {
  renderer.clearScreen(0xFF);
  renderer.drawText(FONT, 20, 20, "unrotated at 20,20");
  renderer.drawTextRotated90CW(FONT, 100, 400, "ROTATED at 100,400");
  renderer.drawLine(100, 400, 140, 400, 1, true);
}

// --- F / G: flattened, but the column names are said ONCE -----------------
//
// The flattened form repeats "Expedition:" in front of every cell of every row,
// which is most of its bulk and all of its drone. F names the columns on the
// first row only; G lifts those names onto their own bold line, so the first
// row reads as a key and the rows under it as data.
static void renderFlatNamedOnce(const bool namesOnOwnLine) {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, namesOnOwnLine ? "G  Named once, on their own line" : "F  Named once",
             namesOnOwnLine ? "the column names lead in bold; the rows below are values only"
                            : "the first row carries the names; the rows below are values only");
  const int w = pageWidth() - MARGIN * 2;
  const int lineH = lineH14();

  for (int r = 0; r < kWideRowCount; r++) {
    for (int c = 0; c < 5; c++) {
      if (r == 0 && namesOnOwnLine) {
        renderer.drawText(FONT, MARGIN, y, kWideHead[c], true, EpdFontFamily::BOLD);
        y += lineH;
        y = drawWrapped(MARGIN, y, w, kWideRows[r].c[c]);
      } else if (r == 0) {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s: %s", kWideHead[c], kWideRows[r].c[c]);
        y = drawWrapped(MARGIN, y, w, buf);
      } else {
        y = drawWrapped(MARGIN, y, w, kWideRows[r].c[c]);
      }
    }
    y += CAPTION_GAP;
  }
}

// --- H: F, with the names in a LEFT-ALIGNED column ------------------------
//
// The values stay hard left, in the same column as every other row. The names
// form their own left-aligned column, parked at the leftmost x that clears every
// value in the group -- so the labels line up with each other instead of being
// ragged, and none of them collides with the value it labels. A right-flush set
// looked like five separate decisions; one shared left edge reads as a column.
static void renderFlatBoldName() {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, "H  Named once, bold, one size down", "labels smaller and top-aligned with the values they name");
  const int left = MARGIN;
  const int w = pageWidth() - MARGIN * 2;
  const int lineH = lineH14();
  const int gap = renderer.getSpaceWidth(FONT) * 2;

  // The label column sits just past the WIDEST value, so no value can reach it.
  // The names are set a size DOWN from the values -- they are labels on the
  // data, not part of it, and at reading size they competed with the words they
  // were labelling. TOP-aligned with the value, not baseline-aligned: the label
  // marks where the entry starts, which matters more than the two sizes sharing
  // a baseline when a value wraps to a second line.
  int widestValue = 0;
  int widestLabel = 0;
  for (int c = 0; c < 5; c++) {
    widestValue = std::max(widestValue, renderer.getTextWidth(FONT, kWideRows[0].c[c]));
    widestLabel = std::max(widestLabel, renderer.getTextWidth(SMALL, kWideHead[c], EpdFontFamily::BOLD));
  }
  int labelX = left + widestValue + gap;
  // If that runs the labels off the page, pull the column back to the last x
  // that fits and let the longest value wrap under instead of colliding.
  if (labelX + widestLabel > left + w) {
    labelX = left + w - widestLabel;
  }

  for (int r = 0; r < kWideRowCount; r++) {
    for (int c = 0; c < 5; c++) {
      if (r != 0) {
        y = drawWrapped(left, y, w, kWideRows[r].c[c]);
        continue;
      }
      const std::string whole = kWideRows[r].c[c];
      // The value WRAPS inside the room left of the label column -- it never
      // truncates. Asking for one line would ellipsize the longest value, which
      // loses text to a layout decision, and losing text is the one thing none
      // of these options is allowed to do.
      const int room = labelX - left - gap;
      const auto lines = renderer.wrappedText(FONT, whole.c_str(), room, 6);
      renderer.drawText(SMALL, labelX, y, kWideHead[c], true, EpdFontFamily::BOLD);
      for (const auto& line : lines) {
        renderer.drawText(FONT, left, y, line.c_str());
        y += lineH;
      }
      if (lines.empty()) y += lineH;
    }
    y += CAPTION_GAP;
  }
}

// --- I: G, with the names collected ABOVE the first row --------------------
//
// G names each cell on its own bold line, interleaved with the first row's
// values, so the names are scattered through it. I lifts them out: all five
// names first, as a block, then the rows underneath. The block reads as a key,
// and the first row stops being a special case -- every row below is identical
// in shape, which is the thing G could not manage.
static void renderFlatKeyBlock() {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, "I  Names collected above", "all five names as a block, then every row identical beneath");
  const int left = MARGIN;
  const int w = pageWidth() - MARGIN * 2;
  const int lineH = lineH14();

  for (int c = 0; c < 5; c++) {
    renderer.drawText(FONT, left, y, kWideHead[c], true, EpdFontFamily::BOLD);
    y += lineH;
  }
  y += CAPTION_GAP;

  for (int r = 0; r < kWideRowCount; r++) {
    for (int c = 0; c < 5; c++) {
      y = drawWrapped(left, y, w, kWideRows[r].c[c]);
    }
    y += CAPTION_GAP;
  }
}

// --- C / D / E: the wide table at the smallest reading size ----------------
//
// drawTextRotated90CW anchors a run at its BOTTOM and climbs: text drawn at
// (x, y) occupies one line-height of width at x and runs upward from y. Turn
// the page 90 degrees CLOCKWISE and it reads left to right (verified with a
// probe render before any of this was written). So landscape maps as:
//
//     portrait x = landscape y                 -- landscape rows march across
//     portrait y = screenHeight - landscape x  -- landscape columns march up
//
static int smallLineH() { return renderer.getLineHeight(SMALL); }

// across = distance from the landscape LEFT edge (reading direction)
// down   = distance from the landscape TOP edge (row direction)
//
// Reading runs toward decreasing portrait y, and rows march toward increasing
// portrait x, so: portrait x = down, portrait y = screenHeight - across.
static void drawLandscape(int across, int down, const char* text,
                          EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawTextRotated90CW(SMALL, down, renderer.getScreenHeight() - across, text, true, style);
}

// Wrapped landscape text. Extra lines stack DOWN the page, which is portrait x.
// Returns the down-coordinate after the last line.
static int drawLandscapeWrapped(int across, int down, int w, const char* text,
                                EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  for (const auto& line : renderer.wrappedText(SMALL, text, w, 8, style)) {
    drawLandscape(across, down, line.c_str(), style);
    down += smallLineH();
  }
  return down;
}

struct WideCols {
  int x[5];
  int w[5];
  int right;
};

// Natural widths capped to the axis, widest column giving ground first -- the
// same rule the shipped planner uses, so these renders are not flattering.
static WideCols planWide(int axisWidth, int fontId) {
  int natural[5];
  for (int c = 0; c < 5; c++) {
    natural[c] = renderer.getTextWidth(fontId, kWideHead[c], EpdFontFamily::BOLD);
    for (int r = 0; r < kWideRowCount; r++) {
      const int w = renderer.getTextWidth(fontId, kWideRows[r].c[c]);
      if (w > natural[c]) natural[c] = w;
    }
    natural[c] += 2;
  }
  const int gutter = renderer.getSpaceWidth(fontId) * 2;
  const int avail = axisWidth - gutter * 4;
  int total = 0;
  for (int c = 0; c < 5; c++) total += natural[c];
  while (total > avail) {
    int widest = 0;
    for (int c = 1; c < 5; c++) {
      if (natural[c] > natural[widest]) widest = c;
    }
    if (natural[widest] <= 40) break;
    natural[widest] -= 4;
    total -= 4;
  }
  WideCols cols{};
  int x = 0;
  for (int c = 0; c < 5; c++) {
    cols.x[c] = x;
    cols.w[c] = natural[c];
    x += natural[c] + gutter;
  }
  cols.right = x - gutter;
  return cols;
}

// C: portrait, smallest reading size, nothing else changed.
static void renderWideSmall() {
  renderer.clearScreen(0xFF);
  int y = MARGIN;
  y = header(y, "C  Smallest reading size", "the same columns set at 12, the smallest a book is read at");
  const WideCols cols = planWide(pageWidth() - MARGIN * 2 - PAD * 2, SMALL);
  const int lineH = smallLineH();
  const int pad = 5;
  const int left = MARGIN + PAD;

  int rowTop = y + pad;
  int bottom = rowTop;
  for (int c = 0; c < 5; c++) {
    int ly = rowTop;
    for (const auto& line : renderer.wrappedText(SMALL, kWideHead[c], cols.w[c], 6, EpdFontFamily::BOLD)) {
      renderer.drawText(SMALL, left + cols.x[c], ly, line.c_str(), true, EpdFontFamily::BOLD);
      ly += lineH;
    }
    bottom = std::max(bottom, ly);
  }
  y = bottom + pad;
  renderer.drawLine(left, y, left + cols.right, y, 2, true);

  for (int r = 0; r < kWideRowCount; r++) {
    rowTop = y + pad;
    bottom = rowTop;
    for (int c = 0; c < 5; c++) {
      int ly = rowTop;
      for (const auto& line : renderer.wrappedText(SMALL, kWideRows[r].c[c], cols.w[c], 6)) {
        renderer.drawText(SMALL, left + cols.x[c], ly, line.c_str());
        ly += lineH;
      }
      bottom = std::max(bottom, ly);
    }
    y = bottom + pad;
  }
}

// D: smallest size AND rotated, so the table gets the 792px axis.
// E: the same with no chrome, the table given the whole page.
static void renderWideRotated(const bool fullPage) {
  renderer.clearScreen(0xFF);
  const int axis = renderer.getScreenHeight();  // 792 -- the long edge
  // Full page means FULL page: 2px, not the reading margin. A table that has
  // already cost the reader a device turn should not also give back a quarter
  // of the axis it turned for. Owner ruling 2026-08-19.
  const int margin = fullPage ? 2 : MARGIN;
  const int lineH = smallLineH();
  const int pad = 5;
  int down = margin;  // how far down the landscape page the next row sits

  if (!fullPage) {
    drawLandscape(margin, down, "D  Smallest size, rotated", EpdFontFamily::BOLD);
    down += lineH;
    drawLandscape(margin, down, "turn the page clockwise, and the table gets the long axis");
    down += lineH * 2;
  }

  const WideCols cols = planWide(axis - margin * 2, SMALL);
  const int left = margin;  // landscape x origin

  int bottom = down;
  for (int c = 0; c < 5; c++) {
    bottom = std::max(bottom, drawLandscapeWrapped(left + cols.x[c], down, cols.w[c], kWideHead[c], EpdFontFamily::BOLD));
  }
  down = bottom + pad;
  // The header rule runs along the landscape reading direction, which on the
  // portrait page is a VERTICAL line at x = down.
  renderer.drawLine(down, renderer.getScreenHeight() - (left + cols.right), down,
                    renderer.getScreenHeight() - left, 2, true);
  down += pad + 2;

  for (int r = 0; r < kWideRowCount; r++) {
    bottom = down;
    for (int c = 0; c < 5; c++) {
      bottom = std::max(bottom, drawLandscapeWrapped(left + cols.x[c], down, cols.w[c], kWideRows[r].c[c]));
    }
    down = bottom + pad;
  }
}

int main() {
  renderer.begin();
  if (!fontDecompressor.init()) {
    fprintf(stderr, "decompressor init failed\n");
    return 1;
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14Family);
  renderer.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, lfReader12Family);
  renderer.insertFont(SMALL_FONT_ID, smallFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12Family);

  mkdir("fs_", 0755);
  renderFlat();
  writeMonoPortraitBmp("fs_/table_flat.bmp", renderer);

  renderColumns("2  Columns, no rules", "alignment only; nothing drawn but the words", false, false);
  writeMonoPortraitBmp("fs_/table_columns.bmp", renderer);

  renderColumns("3  Columns + header rule", "one rule under the header row", true, false);
  writeMonoPortraitBmp("fs_/table_headrule.bmp", renderer);

  renderColumns("4  Full grid", "box, row rules and column rules", false, true);
  writeMonoPortraitBmp("fs_/table_grid.bmp", renderer);

  renderWideFlat();
  writeMonoPortraitBmp("fs_/wide_flat.bmp", renderer);

  renderWideSqueezed();
  writeMonoPortraitBmp("fs_/wide_squeezed.bmp", renderer);

  renderWideSmall();
  writeMonoPortraitBmp("fs_/wide_small.bmp", renderer);

  // CLOCKWISE ONLY. The owner is right-handed and ruled on 2026-08-19 that a
  // counter-clockwise turn is not to be offered again, so the CCW renders are
  // gone rather than kept as an alternative nobody will pick.
  renderWideRotated(false);
  writeMonoPortraitBmp180("fs_/wide_rotated_cw.bmp", renderer);

  renderWideRotated(true);
  writeMonoPortraitBmp180("fs_/wide_rotated_full_cw.bmp", renderer);

  renderFlatNamedOnce(false);
  writeMonoPortraitBmp("fs_/wide_named_once.bmp", renderer);

  renderFlatNamedOnce(true);
  writeMonoPortraitBmp("fs_/wide_named_bold.bmp", renderer);

  renderFlatBoldName();
  writeMonoPortraitBmp("fs_/wide_named_inline_bold.bmp", renderer);

  renderFlatKeyBlock();
  writeMonoPortraitBmp("fs_/wide_key_block.bmp", renderer);

  printf("wrote 7 BMPs to fs_/\n");
  return 0;
}
