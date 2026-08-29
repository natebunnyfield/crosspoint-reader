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
// THEN IT OVERTURNED ITS OWN STORY, 2026-08-26. The table above is not
// algorithm against algorithm: the stored byte couples the two, so what was
// compared was greedy WITH hyphens against total fit WITHOUT them, and
// hyphenation is exactly what lets the greedy breaker pack tight -- it flatters
// the number being measured. Worse, mean and standard deviation are the wrong
// summary for what total fit is FOR: Knuth-Plass cubes its badness so that one
// terrible line outweighs many slightly loose ones, and averaging hides
// precisely that. Three things came out of re-measuring it:
//
//   * THE 2x2 IS REACHABLE, three cells of it. Hyphenation is an independent
//     axis (HyphenationScope below), and held equal the DP wins where it is
//     supposed to: 4-15% better on the mean paragraph's loosest line in all six
//     configurations, 0-11% better at p95, while giving up 0.7-3.8% on the mean.
//     The fourth cell is not an algorithm and there is a test that says so.
//   * RIVERS were never measured. They are now, and they go FOR the shipped
//     default: a tighter setting has narrower gaps, narrower gaps overlap less.
//     The definition has to clear a ragged null or it measures the text.
//   * HYPHEN QUALITY, not hyphen count. The default runs 13-15% of lines
//     hyphenated with ladders of up to five in a row; that is the one thing the
//     new metrics find against it. Mind the denominator: a paragraph-final line
//     can never be hyphenated, and leaving it out inflates that figure to
//     15-17%. HyphenRuns reports both.
//
// THEN, 2026-08-27, IT SWEPT THE RAGGED GATE. `raggedSkipsHyphen` gives up on
// hyphenation once a ragged line has reached 70% of the measure, and sections 7
// and 8j both listed it as untouched. Swept 40..100 in single-point steps at
// 14 pt / 512 px -- the shipped default size at the X3's own measure, and the
// owner's scope for the run -- the curve has a live band of roughly 66..88 with
// dead plateaux either side, and 70 sits one point past the knee of the two
// strictest hole metrics. It KEPT 70; nothing about what the device draws
// changed. DISABLED_RaggedGateSweep is that instrument.
//
// The full tables are in docs/line-breaking-2026-08-25.md; DISABLED_Sweep and
// DISABLED_RaggedGateSweep at the foot of this file are the instruments that
// produced them.
//
// What the live tests pin is only what held in EVERY configuration:
//   * justified -- greedy sets tighter AND more evenly. Assert the direction,
//     so nobody re-labels the row on the survey's version of the story.
//   * ragged -- the two are close. Assert only that total-fit does not become
//     markedly WORSE, which is what a broken DP would look like.
//   * hyphens -- the actual trade, and the only large effect.
//   * cost -- total-fit is a small multiple of greedy, not a different order.
//   * the fourth cell -- handing the DP a trie changes nothing, measured.
//   * de-confounding -- hyphenation moves the page more than the algorithm.
//   * worst line -- at equal hyphenation the DP protects the loosest line.
//   * rivers -- the metric clears its ragged null, and hyphens reduce rivers.
//   * hyphen runs -- the run counter is wired to the same lines as the rest.
//   * the ragged gate -- it is 70, the x100 rewrite is exact at every width, it
//     cannot reach a justified page, and it DOES move a ragged one.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <algorithm>
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
    // 14 pt is CrossPointSettings::DEFAULT_FONT_POINT_SIZE -- the size a reader
    // who has changed nothing is looking at. Added 2026-08-27 for the ragged
    // gate sweep (section 9), which is measured at that size and nowhere else.
    renderer_.insertFont(LIBREFRANKLIN_READER_14_FONT_ID, lfReader14_);
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
  EpdFont lfr14R_{&librefranklin_reader_14_regular}, lfr14B_{&librefranklin_reader_14_bold},
      lfr14I_{&librefranklin_reader_14_italic}, lfr14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lfReader14_{&lfr14R_, &lfr14B_, &lfr14I_, &lfr14BI_};
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

// WHAT THIS CANNOT SEE, stated because the residue disclosure above names three
// mechanisms and this counter can only observe two. An apostrophe-contraction
// break emits requiresInsertedHyphen = false (Hyphenator.cpp), so the prefix
// ends in an apostrophe and no '-' is rendered -- such a split is invisible
// here by construction. It does not affect any number in the doc: this corpus
// contains zero apostrophe-boundary candidates and zero soft hyphens (checked),
// so its whole hyphen residue is explicit-hyphen compounds. A corpus with
// French or Italian elision would need a second predicate.
bool endsWithHyphen(const std::string& s) { return !s.empty() && s.back() == '-'; }

// ---------------------------------------------------------------------------
// One laid-out line, kept whole
// ---------------------------------------------------------------------------

// The 2026-08-25 sweep read two scalars per line straight into two flat vectors
// and threw the line away. That is why it could report a mean and a standard
// deviation and NOTHING about the worst line, and why rivers -- a defect that
// only exists BETWEEN lines -- could not be measured at all. Keeping the line's
// gap geometry, and keeping lines grouped by paragraph, is what the 2026-08-26
// metrics are built on. It changes no number the old columns reported: the
// means and deviations below are accumulated from the same samples in the same
// order.
struct LineRecord {
  // The line's inter-word gap, px; valid only when gapCount > 0. Called a MEAN
  // because it is computed as one, but on a justified line every gap is the
  // same width to the pixel -- measured over the whole specimen at both sizes
  // and in all three cells, the within-line spread is exactly 0.000 px. So
  // "the line's gap" and "the mean of its gaps" are the same number, and the
  // worst-line statistics below are order statistics over LINES, not a
  // second-order summary of a first-order one.
  double meanGap = 0.0;
  int gapCount = 0;
  double end = 0.0;         // right edge of the line, px
  bool hyphenated = false;  // the line's last token ends in '-'
  // The last line of its paragraph. RECORDED RATHER THAN DISCARDED, which is
  // the 2026-08-26 correction: a paragraph-final line is never justified and
  // has no break after it, so it says nothing about spacing and CANNOT be
  // hyphenated -- every geometry metric below excludes it, exactly as the
  // original loop did by stopping one short. But it is a real line on a real
  // page, so leaving it out of the HYPHEN DENSITY denominator inflated that
  // figure by its share (394 lines over this corpus, 8-17% relative). Density
  // is now reported both ways and the doc quotes the all-lines one.
  bool isFinal = false;
  // The horizontal SPAN of every measurable gap, px, in order. Rivers are
  // chains of these across consecutive lines, so they must be kept per line.
  // The span rather than the centre: the overlap definition below needs both
  // edges, and the centre is one subtraction away.
  std::vector<float> gapStart, gapEnd;
};

// Lines grouped by paragraph. Rivers and hyphen ladders are BOTH bounded by the
// paragraph -- a gap on the last line of one paragraph and a gap on the first
// line of the next are not a river, they are two paragraphs -- so the grouping
// is load-bearing rather than tidiness.
using ParagraphLines = std::vector<LineRecord>;
using Corpus = std::vector<ParagraphLines>;

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

// ---------------------------------------------------------------------------
// Hyphenation as an INDEPENDENT axis
// ---------------------------------------------------------------------------

// THE 2026-08-25 TABLE WAS NOT ALGORITHM AGAINST ALGORITHM AND SAID SO NOWHERE.
// The stored byte couples the two, so "Hyphenated" is greedy WITH hyphen points
// and "Whole Words" is total-fit WITHOUT them -- and hyphenation is precisely
// what lets the greedy breaker pack tight, which is the number that was
// compared. De-confounding it needs a second axis, and the axis already exists:
// `Hyphenator::cachedHyphenator_` is filled only by setPreferredLanguage, and
// only English and Spanish tries ship (LanguageRegistry.cpp). Point it at any
// other tag and it goes null, pattern breaks disappear, and the greedy breaker
// falls back to plain first fit.
//
// THAT CELL IS NOT SYNTHETIC. It is what a French, German or untagged EPUB
// renders as on a shipped device today, with the default stored byte -- the
// same code path, minus a trie. Hyphenator::setPreferredLanguage raises
// BookNotes::NoHyphenationForLanguage on exactly this transition.
//
// What does NOT go away is the residue both breakers keep, all three of it above
// the `if (hyphenator)` guard in Hyphenator::breakOffsets: an explicit '-' or
// soft hyphen already in the word (buildExplicitBreakInfos returns before the
// trie is consulted), an apostrophe contraction boundary
// (appendApostropheContractionBreaks, applied regardless), and the every-N-char
// fallback on a word too wide to fit a line alone. So "off" means "no
// opportunistic PATTERN hyphenation", the same caveat the row's label carries.
// Measured, that residue is 1.6-2.4% of lines against 13-15% with the trie --
// unclean in the direction that flatters the greedy cell, which still loses the
// worst-line comparison.
//
// AND IT IS NOT A PURE SUBTRACTION, which is worth knowing before reading any
// "off" arm as "the same thing minus hyphens". With no trie the fallback uses
// LiangWordConfig::kDefaultMinPrefix/Suffix = 2/2 (LiangHyphenation.h) where
// English's hyphenator declares 3/3 (LanguageRegistry.cpp), and
// hyphenateWordAtIndex takes the WIDEST fitting prefix -- so on an oversized
// word the no-trie arm has a strictly LARGER candidate set and can split where
// the trie arm cannot. That is half the mechanism behind the one 0.06% row in
// section 0 of the sweep. Found by adversarial review.
class HyphenationScope {
 public:
  explicit HyphenationScope(const bool on) {
    // Force the renderer singleton up FIRST. Its constructor calls
    // setPreferredLanguage("en") itself, so a first-ever Gfx::instance() from
    // inside a scope would silently undo the scope -- an "off" arm quietly
    // measured with the trie installed, which looks like a null result rather
    // than a broken one.
    (void)Gfx::instance();
    Hyphenator::setPreferredLanguage(on ? "en" : "zxx");
  }
  // NOT a full undo, and the name overpromises slightly: entering the "off"
  // state raises booknotes::Note::NoHyphenationForLanguage on the global ledger
  // (Hyphenator.cpp) and restoring the language does not clear it. Harmless
  // here -- nothing in layout branches on BookNotes, checked -- but a suite that
  // ever asserts on book notes must reset the ledger itself.
  ~HyphenationScope() { Hyphenator::setPreferredLanguage("en"); }
  HyphenationScope(const HyphenationScope&) = delete;
  HyphenationScope& operator=(const HyphenationScope&) = delete;
};

