#include "CssParser.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
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

  bool push_back(char c) {
    if (len >= CAPACITY) return false;
    data[len++] = c;
    return true;
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

// Flat rule-store caps. The index is 12KB at MAX_RULES, selector text is
// bounded to 32KB, and deduplicated style bodies are bounded to about 26KB.
constexpr size_t MAX_RULES = 1500;
constexpr size_t SELECTOR_POOL_CAP = 32 * 1024;
constexpr size_t MAX_UNIQUE_STYLES = 256;

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

constexpr std::array STYLE_LENGTH_FIELDS = {
    &CssStyle::textIndent,   &CssStyle::marginTop,   &CssStyle::marginBottom,  &CssStyle::marginLeft,
    &CssStyle::marginRight,  &CssStyle::paddingTop,  &CssStyle::paddingBottom, &CssStyle::paddingLeft,
    &CssStyle::paddingRight, &CssStyle::imageHeight, &CssStyle::imageWidth,
};
constexpr size_t STYLE_LENGTH_FIELD_COUNT = STYLE_LENGTH_FIELDS.size();
constexpr size_t STYLE_WIRE_BYTES =
    5 + STYLE_LENGTH_FIELD_COUNT * (sizeof(decltype(CssLength::value)) + 1) + 2 + sizeof(uint32_t);
constexpr uint32_t CSS_DEFINED_BITS_MASK = (1u << 18) - 1;

void encodeStyleWire(const CssStyle& style, uint8_t (&out)[STYLE_WIRE_BYTES]) {
  size_t offset = 0;
  out[offset++] = static_cast<uint8_t>(style.textAlign);
  out[offset++] = static_cast<uint8_t>(style.fontStyle);
  out[offset++] = static_cast<uint8_t>(style.fontWeight);
  out[offset++] = static_cast<uint8_t>(style.textDecoration);
  out[offset++] = static_cast<uint8_t>(style.direction);

  const auto putLength = [&out, &offset](const CssLength& length) {
    memcpy(out + offset, &length.value, sizeof(length.value));
    offset += sizeof(length.value);
    out[offset++] = static_cast<uint8_t>(length.unit);
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    putLength(style.*field);
  }
  out[offset++] = static_cast<uint8_t>(style.display);
  out[offset++] = static_cast<uint8_t>(style.verticalAlign);

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
  memcpy(out + offset, &definedBits, sizeof(definedBits));
}

bool decodeStyleWire(const uint8_t (&in)[STYLE_WIRE_BYTES], CssStyle& style) {
  size_t offset = 0;
  const uint8_t textAlign = in[offset++];
  const uint8_t fontStyle = in[offset++];
  const uint8_t fontWeight = in[offset++];
  const uint8_t textDecoration = in[offset++];
  const uint8_t direction = in[offset++];
  if (textAlign > static_cast<uint8_t>(CssTextAlign::None) || fontStyle > static_cast<uint8_t>(CssFontStyle::Italic) ||
      fontWeight > static_cast<uint8_t>(CssFontWeight::Bold) || (textDecoration & ~CSS_TEXT_DECORATION_MASK) != 0 ||
      direction > static_cast<uint8_t>(CssTextDirection::Rtl)) {
    return false;
  }
  style.textAlign = static_cast<CssTextAlign>(textAlign);
  style.fontStyle = static_cast<CssFontStyle>(fontStyle);
  style.fontWeight = static_cast<CssFontWeight>(fontWeight);
  style.textDecoration = static_cast<CssTextDecoration>(textDecoration);
  style.direction = static_cast<CssTextDirection>(direction);

  const auto getLength = [&in, &offset](CssLength& length) {
    decltype(CssLength::value) value = 0;
    memcpy(&value, in + offset, sizeof(value));
    offset += sizeof(length.value);
    const uint8_t unit = in[offset++];
    if (!std::isfinite(value) || unit > static_cast<uint8_t>(CssUnit::Percent)) return false;
    length.value = value;
    length.unit = static_cast<CssUnit>(unit);
    return true;
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    if (!getLength(style.*field)) return false;
  }

  const uint8_t display = in[offset++];
  const uint8_t verticalAlign = in[offset++];
  if (display > static_cast<uint8_t>(CssDisplay::None) || verticalAlign > static_cast<uint8_t>(CssVerticalAlign::Sub)) {
    return false;
  }
  style.display = static_cast<CssDisplay>(display);
  style.verticalAlign = static_cast<CssVerticalAlign>(verticalAlign);

  uint32_t definedBits = 0;
  memcpy(&definedBits, in + offset, sizeof(definedBits));
  if ((definedBits & ~CSS_DEFINED_BITS_MASK) != 0) return false;
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
  return true;
}

}  // anonymous namespace

