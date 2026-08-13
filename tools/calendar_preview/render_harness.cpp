// CrossPoint off-device RENDER HARNESS.
//
// NOT the simulator. The simulator is ~/src/crosspoint-simulator: it boots the
// whole firmware as an SDL app on Mac/Linux and as an iOS app, and the firmware
// repo consumes it via `lib_deps: simulator=symlink://...` + `[env:simulator]`.
// Run that with `pio run -e simulator -t run_simulator`.
//
// This is the test rig that sits underneath it. It links a handful of firmware
// translation units, calls one draw function directly — no boot, no navigation,
// no window — and dumps the device's raw 528x792 1-bit framebuffer. That makes
// it ~9ms per render (sweep.py does 203 dates in 1.87s) and pixel-exact, which
// is what check_centering.py's 2px assertion needs. The simulator's screenshots
// are SDL output at the host's Retina drawable size, so they cannot serve that
// measurement. Keep both; they answer different questions.
//
// One binary, two modes, both driving the REAL firmware GfxRenderer / EpdFont /
// SdCardFont against a host-side framebuffer:
//
//   render_harness calendar [YYYY MM DD]      -> fs_/sleep.bmp
//   render_harness fonts    FAMILY_A FAMILY_B -> fs_/kern_specimen_{12,14,16,18}.bmp
//   render_harness YYYY MM DD                 -> legacy form (sweep.py, check_centering.py)
//
// Merged from the previous render_harness.cpp + kern_specimen.cpp, which were
// two mains duplicating the same framebuffer setup and BMP writer.
//
// The BMP writer lives HERE and not in the firmware on purpose: the device
// draws straight to the panel, so serialisation is purely a development
// convenience. Keeping it out of src/ means the firmware carries no file
// format, no SD write and no cache to go stale.
//
// PORTABILITY: renderCalendar() and renderFontSpecimen() take every output
// path as an argument and touch no globals beyond the renderer singletons, so a
// GUI host (an iOS app target, say) can link this translation unit with
// SIM_NO_MAIN defined and call them directly. Keep it that way — no getenv, no
// cwd-relative literals, and no argv parsing below the main() boundary.
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <SdCardFontSystem.h>
#include <builtinFonts/all.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "HalGPIO.h"
#include "activities/boot_sleep/CalendarSleepScreen.h"
#include "activities/boot_sleep/HolidayCalculator.h"
#include "fontIds.h"

HalDisplay display;
HalGPIO gpio;
GfxRenderer renderer(display);
FontDecompressor fontDecompressor;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());

extern SdCardFontSystem sdFontSystem;

// sd_font_stub.cpp. Loads a family's Nth installed size rather than the size
// nearest a nominal 12/14/16/18 — see the comment on renderReadingSpecimen().
int loadSdFontByOrdinal(const char* familyName, uint8_t ordinal, GfxRenderer& renderer);

// Global font objects, mirroring src/main.cpp — including ALL FOUR styles per
// family. Registering only regular+bold here would make italic silently fall
// back inside EpdFontFamily::getFont(), so the sim would render something the
// device does not.
EpdFont lfr12R(&librefranklin_reader_12_regular), lfr12B(&librefranklin_reader_12_bold),
    lfr12I(&librefranklin_reader_12_italic), lfr12BI(&librefranklin_reader_12_bolditalic);
EpdFontFamily lfReader12Family(&lfr12R, &lfr12B, &lfr12I, &lfr12BI);
EpdFont lfr14R(&librefranklin_reader_14_regular), lfr14B(&librefranklin_reader_14_bold),
    lfr14I(&librefranklin_reader_14_italic), lfr14BI(&librefranklin_reader_14_bolditalic);
EpdFontFamily lfReader14Family(&lfr14R, &lfr14B, &lfr14I, &lfr14BI);
EpdFont lfr18R(&librefranklin_reader_18_regular), lfr18B(&librefranklin_reader_18_bold),
    lfr18I(&librefranklin_reader_18_italic), lfr18BI(&librefranklin_reader_18_bolditalic);
EpdFontFamily lfReader18Family(&lfr18R, &lfr18B, &lfr18I, &lfr18BI);

