// What the two line breakers actually do to a page, measured.
//
// `LineBreakModeTest` pins WHICH breaker a stored byte selects. This suite
// measures what that choice is worth, by laying real prose out through the REAL
// layout engine (ParsedText + GfxRenderer + the built-in Libre Franklin reader
// faces) under both modes and reading the geometry back off the TextBlocks the
// paginator would have baked.
//
// IT EXISTS BECAUSE IT OVERTURNED THE STORY THE ROW WAS NEARLY SHIPPED WITH.
// The 2026-08-25 survey (docs/typography-possible-2026-08-25.md) said the
// total-fit breaker buys "a noticeably more even right edge", and the row was
// drafted with a second label of "Even Spacing" on that basis. Measured over
// 394 paragraphs / 23,075 words, six measure x size configurations, the
// opposite is true where it matters most: on a JUSTIFIED page the greedy
// breaker is both tighter and more even, in all six, because hyphen points give
// it fitting freedom the DP has none of. Word spacing is what a reader means by
// "even", so "Even Spacing" would have been a label the page disproves. The
// honest trade is hyphens against whole words, which is what the row says now.
//
// The full table is in docs/line-breaking-2026-08-25.md; DISABLED_Sweep at the
// foot of this file is the instrument that produced it.
//
// What the four live tests pin is only what held in EVERY configuration:
//   * justified -- greedy sets tighter AND more evenly. Assert the direction,
//     so nobody re-labels the row on the survey's version of the story.
//   * ragged -- the two are close. Assert only that total-fit does not become
//     markedly WORSE, which is what a broken DP would look like.
//   * hyphens -- the actual trade, and the only large effect.
//   * cost -- total-fit is a small multiple of greedy, not a different order.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "Epub/AutoJustify.h"
#include "Epub/LineBreakMode.h"
#include "Epub/ParsedText.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "fontIds.h"

HalDisplay display;

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
    if (!decompressor_.init()) {
      ADD_FAILURE() << "font decompressor init failed";
    }
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(LIBREFRANKLIN_READER_12_FONT_ID, lfReader12_);
    renderer_.insertFont(LIBREFRANKLIN_READER_18_FONT_ID, lfReader18_);
    // WITHOUT THIS THE GREEDY BREAKER CANNOT HYPHENATE AT ALL, and the whole
    // suite silently measures two breakers that both set whole words.
    // Hyphenator::cachedHyphenator_ starts null and is only filled by this
    // call; the reader makes it from the EPUB's dc:language. It cost a wrong
    // reading here first: 122 lines of English produced ONE hyphen.
    Hyphenator::setPreferredLanguage("en");
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont lfr12R_{&librefranklin_reader_12_regular}, lfr12B_{&librefranklin_reader_12_bold},
      lfr12I_{&librefranklin_reader_12_italic}, lfr12BI_{&librefranklin_reader_12_bolditalic};
  EpdFontFamily lfReader12_{&lfr12R_, &lfr12B_, &lfr12I_, &lfr12BI_};
  EpdFont lfr18R_{&librefranklin_reader_18_regular}, lfr18B_{&librefranklin_reader_18_bold},
      lfr18I_{&librefranklin_reader_18_italic}, lfr18BI_{&librefranklin_reader_18_bolditalic};
  EpdFontFamily lfReader18_{&lfr18R_, &lfr18B_, &lfr18I_, &lfr18BI_};
};