// The three reachable cells of the 2x2. The fourth -- total fit WITH hyphen
// points, which is what Knuth-Plass actually is -- cannot be reached from any
// input this harness controls: ParsedText::hyphenateWordAtIndex splits words[]
// in place and the DP indexes tables sized before its loop, so the DP can only
// ever see the hyphens its own oversized-word pre-pass has already committed.
// See lib/Epub/Epub/LineBreakMode.h. It is a build, not a flag, and this enum
// deliberately has no name for it rather than pretending a fourth row exists.
enum class Cell {
  GreedyHyphenated,   // the shipped default
  GreedyPlain,        // stored byte 1, no trie -- what an untagged book gets today
  TotalFitPlain,      // the shipped alternative
  TotalFitHyphenated  // stored byte 0 WITH the trie -- a reachable input that is not a fourth
                      // algorithm; section 0 of the sweep shows it collapsing onto TotalFitPlain
};

const char* cellName(const Cell c) {
  switch (c) {
    case Cell::GreedyHyphenated:
      return "greedy+hy";
    case Cell::GreedyPlain:
      return "greedy-hy";
    case Cell::TotalFitPlain:
      return "totalfit";
    case Cell::TotalFitHyphenated:
      return "totalfit+hy";
  }
  return "?";
}

uint8_t storedModeFor(const Cell c) {
  return (c == Cell::GreedyHyphenated || c == Cell::GreedyPlain) ? linebreak::STORED_HYPHENATED
                                                                 : linebreak::STORED_WHOLE_WORDS;
}

bool hyphenationOnFor(const Cell c) { return c == Cell::GreedyHyphenated || c == Cell::TotalFitHyphenated; }

// The three cells the tables report. TotalFitHyphenated is left out of them
// because section 0 of the sweep shows it IS TotalFitPlain to within a rounding
// error -- reporting it as a fourth row would imply a fourth algorithm.
const std::vector<Cell>& reachableCells() {
  static const std::vector<Cell> c = {Cell::GreedyHyphenated, Cell::GreedyPlain, Cell::TotalFitPlain};
  return c;
}

// ---------------------------------------------------------------------------
// Laying a paragraph out and reading its geometry back
// ---------------------------------------------------------------------------

// One paragraph laid out, with every line's geometry read back off the TextBlock
// the paginator would have baked. Appends ONE ParagraphLines entry to `out`
// (possibly empty, for a paragraph that fits on a single line). Every figure is
// in framebuffer pixels.
void layoutParagraph(const std::string& text, const int fontId, const uint16_t measure, const uint8_t storedMode,
                     const CssTextAlign align, const int justifyThresholdChars, Corpus& out) {
  BlockStyle style;
  style.alignment = align;
  ParsedText block(/*extraParagraphSpacing=*/false, linebreak::splitsWordsAtLineEnds(storedMode),
                   /*focusReadingEnabled=*/false, style);
  for (const auto& w : splitWords(text)) block.addWord(w, EpdFontFamily::REGULAR);

  std::vector<std::shared_ptr<TextBlock>> lines;
  block.layoutAndExtractLines(
      Gfx::instance().renderer(), fontId, measure,
      [&](const std::shared_ptr<TextBlock>& line) { lines.push_back(line); },
      /*includeLastLine=*/true, justifyThresholdChars);

  auto& r = Gfx::instance().renderer();
  out.emplace_back();
  ParagraphLines& para = out.back();
  for (size_t li = 0; li < lines.size(); ++li) {
    const TextBlock& line = *lines[li];
    if (line.wordCount() == 0) continue;
    // The last line is never justified, so its geometry says nothing about the
    // breaker. It is still RECORDED -- see LineRecord::isFinal -- and every
    // consumer below skips it except the density denominator.
    const bool isFinal = li + 1 == lines.size();

    // A ONE-WORD LINE IS SKIPPED FOR GAPS AND COUNTED FOR EVERYTHING ELSE. It
    // has no gap to measure, but it is a real and maximally short line end, and
    // the two breakers do not produce one-word lines at the same rate -- so
    // dropping it from endSamples would remove exactly the deepest rag from the
    // column that exists to measure the rag. An earlier version of this loop
    // `continue`d past the whole line and the published figures were wrong.
    LineRecord rec;
    double gapSum = 0.0;
    for (uint16_t w = 0; w + 1 < line.wordCount(); ++w) {
      const int adv = r.getTextAdvanceX(fontId, line.wordText(w), line.wordStyle(w));
      const double gapStart = static_cast<double>(line.wordXpos(w)) + adv;
      const double gap = static_cast<double>(line.wordXpos(w + 1)) - gapStart;
      // A continuation fragment attaches with no space at all (an em dash, a
      // no-break group). Counting those zeros would dilute both modes' means by
      // however many of them the line happens to hold, which is noise about the
      // text rather than signal about the breaker.
      if (gap < 1.0) continue;
      gapSum += gap;
      rec.gapCount++;
      rec.gapStart.push_back(static_cast<float>(gapStart));
      rec.gapEnd.push_back(static_cast<float>(gapStart + gap));
    }
    if (rec.gapCount > 0) rec.meanGap = gapSum / rec.gapCount;

    const uint16_t last = line.wordCount() - 1;
    const int lastAdv = r.getTextAdvanceX(fontId, line.wordText(last), line.wordStyle(last));
    rec.end = static_cast<double>(line.wordXpos(last) + lastAdv);
    rec.hyphenated = endsWithHyphen(line.wordText(last));
    rec.isFinal = isFinal;
    para.push_back(std::move(rec));
  }
}

// Lay a whole corpus out in one cell. The HyphenationScope is taken HERE rather
// than by the caller so no measurement can accidentally run under whatever trie
// the previous one left installed -- the failure mode that cost this suite its
// first wrong reading, in the other direction.
Corpus layoutCorpus(const std::vector<std::string>& corpus, const Cell cell, const CssTextAlign align, const int fontId,
                    const uint16_t measurePx, const int justifyThresholdChars) {
  const HyphenationScope hy(hyphenationOnFor(cell));
  Corpus out;
  out.reserve(corpus.size());
  for (const auto& p : corpus) {
    layoutParagraph(p, fontId, measurePx, storedModeFor(cell), align, justifyThresholdChars, out);
  }
  return out;
}

std::vector<double> gapSamplesOf(const Corpus& c) {
  std::vector<double> v;
  for (const auto& para : c)
    for (const auto& l : para)
      if (!l.isFinal && l.gapCount > 0) v.push_back(l.meanGap);
  return v;
}

std::vector<double> endSamplesOf(const Corpus& c) {
  std::vector<double> v;
  for (const auto& para : c)
    for (const auto& l : para)
      if (!l.isFinal) v.push_back(l.end);
  return v;
}

int hyphenatedLinesOf(const Corpus& c) {
  int n = 0;
  for (const auto& para : c)
    for (const auto& l : para)
      if (!l.isFinal && l.hyphenated) n++;
  return n;
}

// Lines the breaker had a choice about -- i.e. every line but each paragraph's
// last. This is the denominator for every SPACING statistic. Hyphen density
// needs the OTHER one, and gets it from HyphenRuns::allLines below.
int totalLinesOf(const Corpus& c) {
  int n = 0;
  for (const auto& para : c)
    for (const auto& l : para)
      if (!l.isFinal) n++;
  return n;
}

// Rivers are chains of GAPS, and the cells do not put the same number of gaps
// on a page: the tighter a breaker packs, the more words a line holds and the
// more chances a river has to start. Normalising per 1000 lines does not
// absorb that; per 1000 gaps does, and the two together bracket it.
int totalGapsOf(const Corpus& c) {
  int n = 0;
  for (const auto& para : c)
    for (const auto& l : para)
      if (!l.isFinal) n += l.gapCount;
  return n;
}

// ---------------------------------------------------------------------------
// B. Worst-line badness
// ---------------------------------------------------------------------------

// WHY THE MEAN AND THE SD ARE THE WRONG SUMMARY FOR THIS COMPARISON, and the
// reason this block exists. Total fit is not built to make the average line
// good; Knuth-Plass cubes its badness precisely so that ONE very loose line
// outweighs many slightly loose ones, because that is the line a reader sees.
// A mean and a standard deviation average exactly that away. These are the
// order statistics of the SAME per-line gap the old columns averaged -- over
// LINES, not a summary of a summary, because a justified line's gaps are all
// the same width to the pixel (see LineRecord::meanGap).
//
// WHAT THEY FOUND, held at equal hyphenation over 394 paragraphs: total fit
// gives up 0.7-3.8% on the average line and buys back 4.1-14.4% on the
// loosest line of a paragraph, winning that trade in all six configurations
// and 5 of 6 at p95 and p99. That is the Knuth-Plass bargain, invisible to a
// mean. Against the SHIPPED default -- which also has hyphens -- it still
// loses 6/6 on p95, p99 and meanParaMax. Both halves are the point.
struct Worst {
  double p95 = 0.0;  // 95th percentile of the per-line mean gap, px
  double p99 = 0.0;
  double max = 0.0;          // the single loosest line in the corpus, px
  double meanParaMax = 0.0;  // mean over paragraphs of that paragraph's loosest line, px
  // MEASURED AND FOUND REDUNDANT, kept only so the sweep's table 1 still prints
  // them. At every measure and size here the TYPICAL line already exceeds 2x
  // (the mean runs 1.79-5.46 space widths), so the threshold names no defect --
  // 39-57% of lines are "loose" at 12 pt / 512 px -- and the counts rank the
  // three cells identically to the mean in all six configurations. They are the
  // mean restated, not a tail statistic. p95, p99 and meanParaMax are the ones
  // that carry the worst-line argument.
  int loose2x = 0;  // lines set at more than 2x the font's natural word space
  int loose3x = 0;
  int lines = 0;
};

double percentile(std::vector<double> v, const double q) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const size_t idx = static_cast<size_t>(std::ceil(q * v.size()));
  return v[std::min(v.size(), std::max<size_t>(1, idx)) - 1];
}

