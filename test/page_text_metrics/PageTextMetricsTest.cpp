// HOW MUCH TEXT WAS ON THE PAGE. src/activities/reader/PageTextMetrics.h.
//
// The denominator of every reading rate in the usage ledger
// (crosspoint-simulator/docs/reading-experiments.md). Its failure mode is not a
// crash or a wrong picture: it is a number in a log file that nothing checks,
// discovered a year later as a conclusion that was never true. A systematic
// 5% miscount is larger than any typography effect the experiment could hope
// to detect, and it would point the same way every time.
//
// The one that matters most is the SOFT HYPHEN. The line breaker that splits
// words inserts them, and which breaker runs is itself one of the settings the
// experiment varies -- so counting them would inflate one arm's character count
// by exactly the amount that arm splits words, which is a confound perfectly
// aligned with the treatment. It would look like an effect.

#include <gtest/gtest.h>

#include "PageTextMetrics.h"

namespace {

// One line of tokens, the way a PageLine's TextBlock hands them over.
pagemetrics::Counts count(std::initializer_list<std::initializer_list<const char*>> lines) {
  pagemetrics::Counter c;
  for (const auto& line : lines) {
    c.beginLine();
    for (const char* t : line) c.addToken(t);
    c.endLine();
  }
  return c.result();
}

TEST(PageTextMetrics, CountsPlainAsciiWordsCharsAndLines) {
  const auto r = count({{"the", " ", "quick"}, {"brown", " ", "fox"}});
  EXPECT_EQ(r.words, 4u);
  EXPECT_EQ(r.chars, 3u + 5u + 5u + 3u);
  EXPECT_EQ(r.lines, 2u);
}

TEST(PageTextMetrics, BlankTokensAreNotWords) {
  // readaloud::tokenIsBlank's rule, reused rather than restated: any run of
  // spaces is blank. An inter-word gap is a token in this display list.
  const auto r = count({{"a", " ", "   ", "b"}});
  EXPECT_EQ(r.words, 2u);
  EXPECT_EQ(r.chars, 2u);
  EXPECT_EQ(r.lines, 1u);
}

TEST(PageTextMetrics, ALineOfNothingIsNotALine) {
  const auto r = count({{"a"}, {}, {" ", "  "}, {"b"}});
  EXPECT_EQ(r.lines, 2u) << "an empty or all-blank line carried no text and is not counted";
  EXPECT_EQ(r.words, 2u);
}

TEST(PageTextMetrics, SoftHyphensAreNotCharacters) {
  // U+00AD, the two bytes the tokens actually carry. The renderer never draws
  // them, so they are not on the page -- and counting them would bias one line
  // -break arm against the other.
  const auto plain = count({{"hyphenation"}});
  const auto split = count({{"hyphen\xc2\xad"}, {"ation"}});
  EXPECT_EQ(plain.chars, 11u);
  EXPECT_EQ(split.chars, 11u) << "a word split across lines contributes the same characters";
  EXPECT_EQ(split.words, 2u) << "...but two WORDS, which is exactly why chars is the denominator";
}

TEST(PageTextMetrics, CountsCodepointsNotBytes) {
  // "Söhne" is 5 codepoints in 6 bytes. Counting bytes would score a page of
  // accented text as 1.2 pages of English, and the inflation would track the
  // BOOK -- which is the confound the whole design blocks within a book to
  // avoid.
  const auto r = count({{"S\xc3\xb6hne"}});
  EXPECT_EQ(r.chars, 5u);
  // Four-byte codepoints too: an emoji is one character.
  const auto e = count({{"\xf0\x9f\x93\x96"}});
  EXPECT_EQ(e.chars, 1u);
}

TEST(PageTextMetrics, MalformedUtf8DoesNotRunOffTheEnd) {
  // A stray continuation byte is simply not counted -- it starts no codepoint.
  // The alternative (looking for the sequence it belongs to) reads past the
  // NUL on a truncated token.
  EXPECT_EQ(pagemetrics::codepointsOf("\x80\x80"), 0u);
  EXPECT_EQ(pagemetrics::codepointsOf("a\x80"), 1u);
  // A truncated soft hyphen: 0xC2 with nothing after it. The two-byte check
  // reads p[1], which is the NUL, so it must not match and must not advance
  // past the terminator.
  EXPECT_EQ(pagemetrics::codepointsOf("a\xc2"), 2u);
}

TEST(PageTextMetrics, NullAndEmptyAreSafe) {
  EXPECT_EQ(pagemetrics::codepointsOf(nullptr), 0u);
  EXPECT_EQ(pagemetrics::codepointsOf(""), 0u);
  const auto r = count({{nullptr, "a", ""}});
  EXPECT_EQ(r.words, 1u);
  EXPECT_EQ(r.chars, 1u);
  EXPECT_EQ(r.lines, 1u);
}

TEST(PageTextMetrics, AnEmptyPageIsZeroNotOne) {
  pagemetrics::Counter c;
  const auto r = c.result();
  EXPECT_EQ(r.words, 0u);
  EXPECT_EQ(r.chars, 0u);
  EXPECT_EQ(r.lines, 0u);
}

TEST(PageTextMetrics, ABeginWithoutAnEndContributesNoLine) {
  // The caller `continue`s past a block that is invalid or empty, which can
  // leave a beginLine() unmatched. That must not leak into the next line's
  // count as a phantom.
  pagemetrics::Counter c;
  c.beginLine();
  c.addToken("a");
  c.beginLine();  // abandoned without endLine
  c.addToken("b");
  c.endLine();
  EXPECT_EQ(c.result().lines, 1u);
  EXPECT_EQ(c.result().words, 2u) << "the tokens still counted; only the line tally is affected";
}

}  // namespace
