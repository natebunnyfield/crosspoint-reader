// Cascade (sense-lined) editions: the lines of one paragraph sit at the
// ordinary line pitch, the paragraph still ends with its half-line gap, and a
// cascade line is never justified.
//
// THE RULING (owner, 2026-09-26, after seeing renders): cascade formatting,
// built EPUB SIDE as separate editions of his generated books (claude-tools
// scripts/build_cascade.py, docs/cascade-editions.md there). Each phrase line
// is its own <p>:
//
//   <p class="cascade-line cascade-d1 cascade-cont cascade-join">phrase</p>
//
// MEASURED BEFORE THE FIX, on the real reader (simulator_x3, the AI book's
// Chapter 6 cascade edition): every phrase line sat at 1.5x line pitch,
// because makePages() adds half a line after EVERY block
// (CrossPointSettings::extraParagraphSpacing is constexpr 1), and CSS cannot
// take it away (a negative margin-bottom is clamped to 0). The gap that
// separates two paragraphs was then indistinguishable from the gap between two
// lines of one. The fix is two class tokens and nothing else:
//   cascade-join  -> no half-line gap after this block (BlockStyle::cascadeJoin)
//   cascade-line  -> ragged: the book's text-align is otherwise discarded
//                    (paragraphAlignment is constexpr JUSTIFIED)
//
// WHAT THIS SUITE PINS:
//   1. joined lines advance by exactly one line height        (the fix)
//   2. the paragraph's LAST line still gets the half-line gap  (paragraphs
//      stay visibly separate)
//   3. without the token the same markup is at 1.5x pitch      (the control:
//      proves it is the token, not the CSS, that does 1)
//   4. a cascade line that wraps is set Left, not Justify
//   5. a book that carries no token is untouched: plain <p>s still get the
//      half-line gap and are still justified
//   6. the depth classes indent by the stylesheet's em steps
//
// Case 1 and case 4 FAIL against the tree before the fix (checked by
// reverting ChapterHtmlSlimParser.cpp's two token reads).

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

// Same two stubs as definition_list, for the same reason: only the <img> path
// reaches them and no fixture here has one.
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

struct Line {
  std::string text;
  int x;
  int y;
  CssTextAlign alignment;
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

// The edition's own stylesheet (claude-tools scripts/build_cascade.py
// CASCADE_CSS) after the AI book's `p` rule, which is the cascade it rides on.
constexpr const char* kCss =
    "p { margin: 0 0 0.75em; }\n"
    ".cascade-line { text-indent: 0; text-align: left; }\n"
    ".cascade-cont { margin-top: 0; padding-top: 0; }\n"
    ".cascade-join { margin-bottom: 0; padding-bottom: 0; }\n"
    ".cascade-d1 { margin-left: 1em; }\n"
    ".cascade-d2 { margin-left: 2em; }\n"
    ".cascade-d3 { margin-left: 2em; padding-left: 1em; }\n";

class LoadedCss {
 public:
  explicit LoadedCss(const char* css) : parser_("/tmp/cp_cascade_css_cache_unused") {
    path_ = writeTemp("/tmp/cp_cascade_css_XXXXXX", css);
    if (path_.empty()) return;
    HalFile f;
    if (!Storage.openFileForRead("TEST", path_, f)) {
      ADD_FAILURE() << "could not reopen the stylesheet fixture";
      return;
    }
    if (!parser_.loadFromStream(f)) ADD_FAILURE() << "loadFromStream failed";
  }
  ~LoadedCss() {
    if (!path_.empty()) ::remove(path_.c_str());
  }
  const CssParser* get() const { return &parser_; }

 private:
  CssParser parser_;
  std::string path_;
};

std::vector<Line> layout(const std::string& bodyHtml) {
  static LoadedCss css(kCss);
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><head><title>t</title></head><body>" +
      bodyHtml + "</body></html>";
  const std::string path = writeTemp("/tmp/cp_cascade_XXXXXX", doc);
  if (path.empty()) return {};

  std::vector<Line> lines;
  ChapterHtmlSlimParser parser(
      nullptr, path, Gfx::instance().renderer(), LIBREFRANKLIN_READER_14_FONT_ID,
      /*smallFontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/true,
      /*paragraphAlignment=*/0, kViewportWidth, kViewportHeight, /*hyphenationEnabled=*/false,
      /*focusReadingEnabled=*/false, /*lineGridEnabled=*/false,
      /*justifyThresholdChars=*/autojustify::THRESHOLD_CHARS,
      [&lines](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
        for (const auto& element : page->elements) {
          const auto line = std::dynamic_pointer_cast<PageLine>(element);
          if (!line || !line->getBlock()) continue;
          const TextBlock& block = *line->getBlock();
          std::string text;
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!text.empty()) text += ' ';
            text += block.wordText(i);
          }
          lines.push_back({std::move(text), line->xPos, line->yPos, block.getBlockStyle().alignment});
        }
      },
      /*embeddedStyle=*/true, /*contentBase=*/"", /*imageBasePath=*/"",
      /*imageRendering=*/0, /*tocAnchors=*/{}, /*popupFn=*/nullptr, css.get());
  parser.parseAndBuildPages();
  ::remove(path.c_str());
  if (getenv("CROSSPOINT_DUMP_PAGES")) {
    for (const Line& l : lines) printf("  x=%3d y=%4d | %s\n", l.x, l.y, l.text.c_str());
  }
  return lines;
}

