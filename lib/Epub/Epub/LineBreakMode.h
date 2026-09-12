#pragma once
#include <cstdint>

// WHICH LINE BREAKER RUNS, and what the stored byte means.
//
// `CrossPointSettings::hyphenationEnabled` selects between two DIFFERENT
// algorithms, not between drawing a hyphen and not drawing one. That is the
// whole reason this header exists: the field's name describes the visible
// side-effect and hides the actual switch, and until 2026-08-25 it was frozen
// at 1 so only one of the two ever ran.
//
//   1  Hyphenated   ParsedText::computeHyphenatedLineBreaks -- first-fit
//                   greedy. Each line takes words until the next one will not
//                   fit, then tries to split that word at a legal hyphenation
//                   point so a prefix still lands on this line. Lines come out
//                   as FULL as the measure allows, at the cost of a hyphen at
//                   the end of a good many of them.
//
//   0  WholeWords   ParsedText::computeLineBreaks -- a total-fit dynamic
//                   program over the whole paragraph, minimizing the SUM OF
//                   SQUARED trailing slack. Squaring is what makes it spread
//                   the shortfall: two lines 20 px short cost less than one
//                   line 40 px short, so it would rather pull a word down early
//                   than leave one gaping line. Whole words only -- see the
//                   caveat below.
//
// WHICH IS BETTER IS NOT THE OBVIOUS ANSWER, and it was measured before the row
// shipped (394 paragraphs, 23,075 words, six measure x size configurations;
// test/line_break_quality, table in docs/line-breaking-2026-08-25.md). On a
// JUSTIFIED page the greedy breaker wins on both counts in all six: word
// spacing is tighter (10.45 px against 12.87 at the X3's 512 px measure, 12 pt)
// AND more even (sd 5.14 against 6.83). The optimizer loses because every break
// it would like to take inside a word is unavailable to it, so it pays the
// difference in slack and justification turns slack into word space. On a
// RAGGED page the two are close and the sign changes with the size. The one
// large, reliable effect is the hyphens themselves: 489 hyphenated lines
// against 33 in that same configuration.
//
// So the row is a genuine taste trade and not an upgrade, and its labels say
// so. An earlier draft called mode 0 "Even Spacing" on the strength of the
// survey's prediction; the page disproves it.
//
// THE CAVEAT, and it belongs in the row's help text more than here: WholeWords
// is not "hyphens never appear". computeLineBreaks still runs a pre-pass that
// splits any word too wide to fit a line even on its own (ParsedText.cpp, the
// `while (wordWidths[i] > effectiveWidth)` loop). A German compound or a URL
// still breaks with a hyphen, because the alternative is a word running off the
// edge of the glass. What goes away is OPPORTUNISTIC hyphenation.
//
// WHY THE TWO ARE COUPLED TO ONE FLAG -- asked, and answered from the code
// rather than assumed (2026-08-25). It is not a policy pairing and not a
// performance budget. `ParsedText::hyphenateWordAtIndex` implements hyphenation
// DESTRUCTIVELY: it splits words[i] in place and inserts the remainder into
// `words`, `wordStyles`, `wordWidths`, `wordContinues`, `wordNoSpaceBefore`,
// `wordSourceStart`, `wordIsFocusSuffix` and `rubyTexts`, shifting every index
// above it. The DP's `dp[]` and `ans[]` are sized from a word count captured
// before its loop and indexed by that same word index, and its output contract
// -- a vector of word indices that `extractLine` slices `words` by -- cannot
// express "break inside word 12" at all. So this DP cannot WEIGH a hyphen
// candidate: taking one rewrites the array it is indexing. The greedy breaker
// can do it precisely because it re-reads `wordWidths.size()` on every
// iteration and never looks back.
//
// BE PRECISE ABOUT WHAT THAT RULES OUT, because an earlier version of this
// comment said "the two cannot be combined" and that is too strong. The DP
// already tolerates destructive hyphenation -- in its own pre-pass, which runs
// BEFORE `totalWordCount` is captured, so each fragment is simply another word.
// Splitting eagerly there at every legal breakpoint would hand the DP hyphen
// candidates with no change to its indexing and none to its output. What is
// actually missing is two things:
//
//   * a PER-BREAK PENALTY. Without one the optimizer takes hyphens for free and
//     sets a page of them, since a break inside a word costs it nothing in
//     squared slack. Knuth's hyphen penalty is not decoration.
//   * a NO-SPACE-BETWEEN-FRAGMENTS flag. `hyphenateWordAtIndex` gives the
//     remainder `wordContinues = false` (it starts the next line, by
//     construction today), so two fragments of one word landing on the SAME
//     line would be set with a full word space between them.
//
// The second is load-bearing for the pre-pass as it stands and is worth writing
// down: today the two fragments can never share a line, because the prefix is
// the widest that fits and prefix + remainder is at least the original word,
// which did not fit. Eager splitting breaks that guarantee immediately.
//
// So: classical Knuth-Plass -- total fit WITH hyphen points as candidates --
// is a real piece of work rather than a flag, but a smaller one than "the two
// cannot be combined" implies, and the justified measurement above says
// plainly that it is the combination worth having: the DP's whole deficit
// there is the breaks it is not allowed to consider. Recorded, with the
// timings that say it would be affordable, in docs/line-breaking-2026-08-25.md.
//
// HISTORICALLY there is nothing to find: the fork's log is flattened at
// 3da2cd3cf, a squashed import where both functions and the dispatch between
// them arrive already coupled. `git log -S` on the flag and on both function
// names turns up no commit that chose the pairing.

