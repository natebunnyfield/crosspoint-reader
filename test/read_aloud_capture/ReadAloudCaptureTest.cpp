// Read-aloud capture grouping — spacing and rect height survive a non-resident
// reader font.
//
// The bug this pins: on some EPUB section-start pages (Standard Ebooks
// "Uncopyright" was the report) the reader font used to lay the page out was not
// resident when the page was captured, so the renderer reported 0 for its line
// height, space width and every glyph advance. Fed those zeros, captureReadAloud
// laid every word of a line on top of the next — the pixel-gap glue test
// (gap <= spaceWidth/2) reads 0 <= 0 as "glued" — and stamped a zero-height rect
// on each. An iOS accessibility probe saw one element,
// "Uncopyright Mayyoudogoodandnotevil. …", frame height 0.
//
// The grouping is exercised directly (ReadAloudCapture.h is pure and header
// only), with inputs modelled on the REAL display list the simulator dumped for
// that page:
//   * a font-less layout collapses every word to xpos 0 with advance 0, so the
//     tokens below use those values, and the metrics are all 0;
//   * a resident layout keeps the real xpos/advance the simulator printed
//     (May x=0 adv=76, you x=91 adv=61, …; space width 15), so the
//     behaviour-preservation cases test the exact numbers that ship.
// The adapter that fills these inputs from a Page + GfxRenderer is thin and is
// covered by the simulator build (its output is byte-identical before and after
// the fix on a resident-font page).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ReadAloudCapture.h"

namespace {

using readaloud::CaptureLine;
using readaloud::CaptureMetrics;
using readaloud::CaptureToken;

// Field-identical stand-in for the firmware/simulator ReadAloudWordRect POD, so
// the test pulls in neither HalGPIO.h (Arduino + InputManager) nor the
// simulator's channel header.
struct Rect {
  uint16_t x, y, w, h;
  uint32_t byteOffset;
  uint16_t byteLen;
};

// Builds the pointer-stable line/token storage buildCapture() expects.
class PageBuilder {
 public:
  PageBuilder& line(int x, int yTop, std::vector<CaptureToken> tokens, bool barrierBefore = false) {
    lineTokens_.push_back(std::move(tokens));
    protoLines_.push_back({x, yTop, barrierBefore});
    return *this;
  }

  std::string run(const CaptureMetrics& m, std::vector<Rect>& rects) {
    std::vector<CaptureLine> lines;
    lines.reserve(protoLines_.size());
    for (size_t i = 0; i < protoLines_.size(); i++) {
      lines.push_back({protoLines_[i].x, protoLines_[i].yTop, lineTokens_[i].data(), lineTokens_[i].size(),
                       protoLines_[i].barrierBefore});
    }
    std::string text;
    rects.clear();
    readaloud::buildCapture(lines.data(), lines.size(), m, text, rects);
    return text;
  }

 private:
  struct Proto {
    int x, yTop;
    bool barrierBefore;
  };
  std::vector<std::vector<CaptureToken>> lineTokens_;  // stable: inner buffers survive outer reallocation
  std::vector<Proto> protoLines_;
};

CaptureToken tok(const char* text, int xpos, int advance) { return CaptureToken{text, xpos, advance}; }

// A reader font that is not resident: every metric the renderer can report is 0.
CaptureMetrics absentFont(int fallbackLineHeight) { return CaptureMetrics{0, 0, fallbackLineHeight}; }

// A resident font: real line height and a real space width (Coelacanth 20 pt on
// the reported page had a ~15 px space).
CaptureMetrics residentFont(int lineHeight = 55, int spaceWidth = 15) {
  return CaptureMetrics{lineHeight, spaceWidth, /*fallback unused*/ 24};
}

bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

}  // namespace

// THE BUG. A font-less layout collapses "May you do good" / "and not evil." to
// xpos 0 with advance 0; metrics are all 0. The words must NOT merge.
TEST(ReadAloudCapture, NonResidentFontKeepsVerseWordSpacing) {
  PageBuilder page;
  page.line(86, 0, {tok("May", 0, 0), tok("you", 0, 0), tok("do", 0, 0), tok("good", 0, 0)})
      .line(86, 0, {tok("and", 0, 0), tok("not", 0, 0), tok("evil.", 0, 0)});

  std::vector<Rect> rects;
  const std::string text = page.run(absentFont(30), rects);

  EXPECT_EQ(text, "May you do good and not evil.");
  EXPECT_FALSE(contains(text, "Mayyou")) << "words merged into a blob: '" << text << "'";
  EXPECT_EQ(rects.size(), 7u) << "expected one rect per word, got a merged run";
}