int lineHeight() {
  return static_cast<int>(Gfx::instance().renderer().getLineHeight(LIBREFRANKLIN_READER_14_FONT_ID, 1.0f));
}

// One cascaded paragraph (three phrase lines) followed by a second one.
// Single-token markers so each line is findable by one word.
std::string cascade(const bool withJoin) {
  const std::string join = withJoin ? " cascade-join" : "";
  return "<p class=\"cascade-line" + join + "\">Alpha begins here</p>"
         "<p class=\"cascade-line cascade-d1 cascade-cont" + join + "\">bravo steps in</p>"
         "<p class=\"cascade-line cascade-d2 cascade-cont\">charlie ends it.</p>"
         "<p class=\"cascade-line" + join + "\">Delta opens another</p>"
         "<p class=\"cascade-line cascade-d1 cascade-cont\">echo closes.</p>";
}

// --- 1 and 2 --------------------------------------------------------------

TEST(CascadeLines, JoinedLinesAdvanceByOneLineHeight) {
  const auto lines = layout(cascade(true));
  ASSERT_EQ(lines.size(), 5u);
  const int lh = lineHeight();
  EXPECT_EQ(lines[1].y - lines[0].y, lh) << "a joined line must not carry the half-line paragraph gap";
  EXPECT_EQ(lines[2].y - lines[1].y, lh);
  EXPECT_EQ(lines[4].y - lines[3].y, lh);
}

TEST(CascadeLines, TheParagraphsLastLineKeepsTheHalfLineGap) {
  const auto lines = layout(cascade(true));
  ASSERT_EQ(lines.size(), 5u);
  const int lh = lineHeight();
  EXPECT_EQ(lines[3].y - lines[2].y, lh + lh / 2) << "two cascaded paragraphs must stay visibly separate";
}

// --- 3. The control -------------------------------------------------------

TEST(CascadeLines, WithoutTheTokenTheSameMarkupIsAtOneAndAHalfPitch) {
  const auto lines = layout(cascade(false));
  ASSERT_EQ(lines.size(), 5u);
  const int lh = lineHeight();
  EXPECT_EQ(lines[1].y - lines[0].y, lh + lh / 2);
  EXPECT_EQ(lines[2].y - lines[1].y, lh + lh / 2);
}

// --- 4. Ragged ------------------------------------------------------------

TEST(CascadeLines, AWrappingCascadeLineIsSetLeftNotJustified) {
  // Long enough to wrap at 600 px: a phrase that overruns the measure at a
  // large font size must stay ragged.
  const auto lines = layout(
      "<p class=\"cascade-line cascade-join\">wrapone wraptwo wrapthree wrapfour wrapfive wrapsix wrapseven "
      "wrapeight wrapnine wrapten wrapeleven wraptwelve wrapthirteen wrapfourteen wrapfifteen</p>"
      "<p class=\"cascade-line cascade-cont\">last.</p>");
  ASSERT_GE(lines.size(), 3u) << "the fixture did not wrap; widen it";
  EXPECT_EQ(lines[0].alignment, CssTextAlign::Left);
}

// --- 5. Everything else is untouched ----------------------------------------

TEST(CascadeLines, APlainParagraphStillGetsItsGapAndIsStillJustified) {
  const auto lines = layout(
      "<p>plainone plaintwo plainthree plainfour plainfive plainsix plainseven plaineight plainnine "
      "plainten plaineleven plaintwelve plainthirteen plainfourteen plainfifteen</p>"
      "<p>next paragraph.</p>");
  ASSERT_GE(lines.size(), 3u);
  const int lh = lineHeight();
  EXPECT_EQ(lines[0].alignment, CssTextAlign::Justify);
  EXPECT_EQ(lines.back().y - lines[lines.size() - 2].y, lh + lh / 2);
}

// --- 6. Indents -----------------------------------------------------------

TEST(CascadeLines, DepthClassesIndentByTheStylesheetsEmSteps) {
  const auto lines = layout(cascade(true));
  ASSERT_EQ(lines.size(), 5u);
  const int step = lines[1].x - lines[0].x;
  EXPECT_GT(step, 0) << "cascade-d1 did not indent";
  EXPECT_EQ(lines[2].x - lines[0].x, 2 * step) << "cascade-d2 is two steps";
}

}  // namespace
