// WHOLE-BOOK FIND -- the matcher alone, then the real engine on a real book.
//
// The owner's purpose (docs/find-in-book-options-2026-09-28.md section 8):
// finding his place again after the position was lost, by searching for a
// simple string from where the reader is, through the rest of the book, and
// wrapping. So the cases below are the ways that search can fail to land on the
// text he typed:
//
//   * a hit in a LATER chapter (the lost place is usually elsewhere);
//   * a phrase that runs off one page onto the next;
//   * a word the layout broke with a hyphen at the end of a line;
//   * wrap-around, from the end of the book back to the reading position;
//   * a second match on the SAME page is the "next" one;
//   * a miss returns nothing.
//
// The fixture is test/epubs/test_find.epub (scripts/generate_find_test_epub.py).
// It guarantees which words exist and where; which line and page breaks the
// layout chooses is MEASURED here from the laid-out pages, never assumed, so a
// change in the layout engine cannot quietly turn a case into a no-op -- each
// data-driven case asserts that it found something to test.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "BookFinder.h"
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/Section.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "FindMatcher.h"
#include "PageTextCapture.h"
#include "ReadAloudCapture.h"
#include "fontIds.h"

HalDisplay display;
ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string&) { return nullptr; }
bool ImageDecoderFactory::isFormatSupported(const std::string&) { return false; }

namespace {

// ---------------------------------------------------------------------------
// The matcher on its own.
// ---------------------------------------------------------------------------

struct Feed {
  findtext::Query q;
  findtext::Matcher m;
  explicit Feed(const char* query) {
    EXPECT_TRUE(q.set(query));
    m.setQuery(q);
  }
  std::optional<findtext::TextPos> page(uint16_t n, const std::string& text, bool barrier = false,
                                        bool joinable = false) {
    findtext::TextPos hit;
    if (m.feedPage(n, text, barrier, joinable, hit)) return hit;
    return std::nullopt;
  }
};

TEST(FindMatcher, CaseAndWhitespaceAreNormalized) {
  Feed f("  The   KEEPER\tslept ");
  const auto hit = f.page(0, "At dusk the keeper  slept.");
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->offset, 8);  // "the" in the page text
}

TEST(FindMatcher, FoldsLatinAndCyrillic) {
  Feed f("ÉCOLE МОРЕ");
  EXPECT_TRUE(f.page(0, "une école море"));
}

TEST(FindMatcher, PhraseAcrossAPageBoundary) {
  Feed f("salt wind");
  EXPECT_FALSE(f.page(3, "the grey salt"));
  const auto hit = f.page(4, "wind rose");
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->page, 3);  // reported where it STARTS
  EXPECT_EQ(hit->offset, 9);
}

TEST(FindMatcher, LineBreakHyphenJoinsAcrossAPage) {
  // Page 0 ends "extra-" (a layout hyphen, joinable); page 1 opens "ordinary".
  Feed f("extraordinary");
  EXPECT_FALSE(f.page(0, "it was extra-", false, true));
  const auto hit = f.page(1, "ordinary work");
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->page, 0);
  // ...but an image at the top of page 1 is a barrier: no join.
  Feed g("extraordinary");
  EXPECT_FALSE(g.page(0, "it was extra-", false, true));
  EXPECT_FALSE(g.page(1, "ordinary work", true, false));
}

TEST(FindMatcher, HyphensAreIgnoredBothWays) {
  // buildCapture rejoins "well-" + "known" at a line end as "wellknown".
  EXPECT_TRUE(Feed("well-known").page(0, "a wellknown bell"));
  EXPECT_TRUE(Feed("wellknown").page(0, "a well-known bell"));
  // Soft hyphens never reach capture text, but a query may carry one.
  EXPECT_TRUE(Feed("super\xC2\xAD"
                   "cali")
                  .page(0, "supercali"));
}

TEST(FindMatcher, BoundsSelectTheNextInstance) {
  const std::string text = "keeper one keeper two";
  Feed f("keeper");
  const findtext::TextPos first{5, 0};
  f.m.setBounds(&first, nullptr);  // strictly after the first match
  const auto hit = f.page(5, text);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->offset, 11);
  // Upper bound (the wrap's return): nothing after (5, 0) is accepted.
  Feed g("keeper");
  const findtext::TextPos upTo{5, 0};
  g.m.setBounds(nullptr, &upTo);
  const auto wrapHit = g.page(5, text);
  ASSERT_TRUE(wrapHit);
  EXPECT_EQ(wrapHit->offset, 0);
  Feed h("two");
  h.m.setBounds(nullptr, &upTo);
  EXPECT_FALSE(h.page(5, text));
}

