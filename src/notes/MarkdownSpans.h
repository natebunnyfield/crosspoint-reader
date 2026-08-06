// Markdown-lite line analysis for the note editor and viewer.
//
// Deliberately small and allocation-free: one line in, a block kind plus a
// handful of inline spans out, all on the caller's stack. Host-testable
// (test/markdown_spans) because it touches no renderer and no Arduino API.
//
// Supported, per the owner's TODO: #/##/### headings (bold), **bold**,
// _italic_ / *italic*, `code`, -/*/1. list prefixes with a hanging indent, and
// > blockquote. Tables and images are deliberately NOT rendered.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace mdspans {

enum class Block : uint8_t {
  Paragraph,
  Heading1,
  Heading2,
  Heading3,
  Bullet,
  Numbered,
  Quote,
  CodeFence,  // the ``` line itself
};

enum class Style : uint8_t { Regular, Bold, Italic, BoldItalic, Code };

struct Span {
  uint16_t start;  // offset into the line body (after any block marker)
  uint16_t len;
  Style style;
};

struct Line {
  Block block = Block::Paragraph;
  uint16_t bodyStart = 0;  // offset of the text after "## ", "- ", "> " etc.
  uint8_t indent = 0;      // hanging-indent steps for list/quote rendering
  size_t spanCount = 0;
  Span spans[16];
};

inline bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Classify the block and find where its body text starts.
inline void classify(const char* text, size_t len, Line& out) {
  size_t i = 0;
  while (i < len && isSpace(text[i])) ++i;

  if (i + 2 < len && text[i] == '`' && text[i + 1] == '`' && text[i + 2] == '`') {
    out.block = Block::CodeFence;
    out.bodyStart = static_cast<uint16_t>(i);
    return;
  }

  if (i < len && text[i] == '#') {
    size_t hashes = 0;
    while (i + hashes < len && text[i + hashes] == '#' && hashes < 4) ++hashes;
    // "####" and deeper are not headings here; and a hash must be followed by a
    // space to count, so a bare "#tag" stays a paragraph.
    if (hashes >= 1 && hashes <= 3 && i + hashes < len && isSpace(text[i + hashes])) {
      out.block = hashes == 1 ? Block::Heading1 : (hashes == 2 ? Block::Heading2 : Block::Heading3);
      size_t b = i + hashes;
      while (b < len && isSpace(text[b])) ++b;
      out.bodyStart = static_cast<uint16_t>(b);
      return;
    }
  }

  if (i < len && text[i] == '>') {
    out.block = Block::Quote;
    out.indent = 1;
    size_t b = i + 1;
    while (b < len && isSpace(text[b])) ++b;
    out.bodyStart = static_cast<uint16_t>(b);
    return;
  }

  // "- " or "* " (but not "**bold**" at line start, which has no space).
  if (i + 1 < len && (text[i] == '-' || text[i] == '*') && isSpace(text[i + 1])) {
    out.block = Block::Bullet;
    out.indent = 1;
    size_t b = i + 2;
    while (b < len && isSpace(text[b])) ++b;
    out.bodyStart = static_cast<uint16_t>(b);
    return;
  }

  // "1. " / "12) "
  size_t d = i;
  while (d < len && text[d] >= '0' && text[d] <= '9') ++d;
  if (d > i && d + 1 < len && (text[d] == '.' || text[d] == ')') && isSpace(text[d + 1])) {
    out.block = Block::Numbered;
    out.indent = 1;
    size_t b = d + 2;
    while (b < len && isSpace(text[b])) ++b;
    out.bodyStart = static_cast<uint16_t>(b);
    return;
  }

  out.block = Block::Paragraph;
  out.bodyStart = static_cast<uint16_t>(i);
}

namespace detail {
inline void push(Line& out, size_t start, size_t end, Style style) {
  if (end <= start) return;
  if (out.spanCount >= sizeof(out.spans) / sizeof(out.spans[0])) return;
  out.spans[out.spanCount++] = Span{static_cast<uint16_t>(start), static_cast<uint16_t>(end - start), style};
}
}  // namespace detail

// Split the body into styled spans. Markers are consumed (not emitted), so the
// rendered line shows "bold" rather than "**bold**".
//
// Unclosed markers stay literal: "a ** b" renders as typed, which matters in an
// EDITOR where the user is mid-keystroke and half a marker is the normal state.
inline void inlineSpans(const char* text, size_t len, Line& out) {
  const size_t bodyLen = len;
  size_t plain = 0;  // start of the current unstyled run
  size_t i = 0;

  auto closerAt = [&](size_t from, const char* marker, size_t mlen) -> size_t {
    for (size_t j = from; j + mlen <= bodyLen; ++j) {
      bool hit = true;
      for (size_t k = 0; k < mlen; ++k) {
        if (text[j + k] != marker[k]) {
          hit = false;
          break;
        }
      }
      if (hit) return j;
    }
    return bodyLen;  // no closer
  };

  while (i < bodyLen) {
    const char c = text[i];
    size_t mlen = 0;
    Style style = Style::Regular;
    const char* marker = nullptr;

    if (c == '*' && i + 1 < bodyLen && text[i + 1] == '*') {
      marker = "**";
      mlen = 2;
      style = Style::Bold;
    } else if (c == '*' || c == '_') {
      marker = c == '*' ? "*" : "_";
      mlen = 1;
      style = Style::Italic;
    } else if (c == '`') {
      marker = "`";
      mlen = 1;
      style = Style::Code;
    }

    if (mlen == 0) {
      ++i;
      continue;
    }

    const size_t close = closerAt(i + mlen, marker, mlen);
    if (close >= bodyLen || close == i + mlen) {
      // No closer, or an empty pair like "**" — leave it literal.
      ++i;
      continue;
    }

    detail::push(out, plain, i, Style::Regular);
    detail::push(out, i + mlen, close, style);
    i = close + mlen;
    plain = i;
  }
  detail::push(out, plain, bodyLen, Style::Regular);
}

// Full analysis of one line: block kind, body offset, styled spans (whose
// offsets are relative to bodyStart).
inline Line analyze(const char* text, size_t len) {
  Line out;
  classify(text, len, out);
  if (out.block == Block::CodeFence) return out;
  inlineSpans(text + out.bodyStart, len - out.bodyStart, out);
  return out;
}

// Headings render bold; everything else takes its span style.
inline bool blockIsBold(Block b) { return b == Block::Heading1 || b == Block::Heading2 || b == Block::Heading3; }

}  // namespace mdspans