namespace linebreak {

// The stored byte. These ARE the persisted values of the "hyphenationEnabled"
// key and they are append-only in the strictest sense: settings.json files
// written before the 2026-08-21 reduction already carry 0 and 1 with exactly
// this meaning, so neither may ever be re-pointed. A three-way mode would take
// the value 2.
inline constexpr uint8_t STORED_WHOLE_WORDS = 0;
inline constexpr uint8_t STORED_HYPHENATED = 1;
// The three-way mode this header anticipated. 2 has never been written by any
// build, so it is free, and 0 and 1 keep their meanings exactly -- a card full
// of section files stays valid until the reader actually chooses Automatic.
inline constexpr uint8_t STORED_AUTOMATIC = 2;

// What every shipped build has rendered since the flag was frozen, and what a
// fresh install must still get. An existing install renders identically until
// the row is touched.
inline constexpr uint8_t STORED_DEFAULT = STORED_HYPHENATED;

enum class Mode : uint8_t {
  WholeWords = STORED_WHOLE_WORDS,
  Hyphenated = STORED_HYPHENATED,
  // NOT a breaker. A policy that resolves to one of the two above, per block,
  // against that block's own measure -- see resolveAutomatic below.
  Automatic = STORED_AUTOMATIC,
};

// Anything that is not a mode falls to the shipped default rather than to 0.
// Falling to 0 would mean a corrupt or future settings.json silently changing
// every line break in every book to the mode nobody chose.
constexpr Mode modeFor(const uint8_t stored) {
  if (stored == STORED_WHOLE_WORDS) return Mode::WholeWords;
  if (stored == STORED_AUTOMATIC) return Mode::Automatic;
  return Mode::Hyphenated;
}

constexpr bool isAutomatic(const Mode mode) { return mode == Mode::Automatic; }
constexpr bool isAutomatic(const uint8_t stored) { return isAutomatic(modeFor(stored)); }

// ---------------------------------------------------------------------------
// AUTOMATIC: hyphens only where NOT hyphenating is the worse harm
// ---------------------------------------------------------------------------
//
// Owner ruling 2026-09-11: "make a third setting that only turns on
// hyphenation automatically when it is helpful for even a dyslexic reader."
// That last clause sets the BAR, and it is a high one, because the two costs
// being weighed both land on the same reader:
//
//   * A split word is a real cost. The British Dyslexia Association's Style
//     Guide asks plainly for text that is not justified and for words not to be
//     broken across lines; a word arriving in two pieces across a line end has
//     to be rejoined before it can be recognised.
//   * Erratic word spacing is also a real cost, and for the same reader. It is
//     what justification does to a short line, and the white channels it opens
//     down a paragraph ("rivers") pull the eye off the line it is tracking --
//     which is why the same guide asks for ragged right in the first place.
//
// So Automatic does not ask "are hyphens good?". It asks the only question
// whose answer can be defended: IS THIS LINE SO SHORT THAT SETTING IT
// JUSTIFIED WITHOUT HYPHENS WOULD DO MORE DAMAGE THAN THE HYPHENS? Almost
// always the answer is no, and Automatic sets whole words.
//
// TWO CONDITIONS, BOTH REQUIRED.
//
// 1. THE BLOCK MUST STILL BE JUSTIFIED -- after auto-justification has had its
//    say (AutoJustify.h), not before. A ragged block has no stretched gaps at
//    all: its spacing is already even, so a hyphen there buys nothing and costs
//    a stumble. This is also what makes Automatic genuinely different from
//    "Allow hyphens", which still hyphenates a ragged block as a rescue against
//    a conspicuously short line (RAGGED_HYPHEN_GATE_PCT below). Under
//    Automatic that rescue does not run: a short ragged line is not a defect.
//
// 2. THE MEASURE MUST BE INSIDE THE GREY BAND. Above HELPFUL_MAX_CHARS the
//    line holds enough gaps to absorb its slack invisibly and justification
//    composes on its own, so hyphens are pure cost. Below the justification
//    threshold the block is already ragged and condition 1 has excluded it.
//    What is left is the narrow band between them, which is exactly the zone
//    every source names as the place justified text goes wrong.
//
// Note what condition 1 implies: raise the Justified Text threshold to 50 and
// Automatic stops hyphenating altogether, because nothing is left between the
// two bounds. That is correct rather than a degenerate case -- a reader who has
// asked for ragged setting below 50 characters has asked for the remedy that
// makes hyphens unnecessary.

// The upper bound of the band, in characters per line.
//
// 50 rather than a rounder number, and deliberately at the CONSERVATIVE end of
// what the sources would allow, because the bar is "helpful for even a dyslexic
// reader" -- when in doubt, do not hyphenate:
//
//   * Butterick, "Practical Typography", gives 45-90 characters as the
//     comfortable band. At 50 the line is inside it with room to spare, so
//     justification has enough gaps to hide its slack and a hyphen is buying
//     nothing.
//   * Bringhurst's failure zone is "less than 38 or 40" (AutoJustify.h quotes
//     it in full), and 40 is this reader's default justification threshold. So
//     the band Automatic hyphenates in is [40, 50): justified, and short.
//   * Gregory & Poulton (1970) measured justification significantly worse than
//     ragged at ~38-39 characters and found NO disadvantage by ~66. Setting the
//     bound at 66 would have been defensible for a general reader and is not
//     defensible here: between 50 and 66 justification is merely imperfect, not
//     harmful, and imperfect does not justify a split word to someone who pays
//     for one.
//
// On this device's own sweep (13 face/size pairs at 512 px, the calibration
// table in docs/auto-justification.md) the measured range is 28-53 characters,
// so 50 leaves both regimes reachable: the widest setting on the card sits
// above the bound and sets whole words, everything narrower that is still
// justified hyphenates.
inline constexpr int HELPFUL_MAX_CHARS = 50;

// Automatic's decision for ONE block. `charsPerLine` is autojustify's estimate
// for this block's own measure and face; `blockIsJustified` is the alignment
// AFTER auto-justification.
//
// An unmeasurable line (charsPerLine <= 0, a face whose alphabet could not be
// measured) resolves to WholeWords. This is the opposite of autojustify's
// fallback, and on purpose: there the fallback preserves what the BOOK asked
// for, while here there is no request to preserve, only a claim that hyphens
// would help -- and a claim that cannot be checked has not been made.
constexpr Mode resolveAutomatic(const bool blockIsJustified, const int charsPerLine) {
  if (!blockIsJustified) return Mode::WholeWords;
  if (charsPerLine <= 0) return Mode::WholeWords;
  return charsPerLine < HELPFUL_MAX_CHARS ? Mode::Hyphenated : Mode::WholeWords;
}

// Resolve whatever the reader stored down to a breaker. The two predicates
// below are exhaustive and exclusive over the RESULT, which is why Automatic
// has to come through here first.
constexpr Mode resolvedMode(const uint8_t stored, const bool blockIsJustified, const int charsPerLine) {
  const Mode mode = modeFor(stored);
  return isAutomatic(mode) ? resolveAutomatic(blockIsJustified, charsPerLine) : mode;
}

// True when the breaker may split a word that would otherwise not fit the
// current line -- i.e. when computeHyphenatedLineBreaks runs. Automatic is not
// a valid argument: resolve it first.
constexpr bool splitsWordsAtLineEnds(const Mode mode) { return mode == Mode::Hyphenated; }

// True when the total-fit dynamic program runs.
constexpr bool usesTotalFit(const Mode mode) { return mode == Mode::WholeWords; }

// ---------------------------------------------------------------------------
// THE RAGGED HYPHENATION GATE
// ---------------------------------------------------------------------------
//
// On a RAGGED block the greedy breaker does not hyphenate to pack the line; it
// hyphenates only as a RESCUE against a conspicuously short line. Once the line
// has already reached this share of the measure, the ragged edge is accepted and
// the overflowing word travels down whole. Below it -- and always for an
// oversized first word, where lineWidth is 0 -- the split logic runs exactly as
// it does for justified text. `ParsedText.cpp`'s `raggedSkipsHyphen` is the one
// consumer.
//
// A JUSTIFIED block never reaches the gate at all (`blockStyle.alignment !=
// CssTextAlign::Justify` is the first term of that condition), so moving this
// number cannot change a justified page. Note that "justified" means AFTER
// auto-justification has had its say: a block demoted for a narrow measure is
// ragged, and the gate then applies to it.
//
// 70 SWEPT AND KEPT, 2026-08-27. Measured at 14 pt / 512 px -- the shipped
// default size at the X3's own measure -- across gate values 40..100 in
// single-point steps, over the same 394-paragraph corpus as the rest of
// test/line_break_quality. See docs/line-breaking-2026-08-25.md section 9.
// Moving this number is a change to DEFAULT rendering and therefore costs a
// SECTION_FILE_VERSION bump and a repagination of every book on every card;
// the sweep did not find a value worth that.
inline constexpr int RAGGED_HYPHEN_GATE_PCT = 70;

// The gate as the breaker reads it. Fixed in every shipped configuration.
//
// CROSSPOINT_RAGGED_GATE_TUNABLE makes it settable, and is defined by exactly
// one target: test/line_break_quality, whose DISABLED_RaggedGateSweep has to
// walk the value inside a single process. Sweeping it by rebuilding the binary
// per point would be sixty builds of the layout engine and the built-in faces
// to answer one question. Nothing else may define it -- a mutable layout
// parameter on a device is a way for two pages of the same book to disagree.
#ifdef CROSSPOINT_RAGGED_GATE_TUNABLE
inline int& raggedHyphenGatePctRef() {
  static int value = RAGGED_HYPHEN_GATE_PCT;
  return value;
}
inline int raggedHyphenGatePct() { return raggedHyphenGatePctRef(); }
inline void setRaggedHyphenGatePct(const int pct) { raggedHyphenGatePctRef() = pct; }
#else
constexpr int raggedHyphenGatePct() { return RAGGED_HYPHEN_GATE_PCT; }
#endif

}  // namespace linebreak