Worst worstOf(const Corpus& c, const double naturalSpacePx) {
  Worst w;
  std::vector<double> gaps = gapSamplesOf(c);
  w.lines = static_cast<int>(gaps.size());
  w.p95 = percentile(gaps, 0.95);
  w.p99 = percentile(gaps, 0.99);
  for (const double g : gaps) {
    w.max = std::max(w.max, g);
    if (g > naturalSpacePx * 2.0) w.loose2x++;
    if (g > naturalSpacePx * 3.0) w.loose3x++;
  }
  int paras = 0;
  for (const auto& para : c) {
    double m = 0.0;
    bool any = false;
    for (const auto& l : para) {
      if (l.isFinal || l.gapCount == 0) continue;
      any = true;
      m = std::max(m, l.meanGap);
    }
    if (any) {
      w.meanParaMax += m;
      paras++;
    }
  }
  if (paras > 0) w.meanParaMax /= paras;
  return w;
}

// The font's own word space, used to express looseness as a MULTIPLE rather
// than a pixel count -- 12 pt and 18 pt cannot otherwise be read off one table.
// 'n' either side is an ordinary lowercase pair with no kerning special case.
double naturalSpacePx(const int fontId) {
  return Gfx::instance().renderer().getSpaceAdvance(fontId, 'n', 'n', EpdFontFamily::REGULAR);
}

// ---------------------------------------------------------------------------
// C. Rivers
// ---------------------------------------------------------------------------

// THE DEFINITION, stated here because there is no prior art for it in this repo
// and a river metric is easy to define into whatever answer you want. TWO
// definitions are implemented, and the first one is here because it FAILED --
// keeping it is cheaper than the next pass rediscovering that it fails.
//
// Common to both: a river is a maximal chain of gaps on CONSECUTIVE lines of
// the SAME paragraph, spanning THREE OR MORE lines, where each consecutive pair
// is LINKED. Three choices in that, each with a reason:
//
//   * THREE lines minimum. Two aligned gaps happen constantly by chance and
//     nobody calls that a river; three is the threshold the typography manuals
//     use.
//   * WITHIN A PARAGRAPH. A chain crossing a paragraph boundary is not one
//     stripe, and the first line of a paragraph is indented besides.
//   * IN SPACE WIDTHS, not pixels, so one number means the same thing at 12 pt
//     and at 18 pt. Both are swept, because a single hand-picked value is
//     exactly how this metric gets cooked.
//
// Counting is by ENDPOINT: a chain is counted where it stops, i.e. at a gap of
// chain length >= 3 that nothing on the next line links to. A gap that two gaps
// on the next line both link to is the head of two rivers and is counted twice,
// which is the right answer -- a forking stripe is two stripes.
//
// LINKAGE 1, CENTRES (riversByCentre): linked when the two gaps' horizontal
// centres lie within `tol`. This is the obvious definition and it is WRONG for
// this comparison, for a reason that only shows up against a null. A justified
// page's gaps run 2-5x the natural word space; a ragged page's are exactly one.
// A tolerance quoted in natural spaces is therefore a far looser RELATIVE
// tolerance on the ragged page, so the null comes out at or above the
// justified rate and the metric reports that justification does not cause
// rivers -- which is false, and is an artifact of the yardstick.
//
// LINKAGE 2, OVERLAP (riversByOverlap): linked when the two gaps' horizontal
// SPANS overlap by at least `minOverlap`. This is what the eye is actually
// doing -- a river is visible when there is a column of white running through
// consecutive lines -- and it has the property the centre rule lacks: a wider
// gap can overlap more, so a loosely set line is genuinely more river-prone,
// which is the whole reason justified text has rivers and ragged text does not.
// It is the one the doc reports.
//
// BE PRECISE ABOUT WHAT THE NULL PROVES, because it is less than it looks and
// adversarial review had to derive it. On a ragged page every gap is exactly
// one space w, so two gaps overlap by >= t*w IFF their centres differ by
// <= (1-t)*w -- on the null the two linkages are THE SAME METRIC with t
// reversed, and at t = 0.50 they return the identical number (the sweep shows
// 6.79/3.96/1.64 against 1.64/3.96/6.79). The whole difference is on the
// JUSTIFIED side, where half a space of overlap corresponds to a centre window
// of w - 0.5 ~ 1.6-5.0 spaces. So the ratio is a sanity check that the metric
// fires on justified text and not on the null; it is not evidence that the
// linkage discovered anything the mean gap did not. It did not -- r^2 = 0.95.
struct Rivers {
  int count = 0;    // maximal chains of length >= 3
  int longest = 0;  // longest chain seen, in lines
};

// Shared chain walk. `linked(prevIdx, curIdx)` decides one pair.
template <typename Linked>
Rivers riversWith(const Corpus& c, const Linked& linked) {
  Rivers r;
  std::vector<int> prevLen, curLen;
  std::vector<bool> prevExtended;
  const LineRecord* prevLine = nullptr;
  for (const auto& para : c) {
    prevLen.clear();
    prevExtended.clear();
    prevLine = nullptr;
    for (const auto& line : para) {
      // A paragraph-final line is not justified, so its gaps are one word space
      // and belong to no river. Skipping it here rather than filtering upstream
      // keeps the chain break at the paragraph boundary where it belongs.
      if (line.isFinal) continue;
      curLen.assign(line.gapStart.size(), 1);
      for (size_t g = 0; g < line.gapStart.size(); ++g) {
        for (size_t p = 0; p < prevLen.size(); ++p) {
          if (!linked(*prevLine, p, line, g)) continue;
          prevExtended[p] = true;
          curLen[g] = std::max(curLen[g], prevLen[p] + 1);
        }
        r.longest = std::max(r.longest, curLen[g]);
      }
      // Anything on the previous line that nothing here continued is an end.
      for (size_t p = 0; p < prevLen.size(); ++p) {
        if (!prevExtended[p] && prevLen[p] >= 3) r.count++;
      }
      prevLen = curLen;
      prevLine = &line;
      prevExtended.assign(prevLen.size(), false);
    }
    for (size_t p = 0; p < prevLen.size(); ++p) {
      if (!prevExtended[p] && prevLen[p] >= 3) r.count++;
    }
  }
  return r;
}

Rivers riversByCentre(const Corpus& c, const double tolPx) {
  return riversWith(c, [tolPx](const LineRecord& a, const size_t i, const LineRecord& b, const size_t j) {
    const double ca = 0.5 * (a.gapStart[i] + a.gapEnd[i]);
    const double cb = 0.5 * (b.gapStart[j] + b.gapEnd[j]);
    return std::abs(ca - cb) <= tolPx;
  });
}

Rivers riversByOverlap(const Corpus& c, const double minOverlapPx) {
  return riversWith(c, [minOverlapPx](const LineRecord& a, const size_t i, const LineRecord& b, const size_t j) {
    const double lo = std::max(a.gapStart[i], b.gapStart[j]);
    const double hi = std::min(a.gapEnd[i], b.gapEnd[j]);
    return (hi - lo) >= minOverlapPx;
  });
}

// ---------------------------------------------------------------------------
// D. Hyphen quality, not hyphen count
// ---------------------------------------------------------------------------

// 489 hyphenated lines against 33 was published as the headline difference and
// says nothing about whether those 489 are well behaved. The typographic limit
// is two hyphenated lines in a row; three is a LADDER and is the defect a
// reader notices, in the same way a river is. Runs are bounded by the
// paragraph, for the same reason rivers are.
//
// MEASURED (394 paragraphs, six configurations): the shipped default runs
// 13.0-15.2% of ALL lines hyphenated, with 6-19 ladders and a longest run of
// 4-5; both whole-word cells never exceed ONE ladder. So the density is high
// and the runs are well behaved -- one ladder every 313-731 lines. This is the
// only one of the 2026-08-26 metrics that finds against the shipped default,
// and what it finds is small.
struct HyphenRuns {
  int hyphenatedLines = 0;
  int allLines = 0;        // every line on the page
  int breakableLines = 0;  // every line but each paragraph's last
  int runs2 = 0;           // runs of exactly 2 -- allowed, reported for scale
  int runs3plus = 0;       // ladders
  int longest = 0;
  double densityPct = 0.0;             // hyphenated lines as a % of ALL lines -- what a page shows
  double densityOfBreakablePct = 0.0;  // ... as a % of the lines that could be hyphenated
};

// TWO DENOMINATORS, AND THE DOC QUOTES THE FIRST. A paragraph-final line has no
// break after it, so it can never end in a hyphen -- it is structurally always
// a zero. Dividing by breakable lines only answers "of the lines the breaker
// had a choice about, how many did it split", which is the right question about
// the ALGORITHM; dividing by all lines answers "how much of what I see is
// hyphens", which is the right question about the PAGE, and is the claim the
// doc makes. Over this corpus the two differ by 8-17% relative (394 final lines
// against ~3,000-6,500), and the first version of this metric reported the
// breakable figure under the all-lines wording. Found by adversarial review.
HyphenRuns hyphenRunsOf(const Corpus& c) {
  HyphenRuns h;
  for (const auto& para : c) {
    int run = 0;
    for (const auto& l : para) {
      h.allLines++;
      if (l.isFinal) continue;  // counted on the page, never a hyphen candidate
      h.breakableLines++;
      if (l.hyphenated) {
        h.hyphenatedLines++;
        run++;
        h.longest = std::max(h.longest, run);
      } else {
        if (run == 2) h.runs2++;
        if (run >= 3) h.runs3plus++;
        run = 0;
      }
    }
    if (run == 2) h.runs2++;
    if (run >= 3) h.runs3plus++;
  }
  if (h.allLines > 0) h.densityPct = 100.0 * h.hyphenatedLines / h.allLines;
  if (h.breakableLines > 0) h.densityOfBreakablePct = 100.0 * h.hyphenatedLines / h.breakableLines;
  return h;
}

// ---------------------------------------------------------------------------