TEST(FindMatcher, WrapStopsOnlyWhenNothingCanStillComplete) {
  // The wrap's upper bound is (page 5, offset 0). A match starting there that
  // runs over an image-only page (6) onto page 7 must still be found, so the
  // window is not exhausted until every codepoint that could start one is past
  // the bound.
  Feed f("salt wind");
  const findtext::TextPos upTo{5, 0};
  f.m.setBounds(nullptr, &upTo);
  EXPECT_FALSE(f.page(5, "salt"));
  EXPECT_FALSE(f.m.exhausted()) << "a match starting on the bound is still open";
  EXPECT_FALSE(f.page(6, ""));  // an image page: one space
  EXPECT_FALSE(f.m.exhausted());
  const auto hit = f.page(7, "wind rose");
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->page, 5);
  // Once the tail is all past the bound, the wrap can stop.
  Feed g("salt wind");
  g.m.setBounds(nullptr, &upTo);
  EXPECT_FALSE(g.page(5, "salt"));
  EXPECT_FALSE(g.page(6, "the grey sea and the long tide"));
  EXPECT_TRUE(g.m.exhausted());
}

TEST(FindMatcher, RejectsNothingSearchable) {
  findtext::Query q;
  EXPECT_FALSE(q.set(""));
  EXPECT_FALSE(q.set("   "));
  EXPECT_FALSE(q.set(" - -- "));
  EXPECT_FALSE(findtext::hasSearchableText(" -\xC2\xAD "));
  EXPECT_TRUE(findtext::hasSearchableText(" a "));
  EXPECT_FALSE(q.set(std::string(findtext::kMaxQueryCodepoints + 1, 'a')));
  EXPECT_TRUE(q.set(std::string(findtext::kMaxQueryCodepoints, 'a')));
}

// ---------------------------------------------------------------------------
// The engine, on a real book.
// ---------------------------------------------------------------------------

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

constexpr int kFont = LIBREFRANKLIN_READER_14_FONT_ID;

// A narrow page, so the long words split and chapters run to several pages.
ReaderRenderSpec readerSpec(const uint16_t width = 300, const uint16_t height = 420) {
  ReaderRenderSpec spec;
  spec.fontId = kFont;
  spec.smallFontId = kFont;
  spec.viewportWidth = width;
  spec.viewportHeight = height;
  spec.extraParagraphSpacing = true;
  spec.embeddedStyle = true;
  spec.hyphenationEnabled = 1;  // linebreak::STORED_HYPHENATED
  spec.ligatureFingerprint = 1;
  return spec;
}

std::string fixturePath(const char* name = "test_find.epub") {
  return std::string(CROSSPOINT_TEST_EPUB_DIR) + "/" + name;
}

