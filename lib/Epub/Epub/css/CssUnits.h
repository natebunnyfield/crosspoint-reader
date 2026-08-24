#pragma once

#include <cstdint>
#include <string_view>

// What a CSS length's unit is worth in panel pixels.
//
// This exists because every unit the parser did not recognize used to fall
// through to PIXELS. `margin: 1cm` became one pixel: not a simplification of
// the publisher's intent but the opposite of it, and silent. A unit that cannot
// be converted honestly now has to say so, and the caller drops the declaration
// rather than inventing a number for it.
//
// THE BASIS IS 150 DPI, and it is not a guess about the glass.
//
// Three numbers were candidates. The CSS reference pixel's 96 dpi is what a
// browser uses and what this firmware would get by accident. The X3 panel's own
// resolution is ~257 ppi (792 x 528 over a 3.7" diagonal,
// docs/hardware-dimensions.md), and the X4's is ~219. Neither is the right
// anchor here, because this firmware ALREADY has a physical-to-pixel ratio and
// has had one since the first font was built: every reading face, built-in and
// on the card, is rasterized by lib/EpdFont/scripts/fontconvert.py and
// fontconvert_sdcard.py at `set_char_size(pt << 6, pt << 6, 150, 150)` -- 150
// dpi, stated in both scripts' own comments as `ppem = pt * 150 / 72`. The
// built-in 18 pt Libre Franklin's advanceY of 45 px is 1.2 x that ppem, which
// is what confirms it from the shipped data rather than from the build script.
//
// So a point already means 150/72 = 2.083 px in this renderer, and anchoring
// lengths anywhere else would make a book's `margin: 12pt` a different physical
// size from the 12 pt type it sits beside -- at the panel's true 257 ppi that
// margin would stand 2.4x the height of the type. One renderer, one point.
//
// TWO HONEST LIMITS ON THAT ARGUMENT, both found by review and neither a reason
// to pick a different number:
//   * A BOOK cannot set 12 pt type here -- `font-size` is read and discarded
//     (`parseDeclarationIntoStyle` has no branch for it), so the type size is
//     the reader's setting. The parity is between a book's `pt` and the type
//     the reader is actually looking at, which is still the relationship that
//     decides the number.
//   * `em` does NOT resolve against the em box. `BlockStyle::fromCssStyle` is
//     handed the font's advanceY -- 45 px at 18 pt, against a 37.5 px em -- so
//     `margin: 18pt` and `margin: 1em` differ by 20% in this renderer. That is
//     an old inaccuracy in what `emSize` means, not in this table; correcting
//     it moves every em-based margin in every book and is its own change.
//
// This also retires the old fixed `pt` x1.33, which was the 96 dpi answer
// arrived at without the question being asked (survey item #24,
// docs/book-notes-and-sparse-ruby-2026-08-23.md).
namespace cssunits {

// Pixels per inch. Both font converters rasterize at this; see above.
inline constexpr float kPixelsPerInch = 150.0f;

enum class Kind : uint8_t {
  Pixels,         // no unit at all, or `px`
  Em,             // font-relative, resolved by the caller against the current size
  Rem,            // treated as Em: there is no root font size separate from the reading size
  Percent,        // resolved by the caller against a container width
  Absolute,       // cm mm Q in pt pc -- `pixelsPerUnit` carries the whole conversion
  Unconvertible,  // ex ch vw vh vmin vmax, and anything not a CSS unit at all
};

struct Classified {
  Kind kind = Kind::Pixels;
  // Meaningful only for Kind::Absolute. One of `unit` is this many pixels.
  float pixelsPerUnit = 1.0f;
};

namespace detail {
constexpr char lowerAscii(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

constexpr bool unitIs(const std::string_view unit, const char* literal) {
  size_t i = 0;
  for (; literal[i] != '\0'; ++i) {
    if (i >= unit.size() || lowerAscii(unit[i]) != literal[i]) return false;
  }
  return i == unit.size();
}
}  // namespace detail

// Is this even a unit? A CSS unit is ASCII letters, or the lone percent sign.
// The scan that produces `unit` stops at the first non-numeric character, so an
// invalid value like `margin-top: 10px 20px` or `1cm/2` hands the whole
// remainder over -- and calling THAT an unconvertible unit would print
// `px 20px` into a notice a person reads. Such a value is not a length and
// never has been; it takes the same road as `auto`.
constexpr bool isUnitToken(const std::string_view unit) {
  if (unit == "%") return true;
  for (const char c : unit) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
  }
  return true;  // includes the empty view, which means "no unit": pixels
}

// Classify the unit text that followed a CSS number. `unit` is the raw
// remainder of the value token, already trimmed; an empty view means the number
// carried no unit, which CSS only allows for zero but which books write anyway
// and which this reader has always read as pixels.
//
// The viewport units are deliberately in the Unconvertible set rather than
// missing from it: they are convertible in principle, but resolving `vh` needs
// a viewport HEIGHT that CssLength::toPixels is not given, and inventing one
// from the container width is the same class of mistake this header exists to
// stop. `ex` and `ch` need the reading face's x-height and zero-advance, which
// are not reachable from here either -- and the em size the caller does supply
// is the font's LINE height, so deriving an x-height from it would compound one
// approximation with another.
constexpr Classified classify(const std::string_view unit) {
  using detail::unitIs;
  if (unit.empty() || unitIs(unit, "px")) return {Kind::Pixels, 1.0f};
  if (unitIs(unit, "em")) return {Kind::Em, 1.0f};
  if (unitIs(unit, "rem")) return {Kind::Rem, 1.0f};
  if (unit == "%") return {Kind::Percent, 1.0f};
  // Absolute lengths, all defined against the inch (CSS Values 4, section 6.2).
  if (unitIs(unit, "in")) return {Kind::Absolute, kPixelsPerInch};
  if (unitIs(unit, "cm")) return {Kind::Absolute, kPixelsPerInch / 2.54f};
  if (unitIs(unit, "mm")) return {Kind::Absolute, kPixelsPerInch / 25.4f};
  if (unitIs(unit, "q")) return {Kind::Absolute, kPixelsPerInch / 101.6f};
  if (unitIs(unit, "pt")) return {Kind::Absolute, kPixelsPerInch / 72.0f};
  if (unitIs(unit, "pc")) return {Kind::Absolute, kPixelsPerInch / 6.0f};
  return {Kind::Unconvertible, 1.0f};
}

}  // namespace cssunits