// The four pre-2026-08-26 tests go through here, and it must reproduce what they
// measured before the 2x2 existed -- which means BOTH arms run with the English
// trie installed, because that is what the Gfx constructor leaves in place and
// the stored byte was the only thing those tests varied. Mapping WHOLE_WORDS to
// TotalFitPlain instead would quietly move the DP's arm onto a different
// oversized-word pre-pass (LiangWordConfig's 2/2 minimum instead of English's
// 3/3) -- no measurable difference on this corpus, but a behavior change the
// old tests never asked for. Adversarial review caught the first version doing
// exactly that.
Stats measure(const uint8_t storedMode, const CssTextAlign align, const int fontId, const uint16_t measurePx,
              const int justifyThresholdChars, const bool useGaps) {
  const Cell cell = storedMode == linebreak::STORED_HYPHENATED ? Cell::GreedyHyphenated : Cell::TotalFitHyphenated;
  const Corpus c = layoutCorpus(paragraphs(), cell, align, fontId, measurePx, justifyThresholdChars);
  Stats s;
  s.accumulate(useGaps ? gapSamplesOf(c) : endSamplesOf(c));
  s.hyphenatedLines = hyphenatedLinesOf(c);
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
  //
  // BE PRECISE ABOUT WHAT THIS TEST DOES AND DOES NOT SAY, which is what the
  // 2026-08-26 de-confounding added. It compares two SETTINGS, and hyphenation
  // is 4.5-35x the larger of the two things they differ by. It is NOT a claim
  // that the greedy ALGORITHM is the better one -- held at equal hyphenation
  // the DP wins the worst line in all six, which is what
  // AtEqualHyphenationTotalFitProtectsTheWorstLine pins. Both are true and the
  // row still ships greedy, because the hyphens are worth more than the
  // optimizer.
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
              Gfx::instance().renderer(), kRaggedFont, kMeasure, [&seen](const std::shared_ptr<TextBlock>&) { seen++; },
              /*includeLastLine=*/true, autojustify::THRESHOLD_CHARS);
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
// The 2026-08-26 metrics: de-confounding, worst line, rivers, hyphen runs
// ---------------------------------------------------------------------------

// Helper for the four tests below: lay the embedded specimen out in one cell at
// one size, justified, and hand back everything the new metrics need.
struct CellResult {
  Stats stats;
  Worst worst;
  HyphenRuns hyphens;
  double riversPer1000Gaps = 0.0;
  double raggedRiversPer1000Gaps = 0.0;
  int gaps = 0;
};

CellResult evaluate(const Cell cell, const int fontId) {
  const double space = naturalSpacePx(fontId);
  const Corpus just = layoutCorpus(paragraphs(), cell, CssTextAlign::Justify, fontId, kMeasure, /*threshold=*/0);
  const Corpus rag = layoutCorpus(paragraphs(), cell, CssTextAlign::Justify, fontId, kMeasure, /*threshold=*/255);
  CellResult out;
  out.stats.accumulate(gapSamplesOf(just));
  out.stats.hyphenatedLines = hyphenatedLinesOf(just);
  out.worst = worstOf(just, space);
  out.hyphens = hyphenRunsOf(just);
  out.gaps = totalGapsOf(just);
  const int ragGaps = totalGapsOf(rag);
  if (out.gaps > 0) out.riversPer1000Gaps = 1000.0 * riversByOverlap(just, 0.5 * space).count / out.gaps;
  if (ragGaps > 0) out.raggedRiversPer1000Gaps = 1000.0 * riversByOverlap(rag, 0.5 * space).count / ragGaps;
  return out;
}

// THERE IS NO FOURTH CELL, and this proves it by measurement rather than by
// reading the code. Handing the DP an English trie is a legal input, so "total
// fit WITH hyphenation" looks reachable; it is not an algorithm, because the DP
// can only ever see the hyphens its own oversized-word pre-pass already
// committed. If that reasoning is right the two runs must land on the same
// page, and they do -- same lines, same hyphens, same spacing.
//
// The day somebody implements eager splitting plus a hyphen penalty
// (LineBreakMode.h), this test fails, and that failure is the announcement.
TEST(LineBreakQuality, TheDpCannotUseHyphenPointsSoThereIsNoFourthCell) {
  for (const int fontId : {kJustifiedFont, kRaggedFont}) {
    const CellResult plain = evaluate(Cell::TotalFitPlain, fontId);
    const CellResult withTrie = evaluate(Cell::TotalFitHyphenated, fontId);

    // WITHOUT THIS THE TEST PROVES NOTHING. Two identical rows are also what a
    // dead hyphenation axis produces, and a dead axis is this suite's oldest
    // failure mode. The same scope the withTrie arm ran under must be shown to
    // move a breaker that CAN use it. Validated by mutation, 2026-08-26:
    // stubbing hyphenationOnFor to false fails here rather than passing.
    ASSERT_GT(evaluate(Cell::GreedyHyphenated, fontId).stats.hyphenatedLines,
              evaluate(Cell::GreedyPlain, fontId).stats.hyphenatedLines * 2)
        << "the hyphenation axis is not live, so the rows below are identical for the wrong reason";

    EXPECT_EQ(plain.stats.lineCount, withTrie.stats.lineCount) << "the trie moved a DP line break";
    EXPECT_EQ(plain.stats.hyphenatedLines, withTrie.stats.hyphenatedLines)
        << "the trie changed how often the DP splits";
    // Not an exact equality: the pre-pass on an oversized word may split it at a
    // PATTERN point instead of a fallback point, which moves that one line's
    // geometry without changing whether a hyphen was taken. Measured over 394
    // paragraphs that is 0.06% of the mean in one of six configurations and
    // nothing in the other five.
    EXPECT_NEAR(plain.stats.mean, withTrie.stats.mean, plain.stats.mean * 0.01);
  }
}

// THE 2026-08-25 TABLE COMPARED TWO SETTINGS, NOT TWO ALGORITHMS, and this is
// the measurement that says so. greedy+hy against greedy-hy is the SAME breaker
// with the trie taken away; greedy-hy against totalfit is the algorithm change
// with hyphenation held off on both sides. The first moves the page far more
// than the second, so a comparison that varies both at once is a report about
// hyphenation wearing an algorithm's name.
TEST(LineBreakQuality, HyphenationMovesThePageMoreThanTheAlgorithmDoes) {
  const CellResult greedyHy = evaluate(Cell::GreedyHyphenated, kJustifiedFont);
  const CellResult greedyPlain = evaluate(Cell::GreedyPlain, kJustifiedFont);
  const CellResult totalFit = evaluate(Cell::TotalFitPlain, kJustifiedFont);

  const double hyphenationEffect = std::abs(greedyHy.stats.mean - greedyPlain.stats.mean);
  const double algorithmEffect = std::abs(greedyPlain.stats.mean - totalFit.stats.mean);

  printf(
      "[de-confounded, %u px measure, justified]\n"
      "  greedy +hyphens  mean %6.2f px  p95 %6.2f  paraWorst %6.2f  hyphenated %d\n"
      "  greedy -hyphens  mean %6.2f px  p95 %6.2f  paraWorst %6.2f  hyphenated %d\n"
      "  total fit        mean %6.2f px  p95 %6.2f  paraWorst %6.2f  hyphenated %d\n"
      "  hyphenation moves the mean %.2f px, the algorithm %.2f px\n",
      kMeasure, greedyHy.stats.mean, greedyHy.worst.p95, greedyHy.worst.meanParaMax, greedyHy.stats.hyphenatedLines,
      greedyPlain.stats.mean, greedyPlain.worst.p95, greedyPlain.worst.meanParaMax, greedyPlain.stats.hyphenatedLines,
      totalFit.stats.mean, totalFit.worst.p95, totalFit.worst.meanParaMax, totalFit.stats.hyphenatedLines,
      hyphenationEffect, algorithmEffect);

  // The axis has to actually move, or every cell below is measuring one thing
  // twice -- the failure mode that cost this suite its first wrong reading, in
  // the other direction (Hyphenator::cachedHyphenator_ left null).
  ASSERT_GT(greedyHy.stats.hyphenatedLines, 0) << "the trie is not installed -- greedy+hy hyphenated nothing";
  EXPECT_LT(greedyPlain.stats.hyphenatedLines, greedyHy.stats.hyphenatedLines / 2)
      << "clearing the trie did not stop the greedy breaker hyphenating";

  EXPECT_GT(hyphenationEffect, algorithmEffect * 3.0)
      << "the coupled comparison is no longer dominated by hyphenation -- the 2026-08-26 de-confounding may need "
         "redoing";
}

// WHAT TOTAL FIT IS ACTUALLY FOR, and the metric the 2026-08-25 table could not
// see. Knuth-Plass cubes its badness so that ONE very loose line outweighs many
// slightly loose ones; a mean and a standard deviation average exactly that
// away. Held at equal hyphenation, the DP gives up a little on the average line
// and buys back the worst one -- 4-15% on the mean paragraph's loosest line
// across all six sweep configurations, and it never loses.
TEST(LineBreakQuality, AtEqualHyphenationTotalFitProtectsTheWorstLine) {
  for (const int fontId : {kJustifiedFont, kRaggedFont}) {
    const CellResult greedyPlain = evaluate(Cell::GreedyPlain, fontId);
    const CellResult totalFit = evaluate(Cell::TotalFitPlain, fontId);

    EXPECT_LT(totalFit.worst.meanParaMax, greedyPlain.worst.meanParaMax)
        << "total fit stopped protecting the loosest line of a paragraph -- it is no longer optimizing total fit";
    EXPECT_LE(totalFit.worst.p95, greedyPlain.worst.p95) << "total fit's 95th-percentile line got worse than greedy's";
  }
}

// THE RIVER METRIC HAS TO FIRE ON JUSTIFIED TEXT AND NOT ON THE NULL, and
// pinning that is the point of this test. A ragged page sets every gap at
// exactly one word space, so no gap position there owes anything to the
// breaker; whatever alignment survives is the text's and chance's. The overlap
// definition beats that floor by 6.7-21x. The centre definition -- the obvious
// one, tried first -- does not, because a fixed window quoted in natural spaces
// is a far looser RELATIVE window on a ragged page than on a justified one. If
// someone swaps the linkage back, this fails. See the note on riversByOverlap
// for why that ratio is a sanity check rather than a validation of the model.
TEST(LineBreakQuality, TheRiverMetricClearsItsRaggedNull) {
  const CellResult cells[] = {evaluate(Cell::GreedyHyphenated, kRaggedFont), evaluate(Cell::GreedyPlain, kRaggedFont),
                              evaluate(Cell::TotalFitPlain, kRaggedFont)};
  for (const CellResult& c : cells) {
    EXPECT_GT(c.riversPer1000Gaps, 10.0) << "the justified page has almost no rivers -- the metric is not firing";
    EXPECT_GT(c.riversPer1000Gaps, c.raggedRiversPer1000Gaps * 3.0)
        << "the river rate no longer separates a justified page from a ragged one, so it is measuring the text and "
           "not the setting";
  }

  // And the ranking, which is stable across the whole 0.25-1.5 tolerance sweep
  // and all six configurations: a tighter setting has narrower gaps, narrower
  // gaps overlap less, so hyphenation REDUCES rivers. This is the one new
  // metric that could have gone against the shipped default and does not.
  EXPECT_LT(cells[0].riversPer1000Gaps, cells[1].riversPer1000Gaps)
      << "hyphenation stopped reducing rivers relative to the same breaker without it";
  EXPECT_LT(cells[0].riversPer1000Gaps, cells[2].riversPer1000Gaps);
}

