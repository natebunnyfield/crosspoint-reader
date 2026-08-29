#include "CssParser.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "CssUnits.h"
#include "Epub/BookNotes.h"

namespace {

// Fixed-capacity string buffer to avoid heap reallocations during parsing
// Provides string-like interface with fixed capacity
struct StackBuffer {
  static constexpr size_t CAPACITY = 1024;
  char data[CAPACITY];
  size_t len = 0;

  void push_back(char c) {
    if (len < CAPACITY - 1) {
      data[len++] = c;
    }
  }

  void clear() { len = 0; }
  bool empty() const { return len == 0; }
  size_t size() const { return len; }

  // Get string view of current content (zero-copy)
  std::string_view view() const { return std::string_view(data, len); }
  operator std::string_view() const noexcept { return view(); }
};

// Buffer size for reading CSS files
constexpr size_t READ_BUFFER_SIZE = 512;

// loadFromStream's three parsing buffers, bundled so a single heap allocation
// covers all of them. As locals they measured a 2800-byte frame with
// -fstack-usage — the largest non-vendor frame in the firmware — and stylesheet
// parsing runs on the render task, whose stack is 8192 bytes. One allocation per
// stylesheet is negligible; the point of the fixed capacities is still to avoid
// per-token reallocation while parsing.
struct ParseScratch {
  StackBuffer selector;
  StackBuffer declBuffer;
  char readBuffer[READ_BUFFER_SIZE];
};

// Maximum number of CSS rules to store in the selector map
// Prevents unbounded memory growth from pathological CSS files
constexpr size_t MAX_RULES = 1500;

// A rule COUNT cap bounds how many rules exist; it says nothing about whether
// the heap can hold the next one. Under -fno-exceptions the map's allocation
// aborts the device rather than failing, so registration also needs a heap
// FLOOR -- the two guards answer different questions and a book can trip the
// second while nowhere near the first. Ported from zrn-ns/crosspoint-jp's
// issue #103 fix (f59d0fa0f), which is the same class as this fork's B-030 and
// B-031: turn an abort into degraded output. Partial styling beats a reboot.
constexpr size_t MIN_FREE_HEAP_FOR_CSS_RULES = 32 * 1024;

// Minimum free heap required to apply CSS during rendering
// If below this threshold, we skip CSS to avoid display artifacts.
constexpr size_t MIN_FREE_HEAP_FOR_CSS = 48 * 1024;

// Maximum length for a single selector string
// Prevents parsing of extremely long or malformed selectors
constexpr size_t MAX_SELECTOR_LENGTH = 256;

// Check if character is CSS whitespace
constexpr bool isCssWhitespace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

constexpr std::string_view trimCssWhitespace(std::string_view s) {
  while (!s.empty() && isCssWhitespace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isCssWhitespace(s.back())) s.remove_suffix(1);
  return s;
}

constexpr char asciiToLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Case-insensitive equality on ASCII. lowercaseKeyword MUST already be
// lowercase; CSS keywords are ASCII by spec so byte-wise tolower is safe.
constexpr bool iequalsAscii(std::string_view value, std::string_view lowercaseKeyword) {
  return std::equal(value.begin(), value.end(), lowercaseKeyword.begin(), lowercaseKeyword.end(),
                    [](char a, char b) { return asciiToLower(a) == b; });
}

// Walk s and invoke fn(token) for each non-empty run between delimiters.
// Tokens are boundary-trimmed and yielded as string_views into s; no
// allocation. Runs of consecutive delimiters coalesce — no empty tokens are
// emitted. `isDelimiter` is invoked once per character.
template <typename Pred, typename F>
void forEachDelimitedToken(std::string_view s, Pred isDelimiter, F&& fn) {
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || isDelimiter(s[i])) {
      const std::string_view trimmed = trimCssWhitespace(s.substr(start, i - start));
      if (!trimmed.empty()) {
        fn(trimmed);
      }
      start = i + 1;
    }
  }
}

// FNV-1a per Fowler/Noll/Vo, sized to match size_t on the target. The firmware
// runs on a 32-bit core where size_t is 32 bits, so naively using the 64-bit
// constants would silently truncate FNV_PRIME to a non-prime and wreck hash
// distribution. The selection below picks the canonical 32- or 64-bit
// constants at compile time so the same source works in a 64-bit host
// simulator. `fnv1aMix` is the per-byte mix step; callers apply any
// byte-level transform (e.g. asciiToLower) first.
static_assert(sizeof(size_t) == 4 || sizeof(size_t) == 8, "FNV constants are only defined for 32- or 64-bit size_t");
constexpr size_t FNV_OFFSET_BASIS =
    sizeof(size_t) == 8 ? static_cast<size_t>(14695981039346656037ULL) : static_cast<size_t>(2166136261U);
constexpr size_t FNV_PRIME =
    sizeof(size_t) == 8 ? static_cast<size_t>(1099511628211ULL) : static_cast<size_t>(16777619U);

constexpr size_t fnv1aMix(size_t hash, unsigned char byte) { return (hash ^ byte) * FNV_PRIME; }

