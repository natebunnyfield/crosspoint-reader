// The spaced-sign rule at the ParsedText level: real layout (ParsedText ->
// TextBlock), built-in LibreFranklin reader 12, one-line paragraphs so nothing
// is justified. A gap beside a lone sign takes GfxRenderer::getSpacedSignGap;
// every other gap is exactly the plain getSpaceAdvance the layout always used.

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Epub/ParsedText.h"
#include "fontIds.h"

extern HalDisplay display;  // WordAnchorTest.cpp

namespace {

uint32_t firstCp(const std::string& w) { return static_cast<unsigned char>(w.front()); }  // ASCII words only
uint32_t lastCp(const std::string& w) { return static_cast<unsigned char>(w.back()); }

class SpacedSignLayout : public ::testing::Test {
 protected:
  SpacedSignLayout() : cache_(renderer_.getFontMap(), renderer_.getSdCardFonts()) {
    renderer_.begin();
    EXPECT_TRUE(decompressor_.init());
    cache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&cache_);
    renderer_.insertFont(kFont, family_);
  }

  struct Laid {
    std::vector<std::string> words;
    std::vector<int> gaps;  // gaps[i]: between words[i] and words[i + 1]
  };

  // Lay one line out and read each gap back off the TextBlock: the next word's
  // x minus this word's x minus this word's measured width.
  Laid layOut(const std::vector<std::string>& words) {
    ParsedText block(false);
    for (const auto& w : words) block.addWord(w, EpdFontFamily::REGULAR);
    Laid out;
    int lines = 0;
    block.layoutAndExtractLines(renderer_, kFont, 480, [&](std::unique_ptr<TextBlock> line) {
      lines++;
      for (uint16_t i = 0; i < line->wordCount(); i++) {
        out.words.emplace_back(line->wordText(i), line->wordTextLen(i));
      }
      for (uint16_t i = 0; i + 1 < line->wordCount(); i++) {
        const int width = renderer_.getTextAdvanceX(kFont, out.words[i].c_str(), EpdFontFamily::REGULAR);
        out.gaps.push_back(line->wordXpos(i + 1) - line->wordXpos(i) - width);
      }
    });
    EXPECT_EQ(lines, 1);
    EXPECT_EQ(out.words, words);
    return out;
  }

  int plain(const std::string& l, const std::string& r) {
    return renderer_.getSpaceAdvance(kFont, lastCp(l), firstCp(r), EpdFontFamily::REGULAR);
  }

  static constexpr int kFont = LIBREFRANKLIN_READER_12_FONT_ID;
  GfxRenderer renderer_{display};
  FontDecompressor decompressor_;
  FontCacheManager cache_;
  EpdFont r_{&librefranklin_reader_12_regular}, b_{&librefranklin_reader_12_bold}, i_{&librefranklin_reader_12_italic},
      bi_{&librefranklin_reader_12_bolditalic};
  EpdFontFamily family_{&r_, &b_, &i_, &bi_};
};

}  // namespace

// A gap with no lone sign beside it is the plain space, to the pixel -- prose,
// and prose or code that merely contains a sign.
TEST_F(SpacedSignLayout, GapsWithoutALoneSignAreThePlainSpace) {
  const std::vector<std::vector<std::string>> lines = {
      {"The", "quiet", "river", "ran", "past", "the", "mill."},
      {"C++", "is", "a", "language"},
      {"A+", "grade", "for", "18+", "years"},
      {"a", "->", "b", "and", "a", "!=", "b"},
  };
  for (const auto& words : lines) {
    const Laid laid = layOut(words);
    ASSERT_EQ(laid.gaps.size(), words.size() - 1);
    for (size_t i = 0; i < laid.gaps.size(); i++) {
      EXPECT_EQ(laid.gaps[i], plain(words[i], words[i + 1])) << words[i] << " | " << words[i + 1];
    }
  }
}

// A gap beside a lone sign takes the rule's space, and on this face that moves
// at least one gap off the plain space (the fixture discriminates).
TEST_F(SpacedSignLayout, GapsBesideALoneSignTakeTheRule) {
  const std::vector<std::vector<std::string>> lines = {{"a", "<", "b"}, {"3", "+", "4", "=", "7"}, {"1", "=", "2"}};
  int moved = 0;
  for (const auto& words : lines) {
    const Laid laid = layOut(words);
    ASSERT_EQ(laid.gaps.size(), words.size() - 1);
    for (size_t i = 0; i < laid.gaps.size(); i++) {
      const int ruled =
          renderer_.getSpacedSignGap(kFont, lastCp(words[i]), EpdFontFamily::REGULAR, firstCp(words[i + 1]),
                                     EpdFontFamily::REGULAR, EpdFontFamily::REGULAR);
      ASSERT_GE(ruled, 0);
      EXPECT_EQ(laid.gaps[i], ruled) << words[i] << " | " << words[i + 1];
      if (laid.gaps[i] != plain(words[i], words[i + 1])) moved++;
    }
  }
  EXPECT_GT(moved, 0) << "no sign gap moved: the fixture does not discriminate";
}