// HYPHEN QUALITY, NOT HYPHEN COUNT. "489 against 33" was published as the
// headline difference and says nothing about whether those 489 are well
// behaved. The typographic limit is two hyphenated lines in a row; three is a
// LADDER. Over the corpus the shipped default runs 15-17% of lines hyphenated
// with 6-19 ladders and a longest run of 4-5, while both whole-word cells never
// exceed one ladder. The embedded specimen is too small to hold a ladder at
// all, so what is pinned here is the DENSITY -- the part that does show at this
// scale -- and the fact that the run machinery is wired to the same lines the
// rest of the suite measures.
TEST(LineBreakQuality, HyphenRunsAreCountedAndTheDefaultsDensityIsHigh) {
  const CellResult greedyHy = evaluate(Cell::GreedyHyphenated, kJustifiedFont);
  const CellResult totalFit = evaluate(Cell::TotalFitPlain, kJustifiedFont);

  printf(
      "[hyphen quality, %u px measure, justified]\n"
      "  greedy +hyphens  %d of %d lines hyphenated (%.1f%%)  runs of 2 %d  ladders (3+) %d  longest %d\n"
      "  total fit        %d of %d lines hyphenated (%.1f%%)  runs of 2 %d  ladders (3+) %d  longest %d\n",
      kMeasure, greedyHy.hyphens.hyphenatedLines, greedyHy.hyphens.allLines, greedyHy.hyphens.densityPct,
      greedyHy.hyphens.runs2, greedyHy.hyphens.runs3plus, greedyHy.hyphens.longest, totalFit.hyphens.hyphenatedLines,
      totalFit.hyphens.allLines, totalFit.hyphens.densityPct, totalFit.hyphens.runs2, totalFit.hyphens.runs3plus,
      totalFit.hyphens.longest);

  EXPECT_GT(greedyHy.hyphens.densityPct, 5.0) << "the specimen cannot show the trade the row is selling";
  EXPECT_LT(totalFit.hyphens.densityPct, greedyHy.hyphens.densityPct / 4.0);

  // The all-lines denominator must be the larger one, by exactly the number of
  // paragraphs that laid out. If these ever coincide, the final line stopped
  // being recorded and the density is back to the inflated figure.
  EXPECT_GT(greedyHy.hyphens.allLines, greedyHy.hyphens.breakableLines);
  EXPECT_LT(greedyHy.hyphens.densityPct, greedyHy.hyphens.densityOfBreakablePct);
}

// ---------------------------------------------------------------------------
// The counters, against answers worked out by hand
// ---------------------------------------------------------------------------

// THE TWO ASSERTIONS THAT USED TO STAND HERE WERE TAUTOLOGIES, and adversarial
// review proved it by mutation: deleting `run = 0;` from hyphenRunsOf and
// feeding it an alternating 1,0,1,0,... paragraph makes it report longest = 4
// and runs3plus = 3 where the truth is 1 and 0 -- and BOTH old assertions
// ("longest <= hyphenatedLines", "no ladder without three hyphens") still
// passed. They were green on precisely the bug their comment named, because
// neither can fail for any implementation that only increments `run` on a
// hyphenated line.
//
// A counter can only be checked against an answer computed a different way. So
// these are hand-built corpora with the answers worked out on paper, including
// the exact alternating case the mutation survives. Nothing here touches the
// layout engine; it is arithmetic about the bookkeeping.
namespace synth {

LineRecord line(const bool hyphenated, const std::vector<std::pair<float, float>>& gaps, const bool isFinal = false) {
  LineRecord r;
  r.hyphenated = hyphenated;
  r.isFinal = isFinal;
  for (const auto& g : gaps) {
    r.gapStart.push_back(g.first);
    r.gapEnd.push_back(g.second);
    r.gapCount++;
  }
  return r;
}

// One paragraph from a string of flags: '1' hyphenated, '0' not. The last
// character becomes the paragraph-final line when `withFinal` is set.
ParagraphLines paragraph(const std::string& flags, const bool withFinal) {
  ParagraphLines p;
  for (size_t i = 0; i < flags.size(); ++i) {
    p.push_back(line(flags[i] == '1', {}, withFinal && i + 1 == flags.size()));
  }
  return p;
}

}  // namespace synth

TEST(LineBreakQuality, HyphenRunCountingMatchesHandWorkedAnswers) {
  // The mutation case. Four hyphenated lines, never two in a row: no runs at
  // all, longest run 1. A counter that fails to reset says longest 4, 3 ladders.
  {
    const Corpus c = {synth::paragraph("10101010", false)};
    const HyphenRuns h = hyphenRunsOf(c);
    EXPECT_EQ(h.hyphenatedLines, 4);
    EXPECT_EQ(h.longest, 1) << "the run counter is not resetting on an un-hyphenated line";
    EXPECT_EQ(h.runs2, 0);
    EXPECT_EQ(h.runs3plus, 0);
  }
  // One pair, one ladder of three, and a single. 6 hyphenated lines.
  {
    const Corpus c = {synth::paragraph("11011101", false)};
    const HyphenRuns h = hyphenRunsOf(c);
    EXPECT_EQ(h.hyphenatedLines, 6);
    EXPECT_EQ(h.runs2, 1);
    EXPECT_EQ(h.runs3plus, 1);
    EXPECT_EQ(h.longest, 3);
  }
  // A run ending exactly at the paragraph's end is still flushed.
  {
    const Corpus c = {synth::paragraph("00111", false)};
    const HyphenRuns h = hyphenRunsOf(c);
    EXPECT_EQ(h.runs3plus, 1);
    EXPECT_EQ(h.longest, 3);
  }
  // RUNS DO NOT CROSS A PARAGRAPH BOUNDARY. Two paragraphs, each ending and
  // beginning mid-run: 2 + 2, never a ladder of four.
  {
    const Corpus c = {synth::paragraph("0011", false), synth::paragraph("1100", false)};
    const HyphenRuns h = hyphenRunsOf(c);
    EXPECT_EQ(h.hyphenatedLines, 4);
    EXPECT_EQ(h.runs2, 2);
    EXPECT_EQ(h.runs3plus, 0) << "a run joined across a paragraph break";
    EXPECT_EQ(h.longest, 2);
  }
  // The two denominators. Five lines, the last of them paragraph-final: it is
  // on the page but was never a hyphen candidate.
  {
    const Corpus c = {synth::paragraph("11000", true)};
    const HyphenRuns h = hyphenRunsOf(c);
    EXPECT_EQ(h.allLines, 5);
    EXPECT_EQ(h.breakableLines, 4);
    EXPECT_EQ(h.hyphenatedLines, 2);
    EXPECT_DOUBLE_EQ(h.densityPct, 40.0);
    EXPECT_DOUBLE_EQ(h.densityOfBreakablePct, 50.0);
  }
}

