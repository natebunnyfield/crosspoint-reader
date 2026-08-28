// A definition list must read as one: term, then what defines it, indented
// under it.
//
// THE REPORT (owner, 2026-08-28, a screenshot of a quiz book): a question ran
// straight into its own answer and then into the note under that, all on one
// line --
//
//   "It was the best of times, it was the worst of times" opens which
//   novel?**A Tale of Two Cities (Dickens, 1859)**The two cities are London and
//   Paris during the French Revolution...
//
// That is a <dl>: the question is a <dt>, the answer and the note are two
// <dd>s. `dl`, `dt` and `dd` appeared NOWHERE in lib/Epub/ -- the parser had
// never heard of them, so all three fell through to the INLINE branch at the
// foot of startElement and no block ever opened. Note what WAS working, and is
// why the screenshot shows the answer in bold: that inline branch reads
// font-weight off the resolved CSS, so the book's `dt { font-weight: bold }`
// was honored the whole time. Only the BLOCK half was missing.
//
// WHAT THIS SUITE PINS, in the order the cases appear:
//   1. the term and its definition are never concatenated  (the report)
//   2. the definition is indented relative to the term     (the relationship)
//   3. every <dd> under one <dt> gets its own block        (this book: 2 each)
//   4. publisher CSS wins over the built-in step           (the regression
//      that hardcoding an indent would be)
//   5. a <dl> does not disturb the prose around it
//   6. a <dt> is not left stranded at the foot of a page   (keep-with-next)
//
// Case 4 is the one that needs a real CssParser over a real stylesheet, which
// is why this is a suite of its own rather than cases bolted onto
// table_keep_together: nothing there loads CSS, and the whole point of the fix
// is that the book's own `dd { margin-left: 1em }` decides the indent.

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

// Images are the only thing the parser asks its Epub for, and no fixture here
// has one. Defined rather than linked: Epub.cpp drags in the zip reader, the
// metadata cache and the cover pipeline for a call that never happens.
bool Epub::readItemContentsToStream(const std::string&, Print&, size_t, bool) const { return false; }

// Likewise the image DECODERS: linking them pulls in JPEGDEC and the PNG
// pipeline, both of which come from PlatformIO's lib_deps rather than the repo.
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

// A page-shaped viewport, same reasoning as table_keep_together's: not the
// device's to the pixel, but fixed, and tall enough that a <dt> can be
// stranded at the foot of one. `extraParagraphSpacing` is TRUE below because
// that is CrossPointSettings' shipped default.
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
    // 14 pt is CrossPointSettings::DEFAULT_FONT_POINT_SIZE, the size a reader
    // who has changed nothing is looking at.
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

// One laid-out line: its text, and the x it was placed at. The x is what
// carries the indent -- placeLineOnPage writes the block's leftInset() into
// PageLine::xPos -- and a test that reads only the words cannot tell a
// definition from its term.
struct Line {
  std::string text;
  int x;
};
using PageLines = std::vector<Line>;

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

std::vector<PageLines> layout(const std::string& bodyHtml, const CssParser* css = nullptr,
                              const bool lineGrid = false) {
  const std::string doc =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<html xmlns=\"http://www.w3.org/1999/xhtml\" lang=\"en\"><head><title>t</title></head><body>" +
      bodyHtml + "</body></html>";
  const std::string path = writeTemp("/tmp/cp_deflist_XXXXXX", doc);
  if (path.empty()) return {};

  std::vector<PageLines> pages;
  ChapterHtmlSlimParser parser(
      nullptr, path, Gfx::instance().renderer(), LIBREFRANKLIN_READER_14_FONT_ID,
      /*smallFontId=*/0, /*lineCompression=*/1.0f, /*extraParagraphSpacing=*/true,
      /*paragraphAlignment=*/0, kViewportWidth, kViewportHeight, /*hyphenationEnabled=*/false,
      /*focusReadingEnabled=*/false, /*lineGridEnabled=*/lineGrid,
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
          lines.push_back({std::move(text), line->xPos});
        }
        pages.push_back(std::move(lines));
      },
      /*embeddedStyle=*/css != nullptr, /*contentBase=*/"", /*imageBasePath=*/"",
      /*imageRendering=*/0, /*tocAnchors=*/{}, /*popupFn=*/nullptr, css);
  parser.parseAndBuildPages();
  ::remove(path.c_str());
  // CROSSPOINT_DUMP_PAGES=1 prints what actually landed on each page, with the
  // x of every line. When one of these fails the only useful next question is
  // what the pages hold.
  if (getenv("CROSSPOINT_DUMP_PAGES")) {
    for (size_t p = 0; p < pages.size(); p++) {
      printf("--- page %zu ---\n", p);
      for (const Line& l : pages[p]) printf("  x=%3d | %s\n", l.x, l.text.c_str());
    }
  }
  return pages;
}