// Parse the entirety of s as a number into `out`. Accepts an optional leading
// '+' (which std::from_chars rejects by spec) so callers can pass CSS-style
// signed numbers without manual trimming. Returns false on empty input, a
// non-numeric suffix, or any from_chars error.
template <typename T>
bool tryParseNumber(std::string_view s, T& out) {
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  if (begin < end && *begin == '+') ++begin;
  if (begin >= end) return false;
  if constexpr (std::is_floating_point_v<T>) {
    // The FLOATING-POINT std::from_chars overloads live in libc++'s dylib and
    // Apple marks them introduced in iOS 26.0, so using them here silently
    // raised the whole app's minimum OS. strtof/strtod are C89 and available
    // everywhere, including the ESP32 build, and give the same all-or-nothing
    // parse once the end pointer is checked. The integral overloads below are
    // header-only and carry no such gate.
    //
    // strtod needs a NUL terminator, which a string_view does not promise. CSS
    // numeric tokens are short; anything longer than this buffer is not a number
    // we could represent anyway, so it is rejected rather than truncated (a
    // truncated parse would silently succeed on the wrong value).
    char buf[64];
    const size_t n = static_cast<size_t>(end - begin);
    if (n >= sizeof(buf)) return false;
    memcpy(buf, begin, n);
    buf[n] = '\0';
    char* parseEnd = nullptr;
    errno = 0;
    const double v = std::strtod(buf, &parseEnd);
    if (parseEnd != buf + n || errno == ERANGE) return false;
    out = static_cast<T>(v);
    return true;
  } else {
    const auto r = std::from_chars(begin, end, out);
    return r.ec == std::errc{} && r.ptr == end;
  }
}

// Collect up to 4 whitespace-separated tokens for a CSS edge-value shorthand
// (margin, padding, and the border-* family). Returns the number of tokens
// written; extras are silently dropped. Callers apply the 1/2/3/4-value
// fallback rule using the returned count.
size_t collectEdgeValueTokens(std::string_view s, std::string_view (&out)[4]) {
  size_t count = 0;
  forEachDelimitedToken(s, isCssWhitespace, [&](std::string_view tok) {
    if (count < 4) out[count++] = tok;
  });
  return count;
}

std::string_view stripTrailingImportant(std::string_view value) {
  // The bang and the keyword are two separate tokens in the CSS grammar, so
  // whitespace between them is legal and `! important` means exactly what
  // `!important` means. This used to match the fused spelling as one literal,
  // and the spaced one then survived into the value. Measured on 2026-08-24,
  // both silently and on a page that still rendered: a length reached the unit
  // scan as "cm ! important", which is not a unit TOKEN at all, so parseLength
  // answered NotALength and acceptLength turned that into a defined ZERO --
  // `margin-top: 1cm ! important` came out 0 px against the 59 the book asked
  // for, and not even the unsupported-unit note fired. A keyword fell through
  // to its unmatched default, which for alignment is Left rather than neutral,
  // so a centered heading was FORCED left. Both are the failure modes the fused
  // spelling's own comments (parseLength, interpretAlignment) already describe;
  // the spelling was the only difference.
  //
  // Comments are the other thing the grammar allows between the two tokens
  // (`! /*sic*/ important`). Not handled, because this parser strips comments
  // NOWHERE -- a `/*...*/` anywhere in a declaration already survives into the
  // value -- so handling them here alone would be a lone exception rather than
  // a fix.
  constexpr std::string_view KEYWORD = "important";

  const auto trimTrailingWhitespace = [](std::string_view& v) {
    while (!v.empty() && isCssWhitespace(v.back())) {
      v.remove_suffix(1);
    }
  };

  trimTrailingWhitespace(value);

  if (value.size() < KEYWORD.size() + 1) {  // + 1 for the bang itself
    return value;
  }

  const size_t keywordPos = value.size() - KEYWORD.size();
  if (!iequalsAscii(value.substr(keywordPos), KEYWORD)) {
    return value;
  }

  // Everything before the keyword, with any separating whitespace taken off.
  // The bang has to be what is left touching it -- so "notimportant" and
  // "1em important" are values, not annotations.
  std::string_view head = value.substr(0, keywordPos);
  trimTrailingWhitespace(head);
  if (head.empty() || head.back() != '!') {
    return value;
  }

  head.remove_suffix(1);
  trimTrailingWhitespace(head);
  return head;
}

}  // anonymous namespace

// Transparent case-insensitive hash/equal. Bodies live here (rather than
// inline in the header) so they can share the anonymous-namespace asciiToLower
// with the other ASCII helpers in this translation unit.

size_t CssParser::SvHash::operator()(std::string_view sv) const noexcept {
  size_t h = FNV_OFFSET_BASIS;
  for (char c : sv) h = fnv1aMix(h, asciiToLower(c));
  return h;
}

size_t CssParser::SvHash::operator()(const std::string& s) const noexcept { return operator()(std::string_view(s)); }

size_t CssParser::SvHash::operator()(CompositeKey k) const noexcept {
  // Hash the case-folded concatenation of every piece without materializing
  // it — the running hash continues across pieces as if they were one buffer.
  size_t h = FNV_OFFSET_BASIS;
  for (std::string_view piece : k.pieces) {
    for (char c : piece) h = fnv1aMix(h, asciiToLower(c));
  }
  return h;
}

bool CssParser::SvEqual::operator()(std::string_view a, std::string_view b) const noexcept {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (asciiToLower(a[i]) != asciiToLower(b[i])) return false;
  }
  return true;
}

bool CssParser::SvEqual::operator()(const std::string& a, std::string_view b) const noexcept {
  return operator()(std::string_view(a), b);
}

bool CssParser::SvEqual::operator()(std::string_view a, const std::string& b) const noexcept {
  return operator()(a, std::string_view(b));
}

bool CssParser::SvEqual::operator()(const std::string& a, const std::string& b) const noexcept {
  return operator()(std::string_view(a), std::string_view(b));
}

bool CssParser::SvEqual::operator()(CompositeKey k, std::string_view sv) const noexcept {
  size_t total = 0;
  for (std::string_view piece : k.pieces) total += piece.size();
  if (total != sv.size()) return false;
  size_t i = 0;
  for (std::string_view piece : k.pieces) {
    for (char c : piece) {
      if (asciiToLower(c) != asciiToLower(sv[i++])) return false;
    }
  }
  return true;
}

bool CssParser::SvEqual::operator()(std::string_view sv, CompositeKey k) const noexcept { return operator()(k, sv); }