TEST(LineBreakQuality, RiverChainCountingMatchesHandWorkedAnswers) {
  const double tol = 1.0;  // require 1 px of overlap
  auto g = [](float a, float b) { return std::make_pair(a, b); };

  // Three lines, one column of white through all of them: ONE river of 3.
  {
    const Corpus c = {
        {synth::line(false, {g(10, 20)}), synth::line(false, {g(11, 21)}), synth::line(false, {g(12, 22)})}};
    const Rivers r = riversByOverlap(c, tol);
    EXPECT_EQ(r.count, 1);
    EXPECT_EQ(r.longest, 3);
  }
  // Two lines is not a river.
  {
    const Corpus c = {{synth::line(false, {g(10, 20)}), synth::line(false, {g(11, 21)})}};
    EXPECT_EQ(riversByOverlap(c, tol).count, 0);
  }
  // Five aligned lines are ONE river, not three.
  {
    ParagraphLines p;
    for (int i = 0; i < 5; ++i) p.push_back(synth::line(false, {g(10.0f + i, 20.0f + i)}));
    const Rivers r = riversByOverlap({p}, tol);
    EXPECT_EQ(r.count, 1) << "a long chain is being counted once per link";
    EXPECT_EQ(r.longest, 5);
  }
  // A ONE-WORD LINE (no gaps) breaks the chain, and the halves are not rejoined.
  {
    const Corpus c = {{synth::line(false, {g(10, 20)}), synth::line(false, {g(11, 21)}), synth::line(false, {}),
                       synth::line(false, {g(10, 20)}), synth::line(false, {g(11, 21)})}};
    EXPECT_EQ(riversByOverlap(c, tol).count, 0) << "a chain bridged a line that has no gaps";
  }
  // A PARAGRAPH BOUNDARY breaks it too: two 2-chains, not one 4-chain.
  {
    const ParagraphLines a = {synth::line(false, {g(10, 20)}), synth::line(false, {g(11, 21)})};
    EXPECT_EQ(riversByOverlap({a, a}, tol).count, 0) << "a chain joined across a paragraph break";
  }
  // A FORK is two stripes, which is the documented intent.
  {
    const Corpus c = {
        {synth::line(false, {g(10, 40)}), synth::line(false, {g(10, 40)}), synth::line(false, {g(10, 20), g(30, 40)})}};
    const Rivers r = riversByOverlap(c, tol);
    EXPECT_EQ(r.count, 2) << "a forking river is one stripe, or the endpoint rule changed";
    EXPECT_EQ(r.longest, 3);
  }
  // Two separate rivers in one paragraph are two.
  {
    const Corpus c = {{synth::line(false, {g(10, 20), g(100, 110)}), synth::line(false, {g(10, 20), g(100, 110)}),
                       synth::line(false, {g(10, 20), g(100, 110)})}};
    EXPECT_EQ(riversByOverlap(c, tol).count, 2);
  }
  // THE OVERLAP THRESHOLD BITES. Gaps that touch but barely overlap link at a
  // 1 px requirement and not at 5 px -- which is what the tolerance sweep is
  // sweeping, so it has to actually do something.
  {
    const Corpus c = {
        {synth::line(false, {g(10, 20)}), synth::line(false, {g(18, 28)}), synth::line(false, {g(26, 36)})}};
    EXPECT_EQ(riversByOverlap(c, 1.0).count, 1);
    EXPECT_EQ(riversByOverlap(c, 5.0).count, 0) << "the overlap threshold is not being applied";
  }
  // A paragraph-final line is not part of any river.
  {
    const Corpus c = {{synth::line(false, {g(10, 20)}), synth::line(false, {g(11, 21)}),
                       synth::line(false, {g(12, 22)}, /*isFinal=*/true)}};
    EXPECT_EQ(riversByOverlap(c, tol).count, 0) << "a paragraph-final line was counted into a river";
  }
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
  size_t words = 0;
  for (const auto& p : corpus) words += splitWords(p).size();
  printf("corpus: %zu paragraphs, %zu words\n", corpus.size(), words);

  struct Cfg {
    const char* name;
    int fontId;
    uint16_t measure;
  };
  const Cfg cfgs[] = {
      {"LF 12pt @ 400", LIBREFRANKLIN_READER_12_FONT_ID, 400}, {"LF 12pt @ 512", LIBREFRANKLIN_READER_12_FONT_ID, 512},
      {"LF 12pt @ 640", LIBREFRANKLIN_READER_12_FONT_ID, 640}, {"LF 18pt @ 400", LIBREFRANKLIN_READER_18_FONT_ID, 400},
      {"LF 18pt @ 512", LIBREFRANKLIN_READER_18_FONT_ID, 512}, {"LF 18pt @ 640", LIBREFRANKLIN_READER_18_FONT_ID, 640},
  };

  // The river tolerance, in SPACE WIDTHS. Swept rather than picked: this is the
  // one metric here with no prior art in the repo, and a single hand-chosen
  // tolerance is how such a metric gets cooked. If the ranking is not stable
  // across this range, the metric does not support a ranking and the doc must
  // say so.
  const double kTolSweep[] = {0.25, 0.5, 0.75, 1.0, 1.5};
  constexpr double kTolReported = 0.5;  // the row printed in the main table

  // -------------------------------------------------------------------------
  // -------------------------------------------------------------------------
  // THE FOURTH CELL, settled with a measurement rather than only from the code.
  //
  // Handing the DP an English trie is a legal input, so "total fit WITH
  // hyphenation" LOOKS reachable. It is not an algorithm: the DP can only ever
  // see hyphens its own oversized-word pre-pass already committed, so the trie
  // changes WHERE such a word splits and never WHETHER the optimizer takes a
  // hyphen. If that is right the two runs must land on the same page. This
  // prints the difference so the claim is evidence and not an argument.
  printf("\n=== 0. Does the trie do anything for the DP? ===\n");
  printf("If the fourth cell existed, these two rows would differ.\n\n");
  printf("%-14s %-14s %6s %8s %8s %8s %6s\n", "config", "cell", "lines", "mean", "sd", "p95", "hy");
  for (const Cfg& c : cfgs) {
    const double space = naturalSpacePx(c.fontId);
    for (const Cell cell : {Cell::TotalFitPlain, Cell::TotalFitHyphenated}) {
      const Corpus laid = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, 0);
      Stats st;
      st.accumulate(gapSamplesOf(laid));
      const Worst w = worstOf(laid, space);
      printf("%-14s %-14s %6d %8.3f %8.3f %8.3f %6d\n", c.name, cellName(cell), st.lineCount, st.mean, st.stddev, w.p95,
             hyphenatedLinesOf(laid));
    }
  }

  printf("\n=== 1. The 2x2, and the metrics per cell ===\n");
  printf("(totalfit+hy is structurally unreachable -- see LineBreakMode.h)\n\n");
  printf("%-14s %-10s %-10s %6s %7s %7s %7s %7s %7s %6s %6s %6s %6s %6s %6s\n", "config", "align", "cell", "lines",
         "mean", "sd", "p95", "p99", "max", "lo2x", "lo3x", "hy", "hy3+", "hyMax", "riv");
  for (const Cfg& c : cfgs) {
    const double space = naturalSpacePx(c.fontId);
    for (const bool justify : {true, false}) {
      // Forcing the regime rather than letting autojustify pick, so the same
      // measure can be read both ways. A threshold of 0 justifies everything
      // and 255 nothing; neither is a device setting, both are legal inputs.
      const int threshold = justify ? 0 : 255;
      for (const Cell cell : reachableCells()) {
        const Corpus laid = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, threshold);
        Stats st;
        st.accumulate(justify ? gapSamplesOf(laid) : endSamplesOf(laid));
        const Worst w = worstOf(laid, space);
        const HyphenRuns h = hyphenRunsOf(laid);
        const Rivers r = riversByOverlap(laid, kTolReported * space);
        printf("%-14s %-10s %-10s %6d %7.2f %7.2f %7.2f %7.2f %7.2f %6d %6d %6d %6d %6d %6d\n", c.name,
               justify ? "justified" : "ragged", cellName(cell), st.lineCount, st.mean, st.stddev, w.p95, w.p99, w.max,
               w.loose2x, w.loose3x, h.hyphenatedLines, h.runs3plus, h.longest, r.count);
      }
    }
  }
  printf(
      "\nmean/sd: justified rows are the per-line inter-word gap, ragged rows the line end.\n"
      "p95/p99/max, lo2x, lo3x: the per-line gap only -- meaningless on a ragged row, printed for completeness.\n"
      "lo2x/lo3x: lines set at more than 2x / 3x the font's natural word space.\n"
      "hy: lines ending in a hyphen. hy3+: ladders of 3+ hyphenated lines in a row. hyMax: longest such run.\n"
      "riv: maximal chains of 3+ overlapping gaps on consecutive lines, overlap >= %.2f space widths.\n"
      "     An ABSOLUTE count -- section 3a normalizes it per 1000 gaps and gives it its null.\n",
      kTolReported);

  // -------------------------------------------------------------------------
  printf("\n=== 2. Worst-line badness, justified only, as a multiple of the natural word space ===\n\n");
  printf("%-14s %-10s %7s %7s %7s %7s %9s\n", "config", "cell", "mean", "p95", "p99", "max", "paraMax");
  for (const Cfg& c : cfgs) {
    const double space = naturalSpacePx(c.fontId);
    for (const Cell cell : reachableCells()) {
      const Corpus laid = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, 0);
      Stats st;
      st.accumulate(gapSamplesOf(laid));
      const Worst w = worstOf(laid, space);
      printf("%-14s %-10s %7.2f %7.2f %7.2f %7.2f %9.2f\n", c.name, cellName(cell), st.mean / space, w.p95 / space,
             w.p99 / space, w.max / space, w.meanParaMax / space);
    }
  }
  printf("\nnatural word space: 12 pt %.1f px, 18 pt %.1f px.\n", naturalSpacePx(LIBREFRANKLIN_READER_12_FONT_ID),
         naturalSpacePx(LIBREFRANKLIN_READER_18_FONT_ID));

  // -------------------------------------------------------------------------
  printf("\n=== 3a. Rivers by OVERLAP, sweep, against the ragged null ===\n");
  printf(
      "Linked when two gaps' spans overlap by at least t space widths. Rates per 1000 gaps, not per\n"
      "1000 lines: a tighter breaker puts more words -- and so more gaps -- on each line, which\n"
      "mechanically gives a river more chances to start.\n"
      "\n"
      "THE RAGGED COLUMN IS THE NULL. On a ragged page every gap is exactly one word space, so no gap\n"
      "position owes anything to the breaker's spacing decisions; whatever alignment survives there is\n"
      "the text's own and chance's. A justified rate that does not clear its own ragged rate is not a\n"
      "measurement of the breaker.\n\n");
  printf("%-14s %-10s %6s %6s", "config", "cell", "lines", "gaps");
  for (const double t : kTolSweep) printf("  jt=%.2f", t);
  printf("  |");
  for (const double t : kTolSweep) printf("  rg=%.2f", t);
  printf("  long\n");
  for (const Cfg& c : cfgs) {
    const double space = naturalSpacePx(c.fontId);
    for (const Cell cell : reachableCells()) {
      const Corpus just = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, 0);
      const Corpus rag = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, 255);
      const int jg = totalGapsOf(just);
      const int rg = totalGapsOf(rag);
      printf("%-14s %-10s %6d %6d", c.name, cellName(cell), totalLinesOf(just), jg);
      for (const double t : kTolSweep) {
        printf("  %6.2f", jg > 0 ? 1000.0 * riversByOverlap(just, t * space).count / jg : 0.0);
      }
      printf("  |");
      for (const double t : kTolSweep) {
        printf("  %6.2f", rg > 0 ? 1000.0 * riversByOverlap(rag, t * space).count / rg : 0.0);
      }
      printf("  %4d\n", riversByOverlap(just, kTolReported * space).longest);
    }
  }

  // -------------------------------------------------------------------------
  printf("\n=== 3b. Rivers by CENTRE, the definition that FAILS its null ===\n");
  printf("Kept so the next pass does not rediscover it. See the comment on riversByCentre.\n\n");
  printf("%-14s %-10s %6s", "config", "cell", "gaps");
  for (const double t : kTolSweep) printf("  jt=%.2f", t);
  printf("  |");
  for (const double t : kTolSweep) printf("  rg=%.2f", t);
  printf("\n");
  for (const Cfg& c : cfgs) {
    const double space = naturalSpacePx(c.fontId);
    for (const Cell cell : reachableCells()) {
      const Corpus just = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, 0);
      const Corpus rag = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, 255);
      const int jg = totalGapsOf(just);
      const int rg = totalGapsOf(rag);
      printf("%-14s %-10s %6d", c.name, cellName(cell), jg);
      for (const double t : kTolSweep) {
        printf("  %6.2f", jg > 0 ? 1000.0 * riversByCentre(just, t * space).count / jg : 0.0);
      }
      printf("  |");
      for (const double t : kTolSweep) {
        printf("  %6.2f", rg > 0 ? 1000.0 * riversByCentre(rag, t * space).count / rg : 0.0);
      }
      printf("\n");
    }
  }

  // -------------------------------------------------------------------------
  printf("\n=== 4. Hyphen quality ===\n\n");
  printf("%-14s %-10s %-10s %6s %6s %6s %8s %8s %6s %6s %6s\n", "config", "align", "cell", "all", "brk", "hy",
         "dens/all", "dens/brk", "run2", "run3+", "hyMax");
  for (const Cfg& c : cfgs) {
    for (const bool justify : {true, false}) {
      for (const Cell cell : reachableCells()) {
        const Corpus laid = layoutCorpus(corpus, cell, CssTextAlign::Justify, c.fontId, c.measure, justify ? 0 : 255);
        const HyphenRuns h = hyphenRunsOf(laid);
        printf("%-14s %-10s %-10s %6d %6d %6d %7.2f%% %7.2f%% %6d %6d %6d\n", c.name, justify ? "justified" : "ragged",
               cellName(cell), h.allLines, h.breakableLines, h.hyphenatedLines, h.densityPct, h.densityOfBreakablePct,
               h.runs2, h.runs3plus, h.longest);
      }
    }
  }
}

