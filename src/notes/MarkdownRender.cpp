#include "MarkdownRender.h"

#include <GfxRenderer.h>

#include <cstdio>
#include <cstring>

#include "notes/MarkdownSpans.h"

namespace mdrender {
namespace {

// One piece buffer per call, on the stack. 192 bytes is inside the 256-byte
// local budget and matches what drawLine has always used.
constexpr size_t PIECE_CAP = 192;

// Which face a span renders in. Named once so drawLine, measureLine, caretX and
// wrapLine cannot disagree about it -- the four of them exist precisely to
// agree, and a copy of this ladder in each was the shape the spacing bug took.
EpdFontFamily::Style styleFor(const mdspans::Line& md, const mdspans::Span& sp) {
  if (mdspans::blockIsBold(md.block) || sp.style == mdspans::Style::Bold) return EpdFontFamily::BOLD;
  if (sp.style == mdspans::Style::Italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

// Copy at most PIECE_CAP-1 bytes of a span into `piece` and terminate it.
void copyPiece(char* piece, const char* src, size_t len) {
  if (len > PIECE_CAP - 1) len = PIECE_CAP - 1;
  memcpy(piece, src, len);
  piece[len] = '\0';
}

bool isBreakSpace(char c) { return c == ' ' || c == '\t'; }

}  // namespace

int advanceOf(const GfxRenderer& renderer, int fontId, const char* piece, EpdFontFamily::Style style) {
  return renderer.getTextAdvanceX(fontId, piece, style);
}

int measureLine(const GfxRenderer& renderer, const int fontId, const int indentStep, const char* text,
                const size_t len) {
  if (text == nullptr || len == 0) return 0;
  const mdspans::Line md = mdspans::analyze(text, len);

  int x = md.indent * indentStep;
  const char* body = text + md.bodyStart;
  char piece[PIECE_CAP];
  for (size_t i = 0; i < md.spanCount; ++i) {
    const mdspans::Span& sp = md.spans[i];
    copyPiece(piece, body + sp.start, sp.len);
    x += advanceOf(renderer, fontId, piece, styleFor(md, sp));
  }
  return x;
}

int caretX(const GfxRenderer& renderer, const int fontId, const int indentStep, const char* text, const size_t len,
           const size_t column) {
  if (text == nullptr || len == 0) return 0;
  const mdspans::Line md = mdspans::analyze(text, len);

  int x = md.indent * indentStep;
  // Anywhere in the block prefix ("## ", "- ", "> ") sits against the body: the
  // prefix either is not drawn at all or lives in the gutter, so there is no
  // column inside it to point at.
  if (column <= md.bodyStart) return x;

  const char* body = text + md.bodyStart;
  const size_t col = column - md.bodyStart;
  char piece[PIECE_CAP];
  for (size_t i = 0; i < md.spanCount; ++i) {
    const mdspans::Span& sp = md.spans[i];
    const EpdFontFamily::Style style = styleFor(md, sp);
    if (col <= sp.start) return x;  // inside the marker that opens this span
    if (col < static_cast<size_t>(sp.start) + sp.len) {
      copyPiece(piece, body + sp.start, col - sp.start);
      return x + advanceOf(renderer, fontId, piece, style);
    }
    copyPiece(piece, body + sp.start, sp.len);
    x += advanceOf(renderer, fontId, piece, style);
  }
  return x;  // past the last span: a closing marker, or the end of the line
}

size_t wrapLine(const GfxRenderer& renderer, const int fontId, const int indentStep, const char* text, const size_t len,
                const int maxWidth, Fragment* out, const size_t maxFragments) {
  if (out == nullptr || maxFragments == 0) return 0;
  if (text == nullptr || len == 0 || maxWidth <= 0) {
    out[0] = Fragment{0, 0};
    return 1;
  }

  size_t count = 0;
  size_t start = 0;
  while (start < len && count < maxFragments) {
    // Candidate ends, ascending: every space, then the end of the line. The
    // rendered width grows monotonically with the prefix, so the first
    // candidate that does not fit bounds the scan -- this stays linear in the
    // fragment rather than in the line.
    size_t best = 0;
    for (size_t j = start + 1; j <= len; ++j) {
      if (j != len && !isBreakSpace(text[j])) continue;
      if (measureLine(renderer, fontId, indentStep, text + start, j - start) > maxWidth) break;
      best = j;
      if (j == len) break;
    }

    if (best == 0) {
      // One word wider than the line. Cut at the widest prefix that fits,
      // stepping back over UTF-8 continuation bytes so a multi-byte character
      // is never split into a replacement glyph.
      size_t j = len;
      while (j > start + 1) {
        --j;
        while (j > start + 1 && (static_cast<unsigned char>(text[j]) & 0xC0) == 0x80) --j;
        if (measureLine(renderer, fontId, indentStep, text + start, j - start) <= maxWidth) break;
      }
      best = j > start ? j : start + 1;
    }

    out[count++] = Fragment{static_cast<uint16_t>(start), static_cast<uint16_t>(best)};
    start = best;
    while (start < len && isBreakSpace(text[start])) ++start;  // drop the space the break was taken at
  }
  return count;
}

int drawLine(const GfxRenderer& renderer, int fontId, int indentStep, int originX, int y, const char* text, size_t len,
             int maxX) {
  const mdspans::Line md = mdspans::analyze(text, len);

  int x = originX + md.indent * indentStep;
  const char* body = text + md.bodyStart;

  // List/quote markers stay visible so the source is still recognisable while
  // editing. The numbered marker is copied from the text rather than invented,
  // so "3." and "12)" keep their real value.
  if (md.block == mdspans::Block::Bullet) {
    renderer.drawText(fontId, originX, y, "-");
  } else if (md.block == mdspans::Block::Quote) {
    renderer.drawText(fontId, originX, y, ">");
  } else if (md.block == mdspans::Block::Numbered) {
    char marker[8];
    size_t m = 0;
    for (size_t k = 0; k < md.bodyStart && m < sizeof(marker) - 1; ++k) {
      if (text[k] != ' ' && text[k] != '\t') marker[m++] = text[k];
    }
    marker[m] = '\0';
    renderer.drawText(fontId, originX, y, marker);
  }

  char piece[PIECE_CAP];
  for (size_t i = 0; i < md.spanCount; ++i) {
    const mdspans::Span& sp = md.spans[i];
    copyPiece(piece, body + sp.start, sp.len);
    const EpdFontFamily::Style style = styleFor(md, sp);
    if (maxX > 0) {
      // Cut to what fits. Trailing UTF-8 continuation bytes (0b10xxxxxx) are
      // dropped with their lead byte, so a multi-byte character is never split
      // into a replacement glyph.
      while (piece[0] != '\0' && x + advanceOf(renderer, fontId, piece, style) > maxX) {
        size_t cut = strlen(piece);
        do {
          --cut;
        } while (cut > 0 && (static_cast<unsigned char>(piece[cut]) & 0xC0) == 0x80);
        piece[cut] = '\0';
      }
      if (piece[0] == '\0') break;
    }
    renderer.drawText(fontId, x, y, piece, true, style);
    x += advanceOf(renderer, fontId, piece, style);
  }

  return x;
}

}  // namespace mdrender