// Property value interpreters

CssTextAlign CssParser::interpretAlignment(std::string_view val) {
  // `!important` off first, here as on the length paths. `display` and
  // `direction` already did this; alignment did not, and its default is not
  // neutral -- an unmatched value returns Left below and the caller then sets
  // `defined.textAlign`, so `text-align: center !important` FORCED LEFT. On a
  // heading, which is the one block whose CSS alignment is honored, that is
  // visible on the page.
  val = trimCssWhitespace(stripTrailingImportant(val));

  if (iequalsAscii(val, "left") || iequalsAscii(val, "start")) return CssTextAlign::Left;
  if (iequalsAscii(val, "right") || iequalsAscii(val, "end")) return CssTextAlign::Right;
  if (iequalsAscii(val, "center")) return CssTextAlign::Center;
  if (iequalsAscii(val, "justify")) return CssTextAlign::Justify;

  return CssTextAlign::Left;
}

CssFontStyle CssParser::interpretFontStyle(std::string_view val) {
  val = trimCssWhitespace(stripTrailingImportant(val));

  if (iequalsAscii(val, "italic") || iequalsAscii(val, "oblique")) return CssFontStyle::Italic;
  return CssFontStyle::Normal;
}

CssFontWeight CssParser::interpretFontWeight(std::string_view val) {
  val = trimCssWhitespace(stripTrailingImportant(val));

  // Named values
  if (iequalsAscii(val, "bold") || iequalsAscii(val, "bolder")) return CssFontWeight::Bold;
  if (iequalsAscii(val, "normal") || iequalsAscii(val, "lighter")) return CssFontWeight::Normal;

  // Numeric values: 100-900
  // CSS spec: 400 = normal, 700 = bold
  // We use: 0-400 = normal, 700+ = bold, 500-600 = normal (conservative)
  long numericWeight = 0;
  if (tryParseNumber(val, numericWeight)) {
    return numericWeight >= 700 ? CssFontWeight::Bold : CssFontWeight::Normal;
  }
  return CssFontWeight::Normal;
}

CssTextDecoration CssParser::interpretDecoration(std::string_view val) {
  // text-decoration can have multiple space-separated values. Compare whole tokens
  // so malformed values like "notunderline" do not accidentally enable a line.
  CssTextDecoration result = CssTextDecoration::None;
  bool explicitNone = false;
  forEachDelimitedToken(stripTrailingImportant(val), isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "none")) {
      explicitNone = true;
    } else if (iequalsAscii(token, "underline")) {
      result = result | CssTextDecoration::Underline;
    } else if (iequalsAscii(token, "line-through")) {
      result = result | CssTextDecoration::LineThrough;
    }
  });
  return explicitNone ? CssTextDecoration::None : result;
}

CssParser::LengthParse CssParser::parseLength(std::string_view val, CssLength& out, std::string_view& unitOut) {
  unitOut = {};
  // `!important` comes off FIRST. It used to reach the unit scan glued to the
  // unit ("1cm !important"), which matched nothing and so read as pixels; with
  // an unrecognized unit now dropping the declaration, leaving it on would fire
  // the book note on most styled books and name a unit that does not exist.
  val = trimCssWhitespace(stripTrailingImportant(val));
  if (val.empty()) {
    out = CssLength{};
    return LengthParse::NotALength;
  }

  size_t unitStart = val.size();
  for (size_t i = 0; i < val.size(); ++i) {
    const char c = val[i];
    if (!std::isdigit(c) && c != '.' && c != '-' && c != '+') {
      unitStart = i;
      break;
    }
  }

  float numericValue;
  if (!tryParseNumber(val.substr(0, unitStart), numericValue)) {
    out = CssLength{};
    return LengthParse::NotALength;  // No number parsed (e.g. auto, inherit, initial)
  }

  const std::string_view unitPart = val.substr(unitStart);
  if (!cssunits::isUnitToken(unitPart)) {
    out = CssLength{};
    return LengthParse::NotALength;
  }
  const cssunits::Classified unit = cssunits::classify(unitPart);
  switch (unit.kind) {
    case cssunits::Kind::Pixels:
      out = CssLength{numericValue, CssUnit::Pixels};
      return LengthParse::Ok;
    case cssunits::Kind::Em:
      out = CssLength{numericValue, CssUnit::Em};
      return LengthParse::Ok;
    case cssunits::Kind::Rem:
      out = CssLength{numericValue, CssUnit::Rem};
      return LengthParse::Ok;
    case cssunits::Kind::Percent:
      out = CssLength{numericValue, CssUnit::Percent};
      return LengthParse::Ok;
    case cssunits::Kind::Absolute:
      // Converted here rather than carried, so nothing downstream needs a
      // physical basis and CssLength keeps the three units it can resolve on
      // its own.
      out = CssLength{numericValue * unit.pixelsPerUnit, CssUnit::Pixels};
      return LengthParse::Ok;
    case cssunits::Kind::Unconvertible:
      break;
  }
  // A unit with no honest conversion. Do NOT touch `out`: the caller must leave
  // the property exactly as the cascade left it, which is what a browser does
  // with an invalid declaration and is the only answer that does not put a
  // number on the page that the publisher never asked for.
  unitOut = unitPart;
  return LengthParse::UnsupportedUnit;
}

bool CssParser::tryInterpretLength(std::string_view val, CssLength& out) {
  std::string_view unit;
  const LengthParse result = parseLength(val, out, unit);
  if (result == LengthParse::UnsupportedUnit) {
    booknotes::current().raiseUnsupportedCssUnit(unit);
  }
  return result == LengthParse::Ok;
}