// ===========================================================================
// The ragged hyphenation gate, swept.  2026-08-27.
// ===========================================================================
//
// `raggedSkipsHyphen` (ParsedText.cpp, now reading linebreak::raggedHyphenGatePct)
// suppresses hyphenation once a ragged line has already reached 70% of the
// measure. Section 7 and section 8j of docs/line-breaking-2026-08-25.md both
// list it as untouched; this is the sweep the owner asked for.
//
// ONE CONFIGURATION, deliberately, and it is the one that ships: 14 pt
// (CrossPointSettings::DEFAULT_FONT_POINT_SIZE) at 512 px (the X3's portrait
// measure at the default screen margin). Owner scope, 2026-08-27, narrowed
// twice: "just measure at 14", then "only 512". The saving is spent on stepping
// the gate by ONE point across 40..100 instead of by five across a grid of
// sizes and measures nobody reads at. 12 / 16 / 18 pt and 400 / 640 px are
// therefore NOT covered by this sweep and the doc says so; in particular
// section 7's claim that this gate is "most of why the greedy rag is uneven at
// 18 pt" stays UNVERIFIED.
//
// WHAT GOOD RAG MEANS, stated before anything is ranked, because "less rag" is
// not the goal and ranking on depth alone would quietly re-derive justified
// text:
//
//   * A ragged setting is SUPPOSED to look ragged. Mean shortfall near zero is
//     a failure, not a win -- it is justified text without the justification.
//     So depth is REPORTED and is not the ranking key.
//   * The named defect of ragged setting is a HOLE: one line conspicuously
//     shorter than the lines around it, which reads as a paragraph break that
//     is not there. This is the defect the gate's own comment says it exists to
//     rescue against, so it is the metric the gate must be judged on. Measured
//     two ways -- absolutely (shortfall past a share of the measure) and
//     relative to the line's own neighbours, since a short line among short
//     lines is not a hole.
//   * The other named defect is a rag so deep the column loses its shape,
//     which p95 and the per-paragraph worst carry.
//
// And the cost side, which is what a higher gate buys the rag with: hyphen
// density on the page denominator, runs of two, and ladders of three or more.
namespace {

// The rag, as a distribution of SHORTFALL from the measure. Every figure is a
// percentage OF THE MEASURE so the numbers mean the same thing whatever the
// measure is, and paragraph-final lines are excluded throughout -- a final line
// is where the text ran out, not a decision the breaker made.
struct RagShape {
  int lines = 0;
  double meanPct = 0.0;  // rag DEPTH. Reported, not ranked -- see above.
  double sdPct = 0.0;    // how active the edge is
  double p95Pct = 0.0;
  double p99Pct = 0.0;
  double maxPct = 0.0;          // the single shortest line in the corpus
  double meanParaMaxPct = 0.0;  // mean over paragraphs of that paragraph's shortest line
  int holes25 = 0;              // lines ending short of 75% of the measure
  int holes33 = 0;
  int holes40 = 0;
  // A hole RELATIVE to its neighbours: this line's shortfall less the larger of
  // the two lines beside it, within the paragraph. Positive means it is shorter
  // than both. Interior lines only, since an edge line has one neighbour and a
  // one-sided comparison is a different quantity.
  int relHoles15 = 0;  // shorter than both neighbours by more than 15% of the measure
  int relHoles25 = 0;
  double relP95Pct = 0.0;
  double relMaxPct = 0.0;
};

RagShape ragShapeOf(const Corpus& c, const double measurePx) {
  RagShape r;
  std::vector<double> s;    // shortfall, % of measure
  std::vector<double> rel;  // neighbour-relative hole depth, % of measure
  int paras = 0;
  for (const auto& para : c) {
    std::vector<double> pv;
    for (const auto& l : para) {
      if (l.isFinal) continue;
      pv.push_back(100.0 * (measurePx - l.end) / measurePx);
    }
    if (pv.empty()) continue;
    paras++;
    double worst = 0.0;
    for (const double v : pv) worst = std::max(worst, v);
    r.meanParaMaxPct += worst;
    for (size_t i = 0; i < pv.size(); ++i) {
      s.push_back(pv[i]);
      if (pv[i] > 25.0) r.holes25++;
      if (pv[i] > 33.0) r.holes33++;
      if (pv[i] > 40.0) r.holes40++;
      if (i > 0 && i + 1 < pv.size()) {
        const double d = pv[i] - std::max(pv[i - 1], pv[i + 1]);
        rel.push_back(d);
        if (d > 15.0) r.relHoles15++;
        if (d > 25.0) r.relHoles25++;
      }
    }
  }
  if (paras > 0) r.meanParaMaxPct /= paras;
  r.lines = static_cast<int>(s.size());
  if (!s.empty()) {
    for (const double v : s) r.meanPct += v;
    r.meanPct /= s.size();
    for (const double v : s) {
      const double d = v - r.meanPct;
      r.sdPct += d * d;
      r.maxPct = std::max(r.maxPct, v);
    }
    r.sdPct = std::sqrt(r.sdPct / s.size());
    r.p95Pct = percentile(s, 0.95);
    r.p99Pct = percentile(s, 0.99);
  }
  if (!rel.empty()) {
    for (const double v : rel) r.relMaxPct = std::max(r.relMaxPct, v);
    r.relP95Pct = percentile(rel, 0.95);
  }
  return r;
}

// A gate value applied for the duration of a scope. Restores whatever was live
// before, so a sweep row cannot leak into the next measurement -- the same
// discipline HyphenationScope enforces for the trie, and for the same reason:
// this suite's oldest failure mode is an arm quietly measured under the
// previous arm's global.
class GateScope {
 public:
  explicit GateScope(const int pct) : previous_(linebreak::raggedHyphenGatePct()) {
    linebreak::setRaggedHyphenGatePct(pct);
  }
  ~GateScope() { linebreak::setRaggedHyphenGatePct(previous_); }
  GateScope(const GateScope&) = delete;
  GateScope& operator=(const GateScope&) = delete;

 private:
  const int previous_;
};

constexpr int kGateFont = LIBREFRANKLIN_READER_14_FONT_ID;

}  // namespace

// The gate as it shipped, and as it still ships. `raggedSkipsHyphen` read
// `lineWidth * 10 >= effectivePageWidth * 7` until 2026-08-27, when it became
// `lineWidth * 100 >= effectivePageWidth * raggedHyphenGatePct()` so the sweep
// could walk it. That is the same comparison scaled by ten on BOTH sides, so it
// must be EXACT and not merely equivalent at typical widths -- an integer
// rewrite that agrees on 512 px and disagrees on some other measure would move
// line breaks in books nobody sweeps. Checked over every measure the engine can
// be handed and every line width inside it.
TEST(LineBreakQuality, TheRaggedGateIsSeventyAndTheRewriteIsExact) {
  EXPECT_EQ(linebreak::RAGGED_HYPHEN_GATE_PCT, 70)
      << "moving this is a change to DEFAULT rendering: it needs a SECTION_FILE_VERSION bump, "
         "which repaginates every book on every card. See docs/line-breaking-2026-08-25.md section 9.";
  for (int width = 1; width <= 2048; ++width) {
    for (int lineWidth = 0; lineWidth <= width; ++lineWidth) {
      const bool before = lineWidth * 10 >= width * 7;
      const bool after = lineWidth * 100 >= width * linebreak::RAGGED_HYPHEN_GATE_PCT;
      ASSERT_EQ(before, after) << "width " << width << ", lineWidth " << lineWidth;
    }
  }
  // NEGATIVE WIDTHS TOO. `effectivePageWidth` is `pageWidth - firstLineIndent`
  // (ParsedText.cpp), and nothing clamps that difference -- a book whose CSS
  // asks for a text-indent wider than the measure makes it negative. Both forms
  // then compare a non-negative left side against a negative right side and say
  // true, but "both say true" is the claim, so it is checked rather than
  // reasoned about. Adversarial review, 2026-08-27, judged this path
  // unreachable; the sweep is cheap and the assertion outlives the judgment.
  for (int width = -2048; width < 0; ++width) {
    for (int lineWidth = 0; lineWidth <= 2048; lineWidth += 7) {
      ASSERT_EQ(lineWidth * 10 >= width * 7, lineWidth * 100 >= width * linebreak::RAGGED_HYPHEN_GATE_PCT)
          << "width " << width << ", lineWidth " << lineWidth;
    }
  }
}

// THE GATE MUST NOT BE ABLE TO REACH A JUSTIFIED PAGE. Its condition begins
// `blockStyle.alignment != CssTextAlign::Justify`, so this is structural -- but
// "structural" is an argument, and the 2026-08-27 sweep had to report the
// measurement. Both ends of the legal range against the shipped value, and the
// page has to come out identical in every statistic, hyphens included.
//
// Note what "justified" means here: AFTER auto-justification. A block demoted
// for a narrow measure is ragged and the gate does apply to it, which is the
// case the next test covers.
TEST(LineBreakQuality, MovingTheRaggedGateCannotChangeAJustifiedPage) {
  Stats reference;
  int referenceHyphens = 0;
  for (const int gate : {40, linebreak::RAGGED_HYPHEN_GATE_PCT, 100}) {
    const GateScope g(gate);
    const Corpus laid =
        layoutCorpus(paragraphs(), Cell::GreedyHyphenated, CssTextAlign::Justify, kGateFont, kMeasure, /*threshold=*/0);
    Stats st;
    st.accumulate(gapSamplesOf(laid));
    const int hy = hyphenatedLinesOf(laid);
    if (gate == 40) {
      reference = st;
      referenceHyphens = hy;
      ASSERT_GT(st.lineCount, 0);
      continue;
    }
    EXPECT_EQ(st.lineCount, reference.lineCount) << "gate " << gate;
    EXPECT_DOUBLE_EQ(st.mean, reference.mean) << "gate " << gate;
    EXPECT_DOUBLE_EQ(st.stddev, reference.stddev) << "gate " << gate;
    EXPECT_EQ(hy, referenceHyphens) << "gate " << gate;
  }
}

