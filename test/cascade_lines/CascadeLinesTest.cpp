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
  bool rtl = false;
  bool anySup = false;
  int firstWordX = 0;  // the first word's x inside the block: where a first-line indent shows
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
    ".cascade-d3 { margin-left: 2em; padding-left: 1em; }\n"
    ".lead { margin-top: 1.5em; text-align: left; }\n"
    ".hang { text-indent: -1.5em; margin-left: 2em; margin-top: 1.5em; text-align: left; }\n";

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
    if (parser_.loadFromStream(f) != CssParser::ParseResult::Complete) ADD_FAILURE() << "loadFromStream failed";
  }
  ~LoadedCss() {
    if (!path_.empty()) ::remove(path_.c_str());
  }
  const CssParser* get() const { return &parser_; }

 private:
  CssParser parser_;
  std::string path_;
};

std::vector<Line> layout(const std::string& bodyHtml, const bool lineGrid = false) {
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
      /*focusReadingEnabled=*/false, /*lineGridEnabled=*/lineGrid,
      /*justifyThresholdChars=*/autojustify::THRESHOLD_CHARS,
      [&lines](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
        for (const auto& element : page->elements) {
          const auto line = dynamic_cast<const PageLine*>(element.get());
          if (!line || !line->getBlock()) continue;
          const TextBlock& block = *line->getBlock();
          std::string text;
          bool anySup = false;
          for (uint16_t i = 0; i < block.wordCount(); i++) {
            if (!text.empty()) text += ' ';
            text += block.wordText(i);
            if (block.wordStyle(i) & EpdFontFamily::SUP) anySup = true;
          }
          lines.push_back({std::move(text), line->xPos, line->yPos, block.getBlockStyle().alignment,
                           block.getBlockStyle().isRtl, anySup, block.wordXpos(0)});
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
  return "<p class=\"cascade-line" + join +
         "\">Alpha begins here</p>"
         "<p class=\"cascade-line cascade-d1 cascade-cont" +
         join +
         "\">bravo steps in</p>"
         "<p class=\"cascade-line cascade-d2 cascade-cont\">charlie ends it.</p>"
         "<p class=\"cascade-line" +
         join +
         "\">Delta opens another</p>"
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

// --- Paragraph base direction (upstream ce6c6cbce, section v72) -------------
// An inline element's direction must not replace its paragraph's: a Hebrew
// paragraph that OPENS with an English term (a common shape in technical
// books) stays RTL. Before the port, the empty block took the span's ltr as
// the paragraph direction, so the whole paragraph was set left-to-right.

TEST(ParagraphDirection, AnOpeningLtrSpanDoesNotFlipAnRtlParagraph) {
  const auto lines = layout(
      "<p dir=\"rtl\"><span dir=\"ltr\">CSS</span> \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
      "\xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D</p>");
  ASSERT_FALSE(lines.empty());
  for (const Line& l : lines) EXPECT_TRUE(l.rtl) << l.text;
}

TEST(ParagraphDirection, BodyDirStillSetsTheParagraphDirection) {
  const auto lines = layout("<div dir=\"rtl\"><span>plain</span> words</div>");
  ASSERT_FALSE(lines.empty());
  for (const Line& l : lines) EXPECT_TRUE(l.rtl) << l.text;
}

// Deliberately NOT asserted: "<p>Alpha <span dir=rtl>HEBREW</span> omega</p>"
// comes out RTL. That is ParsedText's paragraph auto-detection
// (ParsedText.cpp, RTL_PARAGRAPH_PROBE_WORDS = 3, from upstream f5bc554ae): an
// undirected paragraph whose first three words include one starting RTL is
// set RTL, so an unmarked Hebrew paragraph that opens with an English term
// reads correctly. Its price is an English paragraph with a Hebrew word among
// its first three. Recorded in docs/fork-sync.md (batch 4), not changed here.

// --- Footnote links keep vertical-align (upstream badfa95ff, section v73) ---
// The internal-link branch returns before the generic inline path, so it lost
// `vertical-align: super`, and footnote markers rendered at body size.

TEST(FootnoteLinkStyle, AnInternalLinksSuperscriptIsKept) {
  const auto lines = layout("<p>Body text<a href=\"#fn1\" style=\"vertical-align: super\">1</a> continues.</p>");
  ASSERT_FALSE(lines.empty());
  bool sawSup = false;
  for (const Line& l : lines) sawSup = sawSup || l.anySup;
  EXPECT_TRUE(sawSup);
}

TEST(FootnoteLinkStyle, APlainInternalLinkIsNotSuperscript) {
  const auto lines = layout("<p>See <a href=\"#ch2\">chapter two</a> next.</p>");
  ASSERT_FALSE(lines.empty());
  for (const Line& l : lines) EXPECT_FALSE(l.anySup) << l.text;
}

// --- The HTML hidden attribute (upstream 6eda8f0b7, section v75) ------------

TEST(HiddenAttribute, AHiddenElementLaysOutNothing) {
  const auto lines =
      layout("<p>Shown words</p><p hidden=\"hidden\">Secret words</p><div hidden=\"\">More secret</div>");
  ASSERT_FALSE(lines.empty());
  for (const Line& l : lines) {
    EXPECT_EQ(l.text.find("Secret"), std::string::npos) << l.text;
    EXPECT_EQ(l.text.find("secret"), std::string::npos) << l.text;
  }
}

// --- A skipped (display:none) block must not pop its parent's style ---------
// A display:none element pushes nothing when it opens, so its close must not
// pop the block style, list or inline state of the element around it.

int xOf(const std::vector<Line>& lines, const char* word) {
  for (const Line& l : lines)
    if (l.text.find(word) != std::string::npos) return l.x;
  return -1;
}

TEST(SkippedBlock, AHiddenParagraphKeepsItsContainersIndent) {
  const int control = xOf(layout("<div class=\"cascade-d1\"><p>after</p></div>"), "after");
  ASSERT_GE(control, 0);
  EXPECT_EQ(control, xOf(layout("<div class=\"cascade-d1\"><p hidden=\"\">x</p><p>after</p></div>"), "after"));
  EXPECT_EQ(control,
            xOf(layout("<div class=\"cascade-d1\"><p style=\"display:none\">x</p><p>after</p></div>"), "after"));
}

TEST(SkippedBlock, AHiddenSubtreeWithBlocksKeepsItsContainersIndent) {
  const int control = xOf(layout("<div class=\"cascade-d1\"><p>after</p></div>"), "after");
  ASSERT_GE(control, 0);
  EXPECT_EQ(control, xOf(layout("<div class=\"cascade-d1\"><div hidden=\"\"><p>a</p><p>b</p></div>"
                                "<p>after</p></div>"),
                         "after"));
}

TEST(SkippedBlock, AHiddenNestedListDoesNotEndItsParentList) {
  const auto lines =
      layout("<ol><li>one</li><li>two<ol style=\"display:none\"><li>x</li></ol></li><li>three</li></ol>");
  bool sawThree = false;
  for (const Line& l : lines)
    if (l.text.find("three") != std::string::npos) {
      sawThree = true;
      EXPECT_EQ(0u, l.text.find("3.")) << l.text;
    }
  EXPECT_TRUE(sawThree);
}

// --- list-style-type: none (upstream 2b2ba72f0, section v77) ---------------

std::string lineWith(const std::vector<Line>& lines, const char* word) {
  for (const Line& l : lines)
    if (l.text.find(word) != std::string::npos) return l.text;
  return {};
}

TEST(ListStyleNone, TheLonghandDropsTheMarker) {
  const auto lines = layout("<ul style=\"list-style-type: none\"><li>alpha item</li></ul>");
  EXPECT_EQ("alpha item", lineWith(lines, "alpha"));
}

TEST(ListStyleNone, TheShorthandDropsTheMarker) {
  const auto lines = layout("<ol style=\"list-style: none\"><li>beta item</li><li>gamma item</li></ol>");
  EXPECT_EQ("beta item", lineWith(lines, "beta"));
  EXPECT_EQ("gamma item", lineWith(lines, "gamma"));
}

TEST(ListStyleNone, AnOrdinaryListKeepsItsMarkers) {
  const auto lines =
      layout("<ol><li>delta item</li></ol><ul style=\"list-style: square inside\"><li>eps item</li></ul>");
  EXPECT_EQ(0u, lineWith(lines, "delta").find("1."));
  EXPECT_EQ(0u, lineWith(lines, "eps").find("\xe2\x80\xa2"));
}

// A hidden <rt>: the </rt> and </ruby> branches returned before the "leaving
// skip" reset, so skipUntilDepth stayed stale. Later text at that depth was
// dropped and the next close at that depth was read as skipped (adversarial
// review 2026-11-08, F1).
TEST(SkippedBlock, AHiddenRubyTextDoesNotSwallowWhatFollows) {
  for (const char* rt : {"<rt hidden=\"\">kan</rt>", "<rt style=\"display:none\">kan</rt>"}) {
    const std::string html =
        std::string("<p>alpha<ruby>X") + rt +
        "</ruby>beta</p><div class=\"cascade-d1\"><div><p>lostpara</p></div></div><p>afterpara</p>";
    const auto lines = layout(html);
    EXPECT_GE(xOf(lines, "lostpara"), 0) << rt;
    EXPECT_EQ(0, xOf(lines, "afterpara")) << rt;
  }
}

// --- paragraph continuity across soft flushes (upstream d5693c321) ------------
//
// A paragraph longer than the soft-flush threshold (320 words with embedded CSS) is laid out in
// two or more calls. Before the fix the tail came back as a NEW block: it took a second first-line
// indent and spent the block's top margin again, in the middle of the paragraph.
//
// Pinned with it, because the soft flush now goes through makePages(): the fork's inter-block gap
// cap (half a line), the Line Grid, and the widow/orphan hold-back.

namespace {

std::string longParagraph(const char* cls, const int words) {
  std::string text;
  for (int i = 0; i < words; ++i) {
    text += (i % 7 == 6) ? "tomorrow " : "word ";
  }
  return std::string("<p class=\"") + cls + "\">" + text + "end.</p>";
}

// y of each line's page: a page break is where y does not increase.
std::vector<int> pageSizes(const std::vector<Line>& lines) {
  std::vector<int> sizes;
  int count = 0;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0 && lines[i].y <= lines[i - 1].y) {
      sizes.push_back(count);
      count = 0;
    }
    ++count;
  }
  if (count > 0) sizes.push_back(count);
  return sizes;
}

}  // namespace

TEST(SoftFlush, TheIndentIsAppliedToTheFirstLineOnly) {
  // Positive text-indent is not honored while paragraphs are separated by a gap (the fork's
  // ParsedText::resolveFirstLineIndent); a negative one is, so this pins the hanging first line.
  const auto lines = layout(longParagraph("hang", 700));
  ASSERT_GT(lines.size(), 12u);
  const int bodyX = lines[1].firstWordX;
  EXPECT_LT(lines[0].firstWordX, bodyX) << "the opening line hangs";
  for (size_t i = 1; i < lines.size(); ++i) {
    EXPECT_EQ(lines[i].firstWordX, bodyX) << "line " << i << " must not repeat the first-line indent";
  }
}

TEST(SoftFlush, TheTopMarginIsSpentOnceBeforeTheFirstLineNotInTheMiddle) {
  const auto lines = layout(longParagraph("lead", 700));
  ASSERT_GT(lines.size(), 12u);
  const int lh = lineHeight();
  for (size_t i = 1; i < lines.size(); ++i) {
    if (lines[i].y <= lines[i - 1].y) continue;  // a new page restarts y
    EXPECT_EQ(lines[i].y - lines[i - 1].y, lh) << "line " << i << " carries extra space inside the paragraph";
  }
}

TEST(SoftFlush, TheGapBetweenTwoLongParagraphsIsStillCappedAtHalfALine) {
  // The first paragraph ends in the soft-flush tail, the second opens with a 1.5 em top margin and
  // the first closes with the stylesheet's 0.75 em bottom margin: summed they exceed a half line.
  const int lh = lineHeight();
  bool seenGap = false;
  // Several lengths, so the paragraph break lands mid-page for at least one of them.
  for (const int words : {330, 345, 360, 375, 390}) {
    const auto lines = layout(longParagraph("lead", words) + longParagraph("lead", words));
    for (size_t i = 1; i < lines.size(); ++i) {
      if (lines[i].y <= lines[i - 1].y) continue;
      const int step = lines[i].y - lines[i - 1].y;
      EXPECT_LE(step, lh + lh / 2) << words << " words, line " << i << ": the gap cap must hold";
      if (step == lh + lh / 2) seenGap = true;
    }
  }
  EXPECT_TRUE(seenGap) << "the paragraph break should still show the half-line gap";
}

TEST(SoftFlush, LineGridKeepsEveryBaselineOnTheGrid) {
  const auto lines = layout(longParagraph("lead", 400) + longParagraph("lead", 400), /*lineGrid=*/true);
  ASSERT_GT(lines.size(), 12u);
  const int lh = lineHeight();
  for (const Line& line : lines) {
    EXPECT_EQ(line.y % lh, 0) << "baseline off the line grid at y=" << line.y;
  }
}

TEST(SoftFlush, NoPageOfALongParagraphIsLeftWithASingleLine) {
  // Widow/orphan hold-back: with a paragraph spanning several pages, no page carries just one line
  // of it (the last line is held back with its predecessor).
  const auto lines = layout(longParagraph("lead", 700));
  const auto sizes = pageSizes(lines);
  ASSERT_GE(sizes.size(), 2u) << "the fixture must span pages";
  for (const int count : sizes) EXPECT_GE(count, 2);
}