EpdFont smallFont(&librefranklin_8_regular);
EpdFontFamily smallFamily(&smallFont);
EpdFont ui12Regular(&librefranklin_12_regular), ui12Bold(&librefranklin_12_bold);
EpdFontFamily ui12Family(&ui12Regular, &ui12Bold);

namespace {

constexpr uint8_t kPointSizes[] = {12, 14, 16, 18};

const char* kSamples[] = {
    "Fjord waffle",
    "Tree Try Trust Yttrium",
    "backdrop Kydd Wry Vamp",
    "AVATAR Wavy Yak Toy",
};

// Read one logical (Portrait) pixel out of the physical landscape framebuffer.
// GfxRenderer::drawPixel rotates Portrait coords with (rotateCoordinates,
// GfxRenderer.cpp:187): phyX = y, phyY = panelHeight - 1 - x, writing 1bpp
// MSB-first where a SET bit is white. This inverts that mapping.
bool logicalPixelIsWhite(const uint8_t* fb, int panelHeight, int panelWidthBytes, int lx, int ly) {
  const int px = ly;
  const int py = (panelHeight - 1) - lx;
  return (fb[py * panelWidthBytes + (px >> 3)] >> (7 - (px & 7))) & 0x1;
}

bool writeMonoPortraitBmp(const char* path, const GfxRenderer& r) {
  const int W = r.getScreenWidth();
  const int H = r.getScreenHeight();
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

// mkdir(2) rather than system("mkdir -p"): no shell, and no process spawn —
// which a sandboxed GUI host would not be allowed to do anyway.
void ensureOutDir(const char* dir) { mkdir(dir, 0755); }

}  // namespace

// The BMP writer above has internal linkage, which is right for it. This is the
// one seam another translation unit in this tool needs (daisy_preview.cpp), so
// it gets an explicit external wrapper rather than the whole anonymous
// namespace being opened up.
bool writeMonoPortraitBmpExternal(const char* path, const GfxRenderer& r) { return writeMonoPortraitBmp(path, r); }

// Defined in daisy_preview.cpp (decision aid for the daisywheel ring redesign).
bool renderDaisyVariants(const char* outDir);

// Wire up the renderer and every builtin font family. Must run before any
// drawText call; the builtin fonts are compressed, so the decompressor has to
// be attached first (mirrors src/main.cpp).
bool simInit() {
  renderer.begin();
  if (!fontDecompressor.init()) {
    fprintf(stderr, "decompressor init failed\n");
    return false;
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, lfReader12Family);
  renderer.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14Family);
  renderer.insertFont(LIBREFRANKLIN_READER_18_FONT_ID, lfReader18Family);
  renderer.insertFont(SMALL_FONT_ID, smallFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12Family);
  return true;
}

bool renderCalendar(int year, int month, int day, const char* outPath) {
  calendar::CalendarSleepScreen::render(
      renderer, calendar::YMD{static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)});
  if (!writeMonoPortraitBmp(outPath, renderer)) return false;
  printf("wrote %s\n", outPath);
  return true;
}

bool renderCalendarWestside(int year, int month, int day, const char* outPath) {
  calendar::CalendarSleepScreen::render(
      renderer, calendar::YMD{static_cast<uint16_t>(year), static_cast<uint8_t>(month), static_cast<uint8_t>(day)}, 5,
      calendar::Style::WestsideEN);
  if (!writeMonoPortraitBmp(outPath, renderer)) return false;
  printf("wrote %s\n", outPath);
  return true;
}

