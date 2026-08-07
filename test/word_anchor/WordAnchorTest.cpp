// Word-anchor layout invariance.
//
// The reader preserves position across a font/size/spacing reflow by anchoring
// the current page's first word to its SOURCE byte offset (ParsedText::
// wordSourceStart -> parser chapter anchor -> Section v35 word LUT). The whole
// mechanism rests on one contract, asserted here against the real layout
// engine:
//
//   The anchor attached to a line's first fragment identifies the same source
//   word under ANY layout — different point size, different line width,
//   hyphenation on or off. Line breaking and hyphenation may split words into
//   different fragments per layout, but a fragment's anchor is always the
//   offset its source word was added at.
//
// This is the invariant whose absence produced the field bug: paragraph-level
// anchors resolved every page of a <p>-less chapter to paragraph 0, so any
// size change rewound the reader to the chapter start (reproduced in the
// simulator on Leaves of Grass before the fix).

#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <builtinFonts/all.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

#include "Epub/ParsedText.h"
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

// The specimen: plain prose plus accents and deliberately long words so a
// narrow layout must hyphenate or fallback-split.
const std::vector<std::string>& specimenWords() {
  static const std::vector<std::string> words = [] {
    std::vector<std::string> w;
    const char* text[] = {"Aging",
                          "quickly,",
                          "both",
                          "jovial",
                          "elephants",
                          "prayed",
                          "for",
                          "extra",
                          "warmth.",
                          "The",
                          "zephyr",
                          "blew",
                          "across",
                          "the",
                          "fjord",
                          "at",
                          "dawn,",
                          "and",
                          "Aunt",
                          "Ágnes",
                          "buttoned",
                          "her",
                          "coat",
                          "to",
                          "the",
                          "chin.",
                          "Extraordinarily",
                          "incomprehensible",
                          "responsibilities",
                          "notwithstanding,",
                          "the",
                          "naïve",
                          "café",
                          "regulars",
                          "reconvened.",
                          "Beyond",
                          "thy",
                          "lectures",
                          "learn'd",
                          "professor,",
                          "beyond",
                          "all",
                          "mathematics,",
                          "the",
                          "entities",
                          "of",
                          "entities,",
                          "eidolons.",
                          "Unfix'd",
                          "yet",
                          "fix'd,",
                          "ever",
                          "shall",
                          "be,",
                          "ever",
                          "have",
                          "been",
                          "and",
                          "are,",
                          "sweeping",
                          "the",
                          "present",
                          "to",
                          "the",
                          "infinite",
                          "future,",
                          "eidolons,",
                          "eidolons,",
                          "eidolons."};
    for (const char* t : text) w.emplace_back(t);
    // Repeat to span several viewport heights of lines.
    std::vector<std::string> out;
    for (int rep = 0; rep < 4; rep++) {
      for (const auto& s : w) out.push_back(s);
    }
    return out;
  }();
  return words;
}

// Expected anchors, derived independently of layout: prefix sums of the words'
// byte lengths in addWord order (the specimen is already NFC, so composition
// inside addWord is an identity here).
std::map<uint32_t, std::string> expectedAnchorMap() {
  std::map<uint32_t, std::string> m;
  uint32_t off = 0;
  for (const auto& w : specimenWords()) {
    m[off] = w;
    off += static_cast<uint32_t>(w.size());
  }
  return m;
}

struct LineRecord {
  uint32_t anchor;
  std::string firstFragment;
};

// Lay the specimen out and record (anchor, first fragment text) per line.
std::vector<LineRecord> layout(const int fontId, const uint16_t width, const bool hyphenate) {
  ParsedText block(false, hyphenate, false);
  for (const auto& w : specimenWords()) {
    block.addWord(w, EpdFontFamily::REGULAR);
  }
  std::vector<LineRecord> lines;
  block.layoutAndExtractLines(Gfx::instance().renderer(), fontId, width, [&](const std::shared_ptr<TextBlock>& line) {
    LineRecord r;
    r.anchor = block.lastExtractedLineSourceStart();
    r.firstFragment = line->wordCount() > 0 ? std::string(line->wordText(0)) : std::string();
    lines.push_back(std::move(r));
  });
  return lines;
}

// The source word an anchor identifies, or "" when the anchor is not a word
// start (which the tests treat as a failure).
std::string wordAt(const std::map<uint32_t, std::string>& expect, const uint32_t anchor) {
  const auto it = expect.find(anchor);
  return it == expect.end() ? std::string() : it->second;
}

}  // namespace

// Every line-start anchor is a real source-word offset, and anchors are
// non-decreasing (pages built from these lines get a monotone LUT, which the
// reposition binary walk depends on).
TEST(WordAnchor, AnchorsAreSourceWordStartsAndMonotone) {
  const auto expect = expectedAnchorMap();
  for (const bool hyphenate : {false, true}) {
    const auto lines = layout(LIBREFRANKLIN_READER_12_FONT_ID, 400, hyphenate);
    ASSERT_GT(lines.size(), 20u) << "specimen did not span enough lines";
    uint32_t prev = 0;
    for (const auto& line : lines) {
      EXPECT_GE(line.anchor, prev) << "anchor regressed (hyphenate=" << hyphenate << ")";
      prev = line.anchor;
      const std::string word = wordAt(expect, line.anchor);
      ASSERT_FALSE(word.empty()) << "anchor " << line.anchor << " is not a source word start (hyphenate=" << hyphenate
                                 << ", fragment '" << line.firstFragment << "')";
      // A continuation fragment ("kilometer" of a split word) keeps the SOURCE
      // word's anchor; a plain fragment must be a prefix of the word there.
      if (!hyphenate) {
        EXPECT_EQ(word.rfind(line.firstFragment, 0), 0u)
            << "line starts with '" << line.firstFragment << "' but anchor points at '" << word << "'";
      }
    }
  }
}

// The reflow contract itself: the same anchor identifies the same source word
// under four different layouts (two sizes x two widths, hyphenation on).
TEST(WordAnchor, AnchorsIdentifySameWordAcrossReflows) {
  const auto expect = expectedAnchorMap();
  struct Cfg {
    int fontId;
    uint16_t width;
  };
  const Cfg cfgs[] = {{LIBREFRANKLIN_READER_12_FONT_ID, 440},
                      {LIBREFRANKLIN_READER_12_FONT_ID, 300},
                      {LIBREFRANKLIN_READER_18_FONT_ID, 440},
                      {LIBREFRANKLIN_READER_18_FONT_ID, 300}};
  std::map<uint32_t, std::string> seen;  // anchor -> source word, across all layouts
  for (const auto& cfg : cfgs) {
    const auto lines = layout(cfg.fontId, cfg.width, true);
    for (const auto& line : lines) {
      const std::string word = wordAt(expect, line.anchor);
      ASSERT_FALSE(word.empty());
      const auto [it, inserted] = seen.emplace(line.anchor, word);
      if (!inserted) {
        EXPECT_EQ(it->second, word) << "anchor " << line.anchor << " changed identity across layouts";
      }
    }
  }
  // The layouts genuinely differ (different fragmentations), or this test
  // proves nothing.
  ASSERT_GT(seen.size(), 40u);
}
