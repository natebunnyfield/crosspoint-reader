#include <gtest/gtest.h>

#include <string>

#include "components/SingleLineField.h"

namespace {

// 10 px per character (code point), whatever its byte length.
int codepoints(const std::string& s, size_t a, size_t b) {
  int n = 0;
  for (size_t i = a; i < b && i < s.size(); ++i) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
  }
  return n;
}

singleline::Window fitIn(const std::string& s, size_t cursor, int width = 100) {
  // caret reserve 5, ellipsis 10
  return singleline::fit(s, cursor, width, 5, 10, [&](size_t a, size_t b) { return codepoints(s, a, b) * 10; });
}

bool isBoundary(const std::string& s, size_t i) {
  return i == s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80;
}

}  // namespace

TEST(SingleLineField, ShortTextIsWholeWithNoEllipsis) {
  const auto w = fitIn("hello", 5);
  EXPECT_EQ(w.start, 0u);
  EXPECT_EQ(w.end, 5u);
  EXPECT_FALSE(w.leftCut);
  EXPECT_FALSE(w.rightCut);
}

TEST(SingleLineField, LongTextAtTheEndShowsTheTailBehindAnEllipsis) {
  const std::string s(30, 'a');  // 300 px in a 100 px field
  const auto w = fitIn(s, s.size());
  EXPECT_TRUE(w.leftCut);
  EXPECT_FALSE(w.rightCut);
  EXPECT_EQ(w.end, s.size());
  // ellipsis 10 + caret 5 leaves 85 px: 8 characters.
  EXPECT_EQ(w.start, s.size() - 8);
}

TEST(SingleLineField, CursorAtTheStartShowsTheHeadWithATrailingEllipsis) {
  const std::string s(30, 'a');
  const auto w = fitIn(s, 0);
  EXPECT_EQ(w.start, 0u);
  EXPECT_FALSE(w.leftCut);
  EXPECT_TRUE(w.rightCut);
  EXPECT_EQ(w.end, 9u);  // 100 - 10 for the trailing ellipsis: 9 characters
}

TEST(SingleLineField, CursorInTheMiddleStartsTheWindowAtTheCursor) {
  const std::string s(30, 'a');
  const auto w = fitIn(s, 10);
  EXPECT_EQ(w.start, 10u);
  EXPECT_TRUE(w.leftCut);
  EXPECT_TRUE(w.rightCut);
  EXPECT_EQ(w.end, 18u);  // 100 - 10 - 10: 8 characters
}

TEST(SingleLineField, TheWindowAlwaysHoldsTheCursor) {
  const std::string s(40, 'a');
  for (size_t c = 0; c <= s.size(); ++c) {
    const auto w = fitIn(s, c);
    EXPECT_LE(w.start, c) << c;
    EXPECT_GE(w.end, c) << c;
  }
}

TEST(SingleLineField, BoundariesNeverSplitAUtf8Character) {
  std::string s;
  for (int i = 0; i < 20; ++i) s += "\xC3\xA9";  // é, two bytes each
  for (size_t c = 0; c <= s.size(); c += 2) {
    const auto w = fitIn(s, c);
    EXPECT_TRUE(isBoundary(s, w.start)) << c;
    EXPECT_TRUE(isBoundary(s, w.end)) << c;
  }
  // A cursor inside a character snaps back to its first byte.
  const auto w = fitIn(s, 5);
  EXPECT_EQ(w.start, 4u);
}

TEST(SingleLineField, APasswordMaskKeepsTheRevealedCharacterWhole) {
  // A masked password: one '*' per byte, the last character (é) revealed.
  std::string s(30, '*');
  s += "\xC3\xA9";
  const auto w = fitIn(s, s.size());
  EXPECT_EQ(w.end, s.size());
  EXPECT_TRUE(isBoundary(s, w.start));
  EXPECT_LT(w.start, s.size() - 2);  // the revealed character is on screen
}