// The 1-to-4 value edge shorthand, expanded to top/right/bottom/left.
//
// One unconvertible component drops the WHOLE declaration, which is the CSS
// rule for an invalid value in a shorthand and is also the only answer that
// cannot leave a book with three sides of a margin. Returns false when there
// was nothing to apply.
bool CssParser::acceptEdgeShorthand(std::string_view val, CssLength (&out)[4]) {
  std::string_view tokens[4];
  const size_t count = collectEdgeValueTokens(stripTrailingImportant(val), tokens);
  if (count == 0) return false;

  CssLength parsed[4];
  for (size_t i = 0; i < count; ++i) {
    std::string_view unit;
    if (parseLength(tokens[i], parsed[i], unit) == LengthParse::UnsupportedUnit) {
      booknotes::current().raiseUnsupportedCssUnit(unit);
      return false;
    }
  }
  out[0] = parsed[0];
  out[1] = count >= 2 ? parsed[1] : parsed[0];
  out[2] = count >= 3 ? parsed[2] : parsed[0];
  out[3] = count >= 4 ? parsed[3] : out[1];
  return true;
}

bool CssParser::acceptLength(std::string_view val, CssLength& out) {
  std::string_view unit;
  const LengthParse result = parseLength(val, out, unit);
  if (result == LengthParse::UnsupportedUnit) {
    booknotes::current().raiseUnsupportedCssUnit(unit);
    return false;
  }
  // NotALength keeps the long-standing behavior: `margin: auto` has always
  // resolved to zero here, and turning it into an inherited margin is a
  // different change from this one.
  return true;
}

// Declaration parsing

void CssParser::parseDeclarationIntoStyle(std::string_view decl, CssStyle& style) {
  const size_t colonPos = decl.find(':');
  if (colonPos == std::string_view::npos || colonPos == 0) return;

  const std::string_view name = trimCssWhitespace(decl.substr(0, colonPos));
  const std::string_view value = trimCssWhitespace(decl.substr(colonPos + 1));

  if (name.empty() || value.empty()) return;

  if (iequalsAscii(name, "text-align")) {
    style.textAlign = interpretAlignment(value);
    style.defined.textAlign = 1;
  } else if (iequalsAscii(name, "font-style")) {
    style.fontStyle = interpretFontStyle(value);
    style.defined.fontStyle = 1;
  } else if (iequalsAscii(name, "font-weight")) {
    style.fontWeight = interpretFontWeight(value);
    style.defined.fontWeight = 1;
  } else if (iequalsAscii(name, "text-decoration") || iequalsAscii(name, "text-decoration-line")) {
    style.textDecoration = interpretDecoration(value);
    style.defined.textDecoration = 1;
  } else if (iequalsAscii(name, "text-indent")) {
    if (acceptLength(value, style.textIndent)) style.defined.textIndent = 1;
  } else if (iequalsAscii(name, "margin-top")) {
    if (acceptLength(value, style.marginTop)) style.defined.marginTop = 1;
  } else if (iequalsAscii(name, "margin-bottom")) {
    if (acceptLength(value, style.marginBottom)) style.defined.marginBottom = 1;
  } else if (iequalsAscii(name, "margin-left")) {
    if (acceptLength(value, style.marginLeft)) style.defined.marginLeft = 1;
  } else if (iequalsAscii(name, "margin-right")) {
    if (acceptLength(value, style.marginRight)) style.defined.marginRight = 1;
  } else if (iequalsAscii(name, "margin")) {
    CssLength edges[4];
    if (acceptEdgeShorthand(value, edges)) {
      style.marginTop = edges[0];
      style.marginRight = edges[1];
      style.marginBottom = edges[2];
      style.marginLeft = edges[3];
      style.defined.marginTop = style.defined.marginRight = style.defined.marginBottom = style.defined.marginLeft = 1;
    }
  } else if (iequalsAscii(name, "padding-top")) {
    if (acceptLength(value, style.paddingTop)) style.defined.paddingTop = 1;
  } else if (iequalsAscii(name, "padding-bottom")) {
    if (acceptLength(value, style.paddingBottom)) style.defined.paddingBottom = 1;
  } else if (iequalsAscii(name, "padding-left")) {
    if (acceptLength(value, style.paddingLeft)) style.defined.paddingLeft = 1;
  } else if (iequalsAscii(name, "padding-right")) {
    if (acceptLength(value, style.paddingRight)) style.defined.paddingRight = 1;
  } else if (iequalsAscii(name, "padding")) {
    CssLength edges[4];
    if (acceptEdgeShorthand(value, edges)) {
      style.paddingTop = edges[0];
      style.paddingRight = edges[1];
      style.paddingBottom = edges[2];
      style.paddingLeft = edges[3];
      style.defined.paddingTop = style.defined.paddingRight = style.defined.paddingBottom = style.defined.paddingLeft =
          1;
    }
  } else if (iequalsAscii(name, "height")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      style.imageHeight = len;
      style.defined.imageHeight = 1;
    }
  } else if (iequalsAscii(name, "width")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      style.imageWidth = len;
      style.defined.imageWidth = 1;
    }
  } else if (iequalsAscii(name, "display")) {
    const std::string_view displayValue = stripTrailingImportant(value);
    style.display = iequalsAscii(displayValue, "none") ? CssDisplay::None : CssDisplay::Block;
    style.defined.display = 1;
  } else if (iequalsAscii(name, "direction")) {
    const std::string_view directionValue = stripTrailingImportant(value);
    if (iequalsAscii(directionValue, "rtl")) {
      style.direction = CssTextDirection::Rtl;
      style.defined.direction = 1;
    } else if (iequalsAscii(directionValue, "ltr")) {
      style.direction = CssTextDirection::Ltr;
      style.defined.direction = 1;
    }
  } else if (iequalsAscii(name, "vertical-align")) {
    const std::string_view alignValue = stripTrailingImportant(value);
    if (iequalsAscii(alignValue, "super")) {
      style.verticalAlign = CssVerticalAlign::Super;
      style.defined.verticalAlign = 1;
    } else if (iequalsAscii(alignValue, "sub")) {
      style.verticalAlign = CssVerticalAlign::Sub;
      style.defined.verticalAlign = 1;
    }
  } else if (iequalsAscii(name, "writing-mode")) {
    // Parsed for the note ONLY -- there is no vertical layout in this firmware
    // and no CssStyle field to hold one. A vertical-rl book still renders
    // horizontally left-to-right, which for a tategaki novel is not a
    // degradation but a different book, so the reader has to be told.
    const std::string_view mode = stripTrailingImportant(value);
    if (mode.size() >= 8 && iequalsAscii(mode.substr(0, 8), "vertical")) {
      booknotes::current().raise(booknotes::Note::VerticalWritingIgnored);
    }
  }
}

