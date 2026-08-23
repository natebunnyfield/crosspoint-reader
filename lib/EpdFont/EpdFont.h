#pragma once
#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;

 public:
  const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;

  /// Where the returned glyph came from. Direct means the face has a shape for
  /// the codepoint that was asked for; the other two mean it does not and
  /// getGlyph substituted, which is exactly the thing that used to be invisible
  /// (sweep item #38 -- a '?' substituted into prose reads as a question mark
  /// the author typed).
  enum class GlyphSource : uint8_t { Direct, Replacement, Fallback };

  /// `source`, when given, says whether this is the codepoint's own glyph or a
  /// substitute. The default keeps every existing caller unchanged: a caller
  /// that only needs metrics does not care which it got, and the metrics are
  /// the same either way by construction.
  const EpdGlyph* getGlyph(uint32_t cp, GlyphSource* source = nullptr) const;

  /// Returns true if this font covers `cp`: either via its in-RAM interval
  /// table or, for SD card fonts, via the coverageHandler that consults the
  /// full RAM-resident coverage index. Unlike getGlyph(), it never performs
  /// storage I/O and never falls back to the replacement glyph — it reports
  /// only what this font can render. Used by the CJK UI font fallback to
  /// decide whether a string needs to be routed to another font.
  bool hasCodepoint(uint32_t cp) const;

  /// Returns the kerning adjustment (4.4 fixed-point in pixels) between two codepoints.
  /// Returns 0 if no kerning data exists for the pair.
  int8_t getKerning(uint32_t leftCp, uint32_t rightCp) const;

  /// Returns the ligature codepoint for a pair, or 0 if no ligature exists.
  uint32_t getLigature(uint32_t leftCp, uint32_t rightCp) const;

  /// Greedily applies ligature substitutions starting from cp, consuming
  /// as many following codepoints from text as possible. Returns the
  /// (possibly substituted) codepoint; advances text past consumed chars.
  uint32_t applyLigatures(uint32_t cp, const char*& text) const;
};