int CssParser::compareEntryToPieces(const SelectorEntry& entry, const std::string_view p0, const std::string_view p1,
                                    const std::string_view p2) const {
  const char* stored = selectorPool_.get() + entry.offset;
  const std::string_view pieces[] = {p0, p1, p2};
  size_t index = 0;
  for (const std::string_view piece : pieces) {
    for (const char c : piece) {
      if (index == entry.length) return -1;
      const auto storedByte = static_cast<unsigned char>(stored[index]);
      const auto probeByte = static_cast<unsigned char>(asciiToLower(c));
      if (storedByte != probeByte) return storedByte < probeByte ? -1 : 1;
      ++index;
    }
  }
  return index == entry.length ? 0 : 1;
}

size_t CssParser::lowerBound(const std::string_view p0, const std::string_view p1, const std::string_view p2,
                             bool& exact) const {
  size_t low = 0;
  size_t high = entryCount_;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (compareEntryToPieces(entries_[middle], p0, p1, p2) < 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  exact = low < entryCount_ && compareEntryToPieces(entries_[low], p0, p1, p2) == 0;
  return low;
}

const CssStyle* CssParser::findStyle(const std::string_view p0, const std::string_view p1,
                                     const std::string_view p2) const {
  bool exact = false;
  const size_t index = lowerBound(p0, p1, p2, exact);
  return exact ? &stylePool_[entries_[index].styleIndex] : nullptr;
}

std::string_view CssParser::selectorAt(const size_t index) const {
  const SelectorEntry& entry = entries_[index];
  return {selectorPool_.get() + entry.offset, entry.length};
}

CssParser::PoolResult CssParser::ensureEntryCapacity(const size_t needed) {
  if (needed <= entryCapacity_) return PoolResult::Ready;
  if (needed > MAX_RULES) return PoolResult::Limit;

  size_t capacity = entryCapacity_ ? entryCapacity_ * 2u : 128u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_RULES);
  auto grown = makeUniqueNoThrow<SelectorEntry[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: selector index (%zu entries)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (entryCount_ > 0) memcpy(grown.get(), entries_.get(), entryCount_ * sizeof(SelectorEntry));
  entries_ = std::move(grown);
  entryCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureSelectorPoolCapacity(const size_t needed) {
  if (needed <= selectorPoolCapacity_) return PoolResult::Ready;
  if (needed > SELECTOR_POOL_CAP) return PoolResult::Limit;

  size_t capacity = selectorPoolCapacity_ ? selectorPoolCapacity_ * 2u : 4096u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, SELECTOR_POOL_CAP);
  auto grown = makeUniqueNoThrow<char[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: selector pool (%zu bytes)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (selectorPoolSize_ > 0) memcpy(grown.get(), selectorPool_.get(), selectorPoolSize_);
  selectorPool_ = std::move(grown);
  selectorPoolCapacity_ = static_cast<uint32_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureStyleCapacity(const size_t needed) {
  if (needed <= styleCapacity_) return PoolResult::Ready;
  if (needed > MAX_UNIQUE_STYLES) return PoolResult::Limit;

  size_t capacity = styleCapacity_ ? styleCapacity_ * 2u : 16u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_UNIQUE_STYLES);
  auto grownStyles = makeUniqueNoThrow<CssStyle[]>(capacity);
  if (!grownStyles) {
    LOG_ERR("CSS", "OOM: style pool (%zu styles)", capacity);
    return PoolResult::OutOfMemory;
  }
  for (size_t i = 0; i < styleCount_; ++i) grownStyles[i] = stylePool_[i];
  stylePool_ = std::move(grownStyles);
  styleCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::internStyle(const CssStyle& style, uint16_t& indexOut) {
  uint8_t wire[STYLE_WIRE_BYTES];
  encodeStyleWire(style, wire);
  for (uint16_t i = 0; i < styleCount_; ++i) {
    uint8_t existingWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[i], existingWire);
    if (memcmp(existingWire, wire, STYLE_WIRE_BYTES) == 0) {
      indexOut = i;
      return PoolResult::Ready;
    }
  }

  const PoolResult capacityResult = ensureStyleCapacity(static_cast<size_t>(styleCount_) + 1);
  if (capacityResult != PoolResult::Ready) return capacityResult;
  stylePool_[styleCount_] = style;
  indexOut = styleCount_++;
  return PoolResult::Ready;
}

CssParser::RuleInsertResult CssParser::insertOrMerge(const std::string_view selector, const CssStyle& style) {
  bool exact = false;
  const size_t position = lowerBound(selector, {}, {}, exact);
  if (exact) {
    const uint16_t currentStyleIndex = entries_[position].styleIndex;
    CssStyle merged = stylePool_[currentStyleIndex];
    merged.applyOver(style);

    bool styleIsShared = false;
    for (uint16_t i = 0; i < entryCount_; ++i) {
      if (i != position && entries_[i].styleIndex == currentStyleIndex) {
        styleIsShared = true;
        break;
      }
    }
    if (!styleIsShared) {
      stylePool_[currentStyleIndex] = merged;
      return RuleInsertResult::Merged;
    }

    uint16_t styleIndex = 0;
    const PoolResult result = internStyle(merged, styleIndex);
    if (result == PoolResult::Limit) return RuleInsertResult::Limit;
    if (result == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    entries_[position].styleIndex = styleIndex;
    return RuleInsertResult::Merged;
  }

  const PoolResult entryResult = ensureEntryCapacity(static_cast<size_t>(entryCount_) + 1);
  if (entryResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (entryResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const size_t requiredSelectorBytes = static_cast<size_t>(selectorPoolSize_) + selector.size();
  const PoolResult selectorResult = ensureSelectorPoolCapacity(requiredSelectorBytes);
  if (selectorResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (selectorResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  uint16_t styleIndex = 0;
  const PoolResult styleResult = internStyle(style, styleIndex);
  if (styleResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (styleResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const uint32_t selectorOffset = selectorPoolSize_;
  char* destination = selectorPool_.get() + selectorOffset;
  for (const char c : selector) *destination++ = asciiToLower(c);
  selectorPoolSize_ = static_cast<uint32_t>(requiredSelectorBytes);

  SelectorEntry* entries = entries_.get();
  memmove(entries + position + 1, entries + position, (entryCount_ - position) * sizeof(SelectorEntry));
  entries[position] = {selectorOffset, styleIndex, static_cast<uint16_t>(selector.size())};
  ++entryCount_;
  return RuleInsertResult::Inserted;
}

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

  // Heap floor: the store's growth is fallible now (upstream b6746c1df), but a
  // floor still keeps stylesheet rules from taking the heap the render path
  // needs. Stopping growth (rather than the whole stylesheet) keeps the cascade
  // running for rules already stored. Partial styling beats a reboot.
  if (!ruleGrowthStopped_ && ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_CSS_RULES) {
    LOG_ERR("CSS", "Low heap during CSS parse (%u bytes), stopping growth at %u rules", ESP.getFreeHeap(), entryCount_);
    ruleGrowthStopped_ = true;
    booknotes::current().raise(booknotes::Note::StylesheetSkipped);
  }

  // Walk comma-separated selectors in place. The bounded store reports every
  // capacity or allocation failure without crossing a throwing STL boundary.
  forEachDelimitedToken(
      selectorGroup, [](char c) { return c == ','; },
      [&](std::string_view sel) {
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

        if (ruleGrowthStopped_) {
          // Continue the cascade for stored selectors without retrying failed
          // allocations for new rules.
          bool exact = false;
          const size_t matchingIndex = lowerBound(sel, {}, {}, exact);
          if (!exact || matchingIndex >= entryCount_) return;
        }
        const RuleInsertResult result = insertOrMerge(sel, style);
        if (result == RuleInsertResult::Limit) {
          LOG_ERR("CSS", "CSS rule store limit reached at %u rules", entryCount_);
          ruleGrowthStopped_ = true;
          booknotes::current().raise(booknotes::Note::StylesheetSkipped);
        } else if (result == RuleInsertResult::OutOfMemory) {
          LOG_ERR("CSS", "OOM while growing CSS rule store at %u rules", entryCount_);
          ruleGrowthStopped_ = true;
          booknotes::current().raise(booknotes::Note::StylesheetSkipped);
        }
      });
}

// Main parsing entry point

CssParser::ParseResult CssParser::loadFromStream(HalFile& source) {
  if (!source) {
    LOG_ERR("CSS", "Cannot read from invalid file");
    return ParseResult::Error;
  }

  // Parsing buffers live in one transient heap block (see ParseScratch above);
  // value-initialized, so every StackBuffer starts with len == 0 exactly as the
  // former locals did.
  auto scratch = makeUniqueNoThrow<ParseScratch>();
  if (!scratch) {
    LOG_ERR("CSS", "OOM: %zu bytes for parse buffers", sizeof(ParseScratch));
    return ParseResult::Error;
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
  bool selectorTruncated = false;
  bool declarationTruncated = false;
  bool inputTruncated = false;
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
        skippingRule = selectorTruncated || selector.size() > MAX_SELECTOR_LENGTH * 4;
        if (skippingRule) booknotes::current().raise(booknotes::Note::StylesheetSkipped);
        return;
      }
      if (!selector.push_back(c)) {
        selectorTruncated = true;
        inputTruncated = true;
      }
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
        if (!skippingRule && !declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        if (!skippingRule) {
          processRuleBlockWithStyle(selector, currentStyle);
        }
        selector.clear();
        declBuffer.clear();
        skippingRule = false;
        selectorTruncated = false;
        declarationTruncated = false;
        return;
      }
      return;
    }
    if (bodyDepth > 1) {
      return;
    }
    if (!skippingRule) {
      if (c == ';') {
        if (!declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        declBuffer.clear();
        declarationTruncated = false;
      } else {
        if (!declBuffer.push_back(c)) {
          declarationTruncated = true;
          inputTruncated = true;
        }
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

  if (inputTruncated) {
    LOG_ERR("CSS", "CSS input exceeded parser buffer; cache will remain partial");
  }
  const bool incompleteInput = bodyDepth > 0 || inAtRule || inComment || !selector.empty();
  LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", ruleCount(), totalRead);
  return ruleGrowthStopped_ || inputTruncated || incompleteInput ? ParseResult::Partial : ParseResult::Complete;
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

  // 1. Apply element-level style (lowest priority).
  if (const CssStyle* style = findStyle(tagName)) {
    result.applyOver(*style);
  }

  if (classAttr.empty()) return result;

  // TODO: Support combinations of classes (e.g. style on .class1.class2)
  // 2. Apply class styles (medium priority).
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
    if (const CssStyle* style = findStyle(".", cls)) {
      result.applyOver(*style);
    }
  });

  // TODO: Support combinations of classes (e.g. style on p.class1.class2)
  // 3. Apply element.class styles (higher priority).
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
    if (const CssStyle* style = findStyle(tagName, ".", cls)) {
      result.applyOver(*style);
    }
  });

  return result;
}

// Inline style parsing (static - doesn't need rule database)

CssStyle CssParser::parseInlineStyle(std::string_view styleValue) { return parseDeclarations(styleValue); }

// Cache serialization

// Cache file name (version is CssParser::CSS_CACHE_VERSION)
constexpr char rulesCache[] = "/css_rules.cache";
constexpr char rulesCacheTmp[] = "/css_rules.cache.tmp";
constexpr char rulesCacheBackup[] = "/css_rules.cache.bak";
constexpr uint8_t CSS_CACHE_FLAG_PARTIAL = 1 << 0;
constexpr uint8_t CSS_CACHE_KNOWN_FLAGS = CSS_CACHE_FLAG_PARTIAL;

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

bool CssParser::restoreCacheBackupIfNeeded() const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  if (Storage.exists(finalPath.c_str())) {
    return true;
  }

  const std::string backupPath = cachePath + rulesCacheBackup;
  if (!Storage.exists(backupPath.c_str())) {
    return false;
  }

  if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to restore CSS cache backup");
    return false;
  }

  LOG_DBG("CSS", "Restored CSS cache backup after interrupted replacement");
  return true;
}

void CssParser::deleteCache() const {
  // NOT gated on hasCache(): that answers "is there a CURRENT cache", and a
  // delete asked for by a caller has to remove a stale one too -- otherwise the
  // metadata path's deleteCache() would leave behind exactly the file whose
  // staleness sent it down that branch. The temp and backup files of an
  // interrupted replacement (upstream b6746c1df) go with it.
  if (cachePath.empty()) return;
  for (const char* suffix : {rulesCache, rulesCacheTmp, rulesCacheBackup}) {
    const std::string path = cachePath + suffix;
    if (Storage.exists(path.c_str())) Storage.remove(path.c_str());
  }
}

CssParser::CacheStatus CssParser::inspectCache() const {
  if (cachePath.empty() || (!hasCache() && !restoreCacheBackupIfNeeded())) {
    return CacheStatus::Missing;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    return CacheStatus::Invalid;
  }

  uint8_t version = 0;
  uint8_t flags = 0;
  uint16_t ruleCount = 0;
  if (file.read(&version, sizeof(version)) != sizeof(version) || version != CSS_CACHE_VERSION ||
      file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0 ||
      file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount) || ruleCount > MAX_RULES) {
    return CacheStatus::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  if (!partial) {
    // Complete caches are fully validated while hydrating, avoiding a second
    // payload scan on every EPUB open.
    return CacheStatus::Complete;
  }

  const auto skipBytes = [&file](const size_t byteCount) {
    return static_cast<size_t>(file.available()) >= byteCount && file.seekCur(byteCount);
  };
  size_t selectorBytes = 0;
  for (uint16_t i = 0; i < ruleCount; ++i) {
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen) || selectorLen == 0 ||
        selectorLen > MAX_SELECTOR_LENGTH) {
      return CacheStatus::Invalid;
    }
    selectorBytes += selectorLen;
    if (selectorBytes > SELECTOR_POOL_CAP || !skipBytes(static_cast<size_t>(selectorLen) + STYLE_WIRE_BYTES)) {
      return CacheStatus::Invalid;
    }
  }

  if (file.available() != 0) {
    return CacheStatus::Invalid;
  }

  return CacheStatus::Partial;
}

bool CssParser::saveToCache(const bool complete) const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  const std::string tmpPath = cachePath + rulesCacheTmp;
  const std::string backupPath = cachePath + rulesCacheBackup;

  Storage.remove(tmpPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("CSS", tmpPath, file)) {
    return false;
  }

  bool writeOk = true;
  const auto writeBytes = [&file, &writeOk](const void* data, const size_t size) {
    if (writeOk && size > 0 && file.write(data, size) != size) {
      writeOk = false;
    }
  };
  const auto writeByte = [&writeBytes](const uint8_t value) { writeBytes(&value, sizeof(value)); };

  writeByte(CssParser::CSS_CACHE_VERSION);

  // A partial cache can style the current low-memory session, but the next
  // EPUB load must retry the source stylesheets instead of trusting it.
  writeByte(complete ? 0 : CSS_CACHE_FLAG_PARTIAL);

  // Write rule count
  const uint16_t ruleCount = entryCount_;
  writeBytes(&ruleCount, sizeof(ruleCount));

  // Write each rule: selector string + CssStyle fields
  for (uint16_t i = 0; i < entryCount_; ++i) {
    const std::string_view selector = selectorAt(i);
    // Write selector string (length-prefixed)
    const auto selectorLen = static_cast<uint16_t>(selector.size());
    writeBytes(&selectorLen, sizeof(selectorLen));
    writeBytes(selector.data(), selectorLen);

    uint8_t styleWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[entries_[i].styleIndex], styleWire);
    writeBytes(styleWire, sizeof(styleWire));
    if (!writeOk) break;
  }

  if (!writeOk || !file.close()) {
    LOG_ERR("CSS", "Failed to write temporary CSS cache");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  const bool hadExistingCache = Storage.exists(finalPath.c_str());
  if (hadExistingCache) {
    Storage.remove(backupPath.c_str());
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      LOG_ERR("CSS", "Failed to back up existing CSS cache");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to promote temporary CSS cache");
    Storage.remove(tmpPath.c_str());
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      LOG_ERR("CSS", "Failed to restore previous CSS cache");
    }
    return false;
  }

  Storage.remove(backupPath.c_str());

  LOG_DBG("CSS", "Saved %u rules to %s cache", ruleCount, complete ? "complete" : "partial");
  return true;
}