CssStyle CssParser::parseDeclarations(std::string_view declBlock) {
  CssStyle style;

  size_t start = 0;
  for (size_t i = 0; i <= declBlock.size(); ++i) {
    if (i == declBlock.size() || declBlock[i] == ';') {
      if (i > start) {
        parseDeclarationIntoStyle(declBlock.substr(start, i - start), style);
      }
      start = i + 1;
    }
  }

  return style;
}

// Rule processing

void CssParser::processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style) {
  // Skip rules that don't define any supported properties to save RAM.
  if (!style.defined.anySet()) {
    return;
  }

  // Check if we've reached the rule limit before processing
  if (rulesBySelector_.size() >= MAX_RULES) {
    LOG_DBG("CSS", "Reached max rules limit (%zu), stopping CSS parsing", MAX_RULES);
    booknotes::current().raise(booknotes::Note::StylesheetSkipped);
    return;
  }

  // ...and whether there is heap left to hold another one.
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_CSS_RULES) {
    LOG_ERR("CSS", "Low heap during CSS parse (%u bytes), stopping with %zu rules", ESP.getFreeHeap(),
            rulesBySelector_.size());
    booknotes::current().raise(booknotes::Note::StylesheetSkipped);
    return;
  }

  // Walk comma-separated selectors in place — no vector allocation. Selectors
  // with unsupported syntax (combinators, attributes, pseudo, etc.) are skipped
  // silently; the only heap allocation per kept selector is the std::string
  // map key, which is unavoidable since the map owns its keys.
  bool limitReached = false;
  forEachDelimitedToken(
      selectorGroup, [](char c) { return c == ','; },
      [&](std::string_view sel) {
        if (limitReached) return;

        if (sel.size() > MAX_SELECTOR_LENGTH) {
          LOG_DBG("CSS", "Selector too long (%zu > %zu), skipping", sel.size(), MAX_SELECTOR_LENGTH);
          return;
        }

        // TODO: Support richer CSS selector syntax in the future. For now we only
        // handle `tag`, `.class`, or `tag.class`. Reject anything containing a
        // character that introduces unsupported syntax:
        //   '+'  adjacent sibling combinator
        //   '>'  child combinator
        //   '['  attribute selector
        //   ':'  pseudo class/element
        //   '#'  ID selector
        //   '~'  general sibling combinator
        //   '*'  wildcard
        //   ' '  descendant combinator
        // Single-pass scan via find_first_of instead of eight sequential find() calls.
        constexpr std::string_view kUnsupportedSelectorChars = "+>[:#~* ";
        if (sel.find_first_of(kUnsupportedSelectorChars) != std::string_view::npos) {
          // Nearly every real stylesheet has at least one of these, so this is
          // the note that most often explains "the book does not look like the
          // publisher's proof". Counted, because one dropped descendant
          // selector and four hundred of them are different books.
          booknotes::current().countDroppedCssRules(1);
          return;
        }

        // Skip if this would exceed the rule limit
        if (rulesBySelector_.size() >= MAX_RULES) {
          LOG_DBG("CSS", "Reached max rules limit, stopping selector processing");
          booknotes::current().raise(booknotes::Note::StylesheetSkipped);
          limitReached = true;
          return;
        }

        // Store or merge with existing. Hash/equal are case-insensitive, so two
        // selectors that differ only in ASCII case collide on insert and merge.
        auto it = rulesBySelector_.find(sel);
        if (it != rulesBySelector_.end()) {
          it->second.applyOver(style);
        } else {
          rulesBySelector_.emplace(std::string(sel), style);
        }
      });
}

// Main parsing entry point