// A font-less layout must still stamp a real height on every rect, never the
// zero-height sliver getLineHeight() returns for an absent font.
TEST(ReadAloudCapture, NonResidentFontGivesRectsRealHeight) {
  PageBuilder page;
  page.line(86, 0, {tok("May", 0, 0), tok("you", 0, 0)});

  std::vector<Rect> rects;
  page.run(absentFont(30), rects);

  ASSERT_FALSE(rects.empty());
  for (const Rect& r : rects) EXPECT_EQ(r.h, 30) << "rect kept the zero height of an absent font";
}

// The accessibility probe saw the heading and the whole verse as ONE element
// ("Uncopyright Mayyoudogoodandnotevil. …") because the font-less layout stacked
// every line at y 0. Spacing must survive even so — the words read back whole.
TEST(ReadAloudCapture, HeadingAndVerseCollapsedToOneYStillReadsAsWords) {
  PageBuilder page;
  page.line(132, 0, {tok("Uncopyright", 0, 0)})
      .line(86, 0, {tok("May", 0, 0), tok("you", 0, 0), tok("do", 0, 0), tok("good", 0, 0)})
      .line(86, 0, {tok("and", 0, 0), tok("not", 0, 0), tok("evil.", 0, 0)});

  std::vector<Rect> rects;
  const std::string text = page.run(absentFont(30), rects);

  EXPECT_EQ(text, "Uncopyright May you do good and not evil.");
}

// Behaviour preservation: with a resident font the real xpos/advance pixel-gaps
// separate the words exactly as they shipped, and rects carry the real height.
TEST(ReadAloudCapture, ResidentFontSeparatesWordsByPixelGap) {
  PageBuilder page;
  page.line(86, 369, {tok("May", 0, 76), tok("you", 91, 61), tok("do", 167, 42), tok("good", 224, 83)});

  std::vector<Rect> rects;
  const std::string text = page.run(residentFont(/*lineHeight=*/55, /*spaceWidth=*/15), rects);

  EXPECT_EQ(text, "May you do good");
  ASSERT_EQ(rects.size(), 4u);
  EXPECT_EQ(rects[0].x, 86);   // lineX + xpos 0
  EXPECT_EQ(rects[0].w, 76);   // advance
  EXPECT_EQ(rects[1].x, 177);  // lineX 86 + xpos 91
  EXPECT_EQ(rects[1].w, 61);
  for (const Rect& r : rects) EXPECT_EQ(r.h, 55);
}

// Behaviour preservation: the glue run still fuses two tokens the layout placed
// with no visible gap (a punctuation slice) when the space metric is trustworthy.
TEST(ReadAloudCapture, ResidentFontGluesAdjacentPunctuation) {
  PageBuilder page;
  // "evil" ends at 135+48=183, and "." starts at 183 — gap 0 <= spaceWidth/2.
  page.line(0, 0, {tok("evil", 135, 48), tok(".", 183, 15)});

  std::vector<Rect> rects;
  const std::string text = page.run(residentFont(/*lineHeight=*/55, /*spaceWidth=*/15), rects);

  EXPECT_EQ(text, "evil.");
  EXPECT_EQ(rects.size(), 1u) << "adjacent punctuation should stay one run";
}

// Behaviour preservation: a word the layout hyphen-split across two lines is
// rejoined (hyphen dropped) with a resident font.
TEST(ReadAloudCapture, ResidentFontRejoinsHyphenSplitWord) {
  PageBuilder page;
  page.line(86, 506, {tok("find", 0, 90), tok("for-", 245, 61)}).line(86, 561, {tok("giveness", 0, 134)});

  std::vector<Rect> rects;
  const std::string text = page.run(residentFont(), rects);

  EXPECT_TRUE(contains(text, "forgiveness")) << "hyphen-split word not rejoined: '" << text << "'";
  EXPECT_FALSE(contains(text, "for-")) << "visible hyphen leaked into spoken text: '" << text << "'";
}

// A non-line element (image, rule) between two lines breaks a pending hyphen
// join, so a line-final '-' before it is a real hyphen and stays.
TEST(ReadAloudCapture, NonLineBarrierBreaksHyphenJoin) {
  PageBuilder page;
  page.line(0, 0, {tok("wall-", 0, 90)}).line(0, 55, {tok("flower", 0, 120)}, /*barrierBefore=*/true);

  std::vector<Rect> rects;
  const std::string text = page.run(residentFont(), rects);

  EXPECT_EQ(text, "wall- flower");
}
