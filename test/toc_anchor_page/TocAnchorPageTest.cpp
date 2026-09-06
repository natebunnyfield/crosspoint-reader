// Where a table-of-contents pick lands.
//
// THE REPORT (owner, 2026-09-06): "chapter selection is going to the page
// before intended place".
//
// A TOC entry with a fragment (ch02.xhtml#chapter-2) resolves through the
// anchor map the parser records while it paginates -- {id, page} pairs, with
// a forced page break in front of the id when it is a TOC anchor
// (flushPendingAnchor). The reader then opens that page. So the question is
// simply: for every shape a chapter opening takes in real books, is the
// recorded page the one the heading is on, and is the heading at its top?
//
//   <h2 id="c2">                         the id on the heading itself
//   <div id="c2"><h2>                    a wrapper (Calibre, InDesign)
//   <section id="c2" epub:type="chapter"> Standard Ebooks
//   <a id="c2"></a><h2>                  an empty anchor before the heading
//   <h2><a id="c2"></a>Heading</h2>      Project Gutenberg: the anchor INSIDE
//   <p id="c2">                          no heading at all
//   <hr/><h2 id="c2">                    a rule before the heading
//
// Each shape runs after 0..N filler paragraphs so the chapter opening lands
// at every position on the page: flush with the top, mid-page, and right at
// the foot where the widow/orphan holdback (keep-2/2) decides the paragraph
// before it. The chain is the real one (parser, layout engine, built-in
// reader face, a real XHTML file on disk), as in definition_list.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/AutoJustify.h"
#include "Epub/Page.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/css/CssParser.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "fontIds.h"

HalDisplay display;

bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

constexpr uint16_t kViewportWidth = 600;
constexpr uint16_t kViewportHeight = 700;

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
    if (!decompressor_.init()) {
      ADD_FAILURE() << "font decompressor init failed";
    }
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

using PageLines = std::vector<std::string>;

struct Laid {
  std::vector<PageLines> pages;
  std::vector<std::pair<std::string, uint16_t>> anchors;
};

std::string writeTemp(const char* pattern, const std::string& contents) {
  std::string path(pattern);
  const int fd = mkstemp(path.data());
  if (fd < 0) {
    ADD_FAILURE() << "mkstemp failed";
    return {};
  }
  const ssize_t wrote = ::write(fd, contents.data(), contents.size());
  ::close(fd);
  if (wrote != static_cast<ssize_t>(contents.size())) {
    ADD_FAILURE() << "short write of the fixture";
    ::remove(path.c_str());
    return {};
  }
  return path;
}

Laid layout(const std::string& bodyHtml, std::vector<std::string> tocAnchors) {
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><head><title>t</title></head><body>" +
      bodyHtml + "</body></html>";
  const std::string path = writeTemp("/tmp/cp_tocanchor_XXXXXX", doc);
  Laid out;
  if (path.empty()) return out;

  ChapterHtmlSlimParser parser(
      nullptr, path, Gfx::instance().renderer(), LIBREFRANKLIN_READER_14_FONT_ID,
      /*smallFontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/true,
      /*paragraphAlignment=*/0, kViewportWidth, kViewportHeight, /*hyphenationEnabled=*/false,
      /*focusReadingEnabled=*/false, /*lineGridEnabled=*/false,
      /*justifyThresholdChars=*/autojustify::THRESHOLD_CHARS,
      [&out](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
        PageLines lines;
        for (const auto& element : page->elements) {
          const auto line = std::dynamic_pointer_cast<PageLine>(element);
          if (!line || !line->getBlock()) continue;
          const TextBlock& block = *line->getBlock();
          std::string text;
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!text.empty()) text += ' ';
            text += block.wordText(i);
          }
          lines.push_back(std::move(text));
        }
        out.pages.push_back(std::move(lines));
      },
      /*embeddedStyle=*/false, /*contentBase=*/"", /*imageBasePath=*/"",
      /*imageRendering=*/0, std::move(tocAnchors), /*popupFn=*/nullptr, nullptr);
  parser.parseAndBuildPages();
  out.anchors = parser.getAnchors();
  ::remove(path.c_str());
  if (getenv("CROSSPOINT_DUMP_PAGES")) {
    for (size_t p = 0; p < out.pages.size(); p++) {
      printf("--- page %zu ---\n", p);
      for (const auto& l : out.pages[p]) printf("  | %s\n", l.c_str());
    }
    for (const auto& [id, page] : out.anchors) printf("anchor %s -> page %u\n", id.c_str(), page);
  }
  return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

int pageOf(const std::vector<PageLines>& pages, const std::string& needle) {
  for (size_t p = 0; p < pages.size(); p++) {
    for (const auto& l : pages[p]) {
      if (contains(l, needle)) return static_cast<int>(p);
    }
  }
  return -1;
}

int anchorPage(const Laid& laid, const std::string& id) {
  for (const auto& [key, page] : laid.anchors) {
    if (key == id) return page;
  }
  return -1;
}