// Every line of every page, in document order. Most cases here are about
// adjacency rather than about which page a thing landed on.
std::vector<Line> allLines(const std::vector<PageLines>& pages) {
  std::vector<Line> out;
  for (const auto& page : pages) out.insert(out.end(), page.begin(), page.end());
  return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// The first line whose text contains `needle`, or nullptr.
const Line* find(const std::vector<Line>& lines, const std::string& needle) {
  for (const Line& l : lines) {
    if (contains(l.text, needle)) return &l;
  }
  return nullptr;
}

int indexOf(const std::vector<Line>& lines, const std::string& needle) {
  for (size_t i = 0; i < lines.size(); i++) {
    if (contains(lines[i].text, needle)) return static_cast<int>(i);
  }
  return -1;
}

int pageOf(const std::vector<PageLines>& pages, const std::string& needle) {
  for (size_t p = 0; p < pages.size(); p++) {
    for (const Line& l : pages[p]) {
      if (contains(l.text, needle)) return static_cast<int>(p);
    }
  }
  return -1;
}

// The reported book's own shape: one <dt> and exactly TWO <dd>s, the second
// carrying class="why". Measured from the epub the owner sent -- 18 <dl>, 255
// <dt>, 510 <dd>, every entry question / answer / explanation.
constexpr const char* kQuestion = "novelquestion";
constexpr const char* kAnswer = "citiesanswer";
constexpr const char* kWhy = "revolutionnote";

// Single-token words, deliberately: the assertions are "is this token on the
// same LINE as that one", and a token the line breaker can split across lines
// makes a true rendering and a useless thing to search for. Same reason
// table_keep_together turns hyphenation off.
std::string reportedEntry() {
  return std::string("<dl><dt>Which ") + kQuestion +
         "</dt>"
         "<dd>The " +
         kAnswer +
         "</dd>"
         "<dd class=\"why\">A " +
         kWhy + "</dd></dl>";
}

std::string filler(const int count) {
  std::string out;
  for (int i = 0; i < count; i++) {
    out += "<p>Filler line number " + std::to_string(i) + ", one line of prose.</p>";
  }
  return out;
}

// The reported book's stylesheet, verbatim from OEBPS/style.css. Everything
// this fix relies on is in the `dl` / `dt` / `dd` / `dd.why` rules; the rest is
// carried so the fixture is the book's real cascade and not a trimmed one.
constexpr const char* kBookCss =
    "body { font-family: Georgia, serif; line-height: 1.55; margin: 1em; }\n"
    "h1 { font-size: 1.6em; margin: 1.2em 0 0.3em; }\n"
    "h2 { font-size: 1.25em; margin: 1.4em 0 0.4em; }\n"
    "p.lede { color: #555; font-style: italic; }\n"
    "dl { margin: 0.8em 0; }\n"
    "dt { font-weight: bold; margin-top: 0.9em; }\n"
    "dd { margin: 0.25em 0 0.25em 1em; }\n"
    "dd.why { color: #555; font-size: 0.92em; border-left: 2px solid #ccc;\n"
    "         padding-left: 0.6em; margin-top: 0.35em; }\n"
    "table { border-collapse: collapse; font-size: 0.9em; }\n"
    "td, th { text-align: left; padding: 0.15em 0.8em 0.15em 0; }\n";

// A CssParser holding `css`, loaded the way Section.cpp loads a book's
// stylesheet: from a file, through HalStorage. Its cache path points at a temp
// name that nothing ever writes -- saveToCache/loadFromCache are not part of
// what is under test here.
class LoadedCss {
 public:
  explicit LoadedCss(const char* css) : parser_("/tmp/cp_deflist_css_cache_unused") {
    path_ = writeTemp("/tmp/cp_deflist_css_XXXXXX", css);
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

// --- 1. The report --------------------------------------------------------

TEST(DefinitionList, TermAndDefinitionAreNeverConcatenated) {
  const auto lines = allLines(layout(reportedEntry()));
  ASSERT_FALSE(lines.empty());

  const Line* term = find(lines, kQuestion);
  ASSERT_NE(term, nullptr) << "the term never made it onto a page";
  EXPECT_FALSE(contains(term->text, kAnswer))
      << "the <dd> ran into its <dt> on one line -- the reported bug: " << term->text;
  EXPECT_FALSE(contains(term->text, kWhy)) << term->text;

  const Line* answer = find(lines, kAnswer);
  ASSERT_NE(answer, nullptr);
  EXPECT_FALSE(contains(answer->text, kWhy)) << "the second <dd> ran into the first: " << answer->text;
}

TEST(DefinitionList, TermThenDefinitionsInDocumentOrder) {
  const auto lines = allLines(layout(reportedEntry()));
  const int term = indexOf(lines, kQuestion);
  const int answer = indexOf(lines, kAnswer);
  const int why = indexOf(lines, kWhy);
  ASSERT_GE(term, 0);
  ASSERT_GE(answer, 0);
  ASSERT_GE(why, 0);
  EXPECT_LT(term, answer);
  EXPECT_LT(answer, why);
}

// --- 2. The relationship --------------------------------------------------

TEST(DefinitionList, DefinitionIsIndentedRelativeToItsTerm) {
  // No stylesheet: this is the built-in step, the one that applies to a book
  // that ships a <dl> and styles nothing.
  const auto lines = allLines(layout(reportedEntry()));
  const Line* term = find(lines, kQuestion);
  const Line* answer = find(lines, kAnswer);
  ASSERT_NE(term, nullptr);
  ASSERT_NE(answer, nullptr);
  EXPECT_GT(answer->x, term->x) << "a definition list has no marker; the indent IS the relationship";
}

TEST(DefinitionList, TheTermItselfIsNotIndented) {
  // The term sits at the measure's left edge, where a paragraph does -- if the
  // <dl> indented its children the term would move with them and the
  // distinction the indent exists for would be gone.
  //
  // This one passes against the PRE-FIX tree too, and vacuously: nothing was
  // indented then, so every x matched. It carries weight only as the other
  // half of DefinitionIsIndentedRelativeToItsTerm -- that one says the
  // definition moved, this one says the term did not. Neither is sufficient
  // alone.
  const auto pages = layout("<p>Plain prose paragraph before.</p>" + reportedEntry());
  const auto lines = allLines(pages);
  const Line* prose = find(lines, "Plain");
  const Line* term = find(lines, kQuestion);
  ASSERT_NE(prose, nullptr);
  ASSERT_NE(term, nullptr);
  EXPECT_EQ(term->x, prose->x);
}

// --- 3. Two definitions per term, which is every entry in the reported book -

TEST(DefinitionList, EachDefinitionUnderOneTermIsItsOwnBlock) {
  const auto lines = allLines(layout(reportedEntry()));
  const Line* answer = find(lines, kAnswer);
  const Line* why = find(lines, kWhy);
  ASSERT_NE(answer, nullptr);
  ASSERT_NE(why, nullptr);
  // Separate lines is the first half; the second is that BOTH are indented,
  // because a second <dd> is a sibling of the first and not a continuation of
  // the term.
  const Line* term = find(lines, kQuestion);
  ASSERT_NE(term, nullptr);
  EXPECT_GT(why->x, term->x);
  EXPECT_EQ(why->x, answer->x) << "with no stylesheet both <dd>s take the same step";
}

TEST(DefinitionList, ThreeTermsGiveNineDistinctLines) {
  // The whole shape of the reported book: entry after entry, nothing merging.
  std::string body = "<dl>";
  for (int i = 0; i < 3; i++) {
    const std::string n = std::to_string(i);
    body += "<dt>term" + n + "</dt><dd>answer" + n + "</dd><dd class=\"why\">note" + n + "</dd>";
  }
  body += "</dl>";
  const auto lines = allLines(layout(body));
  for (int i = 0; i < 3; i++) {
    const std::string n = std::to_string(i);
    const Line* term = find(lines, "term" + n);
    ASSERT_NE(term, nullptr) << "term" << n;
    EXPECT_FALSE(contains(term->text, "answer" + n)) << term->text;
    const Line* answer = find(lines, "answer" + n);
    ASSERT_NE(answer, nullptr) << "answer" << n;
    EXPECT_FALSE(contains(answer->text, "note" + n)) << answer->text;
    // ...and the next entry's term does not run into this entry's note.
    const Line* note = find(lines, "note" + n);
    ASSERT_NE(note, nullptr) << "note" << n;
    EXPECT_FALSE(contains(note->text, "term" + std::to_string(i + 1))) << note->text;
  }
}

// --- 4. Publisher CSS wins ------------------------------------------------

TEST(DefinitionList, PublisherMarginLeftDecidesTheIndentNotTheBuiltInStep) {
  // The reported book states `dd { margin: 0.25em 0 0.25em 1em }`. 1 em is
  // SMALLER than the built-in 1.5 em step, so a fix that hardcoded the step
  // would show up here as an indent wider than the book asked for -- and would
  // misrender every book whose <dd> margin differs.
  const LoadedCss css(kBookCss);
  const auto styled = allLines(layout(reportedEntry(), css.get()));
  const auto bare = allLines(layout(reportedEntry()));

  const Line* styledTerm = find(styled, kQuestion);
  const Line* styledAnswer = find(styled, kAnswer);
  const Line* bareTerm = find(bare, kQuestion);
  const Line* bareAnswer = find(bare, kAnswer);
  ASSERT_NE(styledTerm, nullptr);
  ASSERT_NE(styledAnswer, nullptr);
  ASSERT_NE(bareTerm, nullptr);
  ASSERT_NE(bareAnswer, nullptr);

  const int styledIndent = styledAnswer->x - styledTerm->x;
  const int bareIndent = bareAnswer->x - bareTerm->x;
  EXPECT_GT(styledIndent, 0) << "the book's own 1em must still indent the definition";
  EXPECT_LT(styledIndent, bareIndent) << "the book asked for 1em and the built-in step is 1.5em; "
                                         "a hardcoded step would make these equal";
}

TEST(DefinitionList, PublisherPaddingLeftAlsoSuppressesTheBuiltInStep) {
  // `dd.why` states its offset as padding-left, beside a border-left this
  // panel cannot draw. Padding is a left inset like any other, so the built-in
  // step must stand down for it too -- otherwise the note is indented twice
  // and reads as a nesting level the document does not have.
  const LoadedCss css("dd { margin: 0; }\ndd.why { padding-left: 0.6em; }\n");
  const auto lines = allLines(layout(reportedEntry(), css.get()));
  const Line* term = find(lines, kQuestion);
  const Line* answer = find(lines, kAnswer);
  const Line* why = find(lines, kWhy);
  ASSERT_NE(term, nullptr);
  ASSERT_NE(answer, nullptr);
  ASSERT_NE(why, nullptr);
  // `dd { margin: 0 }` is an explicit zero and wins outright: fidelity, the
  // same rule <ul> has always followed.
  EXPECT_EQ(answer->x, term->x) << "an explicit `dd { margin: 0 }` must not be overridden";
  // The .why note takes its own 0.6em padding and nothing else.
  EXPECT_GT(why->x, answer->x);
  const auto bareLines = allLines(layout(reportedEntry()));
  const Line* bareAnswer = find(bareLines, kAnswer);
  ASSERT_NE(bareAnswer, nullptr) << "the unstyled control lost its definition";
  const int builtInStep = bareAnswer->x - term->x;
  EXPECT_LT(why->x - answer->x, builtInStep) << "0.6em, not the 1.5em built-in step";
}

// --- 5. The prose around it -----------------------------------------------

TEST(DefinitionList, ADefinitionListDoesNotDisturbTheProseAroundIt) {
  // A <dl> between two paragraphs must leave both where a <dl>-free document
  // would: same x, same order, nothing merged into the list and nothing of the
  // list merged into them. The inter-block gap is capped at half a line by
  // makePages, so the vertical rhythm is the paragraphs' own either way.
  const auto pages = layout("<p>Before the list here.</p>" + reportedEntry() + "<p>After the list here.</p>");
  const auto lines = allLines(pages);

  const Line* before = find(lines, "Before");
  const Line* after = find(lines, "After");
  ASSERT_NE(before, nullptr);
  ASSERT_NE(after, nullptr);
  EXPECT_EQ(before->x, after->x) << "the list must not leave an indent behind it";
  EXPECT_FALSE(contains(before->text, kQuestion)) << before->text;
  EXPECT_FALSE(contains(after->text, kWhy)) << after->text;

  const auto plain = allLines(layout("<p>Before the list here.</p><p>After the list here.</p>"));
  const Line* plainBefore = find(plain, "Before");
  ASSERT_NE(plainBefore, nullptr);
  EXPECT_EQ(before->x, plainBefore->x);

  EXPECT_LT(indexOf(lines, "Before"), indexOf(lines, kQuestion));
  EXPECT_LT(indexOf(lines, kWhy), indexOf(lines, "After"));
}

TEST(DefinitionList, ANestedDefinitionListStepsInAndBackOut) {
  // A <dl> inside a <dd> accumulates one more step, and the outer list's own
  // following <dd> returns to its level -- the block style stack popping
  // correctly, which is what adding these three tags to BLOCK_TAGS buys.
  const auto lines =
      allLines(layout("<dl><dt>outerterm</dt>"
                      "<dd>outerbody<dl><dt>innerterm</dt><dd>innerbody</dd></dl></dd>"
                      "<dd>outertail</dd></dl>"));
  const Line* outerTerm = find(lines, "outerterm");
  const Line* outerBody = find(lines, "outerbody");
  const Line* innerTerm = find(lines, "innerterm");
  const Line* innerBody = find(lines, "innerbody");
  const Line* outerTail = find(lines, "outertail");
  ASSERT_NE(outerTerm, nullptr);
  ASSERT_NE(outerBody, nullptr);
  ASSERT_NE(innerTerm, nullptr);
  ASSERT_NE(innerBody, nullptr);
  ASSERT_NE(outerTail, nullptr);
  EXPECT_GT(outerBody->x, outerTerm->x);
  EXPECT_EQ(innerTerm->x, outerBody->x) << "the inner term sits at its containing <dd>'s edge";
  EXPECT_GT(innerBody->x, innerTerm->x);
  EXPECT_EQ(outerTail->x, outerBody->x) << "the outer list's next <dd> returned to its own level";
}

// --- 6. Keep-with-next ----------------------------------------------------
//
// DECIDED: a <dt> keeps with ONE line of what follows, not with its whole
// <dd>. The parser is streaming -- when the term is laid out its definition
// has not been read, so a whole-group keep would need the buffering the table
// path has, and a <dl> is not bounded the way a table is. One line is the
// classical typesetter's rule and it is what removes the reported defect: the
// reader never turns a page carrying a term with nothing under it.
//
// It sweeps, for the same reason table_keep_together sweeps: where the term
// lands depends on how much prose precedes it, and a single fixture pins one
// alignment out of dozens.

// A definition of a REAL length. The fixture above gives every <dd> a single
// line, and that is the one length at which the keep cannot fail -- so the
// first version of these cases swept 23 page alignments against the only shape
// that could not catch the bug they exist for. Found by adversarial review,
// 2026-08-28.
//
// Why length is the whole story: this layout engine runs widow/orphan keep-2/2
// on the <dd> AFTER the term's keep has already decided. A 1-2 line paragraph
// is exempt from both widow rules (flushPendingLines' `totalLines <= 2` early
// exit), so one line of room is enough and nothing strands. A 3-line paragraph
// is all-or-nothing, and a 4+ line one needs two lines before its first can
// land. Measured strand rate over 41 page alignments, by the room the term
// keep demands:
//
//   room     1-line <dd>   3-line <dd>   4+ line <dd>
//   none        3/41          7/41          4/41
//   1 line      0/41          4/41          1/41     <- the shipped-first rule
//   2 lines     0/41          3/41          0/41
//   3 lines     0/41          0/41          0/41     <- kKeepRoomLines
//
// Cost of the third row, same sweep: on a 1-line definition the keep spends 3
// extra pages per 41 alignments; on a definition of 16 words or more the page
// count is IDENTICAL to no keep at all. Every definition in the reported book
// is three lines or more at the device measure, so it pays nothing.
std::string entryWithDefinitionOf(const int ddWords) {
  std::string dd = kAnswer;
  for (int i = 0; i < ddWords; i++) dd += " filler" + std::to_string(i);
  return std::string("<dl><dt>Which ") + kQuestion + "</dt><dd>" + dd + "</dd></dl>";
}

TEST(DefinitionListKeep, ATermIsNeverStrandedByADefinitionOfAnyLength) {
  // 12 and 14 words are the three-line case at this measure -- the one the
  // one-line rule got wrong on 4 of 41 alignments, and the length every real
  // answer in the reported book has.
  for (const int ddWords : {0, 6, 12, 14, 20, 30, 44}) {
    for (int n = 0; n <= 40; n++) {
      const auto pages = layout(filler(n) + entryWithDefinitionOf(ddWords));
      const int termPage = pageOf(pages, kQuestion);
      const int answerPage = pageOf(pages, kAnswer);
      ASSERT_GE(termPage, 0) << "ddWords=" << ddWords << " fillers=" << n;
      ASSERT_GE(answerPage, 0) << "ddWords=" << ddWords << " fillers=" << n;
      EXPECT_EQ(termPage, answerPage) << "ddWords=" << ddWords << " fillers=" << n
                                      << ": the term was stranded at the foot of page " << termPage
                                      << " and its definition opened page " << answerPage;
      for (size_t pg = 0; pg < pages.size(); pg++) {
        EXPECT_FALSE(pages[pg].empty()) << "ddWords=" << ddWords << " fillers=" << n << ", page " << pg << " is blank";
      }
    }
  }
}

TEST(DefinitionListKeep, TheSameWithLineGridOn) {
  // The grid arm of the case above. Every height in breakBeforeStrandedTerm
  // passes through a snap that is a no-op with the grid off, and the probe
  // accumulates its advances RELATIVELY where placeLineOnPage snaps the
  // ABSOLUTE cursor -- that the two agree is an argument until this runs it.
  // The equivalent arm in table_keep_together exists because collapsing two
  // snaps into one under-measured a row by a whole line-height and nothing
  // else showed it.
  for (const int ddWords : {0, 12, 30}) {
    for (int n = 0; n <= 40; n++) {
      const auto pages = layout(filler(n) + entryWithDefinitionOf(ddWords), /*css=*/nullptr, /*lineGrid=*/true);
      const int termPage = pageOf(pages, kQuestion);
      const int answerPage = pageOf(pages, kAnswer);
      ASSERT_GE(termPage, 0) << "ddWords=" << ddWords << " fillers=" << n;
      ASSERT_GE(answerPage, 0) << "ddWords=" << ddWords << " fillers=" << n;
      EXPECT_EQ(termPage, answerPage) << "ddWords=" << ddWords << " fillers=" << n << " (line grid)";
      for (size_t pg = 0; pg < pages.size(); pg++) {
        EXPECT_FALSE(pages[pg].empty()) << "ddWords=" << ddWords << " fillers=" << n << " (line grid), page " << pg;
      }
    }
  }
}

TEST(DefinitionListKeep, ATermIsNeverTheLastThingOnAPage) {
  constexpr int kSweepFillers = 22;
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + reportedEntry());
    const int termPage = pageOf(pages, kQuestion);
    const int answerPage = pageOf(pages, kAnswer);
    ASSERT_GE(termPage, 0) << "fillers=" << n;
    ASSERT_GE(answerPage, 0) << "fillers=" << n;
    EXPECT_EQ(termPage, answerPage) << "fillers=" << n << ": the term was stranded at the foot of page " << termPage
                                    << " and its definition opened page " << answerPage;
  }
}

TEST(DefinitionListKeep, AKeptTermDoesNotOpenABlankPage) {
  // The keep must never publish an empty page: it refuses on an empty page,
  // where there is nothing above the term to strand it against.
  constexpr int kSweepFillers = 22;
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + reportedEntry());
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "fillers=" << n << ", page " << p << " came out blank";
    }
  }
}

