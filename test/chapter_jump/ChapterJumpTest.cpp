// WHERE A CHAPTER SELECT PICK LANDS -- through the whole chain, not just the parser.
//
// test/toc_anchor_page drives ChapterHtmlSlimParser directly and HANDS IT the
// list of TOC anchors. That is the half it can see. The other half is who
// builds that list: Section::startBuild reads it out of the book's real table
// of contents, and an anchor missing from it is an anchor with no forced
// chapter break -- the chapter then starts in the middle of whatever page the
// previous one ended on, and the pick opens on a page of the PREVIOUS chapter.
// A parser-level suite cannot reach that, because it never asks where the list
// came from.
//
// So this suite starts one layer up, at the real .epub on disk:
//
//   Epub::load()  ->  container, OPF, nav/NCX  ->  BookMetadataCache
//   Section::loadSectionFile / startBuild      ->  the TOC-anchor list
//   ChapterHtmlSlimParser                      ->  pagination + anchor map
//   Section::findAnchor                        ->  the page the reader opens
//
// and asserts the one thing the owner actually checks: the page a pick lands
// on OPENS WITH THAT CHAPTER.
//
// THE FIXTURE (test/epubs/test_toc_revisit.epub) is a two-file book whose
// table of contents visits ch1.xhtml, steps out to ch2.xhtml, then comes back
// to ch1.xhtml -- an ordinary shape (a "Notes" or "Appendix" entry between
// chapters of one file, or any nav that is not grouped by file). Its twelve
// chapters open with <h4>, so the TOC break is the only thing that starts them
// on a fresh page: h1-h3 open one on their own (ChapterHtmlSlimParser's header
// branch), which would mask the defect. Each chapter carries one more filler
// paragraph than the last, so the openings sweep across every position on the
// page.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "fontIds.h"

HalDisplay display;
// Cover decoding is not under test and the real decoders reach JPEGDEC/PNGdec.
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

class Gfx {
 public:
  static Gfx& instance() {
    static Gfx g;
    return g;
  }
  GfxRenderer& renderer() { return renderer_; }

 private:
  Gfx() : renderer_(display), cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()) {
    renderer_.begin();
    if (!decompressor_.init()) ADD_FAILURE() << "font decompressor init failed";
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14_);
    Hyphenator::setPreferredLanguage("en");
  }
  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont lfr14R_{&librefranklin_reader_14_regular}, lfr14B_{&librefranklin_reader_14_bold},
      lfr14I_{&librefranklin_reader_14_italic}, lfr14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lfReader14_{&lfr14R_, &lfr14B_, &lfr14I_, &lfr14BI_};
};

// The reader's spec, minus the settings store (CrossPointSettings is not linked
// here): the fields that move a line break, at the reader's own defaults.
ReaderRenderSpec readerSpec(const uint16_t width, const uint16_t height) {
  ReaderRenderSpec spec;
  spec.fontId = LIBREFRANKLIN_READER_14_FONT_ID;
  spec.smallFontId = LIBREFRANKLIN_READER_14_FONT_ID;
  spec.viewportWidth = width;
  spec.viewportHeight = height;
  spec.extraParagraphSpacing = true;
  spec.embeddedStyle = true;
  spec.ligatureFingerprint = 1;  // 0 is "no preference at all"; see ReaderRenderSpec
  return spec;
}

// The first line of text on a page, as the reader would paint it.
std::string firstLineOf(Section& section, const int page) {
  const auto p = section.loadPage(page);
  if (!p) return "<page load failed>";
  for (const auto& element : p->elements) {
    const auto line = std::dynamic_pointer_cast<PageLine>(element);
    if (!line || !line->getBlock()) continue;
    const TextBlock& block = *line->getBlock();
    std::string text;
    for (uint16_t i = 0; i < block.wordCount(); i++) {
      if (!text.empty()) text += ' ';
      text += block.wordText(i);
    }
    return text;
  }
  return "<no text on the page>";
}

