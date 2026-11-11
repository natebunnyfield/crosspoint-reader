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
// sign sits off-center whenever the figure before it and the sign itself have
// different bearings (a "1" has wide ones, a round figure narrow ones). A font
// cannot fix that: a kern pairs two glyphs, and across a word space each kern
// sees only one of them. getSpaceAdvance is handed BOTH neighbors, so the rule
// lives there.
//
// The rule: when either neighbor of a space is a sign (isSign below), the
// space is set so that the visible white (bearing + space + bearing, measured
// to the columns the reader actually draws) equals ONE target, the same on every side
// of every sign. The target is the white the font itself sets between two
// figures across an ordinary space -- "0 0": rsb('0') + space('0','0') +
// lsb('0'). A sign then stands in the gap a figure-to-figure space would
// leave, which keeps a sum about as wide as the font made it while removing
// the lean. Because both sides of a sign get the same target, the two sides
// are equal by construction, whichever neighbors they have.
//
// "Drawn columns", not the glyph's box: the converter keeps FreeType's bitmap
// box but quantizes coverage under 64/255 to level 0, so a box can carry edge
// columns that paint nothing (measured: up to 2 px on a 12 pt sign). The
// bearings here are taken from the first and last columns holding a nonzero
// pixel, decoded the way drawText decodes them (drawnColumns below).
//
// Everything here is pixels: the layout positions words at whole pixels and
// glyph boxes are whole pixels, so the white this computes is the white drawn
// (before justification adds to the gap; see the caller).

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

// One glyph's box in whole pixels: its snapped advance, where its drawn ink
// starts (left side-bearing) and how wide the drawn ink is.
struct GlyphBox {
  int advancePx = 0;
  int leftPx = 0;
  int widthPx = 0;
  int rightBearingPx() const { return advancePx - leftPx - widthPx; }
};

// The target white: what the font sets between two figures across a space.
inline int targetWhitePx(const GlyphBox& figure, const int figureSpacePx) {
  return figure.rightBearingPx() + figureSpacePx + figure.leftPx;
}

// Does this space take the rule? Only a space with a sign on at least one side.
inline bool applies(const uint32_t leftCp, const uint32_t rightCp) { return isSign(leftCp) || isSign(rightCp); }

// The space, in pixels, that makes  rsb(left) + space + lsb(right) == target.
// Floored at zero: a negative space would let the next word's box start inside
// the previous one, which the line breaker's arithmetic does not expect.
inline int spaceForWhitePx(const GlyphBox& left, const GlyphBox& right, const int targetPx) {
  const int space = targetPx - left.rightBearingPx() - right.leftPx;
  return space < 0 ? 0 : space;
}

}  // namespace spacedsign
