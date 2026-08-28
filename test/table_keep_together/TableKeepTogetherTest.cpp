// A table's header row, and the caption above it, must not be the last thing
// on a page.
//
// THE REPORT (owner, 2026-08-26, two consecutive screenshots of his own
// reading): "don't split up table header or caption from rest (when possible).
// intact is best". The first page ended with a section heading, an intro
// paragraph, the caption line "Effort-to-payoff, best first", the header row
// `Addition | Effort | What it buys`, the rule under it -- and then roughly half
// a page of nothing. The second page opened on the first body row. A header
// with no rows under it is worse than no header: the reader turns the page
// carrying three column names in their head.
//
// WHAT THIS SUITE IS FOR. It runs the REAL parser -- ChapterHtmlSlimParser over
// a real XHTML file, through the real CSS parser, the real layout engine and
// the built-in reader faces -- and reads the pages back. It is the first host
// suite that links that parser; B-037 closed on 2026-08-23 with the gap stated
// in its own entry ("no host test links ChapterHtmlSlimParser, so the emitter
// half of this fix is covered by render only"), and the emitter has now been
// changed twice.
//
// WHY IT SWEEPS rather than asserting one page. Where a table lands depends on
// how much prose precedes it, and a single fixture pins one alignment out of
// dozens -- the one the author happened to write. Every case below walks the
// preceding filler from 0 to kSweepFillers paragraphs, so the header arrives at
// every height on the page including the ones that strand it. Against the
// pre-2026-08-26 tree the sweep fails on the alignments that strand it and
// passes on the rest, which is exactly the shape of the reported bug.

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
#include "Epub/hyphenation/Hyphenator.h"
#include "Epub/parsers/ChapterHtmlSlimParser.h"
#include "Epub/parsers/TableColumnLayout.h"
#include "fontIds.h"

HalDisplay display;

// Images are the only thing the parser asks its Epub for, and no fixture here
// has one. Defined rather than linked: Epub.cpp drags in the zip reader, the
// metadata cache and the cover pipeline for a call that never happens.
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }

// Likewise the image DECODERS: linking them pulls in JPEGDEC and the PNG
// pipeline, both of which come from PlatformIO's lib_deps rather than the repo.
// "No format is supported" is the honest answer for a suite whose fixtures are
// text, and it is reached only from the <img> path.
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

// A page-shaped viewport. Not the device's to the pixel -- what matters here is
// that a page holds enough lines for a table to be stranded at the foot of one,
// and that the numbers stay fixed so a failure is about the keep and not about
// the geometry. Wide enough that the three-column plan gives each column its
// natural width: squeeze one and the cells wrap, and then the assertions below
// are hunting for half a word.
//
// `extraParagraphSpacing` is TRUE because that is
// CrossPointSettings' shipped default (=1), and it is not cosmetic here: with
// it off, ParsedText::resolveFirstLineIndent gives every block a three-space
// first-line indent -- table cells included -- and an 8-character header cell
// then no longer fits the column planned for it.
//
// Hyphenation is OFF for a related reason. Every assertion here is "which page
// is this cell on", and the greedy breaker splits `Addition` into
// `Addi-` / `tion` in a narrow column, which is a true rendering and a useless
// thing to search for.
constexpr uint16_t kViewportWidth = 600;
constexpr uint16_t kViewportHeight = 700;
constexpr int kSweepFillers = 14;

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
    // 14 pt is CrossPointSettings::DEFAULT_FONT_POINT_SIZE, the size a reader
    // who has changed nothing is looking at -- and the size the report came in
    // at. The columns path only plans at all for a table narrow enough for it,
    // so the fixtures below are three short columns.
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

// One laid-out page as the LINES on it, each line's words joined by a space.
// Lines rather than a flat word list because keep-2/2 is a statement about
// lines, and a flat list cannot count them.
using PageLines = std::vector<std::string>;

