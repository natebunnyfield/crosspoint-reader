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

static constexpr int FONT = LIBREFRANKLIN_READER_14_FONT_ID;
static constexpr int MARGIN = 28;
static constexpr int CAPTION_GAP = 10;
static constexpr int PAD = 8;  // inside the outer box, so ink never touches a rule

static int pageWidth() { return renderer.getScreenWidth(); }
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

int main() {
  renderer.begin();
  if (!fontDecompressor.init()) {
    fprintf(stderr, "decompressor init failed\n");
    return 1;
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14Family);
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

  printf("wrote 4 BMPs to fs_/\n");
  return 0;
}