std::string filler(const int count) {
  std::string out;
  for (int i = 0; i < count; i++) {
    // Three lines each at this measure, so the keep-2/2 holdback has something
    // to decide at every page foot the loop below sweeps past.
    out += "<p>Filler paragraph number " + std::to_string(i) +
           " runs to a third line of prose so the widow and orphan rules have work to do at the foot of the "
           "page, where the chapter opening that follows it must still land cleanly.</p>";
  }
  return out;
}

constexpr const char* kHeading = "Chaptertwo";
constexpr const char* kOpening = "Openingline";
constexpr const char* kAnchor = "c2";

// The chapter's own first paragraph, present in every shape but the last.
std::string chapterBody() {
  return std::string("<p>") + kOpening +
         " of the second chapter follows its heading and runs long enough to wrap onto more than one line "
         "of the page.</p><p>A second paragraph, so the chapter is not one block.</p>";
}

// One shape, swept across every page position by the filler count. Reports the
// first position that fails so the failure names the geometry.
void expectLandsOnTheHeading(const char* shapeName, const std::string& (*shape)(), const int maxFiller = 14) {
  for (int n = 0; n <= maxFiller; n++) {
    const Laid laid = layout(filler(n) + shape(), {kAnchor});
    const int anchored = anchorPage(laid, kAnchor);
    const int headingPage = pageOf(laid.pages, kHeading);
    ASSERT_GE(anchored, 0) << shapeName << ": anchor not recorded (filler " << n << ")";
    ASSERT_GE(headingPage, 0) << shapeName << ": heading not laid out (filler " << n << ")";
    EXPECT_EQ(anchored, headingPage) << shapeName << ": the TOC pick opens page " << anchored
                                     << " but the heading is on page " << headingPage << " (filler " << n << ")";
    ASSERT_FALSE(laid.pages[headingPage].empty());
    EXPECT_TRUE(contains(laid.pages[headingPage].front(), kHeading))
        << shapeName << ": the heading is not the first line of its page (filler " << n << "); first line: '"
        << laid.pages[headingPage].front() << "'";
    // The chapter's first paragraph opens on the heading's page: a page that
    // holds nothing but the heading is a break in the wrong place.
    const int openingPage = pageOf(laid.pages, kOpening);
    if (openingPage >= 0) {
      EXPECT_EQ(openingPage, headingPage) << shapeName << ": the heading is alone on page " << headingPage
                                          << ", the text starts on page " << openingPage << " (filler " << n << ")";
    }
  }
}

const std::string& idOnTheHeading() {
  static const std::string s = std::string("<h2 id=\"c2\">") + kHeading + " heading</h2>" + chapterBody();
  return s;
}
const std::string& idOnAWrapperDiv() {
  static const std::string s =
      std::string("<div id=\"c2\" class=\"chapter\"><h2>") + kHeading + " heading</h2>" + chapterBody() + "</div>";
  return s;
}
const std::string& idOnASection() {
  static const std::string s =
      std::string("<section id=\"c2\"><h2>") + kHeading + " heading</h2>" + chapterBody() + "</section>";
  return s;
}
const std::string& idOnAnEmptyAnchorBefore() {
  static const std::string s = std::string("<a id=\"c2\"></a><h2>") + kHeading + " heading</h2>" + chapterBody();
  return s;
}
const std::string& idOnAnAnchorInside() {
  static const std::string s = std::string("<h2><a id=\"c2\"></a>") + kHeading + " heading</h2>" + chapterBody();
  return s;
}
const std::string& idOnTheFirstParagraph() {
  static const std::string s = std::string("<p id=\"c2\">") + kHeading +
                               " opens the chapter with no heading at all, and the paragraph runs on for a "
                               "second line of prose.</p>" +
                               chapterBody();
  return s;
}
const std::string& ruleThenHeading() {
  static const std::string s = std::string("<hr/><h2 id=\"c2\">") + kHeading + " heading</h2>" + chapterBody();
  return s;
}

}  // namespace

TEST(TocAnchorPage, IdOnTheHeading) { expectLandsOnTheHeading("id on <h2>", idOnTheHeading); }
TEST(TocAnchorPage, IdOnAWrapperDiv) { expectLandsOnTheHeading("id on wrapper <div>", idOnAWrapperDiv); }
TEST(TocAnchorPage, IdOnASection) { expectLandsOnTheHeading("id on <section>", idOnASection); }
TEST(TocAnchorPage, IdOnAnEmptyAnchorBeforeTheHeading) {
  expectLandsOnTheHeading("<a id> before <h2>", idOnAnEmptyAnchorBefore);
}
TEST(TocAnchorPage, IdOnAnAnchorInsideTheHeading) {
  expectLandsOnTheHeading("<h2><a id> (Gutenberg)", idOnAnAnchorInside);
}
TEST(TocAnchorPage, IdOnTheFirstParagraph) { expectLandsOnTheHeading("id on <p>", idOnTheFirstParagraph); }
TEST(TocAnchorPage, RuleThenHeading) { expectLandsOnTheHeading("<hr/> then <h2 id>", ruleThenHeading); }
