#pragma once

// Grouping a rendered page's word tokens into the read-aloud channel's spoken
// text + per-word rects. Pulled out of EpubReaderActivity's captureReadAloudPage
// so it is pure, header-only and host-testable: the one piece of this path that
// has actually been wrong could not be exercised off-device otherwise.
//
// It is deliberately robust to a NON-RESIDENT reader font. When the font used to
// lay a page out is not loaded at capture time, the renderer reports 0 for its
// line height, space width and every glyph advance. Fed those zeros, the old
// code collapsed a whole section-start page (Standard Ebooks "Uncopyright" was
// the report) into one zero-height element whose words had lost their spaces —
// "May you do good and not evil." became "Mayyoudogoodandnotevil." The two
// guards below keep such a page legible: word spacing survives a missing font,
// and every rect carries a positive height.
//
// The type of an emitted rect is a template parameter so this header needs
// neither the firmware's HalGPIO.h (heavy: Arduino + InputManager) nor the
// simulator's ReadAloudChannel.h — the caller supplies whichever field-identical
// ReadAloudWordRect POD is in scope, and the host test supplies its own.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace readaloud {

// One stored token as the layout left it, in logical portrait pixels. `advance`
// is the width the renderer reports for this token RIGHT NOW — precomputed by
// the caller rather than queried here, both to keep this grouping pure and so
// the value that goes to 0 for a non-resident font is visible to this code as
// plain data.
struct CaptureToken {
  const char* text;  // NUL-terminated; may carry soft hyphens; "blank" == all spaces
  int xpos;          // x within the line (TextBlock::wordXpos)
  int advance;       // renderer advance for this token's text+style; 0 when the font is absent
};

// One display-list line. `hyphenBarrierBefore` is set when a non-line element
// (image, rule) sat between this line and the previous one: it stops a hyphen
// join from reaching across that gap, exactly as the original loop reset its
// pending-join flag on any non-PageLine element.
struct CaptureLine {
  int x;      // line left in page space (line.xPos + xOffset)
  int yTop;   // line top  in page space (line.yPos + yOffset)
  const CaptureToken* tokens;
  size_t count;
  bool hyphenBarrierBefore;
};

// Renderer metrics for the reader font. lineHeight and spaceWidth BOTH read 0
// when that font is not resident — the failure this grouping must survive.
struct CaptureMetrics {
  int lineHeight;          // getLineHeight(fontId); 0 when the font is absent
  int spaceWidth;          // getSpaceWidth(fontId); 0 when the font is absent
  int fallbackLineHeight;  // a positive height to stamp when lineHeight <= 0
};

inline bool tokenIsBlank(const char* t) {
  for (; *t; ++t)
    if (*t != ' ') return false;
  return true;
}

// The stored words may carry soft hyphens (U+00AD); the renderer never draws
// them, and spoken text must not contain them.
inline void appendStrippingSoftHyphens(std::string& out, const char* t) {
  for (const char* p = t; *p;) {
    if (static_cast<unsigned char>(p[0]) == 0xC2 && static_cast<unsigned char>(p[1]) == 0xAD) {
      p += 2;
      continue;
    }
    out.push_back(*p++);
  }
}

// Build the page's spoken text + per-word rects from its per-line tokens.
//
// Rect is any aggregate with { x, y, w, h : integer; byteOffset : integer;
// byteLen : integer } — the firmware's or the simulator's ReadAloudWordRect, or
// a test's stand-in.
template <typename Rect>
inline void buildCapture(const CaptureLine* lines, size_t lineCount, const CaptureMetrics& m, std::string& text,
                         std::vector<Rect>& rects) {
  // A non-resident font reports height 0; stamp a real fallback so assistive
  // tech is never handed the zero-height sliver that hid the original bug.
  const int lineH = m.lineHeight > 0 ? m.lineHeight : m.fallbackLineHeight;
  // The glue run merges tokens the layout placed with no visible gap
  // (punctuation slices, focus splits, CJK). Judging that gap needs a real space
  // width; with none (a non-resident font), a zero advance makes EVERY token
  // look glued to the next, so fall back to one-word-per-token instead.
  const bool haveSpaceMetric = m.spaceWidth > 0;
  const auto clampU16 = [](const int v) { return static_cast<uint16_t>(v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v)); };

  bool pendingHyphenJoin = false;
  for (size_t li = 0; li < lineCount; li++) {
    const CaptureLine& line = lines[li];
    if (line.hyphenBarrierBefore) pendingHyphenJoin = false;
    const int lineX = line.x;
    const int lineTop = line.yTop;
    const size_t n = line.count;
    bool firstRunOnLine = true;
    size_t i = 0;
    while (i < n) {
      if (tokenIsBlank(line.tokens[i].text)) {
        i++;
        continue;
      }
      const int runX = lineX + line.tokens[i].xpos;
      int runEndX = runX;
      std::string runText;
      while (true) {
        appendStrippingSoftHyphens(runText, line.tokens[i].text);
        runEndX = lineX + line.tokens[i].xpos + line.tokens[i].advance;
        i++;
        if (i >= n || tokenIsBlank(line.tokens[i].text)) break;
        // No trustworthy space width -> never glue: keep each token its own word.
        if (!haveSpaceMetric) break;
        if (lineX + line.tokens[i].xpos - runEndX > m.spaceWidth / 2) break;
      }
      if (runText.empty()) continue;  // tokens that were nothing but soft hyphens
      const uint16_t rx = clampU16(runX);
      const uint16_t ry = clampU16(lineTop);
      const uint16_t rw = clampU16(runEndX - runX);
      const uint16_t rh = clampU16(lineH);
      // A word the layout hyphen-split across lines arrives as two tokens whose
      // prefix ends in '-' at the end of its line (ParsedText appends the visible
      // hyphen to the stored prefix). The TextBlock arena carries no source
      // anchors, so reuniting them is a display-list heuristic: a line-final '-'
      // joins to the next line's first word, dropping the hyphen; the joined
      // word's fragments publish one rect each, sharing its byte range. Known
      // false positive: a paragraph-final real hyphen. Rare, and the capture
      // audit (gate G0) watches for it.
      if (pendingHyphenJoin && firstRunOnLine && !rects.empty() && !text.empty() && text.back() == '-') {
        text.pop_back();
        const uint32_t off = rects.back().byteOffset;
        text += runText;
        const uint16_t newLen = static_cast<uint16_t>(std::min<size_t>(text.size() - off, 0xFFFF));
        for (auto it = rects.rbegin(); it != rects.rend() && it->byteOffset == off; ++it) it->byteLen = newLen;
        rects.push_back(Rect{rx, ry, rw, rh, off, newLen});
      } else {
        if (!text.empty()) text.push_back(' ');
        const uint32_t off = static_cast<uint32_t>(text.size());
        text += runText;
        rects.push_back(Rect{rx, ry, rw, rh, off, static_cast<uint16_t>(std::min<size_t>(runText.size(), 0xFFFF))});
      }
      firstRunOnLine = false;
    }
    pendingHyphenJoin = !text.empty() && text.back() == '-';
  }
}

}  // namespace readaloud