// One page per point size: four sizes of two families would overflow the 792px
// panel and silently clip.
bool renderFontSpecimen(const char* famA, const char* famB, const char* outDir) {
  for (uint8_t sizeEnum = 0; sizeEnum < 4; ++sizeEnum) {
    const int idA = sdFontSystem.loadForDisplay(famA, sizeEnum, renderer);
    const int idB = sdFontSystem.loadForDisplay(famB, sizeEnum, renderer);
    if (idA == 0 || idB == 0) {
      fprintf(stderr, "missing family at size %u (%s=%d %s=%d)\n", kPointSizes[sizeEnum], famA, idA, famB, idB);
      return false;
    }
    auto itA = renderer.getSdCardFonts().find(idA);
    auto itB = renderer.getSdCardFonts().find(idB);
    if (itA == renderer.getSdCardFonts().end() || itB == renderer.getSdCardFonts().end()) {
      fprintf(stderr, "fonts not registered at size %u\n", kPointSizes[sizeEnum]);
      return false;
    }

    renderer.clearScreen(0xFF);
    int y = 6;
    const int margin = 10;
    char hdr[96];
    snprintf(hdr, sizeof(hdr), "%upt  --  %s (top) vs %s (bottom)", kPointSizes[sizeEnum], famA, famB);
    renderer.drawText(UI_12_FONT_ID, margin, y, hdr, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 4;

    for (const char* sample : kSamples) {
      // Kerning is NOT resident until the page is prewarmed: SdCardFont keeps
      // only a per-page mini kern matrix (SdCardFont.h:179), built at
      // SdCardFont.cpp:956 from prewarmStyle. Measuring straight after load()
      // silently reports an UNKERNED font. Each prewarm replaces the previous
      // page's tables, so it has to happen per sample, immediately before use,
      // and metadataOnly must stay false — that path skips the mini kern.
      itA->second->prewarm(sample, 0x0F, /*metadataOnly=*/false);
      const int wA = renderer.getTextWidth(idA, sample);
      renderer.drawText(idA, margin, y, sample);
      char note[32];
      snprintf(note, sizeof(note), "%dpx", wA);
      renderer.drawText(UI_12_FONT_ID, renderer.getScreenWidth() - 52, y, note);
      y += renderer.getLineHeight(idA);

      itB->second->prewarm(sample, 0x0F, /*metadataOnly=*/false);
      const int wB = renderer.getTextWidth(idB, sample);
      renderer.drawText(idB, margin, y, sample);
      snprintf(note, sizeof(note), "%dpx%s", wB, wA == wB ? "" : " *");
      renderer.drawText(UI_12_FONT_ID, renderer.getScreenWidth() - 52, y, note);
      y += renderer.getLineHeight(idB) + 10;

      printf("%2upt  %-26s  %s=%3dpx  %s=%3dpx  delta=%+d\n", kPointSizes[sizeEnum], sample, famA, wA, famB, wB,
             wB - wA);
    }

    char path[256];
    snprintf(path, sizeof(path), "%s/kern_specimen_%u.bmp", outDir, kPointSizes[sizeEnum]);
    if (!writeMonoPortraitBmp(path, renderer)) return false;
    printf("wrote %s\n", path);
  }
  return true;
}

// A whole reading page in ONE family, at each of the four size slots.
//
// renderFontSpecimen() above answers "did this kerning change move a word?" —
// four short strings, two families, no wrapping. Judging a face for READING
// needs the opposite: one family, a full column of wrapped body text, so the
// texture of a paragraph, the leading, and the italic/bold in context are all
// visible at once. Nothing here measures; the output is for looking at.
//
// The body text is written for this harness rather than quoted, so it can carry
// the pairs that decide a face on a 1-bit panel — rn/m, Il1, O0, ligature f's,
// accented capitals, numerals — inside ordinary prose rather than a test string.
namespace {

// Greedy wrap against the real getTextWidth, one word at a time: measuring a
// whole candidate line each time would be O(n^2) on a 90-word paragraph, and
// word-at-a-time is what the reader's own layout does. Draws from `y` down,
// stops before `limit`, returns the y after the last line drawn and reports
// how many lines it managed through `outLines` / `outTruncated`.
int drawWrapped(int id, const char* text, EpdFontFamily::Style style, int x, int y, int colW, int lineH, int limit,
                int* outLines, bool* outTruncated) {
  char line[512] = {0};
  int lineLen = 0;
  int lines = 0;
  bool truncated = false;
  const char* p = text;
  while (*p != '\0') {
    if (y + lineH > limit) {
      truncated = true;
      break;
    }
    const char* sp = strchr(p, ' ');
    const size_t wordLen = sp != nullptr ? static_cast<size_t>(sp - p) : strlen(p);
    char cand[512];
    snprintf(cand, sizeof(cand), "%s%s%.*s", line, lineLen > 0 ? " " : "", (int)wordLen, p);
    if (renderer.getTextWidth(id, cand, style) > colW && lineLen > 0) {
      renderer.drawText(id, x, y, line, true, style);
      y += lineH;
      ++lines;
      snprintf(line, sizeof(line), "%.*s", (int)wordLen, p);
    } else {
      snprintf(line, sizeof(line), "%s", cand);
    }
    lineLen = static_cast<int>(strlen(line));
    p = sp != nullptr ? sp + 1 : p + wordLen;
  }
  if (lineLen > 0 && !truncated) {
    renderer.drawText(id, x, y, line, true, style);
    y += lineH;
    ++lines;
  }
  if (outLines != nullptr) *outLines = lines;
  if (outTruncated != nullptr) *outTruncated = truncated;
  return y;
}

// One word plus the style it is set in. The `inline` specimen exists because
// drawWrapped() takes ONE style for a whole block, which is the wrong shape for
// the question "does this italic belong to this roman" — an italic in its own
// paragraph is judged against nothing, while an italic mid-sentence is judged
// against the roman word touching it. Mixed-source families (a roman from one
// typeface, an italic from another) can only be assessed this way.
struct StyledWord {
  std::string text;
  EpdFontFamily::Style style;
  // No space before this word: it butted straight against the previous one in
  // the source, separated only by a tag. That is how `<i>rounding</i>,` keeps
  // its comma tight instead of rendering as "rounding ," — and glued words
  // wrap as one unit, so a comma can never start a line by itself.
  bool glue;
};

// Split `<i>`-marked text into styled words. Markers may sit inside a word
// (`<i>quiet</i>,`) so punctuation stays with the roman where it belongs.
std::vector<StyledWord> parseInlineMarkup(const char* src) {
  std::vector<StyledWord> out;
  std::string cur;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  EpdFontFamily::Style curStyle = style;
  bool sawSpace = true;  // start of paragraph: nothing to glue to
  bool curGlue = false;
  auto flush = [&]() {
    if (!cur.empty()) {
      out.push_back({cur, curStyle, curGlue});
      cur.clear();
    }
  };
  for (const char* p = src; *p != '\0';) {
    if (strncmp(p, "<i>", 3) == 0) {
      flush();
      style = EpdFontFamily::ITALIC;
      p += 3;
    } else if (strncmp(p, "</i>", 4) == 0) {
      flush();
      style = EpdFontFamily::REGULAR;
      p += 4;
    } else if (strncmp(p, "<b>", 3) == 0) {
      flush();
      style = EpdFontFamily::BOLD;
      p += 3;
    } else if (strncmp(p, "</b>", 4) == 0) {
      flush();
      style = EpdFontFamily::REGULAR;
      p += 4;
    } else if (*p == ' ') {
      flush();
      sawSpace = true;
      ++p;
    } else {
      if (cur.empty()) {
        curStyle = style;
        curGlue = !sawSpace && !out.empty();
        sawSpace = false;
      }
      cur.push_back(*p);
      ++p;
    }
  }
  flush();
  return out;
}

// Draw styled words as one flowing paragraph, wrapping at colW. Advances x per
// word by that word's own width in its own style; kerning is therefore NOT
// applied across a style boundary, which matches how a styled run is laid out
// in the reader (each run is measured and drawn in its own face) rather than
// being a shortcut taken here.
int drawInlineParagraph(int id, const std::vector<StyledWord>& words, int x, int y, int colW, int lineH, int limit) {
  int curX = x;
  // Measured differentially, NOT as getTextWidth(" "): a lone space measures 0
  // through this path, which ran every word of the first build together into
  // one unbroken string. Subtracting the two glyph widths from the spaced pair
  // leaves the space advance plus whatever kerning the pair carries, which is
  // what a word gap actually costs.
  const int spaceW = renderer.getTextWidth(id, "n n", EpdFontFamily::REGULAR) -
                     2 * renderer.getTextWidth(id, "n", EpdFontFamily::REGULAR);
  // Wrap on CLUSTERS, not words: a cluster is a word plus everything glued to
  // it, so `rounding` + `,` measure and break as one unit.
  for (size_t i = 0; i < words.size();) {
    size_t end = i + 1;
    int clusterW = renderer.getTextWidth(id, words[i].text.c_str(), words[i].style);
    while (end < words.size() && words[end].glue) {
      clusterW += renderer.getTextWidth(id, words[end].text.c_str(), words[end].style);
      ++end;
    }
    if (y + lineH > limit) break;
    if (curX > x && curX + spaceW + clusterW > x + colW) {
      y += lineH;
      curX = x;
      if (y + lineH > limit) break;
    } else if (curX > x) {
      curX += spaceW;
    }
    for (size_t k = i; k < end; ++k) {
      renderer.drawText(id, curX, y, words[k].text.c_str(), true, words[k].style);
      curX += renderer.getTextWidth(id, words[k].text.c_str(), words[k].style);
    }
    i = end;
  }
  return y + lineH;
}

}  // namespace

// Inline-mixed specimen: italic set WITHIN roman body text, which is the only
// arrangement that shows whether a borrowed italic matches the roman it serves.
bool renderInlineSpecimen(const char* family, const char* outDir) {
  static const char* kPara1 =
      "The lamp flickered once and held. Marjorie had copied the column out of the "
      "<i>Illinois</i> office three months earlier — 30,865 tons, then 41,072, then a jump "
      "nobody explained. Official policy called the difference <i>rounding</i>, and she did "
      "not believe it.";
  static const char* kPara2 =
      "Every officer aboard knew the difference between <i>quiet</i> and <i>silence</i>. The "
      "first was the absence of noise; the second, as the bosun put it, was <i>the absence of "
      "anyone willing to make any</i>. She wrote both words in the margin and underlined "
      "neither.";
  static const char* kPara3 =
      "She read the entry aloud: <i>thirty thousand eight hundred sixty-five</i>, and then "
      "again, slower. The auditor had signed <b>Approved</b> in a hand that sloped the wrong "
      "way, as though <i>he</i> had been the one in a hurry.";

  for (uint8_t sizeEnum = 0; sizeEnum < 4; ++sizeEnum) {
    const int id = loadSdFontByOrdinal(family, sizeEnum, renderer);
    if (id == 0) {
      fprintf(stderr, "%s: missing at slot %u\n", family, sizeEnum);
      return false;
    }
    auto it = renderer.getSdCardFonts().find(id);
    if (it == renderer.getSdCardFonts().end()) {
      fprintf(stderr, "%s: not registered at slot %u\n", family, sizeEnum);
      return false;
    }

    // One prewarm for the whole page, all four style bits: the mini kern matrix
    // is per-page and per-style, and a stale one measures an unkerned font.
    char page[2048];
    snprintf(page, sizeof(page), "%s %s %s", kPara1, kPara2, kPara3);
    it->second->prewarm(page, 0x0F, /*metadataOnly=*/false);

    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    const int margin = left + 14;
    const int colW = renderer.getScreenWidth() - margin - (right + 14);
    const int lineH = renderer.getLineHeight(id);

    renderer.clearScreen(0xFF);
    int y = top + 6;

    char hdr[160];
    snprintf(hdr, sizeof(hdr), "%s  --  slot %u  --  inline italic", family, sizeEnum);
    renderer.drawText(UI_12_FONT_ID, margin, y, hdr, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 10;

    const int pageBottom = renderer.getScreenHeight() - bottom - 6;
    for (const char* para : {kPara1, kPara2, kPara3}) {
      if (y + lineH > pageBottom) break;
      y = drawInlineParagraph(id, parseInlineMarkup(para), margin, y, colW, lineH, pageBottom);
      y += lineH / 2;
    }

    printf("%-20s slot %u  line %2dpx  col %dpx\n", family, sizeEnum, lineH, colW);

    char path[256];
    snprintf(path, sizeof(path), "%s/inline_%s_%u.bmp", outDir, family, sizeEnum);
    if (!writeMonoPortraitBmp(path, renderer)) return false;
  }
  return true;
}

bool renderReadingSpecimen(const char* family, const char* outDir) {
  static const char* kBody =
      "The lamp on the far wall flickered once and held, and for a while nobody "
      "in the reading room moved. Outside, rain filled the gutters. Marjorie "
      "turned page 148 of the ledger, following a column of figures she had "
      "copied out of the Illinois office three months earlier — 30,865 tons, "
      "then 41,072, then a jump nobody had ever explained. Official policy said "
      "the difference was rounding. She did not believe it, and neither, she "
      "suspected, did the auditor who had signed off on it.";
  static const char* kItalic = "Every officer aboard knew the difference between quiet and silence.";
  static const char* kBold = "Chapter Nine: The Ledger of the Wharf";
  static const char* kHard = "Illinois 1lI0O · rn m · cl d · 3/8 5/6 · fjord ffi · ÄÖÜ Café";
  static const char* kAlphabet = "abcdefghijklmnopqrstuvwxyz";

  for (uint8_t sizeEnum = 0; sizeEnum < 4; ++sizeEnum) {
    // Ordinal slot, NOT nearest-to-12/14/16/18: families reach the same
    // x-height at different point sizes, so slot 3 of a 10/12/14/15 family is
    // its 15pt file, which a nearest-match to 18pt would collapse onto slot 2.
    const int id = loadSdFontByOrdinal(family, sizeEnum, renderer);
    if (id == 0) {
      fprintf(stderr, "%s: missing at slot %u\n", family, sizeEnum);
      return false;
    }
    auto it = renderer.getSdCardFonts().find(id);
    if (it == renderer.getSdCardFonts().end()) {
      fprintf(stderr, "%s: not registered at slot %u\n", family, sizeEnum);
      return false;
    }

    // One prewarm for the whole page: SdCardFont's mini kern matrix is
    // per-page and each prewarm replaces the last, so wrapping against a
    // stale one would measure an unkerned font (see renderFontSpecimen).
    char page[1536];
    snprintf(page, sizeof(page), "%s %s %s %s %s", kBody, kItalic, kBold, kHard, kAlphabet);
    it->second->prewarm(page, 0x0F, /*metadataOnly=*/false);

    int top, right, bottom, left;
    renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
    const int margin = left + 14;
    const int colW = renderer.getScreenWidth() - margin - (right + 14);
    const int lineH = renderer.getLineHeight(id);

    renderer.clearScreen(0xFF);
    int y = top + 6;

    char hdr[128];
    snprintf(hdr, sizeof(hdr), "%s  --  slot %u  --  line %dpx", family, sizeEnum, lineH);
    renderer.drawText(UI_12_FONT_ID, margin, y, hdr, true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID) + 10;

    const int pageBottom = renderer.getScreenHeight() - bottom - 6;
    y = drawWrapped(id, kBold, EpdFontFamily::BOLD, margin, y, colW, lineH, pageBottom, nullptr, nullptr);
    y += lineH / 2;

    // The tail (italic sample + confusables) is the point of the page, so the
    // body gets whatever is left after reserving room for it. Without this the
    // 18px slot runs off the bottom and GfxRenderer logs every clipped glyph.
    const int bodyLimit = pageBottom - (4 * lineH + lineH);

    int lines = 0;
    bool truncated = false;
    y = drawWrapped(id, kBody, EpdFontFamily::REGULAR, margin, y, colW, lineH, bodyLimit, &lines, &truncated);

    y += lineH / 2;
    y = drawWrapped(id, kItalic, EpdFontFamily::ITALIC, margin, y, colW, lineH, pageBottom, nullptr, nullptr);
    y += lineH / 2;
    y = drawWrapped(id, kHard, EpdFontFamily::REGULAR, margin, y, colW, lineH, pageBottom, nullptr, nullptr);

    // Set width is the other half of "readable" on a 528px column: a wider
    // face at the same x-height fits fewer words per line and so more page
    // turns per chapter. Report it rather than leaving it to the eye.
    const int alphaW = renderer.getTextWidth(id, kAlphabet);
    printf("%-20s slot %u  line %2dpx  lc-alphabet %3dpx  body lines %2d%s  col %dpx\n", family, sizeEnum, lineH,
           alphaW, lines, truncated ? " (cut)" : "", colW);

    char path[256];
    snprintf(path, sizeof(path), "%s/reading_%s_%u.bmp", outDir, family, sizeEnum);
    if (!writeMonoPortraitBmp(path, renderer)) return false;
  }
  return true;
}

#ifndef SIM_NO_MAIN

namespace {

int usage(const char* argv0) {
  fprintf(stderr,
          "CrossPoint off-device render harness (NOT the simulator -- that is\n"
          "~/src/crosspoint-simulator, run via `pio run -e simulator -t run_simulator`)\n\n"
          "  %s calendar [YYYY MM DD]                render Spanish/CR sleep screen -> fs_/sleep.bmp\n"
          "  %s calendar-westside [YYYY MM DD]        render Westside/EN sleep screen -> fs_/sleep.bmp\n"
          "  %s fonts FAMILY_A FAMILY_B               A-B two SD font families -> fs_/kern_specimen_*.bmp\n"
          "  %s reading FAMILY                        a wrapped body-text page -> fs_/reading_FAMILY_*.bmp\n"
          "  %s inline FAMILY                         italic set INLINE with roman -> fs_/inline_FAMILY_*.bmp\n"
          "  %s YYYY MM DD                            legacy calendar form\n\n"
          "fonts, reading and inline modes read CPFONT_DIR (use /.fonts), a\n"
          "DEVICE-style path; the stub HalStorage prefixes ./fs_ to it.\n",
          argv0, argv0, argv0, argv0, argv0, argv0);
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  if (!simInit()) return 1;
  ensureOutDir("./fs_");

  printf("screen %dx%d  panel %ux%u\n", renderer.getScreenWidth(), renderer.getScreenHeight(),
         renderer.getDisplayWidth(), renderer.getDisplayHeight());

  const char* mode = argc > 1 ? argv[1] : "calendar";

  if (strcmp(mode, "fonts") == 0) {
    if (argc != 4) return usage(argv[0]);
    return renderFontSpecimen(argv[2], argv[3], "./fs_") ? 0 : 1;
  }

  if (strcmp(mode, "daisy") == 0) {
    return renderDaisyVariants("./fs_") ? 0 : 1;
  }

  if (strcmp(mode, "inline") == 0) {
    if (argc != 3) return usage(argv[0]);
    return renderInlineSpecimen(argv[2], "./fs_") ? 0 : 1;
  }

  if (strcmp(mode, "reading") == 0) {
    if (argc != 3) return usage(argv[0]);
    return renderReadingSpecimen(argv[2], "./fs_") ? 0 : 1;
  }

  // "calendar [Y M D]", "calendar-westside [Y M D]", bare "calendar", and the
  // legacy bare "Y M D" that sweep.py and check_centering.py drive.
  int y = 2026, m = 7, d = 27;
  if (strcmp(mode, "calendar-westside") == 0) {
    if (argc == 5) {
      y = atoi(argv[2]);
      m = atoi(argv[3]);
      d = atoi(argv[4]);
    } else if (argc != 2)
      return usage(argv[0]);
    return renderCalendarWestside(y, m, d, "./fs_/sleep.bmp") ? 0 : 1;
  } else if (strcmp(mode, "calendar") == 0) {
    if (argc == 5) {
      y = atoi(argv[2]);
      m = atoi(argv[3]);
      d = atoi(argv[4]);
    } else if (argc != 2)
      return usage(argv[0]);
  } else if (argc == 4 && atoi(argv[1]) > 1000) {
    y = atoi(argv[1]);
    m = atoi(argv[2]);
    d = atoi(argv[3]);
  } else {
    return usage(argv[0]);
  }

  return renderCalendar(y, m, d, "./fs_/sleep.bmp") ? 0 : 1;
}

#endif  // SIM_NO_MAIN