// ...AND IT MUST REACH A RAGGED ONE. The precondition for the test above, and
// this suite's oldest failure mode: two identical rows are what a DEAD axis
// produces as well as an immune one, and the harness has shipped a dead axis
// twice (the missing trie, and the 2x2's fourth cell). Asserting the direction
// as well as the difference, because the sign is the whole model -- a higher
// gate means hyphenate for longer, so it must produce MORE hyphens and a
// SHALLOWER rag.
TEST(LineBreakQuality, TheRaggedGateBindsOnARaggedPage) {
  const auto ragged = [](const int gate) {
    const GateScope g(gate);
    return layoutCorpus(paragraphs(), Cell::GreedyHyphenated, CssTextAlign::Justify, kGateFont, kMeasure,
                        /*threshold=*/255);
  };
  const RagShape low = ragShapeOf(ragged(40), kMeasure);
  const RagShape shipped = ragShapeOf(ragged(linebreak::RAGGED_HYPHEN_GATE_PCT), kMeasure);
  const RagShape high = ragShapeOf(ragged(100), kMeasure);

  const int hyLow = hyphenatedLinesOf(ragged(40));
  const int hyShipped = hyphenatedLinesOf(ragged(linebreak::RAGGED_HYPHEN_GATE_PCT));
  const int hyHigh = hyphenatedLinesOf(ragged(100));

  // MONOTONE, not strict at every step. The built-in specimen is a few hundred
  // words; on it the 40 and 70 arms both hyphenate zero ragged lines, which is
  // a fact about the specimen and not about the gate (the 394-paragraph corpus
  // in the sweep separates them 4 against 75). The strict inequality is
  // therefore asserted end to end, where the specimen does carry it, and the
  // shipped value is pinned between the two ends.
  EXPECT_LT(hyLow, hyHigh) << "a higher gate must hyphenate for longer";
  EXPECT_LE(hyLow, hyShipped);
  EXPECT_LE(hyShipped, hyHigh);
  EXPECT_GT(low.meanPct, high.meanPct) << "fewer hyphens must leave a DEEPER rag";
  EXPECT_GT(low.p95Pct, high.p95Pct);
  EXPECT_GT(shipped.lines, 0);
}

// THE SWEEP. Disabled like DISABLED_Sweep -- it wants the book corpus, which is
// not checked in (tools/linebreak_corpus.py rebuilds it).
//
//   CROSSPOINT_LINEBREAK_CORPUS=/tmp/corpus.txt \
//     build/test/line_break_quality/LineBreakQualityTest \
//     --gtest_also_run_disabled_tests --gtest_filter='*RaggedGateSweep*'
TEST(LineBreakQuality, DISABLED_RaggedGateSweep) {
  const char* path = std::getenv("CROSSPOINT_LINEBREAK_CORPUS");
  ASSERT_NE(path, nullptr) << "set CROSSPOINT_LINEBREAK_CORPUS to a plain-text corpus, one paragraph per line";
  std::ifstream in(path);
  ASSERT_TRUE(in.good()) << "cannot read " << path;
  std::vector<std::string> corpus;
  for (std::string line; std::getline(in, line);) {
    if (line.size() > 40) corpus.push_back(line);
  }
  ASSERT_FALSE(corpus.empty());
  size_t words = 0;
  for (const auto& p : corpus) words += splitWords(p).size();
  printf("corpus: %zu paragraphs, %zu words\n", corpus.size(), words);

  // Does 14 pt at 512 px actually LAND ragged on a shipped device? The gate only
  // ever runs on a ragged block, so if auto-justification keeps this
  // configuration justified the whole sweep is about a page the default reader
  // never sees. Printed rather than assumed.
  {
    // The same measurement ParsedText makes: the width of autojustify::ALPHABET
    // in the reading face, which is what charsPerLine divides the measure by.
    const int alphabetPx =
        Gfx::instance().renderer().getTextAdvanceX(kGateFont, autojustify::ALPHABET, EpdFontFamily::REGULAR);
    printf("14 pt @ 512: alphabet %d px, ~%d chars/line, threshold %d -> %s\n", alphabetPx,
           autojustify::charsPerLine(kMeasure, alphabetPx), autojustify::THRESHOLD_CHARS,
           autojustify::shouldJustify(kMeasure, alphabetPx, autojustify::THRESHOLD_CHARS) ? "JUSTIFIED" : "RAGGED");
  }

  // -------------------------------------------------------------------------
  // 1. Justified text must not move. The gate's condition begins
  //    `alignment != CssTextAlign::Justify`, so a justified block can never
  //    reach it -- but that is an argument, and this is the measurement.
  printf("\n=== 1. Justified is untouched (gate 40 vs 70 vs 100) ===\n\n");
  printf("%-6s %6s %8s %8s %8s %6s %6s\n", "gate", "lines", "mean", "sd", "p95", "hy", "run3+");
  for (const int gate : {40, 70, 100}) {
    const GateScope g(gate);
    const Corpus laid = layoutCorpus(corpus, Cell::GreedyHyphenated, CssTextAlign::Justify, kGateFont, kMeasure, 0);
    Stats st;
    st.accumulate(gapSamplesOf(laid));
    const Worst w = worstOf(laid, naturalSpacePx(kGateFont));
    const HyphenRuns h = hyphenRunsOf(laid);
    printf("%-6d %6d %8.4f %8.4f %8.4f %6d %6d\n", gate, st.lineCount, st.mean, st.stddev, w.p95, h.hyphenatedLines,
           h.runs3plus);
  }

  // -------------------------------------------------------------------------
  // 2. The curve. One row per gate point, ragged, 14 pt at 512.
  printf("\n=== 2. The ragged gate curve, 14 pt @ 512 px ===\n");
  printf("shortfall figures are %% of the measure; hole counts are line counts.\n\n");
  printf("%-5s %6s %6s %7s %7s %6s %6s %6s %6s %6s %6s %6s %7s %7s %6s %6s %6s %6s\n", "gate", "lines", "hy", "dns/all",
         "dns/brk", "run2", "run3+", "hyMax", "mean", "sd", "p95", "p99", "paraMax", "max", "h25", "h33", "h40",
         "rel15");
  for (int gate = 40; gate <= 100; ++gate) {
    const GateScope g(gate);
    const Corpus laid = layoutCorpus(corpus, Cell::GreedyHyphenated, CssTextAlign::Justify, kGateFont, kMeasure, 255);
    const HyphenRuns h = hyphenRunsOf(laid);
    const RagShape r = ragShapeOf(laid, kMeasure);
    printf("%-5d %6d %6d %6.2f%% %6.2f%% %6d %6d %6d %6.2f %6.2f %6.2f %6.2f %7.2f %7.2f %6d %6d %6d %6d\n", gate,
           h.allLines, h.hyphenatedLines, h.densityPct, h.densityOfBreakablePct, h.runs2, h.runs3plus, h.longest,
           r.meanPct, r.sdPct, r.p95Pct, r.p99Pct, r.meanParaMaxPct, r.maxPct, r.holes25, r.holes33, r.holes40,
           r.relHoles15);
  }

  // -------------------------------------------------------------------------
  // 3. The neighbour-relative hole, printed on its own because it is the metric
  //    most likely to be inside noise and the doc has to quote its numbers when
  //    it drops it.
  printf("\n=== 3. Neighbour-relative holes ===\n\n");
  printf("%-5s %8s %8s %8s %8s\n", "gate", "rel>15", "rel>25", "relP95", "relMax");
  for (int gate = 40; gate <= 100; gate += 5) {
    const GateScope g(gate);
    const Corpus laid = layoutCorpus(corpus, Cell::GreedyHyphenated, CssTextAlign::Justify, kGateFont, kMeasure, 255);
    const RagShape r = ragShapeOf(laid, kMeasure);
    printf("%-5d %8d %8d %8.2f %8.2f\n", gate, r.relHoles15, r.relHoles25, r.relP95Pct, r.relMaxPct);
  }

  // -------------------------------------------------------------------------
  // 4. THE WORST LINE, and the direction it moves.
  //
  // The mean and the percentiles both improve monotonically as the gate rises.
  // The single deepest line does NOT -- it steps from 51.17% of the measure to
  // 68.36% at gate 75 and stays there. An average hides exactly this, which is
  // the lesson the previous two rounds recorded, so it is printed on its own
  // with the five deepest lines behind it rather than as one number in a wide
  // row. `afterHy` is the count of very deep lines whose PREDECESSOR ended in a
  // hyphen: the mechanism to test is that hyphenating leaves a remainder which
  // has to start the next line and can leave a deeper hole than the one the
  // hyphen prevented.
  printf("\n=== 4. The five deepest lines, and what precedes them ===\n\n");
  printf("%-5s  %-44s %8s\n", "gate", "five deepest shortfalls (% of measure)", "afterHy");
  for (const int gate : {65, 70, 72, 74, 75, 76, 80, 85, 100}) {
    const GateScope g(gate);
    const Corpus laid = layoutCorpus(corpus, Cell::GreedyHyphenated, CssTextAlign::Justify, kGateFont, kMeasure, 255);
    std::vector<double> deep;
    int afterHy = 0;
    for (const auto& para : laid) {
      for (size_t i = 0; i < para.size(); ++i) {
        if (para[i].isFinal) continue;
        const double sf = 100.0 * (kMeasure - para[i].end) / kMeasure;
        deep.push_back(sf);
        if (sf > 40.0 && i > 0 && para[i - 1].hyphenated) afterHy++;
      }
    }
    std::sort(deep.rbegin(), deep.rend());
    printf("%-5d  ", gate);
    for (int i = 0; i < 5 && i < static_cast<int>(deep.size()); ++i) printf("%8.2f", deep[i]);
    printf("    %8d\n", afterHy);
  }
}