std::vector<PageLines> layout(const std::string& bodyHtml, const bool lineGrid = false) {
  char path[] = "/tmp/cp_table_keep_XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) {
    ADD_FAILURE() << "mkstemp failed";
    return {};
  }
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><head><title>t</title></head><body>" +
      bodyHtml + "</body></html>";
  const ssize_t wrote = ::write(fd, doc.data(), doc.size());
  ::close(fd);
  if (wrote != static_cast<ssize_t>(doc.size())) {
    ADD_FAILURE() << "short write of the fixture";
    ::remove(path);
    return {};
  }

  std::vector<PageLines> pages;
  const std::string filepath(path);
  ChapterHtmlSlimParser parser(
      nullptr, filepath, Gfx::instance().renderer(), LIBREFRANKLIN_READER_14_FONT_ID,
      /*smallFontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/true,
      /*paragraphAlignment=*/0, kViewportWidth, kViewportHeight, /*hyphenationEnabled=*/false,
      /*focusReadingEnabled=*/false, lineGrid,
      /*justifyThresholdChars=*/autojustify::THRESHOLD_CHARS,
      [&pages](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
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
        pages.push_back(std::move(lines));
      },
      /*embeddedStyle=*/false, /*contentBase=*/"", /*imageBasePath=*/"");
  parser.parseAndBuildPages();
  ::remove(path);
  // CROSSPOINT_DUMP_PAGES=1 prints what actually landed on each page. Every
  // assertion here is "which page is this word on", and when one fails the only
  // useful next question is what the pages hold.
  if (getenv("CROSSPOINT_DUMP_PAGES")) {
    for (size_t p = 0; p < pages.size(); p++) {
      printf("--- page %zu ---\n", p);
      for (const std::string& l : pages[p]) printf("  | %s\n", l.c_str());
    }
  }
  return pages;
}

bool lineHasWord(const std::string& line, const std::string& word) {
  size_t start = 0;
  while (start <= line.size()) {
    const size_t space = line.find(' ', start);
    const std::string token = line.substr(start, space == std::string::npos ? std::string::npos : space - start);
    if (token == word) return true;
    if (space == std::string::npos) break;
    start = space + 1;
  }
  return false;
}

// The RULES on each page: width and thickness, in document order. Separate
// from layout() because that helper keeps only PageLine elements and drops
// everything else, which is exactly what made the row separators invisible to
// every existing case here.
struct RuleGeom {
  int width;
  int thickness;
};

std::vector<std::vector<RuleGeom>> layoutRules(const std::string& bodyHtml) {
  char path[] = "/tmp/cp_table_rules_XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0) {
    ADD_FAILURE() << "mkstemp failed";
    return {};
  }
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><head><title>t</title></head><body>" +
      bodyHtml + "</body></html>";
  const ssize_t wrote = ::write(fd, doc.data(), doc.size());
  ::close(fd);
  if (wrote != static_cast<ssize_t>(doc.size())) {
    ADD_FAILURE() << "short write of the fixture";
    ::remove(path);
    return {};
  }

  std::vector<std::vector<RuleGeom>> pages;
  const std::string filepath(path);
  ChapterHtmlSlimParser parser(
      nullptr, filepath, Gfx::instance().renderer(), LIBREFRANKLIN_READER_14_FONT_ID,
      /*smallFontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/true,
      /*paragraphAlignment=*/0, kViewportWidth, kViewportHeight, /*hyphenationEnabled=*/false,
      /*focusReadingEnabled=*/false, /*lineGrid=*/false,
      /*justifyThresholdChars=*/autojustify::THRESHOLD_CHARS,
      [&pages](std::unique_ptr<Page> page, uint16_t, uint16_t, uint32_t) {
        std::vector<RuleGeom> rules;
        for (const auto& element : page->elements) {
          const auto rule = std::dynamic_pointer_cast<PageHorizontalRule>(element);
          if (!rule) continue;
          rules.push_back({rule->getWidth(), rule->getThickness()});
        }
        pages.push_back(std::move(rules));
      },
      /*embeddedStyle=*/false, /*contentBase=*/"", /*imageBasePath=*/"");
  parser.parseAndBuildPages();
  ::remove(path);
  return pages;
}

int totalRules(const std::vector<std::vector<RuleGeom>>& pages) {
  int n = 0;
  for (const auto& page : pages) n += static_cast<int>(page.size());
  return n;
}

// SIX columns, one past tablecolumns::kMaxColumns, so the column planner gives
// up and the table takes the FLATTENED path. That is the path the separators
// live on, and picking the trigger deterministically beats hoping a wide table
// happens not to fit.
std::string flatTable(const int dataRows, const int cols = 6) {
  std::string html = "<table><tr>";
  for (int c = 0; c < cols; c++) html += "<th>H" + std::to_string(c) + "</th>";
  html += "</tr>";
  for (int r = 0; r < dataRows; r++) {
    html += "<tr>";
    for (int c = 0; c < cols; c++)
      html += "<td>r" + std::to_string(r) + "c" + std::to_string(c) + "</td>";
    html += "</tr>";
  }
  return html + "</table>";
}