TEST(DefinitionListKeep, TheKeepDoesNotApplyToOrdinaryParagraphs) {
  // The flag is consumed by every block, term or not. If it leaked, a plain
  // paragraph after a <dl> would start being kept with the next one too --
  // which would change pagination for books that have no <dl> in them at all.
  // Same prose, once after a definition list and once alone: the paragraph
  // count per page must be identical.
  const auto withList = layout(reportedEntry() + filler(40));
  const auto alone = layout(filler(40));
  ASSERT_GT(alone.size(), 2u);
  // Page 0 is short in BOTH runs -- it carries the chapter sinkage, the
  // designed drop a section's first page opens with. Take the full-page line
  // count from a page that does not, and sanity-check that the stand-alone
  // run's own interior pages agree on it.
  const size_t fullPageLines = alone[1].size();
  for (size_t p = 1; p + 1 < alone.size(); p++) {
    ASSERT_EQ(alone[p].size(), fullPageLines) << "the stand-alone prose run is not uniform at page " << p;
  }
  const int lastListPage = pageOf(withList, kWhy);
  ASSERT_GE(lastListPage, 0);
  ASSERT_LT(static_cast<size_t>(lastListPage) + 1, withList.size());
  for (size_t p = static_cast<size_t>(lastListPage) + 1; p + 1 < withList.size(); p++) {
    EXPECT_EQ(withList[p].size(), fullPageLines) << "page " << p << " after the list is not a full page of prose";
  }
}