bool CssParser::loadFromStream(HalFile& source) {
  if (!source) {
    LOG_ERR("CSS", "Cannot read from invalid file");
    return false;
  }

  // Parsing buffers live in one transient heap block (see ParseScratch above);
  // value-initialized, so every StackBuffer starts with len == 0 exactly as the
  // former locals did.
  auto scratch = makeUniqueNoThrow<ParseScratch>();
  if (!scratch) {
    LOG_ERR("CSS", "OOM: %zu bytes for parse buffers", sizeof(ParseScratch));
    return false;
  }

  size_t totalRead = 0;

  // Fixed-capacity buffers, no per-token reallocation
  StackBuffer& selector = scratch->selector;
  StackBuffer& declBuffer = scratch->declBuffer;

  bool inComment = false;
  bool maybeSlash = false;
  bool prevStar = false;

  bool inAtRule = false;
  int atDepth = 0;

  int bodyDepth = 0;
  bool skippingRule = false;
  CssStyle currentStyle;

  auto handleChar = [&](const char c) {
    if (inAtRule) {
      if (c == '{') {
        ++atDepth;
      } else if (c == '}') {
        if (atDepth > 0) --atDepth;
        if (atDepth == 0) inAtRule = false;
      } else if (c == ';' && atDepth == 0) {
        inAtRule = false;
      }
      return;
    }

    if (bodyDepth == 0) {
      if (selector.empty() && isCssWhitespace(c)) {
        return;
      }
      if (c == '@' && selector.empty()) {
        // The whole at-rule body is skipped, so a sheet wrapped in @media all
        // -- which publishers do -- contributes nothing at all. @font-face is
        // the other common one, and it is why the embedded-font note exists.
        booknotes::current().raise(booknotes::Note::StylesheetPartlyUnderstood);
        inAtRule = true;
        atDepth = 0;
        return;
      }
      if (c == '{') {
        bodyDepth = 1;
        currentStyle = CssStyle{};
        declBuffer.clear();
        if (selector.size() > MAX_SELECTOR_LENGTH * 4) {
          booknotes::current().raise(booknotes::Note::StylesheetSkipped);
          skippingRule = true;
        }
        return;
      }
      selector.push_back(c);
      return;
    }

    // bodyDepth > 0
    if (c == '{') {
      ++bodyDepth;
      return;
    }
    if (c == '}') {
      --bodyDepth;
      if (bodyDepth == 0) {
        if (!skippingRule && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        if (!skippingRule) {
          processRuleBlockWithStyle(selector, currentStyle);
        }
        selector.clear();
        declBuffer.clear();
        skippingRule = false;
        return;
      }
      return;
    }
    if (bodyDepth > 1) {
      return;
    }
    if (!skippingRule) {
      if (c == ';') {
        if (!declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
          declBuffer.clear();
        }
      } else {
        declBuffer.push_back(c);
      }
    }
  };

  char* const buffer = scratch->readBuffer;
  while (source.available()) {
    int bytesRead = source.read(buffer, READ_BUFFER_SIZE);
    if (bytesRead <= 0) break;

    totalRead += static_cast<size_t>(bytesRead);

    for (int i = 0; i < bytesRead; ++i) {
      const char c = buffer[i];

      if (inComment) {
        if (prevStar && c == '/') {
          inComment = false;
          prevStar = false;
          continue;
        }
        prevStar = c == '*';
        continue;
      }

      if (maybeSlash) {
        if (c == '*') {
          inComment = true;
          maybeSlash = false;
          prevStar = false;
          continue;
        }
        handleChar('/');
        maybeSlash = false;
        // fall through to process current char
      }

      if (c == '/') {
        maybeSlash = true;
        continue;
      }

      handleChar(c);
    }
  }

  if (maybeSlash) {
    handleChar('/');
  }

  LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", rulesBySelector_.size(), totalRead);
  return true;
}

// Style resolution

CssStyle CssParser::resolveStyle(std::string_view tagName, std::string_view classAttr) const {
  static bool lowHeapWarningLogged = false;
  if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_CSS) {
    if (!lowHeapWarningLogged) {
      lowHeapWarningLogged = true;
      LOG_DBG("CSS", "Warning: low heap (%u bytes) below MIN_FREE_HEAP_FOR_CSS (%u), returning empty style",
              ESP.getFreeHeap(), static_cast<unsigned>(MIN_FREE_HEAP_FOR_CSS));
    }
    return CssStyle{};
  }

  CssStyle result;

  // 1. Apply element-level style (lowest priority). The map's hash/equal are
  // case-insensitive, so the raw tagName view can be used as the lookup key.
  if (auto it = rulesBySelector_.find(tagName); it != rulesBySelector_.end()) {
    result.applyOver(it->second);
  }

  if (classAttr.empty()) return result;

  // TODO: Support combinations of classes (e.g. style on .class1.class2)
  // 2. Apply class styles (medium priority). The transparent hash/equal accept
  // a CompositeKey, so we never materialize the concatenation.
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
    if (auto it = rulesBySelector_.find(CompositeKey{".", cls}); it != rulesBySelector_.end()) {
      result.applyOver(it->second);
    }
  });

  // TODO: Support combinations of classes (e.g. style on p.class1.class2)
  // 3. Apply element.class styles (higher priority).
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
    if (auto it = rulesBySelector_.find(CompositeKey{tagName, ".", cls}); it != rulesBySelector_.end()) {
      result.applyOver(it->second);
    }
  });

  return result;
}

// Inline style parsing (static - doesn't need rule database)

CssStyle CssParser::parseInlineStyle(std::string_view styleValue) { return parseDeclarations(styleValue); }

// Cache serialization

// Cache file name (version is CssParser::CSS_CACHE_VERSION)
constexpr char rulesCache[] = "/css_rules.cache";