// Index of the first page carrying `word`, or -1.
int pageOf(const std::vector<PageLines>& pages, const std::string& word) {
  for (size_t p = 0; p < pages.size(); p++) {
    for (const std::string& line : pages[p]) {
      if (lineHasWord(line, word)) return static_cast<int>(p);
    }
  }
  return -1;
}

std::string filler(const int count) {
  std::string out;
  for (int i = 0; i < count; i++) {
    out += "<p>Filler line number " + std::to_string(i) + ", one line of prose.</p>";
  }
  return out;
}

// The reported table, shortened to what fits three narrow columns.
std::string reportedTable(const bool withCaption) {
  return std::string("<table>") + (withCaption ? "<caption>Effort-to-payoff, best first</caption>" : "") +
         "<thead><tr><th>Addition</th><th>Effort</th><th>Buys</th></tr></thead>"
         "<tbody>"
         "<tr><td>Beans</td><td>Rinse</td><td>Fiber</td></tr>"
         "<tr><td>Lentils</td><td>Simmer</td><td>Protein</td></tr>"
         "<tr><td>Kale</td><td>Chop</td><td>Iron</td></tr>"
         "<tr><td>Oats</td><td>Soak</td><td>Bulk</td></tr>"
         "</tbody></table>";
}

// --- The flattened table's row separators (owner 2026-08-27) --------------
//
// "for flat table view, put a line separator between records/rows."
//
// A flattened table is one text block per CELL, so without a separator the only
// thing marking where one record ends is the cell labels -- and a two-column
// table has none at all, because name-then-value already reads as a pair. Those
// ran together completely.

TEST(TableRowSeparator, OneAtEveryRowBoundaryAndNoneAtTheTableEnds) {
  // A separator sits at every boundary BETWEEN rows, which for a table with a
  // header means one under the header as well -- a rule under the head is
  // ordinary table typography and it is still a row boundary. So N data rows
  // under a header give N separators: head|r0, r0|r1, ... r(N-2)|r(N-1).
  EXPECT_EQ(totalRules(layoutRules(flatTable(1))), 1) << "the rule under the header";
  EXPECT_EQ(totalRules(layoutRules(flatTable(2))), 2);
  EXPECT_EQ(totalRules(layoutRules(flatTable(3))), 3);
  EXPECT_EQ(totalRules(layoutRules(flatTable(6))), 6);

  // Never before the first row or after the last: those boundaries divide the
  // table from the prose around it, which is a different decision and not the
  // one asked for. With no header there is no head rule either, so a headerless
  // table of N rows has N-1.
  std::string noHead = "<table><td>stray</td>";
  for (int r = 0; r < 4; r++) {
    noHead += "<tr>";
    for (int c = 0; c < 6; c++) noHead += "<td>r" + std::to_string(r) + "c" + std::to_string(c) + "</td>";
    noHead += "</tr>";
  }
  noHead += "</table>";
  EXPECT_EQ(totalRules(layoutRules(noHead)), 3) << "four records, three boundaries between them";
}

TEST(TableRowSeparator, IsFullMeasureAndHairlineNotAnHrSectionBreak) {
  const auto pages = layoutRules(flatTable(3));
  int seen = 0;
  for (const auto& page : pages) {
    for (const RuleGeom& r : page) {
      EXPECT_EQ(r.thickness, 1) << "a row separator is a hairline; the <hr> break is 2 px";
      EXPECT_EQ(r.width, kViewportWidth) << "a row separator runs the full measure; the <hr> break is a quarter";
      seen++;
    }
  }
  EXPECT_GT(seen, 0) << "no separators were emitted at all";
}

// The two-column case is the one that most needed this: TableCellLabel emits no
// prefix there, so the cells carry nothing at all to mark a record boundary.
TEST(TableRowSeparator, TwoColumnTablesGetThemToo) {
  // Force flattening with a cell outside any row, since two columns would
  // otherwise lay out happily as columns.
  std::string html = "<table><td>stray</td><tr><td>a</td><td>1</td></tr>";
  html += "<tr><td>b</td><td>2</td></tr><tr><td>c</td><td>3</td></tr></table>";
  EXPECT_GT(totalRules(layoutRules(html)), 0);
}