CssParser::CacheLoadResult CssParser::loadFromCache() {
  if (cachePath.empty()) {
    return CacheLoadResult::Invalid;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    if (!restoreCacheBackupIfNeeded() || !Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
      return CacheLoadResult::Invalid;
    }
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
    return CacheLoadResult::Invalid;
  }

  uint8_t flags = 0;
  if (file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0) {
    LOG_DBG("CSS", "Invalid CSS cache flags: %u", flags);
    return CacheLoadResult::Invalid;
  }

  // Read rule count
  uint16_t ruleCount = 0;
  if (file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount)) {
    return CacheLoadResult::Invalid;
  }

  if (ruleCount > MAX_RULES) {
    LOG_DBG("CSS", "Invalid cache rule count (%u > %zu)", ruleCount, MAX_RULES);
    clear();
    return CacheLoadResult::Invalid;
  }

  const PoolResult entryCapacityResult = ensureEntryCapacity(ruleCount);
  if (entryCapacityResult == PoolResult::OutOfMemory) {
    clear();
    return CacheLoadResult::LowMemory;
  }
  if (entryCapacityResult == PoolResult::Limit) {
    clear();
    return CacheLoadResult::Invalid;
  }

  auto selectorBuffer = ruleCount > 0 ? makeUniqueNoThrow<char[]>(MAX_SELECTOR_LENGTH) : nullptr;
  if (ruleCount > 0 && !selectorBuffer) {
    clear();
    return CacheLoadResult::LowMemory;
  }

  // Read each rule
  for (uint16_t i = 0; i < ruleCount; ++i) {
    // The cache was written by a device that had the heap for these rules; the
    // one reading it may not. Stop early and keep what loaded rather than
    // aborting on the insert -- a partially styled chapter still renders.
    if (ESP.getFreeHeap() < MIN_FREE_HEAP_FOR_CSS_RULES) {
      LOG_ERR("CSS", "Low heap loading CSS cache (%u bytes), stopping with %u of %u rules", ESP.getFreeHeap(),
              entryCount_, ruleCount);
      // What loaded is usable (every insert keeps the store sorted), and the
      // cache file is untouched for a later, roomier open.
      return CacheLoadResult::Complete;
    }

    // Read selector string
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    if (selectorLen == 0 || selectorLen > MAX_SELECTOR_LENGTH) {
      LOG_DBG("CSS", "Invalid selector length in cache: %u", selectorLen);
      clear();
      return CacheLoadResult::Invalid;
    }

    if (file.read(selectorBuffer.get(), selectorLen) != selectorLen) {
      clear();
      return CacheLoadResult::Invalid;
    }

    uint8_t styleWire[STYLE_WIRE_BYTES];
    if (file.read(styleWire, sizeof(styleWire)) != sizeof(styleWire)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    CssStyle style;
    if (!decodeStyleWire(styleWire, style)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    const RuleInsertResult insertResult = insertOrMerge(std::string_view(selectorBuffer.get(), selectorLen), style);
    if (insertResult == RuleInsertResult::OutOfMemory) {
      clear();
      return CacheLoadResult::LowMemory;
    }
    if (insertResult == RuleInsertResult::Limit) {
      clear();
      return CacheLoadResult::Invalid;
    }
    if (insertResult == RuleInsertResult::Merged) {
      LOG_DBG("CSS", "Duplicate selector in CSS cache");
      clear();
      return CacheLoadResult::Invalid;
    }
  }

  if (file.available() != 0) {
    clear();
    return CacheLoadResult::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  LOG_DBG("CSS", "Loaded %u rules from %s cache", ruleCount, partial ? "partial" : "complete");
  return CacheLoadResult::Complete;
}