// The X3's real reading measure in portrait: 480 px of panel less the bezel
// margins and the default screenMargin. Prose, not a specimen sentence, and
// real paragraphs rather than one long run: the DP optimizes over a PARAGRAPH,
// so its advantage only shows when paragraphs end.
//
// Text is from wingspan-the-whole-bird.epub, a book on the test card.
const std::vector<std::string>& paragraphs() {
  static const std::vector<std::string> p = {
      "To play a bird you pay its cost - some food, shown on the card - and put it in a row. Now the clever bit: a "
      "bird sitting in a row makes that row better. An empty forest gets you one food. A forest with four birds in it "
      "gets you three food, plus each of those birds may do its own little trick. So the more birds you have in a "
      "row, the more a single turn produces.",
      "The reason for the shrinking is worth knowing, because it makes the number stick. You have eight little wooden "
      "cubes. Each turn you take, you spend one - but you get them all back at the end of the round, except one: at "
      "the end of each round you permanently park one cube on the scoring board to mark how you did on that round's "
      "goal. It never comes back. Eight cubes, then seven, then six, then five.",
      "A useful mental model for a first few games. Your bird values and your eggs will usually be the two biggest "
      "columns on the scorepad. Goals are the swingiest - three contested goals can easily be a twelve-point spread "
      "between first and third. Cached food and tucked cards are the quiet accumulators: individually tiny, but a "
      "bird that tucks a card every time you use a row will out-earn its printed value several times over.",
      "Place a cube on the leftmost exposed forest space and gain that many food by removing that many dice from the "
      "birdfeeder, taking one token matching each die. The split invertebrate/seed face yields one token of either "
      "type, not two. Removed dice sit outside the tray until a refill. Then optionally use the space's card-to-food "
      "trade, choosing from dice remaining in the feeder. Then activate brown forest abilities right to left.",
      "In Wingspan, the birds on your board are the feeders. Early on, one bird gets you almost nothing. By the end, "
      "taking a single action can set off a chain of five birds in a row, each one handing you something. That chain "
      "is your engine. Building an engine means spending your early turns on things that pay you back later, instead "
      "of on things that score points right now.",
      "Place a cube on the leftmost exposed wetland space and draw that many cards, from the three face-up cards or "
      "the top of the deck in any mix. Face-up cards are not replaced until the end of your turn, so a two-card draw "
      "can take two of the three visible. Then optionally discard one egg from a bird to draw one more. Then activate "
      "brown wetland abilities right to left.",
      "Because the payout climbs from 4 to 7 for first place, a goal in round four is worth nearly twice one in round "
      "one. If you can only contest some of them, contest the late ones. And check which goals you currently score "
      "zero on - going from zero to one of something is often worth more than going from three to four, because zero "
      "scores nothing at all.",
      "Caching puts a food token onto a bird, where it is worth one point at the end and can never be spent. If food "
      "tokens run out, cache discarded cards as substitutes. Tucking slides a card face down under a bird, worth one "
      "point each. Some cards show a predator icon or a flocking icon; these matter only because other cards and "
      "bonus cards refer to them.",
  };
  return p;
}

std::vector<std::string> splitWords(const std::string& text) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && text[i] == ' ') i++;
    const size_t start = i;
    while (i < text.size() && text[i] != ' ') i++;
    if (i > start) out.push_back(text.substr(start, i - start));
  }
  return out;
}

struct Stats {
  // Justified: gap width per line. Ragged: line end position.
  double mean = 0.0;
  double stddev = 0.0;
  double maxAbsDeviation = 0.0;
  int lineCount = 0;
  int hyphenatedLines = 0;

  void accumulate(const std::vector<double>& samples) {
    lineCount = static_cast<int>(samples.size());
    if (samples.empty()) return;
    for (const double v : samples) mean += v;
    mean /= samples.size();
    for (const double v : samples) {
      const double d = v - mean;
      stddev += d * d;
      maxAbsDeviation = std::max(maxAbsDeviation, std::abs(d));
    }
    stddev = std::sqrt(stddev / samples.size());
  }
};

bool endsWithHyphen(const std::string& s) { return !s.empty() && s.back() == '-'; }