// EXISTENCE IS NOT VALIDITY, and this is a GATE rather than a paragraph.
//
// This was a bare `Storage.exists()` until 2026-08-25, which made it a test of
// the wrong question in the one place it decides whether the stylesheet gets
// parsed at all. parseCssFiles() returns early when hasCache() is true, and the
// FULL RE-INDEX path (Epub.cpp, after buildBookBin) calls parseCssFiles()
// directly. It does not go through the metadata path's
// `!hasCache() || !loadFromCache()`, whose failed load deletes a stale file as
// a side effect. So a css_rules.cache written by an older CSS_CACHE_VERSION
// survived a re-index and the parse was skipped, leaving the parser with ZERO
// rules resident. Section.cpp's own loadFromCache() then rejected the version
// and returned false with only a LOG_ERR, so every section built during that
// session was laid out against an EMPTY rule set and committed as valid at the
// current section version. A whole book could be read once with none of its
// stylesheet applied -- every margin, indent and alignment gone -- and nothing
// said so louder than one log line.
//
// SCOPE, traced rather than assumed: this is one session per book, not
// permanent. The failing load deletes the stale file on its way out, so the
// NEXT open takes Epub::load's warm branch, finds no cache, reparses and wipes
// the section directory. And it needs skipLoadingCss == false, which today is
// only ReaderActivity opening a book with embedded styles on. That is still the
// common case after a firmware update that moves the book.bin and CSS cache
// versions together -- the re-index and the stale cache arrive at once.
//
// Section.cpp's version-pairing note says the CSS and section versions are kept
// in step by hand; this was a third path that neither the note nor the pairing
// covered. Reading the version byte here costs one open on a path whose only
// true answer is followed by an open anyway.
bool CssParser::hasCache() const {
  if (cachePath.empty()) return false;
  const std::string path = cachePath + rulesCache;
  // exists() first so a legitimately absent cache does not log an open failure
  // on every book that has never been parsed.
  if (!Storage.exists(path.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForRead("CSS", path, file)) return false;
  uint8_t version = 0;
  const bool current = file.read(&version, 1) == 1 && version == CssParser::CSS_CACHE_VERSION;
  file.close();
  if (!current) {
    LOG_DBG("CSS", "Cache version %u is not %u; treating as absent so it is reparsed", version,
            CssParser::CSS_CACHE_VERSION);
  }
  return current;
}

void CssParser::deleteCache() const {
  // NOT gated on hasCache(): that now answers "is there a CURRENT cache", and a
  // delete asked for by a caller has to remove a stale one too -- otherwise the
  // metadata path's deleteCache() would leave behind exactly the file whose
  // staleness sent it down that branch.
  if (cachePath.empty()) return;
  const std::string path = cachePath + rulesCache;
  if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
}

bool CssParser::saveToCache() const {
  if (cachePath.empty()) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForWrite("CSS", cachePath + rulesCache, file)) {
    return false;
  }

  // Write version
  file.write(CssParser::CSS_CACHE_VERSION);

  // Write rule count
  const auto ruleCount = static_cast<uint16_t>(rulesBySelector_.size());
  file.write(reinterpret_cast<const uint8_t*>(&ruleCount), sizeof(ruleCount));

  // Write each rule: selector string + CssStyle fields
  for (const auto& pair : rulesBySelector_) {
    // Write selector string (length-prefixed)
    const auto selectorLen = static_cast<uint16_t>(pair.first.size());
    file.write(reinterpret_cast<const uint8_t*>(&selectorLen), sizeof(selectorLen));
    file.write(reinterpret_cast<const uint8_t*>(pair.first.data()), selectorLen);

    // Write CssStyle fields (all are POD types)
    const CssStyle& style = pair.second;
    file.write(static_cast<uint8_t>(style.textAlign));
    file.write(static_cast<uint8_t>(style.fontStyle));
    file.write(static_cast<uint8_t>(style.fontWeight));
    file.write(static_cast<uint8_t>(style.textDecoration));
    file.write(static_cast<uint8_t>(style.direction));

    // Write CssLength fields (value + unit)
    auto writeLength = [&file](const CssLength& len) {
      file.write(reinterpret_cast<const uint8_t*>(&len.value), sizeof(len.value));
      file.write(static_cast<uint8_t>(len.unit));
    };

    writeLength(style.textIndent);
    writeLength(style.marginTop);
    writeLength(style.marginBottom);
    writeLength(style.marginLeft);
    writeLength(style.marginRight);
    writeLength(style.paddingTop);
    writeLength(style.paddingBottom);
    writeLength(style.paddingLeft);
    writeLength(style.paddingRight);
    writeLength(style.imageHeight);
    writeLength(style.imageWidth);
    file.write(static_cast<uint8_t>(style.display));
    file.write(static_cast<uint8_t>(style.verticalAlign));

    // Write defined flags as uint32_t
    uint32_t definedBits = 0;
    if (style.defined.textAlign) definedBits |= 1 << 0;
    if (style.defined.fontStyle) definedBits |= 1 << 1;
    if (style.defined.fontWeight) definedBits |= 1 << 2;
    if (style.defined.textDecoration) definedBits |= 1 << 3;
    if (style.defined.textIndent) definedBits |= 1 << 4;
    if (style.defined.marginTop) definedBits |= 1 << 5;
    if (style.defined.marginBottom) definedBits |= 1 << 6;
    if (style.defined.marginLeft) definedBits |= 1 << 7;
    if (style.defined.marginRight) definedBits |= 1 << 8;
    if (style.defined.paddingTop) definedBits |= 1 << 9;
    if (style.defined.paddingBottom) definedBits |= 1 << 10;
    if (style.defined.paddingLeft) definedBits |= 1 << 11;
    if (style.defined.paddingRight) definedBits |= 1 << 12;
    if (style.defined.imageHeight) definedBits |= 1 << 13;
    if (style.defined.imageWidth) definedBits |= 1 << 14;
    if (style.defined.display) definedBits |= 1 << 15;
    if (style.defined.direction) definedBits |= 1 << 16;
    if (style.defined.verticalAlign) definedBits |= 1 << 17;
    file.write(reinterpret_cast<const uint8_t*>(&definedBits), sizeof(definedBits));
  }

  LOG_DBG("CSS", "Saved %u rules to cache", ruleCount);
  return true;
}