// The <hr> section break must be unchanged by all of this.
TEST(TableRowSeparator, AnHrInProseIsStillTheQuarterWidthSectionBreak) {
  const auto pages = layoutRules("<p>before</p><hr/><p>after</p>");
  ASSERT_EQ(totalRules(pages), 1);
  for (const auto& page : pages)
    for (const RuleGeom& r : page) {
      EXPECT_EQ(r.thickness, 2);
      EXPECT_LT(r.width, kViewportWidth) << "the section break must not have become full-measure";
    }
}

TEST(TableKeepTogether, HeaderTravelsWithItsFirstBodyRows) {
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + reportedTable(/*withCaption=*/false));
    const int header = pageOf(pages, "Addition");
    ASSERT_GE(header, 0) << "header row missing entirely, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Beans"), header) << "first body row left behind, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Lentils"), header) << "second body row left behind, fillers=" << n;
  }
}

TEST(TableKeepTogether, CaptionTravelsWithTheHeader) {
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + reportedTable(/*withCaption=*/true));
    const int header = pageOf(pages, "Addition");
    ASSERT_GE(header, 0) << "header row missing entirely, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Effort-to-payoff,"), header) << "caption stranded, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Beans"), header) << "first body row left behind, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Lentils"), header) << "second body row left behind, fillers=" << n;
  }
}

// "(when possible)" -- the owner's own qualifier. A table whose header plus one
// row cannot fit an empty page has nowhere to be kept, and the constraint must
// YIELD rather than blank the page it is on and strand itself again on the next.
// The cells here are long enough that each row is many lines tall.
TEST(TableKeepTogether, GivesUpRatherThanBlankingAPageWhenTheKeepCannotFit) {
  const std::string tall =
      "<table><caption>A caption on a table taller than the page</caption>"
      "<thead><tr><th>One</th><th>Two</th></tr></thead><tbody>";
  std::string body;
  for (int r = 0; r < 3; r++) {
    body += "<tr><td>";
    for (int i = 0; i < 60; i++) body += "word" + std::to_string(i) + " ";
    body += "</td><td>x</td></tr>";
  }
  for (int n = 0; n <= 6; n++) {
    const auto pages = layout(filler(n) + tall + body + "</tbody></table>");
    ASSERT_FALSE(pages.empty());
    // No page may be empty. That is the failure mode a keep with no yield
    // produces: break, still does not fit, break again.
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "blank page " << p << ", fillers=" << n;
    }
  }
}

// A page must never come back empty from any of the fixtures above either --
// the same check, run over the case the fix actually changes.
TEST(TableKeepTogether, NoBlankPages) {
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + reportedTable(/*withCaption=*/true));
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "blank page " << p << ", fillers=" << n;
    }
  }
}

// Line Grid ON. It is a shipped settings row and it changes the arithmetic the
// keep is measured in: every block and every row gap rounds up to the next
// line-height multiple, and the row loop performs TWO of those roundings per row
// where a naive model performs one. Modelling a row with a single snap
// under-measured it by a whole line-height, which is the direction that
// completes a page and then splits the group anyway. Found by adversarial
// review, 2026-08-26; invisible to every arm above, all of which run with the
// grid off because that is the default.
TEST(TableKeepTogether, HeaderTravelsWithItsRowsOnTheLineGridToo) {
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + reportedTable(/*withCaption=*/true), /*lineGrid=*/true);
    const int header = pageOf(pages, "Addition");
    ASSERT_GE(header, 0) << "header row missing entirely, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Effort-to-payoff,"), header) << "caption stranded, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Beans"), header) << "first body row left behind, fillers=" << n;
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "blank page " << p << ", fillers=" << n;
    }
  }
}

