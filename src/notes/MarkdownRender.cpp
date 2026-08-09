#include "MarkdownRender.h"

#include <GfxRenderer.h>

#include <cstdio>
#include <cstring>

#include "notes/MarkdownSpans.h"

namespace mdrender {

int advanceOf(GfxRenderer& renderer, int fontId, const char* piece, EpdFontFamily::Style style) {
  char probe[200];
  snprintf(probe, sizeof(probe), "%s|", piece);
  return renderer.getTextWidth(fontId, probe, style) - renderer.getTextWidth(fontId, "|", style);
}

int drawLine(GfxRenderer& renderer, int fontId, int indentStep, int originX, int y, const char* text, size_t len,
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

  char piece[192];
  for (size_t i = 0; i < md.spanCount; ++i) {
    const mdspans::Span& sp = md.spans[i];
    size_t n = sp.len;
    if (n > sizeof(piece) - 1) n = sizeof(piece) - 1;
    memcpy(piece, body + sp.start, n);
    piece[n] = '\0';

    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    if (mdspans::blockIsBold(md.block) || sp.style == mdspans::Style::Bold) {
      style = EpdFontFamily::BOLD;
    } else if (sp.style == mdspans::Style::Italic) {
      style = EpdFontFamily::ITALIC;
    }
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