// One paragraph laid out, with every line's geometry read back off the TextBlock
// the paginator would have baked. `gapSamples` collects the mean inter-word gap
// of each non-final line; `endSamples` collects each non-final line's right
// edge. Both are in framebuffer pixels.
void layoutParagraph(const std::string& text, const int fontId, const uint16_t measure, const uint8_t storedMode,
                     const CssTextAlign align, const int justifyThresholdChars, std::vector<double>& gapSamples,
                     std::vector<double>& endSamples, int& hyphenatedLines) {
  BlockStyle style;
  style.alignment = align;
  ParsedText block(/*extraParagraphSpacing=*/false, linebreak::splitsWordsAtLineEnds(storedMode),
                   /*focusReadingEnabled=*/false, style);
  for (const auto& w : splitWords(text)) block.addWord(w, EpdFontFamily::REGULAR);

  std::vector<std::shared_ptr<TextBlock>> lines;
  block.layoutAndExtractLines(
      Gfx::instance().renderer(), fontId, measure, [&](const std::shared_ptr<TextBlock>& line) { lines.push_back(line); },
      /*includeLastLine=*/true, justifyThresholdChars);

  auto& r = Gfx::instance().renderer();
  for (size_t li = 0; li + 1 < lines.size(); ++li) {  // the last line is never justified; it says nothing
    const TextBlock& line = *lines[li];
    if (line.wordCount() == 0) continue;

    // A ONE-WORD LINE IS SKIPPED FOR GAPS AND COUNTED FOR EVERYTHING ELSE. It
    // has no gap to measure, but it is a real and maximally short line end, and
    // the two breakers do not produce one-word lines at the same rate -- so
    // dropping it from endSamples would remove exactly the deepest rag from the
    // column that exists to measure the rag. An earlier version of this loop
    // `continue`d past the whole line and the published figures were wrong.
    double gapSum = 0.0;
    int gapCount = 0;
    for (uint16_t w = 0; w + 1 < line.wordCount(); ++w) {
      const int adv = r.getTextAdvanceX(fontId, line.wordText(w), line.wordStyle(w));
      const double gap = static_cast<double>(line.wordXpos(w + 1)) - (line.wordXpos(w) + adv);
      // A continuation fragment attaches with no space at all (an em dash, a
      // no-break group). Counting those zeros would dilute both modes' means by
      // however many of them the line happens to hold, which is noise about the
      // text rather than signal about the breaker.
      if (gap < 1.0) continue;
      gapSum += gap;
      gapCount++;
    }
    if (gapCount > 0) gapSamples.push_back(gapSum / gapCount);

    const uint16_t last = line.wordCount() - 1;
    const int lastAdv = r.getTextAdvanceX(fontId, line.wordText(last), line.wordStyle(last));
    endSamples.push_back(static_cast<double>(line.wordXpos(last) + lastAdv));

    if (endsWithHyphen(line.wordText(last))) hyphenatedLines++;
  }
}

Stats measure(const uint8_t storedMode, const CssTextAlign align, const int fontId, const uint16_t measurePx,
              const int justifyThresholdChars, const bool useGaps) {
  std::vector<double> gaps;
  std::vector<double> ends;
  int hyphens = 0;
  for (const auto& p : paragraphs()) {
    layoutParagraph(p, fontId, measurePx, storedMode, align, justifyThresholdChars, gaps, ends, hyphens);
  }
  Stats s;
  s.accumulate(useGaps ? gaps : ends);
  s.hyphenatedLines = hyphens;
  return s;
}

// The X3's real portrait reading measure at the default screen margin of 5:
// 540 px of panel (BoardConfig) less the bezel margins and that 5, measured
// 2026-08-23 for the auto-justify calibration table
// (docs/auto-justification.md). Not 480 -- an earlier version of this comment
// said so, and 480 less margins cannot be 512.
constexpr uint16_t kMeasure = 512;

// TWO REAL DEVICE CONFIGURATIONS, because the reader lands in both and the
// breaker's visible effect is different in each. Neither is invented: they are
// two reachable combinations of the reading size and the Justified Text row.
//
// Both are DEFAULT configurations -- same measure, same threshold of 40, only
// the reading size differs -- so neither regime is manufactured by moving a
// second dial.
//
//   RAGGED     18 pt Libre Franklin is ~32 characters per line at 512 px
//              (alphabet 456 px, x28.1), under the shipped threshold of 40, so
//              autojustify demotes the page to its natural ragged edge. This is
//              also why the row's second label is "Whole Words" and not "Even
//              Spacing": word spacing on a ragged page is one space width in
//              BOTH modes, so a spacing promise would be invisible here.
//   JUSTIFIED  12 pt is ~47 characters at the same measure and justifies at the
//              same threshold. Four of the thirteen face/size pairs in the
//              calibration table do.
constexpr int kRaggedFont = LIBREFRANKLIN_READER_18_FONT_ID;
constexpr int kJustifiedFont = LIBREFRANKLIN_READER_12_FONT_ID;
constexpr int kJustifiedThreshold = autojustify::THRESHOLD_CHARS;

}  // namespace

// ---------------------------------------------------------------------------
// Justified: word spacing from line to line
// ---------------------------------------------------------------------------