// Prose that is still UNLAID above the table and is NOT a caption. Closing a
// </p> lays the paragraph out immediately, so every fixture above leaves
// nothing pending but the <caption> itself; a <div> that never closes before
// the table does. That block is too long to carry, and the keep must decide
// against the cursor the paragraph LEAVES rather than the one it starts from.
// Deciding early breaks the page, and then the second pass breaks it again
// after the paragraph -- two breaks in a row, and the paragraph alone on a page
// of its own. Adversarial review, 2026-08-26.
//
// The bound is the honest statement of what a keep-with-next may cost: it may
// move the table onto a page of its own, and NOTHING MORE. The control is the
// same document with the header row demoted to an ordinary row, which turns the
// keep off entirely (it needs a header to keep).
TEST(TableKeepTogether, LooseProseAboveATableCostsAtMostOnePage) {
  std::string prose = "<div>";
  for (int i = 0; i < 55; i++) prose += "dvword" + std::to_string(i) + " ";

  for (int n = 0; n <= kSweepFillers; n++) {
    const std::string body = filler(n) + prose + reportedTable(/*withCaption=*/false) + "</div>";
    const auto pages = layout(body);
    std::string headerless = body;
    const size_t th = headerless.find("<thead>");
    headerless.replace(th, std::string("<thead>").size(), "<tbody>");
    headerless.replace(headerless.find("</thead>"), std::string("</thead>").size(), "</tbody>");
    const auto control = layout(headerless);

    const int header = pageOf(pages, "Addition");
    ASSERT_GE(header, 0) << "header row missing entirely, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Beans"), header) << "first body row left behind, fillers=" << n;
    EXPECT_LE(pages.size(), control.size() + 1)
        << "the keep spent more than one page, fillers=" << n;
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "blank page " << p << ", fillers=" << n;
    }
  }
}

// The middle rung of the ladder, asserted rather than assumed. These are the
// reported table's own cells, which are long enough that the header plus TWO
// rows cannot fit an empty page while the header plus one can -- so the keep
// must degrade to one row and take it, rather than give up or drag a second row
// it has no room for. It is also the rung the before/after render lands on.
//
// The cells have to stay long WITHOUT defeating tablecolumns::planColumns: a
// cell wide enough to break the plan sends the whole table to the key-block
// fallback, where there is no header row to keep and this test silently
// measures nothing. sawTheRung below is the guard against exactly that drift.
TEST(TableKeepTogether, DegradesToOneRowWhenTwoWillNotFit) {
  std::string tall =
      "<table><caption>Effort-to-payoff, best first</caption>"
      "<thead><tr><th>Addition</th><th>Effort</th><th>What it buys</th></tr></thead><tbody>"
      "<tr><td>Beans</td><td>Open, rinse</td><td>Fiber and protein in the same object; the single "
      "cheapest upgrade in the store. Into pasta, quesadillas, anything.</td></tr>"
      "<tr><td>Lentils</td><td>Microwave</td><td>Picked ripe, flash-frozen, nutritionally comparable "
      "to fresh, often better than fresh that trucked for a week.</td></tr>"
      "<tr><td>Kale</td><td>Open can</td><td>Seafood is the food family most people miss entirely. "
      "Canned salmon, sardines, tuna: protein plus omega-3s.</td></tr>"
      "</tbody></table>";

  bool sawTheRung = false;
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + tall);
    const int header = pageOf(pages, "Addition");
    ASSERT_GE(header, 0) << "header row missing entirely, fillers=" << n;
    EXPECT_EQ(pageOf(pages, "Beans"), header) << "first body row left behind, fillers=" << n;
    if (pageOf(pages, "Lentils") != header) sawTheRung = true;
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "blank page " << p << ", fillers=" << n;
    }
  }
  EXPECT_TRUE(sawTheRung) << "no alignment exercised the one-row rung -- the fixture has drifted, "
                             "most likely into the key-block fallback";
}

// Prose widows and orphans are a SEPARATE mechanism (keep-2/2 in
// flushPendingLines) and the table keep must not disturb it. The paragraph here
// is built from words that appear nowhere else, so its lines can be told apart
// from the filler around them and COUNTED -- which is the only way to state the
// rule, since it is about how many lines land on each side of a break.
TEST(TableKeepTogether, ProseWidowOrphanControlStillHolds) {
  std::string para = "<p>";
  for (int i = 0; i < 40; i++) para += "pxword" + std::to_string(i) + " ";
  para += "</p>";

  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + para + filler(3));
    std::vector<int> linesPerPage;
    int total = 0;
    for (const PageLines& page : pages) {
      int count = 0;
      for (const std::string& line : page) {
        if (line.rfind("pxword", 0) == 0) count++;
      }
      linesPerPage.push_back(count);
      total += count;
    }
    ASSERT_GT(total, 3) << "the fixture paragraph must be 4+ lines, fillers=" << n;
    for (size_t p = 0; p < linesPerPage.size(); p++) {
      // keep-2/2: a page may hold none of the paragraph, all of it, or at least
      // two of its lines. Exactly one is an orphan or a widow.
      EXPECT_NE(linesPerPage[p], 1) << "a single line of the paragraph alone on page " << p
                                    << ", fillers=" << n;
    }
  }
}

}  // namespace
