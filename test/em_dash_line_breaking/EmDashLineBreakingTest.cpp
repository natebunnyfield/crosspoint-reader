// Line breaks after visible hyphens and dashes, upstream 9dfa92ba2 (#3883).
//
// "foo\u2014bar" used to be one token, so it could only break where the hyphenator found a point
// (and not at all with hyphenation off). Now it is two, "foo\u2014" and "bar": attached when they
// share a line (no space is inserted), free to break between. A run of dashes stays together, a
// dash at the start of a word has nothing to break after, and text may not begin a line with a
// dash. Widths are read back from the renderer (the built-in Libre Franklin face) instead of
// upstream's 8 px stub metrics, which this tree does not have.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Epub/KnuthPlassBreaker.h"
#include "Epub/LineBreakMode.h"
#include "Epub/ParsedText.h"
#include "Epub/hyphenation/Hyphenator.h"
#include "fontIds.h"

HalDisplay display;

namespace {

constexpr int kFont = LIBREFRANKLIN_READER_14_FONT_ID;

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
    renderer_.insertFont(kFont, lfReader14_);
  }

  GfxRenderer renderer_;
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont lfr14R_{&librefranklin_reader_14_regular}, lfr14B_{&librefranklin_reader_14_bold},
      lfr14I_{&librefranklin_reader_14_italic}, lfr14BI_{&librefranklin_reader_14_bolditalic};
  EpdFontFamily lfReader14_{&lfr14R_, &lfr14B_, &lfr14I_, &lfr14BI_};
};

struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> xpos;
};

int wide(const char* text) { return Gfx::instance().renderer().getTextAdvanceX(kFont, text, EpdFontFamily::REGULAR); }

std::vector<Line> layout(const std::vector<std::string>& words, const bool hyphenation, const int width,
                         const bool knuthPlass = false) {
  kpbreak::tuning().enabled = knuthPlass;
  Hyphenator::setPreferredLanguage("en");
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  ParsedText text(/*extraParagraphSpacing=*/true,
                  hyphenation ? linebreak::STORED_HYPHENATED : linebreak::STORED_WHOLE_WORDS,
                  /*focusReadingEnabled=*/false, style);
  for (const auto& word : words) text.addWord(word, EpdFontFamily::REGULAR);
  std::vector<Line> lines;
  text.layoutAndExtractLines(Gfx::instance().renderer(), kFont, static_cast<uint16_t>(width),
                             [&](std::unique_ptr<TextBlock> block) {
                               auto& line = lines.emplace_back();
                               for (uint16_t i = 0; i < block->wordCount(); ++i) {
                                 line.words.emplace_back(block->wordText(i));
                                 line.xpos.push_back(block->wordXpos(i));
                               }
                             });
  return lines;
}

std::vector<std::vector<std::string>> wordsOf(const std::vector<Line>& lines) {
  std::vector<std::vector<std::string>> result;
  for (const auto& line : lines) result.push_back(line.words);
  return result;
}

std::string joined(const std::vector<Line>& lines) {
  std::string all;
  for (const auto& line : lines) {
    for (const auto& word : line.words) all += word;
  }
  return all;
}

using Lines = std::vector<std::vector<std::string>>;

}  // namespace

TEST(EmDashLineBreaking, HyphenationOffSplitsAcrossLines) {
  const auto lines = layout({"foo\xe2\x80\x94"
                             "bar"},
                            false, wide("foo\xe2\x80\x94") + 2);
  EXPECT_EQ(wordsOf(lines), (Lines{{"foo\xe2\x80\x94"}, {"bar"}}));
}

TEST(EmDashLineBreaking, KeepsTogetherOnTheSameLineWithoutAGap) {
  const auto lines = layout({"foo\xe2\x80\x94"
                             "bar"},
                            false,
                            4 * wide("foo\xe2\x80\x94"
                                     "bar"));
  ASSERT_EQ(wordsOf(lines), (Lines{{"foo\xe2\x80\x94", "bar"}}));
  // "bar" starts exactly where "foo\u2014" ends, plus the pair's kerning: no word space.
  const int kern = Gfx::instance().renderer().getKerning(kFont, 0x2014, 'b', EpdFontFamily::REGULAR);
  EXPECT_EQ(lines[0].xpos[1], wide("foo\xe2\x80\x94") + kern);
}

TEST(EmDashLineBreaking, ARunOfDashesStaysTogether) {
  const auto lines = layout({"foo\xe2\x80\x94\xe2\x80\x94"
                             "bar"},
                            false, wide("foo\xe2\x80\x94\xe2\x80\x94") + 2);
  EXPECT_EQ(wordsOf(lines), (Lines{{"foo\xe2\x80\x94\xe2\x80\x94"}, {"bar"}}));
}

TEST(EmDashLineBreaking, EveryDashInAWordIsABreakOpportunity) {
  const int width = std::max(wide("foo\xe2\x80\x94"), wide("bar\xe2\x80\x94")) + 1;
  const auto lines = layout({"foo\xe2\x80\x94"
                             "bar\xe2\x80\x94"
                             "baz"},
                            false, width);
  EXPECT_EQ(wordsOf(lines), (Lines{{"foo\xe2\x80\x94"}, {"bar\xe2\x80\x94"}, {"baz"}}));
}

TEST(EmDashLineBreaking, AHyphenMinusBreaksTheSameWay) {
  const auto lines = layout({"well-known"}, false, wide("well-") + 2);
  EXPECT_EQ(wordsOf(lines), (Lines{{"well-"}, {"known"}}));
}

TEST(EmDashLineBreaking, ADashOpeningAWordHasNothingToBreakAfter) {
  const auto lines = layout({"\xe2\x80\x94"
                             "bar"},
                            false,
                            4 * wide("\xe2\x80\x94"
                                     "bar"));
  EXPECT_EQ(wordsOf(lines), (Lines{{"\xe2\x80\x94"
                                    "bar"}}));
}

TEST(EmDashLineBreaking, HyphenationOnSplitsWithoutAnInsertedHyphen) {
  for (const bool knuthPlass : {false, true}) {
    const auto lines = layout({"foo\xe2\x80\x94"
                               "bar"},
                              true, wide("foo\xe2\x80\x94") + 2, knuthPlass);
    EXPECT_EQ(wordsOf(lines), (Lines{{"foo\xe2\x80\x94"}, {"bar"}})) << "knuthPlass=" << knuthPlass;
  }
}

TEST(EmDashLineBreaking, MixedCjkAndLatinSplitsAtTheDash) {
  // The built-in face has no Han glyphs, so each ideograph measures as the same missing glyph.
  const std::string text =
      "\xe6\xbc\xa2\xe5\xad\x97\xe2\x80\x94"
      "bar";  // 漢字—bar
  const auto lines = layout({text}, false, wide("\xe6\xbc\xa2\xe5\xad\x97\xe2\x80\x94") + 2);
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[1].words, (std::vector<std::string>{"bar"}));
  EXPECT_EQ(lines[0].words.back(), "\xe5\xad\x97\xe2\x80\x94");
}
