#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "spike/MarkdownSpans.h"

namespace {

mdspans::Line an(const std::string& s) { return mdspans::analyze(s.data(), s.size()); }

// Rendered text = spans concatenated, so markers must be gone.
std::string rendered(const std::string& s) {
  const mdspans::Line l = an(s);
  std::string out;
  for (size_t i = 0; i < l.spanCount; ++i) {
    out.append(s.data() + l.bodyStart + l.spans[i].start, l.spans[i].len);
  }
  return out;
}

std::vector<mdspans::Style> styles(const std::string& s) {
  const mdspans::Line l = an(s);
  std::vector<mdspans::Style> v;
  for (size_t i = 0; i < l.spanCount; ++i) v.push_back(l.spans[i].style);
  return v;
}

}  // namespace

TEST(Markdown, Headings) {
  EXPECT_EQ(an("# Title").block, mdspans::Block::Heading1);
  EXPECT_EQ(an("## Sub").block, mdspans::Block::Heading2);
  EXPECT_EQ(an("### Deep").block, mdspans::Block::Heading3);
  EXPECT_EQ(rendered("## Sub"), "Sub");
  EXPECT_TRUE(mdspans::blockIsBold(an("# Title").block));
}

// A hash with no space is a tag, not a heading — common in notes.
TEST(Markdown, HashWithoutSpaceIsNotAHeading) {
  EXPECT_EQ(an("#todo buy milk").block, mdspans::Block::Paragraph);
  EXPECT_EQ(an("#### too deep").block, mdspans::Block::Paragraph);
}

TEST(Markdown, Lists) {
  EXPECT_EQ(an("- item").block, mdspans::Block::Bullet);
  EXPECT_EQ(an("* item").block, mdspans::Block::Bullet);
  EXPECT_EQ(an("1. first").block, mdspans::Block::Numbered);
  EXPECT_EQ(an("12) twelfth").block, mdspans::Block::Numbered);
  EXPECT_EQ(rendered("- item"), "item");
  EXPECT_EQ(an("- item").indent, 1);
}

TEST(Markdown, Quote) {
  EXPECT_EQ(an("> quoted").block, mdspans::Block::Quote);
  EXPECT_EQ(rendered("> quoted"), "quoted");
}

TEST(Markdown, BoldItalicCode) {
  EXPECT_EQ(rendered("a **b** c"), "a b c");
  EXPECT_EQ(styles("a **b** c"),
            (std::vector<mdspans::Style>{mdspans::Style::Regular, mdspans::Style::Bold, mdspans::Style::Regular}));

  EXPECT_EQ(rendered("a _b_ c"), "a b c");
  EXPECT_EQ(styles("a _b_ c")[1], mdspans::Style::Italic);

  EXPECT_EQ(rendered("a `code` c"), "a code c");
  EXPECT_EQ(styles("a `code` c")[1], mdspans::Style::Code);
}

// The editor case: a half-typed marker must not swallow the rest of the line.
TEST(Markdown, UnclosedMarkersStayLiteral) {
  EXPECT_EQ(rendered("half **typed"), "half **typed");
  EXPECT_EQ(rendered("_"), "_");
  EXPECT_EQ(rendered("**"), "**");
  EXPECT_EQ(rendered("a ` b"), "a ` b");
}

// "**bold**" at line start must not be read as a bullet.
TEST(Markdown, LeadingBoldIsNotABullet) {
  EXPECT_EQ(an("**bold** start").block, mdspans::Block::Paragraph);
  EXPECT_EQ(rendered("**bold** start"), "bold start");
}

TEST(Markdown, CombinedBlockAndInline) {
  EXPECT_EQ(rendered("## A **bold** heading"), "A bold heading");
  EXPECT_EQ(an("## A **bold** heading").block, mdspans::Block::Heading2);
  EXPECT_EQ(rendered("- buy _milk_"), "buy milk");
}

TEST(Markdown, CodeFence) { EXPECT_EQ(an("```cpp").block, mdspans::Block::CodeFence); }

TEST(Markdown, PlainLineIsOneRegularSpan) {
  EXPECT_EQ(rendered("just words"), "just words");
  EXPECT_EQ(styles("just words"), (std::vector<mdspans::Style>{mdspans::Style::Regular}));
}

TEST(Markdown, EmptyLine) {
  const auto l = an("");
  EXPECT_EQ(l.block, mdspans::Block::Paragraph);
  EXPECT_EQ(rendered(""), "");
}
