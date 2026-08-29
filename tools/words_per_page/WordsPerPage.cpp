// WORDS PER FULL PAGE — measured through the real firmware paginator.
//
// Measure-only scratch instrument (2026-08-26). Nothing here ships and nothing
// it links is modified.
//
// It links the SAME translation units test/table_keep_together/CMakeLists.txt
// links — ChapterHtmlSlimParser, the CSS parser, ParsedText/TextBlock, the
// Liang hyphenator, expat, GfxRenderer, SdCardFont — so the pages it counts are
// the pages the device's paginator emits: real line breaking, real hyphenation,
// real auto-justify demotion, real widow/orphan holdback, real half-line
// paragraph gap, real chapter sinkage.
//
// The only thing it does that the reader does not is load an SD family by
// ORDINAL SLOT rather than by nominal point size, so a six-slot ramp can be
// walked end to end.
//
// Word counting mirrors EpubReaderActivity::publishReadingSample: walk the
// page's TAG_PageLine elements and count the tokens on each block. A word the
// breaker HYPHENATED arrives as two tokens ("hyphen-" + "ation"), so the
// corrected count drops any token ending in '-' — the prefix — which restores
// the source word count. Both are reported.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <SdCardFont.h>
#include <builtinFonts/all.h>
#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/AutoJustify.h"
#include "Epub/Page.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "fontIds.h"

HalDisplay display;

// Same two stubs test/table_keep_together defines, for the same reason: both
// are reachable only from the <img> path and no fixture here has one.
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

GfxRenderer* g_renderer = nullptr;
FontDecompressor* g_decompressor = nullptr;
FontCacheManager* g_cache = nullptr;

// ---------------------------------------------------------------------------
// SD family, by ordinal slot
// ---------------------------------------------------------------------------

std::vector<int> installedSizes(const std::string& dir, const std::string& family) {
  std::vector<int> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  const std::string prefix = family + "_";
  while (dirent* e = readdir(d)) {
    const std::string n = e->d_name;
    if (n.size() < prefix.size() + 8) continue;
    if (n.compare(0, prefix.size(), prefix) != 0) continue;
    if (n.compare(n.size() - 7, 7, ".cpfont") != 0) continue;
    out.push_back(std::atoi(n.substr(prefix.size()).c_str()));
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

int registerSdFont(const std::string& path) {
  static int nextId = 900000;
  auto* font = new SdCardFont();
  if (!font->load(path.c_str())) {
    std::fprintf(stderr, "load failed: %s\n", path.c_str());
    delete font;
    return 0;
  }
  const int id = nextId++;
  g_renderer->registerSdCardFont(id, font);
  g_renderer->insertFont(
      id, EpdFontFamily(font->getEpdFont(0), font->getEpdFont(1), font->getEpdFont(2), font->getEpdFont(3)));
#if CROSSPOINT_RENDER_SCALE > 1
  const size_t slash = path.find_last_of('/');
  const std::string hiResPath =
      path.substr(0, slash + 1) + std::to_string(CROSSPOINT_RENDER_SCALE) + "x/" + path.substr(slash + 1);
  auto* hiRes = new SdCardFont();
  if (hiRes->load(hiResPath.c_str())) {
    g_renderer->registerHiResFont(
        id, hiRes,
        EpdFontFamily(hiRes->getEpdFont(0), hiRes->getEpdFont(1), hiRes->getEpdFont(2), hiRes->getEpdFont(3)));
  } else {
    delete hiRes;
    std::fprintf(stderr, "WARNING: no hi-res companion at %s\n", hiResPath.c_str());
  }
#endif
  return id;
}

// ---------------------------------------------------------------------------
// Counting one emitted page
// ---------------------------------------------------------------------------

struct PageCount {
  int rawTokens = 0;
  int words = 0;  // hyphen-prefix corrected
  int chars = 0;  // codepoints, soft hyphens excluded
  int lines = 0;
};

int codepointsOf(const char* t) {
  int n = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(t); *p;) {
    if (p[0] == 0xC2 && p[1] == 0xAD) {
      p += 2;
      continue;
    }
    if ((p[0] & 0xC0) != 0x80) n++;
    p++;
  }
  return n;
}

bool tokenIsBlank(const char* t) {
  if (!t) return true;
  for (const char* p = t; *p; ++p)
    if (static_cast<unsigned char>(*p) > ' ') return false;
  return true;
}

// PGM of the current framebuffer, physical pixels, so the same page can be
// looked at rather than only counted. Same axis mapping as
// tools/calendar_preview/render_harness.cpp's writeMonoPortraitBmp.
bool logicalPixelIsWhite(const uint8_t* fb, int panelHeight, int panelWidthBytes, int lx, int ly) {
  const int px = ly;
  const int py = (panelHeight - 1) - lx;
  return (fb[py * panelWidthBytes + (px >> 3)] >> (7 - (px & 7))) & 0x1;
}

void writePgm(const char* path, const GfxRenderer& r) {
  const int S = CROSSPOINT_RENDER_SCALE;
  const int W = r.getScreenWidth() * S;
  const int H = r.getScreenHeight() * S;
  const int panelH = r.getDisplayHeight();
  const int panelWB = r.getDisplayWidthBytes();
  const uint8_t* fb = r.getFrameBuffer();
  FILE* f = std::fopen(path, "wb");
  if (!f) return;
  std::fprintf(f, "P5\n%d %d\n255\n", W, H);
  std::vector<uint8_t> row(W);
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) row[x] = logicalPixelIsWhite(fb, panelH, panelWB, x, y) ? 255 : 0;
    std::fwrite(row.data(), 1, W, f);
  }
  std::fclose(f);
}

