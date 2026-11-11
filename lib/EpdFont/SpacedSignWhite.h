#pragma once

// SPACED SIGNS: equal white on both sides of a spaced math sign.
//
// A sum set with word spaces -- "1 + 1", "a = b" -- is three words, and the
// white the reader sees on each side of the sign is
//
//   right side-bearing of the glyph before the space
//     + the space's advance (and its flanking kerns)
//     + left side-bearing of the glyph after it.
//
// The space is the same on both sides, but the two neighbors differ, so the
// sign sits off-center whenever the glyph before it and the glyph after it
// have different bearings. A font cannot fix that: a kern pairs two glyphs,
// and across a word space each kern sees only one of them. The layout sees
// both words, so the rule lives there (ParsedText's word-space helper, which
// asks GfxRenderer::getSpacedSignGap).
//
// The rule: a space whose neighbor is a LONE SIGN -- a word that is exactly
// one sign codepoint -- is set so that the visible white (bearing + space +
// bearing, measured to the columns the reader actually draws) equals ONE
// target, taken from the sign's own style: the white the font sets between two
// figures across an ordinary space, "0 0": rsb('0') + space('0','0') +
// lsb('0'). Both gaps of a sign share that target, so its two sides are equal
// by construction, whatever the neighbors and their styles are.
//
// A sign is ruled on BOTH sides or on NEITHER (decide below): when its far
// side is glued (a no-break space, an attached token, no space at all) or any
// of its neighbors is a superscript/subscript (drawn at half size), neither
// gap is touched, so the rule can never make a sum one-sided.
//
// "Drawn columns", not the glyph's box: the converter keeps FreeType's bitmap
// box but quantizes coverage under 64/255 to level 0, so a box can carry edge
// columns that paint nothing (measured: up to 2 px on a 12 pt sign). The
// bearings are taken from the first and last columns holding a nonzero pixel,
// decoded the way drawText decodes them (drawnColumns below), on the tier
// actually drawn -- the hi-res companion at a render scale above 1.

#include <cstdint>