TEST(LineBreakQuality, HyphenationIsWhatMakesJustifiedSpacingTightAndEven) {
  const Stats greedy =
      measure(linebreak::STORED_HYPHENATED, CssTextAlign::Justify, kJustifiedFont, kMeasure, kJustifiedThreshold, true);
  const Stats totalFit = measure(linebreak::STORED_WHOLE_WORDS, CssTextAlign::Justify, kJustifiedFont, kMeasure,
                                 kJustifiedThreshold, true);

  ASSERT_GT(greedy.lineCount, 30) << "specimen too short to say anything";
  ASSERT_GT(totalFit.lineCount, 30);

  printf(
      "[justified, %u px measure]\n"
      "  Hyphenated    lines %3d  gap mean %6.2f px  sd %5.2f  max dev %6.2f  hyphenated lines %d\n"
      "  Whole Words   lines %3d  gap mean %6.2f px  sd %5.2f  max dev %6.2f  hyphenated lines %d\n",
      kMeasure, greedy.lineCount, greedy.mean, greedy.stddev, greedy.maxAbsDeviation, greedy.hyphenatedLines,
      totalFit.lineCount, totalFit.mean, totalFit.stddev, totalFit.maxAbsDeviation, totalFit.hyphenatedLines);

  // THE SURPRISE, and the reason this suite exists. A total-fit optimizer that
  // may not hyphenate is not a better justifier than a greedy one that may:
  // every break it would like to make is unavailable to it, so it pays in
  // slack, and justification turns slack into word space. Both numbers move the
  // same way in all six sweep configurations.
  EXPECT_LT(greedy.mean, totalFit.mean) << "hyphenation stopped buying a tighter setting";
  EXPECT_LT(greedy.stddev, totalFit.stddev) << "hyphenation stopped buying more even word spacing";

  // Which is also the measurement that says the missing combination -- total
  // fit WITH hyphen points as candidates, i.e. what Knuth-Plass actually is --
  // is the one worth building. See lib/Epub/Epub/LineBreakMode.h for why it is
  // not reachable from here.
}

// ---------------------------------------------------------------------------
// Ragged: the depth and evenness of the rag
// ---------------------------------------------------------------------------

TEST(LineBreakQuality, WholeWordsDoesNotWorsenTheRaggedEdge) {
  const Stats greedy = measure(linebreak::STORED_HYPHENATED, CssTextAlign::Justify, kRaggedFont, kMeasure,
                               autojustify::THRESHOLD_CHARS, false);
  const Stats totalFit = measure(linebreak::STORED_WHOLE_WORDS, CssTextAlign::Justify, kRaggedFont, kMeasure,
                                 autojustify::THRESHOLD_CHARS, false);

  // THIS TEST ASSERTS A REGIME IT DOES NOT CHOOSE. It hands autojustify a
  // justified block and relies on the measure demoting it. If that calibration
  // ever moves, both arms become justified, every line ends flush at the
  // measure, and the two standard deviations collapse to hanging-punctuation
  // noise -- where the comparison below passes or fails at random while
  // appearing to measure a rag. A flush-right page cannot produce this spread.
  ASSERT_GT(greedy.stddev, 10.0) << "this configuration is no longer ragged -- the auto-justify threshold moved";
  ASSERT_GT(totalFit.stddev, 10.0) << "this configuration is no longer ragged -- the auto-justify threshold moved";

  printf(
      "[ragged, %u px measure]\n"
      "  Hyphenated    lines %3d  end mean %6.2f px  sd %5.2f  max dev %6.2f  hyphenated lines %d\n"
      "  Whole Words   lines %3d  end mean %6.2f px  sd %5.2f  max dev %6.2f  hyphenated lines %d\n",
      kMeasure, greedy.lineCount, greedy.mean, greedy.stddev, greedy.maxAbsDeviation, greedy.hyphenatedLines,
      totalFit.lineCount, totalFit.mean, totalFit.stddev, totalFit.maxAbsDeviation, totalFit.hyphenatedLines);

  // NOT "more even". The sweep found total-fit ahead at 12 pt (sd 27.2 against
  // 32.2 at this measure) and level or slightly behind at 18 pt, so a
  // strictly-better assertion here would be pinning one cell of a table whose
  // sign changes. What is worth guarding is the floor: a DP that stopped
  // optimizing would rag far worse than a greedy first fit, not marginally.
  EXPECT_LT(totalFit.stddev, greedy.stddev * 1.2)
      << "total-fit's ragged edge has become markedly worse than a plain greedy fill -- it is no longer optimizing";
}

// ---------------------------------------------------------------------------
// The price
// ---------------------------------------------------------------------------