PageCount countPage(const Page& page) {
  PageCount c;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || !block->valid() || block->isEmpty()) continue;
    bool lineHadInk = false;
    const uint16_t n = block->wordCount();
    for (uint16_t i = 0; i < n; i++) {
      const char* t = block->wordText(i);
      if (tokenIsBlank(t)) continue;
      lineHadInk = true;
      c.rawTokens++;
      c.chars += codepointsOf(t);
      const size_t len = std::strlen(t);
      if (len == 0 || t[len - 1] != '-') c.words++;
    }
    if (lineHadInk) c.lines++;
  }
  return c;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s FONTROOT FAMILY CHAPTER.xhtml [slot]\n", argv[0]);
    return 2;
  }
  const std::string fontRoot = argv[1];
  const std::string family = argv[2];
  const std::string chapter = argv[3];
  const int onlySlot = argc > 4 ? std::atoi(argv[4]) : -1;
  // CROSSPOINT_WPP_DUMP_PAGE=<n> writes fs_-free PGMs of page n for every slot
  // measured, so the SAME paginated page can be looked at as well as counted.
  const char* dumpEnv = std::getenv("CROSSPOINT_WPP_DUMP_PAGE");
  const int dumpPage = dumpEnv ? std::atoi(dumpEnv) : -1;
  const char* dumpDir = std::getenv("CROSSPOINT_WPP_DUMP_DIR");

  GfxRenderer renderer(display);
  FontDecompressor decompressor;
  FontCacheManager cache(renderer.getFontMap(), renderer.getSdCardFonts()
#if CROSSPOINT_RENDER_SCALE > 1
                                                    ,
                         renderer.getHiResSdCardFonts()
#endif
  );
  g_renderer = &renderer;
  g_decompressor = &decompressor;
  g_cache = &cache;

  renderer.begin();
  if (!decompressor.init()) {
    std::fprintf(stderr, "decompressor init failed\n");
    return 1;
  }
  cache.setFontDecompressor(&decompressor);
  renderer.setFontCacheManager(&cache);
  Hyphenator::setPreferredLanguage("en");

  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int screenMargin = 5;  // CrossPointSettings::SCREEN_MARGIN_DEFAULT
  const uint16_t viewportWidth =
      static_cast<uint16_t>(renderer.getScreenWidth() - (left + screenMargin) - (right + screenMargin));
  const uint16_t viewportHeight =
      static_cast<uint16_t>(renderer.getScreenHeight() - (top + screenMargin) - (bottom + screenMargin));

  const std::string dir = fontRoot + "/" + family;
  const std::vector<int> sizes = installedSizes(dir, family);
  if (sizes.empty()) {
    std::fprintf(stderr, "no cuts in %s\n", dir.c_str());
    return 1;
  }

  std::printf("# family=%s scale=%d viewport=%ux%u slots=%zu\n", family.c_str(), CROSSPOINT_RENDER_SCALE, viewportWidth,
              viewportHeight, sizes.size());

  for (size_t slot = 0; slot < sizes.size(); slot++) {
    if (onlySlot >= 0 && static_cast<int>(slot) != onlySlot) continue;
    char path[1024];
    std::snprintf(path, sizeof path, "%s/%s_%d.cpfont", dir.c_str(), family.c_str(), sizes[slot]);
    const int pointSize = sizes[slot];
    const int fontId = registerSdFont(path);
    if (fontId == 0) continue;

    std::vector<PageCount> pages;
    std::vector<std::unique_ptr<Page>> kept;
    ChapterHtmlSlimParser parser(
        nullptr, chapter, renderer, fontId, /*smallFontId=*/0,
        /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/true, /*paragraphAlignment=*/0, viewportWidth,
        viewportHeight, /*hyphenationEnabled=*/true, /*focusReadingEnabled=*/false,
        /*lineGrid=*/false, autojustify::THRESHOLD_CHARS,
        [&](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
          pages.push_back(countPage(*page));
          if (dumpPage >= 0) kept.push_back(std::move(page));
        },
        /*embeddedStyle=*/false, /*contentBase=*/"", /*imageBasePath=*/"");
    parser.parseAndBuildPages();

    // Page 1 carries the chapter sinkage (viewportHeight/5 rounded to a whole
    // line) and the trailing page is whatever was left over. Neither is a FULL
    // page, so neither is counted.
    long sw = 0, sc = 0, sl = 0, st = 0;
    int n = 0;
    for (size_t i = 1; i + 1 < pages.size(); i++) {
      sw += pages[i].words;
      sc += pages[i].chars;
      sl += pages[i].lines;
      st += pages[i].rawTokens;
      n++;
    }
    if (n == 0) {
      std::fprintf(stderr, "%s slot %zu: only %zu pages\n", family.c_str(), slot, pages.size());
      continue;
    }
    const double lineH = renderer.getLineHeight(fontId, 1.0f);
    std::printf(
        "%-16s slot %zu  pt %2d  lineH %2d  meanLines %5.2f  wordsPerPage %7.2f  "
        "tokensPerPage %7.2f  charsPerPage %7.1f  wordsPerLine %5.2f  fullPages %d\n",
        family.c_str(), slot, pointSize, static_cast<int>(lineH), static_cast<double>(sl) / n,
        static_cast<double>(sw) / n, static_cast<double>(st) / n, static_cast<double>(sc) / n,
        static_cast<double>(sw) / static_cast<double>(sl), n);
    if (dumpPage >= 0 && dumpPage < static_cast<int>(kept.size())) {
      // The reader paints the page capInkTrim px higher than the layout's y —
      // EpubReaderActivity::renderContents' paintMarginTop.
      const int paintTop = (top + screenMargin) - renderer.getCapInkTrim(fontId);
      renderer.clearScreen(0xFF);
      kept[dumpPage]->render(renderer, fontId, left + screenMargin, paintTop);
      char out[1024];
      std::snprintf(out, sizeof out, "%s/page_%s_%zu.pgm", dumpDir ? dumpDir : ".", family.c_str(), slot);
      writePgm(out, renderer);
      std::fprintf(stderr, "wrote %s (page %d of %zu, %d words)\n", out, dumpPage, kept.size(), pages[dumpPage].words);
    }
    std::fflush(stdout);
  }
  return 0;
}