class Cache {
 public:
  explicit Cache(const std::string& name) : path_("/tmp/cp_find_in_book_" + name) {
    (void)system(("rm -rf '" + path_ + "'").c_str());
    (void)system(("mkdir -p '" + path_ + "'").c_str());
  }
  ~Cache() { (void)system(("rm -rf '" + path_ + "'").c_str()); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

std::shared_ptr<Epub> openBook(const Cache& cache, const std::string& path) {
  auto epub = std::make_shared<Epub>(path, cache.path());
  if (!epub->load()) return nullptr;
  epub->setupCacheDir();
  return epub;
}

// A page's rendered text exactly as BookFinder reads it.
struct PageText {
  std::string text;
  bool barrierBefore = false;
  bool endsWithBarrier = false;
};
PageText pageTextOf(Section& section, int page) {
  struct R {
    uint16_t x, y, w, h;
    uint32_t byteOffset;
    uint16_t byteLen;
  };
  PageText out;
  const auto p = section.loadPage(page);
  if (!p) return out;
  readaloud::PageCaptureScratch scratch;
  auto& r = Gfx::instance().renderer();
  readaloud::flattenPage(*p, r, kFont, 0, 0, scratch);
  readaloud::CaptureMetrics m{r.getLineHeight(kFont), r.getSpaceWidth(kFont), 1};
  std::vector<R> rects;
  readaloud::buildCapture(scratch.lines.data(), scratch.lines.size(), m, out.text, rects);
  out.barrierBefore = scratch.lines.empty() || scratch.lines.front().hyphenBarrierBefore;
  out.endsWithBarrier = scratch.endsWithBarrier;
  return out;
}

// Every chapter's pages, laid out once with the reader's spec.
std::vector<std::vector<PageText>> layOutBook(const std::shared_ptr<Epub>& epub, const ReaderRenderSpec& spec) {
  std::vector<std::vector<PageText>> book;
  for (int s = 0; s < epub->getSpineItemsCount(); s++) {
    Section section(epub, s, Gfx::instance().renderer());
    if (!section.loadSectionFile(spec)) EXPECT_TRUE(section.createSectionFile(spec)) << "spine " << s;
    std::vector<PageText> pages;
    for (int p = 0; p < section.pageCount; p++) pages.push_back(pageTextOf(section, p));
    book.push_back(std::move(pages));
  }
  return book;
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

struct FindRun {
  BookFinder::Status status = BookFinder::Status::Running;
  BookFinder::Hit hit;
  uint32_t pages = 0;
  uint32_t built = 0;
  double ms = 0;
};

FindRun find(const std::shared_ptr<Epub>& epub, const ReaderRenderSpec& spec, const std::string& query, int spine,
             uint16_t page, int32_t offset = findtext::kPageStart) {
  FindRun run;
  BookFinder finder(epub, Gfx::instance().renderer(), spec, kFont);
  if (!finder.begin(query, spine, page, offset)) {
    run.status = BookFinder::Status::NotFound;
    return run;
  }
  const auto t0 = std::chrono::steady_clock::now();
  for (int guard = 0; guard < 2000000 && run.status == BookFinder::Status::Running; guard++) run.status = finder.step();
  run.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_NE(run.status, BookFinder::Status::Running) << "the search never ended";
  run.hit = finder.hit();
  run.pages = finder.pagesScanned();
  run.built = finder.chaptersBuilt();
  return run;
}

class FindInBook : public ::testing::Test {
 protected:
  void SetUp() override {
    epub = openBook(cache, fixturePath());
    ASSERT_TRUE(epub) << "could not load " << fixturePath();
    ASSERT_EQ(epub->getSpineItemsCount(), 5);
    book = layOutBook(epub, spec);
  }
  // The page a hit names, lower-cased.
  std::string hitPage(const FindRun& r) const { return lower(book[r.hit.spine][r.hit.page].text); }
  // The rendered text from the hit onward, running onto the next page the way
  // the matcher reads it (a page-final line-break hyphen joins), with hyphens
  // dropped as the matcher drops them. A hit must START with the query.
  std::string fromHit(const FindRun& r) const {
    const auto& pages = book[r.hit.spine];
    std::string s = pages[r.hit.page].text.substr(r.hit.offset);
    if (static_cast<size_t>(r.hit.page) + 1 < pages.size()) {
      if (!s.empty() && s.back() == '-') {
        s.pop_back();
      } else {
        s += ' ';
      }
      s += pages[r.hit.page + 1].text;
    }
    s.erase(std::remove(s.begin(), s.end(), '-'), s.end());
    return lower(s);
  }
  static bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

  Cache cache{::testing::UnitTest::GetInstance()->current_test_info()->name()};
  ReaderRenderSpec spec = readerSpec();
  std::shared_ptr<Epub> epub;
  std::vector<std::vector<PageText>> book;
};

TEST_F(FindInBook, HitInALaterChapter) {
  const auto r = find(epub, spec, "zanzibar HARBOUR", 0, 0);
  ASSERT_EQ(r.status, BookFinder::Status::Found);
  EXPECT_EQ(r.hit.spine, 3);  // Chapter Four
  // At 300 px this layout happens to break "Zanz-ibar" over the page edge, so
  // this case also proves the page-boundary hyphen join on a real layout.
  EXPECT_TRUE(startsWith(fromHit(r), "zanzibar harbour")) << fromHit(r).substr(0, 60);
}

TEST_F(FindInBook, ColdBookLaysOutAsItGoes) {
  // No section files on the card: the search builds each chapter it reaches.
  (void)system(("rm -f '" + epub->getCachePath() + "'/sections/*").c_str());
  const auto r = find(epub, spec, "zanzibar", 0, 0);
  ASSERT_EQ(r.status, BookFinder::Status::Found);
  EXPECT_EQ(r.hit.spine, 3);
  EXPECT_EQ(r.built, 4u) << "chapters 1-4 should each have been laid out once";
  EXPECT_TRUE(startsWith(fromHit(r), "zanzibar"));
  // ...and the page it names is the one the reader's own layout produces.
  Section section(epub, 3, Gfx::instance().renderer());
  ASSERT_TRUE(section.loadSectionFile(spec)) << "Find must leave the section file reading would have written";
  EXPECT_EQ(pageTextOf(section, r.hit.page).text, book[3][r.hit.page].text);
}

TEST_F(FindInBook, PhraseAcrossAPageBoundary) {
  int tested = 0;
  for (int s = 0; s < static_cast<int>(book.size()) && tested < 6; s++) {
    for (int p = 0; p + 1 < static_cast<int>(book[s].size()) && tested < 6; p++) {
      const std::string& a = book[s][p].text;
      const std::string& b = book[s][p + 1].text;
      if (a.empty() || b.empty() || a.back() == '-' || book[s][p + 1].barrierBefore) continue;
      // The last two words of page p and the first two of page p+1.
      const size_t aCut = a.rfind(' ', a.rfind(' ') - 1);
      const size_t bCut = b.find(' ', b.find(' ') + 1);
      if (aCut == std::string::npos || bCut == std::string::npos) continue;
      const std::string phrase = a.substr(aCut + 1) + " " + b.substr(0, bCut);
      const auto r = find(epub, spec, phrase, s, static_cast<uint16_t>(p));
      ASSERT_EQ(r.status, BookFinder::Status::Found)
          << "'" << phrase << "' spanning spine " << s << " pages " << p << "/" << p + 1;
      EXPECT_EQ(r.hit.spine, s) << phrase;
      EXPECT_EQ(r.hit.page, p) << phrase;
      EXPECT_EQ(r.hit.offset, static_cast<int32_t>(aCut + 1)) << phrase;
      tested++;
    }
  }
  EXPECT_GE(tested, 3) << "the layout produced too few page boundaries to test";
}

TEST_F(FindInBook, AWordTheLayoutHyphenated) {
  // Find line-final layout hyphens by walking the raw TextBlock tokens -- the
  // capture text has already rejoined them, which is the point.
  static const char* const kLong[] = {
      "incomprehensibility",  "institutionalization", "counterrevolutionary", "electroencephalograph",
      "uncharacteristically", "disproportionately",   "interdisciplinary",    "indistinguishable",
      "misunderstandings",    "photosynthesizing",    "telecommunications",   "overcompensation",
      "unconstitutionally",   "compartmentalization", "internationalization", "responsibilities",
      "extraordinarily",      "characterization",     "entrepreneurship",     "infrastructure",
      "reconsideration",      "individualistic",      "meteorologically",     "circumnavigation",
      "hypersensitivity",     "notwithstanding",      "instrumentalities",    "industrialization",
      "unquestionably",       "transcontinental"};
  int tested = 0;
  for (int s = 0; s < epub->getSpineItemsCount(); s++) {
    Section section(epub, s, Gfx::instance().renderer());
    ASSERT_TRUE(section.loadSectionFile(spec));
    for (int p = 0; p < section.pageCount; p++) {
      const auto page = section.loadPage(p);
      ASSERT_TRUE(page);
      std::string prevLast;
      for (const auto& el : page->elements) {
        if (el->getTag() != TAG_PageLine) continue;
        const auto& block = *static_cast<const PageLine&>(*el).getBlock();
        if (block.wordCount() == 0) continue;
        const std::string first = block.wordText(0);
        if (!prevLast.empty() && prevLast.back() == '-') {
          std::string joined = lower(prevLast.substr(0, prevLast.size() - 1) + first);
          joined.erase(std::remove_if(joined.begin(), joined.end(), [](unsigned char c) { return !std::isalpha(c); }),
                       joined.end());
          for (const char* w : kLong) {
            if (joined != w) continue;
            // Each long word occurs once in the book: from the chapter start,
            // the only match is the split one, and it is on this page.
            const auto r = find(epub, spec, w, s, 0);
            ASSERT_EQ(r.status, BookFinder::Status::Found)
                << w << " split across a line on spine " << s << " page " << p;
            EXPECT_EQ(r.hit.spine, s) << w;
            EXPECT_EQ(r.hit.page, p) << w;
            EXPECT_TRUE(startsWith(fromHit(r), w)) << w << " not rejoined in: " << hitPage(r);
            tested++;
          }
        }
        prevLast = block.wordText(block.wordCount() - 1);
      }
    }
  }
  EXPECT_GE(tested, 1) << "the layout hyphenated none of the planted words; narrow the page";
  // The soft-hyphenated word in the source is found whole.
  const auto soft = find(epub, spec, "supercalifragilistic", 0, 0);
  ASSERT_EQ(soft.status, BookFinder::Status::Found);
  EXPECT_EQ(soft.hit.spine, 1);
  // And an explicit hyphen matches with or without it.
  EXPECT_EQ(find(epub, spec, "well-known bell", 0, 0).hit.spine, 4);
  EXPECT_EQ(find(epub, spec, "wellknown bell", 0, 0).hit.spine, 4);
}

TEST_F(FindInBook, WrapsAroundToTheReadingPosition) {
  // From the last chapter, "marmalade lantern" (only in Chapter One) wraps.
  const int lastSpine = static_cast<int>(book.size()) - 1;
  const auto r = find(epub, spec, "marmalade lantern", lastSpine, 0);
  ASSERT_EQ(r.status, BookFinder::Status::Found);
  EXPECT_EQ(r.hit.spine, 0);
  EXPECT_TRUE(startsWith(fromHit(r), "marmalade lantern"));

  // Within ONE chapter: "keeper" is on an early page of Chapter Three; start on
  // its last page and the search goes through Four and Five, wraps through One
  // and Two, and comes back to it.
  const int keeperSpine = 2;
  const auto fromLast = find(epub, spec, "keeper", keeperSpine, static_cast<uint16_t>(book[keeperSpine].size() - 1));
  ASSERT_EQ(fromLast.status, BookFinder::Status::Found);
  EXPECT_EQ(fromLast.hit.spine, keeperSpine);
  EXPECT_LT(fromLast.hit.page, book[keeperSpine].size() - 1);
  EXPECT_TRUE(startsWith(fromHit(fromLast), "keeper"));
}

TEST_F(FindInBook, NextIsTheSecondMatchOnTheSamePage) {
  const auto first = find(epub, spec, "keeper", 0, 0);
  ASSERT_EQ(first.status, BookFinder::Status::Found);
  ASSERT_EQ(first.hit.spine, 2);
  // Find again from the hit: the next instance, on the same page.
  const auto second = find(epub, spec, "keeper", first.hit.spine, first.hit.page, first.hit.offset);
  ASSERT_EQ(second.status, BookFinder::Status::Found);
  EXPECT_EQ(second.hit.spine, first.hit.spine);
  EXPECT_EQ(second.hit.page, first.hit.page) << "the plant puts both on one page";
  EXPECT_GT(second.hit.offset, first.hit.offset);
  EXPECT_TRUE(startsWith(fromHit(second), "keeper"));
  // Only two in the book: the third Find wraps all the way round to the first.
  const auto third = find(epub, spec, "keeper", second.hit.spine, second.hit.page, second.hit.offset);
  ASSERT_EQ(third.status, BookFinder::Status::Found);
  EXPECT_EQ(third.hit.spine, first.hit.spine);
  EXPECT_EQ(third.hit.page, first.hit.page);
  EXPECT_EQ(third.hit.offset, first.hit.offset);
}

TEST_F(FindInBook, NotFoundReturnsNothing) {
  const auto r = find(epub, spec, "xylophone quux", 2, 1);
  EXPECT_EQ(r.status, BookFinder::Status::NotFound);
  EXPECT_EQ(r.hit.spine, -1);
  // It looked at every page once, plus the wrap's return to the start page.
  size_t total = 0;
  for (const auto& ch : book) total += ch.size();
  EXPECT_GE(r.pages, total);
}

// Host scan time on a real book, for the doc's measurements. Opt-in:
//   CROSSPOINT_FIND_PERF_EPUB=/path/book.epub CROSSPOINT_FIND_PERF_QUERY=word
TEST(FindInBookPerf, ColdAndWarmWholeBookMiss) {
  const char* path = std::getenv("CROSSPOINT_FIND_PERF_EPUB");
  if (!path) GTEST_SKIP() << "set CROSSPOINT_FIND_PERF_EPUB to measure";
  const char* q = std::getenv("CROSSPOINT_FIND_PERF_QUERY");
  const std::string query = q ? q : "zqxjv no such string";
  const Cache cache("perf");
  auto epub = openBook(cache, path);
  ASSERT_TRUE(epub);
  const auto spec = readerSpec(464, 736);
  const auto cold = find(epub, spec, query, 0, 0);
  const auto warm = find(epub, spec, query, 0, 0);
  std::printf("[perf] %s: %d spines; query '%s'\n", path, epub->getSpineItemsCount(), query.c_str());
  std::printf("[perf] cold: %s, %.1f ms, %u pages scanned, %u chapters laid out\n",
              cold.status == BookFinder::Status::Found ? "found" : "not found", cold.ms, cold.pages, cold.built);
  std::printf("[perf] warm: %s, %.1f ms, %u pages scanned, %u chapters laid out\n",
              warm.status == BookFinder::Status::Found ? "found" : "not found", warm.ms, warm.pages, warm.built);
}

}  // namespace