TEST(LineBreakQuality, HyphenatedSplitsWordsAndWholeWordsAlmostNever) {
  const Stats greedy =
      measure(linebreak::STORED_HYPHENATED, CssTextAlign::Justify, kJustifiedFont, kMeasure, kJustifiedThreshold, true);
  const Stats totalFit = measure(linebreak::STORED_WHOLE_WORDS, CssTextAlign::Justify, kJustifiedFont, kMeasure,
                                 kJustifiedThreshold, true);

  // The visible trade the row is selling.
  EXPECT_GT(greedy.hyphenatedLines, 0) << "the greedy breaker hyphenated nothing -- the specimen cannot show the trade";
  EXPECT_LT(totalFit.hyphenatedLines, greedy.hyphenatedLines);

  // NOT zero, and the row's help text must not promise zero: computeLineBreaks
  // still splits a word too wide to fit a line even on its own. This assertion
  // is a ceiling, not an equality, so the day a specimen contains such a word
  // the suite reports it rather than failing.
  EXPECT_LE(totalFit.hyphenatedLines, greedy.hyphenatedLines / 4);
}

// ---------------------------------------------------------------------------
// What it costs to run
// ---------------------------------------------------------------------------

// The DP is O(n * words-per-line) rather than O(n^2) -- its inner loop breaks
// the moment the line overflows -- so it is a small multiple of the greedy
// pass, not a different order. This measures the multiple and prints it.
//
// IT TIMES `layoutAndExtractLines` AND NOTHING ELSE. The first version of this
// test called the measurement helper above, which also splits the text, builds
// the ParsedText, and walks every line calling getTextAdvanceX twice per word.
// All of that is identical in both modes, so it diluted the ratio toward 1.0
// and the figure it published (1.35x) was a FLOOR on the breaker's real
// relative cost, not the cost. The block construction is hoisted out of the
// timed region here for the same reason.
//
// The assertion is a generous CEILING against a rewrite that made the DP
// quadratic in the paragraph, not a pin on today's number: an absolute timing
// assertion on a shared machine is a flake generator.
TEST(LineBreakQuality, TotalFitCostsASmallMultipleOfGreedy) {
  constexpr int kChapterRepeats = 12;  // ~6,500 words, a long chapter
  constexpr int kPasses = 5;

  // One layout pass over a chapter's worth of text, timing ONLY the breaker
  // plus the line extraction it feeds. Each paragraph needs a fresh ParsedText
  // because layoutAndExtractLines consumes the words it lays out, so the build
  // is done first and the clock is started after.
  auto run = [](const uint8_t mode, double& msOut) {
    int lines = 0;
    double total = 0.0;
    for (int pass = 0; pass < kPasses; pass++) {
      for (int rep = 0; rep < kChapterRepeats; rep++) {
        for (const auto& p : paragraphs()) {
          BlockStyle style;
          style.alignment = CssTextAlign::Justify;
          ParsedText block(false, linebreak::splitsWordsAtLineEnds(mode), false, style);
          for (const auto& w : splitWords(p)) block.addWord(w, EpdFontFamily::REGULAR);

          int seen = 0;
          const auto t0 = std::chrono::steady_clock::now();
          block.layoutAndExtractLines(
              Gfx::instance().renderer(), kRaggedFont, kMeasure,
              [&seen](const std::shared_ptr<TextBlock>&) { seen++; }, /*includeLastLine=*/true,
              autojustify::THRESHOLD_CHARS);
          total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
          lines += seen;
        }
      }
    }
    msOut = total / kPasses;
    return lines / kPasses;
  };

  // Warm the font cache and the hyphenation tables so the first mode measured
  // does not pay for both.
  double warm = 0.0;
  run(linebreak::STORED_HYPHENATED, warm);
  run(linebreak::STORED_WHOLE_WORDS, warm);

  double greedyMs = 0.0;
  double dpMs = 0.0;
  const int greedyLines = run(linebreak::STORED_HYPHENATED, greedyMs);
  const int dpLines = run(linebreak::STORED_WHOLE_WORDS, dpMs);

  printf(
      "[line breaking only, %d paragraphs x %d, %d passes averaged]\n"
      "  Hyphenated   %8.2f ms  (%d lines)\n"
      "  Whole Words  %8.2f ms  (%d lines)\n"
      "  ratio        %8.2fx\n",
      static_cast<int>(paragraphs().size()), kChapterRepeats, kPasses, greedyMs, greedyLines, dpMs, dpLines,
      dpMs / greedyMs);

  ASSERT_GT(greedyMs, 0.0);
  EXPECT_LT(dpMs / greedyMs, 4.0) << "the total-fit breaker has become far more expensive than a small multiple of "
                                     "greedy -- check that its inner loop still breaks on overflow";
}

