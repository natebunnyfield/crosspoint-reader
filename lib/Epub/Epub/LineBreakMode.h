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

// What every shipped build has rendered since the flag was frozen, and what a
// fresh install must still get. An existing install renders identically until
// the row is touched.
inline constexpr uint8_t STORED_DEFAULT = STORED_HYPHENATED;

enum class Mode : uint8_t {
  WholeWords = STORED_WHOLE_WORDS,
  Hyphenated = STORED_HYPHENATED,
};

// Anything that is not a mode falls to the shipped default rather than to 0.
// Falling to 0 would mean a corrupt or future settings.json silently changing
// every line break in every book to the mode nobody chose.
constexpr Mode modeFor(const uint8_t stored) {
  return stored == STORED_WHOLE_WORDS ? Mode::WholeWords : Mode::Hyphenated;
}

// True when the breaker may split a word that would otherwise not fit the
// current line -- i.e. when computeHyphenatedLineBreaks runs.
constexpr bool splitsWordsAtLineEnds(const Mode mode) { return mode == Mode::Hyphenated; }

// True when the total-fit dynamic program runs.
constexpr bool usesTotalFit(const Mode mode) { return mode == Mode::WholeWords; }

// The two are exhaustive and exclusive; both call sites below assert it.
constexpr bool splitsWordsAtLineEnds(const uint8_t stored) { return splitsWordsAtLineEnds(modeFor(stored)); }
constexpr bool usesTotalFit(const uint8_t stored) { return usesTotalFit(modeFor(stored)); }

}  // namespace linebreak