bool CssParser::loadFromCache() {
  if (cachePath.empty()) {
    return false;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    return false;
  }

  // Clear existing rules
  clear();

  // Read and verify version
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != CssParser::CSS_CACHE_VERSION) {
    LOG_DBG("CSS", "Cache version mismatch (got %u, expected %u), removing stale cache for rebuild", version,
            CssParser::CSS_CACHE_VERSION);
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove((cachePath + rulesCache).c_str());
    return false;
  }

  // Read rule count
  uint16_t ruleCount = 0;
  if (file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount)) {
    return false;
  }

  if (ruleCount > MAX_RULES) {
    LOG_DBG("CSS", "Invalid cache rule count (%u > %zu)", ruleCount, MAX_RULES);
    rulesBySelector_.clear();
    return false;
  }

  // Size the bucket array up front to avoid incremental rehashes while loading rules.
  rulesBySelector_.reserve(ruleCount);

  auto hasRemainingBytes = [&file](const size_t neededBytes) -> bool {
    return static_cast<size_t>(file.available()) >= neededBytes;
  };

  constexpr size_t CSS_LENGTH_FIELD_COUNT = 11;
  constexpr size_t CSS_LENGTH_BYTES = sizeof(float) + sizeof(uint8_t);
  constexpr size_t CSS_FIXED_STYLE_BYTES =
      5 * sizeof(uint8_t) + (CSS_LENGTH_FIELD_COUNT * CSS_LENGTH_BYTES) + sizeof(uint8_t) + sizeof(uint32_t);

  // Read each rule
  for (uint16_t i = 0; i < ruleCount; ++i) {
    // The cache was written by a device that had the heap for these rules; the
    // one reading it may not. Stop early and keep what loaded rather than
    // aborting on the insert -- a partially styled chapter still renders.
    if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_CSS_RULES) {
      LOG_ERR("CSS", "Low heap loading CSS cache (%u bytes), stopping with %zu of %u rules", ESP.getFreeHeap(),
              rulesBySelector_.size(), ruleCount);
      return true;  // what loaded is usable; a false here would delete the cache
    }

    // Read selector string
    uint16_t selectorLen = 0;
    if (!hasRemainingBytes(sizeof(selectorLen))) {
      rulesBySelector_.clear();
      return false;
    }
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen)) {
      rulesBySelector_.clear();
      return false;
    }

    if (selectorLen == 0 || selectorLen > MAX_SELECTOR_LENGTH || !hasRemainingBytes(selectorLen)) {
      LOG_DBG("CSS", "Invalid selector length in cache: %u", selectorLen);
      rulesBySelector_.clear();
      return false;
    }

    std::string selector;
    selector.resize(selectorLen);
    if (file.read(&selector[0], selectorLen) != selectorLen) {
      rulesBySelector_.clear();
      return false;
    }

    if (!hasRemainingBytes(CSS_FIXED_STYLE_BYTES)) {
      LOG_DBG("CSS", "Truncated CSS cache while reading style payload");
      rulesBySelector_.clear();
      return false;
    }

    // Read CssStyle fields
    CssStyle style;
    uint8_t enumVal;

    if (file.read(&enumVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.textAlign = static_cast<CssTextAlign>(enumVal);

    if (file.read(&enumVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.fontStyle = static_cast<CssFontStyle>(enumVal);

    if (file.read(&enumVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.fontWeight = static_cast<CssFontWeight>(enumVal);

    if (file.read(&enumVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.textDecoration = static_cast<CssTextDecoration>(enumVal & CSS_TEXT_DECORATION_MASK);

    if (file.read(&enumVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.direction = static_cast<CssTextDirection>(enumVal);

    // Read CssLength fields
    auto readLength = [&file](CssLength& len) -> bool {
      if (file.read(&len.value, sizeof(len.value)) != sizeof(len.value)) {
        return false;
      }
      uint8_t unitVal;
      if (file.read(&unitVal, 1) != 1) {
        return false;
      }
      len.unit = static_cast<CssUnit>(unitVal);
      return true;
    };

    if (!readLength(style.textIndent) || !readLength(style.marginTop) || !readLength(style.marginBottom) ||
        !readLength(style.marginLeft) || !readLength(style.marginRight) || !readLength(style.paddingTop) ||
        !readLength(style.paddingBottom) || !readLength(style.paddingLeft) || !readLength(style.paddingRight) ||
        !readLength(style.imageHeight) || !readLength(style.imageWidth)) {
      rulesBySelector_.clear();
      return false;
    }

    // Read display value
    uint8_t displayVal;
    if (file.read(&displayVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.display = static_cast<CssDisplay>(displayVal);

    // Read verticalAlign value
    uint8_t verticalAlignVal;
    if (file.read(&verticalAlignVal, 1) != 1) {
      rulesBySelector_.clear();
      return false;
    }
    style.verticalAlign = static_cast<CssVerticalAlign>(verticalAlignVal);

    // Read defined flags
    uint32_t definedBits = 0;
    if (file.read(&definedBits, sizeof(definedBits)) != sizeof(definedBits)) {
      rulesBySelector_.clear();
      return false;
    }
    style.defined.textAlign = (definedBits & 1 << 0) != 0;
    style.defined.fontStyle = (definedBits & 1 << 1) != 0;
    style.defined.fontWeight = (definedBits & 1 << 2) != 0;
    style.defined.textDecoration = (definedBits & 1 << 3) != 0;
    style.defined.textIndent = (definedBits & 1 << 4) != 0;
    style.defined.marginTop = (definedBits & 1 << 5) != 0;
    style.defined.marginBottom = (definedBits & 1 << 6) != 0;
    style.defined.marginLeft = (definedBits & 1 << 7) != 0;
    style.defined.marginRight = (definedBits & 1 << 8) != 0;
    style.defined.paddingTop = (definedBits & 1 << 9) != 0;
    style.defined.paddingBottom = (definedBits & 1 << 10) != 0;
    style.defined.paddingLeft = (definedBits & 1 << 11) != 0;
    style.defined.paddingRight = (definedBits & 1 << 12) != 0;
    style.defined.imageHeight = (definedBits & 1 << 13) != 0;
    style.defined.imageWidth = (definedBits & 1 << 14) != 0;
    style.defined.display = (definedBits & 1 << 15) != 0;
    style.defined.direction = (definedBits & 1 << 16) != 0;
    style.defined.verticalAlign = (definedBits & 1 << 17) != 0;

    rulesBySelector_[selector] = style;
  }

  LOG_DBG("CSS", "Loaded %u rules from cache", ruleCount);
  return true;
}