// ---------------------------------------------------------------------------
// The instrument: a corpus sweep, DISABLED by default
// ---------------------------------------------------------------------------

// The four tests above run on eight embedded paragraphs so the suite stays
// hermetic and instant. That is enough to pin a direction and nowhere near
// enough to characterize a line breaker, and the difference cost a wrong
// reading on 2026-08-25: at a 458 px measure the total-fit breaker looked
// dramatically better on the rag, and at the device's real 512 px it was a
// wash. Two paragraphs of specimen can say either.
//
// So this is the instrument that produced the numbers in
// docs/line-breaking-2026-08-25.md, kept beside the code rather than in a
// scratch directory. Point it at a plain-text corpus (one paragraph per line)
// and it sweeps measure x size x alignment x mode and prints a table:
//
//   CROSSPOINT_LINEBREAK_CORPUS=/path/to/corpus.txt \
//     build/test/line_break_quality/LineBreakQualityTest \
//     --gtest_also_run_disabled_tests --gtest_filter='*Sweep*'
//
// Disabled rather than skipped-on-missing-file so it never silently reports a
// pass on a corpus that was not there.
TEST(LineBreakQuality, DISABLED_Sweep) {
  const char* path = std::getenv("CROSSPOINT_LINEBREAK_CORPUS");
  ASSERT_NE(path, nullptr) << "set CROSSPOINT_LINEBREAK_CORPUS to a plain-text corpus, one paragraph per line";
  std::ifstream in(path);
  ASSERT_TRUE(in.good()) << "cannot read " << path;

  std::vector<std::string> corpus;
  for (std::string line; std::getline(in, line);) {
    if (line.size() > 40) corpus.push_back(line);
  }
  ASSERT_FALSE(corpus.empty());
  printf("corpus: %zu paragraphs\n", corpus.size());

  struct Cfg {
    const char* name;
    int fontId;
    uint16_t measure;
  };
  const Cfg cfgs[] = {
      {"LF 12pt @ 400", LIBREFRANKLIN_READER_12_FONT_ID, 400},
      {"LF 12pt @ 512", LIBREFRANKLIN_READER_12_FONT_ID, 512},
      {"LF 12pt @ 640", LIBREFRANKLIN_READER_12_FONT_ID, 640},
      {"LF 18pt @ 400", LIBREFRANKLIN_READER_18_FONT_ID, 400},
      {"LF 18pt @ 512", LIBREFRANKLIN_READER_18_FONT_ID, 512},
      {"LF 18pt @ 640", LIBREFRANKLIN_READER_18_FONT_ID, 640},
  };

  printf("%-16s %-10s %-12s %7s %7s %7s %7s %7s\n", "config", "align", "mode", "lines", "mean", "sd", "maxdev",
         "hyphen");
  for (const Cfg& c : cfgs) {
    for (const bool justify : {true, false}) {
      // Forcing the regime rather than letting autojustify pick, so the same
      // measure can be read both ways. A threshold of 0 justifies everything
      // and 255 nothing; neither is a device setting, both are legal inputs.
      const int threshold = justify ? 0 : 255;
      for (const uint8_t mode : {linebreak::STORED_HYPHENATED, linebreak::STORED_WHOLE_WORDS}) {
        std::vector<double> gaps, ends;
        int hy = 0;
        for (const auto& para : corpus) {
          layoutParagraph(para, c.fontId, c.measure, mode, CssTextAlign::Justify, threshold, gaps, ends, hy);
        }
        Stats st;
        st.accumulate(justify ? gaps : ends);
        printf("%-16s %-10s %-12s %7d %7.2f %7.2f %7.2f %7d\n", c.name, justify ? "justified" : "ragged",
               mode == linebreak::STORED_HYPHENATED ? "Hyphenated" : "WholeWords", st.lineCount, st.mean, st.stddev,
               st.maxAbsDeviation, hy);
      }
    }
  }
}
