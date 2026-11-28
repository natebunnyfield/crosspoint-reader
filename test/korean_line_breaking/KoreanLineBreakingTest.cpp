// Hangul line breaking, upstream 794ac97f4 (#3700): "justify Korean text by word spaces only".
//
// Before it, every Hangul word was split into one token per syllable joined by
// zero-width break opportunities, and the justifier stretched every one of
// those joints, so a justified Korean line looked letter-spaced. Now a Hangul
// word is ONE token (like a Latin word): with hyphenation off it wraps at
// spaces only (CSS word-break: keep-all); with hyphenation on it may split at a
// line end between syllables, or where Hangul meets a digit, Latin letter or
// bracket, never before closing or after opening punctuation, and no hyphen is
// drawn. Only word spaces stretch.
//
// The built-in faces carry no Hangul, so every syllable measures as the same
// missing glyph; widths are read back from the renderer instead of hard-coded
// (upstream's suite stubbed the renderer at 8 px a glyph, which this tree does
// not have). The breaker is pinned to greedy (kpbreak off) for the exact-line
// cases, and the Knuth-Plass breaker is held to the same invariants.

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

// A syllable's advance and a space's advance, as this renderer measures them.
int syllable() { return Gfx::instance().renderer().getTextAdvanceX(kFont, "가", EpdFontFamily::REGULAR); }
int wide(const char* text) { return Gfx::instance().renderer().getTextAdvanceX(kFont, text, EpdFontFamily::REGULAR); }
int space() { return Gfx::instance().renderer().getSpaceAdvance(kFont, 0xAC00, 0xAC00, EpdFontFamily::REGULAR); }

std::vector<Line> layout(const std::vector<std::string>& words, const bool hyphenation, const int width,
                         const bool knuthPlass = false) {
  kpbreak::tuning().enabled = knuthPlass;
  Hyphenator::setPreferredLanguage("ko");
  BlockStyle style;
  style.alignment = CssTextAlign::Justify;
  ParsedText text(/*extraParagraphSpacing=*/false,
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

TEST(KoreanLineBreaking, SyllablesAndSpacesMeasureAsExpected) {
  // The fixture the other cases lean on: a syllable is wider than a space, so
  // the widths below can be written in syllables without ambiguity.
  EXPECT_GT(syllable(), space());
  EXPECT_GT(space(), 0);
}

TEST(KoreanLineBreaking, HyphenationOffWrapsAtSpacesOnly) {
  // One 4-syllable word fits a line of 4 syllables and a bit; it never splits and
  // never shares a line with a neighbor.
  const int width = 4 * syllable() + 1;
  const auto lines = layout({"가나", "다라마바", "사"}, false, width);
  EXPECT_EQ(wordsOf(lines), (Lines{{"가나"}, {"다라마바"}, {"사"}}));
}

TEST(KoreanLineBreaking, AHangulWordIsOneTokenSoOnlyWordSpacesStretch) {
  // 가나 + space + 다라: two tokens on the line. Before the change this line held four
  // (one per syllable) and the spare width was shared across all four joints.
  const int width = 5 * syllable() + space() + syllable() / 2;
  const auto lines = layout({"가나", "다라", "마바"}, false, width);
  ASSERT_GE(lines.size(), 1u);
  EXPECT_EQ(lines[0].words, (std::vector<std::string>{"가나", "다라"}));
  // The second word starts after the first plus the (stretched) word space and nothing
  // else: all the spare width landed on that one gap.
  EXPECT_GE(lines[0].xpos[1], 2 * syllable() + space());
}

// The layout engine's own widths are not exactly getTextAdvanceX's (missing glyphs, kerning
// pairs), so these two cases scan every width from one syllable to the whole text instead of
// pinning one number.
TEST(KoreanLineBreaking, HyphenationOnSplitsBetweenSyllablesWithoutHyphen) {
  const std::vector<std::string> words{"가나", "다라마바사아", "자"};
  const std::string original = "가나다라마바사아자";
  bool splitSeen = false;
  for (int width = syllable(); width <= 10 * syllable(); width += 2) {
    const auto lines = layout(words, true, width);
    EXPECT_EQ(joined(lines), original) << width;
    EXPECT_EQ(joined(lines).find('-'), std::string::npos) << width;
    // A split: a line ends with a proper prefix of the long word, and the next line starts with the rest.
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
      const std::string& tail = lines[i].words.back();
      const std::string& head = lines[i + 1].words.front();
      if (tail != "다라마바사아" && tail != "가나" && tail != "자" &&
          (tail + head).find("다라마바사아") != std::string::npos) {
        splitSeen = true;
      }
    }
  }
  EXPECT_TRUE(splitSeen) << "no width split the long Hangul word between syllables";
}

TEST(KoreanLineBreaking, HyphenationOnKeepsTrailingPunctuationWithSyllable) {
  // 라마바사. may split between syllables, but the period never starts a line.
  for (int width = syllable(); width <= 9 * syllable(); width += 2) {
    for (const bool knuthPlass : {false, true}) {
      const auto lines = layout({"가나다", "라마바사.", "자"}, true, width, knuthPlass);
      for (const auto& line : lines) {
        ASSERT_FALSE(line.words.empty());
        EXPECT_NE(line.words.front().front(), '.') << width;
      }
    }
  }
}

TEST(KoreanLineBreaking, KnuthPlassKeepsHangulWordsWholeAndHyphenFree) {
  // The fork breaks justified hyphenating blocks with Knuth-Plass, not greedy, so the
  // same Hangul split points must reach that breaker. Whatever it chooses: the text
  // survives intact, nothing gains a hyphen, and with hyphenation off no word splits.
  const std::vector<std::string> words{"가나", "다라마바사아", "자차카", "타파하", "가나다라마바", "사아"};
  for (const int width : {4 * syllable(), 5 * syllable() + space(), 6 * syllable() + space() + 3}) {
    const auto off = layout(words, false, width, /*knuthPlass=*/true);
    for (const auto& line : off) {
      for (const auto& word : line.words) {
        // A word wider than the line is the one case that may still split (overflow break).
        bool allowed = false;
        for (const auto& original : words) {
          const bool tooWide = static_cast<int>(original.size() / 3) * syllable() > width;
          allowed = allowed || original == word || (tooWide && original.find(word) != std::string::npos);
        }
        EXPECT_TRUE(allowed) << word;
      }
    }
    const auto on = layout(words, true, width, /*knuthPlass=*/true);
    std::string original;
    for (const auto& word : words) original += word;
    EXPECT_EQ(joined(on), original);
    EXPECT_EQ(joined(on).find('-'), std::string::npos);
  }
}