// THE ONE CASE THE KEEP CANNOT SEE, pinned so it is a known cost rather than a
// surprise. A <dt> that nothing follows still gets kept -- the parser is
// streaming and the term is laid out at </dt>, before anything after it has
// been read, so "is there a definition coming" is not a question anyone can
// answer there. When such a term lands at a page foot it opens the next page
// instead, spending a short page on it.
//
// Accepted, on two grounds. It only fires on INVALID markup: a <dl>'s content
// model is one-or-more <dt> followed by one-or-more <dd>, so a list whose last
// child is a term is malformed. And the reported book has none -- 255 terms,
// 510 definitions, exactly two per term. What is asserted here is the bound:
// nothing is lost and no blank page is ever published.
TEST(DefinitionListKeep, ATrailingTermIsKeptAnywayAndCostsAtMostAShortPage) {
  constexpr int kSweepFillers = 22;
  for (int n = 0; n <= kSweepFillers; n++) {
    const auto pages = layout(filler(n) + "<dl><dt>Which " + kQuestion + "</dt></dl>");
    ASSERT_FALSE(pages.empty()) << "fillers=" << n;
    const int termPage = pageOf(pages, kQuestion);
    ASSERT_GE(termPage, 0) << "fillers=" << n << ": the term was lost entirely";
    for (size_t p = 0; p < pages.size(); p++) {
      EXPECT_FALSE(pages[p].empty()) << "fillers=" << n << ", page " << p << " came out blank";
    }
    // The term is on the LAST page whenever it moved, never orphaned into the
    // middle of the chapter.
    EXPECT_EQ(static_cast<size_t>(termPage) + 1, pages.size()) << "fillers=" << n;
  }
}

}  // namespace