namespace spacedsign {

// The signs the rule applies to. Deliberately NOT here: the hyphen-minus and
// the en/em dashes (spaced, they are dashes in prose far more often than
// minus signs), the slash (prose "and / or"), the asterisk, the tilde and the
// middle dot (all common outside mathematics).
inline bool isSign(const uint32_t cp) {
  switch (cp) {
    case 0x002B:  // + PLUS SIGN
    case 0x003C:  // < LESS-THAN SIGN
    case 0x003D:  // = EQUALS SIGN
    case 0x003E:  // > GREATER-THAN SIGN
    case 0x00B1:  // ± PLUS-MINUS SIGN
    case 0x00D7:  // × MULTIPLICATION SIGN
    case 0x00F7:  // ÷ DIVISION SIGN
    case 0x2212:  // − MINUS SIGN
    case 0x2213:  // ∓ MINUS-OR-PLUS SIGN
    case 0x2243:  // ≃ ASYMPTOTICALLY EQUAL TO
    case 0x2245:  // ≅ APPROXIMATELY EQUAL TO
    case 0x2248:  // ≈ ALMOST EQUAL TO
    case 0x2260:  // ≠ NOT EQUAL TO
    case 0x2261:  // ≡ IDENTICAL TO
    case 0x2264:  // ≤ LESS-THAN OR EQUAL TO
    case 0x2265:  // ≥ GREATER-THAN OR EQUAL TO
      return true;
    default:
      return false;
  }
}

// Decode one UTF-8 codepoint at *p and advance; 0 at the end of the string.
// Malformed bytes decode as themselves, one byte each: the result only ever
// feeds isSign, which no malformed byte satisfies.
inline uint32_t nextCodepoint(const unsigned char*& p) {
  const unsigned char c = *p;
  if (c == 0) return 0;
  int extra = 0;
  uint32_t cp = c;
  if (c >= 0xF0 && c < 0xF8) {
    extra = 3;
    cp = c & 0x07;
  } else if (c >= 0xE0) {
    extra = 2;
    cp = c & 0x0F;
  } else if (c >= 0xC0) {
    extra = 1;
    cp = c & 0x1F;
  }
  ++p;
  for (int i = 0; i < extra; i++) {
    if ((*p & 0xC0) != 0x80) return c;
    cp = (cp << 6) | (*p & 0x3F);
    ++p;
  }
  return cp;
}

// A word that is exactly one sign codepoint: "+", "=", "≤". Not "C++",
// "A+", "18+", "<div>", "->", "!=", "-=" -- a sign inside or beside other
// characters is prose or code, not a spaced sum.
inline bool isLoneSign(const char* word) {
  if (!word) return false;
  const auto* p = reinterpret_cast<const unsigned char*>(word);
  const uint32_t cp = nextCodepoint(p);
  return cp != 0 && *p == 0 && isSign(cp);
}

// The sign's far side: the token on the other side of a lone sign from the gap
// being set. `exists` false at a paragraph edge (nothing to balance against);
// `spaced` true when an ordinary word space separates it from the sign.
struct FarSide {
  bool exists = false;
  bool spaced = false;
  bool supSub = false;
};

enum class Sign : uint8_t { None, Left, Right };

// Which word of the gap (left, right) is the ruled sign, or None. A sign is
// ruled only when both of its gaps can be: its far side must be absent or an
// ordinary space to a full-size word, and neither word of this gap may be a
// superscript or subscript. When both words are lone signs ("= −"), the right
// one owns the gap; both far sides must qualify.
inline Sign decide(const char* leftWord, const bool leftSupSub, const char* rightWord, const bool rightSupSub,
                   const FarSide& beforeLeft, const FarSide& afterRight) {
  const bool leftSign = isLoneSign(leftWord);
  const bool rightSign = isLoneSign(rightWord);
  if (!leftSign && !rightSign) return Sign::None;
  if (leftSupSub || rightSupSub) return Sign::None;
  auto farOk = [](const FarSide& f) { return !f.exists || (f.spaced && !f.supSub); };
  if (leftSign && !farOk(beforeLeft)) return Sign::None;
  if (rightSign && !farOk(afterRight)) return Sign::None;
  return rightSign ? Sign::Right : Sign::Left;
}

// The figure whose white across a space is the target.
constexpr uint32_t kTargetFigure = '0';

// First and last columns of a glyph bitmap holding a pixel that draws, in the
// packing drawText reads: row-major, continuous across rows, 2 bits per pixel
// high bits first (level 0 = paper), or 1 bit per pixel MSB first. False for a
// bitmap with no drawn pixel (a blank).
inline bool drawnColumns(const uint8_t* bitmap, const int width, const int height, const bool is2Bit, int& first,
                         int& last) {
  first = width;
  last = -1;
  if (!bitmap) return false;
  int pos = 0;
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++, pos++) {
      const bool drawn = is2Bit ? ((bitmap[pos >> 2] >> ((3 - (pos & 3)) * 2)) & 0x3) != 0
                                : ((bitmap[pos >> 3] >> (7 - (pos & 7))) & 0x1) != 0;
      if (drawn) {
        if (x < first) first = x;
        if (x > last) last = x;
      }
    }
  }
  return last >= 0;
}

// One glyph as drawn, in DEVICE pixels: the cursor step (the logical advance
// times the render scale, since the cursor moves on the logical grid), where
// the drawn ink starts (left side-bearing) and how wide it is.
struct GlyphBox {
  int advancePx = 0;
  int leftPx = 0;
  int widthPx = 0;
  int rightBearingPx() const { return advancePx - leftPx - widthPx; }
};

// The target white in device pixels: what the font sets between two figures
// across a space (the space given in device pixels too).
inline int targetWhitePx(const GlyphBox& figure, const int figureSpacePx) {
  return figure.rightBearingPx() + figureSpacePx + figure.leftPx;
}

// The LOGICAL space that brings rsb(left) + space * scale + lsb(right) to the
// device-pixel target, rounded to the nearest logical pixel (so at a render
// scale of 2 each side lands within one device pixel of the target), floored
// at zero: a negative space would let the next word's box start inside the
// previous one, which the line breaker's arithmetic does not expect.
inline int spaceForWhite(const GlyphBox& left, const GlyphBox& right, const int targetPx, const int scale = 1) {
  const int devSpace = targetPx - left.rightBearingPx() - right.leftPx;
  if (devSpace <= 0) return 0;
  return (devSpace + scale / 2) / scale;
}

}  // namespace spacedsign