std::string fixturePath() { return std::string(CROSSPOINT_TEST_EPUB_DIR) + "/test_toc_revisit.epub"; }

// A cache directory of this suite's own, cleared between cases so every build
// starts from the same place the device would after a section-format bump.
class Cache {
 public:
  explicit Cache(const char* name) : path_(std::string("/tmp/cp_chapter_jump_") + name) {
    (void)system(("rm -rf '" + path_ + "'").c_str());
    (void)system(("mkdir -p '" + path_ + "'").c_str());
  }
  ~Cache() { (void)system(("rm -rf '" + path_ + "'").c_str()); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

// What EpubReaderActivity::render() does with a ChapterResult: open the picked
// spine, resolve the entry's fragment against the section's anchor map, and
// fall back to page 0 when there is no fragment. Returns -1 if the section
// could not be built.
int pageForPick(const std::shared_ptr<Epub>& epub, const BookMetadataCache::TocEntry& item,
                const ReaderRenderSpec& spec, Section& section) {
  if (!section.loadSectionFile(spec) && !section.createSectionFile(spec)) return -1;
  if (item.anchor.empty()) return 0;
  const auto page = section.findAnchor(item.anchor);
  return page ? static_cast<int>(*page) : 0;
}

}  // namespace

// EVERY PICK OPENS ITS OWN CHAPTER.
//
// Swept across viewport heights so each chapter opening falls at a different
// place on the page: the widow/orphan holdback and the forced break decide
// different things at the top of a page than at its foot, and a break that is
// missing only shows when the previous chapter did not happen to end on a page
// boundary.
TEST(ChapterJump, EveryPickOpensItsChapter) {
  for (const uint16_t height : {560, 620, 660, 700, 740, 800}) {
    for (const uint16_t width : {420, 480, 600}) {
      const Cache cache("opens");
      auto epub = std::make_shared<Epub>(fixturePath(), cache.path());
      ASSERT_TRUE(epub->load()) << "could not load " << fixturePath();
      epub->setupCacheDir();
      ASSERT_GT(epub->getTocItemsCount(), 0);

      const ReaderRenderSpec spec = readerSpec(width, height);
      for (int i = 0; i < epub->getTocItemsCount(); i++) {
        const auto item = epub->getTocItem(i);
        ASSERT_GE(item.spineIndex, 0) << "TOC entry " << i << " (" << item.title << ") resolves to no spine item";
        Section section(epub, item.spineIndex, Gfx::instance().renderer());
        const int page = pageForPick(epub, item, spec, section);
        ASSERT_GE(page, 0) << "section build failed for spine " << item.spineIndex;
        ASSERT_LT(page, static_cast<int>(section.pageCount));
        EXPECT_EQ(firstLineOf(section, page).rfind(item.title, 0), 0u)
            << "at " << width << "x" << height << ", picking '" << item.title << "' opens spine " << item.spineIndex
            << " page " << page << " of " << section.pageCount << ", which starts '" << firstLineOf(section, page)
            << "'";
      }
    }
  }
}

// The reader does NOT build the whole chapter for a fragment pick: it starts an
// incremental build and stops the moment findAnchor() answers
// (EpubReaderActivity::render(), the anchorJump branch). That has to land on
// the same page as a finished build, or a pick into a chapter the reader has
// not read yet lands somewhere else than the same pick after it has.
TEST(ChapterJump, IncrementalBuildLandsWhereTheFullBuildDoes) {
  const ReaderRenderSpec spec = readerSpec(480, 700);

  std::map<std::string, int> full;
  {
    const Cache cache("full");
    auto epub = std::make_shared<Epub>(fixturePath(), cache.path());
    ASSERT_TRUE(epub->load());
    epub->setupCacheDir();
    for (int i = 0; i < epub->getTocItemsCount(); i++) {
      const auto item = epub->getTocItem(i);
      if (item.anchor.empty()) continue;
      Section section(epub, item.spineIndex, Gfx::instance().renderer());
      full[item.anchor] = pageForPick(epub, item, spec, section);
    }
  }

  const Cache cache("incremental");
  auto epub = std::make_shared<Epub>(fixturePath(), cache.path());
  ASSERT_TRUE(epub->load());
  epub->setupCacheDir();
  for (int i = 0; i < epub->getTocItemsCount(); i++) {
    const auto item = epub->getTocItem(i);
    if (item.anchor.empty()) continue;
    // No cache for this spine yet: the pick takes the build-to-the-anchor path.
    (void)system(("rm -f '" + epub->getCachePath() + "'/sections/*").c_str());
    Section section(epub, item.spineIndex, Gfx::instance().renderer());
    ASSERT_FALSE(section.loadSectionFile(spec));
    ASSERT_TRUE(section.startBuild(spec));
    while (!section.isBuildComplete() && !section.findAnchor(item.anchor)) {
      ASSERT_TRUE(section.buildSomeMore(8));
    }
    const auto page = section.findAnchor(item.anchor);
    ASSERT_TRUE(page.has_value()) << "'" << item.title << "' never resolved during the build";
    EXPECT_EQ(static_cast<int>(*page), full[item.anchor]) << "'" << item.title << "' lands elsewhere mid-build";
    section.abandonBuild();
  }
}

// A suspended build leaves a PARTIAL section file, and the next pick resolves
// its fragment from that partial's on-disk anchor map with no build at all
// (EpubReaderActivity::render(), "Partial covers target ... deferring"). The
// partial's answer must be the finished build's answer.
TEST(ChapterJump, APartialAnswersTheSameAsAFinishedBuild) {
  const ReaderRenderSpec spec = readerSpec(480, 700);

  std::map<std::string, int> full;
  {
    const Cache cache("partial_truth");
    auto epub = std::make_shared<Epub>(fixturePath(), cache.path());
    ASSERT_TRUE(epub->load());
    epub->setupCacheDir();
    for (int i = 0; i < epub->getTocItemsCount(); i++) {
      const auto item = epub->getTocItem(i);
      if (item.anchor.empty()) continue;
      Section section(epub, item.spineIndex, Gfx::instance().renderer());
      full[item.anchor] = pageForPick(epub, item, spec, section);
    }
  }

  const Cache cache("partial");
  auto epub = std::make_shared<Epub>(fixturePath(), cache.path());
  ASSERT_TRUE(epub->load());
  epub->setupCacheDir();
  // Leave a partial behind for every spine, as an exit mid-build does.
  for (int s = 0; s < epub->getSpineItemsCount(); s++) {
    Section section(epub, s, Gfx::instance().renderer());
    if (section.loadSectionFile(spec) || !section.startBuild(spec)) continue;
    for (int chunk = 0; chunk < 3 && !section.isBuildComplete(); chunk++) ASSERT_TRUE(section.buildSomeMore(4));
    // ~Section() suspends the build and commits the partial.
  }
  int checked = 0;
  for (int i = 0; i < epub->getTocItemsCount(); i++) {
    const auto item = epub->getTocItem(i);
    if (item.anchor.empty()) continue;
    Section section(epub, item.spineIndex, Gfx::instance().renderer());
    if (!section.loadSectionFile(spec) || !section.isPartial()) continue;
    const auto onDisk = section.getPageForAnchor(item.anchor);
    if (!onDisk) continue;  // past the watermark: the reader builds further instead
    checked++;
    EXPECT_EQ(static_cast<int>(*onDisk), full[item.anchor]) << "the partial disagrees for '" << item.title << "'";
    EXPECT_EQ(firstLineOf(section, static_cast<int>(*onDisk)).rfind(item.title, 0), 0u)
        << "'" << item.title << "' does not open the page the partial names";
  }
  EXPECT_GT(checked, 0) << "no anchor fell inside a partial; the case under test never ran";
}
